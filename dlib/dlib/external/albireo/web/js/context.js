// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Le contexte que reçoit le modèle (fork pour macOS : sorti d'app.js) : les parts du contexte de travail (documents,
// extraits, résumé, images, mémoire), l'estimation des tokens, le message système et les messages envoyés au service.
// Le commentaire d'en-tête d'app.js (« LE CONTEXTE D'UNE QUESTION ») explique pourquoi le début du prompt ne change pas.

import * as api from './api.js';
import * as images from './images.js';
import * as tools from './tools.js';
import * as web from './web.js';
import { dayText, today } from './composer.js';
import { projectById } from './lists.js';
import { backgroundContext, backgroundRates, backgroundText, loadedModel, readsImages } from './status.js';
import { imageIds } from './files.js';
import { imageDocument, knowledgeMode } from './indexing.js';
import { state } from './state.js';

/*
    Le contexte de travail d'une discussion (workingContext) : le contexte que le moteur donne au modèle, déjà ramené
    par lui à ce que la mémoire de l'ordinateur contient (sa mémoire physique et son fichier d'échange ou swap), dans la
    limite de ce que l'ordinateur lit en workingSeconds à la vitesse mesurée pour le modèle (readRate),
    minWorkingContext tokens au moins, arrondi à une puissance de deux, pour qu'une nouvelle mesure ne change les choix
    suivants que lorsque la vitesse double ou diminue de moitié.  Le moteur garde les clés et valeurs des discussions
    lues, de sorte qu'une discussion ne relit tout son contexte que dans les rares cas où elles ont quitté la mémoire.
    Les choix automatiques d'une discussion en prennent leurs parts : les documents d'un projet lus en entier, un
    document joint lu en entier, et les extraits donnés avec une question, chacun dans un temps de lecture lui aussi,
    puisqu'une question les attend ; au-delà des deux premiers, les documents sont l'objet d'une recherche.  Le contexte
    du modèle reste la limite de ce que contient une question.
*/
const workingSeconds = 240;
const minWorkingContext = 16384;
export const knowledgeShare = 0.3;
export const knowledgeSeconds = 60;
export const attachmentShare = 0.25;
export const attachmentSeconds = 40;
// Les extraits d'une question : excerptShare du contexte de travail, dans la limite de excerptSeconds de lecture, entre
// le minimum et le maximum de tokens ci-dessous ; un passage par passageTokens.
const excerptShare = 0.2;
const excerptSeconds = 20;
const leastExcerptTokens = 2000;
const mostExcerptTokens = 12000;
const passageTokens = 330;
/*
    La place des échanges est la place du contexte de travail moins le message système et les outils.  Quand les
    échanges les plus anciens partent, les échanges gardés, avec le résumé de ceux qui sont partis, prennent keptShare
    de cette place, pour qu'ils puissent croître de nouveau pendant plusieurs questions avant la coupe suivante.  Une
    fois une réponse terminée, ou une discussion ouverte, quand les échanges occupent compactShare de leur place, leur
    partie la plus ancienne est compressée en arrière-plan : les questions suivantes tiennent encore entre-temps, et
    trouvent le résumé écrit.  Une conversation qui dépasse la place du contexte du modèle perd aussitôt ses échanges
    les plus anciens (fitContext).
*/
export const keptShare = 0.3;
export const compactShare = 0.5;
// Le résumé des échanges qui ont quitté le contexte prend au plus cette part de la place, dans la limite de
// summarySeconds d'écriture par le modèle d'arrière-plan.
const summaryShare = 0.15;
const summarySeconds = 60;
/*
    Le nombre maximal d'images que contient le contexte d'une discussion, de celles de son projet qu'une question reçoit
    avec leurs descriptions, les plus pertinentes d'abord, et de celles que contient un projet.  Une requête du service
    porte 16 images au plus.
*/
export const maxContextImages = 10;
export const maxQuestionProjectImages = 4;
export const maxProjectImages = 10;
// Les éléments de la connaissance d'un projet, ses documents et ses images ensemble.
export const maxProjectItems = 100;
/*
    Les tokens de la fin du raisonnement que le moteur écrit d'emblée pour un modèle qui raisonne toujours quand la
    requête ne demande pas de raisonnement : la phrase et le marqueur qui le terminent.
*/
const closedReasoningTokens = 32;

