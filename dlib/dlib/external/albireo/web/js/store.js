// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les données de l'application, gardées dans la base IndexedDB de ce navigateur pour
    l'origine du serveur local : les discussions avec leurs messages, les projets avec leurs
    instructions et leurs documents, les passages des documents avec leurs vecteurs pour la
    recherche documentaire, les images des discussions et des projets, et les préférences.
    Rien n'est envoyé nulle part ; une discussion incognito n'atteint jamais ce module.
*/

const DB_NAME = 'albireo';
const DB_VERSION = 3;
let db = null;

function request(r) {
  return new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}

// L'ouverture en cours, que partagent les appels faits entre-temps.
let opening = null;

/*
    L'erreur d'une ouverture qu'un onglet d'une version antérieure a bloquée.  Le navigateur met en file toutes les
    ouvertures suivantes derrière celle qui est bloquée, si bien que les appels de cette visite échouent aussitôt avec
    cette erreur au lieu d'attendre.
*/
let blocked = null;

export function open() {
  if (db) return Promise.resolve(db);
  if (blocked) return Promise.reject(blocked);
  if (!opening) {
    opening = connect().then((d) => {
      /*
          Quand une version plus récente de l'application met à niveau la base dans un autre onglet,
          cet onglet la lâche et la rouvre à son prochain accès.
      */
      d.onversionchange = () => {
        d.close();
        if (db === d) db = null;
      };
      db = d;
      return d;
    }).finally(() => { opening = null; });
  }
  return opening;
}

async function connect() {
  const r = indexedDB.open(DB_NAME, DB_VERSION);
  r.onupgradeneeded = () => {
    const d = r.result;
    if (!d.objectStoreNames.contains('conversations')) {
      const s = d.createObjectStore('conversations', { keyPath: 'id' });
      s.createIndex('updated', 'updated');
      s.createIndex('project', 'projectId');
    }
    if (!d.objectStoreNames.contains('projects')) d.createObjectStore('projects', { keyPath: 'id' });
    if (!d.objectStoreNames.contains('settings')) d.createObjectStore('settings', { keyPath: 'key' });
    // Les passages des documents : {id, owner (projet ou discussion), doc, ordinal, text,
    // where, model, vector}.
    if (!d.objectStoreNames.contains('chunks')) {
      const c = d.createObjectStore('chunks', { keyPath: 'id' });
      c.createIndex('owner', 'owner');
      c.createIndex('doc', 'doc');
    }
    // Les images des discussions et des projets (images.js) : {id, owner, name, type, size, width,
    // height, added, blob, thumb}, l'image et sa vignette sous forme de Blobs.
    if (!d.objectStoreNames.contains('images')) {
      const i = d.createObjectStore('images', { keyPath: 'id' });
      i.createIndex('owner', 'owner');
    }
  };
  /*
      Un onglet d'une version antérieure qui garde la base ouverte bloque sa mise à niveau : l'ouverture échoue alors
      avec une BlockedError, de même que les appels suivants de cette visite (open).  La connexion que le navigateur
      ouvre une fois cet onglet fermé, avec la base mise à niveau, est fermée aussitôt : la visite suivante lit la
      base.
  */
  return new Promise((resolve, reject) => {
    r.onsuccess = () => {
      if (blocked) r.result.close();
      else resolve(r.result);
    };
    r.onerror = () => reject(r.error);
    r.onblocked = () => {
      const e = new Error('another tab keeps the database open in an earlier version');
      e.name = 'BlockedError';
      blocked = e;
      reject(e);
    };
  });
}

function store(name, mode) {
  return db.transaction(name, mode).objectStore(name);
}

// La fin d'une transaction.
function completed(tx) {
  return new Promise((resolve, reject) => {
    tx.oncomplete = resolve;
    tx.onerror = () => reject(tx.error);
    tx.onabort = () => reject(tx.error);
  });
}

export function uid() {
  const a = new Uint8Array(12);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => b.toString(16).padStart(2, '0')).join('');
}

export async function allConversations() {
  await open();
  const list = await request(store('conversations', 'readonly').getAll());
  list.sort((a, b) => b.updated - a.updated);
  return list;
}

export async function getConversation(id) {
  await open();
  return request(store('conversations', 'readonly').get(id));
}

export async function putConversation(c) {
  await open();
  return request(store('conversations', 'readwrite').put(c));
}

/*
    Modifie sur place des discussions stockées, en une transaction, pour que les messages qu'un
    autre onglet a enregistrés entre-temps restent : fn(record) modifie l'enregistrement lu dans
    la base et renvoie true pour l'enregistrer.  Seulement la discussion de id, quand id est donné.
*/
export async function updateConversations(fn, id) {
  await open();
  const tx = db.transaction('conversations', 'readwrite');
  const r = tx.objectStore('conversations').openCursor(id == null ? null : IDBKeyRange.only(id));
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    if (fn(c.value)) c.update(c.value);
    c.continue();
  };
  await completed(tx);
}

