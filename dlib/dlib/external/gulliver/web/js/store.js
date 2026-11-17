// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les données de l'application (fork pour macOS).  Elles ne vivent plus dans la base IndexedDB de ce
    navigateur mais dans le service local, rangées par PROFIL : les discussions avec leurs messages, les
    projets avec leurs instructions et leurs documents, les passages des documents avec leurs vecteurs pour
    la recherche documentaire, les images des discussions et des projets, et les préférences.  Chaque appel
    passe au service l'identifiant du profil courant (profile.js) dans l'en-tête X-Gulliver-Profile ; toutes
    les adresses sont relatives, servies par la même origine que la page.

    Ce module garde l'API publique de l'ancienne version sur IndexedDB (mêmes noms, mêmes signatures, mêmes
    types de retour, tout en Promise), si bien que les autres modules n'ont rien à changer.  Deux gardes
    protègent les ÉCRITURES : en incognito (state.incognito, une discussion privée) ou quand aucun profil
    n'est choisi, on n'écrit rien côté service.  Les LECTURES sans profil, ou que le service en panne fait
    échouer, renvoient une valeur vide, comme le faisait une base indisponible.
*/

import { state } from './state.js';
import { currentProfile, hasProfile } from './profile.js';

const BASE = '/gulliver/v1/profile';

/*
    Un appel au service sur une route de données du profil.  method est le verbe HTTP, path le reste de
    l'adresse après BASE (par exemple '/conversations/<id>'), body l'objet JSON du corps, le cas échéant.
    Ajoute l'en-tête X-Gulliver-Profile, vérifie que la réponse est bonne (sinon lève une erreur qui porte
    son code HTTP, pour distinguer 404 et 409), et renvoie le JSON de la réponse.
*/
async function req(method, path, body) {
  const res = await fetch(BASE + path, {
    method,
    headers: { 'Content-Type': 'application/json', 'X-Gulliver-Profile': currentProfile() || '' },
    body: body ? JSON.stringify(body) : undefined
  });
  if (!res.ok) {
    const e = new Error('service ' + res.status);
    e.status = res.status;
    throw e;
  }
  return res.json();
}

// Un appel qui tolère le 404 : renvoie undefined comme IndexedDB.get le faisait pour une clé absente.
async function getOrUndefined(path) {
  try {
    return await req('GET', path);
  } catch (e) {
    if (e && e.status === 404) return undefined;
    throw e;
  }
}

/*
    Vrai quand une ÉCRITURE de persistance ne doit toucher à rien : en incognito (les données d'une
    discussion privée ne quittent jamais la page) ou sans profil choisi (les gardes sont ailleurs, ici on
    reste sûr).  Commenté partout où c'est utilisé : « en incognito ou sans profil, on n'écrit rien côté
    service ».
*/
function noWrite() {
  return state.incognito || !hasProfile();
}

// --- Conversions base64 <-> ArrayBuffer / Blob, pour les vecteurs et les images. ---

// La forme base64 des octets d'un ArrayBuffer ou d'un tableau typé.
function b64ofBuffer(buffer) {
  const bytes = buffer instanceof ArrayBuffer ? new Uint8Array(buffer) : new Uint8Array(buffer.buffer || buffer);
  let bin = '';
  for (let i = 0; i < bytes.length; ++i) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// L'ArrayBuffer des octets d'une chaîne base64 (chaîne vide -> buffer vide).
function bufferOfB64(text) {
  const bin = atob(text || '');
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
  return bytes.buffer;
}

// La forme base64 des octets d'un Blob.
async function b64ofBlob(blob) {
  return b64ofBuffer(await blob.arrayBuffer());
}

export function uid() {
  const a = new Uint8Array(12);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, '0')).join('');
}

/*
    L'ouverture, gardée parce que beaucoup de modules l'appellent avant de lire ou d'écrire.  Il n'y a plus
    de base à ouvrir : elle résout toujours, même sans profil (les gardes du choix de profil sont ailleurs).
*/
export function open() {
  return Promise.resolve();
}

