// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les réglages d'affichage de la page (fork pour macOS : sortis d'app.js) : le thème clair ou sombre, la langue
// (le français seul), et l'enregistrement d'un réglage, que les autres onglets reçoivent.

import * as store from './store.js';
import { storageFailed } from './dom.js';
import { setLanguage } from './i18n.js';
import { state, ui } from './state.js';
import { broadcast } from './tabs.js';

// Thème et langue.
export function applyTheme() {
  const pref = state.settings.theme;
  const dark = pref === 'dark' || (pref === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  document.getElementById('hljs-light').disabled = dark;
  document.getElementById('hljs-dark').disabled = !dark;
}

// La langue de l'interface : le français, la seule de ce fork.
export function applyLanguage() {
  setLanguage('fr');
}

// Enregistre un réglage et prévient les autres onglets ; un réglage que la base refuse reste pour cette visite.
export async function saveSetting(key, value) {
  state.settings[key] = value;
  await store.setSetting(key, value).catch(storageFailed);
  broadcast({ type: 'setting', key, value });
  if (/^memory/.test(key) && ui.memoryView) ui.memoryView();
}
