# Dlib Transformer Extensions sur macOS, épisode 60 : un modèle qui a lu le blog

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.15.0, mesures du 21 novembre) : écriture sur le GPU **132 à 136 tokens/s** (2B), **70 à 71 tokens/s** (4B) après 512 tokens ; lecture sur le GPU **4 706 à 5 075 tokens/s** sur 512 tokens (2B), **2 051 à 2 179** (4B).

Suite de [l'épisode 59](59-quatre-familles-dans-dlib.md), où dlib apprenait à recopier quatre familles de modèles sans changer une note. Le réseau était juste ; il restait à l'entraîner. C'est la deuxième marche de l'atelier, R2 dans la feuille de route : affiner un modèle sur ses propres textes. Le texte choisi pour l'essai est tout trouvé : les 59 épisodes de ce blog.

## Un affinage, en deux mots

Un modèle de langage a appris sur des milliards de pages. L'affiner, c'est lui faire relire un corpus plus petit, le sien, pour qu'il en prenne le vocabulaire et le ton. On ne retouche pas ses milliards de poids : on lui greffe de petits **adaptateurs** (la méthode LoRA), deux minces matrices à côté de certaines de ses couches, et seuls ceux-là apprennent. Pour Gemma 3 4B, cela fait 2,2 millions de nombres entraînés sur 4,55 milliards, moins de 0,05 %.

L'image est celle d'un livre qu'on annote au crayon dans la marge, sans réécrire une seule page. Le livre reste le même ; les annotations changent la façon de le lire.

## Quinze minutes par pas

La première passe de mesure, à l'étape d'avant, avait donné le ton : **un quart d'heure par pas d'entraînement** sur Qwen3-0.6B, un seul cœur au travail sur dix-huit, et 31 Go de mémoire. À ce rythme, une seule lecture du blog aurait pris une demi-journée pour le plus petit modèle de la famille.

La cause était dans la construction. L'atelier compilait dlib sans bibliothèque de calcul (`DLIB_USE_BLAS=OFF`), comme le guide de construction pour le processeur ; les produits de matrices passaient alors par les boucles portables de dlib, honnêtes mais lentes. Or le Mac a sa propre bibliothèque, Accelerate, et la recherche de CMake la trouve toute seule dans le kit de développement d'Apple, sans rien installer. Une option à changer :

| Deux pas d'entraînement, Qwen3-0.6B | Temps |
|---|---|
| Sans Accelerate | 779 et 780 s |
| Avec Accelerate | 7 et 7 s |

**Cent dix fois plus vite**, mesuré en alternance, pour un résultat identique : les deux modèles écrits après un pas sont égaux à l'octet près. Le mérite revient à Apple et à la vieille interface CBLAS de dlib, pas au portage, qui n'a fait que cocher une case.

## La moyenne qui dépendait de l'horloge

Le même essai a pourtant donné deux pertes moyennes différentes : 3,483 avec Accelerate, 3,633 sans. Un écart pareil, quand les calculs sont identiques à l'octet, ressemble à une erreur cachée. On l'a cherchée dans les produits de matrices (justes, contrôlés contre un calcul en double précision), dans les adaptateurs (justes eux aussi), puis on a comparé les modèles après un pas : identiques.

L'explication était ailleurs, dans l'affichage. L'entraîneur de dlib imprime une ligne de progression toutes les 40 secondes, et **remet sa moyenne à zéro à chaque ligne**. Sans Accelerate, un pas dure six minutes : la ligne du premier pas s'imprime, efface la moyenne, et la « moyenne de l'époque » ne compte plus que le second pas. Avec Accelerate, tout tient en sept secondes, aucune ligne ne s'imprime, et la moyenne porte sur les deux. C'est comme un compteur kilométrique remis à zéro chaque fois qu'on regarde le tableau de bord : le chiffre dépend du nombre de coups d'œil, pas de la route. L'exemple d'affinage compte maintenant sa moyenne lui-même ; l'entrée 31 des notes pour l'auteur décrit le piège.

## Un seul brouillon pour toutes les couches gelées

Ensuite, la mémoire. Pour chaque couche, dlib réserve un tampon de la taille de ses poids, où ranger les corrections calculées à chaque pas. Les couches gelées, qui n'apprennent rien, en avaient un aussi : dans un affinage par adaptateurs, c'est la taille du modèle entier gaspillée, 16 Go pour un 4B. Désormais, toutes les couches gelées écrivent dans **un seul brouillon commun**, que personne ne relit. Un cahier de brouillon pour toute la classe, au lieu d'un cahier neuf par élève qui ne rend jamais sa copie.

Sur le petit Qwen3, le gain est modeste, 0,35 à 0,6 Go : une sonde a montré que la mémoire y part surtout dans les résultats intermédiaires que chaque couche garde pour le retour en arrière, environ 10 Mo par mot du lot. Sur un 4B, il compte.

## Le blog, lu deux fois

Il restait une question que la perte d'entraînement ne tranche pas : un modèle qui apprend, ou un modèle qui récite ? L'étape « corpus » de l'exemple n'avait pas de texte mis de côté. Elle en a un maintenant : 5 % des fenêtres de texte, jamais montrées pendant l'entraînement, sur lesquelles on mesure la perte à la fin de chaque tour.

Qwen3-0.6B, trois tours du blog en 21 minutes :

| | Perte sur le texte mis de côté |
|---|---|
| Avant | 3,27 |
| Après un tour | 2,47 |
| Après deux tours | 2,38 |
| Après trois tours | 2,43 |

Le troisième tour fait remonter la perte : le modèle commence à réciter ses fenêtres. Deux tours suffisaient. Et le changement se lit. À la phrase « Dans cet épisode du portage de Gulliver sur Mac, on… », le modèle d'origine répond qu'« on va voir comment les personnages de Gulliver, les personnages de Mac, et les personnages de la série sont liés à l'histoire » : il pense au roman de Swift. Le modèle affiné poursuit qu'« on a donc ajouté ces trois bibliothèques à la liste des bibliothèques de Gulliver, et on les a compilé, puis compilé en mode économie d'énergie, pour les tester sur le Mac », et enchaîne un titre « Pour les curieux : les chiffres ». Les faits sont inventés, le ton est le bon.

## Le 4B, et la pile trop courte

Venait ensuite le premier modèle de 4 milliards de paramètres, Gemma 3 4B. Il est tombé au premier pas, sans message. Le rapport de plantage de macOS parlait d'une « stack guard region » : la pile d'un fil d'exécution était pleine.

La passe d'un réseau dlib descend couche par couche par des appels imbriqués, comme une pile d'assiettes qu'on empile avant de les redescendre une à une. L'entraîneur fait ce calcul dans un fil à part, et macOS donne à un fil secondaire **512 Ko** de pile, seize fois moins que Linux. Les 28 blocs de Qwen3 y tenaient ; les 34 de Gemma, non. Une première correction, dans la fonction qui crée les fils de dlib, n'a rien changé, et c'est le rapport, traduit adresse par adresse, qui a montré le vrai coupable : le pool de fils de dlib, qui passe par `std::thread`, lequel ne laisse pas choisir la taille de la pile. Il crée maintenant ses fils lui-même, avec 16 Mo, réservés mais occupés seulement à l'usage. L'erreur est de celles qu'on ne voit qu'en grandissant : l'entrée 32 des notes la décrit.

Gemma 3 4B a ensuite lu le blog une fois, en **30 minutes**, avec un pic de 45 Go :

| Gemma 3 4B | Perte sur le texte mis de côté |
|---|---|
| Avant | 4,61 |
| Après un tour | 2,28 |

À la même phrase, le Gemma d'origine annonce qu'on « explore les thèmes de la solitude, de la perte et de la mémoire », puis parle d'un podcast sur Gulliver errant sur les mers ; l'affiné écrit un épisode de ce blog, avec sa section « Pour les curieux : les commandes et les fichiers » et un bloc de commandes `brew`. C'est le **premier modèle de 4 milliards de paramètres affiné sur ce Mac**, sans compression de sa base.

## Ailleurs dans Gulliver

Depuis l'épisode 59, Gulliver a aussi remplacé les bibliothèques qu'il embarquait par celles de macOS (les images par ImageIO, SQLite et zlib du système, puis tout le chiffrement par Network.framework : Mbed TLS est parti en 6.14.1), et il ne lit plus rien de dlib. Il a gagné des tests de vitesse et de justesse contre llama.cpp et MLX, avec une rubrique du carnet et une mesure que le Mac refait lui-même chaque dimanche ; le constat est sans fard : Gulliver calcule aussi juste que llama.cpp, mais n'est encore le plus rapide nulle part. Sous chaque réponse, la page donne maintenant la vitesse de lecture et celle d'écriture.

## Pour les curieux : les chiffres et les fichiers

- Corpus : les 59 épisodes du blog, chacun précédé de `<<<doc>>>` (le lecteur ignore ce qui précède le premier, ce qui avait fait perdre un épisode à la passe de mesure de l'étape d'avant) ; 118 605 tokens pour Qwen3, 114 474 pour Gemma ; fenêtres de 512 tokens, 11 mises de côté.
- Réglages : LoRA de rang 8 sur les projections Q et V, alpha 16, pas d'apprentissage 2e-4, AdamW.
- Qwen3-0.6B : lots de 4 fenêtres, 3 tours, 1 248 s, pic de 30,75 Go. Gemma 3 4B : lots d'une fenêtre, 1 tour, 1 834 s, pic de 45,2 Go.
- La commande de Gemma :

```bash
slm_lora_finetune_ex --load slm_imported_model.dat --corpus ../corpus_blog.txt \
  --window 512 --batch-size 1 --epochs 1 --learning-rate 2e-4 --initial-windows 11 \
  --out r2/gemma3_blog.dat
```

- Le plantage, tel que macOS l'a noté : `EXC_BAD_ACCESS (SIGBUS) … KERN_PROTECTION_FAILURE … Could not determine thread index for stack guard region`, 144 appels sur la pile du fil fautif, et au pied de ce fil `dlib::thread_pool_implementation::thread()`.

**Ce que le fork change dans dlib pour R2** (chaque ligne est au registre des notes pour l'auteur) :

- `dlib/dnn/core.h` et son abstract : le brouillon commun des dérivées des couches gelées (`call_layer_backward_into`) ;
- `dlib/threads/thread_pool_extension.cpp` et `.h`, `threads_kernel_2.cpp` : des fils de 16 Mo de pile (entrée 32) ;
- `dlib/examples/slm_lora_finetune_ex.cpp` : la moyenne de l'époque comptée par l'exemple (entrée 31), la part de validation de l'étape « corpus », le message de mémoire ;
- `dlib/test/dnn.cpp` : `test_frozen_params_grad_scratch` ; `dtest --test_dnn` passe, 794 565 vérifications, et les tests des fils aussi.

Côté Gulliver, le script de l'atelier compile dlib avec Accelerate (6.14.2). Versions : Gulliver **6.10.2 à 6.15.0**.

## À suivre

Le 4B a tenu en mémoire avec sa base en nombres pleins, 18 Go. Pour aller jusqu'à un 8B, il faudra garder cette base compressée pendant l'entraînement, comme le fait la méthode QLoRA : c'est la suite de R2. Puis viendra R3, qui apprend la réponse sans apprendre la question.
