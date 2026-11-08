# Journal des modifications — fork Mac

Le plus récent en haut. Une entrée par intervention ; on n'en réécrit jamais une ancienne.

## 2026-11-08 — Dire, pour chaque erreur de l'auteur, si Windows est concerné

- **Quoi** : `docs/notes-pour-l-auteur.md` : une ligne « Mac ou Windows » par entrée et une colonne au résumé (aussi sous Windows : n° 1 `bench_runtime`, n° 3 espace de publication ; Linux et Mac : n° 2 ; propre au Mac : n° 4, 6 à 15, 18 ; avertissements du n° 5 détaillés) ; remarque 19 (seuil de 8 tokens trop bas sur Mac) ; changement 4 (Accelerate) dans la partie vitesse et bilan mis à jour. Consigne ajoutée à CLAUDE.md. Mémo (produits par Accelerate, correction 10), reprise et blog (épisode 13, « L'atelier d'à côté ») mis à jour.
- **Fichiers** : `docs/notes-pour-l-auteur.md`, `CLAUDE.md`, `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, `blog/13-l-atelier-d-a-cote.md`, `blog/README.md`, ce journal.
- **Pourquoi** : l'humain fera des PR vers l'original pour les erreurs qui se reproduisent sous Windows.
- **Étapes** : classement déduit du code de `upstream/main` (pas de machine Windows) : `bench_runtime` construit sous Windows par le CMake, Pseudalis sans script de test Windows, `run_console` à `sigwait` dans la branche hors Windows (ligne 306), `handle_` lu seulement par le chargement de DLL ; aucun chemin privé ni adresse dans les documents.

## 2026-11-08 — Passer les longs produits d'Albireo par Accelerate

- **Quoi** : sur Mac, `matmul_float` (prompts, modèle de recherche, images) appelle `cblas_sgemm` d'Accelerate, qui calcule sur le coprocesseur matriciel de la puce, à partir de 16 tokens ; les poids sont décompressés par tranches de 256 lignes par les fils du pool pendant qu'Accelerate multiplie la tranche précédente. Sur Mac, `qk_float_product_tokens` passe de 8 à 24 : en dessous, les noyaux entiers sont plus rapides. **Fin de l'exactitude au bit près** pour ces produits (décision de l'humain, option C) : les sommes d'Accelerate diffèrent au dernier chiffre.
- **Fichiers** : `albireo/engine/qkernels_accelerate.inc` (neuf), `albireo/engine/qkernels.cpp` (définition, inclusion, aiguillage dans `matmul_float`), `albireo/engine/qkernels.h` (seuil sous `__APPLE__`), `albireo/CMakeLists.txt` (`link_libraries("-framework Accelerate")` dans la branche ARM64, sous `if (APPLE)`), ce journal.
- **Pourquoi** : demande de l'humain d'accélérer encore Albireo (Q8_0 du modèle de recherche) ; le profil montrait le produit en flottants à 80 % et la décompression Q8_0 à 2 ou 3 %.
- **Étapes** : banc d'essai hors dépôt de `cblas_sgemm` (un appel 512×2048×6144 : 1 060 Gflop/s ; 8 fils à petits appels : 315 ; tranches de 256 lignes : 880) ; micro-banc `qk_matmul` (6 144×2 048, Q4_K et Q8_0, 8 à 128 tokens) contre l'ancien moteur : seuils 16 et 24 choisis, tranche de 256 lignes meilleure que 64, 128 et 512 ; build sans avertissement ; `test_qkernels` (tolérance contre une référence en double), `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent ; mesures alternées `bench_runtime`, 8 fils, `lowpowermode 0`, charge 2 à 14 : 2B 64 tokens 60,4 / 62,8 → 114,1 / 133,7 tokens/s, 512 tokens 64,2 / 71,7 → 216,8 / 217,5, 4 096 tokens 51,6 / 57,5 → 155,9 / 162,8, modèle de recherche 512 tokens 183,7 / 212,5 → 572,8 / 600,9 ; génération inchangée ; `engine_chat` sur 4 329 tokens : prompt 51,7 → 134,9 tokens/s, réponse formulée autrement mais de même sens ; question de 21 tokens : « Paris », prompt 45,6 → 58,4 tokens/s ; service relancé avec le nouveau moteur.

## 2026-11-08 — Écrire les notes pour l'auteur de l'original

