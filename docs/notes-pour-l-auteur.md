# Notes pour l'auteur — retour du portage sur Mac Apple Silicon

Ces notes viennent d'un fork de Dlib-Transformer-extensions, [Aughra/Dlib-Transformer-extensions](https://github.com/Aughra/Dlib-Transformer-extensions). Son seul but est de faire tourner le projet sur les Mac à puce Apple. En le portant, on a rencontré quelques défauts dans le code d'origine :

- certains sont de vrais bugs, sur toutes les plateformes ;
- d'autres n'apparaissent que sur macOS ;
- quelques-uns sont de simples remarques.

Chaque entrée donne l'endroit exact dans l'original, ce qui se passe, comment le reproduire, et la correction appliquée (avec son commit dans le fork) ou une suggestion. Une suggestion est marquée **non testée** quand elle n'a pas été essayée.

Chaque entrée dit aussi **si elle concerne Windows** (ligne « Mac ou Windows ») :

- **Aussi sous Windows** : le défaut est dans du code ou un service communs à toutes les plateformes. Il devrait se reproduire sous Windows. Ces points peuvent faire l'objet d'une demande de fusion (PR) vers l'original.
- **Propre au Mac** (ou à Linux) : le défaut tient au système. Windows n'est pas concerné.

Le portage n'a pas de machine Windows : « aussi sous Windows » est déduit de la lecture du code, pas vérifié sur une machine Windows.

Pour chaque point « aussi sous Windows » qui se corrige dans le code, l'entrée donne le fichier à changer, le code avant et le code après la correction, et ce qui a été vérifié sur le Mac.

La dernière partie donne les vitesses mesurées sur le Mac, avant et après les noyaux NEON du fork, que vous voudrez peut-être reprendre.

- **Comparé à** : `Cydral/Dlib-Transformer-extensions`, branche `main`, commit `184ea00` (Albireo 1.0.12, Pseudalis 1.0.5). Les numéros de ligne renvoient à ce commit.
- **Machine** : MacBook Air M2 (8 cœurs, 24 Go, sans ventilateur), macOS 27 (Darwin 27.0.0, arm64), Apple clang 21.0.0 (clang-2100.3.34.2), CMake 4.1.2, Ninja.
- **Ce qui a été construit** : les variantes Linux d'Albireo et de Pseudalis (`--console`), comme le prévoient leurs fichiers CMake.

## Résumé

| N° | Nature | Où | En une ligne | Mac ou Windows |
|---|---|---|---|---|
| 1 | Bug, toutes plateformes | `albireo/tests/bench_runtime.cpp:92` | Un prompt de moins de 64 tokens fait lire le banc d'essai au-delà de son tableau. | **Aussi sous Windows** |
| 2 | Test fragile | `pseudalis/tools/test_linux.sh:202, 315, 328, 330` | Deux étapes attendent un temps fixe au lieu d'attendre le résultat, et échouent sur une machine lente. | Linux et Mac (script non lancé sous Windows) |
| 3 | Réglage du serveur | espace de publication d'Albireo | Le catalogue et chaque téléchargement reçoivent « This link is password protected. » et retombent sur leurs adresses officielles. | **Aussi sous Windows** (côté serveur) |
| 4 | Robustesse | `albireo/host/main.cpp:315`, `pseudalis/host/main.cpp:307` | En `--console`, un service lancé en arrière-plan par un script ne s'arrête pas par SIGINT sur macOS. | Propre au Mac |
| 5 | Avertissements | exemples de dlib, Pseudalis | Cinq avertissements du compilateur et un de l'éditeur de liens avec Apple clang. | Code commun, avertissements propres à clang ; voir l'entrée |
| 6 à 15 | Portabilité macOS | CMake, `platform.cpp`, `system_info.cpp`, `picopltf.h`, `drive_folders.cpp`, scripts de test | Erreurs de compilation, et chemins propres à Linux (`/proc`, `/sys`, `endian.h`, `st_mtim`, options x86). | Propre au Mac |
| 16 à 20 | Remarques | noyaux, pool de fils, mode économie d'énergie, seuil des produits en flottants, nombre de fils sur un processeur hybride | Pas de défaut, mais bon à savoir. | Voir chaque remarque |
| 21 | Remarque, sécurité | `albireo/common/platform.cpp:622, 663, 735`, `pseudalis/common/platform.cpp:622, 636, 700` | Hors de Windows, les trois contrôles de dossiers répondent toujours « oui ». | **Propre à Linux** (et au Mac) ; Windows non concerné |

---

## 1. Bugs et tests fragiles

### 1. `bench_runtime` lit au-delà de son prompt quand celui-ci compte moins de 64 tokens

- **Où** : `dlib/dlib/external/albireo/tests/bench_runtime.cpp`, ligne 92 (ligne 83 avant Albireo 1.0.12), dans la partie « Several sequences advancing together ».
- **Ce qui se passe** : chaque séquence parallèle reçoit les 64 premiers tokens du prompt, quelle que soit la longueur demandée sur la ligne de commande :

  ```cpp
  b.tokens.assign(tokens.begin(), tokens.begin() + 64);
  ```

  Avec `prompt_tokens < 64`, `tokens.begin() + 64` pointe au-delà de la fin du vecteur : c'est un comportement indéfini. Sur le Mac, le programme a lu des identifiants de tokens au hasard et s'est arrêté sur :

  ```
  libc++abi: terminating due to uncaught exception of type std::runtime_error: token identifier out of range
  ```

