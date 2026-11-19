# Premier jour sur le Mac mini M5 Pro

Ce qu'il faut faire, dans l'ordre, pour reprendre le portage sur le Mac mini (M5 Pro, 64 Go) et le comparer au MacBook Air M2. Écrit le 2026-11-13 (Gulliver 4.13.0), mis à jour le 2026-11-16 (Gulliver 5.4.1, Pseudalis 3.1.0) et le 2026-11-17 (backend Metal de dlib ; arrivée prévue vers le 2026-11-21). Les commandes se lancent depuis la racine du dépôt.

## 1. Installer les outils

| Outil | Pourquoi | Comment |
|---|---|---|
| Xcode | AppleClang, Metal, les SDK | App Store, puis l'ouvrir une fois |
| CMake, Ninja | construire | `brew install cmake ninja` |
| `gh` | pousser vers `origin` | `brew install gh`, puis `gh auth login` (compte `Aughra`) |
| Node.js, Google Chrome | banc d'essai de la page | `brew install node` ; Chrome depuis son site |
| llama.cpp | références pour vérifier une architecture | `brew install llama.cpp` (0.9.0 sur le M2) |

## 2. Récupérer et construire

```sh
gh repo clone Aughra/Dlib-Transformer-extensions && cd Dlib-Transformer-extensions
bash dlib/dlib/external/gulliver/tools/fetch_pdfium.sh     # PDFium, non versionné : sans lui, les PDF sont refusés (422 sur /gulliver/v1/extract)
cmake -S dlib/dlib/external/gulliver -B build/macos-gulliver -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-gulliver -- -k 0
cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis -- -k 0
cmake -S dlib/dlib/test -B build/macos-arm64-tests-metal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DDLIB_USE_FFMPEG=OFF -DDLIB_USE_CUDA=OFF -DDLIB_USE_NPU=OFF -DDLIB_USE_METAL=ON
cmake --build build/macos-arm64-tests-metal --target dtest                  # backend Metal de dlib (épisode 51)
```

## 3. Copier les modèles et les données du M2

