# CLAUDE.md — Dlib-Transformer-extensions (fork Mac)

## Langue et communication

- **Répondre exclusivement en français, sur un ton doux et concis.**
- Expliquer simplement : chaque mot technique avec une comparaison de la vie courante, comme pour un ado. Sections courtes, tableaux quand ça aide.
- Expliquer brièvement l'intention d'une modification avant d'en montrer les détails, et nommer les fichiers et sections exacts qu'elle touche.
- Les messages de commit sont en français. Les fichiers propres au fork (ce fichier, `logs/`, le mémo de portage) sont en français ; le blog est en français seulement. **Les commentaires du code d'Albireo et de Pseudalis sont en français** (décision de l'humain, 2026-11-11) : tout commentaire nouveau ou modifié s'y écrit en français. Restent en anglais : les consignes internes envoyées au modèle, les identifiants, les messages de log, dlib, et les documents de l'original (README).
- Si une question est ambiguë, poser **une seule** question de clarification, précise.
- Quand il y a plusieurs options, les présenter avec leurs pour et leurs contre, et dire laquelle est recommandée.
- Pas de flatterie, pas de résumé de la question avant de répondre.

---

## Le projet

Ce dépôt est un **fork** de [`Cydral/Dlib-Transformer-extensions`](https://github.com/Cydral/Dlib-Transformer-extensions) : dlib, des extensions Transformer (modèles de langage, GGUF, LoRA, distillation, vision) et deux applications Windows 11 construites dessus, **Albireo** (moteur GGUF et service de chat) et **Pseudalis** (anonymisation de documents).

**But du fork : faire tourner le projet sur Mac Apple Silicon (arm64), et seulement là.** Machine actuelle : Mac M2 (8 cœurs, 24 Go). Machine prévue : Mac mini M5 Pro, 64 Go. **Du pur Apple** (décision de l'humain, 2026-11-11) : dans Albireo et Pseudalis, le code propre à Windows, à Linux, aux processeurs x86 (AVX2, VNNI), à CUDA et à OpenVINO est retiré, ainsi que `dist/` (installateurs et flux signés Windows de l'original). La bibliothèque dlib reste telle quelle : elle est générique, et la nettoyer n'apporterait rien. Le code neuf n'écrit que pour macOS sur Apple Silicon.

Les solutions propres au Mac sont la règle (instructions NEON, `__fp16`, Accelerate, Metal, Objective-C++, un standard C++ plus récent).

### Deux dépôts distants

| Nom | Dépôt | Rôle |
|---|---|---|
| `origin` | `Aughra/Dlib-Transformer-extensions` | Le fork (public, comme l'original), où l'on pousse. Le compte `gh` connecté sur le Mac est `Aughra`. |
| `upstream` | `Cydral/Dlib-Transformer-extensions` | L'original, qui continue d'avancer ; on ne fait qu'y lire, et on ne le fusionne plus (voir ci-dessous). |

### Détaché de l'original (depuis le 2026-11-11)

Décision de l'humain : l'original avance trop vite pour être suivi (16 commits en deux jours), et le moteur Metal demande des changements profonds. **Le fork ne fusionne plus `upstream/main`** ; la dernière fusion est `535a6fb` (original à `184ea00`, Albireo 1.0.12). Donc :

- Les fichiers de l'original peuvent être modifiés quand le travail le demande, sans la règle du plus petit changement sous `#ifdef __APPLE__`. Rester propre et lisible : ne pas reformater ni réorganiser sans raison.
- Préférer encore un fichier neuf pour un module nouveau (le moteur Metal, par exemple).
- `upstream` reste en lecture : on peut y reprendre une idée ou une correction **à la main**, en le disant dans le journal, jamais par `git merge`.
- Albireo et Pseudalis ne gardent que macOS (voir « Le projet ») ; ne jamais regénérer ni re-signer les flux ou clés de l'original.

---

## Construire sur Mac

La référence des commandes et des résultats est [docs/memo-portage-macos-arm64.md](docs/memo-portage-macos-arm64.md). En bref :

- **dlib + exemples** : `cmake -S dlib/examples -B build/macos-arm64-cpu` (options CPU seules, voir le mémo), puis `cmake --build build/macos-arm64-cpu`.
- **Tests dlib** : `cmake -S dlib/dlib/test -B build/macos-arm64-tests`, puis `build/macos-arm64-tests/dtest`.
- **Albireo / Pseudalis** : `cmake -S dlib/dlib/external/<app> -B build/macos-<app> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64`.
- **Sonde Metal** (GPU du Mac, hors moteur) : `macos/metal-sonde/`, propre au fork ; commandes dans son README.
- **Page d'Albireo** : `bash dlib/dlib/external/albireo/tools/page_test/page_test.sh` (ESLint et scénarios dans Chrome sans fenêtre, sur un service d'essai isolé) après tout changement de la page ; détails dans son README.
- **Service d'Albireo** : lancement et réglages dans le mémo ; les réglages se changent dans l'onglet « Service » de la page, et l'accès depuis le réseau de la maison (http, port 8444, mot de passe) s'y ouvre.

Tous les dossiers de build vont sous `build/`, que `.gitignore` ignore déjà. Un dossier par configuration. Avec Ninja, `-- -k 0` relève toutes les erreurs en un seul passage. Les compilations longues tournent en arrière-plan.

Outils présents sur le Mac : AppleClang (Xcode), CMake et Ninja (Homebrew), `gh`, `gstat` (coreutils).

**Mesurer la vitesse.** Le Mac actuel est un MacBook Air M2 sans ventilateur. Avant toute mesure : `pmset -g | grep lowpowermode` doit donner `0` (sinon le demander à l'humain : en mode économie d'énergie, le moteur va environ dix fois moins vite), et noter la charge (`sysctl -n vm.loadavg`). Pour comparer deux versions, les mesurer en alternance (A, B, A, B) dans la même séance, jamais une version aujourd'hui et l'autre demain. Une optimisation n'est gardée que si elle gagne dans ces conditions. Toutes les mesures du moteur en une commande, en alternance processeur / GPU, avec la machine relevée : `bash dlib/dlib/external/albireo/tools/bench_macos.sh [essais]` (tableau Markdown dans `build/bench/`) ; à lancer en premier sur une nouvelle machine.

---

## Méthode de travail

- **Au début de chaque session : lire [logs/reprise.md](logs/reprise.md) en premier**, puis, pour information seulement, lancer `git fetch upstream` et dire combien de commits l'original a reçus depuis la dernière fusion.
- Énoncer ses hypothèses et traduire la demande en **critère vérifiable** avant de coder (par exemple « `cmake --build` finit sans erreur et `test_host` affiche *all tests passed* »).
- Écrire le minimum qui résout le problème. Ne pas toucher aux fichiers hors du sujet de la demande.
- Signaler toute dépendance ajoutée, et pourquoi.
- Ne déclarer « ça marche » que ce qui a été vérifié sur le Mac. Ce qui a compilé sans avoir été lancé se dit « compilé, pas testé ».
- Les analyses techniques vont au fond : dire clairement quand une architecture a un défaut.

### Sécurité

- Ne jamais afficher, journaliser ni commiter de secrets : clés (`~/keys/`, clés privées des flux de mises à jour), jetons, mots de passe.
- Ne pas regénérer ni re-signer les fichiers `*.sig` et les clés publiques de l'original.
- Moindre privilège dans toute proposition d'architecture ; signaler tout anti-pattern de sécurité repéré.

---

## Reprise, journal et documentation

### `logs/reprise.md` : l'état vivant du chantier

[logs/reprise.md](logs/reprise.md) dit où en est le chantier : objectif, fait, en cours, à faire, décisions, problèmes ouverts, commandes pour reprendre. Il est **réécrit** en continu, et son but est qu'une coupure (session fermée, contexte perdu, changement de machine) ne fasse jamais perdre plus d'une sous-étape.

- **Le mettre à jour à chaque fin de sous-étape**, avant de passer à la suivante.
- **Le lire en premier** au début de chaque session.

### `logs/journal-modifications.md` : l'historique

Un seul fichier, où chaque intervention **ajoute une entrée en tête** (le plus récent en haut), sans réécrire les précédentes :

```markdown
## AAAA-MM-JJ — <titre court à l'impératif>

- **Quoi** : résumé du changement.
- **Fichiers** : chemins touchés.
- **Pourquoi** : intention.
- **Étapes** : actions réalisées (build, tests, commit…).
```

Ce fichier-ci (CLAUDE.md) décrit l'**état actuel** des règles et du projet : quand une section devient fausse, on la réécrit. Les dates et l'historique vont au journal.

### La documentation suit le code

Documents du fork qui doivent toujours rester **vrais** :

- [docs/memo-portage-macos-arm64.md](docs/memo-portage-macos-arm64.md) : commandes de build pour Mac et section « État constaté » (ce qui compile, ce qui passe les tests, les problèmes restants) ;
- ce fichier CLAUDE.md ;
- [logs/reprise.md](logs/reprise.md) ;
- [docs/notes-pour-l-auteur.md](docs/notes-pour-l-auteur.md) (voir ci-dessous).
- [docs/differences-mac-windows.md](docs/differences-mac-windows.md) (voir ci-dessous).

**À la fin de chaque sous-étape**, avant de la déclarer terminée : relire ces documents et corriger ce que le changement rend faux ou incomplet. Une doc qui décrit un état dépassé est un défaut, au même titre qu'un test qui ment. Critère vérifiable : aucune commande, option, cible ou résultat cité dans ces documents ne contredit le code ni le dernier build.

Les documents de l'original (`Readme.md`, `examples/Readme.md`, les README d'Albireo et de Pseudalis) ne sont modifiés que si un changement du fork les rend faux, et le plus légèrement possible (règle de fusion).