/*
    Supprime une discussion : son enregistrement d'abord, dans une transaction à part, puis ses passages et ses
    images.  Les passages et les images que laisse un onglet fermé entre-temps sont élagués au démarrage suivant
    (app.js, pruneIndex et pruneImages).
*/
export async function deleteConversation(id) {
  await open();
  const tx = db.transaction('conversations', 'readwrite');
  tx.objectStore('conversations').delete(id);
  await completed(tx);
  await deleteChunks('owner', id);
  await deleteImages(id);
}

export async function allProjects() {
  await open();
  const list = await request(store('projects', 'readonly').getAll());
  list.sort((a, b) => a.name.localeCompare(b.name));
  return list;
}

export async function getProject(id) {
  await open();
  return request(store('projects', 'readonly').get(id));
}

export async function putProject(p) {
  await open();
  return request(store('projects', 'readwrite').put(p));
}

export async function deleteProject(id) {
  await open();
  /*
      Les discussions du projet restent, hors de tout projet.  Elles sont modifiées sur place, dans
      une même transaction que la suppression du projet, par l'index de leurs projets.
  */
  const tx = db.transaction(['conversations', 'projects'], 'readwrite');
  const r = tx.objectStore('conversations').index('project').openCursor(IDBKeyRange.only(id));
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    c.value.projectId = null;
    c.update(c.value);
    c.continue();
  };
  tx.objectStore('projects').delete(id);
  await completed(tx);
  await deleteChunks('owner', id);
  await deleteImages(id);
}

/*
    Remplace les passages du document docId par records, en une transaction : un onglet fermé entre-temps laisse
    soit les anciens passages, soit les nouveaux.
*/
export async function replaceChunks(docId, records) {
  await open();
  const tx = db.transaction('chunks', 'readwrite');
  const s = tx.objectStore('chunks');
  const cursor = s.index('doc').openKeyCursor(IDBKeyRange.only(docId));
  cursor.onsuccess = () => {
    const c = cursor.result;
    if (c) {
      s.delete(c.primaryKey);
      c.continue();
      return;
    }
    // Les anciens passages supprimés, les nouveaux sont écrits par la même transaction.
    for (const r of records) s.put(r);
  };
  await completed(tx);
}

export async function chunksOf(owner) {
  await open();
  return request(store('chunks', 'readonly').index('owner').getAll(owner));
}

// Les passages d'un document.
export async function chunksOfDocument(docId) {
  await open();
  return request(store('chunks', 'readonly').index('doc').getAll(docId));
}

/*
    L'état des documents d'un propriétaire, lu passage par passage sans garder les passages : Map doc ->
    {passages, vectors, model}, vectors comptant les passages qui contiennent un vecteur de model.
*/
export async function chunkStatus(owner, model) {
  await open();
  const tx = db.transaction('chunks', 'readonly');
  const out = new Map();
  const r = tx.objectStore('chunks').index('owner').openCursor(IDBKeyRange.only(owner));
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    const v = c.value;
    const s = out.get(v.doc) || { passages: 0, vectors: 0, model: v.model };
    s.passages += 1;
    if (v.vector && model && v.model === model) s.vectors += 1;
    out.set(v.doc, s);
    c.continue();
  };
  await completed(tx);
  return out;
}

// Les documents qui ont des passages : un {owner, doc} par document, lu dans un passage de chacun.
export async function chunkDocuments() {
  await open();
  const tx = db.transaction('chunks', 'readonly');
  const out = [];
  const r = tx.objectStore('chunks').index('doc').openCursor(null, 'nextunique');
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    out.push({ owner: c.value.owner, doc: c.value.doc });
    c.continue();
  };
  await completed(tx);
  return out;
}

// Supprime les passages d'un propriétaire ('owner') ou d'un document ('doc').
export async function deleteChunks(index, key) {
  await open();
  const tx = db.transaction('chunks', 'readwrite');
  const s = tx.objectStore('chunks');
  const cursor = s.index(index).openKeyCursor(IDBKeyRange.only(key));
  cursor.onsuccess = () => {
    const c = cursor.result;
    if (!c) return;
    s.delete(c.primaryKey);
    c.continue();
  };
  await completed(tx);
}

// Les images.
export async function putImages(records) {
  await open();
  if (!records.length) return;
  const tx = db.transaction('images', 'readwrite');
  const s = tx.objectStore('images');
  for (const r of records) s.put(r);
  await completed(tx);
}

