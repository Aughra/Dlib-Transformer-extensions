// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les boîtes de dialogue (une boîte, une confirmation, une saisie, un champ avec son aide), les menus et les panneaux
// d'explications qui s'ouvrent sous un bouton (fork pour macOS : sortis d'app.js).

import { h } from './dom.js';
import { icon } from './icons.js';
import { t } from './i18n.js';
import { ui } from './state.js';

export function showMenu(anchor, entries) {
  closeMenu();
  const menu = h('div', { class: 'menu', role: 'menu' });
  for (const e of entries) {
    if (e === '-') { menu.append(h('div', { class: 'sep' })); continue; }
    if (e.head) { menu.append(h('div', { class: 'head' }, e.head)); continue; }
    // Une entrée qui ne s'applique pas dans l'état actuel montre pourquoi, et ne fait rien.
    menu.append(h('div', {
      class: 'entry' + (e.danger ? ' danger' : '') + (e.checked ? ' checked' : '') + (e.disabled ? ' disabled' : ''),
      role: 'menuitem', 'aria-disabled': e.disabled ? 'true' : null, title: e.disabled ? e.hint : null,
      onclick: () => {
        if (e.disabled) return;
        closeMenu();
        e.action();
      }
    }, e.icon ? h('span', { html: icon(e.icon) }) : null,
    h('span', null, e.label, e.desc ? h('span', { class: 'desc' }, e.desc) : null)));
  }
  placeMenu(menu, anchor);
}

// Un panneau d'explications sous un bouton, qui se ferme comme un menu.
export function showPopover(anchor, children) {
  closeMenu();
  placeMenu(h('div', { class: 'menu popover', role: 'dialog' }, children), anchor);
}

// Affiche un menu ou un panneau sous son point d'ancrage, au-dessus quand la fenêtre manque de place en dessous.
function placeMenu(menu, anchor) {
  document.body.append(menu);
  const r = anchor.getBoundingClientRect();
  const mw = menu.offsetWidth, mh = menu.offsetHeight;
  let x = Math.min(r.left, window.innerWidth - mw - 8);
  let y = r.bottom + 6;
  if (y + mh > window.innerHeight - 8) y = Math.max(8, r.top - mh - 6);
  menu.style.left = Math.max(8, x) + 'px';
  menu.style.top = y + 'px';
  ui.menu = menu;
  // Les écouteurs suivent le clic qui a ouvert le menu, sauf si le menu s'est fermé entre-temps.
  setTimeout(() => {
    if (ui.menu !== menu) return;
    document.addEventListener('mousedown', outsideMenu);
    document.addEventListener('keydown', escapeMenu);
  }, 0);
}

function outsideMenu(e) {
  if (ui.menu && !ui.menu.contains(e.target)) closeMenu();
}

function escapeMenu(e) {
  if (e.key === 'Escape') closeMenu();
}

export function closeMenu() {
  if (ui.menu) ui.menu.remove();
  ui.menu = null;
  document.removeEventListener('mousedown', outsideMenu);
  document.removeEventListener('keydown', escapeMenu);
}

// Boîtes de dialogue.
export function dialog(content, small) {
  return new Promise((resolve) => {
    const box = h('div', { class: 'dialog' + (small ? ' small' : ''), role: 'dialog', 'aria-modal': 'true' });
    const overlay = h('div', { class: 'overlay' }, box);
    const close = (v) => { overlay.remove(); document.removeEventListener('keydown', onKey); resolve(v); };
    const onKey = (e) => { if (e.key === 'Escape') close(null); };
    overlay.addEventListener('mousedown', (e) => { if (e.target === overlay) close(null); });
    document.addEventListener('keydown', onKey);
    content(box, close);
    document.body.append(overlay);
    const f = box.querySelector('input, textarea, button.primary');
    if (f) f.focus();
  });
}

export function confirmDialog(title, text) {
  return dialog((box, close) => {
    box.append(h('h2', null, title), h('p', null, text), h('div', { class: 'actions' },
      h('button', { class: 'button', type: 'button', onclick: () => close(false) }, t('cancel')),
      h('button', { class: 'button primary', type: 'button', onclick: () => close(true) }, t('confirm'))));
  }, true);
}

export function promptDialog(title, value) {
  return dialog((box, close) => {
    const input = h('input', { value: value || '', style: { width: '100%' } });
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') close(input.value); });
    box.append(h('h2', null, title), h('div', { class: 'field' }, input), h('div', { class: 'actions' },
      h('button', { class: 'button', type: 'button', onclick: () => close(null) }, t('cancel')),
      h('button', { class: 'button primary', type: 'button', onclick: () => close(input.value) }, t('save'))));
    setTimeout(() => input.select(), 0);
  }, true);
}

export function field(label, control, help) {
  return h('div', { class: 'field' }, h('label', null, label), control, help ? h('p', { class: 'help', style: { margin: 0, color: 'var(--text-faint)', fontSize: '12.5px' } }, help) : null);
}
