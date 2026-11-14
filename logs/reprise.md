# Reprise — portage Mac de Dlib-Transformer-extensions

> À lire en premier à chaque session (règle de CLAUDE.md). Dernière mise à jour : 2026-11-14 vers 22 h 30, sur le Mac M2. Ce fichier va **du plus récent au plus ancien** : l'état actuel, ce qui est en cours et à faire, puis ce qui est fait et les décisions (antéchronologiques), les problèmes ouverts et les commandes. Le détail de chaque étape est dans `logs/journal-modifications.md`.

## État actuel

- `main` = **Albireo 4.26.1** ; Pseudalis 1.0.5 avec les corrections du fork. Plus aucune condition de plateforme dans Albireo et Pseudalis (seul un `#error` dans `qkernels.cpp` si l'on compile sans les instructions de la puce Apple) ; dlib garde les siennes.
- **Vrai service** : tourne en **4.25.0** (relancé le 2026-11-14 à 21 h 01, port 18443, réseau 8444 sans mot de passe, journal `build/albireo-home/logs/console.log`) ; à relancer en 4.26.0 si l'humain le veut (Gemma 4 y écrirait sur le GPU).
- **Tests** : `test_macos.sh` d'Albireo 53 sur 53, de Pseudalis 34 sur 34 ; banc de la page 113 vérifications ; `test_metal` passe ; 0 avertissement de compilation (Albireo, Pseudalis, exemples de dlib).
- **Blog** : épisode 41 ; **notes pour l'auteur** : changement 22, registre de dlib (partie 5).
- **Le M2 reste en mode économie d'énergie** (choix de l'humain) : aucune mesure de vitesse ; celles de 4.21.0 à 4.26.0 se feront sur le **Mac mini M5 Pro (64 Go), attendu vers le 2026-11-27** : suivre `docs/mac-mini-premier-jour.md`.
- **Original** : détaché depuis le 2026-11-11, plus de fusion ; lu à `53ddfe4` (Albireo 1.0.20) pour la reprise à la main du 2026-11-14.
- **Site des documents** : `macos/site-docs/serve.py`, port 8650 ; mot-clé « sitelog » (le lancer et donner l'URL).

## Objectif

Faire tourner le projet sur Mac Apple Silicon, et seulement là : dlib et les exemples SLM, puis le moteur et le service d'Albireo et de Pseudalis, en « pur Apple » (NEON, Accelerate, Metal).

## En cours

**Gemma 4 sur le GPU, étape B : les prompts.** L'étape A est faite (4.25.1 et 4.26.0 : les étapes d'écriture sur le GPU). Les prompts de Gemma 4 restent sur le processeur (`gpu_state::steps_only`) : `attention_mtile` demanderait 64 Ko de mémoire de threadgroup pour des têtes de 512, le M2 en a 32. Pistes : tuiles de clés de 8 lignes au lieu de 32, ou tête coupée en deux moitiés de 256 ; les couches à fenêtre (têtes de 256) pourraient déjà passer par les tuiles existantes. Modèle d'essai `build/models/gemma-4-E2B-it-Q4_K_M.gguf` : 35 couches, largeur 1 536, 8 têtes de requête pour 1 de clés, têtes de 512 (7 couches globales) et de 256 (28 couches à fenêtre de 512), 20 couches à clés partagées (à partir de la 15), entrées par couche de 256, FFN de 6 144 puis 12 288, plafond des logits 30. Pour comparer au processeur : le forcer en produits flottants (comme le contrôle), sinon ses activations arrondies sur 8 bits le font diverger tôt sur ce modèle.

## À faire, dans l'ordre proposé