export async function allConversations() {
  if (!hasProfile()) return [];
  try {
    const r = await req('GET', '/conversations');
    const list = Array.isArray(r.conversations) ? r.conversations : [];
    // Le service les trie déjà par date décroissante ; on re-trie par sécurité.
    list.sort((a, b) => b.updated - a.updated);
    return list;
  } catch (e) {
    return [];
  }
}

export async function getConversation(id) {
  if (!hasProfile()) return undefined;
  try {
    return await getOrUndefined('/conversations/' + encodeURIComponent(id));
  } catch (e) {
    return undefined;
  }
}

export async function putConversation(c) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  // Écriture forcée : pas de révision optimiste (expected_updated absent).
  return req('PUT', '/conversations/' + encodeURIComponent(c.id), {
    updated: c.updated || 0, project: c.projectId || '', doc: c
  });
}

/*
    Modifie sur place des discussions stockées pour que les messages qu'un autre onglet a enregistrés
    entre-temps restent : fn(record) modifie l'enregistrement relu du service et renvoie true pour
    l'enregistrer.  La révision optimiste du service (expected_updated = l'ancien updated) écarte une
    écriture concurrente : sur un 409, on relit et on réessaie.  Seulement la discussion de id quand id est
    donné, sinon toutes.
*/
export async function updateConversations(fn, id) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  const ids = id == null ? (await allConversations()).map((c) => c.id) : [id];
  for (const one of ids) await updateOne(fn, one);
  return undefined;
}

// Applique fn à une discussion, avec quelques essais en cas de conflit de révision (409).
async function updateOne(fn, id) {
  for (let attempt = 0; attempt < 5; ++attempt) {
    const current = await getConversation(id);
    if (!current) return;
    if (!fn(current)) return;
    try {
      await req('PUT', '/conversations/' + encodeURIComponent(id), {
        updated: current.updated || 0, project: current.projectId || '', doc: current,
        expected_updated: current.updated || 0
      });
      return;
    } catch (e) {
      // Sur un conflit de révision, la discussion a changé entre-temps : on la relit et on recommence.
      if (e && e.status === 409) continue;
      throw e;
    }
  }
}

/*
    Supprime une discussion.  Le service ne supprime pas en cascade ses passages ni ses images : on les
    efface donc ensuite, comme le faisait l'ancienne version, pour nettoyer l'index documentaire et les
    images de cette discussion.
*/
export async function deleteConversation(id) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  await req('DELETE', '/conversations/' + encodeURIComponent(id));
  await deleteChunks('owner', id);
  await deleteImages(id);
  return undefined;
}

export async function allProjects() {
  if (!hasProfile()) return [];
  try {
    const r = await req('GET', '/projects');
    // Le service les trie déjà par nom.
    return Array.isArray(r.projects) ? r.projects : [];
  } catch (e) {
    return [];
  }
}

export async function getProject(id) {
  if (!hasProfile()) return undefined;
  try {
    return await getOrUndefined('/projects/' + encodeURIComponent(id));
  } catch (e) {
    return undefined;
  }
}

export async function putProject(p) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  return req('PUT', '/projects/' + encodeURIComponent(p.id), { doc: p });
}

/*
    Supprime un projet.  Le service détache côté base les discussions du projet (par leur colonne), mais le
    JSON des discussions garde son projectId ; comme pour une discussion, les passages et les images du
    projet sont effacés ensuite, comme le faisait l'ancienne version.
*/
export async function deleteProject(id) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  await req('DELETE', '/projects/' + encodeURIComponent(id));
  await deleteChunks('owner', id);
  await deleteImages(id);
  return undefined;
}

