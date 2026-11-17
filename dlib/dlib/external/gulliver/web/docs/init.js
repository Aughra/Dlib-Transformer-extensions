// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    La page de l'interface du service local (fork pour macOS) : le bouton du thème, le démarrage rapide rempli avec
    l'adresse et le dossier de données de ce service, le passage à l'interface de Pseudalis quand il tourne sur ce
    Mac, puis la description par Swagger UI, sans réseau.
*/
window.addEventListener('load', function () {
  const root = document.documentElement;
  const themes = ['auto', 'light', 'dark'], names = { auto: 'Thème : système', light: 'Thème : clair', dark: 'Thème : sombre' };
  let choice = 'auto';
  try { choice = localStorage.getItem('docs.theme') || 'auto'; } catch (e) { /* stockage indisponible */ }
  const button = document.getElementById('theme');
  function apply() {
    root.classList.toggle('dark', choice === 'dark' ||
      (choice === 'auto' && window.matchMedia('(prefers-color-scheme: dark)').matches));
    button.textContent = names[choice] || names.auto;
  }
  button.addEventListener('click', function () {
    choice = themes[(themes.indexOf(choice) + 1) % themes.length];
    try { localStorage.setItem('docs.theme', choice); } catch (e) { /* stockage indisponible */ }
    apply();
  });
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', apply);
  apply();

  const base = location.origin + '/v1';
  const local = location.hostname === 'localhost' || location.hostname === '127.0.0.1';
  let authority = '<dossier des données>/certificates/gulliver-authority.crt';
  const samples = {
    curl: function () {
      return 'curl ' + (location.protocol === 'https:' ? '--cacert "' + authority + '" \\\n  ' : '') + base +
        '/chat/completions \\\n  -H "Content-Type: application/json" \\\n' +
        '  -d \'{"model": "gulliver-general", "messages": [{"role": "user", "content": "Bonjour !"}]}\'';
    },
    Python: function () {
      return '# pip install openai\nimport httpx\nfrom openai import OpenAI\n\nclient = OpenAI(base_url="' + base +
        '", api_key="gulliver"' + (location.protocol === 'https:' ? ',\n                http_client=httpx.Client(verify="' +
        authority + '")' : '') + ')\nanswer = client.chat.completions.create(\n    model="gulliver-general",\n' +
        '    messages=[{"role": "user", "content": "Bonjour !"}])\nprint(answer.choices[0].message.content)';
    },
    JavaScript: function () {
      return 'const r = await fetch("' + base + '/chat/completions", {\n  method: "POST",\n' +
        '  headers: { "Content-Type": "application/json" },\n  body: JSON.stringify({ model: "gulliver-general",\n' +
        '    messages: [{ role: "user", content: "Bonjour !" }] })\n});\nconsole.log((await r.json()).choices[0].message.content);';
    }
  };
  let current = 'curl';
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
  copy.addEventListener('click', function () {
    navigator.clipboard.writeText(sample.textContent).then(function () {
      copy.textContent = 'Copié';
      setTimeout(function () { copy.textContent = 'Copier'; }, 1500);
    }, function () { copy.textContent = 'Copie refusée'; });
  });

  function fact(label, value) {
    const s = document.createElement('span');
    s.append(label + ' ');
    const b = document.createElement('b');
    b.textContent = value;
    s.appendChild(b);
    document.getElementById('facts').appendChild(s);
  }
  fetch('../gulliver/v1/status', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (s) {
    document.getElementById('version').textContent = s.version ? 'version ' + s.version : '';
    if (s.data_dir) authority = s.data_dir + '/certificates/gulliver-authority.crt';
    show();
    fact('Adresse', base);
    if (s.loaded_models && s.loaded_models.length) fact('Modèles chargés', s.loaded_models.length);
    fact('Clé', 'aucune sur le Mac');
  }).catch(function () { fact('Adresse', base); });

  // Pseudalis sur ce Mac : sa propre interface, quand il tourne.
  if (local) {
    fetch('../gulliver/v1/pseudalis', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (p) {
      if (!p.running || !p.port) return;
      const link = document.getElementById('switch');
      link.href = 'https://localhost:' + p.port + '/docs/';
      link.hidden = false;
    }).catch(function () { /* Pseudalis absent */ });
  }

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
    tryItOutEnabled: false,
    persistAuthorization: false
  });
});
