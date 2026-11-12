// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La navigation (fork pour macOS : sortie d'app.js) : les adresses de la page (#/c/…, #/p/…, #/licenses), le
// passage d'une discussion à une autre avec leurs brouillons, une nouvelle discussion, l'accueil et les discussions
// privées.

import * as images from './images.js';
import * as store from './store.js';
import * as web from './web.js';
import { cancelWarmup, compactLater, serviceReady } from './background.js';
import { autoGrow, generations, stopGeneration, unread } from './composer.js';
import { deleted } from './conversations.js';
import { closeMenu } from './dialogs.js';
import { narrowScreen, storageFailed } from './dom.js';
import { cancelDraftSave, draftKey, drafts, forgetDraft, imageIdsOf, storedDraft, storeDraft } from './drafts.js';
import { forgetOwner, indexAttachments } from './indexing.js';
import { state, ui } from './state.js';
import { answeredElsewhere } from './tabs.js';
import { renderAll } from './thread.js';

/*
    Affiche la discussion c à la place de celle qui est affichée, chacune avec son propre brouillon.  Le brouillon de
    celle qui est affichée quitte la zone de saisie pour drafts et localStorage, ou quitte la mémoire pour une
    discussion privée ou supprimée ; le brouillon de c prend sa place : celui que cet onglet garde, sinon le texte de
    localStorage, sinon rien.
*/
export function showDiscussion(c) {
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
export function newConversation(projectId) {
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
export function leaveCurrent() {
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
export function goHome() {
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

export async function route() {
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
  // Sur un écran étroit, le panneau latéral, qui recouvre la page, se ferme une fois la vue choisie.
  if (narrowScreen() && ui.sidebar) ui.sidebar.classList.add('collapsed');
  renderAll();
}
