// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Le travail d'arrière-plan sur les discussions (fork pour macOS : sorti d'app.js) : leur titre écrit par le modèle,
// la compression des échanges anciens hors d'une question, la lecture d'avance du début d'une discussion (warmup),
// et le texte des erreurs du service.

import * as api from './api.js';
import { answerReserve, apiMessages, compact, compactShare, estimateMedia, keepStart, requestMedia, systemPrompt,
  tokenEstimate } from './context.js';
import { answeredElsewhere, refreshViews } from './tabs.js';
import { backgroundChat, backgroundModel, backgroundTimeout, roleCapabilities, roleModel, toolSet } from './status.js';
import { compactions, currentVersion, keptTokens, nextStart, settleCompaction } from './generation.js';
import { deleted, persist } from './conversations.js';
import { fold, languageNames, textLanguage } from './lists.js';
import { generations, renderComposerState } from './composer.js';
import { lockThread } from './thread.js';
import { markOutside } from './files.js';
import { state } from './state.js';
import { t } from './i18n.js';

/*
    Une erreur dans la langue de l'interface : une erreur du service par son code, une erreur de la lecture d'un
    document (documents.js) par son message, sinon son message tel quel.
*/
export function errorText(e) {
  const known = { model_not_found: 'errModelNotFound', context_length_exceeded: 'errContextLength', unloaded: 'errUnloaded',
    starting: 'errStarting', model_loading: 'modelLoading',
    too_many_images: 'errTooManyImages', invalid_image_format: 'errImageFormat',
    engine_busy: 'errEngineBusy', busy: 'errBusy' };
  if (e && known[e.code]) return t(known[e.code]);
  const reading = {
    'zip-invalid': 'errZipInvalid', 'zip-compression': 'errZipCompression', 'xml-invalid': 'errXmlInvalid'
  };
  if (e && Object.prototype.hasOwnProperty.call(reading, e.message)) return t(reading[e.message]);
  // fetch est rejeté avec une TypeError quand le service ne répond pas.
  if (e instanceof TypeError) return t('errService');
  return (e && e.message) || String(e);
}

// Le titre d'une discussion jusqu'à ce que le modèle la nomme : le début de sa première question.
export function provisionalTitle(m) {
  const first = m.content || ((m.attachments || [])[0] || (m.images || [])[0] || {}).name || t('untitled');
  return first.replace(/\s+/g, ' ').slice(0, 70);
}

