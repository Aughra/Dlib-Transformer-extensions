// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Markdown des réponses : le service le met en HTML (route /gulliver/v1/markdown, host/markdown.cpp :
    fork, 2026-11-27, à la place de marked et de DOMPurify), code coloré compris.  Son HTML est sûr par
    construction : le HTML qu'un modèle écrit reste du texte, un lien ne prend que http, https, mailto
    ou une adresse de la page, et une image d'une autre origine devient un lien vers elle.

    La page garde les derniers rendus (clé : en cours d'écriture ou non, puis texte) et regroupe les
    demandes d'un même moment en une requête.  Un élément montre d'abord le texte simple, ou son rendu
    précédent pendant l'écriture d'une réponse, puis son HTML dès qu'il arrive ; il n'a qu'une demande
    en route à la fois, et la suivante porte son texte le plus récent.
*/

import { icon } from './icons.js';

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

const rendered = new Map();
const maxRendered = 400;
const keyOf = (text, live) => (live ? '1' : '0') + text;

function remember(key, html) {
  if (rendered.size >= maxRendered) rendered.delete(rendered.keys().next().value);
  rendered.set(key, html);
}

// Le texte simple, en attendant le HTML ou quand le service ne répond pas.
function plainHtml(text) {
  return '<div class="md-plain">' + escapeHtml(text) + '</div>';
}

/*
    Les demandes du moment, envoyées ensemble au prochain tour : une requête par sorte (en cours
    d'écriture ou non), 64 textes au plus chacune.  Un échec rend le texte simple.
*/
let waiting = [];
let scheduled = false;

function flush() {
  scheduled = false;
  const batch = waiting;
  waiting = [];
  for (const live of [true, false]) {
    const all = batch.filter((w) => w.live === live);
    for (let k = 0; k < all.length; k += 64) {
      const part = all.slice(k, k + 64);
      fetch('/gulliver/v1/markdown', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ texts: part.map((w) => w.text), live })
      }).then((r) => (r.ok ? r.json() : null)).catch(() => null).then((j) => {
        part.forEach((w, i) => {
          const html = j && Array.isArray(j.html) && typeof j.html[i] === 'string' ? j.html[i] : null;
          if (html != null) remember(keyOf(w.text, w.live), html);
          w.resolve(html != null ? html : plainHtml(w.text));
        });
      });
    }
  }
}

function request(text, live) {
  const hit = rendered.get(keyOf(text, live));
  if (hit != null) return Promise.resolve(hit);
  return new Promise((resolve) => {
    waiting.push({ text, live, resolve });
    if (!scheduled) {
      scheduled = true;
      setTimeout(flush, 0);
    }
  });
}

// Le HTML d'un texte s'il est déjà rendu, sinon null.
export function renderedMarkdown(text, live) {
  const hit = rendered.get(keyOf(String(text || ''), !!live));
  return hit == null ? null : hit;
}

// Demande d'avance le HTML d'un texte (la réponse qui vient de s'achever, par exemple).
export function prefetchMarkdown(text, live) {
  return request(String(text || ''), !!live);
}

const wanted = new WeakMap();
const busy = new WeakSet();

function apply(el, html, label) {
  el.innerHTML = html;
  attachCodeButtons(el, label);
}

/*
    Met le HTML du Markdown text dans el ; live pour une réponse en cours d'écriture.  label est le texte
    des boutons de copie.  Rend une promesse résolue une fois le HTML en place.
*/
export function fillMarkdown(el, text, live, label) {
  const want = { key: keyOf(String(text || ''), !!live), text: String(text || ''), live: !!live, label };
  wanted.set(el, want);
  const hit = rendered.get(want.key);
  if (hit != null) {
    apply(el, hit, label);
    return Promise.resolve();
  }
  if (!el.hasChildNodes()) el.innerHTML = plainHtml(want.text);
  if (busy.has(el)) return Promise.resolve();
  busy.add(el);
  const go = (w) => request(w.text, w.live).then((html) => {
    const now = wanted.get(el);
    apply(el, html, w.label);
    if (!now || now.key === w.key) {
      busy.delete(el);
      return undefined;
    }
    // Un texte plus récent est arrivé entre-temps : son tour.
    const next = rendered.get(now.key);
    if (next != null) {
      apply(el, next, now.label);
      busy.delete(el);
      return undefined;
    }
    return go(now);
  });
  return go(want);
}

// Hors d'une page sécurisée (l'accès réseau, en http), une zone de texte sélectionnée est copiée au lieu du
// presse-papiers.
function copyBySelection(text) {
  const area = document.createElement('textarea');
  area.value = text;
  area.setAttribute('readonly', '');
  area.style.position = 'fixed';
  area.style.opacity = '0';
  document.body.append(area);
  area.select();
  try {
    document.execCommand('copy');
  } catch (e) {
    // Plus rien à essayer.
  }
  area.remove();
}

// Les boutons de copie des blocs de code d'un élément : leur icône, leur texte, leur action.
export function attachCodeButtons(root, label) {
  for (const b of root.querySelectorAll('button[data-action="copy-code"]')) {
    if (b.dataset.bound) continue;
    b.dataset.bound = '1';
    if (!b.querySelector('svg')) b.insertAdjacentHTML('afterbegin', icon('copy'));
    if (label) b.lastChild.textContent = label;
    b.addEventListener('click', () => {
      const code = b.closest('pre').querySelector('code');
      const text = code ? code.innerText : '';
      if (navigator.clipboard) navigator.clipboard.writeText(text).catch(() => {});
      else copyBySelection(text);
      const span = b.lastChild;
      const before = span.textContent;
      span.textContent = '✓';
      setTimeout(() => { span.textContent = before; }, 1200);
    });
  }
}

export { escapeHtml };
