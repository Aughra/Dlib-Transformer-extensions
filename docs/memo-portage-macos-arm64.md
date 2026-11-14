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

Albireo, lui, n'utilise pas ce détecteur : il lie Accelerate directement sur Mac (correction 10 ci-dessous). Ce paragraphe ne concerne que dlib.

Avant de valider cette variante, ajouter ou confirmer une détection et un linkage explicites d'Accelerate dans CMake. Ensuite, configurer dans un nouveau répertoire avec `DLIB_USE_BLAS=ON` et `DLIB_USE_LAPACK=ON`, compiler `dtest` et comparer les tests numériques au build CPU de référence. Consigner dans la sortie CMake le framework effectivement lié.

### Codecs d'images

Dans un nouveau build, activer les options `DLIB_JPEG_SUPPORT`, `DLIB_PNG_SUPPORT`, `DLIB_GIF_SUPPORT`, `DLIB_WEBP_SUPPORT` et `DLIB_JXL_SUPPORT`, puis compiler et exécuter les tests dlib d'image. Vérifier la sortie CMake pour distinguer les bibliothèques système des implémentations embarquées : JPEG et PNG peuvent être compilés depuis les sources vendoriées si les paquets système ne sont pas trouvés. Une compilation réussie ne prouve donc pas, à elle seule, que les bibliothèques système sont utilisées.

### FFmpeg

Installer FFmpeg et `pkg-config` (par exemple avec Homebrew), puis configurer un répertoire neuf avec `DLIB_USE_FFMPEG=ON`. Le module [`find_ffmpeg.cmake`](../dlib/dlib/cmake_utils/find_ffmpeg.cmake) recherche les composants `libav*` via `pkg-config`. Confirmer leur détection dans la sortie CMake, compiler `dtest` et exécuter ses tests vidéo/audio.

## Applications Albireo et Pseudalis

Les deux applications de `dlib/dlib/external/` (Albireo : moteur GGUF et service de chat ; Pseudalis : anonymisation de documents) étaient chez l'original des programmes Windows 11. Depuis le nettoyage du 2026-11-11 (« du pur Apple », section suivante), elles ne se construisent plus que pour macOS sur Apple Silicon : leur CMake refuse toute autre plateforme. Elles ont chacune un CMake à elles, que le build des exemples ne compile pas. Le service se lance avec `--console` :

```sh
cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-albireo --parallel 3 -- -k 0

cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis --parallel 3 -- -k 0
```

`-- -k 0` fait continuer Ninja après une erreur, pour relever toutes les erreurs d'un seul passage.

### Nettoyage « du pur Apple » (2026-11-11)

Décision de l'humain : dans Albireo et Pseudalis, tout le code propre à Windows, à Linux, aux processeurs x86, à CUDA et à OpenVINO est retiré ; dlib reste tel quel. Le fork ne fusionne plus l'original (dernière fusion : Albireo 1.0.12, Pseudalis 1.0.5).

| Retiré | Albireo | Pseudalis |
|---|---|---|
| Fichiers Windows (tray, installateur, scripts `.ps1`, ressources, icônes) | oui | oui |
| `dist/` (installateurs et flux signés) | oui (racine du dépôt) | oui |
| Moteur CUDA / OpenVINO (`tensor_backend`) et ses tests | oui | sans objet |
| Branches `_WIN32`, x86 (AVX2, VNNI), CUDA, Linux (par `unifdef`) | 4 854 lignes | 2 831 lignes |
| Mises à jour par installateur Windows (`updater`, routes, réglages) | oui | oui, avec la case de la page web |
| CMake | 864 → 307 lignes, un seul moteur `cpu` | 255 → 161 lignes, moteur intégré au service |

