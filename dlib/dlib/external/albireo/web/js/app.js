// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'application web d'Albireo : les discussions et les projets gardés dans ce navigateur, les discussions privées
    gardées en mémoire seulement, les trois fonctions du service (général, code, cybersécurité), le raisonnement
    approfondi affiché au fil de son écriture, et un mode agent dont les outils s'exécutent dans la page.

    LE CONTEXTE D'UNE QUESTION
        Le moteur garde les clés et valeurs de chaque conversation et des prompts qu'elle partage avec d'autres, et ne
        recalcule que les tokens qui diffèrent de ce qu'il a déjà lu.  Le prompt est donc construit pour que son début
        ne change jamais d'une question à la suivante : le message système ne contient que ce qui reste (la date, les
        instructions, les documents d'un projet quand ils tiennent en entier), chaque message garde, une fois envoyé,
        les extraits de documents choisis pour lui, et les échanges les plus anciens partent d'un seul coup, bien en
        dessous de la limite, plutôt qu'un par un à chaque question.  Chaque question ne coûte alors que ses propres
        tokens, quelle que soit la longueur de la conversation, et les conversations d'un même projet partagent la
        lecture de ses documents.

        Les documents d'un projet qui ne tiennent pas en entier, et les grands documents joints à un message, sont
        l'objet d'une recherche (rag.js) : les extraits les plus proches de la question s'y joignent.

    LES IMAGES
        Une question peut porter des images, pour un modèle qui les lit (images.js).  Elles passent avant son texte tant
        que la question reste dans le contexte, maxContextImages au plus pour la discussion : les images des échanges
        qu'une compression a résumés quittent le contexte avec eux et libèrent leurs places.  Les images d'un projet
        sont décrites par le modèle d'arrière-plan, et leurs descriptions sont cherchées avec ses documents ; une
        question qui en trouve une reçoit l'image avec elle.  Un modèle qui ne lit que du texte reçoit les noms des
        images.
*/

import { t, tn, setLanguage, language } from './i18n.js';
import { icon } from './icons.js';
import * as store from './store.js';
import { renderMarkdown, attachCodeButtons, escapeHtml } from './markdown.js';
import * as api from './api.js';
import { extractText, maxFileBytes, extension, searchPassages, documentAccept, fileKind, pdfPages, terms }
  from './documents.js';
import * as rag from './rag.js';
import * as tools from './tools.js';
import * as web from './web.js';
import * as images from './images.js';

import { modelSheetDialog } from './models.js';
import { defaults, state, ui } from './state.js';
import { copyText, download, fill, formatDecimal, formatDuration, formatLimit, formatNumber, formatShortDuration,
  formatSize, h, iconButton, mimeOf, storageFailed, toast } from './dom.js';
import { closeMenu, confirmDialog, dialog, promptDialog, showMenu, showPopover } from './dialogs.js';
import { changelogDialog, renderLicenses, requestPersistence, settingsDialog } from './settings.js';
import { answerReserve, apiMessages, attachmentSeconds, attachmentShare, compact, compactShare, distinctLines,
  documentsOf, estimateMedia, estimateTokens, excerptBudget, imageTokenCache, keepStart, keptShare, knowledgeSeconds,
  knowledgeShare, maxContextImages, maxProjectImages, maxProjectItems, maxQuestionProjectImages, measureImages,
  memoryLimitWords, memoryWords, readRate, requestMedia, systemPrompt, tokenEstimate, wholeTokens, workingContext,
  writeRate } from './context.js';

export const roles = ['general', 'code', 'security'];
const roleIcons = { general: 'compass', code: 'code', security: 'shield' };
const projectColors = ['#5b8cff', '#f2b544', '#3fb68b', '#e5534b', '#a371f7', '#2fb9d4', '#e08a3c', '#8a90aa'];
// Le nombre maximal d'étapes d'une réponse avec outils : chaque étape est une requête au modèle, puis les appels qu'il
// demande.
const maxAgentSteps = 100;

// Thème et langue.
export function applyTheme() {
  const pref = state.settings.theme;
  const dark = pref === 'dark' || (pref === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  document.getElementById('hljs-light').disabled = dark;
  document.getElementById('hljs-dark').disabled = !dark;
}

// La langue de l'interface : le français, la seule de ce fork.
function applyLanguage() {
  setLanguage('fr');
}

// Enregistre un réglage et prévient les autres onglets ; un réglage que la base refuse reste pour cette visite.
export async function saveSetting(key, value) {
  state.settings[key] = value;
  await store.setSetting(key, value).catch(storageFailed);
  broadcast({ type: 'setting', key, value });
  if (/^memory/.test(key) && ui.memoryView) ui.memoryView();
}

/*
    Les onglets de l'application se communiquent les réglages qu'ils changent, la remise à zéro de
    la mémoire, les discussions et les projets qu'ils enregistrent ou suppriment, et les discussions
    auxquelles ils sont en train de répondre, pour que chaque onglet montre aussitôt l'état actuel.
    Les messages passent par un BroadcastChannel, sinon par l'événement storage de localStorage.
*/
let channel = null;
try {
  channel = new BroadcastChannel('albireo');
  channel.onmessage = (e) => receive(e.data);
} catch (e) {
  channel = null;
  window.addEventListener('storage', (ev) => {
    if (ev.key !== 'albireo-message' || !ev.newValue) return;
    try {
      receive(JSON.parse(ev.newValue).message);
    } catch (e2) {
      // Une valeur qu'une autre application de l'origine a écrite.
    }
  });
}

export function broadcast(message) {
  try {
    if (channel) channel.postMessage(message);
    else localStorage.setItem('albireo-message', JSON.stringify({ message, n: Math.random() }));
  } catch (e) {
    // Les autres onglets relisent les réglages à leur prochain démarrage.
  }
}

// Un message d'un autre onglet.
function receive(message) {
  if (!message || typeof message !== 'object') return;
  if (message.type === 'setting' && Object.prototype.hasOwnProperty.call(defaults, message.key)) {
    state.settings[message.key] = message.value;
    settingChanged(message.key);
  } else if (message.type === 'memoryReset') {
    forgetMemory();
  } else if (message.type === 'conversation' && typeof message.id === 'string') {
    syncConversation(message.id);
  } else if (message.type === 'deleted' && typeof message.id === 'string') {
    conversationDeleted(message.id);
  } else if (message.type === 'cleared') {
    everythingDeleted();
  } else if (message.type === 'lists') {
    syncLists();
  } else if (message.type === 'projects') {
    syncProjects();
  } else if (message.type === 'busy' && typeof message.id === 'string' && typeof message.tab === 'string') {
    busyElsewhere(message.id, message.tab, !!message.busy);
  }
}

// Affiche un réglage changé dans cet onglet ou dans un autre.
function settingChanged(key) {
  if (key === 'theme') applyTheme();
  else if (key === 'language') languageChanged().catch(() => {});
  else if (key === 'userName' || key === 'defaultRole') refreshViews();
  if (/^memory/.test(key) && ui.memoryView) ui.memoryView();
}

/*
    Applique la langue de l'interface après un changement du réglage ici ou dans un autre onglet,
    ou de la langue d'Albireo.  Les textes sont redessinés, et la mémoire est écrite dans la
    nouvelle langue à sa prochaine mise à jour.  force les redessine même quand la langue reste.
*/
async function languageChanged(force) {
  const before = language();
  applyLanguage();
  if (language() === before && !force) return;
  state.catalog = await api.models(language()).catch(() => state.catalog);
  buildLayout();
  renderAll();
  if (language() !== before) rememberLater();
}

/*
    Redessine le panneau latéral, la barre du haut, la zone de saisie, la ligne d'état, et les pages
    sans messages (accueil, projet) sauf si l'utilisateur y est en train de taper, pour qu'ils
    montrent l'état des discussions et des projets.  Les messages de la discussion affichée restent
    tels quels.
*/
export function refreshViews() {
  if (!ui.sideScroll) return;
  renderSidebar();
  if (!ui.topbar.contains(document.activeElement)) renderTopbar();
  renderComposerState();
  renderStatusLine();
  const page = state.view === 'project' || (state.view === 'chat' && state.current && !state.current.messages.length);
  if (page && !editingIn(ui.content)) renderMain();
}

// Vrai quand l'utilisateur est en train de taper dans un champ de el.
function editingIn(el) {
  const a = document.activeElement;
  return !!(a && el && el.contains(a) && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.isContentEditable));
}

// Les messages d'une discussion tels qu'ils sont dessinés, pour savoir si une copie enregistrée change le fil.
function threadKey(c) {
  const messages = (c && c.messages) || [];
  const last = messages[messages.length - 1];
  const steps = last && last.steps
    ? last.steps.map((s) => (s.content || '').length + '.' + (s.toolCalls || []).length) : [];
  const ended = last && (last.error || last.cancelled) ? 1 : 0;
  return messages.map((m) => m.id).join(',') + '|' + steps.join(',') + '|' + ended + '|' + (c.contextStart || 0);
}

/*
    Les champs d'une discussion qu'un autre onglet peut changer pendant que celui-ci y répond ou modifie l'une de ses
    questions : le titre, l'épingle, le projet, la fonction et les boutons de la zone de saisie, que la question
    suivante prend.  La réponse en cours d'écriture garde la fonction et les outils de son début.
*/
const sharedFields = ['title', 'titleSource', 'titleQuestion', 'pinned', 'projectId', 'role', 'reasoning', 'agent',
  'web'];

// Les discussions à relire depuis la base, chacune une fois après une rafale de messages.
const syncTimers = new Map();

function syncConversation(id) {
  clearTimeout(syncTimers.get(id));
  syncTimers.set(id, setTimeout(() => {
    syncTimers.delete(id);
    applyStored(id).catch(storageFailed);
  }, 50));
}

/*
    Prend la copie d'une discussion qu'un autre onglet a enregistrée.  Une discussion à laquelle
    cet onglet répond, affichée ou non, garde ses messages et prend le titre, l'épingle et le
    projet ; sinon, celle qui est affichée prend la copie enregistrée en entier, et ses messages
    sont redessinés quand ils ont changé.
*/
async function applyStored(id) {
  if (deleted.has(id)) return;
  const stored = await store.getConversation(id);
  if (!stored) {
    if (state.conversations.some((x) => x.id === id)) conversationDeleted(id);
    return;
  }
  let copy = stored;
  const shown = state.current && state.current.id === id ? state.current : null;
  const live = generations.get(id);
  let redraw = false;
  // Une question en cours de modification garde la copie à laquelle elle appartient.
  if (live || (shown && (editingIn(ui.content) || (ui.thread && ui.thread.querySelector('textarea'))))) {
    copy = live ? live.c : shown;
    for (const k of sharedFields) copy[k] = stored[k];
  } else if (shown) {
    redraw = threadKey(shown) !== threadKey(stored);
    state.current = stored;
  }
  const i = state.conversations.findIndex((x) => x.id === id);
  if (i >= 0) state.conversations[i] = copy;
  else state.conversations.unshift(copy);
  state.conversations.sort((a, b) => b.updated - a.updated);
  if (redraw && state.view === 'chat') renderMain();
  refreshViews();
}

/*
    Oublie une discussion supprimée dans un autre onglet, arrête ici sa réponse en cours et la compression de ses
    échanges, oublie son brouillon, et ne l'enregistre plus jamais.
*/
function conversationDeleted(id) {
  deleted.add(id);
  othersBusy.delete(id);
  unread.delete(id);
  stopGeneration(id);
  cancelCompaction(id);
  cancelWarmup(id);
  web.forgetPages(id);
  forgetDraft(id);
  state.conversations = state.conversations.filter((x) => x.id !== id);
  if (state.current && state.current.id === id) go('#/');
  else refreshViews();
}

// Toutes les discussions et tous les projets supprimés dans un autre onglet ; le brouillon de "New chat" reste.
function everythingDeleted() {
  for (const c of state.conversations) deleted.add(c.id);
  if (state.current && state.current.messages.length && !state.current.incognito) deleted.add(state.current.id);
  for (const g of generations.values()) if (!g.c.incognito) deleted.add(g.c.id);
  stopAllGenerations();
  unread.clear();
  cancelIndex();
  cancelBackground();
  rag.removeAll();
  images.removeAll(waitingImages());
  forgetStoredDrafts();
  for (const id of deleted) web.forgetPages(id);
  syncLists().then(() => go('#/'));
}

/*
    Les projets relus depuis la base après qu'un autre onglet a changé l'un d'eux, une fois pour une
    rafale de messages : les discussions restent telles quelles.
*/
let projectsSync = null;

function syncProjects() {
  if (!projectsSync) {
    projectsSync = new Promise((resolve) => setTimeout(resolve, 50)).then(async () => {
      projectsSync = null;
      try {
        state.projects = await store.allProjects();
      } catch (e) {
        storageFailed(e);
      }
      refreshViews();
    });
  }
  return projectsSync;
}

/*
    Les listes relues depuis la base, après une importation, la suppression d'un projet ou de tout
    dans un autre onglet, une fois pour une rafale de messages.  La discussion affichée et les
    discussions auxquelles cet onglet répond restent les copies de cet onglet.
*/
let listsSync = null;

export function syncLists() {
  if (!listsSync) {
    listsSync = new Promise((resolve) => setTimeout(resolve, 50)).then(async () => {
      listsSync = null;
      await reloadLists().catch(storageFailed);
      keepOwnCopies();
      refreshViews();
    });
  }
  return listsSync;
}

/*
    Met les copies de cet onglet, la discussion affichée et les discussions auxquelles il répond, dans la liste lue
    depuis la base à la place de celles qui y sont enregistrées.  Une discussion dont le projet a été supprimé le
    quitte, comme l'ont fait les discussions enregistrées.
*/
function keepOwnCopies() {
  const own = Array.from(generations.values(), (g) => g.c);
  if (state.current) own.push(state.current);
  for (const c of own) {
    const i = state.conversations.findIndex((x) => x.id === c.id);
    if (i >= 0) state.conversations[i] = c;
    if (c.projectId && !projectById(c.projectId)) c.projectId = null;
  }
}

/*
    Les discussions auxquelles d'autres onglets répondent : id -> {tab, until}.  Un onglet le redit
    toutes les busyBeat millisecondes tant qu'il répond, pour que la marque d'un onglet fermé
    entre-temps expire.
*/
const othersBusy = new Map();
const busyBeat = 20000;

function busyElsewhere(id, tab, busy) {
  const before = othersBusy.has(id);
  if (busy) othersBusy.set(id, { tab, until: Date.now() + 3 * busyBeat });
  else if (othersBusy.has(id) && othersBusy.get(id).tab === tab) othersBusy.delete(id);
  if (state.current && state.current.id === id) {
    renderComposerState();
    lockThread();
  }
  // Le point de la discussion dans le panneau latéral suit.
  if (before !== othersBusy.has(id) && ui.sideScroll) renderSidebar();
}

// Vrai quand un autre onglet répond dans la discussion c.
function answeredElsewhere(c) {
  const mark = c ? othersBusy.get(c.id) : null;
  if (mark && mark.until < Date.now()) othersBusy.delete(c.id);
  return !!(mark && mark.until >= Date.now());
}

// Oublie les marques des onglets qui ont cessé de dire qu'ils répondent ; vrai quand l'une a expiré.
function expireBusy() {
  let expired = false;
  for (const [id, mark] of othersBusy) {
    if (mark.until >= Date.now()) continue;
    othersBusy.delete(id);
    expired = true;
  }
  return expired;
}

let busyTimer = 0;

/*
    Dit aux autres onglets que cet onglet répond, ou ne répond plus, dans la discussion c, sauf pour une discussion
    privée.  Tant que cet onglet répond dans des discussions enregistrées, il le redit pour chacune d'elles toutes les
    busyBeat millisecondes.
*/
function announceBusy(c, busy) {
  if (c && !c.incognito) broadcast({ type: 'busy', id: c.id, tab: tabId, busy });
  const stored = () => Array.from(generations.values()).filter((g) => !g.c.incognito);
  if (stored().length && !busyTimer) {
    busyTimer = setInterval(() => {
      for (const g of stored()) broadcast({ type: 'busy', id: g.c.id, tab: tabId, busy: true });
    }, busyBeat);
  } else if (!stored().length && busyTimer) {
    clearInterval(busyTimer);
    busyTimer = 0;
  }
}

// État du service et modèles.
function roleModel(role) {
  const s = state.status;
  if (!s || !s.active) return null;
  return s.active[role] || s.active.general || null;
}

function modelInfo(id) {
  if (!state.catalog || !id) return null;
  const same = (m) => m.id === id;
  return (state.catalog.models || []).find(same) || (state.catalog.embedding_models || []).find(same) || null;
}

export function modelName(id) {
  const m = modelInfo(id);
  return m && m.name ? m.name : id;
}

export function loadedModel(id) {
  const list = (state.status && state.status.loaded_models) || [];
  return list.find((m) => m.id === id) || null;
}

// Ce que lit un modèle : "text", et "image" ou "audio" pour les modèles qui les lisent, comme le
// disent le moteur ou le catalogue.
function modelInputs(id) {
  const loaded = loadedModel(id);
  if (loaded && Array.isArray(loaded.inputs) && loaded.inputs.length) return loaded.inputs;
  const m = modelInfo(id);
  return m && Array.isArray(m.inputs) && m.inputs.length ? m.inputs : ['text'];
}

// Ce que lit le modèle d'un rôle ; les pièces jointes suivent.
function roleInputs(role) {
  return modelInputs(roleModel(role));
}

export function readsImages(role) {
  return roleInputs(role).includes('image');
}

function inputsLabel(inputs) {
  return inputs.map((i) => t('input_' + i)).join(', ');
}

/*
    Le sélecteur de fichiers d'un rôle : les documents, lus comme du texte, et les médias que son
    modèle lit, les images tant que room, les places qui leur restent, n'est pas 0.
*/
function acceptFor(role, room) {
  const inputs = roleInputs(role);
  let accept = documentAccept();
  if (inputs.includes('image') && room !== 0) accept += ',image/*';
  if (inputs.includes('audio')) accept += ',audio/*';
  return accept;
}

/*
    Les capacités du modèle d'une fonction : reasoning, reasoningAlways pour un modèle entraîné à raisonner avant chaque
    réponse (sans le raisonnement approfondi, le moteur termine aussitôt son raisonnement), tools, son contexte et son
    contexte de travail (workingContext), 0 tant qu'il n'est pas chargé, et ses vitesses de lecture et d'écriture
    (readRate, writeRate).
*/
function roleCapabilities(role) {
  const id = roleModel(role);
  const loaded = loadedModel(id);
  const read = readRate(id);
  const write = writeRate(id);
  if (loaded) {
    const context = loaded.context || 0;
    return { reasoning: loaded.reasoning !== false, reasoningAlways: loaded.reasoning_always === true,
      tools: loaded.tools !== false, context, working: context ? workingContext(id, context) : 0, read, write };
  }
  const m = modelInfo(id);
  return { reasoning: m ? m.reasoning !== false : true, reasoningAlways: !!(m && m.reasoning_always === true),
    tools: m ? m.tools !== false : true, context: 0, working: 0, read, write };
}

/*
    Si les outils de l'agent et du web peuvent accompagner les questions à un modèle aux capacités caps, reasoning
    disant si le raisonnement approfondi de la discussion est activé : un modèle capable de raisonner ne les appelle
    qu'avec son raisonnement approfondi, pour qu'il raisonne avant chaque appel ; un modèle incapable de raisonner les
    appelle au fil de sa réponse.
*/
function toolsAvailable(caps, reasoning) {
  return !!caps.tools && (!caps.reasoning || !!reasoning);
}

/*
    Pourquoi l'accès au web ne peut pas servir une discussion dont le modèle a les capacités caps, reasoning disant si
    son raisonnement approfondi est activé, '' quand il le peut : un modèle qui n'appelle aucun outil, un modèle capable
    de raisonner sans son raisonnement approfondi, un service mis hors ligne, un accès au web que l'administrateur a
    coupé (le membre "web" de l'état), ou un état qui ne le contient pas.
*/
function webUnavailable(caps, reasoning) {
  if (!caps.tools) return t('webNoTools');
  if (!toolsAvailable(caps, reasoning)) return t('webNeedsReasoning');
  const s = state.status;
  if (!s || !s.web || typeof s.web !== 'object') return t('webUnavailable');
  if (s.web.available) return '';
  return s.offline ? t('webOffline') : t('webOff');
}

/*
    Le fournisseur de la recherche web du service, Wikipédia, dans la langue de l'interface ; '' tant que l'accès au web
    est indisponible ou ne donne pas de recherche (web.search "none").  L'aide du mode agent et la fenêtre « À propos »
    le nomment.
*/
export function searchProvider() {
  const w = state.status && state.status.web;
  if (!w || typeof w !== 'object' || !w.available || w.search !== 'wikipedia') return '';
  return t('provider_wikipedia');
}

/*
    Les outils des requêtes de c et les parties de son message système qui les accompagnent : ceux de l'agent tant qu'il
    est activé, puis ceux du web tant que Web est activé et que le service le propose, web_search quand le service
    propose la recherche dans Wikipédia.  La liste et son ordre restent les mêmes tant que les interrupteurs et le
    fournisseur ne changent pas, pour que le début du prompt reste le même d'une question à la suivante.
*/
function toolSet(c, caps) {
  const reasoning = !!(c.reasoning && caps.reasoning);
  const agent = !!(c.agent && toolsAvailable(caps, reasoning));
  const webOn = !!(c.web && !webUnavailable(caps, reasoning));
  const search = webOn && state.status.web.search === 'wikipedia';
  const definitions = (agent ? tools.definitions : []).concat(webOn ? web.definitions(search) : []);
  return { agent, web: webOn, search, definitions: definitions.length ? definitions : null };
}

/*
    Le travail fait en arrière-plan (titres, résumés, mémoire) va au modèle livré avec Albireo,
    que le service garde chargé, sans raisonnement ; le service le confie au modèle général quand
    il n'y en a pas.  Un modèle qui raisonne toujours voit son raisonnement terminé aussitôt par le
    moteur, hors de la réponse et de son max_tokens.
*/
const backgroundModel = 'albireo-background';

// Le contexte de travail du modèle d'arrière-plan, et ses vitesses de lecture et d'écriture.
export function backgroundContext() {
  const s = state.status;
  const loaded = s && s.background ? loadedModel(s.background) : null;
  if (loaded && loaded.context) return workingContext(loaded.id, loaded.context);
  return roleCapabilities('general').working || 8192;
}

export function backgroundRates() {
  const s = state.status;
  const id = s && s.background && loadedModel(s.background) ? s.background : roleModel('general');
  return { read: readRate(id), write: writeRate(id) };
}

// Vrai quand le modèle d'arrière-plan lit les images, ce dont les descriptions des images des projets ont besoin.
function backgroundReadsImages() {
  const s = state.status;
  return modelInputs((s && s.background) || roleModel('general')).includes('image');
}

// Le texte que le modèle d'arrière-plan écrit pour une consigne et un message.
export function backgroundText(system, user, maxTokens, signal) {
  return backgroundChat([{ role: 'system', content: system }, { role: 'user', content: user }], maxTokens, signal);
}

/*
    Le temps que peut prendre une requête du modèle d'arrière-plan : au-delà, la requête se termine par une
    TimeoutError, ce qui libère le travail qui attend derrière elle (les descriptions des images, la question suivante
    après une compression).
*/
const backgroundTimeout = 10 * 60 * 1000;

/*
    Le texte que le modèle d'arrière-plan écrit après une liste de messages ; signal, quand il est donné, l'arrête
    aussi.  Sans température, le moteur échantillonne comme le conseillent les auteurs du modèle, sans la pénalité de
    présence que certains d'entre eux donnent : les titres, les résumés et les citations de la mémoire reprennent les
    mots de la discussion.
*/
async function backgroundChat(messages, maxTokens, signal, temperature) {
  const body = { model: backgroundModel, reasoning: false, max_tokens: maxTokens, messages, presence_penalty: 0 };
  if (temperature != null) body.temperature = temperature;
  let text = '';
  let finish = null;
  const limit = api.limitedSignal(signal, backgroundTimeout);
  try {
    for await (const ev of api.streamChat(body, limit.signal)) {
      const choice = ev.type === 'chunk' && ev.data.choices && ev.data.choices[0];
      if (!choice) continue;
      if (choice.delta && choice.delta.content) text += choice.delta.content;
      if (choice.finish_reason) finish = choice.finish_reason;
    }
  } finally {
    limit.done();
  }
  return { text: text.trim(), finish };
}

// La lecture de l'état en cours, que partagent les appels faits entre-temps.
let statusPoll = null;

export function refreshStatus() {
  if (!statusPoll) statusPoll = pollStatus().finally(() => { statusPoll = null; });
  return statusPoll;
}

// Si le service était prêt à la lecture précédente de son état (serviceReady).
let serviceWasReady = false;

/*
    Lit l'état du service, avec les modèles des fonctions, que la fenêtre de la zone de
    notification peut changer, et si le service répond.  Un changement redessine aussitôt les
    affichages qui le montrent.  Quand le service devient prêt, à l'ouverture pendant qu'il démarre,
    ou après qu'il n'a pas répondu, le travail qu'une visite précédente ou un onglet fermé a laissé
    reprend : les titres laissés en attente, la mémoire, et les descriptions des images.
*/
async function pollStatus() {
  let changed = false;
  let back = false;
  try {
    const s = await api.status();
    // L'accès au web et le mode hors ligne changent le bouton Web de la zone de saisie et les outils de la question
    // suivante.
    const key = (x) => JSON.stringify([x.active, x.loaded_models, x.state, x.background, x.web, x.offline]);
    changed = !state.status || key(state.status) !== key(s);
    back = state.statusError;
    state.status = s;
    state.statusError = false;
    if (changed || !state.catalog) state.catalog = await api.models(language()).catch(() => state.catalog);
    // Les tokens des images ont été mesurés pour les modèles des fonctions tels qu'ils étaient.
    if (changed) imageTokenCache.clear();
    // Une langue choisie entre-temps dans Albireo, que cette interface suit.
    await languageChanged();
    renderHint();
    // Le modèle d'embedding de la recherche dans les documents ; les documents qui n'ont pas encore
    // reçu de vecteurs sont indexés dès qu'il est là.
    const embedder = (s.loaded_models || []).find((m) => m.embedding && s.active && m.id === s.active.embedding);
    const previous = rag.embeddingModel();
    rag.setEmbeddingModel(embedder ? { id: embedder.id, dimensions: embedder.dimensions } : null);
    if (embedder && (!previous || previous.id !== embedder.id)) indexProjects();
    // Les documents affichés avant que le modèle d'embedding soit connu, ou avec un autre, réaffichent leur état.
    if ((previous ? previous.id : null) !== (embedder ? embedder.id : null)) {
      for (const el of document.querySelectorAll('[data-index-doc]')) showIndexState(el);
    }
  } catch (e) {
    changed = !state.statusError;
    state.statusError = true;
  }
  if ((changed || back || expireBusy()) && state.current) {
    refreshViews();
    lockThread();
  } else {
    renderStatusLine();
  }
  const ready = serviceReady();
  const became = ready && !serviceWasReady;
  serviceWasReady = ready;
  if (became) {
    namePending();
    rememberLater();
  }
  if (back || changed || became) describePending();
}

function renderStatusLine() {
  if (!ui.statusLine) return;
  const s = state.status;
  let led = 'error', text = t('serviceDown');
  if (s && !state.statusError) {
    const stats = s.engine && s.engine.stats;
    const loading = Object.values(s.models_state || {}).some((v) => v === 'loading');
    const model = roleModel(state.current ? state.current.role : 'general');
    if (s.state === 'starting') {
      led = 'busy';
      text = t('serviceStarting');
    } else if (!model) {
      led = 'error';
      text = t('noModel');
    } else if (loading) {
      led = 'busy';
      text = t('modelLoading');
    } else {
      led = stats && stats.active > 0 ? 'busy' : 'ok';
      const tps = stats ? stats.generation_tokens_per_second : 0;
      text = modelName(model) + ' · ' + computeUnit(model)
        + (tps > 0.05 ? ' · ' + t('tokensPerSecond', { n: formatDecimal(tps, 1) }) : '');
    }
  }
  const model = s && roleModel('general');
  ui.statusLine.replaceChildren(h('span', { class: 'led ' + led }), h('span', { class: 'grow', title: text }, text),
    model && modelInfo(model) ? modelHelp(model) : null);
  renderHint();
}

// Où calcule le modèle : le GPU du Mac quand ses passes y vont, le processeur sinon (état du moteur).
function computeUnit(id) {
  const stats = state.status && state.status.engine && state.status.engine.stats;
  const m = stats && (stats.models || []).find((x) => x.id === id);
  return m && m.gpu && m.gpu.state === 'on' ? 'GPU' : 'processeur';
}