/*
    Les tokens qu'une discussion garde libres pour la réponse du modèle de caps, avec ou sans son raisonnement : la
    réponse la plus longue des réglages, sinon de la place pour un raisonnement avant la réponse, un quart du contexte
    au plus au-delà de 2048 tokens.
*/
export function answerReserve(caps, reasoning) {
  const answer = state.settings.maxTokens || (reasoning ? 8192 : 2048);
  const context = caps.context || 8192;
  return Math.min(answer, Math.max(2048, Math.floor(context / 4)))
    + (caps.reasoningAlways && !reasoning ? closedReasoningTokens : 0);
}

/*
    Les tokens d'un texte, estimés d'après ses caractères : 3,6 caractères par token dans les alphabets latins, trois
    tokens pour quatre caractères pour les idéogrammes, les kana et le hangul, et 2,2 caractères par token pour les
    autres écritures (cyrillique, grec, arabe, hébreu, écritures indiennes).
*/
const tokenEstimates = new Map();

export function estimateTokens(text) {
  const s = String(text || '');
  // Les documents ne sont mesurés qu'une fois : leurs textes ne changent pas.
  if (s.length > 8192 && tokenEstimates.has(s)) return tokenEstimates.get(s);
  let latin = 0, wide = 0, other = 0;
  for (let i = 0; i < s.length; ++i) {
    const c = s.charCodeAt(i);
    if (c < 0x370) ++latin;
    else if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x3400 && c <= 0x9fff) || (c >= 0xac00 && c <= 0xd7af)
      || (c >= 0xf900 && c <= 0xfaff)) ++wide;
    else if (c >= 0xd800 && c <= 0xdfff) ++latin;
    else ++other;
  }
  const tokens = Math.ceil(latin / 3.6 + wide * 0.75 + other / 2.2);
  if (s.length > 8192) {
    if (tokenEstimates.size >= 64) tokenEstimates.delete(tokenEstimates.keys().next().value);
    tokenEstimates.set(s, tokens);
  }
  return tokens;
}

/*
    Les vitesses d'un modèle mesurées par le moteur sur ses réponses, en tokens par seconde : sa lecture des prompts,
    arrondie à la puissance de deux inférieure, pour que les choix qu'elle dimensionne restent tant qu'elle varie de
    moins de moitié, et son écriture des réponses.  Tant que le modèle n'a pas répondu, celles du processeur d'un Mac.
*/
export function readRate(id) {
  const loaded = loadedModel(id);
  const rate = loaded && loaded.prompt_tokens_per_second > 0 ? loaded.prompt_tokens_per_second : 128;
  return Math.pow(2, Math.floor(Math.log2(Math.max(1, rate))));
}

export function writeRate(id) {
  const loaded = loadedModel(id);
  if (loaded && loaded.generation_tokens_per_second > 0) return loaded.generation_tokens_per_second;
  return 10;
}

// Le contexte de travail d'un modèle de context tokens (voir workingSeconds).
export function workingContext(id, context) {
  const wanted = Math.max(minWorkingContext, workingSeconds * readRate(id));
  return Math.min(context || 8192, Math.pow(2, Math.round(Math.log2(wanted))));
}

// Les tokens des documents qu'un modèle aux capacités caps lit en entier : ceux d'un projet, ou ceux d'une question.
export function wholeTokens(caps, share, seconds) {
  return Math.min(share * (caps.working || 8192), seconds * (caps.read || 128));
}

// Les extraits d'une question pour un modèle aux capacités caps : {budget, limit}, en caractères et en passages.
export function excerptBudget(caps) {
  const most = Math.max(leastExcerptTokens, Math.min(mostExcerptTokens, excerptSeconds * (caps.read || 128)));
  const tokens = Math.max(leastExcerptTokens, Math.min(most, excerptShare * (caps.working || 8192)));
  return { budget: Math.floor(tokens * 3.6), limit: Math.max(4, Math.min(24, Math.round(tokens / passageTokens))) };
}

/*
    La mémoire des discussions : de courtes phrases sur l'utilisateur, l'une après l'autre, que le modèle écrit à partir
    des discussions, sauf les privées, et que chaque discussion reçoit à son début.  Sa taille suit le contexte de
    travail du modèle d'arrière-plan : memoryShare de celui-ci, memoryWords mots au plus.
*/
export const memoryWords = 400;
const memoryShare = 0.04;