// Un passage envoyé au service : ses champs, le vecteur en base64 (ArrayBuffer ou tableau typé), '' si absent.
function toWire(r) {
  return {
    id: r.id, owner: r.owner, doc: r.doc, ordinal: r.ordinal, name: r.name, where: r.where,
    start: r.start, end: r.end, text: r.text, model: r.model,
    vector: r.vector ? b64ofBuffer(r.vector) : ''
  };
}

// Un passage reçu du service : le vecteur base64 est décodé en ArrayBuffer (rag.js le lit en Float32Array).
function fromWire(c) {
  return Object.assign({}, c, { vector: c.vector ? bufferOfB64(c.vector) : null });
}

/*
    Remplace en bloc les passages du document docId par records.  Le vecteur de chaque passage, un
    ArrayBuffer (ou un tableau typé) côté page, part en base64.
*/
export async function replaceChunks(docId, records) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  return req('PUT', '/chunks/' + encodeURIComponent(docId), { records: records.map(toWire) });
}

export async function chunksOf(owner) {
  if (!hasProfile()) return [];
  try {
    const r = await req('GET', '/chunks?owner=' + encodeURIComponent(owner));
    return (Array.isArray(r.chunks) ? r.chunks : []).map(fromWire);
  } catch (e) {
    return [];
  }
}

// Les passages d'un document.
export async function chunksOfDocument(docId) {
  if (!hasProfile()) return [];
  try {
    const r = await req('GET', '/chunks?doc=' + encodeURIComponent(docId));
    return (Array.isArray(r.chunks) ? r.chunks : []).map(fromWire);
  } catch (e) {
    return [];
  }
}

/*
    L'état des documents d'un propriétaire : Map doc -> {passages, vectors, model}, vectors comptant les
    passages porteurs d'un vecteur de model.  Le service rend déjà ces comptes, sans transférer les vecteurs.
*/
export async function chunkStatus(owner, model) {
  const out = new Map();
  if (!hasProfile()) return out;
  try {
    const r = await req('GET', '/chunks/status?owner=' + encodeURIComponent(owner)
      + '&model=' + encodeURIComponent(model || ''));
    for (const row of Array.isArray(r.status) ? r.status : []) {
      out.set(row.doc, { passages: row.passages, vectors: row.vectors, model: row.model });
    }
  } catch (e) {
    // Service indisponible : état vide, comme une base en panne.
  }
  return out;
}

// Les documents qui ont des passages : un {owner, doc} par document.
export async function chunkDocuments() {
  if (!hasProfile()) return [];
  try {
    const r = await req('GET', '/chunks/documents');
    return Array.isArray(r.documents) ? r.documents : [];
  } catch (e) {
    return [];
  }
}

// Supprime les passages d'un propriétaire ('owner') ou d'un document ('doc').
export async function deleteChunks(index, key) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  const param = index === 'owner' ? 'owner=' : 'doc=';
  return req('DELETE', '/chunks?' + param + encodeURIComponent(key));
}

// Les images.
export async function putImages(records) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  if (!records.length) return undefined;
  // Les octets de l'image et de sa vignette, des Blobs, partent en base64 ; une image par requête.
  await Promise.all(records.map(async (r) => req('PUT', '/images/' + encodeURIComponent(r.id), {
    owner: r.owner, name: r.name, type: r.type, size: r.size, width: r.width, height: r.height,
    added: r.added, blob: r.blob instanceof Blob ? await b64ofBlob(r.blob) : '',
    thumb: r.thumb instanceof Blob ? await b64ofBlob(r.thumb) : ''
  })));
  return undefined;
}

