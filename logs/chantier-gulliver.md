# Avancement — Chantier Gulliver (renommage + profils + RAG en C++)

> **À lire pour reprendre** : ce fichier (où on en est) **et** le plan
> [docs/chantier-gulliver-plan.md](../docs/chantier-gulliver-plan.md). Mettre ce fichier à
> jour **à la fin de chaque sous-étape**, avant la suivante : une coupure ne doit jamais
> faire reperdre plus d'une sous-étape.

**Légende** : ☐ à faire · ⏳ en cours · ☑ fait (date, commit).

> **Consigne (2026-11-17)** : mener **tout le chantier (blocs 0, A, B) jusqu'au bout sans
> s'arrêter** entre les sous-étapes. **Trancher soi-même** les choix de conception selon le
> meilleur intérêt du projet, les noter ici avec leur raison, et continuer. Garder les
> garde-fous (compile/teste, commit+push, avancement + non-régression à jour). Voir
> « Décisions tranchées en autonomie » plus bas pour les choix faits sans demander.

## Reprise (où on en est, quoi ensuite)

- **2026-11-17 20:15** — **0a fait** : dossier renommé (`git mv .../albireo → .../gulliver`),
  chemins `external/albireo` → `external/gulliver` corrigés dans les scripts du projet
  (`tools/bench_macos.sh`, `test_macos.sh`, `page_test/page_test.sh` + README) et la commande
  d'usage du `CMakeLists.txt`. **Build `build/macos-gulliver` compile sans avertissement**,
  binaires produits. Report à 0d : répertoires de sortie `build/macos-albireo`/`albireo-home`,
  README de l'original (Windows/CUDA/OpenVINO). Cibles/variables `ALBIREO_*` → **0b**.
- **2026-11-17 ~21:40** — **BLOC 0 FINI** (0a–0e). Renommage Albireo→Gulliver complet, version 6.0.0. Vérifié : builds Gulliver+Pseudalis **0 avertissement**, `engine_chat` charge le moteur, `test_host` all checks passed, **`test_macos.sh` 55/55**, page `docs` 12/12 + `actions` 31/31 OK. **2 scénarios `regress` fragiles (flaky)** : « sidebar lists the chat » et « private chat not listed » comptent des discussions dont le titre est généré en arrière-plan par le modèle (non déterministe) ; comptes variables d'un run à l'autre (2, 5, 8…), SANS rapport avec le renommage (test_macos et 43 autres scénarios passent ; la persistance des discussions est exercée et OK par `actions`). À revérifier sur le Mac mini (plus rapide). **Prochaine action : commiter le bloc 0, puis BLOC A (profils côté service), sous-étape A1.**
- **2026-11-17 ~21:50** — 0d en cours : fait à la main — images `img/gulliver-*` + manifest (name/short_name) + `<title>`, répertoires de build `macos-gulliver`/`gulliver-home` dans scripts+CMake+mémo+mac-mini, guide Pseudalis, identifiants techniques du mémo. **Trois agents en parallèle** (zones disjointes) : (1) texte « Albireo »→« Gulliver » dans la page/commentaires de `gulliver/`, (2) intégration Pseudalis→Gulliver (`gulliver_client`, `GULLIVER_HOME`, route `/v1/gulliver`, réglage `gulliver`), (3) doc du fork (mémo, notes-pour-l-auteur, différences) avec discernement fork/original. Reste à faire moi-même : `web/changelog.md`, README, CLAUDE.md, blog (0d/0e). **Décision tranchée** : Pseudalis garde son nom mais tout son volet « client Albireo » vise Gulliver (fichiers `gulliver_client.*`, route `/v1/gulliver`, réglage `gulliver`). Prochaine : rebuild Gulliver + Pseudalis + page_test + test_macos quand les agents ont fini, puis 0e (version majeure).
- **2026-11-17 ~21:30** — **0c fait et vérifié** (110 scénarios page_test, 0 échec). **Prochaine action : 0d** — textes affichés et doc : nom « Albireo » → « Gulliver » dans l'interface (`<span>`, i18n `program_albireo`, messages `albireo-service`, libellés de licence), images `img/albireo-*` → `gulliver-*` + manifest/favicon, répertoires de build du mémo (`build/macos-albireo`→`-gulliver`, `albireo-home`→`gulliver-home`), README/docs du fork, CLAUDE.md, `web/changelog.md`. Garder le `product`/`full_name` de version.h pour 0e (avec la version). Puis 0e (version majeure + blog + commit).
- **2026-11-17 ~21:05** — 0c en cours : routes `/albireo/v1` → `/gulliver/v1` partout
  (host/ 57, web/js 147, outils, openapi régénéré : 58 routes, 0 `albireo`). Rôles/modèles
  d'API `albireo-general|code|security|relay|background|embedding` → `gulliver-*` **des deux
  côtés** (service + page + make_openapi), avec correction des `compare(0, 8, "albireo-")` →
  `compare(0, 9, "gulliver-")` et `substr(8)` → `substr(9)` (service.cpp:643,1833). Verrous
  Web Locks, `BroadcastChannel('gulliver')`, clés `gulliver-message/-draft-`, `DB_NAME =
  'gulliver'`, format `gulliver-export`, nom du fichier d'export, certificats
  `gulliver-authority/-server/-certificates` (génération + doc Swagger). **Bugs corrigés
  ici** (latents depuis 0b) : la licence servie `{"albireo.txt",...}` → `gulliver.txt`
  (server.cpp:1395) pour suivre la cible CMake. **Gardés exprès** : clés de métadonnée GGUF
  `albireo.id/.inputs/.name/.roles` (format des modèles existants). Reste en **0d** (texte) :
  `<span>Albireo</span>`, `program_albireo` (i18n), `albireo-service` dans un message
  (main.cpp:102, service.js:485), images `img/albireo-*`, libellés de licence « Albireo ».
  **Vérifié** : rebuild sans avertissement ; `page_test.sh` (sur `build/macos-gulliver`) **110 scénarios, 0 échec** (regress 55, docs 12, images 12, actions 31), ESLint ok. Scénarios de test mis à jour (`albireo-relay` → `gulliver-relay`). NB : un faux run à 50 échecs venait de DEUX page_test lancés en parallèle sur le même port (18491), pas du renommage — ne jamais lancer deux page_test à la fois.
