// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Lecture des réponses à voix haute avec la synthèse vocale du service, qui n'a pas besoin
    de réseau : le texte d'une réponse sans son balisage, la langue que montrent ses mots, puis
    des groupes de phrases lus par le service l'un après l'autre, le suivant étant demandé
    pendant que le courant joue.  Une seule réponse est lue à la fois.
*/

import { getJSON, postAudio } from './api.js';

// Le fork pour macOS ne lit que le français.
export function textLanguage() {
  return 'fr';
}

/*
    Le texte d'une réponse tel qu'il est lu : les blocs de code et le raisonnement laissés de
    côté, les liens et les images par leur texte, les titres et les éléments des listes comme
    des phrases, les marques d'emphase, de tableaux et de citations retirées.
*/
export function spokenText(markdown) {
  let t = String(markdown || '');
  t = t.replace(/<think>[\s\S]*?(<\/think>|$)/g, ' ');
  t = t.replace(/```[\s\S]*?(```|$)/g, '\n');
  t = t.replace(/!\[([^\]]*)\]\([^)]*\)/g, '$1');
  t = t.replace(/\[([^\]]+)\]\([^)]*\)/g, '$1');
  t = t.replace(/https?:\/\/\S+/g, ' ');
  t = t.replace(/`([^`\n]*)`/g, '$1');
  t = t.replace(/<[^>\n]*>/g, ' ');
  t = t.replace(/\$\$?([^$\n]+)\$\$?/g, '$1');
  const lines = [];
  for (let line of t.split('\n')) {
    line = line.replace(/^\s{0,3}>\s?/, '');
    if (/^\s*[-:|\s]{3,}$/.test(line)) continue;
    const heading = /^\s{0,3}#{1,6}\s+(.*)$/.exec(line);
    const item = /^\s*(?:[-*+]|\d+[.)])\s+(.*)$/.exec(line);
    if (heading) line = heading[1];
    else if (item) line = item[1];
    line = line.replace(/\|/g, ', ').replace(/(\*\*|__|~~|\*)/g, '').trim();
    if (!line) continue;
    if ((heading || item) && !/[.!?:;,]$/.test(line)) line += '.';
    lines.push(line);
  }
  return lines.join('\n');
}

/*
    Le texte découpé en groupes de phrases d'au plus max caractères, le premier plus court
    pour que la lecture commence vite.
*/
export function speechParts(text, max = 700) {
  const sentences = text.match(/[^.!?…\n]+[.!?…]*[\s]*|\n+/g) || [];
  const parts = [];
  let part = '';
  for (const s of sentences) {
    const limit = parts.length ? max : Math.min(max, 220);
    if (part && part.length + s.length > limit) {
      parts.push(part.trim());
      part = '';
    }
    // Une phrase plus longue qu'un groupe part en morceaux coupés à ses espaces.
    let rest = s;
    while (rest.length > max) {
      let cut = rest.lastIndexOf(' ', max);
      if (cut < max / 2) cut = max;
      parts.push(rest.slice(0, cut).trim());
      rest = rest.slice(cut);
    }
    part += rest;
  }
  if (part.trim()) parts.push(part.trim());
  return parts.filter((p) => /\p{L}|\p{N}/u.test(p));
}

let voicesAsked = null;

/*
    Les langues que le service lit à voix haute, vide quand il n'a pas de voix.  Une liste vide est redemandée à
    l'appel suivant, pour que les voix installées après l'ouverture de la page apparaissent.
*/
export function speechLanguages() {
  if (!voicesAsked) {
    voicesAsked = getJSON('/albireo/v1/speech/voices')
      .then((r) => {
        const list = Array.from(new Set((r.voices || []).map((v) => v.language)));
        if (!list.length) voicesAsked = null;
        return list;
      })
      .catch(() => {
        voicesAsked = null;
        return [];
      });
  }
  return voicesAsked;
}

let current = null;

// La clé de la réponse en cours de lecture, ou null.
export function readingKey() {
  return current ? current.key : null;
}

// Arrête la lecture en cours, s'il y en a une.
export function stopReading() {
  if (!current) return;
  const c = current;
  current = null;
  c.abort.abort();
  c.audio.pause();
  c.audio.removeAttribute('src');
  c.audio.load();
  if (c.url) URL.revokeObjectURL(c.url);
  c.events.end(c.key);
}

function play(audio, signal) {
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      audio.removeEventListener('ended', done);
      audio.removeEventListener('error', failed);
      signal.removeEventListener('abort', aborted);
    };
    const done = () => {
      cleanup();
      resolve();
    };
    const failed = () => {
      cleanup();
      reject(new Error((audio.error && audio.error.message) || 'audio'));
    };
    const aborted = () => {
      cleanup();
      const e = new Error('aborted');
      e.name = 'AbortError';
      reject(e);
    };
    audio.addEventListener('ended', done);
    audio.addEventListener('error', failed);
    signal.addEventListener('abort', aborted);
    audio.play().catch((e) => {
      cleanup();
      reject(e);
    });
  });
}

/*
    Lit text à voix haute dans language ; key désigne la réponse pour les boutons,
    events.start(key), events.end(key) et events.error(key, error) suivent la lecture.  Une
    lecture en cours s'arrête d'abord.
*/
export async function readAloud(text, language, key, events) {
  stopReading();
  const parts = speechParts(text);
  if (!parts.length) return;
  const state = { key, events, abort: new AbortController(), audio: new Audio(), url: null };
  current = state;
  events.start(key);
  const ask = (i) => {
    const p = postAudio('/v1/audio/speech', { model: 'albireo-speech', input: parts[i], voice: language,
      response_format: 'wav' }, state.abort.signal);
    p.catch(() => {});
    return p;
  };
  try {
    let next = ask(0);
    for (let i = 0; i < parts.length; i++) {
      const blob = await next;
      if (current !== state) return;
      next = i + 1 < parts.length ? ask(i + 1) : null;
      if (state.url) URL.revokeObjectURL(state.url);
      state.url = URL.createObjectURL(blob);
      state.audio.src = state.url;
      await play(state.audio, state.abort.signal);
      if (current !== state) return;
    }
    if (current === state) stopReading();
  } catch (e) {
    if (current !== state) return;
    stopReading();
    if (e.name !== 'AbortError') events.error(key, e);
  }
}
