// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les onglets ouverts sur la page (fork pour macOS : sortis d'app.js) : les messages qu'ils s'envoient (réglages,
// discussions et projets changés ou supprimés, réponses en cours), la relecture de la base qui s'ensuit, et les marques
// des onglets qui sont en train de répondre.

import * as api from './api.js';
import * as images from './images.js';
import * as rag from './rag.js';
import * as store from './store.js';
import * as web from './web.js';
import { applyLanguage, applyTheme, buildLayout, go } from './app.js';
import { cancelWarmup } from './background.js';
import { deleted } from './conversations.js';
import { renderSidebar } from './sidebar.js';
import { cancelBackground, cancelIndex } from './indexing.js';
import { cancelCompaction } from './generation.js';
import { defaults, state, ui } from './state.js';
import { forgetDraft, forgetStoredDrafts, waitingImages } from './drafts.js';
import { forgetMemory, rememberLater, tabId } from './memory.js';
import { generations, renderComposerState, stopAllGenerations, stopGeneration, unread } from './composer.js';
import { language } from './i18n.js';
import { lockThread, renderAll, renderMain, renderTopbar } from './thread.js';
import { projectById, reloadLists } from './lists.js';
import { renderStatusLine } from './status.js';
import { storageFailed } from './dom.js';

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
export async function languageChanged(force) {
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
export function keepOwnCopies() {
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
export function answeredElsewhere(c) {
  const mark = c ? othersBusy.get(c.id) : null;
  if (mark && mark.until < Date.now()) othersBusy.delete(c.id);
  return !!(mark && mark.until >= Date.now());
}

// Oublie les marques des onglets qui ont cessé de dire qu'ils répondent ; vrai quand l'une a expiré.
export function expireBusy() {
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
export function announceBusy(c, busy) {
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
