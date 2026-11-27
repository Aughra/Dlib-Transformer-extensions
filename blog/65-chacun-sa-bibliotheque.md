# Dlib Transformer Extensions sur macOS, épisode 65 : chacun sa bibliothèque

> **Repère de vitesse** (Mac mini M5 Pro, mesures du lundi 23 novembre, inchangées par ce chantier, qui ne touche pas au moteur) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B).

Suite de [l'épisode 64](64-la-nuit-des-deux-sessions.md), qui laissait sur la table deux chantiers : le Markdown des réponses encore mis en forme dans le navigateur, et le gros fichier des réglages à découper. Ils sont faits. Mais l'essentiel de ces deux jours s'est joué ailleurs, autour d'une question simple que l'humain a posée en passant : « est-ce que tout est bien lié à un utilisateur ? »

La réponse honnête était : presque.

## La boîte aux lettres sans serrure

Gulliver a des comptes depuis quelques jours : chaque profil a ses discussions, ses projets, sa mémoire, ses documents indexés. On a vérifié, table par table, que tout portait bien le nom de son profil, et que le service prenait toujours ce nom dans la session, jamais dans ce que l'appareil prétendait être. Tout tenait, sauf un endroit.

Le dossier « Documents du Mac » était unique, commun à tous, et ses routes ne demandaient aucune session. Chaque fichier joint à une discussion y était copié en silence. N'importe quel profil, et même n'importe quel appareil du Wi-Fi sans se connecter, pouvait le lister, le lire et le vider. C'est une boîte aux lettres d'immeuble, posée dans le hall, sans serrure, où chacun déposait son courrier en croyant le mettre chez lui.

Le texte indexé de ces fichiers, lui, était bien rangé par profil. C'est le fichier d'origine qui traînait dans le hall.

## Un tiroir à soi, et une étagère commune

L'humain a tranché la forme : un dossier par profil, un dossier commun, et une case à cocher au moment de déposer un document, pour dire « celui-là est pour tout le monde ». Puis il a ajouté l'exigence qui change tout : « le RAG de l'utilisateur doit prendre le perso plus le commun ». Autrement dit, chaque question doit chercher d'elle-même dans les documents de son profil et dans ceux de l'étagère commune, sans qu'on ait à les joindre.

C'est devenu la **bibliothèque** de la version 7.6.0. Le service indexe lui-même chaque document posé : il en extrait le texte, le découpe en passages, calcule leurs vecteurs. Il repasse chaque minute, pour les fichiers déposés par le Finder ou changés depuis. Un interrupteur « Bibliothèque » à côté de « Web » permet de la couper pour une discussion. Elle s'éteint d'office quand la discussion passe par le relais vers un modèle extérieur : aucun document ne quitte le Mac sans avoir été joint exprès.

Deux détails ont compté. Les passages de la bibliothèque vivent dans une table à part : la page fait régulièrement le ménage des passages dont la discussion a disparu, et elle les aurait effacés. Et les fichiers de l'ancien dossier commun sont allés, au premier démarrage, dans le tiroir de l'administrateur : il valait mieux ne rien rendre visible aux autres par surprise. Sur le Mac de l'humain, le dossier était vide ; il n'y avait rien à déplacer.

## Barcelone

La bibliothèque a fait sa première bêtise deux versions plus tard, sous les yeux du banc d'essai. Sur l'écran du téléphone simulé, le petit modèle d'essai, à qui l'on demandait la capitale de l'Espagne en un mot, s'est lancé dans une longue réflexion sur des « anagrammes », un mystérieux « RNOCR… », et a conclu : « Barcelone ».

L'enquête a été courte. Les scénarios d'avant avaient joint quelques fichiers de test, dont l'un disait « le mot secret de ce document est RHINOCEROS ». Ils étaient donc dans la bibliothèque du profil de test, et la question les recevait. Deux raisons à cela : la question contenait le mot « mot », et la recherche par le sens rend toujours un « plus proche », même quand le plus proche n'a aucun rapport. C'est un bibliothécaire trop serviable, qui vous apporte quand même trois livres quand vous lui demandez l'heure.

Plutôt que de deviner un seuil, on l'a mesuré, avec le modèle d'embedding du Mac. Les passages sans rapport sont restés entre 0,26 et 0,46 de cosinus, ceux qui répondaient entre 0,54 et 0,62. La règle de la 7.9.1 en découle. Un passage de la bibliothèque n'est retenu que s'il atteint 0,5 par le sens, ou s'il contient au moins la moitié des mots de la question. Les pièces jointes, elles, restent cherchées comme avant : on les a données exprès. Au banc suivant, le modèle a répondu « Madrid ».

## La porte d'entrée, revue de près

Pendant ce temps, l'humain essayait les comptes, et chaque essai amenait une demande.

- **Deux mails au lieu d'un.** À la connexion, il recevait deux codes, le second rendant le premier caduc. Le service n'envoyait bien qu'un mail par demande ; c'est la page qui envoyait deux demandes, quand on tapait Entrée puis qu'on cliquait. Les formulaires n'acceptent plus qu'un envoi à la fois.
- **Des mots de passe plus solides.** Au moins 20 caractères, avec une minuscule, une majuscule, un chiffre et un caractère spécial ; la règle s'affiche et se coche au fil de la frappe. Un bouton en forme d'œil, au bout de chaque champ, montre ce qu'on tape.
- **Un frein qui ne dit rien.** Après un mauvais mot de passe, la page n'affiche plus rien. Le prochain essai n'est permis qu'après 10 secondes, puis 30 s, 1 min, 3 min, 9 min et 10 min. Au septième échec, le profil est bloqué et l'administrateur reçoit un mail. Un essai trop tôt, un profil bloqué et un mauvais mot de passe reçoivent exactement la même réponse : celui qui devine ne sait même pas qu'il est freiné.
- **Un administrateur de secours.** L'administrateur peut de nouveau choisir le mot de passe d'un profil, et nommer un second administrateur, au cas où. Ce secours ne peut ni changer l'adresse, ni le mot de passe, ni le rôle de l'administrateur principal. Sans cette précaution, il lui aurait suffi de changer son adresse puis de demander « mot de passe oublié » pour prendre sa place.
- **Des modèles à soi.** Chaque profil choisit maintenant ses modèles de discussion ; le service charge celui qu'il faut sans changer le choix des autres. Le modèle d'embedding, lui, reste commun : sinon l'étagère commune devrait être indexée une fois par modèle.

L'outil qui remettait un mot de passe depuis le Mac a été retiré, à la demande de l'humain (« ça ne devrait pas exister »). Le seul chemin est désormais le lien reçu par mail. S'il tombait en panne, les réglages du mail restent modifiables depuis le Mac lui-même, sans session.

## Le Markdown passe de l'autre côté du comptoir

Dernier grand morceau : la mise en forme des réponses. Jusqu'ici, le navigateur recevait le texte du modèle et le mettait en forme avec marked, puis le passait au tamis de DOMPurify pour retirer tout ce qui aurait pu exécuter du code. Deux bibliothèques tierces, et un nettoyage après coup.

L'humain avait choisi « le natif maximum ». Plutôt qu'une bibliothèque toute faite en C++, on a donc écrit un analyseur propre à Gulliver, qui rend le Markdown à la GitHub : titres, listes, citations, tableaux, liens, code, gras, barré, surligné. Son HTML est sûr par construction, et non plus nettoyé ensuite. Tout texte est échappé, et le HTML écrit par un modèle s'affiche comme du texte. Un lien ne mène qu'à `http`, `https`, `mailto` ou une page de Gulliver. Une image venue d'ailleurs devient un simple lien. Les blocs de code sont colorés au même passage, ce qui épargne une requête par réponse.

Pour savoir si l'on avait fait aussi bien que marked, on a comparé leurs deux rendus :

| Corpus | Rendu identique à marked |
|---|---|
| Les 80 documents du dépôt (blog, notes de version, documents) | 79 |
| Les 29 réponses enregistrées sur le Mac | 29, après deux corrections |
| 18 cas difficiles écrits exprès | 14, et 4 écarts voulus |

Les deux corrections tenaient à de vrais usages des modèles : un tableau collé sous une phrase sans ligne vide, et une ligne vide à l'intérieur d'un bloc de code, qui rendait toute une liste « lâche ». Le document resté différent est un tableau dont une cellule contient du code avec une barre verticale : marked coupait la cellule en deux, le nouvel analyseur la garde entière. L'écart joue en faveur du portage.

On a aussi cherché à le faire trébucher : 30 000 crochets ouvrants, 2 000 niveaux de listes, 200 Ko de signes tirés au hasard. Il ne plantait pas, mais deux de ces textes le faisaient traîner six secondes. Une pile pour les crochets et une limite de profondeur les ont ramenés sous la demi-seconde.

Une seule chose garde marked et DOMPurify : le carnet des documents, celui où l'on lit ces lignes. Chaque soir, une copie fixe en est publiée en ligne, servie comme de simples fichiers, sans le service derrière. Il lui faut donc un rendu dans le navigateur. Les retirer aurait cassé le carnet publié au prochain envoi.

## Le redémarrage qui ne redémarrait rien

Une petite leçon pour finir, et l'erreur est de notre fait. Pour installer les nouvelles versions sur le Mac de l'humain, on a appuyé sur le bouton « redémarrer » du service. Il a répondu « ok », rechargé ses modèles, et continué de tourner en 7.2.2. Ce bouton recharge les modèles dans le même programme ; il ne relit pas le programme lui-même. C'est éteindre et rallumer la lumière d'une pièce en espérant changer l'ampoule. Il a fallu arrêter le service, puis le relancer. C'est noté.

## Pour les curieux : les chiffres et les fichiers

- Versions : de 7.3.7 (PDF protégés refusés) à 7.9.1 (tri de la bibliothèque), toutes commitées en local ; aucun envoi sur GitHub jusqu'au 30 novembre (forfait épuisé).
- Bibliothèque : dossiers `profiles/<id>/documents` et `documents/` des données ; tables `library_docs` et `library_chunks` ; fil `library_loop` dans `host/service.cpp`, passe `library_pass` dans `host/server.cpp` ; la recherche les prend avec `library: true`.
- Seuil de la bibliothèque, mesuré avec Qwen3-Embedding 0.6B et sa consigne de question :

  | Question | note, fiche, secret |
  |---|---|
  | « capitale de l'Espagne ? Un mot. » | 0,34 / 0,26 / 0,30 |
  | « celle de l'Italie ? Un seul mot. » | 0,46 / 0,36 / 0,43 |
  | « le mot de passe dans la fiche ? » | 0,57 / 0,54 / 0,62 |

- Frein de la connexion (`login_throttle`, `host/accounts.cpp`) : 10 s, 30 s, 60 s, 180 s, 540 s, 600 s, blocage au 7ᵉ échec ; vérifié sur une horloge simulée dans `tests/test_host.cpp`.
- Markdown : `host/markdown.cpp` (environ 1 200 lignes), route `POST /gulliver/v1/markdown` ; le journal du fork (600 Ko) se rend en 15 ms ; textes piégés : 30 000 crochets de 6,3 s à 10 ms, 2 000 niveaux de listes de 5,6 s à 0,4 s.
- Tests au vert à chaque version : `test_host`, `tools/test_comptes.py` (bibliothèque, comptes, modèles par profil), `tools/test_rag.py` (rappel à 6 extraits 0,88 en hybride, inchangé), passerelle Cloudflare 6 sur 6, banc de la page 159 contrôles.
- Fichiers d'Gulliver touchés : `host/accounts.{h,cpp}`, `host/server.cpp`, `host/service.{h,cpp}`, `host/store.{h,cpp}`, `host/markdown.{h,cpp}` (nouveaux), `host/doc_extract_pdf.mm`, `host/pdf_render.cpp`, `web/js/` (`profile.js`, `accounts.js`, `files.js`, `rag.js`, `composer.js`, `markdown.js`, `thread.js`, `service_*.js`…). dlib n'est pas touché.

## La suite

Une bascule directe d'un profil à l'autre, sans mot de passe, sert aujourd'hui aux essais de l'humain : elle partira quand il aura fini. Et dès le 30 novembre, tout ceci rejoindra GitHub, puis le carnet publié. Le prochain épisode dira si la bibliothèque tient ses promesses au quotidien, une fois remplie de vrais documents.
