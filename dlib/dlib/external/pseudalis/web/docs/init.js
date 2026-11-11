// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La description de l'interface du service local, affichée par Swagger UI sans réseau.  L'icône
// ouvre cette page avec le jeton d'administration dans le fragment de son adresse, que le
// navigateur n'envoie jamais : le jeton autorise les routes d'administration, puis quitte l'adresse.
window.addEventListener('load', function () {
  // Le lien de retour, dans la langue de l'application : celle qui y a été choisie, sinon celle du
  // navigateur.
  const back = {
    en: 'Back to the application', fr: "Retour à l'application", de: 'Zurück zur Anwendung',
    it: "Torna all'applicazione", es: 'Volver a la aplicación'
  };
  let language = null;
  try { language = localStorage.getItem('pseudalis.language'); } catch (e) { /* stockage indisponible */ }
  if (!back[language]) {
    language = 'en';
    const wanted = navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language];
    for (let i = 0; i < wanted.length; ++i) {
      const code = String(wanted[i] || '').slice(0, 2).toLowerCase();
      if (back[code]) { language = code; break; }
    }
  }
  document.documentElement.lang = language;
  const link = document.getElementById('back');
  if (link) link.textContent = back[language];

  let admin = '';
  const m = /(?:^#|&)admin=([0-9A-Za-z_-]+)/.exec(location.hash || '');
  try {
    if (m) sessionStorage.setItem('pseudalis.admin', m[1]);
    admin = sessionStorage.getItem('pseudalis.admin') || '';
  } catch (e) {
    admin = m ? m[1] : '';
  }
  if (m) history.replaceState(null, '', location.pathname + location.search);
  window.ui = SwaggerUIBundle({
    url: '../openapi.json',
    dom_id: '#swagger-ui',
    deepLinking: true,
    presets: [SwaggerUIBundle.presets.apis],
    layout: 'BaseLayout',
    validatorUrl: null,
    displayRequestDuration: true,
    defaultModelsExpandDepth: 0,
    persistAuthorization: false,
    onComplete: function () {
      if (admin) window.ui.preauthorizeApiKey('admin', admin);
    }
  });
});
