# Dlib Transformer Extensions sur macOS, épisode 15 : le classeur en noir et blanc

> **Repère de vitesse** (MacBook Air M2) : écriture **38 à 39 tokens/s** (2B), **18 tokens/s** (4B) ; lecture de 4 096 tokens 158 à 162 tokens/s (2B, clés en 8 bits).

Suite de [l'épisode 14](14-le-zebre-et-le-perroquet.md) : le modèle du service décrit des images en deux secondes d'encodage. Cette fois, on est revenu à l'écriture elle-même, token après token, et on a fusionné six nouveaux commits de l'auteur. La fusion cachait un piège.

## Mesurer d'abord

On a commencé par mesurer le moteur tel qu'il était, sur le MacBook Air M2, mode économie d'énergie coupé. Le petit modèle (Qwen3-VL 2B) écrit 37 tokens par seconde ; celui du service (Qwen3-VL 4B Thinking), 16,8.

Écrire un token, c'est relire tous les poids du modèle, un peu comme relire un livre entier avant d'écrire chaque mot. Le 4B pèse 2,5 Go. En 60 millisecondes par token, la machine lit donc environ 42 Go par seconde, alors que la mémoire du M2 en permet à peu près 100. Il restait de la marge.

La photographie du moteur (l'outil `sample` de macOS) donne deux postes : 75 % du temps dans le produit des poids, et 21 % dans l'attente entre les fils de calcul.

## Le caissier qui refait l'addition

Le produit des poids, c'est le noyau NEON de [l'épisode 8](08-seize-a-la-fois.md). Il était juste, mais il avait une manie : après chaque petit groupe de 32 poids, il ramassait son résultat en un seul nombre, puis le multipliait par l'échelle du groupe, chiffre par chiffre. C'est le caissier qui refait le total du ticket après chaque article, au lieu de le faire une fois à la fin.

On lui a appris à garder ses sous-totaux en colonnes jusqu'au bout du bloc de 256 poids, et à lire les huit échelles d'un coup. Comme ce sont des sommes de nombres entiers, l'ordre des additions ne change rien : le résultat reste identique au bit près à celui du code de secours. On l'a vérifié sur 43 200 produits, avec les vrais poids des deux modèles. Aucune différence.

Le gain est modeste : de 16,5 à 17 tokens par seconde, on passe à 18 pour le 4B, et de 35 à 37 environ à 38 ou 39 pour le 2B.

## Les déménageurs rapides et les lents

Restait l'attente. Le M2 a quatre cœurs rapides et quatre cœurs lents, et le moteur les emploie tous les huit. À chaque multiplication, le travail est distribué par paquets ; à la fin, les cœurs rapides attendent que le dernier paquet, pris par un cœur lent, soit fini. Ce sont des déménageurs qui ont vidé l'appartement et attendent le plus lent pour fermer le camion.

Première idée : faire des paquets plus petits, pour que le dernier soit vite fini. Mauvaise idée. Le 4B est tombé de 18,1 à 17,0 tokens par seconde, et à 13,7 avec des paquets encore plus petits. Distribuer coûte aussi du temps.

Deuxième essai, plus simple : n'employer que quatre fils. Avec quatre fils, le 4B écrit à 19,4 tokens par seconde, plus vite qu'avec huit (18,2). Les questions courtes sont lues plus vite aussi. Mais sur un long document de 4 096 tokens, les huit cœurs reprennent l'avantage : 163 à 169 tokens par seconde contre 143 à 145. Là, c'est le calcul qui domine, pas la mémoire, et chaque cœur, même lent, fait sa part.

Il n'y a donc pas de bon réglage unique. On n'a rien changé : c'est un choix à faire, et on l'a noté pour l'auteur, car les PC à processeur Intel récent ont, eux aussi, des cœurs rapides et des cœurs lents.

## La fusion, et le classeur en noir et blanc

L'auteur avait publié Albireo 1.0.12 entre-temps. On a fusionné ses six commits. Un seul conflit, facile : l'auteur avait réorganisé les deux fonctions de l'attention où le fork avait ajouté quatre lignes en tête. On a gardé sa nouvelle version, et nos quatre lignes devant. Tout compile, les tests passent, Pseudalis réussit ses 34 étapes.

Puis on a lu ce que la nouvelle version change. L'attention garde en mémoire, pour chaque token déjà lu, une « clé » et une « valeur ». Jusqu'ici, elles étaient rangées en demi-précision, 16 bits par nombre. Désormais, le service les range par défaut en 8 bits, avec une échelle commune pour chaque groupe de 32 nombres. C'est passer d'un classeur en couleur à un classeur en noir et blanc : on en fait tenir deux fois plus sur l'étagère, et les modèles retrouvent le long contexte pour lequel ils ont été entraînés.

Le piège : nos noyaux NEON de l'attention, ceux de [l'épisode 11](11-deux-facons-d-arrondir.md), ne savaient lire que le classeur en couleur. Pour le nouveau format, l'auteur a écrit une version AVX2 pour les PC et une version de secours pour les autres. Sur le Mac, c'est donc la version de secours qui tournait. Mesuré avec les clés en 8 bits, comme le service : un document de 4 096 tokens était lu à 36 ou 38 tokens par seconde, au lieu de 163. La fusion, sans qu'aucun test ne bronche, rendait le service quatre fois plus lent sur les longs documents.

