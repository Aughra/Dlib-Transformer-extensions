// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    La recherche documentaire de l'application web, exécutée dans ce navigateur.

    Les documents d'un projet, et les gros documents joints à une discussion, sont découpés
    en passages de quelques centaines de tokens, qui se chevauchent d'un cinquième pour
    qu'aucune idée ne se perde à une frontière, chacun nommé d'après sa page, sa diapositive
    ou sa feuille.  Le modèle d'embedding du service calcule une fois le vecteur de chaque
    passage (/v1/embeddings, les passages de plusieurs requêtes partageant les passes du
    moteur), et les passages sont gardés avec leurs vecteurs dans la base de données du
    navigateur, ou en mémoire pour une discussion incognito.

    Une question reçoit pour réponse les passages qui en sont le plus proches par le sens,
    par leurs vecteurs, et par les mots, par le score BM25 de ses termes, les deux
    classements fusionnés, puis choisis pour qu'ils ne se répètent pas les uns les autres
    (pertinence marginale maximale) dans un budget de caractères.  Un passage loin des
    meilleurs par le sens, ou dont les mots pèsent peu à côté des meilleurs, est laissé de
    côté, pour qu'une question sans rapport avec les documents ne reçoive aucun extrait.  Le
    passage qui suit chacun des meilleurs l'accompagne quand le budget le permet, les
    passages consécutifs d'un document fusionnent en un seul extrait, et les documents
    viennent dans l'ordre de leur meilleur extrait.

    Sans modèle d'embedding, la recherche ne travaille que sur les mots.
*/

import * as store from './store.js';
import { terms } from './documents.js';

export const passageChars = 1200;
export const overlapChars = 240;
const embedBatch = 16;
/*
    Les seuils de la recherche : le cosinus d'un passage et d'une question en dessous duquel ils sont sans rapport,
    mesuré avec Qwen3-Embedding (0.2 à 0.3 pour une question et des passages qui n'y répondent pas, 0.6 et plus pour
    le passage qui y répond) ; l'écart au meilleur cosinus au-delà duquel un passage est laissé de côté ; la part du
    meilleur score par les mots en dessous de laquelle un passage est laissé de côté ; et les meilleurs passages que
    le suivant accompagne.
*/
const minMeaning = 0.35;
const meaningSpread = 0.2;
const wordsShare = 0.3;
const followed = 3;

// Le modèle d'embedding du service : {id, dimensions}, ou null.
let model = null;

export function setEmbeddingModel(info) {
  const changed = (model && model.id) !== (info && info.id);
  model = info && info.id ? { id: info.id, dimensions: info.dimensions || 0 } : null;
  if (changed) {
    cache.clear();
    statuses.clear();
  }
}

export function embeddingModel() {
  return model;
}

/*
    Le texte qu'un modèle reçoit pour une question.  Qwen3-Embedding lit ses questions avec une
    consigne, et ses passages tels quels ; les autres modèles lisent les deux tels quels.
*/
function questionText(id, question) {
  if (/qwen3-embedding/i.test(id)) {
    return 'Instruct: Given a question, retrieve the passages of the documents that answer it\nQuery:' + question;
  }
  return question;
}

function passageText(name, where, text) {
  return name + (where ? ' (' + where + ')' : '') + '\n' + text;
}

function decodeVector(base64) {
  const bin = atob(base64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
  return new Float32Array(bytes.buffer);
}

// Les vecteurs de textes, issus du modèle d'embedding du service.
async function embed(texts, signal) {
  const r = await fetch('/v1/embeddings', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, signal,
    body: JSON.stringify({ model: model.id, input: texts, encoding_format: 'base64' })
  });
  if (!r.ok) {
    let message = r.status + ' ' + r.statusText;
    try {
      const j = await r.json();
      if (j && j.error) message = j.error.message || message;
    } catch (e) {
      // Le corps n'était pas du JSON.
    }
    throw new Error(message);
  }
  const j = await r.json();
  const out = new Array(texts.length);
  for (const d of j.data) out[d.index] = decodeVector(d.embedding);
  return out;
}

