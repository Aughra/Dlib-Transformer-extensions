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
import { searchPassages, terms } from './documents.js';
import * as rag from './rag.js';
import * as tools from './tools.js';
import * as web from './web.js';
import * as images from './images.js';

import { defaults, state, ui } from './state.js';
import { download, fill, formatSize, h, iconButton, storageFailed, toast } from './dom.js';
import { closeMenu, confirmDialog, promptDialog, showMenu, showPopover } from './dialogs.js';
import { requestPersistence, settingsDialog } from './settings.js';
import { answerReserve, apiMessages, attachmentSeconds, attachmentShare, compact, compactShare, documentsOf,
  estimateMedia, estimateTokens, excerptBudget, keepStart, keptShare, maxContextImages, maxQuestionProjectImages,
  measureImages, memoryWords, requestMedia, systemPrompt, tokenEstimate, wholeTokens } from './context.js';
import { cancelBackground, cancelIndex, forgetDocument, forgetOwner, indexAttachments, indexProjects, knowledgeMode,
  pruneImages, pruneIndex, queueIndex } from './indexing.js';
import { attachMenu, contextImages, guardFileDrops, imageIds, imageLimitText, imageRecord, imageRoom, imageThumb,
  markOutside, pastedFiles, readFiles } from './files.js';
import { newProject } from './projects.js';
import { forgetMemory, listMemory, memoryAbort, memoryParagraph, memoryRunning, memorySentences, releaseMemoryLease,
  rememberLater, tabId } from './memory.js';
import { acceptFor, backgroundChat, backgroundModel, backgroundTimeout, inputsLabel, modelName, readsImages,
  refreshStatus, renderStatusLine, roleCapabilities, roleInputs, roleModel, searchProvider, toolSet, toolsAvailable,
  webUnavailable } from './status.js';
import { dateGroup, excerptHtml, fold, languageNames, markTerms, projectActivity, projectById, queryTerms, reloadLists,
  searchConversations, searchDelay, searchDialog, sideLimit, textLanguage } from './lists.js';
import { cancelDraftSave, draftKey, drafts, forgetDraft, forgetStoredDrafts, imageIdsOf, pruneDrafts, saveDraft,
  storeDraft, storedDraft, waitingImages } from './drafts.js';
import { attachmentChip, fillAssistant, hideJump, jumpDelay, jumpTo, lockThread, renderAll, renderMain, renderTopbar,
  scrollToEnd, threadLocked, threadScrolled, toolIcons, toolLabel, watchComposer } from './thread.js';

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
export function busyHere() {
  return !!generationOf(state.current);
}

// Vrai quand m est la réponse que cet onglet écrit dans la discussion c.
export function liveMessage(c, m) {
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
export const compactions = new Map();

// Arrête la compression en cours des échanges d'une discussion, quand la discussion part.
export function cancelCompaction(id) {
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
