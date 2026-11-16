# Dlib Transformer Extensions sur macOS, épisode 48 : le messager et les surnoms

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens. Le relais de cet épisode n'a pas de vitesse à lui : elle est celle du moteur extérieur auquel il s'adresse.

Suite de [l'épisode 47](47-les-deux-vitrines.md) : les deux applications du dépôt avaient reçu une porte entre elles. Cette fois, Albireo a reçu une porte vers l'extérieur, gardée par Pseudalis.

## Le messager

Un modèle qui tourne sur le Mac répond sans que rien ne sorte de la machine. C'est tout l'intérêt d'Albireo, et sa limite : un Mac de 24 Go fait tourner des modèles de quelques milliards de paramètres, pas les très grands modèles des centres de calcul. Pour une question difficile, on peut préférer l'un d'eux.

L'original, sous Windows, a ajouté cette possibilité dans ses versions 1.0.22 et 1.0.23 : un **relais**. Albireo y devient un messager. Il prend la discussion, la porte à un moteur extérieur (Claude chez Anthropic, le nuage d'Ollama, ou n'importe quel moteur qui parle comme celui d'OpenAI, y compris un autre ordinateur de la maison), puis rapporte la réponse comme si elle venait d'un modèle du Mac. La clé du compte extérieur reste chez le messager : la page et les autres programmes ne la voient jamais.

Le fork ne fusionne plus l'original depuis le 11 novembre. On a donc repris le relais à la main, en lisant l'original à son dernier état. Le cœur, environ 2 700 lignes, était écrit pour toutes les plateformes : seuls la lecture du registre de Windows et le chiffrement propre à Windows ont dû partir. Un agent a traduit ses commentaires en français, et l'on a vérifié, jeton par jeton, que le code lui-même n'avait pas bougé d'un caractère.

## Le vestiaire sans tickets

Le relais de l'original a une seconde moitié, la plus intéressante : il peut passer la discussion par Pseudalis avant de l'envoyer. Chaque nom, adresse ou numéro y est remplacé par un pseudonyme, et le moteur extérieur ne voit que les pseudonymes. Quand la réponse revient, Albireo remet les vrais noms à leur place.

C'est là que le fork coinçait. À l'épisode 2 du [blog de Pseudalis](pseudalis/02-des-pseudonymes-sans-retour.md), on avait décidé que les pseudonymes ne reviennent jamais : chaque document est protégé sous une clé tirée au hasard puis effacée. C'est un vestiaire qui ne donne pas de ticket : on y dépose son manteau, et personne ne peut plus le réclamer. Parfait pour un document qu'on garde en original de toute façon, impossible pour un messager qui doit rapporter les vrais noms.

On n'est pas revenu sur la décision pour les documents. Pseudalis a reçu, en version 3.1.0, une seconde clé, **de séance** : tirée au démarrage du service, gardée en mémoire seulement, jamais écrite sur le disque. Un texte envoyé avec le mot `session` est protégé sous cette clé, et la réponse donne, à côté du texte, la liste « pseudonyme → vraie valeur ». Un vestiaire avec tickets, mais qui brûle tous ses tickets à chaque fermeture. Et comme la clé reste la même tant que le service tourne, un même nom garde le même pseudonyme d'un message à l'autre : le moteur extérieur comprend qu'on parle toujours de la même personne.

L'original obtenait ces valeurs par un second appel à Pseudalis, qui relisait les pseudonymes. Ici, un seul appel suffit. Et si Albireo tombe sur un Pseudalis plus ancien, qui ne donne pas la liste, il refuse d'envoyer : mieux vaut une question qui ne part pas qu'une question qui part avec des pseudonymes impossibles à rendre.

## Les surnoms qui reviennent au fil de la phrase

Remettre les vrais noms n'est pas si simple, car la réponse arrive en morceaux, quelques lettres à la fois. Un pseudonyme peut être coupé en deux : « Xoy » dans un morceau, « doeh Nuggwdfsorn » dans le suivant. Le code de l'original, repris tel quel, garde en attente la fin d'un morceau tant qu'elle peut être le début d'un pseudonyme. Il ne remplace qu'aux limites des mots, pour ne pas changer un morceau d'un autre mot. Et il reconnaît aussi un nom de famille cité seul : si « Jean Dupont » est devenu « Xoydoeh Nuggwdfsorn », « M. Nuggwdfsorn » redevient « M. Dupont ».

Les images, elles, ne partent pas du tout quand la pseudonymisation est allumée : un modèle du Mac qui lit les images les décrit, et c'est la description, protégée comme le reste, qui voyage.

## Le cadenas rouge

Dans la page, le relais se règle dans l'onglet Service, sur une carte de plus, avec sa fiche et ses trois personnages. On y choisit le fournisseur, le modèle et la clé, puis le bouton « Tester » demande au moteur la liste de ses modèles, ce qui suffit à savoir si l'adresse et la clé sont bonnes.

