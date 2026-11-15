// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    La description de l'interface du service local, affichée par Swagger UI sans réseau.  L'icône ouvre cette page
    avec le jeton d'administration dans le fragment de son adresse, que le navigateur n'envoie jamais : le jeton
    autorise les routes d'administration, puis quitte l'adresse.  Le fork pour macOS y ajoute le thème, un
    démarrage rapide rempli avec l'adresse de ce service, et le passage à l'interface d'Albireo quand il tourne.
*/
window.addEventListener('load', function () {
  const words = {
    en: { what: 'Programming interface', back: '← Back to the application', switch: 'Albireo API',
      quick: 'Quick start', copy: 'Copy', copied: 'Copied', refused: 'Copy refused',
      quick_hint: 'Every route answers on this Mac only. The files travel as they are, in the body of the requests; ' +
        'no account is needed. The administration routes take the token of the data folder (Authorize).',
      address: 'Address', admin: 'Administration', admin_on: 'token given', admin_off: 'token needed',
      theme: { auto: 'Theme: system', light: 'Theme: light', dark: 'Theme: dark' } },
    fr: { what: 'Interface de programmation', back: '← Retour à l’application', switch: 'API d’Albireo',
      quick: 'Démarrage rapide', copy: 'Copier', copied: 'Copié', refused: 'Copie refusée',
      quick_hint: 'Chaque route ne répond que sur ce Mac. Les fichiers voyagent tels quels, dans le corps des ' +
        'requêtes ; aucun compte n’est nécessaire. Les routes d’administration demandent le jeton du dossier de ' +
        'données (Authorize).',
      address: 'Adresse', admin: 'Administration', admin_on: 'jeton donné', admin_off: 'jeton nécessaire',
      theme: { auto: 'Thème : système', light: 'Thème : clair', dark: 'Thème : sombre' } }
  };
  let language = null;
  try { language = localStorage.getItem('pseudalis.language'); } catch (e) { /* stockage indisponible */ }
  if (!words[language]) {
    language = 'en';
    const wanted = navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language];
    for (let i = 0; i < wanted.length; ++i) {
      const code = String(wanted[i] || '').slice(0, 2).toLowerCase();
      if (words[code]) { language = code; break; }
    }
  }
  const w = words[language];
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

  let admin = '';
  const m = /(?:^#|&)admin=([0-9A-Za-z_-]+)/.exec(location.hash || '');
  try {
    if (m) sessionStorage.setItem('pseudalis.admin', m[1]);
    admin = sessionStorage.getItem('pseudalis.admin') || '';
  } catch (e) {
    admin = m ? m[1] : '';
  }
  if (m) history.replaceState(null, '', location.pathname + location.search);

  // Le démarrage rapide : un texte et un fichier protégés, avec l'adresse de ce service.
  const base = location.origin;
  let authority = '<data folder>/certificates/pseudalis-authority.crt';
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
  fact(w.admin, admin ? w.admin_on : w.admin_off);
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
    persistAuthorization: false,
    onComplete: function () {
      if (admin) window.ui.preauthorizeApiKey('admin', admin);
    }
  });
});
