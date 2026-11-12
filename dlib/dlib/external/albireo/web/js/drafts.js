// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les brouillons de la zone de saisie (fork pour macOS : sortis d'app.js) : le texte tapé pour chaque discussion,
// gardé dans localStorage une fois que l'utilisateur cesse de taper, et les images en attente qu'ils retiennent.

import * as images from './images.js';
import { deleted } from './conversations.js';
import { state, ui } from './state.js';

/*
    Les brouillons de la zone de saisie, un par discussion : le texte tapé et les documents et images en attente
    d'envoi.  La zone de saisie contient le brouillon de la discussion affichée (ui.input, state.pending), drafts ceux
    des autres discussions de cet onglet, par leur clé (draftKey).  La discussion qu'ouvre "New chat" garde son
    brouillon sous newDraft jusqu'à ce que sa première question crée la discussion.  Les textes restent d'un
    rechargement de la page à l'autre dans localStorage, une clé par discussion, retirée une fois sa question envoyée ou
    la discussion supprimée ; les fichiers restent dans la mémoire de l'onglet.  Une discussion privée ne garde aucun
    brouillon.
*/
export const drafts = new Map();
const newDraft = 'new';
const draftPrefix = 'albireo-draft-';
// Le texte tapé est gardé ce nombre de millisecondes après la dernière touche.
const draftDelay = 400;
let draftTimer = 0;

// La clé du brouillon de c : son id, newDraft tant qu'elle n'est pas enregistrée, '' pour une discussion privée ou
// supprimée.
export function draftKey(c) {
  if (!c || c.incognito || deleted.has(c.id)) return '';
  return c.messages.length || state.conversations.some((x) => x.id === c.id) ? c.id : newDraft;
}

// Le texte du brouillon key que garde localStorage, '' s'il n'y en a pas.
export function storedDraft(key) {
  if (!key) return '';
  try {
    return localStorage.getItem(draftPrefix + key) || '';
  } catch (e) {
    return '';
  }
}

// Garde le texte du brouillon key dans localStorage, ou le retire quand le texte ne contient rien à envoyer.
export function storeDraft(key, text) {
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
export function saveDraft(now) {
  clearTimeout(draftTimer);
  draftTimer = 0;
  if (!now) {
    draftTimer = setTimeout(() => saveDraft(true), draftDelay);
    return;
  }
  if (ui.input) storeDraft(draftKey(state.current), ui.input.value);
}

// Arrête l'enregistrement du brouillon que saveDraft attend, sans l'enregistrer.
export function cancelDraftSave() {
  clearTimeout(draftTimer);
  draftTimer = 0;
}

// Les ids des images d'une liste de pièces jointes.
export function imageIdsOf(list) {
  return list.filter((a) => a.kind === 'image').map((a) => a.id);
}

// Les images en attente dans les brouillons qui restent une fois tout supprimé : celles de la zone de saisie et de "New
// chat".
export function waitingImages() {
  const d = drafts.get(newDraft);
  return new Set(imageIdsOf(state.pending.concat(d ? d.files : [])));
}

// Oublie le brouillon key, une fois sa question envoyée ou sa discussion supprimée, ici ou dans un autre onglet.
export function forgetDraft(key) {
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
export function pruneDrafts() {
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
