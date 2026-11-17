# Dlib Transformer Extensions sur macOS, épisode 51 : un GPU pour apprendre

> **Repère de vitesse** (MacBook Air M2) : Albireo inchangé depuis l'épisode 50, écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B). Nouveau, dans dlib : un produit de deux matrices de 2048 × 2048 en **6,3 ms** sur le GPU, contre 14,5 ms sur le processeur.

Suite de [l'épisode 50](50-la-liste-d-attente.md), qui finissait la liste d'attente du Mac mini. Restait le chantier annoncé : apprendre à dlib à calculer sur le GPU du Mac.

## Deux moteurs dans la même maison

Le projet a deux façons de faire tourner un réseau de neurones. Albireo, le service de discussion, a son propre moteur, écrit pour la puce Apple depuis plusieurs semaines : il lit les modèles et répond, sur le processeur ou sur le GPU. dlib, la bibliothèque sur laquelle tout repose, a les siens, ceux qui servent à **entraîner** : apprendre un petit modèle, l'affiner, le distiller. Sous Windows, dlib entraîne sur les cartes graphiques de NVIDIA, par CUDA. Sur un Mac, il n'avait que le processeur.

L'humain a demandé un backend Metal pour les tenseurs de dlib, en huit phases, chacune avec ses tests, et ses règles : ne recopier le code d'aucun autre projet, ne jamais calculer sur le processeur en cachette, et dire ce qui n'est pas couvert.

## L'étiquette sur le bocal

Un tenseur, c'est un grand tableau de nombres. La première phase lui colle une étiquette : « processeur » ou « Metal ». Sur un Mac à puce Apple, le processeur et le GPU partagent la même mémoire : un tenseur Metal vit dans un tampon que les deux voient. C'est le même bocal posé sur la table de la cuisine, à portée des deux cuisiniers ; personne n'a besoin de transvaser.

Chaque opération de dlib regarde l'étiquette de ses tenseurs avant de travailler. Si tous sont « Metal » et que l'opération sait se faire sur le GPU, elle s'y fait. Si elle ne sait pas encore, elle s'arrête et le dit, au lieu de laisser le processeur faire le travail en douce : un cuisinier qui ne sait pas faire une sauce doit le dire, pas la commander au restaurant d'en face. Un mélange des deux étiquettes dans un même calcul est refusé aussi. Cet aiguillage a été posé dans les 102 fonctions de calcul de dlib, et le chemin du processeur n'a pas changé d'une ligne de comportement.

## Comparer avant de croire

Chaque opération portée sur le GPU a été comparée au processeur, sur les mêmes nombres tirés au hasard, avec une tolérance écrite d'avance. L'addition et la multiplication donnent le même résultat au bit près. Softmax, SiLU et GELU s'en écartent de moins d'un millionième.

GELU a demandé un détour : sa formule utilise une fonction mathématique, erf, que la bibliothèque de Metal n'a pas. On l'a écrite d'après un livre de 1964, le Handbook of Mathematical Functions d'Abramowitz et Stegun, dont une formule garde l'erreur sous 1,5 dix-millionième. Le livre a soixante ans, et il sert encore.

La normalisation de couche a donné la surprise de ces phases. Sur des nombres décalés, le processeur et le GPU différaient plus que prévu. Pour savoir lequel avait raison, on a calculé une troisième réponse, exacte, en double précision. C'est le GPU qui était le plus juste : le processeur additionne des milliers de nombres un par un, et perd un peu à chaque pas, comme une file de personnes qui se passent un seau d'eau ; le GPU les additionne par petits groupes, puis les groupes entre eux, et en perd moins. La règle de comparaison en tient compte : un écart n'est permis que dans la mesure où le processeur lui-même s'éloigne de la réponse exacte.

## La calculatrice d'Apple

Le produit de matrices est le cœur de tout réseau de neurones. Pour lui, il fallait choisir entre écrire notre propre noyau et prendre celui qu'Apple livre avec macOS, `MPSMatrixMultiplication`. On a pris celui d'Apple : il est réglé pour chaque génération de puce, celle du M2 aujourd'hui comme celle du Mac mini demain. Le prix : on ne peut rien glisser dedans, et ses additions ne se font pas dans l'ordre du processeur, si bien que la comparaison se juge à une borne d'erreur calculée, pas au bit près. L'écart mesuré n'a jamais dépassé 11 % de cette borne.

| Taille | Processeur | GPU |
|---|---|---|
| 256 × 256 | 0,031 ms | 0,25 ms |
| 1024 × 1024 | 1,60 ms | 1,31 ms |
| 2048 × 2048 | 14,5 ms | 6,28 ms |

Sur les petites matrices, le GPU perd, et nettement. Chaque calcul lui est envoyé comme un colis : l'aller et le retour prennent environ deux dixièmes de milliseconde, quand le processeur, qui a lui aussi son unité de calcul matriciel, a déjà fini. Le GPU ne gagne qu'au-dessus de 1 024.

## L'attention, en pièces détachées

L'attention est le mécanisme qui permet à un modèle de langage de relier chaque mot aux précédents. dlib ne l'a pas en une fonction : ses réseaux l'assemblent couche par couche. La cinquième phase l'a écrite en une seule fonction, naïvement, avec les pièces déjà vérifiées : un produit de matrices, un masque qui empêche un mot de voir ceux qui le suivent, un softmax, un second produit. La même fonction marche sur les deux étiquettes. Comparée à une réponse exacte, sur dix formes, elle s'en tient à moins d'un millionième des deux côtés. La version par tuiles, qui éviterait d'écrire en entier le tableau des scores, est notée pour plus tard.

## Les poids comprimés, et le ticket de vestiaire

Les modèles se distribuent comprimés : en Q4_0, chaque poids tient en un demi-octet, avec une échelle commune par paquet de 32. La sixième phase décode ces paquets à la volée, au moment du produit, sans jamais déplier le modèle en nombres ordinaires. Sur un vrai modèle de 1,5 milliard de paramètres, les poids Q4_0 occupent 583 Mo sur le GPU, contre 4,14 Go une fois dépliés.

C'est en mesurant cette mémoire qu'on a trouvé une erreur commise dès la première phase. La mémoire réellement occupée dépassait de loin la taille des tampons. Un petit programme d'essai a montré pourquoi : pour obtenir l'adresse d'un tampon, on appelait une fonction de Metal qui laisse derrière elle une sorte de ticket de vestiaire, et ce ticket ne se rend que dans un « autorelease pool », un guichet qu'on n'avait pas ouvert à cet endroit. Sans guichet, personne ne venait jamais récupérer le manteau : chaque tenseur Metal libéré gardait sa mémoire. L'erreur est facile à commettre et on l'a commise. L'appel passe désormais par le guichet, et un test vérifie que dix tenseurs de 50 Mo créés puis libérés ne laissent rien derrière eux.

## Remonter la pente

Apprendre, pour un réseau, c'est descendre une pente : on mesure dans quel sens chaque poids doit bouger pour réduire l'erreur. Ce calcul, la passe arrière, a son propre code pour chaque opération. La septième phase l'a porté sur le GPU, et l'a vérifié deux fois : contre le processeur, et par une méthode plus ancienne que l'informatique, les différences finies. On pousse un nombre d'un cheveu, on regarde de combien la perte change, et on compare à la pente calculée. Tout concorde, à huit millionièmes près au pire.

Les différences finies ont d'ailleurs attrapé une erreur, dans le test cette fois : il donnait au GPU des gradients déjà calculés par le processeur, que le GPU ajoutait une seconde fois. Le GPU semblait se tromper du double ; c'est le test qui comptait deux fois.

## Le petit élève

La dernière phase a porté les deux méthodes d'apprentissage les plus courantes, SGD et Adam, en passant par les vrais solveurs de dlib. Leurs pas sont identiques au bit près sur les deux appareils. Puis on a entraîné un petit réseau, deux couches, une normalisation et une activation, quarante pas de suite, deux fois : sur le processeur et sur le GPU, avec le même tirage au départ. Avec Adam, l'erreur descend de 4,76 à 0,263 ; avec SGD, à 0,838. Les deux trajectoires se superposent à 1,5 dix-millionième près. C'est la même leçon, apprise par deux élèves.

## Ce qui change dans dlib

Tout se passe dans dlib, et presque tout dans des fichiers nouveaux : le moteur Metal (`metal_dlib`, `metal_kernels`), l'aiguillage, et le test. Les fichiers de l'original reçoivent l'étiquette des tenseurs (`gpu_data.h`, `tensor.h`), l'aiguillage dans chaque fonction de calcul (`tensor_tools`), deux fonctions nouvelles (`attention` et `attention_gradient`), une règle de découpage du produit par plans extraite pour être partagée entre le processeur et le GPU, et les solveurs SGD et Adam, dont l'état naît désormais sur l'appareil de leurs gradients. Le tout ne s'allume qu'avec l'option `DLIB_USE_METAL` : sans elle, dlib se comporte comme avant.

## Pour les curieux : les chiffres et les fichiers

- **Build** : `cmake -S dlib/dlib/test -B build/macos-arm64-tests-metal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DDLIB_USE_FFMPEG=OFF -DDLIB_USE_CUDA=OFF -DDLIB_USE_NPU=OFF -DDLIB_USE_METAL=ON`, puis `dtest --test_metal`. La mesure du produit : `DLIB_METAL_BENCH=1` ; celle de la mémoire d'un modèle : `DLIB_METAL_GGUF=<fichier>`.
- **Fichiers nouveaux** : `dlib/dlib/cuda/metal_dlib.h`, `metal_dlib.mm`, `metal_dlib_internal.h`, `metal_dispatch.h`, `metal_kernels.mm`, `gemm_planes.h`, `dlib/dlib/test/metal.cpp`.
- **Fichiers modifiés** : `dlib/dlib/cuda/gpu_data.h`, `gpu_data_abstract.h`, `tensor.h`, `tensor_abstract.h`, `tensor_tools.h`, `tensor_tools.cpp`, `dlib/dlib/dnn/solvers.h`, `dlib/dlib/CMakeLists.txt`, `config.h.in`, `dlib/dlib/test/CMakeLists.txt`.
- **Opérations sur le GPU** : `add` (deux formes), `multiply`, `softmax`, `gelu`, `silu`, `gemm`, `layer_normalize`, `attention`, `gguf_matvec` (Q4_0 et Q8_0), `affine_transform` (formes à scalaires), `compute_adam_update`, et les gradients `softmax_gradient`, `gelu_gradient`, `silu_gradient`, `assign_bias_gradient`, `assign_conv_bias_gradient`, `layer_normalize_gradient`, `attention_gradient`.
- **Tests** : `dtest --test_metal`, 4 041 vérifications avec Metal ; la liste du mémo avec `--test_dnn`, plus de 5,25 millions de vérifications dans chacun des deux builds, à chaque phase. Le premier passage complet de `--test_dnn` sur le Mac date de cette série.
- **Mémoire** : LFM2.5-Audio-1.5B, 92 tenseurs Q4_0, 583 Mo sur le GPU, 4 144 Mo dépliés (× 7,1) ; Qwen3.5-0.8B, 195 tenseurs Q8_0, 821 Mo, 3 089 Mo dépliés (× 3,8).
- **Commits** : de `6cdef74` (phase 1) à la phase 8, un par phase ; le détail, avec les tolérances et les écarts de chaque opération, est au journal du 17 novembre.

## À suivre

Le GPU sait maintenant tout ce qu'il faut pour entraîner un petit réseau, mais pas encore vite : chaque opération attend sa propre réponse, comme un colis envoyé et attendu avant le suivant. Restent l'enchaînement des calculs sans attente, l'attention par tuiles, les autres formats comprimés, les couches de dlib elles-mêmes (elles créent leurs tenseurs sur le processeur), puis LoRA et la distillation. Et le Mac mini, qui arrive bientôt, avec son GPU plus large.