/*
    Une image comme l'ancienne base la rendait : {id, owner, name, type, size, width, height, added, blob,
    thumb}, blob et thumb étant des Blobs.  Le service n'a pas de route « une image » unique ; on lit les
    octets par deux adresses binaires (/blob et /thumb), et on en déduit le type depuis le Blob.  Les autres
    métadonnées (name, width...) ne sont pas lues ici : les appelants (images.js) n'ont besoin que de .blob,
    .thumb et parfois .type.  undefined quand l'image manque (404).
*/
export async function getImage(id) {
  if (!hasProfile()) return undefined;
  const key = encodeURIComponent(id);
  try {
    const [blobRes, thumbRes] = await Promise.all([
      fetch(BASE + '/images/' + key + '/blob', { headers: { 'X-Gulliver-Profile': currentProfile() || '' } }),
      fetch(BASE + '/images/' + key + '/thumb', { headers: { 'X-Gulliver-Profile': currentProfile() || '' } })
    ]);
    if (blobRes.status === 404 || thumbRes.status === 404) return undefined;
    if (!blobRes.ok || !thumbRes.ok) return undefined;
    const blob = await blobRes.blob();
    const thumb = await thumbRes.blob();
    return { id, blob, thumb, type: blob.type || '' };
  } catch (e) {
    return undefined;
  }
}

/*
    Les images d'un propriétaire : [{owner, id}].  Sans propriétaire (owner null ou absent), l'ancienne
    version listait TOUTES les images, pour l'élagage au démarrage ; le service n'a pas d'équivalent (une
    adresse « toutes les images » du profil), et il nettoie déjà ses images par cascade.  CHOIX PRAGMATIQUE :
    sans propriétaire, on renvoie [] ; l'élagage des images au démarrage devient alors un no-op côté page.
*/
export async function imageKeys(owner) {
  if (!hasProfile() || owner == null) return [];
  try {
    const r = await req('GET', '/images?owner=' + encodeURIComponent(owner));
    return (Array.isArray(r.images) ? r.images : []).map((x) => ({ owner: x.owner, id: x.id }));
  } catch (e) {
    return [];
  }
}

// Supprime les images d'un propriétaire.
export async function deleteImages(owner) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  return req('DELETE', '/images?owner=' + encodeURIComponent(owner));
}

export async function deleteImageIds(ids) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  if (!ids.length) return undefined;
  await Promise.all(ids.map((id) => req('DELETE', '/images/' + encodeURIComponent(id))));
  return undefined;
}

// Les images stockées : {count, bytes}, les octets des images et de leurs vignettes.
export async function imageUsage() {
  if (!hasProfile()) return { count: 0, bytes: 0 };
  try {
    const r = await req('GET', '/images/usage');
    return { count: r.count || 0, bytes: r.bytes || 0 };
  } catch (e) {
    return { count: 0, bytes: 0 };
  }
}

// Un Blob sous forme d'URL data ("data:image/jpeg;base64,...").
export function dataUrlOf(blob) {
  return new Promise((resolve) => {
    const reader = new FileReader();
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : null);
    reader.onerror = () => resolve(null);
    reader.readAsDataURL(blob);
  });
}

// Le Blob d'une URL data d'une image, décodé ici, puisque la page ne lit aucune adresse hors de son origine ; null
// pour un autre texte.
function blobOfDataUrl(text) {
  const m = /^data:(image\/[a-z0-9.+-]+);base64,([A-Za-z0-9+/=]*)$/i.exec(String(text || ''));
  if (!m) return null;
  const bin = atob(m[2]);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
  return new Blob([bytes], { type: m[1].toLowerCase() });
}

export async function getSettings() {
  if (!hasProfile()) return {};
  try {
    return await req('GET', '/page-settings');
  } catch (e) {
    return {};
  }
}

export async function setSetting(key, value) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  return req('PUT', '/page-settings', { [key]: value });
}

/*
    Le bail d'une ressource entre les onglets de ce navigateur (la mémoire, par exemple).  Les baux ne
    passent plus par le service (un détail propre à ce navigateur, pas aux données du profil) : ils vivent
    dans localStorage, sous 'gulliver.lease.<key>' = {owner, until}.  claim renvoie true quand le bail est
    libre, expiré ou déjà détenu par owner, et le pose ; release l'enlève quand owner le détient.  Chaque
    accès à localStorage est protégé (il peut lever en navigation privée).
*/
function leaseKey(key) {
  return 'gulliver.lease.' + key;
}

