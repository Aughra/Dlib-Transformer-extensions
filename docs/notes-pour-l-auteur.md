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

- **Comparé à** : `Cydral/Dlib-Transformer-extensions`, branche `main`, commit `8743fcd`. Les numéros de ligne renvoient à ce commit.
- **Machine** : MacBook Air M2 (8 cœurs, 24 Go, sans ventilateur), macOS 27 (Darwin 27.0.0, arm64), Apple clang 21.0.0 (clang-2100.3.34.2), CMake 4.1.2, Ninja.
- **Ce qui a été construit** : les variantes Linux d'Albireo et de Pseudalis (`--console`), comme le prévoient leurs fichiers CMake.

## Résumé

| N° | Nature | Où | En une ligne | Mac ou Windows |
|---|---|---|---|---|
| 1 | Bug, toutes plateformes | `albireo/tests/bench_runtime.cpp:83` | Un prompt de moins de 64 tokens fait lire le banc d'essai au-delà de son tableau. | **Aussi sous Windows** |
| 2 | Test fragile | `pseudalis/tools/test_linux.sh:199, 303, 316, 318` | Deux étapes attendent un temps fixe au lieu d'attendre le résultat, et échouent sur une machine lente. | Linux et Mac (script non lancé sous Windows) |
| 3 | Réglage du serveur | espace de publication d'Albireo | Le catalogue et chaque téléchargement reçoivent « This link is password protected. » et retombent sur leurs adresses officielles. | **Aussi sous Windows** (côté serveur) |
| 4 | Robustesse | `albireo/host/main.cpp:315`, `pseudalis/host/main.cpp:307` | En `--console`, un service lancé en arrière-plan par un script ne s'arrête pas par SIGINT sur macOS. | Propre au Mac |
| 5 | Avertissements | exemples de dlib, Pseudalis | Trois avertissements du compilateur et un de l'éditeur de liens avec Apple clang. | Code commun, avertissements propres à clang ; voir l'entrée |
| 6 à 15 | Portabilité macOS | CMake, `platform.cpp`, `system_info.cpp`, `picopltf.h`, `drive_folders.cpp`, scripts de test | Erreurs de compilation, et chemins propres à Linux (`/proc`, `/sys`, `endian.h`, `st_mtim`, options x86). | Propre au Mac |
| 16 à 19 | Remarques | noyaux, pool de fils, mode économie d'énergie, seuil des produits en flottants | Pas de défaut, mais bon à savoir. | Voir chaque remarque |

---

## 1. Bugs et tests fragiles

### 1. `bench_runtime` lit au-delà de son prompt quand celui-ci compte moins de 64 tokens

- **Où** : `dlib/dlib/external/albireo/tests/bench_runtime.cpp`, ligne 83, dans la partie « Several sequences advancing together ».
- **Ce qui se passe** : chaque séquence parallèle reçoit les 64 premiers tokens du prompt, quelle que soit la longueur demandée sur la ligne de commande :

  ```cpp
  b.tokens.assign(tokens.begin(), tokens.begin() + 64);
  ```

  Avec `prompt_tokens < 64`, `tokens.begin() + 64` pointe au-delà de la fin du vecteur : c'est un comportement indéfini. Sur le Mac, le programme a lu des identifiants de tokens au hasard et s'est arrêté sur :

  ```
  libc++abi: terminating due to uncaught exception of type std::runtime_error: token identifier out of range
  ```