1. Étape B de Gemma 4 (ci-dessus).
2. Images de Gemma 3.
3. Reste de Gemma 4 : le 26B-A4B (mélange d'experts à expert dense parallèle, routeur à norme propre) ; E4B, 12B, 31B jamais essayés ; l'optimisation de llama.cpp (les couches à clés partagées ne calculent que les lignes dont on veut les logits).
4. Reprises de l'original non choisies le 2026-11-14 : boutons Agent, Web et Raisonnement selon les capacités vérifiées de chaque modèle ; Mistral Small 3.2 (son format, ses outils) ; plusieurs tokens par pas (brouillons) ; Pseudalis 1.0.6 à 1.0.11.
5. Sur le Mac mini : `tools/bench_macos.sh` en premier ; trancher le mélange d'experts sur le GPU ou le processeur (sur le M2, à peu près à égalité) ; Qwen3.6-35B-A3B (UD-Q4_K_M, 22,1 Go) et projecteurs de qwen35moe ; qwen3moe, mixtral, granitemoe sur le GPU (admis, jamais essayés) ; noyau Q4_K du GPU (54 Go/s contre 70 au plafond) ; relancer la sonde Metal ; régler `recommended` de `service.js` d'après les mesures.
6. Reste de Qwen 3.5 : une erreur pendant l'exécution d'une étape sur le GPU (analyse du 2026-11-13 : le moteur devrait relire la séquence depuis un point sûr).
7. Plus tard, pas à pas, à la demande de l'humain (accord de principe du 2026-11-11) : déplacer la logique de la page vers le service (conversations et documents gardés par le service et partagés entre appareils ; lecture et recherche des documents ; éventuellement assemblage des questions, mémoire et agent).
8. Plus tard : noyaux NEON de génération pour Q8_0, Q5_K… ; essai de la page de Pseudalis par l'humain ; `dtest` complet et quelques exemples `slm_*` ; s'éloigner de dlib (décision du 2026-11-11).
9. Pour l'humain, images du Qwen3.5-4B déjà installé : onglet Modèles, chercher « unsloth/Qwen3.5-4B-GGUF », cocher « Avec la lecture des images » et ajouter de nouveau le fichier Q4_K_M (seul le projecteur, 672 Mo, est téléchargé ; essayé sur le service d'essai, pas sur le vrai).

## Fait, du plus récent au plus ancien

### 2026-11-14

