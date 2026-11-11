# Dlib Transformer Extensions sur macOS, épisode 17 : la sonde

> **Repère de vitesse** (MacBook Air M2, moteur inchangé depuis l'épisode 16) : écriture **39,7 à 40,8 tokens/s** (2B), **18,2 à 18,6 tokens/s** (4B). Le GPU, sondé à part, lirait les poids du 4B en 45 à 48 ms par token contre 51 à 55 pour le processeur.

Suite de [l'épisode 16](16-deux-equipes.md) : le moteur répartit son travail entre cœurs rapides et cœurs lents, et le modèle du service écrit à un peu plus de 18 tokens par seconde. Restait le GPU du Mac, qu'Albireo n'utilise pas. Avant de lui écrire un moteur, on a voulu savoir ce qu'il rapporterait vraiment.

## Un GPU dans la puce, mais pas de CUDA

Sur un PC, le GPU est souvent une carte à part, avec sa propre mémoire. Sur un Mac à puce Apple, il est gravé dans la même puce que le processeur, et les deux partagent la même mémoire : une seule cuisine pour deux cuisiniers. Le M2 de ce MacBook Air a un GPU de 10 cœurs.

Le moteur GPU d'Albireo est écrit en CUDA, le langage des cartes NVIDIA, à travers les tenseurs de dlib. Sur Mac, CUDA n'existe pas. Le langage du GPU s'appelle Metal. Il faudrait donc un moteur Metal, écrit pour l'occasion.

La bonne nouvelle est dans l'architecture de l'auteur. Son moteur parle au GPU par deux « prises » normalisées : l'une confie au GPU un produit à la fois, l'autre des couches entières du modèle, voire un token complet. Un moteur Metal pourrait se brancher sur ces prises sans toucher au reste.

## La promesse de l'épisode 16 était trop belle

L'épisode 16 annonçait que le GPU du M2 saurait tirer de la mémoire « presque le double » du processeur. C'était une déduction tirée de chiffres de mémoire, pas une mesure. L'erreur est facile à commettre, et on l'a commise. Avant d'écrire des milliers de lignes, on a donc écrit une sonde.

La sonde est un petit programme à part, hors du moteur. Elle donne au GPU les vrais poids du modèle du service, sans les recopier : grâce à la mémoire partagée, le GPU lit directement le fichier que le moteur a ouvert. Elle mesure trois choses. Le temps pour lire tous les poids une fois, ce qu'exige chaque token écrit. Le plafond, c'est-à-dire la vitesse d'une lecture pure, sans aucun calcul. Et le coût d'un aller-retour : le temps de confier un travail au GPU et d'attendre sa réponse.

Il y a eu un détour. Le compilateur Metal, qui transforme les noyaux en code du GPU, est un composant optionnel de Xcode, absent de ce Mac. Mais macOS sait compiler un noyau Metal à la volée, au lancement du programme. On a pris ce chemin, sans rien installer.

## Trois versions d'un noyau

Le premier noyau était juste, et même plus précis que le processeur : il calcule en nombres à virgule là où le processeur arrondit les activations en 8 bits. Mais il n'allait pas plus vite que lui : 57 millisecondes pour lire tous les poids, contre 53.

On a cru que chaque groupe de threads relisait trop souvent les activations, et on lui a confié plus de lignes. C'était pire. On a ensuite cru à un problème d'ordre de lecture : un GPU aime que 32 threads voisins lisent 32 cases voisines, comme 32 personnes qui prennent chacune le livre suivant sur la même étagère. La deuxième version, rangée ainsi mot par mot, a été encore plus lente. Pour chaque petit mot de poids lu, chaque thread décodait des échelles, et ce décodage coûtait plus que la lecture.

La troisième version donne à chaque thread un morceau plus large, 16 octets d'un bloc, et décode ses échelles sans aucun test, avec l'astuce de masques déjà employée pour le processeur à [l'épisode 15](15-le-classeur-en-noir-et-blanc.md). Le format principal du modèle passe de 39 à 53 Go par seconde.

## Le verdict

Avec ses meilleurs noyaux, le GPU lit tous les poids du modèle du service en 45 à 48 millisecondes. Le processeur en met 51 à 55. C'est 5 à 22 % de mieux, selon le nombre de threads du processeur ; un tiers de mieux sur le petit modèle. Le plafond mesuré du GPU, sans aucun calcul, est de 35 millisecondes, soit 70 Go par seconde : même avec des noyaux parfaits, on ne gagnerait qu'une fois et demie, pas le double.

Surtout, ce gain n'existe que si le GPU fait tout le token d'une traite. Chaque aller-retour avec lui coûte 186 microsecondes, presque deux dixièmes de milliseconde. En lui confiant les produits un par un, il faut 120 millisecondes par token, plus du double du processeur. Couche par couche, il en faut 57 à 60, encore plus lent que le processeur. Un moteur Metal n'aurait d'intérêt sur ce Mac que s'il calculait tout, attention comprise, et c'est le chantier le plus lourd.

## Pour les curieux : les chiffres

La sonde vit dans `macos/metal-sonde/`, avec ses commandes. Lancée ainsi sur le modèle du service :

```
Q4=mv3_q4_K ROWS=4 SGS=4 build/metal-sonde macos/metal-sonde/kernels.metal <modèle.gguf> 8
```

Elle a donné, machine calme (charge 3 à 4) :

```
écart max rapporté à sum|w x| : GPU 2.80e-08, processeur (activations en 8 bits) 8.81e-04
(a') soutenu, 10 passes en une commande : 47.77 ms par passe -> 52.1 Go/s
plafond, lecture pure : 35.26 ms par passe -> 70.6 Go/s
Q4_K seuls : 1683 Mo, 33.38 ms -> 52.9 Go/s
Q6_K seuls : 692 Mo, 12.63 ms -> 57.4 Go/s
(b) aller-retour : médiane 188 µs, 10e centile 167 µs, 90e 264 µs
(c) une commande attendue par couche : 56.73 ms (médiane)
(d) une commande attendue par produit : 121.47 ms (médiane)
processeur, 8 threads, mêmes produits : 55.18 ms (médiane) -> 45.1 Go/s
```

Les trois versions du noyau du format Q4_K, seul :

| Version | Ce que lit chaque thread | Débit |
|---|---|---|
| 1 | 32 octets d'un bloc, une échelle | 38,6 Go/s |
| 2 | 4 octets, deux échelles | 26,9 Go/s |
| 3 | 16 octets alignés, deux échelles décodées sans test | 52,9 Go/s |

Sur le petit modèle (2B), le GPU met 19,7 millisecondes et le processeur 26,2.

## À suivre

Trois chemins sont possibles. Fusionner d'abord les seize nouveaux commits de l'auteur, dont l'un apprend au moteur à écrire plusieurs tokens par pas à partir de brouillons, ce qui pourrait rapporter plus que le GPU, et sans lui. Écrire le moteur Metal complet, pour un gain de 5 à 50 % en écriture sur ce Mac (de ce que mesure la sonde aujourd'hui à son plafond). Ou sonder encore, cette fois la lecture des prompts, où le GPU calcule beaucoup et où il pourrait battre Accelerate. Le choix revient à l'humain. La sonde sera aussi relancée sur le futur Mac mini M5 Pro, où le partage des rôles entre GPU et processeur ne sera pas le même.
