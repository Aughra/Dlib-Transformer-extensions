# Dlib Transformer Extensions sur macOS, épisode 5 : cinq corrections et un premier démarrage

> **Repère de vitesse** : pas encore de mesure, Albireo ne tourne pas encore sur le Mac.

Suite de [l'épisode 4](04-le-fork-demenage.md) : le fork a trouvé son nouveau casier, et l'original y a versé dix commits de plus. Retour aux cinq obstacles de [l'épisode 3](03-albireo-pseudalis-et-cinq-obstacles.md), qui barraient la route d'Albireo et de Pseudalis.

## Même liste, après la fusion

Avant de toucher à quoi que ce soit, on a recompilé les deux applications telles quelles. La fusion avait modifié la recette de compilation d'Albireo, et rien ne garantissait que les obstacles étaient restés à leur place. Ils y étaient tous : les mêmes cinq, aux mêmes lignes, avec les mêmes messages. On savait donc exactement d'où l'on partait, et la suite ne pourrait pas mélanger l'effet d'une correction avec celui de la fusion.

## Cinq corrections, toutes réservées au Mac

Une règle a guidé chaque changement : ne pas réécrire le cahier du camarade, mais ajouter une note dans la marge, « sur Mac, lire ceci ». Chaque correction ne s'applique que sur macOS ou sur une puce ARM, et laisse Windows et Linux exactement comme l'auteur les a écrits. C'est le prix d'un fork qui veut rester facile à synchroniser : plus les notes sont petites, moins elles gêneront la prochaine fusion.

- **Les moteurs AVX2 ne sont plus construits sur une puce ARM.** Inutile d'apprendre à la voiture électrique à passer la cinquième : on ne lui monte pas de boîte de vitesses. Albireo et Pseudalis gardent leur moteur « portable », qui existait déjà et fonctionne sur tout processeur. Les tests d'Albireo qui utilisaient les calculs AVX2 les compilent désormais en version portable.
- **La synthèse vocale trouve son sel.** Sur Mac, `endian.h` s'appelle `machine/endian.h`. On va le chercher dans le bon placard.
- **La bibliothèque PNG reçoit sa sauce.** Les trois fichiers qui contiennent ses routines NEON sont maintenant compilés, comme dlib le faisait déjà pour son propre usage. On garde ainsi l'accélération plutôt que de la couper.
- **La couche HTTPS reçoit ses deux briques Apple.** `CoreFoundation` et `Security` sont liés à la cible HTTPS de dlib, ce qui règle la question pour toutes les applications qui s'en servent, pas seulement pour Albireo.
- **Le champ de date change de nom sur Mac.** `st_mtim` devient `st_mtimespec` : « portable » se dit « cellulaire », et l'information reste la même.

Au total, trente lignes ajoutées et deux retirées, dans cinq fichiers.

## L'erreur évitée de justesse

La correction la plus courte a failli être la plus traître. Le code de la synthèse vocale ne se contente pas d'inclure `endian.h` : il vérifie ensuite si la machine range ses nombres « à l'endroit » ou « à l'envers », en comparant deux noms, `__BYTE_ORDER` et `__BIG_ENDIAN`. Or macOS ne connaît pas ces noms. Il dit `BYTE_ORDER` et `BIG_ENDIAN`, sans les tirets bas.

Avec la première version de la correction, ces deux noms seraient restés inconnus, donc valant zéro tous les deux. Zéro égale zéro : la voix aurait conclu que le Mac lit ses nombres à l'envers, alors qu'il les lit à l'endroit. Tout aurait compilé sans un mot, et la voix aurait produit du bruit. Comme un lecteur qui lirait 1234 en 4321, persuadé d'avoir raison.

On ne l'a vu qu'en vérifiant, avant de compiler, ce que macOS définit vraiment. La seconde version traduit les deux noms, comme le code le faisait déjà pour FreeBSD, et le test donne bien « à l'endroit ». Une compilation qui réussit ne prouve pas que le programme dit vrai : il faut aussi regarder ce qu'il a compris.

## Ça compile, et ça démarre

Verdict : Albireo et Pseudalis compilent en entier sur le Mac, sans une seule erreur. Albireo n'a même laissé aucun avertissement ; Pseudalis en laisse un, pour une variable déclarée mais jamais utilisée.

Mais compiler n'est pas fonctionner, on l'a déjà dit à l'épisode 2. On a donc lancé les tests qui n'ont pas besoin d'un modèle de langage. Ils passent tous : les calculs du moteur d'Albireo, sa résistance à 144 fichiers de modèles abîmés, le format des conversations, l'interface du moteur sous 90 requêtes simultanées avec des annulations, la partie serveur ; et, côté Pseudalis, le moteur et la partie serveur.

Pseudalis a même un test plus ambitieux : un script qui démarre le vrai service, lui envoie des textes, des fichiers, des dossiers et des archives, les fait anonymiser puis restaurer, par son interface comme par son lecteur. Sur le Mac, le service démarre, et 26 étapes sur 28 passent. Les documents ressortent anonymisés, puis reviennent identiques à l'original.

## Deux fichiers qui ne se reconnaissent pas

Les deux étapes qui échouent ont un air de famille. Dans la première, une archive déjà protégée est renommée sur le lecteur : sous Linux, Pseudalis la reconnaît et la laisse tranquille ; sur le Mac, il la prend pour une inconnue et la met de côté. Dans la seconde, un fichier protégé est copié : sous Linux, la copie reprend la fiche de l'original ; sur le Mac, elle en reçoit une nouvelle. Dans les deux cas, Pseudalis ne reconnaît pas un fichier qu'il a pourtant déjà vu.

L'enquête reste à faire. Une piste se dessine déjà : sur un Mac, copier un fichier ne le duplique pas forcément octet par octet. Le système de fichiers d'Apple peut faire un « clone », deux noms pour les mêmes données, tant que personne ne les modifie. C'est le genre de détail que le code n'a jamais eu à connaître sous Linux.

Le script de test, lui, demandait deux outils de Linux absents du Mac. `nproc`, qui compte les processeurs, et la forme `stat -c` de la commande qui lit la taille d'un fichier. Sans elle, le script croyait chaque fichier vide. Plutôt que de modifier le script de l'auteur, on lui a prêté des équivalents le temps du test, la version GNU de `stat` étant déjà installée par Homebrew. Les deux échecs restants ne viennent pas de là : avec les bons outils, ils sont toujours là.

## Pour les curieux : les cinq notes en marge

Les changements, tels que `git diff` les montre, un extrait par fichier :

```diff
--- a/dlib/dlib/CMakeLists.txt
+++ b/dlib/dlib/CMakeLists.txt
          target_link_libraries(dlib_httplib INTERFACE Threads::Threads)
+         if (APPLE)
+            # cpp-httplib reads the certificates of the macOS Keychain.
+            target_link_libraries(dlib_httplib INTERFACE "-framework CoreFoundation" "-framework Security")
+         endif()
```

```diff
--- a/dlib/dlib/external/albireo/CMakeLists.txt
+++ b/dlib/dlib/external/albireo/CMakeLists.txt
+if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
+   # On ARM64, libpng calls its NEON routines, as in the build of dlib.
+   target_sources(albireo_image PRIVATE ${png_dir}/arm/arm_init.c ${png_dir}/arm/filter_neon_intrinsics.c
+      ${png_dir}/arm/palette_neon_intrinsics.c)
+endif()
...
+elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
+   # ARM64 (Apple Silicon): no x86 instruction set.  No engine of the AVX2 family is built, and
+   # the tests that take the kernels of AVX2 build them portable.
+   set(ALBIREO_ARM64 ON)
+   set(ALBIREO_ISA_avx2 "")
 else()
...
-   albireo_engine(cpu-avx2 avx2)
+   if (NOT ALBIREO_ARM64)
+      albireo_engine(cpu-avx2 avx2)
+   endif()
```

```diff
--- a/dlib/dlib/external/pseudalis/CMakeLists.txt
+++ b/dlib/dlib/external/pseudalis/CMakeLists.txt
 pseudalis_engine(pseudalis-engine "")
-pseudalis_engine(pseudalis-engine-avx2 "${PSEUDALIS_ISA_avx2}")
+if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
+   # ARM64 (Apple Silicon) has no AVX2: the engine above serves it alone.
+   pseudalis_engine(pseudalis-engine-avx2 "${PSEUDALIS_ISA_avx2}")
+endif()
```

```diff
--- a/dlib/dlib/external/pseudalis/host/drive_folders.cpp
+++ b/dlib/dlib/external/pseudalis/host/drive_folders.cpp
 #include <unistd.h>
+#if defined(__APPLE__)
+// macOS names st_mtimespec the modification time that Linux names st_mtim.
+#define st_mtim st_mtimespec
+#endif
 #endif
```

```diff
--- a/dlib/dlib/external/svox_pico/lib/picopltf.h
+++ b/dlib/dlib/external/svox_pico/lib/picopltf.h
 #include <sys/endian.h>
+#elif defined(__APPLE__)
+/* macOS names the byte order macros without their leading underscores, in machine/endian.h. */
+#include <machine/endian.h>
+#define __BYTE_ORDER BYTE_ORDER
+#define __BIG_ENDIAN BIG_ENDIAN
 #else
```

Ce que macOS définit vraiment, vérifié avec le préprocesseur avant la compilation : `__BYTE_ORDER` et `__BIG_ENDIAN` sont absents, `BYTE_ORDER` vaut `1234`, `LITTLE_ENDIAN` vaut `1234` et `BIG_ENDIAN` vaut `4321`. Avec la correction, `PICO_ENDIANNESS` vaut `2`, c'est-à-dire `ENDIANNESS_LITTLE`.

Le relevé, avant et après :

| | Albireo | Pseudalis |
|---|---|---|
| Épisode 3 | 279 étapes sur 303, 66 cibles en échec | 172 étapes sur 174, 5 cibles en échec |
| Après les corrections | tout compile, 0 avertissement | tout compile, 1 avertissement |
| Tests unitaires | `test_qkernels`, `test_model_file`, `test_chat_format`, `test_engine_api`, `test_host` : passent | `test_engine`, `test_host` : passent |
| Test du service complet | pas encore lancé (il faut un modèle) | 26 étapes sur 28 |

Le test complet de Pseudalis, avec les deux outils prêtés par un dossier placé en tête du `PATH` :

```sh
printf '#!/bin/sh\nsysctl -n hw.ncpu\n' > shim/nproc && chmod +x shim/nproc
ln -sf /opt/homebrew/bin/gstat shim/stat
PATH=$PWD/shim:$PATH bash dlib/dlib/external/pseudalis/tools/test_linux.sh $PWD/build/macos-pseudalis
```

```
drive signatures           FAIL  size 421595, .../home/drive/archive_sans_extension .../home/refused/files/grosse.zip.bak
copies on the drive        FAIL  record '11', with the copy '-', after it '2026-11-06T17:38:14Z'
2 failed
```

## À suivre

Deux fichiers à faire reconnaître par Pseudalis, puis un premier modèle de langage pour Albireo. Le prochain épisode dira si le service d'Albireo répond enfin à une question sur le Mac, et à quelle vitesse va son moteur portable, sans l'aide d'aucune instruction spéciale de la puce.
