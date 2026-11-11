// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Markdown des réponses : markdown à la GitHub, code coloré par highlight.js, formules
    composées par KaTeX entre $...$, $$...$$, \(...\) et \[...\].  Le HTML qu'un modèle
    écrit hors du code est affiché comme du texte, et le résultat est assaini avant
    d'atteindre la page.  Une réponse ne charge rien d'une autre origine, et affiche une image
    d'une autre origine comme un lien vers elle, que le navigateur ne suit que si l'utilisateur
    le fait.
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

function tex(text, display) {
  if (!window.katex) return escapeHtml(display ? '$$' + text + '$$' : '$' + text + '$');
  try {
    return window.katex.renderToString(text, { displayMode: display, throwOnError: false, output: 'html', strict: false });
  } catch (e) {
    return '<code>' + escapeHtml(text) + '</code>';
  }
}

const math = [
  {
    name: 'blockMath',
    level: 'block',
    start(src) {
      const i = src.search(/\$\$|\\\[/);
      return i < 0 ? undefined : i;
    },
    tokenizer(src) {
      const m = /^\$\$([\s\S]+?)\$\$/.exec(src) || /^\\\[([\s\S]+?)\\\]/.exec(src);
      if (m) return { type: 'blockMath', raw: m[0], text: m[1].trim() };
      return undefined;
    },
    renderer(token) {
      return '<div class="math-block">' + tex(token.text, true) + '</div>';
    }
  },
  {
    name: 'inlineMath',
    level: 'inline',
    start(src) {
      const i = src.search(/\$|\\\(/);
      return i < 0 ? undefined : i;
    },
    tokenizer(src) {
      let m = /^\$\$([\s\S]+?)\$\$/.exec(src);
      if (m) return { type: 'inlineMath', raw: m[0], text: m[1].trim(), display: true };
      // Un dollar suivi d'un chiffre après celui qui le ferme est un prix, pas une formule.
      m = /^\$(?![\s$])((?:\\.|[^\\$\n])+?)\$(?!\d)/.exec(src);
      if (m && !/\s$/.test(m[1])) return { type: 'inlineMath', raw: m[0], text: m[1], display: false };
      m = /^\\\(([\s\S]+?)\\\)/.exec(src);
      if (m) return { type: 'inlineMath', raw: m[0], text: m[1].trim(), display: false };
      return undefined;
    },
    renderer(token) {
      return tex(token.text, token.display);
    }
  }
];

// Vrai pendant que renderMarkdown dessine une réponse en flux : le code sans langage attend
// la fin de la réponse pour que son langage soit détecté, et un bloc de code dont la clôture
// n'est pas encore arrivée est coloré une fois qu'elle arrive.
let streaming = false;

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
  extensions: math,
  renderer: {
    code({ text, lang, raw }) {
      const language = String(lang || '').trim().split(/\s+/)[0].toLowerCase();
      let body = escapeHtml(text);
      const hl = window.hljs;
      // Un bloc marqué comme texte brut ou comme sortie d'un programme reste tel quel.
      const plain = /^(text|txt|plain|plaintext|output)$/.test(language) || (streaming && openFence(raw));
      if (hl && !plain) {
        try {
          if (language && hl.getLanguage(language)) body = hl.highlight(text, { language, ignoreIllegals: true }).value;
          else if (!streaming && text.length < 20000) body = hl.highlightAuto(text).value;
        } catch (e) {
          body = escapeHtml(text);
        }
      }
      return '<pre><div class="code-head"><span>' + escapeHtml(language || 'text') + '</span>' +
        '<button type="button" data-action="copy-code">' + icon('copy') + '<span>Copy</span></button></div>' +
        '<code class="hljs">' + body + '</code></pre>';
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
  ADD_ATTR: ['data-action', 'aria-hidden']
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

export function attachCodeButtons(root, label) {
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