function readLease(key) {
  try {
    const raw = localStorage.getItem(leaseKey(key));
    return raw ? JSON.parse(raw) : null;
  } catch (e) {
    return null;
  }
}

export async function claimLease(key, owner, until) {
  const lease = readLease(key);
  if (lease && lease.owner !== owner && lease.until > Date.now()) return false;
  try {
    localStorage.setItem(leaseKey(key), JSON.stringify({ owner, until }));
  } catch (e) {
    // Sans localStorage, le bail n'est pas partagé entre onglets ; on accorde tout de même l'accès à cet onglet.
  }
  return true;
}

export async function releaseLease(key, owner) {
  const lease = readLease(key);
  if (lease && lease.owner === owner) {
    try {
      localStorage.removeItem(leaseKey(key));
    } catch (e) {
      // Sans localStorage, rien à retirer.
    }
  }
  return undefined;
}

/*
    Tout ce qui est stocké, sous forme de fichier JSON dans un Blob construit enregistrement par enregistrement, les
    images en URL data, pour qu'aucune chaîne ne contienne à elle seule tout l'export.  Les enregistrements sont
    lus par les nouvelles fonctions, le format reste identique à l'ancienne version.
*/
export async function exportAll() {
  const parts = ['{"format":"gulliver-export","version":1,"exported":' + Date.now() + ',"conversations":['];
  (await allConversations()).forEach((c, i) => parts.push((i ? ',' : '') + JSON.stringify(c)));
  parts.push('],"projects":[');
  (await allProjects()).forEach((p, i) => parts.push((i ? ',' : '') + JSON.stringify(p)));
  parts.push('],"settings":' + JSON.stringify(await getSettings()) + ',"images":[');
  // Les images, par propriétaire : on énumère les propriétaires (discussions et projets), puis leurs images.
  let first = true;
  const owners = new Set();
  for (const c of await allConversations()) owners.add(c.id);
  for (const p of await allProjects()) owners.add(p.id);
  const seen = new Set();
  for (const owner of owners) {
    for (const k of await imageKeys(owner)) {
      if (seen.has(k.id)) continue;
      seen.add(k.id);
      const record = await getImage(k.id);
      if (!record || !(record.blob instanceof Blob)) continue;
      const picture = {
        id: k.id, owner, type: record.type,
        data: await dataUrlOf(record.blob),
        thumbnail: record.thumb instanceof Blob ? await dataUrlOf(record.thumb) : null
      };
      parts.push((first ? '' : ',') + JSON.stringify(picture));
      first = false;
    }
  }
  parts.push(']}');
  return new Blob(parts, { type: 'application/json' });
}

// Les identifiants que l'application donne à ses enregistrements, que prennent ses adresses (#/c/..., #/p/...).
const recordId = /^[0-9a-f]{1,64}$/;

// Les objets d'une liste, aucun pour une autre valeur.
function objects(list) {
  return Array.isArray(list) ? list.filter((x) => x !== null && typeof x === 'object' && !Array.isArray(x)) : [];
}

function text(value) {
  return typeof value === 'string' ? value : '';
}

