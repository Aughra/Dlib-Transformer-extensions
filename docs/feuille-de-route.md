# Feuille de route du fork

Ce qui vient ensuite, dans l'ordre. Écrite le 2026-11-19, le jour où le Mac mini M5 Pro (64 Go) est arrivé. Ce document dit **quoi faire et dans quel ordre** ; l'avancement au jour le jour reste dans [logs/reprise.md](../logs/reprise.md), et le détail de chaque étape dans le journal.

| Partie | Sujet | État |
|---|---|---|
| 1 | Passer sur le Mac mini, mesurer, régler | à faire (machine en préparation) |
| 2 | Le reste en attente du Mac mini | à faire après la partie 1 |
| 3 | L'atelier : affiner, alléger et créer des modèles | à faire après la partie 2 |
| Fil rouge | Rendre Gulliver indépendant de dlib, un morceau à la fin de chaque chantier | à faire, à partir de la fin de la partie 1 |

---

## Fil rouge — Rendre Gulliver indépendant de dlib

Décision de l'humain (2026-11-19) : dépendre de dlib freine les changements. Gulliver en reprend le code **petit à petit, un morceau à la fin de chaque chantier**, pour le faire sien et l'optimiser. On ne reporte rien dans dlib : la copie de Gulliver est la seule qui vit. Pseudalis reste sur dlib pour l'instant ; on s'en occupera plus tard.

### Ce que Gulliver prend à dlib aujourd'hui

Relevé le 2026-11-19 : 10 fichiers d'en-tête, et le moteur n'utilise pas la partie réseaux de neurones de dlib (il a ses propres noyaux).

| Morceau | Fichiers de dlib | Taille |
|---|---|---|
| ~~Bibliothèques d'autres projets~~ | ~~`external/httplib`, sqlite, `external/libpng`, `external/libjpeg`, mbedtls~~ — **copiées telles quelles dans `gulliver/third_party/` le 2026-11-20 (Gulliver 6.8.1)** | 21 Mo |
| ~~SHA-256~~ | ~~`crypto/sha256.h`~~ — **fait le 2026-11-19 (Gulliver 6.4.1)** : `engine/image_input.cpp` passe par CommonCrypto, comme `host/crypto.cpp` ; test `test_image_input` | 326 lignes |
| ~~Lecture des ZIP, enveloppe de sqlite~~ | ~~`zip/`, `sqlite/`~~ — **fait le 2026-11-20 (Gulliver 6.12.0)** : `host/zip_reader.h` (le lecteur de dlib, décompression et CRC par la zlib de macOS), `host/sqlite_db.h` (l'enveloppe, sur le SQLite de macOS, ouvert en mode « serialized ») | petites |
| ~~Lecture des GGUF~~ | ~~`data_io/gguf_reader.h`, `gguf_dequantize.h`, `gguf_model_spec.h`~~ — **fait le 2026-11-20 (6.12.0)** : `common/gguf_reader.h` (avec `clean_model_name`, seule fonction prise à `gguf_model_spec.h`), `common/gguf_dequantize.h` | 1 411 lignes |
| ~~Tokenizer~~ | ~~`tokenizer/hf_tokenizer.h`~~ — **fait le 2026-11-20 (6.12.0)** : `engine/hf_tokenizer.h`, `engine/hf_unicode.h`, sans la sérialisation de dlib | 1 166 lignes |

### L'ordre, un morceau par fin de chantier

| Ordre | Morceau | Où il va | Remarque |
|---|---|---|---|
| 1 | ~~SHA-256 → CommonCrypto d'Apple~~ **fait (6.4.1)** | `host/`, `engine/` | Le service s'en servait déjà (4.2.0) ; restait l'empreinte des images du moteur |
| 2 | ~~Bibliothèques d'autres projets~~ **fait (6.8.1)** | `gulliver/third_party/` | aucune ligne changée ; le build de Gulliver ne construit plus dlib (copiées et non déplacées : dlib et Pseudalis s'en servent encore) |
| 3 | ~~Lecture des ZIP, enveloppe de sqlite~~ **fait (6.12.0)** | `host/` | demande de l'humain du 2026-11-20 : les morceaux 3 à 5 faits ensemble |
| 4 | ~~Lecture des GGUF~~ **fait (6.12.0)** | `common/` | optimisations à venir |
| 5 | ~~Tokenizer~~ **fait (6.12.0)** | `engine/` | optimisable ensuite, notamment pour le français |

**Depuis Gulliver 6.12.0 (2026-11-20), Gulliver (service et moteur) ne lit plus rien de dlib** : son build n'a plus de chemin d'inclusion vers dlib, et la liste des en-têtes que lit la compilation n'en contient aucun (`ninja -t deps`). Il vit encore dans le dossier de dlib (`dlib/dlib/external/gulliver`). Le code repris garde la licence de dlib (Boost), copiée dans `resources/dlib-license.txt`.

### L'atelier, de la même façon

