// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La zone de saisie (fork pour macOS : sortie d'app.js) : le texte et ses pièces jointes, les boutons du raisonnement,
// de l'agent et du Web, l'envoi d'une question, sa reprise ou sa régénération, et l'arrêt d'une réponse.

import * as images from './images.js';
import * as rag from './rag.js';
import * as store from './store.js';
import { acceptFor, inputsLabel, readsImages, roleCapabilities, roleInputs, roleModel, toolsAvailable,
  webUnavailable } from './status.js';
import { keepImages } from './app.js';
import { cancelWarmup, provisionalTitle, titleQuestionOf } from './background.js';
import { persist } from './conversations.js';
import { renderSidebar } from './sidebar.js';
import { announceBusy, answeredElsewhere, refreshViews } from './tabs.js';
import { runTurn } from './generation.js';
import { attachmentChip, lockThread, renderMain } from './thread.js';
import { attachmentSeconds, attachmentShare, estimateTokens, excerptBudget, keepStart, maxContextImages,
  maxQuestionProjectImages, wholeTokens } from './context.js';
import { cancelDraftSave, draftKey, drafts, forgetDraft, imageIdsOf, storedDraft } from './drafts.js';
import { contextImages, imageLimitText, imageRecord, imageRoom, imageThumb, readFiles } from './files.js';
import { fill, h, toast } from './dom.js';
import { icon } from './icons.js';
import { knowledgeMode, queueIndex } from './indexing.js';
import { memoryAbort } from './memory.js';
import { projectById } from './lists.js';
import { searchPassages, terms } from './documents.js';
import { state, ui } from './state.js';
import { t } from './i18n.js';

// Zone de saisie.
export function renderComposerState() {
  if (!ui.input) return;
  const c = state.current;
  const caps = roleCapabilities(c ? c.role : 'general');
  const reasoning = !!(c && c.reasoning && caps.reasoning);
  // L'agent et le web d'un modèle capable de raisonner viennent avec son raisonnement approfondi (toolsAvailable()).
  const ready = toolsAvailable(caps, reasoning);
  const agent = !!(c && c.agent && ready);
  ui.reasonButton.classList.toggle('on', reasoning);
  ui.reasonButton.setAttribute('aria-pressed', String(reasoning));
  ui.reasonButton.disabled = !caps.reasoning;
  // Un modèle entraîné à raisonner avant chaque réponse répond directement sans le bouton, comme les autres modèles.
  ui.reasonButton.title = t('reasoningHint');
  ui.agentButton.classList.toggle('on', agent);
  ui.agentButton.setAttribute('aria-pressed', String(agent));
  ui.agentButton.disabled = !ready;
  ui.agentButton.title = ready ? t('agentHint') : caps.tools ? t('agentNeedsReasoning') : t('agentNoTools');
  // Le bouton Web est désactivé et grisé, avec la raison comme titre, tant que le modèle n'appelle aucun outil, tant
  // qu'un modèle capable de raisonner n'a pas son raisonnement approfondi, ou tant que le service ne donne pas d'accès
  // au web.
  const reason = webUnavailable(caps, reasoning);
  const webOn = !!(c && c.web && !reason);
  ui.webButton.classList.toggle('on', webOn);
  ui.webButton.setAttribute('aria-pressed', String(webOn));
  ui.webButton.disabled = !!reason;
  ui.webButton.title = reason || t('webHint');
  // Les pièces jointes que lit le modèle de la fonction, les images tant que le contexte a des places pour elles.
  const role = c ? c.role : 'general';
  const room = imageRoom(c);
  if (ui.fileInput) ui.fileInput.accept = acceptFor(role, room);
  if (ui.attachButton) {
    let hint = t('attachInputs', { inputs: inputsLabel(roleInputs(role)) });
    if (readsImages(role)) hint += ' · ' + t('imagePlaces', { left: room, max: maxContextImages });
    ui.attachButton.title = hint;
    ui.attachButton.setAttribute('aria-label', hint);
  }
  // Une discussion à laquelle un autre onglet répond prend sa question suivante une fois cette réponse écrite ; la
  // réponse que cet onglet écrit dans la discussion affichée peut être arrêtée.
  const busy = busyHere();
  const elsewhere = !busy && answeredElsewhere(c);
  ui.sendButton.classList.toggle('stop', busy);
  ui.sendButton.innerHTML = icon(busy ? 'stop' : 'send');
  ui.sendButton.title = busy ? t('stop') : elsewhere ? t('busyElsewhere') : t('send');
  ui.sendButton.setAttribute('aria-label', ui.sendButton.title);
  ui.sendButton.disabled = sendDisabled();
  const p = c && c.projectId ? projectById(c.projectId) : null;
  ui.input.placeholder = p ? t('placeholderProject', { project: p.name }) : t('placeholder');
  // Les images en attente, puis les documents ; les images montrent combien le contexte en contient avec elles.
  const pictures = state.pending.filter((a) => a.kind === 'image');
  const count = pictures.length ? maxContextImages - room : 0;
  fill(ui.pendingBox, pictures.map((a) => imageThumb(a, { list: pictures, onremove: () => removePending(a) })),
    state.pending.filter((a) => a.kind !== 'image').map((a) => attachmentChip(a, true, () => removePending(a))),
    pictures.length ? h('span', { class: 'image-count', title: t('imageCountTitle', { n: count, max: maxContextImages }) },
      count + ' / ' + maxContextImages) : null);
  ui.pendingBox.classList.toggle('hidden', !state.pending.length);
}