- **Albireo 4.26.1 : conditions de plateforme retirées du moteur processeur** (`ALBIREO_QK_NEON`, `ALBIREO_QK_ACCELERATE`, `__APPLE__`) : NEON et Accelerate sans condition, versions portables de l'attention et de l'encodeur retirées, commentaires x86 corrigés ; 244 lignes retirées ; `test_macos.sh` 53 sur 53.
- **Albireo 4.26.0 : Gemma 4 écrit sur le GPU** (étapes A2 à A7) : dimensions par couche, couches à clés partagées, norme des valeurs, entrées par couche, échelle de sortie, plafond des logits ; échelles 8 bits à leur place réelle (`kv_layout::scales`) pour les têtes de 256 dans un cache de 512 ; prompts sur le processeur. Contrôle au chargement à 4e-5 % ; 150 tokens identiques au processeur en floats sur un journal de 2 622 tokens. `test_macos.sh` 53 sur 53. Blog : épisode 41 ; notes : changement 22.
- **Albireo 4.25.1** : noyaux d'écriture du GPU à 512 valeurs par tête (`attention_part` par tranches de 128, `kv_store`, `restore_outputs`) ; `test_metal` en 512.
- Règle de CLAUDE.md : **tout en français**, y compris les messages intermédiaires (demande de l'humain).
- Vrai service relancé en 4.25.0 ; mémo corrigé (SIGINT arrête le service depuis la 4.22.1).
- **Albireo 4.23.0 à 4.25.0 : sept nouveautés reprises à la main de l'original** (1.0.14 à 1.0.17) : garde-fou de l'agent contre les appels répétés, « Tout supprimer » qui efface aussi la mémoire, le nom et les instructions (4.23.0) ; limite de réflexion par défaut, réparation des appels aux guillemets non échappés (`stray_quotes`), manifeste Qwen3-VL 4B Instruct (4.24.0) ; barre de contexte, fenêtre égale au contexte du modèle, champs markdown `mdfield.js` (4.25.0). Banc de la page : attend le chargement du modèle. Blog : épisode 40 ; tableau des différences, partie 5.
- **Albireo 4.22.1 : défauts n° 1, 2, 4 et 5 des notes pour l'auteur corrigés dans le fork** : `bench_runtime` borné à son prompt (prompt vide et zéro étape refusés) ; lecteur de Pseudalis avec `scans`/`idle`, test sans `sleep` fixes, attente de l'archive à 300 s ; SIGINT d'un service lancé en arrière-plan ; 0 avertissement (dlib et exemples corrigés). dlib devient modifiable, avec trace au blog et au registre des notes (partie 5). Pseudalis 34 sur 34. Blog : épisode 39.
- **Site des documents** pour le téléphone (`macos/site-docs/serve.py`, port 8650) et mot-clé « sitelog ».
- **Albireo 4.22.0 : Gemma 3 sur le GPU** (fenêtre glissante dans les noyaux d'attention, `gelu_mul`, `add_rows` ; le 1B, de largeur 1 152, reste sur le processeur). Blog : épisode 38.
- **Albireo 4.21.0 : LFM2-MoE sur le GPU** (`moe_route` sigmoïde, `moe_layer()` ; contrôle à 25 % pour un mélange d'experts en 8 bits).
- **Albireo 4.20.0 : LFM2 dense sur le GPU** (noyau `short_conv` ; 1.2B Q8_0 : lecture 287-296 → 433-462 tokens/s, écriture 35-36 → 40, mesuré avant le mode économie d'énergie).
- **Albireo 4.19.0 : Gemma 3 sur le processeur**, par le chemin de Gemma 4 (format `gemma3` ; comparé à llama.cpp, de préférence avec `--kv f16`). Blog : épisode 37.
- **Albireo 4.18.0 : outils de LFM2** (`call_style::python`, rendus comme les trois gabarits, comparés à llama.cpp).
- **Albireo 4.17.1 : les valeurs 8 bits tournées comme les clés**, sur le processeur et le GPU (`qk_unrotate_row`, `restore_outputs`), comme llama.cpp ; notes : remarque 22.
- **Albireo 4.17.0 : LFM2 et LFM2-MoE sur le processeur** (convolutions courtes, routage sigmoïde, format `lfm2` ; comparés à llama.cpp 0.9.0).
- **Albireo 4.16.0 : images de Gemma 4** sur le processeur (projecteur `gemma4v`, comparé à llama.cpp).
- **Albireo 4.15.0 : outils de Gemma 4** sur le processeur (notation `<|tool_call>`, rendus comparés à Jinja2 et à llama.cpp).
- Service relancé en 4.14.0 ; `dtest --test_tokenizer` : 516 vérifications passent (build `build/macos-arm64-tests`, sans FFmpeg).

### 2026-11-13

- **Albireo 4.14.0 : Gemma 4 dense sur le processeur** (tokenizer `kind::spm_bpe` dans dlib, dimensions par couche, fenêtre, clés partagées, entrées par couche, plafond des logits ; comparé à llama.cpp). Blog : épisode 34.
- Albireo 4.13.2 (fiche des prompts GPU), 4.13.1 (état d'une séquence rendu quand le GPU échoue au milieu d'un prompt), **4.13.0 : images de Qwen 3.5** (épisode 33), **4.12.1 : outils XML de Qwen 3.5**, **4.12.0 : prompts des mélanges d'experts sur le GPU** (qwen35moe, 512 tokens 107-112 → 196-198 tokens/s ; épisode 32), 4.11.12 (licences des programmes, panneau fermé sur téléphone).

### 2026-11-12

- Banc d'essai de la page rangé dans le dépôt (`tools/page_test`).
- **Albireo 4.11.1 à 4.11.11 : la page rangée** : code mort et restes de Windows retirés, `app.js` 7 274 → 156 lignes en 22 modules, sans changement visible.
- **Albireo 4.10.0 et 4.11.0 : attention des prompts en tuiles, puis par produits de matrices 8 × 8 sur le GPU** (2B, 4 096 tokens : 424 contre 170-177 tokens/s sur le processeur ; limite des prompts GPU portée à 262 144 positions). Blog : épisode 30.
- `tools/bench_macos.sh` : toutes les mesures en une commande.
- **Qwen 3.5 et 3.6** : processeur (4.4.0, épisode 26), écriture sur le GPU (4.5.0, 4B 11,7-12,2 → 19,5 tokens/s, épisode 27), mélange d'experts sur le processeur (4.6.0) puis sur le GPU (4.7.0, épisode 28), points de reprise de l'état (4.8.0), prompts sur le GPU (4.9.0, 86 → 148 tokens/s, produits MPS de moins de 32 lignes corrigés, épisode 29).
- Tableau des différences avec Windows créé (`docs/differences-mac-windows.md`).
- **Réserve des modèles** : clones APFS, SHA-256 par CommonCrypto, plafond de 200 Go (4.2.0) ; réponses de Hugging Face gardées, anciens modèles rangés (4.3.0, épisode 25).
- **Retrait des clés et réserve hors ligne**, écrits sous Windows (branche `bureau-2026-11-12`, Albireo 4.0.0 à 4.1.0), fusionnés et vérifiés sur le Mac ; ACL de macOS dans les contrôles de dossiers (4.1.1) ; branches `bureau-2026-11-12` et `travail-windows` supprimées. Blog : épisode 24.

### 2026-11-11

- **Albireo 3.2.0** : tous les GGUF de Hugging Face ; **3.1.0** et 3.1.1 : onglet « Modèles », un modèle pour tout ; **3.0.0** : lecture à voix haute retirée ; 2.0.1 et 2.0.2 ; **2.0.0** : numérotation du fork, notes de version, français seulement (Pseudalis : anglais et français).
- Documents pris dans un dossier du Mac ; réglages du service depuis la page (onglet « Service ») et accès depuis le réseau de la maison (port 8444) ; GPU montré dans la page.
- **Moteur Metal** : GPU ouvert dans le moteur (M1), noyaux du pas d'écriture testés (M2), **pas d'écriture sur le GPU** (M3, M4 en partie : 4B 18,3-19,0 → 21,3-21,7 tokens/s ; épisode 19), contrôle au chargement, **prompts sur le GPU** (M5 : 4B 512 tokens 92 → 154 tokens/s ; épisode 20) ; cache des clés et valeurs en une zone réservée.
- **« Du pur Apple »** : code de Windows, de Linux, des x86 et de CUDA retiré d'Albireo et de Pseudalis, mises à jour par installateur retirées, tests Linux remplacés par `test_macos.sh`. Blog : épisode 18. Repère de vitesse en tête de chaque épisode.
- **Fork détaché de l'original** ; sonde Metal (`macos/metal-sonde/`, épisode 17).

### 2026-11-09

- **Écriture sur les cœurs de performance** (thread pool à 4 threads sous 24 tokens) : 4B 17,6-18,1 → 18,2-18,6 tokens/s. Blog : épisode 16.
- **Attention NEON pour les clés et valeurs en 8 bits** : prompt de 4 096 tokens 37 → 158-162 tokens/s après la fusion. Blog : épisode 15.
- Fusion de l'original : Albireo 1.0.12, Pseudalis 1.0.5 (`535a6fb`, dernière fusion).
- Sommes des noyaux Q4_K et Q6_K gardées en vecteurs : 4B 16,5-17,0 → 18,0-18,1 tokens/s.

### 2026-11-08

- Fusion de l'original : Pseudalis 1.0.4 et 1.0.5 ; Pseudalis 34 sur 34.
- **Attention de l'encodeur d'images par Accelerate** : image 512 × 512 6,7-8,5 s → 1,8-2,2 s. Blog : épisode 14.
- Notes pour l'auteur : code avant et après pour les erreurs qui touchent aussi Windows, ligne « Mac ou Windows ».
- **Produits des prompts par Accelerate** : 2B 512 tokens 64-72 → 217 tokens/s. Blog : épisode 13.
- **Notes pour l'auteur** créées. **Pseudalis à 28 sur 28** (chemin du programme). Blog : épisode 12.
- Second essai du PDF dans la page.

### 2026-11-07

- **Attention NEON** : 4 096 tokens, prompt 25 → 51 tokens/s, génération 8 → 21. Blog : épisode 11.
- Page web essayée par l'humain (conversation, résumé d'un PDF de l'ANSSI).
- **Service d'Albireo lancé sur le Mac** (chemin du programme par `_NSGetExecutablePath`, mémoire par `sysctl`). Blog : épisode 10.
- Dernier commit de l'original fusionné (`8743fcd`).
- **Produit NEON en floats pour le prompt** : 5-13 → 35-50 tokens/s. Blog : épisode 9.
- **Noyaux NEON Q4_K et Q6_K** : génération 11 → 33-37 tokens/s, exacts au bit près. Blog : épisode 8.

### 2026-11-06

- Le « moteur sur un seul cœur » venait du mode économie d'énergie. Blog : épisode 7.
- **Albireo répond sur le Mac** (Qwen3-VL 2B). Blog : épisode 6.
- **Albireo et Pseudalis compilent sur ARM64** (5 causes d'échec corrigées). Blog : épisode 5.
- Épisodes 1 à 3 repris du carnet TortueTech ; fork déménagé sur `Aughra` (épisode 4) ; 10 commits de l'original fusionnés ; règles du fork et documents de suivi créés.

### 2026-11-05

- Fusion de l'original et première compilation sur Mac : dlib et 24 exemples `slm_*` en Mach-O arm64 ; 5 causes d'échec relevées pour Albireo et Pseudalis.

## Décisions, de la plus récente à la plus ancienne

- 2026-11-14 : tout se dit en français, y compris les messages intermédiaires (règle de CLAUDE.md).
- 2026-11-14 : le fichier de reprise va du plus récent au plus ancien.
- 2026-11-14 : dlib peut être modifié, chaque changement laissant une trace au blog et au registre des notes pour l'auteur.
- 2026-11-14 : reprises de l'original retenues : n° 1, 2, 3, 4, 7, 8 et 11 de la liste (faites).
- 2026-11-14 : le M2 reste en mode économie d'énergie ; les mesures de vitesse se feront sur le Mac mini.
- 2026-11-14 : le GPU pour LFM2 et les Gemma (après LFM2 et LFM2-MoE sur le processeur).
- 2026-11-12 : jusqu'au Mac mini, seulement ce qui sert aux deux Mac (justesse, algorithmes), pas de réglages fins de vitesse propres au M2.
- 2026-11-12 : plan A (réserve), B (architectures : qwen35, puis qwen35moe, puis gemma4), C (attention des prompts en tuiles), D (ranger la page), dans l'ordre ; une sous-étape = une version testée, commitée et poussée.
- 2026-11-11 : fork détaché de l'original ; « du pur Apple » dans Albireo et Pseudalis ; moteur Metal complet, Albireo d'abord ; numérotation propre à partir de 2.0.0 ; français seulement.
- 2026-11-08 : les produits des prompts passent par Accelerate (option C) ; les noyaux de génération et l'attention restent exacts au bit près.
- Depuis le début : Mac Apple Silicon uniquement.

## Problèmes ouverts

- **Sécurité, choix assumé** (2026-11-11) : sur le Mac, le service n'a ni jeton d'administration ni contrôle `Host` / `Origin` (un site piégé ouvert dans un navigateur du Mac pourrait le piloter) ; l'accès réseau (port 8444, http) est **sans mot de passe** à la demande de l'humain, et le trafic passe en clair. À remettre si l'usage change ; le risque est écrit dans les notes de version.
- **Gemma 4** : sur le processeur dans son mode habituel, les activations arrondies sur 8 bits le font diverger tôt du calcul exact (le GPU en est plus proche) ; sur `tests/lfm2/long_prompt.txt`, le E2B prend l'enregistrement 3 pour celui d'Inès (erreur du modèle, des deux côtés). Défaut de son gabarit, reproduit tel quel : une réponse avec du texte **et** des appels voit son texte placé après les résultats.
- Le vrai service a pour modèle installé Qwen3-VL 4B **Thinking** (le catalogue désigne désormais Instruct pour une installation neuve) : il raisonne longtemps avant de répondre.
- Un service lancé en arrière-plan par Claude s'arrête avec la session de Claude : le lancer détaché (commande ci-dessous).
- L'ancien fork `PapyTortueTech/Dlib-Transformer-extensions` n'est plus utilisé : ne jamais y pousser.
- `bench_runtime` range les clés en demi-précision, sauf avec `q8` en 6ᵉ argument ; le service les range en 8 bits : mesurer avec `q8`.

## Commandes pour reprendre

```sh
cd ~/dev/Dlib-Transformer-extensions
git status -sb && git fetch upstream && git rev-list --count 184ea00..upstream/main
cmake --build build/macos-pseudalis -- -k 0
cmake --build build/macos-albireo -- -k 0

# Tests complets ; un service déjà lancé : changer de port
ALBIREO_TEST_PORT=18473 bash dlib/dlib/external/albireo/tools/test_macos.sh
PSEUDALIS_TEST_PORT=18476 bash dlib/dlib/external/pseudalis/tools/test_macos.sh
bash dlib/dlib/external/albireo/tools/page_test/page_test.sh     # banc de la page
build/macos-albireo/tests/test_metal                               # noyaux du GPU

# Service d'Albireo (page : https://localhost:18443/ ; arrêt : Ctrl-C, kill -INT ou kill -TERM)
ALBIREO_HOME=$PWD/build/albireo-home build/macos-albireo/bin/albireo-service --console
# Lancé par Claude : détaché, sinon la fin de la session de Claude l'arrête
(ALBIREO_HOME=$PWD/build/albireo-home nohup build/macos-albireo/bin/albireo-service --console \
  > build/albireo-home/logs/console.log 2>&1 &)

# Site des documents (« sitelog »)
nohup python3 macos/site-docs/serve.py > build/site-docs.log 2>&1 &
```