export function memoryLimitWords(contextTokens) {
  return Math.max(30, Math.min(memoryWords, Math.floor(memoryShare * (contextTokens || 8192) * 0.7)));
}

// Le contexte que reçoit le modèle.
export function documentsOf(c) {
  const docs = [];
  const p = c.projectId ? projectById(c.projectId) : null;
  if (p) for (const f of p.files || []) docs.push({ name: f.name, text: f.text });
  if (p) for (const img of p.images || []) docs.push({ name: img.name, text: imageDocument(img).text });
  for (const m of c.messages) if (m.role === 'user') for (const a of m.attachments || []) docs.push({ name: a.name, text: a.text });
  return docs;
}

/*
    Le message système d'une discussion pour un modèle aux capacités caps ; set, issu de toolSet(), dit quels outils
    accompagnent la question, et leurs consignes ferment le message, celles de l'agent, puis celles du web.  Il contient
    ce qui reste identique d'une question à la suivante, pour que le moteur le lise une fois : le projet et ses
    documents d'abord, que les discussions du projet partagent, puis le jour où la discussion a commencé, en anglais
    quelle que soit la langue de l'interface, et la mémoire telle qu'elle était alors.
*/
export function systemPrompt(c, caps, set) {
  const agent = !!(set && set.agent);
  const parts = [];
  const p = c.projectId ? projectById(c.projectId) : null;
  if (p) {
    parts.push('This discussion belongs to the project "' + p.name + '".' + (p.description ? ' ' + p.description : ''));
    if (p.instructions) parts.push('Instructions of the project:\n' + p.instructions);
    const files = p.files || [];
    // Les documents entrent en entier tant qu'ils tiennent dans leur part du contexte ; au-delà,
    // les extraits les plus proches de chaque question viennent avec elle.  Ce message reste le
    // même d'une question à la suivante.
    if (knowledgeMode(p, caps, c) === 'whole') {
      parts.push('Documents of the project:\n' + files.map((f) => '<document name="' + f.name + '">\n' + f.text + '\n</document>').join('\n'));
    } else if (files.length) {
      const one = files.length === 1;
      parts.push('The project holds ' + (one ? '1 document' : files.length + ' documents') + ', too long to be read '
        + 'whole: ' + files.map((f) => f.name + ' (' + f.format + ')').join(', ') + '. The excerpts of '
        + (one ? 'this document' : 'these documents') + ' closest to each question come with it, in <excerpts>; say '
        + 'so when they do not hold the answer.'
        + (agent ? ' Use search_documents, document_outline and read_document to read more of '
          + (one ? 'it.' : 'them.') : ''));
    }
    const pictures = p.images || [];
    if (pictures.length) {
      parts.push('Images of the project: ' + pictures.map((x) => x.name).join(', ') + '. The description of an image '
        + 'comes with the questions it relates to, in <excerpt image="...">, and the image itself with it when you '
        + 'read images.');
    }
  }
  parts.push('Current date: ' + dayText(c.day || today()) + '.');
  if (state.settings.userName) parts.push('The user is named ' + state.settings.userName + '.');
  if (state.settings.systemPrompt) parts.push(state.settings.systemPrompt);
  // La mémoire telle qu'elle était quand la discussion a commencé : son prompt garde le même début.
  if (c.memory && state.settings.memoryEnabled && !c.incognito) {
    parts.push('What you remember from earlier discussions with the user, to use when it helps, without mentioning it '
      + 'otherwise:\n' + c.memory);
  }
  if (agent) parts.push(tools.agentInstructions);
  if (set && set.web) parts.push(web.instructions(set.search));
  return parts.join('\n\n');
}

