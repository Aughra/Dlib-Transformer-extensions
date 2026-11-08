# Dlib Transformer Extensions sur macOS, épisode 13 : l'atelier d'à côté

Suite de [l'épisode 12](12-le-chronometre-du-test.md) : les deux applications passent tous leurs tests sur le Mac. On avait annoncé les tests de dlib pour la suite. Ils attendront encore un peu : la priorité est restée la vitesse d'Albireo, et cette fois, il a fallu renoncer à une règle qu'on suivait depuis l'épisode 8.

## Le mauvais suspect

La demande partait d'un format de poids, le Q8_0. C'est celui du modèle de recherche, celui qui lit les documents pour retrouver ensuite les bons passages, et celui du projecteur qui lit les images. Il n'avait pas de calcul NEON à lui : on pensait que c'était lui qui ralentissait.

Une photographie du moteur, prise pendant que le modèle de recherche lisait un texte, a dit autre chose. Le Q8_0 se « décompresse » avant d'être multiplié, comme on déballe un colis avant d'en utiliser le contenu. Le déballage ne prenait que 2 à 3 % du temps. Les 80 % restants passaient dans la multiplication elle-même, le produit en flottants qu'on avait accéléré à l'épisode 9. Un calcul Q8_0 dédié n'aurait presque rien gagné.

## La règle du bit près

Le produit de l'épisode 9 était volontairement bridé. Depuis l'épisode 8, chaque calcul NEON du fork donnait exactement les mêmes nombres que le code d'origine, au dernier chiffre près, et donc exactement la même réponse. Pour y arriver, il refusait le geste le plus rapide de la puce, la multiplication-addition fusionnée : c'est l'histoire des deux calculatrices de l'épisode 11.

Pour aller plus vite, il fallait lâcher cette règle. On a présenté trois chemins à l'humain, et il a choisi le plus radical : Accelerate, la bibliothèque de calcul qu'Apple livre avec macOS. Ses résultats diffèrent au dernier chiffre, et le fork s'écarte un peu plus de l'original, qui ne connaît pas Accelerate. En échange, on gagne l'accès à une pièce de la puce qu'aucun code du projet n'utilisait.

## L'atelier d'à côté

Les puces d'Apple ont, à côté de leurs cœurs, un coprocesseur dédié aux multiplications de grands tableaux de nombres. Apple n'en documente pas les instructions : on n'y accède que par Accelerate. C'est un atelier de découpe industrielle installé à côté de l'établi, dont seul le contremaître a la clé.

Un premier essai, hors du moteur, a donné l'échelle. Un produit de la taille de ceux du modèle 2B se fait à 1 060 milliards d'opérations par seconde en un seul appel. Le même produit, découpé en petits morceaux confiés aux huit cœurs, tombe à 315 milliards. L'atelier est partagé par les cœurs d'un même groupe : huit ouvriers qui s'y bousculent travaillent moins bien qu'un seul qui lui confie une grosse commande.

Il fallait pourtant déballer les poids avant de les confier à l'atelier, et ce déballage, lui, se fait très bien à huit. D'où une chaîne de montage : le moteur découpe les poids en tranches de 256 lignes. Pendant que l'atelier multiplie une tranche, les sept autres cœurs déballent la suivante. On a essayé des tranches de 64, 128, 256 et 512 lignes ; 256 était la meilleure.

## Pas pour trois planches

On ne démarre pas une machine industrielle pour couper trois planches. Le déballage coûte le même temps quel que soit le nombre de tokens à traiter, et sur une question très courte, il ne se rattrape pas. On a donc mesuré, taille par taille, les trois façons de faire :

| Tokens lus | Calcul en entiers | Ancien produit NEON | Accelerate |
|---|---|---|---|
| 8 | **0,9 ms** | 1,7 ms | 2,3 ms |
| 16 | **1,8 ms** | 2,5 ms | 2,4 ms |
| 24 | 2,7 ms | 3,4 ms | **2,6 ms** |
| 64 | 7,0 ms | 7,8 ms | **3,2 ms** |

Le tableau a montré au passage autre chose. Le code d'origine bascule vers le calcul en flottants dès 8 tokens. Sur le Mac, c'est trop tôt : le calcul en entiers garde l'avantage jusque vers 24 tokens. Ce seuil avait sans doute été réglé sur un PC à processeur Intel ou AMD, où les rapports de force ne sont pas les mêmes. Sur le Mac seulement, on l'a porté à 24. La remarque est partie dans les notes pour l'auteur, avec le conseil de le remesurer sur PC.

## Trois fois plus vite

Mesures à tour de rôle, avant et après, dans la même séance :

| | Avant | Après |
|---|---|---|
| Lecture de 512 tokens, modèle 2B | 64 à 72 tokens/s | 217 tokens/s |
| Lecture de 4 096 tokens, modèle 2B | 52 à 58 tokens/s | 156 à 163 tokens/s |
| Lecture de 512 tokens, modèle de recherche | 184 à 213 tokens/s | 573 à 601 tokens/s |

Un texte de 4 096 tokens, qu'on lisait en 2 min 40 s avant l'épisode 11, puis en 1 min 20 s, se lit maintenant en 25 secondes. L'écriture, un mot à la fois, ne passe pas par ce chemin : elle n'a pas bougé.

## Le prix payé

Les tests de l'auteur passent tous. Celui des produits ne demande pas l'égalité au bit près : il compare chaque résultat à un calcul en double précision, avec une marge d'arrondi, et les résultats d'Accelerate y entrent.

Les réponses, elles, ne sont plus identiques mot pour mot. On a redemandé au modèle de résumer les épisodes 8 et 9 de ce blog. L'ancien moteur et le nouveau disent la même chose, mais pas avec les mêmes phrases : un dernier chiffre différent, quelques milliers d'opérations plus loin, suffit à faire choisir un autre mot, et la suite part de ce mot-là. Sur une question courte, la réponse reste « La capitale de la France est **Paris**. »

C'est un vrai renoncement, et on l'a fait en connaissance de cause. L'original vit déjà avec : ses calculs pour PC utilisent la multiplication-addition fusionnée, et ne devraient donc pas tomber au bit près sur son code portable non plus (on n'a pas de PC pour le vérifier). Les calculs de l'écriture et de l'attention, eux, restent exacts au bit près sur le Mac.

