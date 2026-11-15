// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le menu de la page (fork pour macOS) : une section à la fois, choisie par l'adresse (#protect, #restore…) ; les
    entrées de l'administration suivent les sections qu'app.js montre quand l'icône donne le jeton ; le bouton du
    thème ; le passage à Albireo, quand Pseudalis le trouve sur ce Mac.
*/
(function () {
  const texts = window.PSEUDALIS_TEXTS || {};
  const language = function () { const s = document.getElementById('language'); return (s && s.value) || 'en'; };
  const word = function (key) { return (texts[language()] || texts.en || {})[key] || (texts.en || {})[key] || key; };
  const links = Array.prototype.slice.call(document.querySelectorAll('#side-nav a[data-card]'));
  const cards = links.map(function (a) { return document.getElementById(a.dataset.card); });

  function show() {
    const wanted = (location.hash || '#protect').slice(1);
    let index = links.findIndex(function (a, i) { return a.hash === '#' + wanted && !cards[i].hidden; });
    if (index < 0) index = 0;
    links.forEach(function (a, i) {
      a.classList.toggle('on', i === index);
      cards[i].classList.toggle('away', i !== index);
    });
  }
  function sync() {
    let admin = false;
    links.forEach(function (a, i) {
      const isAdmin = ['records-card', 'key-card', 'settings-card', 'terms-card'].indexOf(a.dataset.card) >= 0;
      a.hidden = cards[i].hidden;
      if (isAdmin && !cards[i].hidden) admin = true;
    });
    document.getElementById('admin-hint').hidden = admin;
    show();
  }
  // app.js montre ou cache les sections de l'administration par leur attribut hidden.
  const watch = new MutationObserver(sync);
  cards.forEach(function (c) { watch.observe(c, { attributes: true, attributeFilter: ['hidden'] }); });
  window.addEventListener('hashchange', function () { show(); window.scrollTo(0, 0); });
  sync();

  // Le thème : système, clair, sombre.
  const themes = ['auto', 'light', 'dark'], marks = { auto: '◐', light: '☀', dark: '☾' };
  let choice = 'auto';
  try { choice = localStorage.getItem('pseudalis.theme') || 'auto'; } catch (e) { /* stockage indisponible */ }
  const button = document.getElementById('theme');
  function apply() {
    document.documentElement.classList.toggle('dark', choice === 'dark' ||
      (choice === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches));
    button.textContent = marks[choice];
    button.title = word('theme_' + choice);
  }
  button.addEventListener('click', function () {
    choice = themes[(themes.indexOf(choice) + 1) % themes.length];
    try { localStorage.setItem('pseudalis.theme', choice); } catch (e) { /* stockage indisponible */ }
    apply();
  });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', apply);
  document.getElementById('language').addEventListener('change', apply);
  apply();

  // Albireo sur ce Mac : seulement depuis le Mac lui-même.
  if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
    fetch('v1/info', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (info) {
      const a = (info && info.albireo) || {};
      if (!a.found || !a.port) return;
      document.getElementById('to-albireo').href = 'https://localhost:' + a.port + '/';
      document.getElementById('app-switch').hidden = false;
    }).catch(function () { /* service muet */ });
  }

  // Le débit du moteur en direct : les octets lus et écrits des cinq dernières secondes, relus chaque seconde ; au
  // repos, les dernières valeurs mesurées restent, en plus pâle.
  const speed = document.getElementById('speed');
  const last = { read: 0, write: 0 };
  function rate(bytes) {
    const fr = language() === 'fr';
    if (bytes >= 1048576) return (bytes / 1048576).toLocaleString(language(), { maximumFractionDigits: 1 }) + (fr ? ' Mo/s' : ' MB/s');
    return Math.round(bytes / 1024).toLocaleString(language()) + (fr ? ' Ko/s' : ' KB/s');
  }
  function gauge(label, now, before) {
    const g = document.createElement('span');
    g.className = 'gauge' + (now > 0 ? ' on' : '');
    const l = document.createElement('span');
    l.textContent = word(label);
    const b = document.createElement('b');
    b.textContent = now > 0 ? rate(now) : before > 0 ? rate(before) : '–';
    g.append(l, b);
    return g;
  }
  async function pollSpeed() {
    if (document.hidden) return;
    try {
      const s = await fetch('v1/speed', { cache: 'no-store' }).then(function (r) { return r.json(); });
      const read = s.read_bytes_per_second || 0, write = s.written_bytes_per_second || 0;
      if (read > 0) last.read = read;
      if (write > 0) last.write = write;
      speed.title = word('speed_title');
      speed.replaceChildren(gauge('speed_read', read, last.read), gauge('speed_write', write, last.write));
    } catch (e) {
      speed.replaceChildren();
    }
  }
  pollSpeed();
  setInterval(pollSpeed, 1000);
  document.addEventListener('visibilitychange', pollSpeed);
}());
