# Mémo de build macOS Apple Silicon

## Périmètre

Une seule cible est visée : un build natif `arm64` sur Apple Silicon. Aucun build Intel ni binaire universel n'est prévu dans cette étape. Le build initial est CPU seulement, sans GUI dlib ni dépendance optionnelle externe.

Le point d'entrée CMake des exemples du dépôt est `dlib/examples`. Ce projet ajoute `dlib/dlib` comme bibliothèque et déclare les exemples SLM. Le dépôt ne contient pas de `CMakeLists.txt` à sa racine.

## Prérequis

- macOS sur Apple Silicon, avec les outils de ligne de commande Xcode (`clang`, SDK macOS) ;
- CMake 3.17 ou plus récent ;
- vérifier que le terminal est natif ARM avec `uname -m` (`arm64`).

## Build CPU de référence

Depuis la racine du dépôt :

```sh
cmake -S dlib/examples -B build/macos-arm64-cpu \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DDLIB_NO_GUI_SUPPORT=ON \
  -DDLIB_USE_CUDA=OFF \
  -DDLIB_USE_NPU=OFF \
  -DDLIB_USE_BLAS=OFF \
  -DDLIB_USE_LAPACK=OFF \
  -DDLIB_USE_MKL_FFT=OFF \
  -DDLIB_USE_FFMPEG=OFF \
  -DDLIB_JPEG_SUPPORT=OFF \
  -DDLIB_PNG_SUPPORT=OFF \
  -DDLIB_GIF_SUPPORT=OFF \
  -DDLIB_WEBP_SUPPORT=OFF \
  -DDLIB_JXL_SUPPORT=OFF

cmake --build build/macos-arm64-cpu --parallel 2
```

La compilation complète peut être longue, notamment pour les exemples qui instancient de grands réseaux. Le parallélisme limité réduit la pression mémoire.

## Tests de base

Configurer le binaire de tests dlib dans un répertoire distinct, avec les mêmes options CPU et sans dépendances optionnelles :

```sh
cmake -S dlib/dlib/test -B build/macos-arm64-tests \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DDLIB_NO_GUI_SUPPORT=ON \
  -DDLIB_USE_CUDA=OFF \
  -DDLIB_USE_NPU=OFF \
  -DDLIB_USE_BLAS=OFF \
  -DDLIB_USE_LAPACK=OFF \
  -DDLIB_USE_MKL_FFT=OFF \
  -DDLIB_USE_FFMPEG=OFF \
  -DDLIB_JPEG_SUPPORT=OFF \
  -DDLIB_PNG_SUPPORT=OFF \
  -DDLIB_GIF_SUPPORT=OFF \
  -DDLIB_WEBP_SUPPORT=OFF \
  -DDLIB_JXL_SUPPORT=OFF

cmake --build build/macos-arm64-tests --parallel 2
build/macos-arm64-tests/dtest
```

Vérifier que la configuration ne signale pas CUDA ou OpenVINO activé, que les cibles SLM compilent, et que `dtest` termine sans échec. La désactivation de la GUI dlib ne désactive pas le serveur HTTP du chat ni son interface web intégrée.

## Vérifications optionnelles, séparées

Chaque variante doit utiliser un répertoire de build distinct afin de ne pas réutiliser les valeurs de cache du build CPU minimal.

### BLAS et Accelerate

Le détecteur du dépôt, [`find_blas.cmake`](../dlib/dlib/cmake_utils/find_blas.cmake), cherche notamment BLAS via `pkg-config`, MKL, OpenBLAS, ATLAS et des bibliothèques génériques. Il ne contient pas de détection explicite du framework Apple Accelerate. Par conséquent, activer `DLIB_USE_BLAS` et `DLIB_USE_LAPACK` ne suffit pas à démontrer qu'Accelerate est utilisé.

Avant de valider cette variante, ajouter ou confirmer une détection et un linkage explicites d'Accelerate dans CMake. Ensuite, configurer dans un nouveau répertoire avec `DLIB_USE_BLAS=ON` et `DLIB_USE_LAPACK=ON`, compiler `dtest` et comparer les tests numériques au build CPU de référence. Consigner dans la sortie CMake le framework effectivement lié.

