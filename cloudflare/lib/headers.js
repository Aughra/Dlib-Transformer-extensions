// Les en-têtes de sécurité des fichiers de la page, les mêmes que ceux que pose le service sur le Mac
// (host/server.cpp, gestionnaire d'après routage) : une page servie par Pages n'est pas moins protégée
// que celle du Mac. À garder en accord avec le service.

const MAIN_CSP = "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; style-src 'self' 'unsafe-inline'; "
  + "script-src 'self'; font-src 'self' data:; connect-src 'self'; worker-src 'self' blob:; frame-ancestors 'none'; "
  + "base-uri 'none'; form-action 'none'";
const DOCS_CSP = "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; "
  + "connect-src 'self'; frame-ancestors 'none'";
// La page où l'agent exécute son code : encadrée par l'application seule, dans une iframe à origine propre.
const SANDBOX_CSP = "default-src 'none'; script-src 'self' blob: 'unsafe-eval'; worker-src blob:; "
  + "style-src 'unsafe-inline'; frame-ancestors 'self'; base-uri 'none'; form-action 'none'";

export function securityHeaders(path) {
  const h = {
    'Referrer-Policy': 'no-referrer',
    'Cross-Origin-Opener-Policy': 'same-origin',
    'X-Content-Type-Options': 'nosniff',
    // Propre à la version en ligne : pas d'indexation par les moteurs de recherche.
    'X-Robots-Tag': 'noindex, nofollow',
  };
  // Pages sert aussi /sandbox.html sous /sandbox (il retire le .html des adresses et y redirige).
  if (path === '/sandbox.html' || path === '/sandbox') {
    h['X-Frame-Options'] = 'SAMEORIGIN';
    h['Cross-Origin-Resource-Policy'] = 'same-origin';
    h['Content-Security-Policy'] = SANDBOX_CSP;
    return h;
  }
  h['X-Frame-Options'] = 'DENY';
  // Le script du sandbox est chargé par une page d'une autre origine (opaque).
  h['Cross-Origin-Resource-Policy'] = path === '/js/sandbox.js' ? 'cross-origin' : 'same-origin';
  h['Content-Security-Policy'] = path.startsWith('/docs') ? DOCS_CSP : MAIN_CSP;
  return h;
}
