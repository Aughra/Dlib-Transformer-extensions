// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les images des discussions et des projets, préparées dans ce navigateur.  Le navigateur
    décode le fichier (PNG, JPEG, WebP, la première image d'un GIF, BMP, AVIF et les autres
    formats qu'il lit), l'image est mise à l'échelle pour que son plus grand côté fasse au plus
    maxSide pixels, puis encodée en JPEG, ou en PNG quand elle a de la transparence ou quand le
    PNG est plus petit, avec une vignette.

    Les discussions et les projets ne gardent que la description de chaque image (id, name,
    type, size, width, height) ; les images elles-mêmes sont des Blobs gardés dans la base de
    données du navigateur (store.js), par leur propriétaire, la discussion ou le projet, ou en
    mémoire pour une discussion incognito.  Les requêtes les reçoivent sous forme d'URL data,
    construites à partir de ces Blobs au moment de l'envoi.  Rien ne quitte le navigateur, sauf
    ces requêtes au service local.
*/

import * as store from './store.js';

export const maxSide = 1536;
// La taille maximale d'un fichier image, vérifiée avant la lecture du fichier.
export const maxImageBytes = 15 * 1024 * 1024;
const thumbSide = 192;
const jpegQuality = 0.9;
const thumbQuality = 0.82;

/*
    Les images que cet onglet a préparées et pas encore gardées : id -> {blob, thumb, owner}.  Une image quitte cette
    table une fois gardée, dans la base de données ou en mémoire, qui la fournit ensuite.
*/
const held = new Map();
// Les images gardées en mémoire seulement, celles des discussions incognito : id -> enregistrement.
const memory = new Map();
// Les URL d'objet des images affichées : id -> {thumb, full}.
const urls = new Map();

// Un canvas de width sur height pixels, hors de la page là où le navigateur en propose un.
function surface(width, height) {
  if (typeof OffscreenCanvas === 'function') return new OffscreenCanvas(width, height);
  const c = document.createElement('canvas');
  c.width = width;
  c.height = height;
  return c;
}

function encode(canvas, type, quality) {
  if (typeof canvas.convertToBlob === 'function') return canvas.convertToBlob({ type, quality });
  return new Promise((resolve, reject) => canvas.toBlob((b) => (b ? resolve(b) : reject(new Error('encode'))), type,
    quality));
}

// Vrai quand un pixel du canvas n'est pas entièrement opaque.
function transparent(ctx, width, height) {
  const data = ctx.getImageData(0, 0, width, height).data;
  for (let i = 3; i < data.length; i += 4) if (data[i] !== 255) return true;
  return false;
}

/*
    L'image dessinée à partir de source (un ImageBitmap ou un canvas) de width sur height pixels,
    mise à l'échelle de maxSide et encodée : {blob, thumb, width, height}.  opaque indique que la
    source n'a pas de transparence, comme un fichier JPEG ou une page d'un PDF.
*/
async function encodeImage(source, width, height, opaque) {
  const scale = Math.min(1, maxSide / Math.max(width, height));
  const w = Math.max(1, Math.round(width * scale));
  const h = Math.max(1, Math.round(height * scale));
  const canvas = surface(w, h);
  const ctx = canvas.getContext('2d', { willReadFrequently: !opaque });
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(source, 0, 0, w, h);
  const alpha = !opaque && transparent(ctx, w, h);
  let blob = await encode(canvas, alpha ? 'image/png' : 'image/jpeg', jpegQuality);
  if (!alpha) {
    // Une capture d'écran ou un dessin de peu de couleurs est souvent plus petit en PNG, et garde ses bords nets.
    const png = await encode(canvas, 'image/png');
    if (png.size < blob.size) blob = png;
  }
  const ts = Math.min(1, thumbSide / Math.max(w, h));
  const tw = Math.max(1, Math.round(w * ts));
  const th = Math.max(1, Math.round(h * ts));
  const small = surface(tw, th);
  const sctx = small.getContext('2d');
  sctx.imageSmoothingEnabled = true;
  sctx.imageSmoothingQuality = 'high';
  sctx.drawImage(canvas, 0, 0, tw, th);
  const thumb = await encode(small, alpha ? 'image/png' : 'image/jpeg', thumbQuality);
  return { blob, thumb, width: w, height: h };
}

// La description d'une image que gardent les discussions et les projets.
function metaOf(name, r, source) {
  return {
    id: store.uid(), name, type: r.blob.type, size: r.blob.size, width: r.width, height: r.height,
    added: Date.now(), source
  };
}

/*
    Prépare un fichier image : décodé par le navigateur avec son orientation, mis à l'échelle,
    encodé, et détenu par cet onglet jusqu'à ce qu'il soit gardé (keep) ou abandonné (drop).
    Renvoie sa description {id, name, type, size, width, height, added, source: {type, size,
    width, height}}, ou lève "image-too-large" pour un fichier de plus de maxImageBytes, avant de
    le lire, et "image-unreadable" quand le navigateur ne sait pas décoder le fichier (HEIC ou
    TIFF par exemple).
*/
export async function prepare(file, name) {
  if (file.size > maxImageBytes) throw new Error('image-too-large');
  let bitmap;
  try {
    bitmap = await createImageBitmap(file, { imageOrientation: 'from-image' });
  } catch (e) {
    throw new Error('image-unreadable');
  }
  try {
    const opaque = /^image\/jpe?g$/i.test(file.type || '');
    const r = await encodeImage(bitmap, bitmap.width, bitmap.height, opaque);
    const meta = metaOf(name || file.name || 'image', r,
      { type: file.type || '', size: file.size, width: bitmap.width, height: bitmap.height });
    held.set(meta.id, { blob: r.blob, thumb: r.thumb, owner: null });
    return meta;
  } finally {
    bitmap.close();
  }
}

// Prépare une image dessinée dans un canvas, comme une page d'un PDF rendue par pdf.js, qui est opaque.
export async function prepareCanvas(canvas, name) {
  const r = await encodeImage(canvas, canvas.width, canvas.height, true);
  const meta = metaOf(name, r, { type: 'application/pdf', size: 0, width: canvas.width, height: canvas.height });
  held.set(meta.id, { blob: r.blob, thumb: r.thumb, owner: null });
  return meta;
}

/*
    Garde les images d'un propriétaire (une discussion ou un projet) que cet onglet détient et
    n'a pas encore gardées : dans la base de données du navigateur quand persistent est vrai, en
    mémoire sinon.  Les images déjà gardées, comme celles d'une question reposée, sont laissées
    telles quelles.  Lève l'erreur de la base de données, les images restant en mémoire pour
    cette visite.
*/
export async function keep(owner, metas, persistent) {
  const records = [];
  for (const m of metas || []) {
    const h = held.get(m.id);
    if (!h) continue;
    h.owner = owner;
    const record = {
      id: m.id, owner, name: m.name, type: m.type, size: m.size, width: m.width, height: m.height,
      added: m.added || Date.now(), blob: h.blob, thumb: h.thumb
    };
    if (persistent) records.push(record);
    else {
      memory.set(m.id, record);
      held.delete(m.id);
    }
  }
  if (!records.length) return;
  await store.putImages(records);
  for (const r of records) held.delete(r.id);
}

// L'enregistrement d'une image : {blob, thumb, ...}, de cet onglet, de la mémoire ou de la base ; null s'il manque.
async function recordOf(id) {
  const h = held.get(id);
  if (h) return h;
  if (memory.has(id)) return memory.get(id);
  try {
    return (await store.getImage(id)) || null;
  } catch (e) {
    return null;
  }
}

// Le Blob d'une image, null quand elle manque dans ce navigateur.
export async function blobOf(id) {
  const r = await recordOf(id);
  return r && r.blob instanceof Blob ? r.blob : null;
}

// Vrai quand l'image est disponible dans ce navigateur.
export async function available(id) {
  return !!(await blobOf(id));
}

async function objectUrl(id, kind) {
  const u = urls.get(id);
  if (u && u[kind]) return u[kind];
  const r = await recordOf(id);
  const blob = r && (kind === 'thumb' ? r.thumb : r.blob);
  if (!(blob instanceof Blob)) return null;
  const entry = urls.get(id) || {};
  if (!entry[kind]) entry[kind] = URL.createObjectURL(blob);
  urls.set(id, entry);
  return entry[kind];
}

// L'adresse de la vignette d'une image, pour la page ; null quand l'image manque.
export function thumbUrl(id) {
  return objectUrl(id, 'thumb');
}

// L'adresse de l'image elle-même, pour la visionneuse ; null quand l'image manque.
export function fullUrl(id) {
  return objectUrl(id, 'full');
}

// L'image sous forme d'URL data, comme une requête la transporte ; null quand l'image manque.
export async function dataUrl(id) {
  const blob = await blobOf(id);
  return blob ? store.dataUrlOf(blob) : null;
}

function release(id) {
  const u = urls.get(id);
  if (u) {
    if (u.thumb) URL.revokeObjectURL(u.thumb);
    if (u.full) URL.revokeObjectURL(u.full);
    urls.delete(id);
  }
}

// Oublie les images préparées et non gardées, comme celles retirées de la zone de saisie ; une image gardée reste.
export function drop(ids) {
  for (const id of ids || []) {
    if (!held.has(id)) continue;
    held.delete(id);
    release(id);
  }
}

// Révoque les URL d'objet des images dont keep ne contient pas l'id, que la page n'affiche plus.
export function releaseExcept(keep) {
  for (const id of Array.from(urls.keys())) if (!keep.has(id)) release(id);
}

// Supprime des images, où qu'elles soient gardées.
export async function remove(ids) {
  const list = Array.from(new Set(ids || []));
  if (!list.length) return;
  for (const id of list) {
    held.delete(id);
    memory.delete(id);
    release(id);
  }
  await store.deleteImageIds(list).catch(() => {});
}

// Supprime les images d'un propriétaire, une discussion ou un projet.
export async function removeOwner(owner) {
  const ids = [];
  for (const [id, h] of held) if (h.owner === owner) ids.push(id);
  for (const [id, r] of memory) if (r.owner === owner) ids.push(id);
  for (const id of ids) {
    held.delete(id);
    memory.delete(id);
    release(id);
  }
  const stored = await store.imageKeys(owner).catch(() => []);
  for (const k of stored) release(k.id);
  await store.deleteImages(owner).catch(() => {});
}

// Oublie toutes les images, après le vidage de la base, sauf les images préparées et non gardées dont keep contient
// l'id.
export function removeAll(keep) {
  const stays = (id) => !!keep && keep.has(id) && held.has(id);
  for (const id of Array.from(urls.keys())) if (!stays(id)) release(id);
  for (const id of Array.from(held.keys())) if (!stays(id)) held.delete(id);
  memory.clear();
}

/*
    Supprime les images stockées auxquelles plus aucun propriétaire ne renvoie.  listed sont les
    images de la base {owner, id} lues avant les discussions et les projets : une image est
    stockée après la description de son propriétaire, de sorte qu'une image stockée entre-temps
    n'est jamais prise pour une image oubliée.  references associe chaque propriétaire aux id
    auxquels il renvoie.
*/
export async function prune(listed, references) {
  const gone = listed.filter((k) => !(references.get(k.owner) || new Set()).has(k.id)).map((k) => k.id);
  if (gone.length) await remove(gone);
  return gone.length;
}

// Les images stockées dans ce navigateur : {count, bytes}.
export function usage() {
  return store.imageUsage();
}