- **Reproduire** : `bench_runtime modele.gguf 8 32 2 2` (prompt de 32 tokens). Le résultat varie d'un lancement à l'autre, comme toujours avec une lecture hors du tableau : arrêt sur l'exception ci-dessus, arrêt sans la ligne « batched generation », ou fin normale avec des tokens lus au hasard. Pour une preuve nette, compiler `bench_runtime.cpp` avec AddressSanitizer (`-fsanitize=address`). Extrait de la sortie sur le Mac, avec le fichier de l'original au commit `0503bcb` (avant Albireo 1.0.12, d'où les numéros 83 et 48, devenus 92 et 57) copié sous le nom `bench_runtime.orig.cpp` :

  ```
  ==65634==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60c000005a00 ...
  READ of size 256 at 0x60c000005a00 thread T0
      #11 0x000100c0d4dc in main bench_runtime.orig.cpp:83
  0x60c000005a00 is located 0 bytes after 128-byte region [0x60c000005980,0x60c000005a00)
  allocated by thread T0 here:
      #7 0x000100c0cfd8 in main bench_runtime.orig.cpp:48
  ```

  La ligne 83 lit 256 octets (64 tokens) dans le tableau de 128 octets (32 tokens) alloué à la ligne 48.
- **Mac ou Windows** : **aussi sous Windows**. `bench_runtime.cpp` est le même partout, et le CMake le construit sous Windows (`bench_runtime`, `bench_runtime_<famille>`). La lecture hors du vecteur ne dépend pas du système. Non vérifié sur une machine Windows.
- **Fichier à corriger** : `dlib/dlib/external/albireo/tests/bench_runtime.cpp`.
- **Code avant** (lignes 85 à 94 de `upstream/main`) :

  ```cpp
      // Several sequences advancing together.
      std::vector<int> seqs;
      for (long p = 0; p < parallel; ++p)
      {
          const int s = rt.create_sequence();
          rt.reserve_sequence(s, 64 + steps);
          b.clear();
          b.tokens.assign(tokens.begin(), tokens.begin() + 64);
          b.segments.push_back({ s, 0, 64 });
          b.logit_rows.push_back(63);
  ```

- **Code après la correction** : les séquences parallèles prennent les 64 premiers tokens du prompt, ou tout le prompt s'il est plus court.

  `<algorithm>`, nécessaire à `std::min`, est déjà inclus (ligne 13) depuis Albireo 1.0.12.

  ```cpp
      // Several sequences advancing together, each after the first tokens of the prompt (64 at most).
      const long head = std::min<long>(64, prompt);
      std::vector<int> seqs;
      for (long p = 0; p < parallel; ++p)
      {
          const int s = rt.create_sequence();
          rt.reserve_sequence(s, head + steps);
          b.clear();
          b.tokens.assign(tokens.begin(), tokens.begin() + head);
          b.segments.push_back({ s, 0, head });
          b.logit_rows.push_back(head - 1);
  ```

- **Vérifié sur le Mac** (copie corrigée hors du dépôt, mêmes options que le build, plus `-Wall -Wextra`) : compile sans avertissement ; AddressSanitizer ne signale plus rien avec un prompt de 32 tokens ; `bench_runtime modele.gguf 8 <prompt> 4 2` va au bout avec 1, 32 et 64 tokens et affiche la ligne « batched generation ». À 64 tokens et plus, le comportement est celui d'avant (`head` vaut 64). **Non testé sous Windows.**
- **Non couvert par cette correction** : un prompt de 0 token (`prompt - 1` vaut alors -1 à la ligne 64) et `steps` à 0 (division par zéro aux lignes 81 et 110, qui affiche un temps infini ou NaN). On peut les refuser dans le message d'usage.
- **Ce que le fork a fait** : rien dans son code (pour ne pas créer de conflit avec l'original). On donne toujours au moins 64 tokens. La correction ci-dessus est prête pour une demande de fusion.

### 2. Deux étapes du test de Pseudalis attendent un temps fixe

- **Où** : `dlib/dlib/external/pseudalis/tools/test_linux.sh` :
  - ligne 202 (`sleep 8`), avant « drive signatures » ;
  - lignes 315 (`sleep 8`), 328 (`sleep 6`) et 330 (`sleep 4`), avant « copies on the drive ».
- **Ce qui se passe** : toutes les autres étapes du lecteur attendent leur résultat avec `wait_for <secondes> <vérification>`, en réessayant chaque seconde. Ces deux-là dorment un temps fixe, puis ne vérifient qu'une fois. Sur une machine lente, le lecteur n'a pas fini au moment de la vérification, et l'étape échoue alors que le service fonctionne.
- **Constaté sur le Mac** : un premier passage a donné 26 étapes sur 28, avec « drive signatures » et « copies on the drive » en échec. Il a très probablement tourné avec le mode économie d'énergie de macOS actif (voir la remarque 18), qui rend la machine environ dix fois plus lente. Ce n'est pas vérifié pour ce passage précis, mais le mode était actif lors des mesures faites juste après. Mode coupé : 28 sur 28, avec et sans les modifications du fork.
- **Correction suggérée** (non testée) : remplacer chaque `sleep` fixe par un `wait_for` sur la condition que l'étape vérifie ensuite, comme le font les autres étapes du lecteur.
- **Mac ou Windows** : Linux et Mac. Le script ne tourne pas sous Windows, et Pseudalis n'a pas de script de test pour Windows. Sur une machine Linux lente ou chargée, l'échec devrait se reproduire, d'après le code (non vérifié).

### 3. L'espace de publication d'Albireo répond « protégé par mot de passe »

- **Ce qui se passe** : au premier démarrage, le service écrit `publication space: set, without password` dans son journal. Ensuite, chaque fichier qu'il demande à cet espace lui est refusé, et il se rabat sur l'adresse officielle :

  ```
  [warning] models: catalog read from its official address https://raw.githubusercontent.com/Cydral/Dlib-Transformer-extensions/main/dist/albireo/catalog.json, the publication space of Albireo did not serve it (the publication space of Albireo answers "This link is password protected." (2258) for dist/catalog.json)
  ```

  Le même message apparaît pour `models/qwen3-embedding-0.6b/Qwen3-Embedding-0.6B-Q8_0.gguf` et pour `models/qwen3-vl-4b-thinking/Qwen3VL-4B-Thinking-Q4_K_M.gguf`. Ces deux fichiers sont alors venus de Hugging Face.
- **Constaté** : le 2026-11-07, avec Albireo Community Edition 1.0.11, un dossier de données neuf, et les réglages par défaut sauf `port` et `check_updates: false`.
- **Conséquence** : aucune pour l'utilisateur, puisque le repli marche. Mais si l'espace de publication doit servir ces fichiers, il faut vérifier son lien ou les réglages de son serveur. Si la protection est voulue, l'avertissement apparaît à chaque démarrage de chaque installation.
- **Mac ou Windows** : **aussi sous Windows**. La réponse vient du serveur de l'espace de publication, pas du code du Mac : toute installation qui le consulte devrait la recevoir. Non vérifié depuis Windows. C'est un réglage de serveur, pas une correction de code : aucun fichier du dépôt à changer, donc pas de code avant ni après à proposer.

### 4. En `--console`, un service lancé en arrière-plan ne s'arrête pas par SIGINT sur macOS

- **Où** : `run_console` dans `dlib/dlib/external/albireo/host/main.cpp` (`pthread_sigmask` ligne 315, `sigwait` ligne 319), et le même code dans `dlib/dlib/external/pseudalis/host/main.cpp` (lignes 307 et 311).
- **Ce qui se passe** :
  - `run_console` bloque SIGINT, SIGTERM et SIGHUP, et les attend avec `sigwait` dans un fil à part.
  - Un shell non interactif lance ses programmes d'arrière-plan avec SIGINT **ignoré** (règle POSIX).
  - Linux garde en attente un signal bloqué même quand il est ignoré : `sigwait` le reçoit.
  - macOS jette un signal ignoré, même bloqué : `sigwait` ne le voit jamais, et `kill -INT` reste sans effet.
- **Reproduire** (macOS) : `bash -c 'albireo-service --console &'`, puis `kill -INT <pid>`. Le service continue de tourner. Un `sample` du processus montre son fil de signaux toujours dans `__sigwait`. `kill -TERM` l'arrête en une seconde environ, et Ctrl-C dans un terminal marche.
- **Correction suggérée** (non testée) : avant `pthread_sigmask`, remettre les trois signaux à leur comportement par défaut, par exemple `signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL); signal(SIGHUP, SIG_DFL);`. Ainsi, `sigwait` les reçoit quel que soit le réglage hérité du parent.
- **Ce que le fork a fait** : rien dans le code. On arrête les services par SIGTERM.
- **Mac ou Windows** : propre au Mac. Sous Linux, `sigwait` reçoit le signal. Sous Windows, ce code n'est pas compilé : `run_console` y a sa propre version (la branche `#else` commence à la ligne 306 d'Albireo).

### 5. Avertissements avec Apple clang

| Où (original) | Avertissement |
|---|---|
| `dlib/examples/slm_transformer_configs_ex.cpp:883` et `:884` | `braces around scalar initializer [-Wbraced-scalar-init]`, dans `tokenizer.decode({ ref_subset[i] }, false)` et `tokenizer.decode({ gen_subset[i] }, false)` |
| `dlib/examples/slm_cygnus_instruct_ex.cpp:197` | `braces around scalar initializer [-Wbraced-scalar-init]`, dans `get_dataset_as_pairs({ dataset_id::BLACK_HOLE_QA_PARTA })` |
| `dlib/dlib/external/pseudalis/common/engine_loader.h:62` | `private field 'handle_' is not used [-Wunused-private-field]` : hors de Windows, le moteur est intégré au programme et `handle_` n'est jamais lu. |
| `dlib/dlib/data_io/language_model_data.h:185` (`is_text_content`) | `variable 'whitespace_count' set but not used [-Wunused-but-set-variable]` : la variable est incrémentée ligne 195 mais jamais lue. Vu en compilant le moteur de Pseudalis. |
| `dlib/dlib/image_processing/scan_image_pyramid.h:873` | `variable 'total_temp_score' set but not used [-Wunused-but-set-variable]` : la variable reçoit `+= max_val` ligne 940 mais n'est jamais lue. Vu en compilant le moteur de Pseudalis. |
| `dlib/examples/CMakeLists.txt:136` | `ld: warning: -s is obsolete` : l'éditeur de liens d'Apple ne fait plus le dépouillement avec `-s`. Sur macOS, `-Wl,-x` ou une étape `strip` le remplace. |

- **Mac ou Windows** :
  - les deux `-Wbraced-scalar-init` sont dans du code commun, mais c'est un avertissement de clang. MSVC ne le donne probablement pas (non vérifié) ; clang-cl sous Windows le donnerait. La correction, retirer les accolades (`tokenizer.decode(ref_subset[i], false)` si la fonction accepte un seul token, sinon `std::vector<int>{ ... }`), vaudrait pour toutes les plateformes (non testée) ;
  - `whitespace_count` et `total_temp_score` : code commun à toutes les plateformes ; clang et GCC donnent cet avertissement (`-Wall`), MSVC probablement pas (non vérifié). Correction suggérée (non testée) : supprimer la variable et la ligne qui la modifie (`language_model_data.h` lignes 185 et 195, `scan_image_pyramid.h` lignes 873 et 940), ou s'en servir si un usage était prévu ;
  - `handle_` : hors Windows seulement, puisque sous Windows le moteur est une DLL et `handle_` sert ;
  - `ld -s` : propre au Mac.

---

## 2. Portabilité vers macOS sur puce Apple

Aucun de ces points n'est un bug sous Windows ou Linux : ce sont les changements dont le fork a eu besoin. Chacun est protégé par une condition, pour que les builds Windows et Linux restent inchangés. Si vous voulez que le projet se construise sur Mac, ce sont les endroits à toucher.

**Mac ou Windows** : tous propres au Mac (le n° 15 concerne des scripts Linux lancés sur Mac). Rien ne se reproduit sous Windows. Une demande de fusion n'aurait de sens que si vous voulez accueillir le Mac dans l'original. Seule exception de principe : le piège du n° 7 (`0 == 0`) guette toute plateforme qui n'aurait pas `endian.h`.

### Erreurs de compilation (corrigées dans le commit [451c3fe](https://github.com/Aughra/Dlib-Transformer-extensions/commit/451c3fe) du fork)

| N° | Où (original) | Erreur sur le Mac | Correction dans le fork |
|---|---|---|---|
| 6 | `albireo/CMakeLists.txt:291` (`albireo_engine(cpu-avx2 avx2)`) et `pseudalis/CMakeLists.txt:136` (`pseudalis_engine(pseudalis-engine-avx2 ...)`) | `error: unsupported option '-mavx2' for target 'arm64-apple-darwin27.0.0'` (et `-mfma`, `-mf16c`) | Une branche `elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64\|aarch64\|ARM64)$")` qui pose `ALBIREO_ARM64` et une option AVX2 vide. Les moteurs AVX2 ne sont construits que hors ARM64. |
| 7 | `svox_pico/lib/picopltf.h:70–81` (le fork a depuis retiré la synthèse vocale, 2026-11-11 ; l'entrée reste valable pour l'original) | `error: 'endian.h' file not found` | `#elif defined(__APPLE__)` : `#include <machine/endian.h>`, `#define __BYTE_ORDER BYTE_ORDER`, `#define __BIG_ENDIAN BIG_ENDIAN`. Vérifié : `PICO_ENDIANNESS` vaut alors petit-boutiste. **Piège** : le test `#if __BYTE_ORDER == __BIG_ENDIAN` (ligne 81) choisit gros-boutiste sans aucune erreur quand ni `__BYTE_ORDER` ni `__BIG_ENDIAN` ne sont définis, puisque le préprocesseur compare alors `0 == 0`. C'est le cas sur macOS avec un simple `<machine/endian.h>`, qui ne définit que les noms sans soulignés. Un `#error` quand `__BYTE_ORDER` n'est pas défini protégerait toutes les plateformes. |
| 8 | `albireo/CMakeLists.txt:161` (`albireo_image`) | `Undefined symbols for architecture arm64: _png_init_filter_functions_neon` | Sur ARM64, ajout des sources `arm/arm_init.c`, `arm/filter_neon_intrinsics.c` et `arm/palette_neon_intrinsics.c` de libpng, comme le fait le CMake de dlib. |
| 9 | `dlib/dlib/CMakeLists.txt:959–964` (`dlib_httplib`) | `Undefined symbols: _CFRelease, _SecTrustSettingsCopyCertificates, ...` (httplib lit les certificats du Trousseau) | `if (APPLE) target_link_libraries(dlib_httplib INTERFACE "-framework CoreFoundation" "-framework Security")`. |
| 10 | `pseudalis/host/drive_folders.cpp:449, 486, 519` | `error: no member named 'st_mtim' in 'stat'` | `#if defined(__APPLE__) #define st_mtim st_mtimespec #endif`, après `#include <unistd.h>`. |

### Chemins propres à Linux, à l'exécution

| N° | Où (original) | Ce qui se passe sur le Mac | Correction dans le fork |
|---|---|---|---|
| 11 | `albireo/common/platform.cpp:83` et `pseudalis/common/platform.cpp:83` (`executable_path()`, `readlink("/proc/self/exe")`) | macOS n'a pas de `/proc`, donc le dossier du programme est vide. Albireo écrit `programs in ` (vide), `http: no web folder at web` et `engines: no usable engine in` : il ne sert ni son moteur ni sa page. Pseudalis, dont le moteur est intégré, répond 404 pour sa page quand on le lance depuis un autre dossier que `bin/`. | `#elif defined(__APPLE__)` : `_NSGetExecutablePath`, puis `realpath` ([496c025](https://github.com/Aughra/Dlib-Transformer-extensions/commit/496c025), [25715b5](https://github.com/Aughra/Dlib-Transformer-extensions/commit/25715b5)). Vérifié dans les deux sens : page de Pseudalis en 404 sans la correction, en 200 avec. |
| 12 | `albireo/host/main.cpp:329`, `pseudalis/host/main.cpp:321` (redémarrage dans un nouveau processus) | Même `readlink("/proc/self/exe")`. Il échoue, et le code se rabat sur `execvp(argv[0])`, qui devrait marcher quand le service a été lancé avec un chemin (non testé). | Albireo : `albireo::executable_path()` à la place de `readlink` ([982c951](https://github.com/Aughra/Dlib-Transformer-extensions/commit/982c951)). Compilé ; ce chemin (redémarrage dans un nouveau processus, quand un moteur de périphérique a démarré) ne se déclenche plus sur Mac, qui n'a que le moteur processeur, donc pas essayé. Les redémarrages dans le même processus marchent (essayés). Pseudalis : pas modifié. |
| 13 | `albireo/common/system_info.cpp:182` (`/proc/meminfo`) | Mémoire totale et disponible lues à 0 : l'état affiche `"memory":{"total":0,"available":0}`, et le moteur saute la borne qu'il met aux clés et valeurs selon la mémoire libre. | `#elif defined(__APPLE__)` : `sysctlbyname("hw.memsize")` pour le total, et les pages libres, inactives et purgeables de `host_statistics64` pour la mémoire disponible ([496c025](https://github.com/Aughra/Dlib-Transformer-extensions/commit/496c025)). |
| 14 | `albireo/common/system_info.cpp`, `read_cpu_info` (`/sys/devices` à la ligne 157) | Pas de nom de processeur : il vient de CPUID, qui n'existe que sur x86. Le nombre de cœurs retombe sur `hardware_concurrency()`, ce qui est juste sur puce Apple. | `sysctlbyname("machdep.cpu.brand_string")`, qui donne par exemple « Apple M2 ». |
| 15 | `albireo/tools/test_linux.sh:29` (`date +%s%N`), `pseudalis/tools/test_linux.sh:53` (`nproc`) et `:200` (`stat -c %s`) | Le `date` de macOS ne connaît pas `%N`, il n'y a pas de `nproc`, et le `stat` BSD n'a pas `-c`. Dans Pseudalis, la taille lue vaut alors 0, et « drive signatures » échoue pour cette seule raison. | Scripts non modifiés : on place en tête du `PATH` un `nproc` (`sysctl -n hw.ncpu`) et le `stat` de GNU (`gstat`, paquet Homebrew coreutils). |

---

## 3. Remarques

### 16. Le compilateur arrondit les sommes en flottants du code portable de deux façons

Les boucles portables de `qkernels.cpp` sont toutes écrites de la même façon, `t += a * b`. Apple clang 21 en `-O3` ne les traduit pas toutes de la même manière :

- **Dans `dot_f32_ref`**, chaque produit est arrondi, puis ajouté. C'est ce qu'a montré le noyau du changement 2 : la version fusionnée différait, la version en deux temps tombe juste.
- **Dans les scores de `dot_rows_x4`** (branche portable), le code machine vectorise les produits (`fmul.4s`), puis les ajoute un par un (`fadd`). Seuls les `n % 4` derniers termes passent par un `fmadd` fusionné.
- **Dans les valeurs de `add_weighted_rows`** (branche portable), la boucle lit `v` avec un pas de `hd`. Le compilateur garde une boucle scalaire de `fmadd` : un seul arrondi par terme.

**Mac ou Windows** : toutes plateformes, pour le principe : chaque compilateur fait ses propres choix (MSVC compris). Ce n'est pas un défaut à corriger.

Les résultats du build portable dépendent donc des choix du compilateur. Ils devraient aussi différer, au dernier chiffre, de ceux du build AVX2, qui utilise le FMA partout (non vérifié faute de machine x86). Ce n'est pas un défaut, puisque les tests ont des tolérances. Mais si un jour le but est d'obtenir la même réponse sur toutes les machines, il faudra écrire explicitement `std::fma`, ou un produit séparé, dans le code portable. Les noyaux NEON du fork reproduisent au bit près les résultats portables de ce build : ils ont dû suivre ces deux règles.

### 17. `ALBIREO_CPU_RELAX()` vaut `std::this_thread::yield()` hors x86

- **Où** : `albireo/engine/thread_pool.h:20`.
- Sur macOS, le `yield()` de la boucle d'attente fait tomber les fils en attente à la priorité 0 (vu avec `ps -M`).
- Essai avec l'instruction ARM `isb` à la place : les fils restent à la priorité 31, mais la génération n'est pas plus rapide en mesures alternées (5,2 et 5,5 tokens/s avec `isb`, 5,7 et 6,4 sans). Le code d'origine a été gardé, il n'y a rien à changer.
- **Mac ou Windows** : hors x86 seulement. Les PC Windows x86 utilisent `_mm_pause()`, ils ne sont pas concernés.

### 18. Le mode économie d'énergie de macOS fait croire que le moteur n'utilise qu'un cœur

Mode économie actif (`pmset -g` affiche `lowpowermode 1`), le moteur tournait environ dix fois moins vite et n'occupait qu'environ un cœur : 0,78 token/s au lieu de 7,9 pour `engine_chat` à 8 fils. C'est un réglage du Mac, pas d'Albireo. Mais un utilisateur de Mac qui trouve Albireo très lent sera probablement dans ce cas. Une ligne dans la documentation, ou un avertissement dans l'état du service, pourrait lui faire gagner du temps.

**Mac ou Windows** : propre au Mac. Un portable Windows en mode « économie d'énergie » ralentit aussi, mais ce point n'a pas été mesuré.

### 19. Sur le Mac, le seuil de 8 tokens des produits en flottants est trop bas

- **Où** : `albireo/engine/qkernels.h:137`, `const long qk_float_product_tokens = 8;`. À partir de ce nombre de tokens, `qk_matmul` décompresse les poids en flottants au lieu de quantifier les activations.
- **Constaté sur le Mac** (6 144 lignes de 2 048 colonnes, 8 fils, meilleur de 15 appels) : les noyaux entiers restent plus rapides que le produit en flottants bien au-delà de 8 tokens.

  | Tokens | Entiers, Q4_K | Flottants NEON (changement 2), Q4_K | Flottants Accelerate (changement 4), Q4_K |
  |---|---|---|---|
  | 8 | 0,84 à 0,99 ms | 1,64 à 1,68 ms | 2,33 à 2,46 ms |
  | 16 | 1,68 à 1,86 ms | 2,41 à 2,63 ms | 2,35 à 2,48 ms |
  | 24 | 2,68 à 2,83 ms | 3,36 à 3,53 ms | 2,56 à 2,63 ms |
  | 64 | 6,99 à 7,25 ms | 7,79 à 7,91 ms | 3,09 à 3,33 ms |

  Avec Q8_0, les entiers (noyau portable) gardent l'avantage sur Accelerate jusque vers 24 à 32 tokens.
- **Ce que le fork a fait** : sur Mac seulement (`__APPLE__` et `__ARM_FEATURE_DOTPROD`), le seuil passe à 24 ([a38c30d](https://github.com/Aughra/Dlib-Transformer-extensions/commit/a38c30d)). Une question de 21 tokens passe ainsi de 45,6 à 58,4 tokens/s en lecture (une seule paire de mesures).
- **Mac ou Windows** : la mesure est propre au Mac. Le commentaire du seuil semble viser les processeurs x86 ; il vaudrait peut-être la peine de le remesurer sur un PC Windows, par exemple avec les noyaux VNNI. Non vérifié.

### 20. Sur un processeur à cœurs rapides et lents, le nombre de fils par défaut compte aussi les cœurs lents

- **Où** : `albireo/engine/engine.cpp:959` (`if (threads_ <= 0) threads_ = cpu.physical_cores;`), avec `read_cpu_info` dans `albireo/common/system_info.cpp`.
- **Ce qui se passe** : le M2 a 4 cœurs de performance et 4 cœurs d'efficacité (`sysctl hw.perflevel0.physicalcpu` et `hw.perflevel1.physicalcpu` donnent 4 et 4). Sur le Mac, `physical_cores` vaut 8 (voir l'entrée 14), et le moteur prend 8 fils. Or écrire un token relit tous les poids : la mémoire est vite saturée, et à la fin de chaque produit les cœurs rapides attendent le dernier groupe de lignes pris par un cœur lent. Un profil de la génération du 4B à 8 fils donne 21 % des échantillons à cette attente (`swtch_pri`), et 47 % sur un contexte de 4 096 tokens.
- **Constaté sur le Mac** (`bench_runtime`, mesures alternées 8 / 4 / 8 / 4 fils, `lowpowermode 0`, charge 5 à 9, avec les changements 1 à 6 ; dernière série avec le changement 7 et les clés et valeurs en 8 bits) :

  | Cas | 8 fils | 4 fils |
  |---|---|---|
  | 4B, génération, 1 fil / 2 / 4 / 6 / 8 fils | 8,6 / 14,7 / **19,4** / 18,3 / 18,2 tokens/s (une série) | |
  | 4B, prompt de 64 tokens | 51,4 à 52,1 tokens/s | **58,0 à 59,6** |
  | 4B, prompt de 512 tokens | 91,2 à 92,7 tokens/s | 92,5 à 93,2 |
  | 4B, génération | 17,1 à 17,9 tokens/s | **17,7 à 19,0** |
  | 2B, prompt de 64 tokens | 113,2 à 126,0 tokens/s | **149,8 à 151,6** |
  | 2B, génération | 38,6 à 39,8 tokens/s | **41,2 à 42,0** |
  | 2B, prompt de 4 096 tokens | **163,3 à 169,3** tokens/s | 143,0 à 145,2 |
  | 2B, génération après 4 096 tokens | 22,7 à 22,9 tokens/s | 23,0 à 23,4 |
  | 4B, clés en 8 bits, prompt de 64 tokens | 45,9 à 49,4 tokens/s | **57,8 à 58,2** |
  | 4B, clés en 8 bits, génération | 15,8 à 16,2 tokens/s | **16,9 à 17,1** |
  | 2B, clés en 8 bits, prompt de 4 096 tokens | **154,0 à 162,7** tokens/s | 134,1 à 136,5 |

  Avec 4 fils, les questions courtes et la génération vont plus vite ; avec 8, les longs prompts, où le calcul domine, gardent l'avantage (12 à 18 %).
- **Ce que le fork a fait** : sur Mac, tous les cœurs pour les prompts et les cœurs de performance seuls pour la génération (changement 8, partie 4). L'autre piste, prendre par défaut le nombre de cœurs de performance (`hw.perflevel0.physicalcpu`), aurait coûté 12 à 18 % sur les longs prompts.
- **Mac ou Windows** : **probablement aussi sous Windows** sur les processeurs hybrides (Intel de 12ᵉ génération et suivants) : `GetLogicalProcessorInformationEx(RelationProcessorCore)` compte tous les cœurs, rapides et lents, alors que la structure renvoyée donne leur classe (`EfficiencyClass`). Déduit du code, non vérifié sur une machine Windows.


### 21. Hors de Windows, les contrôles de dossiers répondent toujours « oui »

- **Où dans l'original** : `dlib/dlib/external/albireo/common/platform.cpp`, fonctions `owned_by_system` (ligne 622), `secure_folder` (663) et `folder_admin_only` (735), au commit `184ea00` ; les mêmes dans `dlib/dlib/external/pseudalis/common/platform.cpp` (622, 636, 700).
- **Ce qui se passe** : sous Windows, ces fonctions vérifient vraiment le propriétaire et les droits (SYSTEM, administrateurs, entrées des listes de droits). Dans la branche `#else`, donc sous Linux et sur macOS, elles ne vérifient rien :

  ```cpp
  // albireo/common/platform.cpp, lignes 622-633 (184ea00)
  bool owned_by_system(const std::string& p)
  {
  #if defined(_WIN32)
      ...
  #else
      (void)p;
      return true;
  #endif
  }
  ```

  `folder_admin_only` (lignes 778-781) fait de même ; `secure_folder` (lignes 724-731) se contente de créer le dossier. Or le service s'en sert avant de charger un modèle (`host/service.cpp:1390`, le dossier du modèle ne doit pas être modifiable par un autre compte), pour son dossier d'import (`host/service.cpp:1088`), et Pseudalis avant de faire confiance au dossier de données d'Albireo et à son certificat (`pseudalis/host/albireo_client.cpp:122`). Sous Linux, un dossier de modèles ouvert en écriture à tous (`chmod 777`) est donc accepté sans un mot.
- **Reproduire** : sous Linux, lancer le service avec `ALBIREO_HOME` dans un dossier en mode 777, y placer un modèle, et constater qu'il se charge ; la même chose sous Windows est refusée (« lets accounts other than the administrators change its files »). Déduit du code ; pas exécuté sous Linux.
- **Ce que le fork fait** : sur macOS, les trois fonctions vérifient le propriétaire (le compte du service ou root), l'absence de droit d'écriture pour le groupe et les autres, sur l'entrée, son contenu et ses dossiers parents jusqu'à la racine (un dossier temporaire de root avec le bit sticky est accepté), refusent les liens, et `secure_folder` met le dossier en mode 0700 (commit [4c0947b](https://github.com/Aughra/Dlib-Transformer-extensions/commit/4c0947b)). Elles lisent aussi les ACL étendues de macOS : une entrée « allow » qui laisse un autre compte écrire est refusée, une entrée « deny » acceptée, et `secure_folder` vide une ACL dangereuse (commit [0d4946d](https://github.com/Aughra/Dlib-Transformer-extensions/commit/0d4946d)). Ce code est propre à macOS (`acl_get_link_np`, `mbr_uuid_to_id`) ; sous Linux, la partie POSIX conviendrait telle quelle, pas celle des ACL.
- **Vérifié** : sur le Mac, `test_host` d'Albireo (194 vérifications, dont 7 sur les ACL) et de Pseudalis passent, ainsi que `tools/test_macos.sh` (Albireo 18 sur 18, Pseudalis 34 sur 34) ; les données réelles du service passent les nouveaux contrôles. Pas vérifié sous Linux.
- **Mac ou Windows** : **propre à Linux** (et au Mac) ; Windows n'est pas concerné, ses contrôles sont complets.

---

## 4. Vitesse sur le Mac : temps d'origine, changements, nouveaux temps

Le fork ajoute des noyaux NEON pour puce Apple, dans un fichier à lui, `albireo/engine/qkernels_neon.inc`. `qkernels.cpp` l'inclut et l'appelle sous `ALBIREO_QK_NEON` (`__ARM_NEON` et `__ARM_FEATURE_DOTPROD`), par quelques lignes ajoutées. Le changement 4 ajoute un second fichier, `albireo/engine/qkernels_accelerate.inc`, sous `ALBIREO_QK_ACCELERATE` (NEON et `__APPLE__`). Aucune ligne du code portable ou x86 n'est modifiée.

**Mac ou Windows** : ces changements sont propres au Mac. Ils ne corrigent rien sous Windows.

**Changements 1 à 3 : les noyaux NEON donnent les mêmes résultats que le code portable du même build, au bit près**, et les réponses du modèle sont identiques, mot pour mot, à température 0. Les changements 6 et 7 le sont aussi ; le changement 8 donne les résultats d'un moteur à 4 threads en génération ; le changement 9 (GPU) calcule en flottants, à 0,04 % à 0,13 % près des logits du processeur en flottants. **Changement 4 : ce n'est plus vrai** pour les produits des prompts, calculés par Accelerate. Les sommes diffèrent au dernier chiffre, comme celles des noyaux AVX2 avec leur FMA, et une réponse longue peut être formulée autrement.

**Méthode de mesure** :
- MacBook Air M2, mode économie d'énergie coupé, charge moyenne notée, 8 fils ;
- les deux versions lancées en alternance (A, B, A, B) dans la même séance ;
- un changement n'est gardé que s'il gagne dans ces conditions ;
- modèle : Qwen3-VL 2B Instruct Q4_K_M, le fichier du catalogue (1 107 409 952 octets, SHA-256 `089d75c52f4b7ffc56ba998ffc50aae89fcafc755f9e7208aacca281dca6c2ae`), qui ne contient que des tenseurs Q4_K, Q6_K et F32.

### Point de départ : le moteur d'origine sur le Mac (noyaux portables)

| Programme | Prompt | Génération |
|---|---|---|
| `engine_chat`, prompt de 21 tokens, 88 tokens écrits | 9,2 tokens/s | 7,9 tokens/s |
| `bench_runtime modele 8 64 32 2` | 11,3 tokens/s | 8,3 tokens/s (120 ms par token) ; 2 séquences : 8,9 tokens/s |

Un profil (`sample`) montrait presque tout le temps dans `matmul_float` (prompt) et dans `qk_dot_portable` (génération).

### Changement 1 : `qk_dot` en NEON pour Q4_K et Q6_K (génération)

- **Quoi** : `dot_q4_K_neon` et `dot_q6_K_neon`, avec `vdotq_s32` (16 produits int8 et leur somme en une instruction). Ils calculent les mêmes sommes entières par bloc que `dot_q4_K_ref` et `dot_q6_K_ref`, exactes quel que soit l'ordre, puis finissent chaque bloc par la même expression flottante, dans le même ordre.
- **Exactitude** : 0 différence sur 12 000 produits contre `qk_dot_portable` (longueurs 256, 2 048 et 6 144). Même réponse, octet pour octet.
- **Commit du fork** : [4e452d8](https://github.com/Aughra/Dlib-Transformer-extensions/commit/4e452d8).

| Programme | Essai | Origine | NEON |
|---|---|---|---|
| `engine_chat`, génération | 1 | 10,9 tokens/s | 32,5 tokens/s |
| `engine_chat`, génération | 2 | 11,0 tokens/s | 36,9 tokens/s |
| `bench_runtime`, génération | 1 | 6,5 tokens/s | 33,7 tokens/s |
| `bench_runtime`, génération | 2 | 10,8 tokens/s | 32,9 tokens/s |
| `bench_runtime`, 2 séquences | 1 | 11,3 tokens/s | 43,2 tokens/s |
| `bench_runtime`, 2 séquences | 2 | 10,5 tokens/s | 41,5 tokens/s |

### Changement 2 : le produit en flottants de `gemm_rows` en NEON (prompt)

- **Quoi** : `gemm_neon<4>` (4 tokens contre 4 lignes de poids, chaque poids lu servant 4 tokens), et `gemm_neon<1>` pour les tokens restants. Chaque somme garde :
  - les quatre voies de `dot_f32_ref` ;
  - les colonnes restantes dans la première voie, puis `(s0 + s1) + (s2 + s3)` ;
  - ses arrondis : `vmulq_f32` puis `vaddq_f32`.

  Une première version avec `vfmaq_f32` changeait le dernier chiffre de 23 953 sommes sur 32 436, parce qu'Apple clang ne fusionne pas `dot_f32_ref`.
- **Exactitude** : 0 différence sur 32 436 produits contre `qk_dot_f32` (K = 64, 2 048 et 2 051 ; 16 et 37 lignes ; 8 à 64 tokens ; avec et sans accumulation). Même réponse que le moteur d'origine.
- **Commit du fork** : [87f7bbf](https://github.com/Aughra/Dlib-Transformer-extensions/commit/87f7bbf).

| Programme | Essai | Avant (changement 1 seul) | Après |
|---|---|---|---|
| `bench_runtime`, prompt de 64 tokens | 1 | 13,1 tokens/s | 38,3 tokens/s |
| `bench_runtime`, prompt de 256 tokens | 1 | 12,8 tokens/s | 50,4 tokens/s |
| `engine_chat`, prompt de 21 tokens | 1 | 4,6 tokens/s | 34,6 tokens/s |
| `bench_runtime`, prompt de 64 tokens | 2 | 10,8 tokens/s | 46,8 tokens/s |
| `bench_runtime`, prompt de 256 tokens | 2 | 10,3 tokens/s | 44,9 tokens/s |
| `engine_chat`, prompt de 21 tokens | 2 | 11,0 tokens/s | 39,1 tokens/s |

### Changement 3 : l'attention en NEON (longs contextes)

- **Pourquoi** : en résumant un PDF de 4,6 Mo dans la page web, avec Qwen3-VL 4B Thinking, la génération tombait à 3,4 tokens/s. Un profil montrait `qk_attention_values` (3 007 échantillons) et `qk_attention_scores` (2 807), ensemble deux fois plus lourds que `qk_dot` (2 737).
- **Quoi** : `attention_scores_neon` et `attention_values_neon`.
  - Les demi-flottants sont convertis par `vcvt_f32_f16`.
  - Pour les scores, les clés sont transposées par quatre (`vst4q_f32`) : une voie suit une clé. Pour les valeurs, une voie suit un élément.
  - Chaque somme garde l'ordre du code portable et les arrondis qu'Apple clang lui donne (remarque 16) : produits puis additions sur les `hd / 4 * 4` premiers termes d'un score, `fma` pour le reste, et `fma` pour chaque clé d'une valeur.
- **Exactitude** : 0 différence sur 549 760 résultats contre le code portable compilé avec les mêmes options (`hd` de 128, 64, 80, 72 et 6 ; 1 à 2 049 clés ; 1 à 9 lignes). Même réponse, mot pour mot, sur le résumé d'un texte de 4 329 tokens.
- **Commit du fork** : [9b964fd](https://github.com/Aughra/Dlib-Transformer-extensions/commit/9b964fd).

| Programme | Essai | Avant (changements 1 et 2) | Après |
|---|---|---|---|
| `bench_runtime modele 8 4096 32 1`, prompt de 4 096 tokens | 1 | 25,5 tokens/s (161 s) | 52,8 tokens/s (78 s) |
| idem, génération après 4 096 tokens | 1 | 9,0 tokens/s | 19,6 tokens/s |
| `bench_runtime`, prompt de 4 096 tokens | 2 | 24,5 tokens/s (167 s) | 50,0 tokens/s (82 s) |
| idem, génération après 4 096 tokens | 2 | 7,5 tokens/s | 22,0 tokens/s |
| `engine_chat`, résumé de 4 329 tokens, prompt | 1 | 23,1 tokens/s | 47,0 tokens/s |
| idem, génération | 1 | 7,9 tokens/s | 16,9 tokens/s |

### Changement 4 : les produits des prompts par Accelerate

- **Pourquoi** : pour le modèle de recherche (Q8_0), un profil du prompt montrait le produit en flottants (`matmul_float`, donc `gemm_neon`) à 16 512 échantillons, et la décompression `gguf_dq_q8_0` à 494 seulement. Un noyau Q8_0 n'aurait gagné que 2 à 3 %. Le levier était le produit lui-même.
- **Quoi** : à partir de 16 tokens, `matmul_float` appelle `cblas_sgemm` d'Accelerate, la bibliothèque d'Apple, qui calcule sur le coprocesseur matriciel de la puce.
  - Un seul gros appel est bien plus rapide que beaucoup de petits appels venus des fils du pool : 1 060 Gflop/s pour un produit 512 × 2 048 × 6 144, contre 315 par blocs de 16 lignes sur 8 fils. Le coprocesseur est partagé par les cœurs d'un groupe.
  - Les poids sont donc décompressés en flottants par tranches de 256 lignes. Pendant qu'Accelerate multiplie une tranche (tâche 0 du pool, prise par le fil appelant), les autres fils décompressent la suivante.
  - Tranches essayées : 64, 128, 256 et 512 lignes ; 256 est la meilleure.
  - Sous 16 tokens, le coût fixe de la décompression rend Accelerate plus lent que `gemm_neon`, qui reste en place pour ces cas (les experts d'un mélange reçoivent parfois peu de tokens).
  - Le seuil des noyaux entiers passe de 8 à 24 tokens sur Mac (remarque 19).
- **Exactitude** : plus au bit près. `test_qkernels` passe (il compare `qk_matmul` à une référence en double précision, avec la tolérance de l'original, de 8 à 300 tokens), ainsi que `test_model_file`, `test_chat_format`, `test_engine_api` et `test_host`. Sur le résumé de 4 329 tokens, la réponse est formulée autrement mais garde le même sens. Une question courte donne toujours « La capitale de la France est **Paris**. »
- **Dépendance** : le framework Accelerate de macOS (`link_libraries("-framework Accelerate")` dans la branche ARM64 du CMake, sous `if (APPLE)`).
- **Commit du fork** : [a38c30d](https://github.com/Aughra/Dlib-Transformer-extensions/commit/a38c30d).

| Programme (8 fils, `lowpowermode 0`, charge 2 à 14) | Essai | Avant (changements 1 à 3) | Après |
|---|---|---|---|
| `bench_runtime`, 2B, prompt de 64 tokens | 1 | 62,8 tokens/s | 133,7 tokens/s |
| `bench_runtime`, 2B, prompt de 512 tokens | 1 | 71,7 tokens/s | 217,5 tokens/s |
| `bench_runtime`, modèle de recherche Qwen3 Embedding 0.6B (Q8_0), 512 tokens | 1 | 212,5 tokens/s | 600,9 tokens/s |
| `bench_runtime`, 2B, prompt de 4 096 tokens | 1 | 57,5 tokens/s (71 s) | 162,8 tokens/s (25 s) |
| `bench_runtime`, 2B, prompt de 64 tokens | 2 | 60,4 tokens/s | 114,1 tokens/s |
| `bench_runtime`, 2B, prompt de 512 tokens | 2 | 64,2 tokens/s | 216,8 tokens/s |
| `bench_runtime`, modèle de recherche, 512 tokens | 2 | 183,7 tokens/s | 572,8 tokens/s |
| `bench_runtime`, 2B, prompt de 4 096 tokens | 2 | 51,6 tokens/s (79 s) | 155,9 tokens/s (26 s) |
| `engine_chat`, résumé de 4 329 tokens, prompt | 1 | 51,7 tokens/s | 134,9 tokens/s |

La génération, un token à la fois, ne passe pas par ce chemin : elle est inchangée (32 à 36 tokens/s avant comme après, sur une question courte).

### Changement 5 : l'attention de l'encodeur d'images par Accelerate

- **Pourquoi** : après le changement 4, l'encodage d'une image de 512 × 512 pixels par le projecteur de Qwen3-VL 4B (Q8_0) prenait encore 6,6 à 8,5 s. Un profil montrait `qk_encoder_attention` à 20 143 échantillons, et tout le reste à quelques centaines. Sur le Mac, `encoder_scores` et `encoder_values` n'ont que leur forme portable : la boucle des notes lit `Kt` en travers de ses lignes, un flottant à la fois.
- **Quoi** : sous `ALBIREO_QK_ACCELERATE`, les deux produits de chaque tuile (`S = Q Kt`, puis `O += P V`) sont confiés à `cblas_sgemm` (`encoder_scores_accelerate`, `encoder_values_accelerate` dans `qkernels_accelerate.inc`), appelés en tête de `encoder_scores` et `encoder_values` (deux fois quatre lignes). Les tuiles sont petites (64 requêtes, 128 clés) et les fils du pool les appellent ensemble, ce qui coûte à Accelerate une part de son débit, mais il reste loin devant les boucles portables.
- **Exactitude** : aucun test de l'original ne couvre `qk_encoder_attention`. Contrôle du fork contre une attention calculée en double précision, sur 84 cas (`hd` 64, 72, 80 et 128 ; 1 à 1 100 clés ; 1 à 64 lignes) : plus grand écart absolu 3,0e-6 avec Accelerate, 4,0e-6 avec le code portable. Les descriptions de deux images sont identiques, mot pour mot, avant et après ce changement.
- **Commit du fork** : [72091df](https://github.com/Aughra/Dlib-Transformer-extensions/commit/72091df).

| `engine_chat`, 4B Thinking et son projecteur, image 512 × 512 (16 × 16 tokens), « Décris cette image en une phrase. » | Essai | Avant (changement 4) | Après |
|---|---|---|---|
| Zèbre, encodage de l'image | 1 | 6 668 ms | 2 101 ms |
| Perroquet, encodage de l'image | 1 | 7 749 ms | 2 181 ms |
| Zèbre, encodage de l'image | 2 | 8 495 ms | 1 781 ms |
| Perroquet, encodage de l'image | 2 | 8 526 ms | 1 886 ms |
| Lecture complète (image et 277 tokens de prompt) | 1 et 2 | 10,7 à 12,9 s | 5,9 à 7,3 s |

Pour mémoire, avec les changements 1 à 3 seulement : encodage de 8,9 et 10,3 s, lecture complète de 20,2 et 23,3 s (une mesure par image).

### Changement 6 : les noyaux Q4_K et Q6_K du changement 1 gardent leurs sommes en vecteurs

- **Pourquoi** : en génération, un profil du 4B (8 fils) donne 75 % des échantillons à `qk_dot` et 21 % à l'attente des fils (`swtch_pri`). Les noyaux du changement 1 réduisaient chaque groupe de 32 valeurs (Q4_K) ou de 16 (Q6_K) à un entier par `vaddvq_s32`, puis le multipliaient par son échelle en scalaire, et décodaient les échelles et les minimums de Q4_K un par un (`get_scale_min_k4`).
- **Quoi** : dans `qkernels_neon.inc` seulement. Chaque groupe est multiplié par son échelle dans le vecteur (`vmlaq_n_s32`), et un seul `vaddvq_s32` termine le bloc de 256 valeurs. Les huit échelles et les huit minimums de Q4_K sont décodés d'un coup, par masques sur trois mots de 32 bits. La somme des minimums passe par `vpaddq_s16` et `vmull_s16` sur `bsums`.
- **Exactitude** : ce sont les mêmes sommes entières, exactes quel que soit l'ordre des additions, et la même fin de bloc en flottants. Le résultat reste identique au bit près au code portable. Contrôle du fork : 0 différence sur 43 200 produits, avec de vrais poids du 2B et du 4B (plongement Q6_K, `attn_q`, `attn_v`, `attn_output`, `ffn_gate`, `ffn_up`, `ffn_down`) et des activations de six formes (normales, très étalées, minuscules, avec des pics). `test_qkernels` passe.
- **Commit du fork** : [791a211](https://github.com/Aughra/Dlib-Transformer-extensions/commit/791a211).

| `bench_runtime modele 8 64 64 1` (8 fils, `lowpowermode 0`, charge 3,2 à 7,1) | Essai | Avant (changements 1 à 5) | Après |
|---|---|---|---|
| 2B, génération | 1 | 35,4 tokens/s (28,2 ms) | 38,9 tokens/s (25,7 ms) |
| 4B Thinking, génération | 1 | 17,0 tokens/s (58,8 ms) | 18,1 tokens/s (55,4 ms) |
| 2B, génération | 2 | 36,8 tokens/s (27,2 ms) | 38,2 tokens/s (26,2 ms) |
| 4B Thinking, génération | 2 | 16,5 tokens/s (60,4 ms) | 18,0 tokens/s (55,5 ms) |

Le gain est de 4 à 9 %. Essayé aussi, et rejeté : des groupes de lignes plus petits en génération (`rows / (threads * 32)` au lieu de `* 8`), pour que les cœurs rapides attendent moins les cœurs lents à la fin de chaque produit. La génération du 4B tombe de 18,1 à 17,0 tokens/s, et à 13,7 avec `* 128`.

### Changement 7 : l'attention en NEON pour les clés et valeurs en 8 bits (Albireo 1.0.12)

- **Pourquoi** : depuis Albireo 1.0.12, le service range par défaut les clés et valeurs des modèles de langue en entiers de 8 bits (`kv_type` « auto »), par groupes de 32 avec une échelle en demi-précision. Les nouvelles fonctions `qk_attention_scores_q8` et `qk_attention_values_q8` n'ont, hors AVX2, que leur forme portable, et le changement 3 ne servait plus que les clés en demi-précision. Juste après la fusion de l'original, avec les clés en 8 bits, un prompt de 4 096 tokens retombait à 36 à 38 tokens/s, contre 163 en demi-précision.
- **Quoi** : dans `qkernels_neon.inc`, les deux fonctions du changement 3 deviennent des modèles qui reçoivent une « source » de lignes : demi-précision ou 8 bits. La source 8 bits déplie huit entiers à la fois (`vmovl_s8`, `vcvtq_f32_s32`, `vmulq_n_f32` par l'échelle du groupe), à l'endroit ou transposés par quatre clés comme au changement 3. Le calcul qui suit est celui du changement 3. Dans `qkernels.cpp`, quatre lignes sous `ALBIREO_QK_NEON` en tête de chacune des deux fonctions `_q8` (lignes 2529 et 2537 de `upstream/main`).
- **Exactitude** : un entier de 8 bits multiplié par une échelle en demi-précision tient exactement dans un flottant, donc les valeurs dépliées sont celles de `qk_q8_to_float_row`, au bit près. Contrôle du fork (build NEON contre build portable compilé avec `-U__ARM_FEATURE_DOTPROD`) : 0 différence sur 351 780 valeurs (224 cas : `hd` 64 et 128 ; 1 à 1 100 clés ; 1 à 16 lignes ; clés en demi-précision et en 8 bits). `test_qkernels` (avec les nouveaux contrôles 8 bits de l'original) passe. `engine_chat` répond juste avec les clés en 8 bits.

| `bench_runtime modele 8 <prompt> <pas> 1 q8` (8 fils, `lowpowermode 0`, charge 2,7 à 9,9) | Essai | Avant (fusion d'Albireo 1.0.12, changements 1 à 6) | Après |
|---|---|---|---|
| 4B Thinking, prompt de 64 tokens | 1 | 48,2 tokens/s | 52,2 tokens/s |
| 4B Thinking, génération | 1 | 16,4 tokens/s (61,1 ms) | 17,8 tokens/s (56,2 ms) |
| 2B, prompt de 4 096 tokens | 1 | 37,7 tokens/s (109 s) | 162,3 tokens/s (25 s) |
| 2B, génération après 4 096 tokens | 1 | 9,1 tokens/s | 22,6 tokens/s |
| 4B Thinking, prompt de 64 tokens | 2 | 47,2 tokens/s | 50,5 tokens/s |
| 4B Thinking, génération | 2 | 16,6 tokens/s (60,2 ms) | 17,8 tokens/s (56,1 ms) |
| 2B, prompt de 4 096 tokens | 2 | 36,0 tokens/s (114 s) | 157,7 tokens/s (26 s) |
| 2B, génération après 4 096 tokens | 2 | 8,9 tokens/s | 22,5 tokens/s |

Pour comparaison, au même moment, les clés en demi-précision : prompt de 4 096 tokens à 165,6 tokens/s, génération à 21,9 (une mesure). Sur le Mac, le format 8 bits ne coûte donc plus rien en vitesse, et il divise par deux la mémoire des clés et valeurs.

**Mac ou Windows** : le changement est propre au Mac. Mais toute machine sans AVX2 (Linux sur ARM, par exemple) passe par les formes portables de ces deux fonctions, et le constat « quatre fois plus lent sur un long prompt » devrait s'y retrouver. Déduit du code, non vérifié.

### Changement 8 : la génération sur les cœurs de performance, les prompts sur tous les cœurs

- **Pourquoi** : voir la remarque 20. Sur le M2, 4 threads écrivent plus vite que 8, mais 8 lisent plus vite un long prompt.
- **Quoi** : sur Mac seulement, un thread pool de plus de threads que de cœurs de performance garde une seconde équipe, plus petite, d'un thread par cœur de performance (`hw.perflevel0.physicalcpu`). Le thread qui ouvre les sections d'une passe la demande par un objet `fast_team_scope` ; ses sections passent alors sur cette équipe, toujours une section à la fois sur les deux équipes, et les threads de l'autre équipe, sans travail, s'endorment vite : le système garde sur les cœurs de performance les threads qui calculent. `cpu_runtime::forward` demande la petite équipe pour une passe de moins de `qk_float_product_tokens` tokens (24 sur Mac), c'est-à-dire la génération et les questions très courtes, dont les produits passent par les noyaux entiers, limités par la mémoire.
- **Où** (`upstream/main`, `184ea00`) :
  - `albireo/engine/thread_pool.h` : `<memory>` et `<sys/sysctl.h>` après la ligne 21 ; la création de l'équipe à la fin du constructeur ; après la ligne 79 (`size()`), l'inclusion de `thread_pool_apple.inc` (fichier du fork), ou `int active() const { return size(); }` hors Mac ; après la ligne 95, `if (n_tasks > 1 && run_on_fast_team(n_tasks, fn)) return;`. Tout est sous `#if defined(__APPLE__)`, sauf `active()`, qui vaut `size()` ailleurs.
  - `albireo/engine/qkernels.cpp:2147` et `albireo/engine/cpu_runtime.cpp:531-532` : `pool.size()` devient `pool.active()`, le nombre de threads de la section (groupes de lignes de `qk_matmul`, découpage de l'attention de la génération). Hors Mac, c'est le même nombre.
  - `albireo/engine/cpu_runtime.cpp:856` : la demande, sous `#if defined(__APPLE__)`, en tête de `forward`.
- **Exactitude** : chaque produit reste le même. Le découpage de l'attention de la génération entre les threads (Albireo 1.0.12) dépend du nombre de threads actifs, si bien que les résultats sont ceux d'un moteur réglé à 4 threads, au dernier chiffre près, comme avec le réglage `threads` à 4. Tests de l'original passés ; `engine_chat` répond juste (deux questions, température 0).

| `bench_runtime modele 8 <prompt> <pas> 1 q8` (`lowpowermode 0`, charge 2,7 à 8,4) | Essai | Avant (8 threads, changements 1 à 7) | Après | Repère : 4 threads |
|---|---|---|---|---|
| 4B Thinking, génération | 1 | 18,1 tokens/s | 18,6 tokens/s | 18,5 tokens/s |
| idem | 2 | 17,9 tokens/s | 18,2 tokens/s | 18,3 tokens/s |
| idem | 3 | 17,7 tokens/s | 18,4 tokens/s | 17,8 tokens/s |
| idem | 4 | 17,6 tokens/s | 18,3 tokens/s | 18,2 tokens/s |
| 2B, génération | 1 et 2 | 37,9 et 37,4 tokens/s | 39,7 et 40,8 tokens/s | |
| 2B, prompt de 4 096 tokens | 1 et 2 | 173,1 et 163,2 tokens/s | 167,0 et 161,8 tokens/s | 134 à 137 (mesures de la remarque 20) |
| 2B, génération après 4 096 tokens | 1 et 2 | 22,9 et 22,4 tokens/s | 24,1 et 22,4 tokens/s | |

Le gain est de 2 à 6 % en génération ; les longs prompts gardent la vitesse des 8 threads. Avec `engine_chat`, une question de 22 tokens (petite équipe) est lue à 88 tokens/s, une de 25 tokens (toute l'équipe) à 61 (une mesure chacune, pas alternée).

**Mac ou Windows** : le changement est propre au Mac. L'idée vaudrait pour les processeurs hybrides des PC (remarque 20), avec la classe des cœurs que donne Windows ; non essayé.

### Changement 9 : les pas d'écriture sur le GPU du Mac, par Metal

- **Pourquoi** : le moteur GPU de l'original passe par CUDA, absent du Mac. Sur une puce Apple, le GPU partage la mémoire du processeur : il lit les poids et les clés et valeurs là où ils sont, sans copie. Une sonde a mesuré son plafond sur le M2 : lire les poids du 4B en 35 ms, contre 51 à 55 pour le processeur ; mais un aller-retour coûte 186 µs, si bien qu'il faut lui donner le token entier, pas un produit à la fois.
- **Quoi** (fichiers propres au fork) : `engine/metal_device.{h,mm}` (le GPU, ses tampons, les commandes d'une passe), `engine/metal_kernels.metal` (produits Q4_K et Q6_K pour 1 à 8 tokens, normes, positions tournantes, écriture des clés et valeurs, attention en ligne par morceaux), `engine/cpu_runtime_metal.cpp` (la passe entière en une commande, le contrôle et le repli), `tests/test_metal.cpp`. La source Metal est compilée par macOS à l'ouverture du GPU : la construction ne demande pas de compilateur Metal. Le cache des clés et valeurs (`kv_cache.h`) range ses blocs dans une seule zone réservée, que le GPU prend sans copie.
- **Exactitude** : le GPU calcule en flottants : ses logits diffèrent de ceux du processeur, qui arrondit ses activations en 8 bits, d'environ 5 % sur le 2B ; face au processeur en produits flottants (`qk_path::floats`), de 0,04 % à 0,13 %, avec le même token le plus probable. Le moteur fait ce contrôle au chargement du modèle et rend les passes au processeur s'il échoue. Deux questions donnent les mêmes réponses mot pour mot.
- **Une leçon de Metal** : des tableaux locaux indexés par des nombres connus à l'exécution seulement (le nombre de têtes groupées, de tokens) sortent des registres ; en gabarits instanciés par taille, l'attention d'un token passe de 9,2 à 1,7 ms et les produits de 48,8 à 43,4 ms.

| `bench_runtime modele 8 64 64 1 q8 [metal]` (`lowpowermode 0`, charge 4,1 à 8,7) | Essai | Processeur (changements 1 à 8) | GPU |
|---|---|---|---|
| 4B Thinking, génération | 1 | 19,0 tokens/s (52,6 ms) | 21,3 tokens/s (47,0 ms) |
| 4B Thinking, génération | 2 | 18,3 tokens/s (54,6 ms) | 21,7 tokens/s (46,0 ms) |
| 2B, génération | 1 | 40,8 tokens/s | 49,4 tokens/s |
| 2B, génération | 2 | 35,7 tokens/s | 49,6 tokens/s |
| 2B, génération après 4 096 tokens | 1 et 2 | 23,1 et 22,0 tokens/s | 28,5 et 26,5 tokens/s |

**Mac ou Windows** : propre au Mac (Metal, mémoire partagée).

### Changement 10 : la lecture des prompts sur le GPU du Mac

- **Pourquoi** : Accelerate multiplie un prompt sur le coprocesseur matriciel du processeur (0,84 à 0,98 TFLOPS mesurés sur un produit 512 × 2 560 × 9 728) ; le GPU du M2, par Metal Performance Shaders, en fait 2,3.
- **Quoi** : dans `cpu_runtime_metal.cpp`, les passes de 24 tokens ou plus (`qk_float_product_tokens`) dont le contexte final tient en 2 048 positions passent entières sur le GPU ; chaque matrice est décompressée en floats sur le GPU (identique à `qk_dequantize_row`) puis multipliée en float32. Au-delà, l'attention de prompt du GPU, qui lit les clés un token à la fois, perd sur le 2B à 4 096 tokens : le processeur reprend.
- **Exactitude** : contrôle au chargement sur un prompt de 32 tokens : logits à 0,23 % (2B) et 0,11 % (4B) du processeur en flottants. Avec les clés et valeurs en 8 bits, un écart de 1e-7 fait parfois arrondir une valeur à l'entier voisin, et ces sauts se propagent de couche en couche (relevé octet par octet : identiques à la couche 0) ; d'où une tolérance de 5 % pour un prompt en 8 bits dans le contrôle, le même token étant exigé.

| `bench_runtime modele 8 <prompt> 32 1 q8 [metal]` (`lowpowermode 0`, charge 3,0 à 5,2) | Essai | Processeur (changements 1 à 9) | GPU |
|---|---|---|---|
| 4B Thinking, prompt de 64 tokens | 1 et 2 | 53,0 et 54,5 tokens/s | 92,7 et 100,0 tokens/s |
| 4B Thinking, prompt de 512 tokens | 1 et 2 | 93,5 et 91,5 tokens/s | 153,3 et 155,5 tokens/s |
| 2B, prompt de 64 tokens | 1 et 2 | 135,9 et 132,0 tokens/s | 263,5 et 262,0 tokens/s |
| 2B, prompt de 512 tokens | 1 et 2 | 220,2 et 224,4 tokens/s | 421,8 et 417,6 tokens/s |

**Mac ou Windows** : propre au Mac.

### Changement 11 : l'empreinte SHA-256 par CommonCrypto, et les copies de modèles par clone APFS

- **Pourquoi** : chaque téléchargement, vérification, import ou restauration de modèle relit tout le fichier pour son SHA-256. `sha256_stream` (`albireo/host/crypto.cpp`, lignes 29-53 au commit `184ea00`) passe par `mbedtls_sha256_*`, en logiciel : la configuration de mbedtls n'active pas les instructions SHA du processeur (`MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT` est commentée dans `mbedtls_config.h`).
- **Quoi** : sur Mac, `sha256_stream` passe par CommonCrypto (`CC_SHA256_Init/Update/Final`), qui se sert des instructions SHA de la puce Apple ; même interface, même résultat. Les copies de modèles (`copy_open_file`, et la réserve du fork) deviennent des clones APFS (`fclonefileat` depuis le fichier déjà ouvert) : instantanés, sans place prise tant qu'aucun des deux fichiers ne change ; le clone est relu et haché comme une copie (commit [6af1326](https://github.com/Aughra/Dlib-Transformer-extensions/commit/6af1326)).
- **Exactitude** : même empreinte au bit près (`474ecaf1…` pour le décodeur du 4B, identique à celle du catalogue) ; tests `test_host` d'Albireo et de Pseudalis passés.

| Sur le M2 (`lowpowermode 0`, charge 2,3 à 3,6), fichier de 2 497 281 472 octets en cache, mesures alternées | mbedtls | CommonCrypto |
|---|---|---|
| Essais 1 à 4 | 8,66 à 8,81 s (283 à 288 Mo/s) | 1,17 à 1,21 s (2 059 à 2 141 Mo/s) |

| Service, Qwen3-VL 4B Thinking et son projecteur (2,8 Go), restauration hors ligne depuis la réserve | Copie + mbedtls (4.1.0) | Clone + CommonCrypto |
|---|---|---|
| Durée | 23 s | 6,1 et 6,8 s |
| Place disque prise | 2,8 Go | 0 |

**Mac ou Windows** : le clone est **propre au Mac** (APFS). L'empreinte lente est **probablement aussi sous Windows** : mbedtls n'y a pas non plus d'instructions SHA (il ne sait utiliser que celles d'ARMv8), et chaque modèle téléchargé y est haché en logiciel. Suggestion **non testée** : `BCryptHash` de Windows (CNG), qui se sert des instructions SHA des processeurs x86 récents. Déduit du code ; rien n'a été mesuré sous Windows.

### Changement 12 : Qwen 3.5 (architecture `qwen35`), sur le processeur puis sur le GPU

- **Pourquoi** : Qwen 3.5 est l'architecture GGUF la plus téléchargée que le moteur refusait (relevé de 527 dépôts populaires de Hugging Face : 69 dépôts, 58,8 millions de téléchargements). Ce n'est pas un défaut de l'original, qui ne la lit pas : c'est un ajout du fork, consigné ici parce qu'il ajoute des noyaux.
- **Quoi** : trois couches sur quatre sont des couches « Gated DeltaNet » (attention linéaire à état récurrent de taille fixe par séquence), la quatrième une attention à porte de sortie. Formule reprise de llama.cpp (`src/models/qwen35.cpp`, `delta-net-base.cpp`, `ggml-cpu/ops.cpp`, commit `cd959bae9`). Sur le processeur : `cpu_runtime::linear_attention()` ; sur le GPU : noyaux `gdn_conv`, `gdn_l2norm`, `gdn_gates`, `gdn_delta`, `gdn_norm`, `split_gate`, `sigmoid_mul`, et produits `mv_q5_K`, `mv_q8_0` (`metal_kernels.metal`). Les clés et valeurs ne sont gardées que pour les couches d'attention ; une séquence ne se reprend qu'à 0 ou à sa longueur (`runtime::resumable()`), l'état ne revenant pas en arrière. Commits [df56d5c](https://github.com/Aughra/Dlib-Transformer-extensions/commit/df56d5c) (processeur) et [780e69e](https://github.com/Aughra/Dlib-Transformer-extensions/commit/780e69e) (GPU).
- **Exactitude** : comparé à llama.cpp 0.9.0 sur processeur, en choix glouton sur 150 tokens : texte identique pour Qwen3.5-4B Q4_K_M ; pour Qwen3.5-0.8B Q8_0, divergences seulement sur des candidats à égalité au centième (par exemple 0,464 contre 0,446). GPU contre processeur : logits à 0,03 à 0,04 % au contrôle de chargement, mêmes réponses.

| Qwen3.5-4B Q4_K_M, écriture, 200 tokens (`lowpowermode 0`, charge 4,1 à 4,9), mesures alternées | Essai | Processeur | GPU |
|---|---|---|---|
| `engine_chat --reasoning off --temperature 0 --max 200` | 1 | 12,2 tokens/s | 19,5 tokens/s |
| | 2 | 11,7 tokens/s | 19,5 tokens/s |

**Mac ou Windows** : propre au fork (une architecture que l'original ne lit pas) ; le code du processeur est commun, celui du GPU propre au Mac.

### Bilan

| Sur le M2, 8 fils, modèle 2B | Moteur d'origine | Avec les noyaux du fork |
|---|---|---|
| Génération, question courte | environ 7 à 11 tokens/s | 49,4 à 49,6 tokens/s sur le GPU (changement 9) ; 39,7 à 40,8 sur le processeur (37 à 39 avec les changements 1 à 7, 33 à 37 avec 1 à 5) |
| Prompt de 64 tokens | environ 8 à 13 tokens/s | 262 à 264 tokens/s sur le GPU (changement 10) ; 114 à 136 sur le processeur (38 à 50 avec les changements 1 à 3) |
| Prompt de 512 tokens | pas mesuré | 418 à 422 tokens/s sur le GPU ; 217 à 224 sur le processeur (64 à 72 avec les changements 1 à 3) |
| Prompt de 4 096 tokens | pas mesuré | 156 à 166 tokens/s, en demi-précision ou, avec le changement 7, en 8 bits (36 à 38 en 8 bits sans lui ; 50 à 58 avec les changements 1 à 3, 25 avec 1 et 2) |
| Génération après 4 096 tokens | pas mesuré | 26,5 à 28,5 tokens/s sur le GPU ; 20 à 23 sur le processeur (9 en 8 bits sans le changement 7 ; 7,5 à 9 avec les changements 1 et 2 seuls) |
| Modèle de recherche (Q8_0), 512 tokens | pas mesuré | 573 à 601 tokens/s (184 à 213 avec les changements 1 à 3) |
| Encodage d'une image 512 × 512, projecteur du 4B | pas mesuré | 1,8 à 2,2 s (8,9 à 10,3 avec les changements 1 à 3) |
| 4B Thinking (modèle du service), clés en 8 bits, génération | pas mesuré | 21,3 à 21,7 tokens/s sur le GPU ; 18,2 à 18,6 sur le processeur (17,6 à 18,1 sans le changement 8 ; 16,4 à 16,6 sans les changements 6 à 8) |
| SHA-256 d'un fichier de modèle (vérification, import, restauration) | 283 à 288 Mo/s (mbedtls) | 2 059 à 2 141 Mo/s (changement 11) |
| Qwen3.5-4B Q4_K_M (architecture lue par le fork seulement), génération | non lu par le moteur | 19,5 tokens/s sur le GPU ; 11,7 à 12,2 sur le processeur (changement 12) |

Le service, avec son modèle par défaut Qwen3-VL 4B Thinking, répond à une question courte à 14 tokens/s (mesuré avant le changement 4). En génération, les autres formats de poids (Q8_0, Q5_K…) passent encore par les noyaux entiers portables sur le Mac ; en prompt, depuis le changement 4, tous les formats passent par Accelerate, et l'attention de l'encodeur d'images aussi depuis le changement 5.

---

*Écrit pour l'auteur de l'original et tenu à jour par le fork : les entrées les plus récentes peuvent se trouver en fin de section. Ce document ne contient aucun chemin de machine privée, aucune clé et aucun jeton.*