- **Quoi** : `docs/notes-pour-l-auteur.md`, en français, à transmettre à l'auteur : 4 bugs ou fragilités (lecture hors tableau de `bench_runtime` sous 64 tokens, attentes fixes du test de Pseudalis, espace de publication « password protected », SIGINT ignoré en `--console` lancé en arrière-plan sur macOS), 4 avertissements (Apple clang et `ld -s`), 10 points de portabilité macOS avec erreurs copiées et corrections du fork, 3 remarques (arrondis du code portable, `yield`, mode économie), et la partie vitesse : temps d'origine, les trois changements NEON, nouveaux temps. Consignes ajoutées à CLAUDE.md pour le tenir à jour.
- **Fichiers** : `docs/notes-pour-l-auteur.md`, `CLAUDE.md`, `logs/reprise.md`, ce journal.
- **Pourquoi** : demande de l'humain, qui veut transmettre à l'auteur les erreurs trouvées et les gains, et le garder à jour.
- **Étapes** : numéros de ligne relevés dans `upstream/main` (`8743fcd`) ; avertissements retrouvés dans l'historique de la session et dans `engine_loader.h:62` (vérification syntaxique) ; historique du journal relu (ajout du piège de `picopltf.h` : `0 == 0` choisit gros-boutiste) ; une première version en anglais remplacée par la version française à la demande de l'humain ; aucun chemin privé ni adresse dans le document.

## 2026-11-08 — Faire passer Pseudalis à 28 sur 28