### Codecs d'images

Dans un nouveau build, activer les options `DLIB_JPEG_SUPPORT`, `DLIB_PNG_SUPPORT`, `DLIB_GIF_SUPPORT`, `DLIB_WEBP_SUPPORT` et `DLIB_JXL_SUPPORT`, puis compiler et exécuter les tests dlib d'image. Vérifier la sortie CMake pour distinguer les bibliothèques système des implémentations embarquées : JPEG et PNG peuvent être compilés depuis les sources vendoriées si les paquets système ne sont pas trouvés. Une compilation réussie ne prouve donc pas, à elle seule, que les bibliothèques système sont utilisées.

### FFmpeg

Installer FFmpeg et `pkg-config` (par exemple avec Homebrew), puis configurer un répertoire neuf avec `DLIB_USE_FFMPEG=ON`. Le module [`find_ffmpeg.cmake`](../dlib/dlib/cmake_utils/find_ffmpeg.cmake) recherche les composants `libav*` via `pkg-config`. Confirmer leur détection dans la sortie CMake, compiler `dtest` et exécuter ses tests vidéo/audio.

## Applications Albireo et Pseudalis

Les deux applications de `dlib/dlib/external/` (Albireo : moteur GGUF et service de chat ; Pseudalis : anonymisation de documents) sont des programmes Windows 11. Elles ont chacune un CMake à elles, que le build des exemples ne compile pas. Leur variante Linux (service lancé avec `--console`, sans icône, installateur ni service Windows) est celle qui vise macOS :

```sh
cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-albireo --parallel 3 -- -k 0

cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis --parallel 3 -- -k 0
```

`-- -k 0` fait continuer Ninja après une erreur, pour relever toutes les erreurs d'un seul passage. Restent hors de portée sur macOS : l'icône de la zone de notification, l'installateur, le service Windows, le lecteur avec sa lettre et les commandes de l'Explorateur.

## État constaté

Sur Mac M2, macOS arm64, avec AppleClang 21, CMake 4.1.2 et Ninja (2026-11-06, après la fusion de `upstream/main` à `f0f9f91`) :

| Build | Compilation | Tests |
|---|---|---|
| dlib + exemples (CPU de référence) | 0 erreur ; les 24 exemples `slm_*` sont des Mach-O arm64. Avertissements : `ld: -s is obsolete`, `-Wbraced-scalar-init`. | Exemples et `dtest` pas encore exécutés. |
| Albireo | 0 erreur, 0 avertissement. `albireo-service`, `albireo-speech`, `albireo-builtin`, `albireo-sign` et le moteur `libalbireo-engine-cpu.dylib` (noyaux `NEON+dotprod` pour Q4_K et Q6_K, portables pour les autres formats) sont construits. | Passent : `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api` (sur le moteur cpu), `test_host`. Le moteur répond juste avec Qwen3-VL 2B Instruct (`engine_chat`) ; vitesse ci-dessous. Pas lancés (il leur faut un fichier de référence) : `test_runtime`, `test_tokenizer`. Le service n'a pas encore été lancé. |
| Pseudalis | 0 erreur, 1 avertissement (`-Wunused-private-field`). `pseudalis-service` et `libpseudalis-engine.dylib` sont construits. | `test_engine` et `test_host` passent. `tools/test_linux.sh` : 26 étapes sur 28 passent (service, anonymisation et restauration par l'interface et par le lecteur). Échouent : « drive signatures » (une archive protégée renommée sur le lecteur part dans `refused/`) et « copies on the drive » (la copie d'un fichier protégé crée un nouvel enregistrement). |

### Albireo : premier modèle et vitesse

Modèle : Qwen3-VL 2B Instruct, `Qwen3VL-2B-Instruct-Q4_K_M.gguf` (1 107 409 952 octets, SHA-256 `089d75c5…dca6c2ae`, conforme à `dist/albireo/catalog.json`), téléchargé dans `build/models/`. Machine : MacBook Air M2 (`Mac14,15`, sans ventilateur), 4 cœurs performance et 4 cœurs efficacité, sur secteur. Température 0. Le modèle n'a que deux formats de poids : Q4_K (168 tenseurs, 1 204 M valeurs) et Q6_K (29 tenseurs, 517 M valeurs), plus des normes en F32.

