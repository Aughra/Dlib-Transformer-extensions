# Reprise — portage Mac de Dlib-Transformer-extensions

> À lire en premier à chaque session (règle de CLAUDE.md). Dernière mise à jour : 2026-11-06, sur le Mac M2.

## Objectif

Faire tourner le projet sur Mac Apple Silicon : d'abord dlib et les exemples SLM, puis le moteur et le serveur d'Albireo et de Pseudalis (leur variante Linux, lancée avec `--console`). Les parties purement Windows (icône, installateur, service, lecteur, Explorateur) sont hors du périmètre.

## Fait

- 2026-11-05 : remote `upstream` ajouté (Cydral) ; 4 commits de l'original fusionnés (`0abb591`). Build CPU de référence (dlib + exemples) : 0 erreur, 24 exemples `slm_*` en Mach-O arm64. Albireo et Pseudalis : 5 causes d'échec relevées.
- 2026-11-06 : CLAUDE.md, ce fichier, le journal et le blog créés. 10 nouveaux commits de l'original fusionnés (`037a32e`). Fork déménagé sur `Aughra` (public) ; `origin` y pointe, les push passent.
- 2026-11-06 : blog : épisodes 1 à 3 repris du carnet TortueTech, épisode 4 « Le fork déménage », épisode 5 « Cinq corrections et un premier démarrage ».
- 2026-11-06 : **les 5 causes d'échec corrigées** (détail dans le mémo, section « État constaté ») : moteurs `avx2` non construits sur ARM64, `endian.h` de macOS pour `svox_pico`, sources NEON de libpng, frameworks `CoreFoundation` et `Security` liés à `dlib_httplib`, `st_mtimespec`. **Albireo et Pseudalis compilent en entier**, 0 erreur.
- 2026-11-06 : tests : Pseudalis `test_engine` et `test_host` passent ; Albireo `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent. `pseudalis/tools/test_linux.sh` (service complet) : 26 étapes sur 28.

- 2026-11-06 : **Albireo répond sur le Mac** : Qwen3-VL 2B Instruct (Q4_K_M, 1,1 Go, dans `build/models/`, SHA-256 conforme au catalogue), réponse juste. Vitesse : 0,8 token/s avec le moteur en bibliothèque (`engine_chat`), 1,9 token/s avec `bench_runtime` à 4 fils. Chiffres et profils dans le mémo. Blog : épisode 6.

## En cours

Rien. Priorité de l'humain : **Albireo d'abord** (vitesse), Pseudalis ensuite.

## À faire, dans l'ordre proposé

1. **Albireo, le moteur en bibliothèque n'occupe qu'un cœur** (`engine_chat` : 85 % d'un cœur avec 4 fils ; `bench_runtime`, même code compilé dans le programme : environ 2 cœurs). Profil : les fils sont là et ont du travail (`qk_dot_portable`), mais passent autant de temps dans `swtch_pri` (le `yield()` de `ALBIREO_CPU_RELAX` hors x86, `engine/thread_pool.h`). Pistes : la priorité (QoS) des fils créés par le moteur, le `yield()` de macOS qui abaisse la priorité du fil qui l'appelle, une différence de chemin entre `engine_chat` et `bench_runtime`. Critère : `engine_chat` occupe autant de cœurs que `bench_runtime`.
2. **Albireo, des noyaux NEON** pour `qk_dot`, `matmul_float` et `gemm_rows` (aujourd'hui portables, un nombre à la fois). Le plus gros gain attendu. Critère : `test_qkernels` passe et `bench_runtime` va plus vite, à fils égaux.
3. Lancer le service d'Albireo en `--console`, poser une question par l'interface web.

Ensuite, Pseudalis :

1. **Pseudalis, « drive signatures »** : une archive protégée (`grosse.zip`) renommée sur le lecteur en `grosse.zip.bak` est déplacée dans `refused/files/` au lieu de rester. Sur Linux, elle reste. Piste : la façon dont le lecteur (`host/drive_folders.cpp`, scrutation par `stat`) reconnaît un fichier qu'il a déjà protégé, après un renommage, sur APFS.
2. **Pseudalis, « copies on the drive »** : la copie d'un fichier protégé (`cp copies.txt "copies (2).txt"`) crée un nouvel enregistrement au lieu de reprendre celui du fichier. Même piste : la reconnaissance d'un fichier déjà protégé. Attention, `cp` sur APFS fait un clone : à comparer avec Linux.

Puis : construire et lancer `dtest` (tests dlib), et quelques exemples `slm_*`.

Après chaque point : mettre à jour la section « État constaté » du mémo, ce fichier et le journal, puis commiter et pousser. Une grande étape (service d'Albireo qui répond, test de Pseudalis à 28 sur 28, première mesure de vitesse) a son épisode de blog.

## Décisions

- Mac Apple Silicon uniquement ; Windows hors du périmètre.
- Modifier le moins possible les fichiers de l'original, sous `if (APPLE)` / `#ifdef __APPLE__` ou un test d'ARM64 (`CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$"`), pour garder des fusions faciles avec upstream.
- Ne pas modifier `pseudalis/tools/test_linux.sh` : sur Mac, on lui fournit `nproc` et `stat` de GNU par le `PATH` (voir les commandes ci-dessous).

## Problèmes ouverts

- L'ancien fork `PapyTortueTech/Dlib-Transformer-extensions` n'est plus utilisé ; l'humain s'occupe de l'arrêter. Ne plus jamais y pousser.
- Le moteur d'Albireo sur Mac est le moteur portable : correct, mais sans accélération SIMD, et en bibliothèque il n'occupe qu'un cœur (voir À faire, point 1).
- `bench_runtime` exige un prompt d'au moins 64 tokens (sinon il lit hors de son tableau) : `bench_runtime <modèle> 4 64 16 2`.

## Commandes pour reprendre

```sh
cd ~/dev/Dlib-Transformer-extensions
git status -sb && git fetch upstream && git log --oneline main..upstream/main
cmake --build build/macos-pseudalis -- -k 0
cmake --build build/macos-albireo -- -k 0

# Test complet de Pseudalis : nproc et stat de GNU fournis par un dossier du PATH
mkdir -p /tmp/shim && printf '#!/bin/sh\nsysctl -n hw.ncpu\n' > /tmp/shim/nproc && chmod +x /tmp/shim/nproc
ln -sf /opt/homebrew/bin/gstat /tmp/shim/stat
PATH=/tmp/shim:$PATH bash dlib/dlib/external/pseudalis/tools/test_linux.sh $PWD/build/macos-pseudalis
```