- **Reproduire** : `bench_runtime modele.gguf 8 32 2 2` (prompt de 32 tokens). Le résultat varie d'un lancement à l'autre, comme toujours avec une lecture hors du tableau : arrêt sur l'exception ci-dessus, arrêt sans la ligne « batched generation », ou fin normale avec des tokens lus au hasard. Pour une preuve nette, compiler `bench_runtime.cpp` avec AddressSanitizer (`-fsanitize=address`). Extrait de la sortie sur le Mac, le fichier de l'original copié sous le nom `bench_runtime.orig.cpp` :

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
- **Code avant** (lignes 12 et 76 à 85 de `upstream/main`) :

  ```cpp
  #include <chrono>
  ```

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

  ```cpp
  #include <algorithm>
  #include <chrono>
  ```

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
- **Non couvert par cette correction** : un prompt de 0 token (`prompt - 1` vaut alors -1 à la ligne 55) et `steps` à 0 (division par zéro aux lignes 72 et 101, qui affiche un temps infini ou NaN). On peut les refuser dans le message d'usage.
- **Ce que le fork a fait** : rien dans son code (pour ne pas créer de conflit avec l'original). On donne toujours au moins 64 tokens. La correction ci-dessus est prête pour une demande de fusion.

### 2. Deux étapes du test de Pseudalis attendent un temps fixe

- **Où** : `dlib/dlib/external/pseudalis/tools/test_linux.sh` :
  - ligne 199 (`sleep 8`), avant « drive signatures » ;
  - lignes 303 (`sleep 8`), 316 (`sleep 6`) et 318 (`sleep 4`), avant « copies on the drive ».
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
| `dlib/examples/CMakeLists.txt:136` | `ld: warning: -s is obsolete` : l'éditeur de liens d'Apple ne fait plus le dépouillement avec `-s`. Sur macOS, `-Wl,-x` ou une étape `strip` le remplace. |

- **Mac ou Windows** :
  - les deux `-Wbraced-scalar-init` sont dans du code commun, mais c'est un avertissement de clang. MSVC ne le donne probablement pas (non vérifié) ; clang-cl sous Windows le donnerait. La correction, retirer les accolades (`tokenizer.decode(ref_subset[i], false)` si la fonction accepte un seul token, sinon `std::vector<int>{ ... }`), vaudrait pour toutes les plateformes (non testée) ;
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
| 7 | `svox_pico/lib/picopltf.h:70–81` | `error: 'endian.h' file not found` | `#elif defined(__APPLE__)` : `#include <machine/endian.h>`, `#define __BYTE_ORDER BYTE_ORDER`, `#define __BIG_ENDIAN BIG_ENDIAN`. Vérifié : `PICO_ENDIANNESS` vaut alors petit-boutiste. **Piège** : le test `#if __BYTE_ORDER == __BIG_ENDIAN` (ligne 81) choisit gros-boutiste sans aucune erreur quand ni `__BYTE_ORDER` ni `__BIG_ENDIAN` ne sont définis, puisque le préprocesseur compare alors `0 == 0`. C'est le cas sur macOS avec un simple `<machine/endian.h>`, qui ne définit que les noms sans soulignés. Un `#error` quand `__BYTE_ORDER` n'est pas défini protégerait toutes les plateformes. |
| 8 | `albireo/CMakeLists.txt:161` (`albireo_image`) | `Undefined symbols for architecture arm64: _png_init_filter_functions_neon` | Sur ARM64, ajout des sources `arm/arm_init.c`, `arm/filter_neon_intrinsics.c` et `arm/palette_neon_intrinsics.c` de libpng, comme le fait le CMake de dlib. |
| 9 | `dlib/dlib/CMakeLists.txt:959–964` (`dlib_httplib`) | `Undefined symbols: _CFRelease, _SecTrustSettingsCopyCertificates, ...` (httplib lit les certificats du Trousseau) | `if (APPLE) target_link_libraries(dlib_httplib INTERFACE "-framework CoreFoundation" "-framework Security")`. |
| 10 | `pseudalis/host/drive_folders.cpp:448, 485, 518` | `error: no member named 'st_mtim' in 'stat'` | `#if defined(__APPLE__) #define st_mtim st_mtimespec #endif`, après `#include <unistd.h>`. |

### Chemins propres à Linux, à l'exécution

| N° | Où (original) | Ce qui se passe sur le Mac | Correction dans le fork |
|---|---|---|---|
| 11 | `albireo/common/platform.cpp:83` et `pseudalis/common/platform.cpp:83` (`executable_path()`, `readlink("/proc/self/exe")`) | macOS n'a pas de `/proc`, donc le dossier du programme est vide. Albireo écrit `programs in ` (vide), `http: no web folder at web` et `engines: no usable engine in` : il ne sert ni son moteur ni sa page. Pseudalis, dont le moteur est intégré, répond 404 pour sa page quand on le lance depuis un autre dossier que `bin/`. | `#elif defined(__APPLE__)` : `_NSGetExecutablePath`, puis `realpath` ([496c025](https://github.com/Aughra/Dlib-Transformer-extensions/commit/496c025), [25715b5](https://github.com/Aughra/Dlib-Transformer-extensions/commit/25715b5)). Vérifié dans les deux sens : page de Pseudalis en 404 sans la correction, en 200 avec. |
| 12 | `albireo/host/main.cpp:329`, `pseudalis/host/main.cpp:321` (redémarrage dans un nouveau processus) | Même `readlink("/proc/self/exe")`. Il échoue, et le code se rabat sur `execvp(argv[0])`, qui devrait marcher quand le service a été lancé avec un chemin (non testé). | Pas modifié. `executable_path()` pourrait servir ici. |
| 13 | `albireo/common/system_info.cpp:180` (`/proc/meminfo`) | Mémoire totale et disponible lues à 0 : l'état affiche `"memory":{"total":0,"available":0}`, et le moteur saute la borne qu'il met aux clés et valeurs selon la mémoire libre. | `#elif defined(__APPLE__)` : `sysctlbyname("hw.memsize")` pour le total, et les pages libres, inactives et purgeables de `host_statistics64` pour la mémoire disponible ([496c025](https://github.com/Aughra/Dlib-Transformer-extensions/commit/496c025)). |
| 14 | `albireo/common/system_info.cpp`, `read_cpu_info` (`/sys/devices` à la ligne 157) | Pas de nom de processeur : il vient de CPUID, qui n'existe que sur x86. Le nombre de cœurs retombe sur `hardware_concurrency()`, ce qui est juste sur puce Apple. | `sysctlbyname("machdep.cpu.brand_string")`, qui donne par exemple « Apple M2 ». |
| 15 | `albireo/tools/test_linux.sh:29` (`date +%s%N`), `pseudalis/tools/test_linux.sh:50` (`nproc`) et `:197` (`stat -c %s`) | Le `date` de macOS ne connaît pas `%N`, il n'y a pas de `nproc`, et le `stat` BSD n'a pas `-c`. Dans Pseudalis, la taille lue vaut alors 0, et « drive signatures » échoue pour cette seule raison. | Scripts non modifiés : on place en tête du `PATH` un `nproc` (`sysctl -n hw.ncpu`) et le `stat` de GNU (`gstat`, paquet Homebrew coreutils). |

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

---

## 4. Vitesse sur le Mac : temps d'origine, changements, nouveaux temps

Le fork ajoute des noyaux NEON pour puce Apple, dans un fichier à lui, `albireo/engine/qkernels_neon.inc`. `qkernels.cpp` l'inclut et l'appelle sous `ALBIREO_QK_NEON` (`__ARM_NEON` et `__ARM_FEATURE_DOTPROD`), par quelques lignes ajoutées. Le changement 4 ajoute un second fichier, `albireo/engine/qkernels_accelerate.inc`, sous `ALBIREO_QK_ACCELERATE` (NEON et `__APPLE__`). Aucune ligne du code portable ou x86 n'est modifiée.

**Mac ou Windows** : ces changements sont propres au Mac. Ils ne corrigent rien sous Windows.

**Changements 1 à 3 : les noyaux NEON donnent les mêmes résultats que le code portable du même build, au bit près**, et les réponses du modèle sont identiques, mot pour mot, à température 0. **Changement 4 : ce n'est plus vrai** pour les produits des prompts, calculés par Accelerate. Les sommes diffèrent au dernier chiffre, comme celles des noyaux AVX2 avec leur FMA, et une réponse longue peut être formulée autrement.

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

### Bilan

| Sur le M2, 8 fils, modèle 2B | Moteur d'origine | Avec les noyaux du fork |
|---|---|---|
| Génération, question courte | environ 7 à 11 tokens/s | 33 à 37 tokens/s |
| Prompt de 64 tokens | environ 8 à 13 tokens/s | 114 à 134 tokens/s (38 à 50 avec les changements 1 à 3) |
| Prompt de 512 tokens | pas mesuré | 217 tokens/s (64 à 72 avec les changements 1 à 3) |
| Prompt de 4 096 tokens | pas mesuré | 156 à 163 tokens/s (50 à 58 avec les changements 1 à 3, 25 avec 1 et 2) |
| Génération après 4 096 tokens | pas mesuré | 20 à 22 tokens/s (7,5 à 9 avec les changements 1 et 2 seuls) |
| Modèle de recherche (Q8_0), 512 tokens | pas mesuré | 573 à 601 tokens/s (184 à 213 avec les changements 1 à 3) |
| Encodage d'une image 512 × 512, projecteur du 4B | pas mesuré | 1,8 à 2,2 s (8,9 à 10,3 avec les changements 1 à 3) |

Le service, avec son modèle par défaut Qwen3-VL 4B Thinking, répond à une question courte à 14 tokens/s (mesuré avant le changement 4). En génération, les autres formats de poids (Q8_0, Q5_K…) passent encore par les noyaux entiers portables sur le Mac ; en prompt, depuis le changement 4, tous les formats passent par Accelerate, et l'attention de l'encodeur d'images aussi depuis le changement 5.

---

*Écrit pour l'auteur de l'original et tenu à jour par le fork : les entrées les plus récentes peuvent se trouver en fin de section. Ce document ne contient aucun chemin de machine privée, aucune clé et aucun jeton.*