## Pour les curieux : le code et les chiffres

Le cœur de `engine/qkernels_accelerate.inc`, un fichier propre au fork : à chaque passe du pool, la tâche 0, prise par le fil appelant, confie la tranche prête à Accelerate, et les autres tâches déballent la suivante.

```cpp
pool.parallel_for(1 + groups, [&](long task, int) {
    if (task == 0)
    {
        if (s > 0)
        {
            const long r0 = (s - 1) * accelerate_slice;
            product(ready, r0, std::min(accelerate_slice, rows - r0));
        }
        return;
    }
    const long g0 = (task - 1) * accelerate_group;
    const long g1 = std::min(n_next, g0 + accelerate_group);
    for (long r = g0; r < g1; ++r)
        qk_dequantize_row(W.type, W.data + static_cast<size_t>(r_next + r) * W.row_bytes,
            next + r * K, K);
});
```

Les essais de `cblas_sgemm` seul, hors du moteur (512 tokens, 2 048 colonnes, 6 144 lignes) :

| Découpage | Débit |
|---|---|
| Un seul appel | 1 060 Gflop/s |
| Tranches de 256 lignes, appels successifs depuis un fil | 882 Gflop/s |
| Tranches de 256 lignes, 8 fils en parallèle | 796 Gflop/s |
| Blocs de 16 lignes, 8 fils en parallèle | 315 Gflop/s |

Les mesures alternées, `bench_runtime` à 8 fils, `lowpowermode 0`, charge moyenne de 2 à 14 :

| Essai | Version | 2B, 64 tokens | 2B, 512 tokens | 2B, 4 096 tokens | Recherche, 512 tokens |
|---|---|---|---|---|---|
| 1 | avant | 62,8 tokens/s | 71,7 tokens/s | 57,5 tokens/s | 212,5 tokens/s |
| 1 | Accelerate | 133,7 tokens/s | 217,5 tokens/s | 162,8 tokens/s | 600,9 tokens/s |
| 2 | avant | 60,4 tokens/s | 64,2 tokens/s | 51,6 tokens/s | 183,7 tokens/s |
| 2 | Accelerate | 114,1 tokens/s | 216,8 tokens/s | 155,9 tokens/s | 572,8 tokens/s |

Dans les fichiers de l'original, le changement tient en quelques lignes : une ligne dans le CMake, sous `if (APPLE)`, pour lier Accelerate ; dans `qkernels.cpp`, une inclusion et un aiguillage en tête de `matmul_float` ; dans `qkernels.h`, le seuil de 24 tokens, sous une condition propre au Mac.

## À suivre

Le projecteur d'images, en Q8_0, profite lui aussi du nouveau chemin, mais on ne l'a pas encore mesuré sur une vraie image. Restent ensuite la page de Pseudalis à essayer dans un navigateur, et les tests de dlib.
