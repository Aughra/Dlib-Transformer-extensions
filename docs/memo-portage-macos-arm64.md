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

Sur Mac M2, macOS arm64, avec AppleClang 21, CMake 4.1.2 et Ninja (2026-11-05) :

| Build | Résultat |
|---|---|
| dlib + exemples (CPU de référence) | Compilé, 0 erreur ; les 24 exemples `slm_*` sont des Mach-O arm64. Avertissements : `ld: -s is obsolete` (25), `-Wbraced-scalar-init` (3). Exemples et `dtest` pas encore exécutés. |
| Albireo | Configuré ; compilation arrêtée à 279 étapes sur 303. |
| Pseudalis | Configuré ; compilation arrêtée à 172 étapes sur 174. Le moteur `libpseudalis-engine.dylib`, `test_engine` et `test_host` sont construits. |

Causes des échecs, aucune corrigée à cette date :

1. Moteurs et noyaux `avx2` (les deux applications) : `-mavx2 -mfma -mf16c` sont passés à clang pour une cible arm64.
2. `svox_pico/lib/picopltf.h` (synthèse vocale d'Albireo) inclut `<endian.h>`, qui n'existe pas sous ce nom sur macOS.
3. libpng d'Albireo active ses routines NEON sans compiler leurs sources : symboles `_png_*_neon` manquants.
4. httplib lit les certificats du Trousseau sur macOS : `CoreFoundation` et `Security` ne sont pas liés.
5. `pseudalis/host/drive_folders.cpp` emploie `st_mtim` (Linux) au lieu de `st_mtimespec` (macOS).

## Points à surveiller

- Le code NEON dlib est le chemin SIMD attendu sur ARM64; vérifier les tests CPU et les résultats des exemples sur le Mac réel.
- Les sorties numériques d'ARM64 peuvent différer de celles d'une machine x86; comparer avec des tolérances, sauf pour les chemins qui promettent explicitement un résultat déterministe.
- Les tests d'intégration du chat doivent couvrir `/`, `/v1/models`, `/v1/chat/completions`, le streaming, l'annulation, l'UTF-8 et le décodage d'images lorsque les codecs sont activés.
- La section « État constaté » est tenue à jour à chaque build vérifié sur le Mac ; un résultat qui n'y figure pas n'a pas été vérifié.