**Avant toute mesure de vitesse**, vérifier que le mode économie d'énergie est coupé (`pmset -g | grep lowpowermode` doit donner `0`) et noter la charge (`sysctl -n vm.loadavg`). En mode économie d'énergie, le moteur va environ dix fois moins vite et paraît n'occuper qu'un cœur.

```sh
build/macos-albireo/tests/engine_chat build/macos-albireo/bin/engines/libalbireo-engine-cpu.dylib \
  build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf --threads 8 --max 88 --temperature 0 "<question>"
build/macos-albireo/tests/bench_runtime build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf <fils> 64 16 2
```

**Noyaux NEON (2026-11-07).** Sur ARM64 avec les instructions de produit scalaire (`__ARM_FEATURE_DOTPROD`, présentes sur toutes les puces Apple), `qk_dot` prend pour Q4_K et Q6_K les noyaux de `engine/qkernels_neon.inc` (`vdotq_s32`). Ils calculent les mêmes sommes entières que les références, exactes quel que soit l'ordre, et finissent chaque bloc par la même expression : résultat identique au bit près (contrôle : 0 différence sur 12 000 produits contre `qk_dot_portable`, longueurs 256, 2 048 et 6 144 ; même réponse octet pour octet à température 0). Le moteur s'annonce `NEON+dotprod`. Mesures alternées le même jour, 8 fils, charge moyenne de 5 à 7 :

| Programme | Essai | Version | Prompt | Génération | 2 séquences |
|---|---|---|---|---|---|
| `engine_chat` (21 / 88 tokens) | 1 | origine | 7,8 tokens/s | 10,9 tokens/s | |
| `engine_chat` | 1 | NEON | 10,3 tokens/s | 32,5 tokens/s | |
| `engine_chat` | 2 | origine | 12,8 tokens/s | 11,0 tokens/s | |
| `engine_chat` | 2 | NEON | 12,7 tokens/s | 36,9 tokens/s | |
| `bench_runtime` (64 tokens de prompt, 32 pas) | 1 | origine | 8,8 tokens/s | 6,5 tokens/s | 11,3 tokens/s |
| `bench_runtime` | 1 | NEON | 13,7 tokens/s | 33,7 tokens/s | 43,2 tokens/s |
| `bench_runtime` | 2 | origine | 13,4 tokens/s | 10,8 tokens/s | 10,5 tokens/s |
| `bench_runtime` | 2 | NEON | 13,2 tokens/s | 32,9 tokens/s | 41,5 tokens/s |

Le prompt ne change pas : à partir de 8 tokens (`qk_float_product_tokens`), il passe par `matmul_float` (poids développés en floats, puis `gemm_rows` et `qk_dot_f32`), que les noyaux NEON ne touchent pas.

Mesures de référence du moteur d'origine, avant NEON (2026-11-06, mode économie d'énergie coupé, charge moyenne entre 13 et 30 : mises à jour de l'App Store en cours) :

| Programme | Fils | Prompt | Génération | Cœurs occupés en moyenne |
|---|---|---|---|---|
| `engine_chat` (moteur en bibliothèque), 21 tokens de prompt, 88 générés | 4 | 7,7 tokens/s | 6,0 tokens/s | 2,9 |
| `engine_chat` | 8 | 9,2 tokens/s | 7,9 tokens/s | 4,6 |
| `bench_runtime` (moteur compilé dans le programme), 64 tokens de prompt | 1 | 2,7 tokens/s | 2,2 tokens/s (457 ms par token) | 1,0 |
| `bench_runtime` | 4 | 9,6 tokens/s | 7,0 tokens/s (144 ms) ; 2 séquences : 6,9 tokens/s | 3,7 |
| `bench_runtime` | 8 | 11,3 tokens/s | 8,3 tokens/s (120 ms) ; 2 séquences : 8,9 tokens/s | 5,9 |

