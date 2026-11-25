// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Markdown des réponses : markdown à la GitHub, code coloré par le service (route
    /gulliver/v1/highlight, host/highlight.cpp : fork, 2026-11-25, à la place de highlight.js), et
    texte surligné entre == et ==, comme les champs des questions le montrent (mdfield.js).
    Le HTML qu'un modèle écrit hors du code est affiché comme du texte, et le résultat
    est assaini avant d'atteindre la page.  Une réponse ne charge rien d'une autre origine,
    et affiche une image d'une autre origine comme un lien vers elle, que le navigateur
    ne suit que si l'utilisateur le fait.
*/

import { Marked } from '../vendor/marked/marked.esm.js';
import DOMPurify from '../vendor/dompurify/purify.es.mjs';
import { icon } from './icons.js';

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Si une URL désigne l'origine de l'application, ou des données que la page détient (data:, blob:).
function localSource(url) {
  try {
    const u = new URL(String(url), document.baseURI);
    return u.origin === location.origin || u.protocol === 'data:' || u.protocol === 'blob:';
  } catch (e) {
    return false;
  }
}

// Le texte surligné entre == et ==, sans espace à l'intérieur des marques (repris de l'original, Albireo 1.0.15).
const highlight = {
  name: 'highlight',
  level: 'inline',
  start(src) {
    const i = src.indexOf('==');
    return i < 0 ? undefined : i;
  },
  tokenizer(src) {
    const m = /^==(?=[^\s=])([\s\S]*?[^\s=])==/.exec(src);
    if (!m) return undefined;
    return { type: 'highlight', raw: m[0], text: m[1], tokens: this.lexer.inlineTokens(m[1]) };
  },
  renderer(token) {
    return '<mark>' + this.parser.parseInline(token.tokens) + '</mark>';
  }
};

// Vrai pendant que renderMarkdown dessine une réponse en flux : le code sans langage attend
// la fin de la réponse pour que son langage soit détecté, et un bloc de code dont la clôture
// n'est pas encore arrivée est coloré une fois qu'elle arrive.
let streaming = false;

/*
    La coloration du code, faite par le service.  Un bloc déjà coloré est repris de colored, sans
    attendre ; un autre est d'abord dessiné en texte simple, marqué data-hl (son langage, vide pour
    le deviner), puis colorCode demande son HTML au service et le met en place.  colored garde les
    derniers blocs (clé : langage, puis texte), pour que les dessins suivants d'une réponse en flux
    reprennent leurs couleurs sans clignoter.
*/
const colored = new Map();
const coloring = new Map();
const maxColored = 400;

function colorKey(language, text) {
  return language + '\u0000' + text;
}

// Le HTML du service n'a que du texte échappé dans des <span class="hljs-…"> : la page le vérifie quand même.
function safeColors(html) {
  return DOMPurify.sanitize(html, { ALLOWED_TAGS: ['span'], ALLOWED_ATTR: ['class'] });
}

// Vrai pour un bloc de code délimité dont la clôture manque, le bloc en cours d'écriture.
function openFence(raw) {
  const text = String(raw || '').replace(/\s+$/, '');
  const open = /^ {0,3}(`{3,}|~{3,})/.exec(text);
  if (!open) return false;
  const lines = text.split('\n');
  if (lines.length < 2) return true;
  const last = /^ {0,3}(`{3,}|~{3,})\s*$/.exec(lines[lines.length - 1]);
  return !(last && last[1][0] === open[1][0] && last[1].length >= open[1].length);
}

const marked = new Marked({ gfm: true, breaks: false });
marked.use({
  extensions: [highlight],
  renderer: {
    code({ text, lang, raw }) {
      const language = String(lang || '').trim().split(/\s+/)[0].toLowerCase();
      let body = escapeHtml(text);
      let mark = '';
      // Un bloc marqué comme texte brut ou comme sortie d'un programme reste tel quel ; un bloc sans
      // langage n'est deviné qu'une fois la réponse écrite, et sous 20 000 caractères, comme avant.
      const plain = /^(text|txt|plain|plaintext|output)$/.test(language) || (streaming && openFence(raw))
        || (!language && (streaming || text.length >= 20000));
      if (!plain && text) {
        const known = colored.get(colorKey(language, text));
        if (known != null) body = known;
        else mark = ' data-hl="' + escapeHtml(language) + '"';
      }
      return '<pre><div class="code-head"><span>' + escapeHtml(language || 'text') + '</span>' +
        '<button type="button" data-action="copy-code">' + icon('copy') + '<span>Copy</span></button></div>' +
        '<code class="hljs"' + mark + '>' + body + '</code></pre>';
    },
    html({ text }) {
      return escapeHtml(text);
    },
    // Une image de l'application ou de données garde le rendu par défaut ; une autre devient un lien.
    image({ href, title, text, tokens }) {
      if (localSource(href)) return false;
      const label = tokens ? this.parser.parseInline(tokens, this.parser.textRenderer) : text;
      return '<a href="' + escapeHtml(href) + '"' + (title ? ' title="' + escapeHtml(title) + '"' : '') + '>' +
        escapeHtml(label || href) + '</a>';
    }
  }
});