// Le « ? » du modèle choisi : sa fiche, comme celles des paramètres.
function modelHelp(id) {
  return h('button', { type: 'button', class: 'svc-help', title: 'Le modèle utilisé', 'aria-label': 'Le modèle utilisé',
    onclick: () => modelSheetDialog(modelInfo(id), { h, dialog, status: () => state.status,
      openModels: () => settingsDialog('modelsTab') }) }, '?');
}

// Sous la zone de saisie : le modèle qui répond, son « ? », et l'avertissement.
function renderHint() {
  if (!ui.hint) return;
  const model = state.status && !state.statusError ? roleModel('general') : null;
  fill(ui.hint, model ? [h('span', { class: 'hint-model' }, modelName(model)), ' ', modelInfo(model) ? modelHelp(model) : null,
    ' · '] : null, hintText());
}

// Listes.
export async function reloadLists() {
  state.conversations = await store.allConversations();
  state.projects = await store.allProjects();
}

export function projectById(id) {
  return state.projects.find((p) => p.id === id) || null;
}

function dateGroup(ts) {
  const d = new Date(ts);
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const day = 86400000;
  if (d >= today) return 'today';
  if (d >= today - day) return 'yesterday';
  if (d >= today - 7 * day) return 'week';
  if (d >= today - 30 * day) return 'month';
  return 'older';
}

// Les listes du panneau latéral montrent au plus ce nombre de projets et de discussions ; les
// autres se trouvent par la recherche.
const sideLimit = 20;
// Les recherches se lancent une fois que l'utilisateur a cessé de taper pendant ce nombre de millisecondes.
const searchDelay = 200;

// Le texte tel que la recherche le compare : en minuscules, sans accents.
function fold(text) {
  return String(text || '').normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
}

function queryTerms(query) {
  return fold(query).split(/\s+/).filter(Boolean);
}

// La langue de l'interface, nommée en anglais pour les consignes du modèle : le français, la seule de ce fork.
const languageNames = { fr: 'French' };

// La langue d'un texte : le français, la seule de ce fork.
function textLanguage() {
  return 'fr';
}

/*
    Les textes repliés de la recherche, gardés avec le texte dont ils sont tirés (WeakMap : message
    ou discussion -> {text, folded}), pour qu'une recherche ne replie que ce qui a changé depuis la
    dernière.
*/
const messageFolds = new WeakMap();
const titleFolds = new WeakMap();

function foldedOf(cache, owner, text) {
  const hit = cache.get(owner);
  if (hit && hit.text === text) return hit.folded;
  const folded = fold(text);
  cache.set(owner, { text, folded });
  return folded;
}

// Le texte des messages d'une discussion, et sa forme repliée faite des messages repliés.
function conversationFolded(c) {
  const messages = c.messages || [];
  const texts = messages.map(messageText);
  return { text: texts.join('\n'), folded: messages.map((m, i) => foldedOf(messageFolds, m, texts[i])).join('\n') };
}

// La première position d'un terme de terms dans text, ou -1 quand l'un d'eux manque.  Un terme
// correspond au début, au milieu ou à la fin d'un mot.
function findTerms(text, terms) {
  return findFolded(fold(text), terms);
}

// findTerms pour un texte déjà replié.
function findFolded(f, terms) {
  let first = -1;
  for (const term of terms) {
    const i = f.indexOf(term);
    if (i < 0) return -1;
    if (first < 0 || i < first) first = i;
  }
  return first;
}

// Une partie du texte autour d'une position, avec les termes marqués.
function excerptHtml(text, at, terms, width) {
  text = String(text || '').replace(/\s+/g, ' ');
  const w = width || 160;
  const start = Math.max(0, at - Math.floor(w / 3));
  const piece = text.slice(start, start + w);
  return (start > 0 ? '…' : '') + markTerms(piece, terms) + (start + w < text.length ? '…' : '');
}

// text en HTML, les termes de la recherche dans <mark>.
function markTerms(text, terms) {
  text = String(text || '');
  const f = fold(text);
  // Le repliage garde la longueur de la plupart des textes ; les marques sont omises pour les autres.
  if (f.length !== text.length || !terms.length) return escapeHtml(text);
  const marked = new Array(text.length).fill(false);
  for (const term of terms) {
    for (let i = f.indexOf(term); i >= 0; i = f.indexOf(term, i + 1)) marked.fill(true, i, i + term.length);
  }
  let out = '';
  let open = false;
  for (let i = 0; i < text.length; ++i) {
    if (marked[i] !== open) {
      out += open ? '</mark>' : '<mark>';
      open = marked[i];
    }
    out += escapeHtml(text[i]);
  }
  return out + (open ? '</mark>' : '');
}

function messageText(m) {
  if (m.role === 'user') return m.content || '';
  return (m.steps || []).map((s) => s.content || '').join('\n');
}

/*
    Les discussions qui contiennent tous les termes d'une requête, quelles que soient leur casse et
    leurs accents, un terme correspondant au début, au milieu ou à la fin d'un mot : celles dont le
    titre les contient d'abord, puis celles dont les messages les contiennent, chaque groupe de la
    plus récente à la plus ancienne.  Chaque résultat dit où se trouvent les termes : {conversation,
    where: 'title', 'question', 'answer' ou 'messages' (plusieurs d'entre eux), at, text, excerpt}.
*/
function searchConversations(query, limit) {
  const terms = queryTerms(query);
  if (!terms.length) return [];
  const titles = [];
  const contents = [];
  for (const c of state.conversations) {
    const title = c.title || '';
    const at = findFolded(foldedOf(titleFolds, c, title), terms);
    if (at >= 0) {
      titles.push({ conversation: c, where: 'title', at, text: title });
      continue;
    }
    // Le premier message qui contient tous les termes, sinon la discussion dans son ensemble.
    let hit = null;
    for (const m of c.messages || []) {
      const text = messageText(m);
      const i = findFolded(foldedOf(messageFolds, m, text), terms);
      if (i >= 0) {
        hit = { conversation: c, where: m.role === 'user' ? 'question' : 'answer', at: i, text, created: m.created };
        break;
      }
    }
    if (!hit) {
      const all = conversationFolded(c);
      if (findFolded(all.folded, terms) >= 0) {
        hit = { conversation: c, where: 'messages', at: all.folded.indexOf(terms[0]), text: all.text };
      }
    }
    if (hit) contents.push(hit);
  }
  const out = titles.concat(contents).slice(0, limit || 100);
  for (const x of out) x.excerpt = String(x.text).replace(/\s+/g, ' ').slice(Math.max(0, x.at - 60), x.at + 120);
  return out;
}

// L'heure de la dernière activité d'un projet : ses propres changements et ceux de ses discussions.
function projectActivity(p) {
  let t0 = p.updated || p.created || 0;
  for (const c of state.conversations) if (c.projectId === p.id && c.updated > t0) t0 = c.updated;
  return t0;
}

/*
    Les projets qui contiennent tous les termes d'une requête, comme searchConversations : ceux
    dont le nom les contient d'abord, puis ceux dont la description, les instructions, les
    documents, les images ou les discussions les contiennent.  where : 'name', 'description',
    'instructions', 'document', 'image' ou 'discussion', avec le nom du document ou de l'image ou le
    titre de la discussion dans label.
*/
function searchProjects(query, limit) {
  const terms = queryTerms(query);
  if (!terms.length) return [];
  const names = [];
  const contents = [];
  for (const p of state.projects) {
    const at = findTerms(p.name, terms);
    if (at >= 0) {
      names.push({ project: p, where: 'name', at, text: p.name });
      continue;
    }
    // Chaque champ avec son texte replié, les longs repliés une seule fois (foldedOf).
    const fields = [['description', p.description || '', ''], ['instructions', p.instructions || '', '']]
      .map(([where, text, label]) => [where, text, label, fold(text)]);
    for (const f of p.files || []) {
      const text = f.name + '\n' + (f.text || '');
      fields.push(['document', text, f.name, foldedOf(messageFolds, f, text)]);
    }
    for (const img of p.images || []) {
      const text = img.name + '\n' + (img.description || '');
      fields.push(['image', text, img.name, foldedOf(messageFolds, img, text)]);
    }
    for (const c of state.conversations) {
      if (c.projectId !== p.id) continue;
      const all = conversationFolded(c);
      fields.push(['discussion', (c.title || '') + '\n' + all.text, c.title || t('untitled'),
        fold(c.title || '') + '\n' + all.folded]);
    }
    for (const [where, text, label, folded] of fields) {
      const i = findFolded(folded, terms);
      if (i >= 0) {
        contents.push({ project: p, where, at: i, text, label });
        break;
      }
    }
  }
  const byActivity = (a, b) => projectActivity(b.project) - projectActivity(a.project);
  return names.sort(byActivity).concat(contents.sort(byActivity)).slice(0, limit || 100);
}

/*
    La fenêtre de la recherche des projets (kind 'projects') ou des discussions : chaque résultat
    montre son titre, l'endroit où les mots ont été trouvés, et le passage qui les contient.
*/
function searchDialog(kind) {
  const projects = kind === 'projects';
  return dialog((box, close) => {
    const input = h('input', { type: 'search', placeholder: t('searchPlaceholder'), 'aria-label': t('searchPlaceholder') });
    const count = h('div', { class: 'help' });
    const list = h('div', { class: 'search-results', role: 'listbox' });
    const open = (hit) => {
      close(true);
      go(projects ? '#/p/' + hit.project.id : '#/c/' + hit.conversation.id);
    };
    const show = () => {
      const terms = queryTerms(input.value);
      const hits = projects ? searchProjects(input.value) : searchConversations(input.value);
      list.replaceChildren();
      if (!terms.length) {
        count.textContent = t('searchHint');
        return;
      }
      count.textContent = tn('resultsCount', hits.length);
      for (const hit of hits) {
        const title = projects ? hit.project.name : (hit.conversation.title || t('untitled'));
        const date = new Date(projects ? projectActivity(hit.project) : hit.conversation.updated);
        const p = projects ? hit.project : (hit.conversation.projectId ? projectById(hit.conversation.projectId) : null);
        const inTitle = hit.where === 'title' || hit.where === 'name';
        const where = t('found_' + hit.where, { name: hit.label || '' });
        list.append(h('div', { class: 'search-result', role: 'option', tabindex: '0', onclick: () => open(hit),
          onkeydown: (e) => { if (e.key === 'Enter') open(hit); } },
          h('div', { class: 'line' },
            p ? h('span', { class: 'dot', style: { background: p.color || projectColors[0] } })
              : h('span', { html: icon(roleIcons[hit.conversation.role] || 'chat') }),
            h('span', { class: 'title', html: inTitle ? markTerms(title, terms) : escapeHtml(title) }),
            h('span', { class: 'date' }, date.toLocaleDateString(language()))),
          h('div', { class: 'where' }, h('span', { class: 'badge' }, where),
            inTitle ? null : h('span', { class: 'excerpt', html: excerptHtml(hit.text, hit.at, terms) }))));
      }
      if (!hits.length) list.append(h('div', { class: 'empty-note' }, t('noResults')));
    };
    let timer = 0;
    input.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(show, searchDelay); });
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        const first = list.querySelector('.search-result');
        if (first) first.click();
      }
    });
    box.classList.add('search-dialog');
    box.append(h('h2', null, projects ? t('searchProjects') : t('searchDiscussions')), input, count, list,
      h('div', { class: 'actions' }, h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('close'))));
    show();
    setTimeout(() => input.focus(), 0);
  });
}

// Panneau latéral.
function renderSidebar() {
  const scroll = ui.sideScroll;
  scroll.replaceChildren();
  if (state.search.trim()) {
    const terms = queryTerms(state.search);
    const hits = searchConversations(state.search);
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t('searchIn')));
    if (!hits.length) section.append(h('div', { class: 'empty-note' }, t('noResults')));
    for (const hit of hits) {
      const item = conversationItem(hit.conversation);
      item.title = hit.where === 'title' ? hit.text : t('found_' + hit.where) + ' : ' + hit.excerpt;
      if (hit.where !== 'title') {
        item.querySelector('.label').innerHTML = escapeHtml(hit.conversation.title || t('untitled'))
          + '<span class="side-excerpt">' + excerptHtml(hit.text, hit.at, terms, 60) + '</span>';
      } else {
        item.querySelector('.label').innerHTML = markTerms(hit.text, terms);
      }
      section.append(item);
    }
    scroll.append(section);
    return;
  }

  const projects = h('div', { class: 'side-section' },
    h('div', { class: 'side-title' }, h('span', null, t('projects')),
      h('span', { class: 'side-tools' },
        h('button', { type: 'button', title: t('searchProjects'), onclick: () => searchDialog('projects'), html: icon('search') }),
        h('button', { type: 'button', title: t('newProject'), onclick: () => newProject(false), html: icon('plus') }))));
  // Un projet se crée d'ici aussi bien que depuis une discussion.
  projects.append(h('div', { class: 'item new-item', onclick: () => newProject(false) },
    h('span', { html: icon('plus') }), h('span', { class: 'label' }, t('newProject'))));
  const recent = state.projects.slice().sort((a, b) => projectActivity(b) - projectActivity(a));
  for (const p of recent.slice(0, sideLimit)) {
    const count = state.conversations.filter((c) => c.projectId === p.id).length;
    projects.append(h('div', {
      class: 'item' + (state.view === 'project' && state.projectId === p.id ? ' active' : ''),
      onclick: () => go('#/p/' + p.id)
    }, h('span', { class: 'dot', style: { background: p.color || projectColors[0] } }),
    h('span', { class: 'label' }, p.name), h('span', { class: 'faint', style: { fontSize: '12px', color: 'var(--text-faint)' } },
      count ? String(count) : '')));
  }
  if (recent.length > sideLimit) {
    projects.append(h('div', { class: 'item more-item', onclick: () => searchDialog('projects') },
      h('span', { html: icon('search') }), h('span', { class: 'label' }, t('searchMoreProjects', { n: recent.length }))));
  }
  scroll.append(projects);

  // Les discussions : les épinglées, puis par date, sideLimit d'entre elles au plus.
  scroll.append(h('div', { class: 'side-section side-head' }, h('div', { class: 'side-title' }, h('span', null, t('discussions')),
    h('span', { class: 'side-tools' },
      h('button', { type: 'button', title: t('searchDiscussions'), onclick: () => searchDialog('discussions'), html: icon('search') })))));
  const pinned = state.conversations.filter((c) => c.pinned);
  const shown = pinned.slice(0, sideLimit);
  if (shown.length) {
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t('pinned')));
    for (const c of shown) section.append(conversationItem(c));
    scroll.append(section);
  }
  let left = sideLimit - shown.length;
  const groups = { today: [], yesterday: [], week: [], month: [], older: [] };
  for (const c of state.conversations) if (!c.pinned) groups[dateGroup(c.updated)].push(c);
  for (const g of Object.keys(groups)) {
    if (!groups[g].length || left <= 0) continue;
    const section = h('div', { class: 'side-section' }, h('div', { class: 'side-title' }, t(g)));
    for (const c of groups[g].slice(0, left)) section.append(conversationItem(c));
    left -= Math.min(left, groups[g].length);
    scroll.append(section);
  }
  if (!state.conversations.length) scroll.append(h('div', { class: 'empty-note' }, t('noDiscussions')));
  if (state.conversations.length > sideLimit) {
    scroll.append(h('div', { class: 'side-section' }, h('div', { class: 'item more-item', onclick: () => searchDialog('discussions') },
      h('span', { html: icon('search') }), h('span', { class: 'label' },
        t('searchMoreDiscussions', { n: state.conversations.length })))));
  }
}

// Vrai quand la discussion c reçoit une réponse, dans cet onglet ou dans un autre.
function answering(c) {
  return !!c && (generations.has(c.id) || answeredElsewhere(c));
}

/*
    Une discussion du panneau latéral.  À sa gauche, un point clignote pendant qu'elle reçoit une réponse, dans cet
    onglet ou dans un autre, et reste bleu une fois sa réponse terminée pendant qu'une autre discussion était affichée,
    jusqu'à ce qu'elle soit ouverte.
*/
function conversationItem(c) {
  const p = c.projectId ? projectById(c.projectId) : null;
  const active = state.view === 'chat' && state.current && state.current.id === c.id;
  const mark = answering(c) ? 'live' : unread.has(c.id) ? 'unread' : '';
  const markText = mark ? t(mark === 'live' ? 'answering' : 'unreadAnswer') : '';
  return h('div', { class: 'item' + (active ? ' active' : ''), onclick: () => go('#/c/' + c.id) },
    mark ? h('span', { class: 'gen-dot ' + mark, title: markText, role: 'img', 'aria-label': markText }) : null,
    p ? h('span', { class: 'dot', title: p.name, style: { background: p.color || projectColors[0] } })
      : h('span', { html: icon(roleIcons[c.role] || 'chat') }),
    h('span', { class: 'label' }, c.title || t('untitled')),
    c.pinned ? h('span', { class: 'pin', html: icon('pin') }) : null,
    h('button', {
      class: 'more', type: 'button', title: '…', html: icon('more'),
      onclick: (e) => { e.stopPropagation(); conversationMenu(c, e.currentTarget); }
    }));
}

/*
    Ce que font le mode agent et l'accès au web, et les outils que chacun d'eux donne au modèle, avec leur usage ; le
    fournisseur de la recherche web en dernier, tant que le service en propose un.
*/
function agentHelp(anchor) {
  const row = (name) => h('div', { class: 'popover-tool' },
    h('span', { html: icon(toolIcons[name] || 'tool') }),
    h('span', null, h('b', null, toolLabel(name)), h('span', { class: 'desc' }, t('toolHint_' + name))));
  const provider = searchProvider();
  showPopover(anchor, [
    h('div', { class: 'head' }, t('agentHelpTitle')),
    h('p', null, t('agentHelp')),
    h('div', { class: 'popover-tools' }, tools.definitions.map((d) => row(d.function.name))),
    h('p', null, t('webHelp')),
    h('div', { class: 'popover-tools' }, web.definitions(true).map((d) => row(d.function.name))),
    provider ? h('p', { class: 'search-provider' }, t('webSearchBy', { provider })) : null
  ]);
}

/*
    La discussion à laquelle cet onglet répond, affichée ou non, et la discussion ouverte sont les copies qui répondent
    et enregistrent : une action du panneau latéral sur elles change cette copie, pour que le prochain enregistrement
    garde l'action.
*/
function liveCopy(c) {
  const g = generations.get(c.id);
  if (g) return g.c;
  return state.current && state.current.id === c.id ? state.current : c;
}

function conversationMenu(entry, anchor) {
  const c = liveCopy(entry);
  // L'exportation attend la réponse en cours, qu'elle contiendrait en partie.
  const busy = answering(c);
  const hint = t('afterAnswer');
  showMenu(anchor, [
    { label: t('rename'), icon: 'edit', action: () => renameConversation(liveCopy(c)) },
    { label: c.pinned ? t('unpin') : t('pin'), icon: 'pin', action: () => togglePin(liveCopy(c)) },
    { label: t('moveToProject'), icon: 'move', action: () => moveMenu(liveCopy(c), anchor) },
    c.projectId ? { label: t('removeFromProject'), icon: 'detach', action: () => setProject(liveCopy(c), null) } : null,
    '-',
    { label: t('exportMarkdown'), icon: 'download', disabled: busy, hint,
      action: () => exportConversation(liveCopy(c), 'md') },
    { label: t('exportJson'), icon: 'archive', disabled: busy, hint,
      action: () => exportConversation(liveCopy(c), 'json') },
    '-',
    { label: t('delete'), icon: 'trash', danger: true, action: () => removeConversation(liveCopy(c)) }
  ].filter(Boolean));
}

function moveMenu(c, anchor) {
  const entries = [{ head: t('moveTo') }];
  for (const p of state.projects) {
    entries.push({
      label: p.name, icon: 'folder', checked: c.projectId === p.id, action: () => setProject(liveCopy(c), p.id)
    });
  }
  entries.push('-', {
    label: t('newProject'), icon: 'plus',
    action: async () => {
      const p = await newProject(true);
      if (p) await setProject(liveCopy(c), p.id);
    }
  });
  if (c.projectId) entries.push({ label: t('none'), icon: 'detach', action: () => setProject(liveCopy(c), null) });
  showMenu(anchor, entries);
}

// Les discussions supprimées dans cet onglet : le travail qui se termine après leur suppression (une
// réponse, un titre, un résumé, les marques de la mémoire) ne les enregistre pas de nouveau.
export const deleted = new Set();

/*
    Enregistre une discussion et prévient les autres onglets.  Renvoie false quand il n'y a rien à
    enregistrer, ou quand la base l'a refusée, l'utilisateur étant prévenu ; la copie en mémoire
    reste alors celle de cet onglet.
*/
async function persist(c) {
  if (!c || c.incognito || deleted.has(c.id)) return false;
  // La réponse en cours d'écriture est enregistrée une fois terminée, en entier.
  const g = generations.get(c.id);
  const live = g && g.turn;
  const record = live && c.messages.includes(live)
    ? Object.assign({}, c, { messages: c.messages.filter((x) => x !== live) }) : c;
  try {
    await store.putConversation(record);
    // Une suppression arrivée pendant l'enregistrement de la discussion reste.
    if (deleted.has(c.id)) {
      await store.deleteConversation(c.id);
      return false;
    }
  } catch (e) {
    storageFailed(e);
    return false;
  }
  const i = state.conversations.findIndex((x) => x.id === c.id);
  if (i >= 0) state.conversations[i] = c;
  else state.conversations.unshift(c);
  state.conversations.sort((a, b) => b.updated - a.updated);
  broadcast({ type: 'conversation', id: c.id });
  return true;
}

async function renameConversation(c) {
  const name = await promptDialog(t('rename'), c.title || '');
  if (name == null || !name.trim()) return;
  const live = liveCopy(c);
  live.title = name.trim();
  // Un titre de l'utilisateur reste : le modèle ne nomme plus la discussion.
  live.titleSource = 'user';
  delete live.titleQuestion;
  await persist(live);
  refreshViews();
}

async function togglePin(c) {
  c.pinned = !c.pinned;
  await persist(c);
  refreshViews();
}

async function setProject(c, projectId) {
  c.projectId = projectId;
  // Une discussion sans messages, pas encore enregistrée, prend le projet avec sa première question.
  if (c.messages.length) await persist(c);
  refreshViews();
}

async function removeConversation(c) {
  const ok = await confirmDialog(t('deleteDiscussion'), t('deleteDiscussionText', { title: c.title || t('untitled') }));
  if (!ok) return;
  deleted.add(c.id);
  unread.delete(c.id);
  // La réponse en cours, affichée ou écrite en arrière-plan, et la compression des échanges s'arrêtent, et le brouillon
  // part.  L'enregistrement de la discussion quitte d'abord la base, puis les passages de ses documents, de la mémoire
  // de la recherche et de la base.
  stopGeneration(c.id);
  cancelCompaction(c.id);
  cancelWarmup(c.id);
  web.forgetPages(c.id);
  forgetDraft(c.id);
  await store.deleteConversation(c.id).catch(storageFailed);
  await forgetOwner(c.id).catch(() => {});
  await images.removeOwner(c.id).catch(() => {});
  broadcast({ type: 'deleted', id: c.id });
  state.conversations = state.conversations.filter((x) => x.id !== c.id);
  if (state.current && state.current.id === c.id) go('#/');
  else refreshViews();
}

