// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Le travail d'arrière-plan sur les documents et les images (fork pour macOS : sorti d'app.js) : l'indexation des
// documents pour la recherche (rag.js), un document à la fois, et les descriptions des images des projets qu'écrit le
// modèle d'arrière-plan.

import * as images from './images.js';
import * as rag from './rag.js';
import * as store from './store.js';
import { backgroundChat, backgroundReadsImages, broadcast, cancelCompaction, cancelWarmup, compactions, exclusive,
  languageNames, projectById, serviceReady, warmups } from './app.js';
import { imageIds, imageThumb } from './files.js';
import { distinctLines, estimateTokens, keepStart, knowledgeSeconds, knowledgeShare, wholeTokens } from './context.js';
import { formatNumber, formatSize, h, iconButton, storageFailed } from './dom.js';
import { icon } from './icons.js';
import { language, t, tn } from './i18n.js';
import { state, ui } from './state.js';

// La recherche dans les documents : les documents en cours d'indexation, un à la fois en arrière-plan.
const indexing = new Map();   // document -> {owner, done, total, abort}
let indexQueue = Promise.resolve();

export function queueIndex(owner, doc, persistent) {
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
export function forgetDocument(owner, docId) {
  cancelIndex(owner, docId);
  return rag.removeDocument(owner, docId);
}

export function forgetOwner(owner) {
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
export async function pruneIndex(indexed) {
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
export function pruneImages(listed) {
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
export function showIndexState(el) {
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
export function cancelDescription(imageId) {
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

export function queueDescription(projectId, imageId) {
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
export function projectImageRow(p, img) {
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