L'atelier (partie 3) se construit **dans Gulliver**, avec le code d'entraînement de dlib déplacé au fur et à mesure des besoins de chaque phase, puis optimisé. Inclure `dlib/dnn.h` tire 600 fichiers (environ 260 000 lignes, relevé du 2026-11-19), car dlib est très entremêlé : machines à vecteurs de support, traitement d'images, statistiques… dont l'atelier n'a pas besoin. On ne déplace donc pas en bloc : chaque phase reprend ce qu'elle utilise et coupe les inclusions inutiles.

| Phase de l'atelier | Ce qu'elle reprend de dlib (à préciser au début de la phase) |
|---|---|
| A (conversion) | tenseurs et leurs calculs (processeur et Metal, `cuda/tensor_tools`, `metal_dlib`), couches du transformer, import GGUF (`slm_gguf_import_ex`), archives `.dat` |
| B (affinage) | LoRA (`lora_adapter.h`), fonctions d'erreur (`loss.h`), Adam (`solvers.h`), boucle d'entraînement (`trainer.h`), données d'affinage (`finetuning_data.h`) |
| D, E | distillation, apprentissage du tokenizer (`bpe_tokenizer.h`), exemples de préentraînement |

### Le tri : ce qu'on garde, ce qu'on coupe

Premier tri du 2026-11-19, fait en cherchant les noms dans le code de dlib et dans les exemples `slm_*` (import GGUF, LoRA, distillation, Cygnus). Il est **approximatif** : le vrai tri se fait à chaque phase par la compilation (on retire un morceau, ça compile toujours et les tests passent : il était inutile). Par exemple, l'addition des branches résiduelles (`add_prev`) n'a pas été trouvée par la recherche, alors qu'un transformer en a forcément besoin : elle porte sans doute un autre nom (`add_prev1`…).

**On garde :**

| Famille | Ce qu'on garde | À quoi ça sert |
|---|---|---|
| Tenseurs (`cuda/`) | `tensor.h`, `gpu_data`, `tensor_tools`, `cpu_dlib`, `cpu_gguf`, `gguf_blocks`, `operation_mode.h`, backend Metal (`metal_dlib.mm`, `metal_kernels.mm`, `metal_dispatch.h`) | les tableaux de nombres et leurs calculs, processeur et GPU |
| Mécanique des réseaux | `dnn/core.h`, `dnn/network_context.h` | empiler les couches, calculer à l'endroit puis à l'envers |
| Couches (`dnn/layers.h`) | environ 28 sur 65 : `embeddings`, `positional_encodings`, `rms_norm`, `unit_rms_norm`, `rope`, `repeat_heads`, `fc`, `fc_no_bias`, `linear`, `linear_no_bias`, `softmax`, `softmaxm`, `tril`, `tril_mask`, `split_heads_to`, `merge_heads_to`, `multm_prev`, `gelu`, `dropout`, `extract`, `slice`… | les briques d'un modèle de langage |
| Blocs du transformer (`dnn/transformer.h`) | `gqa_attention`, `multihead_attention`, `moe_`, `fused_ffn`, `transformer_block`, `transformer_stack` | Qwen, Gemma, LFM2, Mistral, MoE |
| LoRA | `dnn/lora_adapter.h`, partie fusion de `dnn/visitors.h` (`merge_network_adapters`) | R2 à R5 |
| Fonctions d'erreur (`dnn/loss.h`) | 2 sur 25 : `loss_cross_entropy_per_token`, `loss_distillation_per_token` | apprendre le mot suivant ; distiller (R8) |
| Apprentissage | AdamW (`dnn/solvers.h`), `dnn/trainer.h`, `dnn/lr_scheduler.h`, `dnn/finetuning_data.h` | faire tourner l'entraînement |
| Configurations | `dnn/transformer_config.h`, `dnn/decoder_transformer_config.h`, `dnn/decoder_models.h` | décrire chaque architecture |
| Entrée | `input_tensor` (et `input_embeddings`) de `dnn/input.h` | les tokens en entrée |
| Lecture et écriture | `data_io/gguf_*`, `tokenizer/hf_tokenizer.h`, `tokenizer/bpe_tokenizer.h`, `serialize.h` | modèles, tokenizer et son apprentissage (R14), archives |

**On coupe :**

| Ce qu'on coupe | Pourquoi |
|---|---|
| `svm/` (108 fichiers tirés), traitement d'images, géométrie, détection d'objets | n'arrivent que par trois portes : les fonctions d'erreur de détection (`loss_mmod`, `loss_yolo`… de `loss.h`, qui inclut `svm/ranking_tools.h` et `image_processing/`), `dnn/validation.h` et les entrées d'images de `dnn/input.h` |
| 23 fonctions d'erreur sur 25 | images, classement, détection, métrique |
| Les couches d'images (convolutions `con`/`cont`, pooling, `upsample`, `bn_con`, `reorg`…) | inutiles pour le texte |
| Les entrées d'images (`input_rgb_image`, pyramides…) | seule `input_tensor` sert |
| Vision (`vision_transformer`, `runtime_vision_encoder`, `modality_fusion`) | Gulliver lit les images avec son moteur ; la liste n'entraîne pas de modèle de vision |
| Raisonnement en boucle (`adaptive_halting`, HRM) | exemples de sudoku et d'ARC |
| Inférence de dlib (`runtime_transformer`, `text_generation`) | Gulliver a son propre moteur, plus rapide |
| CUDA, cuDNN, cuBLAS, cuRAND, cuSOLVER (`cuda/*.cu`, `*_dlibapi`), NPU (`npu_dlib`) | pas de carte NVIDIA sur un Mac |
| Petits outils de base (`threads`, `pipe`, `md5`, `dir_nav`, `statistics`) | remplacés par la bibliothèque standard du C++ ou le thread pool de Gulliver |

