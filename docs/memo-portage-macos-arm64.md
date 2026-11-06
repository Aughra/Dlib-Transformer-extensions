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
| Albireo | 0 erreur, 0 avertissement. `albireo-service`, `albireo-speech`, `albireo-builtin`, `albireo-sign` et le moteur `libalbireo-engine-cpu.dylib` (portable) sont construits. | Passent : `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api` (sur le moteur cpu), `test_host`. Le moteur répond juste avec Qwen3-VL 2B Instruct (`engine_chat`) ; vitesse ci-dessous. Pas lancés (il leur faut un fichier de référence) : `test_runtime`, `test_tokenizer`. Le service n'a pas encore été lancé. |
| Pseudalis | 0 erreur, 1 avertissement (`-Wunused-private-field`). `pseudalis-service` et `libpseudalis-engine.dylib` sont construits. | `test_engine` et `test_host` passent. `tools/test_linux.sh` : 26 étapes sur 28 passent (service, anonymisation et restauration par l'interface et par le lecteur). Échouent : « drive signatures » (une archive protégée renommée sur le lecteur part dans `refused/`) et « copies on the drive » (la copie d'un fichier protégé crée un nouvel enregistrement). |

### Albireo : premier modèle et vitesse

Modèle : Qwen3-VL 2B Instruct, `Qwen3VL-2B-Instruct-Q4_K_M.gguf` (1 107 409 952 octets, SHA-256 `089d75c5…dca6c2ae`, conforme à `dist/albireo/catalog.json`), téléchargé dans `build/models/`. Mac M2 : 4 cœurs performance, 4 cœurs efficacité. Moteur portable (sans SIMD), température 0.

```sh
build/macos-albireo/tests/engine_chat build/macos-albireo/bin/engines/libalbireo-engine-cpu.dylib \
  build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf --threads 4 --max 40 --temperature 0 "<question>"
build/macos-albireo/tests/bench_runtime build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf <fils> 64 16 2
```

| Mesure (2026-11-06) | Prompt | Génération | Cœurs occupés en moyenne |
|---|---|---|---|
| `engine_chat`, moteur `libalbireo-engine-cpu.dylib`, 8 fils (21 tokens de prompt, 88 générés) | 0,99 token/s | 0,78 token/s | 0,9 |
| `engine_chat`, même moteur, 4 fils (21 / 39 tokens) | 0,99 token/s | 0,85 token/s | 0,95 |
| `bench_runtime`, 1 fil (32 tokens de prompt) | 0,5 token/s | 0,5 token/s (1 978 ms par token) | 0,5 |
| `bench_runtime`, 4 fils (64 tokens de prompt) | 2,5 tokens/s | 1,9 token/s (526 ms par token) ; 2 séquences : 2,1 tokens/s | 2,1 |
| `bench_runtime`, 8 fils (64 tokens de prompt) | 2,1 tokens/s | 1,8 token/s (551 ms par token) ; 2 séquences : 2,0 tokens/s | 2,1 |

La réponse est correcte (une explication juste d'un compilateur, en français). Constats :

- **Le calcul est scalaire.** Profil (`sample`) : presque tout le temps est dans `matmul_float` (prompt) et `qk_dot_portable` (génération), les versions portables des produits, qui traitent un nombre à la fois. Les versions AVX2 traitent 8 nombres par instruction ; il n'existe pas de version NEON.
- **Le moteur en bibliothèque n'occupe qu'un cœur**, alors que `bench_runtime`, qui compile le même code dans le programme, en occupe environ deux. Profil de `engine_chat` à 4 fils : 5 fils présents, `qk_dot_portable` dans 6 143 échantillons, `swtch_pri` (le `std::this_thread::yield()` de `ALBIREO_CPU_RELAX` hors x86, `engine/thread_pool.h`) dans 5 664 ; le processus est à 85 % d'un cœur. Cause pas encore établie.
- 8 fils ne vont pas plus vite que 4 : les cœurs d'efficacité n'aident pas.
- `bench_runtime` lit toujours 64 tokens de son prompt pour la génération groupée : avec un prompt plus court, il lit hors du tableau (exception `token identifier out of range`). Lui donner au moins 64 tokens.

`tools/test_linux.sh` demande deux outils de Linux absents de macOS : `nproc` (remplacé par `sysctl -n hw.ncpu`) et `stat -c` de GNU (`gstat`, paquet Homebrew `coreutils`). Sans eux, la taille lue est 0, et l'étape « drive signatures » échoue pour une raison qui ne tient pas au programme.

Ce qui a été changé pour compiler sur ARM64, chaque fois sous une condition qui ne vaut que pour macOS ou ARM64 :

1. Moteurs `avx2` : non construits sur ARM64 (`albireo/CMakeLists.txt`, `pseudalis/CMakeLists.txt`) ; les tests d'Albireo qui prennent les noyaux AVX2 les compilent sans options x86, donc en code portable.
2. `svox_pico/lib/picopltf.h` : sur macOS, `<machine/endian.h>`, avec `__BYTE_ORDER` et `__BIG_ENDIAN` ramenés aux noms de macOS (`BYTE_ORDER`, `BIG_ENDIAN`).
3. libpng d'Albireo : sur ARM64, compilation de ses sources NEON (`arm/arm_init.c`, `arm/filter_neon_intrinsics.c`, `arm/palette_neon_intrinsics.c`), comme le fait le CMake de dlib.
4. `dlib/dlib/CMakeLists.txt`, cible `dlib_httplib` : sur macOS, liaison de `CoreFoundation` et `Security` (certificats du Trousseau).
5. `pseudalis/host/drive_folders.cpp` : sur macOS, `st_mtim` devient `st_mtimespec`.

## Points à surveiller

- Le code NEON dlib est le chemin SIMD attendu sur ARM64; vérifier les tests CPU et les résultats des exemples sur le Mac réel.
- Les sorties numériques d'ARM64 peuvent différer de celles d'une machine x86; comparer avec des tolérances, sauf pour les chemins qui promettent explicitement un résultat déterministe.
- Les tests d'intégration du chat doivent couvrir `/`, `/v1/models`, `/v1/chat/completions`, le streaming, l'annulation, l'UTF-8 et le décodage d'images lorsque les codecs sont activés.
- La section « État constaté » est tenue à jour à chaque build vérifié sur le Mac ; un résultat qui n'y figure pas n'a pas été vérifié.