Ensuite : `cpu_info` décrit la puce Apple (cœurs de performance et d'efficacité, extensions DotProd, I8MM, BF16, FP16) ; le réglage `v2ram` est retiré ; le chargeur de Pseudalis ne prend plus que le moteur intégré ; les scripts `test_linux.sh` sont devenus `tools/test_macos.sh` (Albireo : 19 étapes sur 19, première fois que ce test passe en entier sur Mac ; Pseudalis : 34 sur 34), sans `nproc` ni `stat` de GNU. La clé du flux et `pseudalis-sign` de Pseudalis sont retirés en 4.0.0 (écrits sous Windows sur la branche `bureau-2026-11-12`, fusionnée dans `main` le 2026-11-12, compilée et testée sur le Mac) ; des commentaires de l'ancien code restent à revoir. `offload.h` et `layer_pass` sont encore présents dans le runtime du processeur ; le chemin Metal est implémenté séparément. `accelerator` et `device_mb` sont gardés pour le GPU. Les corrections de portage listées plus bas (1 à 10) décrivent l'état d'avant ce nettoyage : plusieurs portaient sur des lignes qui n'existent plus.

### Lancer le service d'Albireo

Le service garde ses données (réglages, certificats, base, modèles) dans `$ALBIREO_HOME`. Un dossier sous `build/`, que git ignore, convient :

```sh
mkdir -p build/albireo-home
echo '{"port": 18443}' > build/albireo-home/settings.json
ALBIREO_HOME=$PWD/build/albireo-home build/macos-albireo/bin/albireo-service --console
```

Puis ouvrir `https://localhost:18443/` (certificat d'une autorité locale créée au premier démarrage, `build/albireo-home/certificates/albireo-authority.crt` : le navigateur l'avertit tant qu'on ne lui fait pas confiance). Au premier démarrage, s'il n'est pas `offline`, il télécharge son modèle par défaut (Qwen3-VL 4B Thinking Q4_K_M, 2,3 Go, et son projecteur d'images, 433 Mo) et son modèle de recherche (Qwen3 Embedding 0.6B Q8_0, 610 Mo) depuis Hugging Face, le catalogue depuis GitHub.

**Lecture à voix haute retirée (2026-11-11, version 3.0.0).** À la demande de l'humain (« ça alourdit le programme et ne sert pas ») : plus de route `/v1/audio/speech` ni `/albireo/v1/speech/voices` (404), plus de programme `albireo-speech`, de bibliothèque SVOX Pico (`dlib/dlib/external/svox_pico`, 76 fichiers, supprimée) ni de dossier `bin/speech` ; plus de bouton de lecture dans la page. Le test complet passe à 17 étapes (les trois de la voix remplacées par une qui vérifie le 404).

**Version 2.0.0 et français seulement (2026-11-11).** Le fork numérote Albireo à part : **2.0.0** (`tools/bump_version.py`, qui met à jour `common/version.h`, `project()` et `web/openapi.json`), puis un incrément à chaque modification (règle de CLAUDE.md). Notes de version : `web/changelog.md`, affichées par le lien « Notes de version » de « À propos ». L'interface et la voix sont en français seulement : `i18n.js` ne garde que le dictionnaire français (427 clés, complet), plus de choix de langue (onglet Général, carte « Langue du service »), détection de langue et exemples envoyés au modèle réduits au français ; une seule voix, `fr-FR` (le build ne copie plus que `fr-FR_ta.bin` et `fr-FR_nk0_sg.bin` dans `bin/speech`) ; le réglage `language` du service n'accepte plus que `auto` et `fr`, et le catalogue se décrit en français par défaut. Le test complet lit une phrase française au lieu d'allemande.

**Les réglages et l'accès réseau (2026-11-11, `982c951`).** Tous les réglages utiles sur Mac se changent dans l'onglet « Service » des paramètres de la page (fichier du fork `web/js/service.js`) : GPU, threads, questions traitées ensemble, contexte, mémoire et format des clés et valeurs, accès réseau, ports, hors ligne, bouton Web, recherche Wikipédia, proxy. Les réglages « en direct » partent à chaque changement ; les autres attendent « Appliquer et redémarrer » (essayé : `max_sequences` 4 → 3 → 4, redémarrage en environ deux secondes). Chaque réglage a sa fiche derrière son « ? » (principe, avantages, inconvénients, schéma SVG, explications normale, du hibou, de la marmotte et de l'abeille), en français seulement. **Plus de jeton d'administration (`admin.token`, `X-Albireo-Admin`) ni de contrôle des en-têtes `Host` et `Origin`** : sur le Mac, toute requête peut tout faire (choix de l'humain pour une version perso ; risque connu : un site piégé pourrait piloter le service par DNS rebinding). **Accès réseau** : réglage `lan` (faux par défaut), port `lan_port` (8444), mot de passe `lan_password` gardé en empreinte PBKDF2-HMAC-SHA-256 (200 000 tours, sel) dans `lan_password_hash` ; un second serveur httplib en http sur `0.0.0.0:8444` sert les mêmes routes, derrière le cookie `albireo_lan` quand un mot de passe est choisi (voir plus bas), (nonce et HMAC de l'empreinte, 30 jours, `HttpOnly`, `SameSite=Strict`) que pose `POST /albireo/v1/lan/login`. Vérifié depuis l'adresse du Mac : page de mot de passe sans cookie, 401 pour l'API, mot de passe faux refusé en 1,2 s, bon mot de passe → cookie, page et API servies ; faux cookie refusé ; l'accès local inchangé. Sur le téléphone, l'adresse est `http://<adresse du Mac>:8444/` (l'onglet l'affiche). En http, pas de dictée au micro, et la copie passe par une zone de texte sélectionnée (`execCommand`). Chaque appareil garde ses propres conversations (rangées dans son navigateur).

**Mot de passe réseau facultatif, documents du Mac, plus de réglages (2026-11-11).** Choix de l'humain (« je suis chez moi ») : le mot de passe de l'accès réseau est facultatif ; vide, la porte réseau est ouverte à tout appareil du Wi-Fi (le service l'écrit dans son journal au démarrage). L'accès réseau est ouvert sur le Mac de l'humain, sans mot de passe. **Documents du Mac** : dossier `<données>/documents` (ici `build/albireo-home/documents`), routes `GET/POST /albireo/v1/documents`, `GET/DELETE /albireo/v1/documents/<nom>` (nom simple, ni dossier ni fichier caché ; 512 Mo au plus ; même contenu sous le même nom gardé une fois, autre contenu renommé « nom (2).ext »). Dans la page, le trombone (conversation, modification d'un message, projet) propose « Depuis cet appareil » ou « Documents du Mac » (liste à cocher, envoi vers le Mac, suppression) ; tout fichier joint est aussi copié dans ce dossier, sauf ceux qui en viennent. Vérifié par l'accès réseau : liste, envoi, doublon, renommage, nom piégé refusé (400), lecture piégée refusée (404), pièce jointe prise dans le dossier (Chrome sans fenêtre, largeur de téléphone). **Réglages ajoutés à l'onglet** : seuils des prompts sur le GPU (`gpu_prompt_tokens`, 24 ; `gpu_prompt_context`, 2 048, 0 pour jamais ; essayé à 4 096 : le journal du contrôle dit « up to 4096 positions »), attente active des threads (`spin_us`), lecture d'avance des historiques (`warm_history`), dossier des modèles (`models_dir`, dossier existant exigé), langue du service. **Réglages conseillés** (objet `recommended` de `service.js`, bouton « Revenir aux réglages conseillés », « Conseillé : … » sur chaque carte qui en diffère) et **sauvegardes** avec libellé (`settings-profiles.json` des données ; routes `GET/POST/DELETE /albireo/v1/settings/profiles`, sans le mot de passe réseau).

Pour l'arrêter : Ctrl-C dans son terminal, ou `kill -TERM`. Lancé en arrière-plan par un shell non interactif (un script, `( … &)`), il ignore SIGINT : le shell le lui fait ignorer, et macOS, contrairement à Linux, jette alors le signal même quand le fil qui l'attend avec `sigwait` le bloque. `kill -INT` reste donc sans effet dans ce cas ; `kill -TERM` l'arrête en une seconde.

### Mesurer toute la vitesse en une commande

```sh
bash dlib/dlib/external/albireo/tools/bench_macos.sh        # 2 essais en alternance processeur / GPU
BENCH_SIZES="512" bash dlib/dlib/external/albireo/tools/bench_macos.sh 1   # une seule taille, un essai
```

Le tableau (machine, version d'Albireo, mode économie, charge, puis lecture et écriture de chaque modèle) va dans `build/bench/<modèle du Mac>-<date>.md`. Les modèles mesurés : ceux du portage présents dans `build/models` et `build/albireo-home/models`, et tout `.gguf` de `build/bench-models/`.

### Vérifier la page web

```sh
bash dlib/dlib/external/albireo/tools/page_test/page_test.sh        # ESLint, puis 5 scénarios dans Chrome sans fenêtre
```

Un service d'essai isolé (port 18491, hors ligne, données dans `build/page-test`) sert la page des sources, avec le Qwen3-VL 2B et, s'il est là, le 4B et son projecteur, clonés depuis le Mac. ESLint vérifie les modules de la page, puis 108 vérifications jouent la page comme un utilisateur (conversation, paramètres, projets, documents, images, actions du fil, deux onglets, largeur d'un téléphone). Il faut Google Chrome et Node.js ; ESLint s'installe une fois depuis npm dans `build/page-test/lint`. Détails et écriture des scénarios : `tools/page_test/README.md`. Dernier passage : 2026-11-12, Albireo 4.11.11, tout passe (3 min 42 s, installation d'ESLint comprise).

## État constaté

Sur Mac M2, macOS arm64, avec AppleClang 21, CMake 4.1.2 et Ninja (état au 2026-11-09, après la fusion de `upstream/main` à `184ea00`, Albireo 1.0.12) :

| Build | Compilation | Tests |
|---|---|---|
| dlib + exemples (CPU de référence) | 0 erreur ; les 24 exemples `slm_*` sont des Mach-O arm64. Avertissements : `ld: -s is obsolete`, `-Wbraced-scalar-init`. | Exemples pas encore exécutés. `dtest` construit le 2026-11-14 (`-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DDLIB_USE_FFMPEG=OFF`) : avec FFmpeg de Homebrew, il ne démarre pas (« Library not loaded: …/libx265.215.dylib », x265 de Homebrew passé en 4.2 sans que FFmpeg 8.0.1 suive). `dtest --test_tokenizer` passe (516 vérifications, dont `hf_tokenizer`) ; les autres tests pas encore lancés. |
| Albireo | 0 erreur, 0 avertissement. `albireo-service`, `albireo-builtin` et le moteur `libalbireo-engine-cpu.dylib` (noyaux `NEON+dotprod` pour Q4_K et Q6_K, portables pour les autres formats en génération ; attention NEON pour les clés et valeurs en demi-précision et en 8 bits ; écriture et lecture des prompts sur le GPU par Metal pour les modèles en Q4_K / Q6_K) sont construits. Version **4.19.0** du fork depuis le 2026-11-14 (en 3.0.0 : lecture à voix haute retirée le 2026-11-11 : plus de `albireo-speech` ni de SVOX Pico). Depuis la **4.0.0** écrite sous Windows : plus aucune clé, catalogue sans signature, plus d'espace de publication, plus de `albireo-sign` ni de `pseudalis-sign`. La branche a été fusionnée dans `main` le 2026-11-12, puis construite sur le Mac sans erreur ni avertissement. | Passent : `test_qkernels`, `test_metal`, `test_model_file`, `test_chat_format`, `test_engine_api` (sur le moteur cpu), `test_host`, et le test complet `tools/test_macos.sh` (**18 étapes sur 18**, 2026-11-12, en 4.1.0, dont la nouvelle étape « offline reserve » ; 17 sur 17 le 2026-11-11 : noyaux, attention, fichiers abîmés, interface du moteur, dossier des modèles, moteur, modèle de recherche, service hors ligne avec import, conversation, absence de la route de voix (404) et arrêt). Le moteur répond juste avec Qwen3-VL 2B Instruct (`engine_chat`) ; vitesse ci-dessous. Pas lancés (il leur faut un fichier de référence) : `test_runtime`, `test_tokenizer`. Le service `--console` démarre, télécharge ses modèles, sert la page web et ses fichiers, et répond par `/v1/chat/completions`, en une fois et en flux (ci-dessous). Page essayée dans un navigateur par l'humain : conversation, et résumé juste d'un PDF de 4,6 Mo (guide d'hygiène informatique de l'ANSSI), « consulté par recherche ». |
| Pseudalis | 0 erreur, 2 avertissements (`-Wunused-but-set-variable` dans `language_model_data.h` et `scan_image_pyramid.h`, code de dlib). Version 1.0.5 depuis la fusion du 2026-11-08. `pseudalis-service`, avec le moteur intégré, est construit (`pseudalis-sign` aussi jusqu'à Albireo 3.2.0 ; retiré avec la 4.0.0, et Pseudalis compile sans lui depuis le 2026-11-12). | `test_engine` et `test_host` passent. `tools/test_macos.sh` : **34 étapes sur 34** (2026-11-12, après la fusion de la branche écrite sous Windows ; déjà 34 sur 34 le 2026-11-11, après le nettoyage) ; auparavant `tools/test_linux.sh` : 34 sur 34 en version 1.0.5 (2026-11-08, après la fusion de l'original, `lowpowermode 0`, `PSEUDALIS_TEST_PORT=18454` pendant que le service tourne sur 18444) ; 28 sur 28 en 1.0.3 : service, anonymisation et restauration par l'interface et par le lecteur, archives, copies, réglages, termes imposés. Le premier passage, à 26 sur 28 (« drive signatures », « copies on the drive »), avait très probablement été fait en mode économie d'énergie (actif lors des mesures d'Albireo qui ont suivi, pas vérifié au moment du test) : ces deux étapes attendent un délai fixe (`sleep 8`, puis 6 et 4 s) que le lecteur ne tenait pas sur un Mac dix fois plus lent ; elles passent aussi sans la correction 9. Le service sert sa page web lancé depuis n'importe quel dossier (correction 9). |

Depuis cette mesure, la branche `bureau-2026-11-12` a reçu sous Windows les versions Albireo 4.0.1 à 4.1.0, la validation des langues Pseudalis, les contrôles de propriétaire/permissions POSIX, le retrait des choix de moteur CUDA/OpenVINO/x86 et les premières opérations de réserve locale. La 4.1.0 permet de restaurer un modèle depuis la réserve en mode « hors ligne », sans repli réseau ; une réserve absente ou altérée est signalée. Les tests Python de publication (3/3) et de génération du catalogue avec réponses Hugging Face simulées (1/1) passent sous Windows. Sous Debian local, `test_host` d'Albireo passe deux fois (187 contrôles chacun) et celui de Pseudalis passe, avec un adaptateur temporaire pour le chemin d'exécutable macOS. L'interface a été essayée dans un navigateur avec des réponses de service simulées. Le 2026-11-12, la branche a été fusionnée dans `main` (`e7aa5f1`). Sur le Mac, Albireo et Pseudalis compilent sans erreur, avec les 2 avertissements de dlib déjà connus ; les tests complets `tools/test_macos.sh` passent (Albireo 18 sur 18, dont « models folder and imports » et « offline reserve » ; Pseudalis 34 sur 34). Les vraies données du service (`build/albireo-home`, `build/pseudalis-home`) et leurs dossiers parents passent les nouveaux contrôles de propriétaire et de permissions. La réserve n'a pas encore de limite de taille ni d'interface de vidage. En **4.1.1** (`0d4946d`), les contrôles de dossiers lisent aussi les ACL étendues de macOS (une entrée « allow » qui laisse un autre compte écrire est refusée, une entrée « deny » acceptée ; `secure_folder` vide une ACL dangereuse) ; `test_host` d'Albireo : 194 vérifications ; `test_macos.sh` repassé : 18 sur 18 et 34 sur 34. Essai de la réserve avec le vrai service (copie clonée des données, port 18473) : suppression du 4B et de son projecteur rangés dans `<données>/reserve` (fichiers 0600, dossier 0700) ; restauration hors ligne en 23 s, empreintes conformes ; un octet changé dans la copie du projecteur fait refuser la restauration (`reserve_unavailable`), sans installation ; en ligne, le modèle entier (2,8 Go) est retéléchargé en 56 s et répond. Limites constatées : le repli en ligne retélécharge aussi le fichier sain, et la copie altérée reste dans la réserve jusqu'à la suppression suivante (qui la remplace par une copie saine). En **4.19.0** : architecture **Gemma 3** (`gemma3`), sur le processeur, par le chemin de Gemma 4 (`model_config::gemma3`, `value_norm` réservé à Gemma 4), format `gemma3` ; modèles d'essai `build/models/gemma-3-1b-it-Q4_K_M.gguf` et `gemma-3-4b-it-Q4_K_M.gguf` (unsloth, ou `GEMMA3_MODEL`) ; comparés à llama.cpp 0.9.0, de préférence avec `engine_chat --kv f16` (le 1B, à une seule tête de clés de 256 valeurs, est sensible à l'arrondi en 8 bits, comme dans llama.cpp) ; tokens attendus dans `tests/gemma3_tokenizer/` ; étapes « gemma 3 tokenizer », « gemma 3 » et « gemma 3 long prompt » de `test_macos.sh` : **46 étapes**. En **4.18.0** : appels d'outils de LFM2, à la manière de Python (`call_style::python`, `python_value()`), prompt rendu comme le gabarit de chaque modèle (comparé à `/apply-template` de llama.cpp 0.9.0 pour le 1.2B, le 8B et l'Audio) ; étapes « lfm2 tool call » et « lfm2 tool result » de `test_macos.sh` : **43 étapes**. En **4.17.1** : valeurs des clés et valeurs 8 bits tournées avant l'arrondi, comme les clés (`qk_rotate_row`), et sorties de l'attention remises en place (`qk_unrotate_row` ; noyau Metal `restore_outputs`, `kv_store` tourne aussi les valeurs) ; LFM2 revient aux 8 bits par défaut ; `test_metal` vérifie `restore_outputs` ; `test_macos.sh` : **41 étapes**. En **4.17.0** : architectures **LFM2** et **LFM2-MoE** (`lfm2`, `lfm2moe`), sur le processeur (convolutions courtes à état de deux positions, attention à normes QK, experts routés par sigmoïde) ; modèles d'essai `build/models/LFM2.5-1.2B-Instruct-Q8_0.gguf` et `build/models/LFM2.5-8B-A1B-Q4_K_M.gguf` (LiquidAI, ou `LFM2_MODEL` et `LFM2MOE_MODEL`) ; comparés à llama.cpp 0.9.0 (`llama-server -ngl 0 --jinja`, `/apply-template`) ; format `lfm2` ; clés et valeurs de LFM2 en demi-précision avec « auto » jusqu'à la 4.17.1 (en 8 bits, le 8B perdait un détail du texte de `tests/lfm2/long_prompt.txt`, que garde llama.cpp grâce à la rotation de ses valeurs) ; `engine_chat --kv auto|q8|f16` ; étapes « lfm2 », « lfm2 resumed state », « lfm2 checkpoint », « lfm2 moe » et « lfm2 moe long prompt » de `test_macos.sh` : **41 étapes**. En **4.16.0** : images de Gemma 4 avec son projecteur `gemma4v` (`build/models/gemma-4-E2B-it-mmproj-F16.gguf`, unsloth, 985 654 080 octets, SHA-256 `140be8d7…` conforme, ou `GEMMA4_PROJECTOR`) ; comparées à llama.cpp 0.9.0 (`llama-server -ngl 0 --no-mmproj-offload --jinja --mmproj … --image-max-tokens 280`) ; étapes « gemma 4 image » et « gemma 4 wide image » de `test_macos.sh` : **36 étapes**. En **4.15.0** : appels d'outils de Gemma 4 dans sa notation (`<|tool_call>call:nom{…}<tool_call|>`, chaînes entre `<|"|>`), prompt rendu comme son gabarit (comparé à Jinja2 3.1.6 et à `/apply-template` de llama.cpp 0.9.0) ; étapes « gemma 4 tool call » et « gemma 4 tool result » de `test_macos.sh` : **34 étapes**. En **4.14.0** : architecture **Gemma 4** dense (`gemma4`), sur le processeur seulement (fenêtre glissante de 512 positions, têtes de 256 et 512, clés et valeurs partagées par les 20 dernières couches, entrées par couche, logits plafonnés), tokenizer `spm_bpe` dans `dlib/tokenizer/hf_tokenizer.h`, format de conversation `gemma4` (raisonnement, sans outils) ; modèle d'essai `build/models/gemma-4-E2B-it-Q4_K_M.gguf` (unsloth/gemma-4-E2B-it-GGUF, 3 106 738 272 octets), ou `GEMMA4_MODEL` ; comparé à llama.cpp 0.9.0 (`llama-server -ngl 0 --jinja` : `/apply-template` pour le gabarit, `/completion` en choix glouton ; `llama-eval-callback` pour les tenseurs intermédiaires) ; étapes « gemma 4 tokenizer », « gemma 4 », « gemma 4 reasoning » de `test_macos.sh` : **32 étapes**. Le service d'essai de `test_macos.sh` prend le port 18443 par défaut, celui du vrai service : quand celui-ci tourne, lancer le script avec `ALBIREO_TEST_PORT=18473`. Vitesse sur le M2 (processeur, clés q8) : écriture 21 à 24 tokens/s, lecture de 512 tokens 143 à 153 tokens/s. En **4.13.0** : images de Qwen 3.5 (projecteur `qwen3vl_merger`, positions rotatives entrelacées lues pour `qwen35` et `qwen35moe` quand le fichier les déclare) ; projecteur de Qwen3.5-4B dans `build/models/Qwen3.5-4B-mmproj-F16.gguf` (unsloth, F16, 672 423 616 octets, SHA-256 `cd88edcf…` conforme) ; étape « qwen 3.5 image » de `test_macos.sh` (perroquet des images de compte de macOS) : **29 étapes**. En **4.12.1** : appels d'outils XML de Qwen 3.5 ; étape « qwen 3.5 tool call ». En **4.12.0** : prompts de Qwen 3.5/3.6 MoE sur le GPU par produits groupés des experts (`moe_group`, `moe_tiles`, `mm_id_*`) ; 11B-A3B, mesures alternées : 512 tokens 107-112 → 196-198 tokens/s, 2 048 tokens 121-122 → 175-176 ; `test_macos.sh` **27 étapes** (nouvelle : « qwen 3.6 moe checkpoint »). En **4.11.12** : licences des programmes copiées dans `bin/licenses`, panneau latéral fermé sur téléphone. De **4.11.1** à **4.11.11** : code mort de la page retiré, puis `web/js/app.js` (7 274 lignes) découpé en 22 modules, sans changement visible ; `app.js` ne garde que le démarrage (156 lignes) ; vérifié par le banc de la page (`tools/page_test`). En **4.11.0** : attention des prompts par produits de matrices 8 × 8 (`attention_mtile`, à la place d'`attention_tile`), limite de contexte par défaut des prompts GPU 262 144 positions (2B, mesures alternées : 4 096 tokens 423-424 contre 170-177 sur le processeur ; 8 192 tokens 295 contre 115-117 ; 16 384 tokens 171 contre 67) ; `test_macos.sh` 26 sur 26. En **4.10.0** : attention des prompts en tuiles sur le GPU, limite de contexte par défaut des prompts GPU 4 096 positions (2B, 4 096 tokens : 198 contre 171-181 tokens/s sur le processeur). En **4.9.0** : prompts de Qwen 3.5 sur le GPU (148 contre 86 tokens/s, 4B) ; produits des prompts arrondis à 32 lignes (NaN de MPS sur le 0.8B) ; `test_macos.sh` : 26 étapes. En **4.8.0** : points de reprise de l'état de Qwen 3.5 toutes les 256 positions ; étape « qwen 3.5 checkpoint » de `test_macos.sh` (25 étapes). En **4.7.0** : mélanges d'experts sur le GPU pour les étapes (Qwen 3.6 11B-A3B : 26,1-26,3 contre 24,4-25,0 tokens/s), format Q5_0 sur le GPU ; étape « qwen 3.6 moe on the GPU » de `test_macos.sh`. En **4.6.0** : architecture **Qwen 3.5 / 3.6 MoE** (`qwen35moe`, expert partagé), sur le processeur ; étape « qwen 3.6 moe » de `test_macos.sh` (modèle `build/models/Qwen3.6-11B-A3B-Niwaki-Q4_K_M.gguf`, ou `QWEN36_MODEL`). En **4.5.0** : les étapes d'écriture de Qwen 3.5 passent sur le GPU (Qwen3.5-4B : 11,7 à 12,2 → 19,5 tokens/s, mesures alternées), avec les produits Q5_K et Q8_0 ; `test_macos.sh` gagne l'étape « qwen 3.5 on the GPU » (modèle `build/albireo-home/models/hf-unsloth-qwen3.5-4b-q4_k_m.gguf`, ou `QWEN35_GPU_MODEL`). En **4.4.0** : nouvelle architecture **Qwen 3.5** (`qwen35`), sur le processeur seulement (couches Gated DeltaNet à état récurrent par séquence, attention à porte) ; comparée à llama.cpp 0.9.0 (`brew install llama.cpp`, `llama-server -ngl 0`, choix glouton) : mêmes 150 tokens pour Qwen3.5-4B Q4_K_M ; Qwen3.5-4B écrit à 10,2 à 11,5 tokens/s par le service. `test_macos.sh` gagne les étapes « qwen 3.5 » et « qwen 3.5 resumed state » (modèle `build/models/Qwen3.5-0.8B-Q8_0.gguf`, ou `QWEN35_MODEL`). En **4.3.0** (`b49dae4`) : réponses de Hugging Face (recherches, fichiers d'un dépôt) gardées dans la base, 30 de chaque, rendues hors ligne, avec `local=1` ou quand Hugging Face ne répond pas (`cached_at`), `GET /albireo/v1/hub/recent` ; boutons « Rafraîchir depuis le Mac » / « depuis Hugging Face » ; modèles installés avant la 4.2.0 clonés dans la réserve au démarrage (4 fichiers, 3,9 Go partagés, sur ce Mac) ; une copie prend la date de son entrée dans la réserve. `test_macos.sh` d'Albireo : **19 étapes sur 19** (nouvelle : « hub search kept on the Mac »). En **4.2.0** (`6af1326`) : chaque modèle téléchargé est gardé dans la réserve dès la fin de son téléchargement ; les copies sont des clones APFS (`fclonefileat`), sans place prise ; l'empreinte SHA-256 passe par CommonCrypto (2,1 Go/s contre 0,29 pour mbedtls, mesures alternées) ; une restauration reprend les fichiers sains et ne télécharge que les autres ; plafond `reserve_gb` (200 Go) et `GET`/`DELETE /albireo/v1/reserve`. Restauration hors ligne du 4B avec son projecteur : 6,1 et 6,8 s (contre 23 s), 0 Mo de place prise. `test_host` : 194 vérifications et plus ; `test_macos.sh` : 18 sur 18 et 34 sur 34. Attention pour un essai sur une copie des données : la base `albireo.db` garde les chemins absolus des modèles ; les réécrire dans la copie, sinon supprimer un modèle depuis la copie efface l'original.

### Albireo : premier modèle et vitesse

Modèle : Qwen3-VL 2B Instruct, `Qwen3VL-2B-Instruct-Q4_K_M.gguf` (1 107 409 952 octets, SHA-256 `089d75c5…dca6c2ae`, conforme à `dist/albireo/catalog.json`), téléchargé dans `build/models/`. Machine : MacBook Air M2 (`Mac14,15`, sans ventilateur), 4 cœurs performance et 4 cœurs efficacité, sur secteur. Température 0. Le modèle n'a que deux formats de poids : Q4_K (168 tenseurs, 1 204 M valeurs) et Q6_K (29 tenseurs, 517 M valeurs), plus des normes en F32.

**Avant toute mesure de vitesse**, vérifier que le mode économie d'énergie est coupé (`pmset -g | grep lowpowermode` doit donner `0`) et noter la charge (`sysctl -n vm.loadavg`). En mode économie d'énergie, le moteur va environ dix fois moins vite et paraît n'occuper qu'un cœur.

```sh
build/macos-albireo/tests/engine_chat build/macos-albireo/bin/engines/libalbireo-engine-cpu.dylib \
  build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf --threads 8 --max 88 --temperature 0 "<question>"
build/macos-albireo/tests/bench_runtime build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf <fils> 64 16 2
```

**Noyaux NEON (2026-11-07).** Sur ARM64 avec les instructions de produit scalaire (`__ARM_FEATURE_DOTPROD`, présentes sur toutes les puces Apple), `qk_dot` prend pour Q4_K et Q6_K les noyaux de `engine/qkernels_neon.inc` (`vdotq_s32`). Ils calculent les mêmes sommes entières que les références, exactes quel que soit l'ordre, et finissent chaque bloc par la même expression : résultat identique au bit près (contrôle : 0 différence sur 12 000 produits contre `qk_dot_portable`, longueurs 256, 2 048 et 6 144 ; même réponse octet pour octet à température 0). Le moteur s'annonce `NEON+dotprod`. Mesures alternées le même jour, 8 fils, charge moyenne de 5 à 7 :

| Programme | Essai | Version | Prompt | Génération | 2 séquences |
|---|---|---|---|---|---|
| `engine_chat` (21 / 88 tokens) | 1 | origine | 7,8 tokens/s | 10,9 tokens/s | |
| `engine_chat` | 1 | NEON | 10,3 tokens/s | 32,5 tokens/s | |
| `engine_chat` | 2 | origine | 12,8 tokens/s | 11,0 tokens/s | |
| `engine_chat` | 2 | NEON | 12,7 tokens/s | 36,9 tokens/s | |
| `bench_runtime` (64 tokens de prompt, 32 pas) | 1 | origine | 8,8 tokens/s | 6,5 tokens/s | 11,3 tokens/s |
| `bench_runtime` | 1 | NEON | 13,7 tokens/s | 33,7 tokens/s | 43,2 tokens/s |
| `bench_runtime` | 2 | origine | 13,4 tokens/s | 10,8 tokens/s | 10,5 tokens/s |
| `bench_runtime` | 2 | NEON | 13,2 tokens/s | 32,9 tokens/s | 41,5 tokens/s |

Le prompt ne change pas : à partir de 8 tokens (`qk_float_product_tokens`), il passe par `matmul_float` (poids développés en floats, puis `gemm_rows` et `qk_dot_f32`), que ces noyaux ne touchent pas.

**Produit en floats NEON pour le prompt (2026-11-07).** `gemm_rows` prend, sous `ALBIREO_QK_NEON`, le noyau `gemm_neon<4>` (4 tokens contre 4 lignes de poids, chaque poids lu servant 4 tokens) et `gemm_neon<1>` pour les tokens restants. Chaque somme garde l'ordre et les arrondis de `dot_f32_ref` : quatre voies, les colonnes restantes dans la première, puis `(s0 + s1) + (s2 + s3)`, et le produit arrondi avant l'addition (`vmulq_f32` puis `vaddq_f32`). Un `vfmaq_f32` (multiplication-addition fusionnée) changeait le dernier bit de 23 953 sommes sur 32 436 : Apple clang ne fusionne pas la référence. Contrôle : 0 différence sur 32 436 produits contre `qk_dot_f32` (K = 64, 2 048 et 2 051 ; 16 et 37 lignes ; 8 à 64 tokens ; avec et sans accumulation) ; même réponse que le moteur d'origine. Mesures alternées le même jour, 8 fils, charge moyenne de 6 à 23 :

| Programme | Essai | Avant (NEON pour la génération seule) | Après |
|---|---|---|---|
| `bench_runtime`, prompt de 64 tokens | 1 | 13,1 tokens/s | 38,3 tokens/s |
| `bench_runtime`, prompt de 256 tokens | 1 | 12,8 tokens/s | 50,4 tokens/s |
| `engine_chat`, prompt de 21 tokens | 1 | 4,6 tokens/s | 34,6 tokens/s |
| `bench_runtime`, prompt de 64 tokens | 2 | 10,8 tokens/s | 46,8 tokens/s |
| `bench_runtime`, prompt de 256 tokens | 2 | 10,3 tokens/s | 44,9 tokens/s |
| `engine_chat`, prompt de 21 tokens | 2 | 11,0 tokens/s | 39,1 tokens/s |

La génération, que ce changement ne touche pas, varie pendant cette série de 14 à 34 tokens/s selon la charge du Mac.

Mesures de référence du moteur d'origine, avant NEON (2026-11-06, mode économie d'énergie coupé, charge moyenne entre 13 et 30 : mises à jour de l'App Store en cours) :

| Programme | Fils | Prompt | Génération | Cœurs occupés en moyenne |
|---|---|---|---|---|
| `engine_chat` (moteur en bibliothèque), 21 tokens de prompt, 88 générés | 4 | 7,7 tokens/s | 6,0 tokens/s | 2,9 |
| `engine_chat` | 8 | 9,2 tokens/s | 7,9 tokens/s | 4,6 |
| `bench_runtime` (moteur compilé dans le programme), 64 tokens de prompt | 1 | 2,7 tokens/s | 2,2 tokens/s (457 ms par token) | 1,0 |
| `bench_runtime` | 4 | 9,6 tokens/s | 7,0 tokens/s (144 ms) ; 2 séquences : 6,9 tokens/s | 3,7 |
| `bench_runtime` | 8 | 11,3 tokens/s | 8,3 tokens/s (120 ms) ; 2 séquences : 8,9 tokens/s | 5,9 |

**Le service (2026-11-07).** Le service lancé comme ci-dessus annonce « Apple M2 », 8 cœurs, 24 Go, le moteur `cpu` en `NEON+dotprod` à 8 fils, et réduit le contexte du modèle 4B de 262 144 à 20 736 tokens selon la mémoire. Charge moyenne de 4 à 5, `lowpowermode 0` :

| Requête (modèle `albireo-general` = Qwen3-VL 4B Thinking) | Tokens | Durée | Prompt | Génération | Résultat |
|---|---|---|---|---|---|
| « Quelle est la capitale de la France ? Réponds en une phrase. », sans raisonnement | 27 lus, 33 écrits | 3,7 s | 19,4 tokens/s | 14,0 tokens/s | « Paris est la capitale de la France. » |
| « Explique en trois phrases ce qu'est une puce Apple M2. », en flux, raisonnement, 600 au plus | 25 lus, 600 écrits | 54 s | 18,6 tokens/s | 11,3 tokens/s | 604 messages du flux ; les 600 tokens restent dans le raisonnement (en anglais), la réponse n'a pas commencé |

Le 4B pèse deux fois le 2B (2,3 Go contre 1,1) : sa génération va à peu près deux fois moins vite. Son projecteur d'images est en Q8_0 ; ses produits et son attention passent par Accelerate depuis le 2026-11-08 (mesures ci-dessous).

**Long contexte : l'attention devient l'étape lente.** Pendant le résumé du PDF (environ 28 500 tokens lus par le service au total, une vingtaine de milliers pour cette requête), l'écriture tombe à 3,4 tokens/s. Profil (`sample`, 3 s, pendant l'écriture) : `qk_attention_values` 3 007 échantillons et `qk_attention_scores` 2 807, contre 2 737 pour `qk_dot` (déjà en NEON). L'attention, qui relit à chaque token toutes les clés et valeurs (en F16), prenait les deux tiers du calcul en code portable.

**Attention NEON (2026-11-07).** `qk_attention_scores` et `qk_attention_values` prennent, sous `ALBIREO_QK_NEON`, `attention_scores_neon` et `attention_values_neon` (`engine/qkernels_neon.inc`). Les demi-flottants sont convertis par `vcvt_f32_f16`. Pour les scores, les clés sont rangées en travers par quatre (`vst4q_f32`), pour qu'une voie suive une clé ; pour les valeurs, une voie suit un élément. Chaque somme garde l'ordre du code portable et ses arrondis tels qu'Apple clang 21 les compile : un score fait produit puis addition sur ses `hd / 4 * 4` premiers termes (boucle vectorisée par le compilateur), puis `fma` sur les autres ; une valeur fait `fma` sur chaque clé (boucle à pas de `hd`, restée scalaire, sauf pour `hd == 1`). Contrôle : 0 différence sur 549 760 résultats contre le code portable compilé avec les mêmes options (`-U__ARM_FEATURE_DOTPROD`), `hd` de 128, 64, 80, 72 et 6, 1 à 2 049 clés, 1 à 9 lignes ; réponse identique, mot pour mot, avant et après, sur un résumé de 4 329 tokens (`engine_chat`, `--context 8192 --temperature 0`). Une règle d'arrondi uniforme ne suffisait pas : tout en produit puis addition laissait 28 907 valeurs différentes sur 49 152 (hd 128) ; la règle des scores appliquée aux valeurs, 25 676.

Mesures alternées, modèle 2B, 8 fils, `bench_runtime <modèle> 8 4096 32 1` (4 096 tokens de prompt, puis 32 tokens écrits), `lowpowermode 0`, charge moyenne de 4 à 16 :

| Essai | Version | Prompt de 4 096 tokens | Génération après 4 096 tokens |
|---|---|---|---|
| 1 | avant | 25,5 tokens/s (161 s) | 9,0 tokens/s |
| 1 | NEON | 52,8 tokens/s (78 s) | 19,6 tokens/s |
| 2 | avant | 24,5 tokens/s (167 s) | 7,5 tokens/s |
| 2 | NEON | 50,0 tokens/s (82 s) | 22,0 tokens/s |

Avec `engine_chat`, résumé de 4 329 tokens : prompt 23,1 → 47,0 tokens/s, génération 7,9 → 16,9 tokens/s.

Le résumé du PDF refait dans la page (2026-11-08, `/albireo/v1/status` relevé toutes les 5 s) : le modèle de recherche range le document (~25 000 tokens, environ 200 tokens/s, 2 min), puis le 4B lit environ 1 800 tokens de passages choisis et écrit 578 tokens à 12 à 13 tokens/s. Pas comparable au premier essai (3,4 tokens/s), dont la quantité de contexte n'a pas été relevée et était bien plus grande.

**Produits des prompts par Accelerate (2026-11-08).** `matmul_float` appelle, sous `ALBIREO_QK_ACCELERATE` (NEON et `__APPLE__`), `matmul_accelerate` (`engine/qkernels_accelerate.inc`, fichier du fork) dès 16 tokens : les poids sont décompressés en flottants par tranches de 256 lignes par les fils du pool, pendant que `cblas_sgemm` d'Accelerate multiplie la tranche précédente sur le coprocesseur matriciel. Sous 16 tokens, `gemm_neon` reste en place. Sur Mac, `qk_float_product_tokens` passe de 8 à 24 (`engine/qkernels.h`) : en dessous, les noyaux entiers sont plus rapides. Le framework Accelerate est lié à toutes les cibles d'Albireo (`link_libraries` dans la branche ARM64 du CMake, sous `if (APPLE)`). **Les produits des prompts ne sont plus identiques au bit près** à ceux du code portable : `test_qkernels` (tolérance contre une référence en double) passe, et une réponse longue peut être formulée autrement. Repères mesurés hors dépôt : un appel `cblas_sgemm` 512 × 2 048 × 6 144 fait 1 060 Gflop/s ; le même découpé en blocs de 16 lignes sur 8 fils, 315.

Mesures alternées, 8 fils, `lowpowermode 0`, charge moyenne de 2 à 14 (prompt ; la génération ne change pas) :

| Essai | Version | 2B, 64 tokens | 2B, 512 tokens | 2B, 4 096 tokens | Recherche (Q8_0), 512 tokens |
|---|---|---|---|---|---|
| 1 | avant | 62,8 tokens/s | 71,7 tokens/s | 57,5 tokens/s | 212,5 tokens/s |
| 1 | Accelerate | 133,7 tokens/s | 217,5 tokens/s | 162,8 tokens/s | 600,9 tokens/s |
| 2 | avant | 60,4 tokens/s | 64,2 tokens/s | 51,6 tokens/s | 183,7 tokens/s |
| 2 | Accelerate | 114,1 tokens/s | 216,8 tokens/s | 155,9 tokens/s | 572,8 tokens/s |

Avec `engine_chat`, résumé de 4 329 tokens : prompt 51,7 → 134,9 tokens/s, réponse de même sens mais formulée autrement. Question de 21 tokens : « Paris », prompt 45,6 → 58,4 tokens/s (une paire de mesures). Le service, relancé avec ce moteur, transforme 1 834 tokens en vecteur de recherche en 5,3 s (mesure seule).

**Attention de l'encodeur d'images par Accelerate (2026-11-08).** Sous `ALBIREO_QK_ACCELERATE`, `encoder_scores` et `encoder_values` (tuiles de `qk_encoder_attention`) appellent `cblas_sgemm`. Contrôle hors dépôt contre une attention en double précision (84 cas, `hd` 64 à 128, 1 à 1 100 clés) : écart maximal 3,0e-6, contre 4,0e-6 en portable. Image 512 × 512 (zèbre et perroquet de macOS, convertis par `sips`), 4B Thinking et son projecteur, `engine_chat --projector … --image … --reasoning off --temperature 0`, mesures alternées : encodage 6,7 à 8,5 s → 1,8 à 2,2 s, lecture complète 10,7 à 12,9 s → 5,9 à 7,3 s ; descriptions justes et identiques mot pour mot.

**Noyaux Q4_K et Q6_K, sommes en vecteurs (2026-11-09, `791a211`).** Dans `dot_q4_K_neon` et `dot_q6_K_neon`, chaque groupe est mis à l'échelle dans le vecteur (`vmlaq_n_s32`) et un seul `vaddvq_s32` termine le bloc de 256 valeurs ; les échelles de Q4_K sont décodées d'un coup. Toujours identique au bit près (contrôle hors dépôt sur de vrais poids du 2B et du 4B : 0 différence sur 43 200 produits). Mesures alternées, `bench_runtime modele 8 64 64 1` : 2B 35,4 / 36,8 → 38,9 / 38,2 tokens/s, 4B 17,0 / 16,5 → 18,1 / 18,0.

**Clés et valeurs en 8 bits (Albireo 1.0.12, fusion du 2026-11-09).** Le moteur range désormais par défaut les clés et valeurs des modèles de langue en entiers de 8 bits (`kv_type` « auto », journal : « keys and values in 8 bits up to 476 MB ») ; les modèles de recherche restent en demi-précision. `bench_runtime` garde la demi-précision, sauf avec `q8` en sixième argument : `bench_runtime <modèle> 8 4096 32 1 q8` mesure comme le service. Sans attention NEON pour ce format, un prompt de 4 096 tokens (2B) tombait à 36 à 38 tokens/s et la génération qui suit à 9 tokens/s. `attention_scores_q8_neon` et `attention_values_q8_neon` (`qkernels_neon.inc`, branchées en tête de `qk_attention_scores_q8` et `qk_attention_values_q8`) reprennent les noyaux de l'attention NEON avec des clés dépliées depuis les 8 bits ; identiques au bit près au code portable (0 différence sur 351 780 valeurs, NEON contre build `-U__ARM_FEATURE_DOTPROD`). Mesures alternées : prompt de 4 096 tokens 36,0 / 37,7 → 157,7 / 162,3 tokens/s, génération qui suit 8,9 / 9,1 → 22,5 / 22,6 ; 4B, génération 16,4 / 16,6 → 17,8 / 17,8. La « mémoire engageable » ajoutée par l'original (`commit_total`, `commit_available`) vaut sur Mac la mémoire physique : la branche `__APPLE__` de `read_memory_info` ne compte pas le swap, et l'original la ramène au moins à la mémoire physique.

**Nombre de threads (2026-11-09).** Sur le Mac, `read_cpu_info` donne 8 cœurs (4 de performance, 4 d'efficacité), et le moteur prend 8 threads. Mesures alternées : 4 threads vont plus vite sur les questions courtes et en génération (4B : prompt de 64 tokens 58 contre 46 à 52 tokens/s, génération 17 à 19 contre 16 à 18), 8 sur les longs prompts (2B, 4 096 tokens : 154 à 169 contre 134 à 145). Choix de l'humain (option B) : le thread pool garde sur Mac une petite équipe d'un thread par cœur de performance (`engine/thread_pool_apple.inc`, fichier du fork), que `cpu_runtime::forward` prend pour les passes de moins de 24 tokens. Génération du 4B 17,6–18,1 → 18,2–18,6 tokens/s, du 2B 37,4–37,9 → 39,7–40,8 ; long prompt inchangé (8 threads). Le réglage `threads` du service fixe toujours la taille de la grande équipe ; à 4 ou moins, il n'y a pas de petite équipe.

Pour mémoire, en mode économie d'énergie : `engine_chat` 0,78 token/s à 8 fils et 0,85 à 4 fils (0,9 cœur occupé) ; `bench_runtime` 1,9 token/s à 4 fils.

Constats :

- **Avant NEON, le calcul était scalaire.** Profil (`sample`) : presque tout le temps est dans `matmul_float` (prompt) et `qk_dot_portable` (génération). Les deux ont maintenant leurs noyaux NEON (ci-dessus), et le prompt passe par Accelerate. En génération, les autres formats de poids (Q4_0, Q5_K, Q8_0, F16…) restent sur les noyaux portables : à faire si un modèle du catalogue les emploie.
- **L'attente des fils.** Hors x86, `ALBIREO_CPU_RELAX()` (`engine/thread_pool.h`) est `std::this_thread::yield()`, qui fait tomber la priorité du fil à 0 sur macOS (`ps -M`). Essai : l'instruction `isb` à la place sur ARM64. Les fils restent alors à la priorité 31, mais la génération n'est pas plus rapide (deux essais alternés : 5,2 et 5,5 tokens/s avec `isb`, 5,7 et 6,4 sans). Essai abandonné, code d'origine gardé.
- `bench_runtime` lit toujours 64 tokens de son prompt pour la génération groupée : avec un prompt plus court, il lit hors du tableau (exception `token identifier out of range`). Lui donner au moins 64 tokens.

Avant le 2026-11-11, `tools/test_linux.sh` (Pseudalis) demandait deux outils de Linux absents de macOS : `nproc` (remplacé par `sysctl -n hw.ncpu`) et `stat -c` de GNU (`gstat`, paquet Homebrew `coreutils`). Sans eux, la taille lue est 0, et l'étape « drive signatures » échoue pour une raison qui ne tient pas au programme.

Ce qui a été changé pour compiler sur ARM64, chaque fois sous une condition qui ne vaut que pour macOS ou ARM64 (avant le nettoyage du 2026-11-11, qui a rendu ces conditions inutiles) :

1. Moteurs `avx2` : non construits sur ARM64 (`albireo/CMakeLists.txt`, `pseudalis/CMakeLists.txt`) ; les tests d'Albireo qui prennent les noyaux AVX2 les compilent sans options x86, donc en code portable.
2. `svox_pico/lib/picopltf.h` : sur macOS, `<machine/endian.h>`, avec `__BYTE_ORDER` et `__BIG_ENDIAN` ramenés aux noms de macOS (`BYTE_ORDER`, `BIG_ENDIAN`).
3. libpng d'Albireo : sur ARM64, compilation de ses sources NEON (`arm/arm_init.c`, `arm/filter_neon_intrinsics.c`, `arm/palette_neon_intrinsics.c`), comme le fait le CMake de dlib.
4. `dlib/dlib/CMakeLists.txt`, cible `dlib_httplib` : sur macOS, liaison de `CoreFoundation` et `Security` (certificats du Trousseau).
5. `pseudalis/host/drive_folders.cpp` : sur macOS, `st_mtim` devient `st_mtimespec`.

Ce qui a été ajouté pour la vitesse :

6. `albireo/engine/qkernels_neon.inc` (fichier du fork) : noyaux NEON de `qk_dot` pour Q4_K et Q6_K. Le même fichier porte `gemm_neon`, le produit en floats du prompt. `albireo/engine/qkernels.cpp` les branche par cinq ajouts sous `ALBIREO_QK_NEON` (`__ARM_NEON` et `__ARM_FEATURE_DOTPROD`) : `#include <arm_neon.h>`, l'inclusion du fichier, deux cas dans `qk_dot`, une branche dans `gemm_rows`, le nom `NEON+dotprod` dans `qk_isa()`. Il porte aussi l'attention (`attention_scores_neon`, `attention_values_neon`), appelée en tête de `qk_attention_scores` et `qk_attention_values` (deux fois quatre lignes), et sa variante pour les clés en 8 bits (`attention_scores_q8_neon`, `attention_values_q8_neon`), appelée en tête de `qk_attention_scores_q8` et `qk_attention_values_q8` (deux fois quatre lignes, depuis Albireo 1.0.12). `albireo/engine/thread_pool_apple.inc` (fichier du fork) : la petite équipe des cœurs de performance, incluse dans `thread_pool.h` sous `__APPLE__` ; `active()` remplace `size()` dans `qk_matmul` et l'attention ; `cpu_runtime::forward` la demande sous `__APPLE__`.

Ce qui a été changé pour lancer le service d'Albireo, sous `#if defined(__APPLE__)` :

7. `albireo/common/platform.cpp`, `executable_path()` : le chemin du programme par `_NSGetExecutablePath` puis `realpath`, à la place de `/proc/self/exe`, qui n'existe pas sur macOS. Sans cela, le service ne trouve ni ses moteurs (« no usable engine in ») ni sa page web (« no web folder at web »). Pseudalis a sa copie du même code (correction 9).
8. `albireo/common/system_info.cpp` : la mémoire (`hw.memsize`, et les pages libres, inactives ou purgeables de `host_statistics64`) à la place de `/proc/meminfo`, qui donnait 0, et le nom du processeur (`machdep.cpu.brand_string`). Le moteur se sert de la mémoire libre pour borner la place des clés et valeurs ; à 0, il sautait ce calcul.
9. `pseudalis/common/platform.cpp`, `executable_path()` : la même correction que la 7. Le moteur de Pseudalis est intégré au service hors de Windows, mais sa page web se cherche dans `web` à côté du programme : sans la correction, lancé depuis un autre dossier que `bin/`, le service répond 404 (« no web folder at web ») ; avec, 200.

Ajouté ensuite pour la vitesse :

10. `albireo/engine/qkernels_accelerate.inc` (fichier du fork) : les produits des prompts par `cblas_sgemm` d'Accelerate, à partir de 16 tokens. `qkernels.cpp` : `ALBIREO_QK_ACCELERATE` et `#include <vecLib/cblas_new.h>` sous NEON et `__APPLE__`, l'inclusion du fichier avant `matmul_float`, et un aiguillage en tête de `matmul_float`, puis en tête de `encoder_scores` et `encoder_values` (attention de l'encodeur d'images). `qkernels.h` : `qk_float_product_tokens` à 24 sous `__APPLE__` et `__ARM_FEATURE_DOTPROD` (8 ailleurs). `albireo/CMakeLists.txt` : `link_libraries("-framework Accelerate")` dans la branche ARM64, sous `if (APPLE)`.

### GPU par Metal : la sonde (2026-11-11)

Le moteur GPU de l'original passe par les tenseurs de dlib compilés pour CUDA (ou OpenVINO) : il ne tourne pas sur Mac. Un moteur Metal se brancherait sur les interfaces `matmul_offload` (un produit à la fois) et `layer_pass` (des couches entières, ou tout le token avec `attend()`) de `engine/offload.h`. Avant de l'écrire, une sonde hors du moteur (`macos/metal-sonde/`, commandes dans son README) mesure ce que le GPU du M2 peut gagner. Le compilateur Metal hors ligne n'est pas installé (composant optionnel de Xcode) : les noyaux sont compilés par macOS au lancement (`newLibraryWithSource`), sans outil de plus.

| Un token, tous les poids Q4_K et Q6_K (charge 3 à 4) | 4B | 2B |
|---|---|---|
| Processeur, 8 / 4 threads | 55,2 / 50,9 ms | 26,2 ms (8 threads) |
| GPU, une commande | 45 à 48 ms | 19,7 ms |
| GPU, plafond (lecture pure) | 35 ms (70 Go/s) | 17,0 ms |
| GPU, une commande attendue par couche | 57 à 60 ms | |
| GPU, une commande attendue par produit | 120 ms | |

Aller-retour avec le GPU : environ 186 µs. Exactitude du GPU : 2,8e-8 contre un calcul en double. Conclusion : sur le M2, le GPU lit les poids 5 à 22 % plus vite que le processeur avec les noyaux de la sonde (un tiers sur le 2B), 1,5 fois au mieux (plafond), et seulement si tout le token tient en une commande ; un produit ou une couche à la fois est plus lent que le processeur. L'estimation « écriture doublée » de la reprise du 2026-11-09 était fausse. Décision de l'humain (2026-11-11) : moteur Metal complet, tout le token sur le GPU (plan dans la reprise). Première brique : le module `engine/metal_device.{h,mm}` ouvre le GPU au démarrage du moteur et fait compiler par macOS les noyaux de `engine/metal_kernels.metal`, glissés dans le moteur sous forme de texte (`metal_kernels.inc.in`) : le build ne demande pas de compilateur Metal. Journal du moteur : « GPU Apple M2 through Metal, 25 kernels compiled, 18186 MB advised, buffers of 13639 MB at most ». Pour s'en passer : réglage `accelerator` du service à `none` (le service passe alors `metal` à faux au moteur, `3796d07`), ou `engine_chat --no-metal`.

**Les pas d'écriture sur le GPU (2026-11-11, `5f0b07f`).** `engine/cpu_runtime_metal.cpp` donne au GPU les passes de 1 à 8 tokens d'un modèle dense dont tous les produits sont en Q4_K ou Q6_K (les modèles du service) : toutes les couches en une commande, les poids lus dans la projection du fichier et les clés et valeurs écrites dans la zone du cache, sans copie ; les prompts, les images, les mélanges d'experts et les autres formats (le modèle de recherche, en Q8_0) restent sur le processeur. Contrôle au chargement du modèle (au premier usage dans les programmes qui ne l'appellent pas, comme `bench_runtime`) contre le processeur en produits flottants : logits à 0,04 % à 0,13 % près, même token ; repli sur le processeur en cas de désaccord ou de panne. Noyaux en gabarits Metal instanciés par taille (têtes groupées, largeur des têtes, un token ou plusieurs), sans quoi leurs tableaux partaient en mémoire (attention 9,2 → 1,7 ms par token). Contrôle des noyaux : `tests/test_metal` (données au hasard). Mesures alternées, clés en 8 bits, `bench_runtime <modèle> 8 64 64 1 q8 [metal]` :

| Écriture | Processeur | GPU |
|---|---|---|
| 4B | 18,3 à 19,0 tokens/s | 21,3 à 21,7 tokens/s |
| 2B | 35,7 à 40,8 tokens/s | 49,4 à 49,6 tokens/s |
| 2B, après 4 096 tokens | 22,0 à 23,1 tokens/s | 26,5 à 28,5 tokens/s |

Service : « the generation steps run on it » ; le contrôle se fait au chargement du modèle (3 s, `f199a04`), et la première question écrit à 21,1 tokens/s (21,2 la suivante). Profil du GPU (4B, 64 positions, 46 ms) : produits 43,4 ms (dont la tête des logits 5,5), attention 1,7, le reste 1 à 2.

**Les prompts sur le GPU (2026-11-11, `cbefd5c`).** Les passes de 24 tokens ou plus (le seuil `qk_float_product_tokens`, où le processeur passe à Accelerate) dont le contexte final tient en 2 048 positions passent aussi sur le GPU : chaque matrice est décompressée en floats (`dequant_q4_K`, `dequant_q6_K`, identiques à `qk_dequantize_row`) puis multipliée par Metal Performance Shaders (float32). Au-delà de 2 048 positions, le processeur reprend : l'attention de prompt du GPU (un token à la fois) perd sur le 2B à 4 096 tokens (134 contre 173 tokens/s). Contrôle au chargement : un prompt de 32 tokens à 0,23 % (2B) et 0,11 % (4B) du processeur en flottants ; en clés 8 bits la tolérance d'un prompt est de 5 % (la quantification propage des sauts d'un cran ; en demi-précision, 0,04 %). Mesures alternées :

| Lecture, clés en 8 bits | Processeur | GPU |
|---|---|---|
| 4B, 64 tokens | 53,0 à 54,5 tokens/s | 92,7 à 100,0 tokens/s |
| 4B, 512 tokens | 91,5 à 93,5 tokens/s | 153,3 à 155,5 tokens/s |
| 2B, 64 tokens | 132,0 à 135,9 tokens/s | 262,0 à 263,5 tokens/s |
| 2B, 512 tokens | 220,2 à 224,4 tokens/s | 417,6 à 421,8 tokens/s |

Service (4B) : un prompt de 619 tokens lu à 142 tokens/s ; empreinte du processus 2,8 Go, dont 290 Mo de tampons du GPU.

**L'attention des prompts sur le GPU, en tuiles puis en matrices (2026-11-12, Albireo 4.10.0 et 4.11.0).** La limite de 2 048 positions ci-dessus n'est plus : le noyau `attention_mtile` (`engine/metal_kernels.metal`) lit les clés et valeurs par tranches de 32 positions partagées par une tuile de 2 à 32 tokens, et calcule scores et somme par `simdgroup_matrix` 8 × 8. Le GPU lit désormais les prompts jusqu'à 262 144 positions (réglage `gpu_prompt_context`). Qwen3-VL 2B, `bench_runtime <modèle> 8 <tokens> 16 1 q8 [metal]`, mesures alternées, charge 2,8 à 3,1 :

| Lecture, clés en 8 bits | Processeur | GPU |
|---|---|---|
| 2B, 512 tokens | 226,5 à 243,0 tokens/s | 603,4 à 608,1 tokens/s |
| 2B, 4 096 tokens | 170,0 à 177,3 tokens/s | 423,0 à 424,0 tokens/s |
| 2B, 8 192 tokens | 114,5 à 116,6 tokens/s | 295,1 à 295,4 tokens/s |
| 2B, 16 384 tokens (1 essai) | 67,2 tokens/s | 170,9 tokens/s |

**Le GPU dans la page (2026-11-11).** L'onglet « À propos » des paramètres a une ligne « GPU » : le nom de la puce, puis, pour chaque modèle chargé, « sur le GPU, N Mo de tampons », « GPU en cours de contrôle » ou « sur le processeur » (la raison du moteur en info-bulle) ; « Coupé (réglage accelerator : none) » quand le réglage le coupe. Le moteur la donne dans ses statistiques (`engine.stats.gpu` et `engine.stats.models[].gpu` de `/albireo/v1/status`, décrits dans `web/openapi.json`). Les tampons comptés sont ceux du moteur seulement (poids et clés et valeurs sont partagés avec le processeur, sans copie) : 199,7 Mo pour le 4B après son contrôle. La barre « Mémoire étendue (V²RAM) » de CUDA, que le moteur ne remplissait plus, est retirée, avec les noms CUDA et OpenVINO des composants tiers et les vitesses par défaut propres à ces moteurs. Vérifié dans Chrome sans fenêtre (capture de l'onglet), réglage `auto` puis `none`. Le GPU ne se coupe pas depuis la page : changer un réglage du service demande le jeton d'administration, que la page n'a pas (voir la reprise).

## Points à surveiller

- Le code NEON dlib est le chemin SIMD attendu sur ARM64; vérifier les tests CPU et les résultats des exemples sur le Mac réel.
- Les sorties numériques d'ARM64 peuvent différer de celles d'une machine x86; comparer avec des tolérances, sauf pour les chemins qui promettent explicitement un résultat déterministe.
- Les tests d'intégration du chat doivent couvrir `/`, `/v1/models`, `/v1/chat/completions`, le streaming, l'annulation, l'UTF-8 et le décodage d'images lorsque les codecs sont activés.
- La section « État constaté » est tenue à jour à chaque build vérifié sur le Mac ; un résultat qui n'y figure pas n'a pas été vérifié.
