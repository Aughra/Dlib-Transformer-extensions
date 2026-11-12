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

import { t, setLanguage, language } from './i18n.js';
import { icon } from './icons.js';
import * as store from './store.js';
import { escapeHtml } from './markdown.js';
import * as api from './api.js';

import * as rag from './rag.js';
import * as tools from './tools.js';
import * as web from './web.js';
import * as images from './images.js';

import { defaults, state, ui } from './state.js';
import { download, formatSize, h, iconButton, storageFailed, toast } from './dom.js';
import { closeMenu, confirmDialog, promptDialog, showMenu, showPopover } from './dialogs.js';
import { requestPersistence, settingsDialog } from './settings.js';
import { answerReserve, apiMessages, compact, compactShare, estimateMedia, keepStart, memoryWords, requestMedia,
  systemPrompt, tokenEstimate } from './context.js';
import { cancelBackground, cancelIndex, forgetDocument, forgetOwner, indexAttachments, indexProjects, pruneImages,
  pruneIndex } from './indexing.js';
import { attachMenu, guardFileDrops, imageIds, markOutside, pastedFiles } from './files.js';
import { newProject } from './projects.js';
import { forgetMemory, listMemory, memoryParagraph, memoryRunning, memorySentences, releaseMemoryLease, rememberLater,
  tabId } from './memory.js';
import { backgroundChat, backgroundModel, backgroundTimeout, refreshStatus, renderStatusLine, roleCapabilities,
  roleModel, searchProvider, toolSet } from './status.js';
import { dateGroup, excerptHtml, fold, languageNames, markTerms, projectActivity, projectById, queryTerms, reloadLists,
  searchConversations, searchDelay, searchDialog, sideLimit, textLanguage } from './lists.js';
import { cancelDraftSave, draftKey, drafts, forgetDraft, forgetStoredDrafts, imageIdsOf, pruneDrafts, saveDraft,
  storeDraft, storedDraft, waitingImages } from './drafts.js';
import { hideJump, jumpDelay, jumpTo, lockThread, renderAll, renderMain, renderTopbar, threadLocked, threadScrolled,
  toolIcons, toolLabel, watchComposer } from './thread.js';
import { addPending, autoGrow, generations, onSend, renderComposerState, stopAllGenerations, stopGeneration, unread } from './composer.js';
import { applyCompaction, cancelCompaction, compactions, currentVersion, keptTokens, nextStart, settleCompaction } from './generation.js';

export const roles = ['general', 'code', 'security'];
export const roleIcons = { general: 'compass', code: 'code', security: 'shield' };
export const projectColors = ['#5b8cff', '#f2b544', '#3fb68b', '#e5534b', '#a371f7', '#2fb9d4', '#e08a3c', '#8a90aa'];
// Le nombre maximal d'étapes d'une réponse avec outils : chaque étape est une requête au modèle, puis les appels qu'il
// demande.
export const maxAgentSteps = 100;

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

// Panneau latéral.
export function renderSidebar() {
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
export function answering(c) {
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
export function liveCopy(c) {
  const g = generations.get(c.id);
  if (g) return g.c;
  return state.current && state.current.id === c.id ? state.current : c;
}

export function conversationMenu(entry, anchor) {
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

export function moveMenu(c, anchor) {
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
export async function persist(c) {
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
export function exportAnswer(c, index, format) {
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
export async function deleteQuestion(c, index) {
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
export function attachmentIds(messages) {
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

/*
    Affiche la discussion c à la place de celle qui est affichée, chacune avec son propre brouillon.  Le brouillon de
    celle qui est affichée quitte la zone de saisie pour drafts et localStorage, ou quitte la mémoire pour une
    discussion privée ou supprimée ; le brouillon de c prend sa place : celui que cet onglet garde, sinon le texte de
    localStorage, sinon rien.
*/
function showDiscussion(c) {
  cancelDraftSave();
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
export async function keepImages(c, list) {
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
export function newChat(projectId) {
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
  cancelDraftSave();
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