// Le nom du fichier d'une discussion : son titre sans les caractères qu'un nom de fichier ne peut pas contenir.
function fileName(c) {
  const name = (c.title || '').replace(/[\\/:*?"<>|\u0000-\u001f\u007f]+/g, '_')
    .replace(/^[\s.]+|[\s.]+$/g, '').slice(0, 80);
  return name || 'discussion';
}

// Les messages d'une discussion en Markdown, sous son titre.
function markdownOf(title, messages) {
  const lines = ['# ' + title, ''];
  for (const m of messages) {
    if (m.role === 'user') {
      lines.push('## ' + (state.settings.userName || t('exportUser')), '', m.content || '');
      for (const a of m.attachments || []) lines.push('', '> ' + a.name + ' (' + formatSize(a.size || 0) + ')');
      for (const img of m.images || []) {
        lines.push('', '> ' + img.name + ' (' + img.width + ' × ' + img.height + ', ' + formatSize(img.size || 0) + ')');
      }
    } else {
      lines.push('## Albireo', '');
      for (const s of m.steps || []) {
        if (s.reasoning) lines.push('<details><summary>' + t('thinking') + '</summary>', '', s.reasoning, '', '</details>', '');
        for (const tc of s.toolCalls || []) lines.push('> ' + tc.name + ' ' + (tc.arguments || ''), '', '```', tc.result || '', '```', '');
        if (s.content) lines.push(s.content);
      }
    }
    lines.push('');
  }
  return lines.join('\n');
}

function exportConversation(c, format) {
  if (format === 'json') download(fileName(c) + '.json', JSON.stringify(c, null, 2), 'application/json');
  else download(fileName(c) + '.md', markdownOf(c.title || t('untitled'), c.messages), 'text/markdown');
}

// Une réponse d'une discussion, avec la question à laquelle elle répond.
function exportAnswer(c, index, format) {
  let q = index - 1;
  while (q >= 0 && c.messages[q].role !== 'user') --q;
  const messages = (q >= 0 ? [c.messages[q]] : []).concat([c.messages[index]]);
  const n = c.messages.slice(0, index + 1).filter((m) => m.role !== 'user').length;
  const name = fileName(c) + '-' + n;
  if (format === 'json') {
    const doc = { title: c.title || '', conversation: c.id, exported: new Date().toISOString(), messages };
    download(name + '.json', JSON.stringify(doc, null, 2), 'application/json');
  } else {
    download(name + '.md', markdownOf(c.title || t('untitled'), messages), 'text/markdown');
  }
}

/*
  Supprime une question avec sa réponse, les messages jusqu'à la question suivante, et retire de la
  recherche de la discussion les documents joints à la question.  Le début du contexte suit les
  messages qui restent.
*/
async function deleteQuestion(c, index) {
  if (threadLocked()) return;
  const id = c.messages[index] && c.messages[index].id;
  if (!await confirmDialog(t('deleteQuestionTitle'), t('deleteQuestionText'))) return;
  // La discussion telle qu'elle est une fois la suppression confirmée, qu'un autre onglet a pu changer.
  c = liveCopy(c);
  index = c.messages.findIndex((x) => x.id === id);
  if (index < 0 || threadLocked()) return;
  // Une compression en cours déplace le début de la discussion : elle se termine d'abord.
  if (compactions.has(c.id)) await compactions.get(c.id).done;
  const m = c.messages[index];
  let end = index + 1;
  while (end < c.messages.length && c.messages[end].role !== 'user') ++end;
  const removed = end - index;
  // Les documents et les images de la question partent, sauf si une autre question, une relance, les partage.
  const others = attachmentIds(c.messages.slice(0, index).concat(c.messages.slice(end)));
  for (const a of (m && m.attachments) || []) {
    if ((a.mode === 'search' || a.searchable) && !others.has(a.id)) forgetDocument(c.id, a.id);
  }
  const otherImages = imageIds(c.messages.slice(0, index).concat(c.messages.slice(end)));
  const gone = ((m && m.images) || []).filter((img) => !otherImages.has(img.id)).map((img) => img.id);
  c.messages.splice(index, removed);
  // La mémoire relit à partir de la question qui a pris sa place.
  if ((c.memoryMark || 0) > index) c.memoryMark = Math.max(index, (c.memoryMark || 0) - removed);
  const start = c.contextStart || 0;
  if (start >= end) c.contextStart = start - removed;
  else if (start > index) c.contextStart = index;
  // Une question déjà compressée : le résumé est réécrit à partir des échanges d'avant le début,
  // sans elle.
  if (c.summary && index < start) await rewriteSummary(c);
  c.updated = Date.now();
  await persist(c);
  await images.remove(gone).catch(() => {});
  if (state.current === c) renderMain();
  refreshViews();
}

// Les documents joints aux questions d'une liste de messages, par leurs ids.
function attachmentIds(messages) {
  const ids = new Set();
  for (const x of messages) for (const a of x.attachments || []) ids.add(a.id);
  return ids;
}

/*
    Réécrit le résumé des échanges d'avant le début du contexte, après une suppression parmi eux.  La tâche prend sa
    place dans compactions, marquée always, pour qu'une question envoyée entre-temps attende le nouveau résumé.
*/
async function rewriteSummary(c) {
  const start = c.contextStart || 0;
  c.summary = null;
  if (start === 0 || !roleModel(c.role)) return;
  const caps = roleCapabilities(c.role);
  const context = caps.working || caps.context || 8192;
  const room = context - Math.min(answerReserve(caps, !!(c.reasoning && caps.reasoning)), Math.floor(context / 2));
  toast(t('compacting'));
  cancelWarmup(c.id);
  const abort = new AbortController();
  const done = compact(c, 0, start, room, 1 / 3.6, abort.signal).then((r) => applyCompaction(c, r))
    .catch(() => { c.summary = null; });
  const job = { done, abort, always: true };
  compactions.set(c.id, job);
  try {
    await done;
  } finally {
    if (compactions.get(c.id) === job) compactions.delete(c.id);
  }
  if (c.summary) warmBeginning(c);
}

// La recherche dans les documents : les documents en cours d'indexation, un à la fois en arrière-plan.
const indexing = new Map();   // document -> {owner, done, total, abort}
let indexQueue = Promise.resolve();

function queueIndex(owner, doc, persistent) {
  if (indexing.has(doc.id)) return indexQueue;
  const run = { owner, done: 0, total: 0, abort: new AbortController() };
  indexing.set(doc.id, run);
  showIndexing(doc.id);
  indexQueue = indexQueue.then(() => {
    // Un document retiré pendant son attente n'est pas indexé.
    if (run.abort.signal.aborted) return null;
    return rag.indexDocument(owner, doc, {
      persistent, signal: run.abort.signal,
      onProgress: (done, total) => {
        run.done = done;
        run.total = total;
        showIndexing(doc.id);
      }
    });
  }).catch(() => {}).then(() => {
    if (indexing.get(doc.id) === run) indexing.delete(doc.id);
    showIndexing(doc.id);
  });
  return indexQueue;
}

// Arrête l'indexation, en attente ou en cours, des documents d'un propriétaire, de l'un d'eux,
// ou de tous.
export function cancelIndex(owner, docId) {
  for (const [id, run] of indexing) {
    if ((owner == null || run.owner === owner) && (docId == null || id === docId)) {
      run.abort.abort();
      indexing.delete(id);
    }
  }
}

/*
    Retire de la recherche un document, ou les documents d'un propriétaire (un projet ou une
    discussion) : leur indexation s'arrête, et les passages qu'elle calculait ne sont jamais écrits.
*/
function forgetDocument(owner, docId) {
  cancelIndex(owner, docId);
  return rag.removeDocument(owner, docId);
}

function forgetOwner(owner) {
  cancelIndex(owner);
  return rag.removeOwner(owner);
}

/*
    Supprime les passages des documents qui n'appartiennent plus à leur projet ou à leur
    discussion, comme ceux qu'un autre onglet a réécrits pendant que celui-ci retirait le document.
    indexed liste les documents qui avaient des passages avant la lecture des listes : un document
    est enregistré dans son projet ou sa discussion avant d'être indexé, pour qu'un document indexé
    entre-temps ne soit jamais pris pour un document retiré.
*/
async function pruneIndex(indexed) {
  if (!indexed.length) return;
  const documents = new Map();
  for (const p of state.projects) {
    documents.set(p.id, new Set((p.files || []).map((f) => f.id).concat((p.images || []).map((x) => x.id))));
  }
  for (const c of state.conversations) {
    const ids = new Set();
    for (const m of c.messages || []) for (const a of m.attachments || []) ids.add(a.id);
    documents.set(c.id, ids);
  }
  for (const { owner, doc } of indexed) {
    const ids = documents.get(owner);
    if (!ids || !ids.has(doc)) await rag.removeDocument(owner, doc);
  }
}

/*
    Supprime les images enregistrées auxquelles ni une discussion ni un projet ne renvoie plus, comme
    celles d'une question supprimée dans un onglet fermé entre-temps.  listed sont les images de la
    base, lues avant les listes (images.prune).
*/
function pruneImages(listed) {
  if (!listed.length) return Promise.resolve(0);
  const references = new Map();
  for (const p of state.projects) references.set(p.id, new Set((p.images || []).map((x) => x.id)));
  for (const c of state.conversations) references.set(c.id, imageIds(c.messages || []));
  return images.prune(listed, references);
}

/*
    L'état d'un document sur la page de son projet : son indexation en cours, ou ses passages, cherchés par leurs
    vecteurs, ou par leurs seuls mots tant qu'ils n'ont pas de vecteurs du modèle d'embedding.
*/
async function indexText(owner, docId) {
  const run = indexing.get(docId);
  if (run) return run.total ? t('indexingProgress', { p: Math.floor(100 * run.done / run.total) }) : t('indexing');
  const st = (await rag.status(owner)).get(docId);
  if (!st) return '';
  const key = rag.embeddingModel() && st.vectors === st.passages ? 'indexed' : 'indexedWords';
  return tn(key, st.passages, { n: formatNumber(st.passages) });
}

// Écrit dans el l'état de son document, après un point médian qui le sépare de l'estimation des tokens.
function showIndexState(el) {
  indexText(el.dataset.indexOwner, el.dataset.indexDoc).then((text) => { el.textContent = text ? ' · ' + text : ''; });
}

function showIndexing(docId) {
  for (const el of document.querySelectorAll('[data-index-doc="' + docId + '"]')) showIndexState(el);
}

// Indexe les documents des projets qui n'ont pas de passages, ou pas de vecteurs du modèle
// d'embedding actuel.
export async function indexProjects() {
  for (const p of state.projects) {
    const st = await rag.status(p.id).catch(() => new Map());
    const model = rag.embeddingModel();
    for (const f of p.files || []) {
      const s2 = st.get(f.id);
      if (!s2 || (model && s2.vectors < s2.passages)) queueIndex(p.id, f, true);
    }
    // Une image rejoint la recherche une fois sa description écrite, ou en échec.
    for (const img of p.images || []) {
      const s2 = st.get(img.id);
      if (img.state !== 'pending' && (!s2 || (model && s2.vectors < s2.passages))) queueIndex(p.id, imageDocument(img), true);
    }
  }
}

/*
    Indexe les documents cherchés des questions d'une discussion qui n'ont pas de passages, comme ceux d'une discussion
    importée, puisqu'une exportation ne porte pas de passages, ou ceux qu'un onglet a fermés pendant leur écriture.
*/
export async function indexAttachments(c) {
  if (!c || !Array.isArray(c.messages)) return;
  const docs = new Map();
  for (const m of c.messages) {
    if (m && m.role === 'user') {
      for (const a of m.attachments || []) if (a.mode === 'search' || a.searchable) docs.set(a.id, a);
    }
  }
  if (!docs.size) return;
  const st = await rag.status(c.id);
  for (const a of docs.values()) if (!complete(st.get(a.id))) queueIndex(c.id, a, !c.incognito);
}

/*
    Si les passages d'un document, tels que rag.status() donne leur état, sont tous là avec leurs vecteurs quand le
    service a un modèle d'embedding : un document dont les vecteurs se sont arrêtés (une réponse arrêtée, le service
    occupé, un nouveau modèle d'embedding) est indexé de nouveau, ses passages déjà calculés gardant leurs vecteurs.
*/
function complete(st) {
  if (!st) return false;
  const embedder = rag.embeddingModel();
  return !embedder || st.vectors >= st.passages;
}

/*
    Comment un projet donne ses documents au modèle aux capacités caps : en entier tant qu'ils tiennent dans leur part
    de son contexte de travail et dans leur temps de lecture (wholeTokens), par recherche au-delà, ou pas du tout.  Une
    discussion dont la question ne tenait pas avec les documents lus en entier (c.knowledgeSearch) les cherche.
*/
export function knowledgeMode(p, caps, c) {
  const files = (p && p.files) || [];
  if (!files.length) return 'none';
  if (c && c.knowledgeSearch) return 'search';
  const total = files.reduce((n, f) => n + estimateTokens(f.text), 0);
  return total <= wholeTokens(caps || {}, knowledgeShare, knowledgeSeconds) ? 'whole' : 'search';
}

/*
    Les descriptions des images des projets, que le modèle d'arrière-plan écrit une à une
    (describeImage) : image -> projet, pour les images en attente ou en cours de description par
    cet onglet, et image -> l'AbortController de la description en cours d'écriture.
*/
const describing = new Map();
const describeJobs = new Map();
let describeQueue = Promise.resolve();

// Arrête la description en cours d'écriture d'une image, quand son image ou son projet part.
function cancelDescription(imageId) {
  const job = describeJobs.get(imageId);
  if (job) job.abort();
}

// Arrête le travail du modèle d'arrière-plan en cours dans cet onglet : les compressions et les descriptions.
export function cancelBackground() {
  for (const id of Array.from(compactions.keys())) cancelCompaction(id);
  for (const id of Array.from(describeJobs.keys())) cancelDescription(id);
  for (const id of Array.from(warmups.keys())) cancelWarmup(id);
}

// Le texte d'une image d'un projet pour la recherche : sa description, sinon son nom.
export function imageDocument(img) {
  return { id: img.id, name: img.name, text: img.description || img.name };
}

function queueDescription(projectId, imageId) {
  if (describing.has(imageId)) return;
  describing.set(imageId, projectId);
  showProjectImage(projectId, imageId);
  describeQueue = describeQueue.then(() => describeImage(projectId, imageId)).catch(() => {}).then(() => {
    describing.delete(imageId);
    showProjectImage(projectId, imageId);
  });
}

// Redemande la description d'une image, depuis le bouton de sa ligne.
async function retryDescription(projectId, imageId) {
  if (await updateProjectImage(projectId, imageId, { state: 'pending' })) queueDescription(projectId, imageId);
}

/*
    Décrit les images des projets qui n'ont pas de description : celles dont la description a
    échoué, et celles qu'un onglet fermé a laissées en cours, dès que le service répond avec un
    modèle d'arrière-plan qui lit les images.
*/
export function describePending() {
  if (!serviceReady() || !backgroundReadsImages()) return;
  for (const p of state.projects) {
    for (const img of p.images || []) if (img.state !== 'ready') queueDescription(p.id, img.id);
  }
}

// La demande de description d'une image, écrite dans la langue de l'interface.
function describeMessages(url, name) {
  const lang = languageNames[language()] || 'English';
  return [{
    role: 'system',
    content: 'You describe images for the document search of a project. Write in ' + lang + ', as plain text without '
      + 'introduction, in at most 220 words: first the kind of image (photograph, screenshot, chart, diagram, table, '
      + 'scanned document, drawing...) and its subject, in one sentence; then the visible text, transcribed exactly '
      + 'as written, in its own language; then the elements and the data the image shows: objects, places, values, '
      + 'labels, axes and trends. Describe only what is visible.'
  }, {
    role: 'user',
    content: [{ type: 'image_url', image_url: { url, detail: 'auto' } },
      { type: 'text', text: 'Describe this image ("' + name + '") in ' + lang + '.' }]
  }];
}

// Une description telle que le modèle l'écrit, sans titre, dans la limite de 4 000 caractères.
function cleanDescription(text) {
  // Une description qui répète ses lignes, écrite par un modèle qui a bouclé, garde chacune d'elles une seule fois.
  const label = /^\s*(description|beschreibung|descrizione|descripci[oó]n)\s*:\s*/i;
  const s = distinctLines(String(text || '').replace(label, ''));
  return s.length > 4000 ? keepStart(s, 4000) : s;
}

/*
    Change une image d'un projet dans la copie de cet onglet, que sa page montre et enregistre, et
    dans le projet enregistré, relu pour que les changements des autres onglets restent.  Renvoie
    l'image, null quand le projet ou l'image a été retiré entre-temps.
*/
async function updateProjectImage(projectId, imageId, change) {
  const p = projectById(projectId);
  const img = p && (p.images || []).find((x) => x.id === imageId);
  if (!img) return null;
  const stored = await store.getProject(projectId).catch(() => null);
  const kept = stored && (stored.images || []).find((x) => x.id === imageId);
  if (stored && !kept) return null;
  Object.assign(img, change);
  try {
    if (kept) Object.assign(kept, change);
    await store.putProject(stored || p);
  } catch (e) {
    storageFailed(e);
  }
  broadcast({ type: 'projects' });
  showProjectImage(projectId, imageId);
  return img;
}

/*
    Demande au modèle d'arrière-plan la description d'une image d'un projet, dans la langue de
    l'interface : le genre d'image, son texte visible, ses éléments et ses données.  La description
    est gardée avec l'image et prend sa place dans la recherche du projet.  Sans modèle
    d'arrière-plan qui lit les images, ou après une erreur, le nom de l'image tient lieu de
    description, et la description est redemandée plus tard (describePending) ou avec le bouton de
    l'image.  Un onglet décrit une image à la fois.  La requête se termine quand l'image ou son
    projet part, et après backgroundTimeout, pour que les descriptions qui attendent derrière elle
    et son verrou soient libérés.
*/
async function describeImage(projectId, imageId) {
  await exclusive('albireo-describe-' + imageId, async () => {
    const p = projectById(projectId);
    const img = p && (p.images || []).find((x) => x.id === imageId);
    if (!img) return;
    // Un autre onglet a pu la décrire entre-temps.
    const stored = await store.getProject(projectId).catch(() => null);
    const kept = stored && (stored.images || []).find((x) => x.id === imageId);
    if (kept && kept.state === 'ready') {
      Object.assign(img, kept);
      return;
    }
    let text = '';
    const job = new AbortController();
    describeJobs.set(imageId, job);
    try {
      if (serviceReady() && backgroundReadsImages()) {
        const url = await images.dataUrl(imageId).catch(() => null);
        if (url) {
          try {
            const r = await backgroundChat(describeMessages(url, img.name), 1024, job.signal);
            text = cleanDescription(r.text);
          } catch (e) {
            console.warn('Albireo: the description of an image failed', e);
            text = '';
          }
        }
      }
    } finally {
      describeJobs.delete(imageId);
    }
    // Une image ou un projet retiré entre-temps ne garde aucun état.
    if (job.signal.aborted) return;
    // Les passages précédents de l'image quittent la recherche avant que son nouvel état soit enregistré, pour qu'un
    // onglet fermé entre-temps laisse l'image à indexer de nouveau (indexProjects).
    await forgetDocument(projectId, imageId).catch(() => {});
    const saved = await updateProjectImage(projectId, imageId, text
      ? { state: 'ready', description: text, language: language() } : { state: 'failed', description: '' });
    if (saved) queueIndex(projectId, imageDocument(saved), true);
  });
}

// L'état de la description d'une image d'un projet : {cls, label, spin, retry}.
function imageState(img) {
  if (describing.has(img.id) || img.state === 'pending') return { cls: 'pending', label: t('imageDescribing'), spin: true };
  if (img.state === 'ready') return { cls: 'ready', label: t('imageDescribed') };
  return { cls: 'failed', label: t('imageDescriptionFailed'), retry: true };
}

/*
    La ligne d'une image sur la page de son projet : sa vignette, son nom et sa taille, l'état de sa
    description (en cours, prête, en échec avec le bouton qui la redemande), la description, et le
    bouton qui retire l'image.
*/
function projectImageRow(p, img) {
  const st = imageState(img);
  const row = h('div', { class: 'row image-row', 'data-project-image': img.id },
    imageThumb(img, { list: p.images || [] }),
    h('div', { class: 'image-text' },
      h('div', { class: 'image-name' }, h('span', { class: 'label' }, img.name),
        h('span', { class: 'faint' }, img.width + ' × ' + img.height + ' · ' + formatSize(img.size || 0))),
      h('div', { class: 'image-state ' + st.cls, title: st.retry ? t('imageDescriptionFailedHint') : null },
        st.spin ? h('span', { class: 'spinner small' }) : h('span', { html: icon(st.retry ? 'info' : 'check') }),
        h('span', null, st.label),
        st.retry ? h('button', {
          class: 'button small', type: 'button', onclick: () => retryDescription(p.id, img.id)
        }, t('retry')) : null),
      img.state === 'ready' && img.description
        ? h('p', { class: 'image-description', title: img.description }, img.description) : null),
    iconButton('trash', t('delete'), () => {
      if (ui.projectPage && ui.projectPage.id === p.id) ui.projectPage.removeImage(img);
    }));
  return row;
}

// Affiche l'état d'une image sur la page de son projet, quand celle-ci est affichée.
function showProjectImage(projectId, imageId) {
  if (state.view !== 'project' || state.projectId !== projectId || !ui.content) return;
  const p = projectById(projectId);
  const img = p && (p.images || []).find((x) => x.id === imageId);
  const row = ui.content.querySelector('[data-project-image="' + imageId + '"]');
  if (row && img) row.replaceWith(projectImageRow(p, img));
}

// Projets.
async function newProject(silent) {
  const name = await promptDialog(t('newProject'), '');
  if (name == null || !name.trim()) return null;
  const p = {
    id: store.uid(), name: name.trim(), description: '', instructions: '', files: [],
    color: projectColors[state.projects.length % projectColors.length], created: Date.now(), updated: Date.now()
  };
  try {
    await store.putProject(p);
  } catch (e) {
    storageFailed(e);
    return null;
  }
  state.projects = state.projects.concat([p]).sort((a, b) => a.name.localeCompare(b.name));
  broadcast({ type: 'projects' });
  refreshViews();
  if (!silent) go('#/p/' + p.id);
  return p;
}

/*
    Les modifications des projets dans cet onglet, l'une après l'autre pour chaque projet : task, une fonction async,
    démarre une fois la modification précédente du projet terminée, qu'elle ait réussi ou non.  Renvoie la promesse de
    task.
*/
const projectEdits = new Map();
function queueProjectEdit(id, task) {
  const run = (projectEdits.get(id) || Promise.resolve()).then(task);
  const tail = run.catch(() => {});
  projectEdits.set(id, tail);
  tail.then(() => {
    if (projectEdits.get(id) === tail) projectEdits.delete(id);
  });
  return run;
}

function renderProject(p) {
  const main = ui.content;
  const convs = state.conversations.filter((c) => c.projectId === p.id);
  const name = h('input', { value: p.name });
  const description = h('input', { value: p.description || '' });
  const instructions = h('textarea', { rows: 8 });
  instructions.value = p.instructions || '';
  const heading = h('h2', null, h('span', { class: 'dot', style: { background: p.color } }), h('span', null, p.name));
  const sub = h('p', { class: 'sub' }, p.description || '');
  /*
      Enregistre une modification du projet, edit(project) changeant les champs qu'elle concerne.  La modification
      s'applique au projet tel qu'il est enregistré, relu, pour que ce qu'un autre onglet ou les descriptions des images
      ont écrit entre-temps reste ; les modifications de cet onglet s'exécutent l'une après l'autre
      (queueProjectEdit()), pour que chacune lise ce que la précédente a écrit, et qu'un projet supprimé entre-temps ne
      soit pas réécrit.  Le nom, la description et la couleur s'affichent aussitôt dans la page, la barre du haut, le
      panneau latéral et les autres onglets.
  */
  const save = (edit) => queueProjectEdit(p.id, async () => {
    let stored = p;
    try {
      stored = await store.getProject(p.id);
    } catch (e) {
      // La base est hors de portée : la copie de cet onglet prend la modification.
    }
    if (!stored) return;
    if (stored !== p) {
      for (const k of Object.keys(p)) if (!(k in stored)) delete p[k];
      Object.assign(p, stored);
    }
    edit(p);
    p.updated = Date.now();
    try {
      await store.putProject(p);
    } catch (e) {
      storageFailed(e);
    }
    const i = state.projects.findIndex((x) => x.id === p.id);
    if (i >= 0) state.projects[i] = p;
    state.projects.sort((a, b) => a.name.localeCompare(b.name));
    heading.lastChild.textContent = p.name;
    heading.firstChild.style.background = p.color;
    sub.textContent = p.description || '';
    broadcast({ type: 'projects' });
    renderSidebar();
    renderTopbar();
  });
  name.addEventListener('change', () => save((x) => { x.name = name.value.trim() || x.name; }));
  description.addEventListener('change', () => save((x) => { x.description = description.value; }));
  instructions.addEventListener('change', () => save((x) => { x.instructions = instructions.value; }));
  const colors = h('div', { class: 'colors' });
  for (const col of projectColors) {
    colors.append(h('button', {
      type: 'button', class: p.color === col ? 'on' : '', style: { background: col }, 'aria-label': col,
      onclick: async () => {
        await save((x) => { x.color = col; });
        renderProject(p);
      }
    }));
  }
  /*
    Les documents et les images, maxProjectItems au plus, dont maxProjectImages images : les documents sont indexés, les
    images décrites.  Un projet d'une version antérieure qui en contient davantage les garde, et n'en prend aucun autre
    tant que certains ne sont pas supprimés.
  */
  const items = () => (p.files || []).length + (p.images || []).length;
  const room = () => Math.max(0, maxProjectItems - items());
  const places = () => Math.max(0, maxProjectImages - (p.images || []).length);
  const fileInput = h('input', {
    type: 'file', multiple: true, class: 'hidden', accept: documentAccept() + (places() ? ',image/*' : '')
  });
  // Ajoute au projet les fichiers choisis avec le bouton ou déposés sur la carte de la connaissance.
  const addFiles = async (files) => {
    if (!room()) {
      toast(t('projectItemsFull', { max: maxProjectItems }), true, 9000);
      return;
    }
    const added = await readFiles(files, null, places(), room());
    // Les limites s'appliquent de nouveau au projet tel qu'il est enregistré, qu'un ajout précédent a pu remplir
    // entre-temps.
    const docs = [];
    const pictures = [];
    const late = [];
    const lateImages = [];
    await save((x) => {
      let free = Math.max(0, maxProjectItems - (x.files || []).length - (x.images || []).length);
      let imageFree = Math.max(0, maxProjectImages - (x.images || []).length);
      for (const a of added) {
        const image = a.kind === 'image';
        if (free <= 0) {
          late.push(a.name);
          continue;
        }
        if (image && imageFree <= 0) {
          lateImages.push(a.name);
          continue;
        }
        --free;
        if (!image) {
          docs.push(a);
          continue;
        }
        --imageFree;
        pictures.push(Object.assign(imageRecord(a), { state: 'pending', description: '' }));
      }
      x.files = (x.files || []).concat(docs);
      if (pictures.length) x.images = (x.images || []).concat(pictures);
    });
    if (lateImages.length) toast(imageLimitText(lateImages, places(), true), true, 10000);
    if (late.length) toast(itemLimitText(late, room()), true, 10000);
    // Les images sont enregistrées une fois que le projet qui les contient l'est (pruneImages).
    try {
      await images.keep(p.id, pictures, true);
    } catch (e) {
      storageFailed(e);
    }
    renderProject(p);
    for (const f of docs) queueIndex(p.id, f, true);
    for (const img of pictures) queueDescription(p.id, img.id);
  };
  fileInput.addEventListener('change', () => {
    const files = Array.from(fileInput.files);
    fileInput.value = '';
    return addFiles(files);
  });
  // Chaque document avec son format, l'estimation de ses tokens et l'état de son indexation sur une ligne.
  const fileList = h('div', { class: 'list' });
  for (const f of p.files || []) {
    const tokens = estimateTokens(f.text);
    const indexState = h('span', { class: 'faint index-state', 'data-index-doc': f.id, 'data-index-owner': p.id });
    fileList.append(h('div', { class: 'row' }, formatBadge(f), h('span', { class: 'label' }, f.name),
      h('span', { class: 'faint' }, f.format + ' · ' + tn('tokensEstimate', tokens, { n: formatNumber(tokens) }),
        indexState),
      iconButton('trash', t('delete'), async () => {
        const removed = forgetDocument(p.id, f.id);
        await save((x) => { x.files = (x.files || []).filter((y) => y.id !== f.id); });
        await removed;
        renderProject(p);
      })));
    showIndexState(indexState);
  }
  // Les images, chacune avec l'état de sa description.
  ui.projectPage = {
    id: p.id,
    removeImage: async (img) => {
      cancelDescription(img.id);
      const removed = forgetDocument(p.id, img.id);
      await save((x) => { x.images = (x.images || []).filter((y) => y.id !== img.id); });
      await removed;
      await images.remove([img.id]);
      renderProject(p);
    }
  };
  const pictures = p.images || [];
  const imagesBlock = h('div', { class: 'knowledge-images' },
    h('div', { class: 'images-head' }, h('span', { html: icon('image') }), h('span', null, t('projectImages')),
      h('span', { class: 'count', title: t('projectImagesCount', { n: pictures.length, max: maxProjectImages }) },
        pictures.length + ' / ' + maxProjectImages)),
    h('p', { class: 'help' }, t('knowledgeImagesHelp')),
    pictures.length ? h('div', { class: 'list image-list' }, pictures.map((img) => projectImageRow(p, img))) : null);
  // La part du contexte que prennent les documents, et la façon dont le modèle les lit.
  const general = roleCapabilities('general');
  const contextTokens = general.context || 0;
  const totalTokens = (p.files || []).reduce((n, f) => n + estimateTokens(f.text), 0);
  const capacity = h('p', { class: 'help' });
  if ((p.files || []).length) {
    const mode = knowledgeMode(p, general);
    const share = contextTokens ? Math.round(100 * totalTokens / contextTokens) : 0;
    capacity.textContent = t(mode === 'whole' ? 'knowledgeWhole' : 'knowledgeSearch',
      { p: share, tokens: tn('tokensCount', totalTokens, { n: formatNumber(totalTokens) }) })
      + (rag.embeddingModel() ? '' : ' ' + t('knowledgeNoVectors'));
  }
  const convList = h('div', { class: 'list' });
  for (const c of convs) {
    convList.append(h('div', { class: 'row click', onclick: () => go('#/c/' + c.id) },
      h('span', { html: icon('chat') }), h('span', { class: 'label' }, c.title || t('untitled')),
      h('span', { class: 'faint' }, new Date(c.updated).toLocaleDateString(language()))));
  }
  if (!convs.length) convList.append(h('div', { class: 'empty-note' }, t('noDiscussions')));
  // La carte de la connaissance, qui prend aussi les fichiers déposés sur elle, avec le nombre de ses éléments.
  const full = !room();
  const knowledge = h('div', { class: 'card' },
    h('h3', { html: icon('book') + escapeHtml(t('knowledge')) },
      h('span', { class: 'count', title: t('projectItemsCount', { n: items(), max: maxProjectItems }) },
        items() + ' / ' + maxProjectItems)),
    h('p', { class: 'help' }, t('knowledgeHelp')), capacity, fileList, imagesBlock,
    h('div', { class: 'buttons', style: { marginTop: '10px' } },
      h('button', {
        class: 'button', type: 'button', disabled: full, onclick: (e) => attachMenu(e.currentTarget, fileInput, addFiles),
        title: full ? t('projectItemsFull', { max: maxProjectItems }) : null,
        html: icon('upload') + '<span>' + escapeHtml(t('addFiles')) + '</span>'
      }),
      fileInput));
  dropFiles(knowledge, addFiles);

  main.replaceChildren(h('div', { class: 'page' }, heading, sub,
    h('div', { class: 'buttons', style: { marginBottom: '16px' } },
      h('button', { class: 'button primary', type: 'button', onclick: () => newChat(p.id), html: icon('plus') + '<span>' + escapeHtml(t('startInProject')) + '</span>' })),
    h('div', { class: 'card' },
      h('div', { class: 'field' }, h('label', null, t('projectName')), name),
      h('div', { class: 'field' }, h('label', null, t('description')), description),
      h('div', { class: 'field' }, h('label', null, t('color')), colors)),
    h('div', { class: 'card' }, h('h3', { html: icon('edit') + escapeHtml(t('instructions')) }),
      h('p', { class: 'help' }, t('instructionsHelp')), h('div', { class: 'field' }, instructions)),
    knowledge,
    h('div', { class: 'card' }, h('h3', { html: icon('chat') + escapeHtml(t('projectDiscussions')) }), convList),
    h('div', { class: 'buttons' },
      h('button', {
        class: 'button danger', type: 'button', html: icon('trash') + '<span>' + escapeHtml(t('deleteProject')) + '</span>',
        onclick: async () => {
          if (!await confirmDialog(t('deleteProject'), t('deleteProjectText', { name: p.name }))) return;
          // Les descriptions de ses images s'arrêtent, les documents quittent la recherche, en mémoire et dans
          // la base, et la discussion affichée quitte le projet comme le font les discussions enregistrées.
          for (const img of p.images || []) cancelDescription(img.id);
          await forgetOwner(p.id).catch(() => {});
          await store.deleteProject(p.id).catch(storageFailed);
          await images.removeOwner(p.id).catch(() => {});
          if (state.current && state.current.projectId === p.id) state.current.projectId = null;
          for (const g of generations.values()) if (g.c.projectId === p.id) g.c.projectId = null;
          await reloadLists().catch(storageFailed);
          keepOwnCopies();
          broadcast({ type: 'lists' });
          go('#/');
        }
      }))));
}

// Les documents du Mac (fork pour macOS).
/*
    Chaque fichier joint dans la page va aussi, une fois, dans le dossier des documents du Mac : les fichiers qui
    viennent de ce dossier sont marqués fromMac, et ceux déjà envoyés pendant cette session sont retenus par leur nom,
    leur taille et leur date.  Un échec est signalé, et le fichier reste joint malgré tout.
*/
const sentToMac = new Set();

function copyToMac(f) {
  if (!f || f.fromMac || typeof f.size !== 'number') return;
  const key = [f.name, f.size, f.lastModified].join('|');
  if (sentToMac.has(key)) return;
  sentToMac.add(key);
  fetch('/albireo/v1/documents?name=' + encodeURIComponent(f.name), {
    method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: f
  }).then(async (r) => {
    if (!r.ok) {
      let message = r.status + ' ' + r.statusText;
      try { message = (await r.json()).error.message || message; } catch (e) { /* pas du JSON */ }
      throw new Error(message);
    }
  }).catch((e) => {
    sentToMac.delete(key);
    toast(t('macDocsCopyFailed', { name: f.name, error: e.message }), true, 8000);
  });
}

// Le menu d'un bouton qui joint des fichiers : depuis cet appareil (input), ou depuis les documents du Mac.
function attachMenu(anchor, input, onFiles) {
  showMenu(anchor, [
    { icon: 'upload', label: t('fromDevice'), action: () => input.click() },
    { icon: 'folder', label: t('macDocs'), action: () => macDocumentsDialog(onFiles) }
  ]);
}

/*
    Les documents du Mac : une liste à cocher, les fichiers cochés donnés à onFiles comme objets File marqués fromMac,
    un bouton qui envoie au Mac des fichiers de cet appareil, et la suppression d'un document.
*/
function macDocumentsDialog(onFiles) {
  return dialog((box, close) => {
    const list = h('div', { class: 'mac-docs' }, h('p', { class: 'help' }, '…'));
    const picked = new Set();
    const add = h('button', { class: 'button primary', type: 'button', disabled: true }, t('macDocsAdd'));
    const sendInput = h('input', { type: 'file', multiple: true, class: 'hidden' });
    const date = (sec) => new Intl.DateTimeFormat(language(), { dateStyle: 'medium', timeStyle: 'short' })
      .format(new Date(sec * 1000));
    const refresh = async () => {
      let r;
      try {
        r = await api.getJSON('/albireo/v1/documents');
      } catch (e) {
        list.replaceChildren(h('p', { class: 'help' }, e.message));
        return;
      }
      const docs = (r.documents || []).sort((a, b) => b.modified - a.modified);
      if (!docs.length) {
        list.replaceChildren(h('p', { class: 'help' }, t('macDocsEmpty', { folder: r.folder })));
        return;
      }
      list.replaceChildren(...docs.map((d) => {
        const box = h('input', { type: 'checkbox', checked: picked.has(d.name) });
        box.addEventListener('change', () => {
          if (box.checked) picked.add(d.name);
          else picked.delete(d.name);
          add.disabled = !picked.size;
        });
        return h('label', { class: 'mac-doc' }, box,
          h('span', { class: 'name' }, d.name),
          h('span', { class: 'meta' }, formatSize(d.size) + ' · ' + date(d.modified)),
          iconButton('trash', t('macDocsDelete'), async (e) => {
            e.preventDefault();
            if (!await confirmDialog(t('macDocsDelete'), d.name)) return;
            await fetch('/albireo/v1/documents/' + encodeURIComponent(d.name), { method: 'DELETE' });
            picked.delete(d.name);
            add.disabled = !picked.size;
            refresh();
          }));
      }));
    };
    sendInput.addEventListener('change', async () => {
      const files = Array.from(sendInput.files);
      sendInput.value = '';
      for (const f of files) {
        try {
          const r = await fetch('/albireo/v1/documents?name=' + encodeURIComponent(f.name), {
            method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: f
          });
          if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
          sentToMac.add([f.name, f.size, f.lastModified].join('|'));
          toast(t('macDocsSent', { name: (await r.json()).name || f.name }));
        } catch (e) {
          toast(t('macDocsCopyFailed', { name: f.name, error: e.message }), true, 8000);
        }
      }
      refresh();
    });
    add.addEventListener('click', async () => {
      const files = [];
      for (const name of picked) {
        try {
          const r = await fetch('/albireo/v1/documents/' + encodeURIComponent(name), { cache: 'no-store' });
          if (!r.ok) throw new Error(r.status + ' ' + r.statusText);
          const blob = await r.blob();
          const f = new File([blob], name, { type: blob.type });
          f.fromMac = true;
          files.push(f);
        } catch (e) {
          toast(name + ' : ' + e.message, true);
        }
      }
      close(true);
      if (files.length) onFiles(files);
    });
    box.append(h('h2', { html: icon('folder') + escapeHtml(t('macDocs')) }), h('p', { class: 'help' }, t('macDocsHelp')),
      list, h('div', { class: 'actions' },
        h('button', { class: 'button', type: 'button', onclick: () => sendInput.click(),
          html: icon('upload') + '<span>' + escapeHtml(t('macDocsSend')) + '</span>' }), sendInput,
        h('span', { class: 'grow' }),
        h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('cancel')), add));
    refresh();
  });
}

// Fichiers.
/*
    Lit les fichiers joints pour le modèle de role (les documents et les images d'un projet pour
    none) : les documents deviennent du texte ; une image est jointe quand le modèle lit les images,
    toujours à un projet, et préparée (images.js) dans la limite de room places, les images au-delà
    étant refusées avec un message qui dit pourquoi ; un son n'est joint que quand le modèle le lit.
    Un PDF dont les pages sont des images sans texte, un scan, est laissé de côté avec une boîte de
    dialogue, ou proposé comme images de ses pages à un modèle qui lit les images ; un PDF dont
    certaines pages sont de telles images est ajouté avec un avertissement qui les compte.  Les
    images reviennent avec kind 'image'.  Un document de plus de maxFileBytes et une image de plus
    de images.maxImageBytes sont refusés avant d'être lus, avec un message qui nomme le fichier et
    la limite.
*/
async function readFiles(files, role, room, most) {
  const out = [];
  const scans = [];
  const refused = [];
  const overflow = [];
  const inputs = role ? roleInputs(role) : ['text', 'image'];
  let left = room || 0;
  for (const f of files) {
    // La connaissance d'un projet, qui prend au plus most éléments de plus.
    if (most !== undefined && out.length >= most) {
      overflow.push(f.name);
      continue;
    }
    const kind = fileKind(f);
    if (kind !== 'document' && !inputs.includes(kind)) {
      const model = role ? modelName(roleModel(role)) : '';
      toast(role ? t('inputRefused', { name: f.name, model, inputs: inputsLabel(inputs) })
        : t('unsupported', { name: f.name }), true);
      continue;
    }
    const limit = kind === 'image' ? images.maxImageBytes : maxFileBytes;
    if (f.size > limit) {
      toast(t(kind === 'image' ? 'imageTooLarge' : 'tooLarge',
        { name: f.name, size: formatSize(f.size), limit: formatLimit(limit) }), true, 9000);
      continue;
    }
    // Une copie dans les documents du Mac.
    copyToMac(f);
    if (kind === 'image') {
      if (left <= 0) {
        refused.push(f.name);
        continue;
      }
      try {
        out.push(Object.assign({ kind: 'image' }, await images.prepare(f)));
        --left;
      } catch (e) {
        toast(t('imageUnreadable', { name: f.name }), true);
      }
      continue;
    }
    try {
      const r = await extractText(f);
      out.push({ id: store.uid(), name: f.name, size: f.size, format: r.format, text: r.text, added: Date.now() });
      if (r.unreadablePages) {
        toast(tn('pdfPagesWithoutText', r.unreadablePages, { total: r.pages, name: f.name }), false, 9000);
      }
    } catch (e) {
      if (e.message === 'pdf-images') scans.push({ file: f, pages: e.pages || 0 });
      else if (e.message === 'unsupported') toast(t('unsupported', { name: f.name }), true);
      else if (e.message === 'legacy') toast(t('legacyFormat', { name: f.name }), true);
      else if (e.message === 'too-large') {
        toast(t('tooLarge', { name: f.name, size: formatSize(f.size), limit: formatLimit(maxFileBytes) }), true, 9000);
      }
      else if (e.message === 'too-large-content') toast(t('tooLargeContent', { name: f.name }), true);
      else if (e.message === 'pdf-password') toast(t('pdfPassword', { name: f.name }), true, 9000);
      else if (e.message === 'pdf-invalid') toast(t('pdfInvalid', { name: f.name }), true);
      else if (e.message === 'pdf-reader') toast(t('pdfReader', { name: f.name }), true, 12000);
      else toast(t('readFailed', { name: f.name, error: errorText(e) }), true);
    }
  }
  if (refused.length) toast(imageLimitText(refused, left, !role), true, 10000);
  if (overflow.length) toast(itemLimitText(overflow, most - out.length), true, 10000);
  for (const scan of scans) {
    if (role && inputs.includes('image')) {
      const pages = await scanPages(scan, left);
      out.push(...pages);
      left -= pages.length;
    } else {
      scanDialog(scan.file.name);
    }
  }
  return out;
}

/*
    Propose de joindre les pages d'un PDF scanné comme images, à un modèle qui les lit, dans la
    limite des places restantes pour les images : les pages sont dessinées par pdf.js, leur plus
    grand côté à images.maxSide pixels, les premières quand les places ne les couvrent pas toutes.
    Renvoie les images des pages, aucune quand l'utilisateur refuse ou quand il ne reste aucune
    place, ce que dit la boîte de dialogue.
*/
async function scanPages(scan, left) {
  const name = scan.file.name;
  const count = Math.min(scan.pages || left, left);
  const accepted = await dialog((box, close) => {
    const text = left > 0 ? t('scanOffer', { name, pages: scan.pages || '?', left })
      + (scan.pages > left ? ' ' + tn('scanFirstPages', count) : '')
      : t('scanNoRoom', { name, max: maxContextImages });
    box.append(h('h2', { html: icon('image') + escapeHtml(t('pdfImagesTitle')) }), h('p', null, text),
      h('div', { class: 'actions' }, left > 0 ? [
        h('button', { class: 'button', type: 'button', onclick: () => close(false) }, t('cancel')),
        h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('scanAttach'))
      ] : h('button', { class: 'button primary', type: 'button', onclick: () => close(false) }, t('close'))));
  }, true);
  if (!accepted || count <= 0) return [];
  toast(t('scanRendering', { name }));
  try {
    const r = await pdfPages(scan.file, images.maxSide, count);
    const out = [];
    const base = name.replace(/\.pdf$/i, '');
    for (let i = 0; i < r.canvases.length; ++i) {
      const meta = await images.prepareCanvas(r.canvases[i], t('scanPageName', { name: base, n: i + 1 }));
      out.push(Object.assign({ kind: 'image' }, meta));
    }
    return out;
  } catch (e) {
    toast(t('readFailed', { name, error: errorText(e) }), true);
    return [];
  }
}

