# Notes pour l'auteur — points qui concernent aussi Windows

Ces notes sont extraites de [notes-pour-l-auteur.md](notes-pour-l-auteur.md), issu d'un fork de Dlib-Transformer-extensions, [Aughra/Dlib-Transformer-extensions](https://github.com/Aughra/Dlib-Transformer-extensions), dont le but est de faire tourner le projet sur les Mac à puce Apple.

Ce document ne garde que les points qui **concernent Windows**, seuls ou en même temps que le Mac : défauts dans du code ou un service communs à toutes les plateformes. Tout ce qui est propre au Mac ou à Linux (chemins `/proc`, Metal, NEON, Accelerate, signaux POSIX, scripts shell) est laissé dans le document complet.

Les numéros d'entrée sont ceux du document complet, pour pouvoir recouper ; ils ne se suivent donc pas.

Le portage n'a pas de machine Windows : « aussi sous Windows » est **déduit de la lecture du code, pas vérifié sur une machine Windows**. Les essais cités ont été faits sur un Mac, dans une copie hors du dépôt. Une suggestion est marquée **non testée** quand elle n'a pas été essayée.

- **Comparé à** : `Cydral/Dlib-Transformer-extensions`, branche `main`, commit `184ea00` (Albireo 1.0.12, Pseudalis 1.0.5). Les numéros de ligne renvoient à ce commit.
- **Machine des essais** : MacBook Air M2 (arm64), macOS 27, Apple clang 21.0.0, CMake 4.1.2, Ninja.

## Résumé

| N° | Nature | Où | En une ligne | Mac ou Windows |
|---|---|---|---|---|
| 1 | Bug, toutes plateformes | `albireo/tests/bench_runtime.cpp:92` | Un prompt de moins de 64 tokens fait lire le banc d'essai au-delà de son tableau. | **Aussi sous Windows** |
| 3 | Réglage du serveur | espace de publication d'Albireo | Le catalogue et chaque téléchargement reçoivent « This link is password protected. » et retombent sur leurs adresses officielles. | **Aussi sous Windows** (côté serveur) |
| 5 (en partie) | Avertissements | exemples de dlib, `language_model_data.h`, `scan_image_pyramid.h` | Quatre avertissements de clang (`-Wbraced-scalar-init`, `-Wunused-but-set-variable`) dans du code commun. | Code commun ; MSVC ne les donne probablement pas (non vérifié) |
| 7 (piège) | Portabilité | `svox_pico/lib/picopltf.h:81` | `#if __BYTE_ORDER == __BIG_ENDIAN` choisit gros-boutiste en silence quand ni l'un ni l'autre n'est défini. | Principe valable partout |
| 16 | Remarque | noyaux de `qkernels.cpp` | Le compilateur arrondit les sommes en flottants à sa façon : les résultats varient d'un compilateur à l'autre. | Toutes plateformes |
| 19 | Remarque | `albireo/engine/qkernels.h:137` | Le seuil de 8 tokens des produits en flottants est trop bas sur le Mac ; à remesurer sur PC. | Mesure propre au Mac, à remesurer |
| 20 | Remarque | `albireo/engine/engine.cpp:959` | Sur un processeur hybride, le nombre de fils par défaut compte aussi les cœurs lents. | **Probablement aussi sous Windows** |
| 21 | Remarque, fonction du fork | `albireo/host/server.cpp:1029, 1458` | La réserve locale du fork doit contourner le refus hors ligne de la route. | Code commun ; pas un défaut de l'original |
| 22 | Correction propre au fork | `albireo/host/public_link.cpp:457` ; `common/log.cpp` dans le fork | Une transcription du fork empêchait de compiler le masquage des paramètres d'adresse. | Code commun ; erreur introduite par le fork |

---

## 1. Bugs

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

