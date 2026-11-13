# Premier jour sur le Mac mini M5 Pro

Ce qu'il faut faire, dans l'ordre, pour reprendre le portage sur le Mac mini (M5 Pro, 64 Go) et le comparer au MacBook Air M2. Écrit le 2026-11-13, Albireo 4.13.0. Les commandes se lancent depuis la racine du dépôt.

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
cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-albireo -- -k 0
cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis -- -k 0
```

## 3. Copier les modèles du M2

Les tests et les mesures cherchent les modèles à ces places (`build/` n'est pas dans git). Les copier du M2 (disque externe, AirDrop ou partage de fichiers) évite 16 Go de téléchargements.

| Fichier | Taille | Sert à |
|---|---|---|
| `build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf` | 1,1 Go | tests, mesures, banc de la page |
| `build/models/Qwen3.5-0.8B-Q8_0.gguf` | 0,8 Go | étapes Qwen 3.5 sur le processeur |
| `build/models/Qwen3.5-4B-mmproj-F16.gguf` | 0,7 Go | images de Qwen 3.5 |
| `build/models/Qwen3.6-11B-A3B-Niwaki-Q4_K_M.gguf` | 7,0 Go | mélange d'experts |
| `build/models/gemma-4-E2B-it-Q4_K_M.gguf` | 3,1 Go | Gemma 4 (processeur ; tokenizer, raisonnement) |
| `build/albireo-home/models/qwen3-vl-4b-thinking.gguf` et `.mmproj.gguf` | 2,9 Go | modèle du service, images |
| `build/albireo-home/models/hf-unsloth-qwen3.5-4b-q4_k_m.gguf` | 2,7 Go | Qwen 3.5 sur le GPU, outils, images |
| `build/albireo-home/models/qwen3-embedding-0.6b.gguf` | 0,6 Go | modèle de recherche |

Le dossier `build/albireo-home` contient aussi les réglages du service (dont, si l'accès réseau est ouvert, l'empreinte de son mot de passe) : le copier en entier garde le service tel qu'il est ; sinon le service retélécharge ses modèles par défaut au premier lancement. Les discussions, elles, sont dans le navigateur, pas dans ce dossier.

Pour le grand modèle à experts, le télécharger directement sur le Mac mini dans `build/bench-models/` (22,1 Go, `unsloth/Qwen3.6-35B-A3B-GGUF`, fichier UD-Q4_K_M), avec son projecteur s'il y en a un.

## 4. Vérifier que tout marche

```sh
ALBIREO_TEST_PORT=18453 bash dlib/dlib/external/albireo/tools/test_macos.sh      # 29 étapes sur le M2
PSEUDALIS_TEST_PORT=18454 bash dlib/dlib/external/pseudalis/tools/test_macos.sh   # 34 étapes
build/macos-albireo/tests/test_metal                                             # noyaux Metal contre le processeur
bash dlib/dlib/external/albireo/tools/page_test/page_test.sh                      # ESLint et 108 vérifications de la page
```

Un écart entre le GPU et le processeur au chargement d'un modèle (« its check disagrees ») renvoie seul le modèle au processeur : le journal du service le dit. Sur une nouvelle puce, c'est la première chose à regarder.

## 5. Mesurer

```sh
pmset -g | grep lowpowermode          # doit donner 0
bash dlib/dlib/external/albireo/tools/bench_macos.sh 2   # tableau dans build/bench/<modèle du Mac>-<date>.md
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
| Le projecteur du 35B-A3B marche-t-il ? | `vision_architectures` de `host/hub.cpp` (ajouter `qwen35moe`) | description d'une image comparée à llama.cpp |

Chaque réglage changé met à jour sa valeur conseillée et sa fiche dans l'onglet Service, avec la mesure en commentaire (règle de CLAUDE.md).

## 7. Mettre la documentation à jour

- CLAUDE.md, partie « Le projet » : la machine actuelle devient le Mac mini.
- `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, le journal, les notes pour l'auteur (partie Vitesse, avec les nouveaux temps), le tableau des différences.
- Un épisode du blog : le premier jour sur la nouvelle machine.