/*
    Une discussion d'un export telle que l'application la lit : null sans identifiant valide ou sans messages ; les
    messages autres que les questions et les réponses sont laissés de côté, et les listes et les textes que lisent
    les pages prennent leurs types.
*/
function cleanConversation(c) {
  if (!c || typeof c !== 'object' || typeof c.id !== 'string' || !recordId.test(c.id) || !Array.isArray(c.messages)) {
    return null;
  }
  const messages = [];
  for (const m of objects(c.messages)) {
    if (m.role === 'user') {
      messages.push(Object.assign({}, m, {
        content: text(m.content),
        attachments: objects(m.attachments).filter((a) => typeof a.id === 'string' && typeof a.name === 'string')
          .map((a) => Object.assign({}, a, { text: text(a.text) })),
        images: objects(m.images).filter((x) => typeof x.id === 'string' && typeof x.name === 'string')
      }));
    } else if (m.role === 'assistant') {
      messages.push(Object.assign({}, m, {
        steps: objects(m.steps).map((s) => Object.assign({}, s, {
          content: text(s.content), reasoning: text(s.reasoning), toolCalls: objects(s.toolCalls)
        }))
      }));
    }
  }
  const now = Date.now();
  const index = (v) => Math.min(Math.max(0, Number.isInteger(v) ? v : 0), messages.length);
  return Object.assign({}, c, {
    title: text(c.title), projectId: typeof c.projectId === 'string' ? c.projectId : null, messages,
    created: Number.isFinite(c.created) ? c.created : now, updated: Number.isFinite(c.updated) ? c.updated : now,
    contextStart: index(c.contextStart), memoryMark: index(c.memoryMark),
    summary: c.summary && typeof c.summary.text === 'string' ? c.summary : null, incognito: false
  });
}

// Un projet d'un export tel que l'application le lit, null sans identifiant ou nom valide.
function cleanProject(p) {
  if (!p || typeof p !== 'object' || typeof p.id !== 'string' || !recordId.test(p.id) || typeof p.name !== 'string') {
    return null;
  }
  return Object.assign({}, p, {
    description: text(p.description), instructions: text(p.instructions),
    color: typeof p.color === 'string' ? p.color : '#5b8cff',
    files: objects(p.files).filter((f) => typeof f.id === 'string' && typeof f.name === 'string')
      .map((f) => Object.assign({}, f, { text: text(f.text) })),
    images: objects(p.images).filter((x) => typeof x.id === 'string' && typeof x.name === 'string')
  });
}

/*
    Importe un export : {conversations, projects, images, ids}, les nombres d'enregistrements gardés et les
    identifiants des discussions importées.  Les enregistrements que les pages ne sauraient pas lire sont laissés
    de côté.
*/
export async function importAll(data) {
  if (!data || data.format !== 'gulliver-export' || !Array.isArray(data.conversations)) throw new Error('format');
  let n = 0, p = 0;
  const ids = [];
  for (const raw of data.conversations) {
    const c = cleanConversation(raw);
    if (!c) continue;
    await putConversation(c);
    ids.push(c.id);
    ++n;
  }
  for (const raw of Array.isArray(data.projects) ? data.projects : []) {
    const pr = cleanProject(raw);
    if (!pr) continue;
    await putProject(pr);
    ++p;
  }
  // Les images, chacune avec le propriétaire auquel elle appartient ; un export sans images (une version antérieure)
  // n'en a aucune.
  const pictures = [];
  for (const x of Array.isArray(data.images) ? data.images : []) {
    if (!x || typeof x.id !== 'string' || typeof x.owner !== 'string') continue;
    const blob = blobOfDataUrl(x.data);
    if (!blob) continue;
    const record = Object.assign({}, x, { blob, thumb: blobOfDataUrl(x.thumbnail) || blob });
    delete record.data;
    delete record.thumbnail;
    pictures.push(record);
  }
  await putImages(pictures);
  return { conversations: n, projects: p, images: pictures.length, ids };
}

/*
    Supprime toutes les discussions, tous les projets, et leurs passages et images (deleteConversation et
    deleteProject les effacent en cascade côté page), puis réapplique les réglages de reset.  Le service n'a
    pas d'adresse « tout effacer » : on liste et on supprime.  Les autres réglages restent.
*/
export async function clearAll(reset) {
  // En incognito ou sans profil, on n'écrit rien côté service.
  if (noWrite()) return undefined;
  for (const c of await allConversations()) await deleteConversation(c.id);
  for (const p of await allProjects()) await deleteProject(p.id);
  // Les réglages de reset (la mémoire, le nom et les instructions partent avec les discussions).
  for (const key of Object.keys(reset || {})) await setSetting(key, reset[key]);
  return undefined;
}