export async function getImage(id) {
  await open();
  return request(store('images', 'readonly').get(id));
}

// Les images d'un propriétaire, ou de tous : [{owner, id}], lues dans l'index sans les images.
export async function imageKeys(owner) {
  await open();
  const tx = db.transaction('images', 'readonly');
  const out = [];
  const r = tx.objectStore('images').index('owner').openKeyCursor(owner == null ? null : IDBKeyRange.only(owner));
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    out.push({ owner: c.key, id: c.primaryKey });
    c.continue();
  };
  await completed(tx);
  return out;
}

// Supprime les images d'un propriétaire.
export async function deleteImages(owner) {
  await open();
  const tx = db.transaction('images', 'readwrite');
  const s = tx.objectStore('images');
  const r = s.index('owner').openKeyCursor(IDBKeyRange.only(owner));
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    s.delete(c.primaryKey);
    c.continue();
  };
  await completed(tx);
}

export async function deleteImageIds(ids) {
  await open();
  if (!ids.length) return;
  const tx = db.transaction('images', 'readwrite');
  const s = tx.objectStore('images');
  for (const id of ids) s.delete(id);
  await completed(tx);
}

// Les images stockées : {count, bytes}, les octets des images et de leurs vignettes.
export async function imageUsage() {
  await open();
  const tx = db.transaction('images', 'readonly');
  let count = 0, bytes = 0;
  const r = tx.objectStore('images').openCursor();
  r.onsuccess = () => {
    const c = r.result;
    if (!c) return;
    count += 1;
    for (const b of [c.value.blob, c.value.thumb]) if (b instanceof Blob) bytes += b.size;
    c.continue();
  };
  await completed(tx);
  return { count, bytes };
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
  await open();
  const rows = await request(store('settings', 'readonly').getAll());
  const s = {};
  for (const r of rows) s[r.key] = r.value;
  return s;
}

export async function setSetting(key, value) {
  await open();
  return request(store('settings', 'readwrite').put({ key, value }));
}

/*
    Prend le bail de key pour owner jusqu'à l'instant until, en une transaction, pour que deux
    onglets ne le prennent jamais tous les deux.  Renvoie true quand le bail était libre, expiré
    ou déjà détenu par owner.
*/
export async function claimLease(key, owner, until) {
  await open();
  const tx = db.transaction('settings', 'readwrite');
  const s = tx.objectStore('settings');
  let taken = false;
  const r = s.get(key);
  r.onsuccess = () => {
    const lease = r.result && r.result.value;
    if (lease && lease.owner !== owner && lease.until > Date.now()) return;
    s.put({ key, value: { owner, until } });
    taken = true;
  };
  await completed(tx);
  return taken;
}

// Rend le bail de key, quand owner le détient.
export async function releaseLease(key, owner) {
  await open();
  const tx = db.transaction('settings', 'readwrite');
  const s = tx.objectStore('settings');
  const r = s.get(key);
  r.onsuccess = () => {
    const lease = r.result && r.result.value;
    if (lease && lease.owner === owner) s.put({ key, value: null });
  };
  await completed(tx);
}

/*
    Tout ce qui est stocké, sous forme de fichier JSON dans un Blob construit enregistrement par enregistrement, les
    images en URL data, pour qu'aucune chaîne ne contienne à elle seule tout l'export.
*/
export async function exportAll() {
  await open();
  const parts = ['{"format":"albireo-export","version":1,"exported":' + Date.now() + ',"conversations":['];
  (await allConversations()).forEach((c, i) => parts.push((i ? ',' : '') + JSON.stringify(c)));
  parts.push('],"projects":[');
  (await allProjects()).forEach((p, i) => parts.push((i ? ',' : '') + JSON.stringify(p)));
  parts.push('],"settings":' + JSON.stringify(await getSettings()) + ',"images":[');
  const records = await request(store('images', 'readonly').getAll());
  let first = true;
  for (const r of records) {
    if (!(r.blob instanceof Blob)) continue;
    const { blob, thumb, ...rest } = r;
    const picture = Object.assign(rest, {
      data: await dataUrlOf(blob), thumbnail: thumb instanceof Blob ? await dataUrlOf(thumb) : null
    });
    parts.push((first ? '' : ',') + JSON.stringify(picture));
    first = false;
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
  if (!data || data.format !== 'albireo-export' || !Array.isArray(data.conversations)) throw new Error('format');
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

// Supprime toutes les discussions, projets, passages et images, en une transaction ; les réglages restent.
export async function clearAll() {
  await open();
  const names = ['conversations', 'projects', 'chunks', 'images'];
  const tx = db.transaction(names, 'readwrite');
  for (const name of names) tx.objectStore(name).clear();
  await completed(tx);
}