/*
    Le texte d'une question pour le modèle, avec le jour où elle a été posée quand il diffère du jour que le modèle
    connaissait avant elle ; cuts (fitContext) raccourcit les textes les plus anciens.
*/
function userContent(m, cuts) {
  const cut = (key, text) => {
    const kept = cuts && cuts.texts.get(key);
    return kept == null ? text : keepEnd(text, kept);
  };
  let s = m.day ? 'Current date: ' + dayText(m.day) + '.\n\n' : '';
  for (const a of m.attachments || []) {
    if (a.mode === 'search') {
      s += '<document name="' + a.name + '" format="' + (a.format || '') + '">\n(Too long to be read whole: its excerpts '
        + 'closest to the questions come with them.)\n</document>\n\n';
    } else {
      s += '<document name="' + a.name + '">\n' + cut(a.id, a.text || '') + '\n</document>\n\n';
    }
  }
  if (m.context) s += m.context + '\n\n';
  return s + cut(m.id, m.content || '');
}

// Les parties des requêtes qui portent une image, avec la description de l'image, pour les estimations de l'ajustement.
const imageParts = new WeakMap();

function imagePart(url, img) {
  const part = { type: 'image_url', image_url: { url, detail: 'auto' } };
  imageParts.set(part, img);
  return part;
}

/*
    Les images d'une liste de messages que reçoit le modèle, "message/position" pour les images d'une question et
    "message/p" et la position pour les images du projet trouvées pour elle : les maxContextImages plus récentes d'entre
    elles.
*/
function sentImages(messages) {
  const out = new Set();
  for (let i = messages.length - 1; i >= 0; --i) {
    const found = (messages[i].projectImages || []).slice(0, maxQuestionProjectImages);
    for (let k = found.length - 1; k >= 0; --k) {
      if (out.size >= maxContextImages) return out;
      out.add(messages[i].id + '/p' + k);
    }
    const list = messages[i].images || [];
    for (let k = list.length - 1; k >= 0; --k) {
      if (out.size >= maxContextImages) return out;
      out.add(messages[i].id + '/' + k);
    }
  }
  return out;
}

/*
    Les messages d'une requête.  media donne les images : read, si le modèle lit les images, et
    url(id), l'URL de données d'une image, null quand ce navigateur ne la contient pas.  Les images
    d'une question passent avant son texte, les maxContextImages plus récentes de la discussion,
    parmi elles les images du projet trouvées pour chaque question, pour qu'une question garde ses
    images quand la suivante arrive ; un modèle qui ne lit que du texte, une requête sans media, et
    les images manquantes reçoivent leurs noms ("[Image: name]").  cuts (fitContext) raccourcit
    les textes les plus anciens, un résultat d'outil gardant son début, omet le raisonnement des
    étapes auxquelles il donne une longueur de 0, et omet les cuts.steps étapes les plus anciennes
    de la réponse en cours d'écriture, le dernier message, qu'une ligne nomme alors.
*/
export function apiMessages(c, system, cuts, media) {
  const out = [];
  if (system) out.push({ role: 'system', content: system });
  // Les échanges d'avant contextStart ont quitté le contexte lors d'une question précédente ; leur
  // résumé vient avec la première question qui reste.
  const start = c.contextStart || 0;
  const messages = c.messages.slice(start);
  const read = !!(media && media.read);
  const sent = read ? sentImages(messages) : null;
  // Un résumé qu'une version antérieure a gardé avec ses lignes répétées donne chacune d'elles une seule fois.
  let summary = start > 0 && c.summary && c.summary.text ? distinctLines(c.summary.text) : '';
  if (summary && cuts && cuts.summary != null) summary = keepEnd(summary, cuts.summary);
  for (const m of messages) {
    if (m.role === 'user') {
      const parts = [];
      const named = [];
      (m.images || []).forEach((img, k) => {
        const url = sent && sent.has(m.id + '/' + k) ? media.url(img.id) : null;
        if (url != null) parts.push(imagePart(url, img));
        else named.push('[Image: ' + img.name + ']');
      });
      if (read) {
        (m.projectImages || []).slice(0, maxQuestionProjectImages).forEach((img, k) => {
          const url = sent.has(m.id + '/p' + k) ? media.url(img.id) : null;
          if (url != null) parts.push(imagePart(url, img));
        });
      }
      let content = [named.join('\n'), userContent(m, cuts)].filter(Boolean).join('\n\n');
      if (summary) {
        content = '<summary_of_the_earlier_discussion>\n' + summary + '\n</summary_of_the_earlier_discussion>\n\n' + content;
        summary = '';
      }
      out.push({ role: 'user', content: parts.length ? parts.concat(content ? [{ type: 'text', text: content }] : []) : content });
      continue;
    }
    const kept = (key) => (cuts ? cuts.texts.get(key) : null);
    let steps = m.steps || [];
    let left = '';
    if (cuts && cuts.steps && m === messages[messages.length - 1]) {
      left = (cuts.steps === 1 ? '[The first step of this answer, its tool calls and their results, left the '
        : '[The first ' + cuts.steps + ' steps of this answer, their tool calls and their results, left the ')
        + 'context.]';
      steps = steps.slice(cuts.steps);
    }
    for (const s of steps) {
      const msg = { role: 'assistant', content: s.content || '' };
      if (left) {
        msg.content = left + (msg.content ? '\n\n' + msg.content : '');
        left = '';
      }
      if (s.reasoning && kept(s) !== 0) msg.reasoning_content = s.reasoning;
      const calls = (s.toolCalls || []).filter((tc) => tc.result != null);
      if (calls.length) {
        msg.tool_calls = calls.map((tc) => ({ id: tc.id, type: 'function', function: { name: tc.name, arguments: tc.arguments || '{}' } }));
      }
      if (!msg.content && !msg.tool_calls) continue;
      out.push(msg);
      for (const tc of calls) {
        const n = kept(tc);
        const content = n == null ? tc.result : keepStart(tc.result, n);
        out.push({ role: 'tool', tool_call_id: tc.id, name: tc.name, content });
      }
    }
  }
  return out;
}