// Retire une pièce jointe de la zone de saisie ; les places d'une image reviennent au contexte.
function removePending(a) {
  state.pending = state.pending.filter((x) => x !== a);
  if (a.kind === 'image') images.drop([a.id]);
  renderComposerState();
  lockThread();
  if (ui.input) ui.input.focus();
}

/*
    Vrai quand le bouton d'envoi ne peut rien faire, sans question ou pendant qu'un autre onglet
    répond dans la discussion.  Pendant que cet onglet répond, le bouton arrête la réponse.
*/
function sendDisabled() {
  if (busyHere()) return false;
  if (answeredElsewhere(state.current)) return true;
  return !ui.input.value.trim() && !state.pending.length;
}

export function autoGrow() {
  ui.input.style.height = 'auto';
  // Une zone de saisie cachée, derrière la page d'un projet, n'a pas de hauteur à mesurer : elle la prend une fois
  // affichée (drawMain).
  if (ui.input.scrollHeight) ui.input.style.height = Math.min(ui.input.scrollHeight, 260) + 'px';
  ui.sendButton.disabled = sendDisabled();
}

/*
    Ajoute des fichiers à la zone de saisie de la discussion affichée, les images dans la limite
    des places que son contexte a laissées.  Les fichiers lus pendant qu'une autre discussion a pris
    sa place rejoignent le brouillon de leur propre discussion, et quittent la mémoire avec une
    discussion privée ou supprimée.
*/
export async function addPending(files) {
  const c = state.current;
  let added = await readFiles(files, c ? c.role : 'general', imageRoom(c));
  const key = draftKey(c);
  const shown = state.current === c || (key !== '' && draftKey(state.current) === key);
  const draft = shown || !key ? null : drafts.get(key) || { text: storedDraft(key), files: [] };
  if (!shown && !draft) {
    images.drop(imageIdsOf(added));
    return;
  }
  // Deux ajouts lus ensemble s'en tiennent eux aussi aux places.
  const waiting = shown ? state.pending : draft.files;
  const room = Math.max(0, maxContextImages - contextImages(shown ? state.current : c) - imageIdsOf(waiting).length);
  const pictures = added.filter((a) => a.kind === 'image');
  if (pictures.length > room) {
    const extra = pictures.slice(room);
    images.drop(extra.map((a) => a.id));
    added = added.filter((a) => !extra.includes(a));
    toast(imageLimitText(extra.map((a) => a.name), 0), true, 10000);
  }
  if (!shown) {
    draft.files = draft.files.concat(added);
    drafts.set(key, draft);
    return;
  }
  state.pending = state.pending.concat(added);
  renderComposerState();
  lockThread();
}

/*
    Les réponses que cet onglet est en train d'écrire, une par discussion au plus : id de la discussion -> {c, abort,
    turn, answering, compaction}.  Une réponse continue pendant qu'une autre discussion est affichée, écrite dans sa
    propre discussion, qui est enregistrée comme d'habitude et que les autres onglets suivent ; la discussion affichée
    dessine la réponse qu'elle contient.  answering dit que le modèle a commencé à répondre, et compaction contient une
    compression des échanges terminée depuis, appliquée une fois la réponse terminée, ou quand une étape de l'agent ne
    tient plus sans elle (fitContext).
*/
export const generations = new Map();
// Les discussions dont la réponse s'est terminée pendant qu'une autre était affichée, marquées dans le panneau latéral
// jusqu'à ce qu'elles soient ouvertes.
export const unread = new Set();