Les tests et les mesures cherchent les modèles à ces places (`build/` n'est pas dans git). Les copier du M2 (disque externe, AirDrop ou partage de fichiers) évite environ 45 Go de téléchargements.

| Dossier ou fichier | Taille | Sert à |
|---|---|---|
| `build/models/` en entier | 39 Go | tous les tests et les mesures : Qwen3-VL 2B, Qwen 3.5 0.8B et son projecteur 4B, Qwen 3.6 11B-A3B, Gemma 3 1B et 4B (et son projecteur), Gemma 4 E2B (et son projecteur) et 26B-A4B, LFM2.5 1.2B et 8B-A1B |
| `build/gulliver-home/models/` | 6,4 Go | le modèle du service (Qwen3-VL 4B Thinking et son projecteur), Qwen 3.5 4B, le modèle de recherche |
| `build/gulliver-home/settings.json`, `gulliver.db` | < 1 Mo | les réglages du service (ports, réseau, relais) et ses modèles installés |
| `build/gulliver-home/relay.secret` | — | la clé du relais, s'il y en a une : la copier seulement par un moyen privé (pas un nuage partagé), ou la retaper dans l'onglet Service |
| `build/pseudalis-home/` | < 1 Mo | les réglages de Pseudalis (port 18444, accès réseau 8445) et ses originaux conservés |

Ne pas copier `build/gulliver-home/library/` (39 Go) ni `reserve/` (6,4 Go) : sur le M2, ce sont des clones APFS de `build/models` et des modèles installés, qui ne prennent pas de place ; copiés vers une autre machine, ils deviendraient de vraies copies. Sur le Mac mini, remplir la bibliothèque par des clones : `cp -c build/models/*.gguf build/gulliver-home/library/`. La réserve se refait seule.

Les certificats (`certificates/` des deux applications) se regénèrent au premier lancement ; ne pas les copier. Les discussions, elles, sont dans le navigateur, pas dans ces dossiers.

Pour le grand modèle à experts, le télécharger directement sur le Mac mini dans `build/bench-models/` (22,1 Go, `unsloth/Qwen3.6-35B-A3B-GGUF`, fichier UD-Q4_K_M), avec son projecteur s'il y en a un.

## 4. Vérifier que tout marche

```sh
GULLIVER_TEST_PORT=18453 bash dlib/dlib/external/gulliver/tools/test_macos.sh      # 60 étapes sur le M2 (accès réseau d'essai sur 18454)
PSEUDALIS_TEST_PORT=18456 bash dlib/dlib/external/pseudalis/tools/test_macos.sh   # 37 étapes
build/macos-gulliver/tests/test_metal                                             # noyaux Metal contre le processeur
bash dlib/dlib/external/gulliver/tools/page_test/page_test.sh                      # ESLint et 131 vérifications de la page
(cd build/macos-arm64-tests-metal && ./dtest --test_metal)                       # backend Metal de dlib : 4 041 vérifications sur le M2
```

Un écart entre le GPU et le processeur au chargement d'un modèle (« its check disagrees ») renvoie seul le modèle au processeur : le journal du service le dit. Sur une nouvelle puce, c'est la première chose à regarder.

## 5. Mesurer

```sh
pmset -g | grep lowpowermode          # doit donner 0
bash dlib/dlib/external/gulliver/tools/bench_macos.sh 2   # tableau dans build/bench/<modèle du Mac>-<date>.md
```

Le produit de matrices de dlib sur le GPU, contre le processeur (sur le M2 : 2048×2048 en 6,3 ms contre 14,5 ms, et le GPU plus lent sous 1024) :

```sh
(cd build/macos-arm64-tests-metal && DLIB_METAL_BENCH=1 ./dtest --test_metal | grep -o 'gemm [0-9]*x[0-9]*: .*')
```

Le script mesure chaque modèle présent (y compris ceux de `build/bench-models/`) sur le processeur et le GPU, en alternance. Garder le tableau du M2 à côté pour comparer.

## 6. Décider avec ces mesures

| Question | Où | Mesure qui tranche |
|---|---|---|
| À partir de combien de tokens le GPU lit-il un prompt plus vite ? | réglage `gpu_prompt_tokens` (24 sur le M2), `recommended` de `web/js/service.js` | `bench_runtime <modèle> <threads> <16, 24, 32…> 16 1 q8 [metal]`, en alternance |
| Jusqu'où le GPU lit-il les prompts ? | `gpu_prompt_context` (262 144) | 4 096, 8 192, 16 384 tokens, processeur contre GPU |
| Combien de threads ? | `threads` (0 = cœurs physiques) | génération et lecture à 6, 10, 12, 14 threads |
| Le mélange d'experts écrit-il plus vite sur le GPU ? | moteur (`set_gpu`) | `engine_chat` sur le 35B-A3B, processeur contre GPU : sur le M2, l'écart était faible (26 contre 25 tokens/s) |
| Les tuiles du produit groupé des experts (`mm_id`) sont-elles à la bonne taille ? | `MM_TT`, `MM_NR` dans `metal_kernels.metal` | lecture de prompts du 35B-A3B, en alternance |
| Les prompts de Gemma 4 vont-ils plus vite sur le GPU (Gulliver 4.27.0) ? | `attention_mtile` en deux tranches de 256 (`SPLIT`, `metal_kernels.metal`) ; à défaut, renvoyer ces prompts au processeur | `bench_runtime` sur gemma-4-E2B, 512 et 4 096 tokens, processeur contre GPU, en alternance : les couches de 512 calculent leurs scores deux fois |
| Combien écrit et lit Gemma 4 26B-A4B sur le processeur (Gulliver 4.29.0), et faut-il le porter au GPU ? | moteur (`set_gpu`, `cpu_runtime_metal.cpp`, qui le refuse) | `engine_chat` et `bench_runtime` sur le 26B-A4B ; le comparer au Qwen 3.6 35B-A3B sur le GPU |
| Combien de temps prend l'encodage d'une image de Gemma 3 (Gulliver 4.28.0) ? | — (sur le M2 en mode économie d'énergie : 26 à 30 s) | `engine_chat --projector … --image …`, ligne « image of 16x16 tokens encoded in » |
| Les modèles jamais essayés tiennent-ils dans 64 Go ? | Gemma 4 E4B, 12B, 31B ; qwen3moe, mixtral, granitemoe sur le GPU (admis, jamais lancés) | `engine_chat` sur chacun, réponses comparées à llama.cpp |
| Mistral Small 3.2 24B (`build/models/Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf`, 14,3 Go) passe-t-il sur le GPU, et à quelle vitesse ? | sur le M2, refusé par le GPU (13,6 Go de tampons au plus) et lent sur le processeur (3,5 tokens/s, services arrêtés) | `engine_chat` et `bench_runtime`, processeur contre GPU ; un appel d'outil et un résultat |
| Le projecteur du 35B-A3B marche-t-il ? | `vision_architectures` de `host/hub.cpp` (ajouter `qwen35moe`) | description d'une image comparée à llama.cpp |

Chaque réglage changé met à jour sa valeur conseillée et sa fiche dans l'onglet Service, avec la mesure en commentaire (règle de CLAUDE.md).

## 7. Mettre la documentation à jour

- CLAUDE.md, partie « Le projet » : la machine actuelle devient le Mac mini.
- `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, le journal, les notes pour l'auteur (partie Vitesse, avec les nouveaux temps), le tableau des différences.
- Un épisode du blog : le premier jour sur la nouvelle machine.
