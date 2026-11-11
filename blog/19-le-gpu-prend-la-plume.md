# Dlib Transformer Extensions sur macOS, épisode 19 : le GPU prend la plume

> **Repère de vitesse** (MacBook Air M2, écriture sur le GPU) : écriture **49,4 à 49,6 tokens/s** (2B), **21,3 à 21,7 tokens/s** (4B), 26,5 à 28,5 tokens/s après un document de 4 096 tokens (2B) ; lecture inchangée, sur le processeur, 150 à 161 tokens/s sur 4 096 tokens (2B).

Suite de [l'épisode 18](18-le-grand-menage.md) : le fork a fait son grand ménage et ne garde que du Mac. Il pouvait enfin construire ce que l'original n'a pas : un moteur qui écrit avec le GPU de la puce Apple.

## Tout le token d'un coup

La sonde de [l'épisode 17](17-la-sonde.md) avait posé la règle. Le GPU du M2 lit les poids plus vite que le processeur, mais chaque aller-retour avec lui coûte près de deux dixièmes de milliseconde. Lui confier les multiplications une par une, c'est envoyer un coursier à chaque ligne d'une lettre. Il fallait lui donner le token entier : les trente-six couches du modèle, l'attention comprise, en une seule commande.

Sur un Mac, cela se fait sans déménager les données. Le GPU partage la mémoire du processeur : il lit les poids directement dans le fichier du modèle, et les clés et valeurs de l'attention dans la zone où le processeur les range. Le travail se partage ainsi. Le processeur lit toujours la question, ce qu'il fait vite depuis [l'épisode 13](13-l-atelier-d-a-cote.md). Le GPU écrit la réponse, en reprenant les clés et valeurs là où le processeur les a posées. C'est une cuisine où le second prépare les ingrédients et où le chef les trouve sur le même plan de travail.

## Dix noyaux et un test

Il a fallu écrire dix programmes pour le GPU, les « noyaux », dans le langage Metal : les multiplications par les poids compressés, les normalisations, la rotation des positions, l'écriture des clés et valeurs, l'attention. Chacun refait ce que le processeur fait déjà, et un test les compare un à un sur des données tirées au hasard. Les écarts vont du cent-millionième au millionième, et les clés et valeurs que le GPU écrit sont identiques, octet pour octet, à celles du processeur.

## Le contrôle qui accusait le mauvais

Le moteur ne fait pas confiance au GPU sur parole. Au chargement du modèle, il lui fait écrire deux tokens et les compare à ceux du processeur. Si les résultats s'écartent de plus de 1 %, il le renvoie.

Au premier essai, il l'a renvoyé : 5 % d'écart. On a d'abord soupçonné le GPU. C'était l'inverse. Pour aller vite, le processeur arrondit ses calculs intermédiaires sur 8 bits, quand le GPU calcule en nombres à virgule complets. Sur vingt-huit couches, ces arrondis finissent par peser quelques pour cent. Le contrôle comparait un GPU précis à un processeur approximatif, et il accusait le plus précis des deux.

Le moteur avait déjà un mode de calcul exact, prévu pour les vérifications. Le contrôle s'en sert maintenant comme référence. L'écart tombe à 0,04 %, avec le même mot choisi à chaque fois.

## Plus lent, puis plus rapide

Le GPU écrivait juste, mais moins vite que le processeur : 16,9 tokens par seconde pour le modèle du service, contre 18,3. La règle du projet est simple : on ne garde que ce qui gagne.

Un chronométrage par morceaux a montré une anomalie. L'attention coûtait 9 millisecondes par token, autant sur un texte de 64 mots que de 2 048, comme une caisse de supermarché qui mettrait le même temps pour un article et pour un chariot plein. On a d'abord cru au coût des lancements : réunir les noyaux deux par deux n'a rien changé. La vraie raison était plus fine. Le noyau rangeait ses résultats intermédiaires dans de petits tableaux dont la taille n'était connue qu'au dernier moment. Faute de pouvoir les garder dans ses registres, sa mémoire la plus proche, le GPU les rangeait plus loin, comme un cuisinier qui irait chercher chaque pincée de sel à la cave.

En donnant ces tailles dès la compilation, une version du noyau par modèle possible, l'attention tombe de 9,2 à 1,7 millisecondes. La même recette appliquée aux multiplications les fait passer de 48,8 à 43,4.

## Le verdict

Mesuré en alternance avec le processeur, le GPU gagne à chaque tour. Le modèle du service écrit à 21,3 à 21,7 tokens par seconde, contre 18,3 à 19,0. Le petit modèle passe de 36 à 41 tokens par seconde à près de 50. Après un document de 4 096 tokens, l'écriture passe de 22 à 23 tokens par seconde à 26,5 à 28,5. Et les réponses sont les mêmes, mot pour mot.

Le moteur l'emploie donc par défaut. Il le contrôle au chargement de chaque modèle, en trois secondes, et rend la main au processeur au moindre écart ou à la moindre panne. Le modèle de recherche, dans un autre format de poids, reste sur le processeur pour l'instant.

## Pour les curieux : les chiffres

Le partage du temps du GPU pour un token du 4B, avant et après les gabarits :

| Morceau | Avant | Après |
|---|---|---|
| Multiplications (dont la tête des logits : 5,5 ms) | 48,2 ms | 43,4 ms |
| Attention et écriture des clés | 9,2 ms | 1,7 ms |
| Le reste | 2 ms | 1 à 2 ms |
| Côté processeur (préparer, encoder) | 0,3 ms | 0,3 ms |

Le contrôle tel que le service l'écrit dans son journal :

```
engine: model 'qwen3-vl-4b-thinking': GPU Apple M2: the generation steps run on it (step 1: logits within
0.0394758% of those of the processor, most likely token the same; step 2: logits within 0.0375869% of those
of the processor, most likely token the same)
```

Les mesures alternées, `bench_runtime <modèle> 8 64 64 1 q8`, puis la même commande avec `metal` :

| Tour | Modèle | Processeur | GPU |
|---|---|---|---|
| 1 | 4B | 19,0 tokens/s | 21,3 tokens/s |
| 1 | 2B | 40,8 tokens/s | 49,4 tokens/s |
| 2 | 4B | 18,3 tokens/s | 21,7 tokens/s |
| 2 | 2B | 35,7 tokens/s | 49,6 tokens/s |

Le GPU lit encore les poids à 54 Go par seconde environ, pour un plafond mesuré de 70.

## À suivre

Il reste de la marge : la multiplication principale, la dernière couche qui choisit le mot, le contrôle qu'on pourrait faire au chargement du modèle plutôt qu'à la première question, et le modèle de recherche. Puis viendra la lecture des questions, où le GPU devra battre l'atelier d'Accelerate.