// La boîte de dialogue qui dit qu'un PDF d'images n'a pas été ajouté, affichée une fois la précédente fermée.
let scanDialogs = Promise.resolve();

function scanDialog(name) {
  scanDialogs = scanDialogs.then(() => dialog((box, close) => {
    box.append(h('h2', { html: icon('image') + escapeHtml(t('pdfImagesTitle')) }),
      h('p', null, t('pdfImagesText', { name })), h('div', { class: 'actions' },
        h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('close'))));
  }, true));
}

// Images.
// Les ids des images des questions d'une liste de messages.
export function imageIds(messages) {
  const ids = new Set();
  for (const x of messages) for (const img of x.images || []) ids.add(img.id);
  return ids;
}

// Les images des questions de c à partir du message from, le début de son contexte par défaut.
function contextImages(c, from) {
  let n = 0;
  if (!c) return n;
  for (let i = from == null ? c.contextStart || 0 : from; i < c.messages.length; ++i) {
    n += (c.messages[i].images || []).length;
  }
  return n;
}

function pendingImageCount() {
  return state.pending.filter((a) => a.kind === 'image').length;
}

// Les places restantes pour les images dans le contexte de c, les images en attente dans la zone de saisie déduites.
function imageRoom(c) {
  return c ? Math.max(0, maxContextImages - contextImages(c) - pendingImageCount()) : 0;
}

// Pourquoi des images n'ont pas été ajoutées : leurs noms, la limite d'une discussion ou d'un projet, et les places
// restantes.
function imageLimitText(names, left, project) {
  return t(project ? 'projectImagesNotAdded' : 'imagesNotAdded', {
    max: project ? maxProjectImages : maxContextImages, left: Math.max(0, left),
    notAdded: tn('notAddedImages', names.length, { names: names.join(', ') })
  });
}

// Le message des fichiers laissés hors de la connaissance d'un projet, dont les éléments atteignent maxProjectItems.
function itemLimitText(names, left) {
  const n = formatNumber(names.length);
  const notAdded = names.length > 8 ? t('notAddedItemsAmong', { n, names: names.slice(0, 8).join(', ') })
    : tn('notAddedItems', names.length, { n, names: names.join(', ') });
  return t('projectItemsNotAdded', { max: maxProjectItems, left: Math.max(0, left), notAdded });
}

// La description d'une image telle qu'une question ou un projet la garde.
function imageRecord(a) {
  return { id: a.id, name: a.name, type: a.type, size: a.size, width: a.width, height: a.height, added: a.added };
}

function imageLabel(img) {
  return img.name + ' · ' + img.width + ' × ' + img.height + ' · ' + formatSize(img.size || 0);
}

// L'infobulle d'une vignette, avec la raison pour laquelle son image n'atteint plus le modèle.
function thumbTitle(button, outside) {
  button.classList.toggle('outside', outside);
  button.title = button.dataset.label + (outside ? '\n' + t('imageOutside') : '');
  button.setAttribute('aria-label', t('imageOpen', { name: button.dataset.name }) + (outside ? '. ' + t('imageOutside') : ''));
}

/*
    La vignette d'une image, un bouton qui l'ouvre dans la visionneuse.  options : list, les images
    que la visionneuse parcourt ; outside, pour une image dont l'échange a quitté le contexte,
    affichée grisée ; onremove, qui ajoute le bouton qui retire l'image.
*/
function imageThumb(img, options) {
  const o = options || {};
  const list = o.list || [img];
  const pic = h('img', { alt: '', draggable: 'false' });
  const button = h('button', { type: 'button', class: 'image-thumb', onclick: () => openViewer(list, list.indexOf(img), button) },
    pic);
  button.dataset.image = img.id;
  button.dataset.name = img.name;
  button.dataset.label = imageLabel(img);
  thumbTitle(button, !!o.outside);
  images.thumbUrl(img.id).then((url) => {
    if (url) {
      pic.src = url;
      return;
    }
    // Une image que ce navigateur ne contient pas, comme une image dont l'enregistrement a échoué.
    button.classList.add('missing');
    button.dataset.label = img.name + ' · ' + t('imageMissing');
    thumbTitle(button, !!o.outside);
    pic.replaceWith(h('span', { html: icon('image') }));
  });
  if (!o.onremove) return button;
  return h('span', { class: 'image-pending' }, button, h('button', {
    type: 'button', class: 'image-remove', title: t('removeImage'), 'aria-label': t('removeImage') + ': ' + img.name,
    html: icon('close'), onclick: o.onremove
  }));
}

/*
    Affiche des images en entier par-dessus la page, à partir de celle d'indice index dans list.
    Échap, un clic à côté de l'image et le bouton de fermeture ferment la visionneuse, les flèches
    parcourent les images, et le focus reste dans la visionneuse, puis revient à opener.
*/
function openViewer(list, index, opener) {
  closeMenu();
  let at = Math.max(0, index);
  let sequence = 0;
  const pic = h('img', { class: 'viewer-image', alt: '' });
  const missing = h('div', { class: 'viewer-missing hidden' }, h('span', { html: icon('image') }), t('imageMissing'));
  const name = h('span', { class: 'viewer-name' });
  const info = h('span', { class: 'viewer-info' });
  const stage = h('div', { class: 'viewer-stage' }, pic, missing);
  const buttons = [];
  const show = async (k) => {
    at = (k + list.length) % list.length;
    const img = list[at];
    const mine = ++sequence;
    name.textContent = img.name;
    info.textContent = img.width + ' × ' + img.height + ' · ' + formatSize(img.size || 0)
      + (list.length > 1 ? ' · ' + (at + 1) + ' / ' + list.length : '');
    box.setAttribute('aria-label', img.name);
    pic.alt = img.name;
    // L'image que montre la visionneuse garde son URL d'objet (releaseHiddenImages).
    pic.dataset.image = img.id;
    const url = await images.fullUrl(img.id);
    if (mine !== sequence) return;
    pic.classList.toggle('hidden', !url);
    missing.classList.toggle('hidden', !!url);
    if (url) pic.src = url;
    else pic.removeAttribute('src');
  };
  if (list.length > 1) {
    buttons.push(iconButton('chevron', t('previousImage'), () => show(at - 1), 'icon-button viewer-prev'),
      iconButton('chevron', t('nextImage'), () => show(at + 1), 'icon-button viewer-next'));
  }
  const closeButton = iconButton('close', t('close'), () => done(), 'icon-button viewer-close');
  buttons.push(closeButton);
  const box = h('div', { class: 'viewer', role: 'dialog', 'aria-modal': 'true', tabindex: '-1' },
    h('div', { class: 'viewer-bar' }, h('span', { class: 'viewer-title' }, name, info), buttons), stage);
  const overlay = h('div', { class: 'overlay viewer-overlay' }, box);
  // Les touches atteignent d'abord la visionneuse, où que soit le focus, et ne vont pas plus loin.
  const onKey = (e) => {
    if (e.key === 'Escape') done();
    else if (e.key === 'ArrowLeft' && list.length > 1) show(at - 1);
    else if (e.key === 'ArrowRight' && list.length > 1) show(at + 1);
    else if (e.key === 'Tab') {
      const i = buttons.indexOf(document.activeElement);
      buttons[i < 0 ? 0 : (i + (e.shiftKey ? buttons.length - 1 : 1)) % buttons.length].focus();
    } else return;
    e.preventDefault();
    e.stopPropagation();
  };
  const done = () => {
    document.removeEventListener('keydown', onKey, true);
    overlay.remove();
    if (opener && opener.isConnected) opener.focus();
  };
  overlay.addEventListener('mousedown', (e) => {
    if (e.target === overlay || e.target === stage) done();
  });
  document.addEventListener('keydown', onKey, true);
  document.body.append(overlay);
  show(at);
  closeButton.focus();
}

// Grise les images des questions qui ont quitté le contexte, dans le fil affiché.
function markOutside() {
  const c = state.current;
  if (!c || !ui.thread || !ui.thread.isConnected) return;
  const start = c.contextStart || 0;
  for (const el of ui.thread.querySelectorAll('.message.user[data-index]')) {
    const outside = Number(el.dataset.index) < start;
    for (const b of el.querySelectorAll('.image-thumb')) thumbTitle(b, outside);
  }
}

/*
    Les fichiers d'un collage.  Une image du presse-papiers, une capture d'écran par exemple, prend
    un nom avec l'heure du collage au lieu de celui que le navigateur leur donne à toutes.
*/
function pastedFiles(e) {
  const files = Array.from(e.clipboardData ? e.clipboardData.files : []);
  const d = new Date();
  const pad = (n) => String(n).padStart(2, '0');
  const date = d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + '.'
    + pad(d.getMinutes()) + '.' + pad(d.getSeconds());
  return files.map((f, i) => {
    if (fileKind(f) !== 'image' || !/^(image\.[a-z0-9]+)?$/i.test(f.name || '')) return f;
    const ext = ((f.type || '').split('/')[1] || 'png').replace('jpeg', 'jpg');
    const name = t('pastedImage', { date }) + (files.length > 1 ? ' (' + (i + 1) + ')' : '') + '.' + ext;
    return new File([f], name, { type: f.type, lastModified: f.lastModified });
  });
}

// Si le glisser de l'événement e porte des fichiers, venant du système ou d'une autre application.
function carriesFiles(e) {
  return !!e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files');
}

/*
    Fait prendre à el les fichiers déposés sur lui, donnés à onFiles, avec la classe drag sur el pendant que des
    fichiers sont glissés au-dessus.  Le pointeur qui traverse les enfants de el entre dans chaque enfant avant de
    quitter l'élément sur lequel il était : el compte les entrées et les sorties du glisser, et perd la classe une fois
    qu'elles s'équilibrent.  Un glisser sans fichiers, d'un texte ou d'un lien, est laissé au navigateur.
*/
function dropFiles(el, onFiles) {
  let depth = 0;
  const end = () => {
    depth = 0;
    el.classList.remove('drag');
  };
  el.addEventListener('dragenter', (e) => {
    if (!carriesFiles(e)) return;
    e.preventDefault();
    ++depth;
    el.classList.add('drag');
  });
  el.addEventListener('dragover', (e) => {
    if (carriesFiles(e)) e.preventDefault();
  });
  el.addEventListener('dragleave', (e) => {
    if (carriesFiles(e) && --depth <= 0) end();
  });
  el.addEventListener('drop', (e) => {
    if (!carriesFiles(e)) return;
    e.preventDefault();
    end();
    if (e.dataTransfer.files.length) onFiles(Array.from(e.dataTransfer.files));
  });
}

/*
    Un fichier déposé là où rien ne le prend serait ouvert par le navigateur à la place de l'application : hors des
    endroits qui prennent les fichiers (la zone de saisie, la modification d'une question, la connaissance d'un projet),
    qui annulent les événements du glisser avant que la fenêtre les reçoive, un glisser de fichiers montre qu'il ne peut
    pas être déposé, et son dépôt est annulé.
*/
function guardFileDrops() {
  window.addEventListener('dragover', (e) => {
    if (e.defaultPrevented || !carriesFiles(e)) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'none';
  });
  window.addEventListener('drop', (e) => {
    if (!e.defaultPrevented && carriesFiles(e)) e.preventDefault();
  });
}

/*
    Les brouillons de la zone de saisie, un par discussion : le texte tapé et les documents et images en attente
    d'envoi.  La zone de saisie contient le brouillon de la discussion affichée (ui.input, state.pending), drafts ceux
    des autres discussions de cet onglet, par leur clé (draftKey).  La discussion qu'ouvre "New chat" garde son
    brouillon sous newDraft jusqu'à ce que sa première question crée la discussion.  Les textes restent d'un
    rechargement de la page à l'autre dans localStorage, une clé par discussion, retirée une fois sa question envoyée ou
    la discussion supprimée ; les fichiers restent dans la mémoire de l'onglet.  Une discussion privée ne garde aucun
    brouillon.
*/
const drafts = new Map();
const newDraft = 'new';
const draftPrefix = 'albireo-draft-';
// Le texte tapé est gardé ce nombre de millisecondes après la dernière touche.
const draftDelay = 400;
let draftTimer = 0;

// La clé du brouillon de c : son id, newDraft tant qu'elle n'est pas enregistrée, '' pour une discussion privée ou
// supprimée.
function draftKey(c) {
  if (!c || c.incognito || deleted.has(c.id)) return '';
  return c.messages.length || state.conversations.some((x) => x.id === c.id) ? c.id : newDraft;
}

// Le texte du brouillon key que garde localStorage, '' s'il n'y en a pas.
function storedDraft(key) {
  if (!key) return '';
  try {
    return localStorage.getItem(draftPrefix + key) || '';
  } catch (e) {
    return '';
  }
}

// Garde le texte du brouillon key dans localStorage, ou le retire quand le texte ne contient rien à envoyer.
function storeDraft(key, text) {
  if (!key) return;
  try {
    if (String(text || '').trim()) localStorage.setItem(draftPrefix + key, text);
    else localStorage.removeItem(draftPrefix + key);
  } catch (e) {
    // Sans localStorage, ou au-delà de sa place, le texte reste dans la mémoire de l'onglet.
  }
}

// Garde le texte de la zone de saisie pour la discussion affichée une fois que l'utilisateur cesse de taper, aussitôt
// avec now.
function saveDraft(now) {
  clearTimeout(draftTimer);
  draftTimer = 0;
  if (!now) {
    draftTimer = setTimeout(() => saveDraft(true), draftDelay);
    return;
  }
  if (ui.input) storeDraft(draftKey(state.current), ui.input.value);
}

// Les ids des images d'une liste de pièces jointes.
function imageIdsOf(list) {
  return list.filter((a) => a.kind === 'image').map((a) => a.id);
}

// Les images en attente dans les brouillons qui restent une fois tout supprimé : celles de la zone de saisie et de "New
// chat".
export function waitingImages() {
  const d = drafts.get(newDraft);
  return new Set(imageIdsOf(state.pending.concat(d ? d.files : [])));
}

// Oublie le brouillon key, une fois sa question envoyée ou sa discussion supprimée, ici ou dans un autre onglet.
function forgetDraft(key) {
  if (!key) return;
  const d = drafts.get(key);
  if (d) images.drop(imageIdsOf(d.files));
  drafts.delete(key);
  storeDraft(key, '');
}

// Oublie les brouillons des discussions enregistrées, une fois tout supprimé ; celui de "New chat" reste.
export function forgetStoredDrafts() {
  for (const key of Array.from(drafts.keys())) if (key !== newDraft) forgetDraft(key);
  try {
    for (let i = localStorage.length - 1; i >= 0; --i) {
      const k = localStorage.key(i);
      if (k && k.startsWith(draftPrefix) && k !== draftPrefix + newDraft) localStorage.removeItem(k);
    }
  } catch (e) {
    // Sans localStorage, aucun texte n'a été gardé.
  }
}

// Retire les textes gardés pour des discussions qui n'existent plus, comme celles supprimées alors qu'aucun onglet
// n'était ouvert.
function pruneDrafts() {
  const ids = new Set(state.conversations.map((c) => c.id));
  try {
    for (let i = localStorage.length - 1; i >= 0; --i) {
      const k = localStorage.key(i);
      if (!k || !k.startsWith(draftPrefix)) continue;
      const id = k.slice(draftPrefix.length);
      if (id !== newDraft && !ids.has(id)) localStorage.removeItem(k);
    }
  } catch (e) {
    // Sans localStorage, aucun texte n'a été gardé.
  }
}

/*
    Affiche la discussion c à la place de celle qui est affichée, chacune avec son propre brouillon.  Le brouillon de
    celle qui est affichée quitte la zone de saisie pour drafts et localStorage, ou quitte la mémoire pour une
    discussion privée ou supprimée ; le brouillon de c prend sa place : celui que cet onglet garde, sinon le texte de
    localStorage, sinon rien.
*/
function showDiscussion(c) {
  clearTimeout(draftTimer);
  draftTimer = 0;
  const key = draftKey(state.current);
  const text = ui.input ? ui.input.value : '';
  if (key) {
    if (text.trim() || state.pending.length) drafts.set(key, { text, files: state.pending });
    else drafts.delete(key);
    storeDraft(key, text);
  } else {
    images.drop(imageIdsOf(state.pending));
  }
  state.current = c;
  const next = draftKey(c);
  const d = next ? drafts.get(next) : null;
  if (d) drafts.delete(next);
  state.pending = d ? d.files : [];
  if (ui.input) {
    ui.input.value = d ? d.text : storedDraft(next);
    autoGrow();
  }
}

/*
    La discussion affichée cède la place à une autre : une discussion privée, qu'aucune liste ne garde, retire de la
    mémoire ses images, les passages de ses documents et les pages web qu'elle a lues.
*/
function forgetIncognito() {
  const c = state.current;
  if (!c || !c.incognito || !c.messages.length) return;
  cancelWarmup(c.id);
  web.forgetPages(c.id);
  images.removeOwner(c.id).catch(() => {});
  forgetOwner(c.id).catch(() => {});
}

// Garde les images d'une question de c, dans la base du navigateur, ou en mémoire pour une discussion privée.
async function keepImages(c, list) {
  if (!list || !list.length) return;
  try {
    await images.keep(c.id, list, !c.incognito);
  } catch (e) {
    storageFailed(e);
  }
}

/*
    Conversations.  Le raisonnement approfondi, l'agent et l'accès au web démarrent désactivés dans chaque discussion,
    dans un projet ou non, quoi qu'aient fait les précédentes ; chaque discussion garde ensuite son propre choix.
*/
function newConversation(projectId) {
  return {
    id: store.uid(), title: '', projectId: projectId || null, created: Date.now(), updated: Date.now(), pinned: false,
    role: 'general', reasoning: false, agent: false, web: false, messages: [],
    incognito: state.incognito
  };
}

/*
    La discussion affichée cède la place à une autre.  La réponse d'une discussion enregistrée continue en arrière-plan
    ; une discussion privée, qu'aucune liste ne garde, arrête sa réponse, et ses images et les passages de ses documents
    quittent la mémoire.
*/
function leaveCurrent() {
  const c = state.current;
  if (c && c.incognito) stopGeneration(c.id);
  forgetIncognito();
}

// Une nouvelle discussion, que remplit le brouillon de "New chat".
function newChat(projectId) {
  leaveCurrent();
  showDiscussion(newConversation(projectId));
  state.view = 'chat';
  if (location.hash !== '#/') history.pushState(null, '', '#/');
  renderAll();
  ui.input.focus();
}