### Les notes pour l'auteur (à transmettre à l'auteur de l'original)

[docs/notes-pour-l-auteur.md](docs/notes-pour-l-auteur.md) recense, en français et **de façon très explicite**, ce que le portage révèle sur le code de l'original. L'humain le transmet à l'auteur : il est écrit pour lui, sans jargon propre au fork, et doit pouvoir se lire seul.

- **Chaque fois qu'on trouve une erreur de l'original**, l'y ajouter dans la même sous-étape. Une erreur, c'est un bug sur toute plateforme, un test fragile, un avertissement du compilateur, un réglage de serveur suspect, ou un code propre à Linux ou à x86 qui casse sur Mac. Ranger l'entrée dans la bonne partie : bugs et tests fragiles, portabilité macOS, ou remarques. Mettre à jour le tableau du résumé.
- **Chaque entrée donne** :
  - l'endroit exact dans l'original (fichier et ligne dans `upstream/main`, avec le commit de référence en tête du document) ;
  - ce qui se passe, avec le message d'erreur copié ;
  - comment le reproduire ;
  - la correction du fork, avec son commit, ou une suggestion marquée **non testée** ;
  - ce qui est vérifié et ce qui ne l'est pas ;
  - une ligne **« Mac ou Windows »** : « aussi sous Windows » quand le défaut est dans du code ou un service communs (l'humain en fait alors une PR vers l'original), « propre au Mac » (ou à Linux) quand il tient au système. Faute de machine Windows, dire que c'est déduit du code, non vérifié. Reporter le classement dans la colonne « Mac ou Windows » du résumé.
  - pour une entrée « aussi sous Windows » qui se corrige dans le code : le **nom du fichier**, l'**extrait du code avant** (avec ses numéros de ligne dans `upstream/main`) et le **code après la correction**, essayé sur le Mac dans une copie hors du dépôt quand c'est possible (le fork ne modifie pas le fichier de l'original pour cela), et marqué non testé sous Windows.
- **Chaque gain de vitesse** y est consigné dans la partie « Vitesse » : les temps d'avant, le changement (quoi, exactitude, commit) et les temps d'après, en mesures alternées selon la règle de mesure. Mettre aussi à jour le tableau « Bilan ».
- Quand l'original corrige un point après une fusion, le marquer comme corrigé, avec le commit de l'original, plutôt que de l'effacer.
- Ni secret, ni chemin privé (`/Users/...`), ni adresse e-mail.
- Critère vérifiable : chaque correction du fork qui touche un fichier de l'original, et chaque noyau ou optimisation du fork, a son entrée dans ce document.

### Le tableau des différences avec Windows

[docs/differences-mac-windows.md](docs/differences-mac-windows.md) résume, en tableaux par thème (vitesse du moteur ; modèles ; système et sécurité ; interface), **tout ce que le fork fait autrement que l'original Windows** : ce que fait l'original, ce que fait le fork, pourquoi, le gain mesuré, et où trouver le détail (changement N des notes pour l'auteur, version des notes de version). Demande de l'humain (2026-11-12) : un seul endroit pour voir d'un coup d'œil ce qui a changé et pourquoi.

- **Chaque changement qui distingue le fork de l'original** (gain de vitesse, fonction ajoutée ou retirée, choix de sécurité, portage système) y ajoute ou met à jour sa ligne, dans la même sous-étape que le changement.
- Les gains viennent des mesures alternées (règle de mesure) ; un changement sans mesure le dit (« — »).
- La ligne « Dernière mise à jour » en tête donne la version d'Albireo.
- Critère vérifiable : chaque « Changement N » de la partie Vitesse des notes pour l'auteur et chaque version des notes de version qui ajoute ou retire une fonction a sa ligne dans le tableau.

### Le blog du portage (un épisode par grande étape, en français)

Le blog raconte le portage **épisode par épisode**, comme un billet de carnet, pour un lecteur curieux qui n'est pas forcément développeur. Il vit dans [blog/](blog/), un dossier propre au fork (aucun conflit avec l'original). Les épisodes 1 à 3 ont d'abord paru sur le carnet TortueTech (`/carnet/dlib-transformer-extensions-macos*`) : ils fixent **le ton à suivre**. Claude ne publie rien en ligne : l'humain décide où et quand.

- **Un épisode par grande étape, en français seulement** : `blog/NN-titre-court.md`, numéroté à la suite. Est une grande étape : un programme ou une application qui compile ou tourne pour la première fois sur Mac, une décision de portage importante, une fusion de l'original qui change la donne, un tournant. Un épisode publié ne se réécrit pas ; on ne le corrige que pour une erreur de fait.
- **Écrit dans le même commit que la fin de l'étape**, avec l'entrée du journal, et ajouté au sommaire [blog/README.md](blog/README.md), et avec la mise à jour des notes de version d'Albireo (voir « Version et notes de version d'Albireo »).
- **Forme d'un épisode** :
  - titre `# Dlib Transformer Extensions sur macOS, épisode N : <sujet>` ;
  - juste sous le titre, une ligne en citation **« Repère de vitesse »** : les tokens/s d'écriture (2B, et 4B quand il est mesuré) et de lecture valables à la fin de l'épisode, tirés du journal ; « pas encore de mesure » avant que le moteur tourne (demande de l'humain, 2026-11-11) ;
  - une ouverture d'une ou deux phrases qui renvoie à l'épisode précédent (« Suite de l'épisode N : … ») ;
  - des sections `##` aux titres parlants, souvent imagés (« L'envoi refusé : bonne clé, mauvais casier », « Cinq obstacles, une seule famille ») ;
  - quand il y a de la technique précise, une section **« Pour les curieux : … »** en fin d'épisode, avec les commandes, les chiffres et les extraits de log, pour que le corps du texte reste lisible sans eux ;
  - une fin **« La suite »** ou **« À suivre »** qui annonce l'épisode suivant (avec un lien quand il existe).
- **Ton** : journalistique, posé, au « on » (« on a voulu le voir tourner sur un Mac »), pas à la première personne de Claude, sans dialogue mis en scène. Chaque notion technique reçoit une **image de la vie courante** (le cahier photocopié, la clé et le casier, l'architecte et l'immeuble, la voiture électrique sans boîte de vitesses, le sel dans un autre placard). Phrases complètes et sobres, un peu d'humour discret, aucune emphase. Les erreurs commises se disent franchement (« l'erreur est facile à commettre et qu'on l'a commise »), et le mérite est rendu à qui il revient (« le mérite revient à l'amont, pas au portage »).
- **Exactitude** : chaque chiffre vient du journal, des commits ou d'une sortie réelle ; un extrait de code, de log ou de commande est copié, jamais inventé. Distinguer ce qui est vérifié de ce qui ne l'est pas (« compilé, pas encore lancé »).
- **Tableaux** pour comparer (applications, options, relevés de build), listes à puces pour énumérer des causes ou des choix.
- **Aucun secret ni donnée privée** : pas d'adresse e-mail, de chemin privé (`/Users/...`), de clé ni de jeton. Les noms des comptes GitHub du fork peuvent apparaître, comme dans l'épisode 1.
- Critère vérifiable : chaque `blog/NN-*.md` figure au sommaire, commence par le titre « Dlib Transformer Extensions sur macOS, épisode N » suivi de sa ligne « Repère de vitesse », et ne contient ni adresse e-mail ni chemin `/Users/`.

### Version et notes de version d'Albireo

Le fork numérote Albireo à part de l'original, à partir de **2.0.0** (2026-11-11), en gestion sémantique des versions.

- **Format toujours `x.y.z`** (trois nombres, sans suffixe). Le choix de l'incrément revient à Claude, selon la règle ci-dessous (accord de l'humain, 2026-11-11).
- **Un incrément à chaque modification d'Albireo** (code, page, service, moteur) livrée par un commit : `python3 dlib/dlib/external/albireo/tools/bump_version.py patch|minor|major` met à jour `common/version.h`, `project()` du `CMakeLists.txt` et `web/openapi.json`, que le build compare. Correctif (`patch`) pour une correction ou un texte, mineur (`minor`) pour une fonction ou un réglage nouveau, majeur (`major`) pour une rupture. Les commits qui ne touchent que la doc du fork, le journal ou le blog ne changent pas la version.
- **Les notes de version** ([web/changelog.md](dlib/dlib/external/albireo/web/changelog.md), en français, affichées par le lien « Notes de version » de l'onglet « À propos ») ont une section par version, **de la plus récente à la plus ancienne**, séparées par un grand trait (`---`). Elles remontent jusqu'au premier commit du fork (2026-11-04) : avant 2.0.0, les sections portent le numéro de la version de l'original sur laquelle reposait le fork (1.0.8, 1.0.11, 1.0.12). Elles sont **mises à jour à chaque fois que le blog l'est**, dans le même commit, et la version du fork y est alors arrêtée. Leur ton est **professionnel** : phrases neutres et factuelles, sans image ni humour, rubriques « Plateforme », « Performances », « Réglages », « Accès et documents », « Sécurité », « Corrections » selon le cas, chiffres tirés du journal.
- Critère vérifiable : le numéro de `common/version.h` a sa section dans `web/changelog.md` dès qu'un épisode du blog le mentionne ou le suit.

### Français seulement

Le fork ne fait que du français (décision de l'humain, 2026-11-11) : l'interface d'Albireo est en français seulement, sans choix de langue (la lecture à voix haute a été retirée en 3.0.0). Tout nouveau texte visible par l'utilisateur s'écrit en français, sans traduction. Pseudalis garde l'anglais et le français, et rien d'autre ; ses détecteurs de données personnelles, qui vivent dans dlib, reconnaissent toujours les autres langues. Les commentaires du code sont en français (voir « Langue et communication ») ; les consignes internes envoyées au modèle restent en anglais.

### La page des réglages d'Albireo

L'onglet « Service » des paramètres d'Albireo ([web/js/service.js](dlib/dlib/external/albireo/web/js/service.js), fichier du fork, en français) expose les réglages du service à l'humain, qui s'en sert depuis le Mac ou depuis son téléphone.

- **Tout réglage qui peut s'exposer y est** : quand le programme gagne ou change un réglage (service, moteur, GPU, réseau), l'ajouter à l'onglet dans la même sous-étape, s'il peut se changer depuis la page (en direct, ou par « Appliquer et redémarrer »), avec sa validation côté service (`validate_settings` dans `host/server.cpp`) et `tools/make_openapi.py`.
- **Chaque réglage a sa fiche** : effet en une ligne, principe, avantages, inconvénients, au moins un schéma, puis l'explication normale, celle du hibou 🦉 (très technique), de la marmotte 🦫 (pour un ado) et de l'abeille 🐝 (ce qui arrive si on pousse trop ou qu'on s'en passe). Chaque fait d'une fiche est vérifié dans le code, le journal du service ou une mesure, comme pour le blog.
- **Les réglages conseillés suivent les mesures** : l'objet `recommended` de `service.js` donne les valeurs que le bouton « Revenir aux réglages conseillés » remet. À chaque mesure qui change le meilleur choix, le mettre à jour, avec sa source en commentaire et dans le journal.
- Critère vérifiable : chaque clé acceptée par `validate_settings` (sauf celles d'un autre usage : `engine`, `device_mb`, et `language`, que le français seul rend sans objet) a sa carte dans l'onglet.

### Autorité

CLAUDE.md est le document d'autorité du fork. En cas de contradiction avec un autre document, la signaler puis la résoudre ici.

---

## Git

- On travaille sur `main`. Pour garder un état, poser un tag (`archive/<sujet>-<date>`).
- **Commit de soi-même** après chaque unité de travail cohérente et **vérifiée** (build ou test réussi), avec son entrée dans le journal. Messages en français, concis, à l'impératif : le *quoi* et le *pourquoi*.
- **Push vers `origin` seulement**, après chaque commit. Jamais de push vers `upstream`, jamais de `--force`, jamais de PR vers l'original sans demande explicite.
- **Ne plus fusionner l'original** (décision du 2026-11-11, voir « Détaché de l'original ») ; si l'humain revenait dessus : `git fetch upstream` puis `git merge upstream/main`, seulement à sa demande. En cas de conflit, s'arrêter, montrer les deux versions et expliquer le choix à faire.
- Ne jamais commiter `build/`, les modèles `.gguf` ni les `.dat` (déjà ignorés).
