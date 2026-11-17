# Notes pour l'auteur — Pseudalis sur Mac Apple Silicon

Ces notes viennent d'un fork de Dlib-Transformer-extensions, [Aughra/Dlib-Transformer-extensions](https://github.com/Aughra/Dlib-Transformer-extensions), qui fait tourner Gulliver et Pseudalis sur les Mac à puce Apple. Elles recensent ce que le portage révèle sur **Pseudalis** : bugs, tests fragiles, remarques. Les points de dlib et de Gulliver, et ceux qui touchent les deux applications, sont dans les [notes générales](../notes-pour-l-auteur.md) ; cette page y renvoie.

Chaque entrée donne l'endroit exact dans l'original, ce qui se passe, comment le reproduire, la correction du fork (avec son commit) ou une suggestion marquée **non testée**, ce qui est vérifié, et une ligne **« Mac ou Windows »** : « aussi sous Windows » quand le défaut est dans du code commun (le fork n'a pas de machine Windows : c'est déduit du code), « propre au Mac » ou « à Linux » sinon.

- **Comparé à** : `Cydral/Dlib-Transformer-extensions`, branche `main`, commit `184ea00` (Pseudalis 1.0.5). Les numéros de ligne renvoient à ce commit.
- **Machine** : MacBook Air M2 (8 cœurs, 24 Go), macOS 27, Apple clang 21.0.0, CMake 4.1.2, Ninja.

## Résumé

| N° | Nature | Où | En une ligne | Mac ou Windows |
|---|---|---|---|---|
| P1 | Test fragile | `pseudalis/tools/test_linux.sh:202, 315, 328, 330` | Deux étapes attendent un temps fixe au lieu d'attendre le résultat, et échouent sur une machine lente (corrigé dans le fork). | Linux et Mac (script non lancé sous Windows) |
| 4 | Robustesse | `pseudalis/host/main.cpp:307` | En `--console`, un service lancé en arrière-plan ne s'arrête pas par SIGINT sur macOS (corrigé dans le fork). Voir les [notes générales](../notes-pour-l-auteur.md), entrée 4. | Propre au Mac |
| 5 | Avertissements | code de dlib employé par Pseudalis | Deux avertissements `-Wunused-but-set-variable` avec Apple clang (corrigés dans le fork). Voir les notes générales, entrée 5. | Code commun, avertissements propres à clang |
| 21 | Remarque, sécurité | `pseudalis/common/platform.cpp:622, 636, 700` | Hors de Windows, les contrôles de dossiers répondent toujours « oui » ; Pseudalis s'en sert avant de faire confiance au dossier de Gulliver. Voir les notes générales, entrée 21. | Propre à Linux (et au Mac) |
| P2 | Remarque | `pseudalis/host/gulliver_client.h:24` | Le commentaire disait les contrôles d'appartenance « sans vérification » sur macOS, alors que le fork les a rendus réels (entrée 21) ; corrigé. | Propre au fork |