// La marque d'Albireo : de nouveau la page d'accueil, une nouvelle discussion avec une zone de saisie vide, sans son
// brouillon ni ses pièces jointes.
function goHome() {
  state.incognito = false;
  newChat(null);
  clearTimeout(draftTimer);
  images.drop(imageIdsOf(state.pending));
  state.pending = [];
  forgetDraft(draftKey(state.current));
  ui.input.value = '';
  autoGrow();
  renderAll();
  ui.input.focus();
}

export function go(hash) {
  if (location.hash === hash) route();
  else location.hash = hash;
}

/*
    Le compte des navigations.  Une discussion lue depuis la base n'est affichée que si aucune
    navigation plus récente n'a pris le relais entre-temps.
*/
let routeSequence = 0;

async function route() {
  closeMenu();
  const sequence = ++routeSequence;
  const hash = location.hash || '#/';
  let m;
  if ((m = /^#\/c\/([0-9a-f]+)$/.exec(hash))) {
    if (!(state.current && state.current.id === m[1])) {
      // Une discussion à laquelle cet onglet répond en arrière-plan est affichée avec sa réponse en cours d'écriture.
      const live = generations.get(m[1]);
      const c = live ? live.c : deleted.has(m[1]) ? null : await store.getConversation(m[1]).catch(() => null);
      if (sequence !== routeSequence) return;
      if (!c) { go('#/'); return; }
      leaveCurrent();
      showDiscussion(c);
      state.incognito = false;
      // Les documents cherchés sans passages, ceux d'une discussion importée par exemple, sont indexés.
      indexAttachments(c).catch(() => {});
      // Une discussion dont les échanges prennent la part de leur place qui mène à une compression la lance
      // maintenant, sauf si un autre onglet y répond.
      if (!live && !answeredElsewhere(c) && serviceReady()) compactLater(c);
    }
    // La réponse écrite pendant qu'une autre discussion était affichée est lue maintenant.
    unread.delete(m[1]);
    state.view = 'chat';
  } else if ((m = /^#\/p\/([0-9a-f]+)$/.exec(hash))) {
    state.view = 'project';
    state.projectId = m[1];
  } else if (hash === '#/licenses') {
    state.view = 'licenses';
  } else {
    state.view = 'chat';
    if (!state.current || state.current.messages.length || (state.current.incognito !== state.incognito)) {
      leaveCurrent();
      showDiscussion(newConversation(null));
    }
  }
  renderAll();
}

// Zone principale.
function renderAll() {
  renderSidebar();
  renderTopbar();
  renderMain();
  renderComposerState();
  renderStatusLine();
  ui.incognitoButton.classList.toggle('on', state.incognito);
  ui.banner.classList.toggle('hidden', !(state.view === 'chat' && state.current && state.current.incognito));
}

// L'engrenage de la barre du haut : les réglages, les réglages du service, l'onglet « À propos » et les notes de
// version.
function gearButton() {
  return iconButton('gear', t('settings'), (e) => showMenu(e.currentTarget, [
    { icon: 'gear', label: t('settings'), action: () => settingsDialog('general') },
    { icon: 'spark', label: t('modelsTab'), action: () => settingsDialog('modelsTab') },
    { icon: 'cpu', label: t('serviceTab'), action: () => settingsDialog('serviceTab') },
    { icon: 'info', label: t('about'), action: () => settingsDialog('about') },
    { icon: 'file', label: t('changelog'), action: () => changelogDialog() }
  ]), 'icon-button topbar-gear');
}

function renderTopbar() {
  const bar = ui.topbar;
  bar.replaceChildren();
  bar.append(iconButton('sidebar', t('collapse'), toggleSidebar));
  if (state.view === 'project') {
    const p = projectById(state.projectId);
    bar.append(h('div', { class: 'title' }, p ? p.name : ''), gearButton());
    return;
  }
  if (state.view === 'licenses') {
    bar.append(h('div', { class: 'title' }, t('licensesTitle')), gearButton());
    return;
  }
  const c = state.current;
  const title = h('div', { class: 'title', contenteditable: c && c.messages.length ? 'true' : 'false', spellcheck: 'false' },
    c && c.title ? c.title : t('untitled'));
  title.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); title.blur(); } });
  // Le texte que montre le titre quand l'utilisateur commence à le modifier : le laisser inchangé ne renomme rien, et
  // montre le titre que le modèle a pu écrire entre-temps.
  let shownText = '';
  title.addEventListener('focus', () => { shownText = title.textContent.trim(); });
  title.addEventListener('blur', async () => {
    if (!c || !c.messages.length) return;
    const live = liveCopy(c);
    const v = title.textContent.trim();
    if (v && v !== shownText && v !== live.title) {
      live.title = v;
      // Un titre de l'utilisateur reste : le modèle ne nomme plus la discussion.
      live.titleSource = 'user';
      delete live.titleQuestion;
      await persist(live);
      refreshViews();
    } else {
      title.textContent = live.title || t('untitled');
    }
  });
  bar.append(title);
  if (c && c.incognito) bar.append(h('span', { class: 'chip incognito', html: icon('incognito') + escapeHtml(t('incognito')) }));
  if (c && c.projectId) {
    const p = projectById(c.projectId);
    if (p) {
      bar.append(h('button', {
        class: 'chip', type: 'button', title: t('moveToProject'),
        onclick: (e) => moveMenu(c, e.currentTarget)
      }, h('span', { class: 'swatch', style: { background: p.color } }), p.name));
    }
  }
  // Le fork n'a plus de sélecteur de fonction : le modèle se choisit dans les paramètres (onglet Modèles).
  if (c && c.messages.length) bar.append(iconButton('more', '…', (e) => conversationMenu(c, e.currentTarget)));
  bar.append(gearButton());
}

function toggleSidebar() {
  const open = ui.sidebar.classList.toggle('collapsed') === false;
  saveSetting('sidebar', open);
}

/*
    Dessine la zone principale pour la vue.  Ensuite, les images qu'elle ne montre plus rendent leurs URL d'objet.
*/
function renderMain() {
  drawMain();
  releaseHiddenImages();
}

// Révoque les URL d'objet des images qu'aucun élément de la page ne montre (data-image), visionneuse comprise.
function releaseHiddenImages() {
  images.releaseExcept(new Set(Array.from(document.querySelectorAll('[data-image]'), (el) => el.dataset.image)));
}

function drawMain() {
  stopClock();
  // La page d'accueil met la zone de saisie au milieu de l'écran (welcome-mode) ; les autres vues la laissent en bas.
  ui.main.classList.remove('welcome-mode');
  // Les boutons qui mènent aux extrémités du fil attendent le prochain défilement de l'utilisateur.
  if (ui.jump) ui.jump.classList.remove('on');
  if (state.view === 'project') {
    const p = projectById(state.projectId);
    ui.composerWrap.classList.add('hidden');
    ui.thread = null;
    if (p) renderProject(p);
    // Un projet supprimé entre-temps, dans cet onglet ou dans un autre, quitte sa page.
    else go('#/');
    return;
  }
  if (state.view === 'licenses') {
    ui.composerWrap.classList.add('hidden');
    ui.thread = null;
    renderLicenses();
    return;
  }
  // La zone de saisie de nouveau affichée prend la hauteur de son texte, qu'elle ne pouvait pas mesurer quand elle
  // était cachée.
  const hidden = ui.composerWrap.classList.contains('hidden');
  ui.composerWrap.classList.remove('hidden');
  if (hidden) autoGrow();
  const c = state.current;
  if (!c.messages.length) {
    ui.main.classList.add('welcome-mode');
    renderWelcome();
    return;
  }
  const thread = h('div', { class: 'thread' });
  c.messages.forEach((m, i) => thread.append(messageElement(c, m, i, i === c.messages.length - 1)));
  ui.content.replaceChildren(thread);
  ui.thread = thread;
  showElsewhere();
  startClock();
  scrollToEnd(true);
}

function renderWelcome() {
  const c = state.current;
  const name = state.settings.userName ? ', ' + state.settings.userName : '';
  const p = c.projectId ? projectById(c.projectId) : null;
  let notice = null;
  if (state.statusError) notice = h('div', { class: 'error-box' }, t('serviceDown'));
  else if (state.status && !roleModel(c.role)) notice = h('div', { class: 'error-box' }, t('noModel'));
  // Avec l'agent activé, ce qu'il fait et les outils dont il dispose.
  const caps = roleCapabilities(c.role);
  const agent = c.agent && toolsAvailable(caps, c.reasoning && caps.reasoning)
    ? h('div', { class: 'agent-note' }, h('span', { html: icon('tool') }), h('span', null, t('agentHelp'))) : null;
  ui.content.replaceChildren(h('div', { class: 'welcome' },
    h('img', { class: 'hero', src: 'img/albireo-mark.svg', alt: '' }),
    h('h1', { html: escapeHtml(t('welcome', { name })).replace('Albireo', '<span>Albireo</span>') }),
    h('p', null, p ? t('newProjectDiscussion', { name: p.name }) : t('welcomeSub')),
    notice, agent));
  ui.thread = null;
}

/*
    Le fil suit sa fin tant que l'utilisateur l'y laisse (ui.stick) : une réponse en cours d'écriture, et une zone de
    saisie qui grandit, gardent ses dernières lignes en vue.  La position est écrite sans être lue d'abord ; les
    défilements que fait l'application se distinguent de ceux de l'utilisateur par la position qu'ils laissent
    (ui.ownTop).
*/
function scrollToEnd(force) {
  if (!force && !ui.stick) return;
  const s = ui.content;
  s.scrollTop = s.scrollHeight;
  ui.ownTop = s.scrollTop;
  ui.stick = true;
}

// Un défilement du fil : s'il reste à sa fin, et, pour un défilement de l'utilisateur, les boutons qui mènent à ses
// extrémités.
function threadScrolled() {
  const s = ui.content;
  const own = Math.abs(s.scrollTop - ui.ownTop) < 2;
  if (!own) ui.stick = s.scrollHeight - s.scrollTop - s.clientHeight < 80;
  if (!own) showJump(true);
  else if (ui.jump.classList.contains('on')) showJump(false);
}

/*
    Les boutons qui mènent au début et à la fin du fil s'affichent pendant que l'utilisateur fait défiler un fil plus
    long que la vue, chacun quand on est loin de son extrémité, et partent un moment après le défilement sauf si le
    pointeur ou le focus est sur eux.  keep dit que l'utilisateur fait défiler, ce qui les affiche de nouveau.
*/
const jumpDelay = 1600;

function showJump(keep) {
  const s = ui.content;
  const thread = state.view === 'chat' && ui.thread && ui.thread.isConnected;
  const fits = !thread || s.scrollHeight <= s.clientHeight + 4;
  const atStart = s.scrollTop < 8;
  const atEnd = s.scrollHeight - s.scrollTop - s.clientHeight < 8;
  ui.jumpStart.classList.toggle('hidden', fits || atStart);
  ui.jumpEnd.classList.toggle('hidden', fits || atEnd);
  const on = !fits && !(atStart && atEnd) && (keep || ui.jump.classList.contains('on'));
  ui.jump.classList.toggle('on', on);
  clearTimeout(ui.jumpTimer);
  if (on) ui.jumpTimer = setTimeout(hideJump, jumpDelay);
}

function hideJump() {
  clearTimeout(ui.jumpTimer);
  if (ui.jump.matches(':hover') || ui.jump.contains(document.activeElement)) {
    ui.jumpTimer = setTimeout(hideJump, jumpDelay);
    return;
  }
  ui.jump.classList.remove('on');
}

// Va aussitôt au début ou à la fin du fil.
function jumpTo(end) {
  const s = ui.content;
  if (end) {
    scrollToEnd(true);
  } else {
    s.scrollTop = 0;
    ui.ownTop = s.scrollTop;
    ui.stick = false;
  }
  showJump(true);
}

/*
    La zone de saisie repose sur le bas du fil : la hauteur qu'elle prend, quels que soient son texte, ses pièces
    jointes et les outils de l'agent, va dans --composer-room, que le bas du fil et les boutons au-dessus laissent
    libre.  Un fil à sa fin reste à sa fin.
*/
function watchComposer() {
  if (ui.composerObserver) ui.composerObserver.disconnect();
  ui.composerObserver = null;
  if (typeof ResizeObserver !== 'function') return;
  ui.composerObserver = new ResizeObserver(() => {
    const height = ui.composerWrap.offsetHeight;
    if (height === ui.composerHeight) return;
    ui.composerHeight = height;
    ui.main.style.setProperty('--composer-room', height + 'px');
    if (ui.stick && ui.thread && ui.thread.isConnected) scrollToEnd(true);
  });
  ui.composerObserver.observe(ui.composerWrap);
}

function messageElement(c, m, index, last) {
  if (m.role === 'user') return userElement(c, m, index, last);
  return assistantElement(c, m, index, last);
}

// Le badge d'un document : son format sur la couleur de sa famille.
function formatBadge(a) {
  const ext = extension(a.name || '') || String(a.format || '').toLowerCase();
  const family = /^(docx?|docm|dotx|odt|rtf)$/.test(ext) ? 'word' : /^(xlsx?|xlsm|xltx|ods|csv|tsv)$/.test(ext) ? 'sheet'
    : /^(pptx?|pptm|ppsx|potx|odp)$/.test(ext) ? 'slides' : ext === 'pdf' ? 'pdf'
    : /^(md|markdown|txt|rst)$/.test(ext) ? 'text' : 'code';
  const label = (ext || 'txt').slice(0, 4).toUpperCase();
  return h('span', { class: 'format-badge ' + family, 'aria-hidden': 'true' }, label);
}

function attachmentChip(a, removable, onremove) {
  return h('span', { class: 'attachment', title: a.name },
    formatBadge(a), h('span', { class: 'name' }, a.name),
    h('span', { class: 'size' }, (a.format || '') + (a.size ? ' · ' + formatSize(a.size) : '')
      + (a.mode === 'search' ? ' · ' + t('searchedDocument') : '')),
    removable ? h('button', { type: 'button', title: t('removeAttachment'), 'aria-label': t('removeAttachment'),
      html: icon('close'), onclick: onremove }) : null);
}

function userElement(c, m, index, last) {
  const el = h('div', { class: 'message user' + (last ? ' last' : '') });
  el.dataset.index = index;
  el.dataset.id = m.id || '';
  // Les images d'une question qui a quitté le contexte s'affichent grisées : le modèle ne les reçoit plus.
  const pictures = m.images || [];
  if (pictures.length) {
    const outside = index < (c.contextStart || 0);
    el.append(h('div', { class: 'attachments image-thumbs' }, pictures.map((img) => imageThumb(img, { list: pictures, outside }))));
  }
  if (m.attachments && m.attachments.length) {
    el.append(h('div', { class: 'attachments' }, m.attachments.map((a) => attachmentChip(a))));
  }
  // Une question faite seulement d'images n'affiche pas de bulle vide.
  if (m.content || !pictures.length) el.append(h('div', { class: 'bubble' }, m.content));
  // L'heure de la question s'affiche avant ses boutons, qui apparaissent quand le pointeur est dessus.
  el.append(h('div', { class: 'meta' }, timeElement(m),
    iconButton('copy', t('copy'), () => copyText(m.content)),
    lockable(iconButton('history', t('replayQuestion'), () => replayQuestion(c, m)), 'replay', pictures.length),
    lockable(iconButton('edit', t('edit'), () => editMessage(c, m, index, el)), 'busy'),
    lockable(iconButton('trash', t('deleteQuestion'), () => deleteQuestion(c, index)), 'busy')));
  return el;
}

/*
    L'heure d'un message dans la langue de l'interface : relative dans la journée ("5 minutes
    ago", "just now" dans la minute), la date courte et l'heure au-delà, la date et l'heure
    complètes dans son infobulle.  Un message enregistré sans son heure n'en affiche aucune.
*/
function timeText(ts) {
  const age = Date.now() - ts;
  if (age < 60000) return t('justNow');
  if (age < 86400000) {
    const relative = new Intl.RelativeTimeFormat(language(), { numeric: 'auto' });
    return age < 3600000 ? relative.format(-Math.floor(age / 60000), 'minute')
      : relative.format(-Math.floor(age / 3600000), 'hour');
  }
  return new Intl.DateTimeFormat(language(), { dateStyle: 'short', timeStyle: 'short' }).format(new Date(ts));
}

function timeElement(m) {
  if (typeof m.created !== 'number' || !Number.isFinite(m.created)) return null;
  const full = new Intl.DateTimeFormat(language(), { dateStyle: 'full', timeStyle: 'medium' })
    .format(new Date(m.created));
  return h('time', { class: 'msg-time', datetime: new Date(m.created).toISOString(), title: full,
    'data-time': String(m.created) }, timeText(m.created));
}

// Les heures relatives du fil affiché, réécrites chaque minute tant qu'il reste affiché.
let clockTimer = 0;

function startClock() {
  clearInterval(clockTimer);
  clockTimer = setInterval(() => {
    if (!ui.thread || !ui.thread.isConnected) {
      stopClock();
      return;
    }
    for (const el of ui.thread.querySelectorAll('.msg-time[data-time]')) {
      el.textContent = timeText(Number(el.dataset.time));
    }
  }, 60000);
}

function stopClock() {
  clearInterval(clockTimer);
  clockTimer = 0;
}

/*
    Repose une question du fil : la même question, avec ses documents et ses images, rejoint la fin
    de la discussion comme nouvelle question, à laquelle le modèle répond là.  L'échange précédent
    reste tel quel, les documents gardent leurs passages et les images leurs Blob, que les deux
    questions partagent ; les images reprennent des places dans le contexte.
*/
async function replayQuestion(c, m) {
  const live = liveCopy(c);
  if (threadLocked() || state.current !== live) return;
  const pictures = m.images || [];
  if (pictures.length > imageRoom(live)) {
    toast(replayLimitText(pictures.length), true, 10000);
    return;
  }
  await sendMessage(m.content || '', (m.attachments || []).map((a) => Object.assign({}, a))
    .concat(pictures.map((img) => Object.assign({ kind: 'image' }, img))));
}

// Pourquoi une question avec n images ne peut pas être reposée : les places restantes pour les images dans le contexte.
function replayLimitText(n) {
  return t('replayImagesFull', { n, left: imageRoom(state.current), max: maxContextImages });
}

/*
    Les boutons qui changent une discussion attendent pendant qu'elle reçoit une réponse, ici ou
    dans un autre onglet (kind 'busy'), les boutons d'une réponse attendent qu'elle soit écrite
    (kind 'turn'), et le bouton qui repose une question attend aussi des places pour ses images
    dans le contexte (kind 'replay').  Ils restent désactivés entre-temps, et disent pourquoi.
*/
function threadLocked() {
  return busyHere() || answeredElsewhere(state.current);
}

/*
    Marque un bouton de kind 'busy', de kind 'turn' pour une réponse dont live dit qu'elle est en
    cours d'écriture, ou de kind 'replay' pour une question avec live images.
*/
function lockable(button, kind, live) {
  button.dataset.lock = kind;
  button.dataset.title = button.title;
  if (kind === 'replay') button.dataset.images = String(live || 0);
  lockButton(button, kind === 'turn' ? !!live : threadLocked());
  return button;
}

function lockButton(button, busy) {
  if (button.dataset.lock === 'replay' && !busy) {
    const n = Number(button.dataset.images || 0);
    if (n > imageRoom(state.current)) {
      applyLock(button, true, replayLimitText(n));
      return;
    }
  }
  applyLock(button, busy);
}

function applyLock(button, locked, reason) {
  button.disabled = locked;
  button.title = locked ? reason || t('afterAnswer') : button.dataset.title;
}

// Donne aux boutons du fil affiché l'état de sa discussion.
function lockThread() {
  if (!ui.content) return;
  const busy = threadLocked();
  for (const b of ui.content.querySelectorAll('button[data-lock]')) {
    lockButton(b, b.dataset.lock === 'turn' ? !!b.closest('.message.live') : busy);
  }
  showElsewhere();
}

// La note à la fin du fil pendant qu'un autre onglet répond dans sa discussion.
function showElsewhere() {
  if (!ui.thread || !ui.thread.isConnected) return;
  const note = ui.thread.querySelector(':scope > .elsewhere');
  const on = !busyHere() && answeredElsewhere(state.current);
  if (on && !note) {
    const spin = h('span', { class: 'spinner' });
    ui.thread.append(h('div', { class: 'progress-line elsewhere' }, spin, t('busyElsewhere')));
  } else if (!on && note) {
    note.remove();
  }
}

// Modifie une question : son texte, et ses documents et images joints, qui peuvent être retirés ou ajoutés.
function editMessage(c, m, index, el) {
  if (threadLocked()) return;
  let attachments = (m.attachments || []).concat((m.images || []).map((img) => Object.assign({ kind: 'image' }, img)));
  // Les images préparées pour cette modification, oubliées quand elle est annulée.
  const prepared = new Set();
  /*
      Les places pour les images de la question modifiée : les questions qui la suivent partent,
      les échanges qui la précèdent reviennent dans le contexte quand elle a été résumée, et les
      images en attente dans la zone de saisie gardent les leurs.
  */
  const places = () => maxContextImages - pendingImageCount()
    - (contextImages(c, (c.contextStart || 0) > index ? 0 : c.contextStart || 0) - contextImages(c, index));
  const room = () => Math.max(0, places() - attachments.filter((a) => a.kind === 'image').length);
  const area = h('textarea', { style: { width: '100%', minHeight: '90px', borderRadius: '12px', padding: '10px', border: '1px solid var(--border-strong)', background: 'var(--bg-input)' } });
  area.value = m.content;
  const chips = h('div', { class: 'attachments', style: { justifyContent: 'flex-start', marginBottom: '6px' } });
  const fileInput = h('input', { type: 'file', multiple: true, class: 'hidden' });
  const remove = (a) => {
    attachments = attachments.filter((x) => x !== a);
    if (prepared.has(a.id)) images.drop([a.id]);
    showChips();
  };
  const showChips = () => {
    const pictures = attachments.filter((a) => a.kind === 'image');
    chips.replaceChildren(...pictures.map((a) => imageThumb(a, { list: pictures, onremove: () => remove(a) })),
      ...attachments.filter((a) => a.kind !== 'image').map((a) => attachmentChip(a, true, () => remove(a))));
    fileInput.accept = acceptFor(c.role, room());
  };
  const add = async (files) => {
    const added = await readFiles(files, c.role, room());
    for (const a of added) if (a.kind === 'image') prepared.add(a.id);
    attachments = attachments.concat(added);
    showChips();
  };
  showChips();
  fileInput.addEventListener('change', () => {
    const files = Array.from(fileInput.files);
    fileInput.value = '';
    add(files);
  });
  area.addEventListener('paste', (e) => {
    const files = pastedFiles(e);
    if (files.length) {
      e.preventDefault();
      add(files);
    }
  });
  const box = h('div', { style: { width: '85%' } }, chips, area, h('div', { class: 'buttons', style: { justifyContent: 'flex-end', marginTop: '6px' } },
    h('button', { class: 'button', type: 'button', onclick: (e) => attachMenu(e.currentTarget, fileInput, add), html: icon('clip') + '<span>' + escapeHtml(t('attach')) + '</span>' }),
    fileInput,
    h('span', { class: 'grow' }),
    h('button', {
      class: 'button', type: 'button',
      onclick: () => {
        images.drop(Array.from(prepared));
        renderMain();
      }
    }, t('cancel')),
    h('button', {
      class: 'button primary', type: 'button',
      onclick: async () => {
        if (threadLocked()) return;
        const text = area.value.trim();
        if (!text && !attachments.length) return;
        // Une compression en cours déplace le début de la discussion : elle se termine d'abord.
        if (compactions.has(c.id)) await compactions.get(c.id).done;
        // Une question envoyée entre-temps reçoit une réponse : les messages restent tels quels.
        if (threadLocked() || state.current !== c) return;
        // Les images s'en tiennent aux places du contexte, qu'un autre onglet a pu prendre entre-temps.
        const pictures = attachments.filter((a) => a.kind === 'image');
        const left = places();
        if (pictures.length > left) {
          toast(imageLimitText(pictures.slice(Math.max(0, left)).map((a) => a.name), left), true, 10000);
          return;
        }
        /*
            Les documents qui ne sont plus joints, à cette question ou aux questions qui la
            suivent, et qui partent avec elle, quittent la recherche de la discussion, sauf si une
            question précédente les partage.  La mémoire relit la discussion à partir de la
            question modifiée.
        */
        const kept = attachmentIds(c.messages.slice(0, index));
        for (const a of attachments) kept.add(a.id);
        // Les images des questions qui partent s'en vont aussi, sauf si une question gardée ou la question modifiée les
        // contient.
        const keptImages = imageIds(c.messages.slice(0, index));
        for (const a of pictures) keptImages.add(a.id);
        const gone = [];
        for (const x of c.messages.slice(index)) {
          for (const a of x.attachments || []) {
            if (!kept.has(a.id) && (a.mode === 'search' || a.searchable)) forgetDocument(c.id, a.id);
          }
          for (const img of x.images || []) if (!keptImages.has(img.id)) gone.push(img.id);
        }
        images.remove(gone).catch(() => {});
        c.messages = c.messages.slice(0, index);
        if ((c.memoryMark || 0) > index) c.memoryMark = index;
        if ((c.contextStart || 0) > index) {
          // Les échanges reviennent dans le contexte, sans le résumé de ce qui suit.
          c.contextStart = 0;
          c.summary = null;
        }
        await sendMessage(text, attachments);
      }
    }, t('send'))));
  box.addEventListener('dragover', (e) => e.preventDefault());
  box.addEventListener('drop', (e) => {
    e.preventDefault();
    if (!e.dataTransfer || !e.dataTransfer.files.length) return;
    add(Array.from(e.dataTransfer.files));
  });
  el.replaceChildren(box);
  area.focus();
}

// Le texte d'une réponse, tel qu'il est quand on le copie.
function answerText(m) {
  return (m.steps || []).map((s) => s.content || '').join('\n\n').trim();
}

/*
    Une réponse, avec ses boutons.  Copier et exporter attendent que la réponse soit écrite, qu'elle
    soit affichée au fil de son écriture ou écrite en arrière-plan.
*/
function assistantElement(c, m, index, last) {
  const live = liveMessage(c, m);
  const el = h('div', { class: 'message assistant' + (last ? ' last' : '') + (live ? ' live' : '') });
  el.dataset.id = m.id || '';
  const bubble = h('div', { class: 'bubble' });
  el.append(bubble);
  fillAssistant(bubble, m, live);
  // Les boutons d'une réponse attendent qu'elle soit écrite ; regénérer attend aussi la discussion.
  const meta = h('div', { class: 'meta' });
  meta.append(lockable(iconButton('copy', t('copy'), () => copyText(answerText(m))), 'turn', live));
  if (last) meta.append(lockable(iconButton('refresh', t('regenerate'), () => regenerate(liveCopy(c))), 'busy'));
  const exporter = lockable(iconButton('download', t('exportAnswer'), () => showMenu(exporter, [
    { label: t('exportMarkdown'), icon: 'download', action: () => exportAnswer(liveCopy(c), index, 'md') },
    { label: t('exportJson'), icon: 'archive', action: () => exportAnswer(liveCopy(c), index, 'json') }
  ])), 'turn', live);
  meta.append(exporter);
  // L'heure de la réponse s'affiche après ses boutons.
  const when = timeElement(m);
  if (when) meta.append(when);
  const stats = statsText(m);
  if (stats) meta.append(h('span', { class: 'stats' }, stats));
  el.append(meta);
  el.dataset.index = index;
  return el;
}

function statsText(m) {
  const parts = [];
  if (m.model) parts.push(modelName(m.model));
  if (m.usage && m.usage.completion_tokens) {
    parts.push(tn('tokensCount', m.usage.completion_tokens, { n: formatNumber(m.usage.completion_tokens) }));
  }
  if (m.timings && m.timings.generation_per_second) {
    parts.push(t('tokensPerSecond', { n: formatDecimal(m.timings.generation_per_second, 1) }));
  }
  return parts.join(' · ');
}

// Le HTML des textes terminés des réponses, avec le texte dont il est tiré (WeakMap : step
// -> {text, html}), pour qu'un fil soit redessiné sans analyser de nouveau ses réponses.
const markdownCache = new WeakMap();

function stepHtml(s, live) {
  if (live) return renderMarkdown(s.content, true);
  const hit = markdownCache.get(s);
  if (hit && hit.text === s.content) return hit.html;
  const html = renderMarkdown(s.content, false);
  markdownCache.set(s, { text: s.content, html });
  return html;
}

// Ce que montre une étape, pour savoir si elle doit être redessinée.
function stepKey(s, active) {
  const calls = (s.toolCalls || []).map(toolKey);
  return JSON.stringify([active, (s.reasoning || '').length, (s.content || '').length, calls]);
}

