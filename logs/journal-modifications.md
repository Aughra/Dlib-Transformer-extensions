# Journal des modifications — fork Mac

Le plus récent en haut. Une entrée par intervention ; on n'en réécrit jamais une ancienne.

## 2026-11-11 — Retirer de Pseudalis le code de Windows, de Linux et des x86 (nettoyage, N1 à N3)

- **Quoi** : fichiers supprimés (`windows/`, `build_windows.ps1`, le contrôle des imports de bibliothèques Windows `check_imports.cpp` et `check_imports.cmake`, scripts CMake de MinGW et de la charge utile, ressources `.rc`, icônes, images et licence de l'installateur, `make_feed.py`, `publish_release.py`) ; `CMakeLists.txt` réécrit pour macOS sur Apple Silicon (255 → 161 lignes) : le moteur n'existe plus qu'à l'intérieur du service et de `test_engine` (la bibliothèque `libpseudalis-engine.dylib`, inutile sur Mac, n'est plus construite) ; `unifdef` sur 19 fichiers (2 831 lignes de moins).
- **Fichiers** : `dlib/dlib/external/pseudalis/` ; ce journal.
- **Pourquoi** : décision « du pur Apple ».
- **Étapes** : reconfiguration à neuf de `build/macos-pseudalis` ; build sans erreur, les 3 avertissements connus ; `test_engine`, `test_host` passent ; `test_linux.sh` (avec `nproc` et `gstat` fournis par le `PATH`) : 34 étapes sur 34. Restent des traces hors des blocs conditionnels (chargeur de moteur, commentaires), à retirer ensuite.

## 2026-11-11 — Retirer d'Albireo le code de Windows, de Linux, des x86 et de CUDA (nettoyage, N1 à N3)

- **Quoi** : décision de l'humain, « du pur Apple », périmètre Albireo et Pseudalis (dlib reste tel quel, on s'en éloignera plus tard). Pour Albireo : fichiers supprimés (`dist/`, `windows/`, scripts `.ps1`, `prerequisites_linux.sh`, outils de publication des installateurs `make_feed.py`, `publish_release.py`, `make_offline_bundle.py`, ressources `.rc`, icônes `.ico` et images du tray et de l'installateur, `tensor_backend` CUDA/OpenVINO avec `test_device_ops` et `test_offload`, scripts CMake de CUDA, MinGW, de la charge utile et des bibliothèques de périphériques) ; `CMakeLists.txt` réécrit pour macOS sur Apple Silicon seulement (864 → 307 lignes, refus des autres plateformes, un seul moteur `cpu`, une seule famille de noyaux) ; `unifdef` sur 34 fichiers : branches `_WIN32`, x86 (AVX2, VNNI), CUDA, OpenVINO et Linux retirées, branches `__APPLE__` gardées seules (4 854 lignes de moins). Les sources SVG des dessins et `make_images.py` restent.
- **Fichiers** : `dist/`, `dlib/dlib/external/albireo/` (CMakeLists.txt, common, engine, host, tests, tools, resources, cmake) ; `CLAUDE.md` (« Du pur Apple ») ; ce journal.
- **Pourquoi** : le fork est détaché de l'original et ne vise que le Mac ; le moteur Metal à venir sera plus simple sans les autres chemins.
- **Étapes** : reconfiguration à neuf de `build/macos-albireo` ; build sans erreur ni avertissement ; `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent ; `engine_chat` (2B) : « La capitale de la France est Paris. » ; service `--console` : moteur `cpu` chargé, page en 200, `/v1/chat/completions` répond « Paris ». Restent des traces hors des blocs conditionnels (champs AVX de `cpu_info`, choix des moteurs x86 dans l'hôte, commentaires), à retirer ensuite ; Pseudalis à faire.

## 2026-11-11 — Ranger les clés et valeurs dans une seule zone réservée (moteur Metal, M1)

- **Quoi** : `kv_pool` réserve d'un coup, par `mmap`, les adresses de tous ses blocs (une zone alignée sur les pages, chaque bloc commençant sur une page) ; macOS ne donne la mémoire qu'au premier usage. `trim()` rend les pages des blocs libres par `madvise(MADV_FREE_REUSABLE)`, `MADV_FREE_REUSE` les reprend. Accès pour le GPU : `region()`, `region_bytes()`, `block_offset()`, `slab_offset()`, `scales_offset()`. Code pour macOS seulement (demande de l'humain : du pur Apple).
- **Fichiers** : `dlib/dlib/external/albireo/engine/kv_cache.h` ; ce journal.
- **Pourquoi** : le GPU du Mac partage la mémoire du processeur ; une zone unique peut devenir un seul tampon Metal sans copie, que le processeur et le GPU liront et écriront tous les deux.
- **Étapes** : build sans avertissement ; `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent ; `engine_chat` (2B) : « La capitale de la France est Paris. » ; `bench_runtime` 2B 512 tokens q8 : prompt 216,8 tokens/s, génération 36,6 (une mesure, pas de comparaison alternée : changement d'allocation seulement).

## 2026-11-11 — Se détacher de l'original

- **Quoi** : décision de l'humain : le fork ne fusionne plus `upstream/main` (dernière fusion `535a6fb`, original à `184ea00`, Albireo 1.0.12). Les fichiers de l'original peuvent être modifiés librement ; `upstream` reste en lecture, pour reprendre une idée à la main. Inchangé : push vers `origin` seulement, pas de PR vers l'original sans demande, la doc et le blog suivent le code, les notes pour l'auteur continuent pour les vrais bugs. Objectif : moteur Metal complet.
- **Fichiers** : `CLAUDE.md` (section « Détaché de l'original », Git, méthode), `logs/reprise.md` (plan du moteur Metal), ce journal.
- **Pourquoi** : 16 commits de l'original en deux jours ; le moteur Metal demande des changements profonds du runtime et du cache des clés et valeurs.
- **Étapes** : interprétation confirmée par l'humain ; aucune modification de code.

## 2026-11-11 — Sonder le GPU du Mac avant un moteur Metal

- **Quoi** : sonde hors moteur, propre au fork : noyaux Metal Q4_K et Q6_K (produit matrice-vecteur, poids lus en place sans copie), mesures d'exactitude, de débit, de plafond, d'aller-retour, et du processeur sur les mêmes produits.
- **Fichiers** : `macos/metal-sonde/kernels.metal`, `macos/metal-sonde/probe.mm`, `macos/metal-sonde/README.md` (nouveaux) ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/17-la-sonde.md`, `blog/README.md`.
- **Pourquoi** : étape décidée par l'humain (moteur Metal) ; vérifier d'abord le gain possible, estimé « écriture doublée » le 2026-11-09 sans mesure.
- **Étapes** : `git fetch upstream` : 16 commits de l'original non fusionnés. Lecture de `tensor_backend.h` et `offload.h` dans `upstream/main` : moteur GPU sur les tenseurs de dlib (CUDA, OpenVINO), interfaces `matmul_offload` et `layer_pass`. Pas de compilateur Metal hors ligne : noyaux compilés au lancement. Charge de 20 au départ (mises à jour de macOS, Spotlight, antivirus) : mesures faites après retour à 3 à 4. Noyaux : `mv_q4_K` (Q4_K 38,6 Go/s), `mv2_q4_K` (lectures de 4 octets, 26,9 Go/s, rejeté), `mv3_q4_K` (16 octets alignés par thread, 52,9 Go/s) ; `mv_q6_K` 57 à 60 Go/s, `mv2_q6_K` 56 Go/s ; plus de lignes par simdgroup : plus lent. Résultat (4B) : GPU 45 à 48 ms par token de poids contre 50,9 (4 threads) à 55,2 ms (8 threads) pour le processeur ; plafond 35 ms (70 Go/s) ; aller-retour 186 à 188 µs ; une commande par couche 57 à 60 ms, par produit 120 ms ; écart du GPU 2,8e-8, du processeur 8,8e-4. 2B : GPU 19,7 ms, processeur 26,2. Commande du README vérifiée depuis la racine.

## 2026-11-09 — Écrire sur les cœurs de performance du Mac, lire sur tous les cœurs

- **Quoi** : sur Mac, le thread pool garde une petite équipe d'un thread par cœur de performance (`hw.perflevel0.physicalcpu`, 4 sur le M2) ; `cpu_runtime::forward` la demande (`fast_team_scope`) pour les passes de moins de `qk_float_product_tokens` tokens (24) : génération et questions très courtes. Les longs prompts restent sur les 8 threads. `active()` (nombre de threads de la section) remplace `size()` dans `qk_matmul` et le découpage de l'attention. Option B choisie par l'humain.
- **Fichiers** : `dlib/dlib/external/albireo/engine/thread_pool_apple.inc` (nouveau, propre au fork), `dlib/dlib/external/albireo/engine/thread_pool.h` (inclusion et trois branchements sous `__APPLE__`, `active()` hors Mac), `dlib/dlib/external/albireo/engine/qkernels.cpp` (une ligne), `dlib/dlib/external/albireo/engine/cpu_runtime.cpp` (deux lignes, et la demande sous `__APPLE__`) ; `docs/notes-pour-l-auteur.md` (changement 8, remarque 20, bilan), `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/16-deux-equipes.md`, `blog/README.md`.
- **Pourquoi** : remarque 20 : 4 threads écrivent plus vite que 8, mais 8 lisent plus vite les longs prompts.
- **Étapes** : build sans avertissement ; `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent ; `engine_chat` répond juste (2 questions). Mesures alternées Q (8 threads) / T (avec la petite équipe), `bench_runtime ... q8`, charge 2,7 à 8,4 : 4B génération, 4 tours, 17,6–18,1 → 18,2–18,6 tokens/s (4 threads seuls : 17,8–18,5) ; 2B génération 37,9 / 37,4 → 39,7 / 40,8 ; 2B prompt de 4 096 tokens 173,1 / 163,2 → 167,0 / 161,8 (inchangé), génération qui suit 22,9 / 22,4 → 24,1 / 22,4. `engine_chat` : question de 22 tokens lue à 88 tokens/s, de 25 tokens à 61 (une mesure chacune).

## 2026-11-09 — Accélérer l'attention d'Albireo pour les clés et valeurs en 8 bits

- **Quoi** : `attention_scores_neon` et `attention_values_neon` deviennent des modèles qui reçoivent une source de lignes (`half_rows` : demi-précision ; `q8_rows` : entiers de 8 bits et échelle par groupe de 32, dépliés huit à la fois) ; `attention_scores_q8_neon` et `attention_values_q8_neon` branchées en tête de `qk_attention_scores_q8` et `qk_attention_values_q8`. Identique au bit près au code portable. Remarque 20 (nombre de fils sur un processeur hybride) et changement 7 ajoutés aux notes pour l'auteur ; numéros de ligne recalés sur `184ea00`.
- **Fichiers** : `dlib/dlib/external/albireo/engine/qkernels_neon.inc`, `dlib/dlib/external/albireo/engine/qkernels.cpp` (deux fois quatre lignes sous `ALBIREO_QK_NEON`) ; `docs/notes-pour-l-auteur.md`, `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/15-le-classeur-en-noir-et-blanc.md`, `blog/README.md`.
- **Pourquoi** : depuis Albireo 1.0.12, le service range les clés et valeurs en 8 bits par défaut ; sur Mac, ce format passait par le code portable, et un prompt de 4 096 tokens retombait de 163 à 37 tokens/s.
- **Étapes** : lecture de `engine.cpp` (`kv_type` « auto » : 8 bits pour les modèles de langue) ; contrôle hors dépôt `attcheck` (build NEON contre build `-U__ARM_FEATURE_DOTPROD`, 224 cas, `hd` 64 et 128, 1 à 1 100 clés, 1 à 16 lignes, demi-précision et 8 bits) : 0 différence sur 351 780 valeurs ; build sans avertissement ; `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api` passent ; `bitcheck` toujours à 0 sur 43 200 ; `engine_chat` (2B, « keys and values in 8 bits ») : « La capitale de la France est Paris. ». Mesures alternées M (fusion seule) / Q (`bench_runtime ... q8`, 8 fils, charge 2,7 à 9,9) : 2B 4 096 tokens, prompt 37,7 / 36,0 → 162,3 / 157,7 tokens/s, génération 9,1 / 8,9 → 22,6 / 22,5 ; 4B 64 tokens, prompt 48,2 / 47,2 → 52,2 / 50,5, génération 16,4 / 16,6 → 17,8 / 17,8. Demi-précision au même moment : 165,6 et 21,9. Profil de la génération après 4 096 tokens : attente des fils 8 691 échantillons, `qk_dot` 5 891, attention 3 808. Fils : 4 contre 8 remesurés en 8 bits, même conclusion qu'avant la fusion (4 pour le court, 8 pour le long) ; pas changé, choix laissé à l'humain.

## 2026-11-09 — Fusionner l'original : Albireo 1.0.12 et Pseudalis 1.0.5

- **Quoi** : 6 commits de Cydral fusionnés (`6e24fac`, `12f383f`, `76188fb`, `3cf725e`, `fccf819`, `184ea00`) dans `535a6fb` : Albireo 1.0.12 (clés et valeurs en 8 bits, budget de mémoire engageable, attention de la génération répartie entre les fils, recherche et compression, page web), réglage des balises de Pseudalis, moyenne des poids des exemples HRM (`parameter_average`), flux de mises à jour.
- **Fichiers** : ceux de l'original ; conflit dans `dlib/dlib/external/albireo/engine/qkernels.cpp`.
- **Pourquoi** : demande de l'humain (« refresh du fork »).
- **Étapes** : conflit dans `qk_attention_scores` et `qk_attention_values` : l'original a déplacé leur corps dans un modèle commun (`attention_scores(..., expand)`) ; résolu en gardant ce corps et, en tête, les quatre lignes NEON du fork. Corrections Mac de `system_info.cpp`, `platform.cpp` et des CMake intactes. Albireo : 0 erreur, 0 avertissement ; Pseudalis : 0 erreur, avertissements connus. `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` d'Albireo passent ; `bitcheck` 0 sur 43 200 ; Pseudalis `test_engine`, `test_host`, `test_linux.sh` : 34 sur 34.

## 2026-11-09 — Garder en vecteurs les sommes des noyaux Q4_K et Q6_K

- **Quoi** : dans `dot_q4_K_neon` et `dot_q6_K_neon`, chaque groupe est multiplié par son échelle dans le vecteur (`vmlaq_n_s32`) et un seul `vaddvq_s32` termine le bloc de 256 valeurs ; échelles et minimums de Q4_K décodés d'un coup par masques ; somme des minimums par `vpaddq_s16` et `vmull_s16`. `dot16` retirée (plus utilisée). Toujours identique au bit près au code portable. Génération 4 à 9 % plus rapide.
- **Fichiers** : `dlib/dlib/external/albireo/engine/qkernels_neon.inc` ; `docs/notes-pour-l-auteur.md` (changement 6, bilan), ce journal.
- **Pourquoi** : profil de la génération du 4B à 8 fils : `qk_dot` 75 %, attente des fils 21 % ; les noyaux réduisaient chaque groupe de 16 ou 32 valeurs en scalaire.
- **Étapes** : `git fetch upstream` : 6 commits de l'original non fusionnés (Albireo 1.0.12, Pseudalis 1.0.5 bis, exemples HRM). Mesure « avant » (8 fils, 64 tokens, `lowpowermode 0`, charge 2,6) : 2B prompt 118,6 / génération 37,0 tokens/s, 4B 51,1 / 16,8. Formats des modèles relevés : chat Q4_K et Q6_K seulement ; Q8_0 pour le modèle de recherche et le projecteur d'images (prompts longs, par Accelerate). Contrôle hors dépôt `bitcheck` (vrais poids du 2B et du 4B, 6 formes d'activations) : 0 différence sur 43 200 produits avant et après. Build sans avertissement ; `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api` passent. Mesures alternées A/B/A/B (`bench_runtime modele 8 64 64 1`, charge 3,2 à 7,1) : 2B 35,4 / 36,8 → 38,9 / 38,2 tokens/s, 4B 17,0 / 16,5 → 18,1 / 18,0. Essayé et rejeté : groupes de lignes plus petits en génération (4B 18,1 → 17,0 et 13,7). Relevé, pas encore changé : 4 fils (les cœurs de performance) battent 8 en génération et en prompt court, 8 gagnent sur un prompt de 4 096 tokens.

## 2026-11-08 — Fusionner l'original : Pseudalis 1.0.4 et 1.0.5

- **Quoi** : 4 commits de Cydral fusionnés (`75d4376`, `0bf2814`, `5c2d401`, `0503bcb`) dans `63f4af4` : Pseudalis 1.0.4 et 1.0.5 (moteur, service, page web, `dlib/privacy`, test complet allongé), installateur Windows 1.0.5 dans `dist/`. Albireo n'est pas touché.
- **Fichiers** : ceux de l'original ; pour le fork, `docs/notes-pour-l-auteur.md` (commit de référence `0503bcb`, numéros de ligne recalés, deux avertissements ajoutés), `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal.
- **Pourquoi** : demande de l'humain.
- **Étapes** : fichiers communs au fork et à la fusion : `pseudalis/CMakeLists.txt` et `pseudalis/host/drive_folders.cpp`, fusionnés sans conflit, corrections Mac intactes (branche ARM64, `#define st_mtim st_mtimespec`) ; message de fusion mis en français avant le push ; builds de Pseudalis et d'Albireo : 0 erreur, 3 avertissements de l'original dans Pseudalis (dont `whitespace_count` et `total_temp_score`, déjà présents aux builds précédents mais oubliés dans les notes) ; `test_engine`, `test_host` passent ; `test_linux.sh` sur le port 18454 (`lowpowermode 0`, charge 4) : 34 sur 34, 0 failed ; service de Pseudalis relancé (1.0.5, page en 200).

## 2026-11-08 — Passer l'attention de l'encodeur d'images par Accelerate

- **Quoi** : sur Mac, les deux produits de chaque tuile de `qk_encoder_attention` (notes `S = Q Kt`, valeurs `O += P V`) passent par `cblas_sgemm` (`encoder_scores_accelerate`, `encoder_values_accelerate` dans `qkernels_accelerate.inc`), appelés en tête de `encoder_scores` et `encoder_values` sous `ALBIREO_QK_ACCELERATE`. Encodage d'une image 512 × 512 (16 × 16 tokens) par le projecteur du 4B : 6,6 à 8,5 s → 1,8 à 2,2 s.
- **Fichiers** : `albireo/engine/qkernels_accelerate.inc`, `albireo/engine/qkernels.cpp` (deux aiguillages de quatre lignes), ce journal.
- **Pourquoi** : mesurer une image après le passage des produits par Accelerate (point annoncé) ; le profil de l'encodage montrait `qk_encoder_attention` à 20 143 échantillons, tout le reste à quelques centaines (code portable sur Mac, clés lues en travers).
- **Étapes** : images de macOS converties en JPEG (`sips`, zèbre et perroquet) ; avant le changement, mêmes descriptions qu'avec le moteur de l'attention NEON (zèbre mot pour mot, perroquet à trois mots près), lecture de l'image et de la question 20,2 → 10,5 s et 23,3 → 11,6 s ; profil ; contrôle hors dépôt contre une attention en double précision (84 cas, `hd` 64 à 128, 1 à 1 100 clés, 1 à 64 lignes) : plus grand écart 3,0e-6 contre 4,0e-6 pour le code portable ; build sans avertissement ; tests `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent ; mesures alternées (`lowpowermode 0`, charge 5 à 11) : encodage 6 668 / 7 749 / 8 495 / 8 526 ms → 2 101 / 2 181 / 1 781 / 1 886 ms, lecture complète 10,7 à 12,9 s → 5,9 à 7,3 s, descriptions identiques mot pour mot ; service relancé.

## 2026-11-08 — Donner le code avant et après pour les erreurs qui touchent aussi Windows

- **Quoi** : `docs/notes-pour-l-auteur.md`, entrée 1 (`bench_runtime` sous 64 tokens) : fichier à corriger, code avant (lignes 12 et 76 à 85 de `upstream/main`), code après (`head = std::min<long>(64, prompt)`), preuve par AddressSanitizer, ce qui est vérifié et ce qui ne l'est pas ; entrée 3 : rien à corriger dans le code (serveur). Consigne ajoutée à CLAUDE.md.
- **Fichiers** : `docs/notes-pour-l-auteur.md`, `CLAUDE.md`, ce journal.
- **Pourquoi** : l'humain propose des PR à l'auteur pour les erreurs reproductibles sous Windows.
- **Étapes** : correction écrite dans une copie hors du dépôt (le fork ne touche pas au fichier de l'original), compilée avec les options du build plus `-Wall -Wextra` : 0 avertissement ; original avec AddressSanitizer : `heap-buffer-overflow`, lecture de 256 octets dans 128 (ligne 83, tableau alloué ligne 48) ; version corrigée : rien signalé ; `bench_runtime` corrigé va au bout avec 1, 32 et 64 tokens. Sans AddressSanitizer, l'original à 32 tokens a fini une fois normalement et s'est arrêté une fois sans la ligne « batched generation » (comportement indéfini). Non testé sous Windows.

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