/*
    Les images que porte la requête d'une question de c, sous forme d'URL de données lues depuis
    leurs Blob au moment de l'envoi : celles des questions de son contexte et celles du projet
    trouvées pour elles, pour un modèle de role qui lit les images.  Renvoie {read, url(id)}, url
    donnant null pour une image que ce navigateur ne contient pas.
*/
export async function requestMedia(c, role) {
  const read = readsImages(role);
  const urls = new Map();
  if (read) {
    const kept = c.messages.slice(c.contextStart || 0);
    const ids = imageIds(kept);
    for (const x of kept) for (const img of x.projectImages || []) ids.add(img.id);
    for (const id of ids) urls.set(id, await images.dataUrl(id).catch(() => null));
  }
  return { read, url: (id) => urls.get(id) || null };
}

// Les images des estimations, qui ne contiennent pas de données : l'ajustement les pèse par leurs tokens.
export function estimateMedia(role) {
  return { read: readsImages(role), url: () => '' };
}

// Les tokens de chaque image pour un modèle, mesurés par le service : modèle -> Map(image -> tokens).
export const imageTokenCache = new Map();

// Les tokens d'une image pour un modèle, tels que mesurés, sinon d'après sa taille : un token par carré de 32 sur 32
// pixels.
function imageTokens(model, img) {
  const known = imageTokenCache.get(model);
  if (known && known.has(img.id)) return known.get(img.id);
  return Math.ceil((img.width || 512) / 32) * Math.ceil((img.height || 512) / 32) + 2;
}

/*
    Mesure avec le service les tokens des images d'une liste de messages qu'il n'a pas encore
    mesurées pour model : les tokens d'une question qui contient l'image, moins ceux de la même
    question sans elle.  Une image que le service ne peut pas compter garde son estimation.
*/
export async function measureImages(model, list) {
  let known = imageTokenCache.get(model);
  if (!known) imageTokenCache.set(model, known = new Map());
  const parts = [];
  for (const m of list) {
    if (!Array.isArray(m.content)) continue;
    for (const part of m.content) {
      const img = imageParts.get(part);
      if (img && !known.has(img.id) && part.image_url.url) parts.push([img, part]);
    }
  }
  if (!parts.length) return;
  const base = await api.count(model, [{ role: 'user', content: '.' }]).catch(() => null);
  if (!base || typeof base.tokens !== 'number') return;
  for (const [img, part] of parts) {
    if (known.has(img.id)) continue;
    const r = await api.count(model, [{ role: 'user', content: [part, { type: 'text', text: '.' }] }]).catch(() => null);
    if (r && typeof r.tokens === 'number') known.set(img.id, Math.max(1, r.tokens - base.tokens));
  }
}

