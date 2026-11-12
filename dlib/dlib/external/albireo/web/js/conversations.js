// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les actions sur une discussion (fork pour macOS : sorties d'app.js) : son enregistrement (persist), son menu
// (renommer, épingler, déplacer vers un projet, exporter, supprimer), l'export d'une réponse, la suppression d'une
// question, et la copie à jour d'une discussion qu'un autre onglet a pu changer (liveCopy).

import * as images from './images.js';
import * as store from './store.js';
import * as web from './web.js';
import { answerReserve,
  compact } from './context.js';
import { answering } from './sidebar.js';
import { applyCompaction, cancelCompaction,
  compactions } from './generation.js';
import { broadcast, refreshViews } from './tabs.js';
import { go } from './app.js';
import { cancelWarmup, warmBeginning } from './background.js';
import { confirmDialog, promptDialog, showMenu } from './dialogs.js';
import { download, formatSize, storageFailed, toast } from './dom.js';
import { forgetDocument, forgetOwner } from './indexing.js';
import { forgetDraft } from './drafts.js';
import { generations, stopGeneration, unread } from './composer.js';

import { imageIds } from './files.js';
import { newProject } from './projects.js';
import { renderMain, threadLocked } from './thread.js';
import { roleCapabilities, roleModel } from './status.js';
import { state } from './state.js';
import { t } from './i18n.js';

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