- **Reproduire** : `bench_runtime modele.gguf 8 32 2 2` (prompt de 32 tokens). Le résultat varie d'un lancement à l'autre, comme toujours avec une lecture hors du tableau : arrêt sur l'exception ci-dessus, arrêt sans la ligne « batched generation », ou fin normale avec des tokens lus au hasard. Pour une preuve nette, compiler `bench_runtime.cpp` avec AddressSanitizer (`-fsanitize=address`). Extrait de la sortie, avec le fichier de l'original au commit `0503bcb` (avant Albireo 1.0.12, d'où les numéros 83 et 48, devenus 92 et 57) copié sous le nom `bench_runtime.orig.cpp` :

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

- **Code après la correction** : les séquences parallèles prennent les 64 premiers tokens du prompt, ou tout le prompt s'il est plus court. `<algorithm>`, nécessaire à `std::min`, est déjà inclus (ligne 13) depuis Albireo 1.0.12.

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

### 3. L'espace de publication d'Albireo répond « protégé par mot de passe »

- **Ce qui se passe** : au premier démarrage, le service écrit `publication space: set, without password` dans son journal. Ensuite, chaque fichier qu'il demande à cet espace lui est refusé, et il se rabat sur l'adresse officielle :

  ```
  [warning] models: catalog read from its official address https://raw.githubusercontent.com/Cydral/Dlib-Transformer-extensions/main/dist/albireo/catalog.json, the publication space of Albireo did not serve it (the publication space of Albireo answers "This link is password protected." (2258) for dist/catalog.json)
  ```

  Le même message apparaît pour `models/qwen3-embedding-0.6b/Qwen3-Embedding-0.6B-Q8_0.gguf` et pour `models/qwen3-vl-4b-thinking/Qwen3VL-4B-Thinking-Q4_K_M.gguf`. Ces deux fichiers sont alors venus de Hugging Face.
- **Constaté** : le 2026-11-07, avec Albireo Community Edition 1.0.11, un dossier de données neuf, et les réglages par défaut sauf `port` et `check_updates: false`.
- **Conséquence** : aucune pour l'utilisateur, puisque le repli marche. Mais si l'espace de publication doit servir ces fichiers, il faut vérifier son lien ou les réglages de son serveur. Si la protection est voulue, l'avertissement apparaît à chaque démarrage de chaque installation.
- **Mac ou Windows** : **aussi sous Windows**. La réponse vient du serveur de l'espace de publication, pas du code du Mac : toute installation qui le consulte devrait la recevoir. Non vérifié depuis Windows. C'est un réglage de serveur, pas une correction de code : aucun fichier du dépôt à changer, donc pas de code avant ni après à proposer.

### 5 (en partie). Avertissements de clang dans du code commun

Seuls les avertissements situés dans du code commun à toutes les plateformes sont repris ici (les deux autres du document complet, `handle_` hors Windows et `ld -s`, ne concernent pas Windows).

| Où (original) | Avertissement |
|---|---|
| `dlib/examples/slm_transformer_configs_ex.cpp:883` et `:884` | `braces around scalar initializer [-Wbraced-scalar-init]`, dans `tokenizer.decode({ ref_subset[i] }, false)` et `tokenizer.decode({ gen_subset[i] }, false)` |
| `dlib/examples/slm_cygnus_instruct_ex.cpp:197` | `braces around scalar initializer [-Wbraced-scalar-init]`, dans `get_dataset_as_pairs({ dataset_id::BLACK_HOLE_QA_PARTA })` |
| `dlib/dlib/data_io/language_model_data.h:185` (`is_text_content`) | `variable 'whitespace_count' set but not used [-Wunused-but-set-variable]` : la variable est incrémentée ligne 195 mais jamais lue. Vu en compilant le moteur de Pseudalis. |
| `dlib/dlib/image_processing/scan_image_pyramid.h:873` | `variable 'total_temp_score' set but not used [-Wunused-but-set-variable]` : la variable reçoit `+= max_val` ligne 940 mais n'est jamais lue. Vu en compilant le moteur de Pseudalis. |

- **Mac ou Windows** :
  - les deux `-Wbraced-scalar-init` sont dans du code commun, mais c'est un avertissement de clang. MSVC ne le donne probablement pas (non vérifié) ; clang-cl sous Windows le donnerait. La correction, retirer les accolades (`tokenizer.decode(ref_subset[i], false)` si la fonction accepte un seul token, sinon `std::vector<int>{ ... }`), vaudrait pour toutes les plateformes (non testée) ;
  - `whitespace_count` et `total_temp_score` : code commun à toutes les plateformes ; clang et GCC donnent cet avertissement (`-Wall`), MSVC probablement pas (non vérifié). Correction suggérée (non testée) : supprimer la variable et la ligne qui la modifie (`language_model_data.h` lignes 185 et 195, `scan_image_pyramid.h` lignes 873 et 940), ou s'en servir si un usage était prévu.

---

## 2. Portabilité : un piège qui guette toute plateforme

### 7 (piège). `#if __BYTE_ORDER == __BIG_ENDIAN` dans `picopltf.h`

- **Où** : `svox_pico/lib/picopltf.h`, ligne 81 (le fork a depuis retiré la synthèse vocale, 2026-11-11 ; l'entrée reste valable pour l'original).
- **Ce qui se passe** : le test `#if __BYTE_ORDER == __BIG_ENDIAN` choisit gros-boutiste **sans aucune erreur** quand ni `__BYTE_ORDER` ni `__BIG_ENDIAN` ne sont définis, puisque le préprocesseur compare alors `0 == 0`. Sur macOS, c'est ce qui arrivait avec un simple `<machine/endian.h>`, qui ne définit que les noms sans soulignés.
- **Correction suggérée** (non testée) : un `#error` quand `__BYTE_ORDER` n'est pas défini protégerait toutes les plateformes, y compris une chaîne d'outils Windows qui n'aurait pas `endian.h`.
- **Mac ou Windows** : l'erreur de compilation d'origine (`'endian.h' file not found`) est propre au Mac ; le piège du `0 == 0` est un principe valable partout. Non vérifié sous Windows.

---

## 3. Remarques

### 16. Le compilateur arrondit les sommes en flottants du code portable de deux façons

Les boucles portables de `qkernels.cpp` sont toutes écrites de la même façon, `t += a * b`. Apple clang 21 en `-O3` ne les traduit pas toutes de la même manière :

- **Dans `dot_f32_ref`**, chaque produit est arrondi, puis ajouté.
- **Dans les scores de `dot_rows_x4`** (branche portable), le code machine vectorise les produits (`fmul.4s`), puis les ajoute un par un (`fadd`). Seuls les `n % 4` derniers termes passent par un `fmadd` fusionné.
- **Dans les valeurs de `add_weighted_rows`** (branche portable), la boucle lit `v` avec un pas de `hd`. Le compilateur garde une boucle scalaire de `fmadd` : un seul arrondi par terme.

**Mac ou Windows** : toutes plateformes, pour le principe : chaque compilateur fait ses propres choix (MSVC compris). Ce n'est pas un défaut à corriger.

Les résultats du build portable dépendent donc des choix du compilateur. Ils devraient aussi différer, au dernier chiffre, de ceux du build AVX2, qui utilise le FMA partout (non vérifié faute de machine x86). Ce n'est pas un défaut, puisque les tests ont des tolérances. Mais si un jour le but est d'obtenir la même réponse sur toutes les machines, il faudra écrire explicitement `std::fma`, ou un produit séparé, dans le code portable.

### 19. Le seuil de 8 tokens des produits en flottants est trop bas sur le Mac ; à remesurer sur PC

- **Où** : `albireo/engine/qkernels.h:137`, `const long qk_float_product_tokens = 8;`. À partir de ce nombre de tokens, `qk_matmul` décompresse les poids en flottants au lieu de quantifier les activations.
- **Constaté sur le Mac** (6 144 lignes de 2 048 colonnes, 8 fils, meilleur de 15 appels) : les noyaux entiers restent plus rapides que le produit en flottants bien au-delà de 8 tokens (Q4_K : 0,84 à 0,99 ms à 8 tokens et 6,99 à 7,25 ms à 64 tokens avec les entiers, contre 1,64 à 1,68 ms et 7,79 à 7,91 ms avec le produit en flottants NEON du fork).
- **Mac ou Windows** : la mesure est propre au Mac. Le commentaire du seuil semble viser les processeurs x86 ; il vaudrait peut-être la peine de le remesurer sur un PC Windows, par exemple avec les noyaux VNNI. Non vérifié.

### 20. Sur un processeur à cœurs rapides et lents, le nombre de fils par défaut compte aussi les cœurs lents

- **Où** : `albireo/engine/engine.cpp:959` (`if (threads_ <= 0) threads_ = cpu.physical_cores;`), avec `read_cpu_info` dans `albireo/common/system_info.cpp`.
- **Ce qui se passe** : le M2 a 4 cœurs de performance et 4 cœurs d'efficacité, et le moteur prend 8 fils. Or écrire un token relit tous les poids : la mémoire est vite saturée, et à la fin de chaque produit les cœurs rapides attendent le dernier groupe de lignes pris par un cœur lent. Un profil de la génération du 4B à 8 fils donne 21 % des échantillons à cette attente, et 47 % sur un contexte de 4 096 tokens.
- **Constaté sur le Mac** (`bench_runtime`, mesures alternées 8 / 4 / 8 / 4 fils, mode économie d'énergie coupé) :

  | Cas | 8 fils | 4 fils |
  |---|---|---|
  | 4B, prompt de 64 tokens | 51,4 à 52,1 tokens/s | **58,0 à 59,6** |
  | 4B, génération | 17,1 à 17,9 tokens/s | **17,7 à 19,0** |
  | 2B, prompt de 64 tokens | 113,2 à 126,0 tokens/s | **149,8 à 151,6** |
  | 2B, génération | 38,6 à 39,8 tokens/s | **41,2 à 42,0** |
  | 2B, prompt de 4 096 tokens | **163,3 à 169,3** tokens/s | 143,0 à 145,2 |

  Avec 4 fils, les questions courtes et la génération vont plus vite ; avec 8, les longs prompts, où le calcul domine, gardent l'avantage (12 à 18 %).
- **Mac ou Windows** : **probablement aussi sous Windows** sur les processeurs hybrides (Intel de 12ᵉ génération et suivants) : `GetLogicalProcessorInformationEx(RelationProcessorCore)` compte tous les cœurs, rapides et lents, alors que la structure renvoyée donne leur classe (`EfficiencyClass`). Déduit du code, non vérifié sur une machine Windows.
- **Piste** (non essayée sous Windows) : l'idée du fork sur Mac, une petite équipe de fils sur les cœurs rapides pour la génération et tous les cœurs pour les longs prompts, vaudrait pour ces processeurs, avec la classe des cœurs que donne Windows. Sur le Mac, elle a gagné 2 à 6 % en génération sans rien perdre sur les longs prompts.

### 21. La réserve locale du fork peut être utilisée hors ligne

- **Où dans l'original** : `dlib/dlib/external/albireo/host/server.cpp:1029-1034` et `1458-1463`, au commit `184ea00`.
- **Comportement** : quand `offline` vaut vrai, `POST /albireo/v1/models/{id}/download` appelle `refuse_offline` et répond `409`, code `offline`, avec « offline mode: the catalog, the updates and the models come from files (import) ». C'est cohérent dans l'original, qui ne possède pas la réserve locale ajoutée au fork.
- **Reproduire dans l'original** : activer `offline`, puis appeler cette route avec l'authentification d'administration de l'original. Le refus ci-dessus est déduit du code ; il n'a pas été exécuté pour cette comparaison.
- **Ce que le fork fait** : depuis Albireo 4.1.0, la même route lance une restauration strictement locale de tous les fichiers vérifiés de la réserve, sans repli réseau. Si un fichier manque ou est altéré, le travail asynchrone signale `reserve_unavailable`.
- **Vérifié** : le test du gestionnaire passe sous Debian, avec et sans projecteur et avec des fichiers absents ou altérés ; la page a été essayée avec des réponses de service simulées. Le service et la route restent à tester sur Mac. Le comportement de l'original n'a pas été exécuté sous Windows.
- **Mac ou Windows** : code de service commun, mais **pas un défaut de l'original**. Ce changement est nécessaire à la réserve propre au fork.

### 22. Une erreur de transcription du fork empêchait de compiler le journal

- **Où dans l'original** : `dlib/dlib/external/albireo/host/public_link.cpp:457-481`, au commit `184ea00`. La fonction `without_queries` y retire les paramètres des adresses avant leur journalisation.
- **Ce qui se passe dans le fork** : lors du déplacement de cette fonction vers `albireo/common/log.cpp` en 4.0.0, des caractères de tabulation, de fin de ligne et de guillemet ont été mal transcrits. La compilation sous Debian s'arrêtait notamment sur `missing terminating ' character`. L'original ne présente pas cette faute de transcription.
- **Reproduire sur la branche avant correction** : compiler `common/log.cpp` en C++14 ; le compilateur refuse les constantes de caractères mal formées.
- **Ce que le fork fait** : les caractères sont à nouveau correctement écrits ; le retour chariot est aussi conservé comme séparateur. Un test couvre plusieurs adresses entre guillemets, sur plusieurs lignes et séparées par une tabulation.
- **Vérifié** : la syntaxe C++14 corrigée et les tests du masquage passent sous Debian ; compilation et test sur Mac restent à faire.
- **Mac ou Windows** : code commun, **erreur introduite par le fork**, pas un défaut de l'original ; non testé sous Windows ni sur Mac.

---

*Extrait pour l'auteur de l'original. Pour tout le reste (portabilité macOS, noyaux NEON, Accelerate, Metal, mesures de vitesse), voir [notes-pour-l-auteur.md](notes-pour-l-auteur.md). Ce document ne contient aucun chemin de machine privée, aucune clé et aucun jeton.*