// Ce que montre un appel d'outil, pour savoir s'il doit être redessiné.
function toolKey(tc) {
  return JSON.stringify([tc.status, tc.ok, (tc.arguments || '').length, tc.result == null ? -1 : tc.result.length,
    tc.ms || 0, !!tc.file]);
}

function stepElement(s, active, firstTool) {
  const el = h('div', { class: 'step' });
  if (s.reasoning) el.append(thinkingElement(s, active && !s.content && !(s.toolCalls || []).length));
  if (s.content) {
    const md = h('div', { class: 'md', html: stepHtml(s, active) });
    attachCodeButtons(md, t('copy'));
    el.append(md);
  }
  (s.toolCalls || []).forEach((tc, j) => el.append(toolElement(tc, firstTool + j + 1)));
  return el;
}

/*
    L'étape d'une réponse en cours d'écriture, dessinée sur place.  el est l'élément qu'a produit le dessin précédent,
    ou null.  Le raisonnement reçoit le texte arrivé depuis, le texte de la réponse ses nouveaux blocs (liveMarkdown),
    et un appel d'outil est redessiné quand son état change, pour qu'un dessin ne coûte que les nouveaux tokens, quelle
    que soit la longueur de la réponse.  Renvoie l'élément de l'étape.
*/
function liveStep(el, s, firstTool) {
  if (!el || !el.live) {
    el = h('div', { class: 'step' });
    el.live = { thinking: null, thinkingLive: false, label: null, text: null, shown: 0, md: null, cut: 0, tail: null,
      tailText: null, calls: [] };
  }
  const st = el.live;
  const calls = s.toolCalls || [];
  if (s.reasoning) {
    const thinking = !s.content && !calls.length;
    if (!st.thinking || st.thinkingLive !== thinking) {
      const d = thinkingElement(s, thinking);
      if (st.thinking) st.thinking.replaceWith(d);
      else el.prepend(d);
      st.thinking = d;
      st.thinkingLive = thinking;
      st.label = d.querySelector('summary > .label');
      st.text = d.querySelector('.thought').firstChild;
      st.shown = s.reasoning.length;
    } else {
      if (s.reasoning.length > st.shown) {
        st.text.appendData(s.reasoning.slice(st.shown));
        st.shown = s.reasoning.length;
      }
      if (thinking) st.label.textContent = thinkingLabel(s);
    }
  }
  if (s.content) {
    if (!st.md) {
      st.md = h('div', { class: 'md' });
      el.insertBefore(st.md, st.calls.length ? st.calls[0].el : null);
    }
    liveMarkdown(st, s.content);
  }
  calls.forEach((tc, j) => {
    const key = toolKey(tc);
    const old = st.calls[j];
    if (old && old.key === key) return;
    const wrap = toolElement(tc, firstTool + j + 1);
    if (old) old.el.replaceWith(wrap);
    else el.append(wrap);
    st.calls[j] = { key, el: wrap };
  });
  return el;
}

/*
    Le texte de l'étape en cours d'écriture, dessiné par blocs dans st.md : la partie que les tokens suivants ne
    changent plus (settledEnd) est dessinée une fois, bloc après bloc, et le reste, le bloc en cours d'écriture, est
    redessiné quand il change.  La réponse une fois écrite est redessinée en entier, à partir de son texte entier.
*/
function liveMarkdown(st, text) {
  const cut = settledEnd(text, st.cut);
  if (cut > st.cut) {
    const block = h('div', { class: 'md-block', html: renderMarkdown(text.slice(st.cut, cut), true) });
    attachCodeButtons(block, t('copy'));
    st.md.insertBefore(block, st.tail);
    st.cut = cut;
  }
  const rest = text.slice(st.cut);
  if (rest === st.tailText) return;
  if (!st.tail) {
    st.tail = h('div', { class: 'md-block' });
    st.md.append(st.tail);
  }
  st.tail.innerHTML = renderMarkdown(rest, true);
  attachCodeButtons(st.tail, t('copy'));
  st.tailText = rest;
}

/*
    La fin de la partie d'un texte en cours d'écriture que les tokens suivants ne changent plus : le début de la
    dernière ligne, après from, qui suit une ligne vide et commence à la marge, hors d'un bloc de code et hors d'une
    formule entre $$ ou \[ \].  Une ligne qui commence par une espace peut continuer un élément de liste, et reste avec
    ce qui suit.  Renvoie from quand il n'y a pas de telle ligne.  Le texte d'avant from se termine hors d'un bloc de
    code et d'une formule.
*/
function settledEnd(text, from) {
  let end = from;
  let fence = '';
  let math = false;
  let blank = false;
  for (let at = from; at < text.length;) {
    const nl = text.indexOf('\n', at);
    const line = text.slice(at, nl < 0 ? text.length : nl);
    if (!fence && !math && blank && /^\S/.test(line)) end = at;
    // La dernière ligne, pas encore terminée, dit seulement où commence un bloc.
    if (nl < 0) break;
    if (fence) {
      const close = /^\s*(`{3,}|~{3,})\s*$/.exec(line);
      if (close && close[1][0] === fence[0] && close[1].length >= fence.length) fence = '';
      blank = false;
    } else {
      const open = /^\s*(`{3,}|~{3,})/.exec(line);
      if (open) {
        fence = open[1];
      } else if (((line.match(/\$\$/g) || []).length + (line.match(/\\\[|\\\]/g) || []).length) % 2) {
        math = !math;
      }
      blank = !open && /^\s*$/.test(line);
    }
    at = nl + 1;
  }
  return end;
}

/*
    Remplit le corps d'une réponse : par étape, son raisonnement, son texte et ses appels d'outils,
    puis les notes sur son état.  Pendant l'écriture de la réponse, les étapes déjà dessinées qui
    n'ont pas changé restent telles quelles, l'étape en cours d'écriture est dessinée sur place
    (liveStep), et les notes ne changent que quand ce qu'elles disent change.  Une étape terminée ne
    change plus pendant l'écriture de la réponse : une fois dessinée en entier, elle est laissée
    telle quelle, pour qu'un dessin coûte la même chose quel que soit le nombre d'étapes.
*/
function fillAssistant(bubble, m, live) {
  const steps = m.steps || [];
  const drawn = Array.from(bubble.children).filter((x) => x.classList.contains('step'));
  // Les étapes passent avant les notes.
  const place = (el) => bubble.insertBefore(el, bubble.querySelector(':scope > :not(.step)'));
  let tools = 0;
  steps.forEach((s, k) => {
    const active = live && k === steps.length - 1;
    const first = tools;
    tools += (s.toolCalls || []).length;
    const old = drawn[k] || null;
    if (active) {
      const el = liveStep(old, s, first);
      if (el !== old) {
        if (old) old.replaceWith(el);
        else place(el);
      }
      return;
    }
    if (live && old && !old.live && old.dataset.key) return;
    const key = stepKey(s, false);
    if (old && !old.live && old.dataset.key === key) return;
    const el = stepElement(s, false, first);
    el.dataset.key = key;
    if (old) old.replaceWith(el);
    else place(el);
  });
  for (let k = steps.length; k < drawn.length; ++k) drawn[k].remove();
  const notes = noteElements(m, live, steps);
  const key = notes.map((x) => x.outerHTML).join('');
  if (bubble.notesKey === key) return;
  for (const x of Array.from(bubble.children)) if (!x.classList.contains('step')) x.remove();
  bubble.append(...notes);
  bubble.notesKey = key;
}

// Les notes sur l'état d'une réponse, sous ses étapes.
function noteElements(m, live, steps) {
  const out = [];
  const note = (...children) => out.push(h('div', { class: 'progress-line' }, ...children));
  if (live && m.progress && m.progress.prompt_total > 256 && m.progress.prompt_done < m.progress.prompt_total) {
    const pct = Math.floor(100 * m.progress.prompt_done / m.progress.prompt_total);
    note(h('span', { class: 'spinner' }), t('readingContext', { p: pct }),
      h('div', { class: 'bar' }, h('div', { style: { width: pct + '%' } })));
  } else if (live && m.compacting) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('compacting')));
  } else if (live && m.preparing) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('preparingContext')));
  } else if (live && !steps.some((s) => s.content || s.reasoning || (s.toolCalls || []).length)) {
    note(h('span', { class: 'spinner' }), h('span', { class: 'shimmer' }, t('thinking') + '…'));
  }
  if (m.compacted) note(h('span', { html: icon('info') }), t('contextCompacted'));
  else if (m.trimmed) note(h('span', { html: icon('info') }), t('contextTrimmed'));
  if (m.imagesNamed) note(h('span', { html: icon('image') }), t('imagesNamed', { model: modelName(m.model) }));
  if (m.error) out.push(h('div', { class: 'error-box' }, t('errorPrefix', { message: m.error })));
  if (m.cancelled) note(t('cancelled'));
  if (m.repetition && !m.cancelled) note(h('span', { html: icon('info') }), t('answerRepeated'));
  if (!live && !m.error && !m.cancelled && !steps.some((s) => s.content || (s.toolCalls || []).length)) {
    note(t('emptyAnswer'));
  }
  if (m.agentLimit) note(t('agentLimit', { n: maxAgentSteps }));
  return out;
}

// L'étiquette d'un raisonnement en cours d'écriture, avec le temps qu'il a pris.
function thinkingLabel(s) {
  return t('thinking') + '…' + (s.reasoningStart ? ' ' + formatDuration(Date.now() - s.reasoningStart) : '');
}

function thinkingElement(s, live) {
  const d = h('details', { class: 'thinking' + (live ? ' live' : '') });
  // Fermé, pendant que le modèle réfléchit et après : l'utilisateur l'ouvre, et il reste ensuite
  // comme l'utilisateur l'a laissé.
  if (s.expanded) d.open = true;
  d.addEventListener('toggle', () => { s.expanded = d.open; });
  let label;
  if (live) {
    label = h('span', { class: 'label shimmer' }, thinkingLabel(s));
  } else {
    label = h('span', { class: 'label' },
      s.reasoningMs ? t('thoughtFor', { s: formatDuration(s.reasoningMs) }) : t('thoughtBriefly'));
  }
  d.append(h('summary', null, h('span', { html: icon('spark', 'spark') }), label, h('span', { html: icon('chevron', 'chev') })),
    h('div', { class: 'thought' }, s.reasoning));
  return d;
}

const toolIcons = {
  calculator: 'calc', run_javascript: 'terminal', search_documents: 'search', document_outline: 'archive',
  read_document: 'book', search_discussions: 'history', current_datetime: 'clock', create_file: 'file',
  web_search: 'globe', fetch_page: 'link'
};

// Le nom d'un outil dans la langue de l'interface, son propre nom pour un outil qui n'en a pas.
function toolLabel(name) {
  const key = 'tool_' + name;
  const label = t(key);
  return label === key ? String(name || '') : label;
}

// Les mots qui résument les arguments d'un appel d'outil, pour la ligne de l'appel.
function toolBrief(tc) {
  let a = null;
  try {
    a = JSON.parse(tc.arguments || '{}');
  } catch (e) {
    return String(tc.arguments || '').replace(/\s+/g, ' ').slice(0, 160);
  }
  if (!a || typeof a !== 'object') return '';
  const text = (v) => (typeof v === 'string' ? v : v == null ? '' : JSON.stringify(v));
  const quoted = (v) => (v ? '"' + text(v) + '"' : '');
  switch (tc.name) {
    case 'calculator': return text(a.expression);
    case 'run_javascript': {
      const lines = text(a.code).trim().split('\n');
      return lines[0] + (lines.length > 1 ? ' …' : '');
    }
    case 'search_documents': case 'search_discussions': case 'web_search': return quoted(a.query);
    case 'read_document': return [a.name, a.part].filter(Boolean).map(text).join(' · ');
    case 'fetch_page': return [a.url, a.start].filter(Boolean).map(text).join(' · ');
    case 'document_outline': case 'create_file': return text(a.name);
    default: return Object.values(a).map(text).join(', ').slice(0, 160);
  }
}

// L'état d'un appel d'outil : en cours d'écriture par le modèle, en cours d'exécution, terminé, en échec ou arrêté.
function toolState(tc) {
  if (tc.status === 'pending') return { cls: 'pending', label: t('toolPending'), spin: true };
  if (tc.status === 'running') return { cls: 'running', label: t('toolRunning'), spin: true };
  const time = tc.ms != null ? ' · ' + formatShortDuration(tc.ms) : '';
  if (tc.status === 'stopped') return { cls: 'stopped', label: t('toolStopped') + time };
  return tc.ok === false ? { cls: 'error', label: t('toolError') + time } : { cls: 'ok', label: t('toolDone') + time };
}

/*
    Les mots d'un appel de fetch_page qui a lu sa page (tc.web), ce que la règle des adresses a permis : l'adresse sous
    forme de lien qui s'ouvre dans un nouvel onglet sans la page d'Albireo comme opener ni referrer, puis le début de la
    partie lue.  null pour les autres appels, dont les mots sont du texte.
*/
function pageBrief(tc) {
  const url = tc.name === 'fetch_page' && tc.web ? web.addressKey(tc.web.url) : '';
  if (!url) return null;
  let start = 0;
  try {
    start = parseInt(JSON.parse(tc.arguments || '{}').start, 10) || 0;
  } catch (e) {
    start = 0;
  }
  return [h('a', { href: url, target: '_blank', rel: 'noopener noreferrer', title: url }, url),
    start > 0 ? ' · ' + start : null];
}

/*
    Un appel d'outil de l'agent ou du web, avec son numéro dans la réponse, le nom de l'outil dans
    la langue de l'interface, les mots de ses arguments, un court résultat sur la même ligne, son
    état et sa durée.  Déplié, il montre ses arguments et son résultat entier.
*/
function toolElement(tc, number) {
  const st = toolState(tc);
  const d = h('details', { class: 'tool-call ' + st.cls });
  const open = !!tc.open;
  if (open) d.open = true;
  d.addEventListener('toggle', () => { if (d.open !== open) tc.open = d.open; });
  const brief = pageBrief(tc) || toolBrief(tc);
  // La valeur d'un résultat de l'interpréteur s'affiche sans sa flèche.
  const result = tc.result == null ? '' : String(tc.result).trim().replace(/^=> /, '');
  // Un résultat d'une seule ligne courte s'affiche sur la ligne de l'appel.
  const inline = st.cls === 'ok' && result && result.length <= 60 && !result.includes('\n') ? result : '';
  let args = tc.arguments || '';
  try { args = JSON.stringify(JSON.parse(args), null, 2); } catch (e) { /* gardés tels qu'envoyés */ }
  d.append(h('summary', { title: tc.name },
    number ? h('span', { class: 'tool-number' }, String(number)) : null,
    h('span', { html: icon(toolIcons[tc.name] || 'tool', 'tool-icon') }),
    h('span', { class: 'tool-name' }, toolLabel(tc.name)),
    h('span', { class: 'tool-brief' }, brief, inline ? h('span', { class: 'tool-result' }, ' → ' + inline) : null),
    h('span', { class: 'state ' + st.cls }, st.spin ? h('span', { class: 'spinner small' }) : null, st.label),
    h('span', { html: icon('chevron', 'chev') })),
  h('div', { class: 'io' }, h('h6', null, t('toolInput') + ' · ' + tc.name), h('pre', null, h('code', null, args)),
    tc.result != null ? [h('h6', null, t('toolOutput')), h('pre', null, h('code', null, tc.result))] : null));
  const wrap = h('div', null, d);
  if (tc.file) {
    wrap.append(h('div', { class: 'attachments', style: { justifyContent: 'flex-start' } },
      h('span', {
        class: 'attachment file-out', title: t('download'),
        onclick: () => download(tc.file.name, tc.file.content, mimeOf(tc.file.name))
      }, h('span', { html: icon('download') }), h('span', { class: 'name' }, tc.file.name),
      h('span', { class: 'size' }, t('generatedFile')))));
  }
  return wrap;
}

