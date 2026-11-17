// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les outils de la page que partagent ses modules (fork pour macOS) : la création des éléments (h), les tailles, nombres
// et durées écrits en français, les messages passagers (toast), le presse-papiers et les fichiers à enregistrer.

import { language, t } from './i18n.js';
import { icon } from './icons.js';
import { ui } from './state.js';

// Vrai sur un écran étroit, un téléphone par exemple, où le panneau latéral passe par-dessus la page (app.css).
export function narrowScreen() {
  return window.matchMedia('(max-width: 860px)').matches;
}

// Fonctions utilitaires.
export function h(tag, props, ...children) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'html') el.innerHTML = v;
      else if (k === 'text') el.textContent = v;
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'style' && typeof v === 'object') {
        // Les propriétés personnalisées ("--c") sont définies par leur nom.
        for (const [name, value] of Object.entries(v)) {
          if (name.startsWith('--')) el.style.setProperty(name, value);
          else el.style[name] = value;
        }
      }
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  for (const c of children.flat(Infinity)) {
    if (c == null || c === false) continue;
    el.append(c.nodeType ? c : document.createTextNode(String(c)));
  }
  return el;
}

// Met des enfants dans el à la place des siens, sans les null et les false, comme h() les laisse de côté.
export function fill(el, ...children) {
  el.replaceChildren(...children.flat(Infinity).filter((c) => c != null && c !== false));
}

export function iconButton(name, title, onclick, cls) {
  return h('button', { class: cls || 'icon-button', title, 'aria-label': title, type: 'button', onclick, html: icon(name) });
}

// Les unités des tailles dans la langue de l'interface : le français les écrit en octets.
function sizeUnits() {
  return language() === 'fr' ? ['o', 'Ko', 'Mo', 'Go'] : ['B', 'KB', 'MB', 'GB'];
}

// Une taille en octets, avec le séparateur décimal et les unités de la langue de l'interface : "2.4 MB", "2,4 Mo".
export function formatSize(n) {
  const units = sizeUnits();
  if (n < 1024) return formatNumber(n) + ' ' + units[0];
  if (n < 1048576) return formatDecimal(n / 1024, 1) + ' ' + units[1];
  if (n < 1073741824) return formatDecimal(n / 1048576, 1) + ' ' + units[2];
  return formatDecimal(n / 1073741824, 2) + ' ' + units[3];
}

// Une limite de taille en mégaoctets entiers, "100 MB", "100 Mo".
export function formatLimit(n) {
  return formatNumber(Math.round(n / 1048576)) + ' ' + sizeUnits()[2];
}

export function formatDuration(ms) {
  const s = Math.round(ms / 1000);
  if (s < 60) return s + ' s';
  return Math.floor(s / 60) + ' min ' + (s % 60) + ' s';
}

export function formatNumber(n) {
  return new Intl.NumberFormat(language()).format(n);
}

// Un nombre avec digits décimales, écrit comme la langue de l'interface l'écrit : "23.5", "23,5".
export function formatDecimal(n, digits) {
  return new Intl.NumberFormat(language(), { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(n);
}

// Une durée courte, comme le temps qu'a pris un outil, en millisecondes en dessous d'une seconde.
export function formatShortDuration(ms) {
  if (ms < 1000) return Math.max(1, Math.round(ms)) + ' ms';
  if (ms >= 60000) return formatDuration(ms);
  const n = new Intl.NumberFormat(language(), { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  return n.format(ms / 1000) + ' s';
}

export function toast(message, error, ms) {
  if (!ui.toasts) return;
  const el = h('div', { class: 'toast' + (error ? ' error' : ''), role: error ? 'alert' : 'status' }, message);
  ui.toasts.append(el);
  setTimeout(() => el.remove(), ms || (error ? 6000 : 2600));
}

// Copie un texte dans le presse-papiers, et dit si elle a pu le faire.
export function copyText(text) {
  const failed = () => toast(t('copyFailed'), true);
  // Hors d'une page sécurisée (l'accès par le réseau, en http), le presse-papiers du navigateur est hors de portée :
  // une zone de texte sélectionnée est copiée à la place.
  if (!navigator.clipboard) return copyBySelection(String(text || '')) ? toast(t('copied')) : failed();
  try {
    navigator.clipboard.writeText(String(text || '')).then(() => toast(t('copied')), failed);
  } catch (e) {
    failed();
  }
}

function copyBySelection(text) {
  const area = document.createElement('textarea');
  area.value = text;
  area.setAttribute('readonly', '');
  area.style.position = 'fixed';
  area.style.opacity = '0';
  document.body.append(area);
  area.select();
  let ok = false;
  try {
    ok = document.execCommand('copy');
  } catch (e) {
    ok = false;
  }
  area.remove();
  return ok;
}

/*
    Une modification que la base de données du navigateur a refusée, par exemple sur un disque
    plein.  L'utilisateur est prévenu au plus une fois toutes les quelques secondes, et le travail
    continue avec les copies en mémoire.
*/
let storageWarned = 0;

export function storageFailed(e) {
  console.warn('Albireo: the database of the browser refused a change', e);
  if (Date.now() - storageWarned < 15000) return;
  storageWarned = Date.now();
  toast(t(e && e.name === 'QuotaExceededError' ? 'errStorageFull' : 'errStorage'), true);
}

export function download(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type: type || 'text/plain;charset=utf-8' }));
  const a = h('a', { href: url, download: name });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

/*
    Le type d'un fichier que l'agent a créé.  Les pages, les images et les scripts qu'un navigateur
    exécuterait sont donnés comme de simples données, pour que le fichier soit enregistré et jamais
    ouvert dans l'origine de l'application.
*/
export function mimeOf(name) {
  const ext = name.toLowerCase().split('.').pop();
  return {
    md: 'text/markdown', csv: 'text/csv', json: 'application/json', py: 'text/x-python', txt: 'text/plain'
  }[ext] || 'application/octet-stream';
}