/*
    Le poids d'une liste de messages pour les estimations de l'ajustement : {chars, tokens}, les
    caractères de ses textes et les tokens de ses images (imageTokens) pour model.
*/
function weigh(list, model) {
  let chars = 0;
  let tokens = 0;
  for (const m of list) {
    if (!Array.isArray(m.content)) {
      chars += JSON.stringify(m).length;
      continue;
    }
    chars += JSON.stringify(Object.assign({}, m, { content: '' })).length;
    for (const part of m.content) {
      const img = imageParts.get(part);
      if (img) tokens += imageTokens(model, img);
      else chars += JSON.stringify(part).length;
    }
  }
  return { chars, tokens };
}

/*
    L'estimation des tokens de listes de messages, à partir d'une liste mesurée à tokens : ses
    images par leurs tokens, ses textes par leurs caractères, mis à l'échelle.  Renvoie {scale,
    of(list)}, scale donnant les tokens par caractère des textes.
*/
export function tokenEstimate(list, tokens, model) {
  const w = weigh(list, model);
  const scale = Math.max(0.02, (tokens - Math.min(w.tokens, 0.9 * tokens)) / Math.max(1, w.chars));
  return {
    scale,
    of: (msgs) => {
      const x = weigh(msgs, model);
      return x.chars * scale + x.tokens;
    }
  };
}