- **Quoi** : `executable_path()` de Pseudalis par `_NSGetExecutablePath` sous `#if defined(__APPLE__)`, comme pour Albireo. `test_linux.sh` : 28 étapes sur 28.
- **Fichiers** : `dlib/dlib/external/pseudalis/common/platform.cpp` ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/12-le-chronometre-du-test.md`, `blog/README.md`.
- **Pourquoi** : même lecture de `/proc/self/exe` qu'Albireo ; et reprise du chantier des deux étapes qui échouaient.
- **Étapes** : correction, build sans erreur ; `test_linux.sh` (avec `nproc` et `gstat` par le `PATH`) : 28/28. Contre-épreuve sans la correction : 28/28 aussi, donc elle n'explique pas les deux étapes ; elles attendent un délai fixe (`sleep 8`, 6, 4) et le premier passage (26/28, 2026-11-06) avait très probablement été fait en mode économie d'énergie (découvert juste après, lors des mesures d'Albireo). Seconde contre-épreuve, service lancé depuis la racine du dépôt : page 404 (« no web folder at web ») sans la correction, 200 avec.

## 2026-11-08 — Relever le second essai du PDF

- **Quoi** : suivi de `/albireo/v1/status` toutes les 5 s pendant que l'humain refait le résumé du PDF de l'ANSSI : rangement par le modèle de recherche (~2 min), lecture d'environ 1 800 tokens par le 4B, 578 tokens écrits à 12 à 13 tokens/s.
- **Fichiers** : `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal.
- **Pourquoi** : voir l'effet de l'attention NEON sur un vrai document ; le contexte étant bien plus court qu'au premier essai, la comparaison ne vaut pas, celle de l'épisode 11 (à texte égal) reste la référence.
- **Étapes** : service relancé détaché de la session (le précédent s'était arrêté avec elle) ; relevés dans `build/att/pdf-suivi.txt`.

## 2026-11-07 — Accélérer l'attention d'Albireo avec NEON

- **Quoi** : `attention_scores_neon` et `attention_values_neon`, appelées en tête de `qk_attention_scores` et `qk_attention_values` sous `ALBIREO_QK_NEON`. Identiques au bit près au code portable. Sur 4 096 tokens : prompt et génération environ deux fois plus rapides.
- **Fichiers** : `dlib/dlib/external/albireo/engine/qkernels_neon.inc`, `dlib/dlib/external/albireo/engine/qkernels.cpp` (deux fois quatre lignes) ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/11-deux-facons-d-arrondir.md`, `blog/README.md`.
- **Pourquoi** : sur le résumé d'un PDF, l'attention prenait les deux tiers du calcul et l'écriture tombait à 3,4 tokens/s.
- **Étapes** : contrôle `attcheck` (NEON contre portable compilé avec `-U__ARM_FEATURE_DOTPROD`, mêmes options que le moteur) : première version tout en produit puis addition, scores justes pour hd 64 à 128 mais 28 907 valeurs sur 49 152 différentes ; lecture de l'assembleur du code portable : Apple clang vectorise la boucle des scores (produit puis addition, `fmadd` sur le reste) et laisse scalaire celle des valeurs (`fmadd` partout) ; règle reproduite : 0 différence sur 549 760. `test_qkernels`, `bitcheck` (0/12 000), `gemmcheck` (0/32 436) OK. Mesures alternées (`bench_runtime` 8 fils, 4 096 tokens ; une première série coupée par la fin de session, refaite entière) : prompt 25,5 / 24,5 → 52,8 / 50,0 tokens/s, génération 9,0 / 7,5 → 19,6 / 22,0 tokens/s. `engine_chat` sur 4 329 tokens (épisodes 8 et 9 du blog) : même réponse, prompt 23,1 → 47,0, génération 7,9 → 16,9 tokens/s.

## 2026-11-07 — Noter l'essai de la page web et le coût de l'attention

- **Quoi** : l'humain a essayé la page d'Albireo dans son navigateur (conversation, résumé d'un PDF de 4,6 Mo « consulté par recherche », juste). Sur ce long contexte, l'écriture tombe à 3,4 tokens/s ; profil : l'attention (`qk_attention_values` 3 007, `qk_attention_scores` 2 807 échantillons) pèse deux fois `qk_dot` (2 737).
- **Fichiers** : `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal.
- **Pourquoi** : l'attention passe en tête des choses à accélérer.
- **Étapes** : état du moteur par `/albireo/v1/status` (28 556 tokens de prompt au total, 3,4 tokens/s en génération) ; `sample` de 3 s du service pendant l'écriture. Pas de changement de code.

## 2026-11-07 — Lancer le service d'Albireo sur le Mac

- **Quoi** : le service `albireo-service --console` trouve son moteur et sa page web, télécharge ses modèles par défaut et répond aux questions, en une fois et en flux. Deux corrections sous `#if defined(__APPLE__)` : chemin du programme par `_NSGetExecutablePath` au lieu de `/proc/self/exe` ; mémoire (`hw.memsize`, `host_statistics64`) et nom du processeur (`machdep.cpu.brand_string`) par `sysctl` au lieu de `/proc/meminfo`.
- **Fichiers** : `dlib/dlib/external/albireo/common/platform.cpp`, `dlib/dlib/external/albireo/common/system_info.cpp` ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/10-le-programme-qui-ne-savait-pas-ou-il-habitait.md`, `blog/README.md`.
- **Pourquoi** : étape suivante de la reprise ; au premier lancement, le journal disait « programs in  » (vide), « no usable engine in », « no web folder at web », et la mémoire valait 0.
- **Étapes** : premier lancement (données dans `build/albireo-home`, port 18443, `check_updates: false`) : modèles téléchargés (Qwen3-VL 4B Thinking 2,3 Go + projecteur 433 Mo, Qwen3 Embedding 0.6B 610 Mo) mais aucun moteur ; correction, rebuild sans erreur ; relance : « Apple M2 », 24 Go, moteur `NEON+dotprod` ; page `/` et ses fichiers en 200 ; `/v1/chat/completions` : « Paris est la capitale de la France. » (3,7 s, 14,0 tokens/s), flux de 604 messages (600 tokens de raisonnement) ; `test_host`, `test_model_file`, `test_engine_api`, `test_qkernels`, `test_chat_format` passent. Constaté : lancé en arrière-plan par un shell non interactif, le service ignore SIGINT (macOS jette le signal ignoré même bloqué) ; `kill -TERM` l'arrête en 1 s. Pseudalis a la même lecture de `/proc/self/exe`, pas encore corrigée. Page pas essayée dans un navigateur.

## 2026-11-07 — Fusionner le dernier commit de l'original

- **Quoi** : `git merge upstream/main`, 1 commit (`8743fcd`, résultats du compresseur de tokens sur enwik9 : archive de 117 169 624 octets, 0,937 bit par octet). Fusion sans conflit.
- **Fichiers** : `Readme.md`, `examples/Readme.md` (venus de l'original) ; `logs/reprise.md`, ce journal.
- **Pourquoi** : demande de l'humain (« fusion »).
- **Étapes** : `git fetch upstream`, `git merge --no-edit upstream/main` ; le commit ne change que deux documents, donc ni build ni test à refaire ; pas d'épisode de blog (fusion qui ne change rien au portage).

## 2026-11-07 — Accélérer la lecture du prompt d'Albireo avec un produit NEON en floats

- **Quoi** : `gemm_neon<4>` et `gemm_neon<1>` (4 tokens, ou 1, contre 4 lignes de poids) pour `gemm_rows`, le produit en floats des prompts d'au moins 8 tokens. Identique au bit près à `qk_dot_f32`. Prompt multiplié par 3,5 à 4 (8 fils : 35 à 50 tokens/s contre 5 à 13).
- **Fichiers** : `dlib/dlib/external/albireo/engine/qkernels_neon.inc`, `dlib/dlib/external/albireo/engine/qkernels.cpp` (une branche `#elif ALBIREO_QK_NEON` dans `gemm_rows`) ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/09-le-dernier-chiffre.md`, `blog/README.md`.
- **Pourquoi** : après l'épisode 8, la lecture des questions était devenue l'étape lente.
- **Étapes** : moteur précédent mis de côté (`build/baseline-gen/`) ; premier noyau avec `vfmaq_f32` : 23 953 sommes sur 32 436 différaient au dernier bit de `qk_dot_f32` ; essai avec `vmulq_f32` puis `vaddq_f32` : 0 différence, gardé ; `test_qkernels` passé ; `bitcheck` des noyaux entiers toujours à 0 ; mesures alternées avant / après (`bench_runtime` à 64 et 256 tokens, `engine_chat`), `lowpowermode 0`, charge 6 à 23 ; réponse identique à celle du moteur d'origine.

## 2026-11-07 — Donner au moteur d'Albireo des noyaux NEON pour Q4_K et Q6_K

- **Quoi** : noyaux NEON de `qk_dot` pour les deux formats du modèle (Q4_K, Q6_K), avec l'instruction de produit scalaire `vdotq_s32`. Mêmes sommes entières que les références, même fin de bloc : résultat identique au bit près. Génération multipliée par 3 environ (8 fils : 33 à 37 tokens/s contre 11).
- **Fichiers** : `dlib/dlib/external/albireo/engine/qkernels_neon.inc` (nouveau, propre au fork), `dlib/dlib/external/albireo/engine/qkernels.cpp` (quatre ajouts sous `ALBIREO_QK_NEON`) ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/08-seize-a-la-fois.md`, `blog/README.md`.
- **Pourquoi** : la génération passait presque tout son temps dans `qk_dot_portable`, qui calcule un nombre à la fois.
- **Étapes** : formats du modèle relevés dans le GGUF (Q4_K, Q6_K, F32) ; `__ARM_FEATURE_DOTPROD` vérifié dans clang ; moteur et banc d'origine mis de côté (`build/baseline/`) ; build ; `test_qkernels` passé (erreur relative 9e-9) ; programme de contrôle hors dépôt : 0 différence sur 12 000 produits contre `qk_dot_portable` ; mesures alternées origine / NEON avec `engine_chat` et `bench_runtime`, `lowpowermode 0`, charge 5 à 7 ; réponse identique octet pour octet.

## 2026-11-06 — Trouver pourquoi le moteur d'Albireo n'occupait qu'un cœur

- **Quoi** : la cause était le mode économie d'énergie du Mac (`pmset -g` : `lowpowermode 1`, sur secteur). Une fois coupé par l'humain, `engine_chat` (moteur en bibliothèque) passe de 0,8 à 7,9 tokens/s à 8 fils, presque comme `bench_runtime` (8,3). Une piste de code a été essayée puis abandonnée : `isb` au lieu de `std::this_thread::yield()` dans `ALBIREO_CPU_RELAX()` sur ARM64 (`engine/thread_pool.h`). Elle remettait les fils d'aide à la priorité 31 (ils étaient à 0 d'après `ps -M`), mais ne gagnait rien.
- **Fichiers** : `CLAUDE.md` (règle de mesure), `docs/memo-portage-macos-arm64.md` (mesures de référence refaites), `logs/reprise.md`, ce journal, `blog/07-le-mode-economie.md`, `blog/README.md`. Aucun code modifié au final.
- **Pourquoi** : priorité de l'humain : la vitesse d'Albireo.
- **Étapes** : `ps -M` (priorités et part de processeur par fil), `sample` ; correction `isb`, recompilation, `test_qkernels` passé, aucun gain ; découverte de `lowpowermode 1` et d'une charge moyenne de 16 ; mode coupé par l'humain ; moteur sans la correction construit à part (`build/macos-albireo-nofix`, supprimé ensuite), mesures alternées avec et sans (5,2 et 5,5 tokens/s avec, 5,7 et 6,4 sans) ; correction retirée (`git checkout`), moteur identique octet pour octet à l'original ; mesures de référence refaites à 1, 4 et 8 fils. Erreur de Claude : les mesures de l'épisode 6 ont été prises sans vérifier le mode d'énergie.

## 2026-11-06 — Faire répondre Albireo sur le Mac et mesurer sa vitesse

- **Quoi** : premier modèle pour Albireo, Qwen3-VL 2B Instruct (Q4_K_M), téléchargé depuis l'adresse du catalogue et vérifié (taille et SHA-256). Le moteur répond juste. Mesures : 0,78 à 0,85 token/s en génération avec le moteur en bibliothèque (`engine_chat`, un seul cœur occupé) ; 1,9 token/s avec `bench_runtime` à 4 fils, 1,8 à 8 fils, 0,5 à 1 fil. Profils : calcul scalaire (`matmul_float`, `qk_dot_portable`), et des fils du moteur en bibliothèque qui attendent dans `swtch_pri`.
- **Fichiers** : `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/06-albireo-parle-lentement.md`, `blog/README.md`. Aucun code modifié. Le modèle reste dans `build/models/` (ignoré par git).
- **Pourquoi** : priorité de l'humain : Albireo avant Pseudalis ; d'abord savoir où l'on en est.
- **Étapes** : `engine_chat` à 8 puis 4 fils ; `bench_runtime` à 1, 4 et 8 fils ; `sample` sur les deux. Erreur de Claude corrigée : un premier `bench_runtime` avec un prompt de 32 tokens a planté (`token identifier out of range`) parce que le banc lit toujours 64 tokens ; relancé avec 64.

## 2026-11-06 — Faire compiler Albireo et Pseudalis sur ARM64

- **Quoi** : correction des 5 causes d'échec : moteurs `avx2` non construits sur ARM64 (tests d'Albireo en noyaux portables), `<machine/endian.h>` pour `svox_pico` sur macOS, sources NEON de libpng pour `albireo_image` sur ARM64, `CoreFoundation` et `Security` liés à `dlib_httplib` sur macOS, `st_mtim` → `st_mtimespec` sur macOS. Albireo et Pseudalis compilent en entier.
- **Fichiers** : `dlib/dlib/CMakeLists.txt`, `dlib/dlib/external/albireo/CMakeLists.txt`, `dlib/dlib/external/pseudalis/CMakeLists.txt`, `dlib/dlib/external/pseudalis/host/drive_folders.cpp`, `dlib/dlib/external/svox_pico/lib/picopltf.h` ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/05-cinq-corrections.md`, `blog/README.md`.
- **Pourquoi** : porter les deux applications sur Mac, avec des changements limités au Mac ou à ARM64 pour ne pas gêner les fusions avec l'original.
- **Étapes** : recompilation d'abord sans rien changer (mêmes 5 causes après la fusion) ; corrections ; vérifié que `PICO_ENDIANNESS` vaut bien `ENDIANNESS_LITTLE` sur macOS (le premier essai de la correction de `endian.h` aurait donné « big endian » : macOS ne définit pas `__BYTE_ORDER`) ; recompilation : 0 erreur ; tests unitaires des deux applications passés ; `test_linux.sh` de Pseudalis : 26 étapes sur 28 (`nproc` et `stat` de GNU fournis par le `PATH`). Exemples dlib recompilés après la modification de `dlib/dlib/CMakeLists.txt`.

## 2026-11-06 — Reprendre les trois épisodes du carnet TortueTech en tête du blog

- **Quoi** : les épisodes 1 à 3 du carnet TortueTech (`/carnet/dlib-transformer-extensions-macos`, `-compilation`, `-albireo-pseudalis`) deviennent les pages 01 à 03 du blog ; ils remplacent les pages 00 et 01 écrites par Claude, qui racontaient la même chose. Épisode 4 « Le fork déménage » écrit dans le même ton. Règles du blog de CLAUDE.md réécrites d'après ces épisodes (épisodes, ton au « on », images, « Pour les curieux », « La suite »).
- **Fichiers** : `blog/01-le-fork-et-l-original.md`, `blog/02-compiler-dlib-et-les-exemples.md`, `blog/03-albireo-pseudalis-et-cinq-obstacles.md`, `blog/04-le-fork-demenage.md`, `blog/README.md`, `CLAUDE.md` ; supprimés : `blog/00-prologue.md`, `blog/01-premiere-compilation.md`.
- **Pourquoi** : demande de l'humain : partir des épisodes déjà publiés et continuer dans leur ton.
- **Étapes** : pages lues sur le serveur local du carnet ; texte repris tel quel, sans le menu, le pied de page ni les liens du glossaire du site, liens entre épisodes changés en liens vers les fichiers du blog. Vérifié : titres, sommaire, ni e-mail ni chemin privé.

## 2026-11-06 — Déménager le fork sur le compte Aughra

- **Quoi** : nouveau fork de `Cydral/Dlib-Transformer-extensions` sur le compte `Aughra` (public, seule possibilité pour un fork d'un dépôt public) ; `origin` pointe désormais sur `Aughra/Dlib-Transformer-extensions` ; tout `main` y est poussé (`f0f9f91..50c2c96`), commit `pour mac` compris.
- **Fichiers** : `CLAUDE.md` (table des dépôts distants), `logs/reprise.md`, ce journal.
- **Pourquoi** : le compte connecté sur le Mac (Aughra) n'avait pas le droit d'écrire sur le fork de PapyTortueTech (403). L'humain arrête ce fork-là.
- **Étapes** : `gh repo fork --clone=false --default-branch-only`, `git remote set-url origin`, `git push -u origin main`. Fork plutôt que dépôt privé : choix de l'humain (bouton « Sync fork », envoi léger).

## 2026-11-06 — Fusionner les 10 nouveaux commits de l'original

- **Quoi** : fusion de `upstream/main` dans `main` (commit `037a32e`, 78 fichiers, sans conflit) : Albireo 1.0.9 à 1.0.11, Pseudalis 1.0.3, modèles Qwen3-VL-Thinking, GGUF sur CPU.
- **Fichiers** : ceux de l'original ; côté fork, `logs/reprise.md` et ce journal.
- **Pourquoi** : rester proche de l'original, à la demande de l'humain.
- **Étapes** : `git fetch --all`, `git merge upstream/main` ; vérifié que la fusion touche `albireo/CMakeLists.txt` et `pseudalis/CMakeLists.txt`, mais pas les lignes des erreurs `st_mtim` et `endian.h`. Pas recompilé. Push tenté.

## 2026-11-06 — Poser les règles du fork et les documents de suivi

- **Quoi** : création de CLAUDE.md (règles du fork : ton doux et concis, but Mac seul, deux remotes, règle de fusion facile, reprise, journal, la documentation suit le code, blog en français, Git), de `logs/reprise.md` et de ce journal ; ajout au mémo de portage des commandes de build d'Albireo et de Pseudalis et d'une section « État constaté » ; blog ouvert avec ses deux premières pages (00 « Deux cahiers », 01 « Première compilation »).
- **Fichiers** : `CLAUDE.md`, `logs/reprise.md`, `logs/journal-modifications.md`, `docs/memo-portage-macos-arm64.md`, `blog/README.md`, `blog/00-prologue.md`, `blog/01-premiere-compilation.md`.
- **Pourquoi** : reprendre le portage d'une séance à l'autre sans rien perdre, sur le modèle du CLAUDE.md du projet miaou ; garder un mémo qui dit ce qui a vraiment été vérifié ; raconter le portage étape par étape.
- **Étapes** : rédaction seule, aucun build. Blog d'abord écrit en deux langues, puis ramené au français seul à la demande de l'humain.

## 2026-11-05 — Fusionner l'original et compiler pour la première fois sur Mac

- **Quoi** : remote `upstream` (Cydral) ajouté ; fusion de 4 commits de l'original (commit `0abb591`, sans conflit, pas poussé : 403). Premier build sur Mac M2 : dlib + exemples compilés sans erreur ; Albireo et Pseudalis configurés, compilation en échec sur 5 causes (moteurs avx2, `endian.h`, libpng NEON, frameworks Apple non liés, `st_mtim`).
- **Fichiers** : aucun fichier source modifié ; builds dans `build/` (ignoré par git).
- **Pourquoi** : suivre l'original et mesurer ce qui reste à porter.
- **Étapes** : `git merge upstream/main` ; `cmake` + `ninja -k 0` pour `dlib/examples`, `albireo` et `pseudalis` ; résumé des erreurs envoyé à la session « Mode mock sans API ».