// La réponse que cet onglet écrit dans la discussion c, ou null.
export function generationOf(c) {
  return (c && generations.get(c.id)) || null;
}

// Vrai quand cet onglet écrit une réponse dans la discussion affichée.
export function busyHere() {
  return !!generationOf(state.current);
}

// Vrai quand m est la réponse que cet onglet écrit dans la discussion c.
export function liveMessage(c, m) {
  const g = generationOf(c);
  return !!g && g.turn === m;
}

// Arrête la réponse que cet onglet écrit dans la discussion id, celle qui est affichée quand id n'est pas donné.
export function stopGeneration(id) {
  const g = generations.get(id == null ? state.current && state.current.id : id);
  if (g) g.abort.abort();
}

export function stopAllGenerations() {
  for (const g of generations.values()) g.abort.abort();
}

export async function onSend() {
  if (busyHere()) { stopGeneration(); return; }
  if (sendDisabled()) return;
  // Les images s'en tiennent aux places du contexte, qu'un autre onglet a pu prendre entre-temps.
  const pictures = state.pending.filter((a) => a.kind === 'image');
  const left = maxContextImages - contextImages(state.current);
  if (pictures.length > left) {
    toast(imageLimitText(pictures.slice(Math.max(0, left)).map((a) => a.name), left), true, 10000);
    return;
  }
  const text = ui.input.value.trim();
  const attachments = state.pending;
  state.pending = [];
  ui.input.value = '';
  autoGrow();
  // Le brouillon part avec sa question : la discussion, créée par elle ou non, continue avec une zone de saisie vide.
  cancelDraftSave();
  forgetDraft(draftKey(state.current));
  await sendMessage(text, attachments);
}

/*
    Démarre une réponse dans la discussion c.  Tant que c est affichée, la zone de saisie propose de
    l'arrêter et les boutons qui changent la discussion attendent ; le panneau latéral la marque,
    les autres onglets le savent, et les questions de c attendent sa fin.  Renvoie le contrôleur qui
    l'arrête.
*/
function beginTurn(c) {
  const g = { c, abort: new AbortController(), turn: null, answering: false, compaction: null };
  generations.set(c.id, g);
  // La question lit elle-même le début : une préchauffe de celui-ci s'arrête, et le moteur garde ce qu'il a lu.
  cancelWarmup(c.id);
  announceBusy(c, true);
  unread.delete(c.id);
  renderComposerState();
  lockThread();
  if (ui.sideScroll) renderSidebar();
  return g.abort;
}

// Une nouvelle réponse de la discussion c, dessinée en direct à partir de maintenant.
function newTurn(c) {
  const turn = { id: store.uid(), role: 'assistant', steps: [], created: Date.now(), model: roleModel(c.role) || '' };
  c.messages.push(turn);
  const g = generations.get(c.id);
  if (g) g.turn = turn;
  return turn;
}

// Le jour de l'ordinateur, "2026-11-01".
export function today() {
  return new Date().toLocaleDateString('en-CA');
}

// Un jour tel que le modèle le reçoit, en anglais comme le reste des consignes : "2026-11-01 (Sunday)".
export function dayText(day) {
  const at = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(day || ''));
  if (!at) return String(day || '');
  const weekday = new Intl.DateTimeFormat('en-US', { weekday: 'long', timeZone: 'UTC' })
    .format(new Date(Date.UTC(Number(at[1]), Number(at[2]) - 1, Number(at[3]))));
  return day + ' (' + weekday + ')';
}

// Le dernier jour donné au modèle de c : celui de la question la plus récente qui en porte un, sinon celui de c.
function lastDay(c) {
  for (let i = c.messages.length - 1; i >= 0; --i) if (c.messages[i].day) return c.messages[i].day;
  return c.day || '';
}

