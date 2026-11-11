# Dlib Transformer Extensions sur macOS, épisode 20 : la lecture aussi

> **Repère de vitesse** (MacBook Air M2, tout sur le GPU jusqu'à 2 048 positions) : écriture **45 à 51 tokens/s** (2B), **20 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 19](19-le-gpu-prend-la-plume.md) : le GPU écrit les réponses. Restait la lecture des questions et des documents, confiée jusqu'ici à Accelerate, l'atelier de calcul du processeur.

## Une sonde d'abord

Lire un prompt, ce n'est pas le même travail qu'écrire un token. Pour écrire, il faut relire tous les poids pour un seul mot : c'est la mémoire qui freine. Pour lire 512 mots d'un coup, chaque poids sert 512 fois : c'est le calcul qui compte, et le GPU en a beaucoup.

Avant d'écrire une ligne, on a mesuré une seule grande multiplication, celle d'une couche du modèle du service sur 512 tokens. Accelerate la fait en 26 à 30 millisecondes. Le GPU, par la bibliothèque d'Apple qui lui est dédiée (Metal Performance Shaders), en 11. Le résultat est le même, au dernier chiffre près.

## Déplier, puis multiplier

Cette bibliothèque ne sait pas lire les poids compressés du modèle. Le GPU les déplie donc d'abord, matrice par matrice, dans un tampon de travail, puis il les multiplie. C'est comme décongeler un plat avant de le cuire. Le dépliage coûte un temps fixe à chaque passe, environ un cinquième de seconde pour le petit modèle, qui ne vaut la peine que pour des passes assez longues.

On a mesuré où se trouve la frontière. Jusqu'à 16 tokens, le processeur reste plus rapide : il travaille sur ses entiers, sans rien déplier. Dès 24 tokens, le GPU va deux fois plus vite. Vingt-quatre, c'est justement le seuil où le processeur passait déjà de ses entiers à Accelerate. Le GPU prend donc le relais exactement là.

## Un débordement, et une fausse alerte

Le contrôle du moteur a d'abord refusé la lecture sur le GPU : 1,5 % d'écart, pour un seuil de 1 %. En cherchant, on a trouvé un vrai bug, le nôtre. Quand l'attention tenait en un seul morceau, elle écrivait aussi deux nombres dans un tampon réduit à rien, donc à côté, dans une mémoire voisine. Les tokens écrits un par un y échappaient par chance. Le bug est corrigé.

L'écart, lui, est resté, et il n'était pas un défaut. On a comparé, octet par octet, les clés que le GPU et le processeur rangent en mémoire : identiques à la première couche, puis de plus en plus différentes. Ces clés sont arrondies sur 8 bits. Quand une valeur tombe presque au milieu entre deux entiers, un écart minuscule, un dix-millionième, suffit à la faire basculer sur l'entier voisin. Ce petit saut se propage aux couches suivantes, qui en font sauter d'autres, comme un mot mal recopié dans un manuscrit qu'on recopie trente fois.

En mémoire non arrondie, l'écart tombe à 0,04 %. Pour un prompt en 8 bits, le contrôle tolère donc 5 %, en exigeant toujours le même mot choisi. Et avec une sonde de 32 tokens, l'écart réel n'est que de 0,1 à 0,2 %.

## Ce que ça donne, et où ça s'arrête

La lecture double à peu près. Le modèle du service lit 512 tokens à 153 à 156 tokens par seconde, contre 91 à 94. Le petit modèle passe de 220 à 420. Dans le service, un texte de 619 tokens est lu à 142 tokens par seconde. Les réponses restent les mêmes, mot pour mot.

Il y a une limite, et on la dit franchement. Pour l'attention d'un prompt, le GPU fait parcourir à chaque mot, un par un, tous les mots qui le précèdent. Sur un document de 4 096 tokens, ce travail croît comme le carré de la longueur et finit par coûter plus que ce que le GPU gagne ailleurs : 134 tokens par seconde sur le petit modèle, contre 173 pour le processeur. Le moteur rend donc la main au processeur au-delà de 2 048 positions. Un long document est lu par morceaux de 512 tokens : les premiers vont au GPU, les suivants au processeur, qui partagent la même mémoire.

## Pour les curieux : les chiffres

La sonde, un produit de 512 × 2 560 × 9 728 en nombres à virgule :

```
Accelerate f32 : 26.04 ms, 0.98 TFLOPS
GPU MPS f32 : 11.07 ms, 2.30 TFLOPS
  écart à Accelerate : 0.00e+00
```

La frontière, une seule passe de prompt, clés en 8 bits :

| Tokens | 2B, processeur | 2B, GPU | 4B, processeur | 4B, GPU |
|---|---|---|---|---|
| 12 | 85,7 tokens/s | 59,5 tokens/s | 32,4 tokens/s | 21,5 tokens/s |
| 16 | 84,4 | 77,7 | 32,6 | 28,3 |
| 24 | 63,2 | 114,3 | 24,1 | 43,0 |
| 1 024 | 231,6 | 390,0 | 86,0 | 179,4 |
| 2 048 | 208,0 | 249,5 | 75,3 | 129,8 |
| 4 096 | 172,8 | 133,7 | 61,1 | 76,2 |

Le relevé qui a disculpé le GPU, les clés et valeurs de deux séquences, l'une lue par le GPU, l'autre par le processeur :

```
layer 0: keys 0, values 1 differ, scales 0/0, beyond 2: 0
layer 1: keys 24, values 71 differ, scales 8/26, beyond 2: 0
layer 2: keys 225, values 419 differ, scales 85/137, beyond 2: 0
```

## À suivre

L'attention des longs documents, en tuiles qui traitent plusieurs mots et plusieurs clés à la fois avec les instructions matricielles du GPU, lèverait la limite des 2 048 positions. Le réglage du GPU doit aussi rejoindre la page des réglages du service.