/*
    Découpe un texte en passages : [{start, end, where, text}].  Les lignes "## " du texte
    nomment les emplacements (pages, diapositives, feuilles) et les sections des passages ; les
    lignes d'une feuille répètent sa première ligne, les noms de ses colonnes, en tête de chaque
    passage.
*/
export function chunk(text) {
  const out = [];
  const place = /^## (Page \d+|Slide \d+.*|Sheet: .*)$/;
  let location = '', section = '', header = '';
  let buffer = '', bufferStart = 0, bufferWhere = '';
  let pos = 0;

  const where = () => (location && section ? location + ' · ' + section : location || section);
  const emit = (keepTail) => {
    const body = buffer.trim();
    if (body) {
      const prefix = header && !buffer.startsWith(header) ? header + '\n' : '';
      out.push({ start: bufferStart, end: pos, where: bufferWhere, text: prefix + body });
    }
    // La fin du passage commence le suivant, coupée à une ligne ou à une phrase.
    let tail = '';
    if (keepTail && buffer.length > overlapChars) {
      tail = buffer.slice(-overlapChars);
      const cut = tail.search(/\n|[.!?;]\s/);
      if (cut >= 0 && cut < tail.length - 40) tail = tail.slice(cut + 1).trimStart();
    }
    buffer = tail;
    bufferStart = pos - tail.length;
    bufferWhere = where();
  };
  const add = (piece) => {
    if (!buffer) {
      bufferStart = pos;
      bufferWhere = where();
    }
    buffer += piece;
  };

  const lines = text.split('\n');
  for (let li = 0; li < lines.length; ++li) {
    const line = lines[li] + (li + 1 < lines.length ? '\n' : '');
    const m = line.trimEnd().match(place);
    if (m) {
      // Un nouvel emplacement : le passage en cours s'y termine, sans chevauchement.
      emit(false);
      location = m[1];
      section = '';
      header = '';
      pos += line.length;
      bufferStart = pos;
      bufferWhere = where();
      continue;
    }
    // Une section commence son propre passage, sans le chevauchement du précédent, dès que celui-ci est assez long.
    const heading = line.trimEnd().match(/^#{1,6} (.+)$/);
    if (heading) {
      if (buffer.length > passageChars / 3) emit(false);
      section = heading[1].trim().slice(0, 80);
      if (!buffer.trim()) bufferWhere = where();
    }
    if (location.startsWith('Sheet: ') && !header && line.trim()) header = line.trimEnd();
    let rest = line;
    while (rest.length) {
      const room = passageChars - buffer.length;
      // Une ligne courte qui ne tient pas, comme une ligne d'une feuille, commence entière le passage suivant.
      if (rest.length > room && rest === line && line.length <= passageChars / 4 && buffer.trim()) {
        emit(true);
        continue;
      }
      if (rest.length <= room) {
        add(rest);
        pos += rest.length;
        rest = '';
      } else {
        // Une longue ligne est coupée à la dernière phrase ou au dernier espace qui tient.
        let cut = room;
        const slice = rest.slice(0, room);
        const sentence = Math.max(slice.lastIndexOf('. '), slice.lastIndexOf('! '), slice.lastIndexOf('? '));
        if (sentence > room / 2) cut = sentence + 2;
        else {
          const space = slice.lastIndexOf(' ');
          if (space > room / 2) cut = space + 1;
        }
        if (cut <= 0) cut = room > 0 ? room : 0;
        if (cut > 0) {
          add(rest.slice(0, cut));
          pos += cut;
          rest = rest.slice(cut);
        }
        emit(true);
      }
    }
    if (buffer.length >= passageChars) emit(true);
  }
  emit(false);
  return out;
}

// Les propriétaires dont les passages restent en mémoire : les discussions incognito.
const memory = new Map();

/*
    Les passages des derniers propriétaires cherchés, lus une fois : owner -> {records, sparse}, sparse (les termes de
    chaque passage, pour la recherche par les mots) construit à la première recherche.  Le cache garde au plus
    cachedOwners propriétaires, le moins récemment utilisé partant le premier.
*/
const cache = new Map();
const cachedOwners = 8;

async function recordsOf(owner) {
  if (memory.has(owner)) return memory.get(owner);
  try {
    return await store.chunksOf(owner);
  } catch (e) {
    return [];
  }
}

async function load(owner) {
  const hit = cache.get(owner);
  if (hit) {
    cache.delete(owner);
    cache.set(owner, hit);
    return hit;
  }
  const records = (await recordsOf(owner)).slice().sort((a, b) => (a.doc < b.doc ? -1 : a.doc > b.doc ? 1 : a.ordinal - b.ordinal));
  for (const r of records) if (r.vector && !(r.vector instanceof Float32Array)) r.vector = new Float32Array(r.vector);
  const entry = { records, sparse: null };
  cache.set(owner, entry);
  while (cache.size > cachedOwners) cache.delete(cache.keys().next().value);
  return entry;
}

// Les termes de chaque passage d'une entrée du cache, calculés à sa première recherche par les mots.
function sparseOf(entry) {
  if (!entry.sparse) {
    entry.sparse = entry.records.map((r) => {
      const tf = new Map();
      const words = terms(r.text);
      for (const w of words) tf.set(w, (tf.get(w) || 0) + 1);
      return { tf, length: words.length };
    });
  }
  return entry.sparse;
}

// Les passages du document docId de owner, lus sans charger les autres documents de owner.
async function documentRecords(owner, docId, persistent) {
  if (!persistent) return (memory.get(owner) || []).filter((r) => r.doc === docId);
  const entry = cache.get(owner);
  if (entry) return entry.records.filter((r) => r.doc === docId);
  return store.chunksOfDocument(docId).catch(() => []);
}

// Les états des documents lus dans la base (status), gardés jusqu'à ce que les passages de leur propriétaire changent.
const statuses = new Map();

function forget(owner) {
  cache.delete(owner);
  statuses.delete(owner);
}

/*
    Les suppressions des documents et de leurs propriétaires, comptées : une indexation commencée
    avant la suppression de son document, de son propriétaire ou de tout n'écrit aucun passage.
*/
const removals = new Map();
let removalsOfAll = 0;

function removalStamp(owner, docId) {
  return removalsOfAll + (removals.get(owner) || 0) + (removals.get(docId) || 0);
}

function countRemoval(key) {
  removals.set(key, (removals.get(key) || 0) + 1);
}

/*
    L'état des documents d'un propriétaire : Map doc -> {passages, vectors, model}, vectors
    comptant les passages qui contiennent un vecteur du modèle courant.  Il est lu dans les
    passages chargés pour la recherche quand ils le sont, sinon dans la base sans les charger.
*/
export async function status(owner) {
  const entry = cache.get(owner);
  const records = entry ? entry.records : memory.get(owner);
  if (!records) {
    if (!statuses.has(owner)) {
      statuses.set(owner, store.chunkStatus(owner, model ? model.id : null).catch(() => {
        statuses.delete(owner);
        return new Map();
      }));
    }
    return statuses.get(owner);
  }
  const out = new Map();
  for (const r of records) {
    const s = out.get(r.doc) || { passages: 0, vectors: 0, model: r.model };
    s.passages += 1;
    if (r.vector && model && r.model === model.id) s.vectors += 1;
    out.set(r.doc, s);
  }
  return out;
}

/*
    Indexe un document {id, name, text} pour un propriétaire : ses passages, et leurs vecteurs
    quand le service a un modèle d'embedding.  Un document déjà indexé avec le modèle courant
    est laissé tel quel.  onProgress(done, total) suit les vecteurs calculés ; signal arrête les
    vecteurs, comme deadline (un instant de Date.now()), et les passages sans vecteur sont alors
    cherchés par leurs mots jusqu'à ce qu'une indexation les complète.  Un document supprimé
    (removeDocument, removeOwner, removeAll) une fois l'indexation commencée ne garde aucun
    passage.  Renvoie {passages, vectors}.
*/
export async function indexDocument(owner, doc, options = {}) {
  const { persistent = true, onProgress = null, signal = null, deadline = 0 } = options;
  const stamp = removalStamp(owner, doc.id);
  const removed = () => removalStamp(owner, doc.id) !== stamp;
  const pieces = chunk(doc.text || '');
  const existing = await documentRecords(owner, doc.id, persistent);
  const current = model ? model.id : null;
  const complete = existing.length === pieces.length && existing.every((r) => r.model === current && (!current || r.vector));
  if (complete && existing.length) return { passages: existing.length, vectors: current ? existing.length : 0 };

  const records = pieces.map((p, i) => ({
    id: doc.id + ':' + i, owner, doc: doc.id, name: doc.name, ordinal: i, start: p.start, end: p.end,
    where: p.where, text: p.text, model: null, vector: null
  }));
  let vectors = 0;
  if (current) {
    // Les passages déjà calculés avec le modèle courant gardent leurs vecteurs : une indexation arrêtée reprend.
    const known = new Map();
    for (const r of existing) {
      if (r.vector && r.model === current) known.set(r.where + '\n' + r.text, r.vector);
    }
    const missing = [];
    for (const r of records) {
      const v = known.get(r.where + '\n' + r.text);
      if (v) {
        r.vector = v instanceof Float32Array ? v : new Float32Array(v);
        r.model = current;
        ++vectors;
      } else {
        missing.push(r);
      }
    }
    for (let i = 0; i < missing.length; i += embedBatch) {
      if ((signal && signal.aborted) || removed() || (deadline && Date.now() > deadline)) break;
      const batch = missing.slice(i, i + embedBatch);
      try {
        const vs = await embed(batch.map((r) => passageText(r.name, r.where, r.text)), signal);
        batch.forEach((r, k) => {
          r.vector = vs[k];
          r.model = current;
        });
        vectors += batch.length;
      } catch (e) {
        if (e.name === 'AbortError') break;
        // Le service a perdu son modèle, ou s'est arrêté : les passages sont cherchés par leurs mots.
        break;
      }
      if (onProgress) onProgress(vectors, records.length);
    }
  }
  // Les suppressions sont vérifiées juste avant l'écriture, et après elle pour une suppression
  // survenue pendant l'écriture des passages.
  if (removed()) return { passages: 0, vectors: 0 };
  if (persistent) {
    // Les anciens passages et les nouveaux sont échangés en une transaction.
    const stored = records.map((r) => Object.assign({}, r, { vector: r.vector ? r.vector.buffer : null }));
    await store.replaceChunks(doc.id, stored);
    if (removed()) {
      await store.deleteChunks('doc', doc.id).catch(() => {});
      forget(owner);
      return { passages: 0, vectors: 0 };
    }
    memory.delete(owner);
  } else {
    const kept = (memory.get(owner) || []).filter((r) => r.doc !== doc.id);
    memory.set(owner, kept.concat(records));
  }
  forget(owner);
  return { passages: records.length, vectors };
}

export async function removeDocument(owner, docId) {
  countRemoval(docId);
  if (memory.has(owner)) memory.set(owner, memory.get(owner).filter((r) => r.doc !== docId));
  else await store.deleteChunks('doc', docId).catch(() => {});
  forget(owner);
}

export async function removeOwner(owner) {
  countRemoval(owner);
  memory.delete(owner);
  await store.deleteChunks('owner', owner).catch(() => {});
  forget(owner);
}

// Oublie tous les passages gardés en mémoire quand la base est vidée (store.clearAll) ; les
// indexations en cours n'en écrivent aucun.
export function removeAll() {
  ++removalsOfAll;
  memory.clear();
  cache.clear();
  statuses.clear();
}

// La plus longue fin de a qui commence b, pour la fusion des passages qui se chevauchent.
function overlap(a, b) {
  const max = Math.min(a.length, b.length, overlapChars + 40);
  for (let k = max; k >= 16; --k) if (a.endsWith(b.slice(0, k))) return k;
  return 0;
}

/*
    Les extraits des documents de owners les plus proches d'une question :
    [{doc, name, where, text, score, passages}], les documents dans l'ordre de leur meilleur
    extrait, les extraits d'un document dans son ordre, dans budget caractères, limit passages
    au plus, passages nommant les identifiants des passages de chaque extrait.  skip, un Set de
    documents, les exclut de la recherche, comme les documents d'un projet que le modèle lit en
    entier ; exclude, un Set d'identifiants de passages, exclut les passages que le contexte
    contient déjà.
*/
export async function search(owners, question, options = {}) {
  const { limit = 12, budget = 12000, signal = null, skip = null, exclude = null } = options;
  const all = [];
  const sparse = [];
  // L'indice de chaque passage parmi tous, pour le passage qui suit un passage choisi.
  const at = new Map();
  for (const o of owners) {
    const entry = await load(o);
    const entrySparse = sparseOf(entry);
    for (let i = 0; i < entry.records.length; ++i) {
      if (skip && skip.has(entry.records[i].doc)) continue;
      at.set(entry.records[i].id, all.length);
      all.push(entry.records[i]);
      sparse.push(entrySparse[i]);
    }
  }
  if (!all.length || !String(question).trim()) return [];
  const excluded = (i) => !!(exclude && exclude.has(all[i].id));

  // Par les mots : BM25 sur les passages des propriétaires.
  const q = Array.from(new Set(terms(question)));
  const words = new Float64Array(all.length);
  if (q.length) {
    const df = new Map();
    for (const s of sparse) for (const w of q) if (s.tf.has(w)) df.set(w, (df.get(w) || 0) + 1);
    const avg = sparse.reduce((x, s) => x + s.length, 0) / sparse.length || 1;
    const n = all.length;
    for (let i = 0; i < n; ++i) {
      let score = 0;
      for (const w of q) {
        const f = sparse[i].tf.get(w) || 0;
        if (!f) continue;
        const d = df.get(w) || 0;
        score += Math.log(1 + (n - d + 0.5) / (d + 0.5)) * (f * 2.2) / (f + 1.2 * (0.25 + 0.75 * sparse[i].length / avg));
      }
      words[i] = score;
    }
  }

  // Par le sens : le cosinus des vecteurs, tous de longueur unité.
  const meaning = new Float64Array(all.length).fill(-2);
  let questionVector = null;
  if (model && all.some((r) => r.vector && r.model === model.id)) {
    try {
      questionVector = (await embed([questionText(model.id, question)], signal))[0];
    } catch (e) {
      if (e.name === 'AbortError') throw e;
      questionVector = null;
    }
  }
  if (questionVector) {
    const d = questionVector.length;
    for (let i = 0; i < all.length; ++i) {
      const v = all[i].vector;
      if (!v || all[i].model !== model.id || v.length !== d) continue;
      let dot = 0;
      for (let k = 0; k < d; ++k) dot += v[k] * questionVector[k];
      meaning[i] = dot;
    }
  }

  /*
      Fusion par rang réciproque des deux classements, chacun sur les passages proches de son meilleur : par le sens,
      un cosinus d'au moins minMeaning et à moins de meaningSpread du meilleur ; par les mots, un score d'au moins
      wordsShare du meilleur.  Une question qui ne correspond à aucun passage d'aucune des deux façons n'en reçoit
      aucun.
  */
  const fused = new Float64Array(all.length);
  const rank = (scores, floor) => {
    const order = Array.from(scores.keys()).filter((i) => scores[i] > floor && !excluded(i))
      .sort((a, b) => scores[b] - scores[a]);
    order.slice(0, 60).forEach((i, r) => { fused[i] += 1 / (60 + r); });
  };
  let bestWords = 0;
  for (let i = 0; i < all.length; ++i) bestWords = Math.max(bestWords, words[i]);
  if (bestWords > 0) rank(words, Math.max(0, wordsShare * bestWords));
  if (questionVector) {
    let bestMeaning = -1;
    for (let i = 0; i < all.length; ++i) bestMeaning = Math.max(bestMeaning, meaning[i]);
    if (bestMeaning >= minMeaning) rank(meaning, Math.max(minMeaning, bestMeaning - meaningSpread) - 1e-9);
  }
  const candidates = Array.from(fused.keys()).filter((i) => fused[i] > 0).sort((a, b) => fused[b] - fused[a]).slice(0, 60);
  if (!candidates.length) return [];

  // Pertinence marginale maximale : chaque passage choisi pour sa pertinence, moins sa ressemblance
  // avec les passages déjà choisis.
  const top = fused[candidates[0]];
  const chosen = [];
  let used = 0;
  const similarity = (a, b) => {
    const va = all[a].vector, vb = all[b].vector;
    if (va && vb && va.length === vb.length) {
      let dot = 0;
      for (let k = 0; k < va.length; ++k) dot += va[k] * vb[k];
      return dot;
    }
    return all[a].doc === all[b].doc && Math.abs(all[a].ordinal - all[b].ordinal) <= 1 ? 0.6 : 0;
  };
  const pool = candidates.slice();
  while (pool.length && chosen.length < limit) {
    let best = -1, bestScore = -Infinity;
    for (let k = 0; k < pool.length; ++k) {
      const i = pool[k];
      let like = 0;
      for (const j of chosen) like = Math.max(like, similarity(i, j));
      const score = 0.75 * (fused[i] / top) - 0.25 * like;
      if (score > bestScore) {
        bestScore = score;
        best = k;
      }
    }
    const i = pool.splice(best, 1)[0];
    if (used + all[i].text.length > budget && chosen.length) continue;
    chosen.push(i);
    used += all[i].text.length;
  }

  // Le passage qui suit chacun des meilleurs le prolonge, dans le budget et la limite.
  const taken = new Set(chosen);
  for (const i of chosen.slice(0, followed)) {
    const next = at.get(all[i].doc + ':' + (all[i].ordinal + 1));
    if (next == null || taken.has(next) || excluded(next) || chosen.length >= limit) continue;
    if (used + all[next].text.length > budget) continue;
    chosen.push(next);
    taken.add(next);
    used += all[next].text.length;
    fused[next] = Math.max(fused[next], fused[i] * 0.5);
  }

  // Les extraits de chaque document dans son ordre, les passages consécutifs fusionnés.
  chosen.sort((a, b) => (all[a].doc < all[b].doc ? -1 : all[a].doc > all[b].doc ? 1 : all[a].ordinal - all[b].ordinal));
  const firstLine = (text) => text.slice(0, Math.max(0, text.indexOf('\n')));
  const out = [];
  for (const i of chosen) {
    const r = all[i];
    const last = out[out.length - 1];
    if (last && last.doc === r.doc && last.lastOrdinal + 1 === r.ordinal) {
      // Les passages d'une feuille commencent par les noms de ses colonnes, que l'extrait contient une seule fois.
      let text = r.text;
      const head = firstLine(last.text);
      if (head && /^Sheet: /.test(r.where || '') && firstLine(text) === head) text = text.slice(head.length + 1);
      const k = overlap(last.text, text);
      last.text += (k ? '' : '\n') + text.slice(k);
      last.lastOrdinal = r.ordinal;
      last.score = Math.max(last.score, fused[i]);
      last.passages.push(r.id);
      continue;
    }
    out.push({ doc: r.doc, name: r.name, where: r.where, text: r.text, score: fused[i], lastOrdinal: r.ordinal,
      passages: [r.id] });
  }
  // Les documents dans l'ordre de leur meilleur extrait.
  const bestOf = new Map();
  for (const x of out) bestOf.set(x.doc, Math.max(bestOf.get(x.doc) || 0, x.score));
  const order = Array.from(bestOf.keys()).sort((a, b) => bestOf.get(b) - bestOf.get(a));
  const rankOf = new Map(order.map((d, k) => [d, k]));
  out.sort((a, b) => rankOf.get(a.doc) - rankOf.get(b.doc) || a.lastOrdinal - b.lastOrdinal);
  return out.map(({ doc, name, where, text, score, passages }) => ({ doc, name, where, text, score, passages }));
}