export async function sendMessage(text, attachments) {
  const c = state.current;
  if (generations.has(c.id) || answeredElsewhere(c)) return;
  const abort = beginTurn(c);
  const all = attachments || [];
  const m = {
    id: store.uid(), role: 'user', content: text, attachments: all.filter((a) => a.kind !== 'image'), created: Date.now()
  };
  const pictures = all.filter((a) => a.kind === 'image').map(imageRecord);
  if (pictures.length) m.images = pictures;
  // La mise à jour de la mémoire cède la place à la question, et revient une fois l'utilisateur au calme.
  if (memoryAbort) memoryAbort.abort();
  /*
      Le message système d'une discussion garde le jour où elle a commencé, pour que le prompt de chaque question garde
      le même début, que le moteur lit une fois ; une question posée un jour suivant porte le jour où elle est posée, à
      sa propre place dans le prompt.
  */
  const day = today();
  if (!c.day) c.day = day;
  else if (day !== lastDay(c)) m.day = day;
  c.messages.push(m);
  // Une nouvelle discussion commence avec la mémoire telle qu'elle est maintenant.
  if (c.messages.length === 1 && !c.incognito && state.settings.memoryEnabled && state.settings.memory) {
    c.memory = state.settings.memory;
  }
  attachmentModes(c, m);
  /*
      Le début de la première question tient lieu de titre jusqu'à ce que le modèle en écrive un,
      et le texte de cette question reste avec lui, le seul texte à partir duquel le modèle nomme
      la discussion (nameConversation).  Le titre d'une discussion dont la première question est
      modifiée ou supprimée reste.
  */
  if (c.messages.length === 1 && !c.title) {
    c.title = provisionalTitle(m);
    c.titleSource = 'question';
    c.titleQuestion = titleQuestionOf(m);
  }
  c.updated = Date.now();
  // La réponse s'affiche aussitôt, ses boutons désactivés, pendant que son contexte est préparé.
  const turn = newTurn(c);
  turn.preparing = true;
  try {
    // La question est enregistrée avant que ses documents soient indexés (pruneIndex) et avant que ses images soient
    // enregistrées (pruneImages).
    renderMain();
    await persist(c);
    await keepImages(c, pictures);
    if (!c.incognito && state.current === c && location.hash !== '#/c/' + c.id) history.replaceState(null, '', '#/c/' + c.id);
    refreshViews();
    await prepareQuestion(c, m, abort.signal);
    await persist(c);
  } catch (e) {
    // Une question qui n'a pas pu être enregistrée reçoit quand même une réponse, et est enregistrée avec elle.
  }
  turn.preparing = false;
  await runTurn(c, abort, turn);
}

export async function regenerate(c) {
  if (generations.has(c.id) || answeredElsewhere(c)) return;
  const removed = [];
  while (c.messages.length && c.messages[c.messages.length - 1].role === 'assistant') removed.unshift(c.messages.pop());
  if (!c.messages.length) return;
  // Une discussion compressée par une version antérieure jusqu'à sa dernière réponse recommence à sa dernière question.
  const lastQuestion = c.messages.map((x) => x.role).lastIndexOf('user');
  if (lastQuestion >= 0 && (c.contextStart || 0) > lastQuestion) c.contextStart = lastQuestion;
  const abort = beginTurn(c);
  const turn = newTurn(c);
  // La nouvelle réponse prend la place de l'ancienne dans le fil, ses boutons attendant qu'elle soit écrite.
  if (state.current === c) renderMain();
  await runTurn(c, abort, turn);
}

// Le temps que peut prendre l'indexation d'un document joint à une question avant sa réponse, en millisecondes.
const indexWait = 15000;

// Comment le modèle lit les documents d'une question : en entier tant qu'ils tiennent dans leur part du contexte de
// travail et dans leur temps de lecture, par recherche au-delà.
function attachmentModes(c, m) {
  const limit = wholeTokens(roleCapabilities(c.role), attachmentShare, attachmentSeconds);
  for (const a of m.attachments || []) {
    if (!a.mode) a.mode = estimateTokens(a.text) <= limit ? 'whole' : 'search';
  }
}