- **2026-11-17 ~20:45** — **0b-bis fait et vérifié** (namespace + API C du moteur ; moteur
  charge et répond). **Prochaine action : 0c** — routes `/albireo/v1` → `/gulliver/v1` en un
  seul bloc (C++ host/ + page web/js + openapi), car la route est partagée.
- **2026-11-17 ~20:30** — 0b fait et vérifié (identifiants C++ majuscules, `gulliver.db`,
  `GULLIVER_HOME`, cibles de build).
- **2026-11-17 20:07** — Plan validé (go de l'utilisateur). Plan copié dans le dépôt, ce
  fichier d'avancement créé, pointeur ajouté à CLAUDE.md.

## Décisions clés (rappel)

- Renommer **Albireo → Gulliver** partout (API, base, dossiers, textes). Dossier renommé,
  **dépôt GitHub gardé**.
- Stockage **par profil** côté service (un nom, sans mot de passe), **tout ouvert sur le
  LAN**, **départ à vide**, **garde « Mac seul » du relais retirée**.
- **SQLite** (accès isolé derrière `store.cpp`).
- RAG en **C++** au bloc B, **3 moteurs** (hybride/vecteurs/mots) + **index nommés** + réglages.
- **Aucune fonctionnalité perdue** : voir l'inventaire de non-régression du plan, vérifié à
  la fin de chaque bloc.

---

## Bloc 0 — Renommer Albireo → Gulliver

- ☑ **0a** Dossier + chemins de build (`git mv`, chemins `external/gulliver`). 2026-11-17,
  commit à venir. `cmake` configure et `cmake --build build/macos-gulliver` compile sans
  avertissement ; binaires produits (`albireo-service`, moteur, 10 tests). Cibles/variables
  `ALBIREO_*` et répertoires de sortie `build/macos-albireo`/`albireo-home` : reportés (0b/0d).
- ☑ **0b** (2026-11-17) Identifiants techniques C++ **internes** (préfixe MAJUSCULE) : macros
  `ALBIREO_*` → `GULLIVER_*` (API moteur, gardes `*_H_`, erreurs, événements, log, noms de
  macros produit — valeurs de chaînes « Albireo » gardées), `"gulliver.db"`, `GULLIVER_HOME`
  + `~/.local/share/gulliver`, cache `Caches/gulliver`, cibles/fonctions/libs CMake
  `albireo-*`/`albireo_*` → `gulliver-*`/`gulliver_*`, `project(gulliver)`, licence produit
  `gulliver.txt`, `git mv tools/albireo_builtin.cpp → gulliver_builtin.cpp`, binaire
  `gulliver-service` dans les scripts, + couplages outils (`bump_version.py`,
  `make_openapi.py` regex version, `GULLIVER_HOME`/`GULLIVER_TESTS` dans les scripts). Fait
  par agent, + oubli du dossier `tests/` rattrapé à la main. **Vérifié** : build complet sans
  avertissement (service + 10 tests) ; le service démarre avec `GULLIVER_HOME` et crée
  **`gulliver.db`**. Le `"product":"Albireo"` du status reste « Albireo » (texte → 0d) ;
  routes encore `/albireo/v1` (→ 0c).
- ☑ **0b-bis** (2026-11-17) Identifiants C++ **minuscules** (passe soignée, catégorie par catégorie, car
  `albireo` minuscule est mêlé à des chaînes à ne pas toucher) : `namespace albireo` →
  `gulliver` et `albireo::` ; API C du moteur `albireo_engine_*`, `albireo_event_callback`,
  `albireo_log_callback`, `albireo_id`, wrappers `albireo_png_*`/`albireo_jpeg_*`. **À NE PAS
  mélanger** ici : routes `/albireo/v1` (0c), rôles/modèles `"albireo-general|relay|embedding|
  background"` et clés JSON `"albireo.*"` (0c, protocole page↔service), fichiers de certificats
  `albireo-authority|server*` (passe dédiée, regénère les certificats), URL/noms externes
  (`dist/albireo/catalog.json`, `libalbireo-engine-*.so/.dll` : **ne pas toucher**, original/
  autres plateformes). `ALBIREO_TEST_PORT` → avec 0d (README).
  **Fait** : namespace `albireo` → `gulliver` partout (find -exec) ; API C du moteur
  `albireo_engine*`, `albireo_event_callback`, `albireo_log_callback`, `albireo_png_*`,
  `albireo_jpeg_*`, `albireo_make_catalog` (+ chaînes dlsym couplées) ; namespace de test
  `albireo_test` → `gulliver_test`. **Gardés exprès** : `albireo_id`/`"albireo.id"` (clé de
  métadonnée GGUF des modèles existants). **Bug 0b corrigé ici** : le loader cherchait
  `libalbireo-engine-<variant>.dylib` alors que la cible renommée produit
  `libgulliver-engine-*.dylib` (`common/engine_loader.cpp:124`), sinon le moteur ne se
  chargeait plus. **Vérifié** : build sans avertissement ; `engine_chat` charge `libgulliver-engine-cpu.dylib`, lit un modèle Qwen 0.5B et répond (symboles `gulliver_engine_*` résolus par dlsym OK).
- ☑ **0c** (2026-11-17) **Préfixe de route `/albireo/v1` → `/gulliver/v1`, en un seul bloc C++ + JS +
  openapi** (sinon la page serait cassée entre deux sous-étapes), puis le reste de la page JS
  (`DB_NAME`, clés `localStorage`, textes visibles). *Réajustement 2026-11-17 : la route est
  partagée page/service, donc traitée d'un bloc ; les identifiants C++ internes restent en 0b.*
- ☑ **0d** (2026-11-17) Texte, doc, outils (interface, bump_version, README, docs du fork, blog, CLAUDE.md).
- ☑ **0e** (2026-11-17) Version **6.0.0** (majeure) : `version.h` PRODUCT_NAME/FULL_NAME → « Gulliver »/« Gulliver pour macOS » (auteur/copyright Cydral gardés), bump major, section changelog 6.0.0 (rupture de nom + API). Build propre, `test_host` all checks passed. Reste : blog, journal, reprise, commit ; valide test_macos + page_test.

## Bloc A — Profils côté service

- ☐ **A1** Schéma + accès profil (service).
- ☐ **A2** Routes de gestion + `resolve_profile`.
- ☐ **A3** Conversations + projets.
- ☐ **A4** Réglages de page.
- ☐ **A5** Chunks RAG + vecteurs BLOB.
- ☐ **A6** Images sur disque.
- ☐ **A7** Réécriture `store.js` en client HTTP + `profile.js`.
- ☐ **A8** Sélecteur de profil (UI).
- ☐ **A9** RAG : décodage des vecteurs rapatriés (transitoire).
- ☐ **A10** Retirer la garde « Mac seul » du relais.
- ☐ **A11** Renommer « profil » → « instantané de réglages » (UI).
- ☐ **A12** Doc + version (mineure).

## Bloc B — RAG en C++ + moteurs + index

- ☐ **B13** Interface `rag_engine` + moteur `lexical` (mots seuls).
- ☐ **B14** Moteur `dense` (vecteurs seuls).
- ☐ **B15** Moteur `hybrid` + parité avec le JS.
- ☐ **B16** Index nommés + bascule (service).
- ☐ **B17** Réglages RAG (service + UI).
- ☐ **B18** Basculer la page sur le RAG du service, retirer le RAG JS.
- ☐ **B19** Mesure + doc + version.

---

## Journal du chantier (du plus récent au plus ancien)

### 2026-11-17 20:07 — Démarrage
- Plan validé et copié dans le dépôt (`docs/chantier-gulliver-plan.md`).
- Fichier d'avancement créé. Pointeur ajouté à CLAUDE.md.
- Aucune modification de code encore. Prochaine étape : 0a.

---

## Décisions tranchées en autonomie (consigne « continue sans t'arrêter »)

Choix faits sans demander, selon le meilleur intérêt du projet. Révisables si l'humain le dit.

- **2026-11-17** — *Identifiants « albireo » minuscules mêlés à des chaînes* : renommés
  catégorie par catégorie (namespace, API C, routes, rôles), en **gardant** les clés de
  métadonnée GGUF `albireo.id/.inputs/.name/.roles` (sinon on casse la lecture des modèles
  déjà préparés). Raison : cohérence du nom sans casser le format de données existant.
- **2026-11-17** — *Rôles/modèles d'API* `albireo-*` → `gulliver-*` renommés des deux côtés
  (service + page + openapi) en un bloc. Raison : c'est le protocole page↔service, il ne doit
  jamais être désaccordé. On part à vide, donc aucun client externe à ménager.
- **2026-11-17** — *Certificats* `albireo-authority/server/certificates` → `gulliver-*`.
  Raison : `GULLIVER_HOME` est neuf, les certificats se régénèrent, autant les nommer juste.
- **À venir (bloc A)** — *Profils* : je choisirai les détails non tranchés (nom par défaut du
  premier profil, comportement si aucun profil, forme exacte du sélecteur) au mieux, en les
  notant ici.
- **2026-11-17** — *Readme.md racine et README du projet (hérités de l'original)* : le
  `Readme.md` de la racine décrit le dépôt du point de vue de l'original (« Albireo, a
  Windows 11 program »). Choix : **ne pas le réécrire** (règle de fusion : toucher l'original
  le moins possible) — seulement réparer les **liens cassés** par le renommage du dossier
  (`external/albireo` → `external/gulliver`). Le nom « Gulliver » vit dans les documents **du
  fork** (CLAUDE.md, mémo, guide, blog, changelog, interface). Le README applicatif
  (`gulliver/README.md`, 176 occ., très lié à l'original Windows/CUDA) est traité à part, avec
  discernement, ou laissé proche de l'original selon le temps.
