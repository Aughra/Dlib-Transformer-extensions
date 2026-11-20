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
| Lecture des ZIP, enveloppe de sqlite | `zip/`, `sqlite/` | petites |
| Lecture des GGUF | `data_io/gguf_reader.h`, `gguf_dequantize.h`, `gguf_model_spec.h` | 1 411 lignes |
| Tokenizer | `tokenizer/hf_tokenizer.h` | 1 166 lignes |

### L'ordre, un morceau par fin de chantier

| Ordre | Morceau | Où il va | Remarque |
|---|---|---|---|
| 1 | ~~SHA-256 → CommonCrypto d'Apple~~ **fait (6.4.1)** | `host/`, `engine/` | Le service s'en servait déjà (4.2.0) ; restait l'empreinte des images du moteur |
| 2 | ~~Bibliothèques d'autres projets~~ **fait (6.8.1)** | `gulliver/third_party/` | aucune ligne changée ; le build de Gulliver ne construit plus dlib (copiées et non déplacées : dlib et Pseudalis s'en servent encore) |
| 3 | Lecture des ZIP, enveloppe de sqlite | `host/` | |
| 4 | Lecture des GGUF | `engine/` | premières optimisations |
| 5 | Tokenizer | là où le moteur et le service le partagent | optimisable ensuite, notamment pour le français |

Après le morceau 5, Gulliver (service et moteur) ne dépend plus de dlib.

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

**Mesures de vitesse arrêtées (décision de l'humain, 2026-11-20 : « arrêt les tests de perfs, et continue la roadmap »)** : la comparaison avec Ollama et MLX, le découpage de l'attention des longues questions, plusieurs tokens par pas et les mesures de la recherche documentaire sont mis de côté. On passe au morceau 2 du fil rouge, puis à la partie 3. Gemma 4 26B-A4B sur le GPU reste ici, à reprendre plus tard.


- Gemma 4 26B-A4B sur le GPU (mesure de la partie 1 : 126-129 tokens/s en lecture, 27 en écriture, sur le processeur seul) ; Gemma 4 E4B, 12B, 31B jamais essayés.
- ~~Qwen3.6-35B-A3B et ses projecteurs~~ **fait (6.5.0, 2026-11-19)** : sur le GPU, images identiques à llama.cpp ; ~~qwen3moe, mixtral, granitemoe sur le GPU (admis, jamais lancés)~~ **essayés et validés contre llama.cpp, prompts sur le GPU, format de Mixtral corrigé (6.6.0)** ; ~~noyau Q4_K du GPU (54 Go/s contre 70 au plafond)~~ **mesuré sur le Mac mini (2026-11-19) : 218 Go/s pour un plafond de 260, 84 à 86 % ; laissé tel quel** (marge d'environ 15 %, voir `macos/metal-sonde/README.md`).
- Mesures de la recherche documentaire en C++ contre l'ancienne en JS ; reranker neuronal si le reranker par mots ne suffit pas.
- Plusieurs tokens par pas (brouillons), repris de l'original, s'il reste du temps.
- **Mesure faite pour llama.cpp (2026-11-20, `docs/mesures/2026-11-20-gulliver-contre-llama-cpp.md`) : llama.cpp lit les prompts environ 3 fois plus vite et écrit 1,3 à 1,8 fois plus vite ; Ollama et MLX à installer si l'humain le veut.** Comparer Gulliver à d'autres moteurs locaux sur les longues questions (demande de l'humain, 2026-11-19) : llama.cpp (déjà installé), Ollama, et éventuellement un moteur MLX, sur le même modèle et le même fichier GGUF, prompts de 512 à 16 384 tokens, lecture et écriture, en alternance. Critère : un tableau « temps avant le premier mot » par moteur, dans les notes pour l'auteur et le tableau des différences. Raison : un retour d'usage lu par l'humain (Ollama abandonné parce que les longues requêtes « prenaient une éternité »).

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