Ce que le fork change dans Pseudalis par choix (plus de jeton d'administration, français par défaut, application réorganisée) n'est pas un défaut de l'original : c'est dans les [notes de version](../../dlib/dlib/external/pseudalis/web/changelog.md) et le [tableau des différences](differences-mac-windows.md).

---

## 1. Bugs et tests fragiles

### P1. Deux étapes du test de Pseudalis attendent un temps fixe

- **Où** : `dlib/dlib/external/pseudalis/tools/test_linux.sh` :
  - ligne 202 (`sleep 8`), avant « drive signatures » ;
  - lignes 315 (`sleep 8`), 328 (`sleep 6`) et 330 (`sleep 4`), avant « copies on the drive ».
- **Ce qui se passe** : toutes les autres étapes du lecteur attendent leur résultat avec `wait_for <secondes> <vérification>`, en réessayant chaque seconde. Ces deux-là dorment un temps fixe, puis ne vérifient qu'une fois. Sur une machine lente, le lecteur n'a pas fini au moment de la vérification, et l'étape échoue alors que le service fonctionne.
- **Constaté sur le Mac** : un premier passage a donné 26 étapes sur 28, avec « drive signatures » et « copies on the drive » en échec. Il a très probablement tourné avec le mode économie d'énergie de macOS actif (voir la remarque 18 des [notes générales](../notes-pour-l-auteur.md)), qui rend la machine environ dix fois plus lente. Ce n'est pas vérifié pour ce passage précis, mais le mode était actif lors des mesures faites juste après. Mode coupé : 28 sur 28, avec et sans les modifications du fork.
- **Correction suggérée au départ** (non testée) : remplacer chaque `sleep` fixe par un `wait_for` sur la condition que l'étape vérifie ensuite. **Elle ne suffit pas**, et c'est une erreur de ces notes : ces étapes vérifient qu'il ne se passe *rien* (l'archive renommée reste en place, l'archive oubliée revient telle quelle, une copie ne crée pas de nouvel original). Attendre que « rien ne se passe » réussit dès le premier essai, avant même que le lecteur ait vu le fichier. Il faut un signe que le lecteur a fini son travail.
- **Corrigé dans le fork** (2026-11-14, commit [eb83ae4](https://github.com/Aughra/Dlib-Transformer-extensions/commit/eb83ae4)) : le lecteur compte ses parcours et dit s'il est au repos. Dans `pseudalis/host/drive_folders.cpp`, `drive_host::status()` donne deux champs de plus, `scans` (parcours finis, comptés dans `scanner_loop`) et `idle` (rien en changement, en attente, dans la file ni en cours de traitement : `pending`, `seen_folders`, `busy` et `queue` vides). `GET /v1/drive` les lit à l'instant (`service::drive_status()`), sans la mise en cache de deux secondes de `status()`. Le script remplace les quatre `sleep` par `settle`, qui attend deux parcours de plus qu'au moment de l'appel, puis le repos (60 s au plus) :

  ```sh
  drive_state() { api "$URL/v1/drive" | python3 -c 'import json, sys
  d = json.load(sys.stdin); print(d.get("scans", 0), 1 if d.get("idle") else 0)'; }
  settled() { set -- "$1" $(drive_state); [ "${2:-0}" -ge $(( $1 + 2 )) ] && [ "$3" = 1 ]; }
  settle() { wait_for 60 settled "$(drive_state | cut -d' ' -f1)"; }
  ```

  Deux parcours, parce que celui qui est en cours au moment de l'appel a pu lister le dossier avant le changement ; le suivant le voit forcément, et le fichier vu reste « en attente » jusqu'à son traitement, donc `idle` ne peut pas être vrai trop tôt. Le script du fork est `pseudalis/tools/test_macos.sh` (le `test_linux.sh` de l'original, adapté au Mac).
- **Un second délai trop court, trouvé grâce à cette correction** : avant de renommer l'archive, l'étape « drive signatures » attend sa protection par `wait_for 90 archive_done` (ligne 199 de `test_linux.sh` au commit `184ea00`), sans vérifier que l'attente a réussi. Sur le Mac en mode économie d'énergie, la protection de cette archive de 421 595 octets a pris plus de deux minutes : le test a renommé l'archive d'origine, que le lecteur a refusée (`refused/files/grosse.zip.bak`, identique à l'original), puis le lecteur a écrit l'archive protégée sous son ancien nom. L'ancien `sleep 8` masquait ce défaut, puisqu'il vérifiait avant le passage du lecteur. Le fork porte l'attente à 300 s (elle s'arrête dès que l'archive est prête) et fait échouer l'étape avec la raison quand elle expire. Vérifié : `test_macos.sh` de Pseudalis passe **34 sur 34** sur le Mac en mode économie d'énergie (le premier passage, avec 90 s, avait donné 33 sur 34).
- **Mac ou Windows** : Linux et Mac. Le script ne tourne pas sous Windows, et Pseudalis n'a pas de script de test pour Windows. Sur une machine Linux lente ou chargée, l'échec devrait se reproduire, d'après le code (non vérifié).

## 2. Remarques

### P2. Un commentaire de `gulliver_client.h` décrivait des contrôles qui vérifient désormais

- **Où** : `dlib/dlib/external/pseudalis/host/gulliver_client.h` (ligne 24 dans le fork), commentaire de la classe `gulliver_client` : « Sur macOS, les contrôles d'appartenance employés ici sont actuellement des fonctions sans vérification ».
- **Ce qui se passe** : c'était vrai de l'original hors de Windows (entrée 21 des notes générales). Depuis les commits [4c0947b](https://github.com/Aughra/Dlib-Transformer-extensions/commit/4c0947b) et [0d4946d](https://github.com/Aughra/Dlib-Transformer-extensions/commit/0d4946d) du fork, `owned_by_system`, `secure_folder` et `folder_admin_only` vérifient le propriétaire, les droits POSIX et les ACL sur macOS ; le commentaire est resté.
- **Corrigé dans le fork** (2026-11-15, Pseudalis 2.0.0) : le commentaire dit maintenant que ces contrôles vérifient le propriétaire, les droits POSIX et les ACL. Rien à changer dans l'original, dont les fonctions ne vérifient en effet rien hors de Windows. Vérifié : `common/platform.cpp` de Pseudalis appelle bien `acl_get_link_np`.
- **Mac ou Windows** : propre au fork.