### Dernier pas

Gulliver vit encore physiquement dans l'arbre de dlib (`dlib/dlib/external/gulliver/`). Quand il n'en dépendra plus, son dossier pourra sortir à la racine du dépôt.

### Règles

- Un morceau déplacé passe `test_host`, `test_macos.sh` et le banc de la page, comme toute sous-étape.
- Le code repris a ses commentaires en français et perd ce qui ne sert pas au Mac ; une optimisation n'est gardée que si elle gagne en mesures alternées.
- Le code repris n'est plus du code de dlib : il n'a pas de ligne au registre « Ce que le fork change dans dlib ». Seul un changement fait dans `dlib/` lui-même en a une.

---

## Partie 1 — Le Mac mini

**Avancement (2026-11-19)** : étapes 1 à 5 et 7 faites ; étape 6 faite pour les threads (l'équipe rapide corrigée, Gulliver 6.4.0), `gpu_prompt_tokens`, `gpu_prompt_context` et les experts sur le GPU ; reste de l'étape 6 : les modèles jamais essayés et ceux à porter au GPU. Mesures : `docs/mesures/` ; décisions : `logs/nuit-2026-11-19.md`.

Le plan détaillé est [docs/mac-mini-premier-jour.md](mac-mini-premier-jour.md), dans l'ordre :

1. Installer les outils (Xcode, CMake, Ninja, `gh`, Node.js, Chrome, llama.cpp).
2. Récupérer le dépôt et construire Gulliver, Pseudalis et les tests Metal de dlib.
3. Copier les modèles et les données du M2 (`build/models/`, `build/gulliver-home/models/`, réglages et base `gulliver.db`, `build/pseudalis-home/`) ; refaire la bibliothèque par clones.
4. Vérifier : `test_macos.sh` des deux applications, `test_metal`, banc de la page, `dtest --test_metal`.
5. Mesurer : `tools/bench_macos.sh`, en premier (mode économie d'énergie coupé).
6. Décider avec ces mesures : `gpu_prompt_tokens`, `gpu_prompt_context`, threads, experts sur le GPU ou le processeur, tuiles `mm_id`, prompts de Gemma 4 sur le GPU, Gemma 4 26B-A4B, modèles jamais essayés, Mistral Small 3.2 sur le GPU, projecteur du 35B-A3B ; mettre à jour `recommended` de `service.js` et les fiches.
7. Mettre la documentation à jour (machine actuelle dans CLAUDE.md, mémo, notes pour l'auteur, différences, épisode du blog).

## Partie 2 — Le reste en attente du Mac mini

Repris de la liste « À faire » de la reprise et des suites du bloc C :

**Mesures de vitesse reprises (décision de l'humain, 2026-11-21 : « je veux un Gulliver le plus rapide possible maintenant que nous sommes sur le mini »)** : voir le chantier « Vitesse » ci-dessous, prioritaire. Avant, le 2026-11-20 : **mesures de vitesse arrêtées** (« arrêt les tests de perfs, et continue la roadmap ») : la comparaison avec Ollama et MLX, le découpage de l'attention des longues questions, plusieurs tokens par pas et les mesures de la recherche documentaire sont mis de côté. On passe au morceau 2 du fil rouge, puis à la partie 3. Gemma 4 26B-A4B sur le GPU reste ici, à reprendre plus tard.


- ~~Gemma 4 26B-A4B sur le GPU~~ **fait (7.12.0, 2026-11-27)** : lecture de 512 tokens 1 341-1 421 jetons/s, écriture 46-49 (llama.cpp 1 951 et 59,7) ; ~~12B~~ fait en 7.10.0 ; E4B déjà sur le GPU (contrôle à 0,4 à 1 %, changement 59) ; 31B pas installé.
- ~~Qwen3.6-35B-A3B et ses projecteurs~~ **fait (6.5.0, 2026-11-19)** : sur le GPU, images identiques à llama.cpp ; ~~qwen3moe, mixtral, granitemoe sur le GPU (admis, jamais lancés)~~ **essayés et validés contre llama.cpp, prompts sur le GPU, format de Mixtral corrigé (6.6.0)** ; ~~noyau Q4_K du GPU (54 Go/s contre 70 au plafond)~~ **mesuré sur le Mac mini (2026-11-19) : 218 Go/s pour un plafond de 260, 84 à 86 % ; laissé tel quel** (marge d'environ 15 %, voir `macos/metal-sonde/README.md`).
- **Passer Gulliver au C++23** (demande de l'humain, 2026-11-27 : « tu n'oublies pas C++23 ») : ~~le réglage seul~~ fait en 7.13.1 (2026-11-28 : `CMAKE_CXX_STANDARD 23`, trois combinaisons d'énumérations corrigées, build sans avertissement, tests au vert). Reste, au fil des chantiers, `std::expected` pour les erreurs, `std::span`, `std::format`, plus de `constexpr`. Pas de gain de vitesse attendu (les calculs lourds sont dans les noyaux Metal) : un code plus sûr et plus court.
- **Tout le Python du dépôt en C++** (décision de l'humain, 2026-11-28 : « c'est moi qui veux du tout C++ ») : les outils de `dlib/dlib/external/gulliver/tools/` (conseil et épreuve du lundi, retrait et veille des modèles, mesures du service, test de la recherche, mail, surveillant) et leurs tests. Commencé par la session Temper (banc et conseil, 7.15.0 à 7.19.0), porté depuis la 7.20.0 par la session Gulliver seule (demande de l'humain : « je veux pas deux parties »), un fichier à la fois, chacun vérifié contre la sortie du Python sur des données réelles avant de le remplacer ; le surveillant en dernier. Fait : conseil (7.19.0), mail à l'administrateur, liste des modèles installés, retrait du dernier du classement, session du test de la page (7.20.0), épreuve et sondes de cybersécurité branchées au moteur (7.21.0), mesure du service et test de la recherche (7.22.0), veille du lundi (7.23.0), surveillant et son test (7.24.0), morceaux de Python des scripts et `comment_check` (7.25.0). Reste : les outils de Pseudalis (`bump_version.py`, `make_images.py`, les `python3` de son `test_macos.sh`).
- **Vider le JavaScript du navigateur** (demande de l'humain, 2026-11-27 : « vider JS du browser ») : faire faire au service, en C++, ce que la page fait encore en JavaScript, comme le Markdown en 7.9.0, pour ne garder dans la page que l'affichage et les clics. D'abord l'inventaire de `web/js/` (ce que chaque module calcule ou décide), puis un morceau par version, en commençant par ce qui touche à la sécurité ou aux données ; banc de la page (`tools/page_test/`) à chaque pas.
- Mesures de la recherche documentaire en C++ contre l'ancienne en JS ; reranker neuronal si le reranker par mots ne suffit pas.
- Plusieurs tokens par pas (brouillons), repris de l'original, s'il reste du temps.
- **Mesure faite pour llama.cpp (2026-11-20, `docs/mesures/2026-11-20-gulliver-contre-llama-cpp.md`) : llama.cpp lit les prompts environ 3 fois plus vite et écrit 1,3 à 1,8 fois plus vite ; Ollama et MLX à installer si l'humain le veut.** Comparer Gulliver à d'autres moteurs locaux sur les longues questions (demande de l'humain, 2026-11-19) : llama.cpp (déjà installé), Ollama, et éventuellement un moteur MLX, sur le même modèle et le même fichier GGUF, prompts de 512 à 16 384 tokens, lecture et écriture, en alternance. Critère : un tableau « temps avant le premier mot » par moteur, dans les notes pour l'auteur et le tableau des différences. Raison : un retour d'usage lu par l'humain (Ollama abandonné parce que les longues requêtes « prenaient une éternité »).

### Chantier « Vitesse » — le Gulliver le plus rapide (prioritaire depuis le 2026-11-21)

But : devancer llama.cpp et MLX sur le Mac mini, en lecture et en écriture, justesse identique. Écarts et explications : [mesures/tests-de-vitesse.md](mesures/tests-de-vitesse.md) ; mesure hebdomadaire (dimanche 4 h) dans le carnet. Dans l'ordre du gain attendu :

| N° | Chantier | Écart à combler (21 novembre) |
|---|---|---|
| V1 | L'attention des questions : elle est déjà d'un seul tenant en demi-précision (`attention_mtile`, 4.11.0) ; la réorganiser comme celle de MLX (`steel_attention` : scores gardés dans les registres, pas de 32 positions et moins d'attentes, tuiles plus grandes), puis sur les accélérateurs du M5 (V5) | lecture ×1,2 (512 tokens) à ×3,9 (16 384, MLX) |
| V2 | L'étape d'écriture mesurée noyau par noyau (capture Metal) : Gulliver à 69 % du plafond de la mémoire, MLX à 93 % ; lire ses noyaux quantifiés (`qmv`) et sa façon d'enchaîner les commandes | écriture ×1,3 à ×1,6 |
| V3 | Les produits groupés des mélanges d'experts (`mm_id`) ; le contrôle du GPU qui renvoie les mélanges d'experts au processeur en demi-précision | lecture ×2,5, écriture ×1,7 |
| V4 | Un format de poids plus compact et plus juste, comme le 4 bits de MLX (Gulliver le lirait, sans embarquer MLX) ; essayer aussi des poids plus petits (3 bits) | écriture proportionnelle à la taille ; qualité à juger modèle par modèle : le 4 bits de MLX bat le Q4_K_M sur Qwen3-4B (17,17 contre 17,71) mais fait moins bien sur les quatre autres modèles (+3,6 à +9,2 %) |
| V5 | **Lecture : les accélérateurs neuronaux du M5** (calcul de matrices dans chaque cœur du GPU, par `MetalPerformancePrimitives` de Metal 4 ; présent dans le SDK de macOS 27.2). **MLX s'en sert déjà** (« NAX », `is_nax_available()` : M5 et macOS 26.2 ou plus) pour ses produits de matrices, ses poids en 4 bits, ses mélanges d'experts (`gather`) et son attention : c'est probablement l'essentiel de son avance en lecture. D'abord une sonde (un produit de matrices en demi-précision, simdgroup contre accélérateurs), puis l'attention (V1) et les experts (V3) dessus | lecture, à mesurer ; **à faire en premier** (2026-11-21) |
| V6 | **Lecture : le Neural Engine** (Core ML), en concurrence de V5 | lecture des petits modèles, à mesurer |
| V7 | **Écriture : le décodage spéculatif** (un petit modèle propose plusieurs mots, le gros les vérifie en une passe ; sortie identique mot pour mot ; l'original avait des brouillons) | écriture, souvent ×1,5 à ×2,5 ailleurs |
| V8 | **Écriture : la devinette par répétition** (« prompt lookup » : propositions tirées du texte déjà présent), en concurrence ou en complément de V7. **Fait en 6.22.0** (repris d'Albireo 1.0.27, `draft_tokens` 7 par défaut), avec les passes de plusieurs tokens sur les accélérateurs (`mv_nax`, une passe de 8 tokens à 1,37 fois une passe d'un token) : recopie ×3,1, question sur un texte +26 %, code +9 %, réponse libre +1 % ; texte identique sauf aux quasi-égalités. Reste : les étapes de plusieurs tokens des mélanges d'experts (Qwen3-30B-A3B −2,5 % en réponse libre) | écriture sur documents et code |
| V9 | **Écriture : une seule commande au GPU par mot** (tous les noyaux d'une étape envoyés d'un bloc), à décider après la capture de V2 | une partie des 30 % sous le plafond de la mémoire |
| V10 | Lire **mistral.rs** (moteur en Rust avec Metal) en plus de MLX et llama.cpp, pour ses noyaux et son décodage spéculatif | idées |

**État au 22 novembre, après la nuit** (journal et carnet `logs/nuit-2026-11-22.md`) : **V1** fait pour les têtes de 64 et 128 valeurs (6.37.0, `attention_flat`, organisation de MLX : 52 → 21 ms par couche à 8 192 tokens ; reste les têtes de 256 et 512 et les fenêtres glissantes) ; **V2** : écriture du modèle du service devant llama.cpp avec la mémoire en 8 bits (89,8 contre 88,2), 4 % derrière en demi-précision, le GPU seul prenant 10,45 ms par mot ; quatre essais sans gain (encodeur concurrent, attente active, fusions, parties d'attention) ; **V3** : produit groupé des experts +23 % à 512 tokens (6.38.0), encore derrière llama.cpp sur les questions courtes ; **V9** : la première couche envoyée sans attendre (6.35.0, +1 %).

**État au 27 novembre** (comparaison à llama.cpp et MLX en 7.12.0, `mesures/tests-de-vitesse.md`, « Le 27 novembre ») : en lecture, Gulliver devance llama.cpp et égale MLX jusqu'à 4 096 tokens ; l'avance de MLX en écriture tient surtout à son format 4 bits. Ordre décidé avec l'humain (« 1, 2, 3 ») :

1. ~~**V1 pour les têtes de 512 (Gemma 4)**~~ **fait (7.13.0)** : 26B à 16 384 tokens 263 → 1 505 tokens/s, devant llama.cpp (1 428) ; E2B ×2,8, E4B ×1,9 ; reste : les couches qui partagent leurs clés et valeurs (E2B, E4B) hors de ce chemin, cause à trouver. Avant : la lecture du 26B-A4B s'effondrait avec la longueur (260 tokens/s à 16 384, llama.cpp 1 428), 61 % du temps du GPU dans l'attention des 5 couches globales ; `attention_flat_split_512` en 7.13.0.
2. **V2 sur une longue mémoire** : l'écriture après 16 384 tokens est 25 à 45 % sous llama.cpp sur le même fichier (MLX 40 à 67 %) ; reprendre l'attention d'une étape de MLX (`sdpa_vector`).
3. **Justesse de Gemma 4 et des fichiers** : la perplexité de Gemma 4 (2 938, llama.cpp 2 881) reste 40 % au-dessus de MLX avec le BOS (1 734) ; le fichier MLX de Granite 4.2 8B est 8 à 10 % plus juste que le GGUF : comprendre, et choisir de meilleurs fichiers si c'est leur fabrication.

**Pistes en concurrence** (choix de l'humain, 2026-11-21) : quand deux pistes visent le même but (V5 ou V6 pour la lecture ; V7 ou V8 pour l'écriture), on essaie les deux, on les mesure en alternance, et on garde la meilleure (ou les deux si elles s'ajoutent). Ollama et LM Studio ne sont pas des pistes : ils habillent llama.cpp et MLX.

Règles : chaque changement mesuré en alternance contre la version d'avant (`tools/bench_moteurs.sh`), perplexité vérifiée (`tools/justesse_moteurs.sh`), notes pour l'auteur (partie Vitesse) et tableau des différences tenus. MLX n'est pas embarqué dans Gulliver (choix de l'humain, 2026-11-21 : option B, apprendre de lui) ; le relayer par son serveur reste un essai possible, pas une solution.

---

## Partie 3 — L'atelier : affiner, alléger et créer des modèles

Demande de l'humain du 2026-11-19. Les dix-sept points ci-dessous sont sa liste, rangée en phases selon ce dont chacun a besoin. Chaque point a un numéro (R1 à R17) pour le suivi.

### Ce que dlib offre déjà (point de départ)

Relevé par une recherche dans le code le 2026-11-19, **pas encore vérifié en le lançant** : la première sous-étape de chaque phase le confirme ou le corrige.

| Brique | Où | Remarque |
|---|---|---|
| Import d'un GGUF en réseau dlib | `examples/slm_gguf_import_ex.cpp` (architecture détectée dans un `model_spec`, poids vérifiés, archive `.dat`) | architectures couvertes à relever |
| Adaptateurs LoRA | `dlib/dnn/lora_adapter.h`, `examples/slm_lora_finetune_ex.cpp`, `slm_lora_adapter_check_ex.cpp` | base en flottants : pas de QLoRA trouvé |
| Fusion des adaptateurs | `merge_adapters()` (`transformer.h`), `merge_network_adapters()` (`visitors.h`) | existe |
| Masquage du prompt dans l'erreur | `dlib/dnn/loss.h` (« scores the prompt and the padding of a supervised dataset ») | à confirmer pour un jeu question/réponse |
| Distillation | `examples/slm_distill_ex.cpp` | entre architectures : à vérifier |
| Retrait de blocs | `examples/slm_tools/slm_prune_distill.py` | outil Python seulement, à porter en C++ |
| Couche à experts (MoE) | `moe_` (`dlib/dnn/transformer.h`) | entraînement de Qwen 3.6 MoE et LFM2-MoE à vérifier |
| Matrices d'entrée et de sortie partagées | « tied » dans `decoder_models.h`, `runtime_transformer.h` | à l'exécution ; à l'entraînement, à vérifier |
| Apprentissage d'un tokenizer BPE | `bpe_tokenizer::train()` (`dlib/tokenizer/bpe_tokenizer.h`) | existe |
| Préentraînement | `slm_basic_train_ex`, `slm_advanced_*_train_ex`, `slm_enwiki_train_ex`, `slm_cygnus_foundation_ex` et `slm_cygnus_instruct_ex` | existe à petite échelle |
| DPO | — | rien trouvé : à écrire |
| Calcul sur le GPU | backend Metal de dlib (phases 1 à 8, épisode 51) : `gemm`, attention et son gradient, SGD, Adam, `gguf_matvec` Q4_0 et Q8_0 | un réseau `dnn` entier ne tourne pas encore sur Metal (ses couches créent leurs tenseurs sur le processeur) |

### Phase A — Faire entrer les modèles dans dlib

| N° | Objectif | Dépend de |
|---|---|---|
| R1 | Convertir un GGUF (Qwen, Gemma, LFM2, Mistral) en réseau dlib entraînable. | — |
| R11 | Entraîner et convertir les modèles MoE (Qwen 3.6 MoE, LFM2-MoE) comme les autres. | R1 |

**Avancement de R1 (2026-11-20)** : l'import de dlib (`slm_gguf_import_ex`) tourne sur le Mac mini une fois corrigé (notes pour l'auteur n° 27) ; **Qwen3-0.6B** importé donne les log-probabilités du moteur de Gulliver (KL moyenne 4,2e-7, 25 positions, `tools/atelier/compare_dlib_import.sh`, Gulliver 6.8.2). Constaté : l'import ne connaît que `llama`, `mistral`, `gemma`, `gemma2`, `qwen2`, `qwen3` (et refuse `mixtral`, `granitemoe` : « MoE models import only through the runtime engine », R11 part donc de plus loin que prévu) ; ni Qwen 3.5, ni Gemma 3/4, ni LFM2, ni `mistral3`. Le réseau dlib est en float32 : 4 octets par paramètre (un 4B ≈ 16 Go, un 8B ≈ 32 Go). Suite : les familles manquantes de R1, dans l'ordre Gemma 3, Qwen 3.5, LFM2, Mistral. Une architecture inconnue est désormais refusée par l'import (notes n° 28 : Gemma 3 passait comme un Llama).

**Plan de Gemma 3 dans dlib (relevé du 2026-11-20)**, chaque point vérifié par `compare_dlib_import.sh` sur `gemma-3-1b-it-Q4_K_M.gguf` puis le 4B. **Points 1 à 5 faits (Gulliver 6.8.3)**, avec en plus l'échelle linéaire des positions des couches globales du 4B (facteur 8) : 1B et 4B identiques au moteur (KL 1,2e-7 et 1,1e-7). **Point 6 fait (6.8.4)** : 1B sur 1 303 tokens identique au moteur (KL 3,8e-7). Gemma 3 est terminé pour R1 ; suite : Qwen 3.5, LFM2, Mistral 3.

**LFM2 fait (Gulliver 6.10.1)**, passé avant Qwen 3.5 (décision du 2026-11-20 : il pose la pile mixte dont Qwen 3.5 aura besoin, avec une couche neuve bien plus simple que la règle delta) : couche `short_conv_` dans dlib (dérivée, état gardé pour l'écriture token par token), `hybrid_stack` choisie par un masque de bits, `short_conv_transformer_config` ; LFM2.5-1.2B identique au moteur (KL 3,2e-7). **Qwen 3.5 fait (2026-11-20, dlib seulement, Gulliver reste en 6.10.1)** : couche `gated_delta_net_` entraînable (dérivée par morceaux de 64 tokens, vérifiée contre une référence en double), porte de sortie et rotation partielle dans `gqa_attention_`, `delta_net_transformer_config` sur la pile mixte ; Qwen3.5-0.8B identique au moteur (KL 4e-8 sur 26 positions, 1,8e-9 sur 1 560). **Mistral 3 fait (2026-11-20, dlib seulement)** : Ministral 3 3B téléchargé (le 24B du Mac ferait 96 Go en float32), YaRN calculé comme llama.cpp et température d'attention dans dlib ; identique au moteur (KL 3,8e-8 sur 26 positions, 2,5e-8 sur 1 561 ; sans YaRN, comme l'outil d'origine : 2,2e-3, notes n° 30). **R1 est terminé** (épisode 59 du blog). Suite de la feuille de route : R2. Ancienne suite : Mistral 3 (le Mistral-Small-3.2-24B du Mac fait 96 Go en float32 : un plus petit Mistral 3 sera à télécharger).

1. Les normes de Gemma n'ont pas besoin de « (1+w) » : la conversion de llama.cpp ajoute déjà 1 aux poids (`output_norm` du 1B : moyenne 8,04). Le blocage « Gemma family needs (1+w) RMSNorm » se lève pour `gemma3` en lisant les poids tels quels.
2. Entrée multipliée par √d (une couche d'échelle fixe après `embeddings`).
3. GeGLU : variante de `swiglu` avec GELU (approximation tanh) à la place de SiLU.
4. Deux normes de plus par bloc (`post_attention_norm`, `post_ffw_norm`), avant chaque addition résiduelle : un nouveau bloc dans `gqa_transformer_unified` ; l'ordre de visite du chargeur suit.
5. Base RoPE par couche (10 000 pour les couches locales, 1 000 000 pour une sur six) : `set_rope_theta_base` est aujourd'hui appliqué à toutes les couches d'attention d'un coup.
6. Fenêtre glissante de 512 positions dans `gqa_attention` (passe avant, passe arrière, mode incrémental) : sans effet sous 512 tokens, donc vérifiable en deux temps (prompt court, puis long).

Critère : le réseau converti donne les mêmes logits que le moteur de Gulliver sur le même prompt (à une tolérance près, notée), pour chaque architecture.

### Phase B — Affiner

| N° | Objectif | Dépend de |
|---|---|---|
| R2 | Affiner un modèle en QLoRA sur tes propres textes, avec sa base compressée en mémoire, jusqu'à un 4B ou un 8B sur le Mac. | R1 |
| R3 | Affiner avec masquage du prompt : le modèle apprend la réponse, pas la question. | R2 |
| R4 | Lui apprendre tes préférences en DPO (« cette réponse plutôt que celle-là »), sans charger un second modèle. | R2 |
| R5 | Fusionner les adaptateurs dans le modèle, ou les garder à part, légers et interchangeables. | R2 |

Critère : sur un petit jeu d'essai, l'erreur baisse, et la mémoire prise par un 4B en QLoRA est mesurée (avec la base compressée, elle doit rester loin des 64 Go).

### Phase C — Retour dans Gulliver

| N° | Objectif | Dépend de |
|---|---|---|
| R6 | Recharger le modèle affiné dans Gulliver et discuter avec lui tout de suite. | R5 |
| R7 | Garder plusieurs adaptateurs sur une même base (code, écriture, ton style) et passer de l'un à l'autre. | R5, R6 |

**Décisions de l'humain (2026-11-19)** :

1. **Les deux chemins, dans cet ordre.**
   - **D'abord le GGUF réécrit (R6), en Q8_0** : on en aura besoin pour les phases D et E (un modèle distillé, aminci ou maison a changé de forme : ce n'est pas un adaptateur) ; il ne demande aucun changement du moteur ; il permet de discuter vite avec un premier modèle affiné, ce qui vérifie toute la chaîne d'un bout à l'autre. Q8_0 plutôt que Q4 : l'arrondi refait après la fusion d'un adaptateur appris en QLoRA déplace un peu ce qu'il a appris ; en Q8_0 la perte est faible (un 4B pèserait environ 4,3 Go, estimation).
   - **Ensuite l'adaptateur au chargement (R7)** : sur le processeur d'abord (un seul endroit à toucher : `cpu_runtime::product()`, `engine/cpu_runtime.cpp`), puis sur le GPU (8 points d'appel dans `cpu_runtime_metal.cpp`, plus les noyaux, et le contrôle du chargement GPU contre processeur), avec une mesure de vitesse à chaque étape. Pièges notés : le contexte déjà calculé d'une discussion (cache des clés et valeurs, points de reprise de Qwen 3.5) dépend de l'adaptateur et doit lui être rattaché ; pour les modèles à experts, un adaptateur par expert est plus compliqué que la fusion. Atout : plusieurs discussions en même temps, chacune avec son adaptateur, sur une seule base.
2. **Pleine précision pour l'original, compression pour la copie servie.** L'original affiné est gardé en pleine précision (archive `.dat` de dlib) ; seule la copie servie par Gulliver est compressée, et toujours depuis cet original, jamais depuis une copie déjà compressée. Conversion (changer de format) et quantification (arrondir les poids) sont deux opérations distinctes.
3. **Pas de format d'adaptateur maison.** On reprend l'un des deux formats répandus : celui de Hugging Face (PEFT : `adapter_model.safetensors` et `adapter_config.json`) ou le GGUF d'adaptateur de llama.cpp. Le choix entre les deux se fait au début de R7.

Rappel : un GGUF n'est pas universel. Il se lit dans llama.cpp et les outils bâtis dessus (LM Studio, Ollama…) et dans Gulliver ; PyTorch et Transformers travaillent plutôt en `.safetensors`. Un export `.safetensors` n'est pas dans la liste ; il s'ajoutera si l'on veut partager un modèle avec le monde Python.

### Phase D — Alléger

| N° | Objectif | Dépend de |
|---|---|---|
| R8 | Distiller un gros modèle dans un petit, même d'une autre architecture. | R1 |
| R9 | Retirer des blocs entiers d'un modèle, choisis par la mesure, puis réparer le reste. | R1, R8 |
| R13 | Partager la matrice d'entrée et celle de sortie pour alléger un petit modèle. | R1 |
| R12 | Distiller vers un élève MoE et le servir avec les mêmes outils. | R8, R11 |
| R10 | Faire servir ces modèles allégés par Gulliver, plus vite et avec moins de mémoire. | R6, R8 à R13 |

Critère : chaque modèle allégé a sa vitesse et sa mémoire mesurées dans Gulliver (mesures alternées), et sa qualité comparée au modèle d'origine sur un même jeu d'essai.

### Phase E — Nos propres modèles

| N° | Objectif | Dépend de |
|---|---|---|
| R14 | Entraîner notre propre tokenizer, avec un vocabulaire taillé pour le français. | — |
| R15 | Préentraîner un petit modèle maison sur le Mac mini M5 Pro, puis l'instruire et le mesurer. | R14, R3, R6 |

Critère : le tokenizer découpe un texte français en moins de tokens qu'un tokenizer de Qwen (compté sur un même corpus) ; le petit modèle est servi par Gulliver et mesuré.

### Phase F — La page « Atelier » de Gulliver

| N° | Objectif | Dépend de |
|---|---|---|
| R16 | Tout piloter depuis une page « Atelier » de Gulliver : modèle, jeu de données, réglages, courbe d'apprentissage en direct, depuis le Mac ou le téléphone. | — (grandit avec chaque phase) |
| R17 | Avoir une fiche pour chaque réglage (hibou, marmotte, abeille). | R16 |

La page se construit **au fil des phases**, pas à la fin : chaque fonction de l'atelier arrive avec son bouton et ses fiches, selon la règle des réglages de CLAUDE.md. Comme le reste de Gulliver, le calcul se fait dans le service, jamais dans le navigateur.

### Règles pour toute la partie 3

- Une sous-étape à la fois : elle compile, elle est testée, elle est commitée et poussée, avec l'avancement à jour.
- Le code d'entraînement est repris de dlib dans Gulliver au fil des phases (voir le fil rouge) ; un changement fait dans `dlib/` lui-même garde sa ligne au registre des notes pour l'auteur et sa mention au blog.
- Chaque gain de vitesse ou de mémoire vient d'une mesure alternée sur le Mac mini.
- Les commentaires du code sont en français ; l'interface aussi.