## Déplier, puis calculer comme avant

La correction tient en une idée. Un nombre de 8 bits multiplié par une échelle en demi-précision tombe toujours juste dans un nombre à virgule ordinaire : le produit est exact, quelle que soit la façon de le calculer. Il suffit donc de « déplier » les clés du classeur en noir et blanc, huit nombres à la fois, et de les donner au calcul de l'épisode 11, qui ne change pas.

Le résultat est identique au bit près au code de secours : on a comparé 351 780 valeurs, sur 224 cas de tailles différentes, entre le moteur NEON et un moteur compilé sans NEON. Aucune différence. Et le moteur répond toujours que la capitale de la France est Paris.

La vitesse revient : 158 à 162 tokens par seconde pour lire 4 096 tokens, au lieu de 36 à 38, et 22,5 tokens par seconde pour écrire la suite, au lieu de 9. C'est la vitesse du classeur en couleur (166 et 22 au même moment), avec deux fois moins de mémoire. Le mérite du format revient à l'auteur ; le portage n'a fait que lui apprendre à lire le Mac.

## Pour les curieux : les chiffres

Les commandes (le sixième argument `q8` range les clés et valeurs en 8 bits, comme le service) :

```
bench_runtime <modèle> 8 64 64 1 q8
bench_runtime <modèle> 8 4096 32 1 q8
```

La photographie du 4B en génération, avant tout changement (8 fils, en échantillons) :

```
albireo::qk_dot(dlib::ggml_type, unsigned char const*, void const*, long)  (in bench_runtime)        18570
swtch_pri  (in libsystem_kernel.dylib)        5332
```

Les noyaux Q4_K et Q6_K, mesures alternées, `bench_runtime <modèle> 8 64 64 1`, charge de 3,2 à 7,1 :

| Essai | Modèle | Avant | Après |
|---|---|---|---|
| 1 | 2B | 35,4 tokens/s | 38,9 tokens/s |
| 1 | 4B | 17,0 tokens/s | 18,1 tokens/s |
| 2 | 2B | 36,8 tokens/s | 38,2 tokens/s |
| 2 | 4B | 16,5 tokens/s | 18,0 tokens/s |

L'attention en 8 bits, mesures alternées entre le moteur fusionné seul et le moteur corrigé, charge de 2,7 à 9,9 :

| Essai | Cas | Fusion seule | Corrigé |
|---|---|---|---|
| 1 | 2B, lecture de 4 096 tokens | 37,7 tokens/s (109 s) | 162,3 tokens/s (25 s) |
| 1 | 2B, écriture après 4 096 tokens | 9,1 tokens/s | 22,6 tokens/s |
| 1 | 4B, écriture | 16,4 tokens/s | 17,8 tokens/s |
| 2 | 2B, lecture de 4 096 tokens | 36,0 tokens/s (114 s) | 157,7 tokens/s (26 s) |
| 2 | 2B, écriture après 4 096 tokens | 8,9 tokens/s | 22,5 tokens/s |
| 2 | 4B, écriture | 16,6 tokens/s | 17,8 tokens/s |

Quatre fils contre huit, le 4B en écriture (une série) :

| Fils | 1 | 2 | 4 | 6 | 8 |
|---|---|---|---|---|---|
| Tokens/s | 8,6 | 14,7 | 19,4 | 18,3 | 18,2 |

Dans le code de l'auteur, la correction de l'attention en 8 bits tient en deux fois quatre lignes, en tête de `qk_attention_scores_q8` et `qk_attention_values_q8`, sous la condition `ALBIREO_QK_NEON`. Le reste vit dans `qkernels_neon.inc`, le fichier du fork.

## À suivre

Le choix du nombre de fils : quatre pour écrire, huit pour lire les longs documents, ou un réglage qui fasse les deux. Puis la page de Pseudalis à essayer dans un navigateur, et les tests de dlib, toujours en attente.