DOMPurify.addHook('afterSanitizeAttributes', (node) => {
  if (node.tagName === 'A') {
    node.setAttribute('target', '_blank');
    node.setAttribute('rel', 'noopener noreferrer');
  }
  // Les attributs qui chargent une ressource gardent l'origine de l'application, quoi qui les ait écrits.
  if (!node.hasAttribute) return;
  if (node.hasAttribute('srcset')) node.removeAttribute('srcset');
  for (const name of ['src', 'poster', 'background']) {
    if (node.hasAttribute(name) && !localSource(node.getAttribute(name))) node.removeAttribute(name);
  }
});

const purifyOptions = {
  FORBID_TAGS: ['style', 'form', 'input', 'textarea', 'select', 'iframe', 'object', 'embed', 'link', 'meta'],
  FORBID_ATTR: ['action', 'formaction'],
  ADD_ATTR: ['data-action', 'aria-hidden', 'data-hl']
};

// Le HTML d'un texte ; live pour une réponse en flux, redessinée à chacun de ses morceaux.
export function renderMarkdown(text, live) {
  streaming = !!live;
  try {
    return DOMPurify.sanitize(marked.parse(String(text || '')), purifyOptions);
  } finally {
    streaming = false;
  }
}

// Les boutons de copie des blocs de code d'un élément.
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

/*
    Colore les blocs de code d'un élément marqués data-hl : leurs textes partent au service en une
    requête, et chaque bloc reçoit son HTML s'il n'a pas changé entre-temps.  Un échec laisse le
    code en texte simple, lisible.
*/
export function colorCode(root) {
  const codes = [...root.querySelectorAll('code[data-hl]')];
  if (!codes.length) return;
  const wanted = [];
  for (const code of codes) {
    const language = code.dataset.hl;
    const key = colorKey(language, code.textContent);
    if (colored.has(key)) {
      code.innerHTML = colored.get(key);
      code.removeAttribute('data-hl');
    } else if (!coloring.has(key) && !wanted.some((w) => w.key === key)) {
      wanted.push({ key, language, code: code.textContent });
    }
  }
  const batch = wanted.slice(0, 64);
  const request = batch.length ? fetch('/gulliver/v1/highlight', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ blocks: batch.map((w) => ({ code: w.code, language: w.language })) })
  }).then((r) => (r.ok ? r.json() : null)).then((j) => {
    (j && j.blocks || []).forEach((b, i) => {
      if (colored.size >= maxColored) colored.delete(colored.keys().next().value);
      colored.set(batch[i].key, safeColors(b.html));
    });
  }).catch(() => {}).finally(() => {
    for (const w of batch) coloring.delete(w.key);
  }) : Promise.resolve();
  for (const w of batch) coloring.set(w.key, request);
  // Une fois les couleurs arrivées (celles de cette requête ou d'une autre déjà en route), les blocs encore là les reçoivent.
  const pending = codes.map((c) => coloring.get(colorKey(c.dataset.hl, c.textContent))).filter(Boolean);
  Promise.all(pending.concat(request)).then(() => {
    for (const code of codes) {
      if (!code.isConnected || code.dataset.hl == null) continue;
      const known = colored.get(colorKey(code.dataset.hl, code.textContent));
      if (known == null) continue;
      code.innerHTML = known;
      code.removeAttribute('data-hl');
    }
  });
}

export function attachCodeButtons(root, label) {
  colorCode(root);
  for (const b of root.querySelectorAll('button[data-action="copy-code"]')) {
    if (b.dataset.bound) continue;
    b.dataset.bound = '1';
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
