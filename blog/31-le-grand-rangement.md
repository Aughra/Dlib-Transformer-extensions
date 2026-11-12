# Dlib Transformer Extensions sur macOS, épisode 31 : le grand rangement

> **Repère de vitesse** (MacBook Air M2) : inchangé depuis l'épisode 30 : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens et **423 tokens/s** sur 4 096 tokens (2B), **152 tokens/s** sur un prompt d'environ 3 660 tokens (4B).

Suite de [l'épisode 30](30-la-table-de-multiplication.md) : le moteur lisait vite. Restait la page web d'Albireo, dont presque tout tenait dans un seul fichier de 7 274 lignes.

## Une maison d'une seule pièce

Le fichier `app.js` faisait tout : les discussions, les projets, les documents joints, la mémoire, les paramètres, la synchronisation entre onglets, l'envoi des questions et l'écriture des réponses. C'est une maison d'une seule pièce où la cuisine, la chambre et l'atelier se partagent le même plancher. On y vit, mais pour retrouver un tournevis, il faut tout parcourir, et l'on n'ose rien déplacer de peur de renverser quelque chose ailleurs.

Le but était d'abattre des cloisons à l'envers : bâtir des pièces, sans rien changer à ce qui s'y fait. Pour qui se sert d'Albireo, rien ne devait bouger.

## D'abord, vider les placards

Avant de déménager, on trie. Un script a cherché ce que la page ne montre plus : 31 textes jamais affichés (des suggestions de questions et des aides d'un sélecteur de rôle retiré en version 3.1.0), cinq règles de style sans élément, une fonction de détection de langue devenue inutile dans un fork qui ne parle que français. Le tri a aussi fait remonter trois souvenirs de Windows :

- le message affiché quand aucun modèle n'est actif renvoyait au « menu de la zone de notification », qui n'existe pas sur Mac ; il renvoie maintenant à l'onglet Modèles ;
- la liste des licences annonçait cuDNN, une bibliothèque des cartes NVIDIA, et SVOX Pico, la voix retirée en 3.0.0 ;
- les fichiers créés par l'agent n'étaient protégés que contre les extensions que Windows exécute (`.exe`, `.bat`…). Ils le sont aussi, désormais, contre celles que macOS ouvre comme un programme (`.command`, `.app`, `.pkg`, `.sh`…).

## Un déménageur qui ne touche à rien

Pour éviter de casser quoi que ce soit, on n'a rien réécrit. Le code a été déplacé **mot pour mot**, bloc par bloc, avec ses commentaires, par un petit outil qui calcule ensuite les « adresses » que chaque pièce doit connaître des autres : les lignes `import` et `export`. C'est un déménageur qui transporte les cartons fermés et se contente de mettre à jour le carnet d'adresses.

Les pièces ont été bâties du bas vers le haut : d'abord l'état commun et les outils, puis les boîtes de dialogue, les paramètres, la construction du contexte envoyé au modèle, les documents, les projets, la mémoire, et pour finir le fil des messages, la zone de saisie et l'écriture des réponses. Il y a maintenant 22 modules, et `app.js` ne garde que le démarrage et la carte des pièces : 156 lignes.

## Le relecteur et le robot

Deux gardiens ont vérifié chaque étape.

Le premier est un relecteur, ESLint, qui lit le code sans l'exécuter et signale tout nom employé quelque part sans être défini ou importé. Un oubli dans le carnet d'adresses ne lui échappe pas. Il a d'ailleurs rattrapé une erreur de l'outil de déménagement, et l'erreur est facile à commettre : en cherchant la ligne d'adresses à corriger, l'outil pouvait partir de la ligne d'avant, si bien que sept noms se sont retrouvés importés deux fois.

Le second est un robot qui se sert de la page comme un utilisateur. Un service d'essai, isolé de celui dont on se sert, tourne avec ses propres données et des modèles copiés depuis le Mac sans prendre de place. Chrome, sans fenêtre, y pose des questions, joint un document où est caché un mot secret, dessine un carré rouge et demande sa couleur, régénère une réponse, renomme une discussion, ouvre un second onglet pour voir si le premier le prévient, et rétrécit la page à la largeur d'un téléphone. Toute erreur dans la page fait échouer l'essai. Le robot a grandi au fil du rangement : 29 vérifications au départ, 106 à la fin. Il reste dans le dépôt, pour la prochaine retouche.

## Ce que le rangement n'a pas changé

Rien de visible, et c'était le but. Le robot a même relevé deux bizarreries qui préexistaient au rangement : sur un téléphone, le panneau latéral s'ouvre au premier affichage et recouvre la page ; et une image jointe à un modèle qui ne lit pas les images est écartée sans message dans le fil. On les a vérifiées sur la page d'avant le découpage : elles y étaient déjà. Elles attendront leur tour.

## Pour les curieux : les chiffres

| Étape | Version | Modules sortis | `app.js` |
|---|---|---|---|
| Départ | 4.11.0 | — | 7 274 lignes |
| Code mort retiré | 4.11.1 | — | 7 273 lignes |
| Socle | 4.11.2 | `state.js`, `dom.js` | 7 101 lignes |
| Boîtes et paramètres | 4.11.3 | `dialogs.js`, `settings.js` | 6 473 lignes |
| Contexte du modèle | 4.11.4 | `context.js` | 5 896 lignes |
| Documents et projets | 4.11.5 | `indexing.js`, `files.js`, `projects.js` | 4 765 lignes |
| Mémoire | 4.11.6 | `memory.js` | 4 123 lignes |
| Service, listes, brouillons | 4.11.7 | `status.js`, `lists.js`, `drafts.js` | 3 435 lignes |
| Fil des messages | 4.11.8 | `thread.js` | 2 509 lignes |
| Saisie et réponses | 4.11.9 | `composer.js`, `generation.js` | 1 513 lignes |
| Onglets, panneau, actions | 4.11.10 | `tabs.js`, `sidebar.js`, `conversations.js`, `background.js` | 463 lignes |
| Fin | 4.11.11 | `preferences.js`, `navigation.js`, `layout.js` | 156 lignes |

Les scénarios du robot, à la fin (`bash dlib/dlib/external/albireo/tools/page_test/page_test.sh`) :

| Scénario | Vérifications |
|---|---|
| Conversation, paramètres, projets, recherche, mémoire | 40 |
| Documents joints et documents de projet | 12 |
| Images (avec le Qwen3-VL 4B et son projecteur) | 12 |
| Actions du fil | 24 |
| Deux onglets | 11 |
| Largeur d'un téléphone | 7 |

Une seule fonction nouvelle est apparue dans tout le rangement, `cancelDraftSave()` : une pièce ne peut pas réaffecter une variable qu'elle a reçue d'une autre, et le minuteur des brouillons était remis à zéro depuis trois endroits.

## À suivre

Le Mac mini M5 Pro arrive dans une quinzaine de jours. D'ici là, le robot et le script de mesure de l'épisode 30 sont prêts pour son premier jour.
