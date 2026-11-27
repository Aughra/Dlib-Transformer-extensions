// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Les moteurs extérieurs » de l'onglet Service : le relais vers un modèle extérieur.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema } from './service_commun.js';

export const items = {
  relay: {
    title: 'Relais vers un moteur extérieur',
    control: { kind: 'relay' },
    restart: false,
    effect: 'Envoie les discussions qui le demandent à Claude, à Ollama ou à un autre moteur, et rend leur réponse ici.',
    principle: 'Le service sert d’intermédiaire : il envoie la discussion au moteur extérieur avec ta clé, et rend sa ' +
      'réponse comme celle d’un modèle du Mac.',
    pros: ['Des modèles bien plus grands que ceux qui tiennent dans le Mac (Claude : un million de tokens de contexte).',
      'Avec Pseudalis, les noms, adresses et numéros partent remplacés par des pseudonymes, et la réponse revient ' +
        'avec les vrais.',
      'La clé reste dans le service : la page et les autres programmes ne la voient jamais.',
      'Hors ligne, un moteur du réseau de la maison (un autre ordinateur) reste joignable.'],
    cons: ['La discussion quitte le Mac : seule la pseudonymisation en retire les données personnelles, et elle ne ' +
        'reconnaît pas tout (un détail qui identifie quelqu’un sans être un nom, par exemple).',
      'Payant chez Claude et Ollama : chaque question consomme le crédit du compte.',
      'Ni outils, ni sons ; la recherche sur le web n’existe qu’avec Claude.',
      'Avec l’accès depuis le réseau ouvert, tout appareil du Wi-Fi peut y envoyer des discussions, donc utiliser ' +
        'ton crédit ; ses réglages, eux, ne se changent que sur le Mac.'],
    normal: "Un modèle du Mac répond sans que rien ne sorte. Pour une question difficile, on peut préférer un modèle " +
      "beaucoup plus grand, qui tourne chez Anthropic (Claude), chez Ollama, ou sur un autre ordinateur. Le relais " +
      "envoie alors la discussion à ce moteur, avec ta clé, puis rend sa réponse dans Gulliver. Avec la " +
      "pseudonymisation, Pseudalis remplace d’abord chaque donnée personnelle par un pseudonyme ; le moteur ne voit " +
      "que les pseudonymes, et Gulliver remet les vrais noms dans la réponse pendant qu’elle s’écrit. Les images ne " +
      "partent pas : un modèle du Mac qui lit les images les décrit, et la description part protégée elle aussi.",
    owl: "Le modèle albireo-relay de l’interface (POST /v1/chat/completions) passe par relay_service (host/relay.cpp, " +
      "repris de l’original 1.0.23) : huit réponses à la fois au plus, chacune sur son thread. OpenAI : " +
      "/chat/completions à l’adresse donnée (/v1 ajouté si seuls ses modèles y répondent) ; Ollama : " +
      "https://ollama.com/v1 ; Claude : /v1/messages, système à part, raisonnement adaptatif, effort, 64 000 tokens " +
      "de réponse sans borne de la requête, web_search à cinq recherches au plus. La clé ne va que dans les en-têtes " +
      "(x-api-key ou Authorization), et vit dans relay.secret (0600) ; settings.json n’a que ********, valable pour le " +
      "même fournisseur, la même adresse et le même utilisateur. Réglages et essai depuis n’importe quel appareil du " +
      "Wi-Fi, le téléphone compris (6.0.0 ; Wi-Fi personnel maîtrisé). Pseudonymisation : POST /v1/text/anonymize de " +
      "Pseudalis avec session: true (sa clé de séance, 3.1.0), réponse values ; une mémoire de 2 h (4 096 textes, " +
      "16 384 valeurs) évite de reprotéger les messages renvoyés à chaque tour ; stream_restorer remet les valeurs au " +
      "fil des fragments, mots seuls de quatre octets ou plus compris. Les séquences d’arrêt ne partent pas avec la " +
      "pseudonymisation. Hors ligne : seules les adresses de bouclage, privées et lien-local, vérifiées avant la " +
      "connexion.",
    marmot: "Imagine que tu demandes de l’aide pour un devoir à un prof d’une autre école. Le relais, c’est le " +
      "messager qui porte ta question et te rapporte la réponse. La pseudonymisation, c’est comme si le messager " +
      "remplaçait ton nom et celui de tes potes par des surnoms avant de partir, puis remettait les vrais noms en " +
      "revenant : le prof aide sans savoir de qui on parle.",
    bee: "Coupé, tout reste sur le Mac. Allumé sans pseudonymisation, la discussion part telle quelle chez le " +
      "fournisseur. Pseudalis arrêté ou trop ancien alors que la pseudonymisation est demandée : la question ne part " +
      "pas du tout, plutôt que de partir en clair. Une clé fausse ou un compte sans crédit donnent une erreur, sans " +
      "jamais montrer la clé."
  }
};

export const schemas = {
  relay: schema('Le relais : la question part pseudonymisée, la réponse revient en clair',
    box(16, 62, 120, 56, 'Gulliver', 's-blue', 'la discussion') + arrow(136, 90, 236, 90, 'texte') +
    box(236, 62, 128, 56, 'Pseudalis', 's-gold', 'clé de séance') + arrow(364, 90, 464, 90, 'pseudonymes') +
    box(464, 62, 120, 56, 'Moteur', 's-soft', 'Claude, Ollama…') +
    `<line x1="524" y1="118" x2="524" y2="176" class="s-line"/>` +
    arrow(524, 176, 136, 176, 'réponse : les vrais noms remis au fil du flux') +
    box(16, 154, 120, 44, 'Réponse', 's-blue') +
    text(300, 222, 'les images restent sur le Mac : un modèle local les décrit, la description part protégée', 's-f'), 235)
};
