// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Le thème de la page, posé avant le premier affichage : celui que le bouton a choisi, sinon celui du système
// (fork pour macOS).
(function () {
  let choice = 'auto';
  try { choice = localStorage.getItem('pseudalis.theme') || 'auto'; } catch (e) { /* stockage indisponible */ }
  const dark = choice === 'dark' || (choice === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.classList.toggle('dark', dark);
}());
