// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Internet » de l'onglet Service : le mode hors ligne, l'accès au Web, la recherche sur le Web et le proxy.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema } from './service_commun.js';

export const items = {
  offline: {
    title: 'Hors ligne',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Plus aucune connexion vers Internet : ni mise à jour du catalogue, ni téléchargement réseau, ni recherche web.',
    principle: 'Le service coupe ses connexions sortantes ; les modèles viennent de la bibliothèque du Mac, des imports ou de la réserve locale.',
    pros: ['Rien ne sort du Mac : confidentialité totale.', 'Aucun téléchargement surprise.'],
    cons: ['Pas de modèles qui ne sont pas déjà sur le Mac, pas de bouton Web.',
      'Un téléchargement en cours s’arrête (sa progression est gardée).'],
    normal: "Gulliver calcule déjà tout sur le Mac. Il ne va sur Internet que pour trois choses : le catalogue des " +
      "modèles, leur téléchargement, et la recherche du bouton Web. Hors ligne, il n’y va plus du tout. On peut " +
      "toujours charger un modèle de la bibliothèque du Mac (« Rafraîchir depuis le Mac », section « Sur ce Mac »), " +
      "importer un modèle comme fichier, ou restaurer celui dont tous les fichiers sont vérifiés dans la réserve.",
    owl: "offline = true annule à l’instant les transferts en cours (http_cancel_all, raison « offline mode »), refuse " +
      "les routes qui ont besoin du réseau (refuse_offline : 409, code offline) et ferme " +
      "l’accès web (403, code offline). Les restaurations de la réserve vérifient taille et SHA-256, sans aucun repli " +
      "réseau, même si le réglage change pendant le travail. Le dossier d’import et la bibliothèque du Mac " +
      "(GET /gulliver/v1/library) restent lus. Remis à faux, http_restore() " +
      "rouvre les transferts.",
    marmot: "C’est le mode avion de ton téléphone, mais pour Gulliver. Tout ce qui est déjà sur le Mac marche toujours ; " +
      "seulement, plus rien ne part vers Internet, et rien n’en vient.",
    bee: "Oublié allumé, le catalogue ne se met plus à jour et le bouton Web répond « hors ligne ». Si la réserve " +
      "ne contient pas tous les fichiers vérifiés, il faut les importer ou revenir en ligne."
  },
  web_access: {
    title: 'Bouton Web',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Permet au modèle de chercher sur le web et de lire des pages pour répondre.',
    principle: 'Le service fait les recherches et lit les pages pour la page, qui les donne ensuite au modèle.',
    pros: ['Des réponses à jour, avec des sources.', 'Le modèle peut lire une adresse que tu lui donnes.'],
    cons: ['Les mots de ta recherche partent vers le site cherché.', 'Plus lent : il faut télécharger puis lire les pages.'],
    normal: "Un modèle ne connaît que ce qu’il a appris jusqu’à une certaine date. Avec le bouton Web, il peut chercher " +
      "et lire des pages, puis répondre en citant ce qu’il a lu. Coupé, il répond seulement avec ce qu’il sait déjà.",
    owl: "Routes POST /gulliver/v1/web/search (1 à 10 résultats, http et https seulement) et /gulliver/v1/web/fetch " +
      "(octets de la page en base64, adresses publiques seulement : les adresses locales et privées sont refusées pour " +
      "éviter de servir de relais vers le réseau de la maison). web_access = false les ferme (403).",
    marmot: "C’est comme autoriser ton assistant à aller à la bibliothèque. Il revient avec des livres récents et te dit " +
      "où il a trouvé l’info. Sans ça, il te répond de mémoire.",
    bee: "Coupé, les questions sur l’actualité reçoivent des réponses datées. Allumé, ce que tu cherches sort du Mac " +
      "vers Wikipédia et les sites lus."
  },
  web_search: {
    title: 'Recherche Wikipédia',
    control: { kind: 'switch', on: 'wikipedia', off: 'none' },
    restart: false,
    effect: 'Le moteur de recherche du bouton Web : Wikipédia, ou aucun.',
    principle: 'Le service interroge Wikipédia dans la langue de la question et rend les articles trouvés.',
    pros: ['Sans clé, sans compte, et des sources de bonne qualité.'],
    cons: ['Seulement ce que Wikipédia couvre : ni actualité fine, ni sites commerciaux.'],
    normal: "Quand le modèle a besoin de chercher, le service pose la question à Wikipédia et rapporte les articles. " +
      "Coupée, la recherche ne marche plus, mais le bouton Web sait toujours lire une adresse précise que tu donnes.",
    owl: "web_search = « wikipedia » ou « none ». Les anciennes valeurs (« auto », « brave », « searxng ») sont ramenées " +
      "à Wikipédia à la lecture des réglages. La recherche passe par l’API publique de Wikipédia, dans la langue de la " +
      "requête (paramètre language), via le proxy éventuel.",
    marmot: "C’est le moteur de recherche que ton assistant utilise : une grande encyclopédie. Pratique et fiable, mais " +
      "il n’y trouvera pas le prix du dernier jeu vidéo.",
    bee: "Coupée, le modèle ne trouve plus rien seul : il faut lui donner les adresses des pages à lire."
  },
  proxy: {
    title: 'Proxy',
    control: { kind: 'text', placeholder: 'vide, none, ou http://hôte:port' },
    restart: false,
    effect: 'Un relais pour sortir sur Internet, si ton réseau l’exige. Vide le plus souvent.',
    principle: 'Toutes les connexions du service vers Internet passent par ce relais.',
    pros: ['Indispensable sur certains réseaux d’entreprise ou d’école.'],
    cons: ['Mal réglé, plus rien ne passe : catalogue, téléchargements et recherche échouent.'],
    normal: "Sur certains réseaux, on ne sort pas directement sur Internet : on passe par un relais, le proxy. Vide, " +
      "le service suit les variables du système (HTTPS_PROXY), et sinon sort en direct. « none » force le direct.",
    owl: "Vide : HTTPS_PROXY / HTTP_PROXY / ALL_PROXY de l’environnement du service (pas les réglages de proxy des " +
      "Réglages Système de macOS), NO_PROXY respecté, localhost toujours en direct. « none » : jamais de proxy. " +
      "Sinon http://[utilisateur:mot-de-passe@]hôte:port ; le mot de passe est rendu masqué (********) et gardé à part " +
      "dans proxy.secret. Appliqué à chaud (set_http_proxy).",
    marmot: "C’est comme passer par l’accueil d’un bâtiment pour sortir : certains endroits l’exigent. Chez toi, la " +
      "porte donne directement sur la rue, et tu n’en as pas besoin.",
    bee: "Une adresse fausse coupe Gulliver d’Internet sans prévenir : téléchargements et recherches échouent avec " +
      "« the proxy refuses the connection ». Vide-le pour revenir au réglage normal."
  }
};

