// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    La recherche documentaire de l'application web, confiée au service.

    Le navigateur n'orchestre plus rien : il envoie au service le texte brut d'un document, et le
    service le découpe en passages, calcule leurs vecteurs avec son modèle d'embedding et les range
    dans le stockage du profil.  Une question est elle aussi envoyée au service, qui rend les
    extraits des documents les plus proches par le sens et par les mots.  La page ne garde ici que
    les appels d'API et, pour la recherche, le calcul du vecteur de la question (envoyé en base64 au
    service pour les moteurs dense et hybrid).

    Les discussions incognito ne font plus de recherche documentaire : leurs passages ne seraient
    jamais rangés côté service, et le navigateur n'indexe plus rien lui-même.
*/

import * as store from './store.js';
import { authHeaders } from './profile.js';

// Le modèle d'embedding du service : {id, dimensions}, ou null.
let model = null;

export function setEmbeddingModel(info) {
  const changed = (model && model.id) !== (info && info.id);
  model = info && info.id ? { id: info.id, dimensions: info.dimensions || 0 } : null;
  // Le modèle change les états d'indexation lus (vecteurs du modèle courant) : on les oublie.
  if (changed) statuses.clear();
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

function decodeVector(base64) {
  const bin = atob(base64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
  return new Float32Array(bytes.buffer);
}

// L'inverse de decodeVector : un Float32Array en base64 (ses octets bruts), pour envoyer le vecteur
// de la question au service.
function b64OfFloat32(arr) {
  const bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
  let bin = '';
  for (let i = 0; i < bytes.length; ++i) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// Les vecteurs de textes, issus du modèle d'embedding du service.  Seul le vecteur de la question
// est encore calculé ici ; les vecteurs des passages sont calculés par le service à l'indexation.
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

// Les états des documents lus dans le service (status), gardés jusqu'à ce que les passages de leur
// propriétaire changent.
const statuses = new Map();

function forget(owner) {
  statuses.delete(owner);
}

/*
    Les suppressions des documents et de leurs propriétaires, comptées : une indexation commencée
    avant la suppression de son document, de son propriétaire ou de tout n'affiche aucun passage.
    Le service range les passages, mais la page ne les compte pas comme indexés si leur document a
    disparu entre-temps.
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
    comptant les passages qui contiennent un vecteur du modèle courant.  Il est lu dans le service
    (store.chunkStatus), et gardé jusqu'à ce que les passages du propriétaire changent.
*/
export async function status(owner) {
  if (!statuses.has(owner)) {
    statuses.set(owner, store.chunkStatus(owner, model ? model.id : null).catch(() => {
      statuses.delete(owner);
      return new Map();
    }));
  }
  return statuses.get(owner);
}

/*
    Indexe un document {id, name, text} pour un propriétaire, entièrement côté service : la page
    envoie le texte brut, le service le découpe, calcule les vecteurs quand il a un modèle
    d'embedding, et range les passages dans le stockage du profil.  La page n'orchestre plus rien.

    onProgress(done, total) suit la progression rendue par le flux ; signal arrête l'indexation
    (AbortController passé au fetch) ; deadline n'a plus d'effet (le service mène l'indexation) mais
    reste accepté pour ne pas casser les appelants.  Un document supprimé (removeDocument,
    removeOwner, removeAll) une fois l'indexation commencée n'est pas compté comme indexé.  Une
    discussion incognito (persistent === false) ne fait pas de recherche documentaire : rien n'est
    indexé, et {passages: 0, vectors: 0} est rendu aussitôt.  Renvoie {passages, vectors}.
*/
export async function indexDocument(owner, doc, options = {}) {
  const { persistent = true, onProgress = null, signal = null } = options;
  // L'incognito ne fait plus de RAG : le service ne garderait pas ses passages.
  if (persistent === false) return { passages: 0, vectors: 0 };

  const stamp = removalStamp(owner, doc.id);
  const removed = () => removalStamp(owner, doc.id) !== stamp;

  let response;
  try {
    response = await fetch(
      '/gulliver/v1/profile/documents/' + encodeURIComponent(doc.id) + '/index',
      {
        method: 'POST',
        headers: authHeaders({ 'Content-Type': 'application/json' }),
        signal,
        body: JSON.stringify({ owner, doc: doc.id, name: doc.name, text: doc.text || '', persistent: true })
      }
    );
  } catch (e) {
    if (e.name === 'AbortError') return { passages: 0, vectors: 0 };
    // Service indisponible : aucun passage indexé.
    forget(owner);
    return { passages: 0, vectors: 0 };
  }
  if (!response.ok || !response.body) {
    forget(owner);
    return { passages: 0, vectors: 0 };
  }

  // Le flux text/event-stream est lu ligne à ligne : les lignes ": progress {...}" portent la
  // progression, et la ligne "data: {...}" finale le décompte {passages, vectors}.
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';
  let result = { passages: 0, vectors: 0 };

  const handleLine = (line) => {
    if (line.startsWith(': progress ')) {
      if (onProgress) {
        try {
          const p = JSON.parse(line.slice(': progress '.length));
          onProgress(p.done || 0, p.total || 0);
        } catch (e) {
          // Ligne de progression illisible : ignorée.
        }
      }
    } else if (line.startsWith('data: ')) {
      const payload = line.slice('data: '.length);
      if (payload === '[DONE]') return;
      try {
        const d = JSON.parse(payload);
        // Le service range les passages même sans vecteur (champ error) : on garde le décompte.
        result = { passages: d.passages || 0, vectors: d.vectors || 0 };
      } catch (e) {
        // data final illisible : on garde le décompte courant.
      }
    }
  };

  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (value) buffer += decoder.decode(value, { stream: true });
      // Les événements sont séparés par une ligne vide ; on traite chaque ligne terminée.
      let nl;
      while ((nl = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, nl).replace(/\r$/, '');
        buffer = buffer.slice(nl + 1);
        if (line) handleLine(line);
      }
      if (done) break;
    }
  } catch (e) {
    if (e.name === 'AbortError') return { passages: 0, vectors: 0 };
    // Flux interrompu : on garde ce qui a été lu.
  }

  forget(owner);
  // Un document supprimé pendant l'indexation n'est pas compté comme indexé.
  if (removed()) return { passages: 0, vectors: 0 };
  return result;
}

export async function removeDocument(owner, docId) {
  countRemoval(docId);
  await store.deleteChunks('doc', docId).catch(() => {});
  forget(owner);
}

export async function removeOwner(owner) {
  countRemoval(owner);
  await store.deleteChunks('owner', owner).catch(() => {});
  forget(owner);
}

// Oublie les états d'indexation gardés quand la base est vidée (store.clearAll) ; les indexations
// en cours ne sont plus comptées comme indexées.
export function removeAll() {
  ++removalsOfAll;
  statuses.clear();
}

/*
    La recherche d'un ou de plusieurs propriétaires, confiée au service : il garde leurs passages
    par profil et connaît les trois moteurs (hybrid, dense, lexical), le moteur et le nombre de
    passages par défaut (rag_engine, rag_top_k) étant des réglages du service, que la page ne
    précise pas.  hybrid et dense ont besoin du vecteur de la question ; la page le calcule quand un
    modèle d'embedding est chargé et l'envoie en base64, et lexical l'ignore.  En cas d'échec du
    calcul (service sans modèle, ou arrêté), le vecteur est simplement omis ; un abort est relancé.

    skip (Set de documents) et exclude (Set d'identifiants de passages) sont filtrés ici, côté
    client, car le service ne les connaît pas : on retire les extraits dont le document est dans
    skip, et ceux dont TOUS les passages sont dans exclude (un extrait dont une partie seulement des
    passages est exclue est gardé tel quel).  Si le service est indisponible, on renvoie [].
*/
async function searchInService(owners, question, options = {}) {
  const { limit = 12, budget = 12000, signal = null, skip = null, exclude = null } = options;
  if (!owners.length || !String(question).trim()) return [];

  // Le vecteur de la question, quand un modèle d'embedding est chargé (pour hybrid et dense).
  let qv = null;
  if (model) {
    try {
      qv = (await embed([questionText(model.id, question)], signal))[0];
    } catch (e) {
      if (e.name === 'AbortError') throw e;
      // Le service a perdu son modèle, ou s'est arrêté : la question part sans vecteur (lexical).
      qv = null;
    }
  }

  let json;
  try {
    const r = await fetch('/gulliver/v1/profile/search', {
      method: 'POST',
      headers: authHeaders({ 'Content-Type': 'application/json' }),
      signal,
      body: JSON.stringify({
        question,
        owners,
        model: model ? model.id : '',
        question_vector: qv ? b64OfFloat32(qv) : '',
        limit,
        budget
      })
    });
    if (!r.ok) return [];
    json = await r.json();
  } catch (e) {
    if (e.name === 'AbortError') throw e;
    // Service indisponible : aucun extrait.
    return [];
  }

  let excerpts = (json && json.excerpts) || [];
  // skip et exclude filtrés côté client (le service ne les connaît pas).
  if (skip) excerpts = excerpts.filter((x) => !skip.has(x.doc));
  if (exclude) excerpts = excerpts.filter((x) => !(x.passages || []).every((id) => exclude.has(id)));
  return excerpts;
}

/*
    La recherche documentaire : tous les propriétaires sont persistants (leurs passages vivent dans
    le service, par profil) et cherchés par le service.  Un propriétaire incognito, s'il en arrivait
    un, n'a pas de passages dans le service et rend simplement zéro extrait : on les passe tous à la
    recherche du service sans distinction.
*/
export async function search(owners, question, options = {}) {
  return searchInService(owners, question, options);
}
