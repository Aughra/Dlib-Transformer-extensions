# Reprise — portage Mac de Dlib-Transformer-extensions

> À lire en premier à chaque session (règle de CLAUDE.md). Dernière mise à jour : 2026-11-11, sur le Mac M2.

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
- 2026-11-07 : **attention NEON** (`attention_scores_neon`, `attention_values_neon` dans `qkernels_neon.inc`), identique au bit près au code portable (0 différence sur 549 760 ; règle d'arrondi d'Apple clang reproduite : scores en produit puis addition sauf les `hd % 4` derniers termes en `fma`, valeurs en `fma`). Sur 4 096 tokens (2B, mesures alternées) : prompt 25 → 51 tokens/s, génération 8 → 21 tokens/s. Même réponse sur un résumé de 4 329 tokens. Blog : épisode 11.
- 2026-11-08 : PDF refait dans la page : la recherche n'a donné au 4B qu'environ 1 800 tokens ; écriture à 12,5 tokens/s. Pas comparable au premier essai (contexte inconnu, bien plus long).
- 2026-11-08 : **Pseudalis à 28 sur 28** (`test_linux.sh`). Correction du chemin du programme (`pseudalis/common/platform.cpp`, comme Albireo) : sans elle, la page web répond 404 quand le service est lancé hors de `bin/`. Les deux étapes qui échouaient (« drive signatures », « copies on the drive ») passent aussi sans elle : le premier passage avait très probablement été fait en mode économie, et elles attendent un délai fixe. La piste APFS (clones, renommages) était fausse. Blog : épisode 12.
- 2026-11-08 : **notes pour l'auteur** (`docs/notes-pour-l-auteur.md`, en français) : erreurs de l'original trouvées depuis le début (bugs, tests fragiles, avertissements, portabilité macOS, remarques) et vitesses avant / après. Consignes dans CLAUDE.md : le tenir à jour à chaque erreur trouvée et chaque gain.
- 2026-11-08 : **produits des prompts par Accelerate** (`engine/qkernels_accelerate.inc`, `a38c30d`), décision de l'humain (option C, fin de l'exactitude au bit près pour ces produits). `cblas_sgemm` à partir de 16 tokens, poids décompressés par tranches de 256 lignes pendant le calcul ; seuil des noyaux entiers à 24 tokens sur Mac. Mesures alternées : 2B 512 tokens 64 à 72 → 217 tokens/s, 4 096 tokens 52 à 58 → 156 à 163, modèle de recherche 184 à 213 → 573 à 601 ; génération inchangée. Tests passent. Service relancé avec ce moteur. Notes pour l'auteur : changement 4, remarque 19 (seuil), et une ligne « Mac ou Windows » par entrée (demande de l'humain, qui fera des PR pour ce qui se reproduit sous Windows ; consigne ajoutée à CLAUDE.md). Blog : épisode 13.
- 2026-11-08 : **image lue par le 4B** (zèbre et perroquet de macOS) : descriptions justes. Attention de l'encodeur d'images par Accelerate (`72091df`) : encodage d'une image 512 × 512 6,7 à 8,5 s → 1,8 à 2,2 s, même précision contre la double, descriptions identiques. Notes pour l'auteur : changement 5. Code avant / après de `bench_runtime` (entrée 1) ajouté aux notes, essayé avec AddressSanitizer (`5c81142`). Blog : épisode 14.
- 2026-11-08 : **4 commits de l'original fusionnés** (`63f4af4` : Pseudalis 1.0.4 et 1.0.5, `dlib/privacy`, exemple d'anonymisation), sans conflit ; corrections Mac de `pseudalis/CMakeLists.txt` et `host/drive_folders.cpp` intactes. Builds de Pseudalis et d'Albireo sans erreur ; `test_engine`, `test_host` et `test_linux.sh` de Pseudalis : **34 sur 34**. Service de Pseudalis relancé en 1.0.5 (page en 200). Notes pour l'auteur recalées sur `0503bcb` (numéros de ligne de `test_linux.sh` et `drive_folders.cpp`) ; deux avertissements oubliés ajoutés (`language_model_data.h:185`, `scan_image_pyramid.h:873`).
- 2026-11-09 : **noyaux Q4_K et Q6_K, sommes gardées en vecteurs** (`791a211`), exacts au bit près (0 sur 43 200 produits, vrais poids) : génération 4B 16,5–17,0 → 18,0–18,1 tokens/s, 2B 35,4–36,8 → 38,2–38,9 (mesures alternées). Rejeté : paquets de lignes plus petits en génération (plus lent). Notes pour l'auteur : changement 6.
- 2026-11-09 : **6 commits de l'original fusionnés** (`535a6fb`, Albireo 1.0.12 : clés et valeurs en 8 bits par défaut, attention de la génération répartie entre les fils). Un conflit dans `qkernels.cpp` (corps de l'attention devenu un modèle commun), résolu en gardant le corps de l'original et nos 4 lignes NEON en tête. Tests d'Albireo et de Pseudalis (34 sur 34) passent.
- 2026-11-09 : **attention NEON pour les clés et valeurs en 8 bits**, exacte au bit près (0 sur 351 780 valeurs) : sans elle, la fusion faisait tomber un prompt de 4 096 tokens de 163 à 37 tokens/s sur Mac ; avec, 158–162, génération après 4 096 tokens 9 → 22,5. Notes pour l'auteur : changement 7, remarque 20 (fils), numéros recalés sur `184ea00`. Blog : épisode 15.
- 2026-11-09 : **génération sur les cœurs de performance** (option B de l'humain) : petite équipe de 4 threads dans le thread pool sur Mac (`thread_pool_apple.inc`), prise par `forward` sous 24 tokens ; longs prompts sur les 8 threads. 4B génération 17,6–18,1 → 18,2–18,6 tokens/s, 2B 37,4–37,9 → 39,7–40,8 ; long prompt inchangé. Notes : changement 8. Blog : épisode 16. Vocabulaire : dire « thread pool » et « threads » (demande de l'humain).
- 2026-11-11 : **sonde Metal** (`macos/metal-sonde/`) : sur le M2, le GPU lit tous les poids du 4B en 45 à 48 ms contre 51 à 55 pour le processeur (plafond 35 ms, 70 Go/s) ; aller-retour 186 µs, donc rien à gagner produit par produit ni couche par couche (57 à 60 ms) ; exactitude 2,8e-8. L'estimation « écriture doublée » était fausse : 5 à 22 % aujourd'hui (un tiers sur le 2B), 1,5 fois au mieux, et seulement avec tout le token sur le GPU. Blog : épisode 17. 16 commits de l'original en attente (Albireo 1.0.13 à 1.0.15, dont prompts en 8 bits et écriture de plusieurs tokens par pas).
- 2026-11-11 : **fork détaché de l'original** (plus de fusion) et **« du pur Apple »** dans Albireo et Pseudalis : fichiers Windows, `dist/`, moteur CUDA/OpenVINO, branches `_WIN32`/x86/CUDA/Linux (`unifdef`), mises à jour par installateur Windows retirés ; CMake pour macOS seulement. Tests d'Albireo et de Pseudalis passent, `test_linux.sh` 34 sur 34, service d'Albireo vérifié. Cache des clés et valeurs en une zone réservée (`c583d29`, M1). Repère de vitesse en tête de chaque épisode du blog. dlib reste ; on s'en éloignera plus tard.
- 2026-11-11 (sieste de l'humain, choix consignés dans `logs/sieste-2026-11-11.md`) : **les pas d'écriture tournent sur le GPU** (M2, M3, M4 en partie) : 4B 21,3 à 21,7 tokens/s contre 18,3 à 19,0 sur le processeur, 2B 49,4 à 49,6 contre 35,7 à 40,8 ; mêmes réponses ; activé par défaut. Blog : épisode 19.
- 2026-11-11 (sieste) : **les prompts aussi sur le GPU** (M5) : 4B 512 tokens 92 → 154 tokens/s, 2B 222 → 420 ; à partir de 24 tokens, jusqu'à 2 048 positions. Contrôle au chargement du modèle. Bug de débordement corrigé dans l'attention. Blog : épisode 20.

## En cours

**Moteur Metal complet** (décision de l'humain, 2026-11-11 ; le fork est détaché de l'original). Plan :

| Sous-étape | Contenu | Critère vérifiable |
|---|---|---|
| N4 (avant M1, presque fait) | Fin du nettoyage (fait : `cpu_info`, `v2ram`, chargeur de Pseudalis, `test_macos.sh` ; reste : clé du flux et `pseudalis-sign`, commentaires ; `offload.h` / `layer_pass` remplacés en M3) : `offload.h` et `layer_pass` dans `cpu_runtime`, champs x86 de `cpu_info`, réglages `v2ram` / `accelerator`, chargeur de Pseudalis, clé du flux et `pseudalis-sign`, commentaires ; `test_linux.sh` → `test_macos.sh` sans `nproc` ni `gstat` | build sans avertissement ; tous les tests ; services vérifiés |
| M1 (**fait**) | Cache des clés et valeurs dans une seule zone réservée, alignée sur les pages (`c583d29` ; la zone du 4B réserve 18,7 Go d'adresses, plus que les 13,6 Go d'un tampon Metal sur le M2 : n'envelopper que les blocs utilisés) ; module `metal_device` (fait) ; accès au début de la projection du modèle ; bibliothèque `albireo_metal` (Objective-C++, noyaux compilés au lancement) liée sous `APPLE` ; le moteur s'ouvre avec le GPU | build sans avertissement ; tous les tests passent ; `engine_chat` répond juste ; le journal du moteur nomme le GPU |
| M2 (**fait**, `32312f6`) | Noyaux : produits Q4_K / Q6_K (1 à 8 tokens), normes RMS, rope, SiLU, écriture des clés et valeurs (q8 tournées, f16), attention en ligne sur les blocs, tête des logits | test `test_metal` : chaque noyau à 1e-4 près du processeur |
| M3 (**fait**, `5f0b07f`) | Un pas de génération entier sur le GPU, une commande par passe ; contrôle au chargement (logits à 1 % près, même token le plus probable) ; repli sur le processeur sinon | `engine_chat` juste ; mesures alternées processeur / GPU |
| M4 (en cours ; gabarits faits, `5f0b07f` : 4B 18,3-19,0 → 21,3-21,7 tokens/s) | Réglages : noyau Q4_K (54 Go/s contre 70 au plafond), tête des logits, contrôle au chargement du modèle (fait, `f199a04`), Q8_0 (modèle de recherche) | gain en mesures alternées |
| M5 (**fait**, `cbefd5c` : lecture ×1,7 à ×2, jusqu'à 2 048 positions) | Prompts sur le GPU (produits en tuiles, attention groupée) contre Accelerate | le plus rapide gardé, mesuré |
| M6 (**fait**, `bef3d9a`, `982c951`) | Page fidèle au moteur Metal : ligne « GPU » dans « À propos » ; onglet « Service » qui règle tout (GPU compris), avec fiches à trois explications et schémas ; plus de jeton ni de contrôle d'origine (choix de l'humain, version perso) ; accès réseau http sur le port 8444 derrière un mot de passe | page et API servies par le GPU ; réglages essayés depuis la page ; accès réseau essayé (cookie, refus) |
| M7 (proposé) | Attention de prompt en tuiles (instructions matricielles), pour lever la limite de 2 048 positions ; noyau Q4_K (54 Go/s contre 70) | gain en mesures alternées |

**Conception du pas sur le GPU (M2-M3)**, une commande par passe de 1 à 8 tokens, tout dans un seul encodeur sériel :
- tampons partagés par modèle : `x`, `xn`, `q`, `k`, `v`, `ctx`, `gate`, `up`, logits, morceaux de l'attention ; poids : la projection du fichier en un tampon sans copie (offsets des matrices) ; clés et valeurs : la zone de `kv_pool` en un tampon sans copie, budget du cache plafonné à `maxBufferLength` quand Metal est actif ;
- par passe, écrits par le processeur : `x` (plongement, comme aujourd'hui), cos/sin de chaque token (tables de `cpu_runtime`), table des blocs de chaque séquence, positions ;
- noyaux : `rms_norm` (lignes, largeur, eps), `mv_q4_K` / `mv_q6_K` pour T ≤ 8 tokens avec `y = alpha * W x + beta * y` (somme résiduelle), biais, normes des têtes (même `rms_norm`), `rope` (neox ou paires), `kv_store` (q8 : signes et Hadamard de `qk_rotate_row`, puis échelle en demi-précision comme `qk_float_to_q8_row` ; f16), préparation des requêtes (échelle, rotation en q8), attention en ligne par morceaux de positions (grille T × têtes kv × morceaux, 4 simdgroups, têtes groupées lues une fois), fusion des morceaux, `silu_mul`, norme finale et tête des logits ;
- repli sur le processeur : MoE, images (positions données, DeepStack), `attn_temp_scale`, formats autres que Q4_K / Q6_K, plus de 8 tokens (prompts : M5) ;
- contrôle : passe de sonde au chargement du modèle (`check_gpu()`, fait), logits à 1 % près de ceux du processeur en produits flottants, même token le plus probable.

Formats couverts d'abord : Q4_K et Q6_K (les modèles du service) ; les autres modèles restent sur le processeur. Priorité de l'humain : **Albireo d'abord** (vitesse), Pseudalis ensuite.

## À faire, dans l'ordre proposé

0. ~~Documents depuis un dossier du Mac, et envoi par la page~~ : fait le 2026-11-11 (dossier `<données>/documents`, trombone « Documents du Mac », copie automatique des pièces jointes). Plus tard, si l'humain le veut : rendre tout le dossier interrogeable par le modèle (index de recherche côté Mac), option écartée pour l'instant.

1. ~~Décision de l'humain attendue~~ : prise le 2026-11-11, moteur Metal complet (voir « En cours »). Options qui étaient proposées : (a) fusionner d'abord les 16 commits de l'original (1.0.13 lit les prompts en entiers de 8 bits, ce qui touche le chemin Accelerate du fork, et écrit plusieurs tokens par pas à partir de brouillons : gain possible sans Metal), puis mesurer ; (b) moteur Metal complet, tout le token sur le GPU (`layer_pass` + `attend()`), gain estimé de 5 à 50 % en écriture sur M2 (sonde aujourd'hui, puis plafond), gros chantier, non exact au bit près ; (c) sonde n° 2 avant de décider : produits des prompts sur le GPU (MPS ou `simdgroup_matrix`) contre Accelerate. Relancer la sonde sur le Mac mini M5 Pro, où le rapport GPU / processeur sera différent.
2. Plus tard, si utile : noyaux NEON de génération pour les autres formats de poids (Q8_0, Q5_K…) ; `qk_float_to_q8_row` et `qk_rotate_row` restent portables (peu de temps au profil).

Ensuite :

1. Essai de la page de Pseudalis dans un navigateur, par l'humain (service lancé comme Albireo, port 18444, `PSEUDALIS_HOME`).
2. Construire et lancer `dtest` (tests dlib), et quelques exemples `slm_*`.
3. Plus tard (décision de l'humain) : s'éloigner de dlib.

Après chaque point : mettre à jour la section « État constaté » du mémo, ce fichier et le journal, puis commiter et pousser. Une grande étape (service d'Albireo qui répond, test de Pseudalis à 28 sur 28, première mesure de vitesse) a son épisode de blog.

## Décisions

- 2026-11-08 : les produits des prompts passent par Accelerate (option C choisie par l'humain) ; ils ne sont plus identiques au bit près au code portable. Les noyaux de génération (`qk_dot`) et l'attention restent exacts au bit près.

- Mac Apple Silicon uniquement ; Windows hors du périmètre.
- Depuis le 2026-11-11, le fork ne fusionne plus l'original : ses fichiers se modifient librement, proprement, sans la règle du plus petit changement sous `#ifdef __APPLE__` (voir CLAUDE.md, « Détaché de l'original »).
- Les tests complets sont `tools/test_macos.sh` (Albireo, Pseudalis), écrits pour macOS : plus de `nproc` ni de `stat` de GNU à fournir.

## Problèmes ouverts

- **Sécurité, choix assumé** (2026-11-11) : sur le Mac, le service n'a plus de jeton d'administration ni de contrôle `Host` / `Origin`. Un site piégé ouvert dans un navigateur du Mac pourrait piloter le service (DNS rebinding). L'accès réseau est ouvert (port 8444, http, **sans mot de passe** à la demande de l'humain, « je suis chez moi ») : tout appareil du Wi-Fi peut se servir d'Albireo, et le trafic passe en clair. À remettre si l'usage change (fiche « Pas de serrure sur ce Mac » de l'onglet Service).

- L'ancien fork `PapyTortueTech/Dlib-Transformer-extensions` n'est plus utilisé ; l'humain s'occupe de l'arrêter. Ne plus jamais y pousser.
- Noyaux NEON pour Q4_K, Q6_K et l'attention (clés en demi-précision et en 8 bits) ; prompts par Accelerate pour tous les formats. En génération, les autres formats (Q8_0, Q5_K…) restent portables.
- Lancé en arrière-plan par un script, le service ignore SIGINT (le shell le lui fait ignorer, et macOS jette alors le signal) : l'arrêter par `kill -TERM`. Dans un terminal, Ctrl-C marche. Même chose attendue pour `tools/test_macos.sh` d'Albireo à l'étape « service stop ».
- Un service lancé en arrière-plan par Claude s'arrête avec la session de Claude (« service stopped » à 22 h 22 le 2026-11-07, pendant le second essai du PDF, dont les chiffres sont perdus) : le lancer détaché (commande ci-dessous).
- Le modèle par défaut est un modèle « Thinking » : il raisonne longtemps (en anglais) avant de répondre ; prévoir un `max_tokens` large ou `"reasoning": false`.
- `bench_runtime` exige un prompt d'au moins 64 tokens (sinon il lit hors de son tableau) : `bench_runtime <modèle> 4 64 16 2`. Il range les clés en demi-précision, sauf avec `q8` en 6ᵉ argument ; le service, lui, les range en 8 bits : mesurer avec `q8`.

## Commandes pour reprendre

```sh
cd ~/dev/Dlib-Transformer-extensions
git status -sb && git fetch upstream && git log --oneline main..upstream/main
cmake --build build/macos-pseudalis -- -k 0
cmake --build build/macos-albireo -- -k 0

# Tests complets (scripts pour macOS, sans outil de plus) ; un service déjà lancé : changer de port
bash dlib/dlib/external/albireo/tools/test_macos.sh      # ALBIREO_TEST_PORT=18453 si le service tourne
bash dlib/dlib/external/pseudalis/tools/test_macos.sh    # PSEUDALIS_TEST_PORT=18454 si le service tourne

# Service d'Albireo (page : https://localhost:18443/ ; arrêt : Ctrl-C, ou kill -TERM)
ALBIREO_HOME=$PWD/build/albireo-home build/macos-albireo/bin/albireo-service --console
# Lancé par Claude : détaché dans sa propre session, sinon la fin de la session de Claude l'arrête
ALBIREO_HOME=$PWD/build/albireo-home nohup python3 -c "import os,sys; os.setsid(); os.execv(sys.argv[1], sys.argv[1:])" \
  $PWD/build/macos-albireo/bin/albireo-service --console > build/albireo-service.log 2>&1 < /dev/null &
curl -sk https://localhost:18443/v1/chat/completions -H "Content-Type: application/json" \
  -d '{"model": "albireo-general", "reasoning": false, "max_tokens": 80, "messages": [{"role": "user", "content": "Bonjour"}]}'
```