Ensuite, dans la zone de saisie, un petit cadenas apparaît à côté du bouton d'envoi. Fermé, la discussion reste sur le Mac. Ouvert, il devient rouge et affiche le nom du modèle extérieur : les questions de cette discussion, et d'elle seule, quittent la machine. Chaque nouvelle discussion commence cadenas fermé. L'agent et ses outils restent aux modèles du Mac. Le bouton Web, lui, ne sert qu'avec Claude, qui cherche sur le web de son côté, cinq recherches au plus par réponse, facturées par Anthropic.

## Ce que le messager a le droit de faire

Un messager a besoin de règles. La clé est gardée dans un fichier que seul le compte du service peut lire. Dans les réglages, elle apparaît sous la forme `********`, et ce masque ne vaut que pour le fournisseur, l'adresse et l'utilisateur pour lesquels on l'a donnée : changer l'adresse oblige à retaper la clé. Elle ne peut donc pas être envoyée ailleurs en douce. Même quand un moteur extérieur recopie la clé dans un message d'erreur, Albireo la cache avant de l'afficher. En mode hors ligne, le messager ne sort pas de la maison : seules les adresses du Mac et du réseau local restent permises, vérifiées avant toute connexion.

Il reste une faiblesse, et on la dit franchement. L'accès depuis le réseau de la maison n'a plus de mot de passe depuis la version 5.0.0, à la demande de l'humain. Une fois le relais réglé, tout appareil du Wi-Fi peut donc s'en servir, et dépenser le crédit du compte extérieur, sans pouvoir lire la clé. C'est écrit dans les notes de version et dans la fiche, et l'humain décidera s'il veut réserver ces réglages au Mac seul.

## L'essai sans clé

Aucune clé de Claude ni d'Ollama n'est sur le Mac. Pour essayer le relais en vrai, et pas seulement avec les faux serveurs des tests, on lui a donné comme « moteur extérieur » Albireo lui-même, par son accès réseau, avec le vrai Pseudalis au milieu. C'est un peu comme s'envoyer une lettre à soi-même pour vérifier que La Poste fonctionne.

La consigne était de recopier « Jean Dupont habite 12 rue de la Paix à Paris. ». Avec le réglage de contrôle qui laisse les pseudonymes dans la réponse, on voit ce que le moteur a reçu : « Vkqgmsm Iovtudhdkil habite xfujwb rue de la Rwybndx à Pjxmnaguc. » Son raisonnement, en anglais, ne parle que de ces mots-là. Sans le contrôle, la réponse revient, au fil du flux : « Jean Dupont habite 12 rue de la Paix à Paris. » Aucun journal des deux services ne contient « Dupont ».

## Pour les curieux : les chiffres et les fichiers

- **Albireo 5.3.0** (le service et sa carte) puis **5.4.0** (le cadenas) ; **Pseudalis 3.1.0** (la clé de séance). Aucun fichier de dlib n'a été modifié dans cet épisode.
- Repris de l'original (`upstream/main` à `3bea372`) : `host/relay.{h,cpp}` et `host/pseudonyms.{h,cpp}`, et les parties du relais de `server.cpp`, `service.cpp`, `settings.cpp`, `http_fetch.cpp` et des tests. Adaptations du fork : un seul appel à Pseudalis (`"session": true`, réponse `values`), le dossier de Pseudalis vérifié par propriétaire, droits et ACL du Mac, la clé dans `relay.secret` (droits 0600) au lieu du chiffrement de Windows, plus de réglage des étiquettes.
- Pseudalis : `engine::session_key` et `engine::session_values()` (`engine/engine.{h,cpp}`), qui ouvrent les étiquettes des pseudonymes faits sous la clé de séance et rendent leurs valeurs.
- Page : carte « Relais vers un moteur extérieur » (`web/js/service.js`), cadenas (`layout.js`, `composer.js`), `chatRole()` et les capacités du modèle extérieur (`status.js`), `web_search` dans la requête (`generation.js`), messages d'erreur du relais en français (`background.js`, `i18n.js`).
- Tests : `test_host` passe, dont 22 vérifications du relais reprises de l'original (faux serveurs OpenAI, Ollama, Claude et Pseudalis) et une du fork (un Pseudalis qui ne donne pas les valeurs garde le texte) ; test complet d'Albireo, 59 sur 59 ; banc de la page, 128 vérifications ; test complet de Pseudalis, 35 sur 35.
- Trois maladresses en chemin. Deux scénarios du banc ont d'abord échoué : le format français écrit « 1 000 » avec une espace fine insécable, et l'essai du relais signalait la clé manquante avant le mode hors ligne ; le cadenas, lui, n'apparaissait pas, parce que la page ne se redessinait pas quand seul le relais changeait. Et le vrai Pseudalis, relancé, s'annonçait encore en 3.0.0 : il avait été compilé avant le changement de numéro.

## À suivre

Le relais est prêt à recevoir une vraie clé ; on attend l'avis de l'humain sur les réglages réservés au Mac. Le Mac mini M5 Pro arrive dans une dizaine de jours, avec ses mesures.
