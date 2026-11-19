# Feuille de route du fork

Ce qui vient ensuite, dans l'ordre. Écrite le 2026-11-19, le jour où le Mac mini M5 Pro (64 Go) est arrivé. Ce document dit **quoi faire et dans quel ordre** ; l'avancement au jour le jour reste dans [logs/reprise.md](../logs/reprise.md), et le détail de chaque étape dans le journal.

| Partie | Sujet | État |
|---|---|---|
| 1 | Passer sur le Mac mini, mesurer, régler | à faire (machine en préparation) |
| 2 | Le reste en attente du Mac mini | à faire après la partie 1 |
| 3 | L'atelier : affiner, alléger et créer des modèles | à faire après la partie 2 |

---

## Partie 1 — Le Mac mini

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

- Gemma 4 26B-A4B sur le GPU (selon la mesure de la partie 1) ; Gemma 4 E4B, 12B, 31B jamais essayés.
- Qwen3.6-35B-A3B et ses projecteurs ; qwen3moe, mixtral, granitemoe sur le GPU (admis, jamais lancés) ; noyau Q4_K du GPU (54 Go/s contre 70 au plafond).
- Mesures de la recherche documentaire en C++ contre l'ancienne en JS ; reranker neuronal si le reranker par mots ne suffit pas.
- Plusieurs tokens par pas (brouillons), repris de l'original, s'il reste du temps.

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
- Chaque changement de dlib a sa ligne au registre des notes pour l'auteur et sa mention au blog.
- Chaque gain de vitesse ou de mémoire vient d'une mesure alternée sur le Mac mini.
- Les commentaires du code sont en français ; l'interface aussi.
