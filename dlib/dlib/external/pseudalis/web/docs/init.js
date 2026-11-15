// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    La description de l'interface du service local, affichée par Swagger UI sans réseau, avec (fork pour macOS) le
    thème, un démarrage rapide rempli avec l'adresse de ce service, et le passage à l'interface d'Albireo quand il
    tourne.  Sur macOS, l'administration est ouverte sur le Mac, sans jeton.
*/
window.addEventListener('load', function () {
  // Le français seulement (fork pour macOS, demande de l'humain du 2026-11-15).
  const w = { what: 'Interface de programmation', back: '← Retour à l’application', switch: 'API d’Albireo',
      quick: 'Démarrage rapide', copy: 'Copier', copied: 'Copié', refused: 'Copie refusée',
      quick_hint: 'Chaque route ne répond que sur ce Mac. Les fichiers voyagent tels quels, dans le corps des ' +
        'requêtes ; ni compte ni jeton ne sont nécessaires.',
      address: 'Adresse',
      theme: { auto: 'Thème : système', light: 'Thème : clair', dark: 'Thème : sombre' }
  };
  const language = 'fr';
  document.documentElement.lang = language;
  document.querySelectorAll('[data-t]').forEach(function (e) { e.textContent = w[e.dataset.t]; });

  const root = document.documentElement, themes = ['auto', 'light', 'dark'];
  let choice = 'auto';
  try { choice = localStorage.getItem('docs.theme') || 'auto'; } catch (e) { /* stockage indisponible */ }
  const button = document.getElementById('theme');
  function apply() {
    root.classList.toggle('dark', choice === 'dark' ||
      (choice === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches));
    button.textContent = w.theme[choice] || w.theme.auto;
  }
  button.addEventListener('click', function () {
    choice = themes[(themes.indexOf(choice) + 1) % themes.length];
    try { localStorage.setItem('docs.theme', choice); } catch (e) { /* stockage indisponible */ }
    apply();
  });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', apply);
  apply();


  // Le démarrage rapide : un texte et un fichier protégés, avec l'adresse de ce service.
  const base = location.origin;
  let authority = '<dossier des données>/certificates/pseudalis-authority.crt';
  const samples = {
    'curl (texte)': function () {
      return 'curl --cacert "' + authority + '" \\\n  ' + base + '/v1/text/anonymize \\\n' +
        '  -H "Content-Type: application/json" \\\n  -d \'{"text": "Jean Dupont habite 12 rue des Lilas à Rennes."}\'';
    },
    'curl (fichier)': function () {
      return 'curl --cacert "' + authority + '" \\\n  "' + base + '/v1/anonymize?name=contrat.docx" \\\n' +
        '  --data-binary @contrat.docx -o contrat-protege.docx';
    },
    Python: function () {
      return '# pip install requests\nimport requests\n\nr = requests.post("' + base + '/v1/text/anonymize",\n' +
        '    json={"text": "Jean Dupont habite 12 rue des Lilas à Rennes."},\n    verify="' + authority + '")\n' +
        'print(r.json())';
    }
  };
  let current = 'curl (texte)';
  const tabs = document.getElementById('tabs'), sample = document.getElementById('sample');
  function show() {
    sample.textContent = samples[current]();
    Array.prototype.forEach.call(tabs.children, function (b) { b.classList.toggle('on', b.textContent === current); });
  }
  Object.keys(samples).forEach(function (name) {
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = name;
    b.addEventListener('click', function () { current = name; show(); });
    tabs.appendChild(b);
  });
  show();
  const copy = document.getElementById('copy');
  copy.textContent = w.copy;
  copy.addEventListener('click', function () {
    navigator.clipboard.writeText(sample.textContent).then(function () {
      copy.textContent = w.copied;
      setTimeout(function () { copy.textContent = w.copy; }, 1500);
    }, function () { copy.textContent = w.refused; });
  });
  function fact(label, value) {
    const s = document.createElement('span');
    s.append(label + ' ');
    const b = document.createElement('b');
    b.textContent = value;
    s.appendChild(b);
    document.getElementById('facts').appendChild(s);
  }
  fact(w.address, base);
  fetch('../v1/info', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (info) {
    document.getElementById('version').textContent = info.version || '';
    if (info.drive && info.drive.folder) {
      authority = info.drive.folder.replace(/\/drive$/, '') + '/certificates/pseudalis-authority.crt';
      show();
    }
    // Albireo sur ce Mac : sa propre interface, quand Pseudalis le trouve.
    const a = info.albireo || {};
    if (a.found && a.port && (location.hostname === 'localhost' || location.hostname === '127.0.0.1')) {
      const link = document.getElementById('switch');
      link.href = 'https://localhost:' + a.port + '/docs/';
      link.hidden = false;
    }
  }).catch(function () { /* service muet */ });

  window.ui = SwaggerUIBundle({
    url: '../openapi.json',
    dom_id: '#swagger-ui',
    deepLinking: true,
    presets: [SwaggerUIBundle.presets.apis],
    layout: 'BaseLayout',
    validatorUrl: null,
    displayRequestDuration: true,
    defaultModelsExpandDepth: 0,
    docExpansion: 'list',
    filter: true,
    persistAuthorization: false
  });
});