export const schemas = {
  offline: schema('Hors ligne : rien ne sort du Mac',
    box(40, 80, 150, 60, 'Mac', 's-blue', 'service') +
    `<ellipse cx="460" cy="108" rx="100" ry="44" class="s-box s-soft"/>` + text(460, 113, 'Internet') +
    arrow(190, 108, 355, 108, '', 's-cut') + `<path d="M262 92 l16 32 M278 92 l-16 32" class="s-cross"/>` +
    box(40, 160, 150, 40, 'imports + réserve', 's-gold') + arrow(115, 160, 115, 140), 215),
  web_access: schema('Le bouton Web de la page',
    box(20, 80, 120, 56, 'Question', 's-soft') + arrow(140, 108, 230, 108) +
    box(230, 80, 130, 56, 'Service', 's-blue', 'recherche, lecture') + arrow(360, 108, 450, 108) +
    box(450, 80, 130, 56, 'Pages web', 's-gold') +
    text(300, 170, 'coupé : le modèle répond avec ce qu’il sait déjà', 's-f'), 190),
  web_search: schema('Où le service cherche',
    box(30, 80, 140, 56, 'Recherche', 's-blue') + arrow(170, 108, 300, 108) +
    box(300, 80, 170, 56, 'Wikipédia', 's-gold', 'articles, liens') +
    text(300, 170, 'aucun : le bouton Web lit seulement les adresses données', 's-f'), 190),
  proxy: schema('Le relais vers Internet',
    box(20, 80, 120, 56, 'Mac', 's-blue') + arrow(140, 108, 240, 108) +
    box(240, 80, 120, 56, 'Proxy', 's-gold', 'relais') + arrow(360, 108, 460, 108) +
    box(460, 80, 120, 56, 'Internet', 's-soft') +
    text(300, 170, 'vide : les variables HTTPS_PROXY ; « none » : en direct', 's-f'), 190)
};
