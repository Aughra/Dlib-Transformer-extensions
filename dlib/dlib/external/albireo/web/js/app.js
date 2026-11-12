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

import { t, setLanguage } from './i18n.js';
import { icon } from './icons.js';
import * as store from './store.js';
import { escapeHtml } from './markdown.js';
import * as api from './api.js';

import * as rag from './rag.js';

import * as web from './web.js';
import * as images from './images.js';

import { defaults, state, ui } from './state.js';
import { h, iconButton, storageFailed, toast } from './dom.js';
import { closeMenu } from './dialogs.js';
import { requestPersistence, settingsDialog } from './settings.js';
import { memoryWords } from './context.js';
import { forgetOwner, indexAttachments, indexProjects, pruneImages, pruneIndex } from './indexing.js';
import { attachMenu, guardFileDrops, pastedFiles } from './files.js';

import { listMemory, memoryParagraph, memoryRunning, memorySentences, releaseMemoryLease, tabId } from './memory.js';
import { refreshStatus } from './status.js';
import { reloadLists, searchDelay } from './lists.js';
import { cancelDraftSave, draftKey, drafts, forgetDraft, imageIdsOf, pruneDrafts, saveDraft, storeDraft, storedDraft } from './drafts.js';
import { hideJump, jumpDelay, jumpTo, renderAll, threadScrolled, watchComposer } from './thread.js';
import { addPending, autoGrow, generations, onSend, renderComposerState, stopGeneration, unread } from './composer.js';

import { answeredElsewhere, broadcast, refreshViews } from './tabs.js';
import { agentHelp, renderSidebar } from './sidebar.js';
import { deleted, persist } from './conversations.js';
import { cancelWarmup, compactLater, hintText, serviceReady } from './background.js';

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
export function applyLanguage() {
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

// Mise en page.
export function buildLayout() {
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
