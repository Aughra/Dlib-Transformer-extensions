// La liste des routes que la passerelle laisse passer vers le Mac (plan : docs/plan-discussion-en-ligne.md).
//
// Tout ce qui n'est pas ici est refusé : l'administration du service (modèles, réglages, redémarrage,
// journaux…) ne se fait que depuis le Mac ou le réseau de la maison. Une route que le service gagne
// un jour reste donc fermée tant qu'on ne l'ajoute pas ici.

// [méthodes, motif du chemin, raison]
const ALLOWED = [
  // Le calcul.
  [['POST'], /^\/v1\/(chat\/completions|completions|embeddings)$/, 'calcul'],
  [['GET'], /^\/v1\/models$/, 'lecture'],
  [['POST'], /^\/gulliver\/v1\/(count|extract|extract\/pages|highlight)$/, 'calcul'],
  [['POST'], /^\/gulliver\/v1\/web\/(fetch|search)$/, 'calcul'],

  // Les données de la discussion, rangées par le Mac.
  // La connexion (comptes, Gulliver 6.31.0) : la liste des profils, se connecter (adresse et mot de passe, puis le code
  // reçu par mail, Gulliver 7.0.0), le mot de passe oublié et son lien, se déconnecter, sa session et son mot de
  // passe.  La gestion des profils (créer, renommer, supprimer) et le premier compte restent au Mac et à la maison :
  // la passerelle les refuse, et le service aussi (en-tête X-Gulliver-Remote).
  [['GET'], /^\/gulliver\/v1\/profiles$/, 'connexion'],
  [['POST'], /^\/gulliver\/v1\/(login|login\/code|logout|session\/password|password\/forgot|password\/reset)$/, 'connexion'],
  [['GET'], /^\/gulliver\/v1\/session$/, 'connexion'],
  [['GET', 'POST', 'PUT', 'DELETE'], /^\/gulliver\/v1\/profile(\/[^?#]*)?$/, 'données'],
  [['GET', 'POST'], /^\/gulliver\/v1\/documents$/, 'données'],
  [['GET', 'DELETE'], /^\/gulliver\/v1\/documents\/[^/]+$/, 'données'],

  // Les lectures dont la page a besoin pour s'afficher (le secret du relais y est masqué par le service).
  [['GET'], /^\/gulliver\/v1\/(status|speed|usage|pseudalis|models|settings|relay|engines|licenses)$/, 'lecture'],
  [['GET'], /^\/gulliver\/v1\/settings\/profiles$/, 'lecture'],
  [['GET'], /^\/gulliver\/v1\/licenses\/[^/]+$/, 'lecture'],
  [['GET'], /^\/gulliver\/v1\/carnet(\/file)?$/, 'lecture'],
  [['GET'], /^\/gulliver\/v1\/thermal\/history$/, 'lecture'],
  [['GET'], /^\/gulliver\/v1\/models\/advice$/, 'lecture'],
];

// Une requête vers l'interface du service ? (les autres chemins sont les fichiers de la page)
export function isApiPath(path) {
  return path === '/v1' || path.startsWith('/v1/') || path === '/gulliver/v1' || path.startsWith('/gulliver/v1/');
}

// La raison de laisser passer, ou null pour refuser. HEAD suit GET. Un chemin à « .. », à « // », à barre
// inverse ou à barre encodée (%2F, %5C), que le service décoderait autrement que cette liste, est refusé.
export function allowed(method, path) {
  if (path.includes('..') || path.includes('//') || path.includes('\\') || /%2f|%5c|%2e/i.test(path)) return null;
  const m = method === 'HEAD' ? 'GET' : method;
  for (const [methods, pattern, why] of ALLOWED) {
    if (methods.includes(m) && pattern.test(path)) return why;
  }
  return null;
}