/*
    Prépare une question une fois, quand elle est envoyée : ses grands documents sont indexés pour
    la recherche de la discussion, et les extraits des documents cherchés les plus proches d'elle
    sont gardés avec elle, pour que le prompt des questions suivantes commence de la même façon.
    signal arrête les vecteurs des documents, alors cherchés par leurs mots, et la recherche.
*/
export async function prepareQuestion(c, m, signal) {
  const caps = roleCapabilities(c.role);
  const owners = [];
  attachmentModes(c, m);
  for (const a of m.attachments || []) {
    if (a.mode === 'search') {
      /*
          Indexés aussitôt, à côté des documents des projets qui peuvent être en cours : les vecteurs calculés dans la
          limite de indexWait, les passages cherchés par leurs mots au-delà, jusqu'à ce que la file complète leurs
          vecteurs.
      */
      toast(t('indexingDocument', { name: a.name }));
      const r = await rag.indexDocument(c.id, a, { persistent: !c.incognito, signal, deadline: Date.now() + indexWait })
        .catch(() => null);
      if (r && rag.embeddingModel() && r.vectors < r.passages && !(signal && signal.aborted)) {
        queueIndex(c.id, a, !c.incognito);
      }
    }
  }
  for (const x of c.messages) {
    if (x.role === 'user' && (x.attachments || []).some((a) => a.mode === 'search' || a.searchable)) {
      owners.push(c.id);
      break;
    }
  }
  const p = c.projectId ? projectById(c.projectId) : null;
  // Les descriptions des images du projet sont cherchées, même quand ses documents sont lus en entier.
  const pictures = new Map(((p && p.images) || []).map((x) => [x.id, x]));
  let skip = null;
  if (p && knowledgeMode(p, caps, c) === 'search') owners.unshift(p.id);
  else if (p && pictures.size) {
    owners.unshift(p.id);
    skip = new Set((p.files || []).map((f) => f.id));
  }
  m.context = '';
  delete m.projectImages;
  delete m.passages;
  if (!owners.length) return;
  /*
      Une question de peu de mots emporte avec elle le début de la précédente : "and in 2025?" est cherché avec ce qu'il
      prolonge.
  */
  let question = m.content || '';
  if (terms(question).length < 6) {
    const users = c.messages.filter((x) => x.role === 'user' && x !== m);
    if (users.length) question = keepStart(users[users.length - 1].content || '', 300) + '\n' + question;
  }
  if (!question.trim()) question = (m.attachments || []).map((a) => a.name).join(' ');
  // Les passages que les questions du contexte ont déjà apportés ne sont pas redonnés.
  const exclude = new Set();
  for (const x of c.messages.slice(c.contextStart || 0)) {
    if (x !== m && x.role === 'user') for (const id of x.passages || []) exclude.add(id);
  }
  try {
    const { budget, limit } = excerptBudget(caps);
    const hits = await rag.search(owners, question, { budget, limit, signal, skip, exclude });
    if (hits.length) {
      m.passages = hits.reduce((list, x) => list.concat(x.passages || []), []);
      m.context = '<excerpts>\n' + hits.map((x) => (pictures.has(x.doc) ? '<excerpt image="' + x.name + '"'
        : '<excerpt document="' + x.name + '"' + (x.where ? ' location="' + x.where + '"' : '')) + '>\n' + x.text
        + '\n</excerpt>').join('\n') + '\n</excerpts>';
      // Les images dont les descriptions ont été trouvées viennent avec la question, les plus pertinentes d'abord.
      const found = hits.filter((x) => pictures.has(x.doc)).sort((a, b) => b.score - a.score)
        .slice(0, maxQuestionProjectImages).map((x) => imageRecord(pictures.get(x.doc)));
      if (found.length) m.projectImages = found;
    }
  } catch (e) {
    m.context = '';
  }
}

/*
    Les passages des documents d'une discussion et de son projet les plus proches d'une requête :
    les documents indexés par leurs passages (rag.js), et ceux lus en entier dans les messages, par
    leurs mots.
*/
export async function searchDocumentsOf(c, query, limit) {
  const owners = [];
  const p = c.projectId ? projectById(c.projectId) : null;
  if (p && ((p.files || []).length || (p.images || []).length)) owners.push(p.id);
  const whole = [];
  let searched = false;
  for (const m of c.messages) {
    if (m.role !== 'user') continue;
    for (const a of m.attachments || []) {
      if (a.mode === 'search' || a.searchable) searched = true;
      if (a.mode !== 'search') whole.push({ name: a.name, text: a.text });
    }
  }
  if (searched) owners.push(c.id);
  const hits = owners.length ? await rag.search(owners, query, { limit, budget: 1500 * limit }) : [];
  const words = whole.length ? searchPassages(whole, query, limit).map((x) => ({ name: x.name, where: '', text: x.text })) : [];
  // Les deux listes à tour de rôle, pour que les documents lus en entier gardent leur place.
  const out = [];
  for (let k = 0; out.length < limit && (k < hits.length || k < words.length); ++k) {
    if (k < hits.length) out.push(hits[k]);
    if (k < words.length && out.length < limit) out.push(words[k]);
  }
  return out;
}
