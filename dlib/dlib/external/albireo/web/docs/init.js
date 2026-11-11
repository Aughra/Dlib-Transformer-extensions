// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// La description de l'interface du service local, affichée par Swagger UI sans réseau.
window.addEventListener('load', function () {
  window.ui = SwaggerUIBundle({
    url: '../openapi.json',
    dom_id: '#swagger-ui',
    deepLinking: true,
    presets: [SwaggerUIBundle.presets.apis],
    layout: 'BaseLayout',
    validatorUrl: null,
    displayRequestDuration: true,
    defaultModelsExpandDepth: 0,
    persistAuthorization: false
  });
});
