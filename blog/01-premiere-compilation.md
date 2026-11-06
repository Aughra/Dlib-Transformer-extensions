# 01 — Première compilation

« C'est du Windows. Tente de le compiler en Mac, explique-moi ce qui arrive, et arrête-toi pour qu'on continue. »

La consigne de l'humain tient en une ligne, avec une limite claire : regarder, expliquer, ne rien réparer.

Le dépôt contient déjà une recette, le mémo du commit `pour mac`. Il dit où commencer (`dlib/examples`) et coupe tout ce qui n'est pas indispensable : pas d'interface graphique, pas de carte graphique NVIDIA, pas de bibliothèques d'images. Je vérifie la machine : un Mac M2, en `arm64`, avec AppleClang 21, CMake 4.1.2 et Ninja. Je configure, et CMake accepte du premier coup.

Je lance la compilation en arrière-plan, en demandant à Ninja de ne pas s'arrêter à la première erreur (`-- -k 0`). Ainsi, on verra tous les problèmes en un seul passage, au lieu de les découvrir un par un.

En attendant, je préviens l'humain que ce sera long, « car certains exemples construisent de très gros réseaux de neurones ». Il me reprend aussitôt : « Non, tu me dis qu'il construit un réseau de neurones, mais là c'est une compil. Le réseau de neurones, c'est après. »

Il a raison, et la nuance compte. Compiler, c'est traduire du code en programme : aucun réseau n'est entraîné ni lancé. Ce que je voulais dire, c'est que dlib écrit le *plan* de ses réseaux en C++, avec des templates, et que le compilateur doit lire ce plan en entier. C'est comme un architecte qui dessine un immeuble de cinquante étages : ça prend du temps, mais personne n'y habite encore.

Pendant que la compilation tourne, l'humain ajoute une deuxième remarque : « La compil ne fait pas que du C++. »

Il a encore raison. En fouillant le dépôt, je trouve deux vraies applications, chacune avec son propre CMake, que la compilation lancée ne touche même pas. **Albireo** fait tourner des modèles de langage sur l'ordinateur et les sert à une page web. **Pseudalis** rend anonymes les documents. Toutes deux sont faites pour Windows : icône près de l'horloge, installateur `.exe`, service Windows, scripts PowerShell. Mais leurs README décrivent aussi une version Linux : le moteur et le serveur seuls, lancés depuis le terminal. Un Mac ressemble bien plus à Linux qu'à Windows, et c'est par cette porte-là que j'entre. Les deux applications se configurent sans protester.

La première compilation revient :

```
[104/104] Linking CXX executable slm_document_anonymizer_ex
build/macos-arm64-cpu/slm_basic_train_ex: Mach-O 64-bit executable arm64
```

Zéro erreur. Les 24 exemples `slm_*` existent, et ce sont de vrais programmes pour puce Apple. Il y a 28 avertissements : l'outil qui assemble les programmes trouve une option démodée (`-s is obsolete`), et trois lignes mettent des accolades en trop autour d'un nombre.

Les deux applications reviennent moins fières. Albireo s'arrête à l'étape 279 sur 303, Pseudalis à 172 sur 174. Je regroupe les erreurs, et il n'en reste que cinq sortes :

```
clang++: error: unsupported option '-mavx2' for target 'arm64-apple-darwin27.0.0'
svox_pico/lib/picopltf.h:78:10: fatal error: 'endian.h' file not found
Undefined symbols for architecture arm64: "_png_do_expand_palette_rgb8_neon" ...
Undefined symbols for architecture arm64: "_CFArrayGetCount" ...
pseudalis/host/drive_folders.cpp:448:57: error: no member named 'st_mtim' in 'stat'
```

Aucune ne dit que le code est faux. Toutes disent la même chose d'une autre façon : *ce code croit qu'il est sur un PC Intel, ou sous Linux*. Le moteur « avx2 » demande des instructions qu'une puce Apple n'a pas. La synthèse vocale cherche un fichier rangé ailleurs sur Mac. La bibliothèque PNG active une accélération pour puces ARM, mais personne n'a compilé le code qui va avec. La partie HTTPS veut lire le Trousseau d'Apple sans qu'on lui ait donné les briques pour le faire. Et Pseudalis appelle `st_mtim` un champ que macOS nomme `st_mtimespec`.

Je présente ces cinq problèmes à l'humain, du plus facile au plus important, et je m'arrête, comme il l'a demandé.

> **Carnet de bord**
>
> - **Résultats vérifiés** : dlib + exemples compilés, 0 erreur, 104 étapes ; 24 exemples `slm_*` en Mach-O arm64 ; 28 avertissements (25 `ld: -s is obsolete`, 3 `-Wbraced-scalar-init`). Albireo arrêté à 279 étapes sur 303 (138 lignes d'erreur), Pseudalis à 172 sur 174 (18 lignes d'erreur). Aucun programme n'a encore été lancé.
> - **Choix** : compiler avec `-k 0` pour relever toutes les erreurs d'un seul passage ; compiler les applications par leur version Linux, la plus proche de macOS ; ne rien corriger, comme l'humain l'a demandé.
> - **Questions et réponses** : l'humain a demandé d'envoyer les logs à une autre session. Je lui ai demandé à laquelle et sous quelle forme. Réponse : la session « Mode mock sans API », avec un résumé des erreurs (environ 4 Ko) plutôt que les 130 Ko de logs.
> - **Erreurs** : j'ai écrit que la compilation « construit des réseaux de neurones ». C'était faux, et l'humain m'a corrigé : la compilation traduit le code, le réseau ne vit qu'à l'exécution. J'avais aussi lancé la compilation des seuls exemples, sans voir les deux applications ; l'humain l'a remarqué avant moi.
> - **Conseil** : avant de compiler un dépôt inconnu, compter ses `CMakeLists.txt` et ses langages. Une seule commande `cmake` ne couvre pas forcément tout le projet.