// Un titre tel que le modèle l'écrit, sans ses guillemets, ses marques et sa ponctuation finale.
function cleanTitle(text) {
  let title = String(text || '').split('\n').map((l) => l.trim()).find((l) => l) || '';
  title = title.replace(/^(title|titre|titel|titolo|t[ií]tulo)\s*:\s*/i, '').replace(/[*#_`]+/g, '')
    .replace(/^["'\u00ab\u201c\u201e\s]+|["'\u00bb\u201d\s]+$/g, '').replace(/[.!?:;,\u2026]+$/, '').trim();
  // Un identifiant écrit en guise de titre (ReverseWordsInSentence) devient des mots.
  if (!/\s/.test(title) && /[a-z][A-Z]/.test(title)) title = title.replace(/([a-z0-9])([A-Z])/g, '$1 $2');
  if (title) title = title.charAt(0).toLocaleUpperCase() + title.slice(1);
  return title.length > 70 ? keepStart(title, 70).replace(/\s*\u2026$/, '') : title;
}

// Le texte à partir duquel le modèle nomme une discussion : sa première question, avec les noms de ses documents et de
// ses images.
export function titleQuestionOf(m) {
  const names = (m.attachments || []).concat(m.images || []).map((a) => a.name);
  return keepStart((m.content || '') + (names.length ? '\n(' + names.join(', ') + ')' : ''), 1500);
}

// Vrai quand une discussion porte encore le début de sa première question comme titre.
export function needsTitle(c) {
  if (!c || deleted.has(c.id) || c.titleSource === 'user' || c.titleSource === 'model') return false;
  const first = (c.messages || []).find((m) => m.role === 'user');
  if (!first) return false;
  if (c.titleSource === 'question' || c.titleSource === 'pending') return true;
  return !c.title || c.title === provisionalTitle(first);
}

/*
    Des exemples de titres, un par langue de l'interface, qui montrent à un petit modèle qu'un titre
    nomme le sujet du message en quelques mots et dans sa langue, sans sa réponse ni ses premiers
    mots.
*/
const titleExamples = [
  ['French', 'Je suis infirmière à Lyon. En une ligne, c\'est quoi le triage ?', 'Définition du triage'],
  ['French', 'Peux-tu m\'expliquer comment fonctionne la photosynthèse ?', 'Fonctionnement de la photosynthèse'],
  ['French', 'Calcule 15 % de 80 et explique le calcul.', 'Calcul de pourcentage'],
  ['French', 'Écris un court poème sur la mer.', 'Poème sur la mer'],
  ['French', 'Comment inverser une liste chaînée en Java ?', 'Inversion d\'une liste en Java']
];

/*
    La demande d'un titre, avec les exemples comme échanges précédents, puis la question avec le nom
    de sa langue quand on peut la reconnaître.  again demande d'autres mots que ceux de la question.
*/
function titleMessages(question, again) {
  const code = textLanguage(question);
  const name = code ? languageNames[code] : 'the language of the message';
  const out = [{
    role: 'system',
    content: 'You give a short title to a discussion from its first message. The title names the topic in two to six '
      + 'words, as a noun phrase, in the language given. It never answers the message and never copies it. '
      + 'Answer with the title alone.'
  }];
  for (const [language, message, title] of titleExamples) {
    out.push({ role: 'user', content: 'Language: ' + language + '\nMessage: ' + message },
      { role: 'assistant', content: title });
  }
  out.push({
    role: 'user',
    content: 'Language: ' + name + '\nMessage: ' + question
      + (again ? '\nThe title uses other words than the message: it names the kind of request.' : '')
  });
  return out;
}

// Les mots d'un texte tels que les titres sont comparés, repliés et coupés à leurs cinq premières lettres.
function titleWords(text) {
  return fold(text).split(/[^\p{L}\p{N}]+/u).filter(Boolean).map((w) => w.slice(0, 5));
}

/*
    Vrai quand un titre est vide, contient plus de dix mots, ou répète les premiers mots d'une
    question de trois mots ou plus, sous d'autres formes ("Calculer 17,5 %" pour "Calcule 17,5 %").
*/
function poorTitle(title, question) {
  const words = titleWords(title);
  if (!words.length || words.length > 10) return true;
  const asked = titleWords(question);
  return asked.length >= 3 && words.join(' ') === asked.slice(0, words.length).join(' ');
}

/*
    Le titre que le modèle d'arrière-plan écrit pour une question, redemandé une seconde fois avec
    d'autres mots quand le premier ne fait que répéter la question.
*/
async function askTitle(question) {
  const first = cleanTitle((await backgroundChat(titleMessages(question, false), 24)).text);
  if (!poorTitle(first, question)) return first;
  const second = cleanTitle((await backgroundChat(titleMessages(question, true), 24, null, 0.7)).text);
  return poorTitle(second, question) && first ? first : second;
}

/*
    Les erreurs après lesquelles la demande d'un titre est renvoyée : pendant que le service démarre,
    pendant que son modèle se charge, manque ou est occupé avec des images, pendant que les corps
    qu'il lit d'un coup contiennent trop de valeurs, ou pendant que le service ne répond pas.  La
    tentative suivante attend au moins les secondes que donne l'en-tête Retry-After de la réponse
    (e.retryAfter, api.js).
*/
function retryable(e) {
  if (e instanceof TypeError) return true;
  return ['starting', 'model_loading', 'unloaded', 'model_not_found', 'engine_busy', 'busy'].includes(e && e.code)
    || /^5\d\d\b/.test(String(e && e.message));
}

// Les pauses entre les tentatives d'un titre, en secondes, environ deux minutes en tout.
const titleRetries = [2, 4, 8, 15, 30, 30, 30];

// Vrai quand le service répond et a un modèle, ce dont un titre a besoin.
export function serviceReady() {
  return !!state.status && !state.statusError && state.status.state !== 'starting' && !!roleModel('general');
}

/*
    Exécute fn sous un verrou que partagent les onglets, pour qu'un seul onglet fasse le travail ; ne
    fait rien quand un autre onglet le détient.  Là où le navigateur n'a pas de tels verrous, fn
    s'exécute.
*/
export function exclusive(name, fn) {
  if (!navigator.locks || typeof navigator.locks.request !== 'function') return fn();
  return navigator.locks.request(name, { ifAvailable: true }, (lock) => (lock ? fn() : null));
}

/*
    Les discussions que cet onglet est en train de nommer.  La marque reste hors des discussions,
    pour qu'aucune ne soit enregistrée avec elle, et qu'une discussion dont l'onglet s'est fermé
    pendant qu'on la nommait soit nommée lors d'une visite ultérieure.
*/
const naming = new Set();

/*
    Nomme une discussion d'après sa première question.  Le modèle d'arrière-plan écrit un titre
    court, dans la langue de la question, qui remplace le début de la question qui en tenait lieu
    jusque-là, sauf si l'utilisateur renomme la discussion entre-temps.  La requête attend le
    service et son modèle, et est renvoyée après une erreur passagère, un nombre limité de fois ;
    elle ne dépend ni de la discussion affichée, ni des questions qui suivent : le texte de la
    première question est celui gardé avec le titre provisoire (titleQuestion), qu'une
    modification ou une suppression de la question laisse tel qu'il était, sinon la première
    question d'une discussion enregistrée avant qu'il soit gardé.  Le titre s'affiche aussitôt dans
    le panneau latéral, la barre du haut et les autres onglets.
*/
export async function nameConversation(c) {
  if (naming.has(c.id)) return;
  naming.add(c.id);
  try {
    await exclusive('albireo-title-' + c.id, async () => {
      // Les secondes que le dernier refus du service a demandé d'attendre (Retry-After), que la pause suivante dure au
      // moins.
      let asked = 0;
      for (let attempt = 0; attempt <= titleRetries.length; ++attempt) {
        if (attempt) await new Promise((r) => setTimeout(r, Math.max(titleRetries[attempt - 1], asked) * 1000));
        asked = 0;
        const now = await currentVersion(c).catch(() => null);
        if (!needsTitle(now)) return;
        if (!serviceReady()) continue;
        const question = typeof now.titleQuestion === 'string' && now.titleQuestion.trim() ? now.titleQuestion
          : titleQuestionOf(now.messages.find((m) => m.role === 'user'));
        let title = '';
        try {
          title = await askTitle(question);
        } catch (e) {
          if (!retryable(e)) return;
          asked = e.retryAfter || 0;
          continue;
        }
        // Sans titre, le début de la question reste, et une réponse ultérieure réessaie.
        const latest = await currentVersion(c).catch(() => null);
        if (!title || !needsTitle(latest)) return;
        latest.title = title;
        latest.titleSource = 'model';
        delete latest.titleQuestion;
        await persist(latest);
        refreshViews();
        return;
      }
    });
  } finally {
    naming.delete(c.id);
  }
}

/*
    Nomme les discussions enregistrées qu'un onglet fermé, un rechargement ou un service hors de
    portée a laissées avec le début de leur question comme titre, les plus récentes d'abord, l'une
    après l'autre.  Une discussion dont la première réponse ne s'est pas terminée est nommée elle
    aussi : le titre ne suit que la question.
*/
let namingPending = false;

export async function namePending() {
  if (namingPending || state.incognito) return;
  namingPending = true;
  try {
    const waiting = state.conversations.filter((c) => needsTitle(c));
    for (const c of waiting.slice(0, 5)) {
      if (!serviceReady()) break;
      await nameConversation(c);
    }
  } finally {
    namingPending = false;
  }
}

/*
    Compresse en arrière-plan les échanges les plus anciens d'une discussion une fois qu'ils prennent compactShare de la
    place que leur laissent le message système et les outils, tels que la question suivante les recevra : après une
    réponse, et quand la discussion est ouverte.  La tâche prend aussitôt sa place dans compactions, avec son propre
    AbortController, dont se sert la suppression de la discussion (cancelCompaction) ; une question qui arrive avant que
    le résumé soit écrit ne l'attend que si elle ne tient pas sans lui (fitContext).  Un seul onglet compresse une
    discussion à la fois.
*/
export function compactLater(c) {
  if (!c || !c.messages.length || compactions.has(c.id) || !roleModel(c.role)) return;
  const abort = new AbortController();
  const job = { done: null, abort, always: false };
  const work = () => exclusive('albireo-compact-' + c.id, () => compactInBackground(c, abort.signal));
  job.done = Promise.resolve().then(work).catch(() => {}).finally(() => {
    if (compactions.get(c.id) === job) compactions.delete(c.id);
  });
  compactions.set(c.id, job);
}

/*
    Le travail de compactLater, sur les messages de c tels qu'ils sont à son démarrage.  Le service mesure le texte de
    la conversation, et à part le message système avec les outils ; les images comptent par leurs tokens (imageTokens).
    Les échanges qui partent sont ceux qui ramènent la conversation à keptTokens.  Le résumé va à la discussion telle
    qu'elle est une fois écrit, quand les échanges qu'il a compressés sont encore là ; une réponse en cours d'écriture
    dans la discussion garde le début de son prompt, et le résumé s'applique à sa fin.
*/
async function compactInBackground(c, signal) {
  const view = Object.assign({}, c, { messages: c.messages.slice() });
  const role = view.role;
  const model = 'albireo-' + role;
  const caps = roleCapabilities(role);
  const set = toolSet(view, caps);
  const toolDefs = set.definitions;
  const system = systemPrompt(view, caps, set);
  const text = apiMessages(view, system, null, { read: false, url: () => null });
  const [all, base] = await Promise.all([api.count(model, text, toolDefs),
    api.count(model, text.slice(0, 1), toolDefs).catch(() => null)]);
  if (signal.aborted || !all || typeof all.tokens !== 'number' || !all.context) return;
  const reserve = answerReserve(caps, !!(view.reasoning && caps.reasoning));
  // La place du contexte de travail, dans les limites de celle du contexte du modèle.
  const context = Math.min(all.context, caps.working || all.context);
  const room = context - Math.min(reserve, Math.floor(context / 2));
  const estimate = tokenEstimate(text, all.tokens, model);
  const media = estimateMedia(role);
  const build = () => apiMessages(view, system, null, media);
  const fixed = base && typeof base.tokens === 'number' ? base.tokens : estimate.of(text.slice(0, 1));
  if (estimate.of(build()) - fixed < compactShare * Math.max(0, room - fixed)) return;
  // La dernière question reste avec sa réponse, qu'une nouvelle réponse peut remplacer (regenerate).
  const next = nextStart(view, build, estimate.of, keptTokens(fixed, room));
  if (next <= (view.contextStart || 0)) return;
  cancelWarmup(c.id);
  const r = await compact(view, view.contextStart || 0, next, room, estimate.scale, signal);
  const now = await currentVersion(c);
  if (!now) return;
  const g = generations.get(now.id);
  if (g && g.answering) {
    g.compaction = r;
    return;
  }
  // Une réponse écrite dans un autre onglet garde le début de son prompt ; la discussion est compressée de nouveau plus
  // tard.
  if (answeredElsewhere(now)) return;
  if (!settleCompaction(now, r)) return;
  await persist(now);
  // Les images des échanges résumés ont quitté le contexte : leurs places vont à de nouvelles.
  if (state.current === now) {
    markOutside();
    renderComposerState();
    lockThread();
  }
  warmBeginning(now);
}

/*
    Les préchauffes du début d'une discussion après une compression, une par discussion au plus : id de la discussion ->
    AbortController.  Une question envoyée dans la discussion, sa suppression et une nouvelle compression de celle-ci
    arrêtent la préchauffe.
*/
export const warmups = new Map();

export function cancelWarmup(id) {
  const abort = warmups.get(id);
  if (!abort) return;
  warmups.delete(id);
  abort.abort();
}

/*
    Lit d'avance, en arrière-plan, le début du prompt de la question suivante de c une fois qu'un résumé l'a changé : le
    message système, le résumé et les échanges gardés, tels que le code d'une question les construit (apiMessages),
    suivis d'une question d'un caractère, pour que le moteur rende la dernière réponse comme un tour de l'historique,
    comme le fera la question suivante.  La requête va au modèle d'arrière-plan, que le service sert derrière les
    réponses, sans raisonnement et pour un token ; le moteur garde les clés et valeurs qu'il a lues, que la question
    suivante reprend, et ne calcule alors que ses propres tokens.  Le moteur lit de lui-même d'avance l'historique de
    chaque réponse ; la page ne le lui demande qu'après un résumé, qui change le début.  La requête est envoyée quand le
    modèle d'arrière-plan est le modèle de la fonction de c, dont la question suivante lit les clés et valeurs, par
    l'onglet qui a appliqué le résumé, tant qu'aucune réponse n'est en cours d'écriture dans c.  Son échec n'est signalé
    que dans la console.
*/
export async function warmBeginning(c) {
  if (!c || deleted.has(c.id) || generations.has(c.id) || !c.messages.length || !serviceReady()) return;
  const model = roleModel(c.role);
  const s = state.status;
  if (!model || ((s && s.background) || roleModel('general')) !== model) return;
  cancelWarmup(c.id);
  const abort = new AbortController();
  warmups.set(c.id, abort);
  let limit = null;
  try {
    const caps = roleCapabilities(c.role);
    const set = toolSet(c, caps);
    const media = await requestMedia(c, c.role);
    if (abort.signal.aborted) return;
    const next = Object.assign({}, c, { messages: c.messages.concat([{ id: 'next', role: 'user', content: '.' }]) });
    const body = { model: backgroundModel, reasoning: false, max_tokens: 1, stream: false,
      messages: apiMessages(next, systemPrompt(c, caps, set), null, media) };
    if (set.definitions) body.tools = set.definitions;
    limit = api.limitedSignal(abort.signal, backgroundTimeout);
    await api.postJSON('/v1/chat/completions', body, limit.signal);
  } catch (e) {
    if (!abort.signal.aborted) console.info('Albireo: the beginning of a discussion was not read ahead', e);
  } finally {
    if (limit) limit.done();
    if (warmups.get(c.id) === abort) warmups.delete(c.id);
  }
}

// La ligne sous la zone de saisie : ce que sont les réponses ; la version et le copyright sont dans « À propos ».
export function hintText() {
  return t('aiNotice');
}
