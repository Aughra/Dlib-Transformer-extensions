# Reprise — portage Mac de Dlib-Transformer-extensions

> À lire en premier à chaque session (règle de CLAUDE.md). Dernière mise à jour : 2026-11-07, sur le Mac M2.

## Objectif

Faire tourner le projet sur Mac Apple Silicon : d'abord dlib et les exemples SLM, puis le moteur et le serveur d'Albireo et de Pseudalis (leur variante Linux, lancée avec `--console`). Les parties purement Windows (icône, installateur, service, lecteur, Explorateur) sont hors du périmètre.

## Fait

- 2026-11-05 : remote `upstream` ajouté (Cydral) ; 4 commits de l'original fusionnés (`0abb591`). Build CPU de référence (dlib + exemples) : 0 erreur, 24 exemples `slm_*` en Mach-O arm64. Albireo et Pseudalis : 5 causes d'échec relevées.
- 2026-11-06 : CLAUDE.md, ce fichier, le journal et le blog créés. 10 nouveaux commits de l'original fusionnés (`037a32e`). Fork déménagé sur `Aughra` (public) ; `origin` y pointe, les push passent.
- 2026-11-06 : blog : épisodes 1 à 3 repris du carnet TortueTech, épisode 4 « Le fork déménage », épisode 5 « Cinq corrections et un premier démarrage ».
- 2026-11-06 : **les 5 causes d'échec corrigées** (détail dans le mémo, section « État constaté ») : moteurs `avx2` non construits sur ARM64, `endian.h` de macOS pour `svox_pico`, sources NEON de libpng, frameworks `CoreFoundation` et `Security` liés à `dlib_httplib`, `st_mtimespec`. **Albireo et Pseudalis compilent en entier**, 0 erreur.
- 2026-11-06 : tests : Pseudalis `test_engine` et `test_host` passent ; Albireo `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` passent. `pseudalis/tools/test_linux.sh` (service complet) : 26 étapes sur 28.

- 2026-11-06 : **Albireo répond sur le Mac** : Qwen3-VL 2B Instruct (Q4_K_M, 1,1 Go, dans `build/models/`, SHA-256 conforme au catalogue), réponse juste. Vitesse : 0,8 token/s avec le moteur en bibliothèque (`engine_chat`), 1,9 token/s avec `bench_runtime` à 4 fils. Chiffres et profils dans le mémo. Blog : épisode 6.
- 2026-11-06 : **le « moteur sur un seul cœur » venait du mode économie d'énergie** (`lowpowermode 1`). Coupé par l'humain : `engine_chat` passe à 6,0 tokens/s à 4 fils et 7,9 à 8 fils, presque comme `bench_runtime` (7,0 et 8,3). Essai de l'instruction `isb` dans `ALBIREO_CPU_RELAX()` sur ARM64 : remet les fils à la priorité 31 mais ne gagne rien en mesures alternées ; abandonné, code d'origine gardé. Règle de mesure ajoutée à CLAUDE.md. Blog : épisode 7.
- 2026-11-07 : **noyaux NEON de `qk_dot` pour Q4_K et Q6_K** (`engine/qkernels_neon.inc`, branchés dans `qkernels.cpp`). Résultat identique au bit près à la version portable (0 différence sur 12 000 produits ; même réponse). Génération à 8 fils : 33 à 37 tokens/s contre 11 (mesures alternées). Le prompt ne change pas (`matmul_float`). Blog : épisode 8. L'original a 1 commit de plus (`8743fcd`, mesure du compresseur sur enwik9), pas fusionné.
- 2026-11-07 : **produit en floats NEON pour le prompt** (`gemm_neon` dans `qkernels_neon.inc`, branché dans `gemm_rows`), identique au bit près à `qk_dot_f32` (0 différence sur 32 436 produits ; un `vfmaq_f32` aurait changé le dernier bit). Prompt à 8 fils : 35 à 50 tokens/s contre 5 à 13 (mesures alternées). Même réponse. Blog : épisode 9.
- 2026-11-07 : 1 commit de l'original fusionné (`8743fcd`, mesure du compresseur de tokens sur enwik9), sans conflit. Il ne touche que `Readme.md` et `examples/Readme.md` : pas de code, donc pas de rebuild.
- 2026-11-07 : **le service d'Albireo tourne sur le Mac** (`--console`, données dans `build/albireo-home`, port 18443). Deux corrections sous `#if defined(__APPLE__)` : chemin du programme par `_NSGetExecutablePath` (`common/platform.cpp`, sans quoi ni moteur ni page web n'étaient trouvés), mémoire et nom du processeur par `sysctl` (`common/system_info.cpp`). Modèles par défaut téléchargés en une minute (Qwen3-VL 4B Thinking, 2,3 Go + projecteur 433 Mo ; Qwen3 Embedding 0.6B, 610 Mo). Page web et fichiers servis (200) ; `/v1/chat/completions` répond « Paris est la capitale de la France. » en 3,7 s (14 tokens/s), et en flux. Tests `test_host`, `test_model_file`, `test_engine_api`, `test_qkernels`, `test_chat_format` repassés. Blog : épisode 10.
- 2026-11-07 : **page web essayée par l'humain** dans son navigateur : conversation, puis résumé juste d'un PDF de 4,6 Mo (guide d'hygiène de l'ANSSI) lu par recherche. Écriture à 3,4 tokens/s sur ce long contexte : l'attention, non accélérée, prend les deux tiers du calcul.

## En cours

Rien. Priorité de l'humain : **Albireo d'abord** (vitesse), Pseudalis ensuite.

## À faire, dans l'ordre proposé

1. **Noyaux NEON pour l'attention** (`qk_attention_scores`, `qk_attention_values`, clés et valeurs en F16) : avec un PDF (~20 000 tokens de contexte), l'écriture tombe à 3,4 tokens/s et l'attention prend les deux tiers du calcul (profil dans le mémo). Viser le résultat au bit près, comme pour `gemm_neon`, et mesurer en alternance sur un long contexte.
2. Plus tard, si utile : noyaux NEON pour les autres formats de poids (Q8_0 du projecteur et du modèle de recherche, Q5_K…).

Ensuite, Pseudalis :

0. **Pseudalis, chemin du programme** : `pseudalis/common/platform.cpp` et `pseudalis/host/main.cpp` lisent aussi `/proc/self/exe`. Même correction que pour Albireo (`_NSGetExecutablePath`). À faire avant de chercher la cause des deux étapes qui échouent.
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
- Noyaux NEON seulement pour Q4_K, Q6_K et le produit en floats. Les autres formats (Q8_0 du projecteur d'images et du modèle de recherche, Q5_K…) et l'attention restent portables.
- Lancé en arrière-plan par un script, le service ignore SIGINT (le shell le lui fait ignorer, et macOS jette alors le signal) : l'arrêter par `kill -TERM`. Dans un terminal, Ctrl-C marche. Même chose attendue pour `test_linux.sh` d'Albireo à l'étape « service stop ».
- Le modèle par défaut est un modèle « Thinking » : il raisonne longtemps (en anglais) avant de répondre ; prévoir un `max_tokens` large ou `"reasoning": false`.
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

# Service d'Albireo (page : https://localhost:18443/ ; arrêt : Ctrl-C, ou kill -TERM)
ALBIREO_HOME=$PWD/build/albireo-home build/macos-albireo/bin/albireo-service --console
curl -sk https://localhost:18443/v1/chat/completions -H "Content-Type: application/json" \
  -d '{"model": "albireo-general", "reasoning": false, "max_tokens": 80, "messages": [{"role": "user", "content": "Bonjour"}]}'
```
