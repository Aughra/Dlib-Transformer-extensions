# Dlib Transformer Extensions sur macOS, épisode 3 : Albireo, Pseudalis et cinq obstacles

*Publié sur le carnet TortueTech le 5 novembre 2026.*

Suite de [l'épisode 2](02-compiler-dlib-et-les-exemples.md) : dlib et les exemples compilent sur le Mac. Reste le plus gros du dépôt.

## Ce qui reste : deux applications pensées pour Windows

Le dépôt ne contient pas qu'une bibliothèque. Il livre aussi deux applications complètes, conçues pour Windows 11 :

| Application | Ce qu'elle fait | Ce qui est propre à Windows |
|---|---|---|
| Albireo | un assistant conversationnel local : un serveur et une page web | le service Windows, l'icône près de l'horloge, l'installateur `.exe` |
| Pseudalis | la pseudonymisation de documents | les mêmes, plus un lecteur réseau avec sa lettre et une entrée dans le menu contextuel de l'Explorateur |

Autour d'elles, tout n'est pas du C++ : l'interface web en JavaScript et HTML, que le navigateur lit tel quel ; des outils Python pour préparer les données ; et des scripts PowerShell qui fabriquent l'installateur Windows, sans usage sur un Mac.

La bonne nouvelle, c'est que les deux applications existent aussi en version Linux, réduite au moteur et au serveur, sans icône ni installateur. Un Mac ressemble bien davantage à Linux qu'à Windows, et c'est cette version que CMake a acceptée. Un premier écueil est déjà visible : Albireo embarque des moteurs optimisés pour les instructions AVX des processeurs Intel et AMD, qui n'existent pas sur une puce Apple. Il faudra voir comment il se comporte sans elles.

## Cinq obstacles, une seule famille

Verdict pour les deux applications : elles ne compilent pas encore sur le Mac, mais elles en sont près. Pseudalis a franchi 172 étapes sur 174 : le moteur, ses tests et l'outil de signature sont construits, seul le service manque. Albireo en a franchi 279 sur 303 : quelques outils sont là, mais ni le serveur ni son moteur principal.

Les échecs se résument à cinq problèmes, et tous racontent la même histoire : le code suppose un PC Intel ou un Linux. Aucun ne touche à la logique, et aucun fichier n'a encore été modifié pour les contourner.

- **Des options réservées aux processeurs Intel et AMD** (les deux applications). Les moteurs « avx2 » demandent au compilateur `-mavx2`, `-mfma` et `-mf16c`, des jeux d'instructions qu'une puce Apple n'a pas : c'est demander à une voiture électrique de passer la cinquième, elle n'a pas de boîte de vitesses. La puce Apple a son équivalent, NEON. Il faudra soit ne pas construire ces moteurs sur Mac, soit se rabattre sur la version portable, qui existe déjà dans le code.
- **Un en-tête Linux introuvable** (Albireo, la synthèse vocale `svox_pico`). `endian.h`, qui dit dans quel ordre la machine range les octets d'un nombre, n'est pas au même endroit ni sous le même nom sur macOS. Le sel existe dans les deux cuisines ; il n'est simplement pas dans le même placard.
- **Une accélération ARM à moitié branchée** (Albireo, lecture des images PNG). La bibliothèque PNG détecte une puce ARM et appelle ses routines NEON, mais les fichiers qui les contiennent ne sont pas compilés. La recette dit « ajoutez la sauce » et personne n'a préparé la sauce. Il suffira de compiler ces fichiers, ou de couper l'accélération.
- **Deux frameworks Apple oubliés** (Albireo, téléchargements HTTPS). Sur Mac, la couche HTTPS va chercher les certificats dans le Trousseau, ce qui exige de lier `CoreFoundation` et `Security`. Personne ne l'a fait, puisque le code n'avait jamais vu de Mac : une ligne à ajouter dans CMake.
- **Un même champ, deux noms** (Pseudalis, le dossier surveillé). La date de modification d'un fichier s'appelle `st_mtim` sous Linux et `st_mtimespec` sous macOS. Même information, autre nom, comme « portable » et « cellulaire » : trois lignes à changer.

L'ordre de correction retenu va du plus simple au plus utile : le champ de date, puis les frameworks, la bibliothèque PNG, l'en-tête, et en dernier les moteurs AVX, les plus importants pour la vitesse sur une puce M2.

## Pour les curieux : le relevé exact

La machine : un Mac à puce M2, AppleClang 21, CMake 4.1.2 et Ninja. Chaque application est configurée et compilée seule, en architecture `arm64`, avec l'option `-k 0` qui demande à Ninja d'aller jusqu'au bout malgré les erreurs, pour les voir toutes d'un coup :

```sh
cmake -S dlib/dlib/external/albireo -B build/macos-albireo -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-albireo -- -k 0
```

Le relevé, application par application :

|   | Albireo | Pseudalis |
|---|---|---|
| étapes franchies | 279 sur 303 | 172 sur 174 |
| cibles en échec | 66 | 5 |
| options AVX refusées | 36 fois chacune de `-mavx2`, `-mfma`, `-mf16c` | 4 fois chacune |
| autres erreurs | 28 fois `endian.h` introuvable, 2 éditions de liens en échec | 6 fois `st_mtim` inconnu |

### Le journal

Voici le résumé du journal, tel que la compilation l'a produit, regroupé par message et par cible. Les chiffres en tête de ligne comptent les occurrences.

#### Albireo : 279 étapes sur 303, 66 cibles en échec

```
Erreurs regroupées
  36  clang++: error: unsupported option '-mfma' for target 'arm64-apple-darwin27.0.0'
  36  clang++: error: unsupported option '-mf16c' for target 'arm64-apple-darwin27.0.0'
  36  clang++: error: unsupported option '-mavx2' for target 'arm64-apple-darwin27.0.0'
  28  external/svox_pico/lib/picopltf.h:78:10: fatal error: 'endian.h' file not found
   2  clang++: error: linker command failed with exit code 1

Cibles en échec
  28  albireo_pico
  10  albireo-engine-cpu-avx2
   5  bench_runtime
   5  test_model_file
   5  test_qkernels
   5  test_runtime
   5  test_tokenizer
   1  albireo-kernels-avx2
   1  bin/engines/libalbireo-engine-cpu.dylib
   1  tests/test_host
```

La commande type d'un fichier AVX2, où l'on voit l'architecture `arm64` et, trois options plus loin, les instructions Intel qu'elle ne connaît pas :

```
/usr/bin/c++ -I dlib -O3 -DNDEBUG -std=c++14 -arch arm64 -fPIC
  -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra
  -Wno-unused-parameter -Wno-missing-field-initializers
  -mavx2 -mfma -mf16c
  -c dlib/dlib/external/albireo/engine/qkernels.cpp
```

Le chemin de l'en-tête manquant dans la synthèse vocale :

```
picoos.h:37 → picopal.h:43 → picopltf.h:78: #include <endian.h> : file not found
```

Les symboles introuvables à l'édition des liens, d'abord pour libpng, puis pour le Trousseau :

```
libpng (libalbireo_image.a : pngrtran.c.o, pngrutil.c.o)
  _png_do_expand_palette_rgb8_neon
  _png_do_expand_palette_rgba8_neon
  _png_init_filter_functions_neon
  _png_riffle_palette_neon

httplib (http_fetch.cpp.o : enumerate_macos_keychain_certs)
  _CFArrayGetCount
  _CFArrayGetValueAtIndex
  _CFDataGetBytePtr
  _CFDataGetLength
  _CFRelease
  _SecCertificateCopyData
  _SecTrustSettingsCopyCertificates
```

#### Pseudalis : 172 étapes sur 174, 5 cibles en échec

```
Erreurs regroupées
   4  clang++: error: unsupported option '-mfma'
   4  clang++: error: unsupported option '-mf16c'
   4  clang++: error: unsupported option '-mavx2'
   6  external/pseudalis/host/drive_folders.cpp:448, 485, 518:
        error: no member named 'st_mtim' in 'stat'   (2 par ligne)

Cibles en échec
   4  pseudalis-engine-avx2
   1  pseudalis-service
```

La ligne fautive, la première des trois :

```cpp
e.written = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
```

Sur macOS, le même calcul s'écrit avec `st.st_mtimespec`.

## Ce que le Mac n'aura pas

Une partie des applications restera chez Windows : l'icône près de l'horloge, l'installateur `.exe`, le service Windows, le lecteur réseau avec sa lettre et l'entrée du menu contextuel de l'Explorateur. Sur Mac, on visera ce qu'offre déjà la version Linux : le moteur, le serveur et l'interface web, lancés depuis le terminal avec `--console`.

## À suivre

Les cinq corrections restent à faire. Le prochain billet dira si Albireo et Pseudalis démarrent enfin sur le Mac, et à quelle vitesse.
