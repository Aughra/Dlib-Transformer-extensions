// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Swagger UI en français (fork pour macOS, demande de l'humain du 2026-11-15 : tout en français) : ses libellés
    anglais, remplacés à mesure qu'il les affiche (un texte entier, ou un texte d'aide de champ), sans toucher aux
    descriptions de l'interface ni au code.  Pseudalis en a sa propre copie depuis le 2026-11-20.
*/
(function () {
  const words = {
    'Parameters': 'Paramètres', 'No parameters': 'Aucun paramètre', 'Try it out': 'Essayer', 'Cancel': 'Annuler',
    'Execute': 'Exécuter', 'Clear': 'Effacer', 'Responses': 'Réponses', 'Response body': 'Corps de la réponse',
    'Response headers': 'En-têtes de la réponse', 'Request body': 'Corps de la requête', 'Description': 'Description',
    'Code': 'Code', 'Links': 'Liens', 'No links': 'Aucun lien', 'Example Value': 'Exemple', 'Schema': 'Schéma',
    'Schemas': 'Schémas', 'Media type': 'Type de média', 'Servers': 'Serveurs', 'Request URL': 'Adresse de la requête',
    'Server response': 'Réponse du serveur', 'Duration': 'Durée', 'Details': 'Détails', 'required': 'obligatoire',
    'Download': 'Télécharger', 'Authorize': 'Autoriser', 'Available authorizations': 'Autorisations disponibles',
    'Close': 'Fermer', 'Logout': 'Se déconnecter', 'Loading...': 'Chargement…', 'Name': 'Nom', 'Value': 'Valeur',
    'Undocumented': 'Non documenté', 'Controls Accept header.': 'Fixe l’en-tête Accept.',
    'Send empty value': 'Envoyer une valeur vide', 'Add item': 'Ajouter', 'Example': 'Exemple', 'Examples': 'Exemples',
    'Default value': 'Valeur par défaut', 'Available values': 'Valeurs possibles', 'Responses ': 'Réponses ',
    'Copy to clipboard': 'Copier', 'Expand all': 'Tout déplier', 'Collapse all': 'Tout replier',
    'No operations defined in spec!': 'Aucune opération décrite.', 'Failed to load API definition.':
      'La description de l’interface ne se charge pas.', 'Fetch error': 'Erreur de lecture'
  };
  const placeholders = { 'Filter by tag': 'Filtrer les sections' };
  function translate(root) {
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    for (let n = walker.nextNode(); n; n = walker.nextNode()) {
      const text = n.nodeValue.trim();
      if (text && Object.prototype.hasOwnProperty.call(words, text) && !n.parentElement.closest('pre, code, .markdown, .renderedMarkdown')) {
        n.nodeValue = n.nodeValue.replace(text, words[text]);
      }
    }
    root.querySelectorAll && root.querySelectorAll('input[placeholder]').forEach(function (i) {
      if (placeholders[i.placeholder]) i.placeholder = placeholders[i.placeholder];
    });
  }
  window.addEventListener('load', function () {
    const target = document.getElementById('swagger-ui');
    if (!target) return;
    translate(target);
    new MutationObserver(function () { translate(target); }).observe(target, { childList: true, subtree: true });
  });
}());