// Là où text peut être coupé : après la fin d'une phrase suivie d'une espace, ou une ligne vide.
const sentenceBreak = /[.!?\u2026\u3002\uff01\uff1f]["'\u00bb\u201d)\]]*\s+|\n\s*\n/g;

/*
    La fin de text, sans le début qui le fait dépasser max caractères : la coupe vient après la fin
    d'une phrase, pour que l'information la plus ancienne parte d'abord et qu'aucune phrase ne
    reste en partie.  Un texte sans fin de phrase à cet endroit est coupé à une espace.
*/
function keepEnd(text, max) {
  text = String(text || '');
  if (text.length <= max) return text;
  const from = text.length - Math.max(0, max - 2);
  sentenceBreak.lastIndex = from;
  const m = sentenceBreak.exec(text);
  let start = m ? m.index + m[0].length : text.indexOf(' ', from) + 1;
  if (start <= 0 || start >= text.length) start = from;
  return '\u2026 ' + text.slice(start);
}

// Le début de text, au plus max caractères, se terminant par sa dernière phrase entière.
export function keepStart(text, max) {
  text = String(text || '');
  if (text.length <= max) return text;
  const head = text.slice(0, max);
  let end = 0;
  sentenceBreak.lastIndex = 0;
  for (let m; (m = sentenceBreak.exec(head)); ) end = m.index + m[0].length;
  if (end < max / 2) end = Math.max(head.lastIndexOf(' '), max / 2);
  return head.slice(0, end).trim() + ' \u2026';
}

// Les échanges en texte brut, pour leur résumé : les questions avec leurs images nommées
// ("[Image: name]") et les noms de leurs documents, les réponses sans leur raisonnement, les
// résultats des outils en bref.
function transcriptOf(messages) {
  const lines = [];
  for (const m of messages) {
    if (m.role === 'user') {
      const docs = (m.attachments || []).map((a) => a.name);
      const pictures = (m.images || []).map((img) => '[Image: ' + img.name + ']');
      lines.push('User: ' + (pictures.length ? pictures.join(' ') + '\n' : '') + (m.content || '')
        + (docs.length ? '\n(documents: ' + docs.join(', ') + ')' : ''));
      continue;
    }
    const parts = [];
    for (const s of m.steps || []) {
      for (const tc of s.toolCalls || []) {
        parts.push('[' + tc.name + ' ' + (tc.arguments || '') + ': ' + keepStart(tc.result || '', 600) + ']');
      }
      if (s.content) parts.push(s.content);
    }
    if (parts.length) lines.push('Assistant: ' + parts.join('\n'));
  }
  return lines.join('\n\n');
}

/*
    Les lignes d'un texte écrit par un modèle qui a bouclé, comparées sans tenir compte de la casse et des espaces : une
    ligne de 24 caractères ou plus qui revient trois fois ou plus garde seulement sa première place, et une suite de
    lignes vides une seule d'entre elles.  Les lignes de code entre balises, les courtes et celles qui reviennent deux
    fois restent, comme un résumé ou une description les écrit.
*/
export function distinctLines(text) {
  const lines = String(text || '').split('\n');
  const keyOf = (line) => line.trim().replace(/\s+/g, ' ').toLowerCase();
  const fenced = [];
  let inCode = false;
  for (const line of lines) {
    const fence = /^\s*(```|~~~)/.test(line);
    fenced.push(inCode || fence);
    if (fence) inCode = !inCode;
  }
  const times = new Map();
  lines.forEach((line, i) => {
    const key = keyOf(line);
    if (!fenced[i] && key.length >= 24) times.set(key, (times.get(key) || 0) + 1);
  });
  const seen = new Set();
  const out = [];
  lines.forEach((line, i) => {
    const key = keyOf(line);
    if (!fenced[i] && (times.get(key) || 0) >= 3) {
      if (seen.has(key)) return;
      seen.add(key);
    } else if (!key && !fenced[i] && (!out.length || !out[out.length - 1].trim())) {
      return;
    }
    out.push(line);
  });
  return out.join('\n').trim();
}

function summaryInstructions(words) {
  return 'You keep the memory of a long discussion between a user and an assistant. Rewrite the previous summary, '
    + 'when there is one, together with the new exchanges, into a single summary that the assistant will read in '
    + 'place of these exchanges. Keep, in the order they came: the facts, figures, names, decisions and '
    + 'conclusions; the requests, preferences and constraints of the user; the code, commands and values that may '
    + 'be needed again, in short; the documents and the images by their names; the questions left open. Leave out '
    + 'greetings, repetitions and reasoning. Write in the language of the latest exchanges, as a compact list of '
    + 'points, in at most ' + words + ' words.';
}

// Un résumé écrit par le modèle d'arrière-plan à partir du précédent et d'une transcription.
async function summarize(c, previous, transcript, limit, signal) {
  const r = await backgroundText(summaryInstructions(Math.floor(limit * 0.6)),
    (previous ? '<previous_summary>\n' + previous + '\n</previous_summary>\n\n' : '')
      + '<exchanges>\n' + transcript + '\n</exchanges>', limit, signal);
  // Un résumé qui répète ses lignes, écrit par un modèle qui a bouclé, garde chacune d'elles une seule fois.
  const text = distinctLines(r.text);
  const finish = r.finish;
  if (!text) throw new Error('empty summary');
  // Un résumé arrêté par sa longueur se termine par sa dernière phrase entière.
  return finish === 'length' ? keepStart(text, text.length - 1) : text;
}

/*
    Compresse les échanges [from, to) d'une discussion dans son résumé : le modèle d'arrière-plan
    réécrit le résumé avec eux, par parties qui tiennent dans son contexte, les plus anciennes
    d'abord.  Renvoie {summary, to}, qu'applyCompaction donne à la discussion, qui commence alors à
    to, son résumé donné avec sa première question.  Un résumé qui a grandi au-delà de sa part de
    la place perd ses points les plus anciens.
*/
export async function compact(c, from, to, room, scale, signal) {
  const most = Math.max(384, Math.min(1536, Math.floor(summarySeconds * backgroundRates().write)));
  const limit = Math.max(128, Math.min(most, Math.floor(summaryShare * room)));
  // Les parties tiennent dans le contexte du modèle d'arrière-plan, qui écrit le résumé.
  room = Math.min(room, backgroundContext() - 64);
  let summary = (c.summary && c.summary.text) || '';
  // Les caractères d'échanges par partie : la place, moins le résumé, ses consignes et le résumé à
  // écrire.
  const chars = (n) => Math.floor(n / Math.max(scale, 1e-6));
  let i = from;
  while (i < to) {
    const available = Math.max(chars(Math.floor(room / 4)), chars(room - limit - 400) - summary.length);
    let j = i;
    let length = 0;
    while (j < to) {
      const piece = transcriptOf([c.messages[j]]).length + 2;
      if (j > i && length + piece > available) break;
      length += piece;
      ++j;
    }
    summary = await summarize(c, summary, keepEnd(transcriptOf(c.messages.slice(i, j)), available), limit, signal);
    i = j;
  }
  // Le résumé reste tel qu'il est écrit : sa longueur est bornée par les tokens de sa requête.
  return { summary: { text: summary, upTo: to, updated: Date.now() }, to,
    last: c.messages[to - 1] && c.messages[to - 1].id };
}