// Zone de saisie.
function renderComposerState() {
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

function autoGrow() {
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
async function addPending(files) {
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
function generationOf(c) {
  return (c && generations.get(c.id)) || null;
}

// Vrai quand cet onglet écrit une réponse dans la discussion affichée.
function busyHere() {
  return !!generationOf(state.current);
}

// Vrai quand m est la réponse que cet onglet écrit dans la discussion c.
function liveMessage(c, m) {
  const g = generationOf(c);
  return !!g && g.turn === m;
}

// Arrête la réponse que cet onglet écrit dans la discussion id, celle qui est affichée quand id n'est pas donné.
function stopGeneration(id) {
  const g = generations.get(id == null ? state.current && state.current.id : id);
  if (g) g.abort.abort();
}

export function stopAllGenerations() {
  for (const g of generations.values()) g.abort.abort();
}

async function onSend() {
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
  clearTimeout(draftTimer);
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

async function sendMessage(text, attachments) {
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

async function regenerate(c) {
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
async function prepareQuestion(c, m, signal) {
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
async function searchDocumentsOf(c, query, limit) {
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

// Donne une compression à la discussion c.
function applyCompaction(c, r) {
  c.summary = r.summary;
  c.contextStart = r.to;
  indexLeftDocuments(c, r.to);
}

/*
    Indexe pour la recherche les documents lus en entier des questions d'avant le message to de c, qui ont quitté le
    contexte, pour que les extraits des questions suivantes puissent en venir.
*/
function indexLeftDocuments(c, to) {
  for (const m of c.messages.slice(0, to)) {
    if (!m || m.role !== 'user') continue;
    for (const a of m.attachments || []) {
      if (a.mode === 'search' || a.searchable || !a.text) continue;
      a.searchable = true;
      queueIndex(c.id, a, !c.incognito);
    }
  }
}

/*
    La discussion telle qu'elle est maintenant, pour le travail qui se termine en arrière-plan : celle à laquelle cet
    onglet répond, affichée ou non, sinon celle qui est affichée, sinon celle qui est enregistrée.  Null quand elle a
    été supprimée.
*/
async function currentVersion(c) {
  if (deleted.has(c.id)) return null;
  const g = generations.get(c.id);
  if (g) return g.c;
  if (state.current && state.current.id === c.id) return state.current;
  if (c.incognito) return c;
  return (await store.getConversation(c.id)) || null;
}

// Donne une compression des échanges à la discussion c, quand les échanges qu'elle a compressés sont encore ceux de c.
function settleCompaction(c, r) {
  const last = c.messages[r.to - 1];
  if (!last || last.id !== r.last || (c.contextStart || 0) > r.to) return false;
  applyCompaction(c, r);
  return true;
}

/*
    Les compressions des échanges en cours : discussion -> {done, abort, always}, done une promesse qui n'est jamais
    rejetée, abort l'AbortController de ses requêtes, always vrai pour un résumé réécrit après une suppression
    (rewriteSummary), que chaque question attend.
*/
const compactions = new Map();

// Arrête la compression en cours des échanges d'une discussion, quand la discussion part.
function cancelCompaction(id) {
  const job = compactions.get(id);
  if (job) job.abort.abort();
}

// Attend promise, qui n'est jamais rejetée, ou lance une AbortError dès que signal arrête la réponse.
function untilDone(promise, signal) {
  if (!signal) return promise;
  return new Promise((resolve, reject) => {
    const stop = () => reject(new DOMException('The answer was stopped.', 'AbortError'));
    if (signal.aborted) {
      stop();
      return;
    }
    signal.addEventListener('abort', stop, { once: true });
    promise.then((v) => {
      signal.removeEventListener('abort', stop);
      resolve(v);
    });
  });
}

/*
    Le début d'une discussion qui ramène la conversation construite par build à target tokens au plus, ses tokens donnés
    par estimate : la première question après le début actuel pour laquelle elle tient, la dernière question au plus.
    Renvoie -1 quand aucune question ne peut partir.
*/
function nextStart(c, build, estimate, target) {
  const from = c.contextStart || 0;
  const users = [];
  for (let i = from + 1; i < c.messages.length; ++i) if (c.messages[i].role === 'user') users.push(i);
  // La question qui reçoit une réponse reste : la dernière des messages, ou celle d'avant la
  // réponse en cours.
  const lastUser = c.messages.map((m) => m.role).lastIndexOf('user');
  const candidates = users.filter((i) => i <= lastUser);
  if (!candidates.length) return -1;
  const saved = c.contextStart;
  let next = candidates[candidates.length - 1];
  for (const i of candidates) {
    c.contextStart = i;
    if (estimate(build()) <= target) {
      next = i;
      break;
    }
  }
  c.contextStart = saved;
  return next;
}

/*
    Les tokens qu'une conversation garde une fois ses échanges les plus anciens partis : le message système et les
    outils (fixes), et keptShare de la place qu'ils laissent aux échanges.
*/
function keptTokens(fixed, room) {
  return fixed + keptShare * Math.max(0, room - fixed);
}

/*
    Ajuste la conversation au contexte du modèle.  Quand elle ne tient plus, les échanges les plus
    anciens partent d'un seul coup, jusqu'à ce qu'ils prennent keptShare de la place que leur
    laissent le message système et les outils, et le point où elle commence désormais reste avec la
    conversation : les questions suivantes partent de ce même point, pour que le moteur réutilise
    ce qu'il a déjà lu, et la coupe suivante ne vient qu'après plusieurs questions.  Les échanges
    qui partent sont compressés dans le résumé de la discussion, que le modèle reçoit avec la
    première question qui reste (compact) ; les échanges partent sans lui quand le modèle ne peut
    pas l'écrire.  Quand la dernière question seule ne tient pas, les textes les plus anciens
    perdent leur début, à la fin d'une phrase, puis les étapes les plus anciennes de la réponse en
    cours d'écriture partent (shortenOldest).  Le message système et la dernière question restent
    toujours.  onCompact signale le démarrage d'une compression, et model nomme le modèle pour
    lequel les tokens sont comptés.  Renvoie {messages, trimmed, compacted, context, tokens}.

    La réponse de l'agent s'ajuste à son contexte avant chacune de ses étapes, avec les coupes de
    ses étapes précédentes (kept, qu'une compression vide), pour que le début du prompt reste d'une
    étape à la suivante ; une fois ses étapes coupées, un quart de la place des échanges est libéré
    au-delà du nécessaire, pour que les étapes suivantes tiennent sans nouvelle coupe.

    Une compression que l'arrière-plan a lancée après une réponse précédente (compactLater) n'est
    attendue que quand la conversation ne tient pas sans elle, et une compression terminée pendant
    l'écriture de la réponse (generations) s'applique alors ; un résumé en cours de réécriture après
    une suppression (rewriteSummary) est attendu dans tous les cas.  signal met fin à ces attentes
    aussi.
*/
async function fitContext(c, build, toolDefs, reserve, signal, onCompact, model, kept) {
  let trimmed = false;
  let compacted = false;
  const waited = new Set();
  const wait = async (job) => {
    waited.add(job);
    if (onCompact) onCompact();
    await untilDone(job.done, signal);
  };
  const first = compactions.get(c.id);
  if (first && first.always) await wait(first);
  const cuts = kept || { summary: null, texts: new Map(), steps: 0 };
  // Une compression déplace le début de la conversation, et les textes sont de nouveau mesurés en entier.
  const restart = () => {
    cuts.summary = null;
    cuts.texts.clear();
    cuts.steps = 0;
  };
  // Les étapes de la réponse en cours d'écriture que contient la requête, le dernier message de c contenant celle qui
  // est demandée.
  const answer = c.messages[c.messages.length - 1];
  let spare = answer && answer.role === 'assistant' && (answer.steps || []).length > 1 ? 0.25 : 0;
  const counted = model || 'albireo-' + c.role;
  for (let attempt = 0; attempt < 12; ++attempt) {
    const list = build(cuts);
    // Les images que le service n'a pas encore mesurées pour ce modèle, une fois.
    if (!attempt) await measureImages(counted, list);
    let r;
    try {
      r = await api.count(counted, list, toolDefs);
    } catch (e) {
      return { messages: list, trimmed, compacted, context: 0 };
    }
    if (!r || typeof r.tokens !== 'number' || !r.context) return { messages: list, trimmed, compacted, context: 0 };
    const room = r.context - Math.min(reserve, Math.floor(r.context / 2));
    if (r.tokens <= room) return { messages: list, trimmed, compacted, context: r.context, tokens: r.tokens };
    // La conversation ne tient pas : la compression terminée ou en cours déplace son début, et elle est mesurée de
    // nouveau.
    const g = generations.get(c.id);
    if (g && g.compaction) {
      const done = g.compaction;
      g.compaction = null;
      if (settleCompaction(c, done)) {
        compacted = true;
        restart();
        continue;
      }
    }
    const job = compactions.get(c.id);
    if (job && !waited.has(job)) {
      await wait(job);
      continue;
    }
    // Les tokens d'une liste de messages : ses images par leurs tokens, ses textes d'après leurs caractères mis à
    // l'échelle de la mesure.  Le message système et les outils sont mesurés à part, puisqu'ils restent.
    const estimate = tokenEstimate(list, r.tokens, counted);
    const scale = estimate.scale;
    const base = await api.count(counted, list.slice(0, 1), toolDefs).catch(() => null);
    if (signal && signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
    const fixed = base && typeof base.tokens === 'number' ? base.tokens : estimate.of(list.slice(0, 1));
    const next = nextStart(c, () => build(cuts), estimate.of, keptTokens(fixed, room));
    if (next > (c.contextStart || 0)) {
      try {
        if (onCompact) onCompact();
        cancelWarmup(c.id);
        applyCompaction(c, await compact(c, c.contextStart || 0, next, room, scale, signal));
        compacted = true;
      } catch (e) {
        if (e.name === 'AbortError') throw e;
        c.contextStart = next;
        indexLeftDocuments(c, next);
        trimmed = true;
      }
      restart();
      continue;
    }
    // Plus rien ne peut partir en entier : les textes les plus anciens perdent leur début, avec la place de réserve une
    // fois.
    const excess = Math.ceil((r.tokens - room + spare * Math.max(0, room - fixed)) / scale) + 200;
    spare = 0;
    if (!shortenOldest(c, cuts, excess)) {
      return { messages: list, trimmed, compacted, context: r.context, tokens: r.tokens, overflow: true };
    }
    trimmed = true;
  }
  return { messages: build(cuts), trimmed, compacted, context: 0 };
}

/*
    Retire les caractères en trop des textes que reçoit le modèle, les plus anciens d'abord : le
    résumé, les documents et les textes des questions précédentes avec les résultats des outils de
    leurs réponses, puis le raisonnement et les résultats des outils des étapes de la réponse en
    cours d'écriture, le dernier message de c, sauf ceux de sa dernière étape, puis les documents
    et le texte de la question à laquelle elle répond, et les résultats de la dernière étape de la
    réponse en dernier.  cuts contient les caractères que chacun d'eux garde : un texte garde sa fin
    et un résultat son début, 200 caractères au moins, et le raisonnement d'une étape part en
    entier.  Quand les textes d'avant la question n'ont plus rien à donner, les étapes les plus
    anciennes de la réponse partent en entier, la dernière étape de la requête restant
    (cuts.steps).  Renvoie false quand il ne reste rien à retirer.
*/
function shortenOldest(c, cuts, excess) {
  const messages = c.messages.slice(c.contextStart || 0);
  const lastUser = messages.map((m) => m.role).lastIndexOf('user');
  const end = messages[messages.length - 1];
  const answer = end && end.role === 'assistant' && messages.length - 1 > lastUser ? end : null;
  // La requête contient les étapes de la réponse d'avant la dernière, qui est celle qui est demandée.
  const latest = answer && answer.steps.length > 1 ? answer.steps[answer.steps.length - 2] : null;
  const early = [];
  const reasoning = [];
  const results = [];
  const question = [];
  const last = [];
  if (c.summary && c.summary.text && (c.contextStart || 0) > 0) early.push({ key: 'summary', text: c.summary.text });
  messages.forEach((m, i) => {
    if (m.role === 'user') {
      const list = i === lastUser ? question : early;
      for (const a of m.attachments || []) if (a.mode !== 'search') list.push({ key: a.id, text: a.text || '' });
      list.push({ key: m.id, text: m.content || '' });
      return;
    }
    for (const s of m.steps || []) {
      if (m === answer && s.reasoning) reasoning.push({ key: s, text: s.reasoning, whole: true });
      for (const tc of s.toolCalls || []) {
        if (tc.result == null) continue;
        const list = m !== answer ? early : s === latest ? last : results;
        list.push({ key: tc, text: tc.result });
      }
    }
  });
  const keptOf = (key, text) => {
    const kept = key === 'summary' ? cuts.summary : cuts.texts.get(key);
    return kept == null ? text.length : kept;
  };
  const take = (list) => {
    for (const x of list) {
      if (excess <= 0) return;
      const length = keptOf(x.key, x.text);
      // Un texte garde 200 caractères au moins : les dernières phrases d'une question, le début d'un résultat.
      const n = x.whole ? length : Math.min(excess, length - Math.min(length, 200));
      if (n <= 0) continue;
      if (x.key === 'summary') cuts.summary = length - n;
      else cuts.texts.set(x.key, length - n);
      excess -= n;
    }
  };
  take(early);
  take(reasoning);
  take(results);
  if (excess > 0 && answer) {
    const steps = answer.steps.length - 1;
    while (excess > 0 && cuts.steps < steps - 1) {
      const s = answer.steps[cuts.steps];
      let n = 60 + (s.content || '').length + (s.reasoning ? keptOf(s, s.reasoning) : 0);
      for (const tc of s.toolCalls || []) {
        if (tc.result != null) n += 80 + (tc.arguments || '').length + keptOf(tc, tc.result);
      }
      excess -= n;
      cuts.steps += 1;
    }
  }
  take(question);
  take(last);
  return excess <= 0;
}

/*
    La réponse en cours d'écriture dans la discussion affichée est redessinée à l'image suivante, au
    plus une fois toutes les renderInterval millisecondes, et moins souvent quand un dessin prend du
    temps, pour qu'une longue réponse laisse la page réactive.  Une réponse écrite en arrière-plan
    est dessinée en entier quand sa discussion est de nouveau affichée.
*/
const renderInterval = 100;
let renderSpacing = renderInterval;
let renderTimer = 0;
let renderFrame = 0;
let renderedAt = 0;

function scheduleRender(c) {
  if (state.view !== 'chat' || state.current !== c || renderTimer || renderFrame) return;
  const draw = () => {
    renderFrame = 0;
    const g = generationOf(state.current);
    if (!g || !g.turn) return;
    const t0 = performance.now();
    renderedAt = t0;
    drawLive(g.c, g.turn);
    renderSpacing = Math.min(1000, Math.max(renderInterval, 4 * (performance.now() - t0)));
  };
  const wait = renderSpacing - (performance.now() - renderedAt);
  if (wait > 0) {
    renderTimer = setTimeout(() => {
      renderTimer = 0;
      renderFrame = requestAnimationFrame(draw);
    }, wait);
  } else {
    renderFrame = requestAnimationFrame(draw);
  }
}

/*
    Dessine la réponse m de c en cours d'écriture, tant que c est affichée.  Les positions des
    défilements sont lues avant que le dessin change la page et écrites après, pour que la page
    soit mise en page une seule fois.
*/
function drawLive(c, m) {
  const el = answerElement(c, m);
  if (!el) return;
  const bubble = el.querySelector('.bubble');
  const thought = bubble.querySelector('.thinking.live .thought');
  const follow = !thought || thought.scrollHeight - thought.scrollTop - thought.clientHeight < 40;
  fillAssistant(bubble, m, true);
  const now = bubble.querySelector('.thinking.live .thought');
  if (now && follow) now.scrollTop = now.scrollHeight;
  scrollToEnd(false);
}

// Abandonne le dessin que scheduleRender a prévu, une fois la réponse dessinée en entier.
function cancelRender() {
  clearTimeout(renderTimer);
  cancelAnimationFrame(renderFrame);
  renderTimer = 0;
  renderFrame = 0;
}

// L'élément du message m de la discussion c, tant que c est la discussion affichée.
function answerElement(c, m) {
  if (state.view !== 'chat' || state.current !== c || !ui.thread || !ui.thread.isConnected) return null;
  const index = c.messages.indexOf(m);
  const el = index >= 0 ? ui.thread.children[index] : null;
  return el && el.dataset.index === String(index) && el.dataset.id === String(m.id || '') ? el : null;
}

/*
    Répond à la dernière question de c ; abort, issu de beginTurn, arrête la réponse.  La réponse
    est dessinée tant que c est la discussion affichée, continue en arrière-plan pendant qu'une
    autre est affichée, et est enregistrée avec c à sa fin ; une réponse qui se termine pendant que
    c n'est pas affichée marque c dans le panneau latéral jusqu'à ce qu'elle soit ouverte.
*/
async function runTurn(c, abort, turn) {
  const g = generationOf(c);
  // La fonction, le raisonnement et l'agent de la réponse restent ceux de son début.
  const role = c.role;
  const model = roleModel(role);
  turn.model = model || '';
  const signal = abort.signal;
  const caps = roleCapabilities(role);
  const reasoning = !!(c.reasoning && caps.reasoning);
  // Les outils de l'agent et du web.  Les appels d'une étape s'exécutent tant que l'un d'eux est activé, et un appel
  // d'un outil que la réponse ne propose pas est refusé.
  const set = toolSet(c, caps);
  const toolDefs = set.definitions;
  const useTools = !!toolDefs;
  const offered = new Set((toolDefs || []).map((d) => d.function.name));
  try {
    if (state.current === c && !answerElement(c, turn)) renderMain();
    else scheduleRender(c);
    if (signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
    if (!model) throw new Error(t('noModel'));
    // Les images du contexte sous forme d'URL de données pour un modèle qui les lit ; un modèle qui ne lit que du texte
    // reçoit leurs noms, ce que la discussion signale une fois.
    const media = await requestMedia(c, role);
    if (!media.read && contextImages(c) && !c.imagesNamed) {
      c.imagesNamed = true;
      turn.imagesNamed = true;
      toast(t('imagesNamed', { model: modelName(model) }), false, 9000);
    }
    // Les coupes des textes que partagent les étapes de la réponse (fitContext).
    const cuts = { summary: null, texts: new Map(), steps: 0 };
    for (let step = 0; step < maxAgentSteps; ++step) {
      const s = { reasoning: '', content: '', toolCalls: [] };
      turn.steps.push(s);
      const system = systemPrompt(c, caps, set);
      // La conversation, avec les étapes de cette réponse déjà faites (celles de l'agent).
      const build = (x) => apiMessages(Object.assign({}, c, {
        messages: c.messages.slice(0, -1).concat([Object.assign({}, turn, { steps: turn.steps.slice(0, -1) })])
      }), system, x, media);
      const fitted = await fitContext(c, build, toolDefs, answerReserve(caps, reasoning), signal, () => {
        turn.compacting = true;
        scheduleRender(c);
      }, 'albireo-' + role, cuts);
      turn.compacting = false;
      /*
          Une question qui ne tient pas à côté des documents du projet lus en entier les fait chercher, dans cette
          discussion désormais, ses extraits sont retrouvés, et l'étape recommence.
      */
      const project = c.projectId ? projectById(c.projectId) : null;
      if (fitted.overflow && !c.knowledgeSearch && knowledgeMode(project, caps, c) === 'whole') {
        c.knowledgeSearch = true;
        const asked = c.messages.filter((x) => x.role === 'user');
        if (asked.length) await prepareQuestion(c, asked[asked.length - 1], signal);
        // Les coupes de l'ajustement qui n'a pas tenu partent : les textes sont de nouveau mesurés en entier.
        cuts.summary = null;
        cuts.texts.clear();
        cuts.steps = 0;
        turn.steps.pop();
        --step;
        continue;
      }
      if (fitted.trimmed) turn.trimmed = true;
      if (fitted.compacted) turn.compacted = true;
      if (fitted.context) {
        turn.contextTokens = fitted.context;
        turn.promptTokens = fitted.tokens;
      }
      const body = { model: 'albireo-' + role, messages: fitted.messages, reasoning };
      if (toolDefs) body.tools = toolDefs;
      if (state.settings.temperature != null) body.temperature = state.settings.temperature;
      if (state.settings.topP != null) body.top_p = state.settings.topP;
      if (state.settings.topK != null) body.top_k = state.settings.topK;
      if (state.settings.maxTokens > 0) body.max_tokens = state.settings.maxTokens;
      let finish = null;
      // Une compression des échanges qui se termine désormais attend la fin de la réponse, dont les étapes gardent le
      // début de leur prompt.
      if (g) g.answering = true;
      for await (const ev of api.streamChat(body, signal)) {
        if (ev.type === 'progress') {
          turn.progress = ev.data;
          scheduleRender(c);
          continue;
        }
        const chunk = ev.data;
        if (chunk.usage) {
          turn.usage = turn.usage ? {
            prompt_tokens: chunk.usage.prompt_tokens, completion_tokens: (turn.usage.completion_tokens || 0) + chunk.usage.completion_tokens
          } : chunk.usage;
          turn.timings = chunk.timings || turn.timings;
        }
        // Une réponse que le moteur a arrêtée parce qu'elle se répétait.
        if (chunk.repetition) turn.repetition = true;
        const choice = chunk.choices && chunk.choices[0];
        if (!choice) continue;
        const d = choice.delta || {};
        if (d.reasoning_content) {
          if (!s.reasoningStart) s.reasoningStart = Date.now();
          s.reasoning += d.reasoning_content;
        }
        if (d.content) {
          if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
          s.content += d.content;
        }
        for (const tc of d.tool_calls || []) {
          if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
          const existing = tc.index != null ? s.toolCalls.find((x) => x.index === tc.index) : null;
          if (existing) existing.arguments += (tc.function && tc.function.arguments) || '';
          else s.toolCalls.push({
            index: tc.index, id: tc.id || ('call_' + store.uid()), name: tc.function ? tc.function.name : '',
            arguments: (tc.function && tc.function.arguments) || '', status: 'pending'
          });
        }
        if (choice.finish_reason) finish = choice.finish_reason;
        turn.progress = null;
        scheduleRender(c);
      }
      if (s.reasoningStart && !s.reasoningMs) s.reasoningMs = Date.now() - s.reasoningStart;
      delete s.reasoningStart;
      if (!(useTools && s.toolCalls.length && (finish === 'tool_calls' || finish === 'stop'))) break;
      // Les outils de l'étape s'exécutent, chacun chronométré, puis le modèle continue avec leurs résultats.
      for (const tc of s.toolCalls) {
        if (signal.aborted) throw new DOMException('The answer was stopped.', 'AbortError');
        tc.status = 'running';
        tc.started = Date.now();
        scheduleRender(c);
        let r;
        if (!offered.has(tc.name)) {
          r = { ok: false, text: 'The tool ' + tc.name + ' is not available in this answer.' };
        } else if (web.isTool(tc.name)) {
          // fetch_page lit les adresses de la discussion telle qu'elle est quand l'appel s'exécute, y compris les
          // résultats des appels de cette réponse.
          r = await web.run(tc.name, tc.arguments, {
            id: c.id, language: language(), signal, allowed: () => web.allowedAddresses(c)
          });
        } else {
          r = await tools.run(tc.name, tc.arguments, {
            documents: documentsOf(c),
            searchDocuments: (query, limit) => searchDocumentsOf(c, query, limit),
            searchDiscussions: async (q) => searchConversations(q, 6).filter((x) => x.conversation.id !== c.id)
              .map((x) => ({
                title: x.conversation.title, date: new Date(x.conversation.updated).toLocaleDateString(),
                excerpt: x.excerpt
              })),
            createFile: (name, content) => ({ name, content }),
            signal
          });
        }
        tc.ms = Date.now() - tc.started;
        delete tc.started;
        // Un appel que l'arrêt de la réponse a terminé garde ce qu'il a renvoyé, comme arrêté.
        tc.status = signal.aborted ? 'stopped' : 'done';
        tc.ok = r.ok;
        tc.result = r.text;
        if (r.file) tc.file = r.file;
        // Les adresses qu'un outil web ajoute à la discussion, que fetch_page peut lire dès lors.
        if (r.web) tc.web = r.web;
        delete tc.index;
        scheduleRender(c);
      }
      if (step === maxAgentSteps - 1) turn.agentLimit = true;
    }
  } catch (e) {
    if (e.name === 'AbortError') turn.cancelled = true;
    else turn.error = errorText(e);
  } finally {
    if (state.current === c) cancelRender();
    // Les appels qu'une réponse arrêtée ou en échec n'a pas exécutés restent arrêtés, sans résultat.
    for (const s of turn.steps) {
      delete s.reasoningStart;
      for (const tc of s.toolCalls || []) {
        if (tc.status === 'done') continue;
        tc.status = 'stopped';
        delete tc.started;
        delete tc.index;
      }
    }
    turn.progress = null;
    turn.compacting = false;
    turn.preparing = false;
    if (g && generations.get(c.id) === g) generations.delete(c.id);
    // Une compression des échanges terminée pendant l'écriture de la réponse s'applique maintenant.
    const settled = !!(g && g.compaction && settleCompaction(c, g.compaction));
    announceBusy(c, false);
    c.updated = Date.now();
    await persist(c);
    if (settled) warmBeginning(c);
    if (!(state.view === 'chat' && state.current && state.current.id === c.id) && !c.incognito && !deleted.has(c.id)) {
      unread.add(c.id);
    }
    if (state.current === c) renderMain();
    else lockThread();
    refreshViews();
  }
  if (deleted.has(c.id)) return;
  // Une discussion encore nommée d'après sa première question reçoit un titre du modèle, quelle
  // que soit la fin de sa réponse, puisque le titre suit la question.
  if (needsTitle(c)) nameConversation(c);
  if (!turn.error && !turn.cancelled) {
    compactLater(c);
    if (!c.incognito) rememberLater();
  }
}

/*
    La mémoire lit les questions des discussions qu'elle n'a pas encore lues, chaque discussion à
    partir de sa marque, en un seul passage une fois qu'elles sont assez nombreuses
    (memoryBatchQuestions questions ou memoryBatchChars caractères) et que l'utilisateur est
    inactif depuis memoryIdle : le modèle n'écrit que les faits nouveaux, qui rejoignent la mémoire
    sans réécriture, et la mémoire n'est réécrite en entier que quand elle dépasse sa taille.  Un
    seul onglet la met à jour à la fois, grâce à un bail qui expire : un onglet fermé au milieu
    d'une mise à jour ne retient rien, et les questions qu'il lisait sont relues par la mise à jour
    suivante, puisque les marques ne bougent qu'une fois la mémoire enregistrée.  La mémoire est
    écrite dans la langue de l'interface, quelles que soient les langues des discussions, et une
    mise à jour y réécrit d'abord les faits écrits dans une autre langue, après un changement de
    langue entre autres.
*/
const memoryBatchQuestions = 3;
const memoryBatchChars = 1200;
const memoryIdle = 20000;
const memoryLeaseMs = 180000;
// Le nombre maximal de questions que lit une mise à jour : ce que le modèle d'arrière-plan lit en memoryReadSeconds, la
// moitié de son contexte de travail au plus ; les autres attendent la mise à jour suivante.
const memoryReadSeconds = 30;
const tabId = store.uid();
let memoryTimer = null;
let memoryRunning = false;
let memoryAbort = null;   // la mise à jour en cours, qu'une nouvelle question arrête

function rememberLater() {
  if (!state.settings.memoryEnabled) return;
  clearTimeout(memoryTimer);
  memoryTimer = setTimeout(() => {
    memoryTimer = null;
    updateMemory().catch(() => {});
  }, memoryIdle);
}

function sensitiveRule(sensitive) {
  return sensitive ? 'Information the user shares about themselves may be kept, sensitive or not. '
    : 'Leave out sensitive information: health, finances, passwords, keys and identifiers, political, religious '
      + 'or philosophical opinions, sexual orientation, private matters, and information about other people. ';
}

/*
    La langue dans laquelle la mémoire est écrite, nommée en anglais pour le modèle.  C'est celle de
    l'interface, quelles que soient les langues des discussions dont la mémoire apprend.
*/
export function memoryLanguage() {
  return languageNames[language()] || 'English';
}

/*
    Les deux faits de l'exemple du delta dans chaque langue de l'interface, pour un message en
    français (en anglais pour une interface en français), pour que l'exemple montre un fait dans la
    langue de l'interface à côté des mots de l'utilisateur dans sa propre langue.  Un fait est une
    phrase courte dont le sujet, l'utilisateur, est omis.
*/
const memoryExamples = {
  fr: ['Travaille comme infirmière à Lyon.', 'Préfère les réponses courtes.']
};

/*
    Les faits nouveaux qu'apprennent les questions, qui rejoignent la mémoire, chacun suivi des mots
    de l'utilisateur qui le disent, ce que la mise à jour vérifie dans les questions qu'elle a lues
    (groundedFacts).  La mémoire arrive numérotée, et un fait qui en change un donne son numéro,
    pour qu'il le remplace.  Les exemples montrent un message avec deux faits, et une demande, qui
    n'en apprend aucun.
*/
function memoryDeltaInstructions(words, sensitive) {
  const code = 'fr';
  const [a, b] = memoryExamples[code];
  const english = code === 'fr';
  const message = english ? 'I am a nurse in Lyon and I prefer short answers. What does triage mean?'
    : 'Je suis infirmière à Lyon et je préfère les réponses courtes. Que veut dire triage ?';
  const [qa, qb] = english ? ['I am a nurse in Lyon', 'I prefer short answers']
    : ['Je suis infirmière à Lyon', 'je préfère les réponses courtes'];
  const request = english ? 'Write a Python program that computes the integral of x squared between 0 and 1.'
    : 'Crée un programme Python pour calculer l\'intégrale de x au carré entre 0 et 1.';
  return 'You keep a short memory about the user for the future discussions of an assistant. From the new messages '
    + 'of the user, write the lasting facts about the user that the memory does not hold yet, those that will help '
    + 'in other discussions: the role, the profession and the field of the user, the organization they work for, '
    + 'their projects and the goals of these projects, the languages, tools, frameworks, systems and computers they '
    + 'use, their level of expertise, their habits of work, and the way they want to be answered (language, length, '
    + 'format, style), with their standing instructions ("always...", "never..."). A fact says who the user is, what '
    + 'the user does, uses or wants over time, as the user says it about themselves, even inside a question ("As a '
    + 'teacher, how do I..."). A request of the user (write, create, explain, translate or compute something) and the '
    + 'topic of a question are not facts about the user: leave them out, whatever their words. Leave out what serves '
    + 'one discussion only, the content of documents, and what the memory already holds, even in other words. '
    + sensitiveRule(sensitive)
    + 'Write each new fact on a line of its own: "- ", the fact as a short sentence in ' + memoryLanguage()
    + ' about the user, without naming the user, " | ", then the statement of the user that says it, copied '
    + 'exactly as the user wrote it, in its language, between double quotes; a request of the user states no fact. '
    + 'The facts of the memory are numbered: a new fact that changes one of them, a new role or another preference, '
    + 'starts with its number between brackets, "- [3] ", and replaces it. A fact the user did not state in so many '
    + 'words is left out. The facts take at most ' + words + ' words in all. For example, for the message "'
    + message + '":\n'
    + '- ' + a + ' | "' + qa + '"\n'
    + '- ' + b + ' | "' + qb + '"\n'
    + 'For the message "' + request + '": NONE\n'
    + 'When there is no new fact, answer NONE.';
}

/*
    Les mots d'un texte tels que la mémoire compare une citation de l'utilisateur avec les
    questions : en minuscules, une seule sorte d'apostrophe, sans guillemets doubles, les espaces
    réduites à une seule.
*/
function evidenceText(text) {
  return String(text || '').toLowerCase().replace(/[\u2018\u2019\u201a\u201b\u2032`\u00b4]/g, '\'')
    .replace(/["\u201c\u201d\u201e\u201f\u00ab\u00bb\u2033]/g, '').replace(/\s+/g, ' ').trim();
}

/*
    Les premiers mots des demandes, en français, repliés (fold) : les verbes des demandes fréquentes à l'impératif, et
    les débuts d'une demande polie.  Une affirmation de l'utilisateur qui commence par l'un d'eux demande une tâche, ce
    qui ne dit rien de durable sur l'utilisateur.
*/
const requestOpeners = new Set(('ecris ecrivez cree creez fais faites genere generez calcule calculez explique '
  + 'expliquez decris decrivez donne donnez montre montrez liste listez resume resumez traduis traduisez convertis '
  + 'convertissez corrige corrigez reecris reecrivez redige redigez dessine dessinez trouve trouvez cherche cherchez '
  + 'dis dites aide aidez implemente implementez ajoute ajoutez supprime supprimez verifie verifiez resous resolvez '
  + 'analysez comparez preparez concois concevez developpe developpez optimisez proposez suggere suggerez completez '
  + 'simplifie simplifiez ameliore ameliorez peux pourrais pouvez pourriez').split(' '));

// Les pronoms qui suivent un impératif après un trait d'union en français ("écris-moi", "donne-le-moi").
const requestPronouns = new Set(['moi', 'toi', 'nous', 'vous', 'le', 'la', 'les', 'lui', 'leur', 'en', 'y']);

/*
    Vrai quand une proposition demande une tâche : son premier mot, après « s'il te plaît » ou « s'il vous plaît », est
    un début de demande, seul ou avant les pronoms d'un impératif français.
*/
function asksForTask(clause) {
  const words = fold(clause).replace(/[^\p{L}\s'-]+/gu, ' ').trim().split(/\s+/);
  let k = 0;
  if (words[0] === 's\'il' && (words[1] === 'te' || words[1] === 'vous')) k = 3;
  const w = words[k] || '';
  if (requestOpeners.has(w)) return true;
  const parts = w.split('-');
  return parts.length > 1 && requestOpeners.has(parts[0]) && parts.slice(1).every((x) => requestPronouns.has(x));
}

/*
    Les mots d'une consigne durable de l'utilisateur, repliés, dans les langues de l'interface : une demande qui en
    contient un ("Please always answer in French") dit comment l'utilisateur veut qu'on lui réponde désormais.
*/
const standingWords = new RegExp('\\b(always|never|from now on|toujours|jamais|desormais|dorenavant|a l\'avenir|immer|nie'
  + '|niemals|ab jetzt|kunftig|sempre|mai|d\'ora in poi|siempre|nunca|a partir de ahora|de ahora en adelante)\\b');

/*
    Les débuts d'une proposition dans laquelle l'utilisateur dit ce qu'il est ou fait, repliés : un rôle ("as a
    teacher") ou un sujet à la première personne avec un verbe d'état ou de travail ("I work", "je suis", "ich arbeite",
    "sono", "trabajo").  Une proposition qui commence par l'un d'eux affirme un fait sur l'utilisateur même à
    l'intérieur d'une question ("As a teacher, how do I...?") ; les autres parties d'une question n'en appuient aucun.
*/
const statementOpeners = new RegExp('^(as an? |en tant qu|in qualita di |als |i am |i\'m |i work |i use |i develop '
  + '|i teach |i study |i manage |i lead |je suis |je travaille |j\'utilise |je developpe |j\'enseigne |j\'etudie '
  + '|je dirige |ich bin |ich arbeite |ich nutze |ich verwende |ich entwickle |ich unterrichte |ich studiere |sono '
  + '|lavoro |insegno |studio |soy |trabajo |utilizo |desarrollo |enseno |estudio )');

/*
    Si les mots d'une citation, normalisés et repliés, apparaissent dans les questions lues,
    normalisées et repliées elles aussi, avec 3 mots ou 12 caractères au moins dans une partie de
    phrase qui ne demande rien.  Les parties se terminent aux signes qui terminent une phrase, aux
    deux-points et aux points-virgules, et une partie qui se termine par un point d'interrogation
    n'appuie aucun fait, comme dans "One line: what is RAII?", sauf si la citation commence par dire
    ce que l'utilisateur est ou fait (statementOpeners : "As a C++ developer, how do I...?").  Une
    citation dont la proposition commence par une demande ("Write a program that...") n'appuie
    aucun fait non plus (asksForTask), sauf si elle contient une consigne durable
    ("Always answer in French").
*/
function statedIn(quote, where) {
  const at = quote ? where.indexOf(quote) : -1;
  if (at < 0) return false;
  let clause = at;
  while (clause > 0 && !/[.!?:;,\u2026]/.test(where[clause - 1])) --clause;
  const said = where.slice(clause, at + quote.length);
  if (asksForTask(said) && !standingWords.test(said)) return false;
  const personal = statementOpeners.test(quote);
  const end = at + quote.length;
  for (const s of where.matchAll(/[^.!?:;\u2026]+[.!?:;\u2026]*/g)) {
    const from = Math.max(at, s.index);
    const to = Math.min(end, s.index + s[0].length);
    if (from >= to || (/\?\s*$/.test(s[0]) && !personal)) continue;
    const part = where.slice(from, to).replace(/[.!?;:,\u2026]+$/, '').trim();
    if (part.split(' ').filter(Boolean).length >= 3 || part.length >= 12) return true;
  }
  return false;
}

// Les morceaux d'une citation que le modèle a raccourcie par des points de suspension, chacun dans les questions, dans
// leur ordre.
function quotePieces(quote, where) {
  const pieces = quote.split(/\s*(?:\u2026|\.\.\.)\s*/).map((x) => x.trim()).filter(Boolean);
  let from = 0;
  for (const piece of pieces) {
    const at = where.indexOf(piece, from);
    if (at < 0) return null;
    from = at + piece.length;
  }
  return pieces;
}

/*
    Les faits nouveaux d'un delta qu'appuient les questions lues par la mise à jour.  Chaque ligne
    contient un fait, " | " et les mots de l'utilisateur qui le disent, qui doivent se trouver dans
    une affirmation des questions lues (statedIn), les morceaux d'une citation raccourcie par des
    points de suspension dans leur ordre.  Les mots de l'utilisateur appuient un seul fait, et un
    fait suivant qui les cite de nouveau est omis.  Un fait peut commencer par le numéro du fait de
    la mémoire qu'il remplace, "[3]".  Les faits ne gardent aucune citation, et deviennent des
    phrases.  Renvoie {facts: [{text, replaces}], proposed, discarded}, replaces étant le numéro du
    fait remplacé, 0 pour aucun.
*/
function groundedFacts(text, read, sensitive) {
  const where = fold(evidenceText(read));
  const cited = new Set();
  const facts = [];
  let proposed = 0;
  let discarded = 0;
  for (const raw of String(text || '').split('\n')) {
    const line = raw.trim();
    if (!/^[-*\u2022]\s*\S/.test(line)) continue;
    ++proposed;
    const m = /^[-*\u2022]\s*(?:\[(\d+)\]\s*)?(.+?)\s*\|\s*(.+)$/.exec(line);
    const quote = m ? fold(evidenceText(m[3])).replace(/[.!?;:,\u2026]+$/, '').trim() : '';
    const pieces = quote ? quotePieces(quote, where) : null;
    const longest = (pieces || []).reduce((x, y) => (y.length > x.length ? y : x), '');
    if (!m || !m[2].trim() || !pieces || cited.has(quote) || !statedIn(longest, where)) {
      ++discarded;
      continue;
    }
    cited.add(quote);
    const replaces = m[1] ? Number(m[1]) : 0;
    for (const x of memorySentences(sentenceOf(m[2]), sensitive)) facts.push({ text: x, replaces });
  }
  return { facts, proposed, discarded };
}

// La mémoire réécrite dans les limites de sa taille, quand les faits nouveaux la lui ont fait dépasser.
function memoryCompactInstructions(words, sensitive) {
  return 'You keep a short memory about the user for the future discussions of an assistant. Rewrite it in at most '
    + words + ' words, as short sentences one after the other in a single paragraph, without a list, a bullet or a '
    + 'line break: merge the sentences that say the same thing, keep the most lasting facts about the user, those '
    + 'that help in any discussion first (role, field, projects, tools, standing preferences), the newest last, and '
    + 'leave out the requests and the tasks of past discussions. ' + sensitiveRule(sensitive)
    + 'Write every sentence in ' + memoryLanguage() + ', the sentences written in another language included. '
    + 'Answer with the paragraph alone.';
}

// La mémoire réécrite dans la langue de l'interface, phrase par phrase, chacune avec son numéro.
function memoryTranslateInstructions() {
  return 'You keep a short memory about the user for the future discussions of an assistant. Its sentences are '
    + 'numbered. Rewrite each of them in ' + memoryLanguage() + ', on a line of its own that starts with its number '
    + 'between brackets, as "[1] ": translate the sentences written in another language, and keep the sentences '
    + 'already written in ' + memoryLanguage() + ' as they are. Answer with the numbered lines alone.';
}

/*
    Les phrases des lignes de la mémoire une fois traduites : la ligne de chaque numéro dans la réponse du modèle, la
    phrase telle qu'elle était pour un numéro qui manque à la réponse.  Renvoie null quand la réponse n'en a traduit
    aucune.
*/
function translatedSentences(lines, answer) {
  const out = lines.slice();
  let found = 0;
  for (const raw of String(answer || '').split('\n')) {
    const m = /^\s*\[(\d+)\]\s*(.+)$/.exec(raw);
    const k = m ? Number(m[1]) - 1 : -1;
    if (k < 0 || k >= lines.length) continue;
    const sentence = memorySentences(m[2], true).join(' ');
    if (!sentence) continue;
    out[k] = sentence;
    ++found;
  }
  return found ? out : null;
}

// Vrai quand une phrase d'une mémoire est écrite dans une autre langue que celle de l'interface.
function memoryForeign(text) {
  const target = language();
  return memorySentences(text, true).some((x) => {
    const found = textLanguage(x, true);
    return !!found && found !== target;
  });
}

// Les identifiants de connexion, les clés et les identifiants de paiement, que la mémoire ne garde que quand les
// réglages gardent les informations sensibles, quoi que le modèle ait écrit.
const secretPatterns = [
  /\b(mots? de passe|passwords?|passw[o\u00f6]rt|kennwort|contrase[n\u00f1]as?|parol[ae] d'ordine|passphrase|passcode)\b/i,
  /\b(pin|cvv|cvc|iban|bic|swift)\b/i,
  /\b(api[ _-]?keys?|cl[e\u00e9]s? d'api|access[ _-]?tokens?|bearer|secret[ _-]?keys?|private[ _-]?keys?|cl[e\u00e9]s? priv[e\u00e9]es?)\b/i,
  /\b(?:\d[ -]?){13,19}\b/,
  /[A-Za-z0-9+\/=_-]{32,}/
];

// Un fait sous forme de phrase de la mémoire : sans les marques d'une liste ni guillemets autour, sa première lettre en
// majuscule, avec son point final.
function sentenceOf(text) {
  let x = String(text || '').replace(/\s+/g, ' ').trim().replace(/^[-*\u2022]\s*/, '')
    .replace(/^["'\u00ab\u201c\u201e]+|["'\u00bb\u201d]+$/g, '').trim();
  if (!x) return '';
  x = x.charAt(0).toLocaleUpperCase() + x.slice(1);
  return /[.!?\u2026]$/.test(x) ? x : x + '.';
}

/*
    Les phrases d'une mémoire : les lignes des notes écrites sous forme de liste par une version antérieure, chacune
    devenant une phrase, ou les phrases d'un paragraphe, sans les secrets sauf si les réglages gardent les informations
    sensibles.
*/
export function memorySentences(text, sensitive) {
  const out = [];
  for (const line of String(text || '').split('\n').map((l) => l.trim()).filter(Boolean)) {
    if (/^[-*\u2022]\s*\S/.test(line)) out.push(sentenceOf(line));
    else for (const x of line.split(/(?<=[.!?\u2026])\s+(?=\S)/)) out.push(sentenceOf(x));
  }
  return out.filter((x) => x && (sensitive || !secretPatterns.some((re) => re.test(x))));
}

const wordCount = (lines) => lines.join(' ').split(/\s+/).filter(Boolean).length;

// Les mots que peuvent prendre les faits que le modèle apprend à côté des phrases que l'utilisateur a écrites : la
// moitié de la mémoire au moins.
function learnedRoom(words, pinned) {
  return Math.max(words - wordCount(pinned), Math.floor(words / 2));
}

/*
    La mémoire telle qu'elle est gardée : ses phrases l'une après l'autre, dans la limite de words mots, les plus
    anciennes partant d'abord, sauf les phrases que l'utilisateur a écrites (pinned, un Set).
*/
export function memoryParagraph(sentences, words, pinned) {
  const list = sentences.slice();
  while (list.length > 1 && wordCount(list) > words) {
    const k = list.findIndex((x) => !(pinned && pinned.has(x)));
    if (k < 0) break;
    list.splice(k, 1);
  }
  return list.join(' ');
}

// Vrai quand une mémoire contient des notes écrites sous forme de liste, qui deviennent des phrases (memorySentences).
function listMemory(text) {
  return /^\s*[-*\u2022]\s*\S/m.test(String(text || ''));
}

// Les mots d'une phrase, pour la comparaison de deux faits : leurs cinq premières lettres, qui
// rapprochent les formes d'un mot ("courtes", "court").
function factWords(line) {
  return new Set(line.toLowerCase().normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^\p{L}\p{N}]+/gu, ' ')
    .split(' ').filter((w) => w.length > 2).map((w) => w.slice(0, 5)));
}

// Vrai quand deux phrases disent presque la même chose : la plupart de leurs mots sont communs.
function sameFact(a, b) {
  const x = factWords(a);
  const y = factWords(b);
  if (!x.size || !y.size) return false;
  let shared = 0;
  for (const w of x) if (y.has(w)) ++shared;
  return shared / (x.size + y.size - shared) >= 0.7;
}

/*
    Les questions que la mémoire n'a pas encore lues, chaque discussion à partir de sa marque, les discussions changées
    le moins récemment d'abord : [{id, to, asked: [{text, to}]}], to étant la marque après la discussion ou après la
    question.
*/
function pendingMemory() {
  const items = [];
  let questions = 0;
  let chars = 0;
  const all = state.conversations.slice();
  if (state.current && !state.current.incognito && !all.includes(state.current)) all.push(state.current);
  all.sort((a, b) => (a.updated || 0) - (b.updated || 0));
  for (const c of all) {
    if (c.incognito || !Array.isArray(c.messages) || c.messages.length <= (c.memoryMark || 0)) continue;
    const asked = [];
    for (let i = c.memoryMark || 0; i < c.messages.length; ++i) {
      const m = c.messages[i];
      if (m.role === 'user' && (m.content || '').trim()) asked.push({ text: m.content, to: i + 1 });
    }
    items.push({ id: c.id, to: c.messages.length, asked });
    questions += asked.length;
    chars += asked.reduce((n, a) => n + a.text.length, 0);
  }
  return { items, questions, chars };
}

/*
    Les questions que lit une mise à jour, les plus anciennes d'abord, dans la limite de chars caractères : le texte que
    lit le modèle, et les éléments à marquer comme lus, le dernier d'entre eux jusqu'à la dernière question qu'il a
    prise.
*/
function memoryReading(items, chars) {
  const parts = [];
  const marks = [];
  let used = 0;
  for (const item of items) {
    let taken = 0;
    for (const a of item.asked) {
      const part = 'User: ' + keepStart(a.text, 1500);
      if (parts.length && used + part.length + 2 > chars) break;
      parts.push(part);
      used += part.length + 2;
      ++taken;
    }
    // Une discussion lue en entier est marquée à sa fin, une discussion lue en partie après sa dernière question lue.
    if (taken === item.asked.length) {
      marks.push({ id: item.id, to: item.to });
      continue;
    }
    if (taken) marks.push({ id: item.id, to: item.asked[taken - 1].to });
    break;
  }
  return { read: parts.join('\n\n'), marks };
}

// Le bail de cet onglet sur la mémoire : false quand un autre onglet le détient et qu'il n'a pas expiré.
function takeMemoryLease() {
  return store.claimLease('memoryLease', tabId, Date.now() + memoryLeaseMs);
}

function releaseMemoryLease() {
  return store.releaseLease('memoryLease', tabId);
}

// Les copies d'une discussion dans cet onglet : celle qui est affichée et celle des listes.
function copiesOf(id) {
  const out = state.conversations.filter((x) => x.id === id);
  if (state.current && state.current.id === id && !out.includes(state.current)) out.push(state.current);
  return out;
}

/*
    Les discussions lues jusqu'aux questions que la mémoire a prises.  La marque change sur la
    discussion enregistrée, relue depuis la base, pour que les messages qu'un autre onglet a
    enregistrés restent, et sur les copies de cet onglet, pour que leur prochain enregistrement la
    garde.
*/
async function markRead(items) {
  for (const item of items) {
    if (deleted.has(item.id)) continue;
    const mark = (x) => {
      const to = Math.min(item.to, Array.isArray(x.messages) ? x.messages.length : 0);
      if ((x.memoryMark || 0) >= to) return false;
      x.memoryMark = to;
      return true;
    };
    await store.updateConversations(mark, item.id).catch(() => {});
    copiesOf(item.id).forEach(mark);
    broadcast({ type: 'conversation', id: item.id });
  }
}

// La mémoire et ses réglages tels qu'ils sont enregistrés, qu'un autre onglet a pu changer.
async function storedMemory() {
  const s = Object.assign({}, defaults, await store.getSettings());
  return {
    enabled: !!s.memoryEnabled, sensitive: !!s.memorySensitive, memory: s.memory || '', language: s.memoryLanguage,
    pinned: s.memoryPinned || ''
  };
}

// Les copies des discussions dans cet onglet, après une remise à zéro de la mémoire dans cet onglet ou dans un autre.
export function forgetMemory() {
  const all = state.conversations.slice();
  if (state.current && !all.includes(state.current)) all.push(state.current);
  for (const c of all) {
    delete c.memory;
    c.memoryMark = c.messages.length;
  }
  clearTimeout(memoryTimer);
  memoryTimer = null;
}

/*
    Met à jour la mémoire avec les questions qu'elle n'a pas encore lues, quand elles sont assez
    nombreuses ou que force est vrai : le modèle d'arrière-plan écrit les faits nouveaux, qui
    rejoignent la mémoire sauf si elle les contient déjà, puis la mémoire est réécrite dans les
    limites de sa taille quand elle l'a dépassée, ou perd ses phrases les plus anciennes quand la
    réécriture échoue.  La mémoire est gardée sous forme de courtes phrases l'une après l'autre.
    Une notification dit combien de faits l'ont rejointe.
*/
async function updateMemory(force) {
  if (memoryRunning || !state.settings.memoryEnabled || state.incognito) return;
  const pending = pendingMemory();
  const enough = pending.items.length > 0
    && (force || pending.questions >= memoryBatchQuestions || pending.chars >= memoryBatchChars);
  // Une mémoire écrite dans une autre langue que celle de l'interface y est réécrite, avec ou sans
  // nouvelles questions à lire.
  const memory = state.settings.memory || '';
  const foreign = !!memory && (memoryForeign(memory)
    || (!!state.settings.memoryLanguage && state.settings.memoryLanguage !== language()));
  if (!enough && !foreign) return;
  // Les réponses en cours passent d'abord.
  if (generations.size) {
    rememberLater();
    return;
  }
  memoryRunning = true;
  memoryAbort = new AbortController();
  const signal = memoryAbort.signal;
  let leased = false;
  try {
    leased = await takeMemoryLease();
    if (!leased) return;
    // La mémoire telle qu'elle est enregistrée : un autre onglet a pu la remettre à zéro ou la modifier.
    const stored = await storedMemory();
    if (!stored.enabled) return;
    const sensitive = stored.sensitive;
    const context = backgroundContext();
    const words = memoryLimitWords(context);
    const current = stored.memory;
    const target = language();
    // Les phrases que l'utilisateur a écrites dans la mémoire, qui restent ; le bail, renouvelé avant chaque requête au
    // modèle.
    let pinned = memorySentences(stored.pinned, true);
    const renew = async () => {
      if (!(await takeMemoryLease())) throw new DOMException('The memory is updated by another tab.', 'AbortError');
    };
    // Vrai quand la mémoire a été remise à zéro, modifiée ou désactivée entre-temps, dans cet onglet
    // ou dans un autre, telle qu'elle est enregistrée maintenant : la mise à jour est alors abandonnée.
    const changed = async () => {
      const now = await storedMemory();
      return !now.enabled || now.memory !== current;
    };
    // Les notes écrites sous forme de liste par une version antérieure deviennent des phrases, et sont enregistrées
    // comme telles.
    let lines = memorySentences(current, true);
    let rewritten = false;
    /*
        Les faits écrits dans une autre langue sont d'abord réécrits dans la langue de
        l'interface, pour que les faits nouveaux soient comparés avec eux dans une seule langue.
        Une réécriture qui a perdu des faits est écartée, et la mise à jour suivante réessaie.
    */
    if (lines.length && (memoryForeign(current) || (!!stored.language && stored.language !== target))) {
      await renew();
      const r = await backgroundText(memoryTranslateInstructions(),
        '<memory>\n' + lines.map((x, k) => '[' + (k + 1) + '] ' + x).join('\n') + '\n</memory>\n\nRewrite every '
          + 'sentence in ' + memoryLanguage() + '.', Math.ceil(wordCount(lines) * 2.5) + 8 * lines.length + 32,
        signal).catch((e) => {
        if (e.name === 'AbortError') throw e;
        return null;
      });
      if (await changed()) return;
      // Une traduction garde chaque phrase, les phrases que l'utilisateur a écrites prenant leurs traductions.
      const translated = r ? translatedSentences(lines, r.text) : null;
      if (translated) {
        const kept = new Set(pinned);
        pinned = translated.filter((x, k) => kept.has(lines[k]));
        lines = translated;
        rewritten = true;
      }
    }
    let added = 0;
    let moved = 0;
    let marks = [];
    if (enough && pending.chars >= 40) {
      // Ce que l'utilisateur a écrit, les plus anciens d'abord, dans la limite de la lecture de la mise à jour : la
      // mémoire porte sur l'utilisateur, et les réponses la rempliraient de ce que l'assistant a dit.
      const rates = backgroundRates();
      const reading = memoryReading(pending.items,
        Math.floor(Math.min(0.5 * context, Math.max(2048, memoryReadSeconds * rates.read)) * 3.6));
      const read = reading.read;
      marks = reading.marks;
      await renew();
      const r = await backgroundText(memoryDeltaInstructions(words, sensitive),
        '<memory>\n' + (lines.map((x, k) => '[' + (k + 1) + '] ' + x).join('\n') || '(empty)')
          + '\n</memory>\n\n<messages>\n' + read
          + '\n</messages>\n\nWrite the new facts in ' + memoryLanguage() + ', each with the words of the user.',
        Math.ceil(words * 3.2) + 32, signal);
      if (await changed()) return;
      const delta = groundedFacts(r.text, read, sensitive);
      console.info('Albireo: memory: ' + delta.proposed + ' facts proposed, ' + delta.discarded
        + ' left out, unsupported by the words of the user');
      /*
          Un fait qui en remplace un de la mémoire prend sa place, un fait redit passe à la fin avec les nouveaux, pour
          que les faits que les questions n'apportent plus partent d'abord ; les phrases que l'utilisateur a écrites
          restent telles qu'il les a écrites, sauf si les mots de l'utilisateur changent l'une d'elles.
      */
      const kept = new Set(pinned);
      const recent = [];
      const slots = lines.slice();
      for (const fact of delta.facts) {
        const k = fact.replaces - 1;
        if (k >= 0 && k < slots.length && slots[k] != null) {
          if (slots[k] !== fact.text) ++added;
          kept.delete(slots[k]);
          slots[k] = null;
          recent.push(fact.text);
          continue;
        }
        const same = slots.findIndex((l) => l != null && sameFact(l, fact.text));
        if (same >= 0) {
          if (!kept.has(slots[same])) {
            recent.push(slots[same]);
            slots[same] = null;
            ++moved;
          }
          continue;
        }
        if (recent.some((l) => sameFact(l, fact.text))) continue;
        recent.push(fact.text);
        ++added;
      }
      if (added || moved) {
        lines = slots.filter((x) => x != null).concat(recent);
        pinned = pinned.filter((x) => kept.has(x));
      }
      if (added && wordCount(lines) > wordCount(pinned) + learnedRoom(words, pinned)) {
        // Les faits que le modèle a appris sont réécrits dans la place que leur laissent les phrases de l'utilisateur.
        const learned = lines.filter((x) => !pinned.includes(x));
        const room = learnedRoom(words, pinned);
        await renew();
        const compact = await backgroundText(memoryCompactInstructions(room, sensitive),
          '<memory>\n' + learned.join(' ') + '\n</memory>', Math.ceil(room * 2.2) + 16, signal).catch(() => null);
        if (signal.aborted) return;
        if (await changed()) return;
        const compacted = compact ? memorySentences(compact.text, sensitive) : [];
        if (compacted.length && wordCount(compacted) <= room * 1.2) lines = pinned.concat(compacted);
      }
    }
    /*
        Les faits que le modèle a appris prennent la place que leur laissent les phrases de l'utilisateur, la moitié de
        la mémoire au moins ; une mémoire que l'utilisateur a écrite, qui peut prendre deux fois sa taille, la garde
        tant qu'aucun fait ne la rejoint.
    */
    if (added || moved || rewritten || listMemory(current)) {
      const limit = wordCount(pinned) + learnedRoom(words, pinned);
      await saveSetting('memory', memoryParagraph(lines, added ? limit : Math.max(limit, words * 2), new Set(pinned)));
      await saveSetting('memoryPinned', pinned.join(' '));
      await saveSetting('memoryUpdated', Date.now());
      await saveSetting('memoryLanguage', target);
    }
    // Les questions trop courtes pour dire quoi que ce soit sont lues malgré tout.
    if (enough && pending.chars < 40) marks = pending.items;
    if (marks.length) await markRead(marks);
    if (added) toast(tn('memoryLearned', added), false, 4500);
    else if (rewritten) toast(t('memoryRewritten'), false, 4500);
  } finally {
    memoryRunning = false;
    memoryAbort = null;
    if (leased) await releaseMemoryLease().catch(() => {});
  }
}

/*
    Une erreur dans la langue de l'interface : une erreur du service par son code, une erreur de la lecture d'un
    document (documents.js) par son message, sinon son message tel quel.
*/
function errorText(e) {
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
function provisionalTitle(m) {
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
function titleQuestionOf(m) {
  const names = (m.attachments || []).concat(m.images || []).map((a) => a.name);
  return keepStart((m.content || '') + (names.length ? '\n(' + names.join(', ') + ')' : ''), 1500);
}

// Vrai quand une discussion porte encore le début de sa première question comme titre.
function needsTitle(c) {
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
function serviceReady() {
  return !!state.status && !state.statusError && state.status.state !== 'starting' && !!roleModel('general');
}

/*
    Exécute fn sous un verrou que partagent les onglets, pour qu'un seul onglet fasse le travail ; ne
    fait rien quand un autre onglet le détient.  Là où le navigateur n'a pas de tels verrous, fn
    s'exécute.
*/
function exclusive(name, fn) {
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
async function nameConversation(c) {
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
function compactLater(c) {
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
const warmups = new Map();

function cancelWarmup(id) {
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
async function warmBeginning(c) {
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
function hintText() {
  return t('aiNotice');
}

// Mise en page.
function buildLayout() {
  const app = document.getElementById('app');
  // La question en cours de saisie reste quand la mise en page est reconstruite dans une autre langue.
  const draft = ui.input ? ui.input.value : '';
  app.replaceChildren();
  ui.sidebar = h('aside', { class: 'sidebar' + (state.settings.sidebar === false ? ' collapsed' : '') });
  ui.sideScroll = h('div', { class: 'side-scroll' });
  ui.statusLine = h('div', { class: 'status-line' });
  const search = h('input', { type: 'search', placeholder: t('search'), value: state.search });
  let searchTimer = 0;
  search.addEventListener('input', () => {
    clearTimeout(searchTimer);
    searchTimer = setTimeout(() => {
      state.search = search.value;
      renderSidebar();
    }, searchDelay);
  });
  ui.incognitoButton = h('button', {
    class: 'incognito-toggle' + (state.incognito ? ' on' : ''), type: 'button', title: t('incognitoStart'), html: icon('incognito'),
    onclick: () => {
      state.incognito = !state.incognito;
      leaveCurrent();
      showDiscussion(newConversation(null));
      state.view = 'chat';
      if (location.hash !== '#/') history.pushState(null, '', '#/');
      renderAll();
    }
  });
  ui.sidebar.append(
    h('div', { class: 'brand' },
      h('button', { class: 'brand-home', type: 'button', title: t('home'), 'aria-label': t('home'), onclick: goHome },
        h('img', { src: 'img/albireo-mark.svg', alt: '' }), h('div', { class: 'name', html: '<span>Albireo</span>' })),
      h('div', { class: 'spacer' })),
    h('div', { class: 'sidebar-actions' },
      h('button', {
        class: 'new-chat', type: 'button', html: icon('plus') + '<span>' + escapeHtml(t('newChat')) + '</span>',
        onclick: () => { state.incognito = false; newChat(null); }
      }), ui.incognitoButton),
    h('div', { class: 'search' }, h('span', { html: icon('search') }), search),
    ui.sideScroll,
    h('div', { class: 'sidebar-footer' }, ui.statusLine, h('div', { class: 'footer-buttons' },
      h('button', { type: 'button', onclick: () => settingsDialog('general'), html: icon('gear') + '<span>' + escapeHtml(t('settings')) + '</span>' }),
      h('button', { type: 'button', onclick: () => window.open('/docs/', '_blank'), html: icon('book') + '<span>' + escapeHtml(t('api')) + '</span>' }))));

  ui.topbar = h('header', { class: 'topbar' });
  ui.banner = h('div', { class: 'banner hidden', html: icon('incognito') + '<span>' + escapeHtml(t('incognitoBanner')) + '</span>' });
  ui.content = h('div', { class: 'scroller' });
  ui.input = h('textarea', { rows: 1, 'aria-label': t('placeholder') });
  ui.input.value = draft;
  ui.input.addEventListener('input', () => {
    autoGrow();
    saveDraft();
  });
  ui.input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing && state.settings.sendOnEnter) {
      e.preventDefault();
      onSend();
    }
  });
  ui.input.addEventListener('paste', (e) => {
    const files = pastedFiles(e);
    if (files.length) { e.preventDefault(); addPending(files); }
  });
  const fileInput = h('input', { type: 'file', multiple: true, class: 'hidden' });
  fileInput.addEventListener('change', () => { addPending(Array.from(fileInput.files)); fileInput.value = ''; });
  ui.fileInput = fileInput;
  ui.reasonButton = h('button', {
    class: 'toggle reason', type: 'button', title: t('reasoningHint'), html: icon('spark') + '<span>' + escapeHtml(t('reasoning')) + '</span>',
    onclick: async () => {
      const c = state.current;
      c.reasoning = !c.reasoning;
      // L'agent et le web d'un modèle capable de raisonner se terminent avec son raisonnement approfondi.
      if (!c.reasoning) {
        c.agent = false;
        c.web = false;
      }
      renderComposerState();
      if (c.messages.length) await persist(c);
      refreshViews();
    }
  });
  ui.agentButton = h('button', {
    class: 'toggle', type: 'button', title: t('agentHint'), html: icon('tool') + '<span>' + escapeHtml(t('agent')) + '</span>',
    onclick: async () => {
      const c = state.current;
      c.agent = !c.agent;
      renderComposerState();
      if (c.messages.length) await persist(c);
      refreshViews();
    }
  });
  /*
      L'accès au web de la discussion affichée, désactivé dans chaque nouvelle discussion comme le sont le raisonnement
      approfondi et l'agent : le choix appartient à sa discussion et ne devient jamais la valeur par défaut des
      suivantes.
  */
  ui.webButton = h('button', {
    class: 'toggle web', type: 'button', title: t('webHint'), 'aria-label': t('webAccess'), 'aria-pressed': 'false',
    html: icon('globe') + '<span>' + escapeHtml(t('web')) + '</span>',
    onclick: async () => {
      const c = state.current;
      c.web = !c.web;
      renderComposerState();
      if (c.messages.length) await persist(c);
      refreshViews();
    }
  });
  // Ce que font le mode agent et l'accès au web, et les outils qu'ils donnent, depuis un bouton à côté d'eux.
  ui.agentHelp = h('button', {
    class: 'help-button', type: 'button', title: t('agentHelpTitle'), 'aria-label': t('agentHelpTitle'),
    html: icon('info'), onclick: (e) => agentHelp(e.currentTarget)
  });
  ui.sendButton = h('button', { class: 'send', type: 'button', onclick: onSend });
  ui.pendingBox = h('div', { class: 'composer-attachments hidden' });
  ui.attachButton = iconButton('clip', t('attach'), (e) => attachMenu(e.currentTarget, fileInput, addPending));
  const composer = h('div', { class: 'composer' }, ui.pendingBox, ui.input, h('div', { class: 'row' },
    ui.attachButton, fileInput, ui.reasonButton, ui.agentButton, ui.webButton, ui.agentHelp,
    h('span', { class: 'grow' }), ui.sendButton));
  for (const ev of ['dragenter', 'dragover']) composer.addEventListener(ev, (e) => { e.preventDefault(); composer.classList.add('drag'); });
  for (const ev of ['dragleave', 'drop']) composer.addEventListener(ev, () => composer.classList.remove('drag'));
  composer.addEventListener('drop', (e) => {
    e.preventDefault();
    if (e.dataTransfer && e.dataTransfer.files.length) addPending(Array.from(e.dataTransfer.files));
  });
  ui.hint = h('div', { class: 'hint' }, hintText());
  ui.composerWrap = h('div', { class: 'composer-wrap' }, composer, ui.hint);
  ui.toasts = h('div', { class: 'toast-zone' });
  // Les boutons qui mènent au début et à la fin du fil, par-dessus son coin inférieur droit.
  ui.jumpStart = iconButton('toStart', t('scrollStart'), () => jumpTo(false), 'icon-button jump-start');
  ui.jumpEnd = iconButton('toEnd', t('scrollEnd'), () => jumpTo(true), 'icon-button jump-end');
  ui.jump = h('div', { class: 'jump-buttons' }, ui.jumpStart, ui.jumpEnd);
  const later = () => {
    clearTimeout(ui.jumpTimer);
    ui.jumpTimer = setTimeout(hideJump, jumpDelay);
  };
  ui.jump.addEventListener('mouseleave', later);
  ui.jump.addEventListener('focusout', later);
  ui.stick = true;
  ui.ownTop = 0;
  ui.content.addEventListener('scroll', threadScrolled, { passive: true });
  ui.main = h('main', { class: 'main' }, ui.topbar, ui.banner, ui.content, ui.jump, ui.composerWrap);
  app.append(ui.sidebar, ui.main, ui.toasts);
  ui.composerHeight = -1;
  watchComposer();
}

async function init() {
  // Un échec que le code ne traite pas est journalisé, et un refus de la base est signalé.
  window.addEventListener('unhandledrejection', (e) => {
    const r = e.reason;
    if (r && r.name === 'AbortError') {
      e.preventDefault();
      return;
    }
    console.warn('Albireo: an operation failed', r);
    if (r && (r.name === 'QuotaExceededError' || r.name === 'UnknownError' || r.name === 'InvalidStateError')) {
      storageFailed(r);
    }
  });
  let blocked = false;
  try {
    await store.open();
    state.settings = Object.assign({}, defaults, await store.getSettings());
    // Les notes de la mémoire écrites sous forme de liste par une version antérieure deviennent des phrases l'une après
    // l'autre.
    if (listMemory(state.settings.memory)) {
      state.settings.memory = memoryParagraph(memorySentences(state.settings.memory, true), memoryWords * 2);
      await store.setSetting('memory', state.settings.memory);
    }
    // Le navigateur est invité à garder la base, sans attendre sa réponse.
    requestPersistence();
  } catch (e) {
    // Sans IndexedDB (une fenêtre de navigation privée de certains navigateurs), ou tant qu'un onglet
    // d'une version antérieure la garde ouverte, l'application garde ses données en mémoire, comme
    // dans une discussion privée.
    console.warn('Albireo: the database of the browser is unavailable', e);
    blocked = !!e && e.name === 'BlockedError';
    state.incognito = true;
  }
  // La langue d'Albireo, quand cette interface la suit, avant les premiers textes.
  state.status = await api.status().catch(() => null);
  applyLanguage();
  applyTheme();
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', applyTheme);
  // Les documents qui ont des passages, listés avant la lecture des listes (pruneIndex), qui ne
  // les élague qu'une fois les listes lues en entier.
  const indexed = state.incognito ? [] : await store.chunkDocuments().catch(() => []);
  const pictures = state.incognito ? [] : await store.imageKeys().catch(() => []);
  if (await reloadLists().then(() => true, () => false)) {
    pruneIndex(indexed).catch(() => {});
    pruneImages(pictures).catch(() => {});
    if (!state.incognito) pruneDrafts();
  }
  buildLayout();
  guardFileDrops();
  if (blocked) toast(t('dbBlocked'), true, 30000);
  window.addEventListener('hashchange', route);
  // Le texte en cours de saisie reste quand la page est quittée, rechargée ou cachée.
  window.addEventListener('pagehide', () => saveDraft(true));
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'hidden') saveDraft(true);
  });
  await route();
  /*
      La première lecture de l'état, une fois le service prêt maintenant ou plus tard (pollStatus),
      planifie les questions qu'une visite précédente a laissées à la mémoire, les titres qu'elle a
      laissés à écrire et les descriptions des images.
  */
  await refreshStatus();
  setInterval(refreshStatus, 4000);
  // Les documents des projets sans passages sont indexés, par leurs mots sans modèle d'embedding ;
  // avec un, l'état a déjà lancé leur indexation.
  if (!rag.embeddingModel()) indexProjects();
  // Un onglet qui se ferme rend le bail de la mémoire et dit qu'il ne répond plus.
  window.addEventListener('pagehide', () => {
    if (memoryRunning) releaseMemoryLease().catch(() => {});
    for (const g of generations.values()) {
      if (!g.c.incognito) broadcast({ type: 'busy', id: g.c.id, tab: tabId, busy: false });
    }
  });
}

init();