Pour mémoire, en mode économie d'énergie : `engine_chat` 0,78 token/s à 8 fils et 0,85 à 4 fils (0,9 cœur occupé) ; `bench_runtime` 1,9 token/s à 4 fils.

Constats :

- **Avant NEON, le calcul était scalaire.** Profil (`sample`) : presque tout le temps est dans `matmul_float` (prompt) et `qk_dot_portable` (génération). La génération a maintenant ses noyaux NEON (ci-dessus) ; le prompt reste sur `matmul_float`, prochaine piste de vitesse.
- **L'attente des fils.** Hors x86, `ALBIREO_CPU_RELAX()` (`engine/thread_pool.h`) est `std::this_thread::yield()`, qui fait tomber la priorité du fil à 0 sur macOS (`ps -M`). Essai : l'instruction `isb` à la place sur ARM64. Les fils restent alors à la priorité 31, mais la génération n'est pas plus rapide (deux essais alternés : 5,2 et 5,5 tokens/s avec `isb`, 5,7 et 6,4 sans). Essai abandonné, code d'origine gardé.
- `bench_runtime` lit toujours 64 tokens de son prompt pour la génération groupée : avec un prompt plus court, il lit hors du tableau (exception `token identifier out of range`). Lui donner au moins 64 tokens.

`tools/test_linux.sh` demande deux outils de Linux absents de macOS : `nproc` (remplacé par `sysctl -n hw.ncpu`) et `stat -c` de GNU (`gstat`, paquet Homebrew `coreutils`). Sans eux, la taille lue est 0, et l'étape « drive signatures » échoue pour une raison qui ne tient pas au programme.

Ce qui a été changé pour compiler sur ARM64, chaque fois sous une condition qui ne vaut que pour macOS ou ARM64 :

1. Moteurs `avx2` : non construits sur ARM64 (`albireo/CMakeLists.txt`, `pseudalis/CMakeLists.txt`) ; les tests d'Albireo qui prennent les noyaux AVX2 les compilent sans options x86, donc en code portable.
2. `svox_pico/lib/picopltf.h` : sur macOS, `<machine/endian.h>`, avec `__BYTE_ORDER` et `__BIG_ENDIAN` ramenés aux noms de macOS (`BYTE_ORDER`, `BIG_ENDIAN`).
3. libpng d'Albireo : sur ARM64, compilation de ses sources NEON (`arm/arm_init.c`, `arm/filter_neon_intrinsics.c`, `arm/palette_neon_intrinsics.c`), comme le fait le CMake de dlib.
4. `dlib/dlib/CMakeLists.txt`, cible `dlib_httplib` : sur macOS, liaison de `CoreFoundation` et `Security` (certificats du Trousseau).
5. `pseudalis/host/drive_folders.cpp` : sur macOS, `st_mtim` devient `st_mtimespec`.

Ce qui a été ajouté pour la vitesse :

6. `albireo/engine/qkernels_neon.inc` (fichier du fork) : noyaux NEON de `qk_dot` pour Q4_K et Q6_K. `albireo/engine/qkernels.cpp` les branche par quatre ajouts sous `ALBIREO_QK_NEON` (`__ARM_NEON` et `__ARM_FEATURE_DOTPROD`) : `#include <arm_neon.h>`, l'inclusion du fichier, deux cas dans `qk_dot`, le nom `NEON+dotprod` dans `qk_isa()`.

## Points à surveiller

- Le code NEON dlib est le chemin SIMD attendu sur ARM64; vérifier les tests CPU et les résultats des exemples sur le Mac réel.
- Les sorties numériques d'ARM64 peuvent différer de celles d'une machine x86; comparer avec des tolérances, sauf pour les chemins qui promettent explicitement un résultat déterministe.
- Les tests d'intégration du chat doivent couvrir `/`, `/v1/models`, `/v1/chat/completions`, le streaming, l'annulation, l'UTF-8 et le décodage d'images lorsque les codecs sont activés.
- La section « État constaté » est tenue à jour à chaque build vérifié sur le Mac ; un résultat qui n'y figure pas n'a pas été vérifié.
