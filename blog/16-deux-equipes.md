# Dlib Transformer Extensions sur macOS, épisode 16 : deux équipes

Suite de [l'épisode 15](15-le-classeur-en-noir-et-blanc.md) : on y avait constaté qu'Albireo écrit plus vite avec quatre threads qu'avec huit, mais lit plus vite les longs documents avec huit. Il fallait choisir. On a choisi de ne pas choisir.

## Un thread, ce n'est pas un agent

D'abord un mot de vocabulaire, parce que la question nous a été posée. Un *thread*, c'est un fil d'exécution à l'intérieur d'un programme : un ouvrier qui fait exactement le morceau de calcul qu'on lui confie, sans rien décider. Ce n'est pas un agent d'intelligence artificielle. Le moteur embauche ses threads une fois pour toutes, au démarrage, et les garde dans un *thread pool*, une équipe permanente. À chaque multiplication, il découpe le travail en paquets, et chaque thread prend le paquet suivant.

Le M2 a quatre cœurs rapides et quatre cœurs lents, et le moteur embauche huit threads, un par cœur.

## Lire et écrire ne demandent pas le même effort

Lire un long document, c'est beaucoup de calcul pour chaque poids du modèle : les huit cœurs, même les lents, ont tous de quoi faire. Écrire un token, c'est relire tous les poids pour très peu de calcul. Là, c'est la mémoire qui freine, et les cœurs lents ne font que retarder la fin de chaque multiplication. C'est un chantier où l'on attend le plus lent pour fermer le camion.

## Deux équipes, un seul chantier

La solution retenue : sur Mac, le thread pool garde une seconde équipe, plus petite, d'un thread par cœur rapide. macOS dit combien il y en a, par une simple question au système (`hw.perflevel0.physicalcpu`, qui répond 4 sur le M2).

Au début de chaque passe, le moteur regarde combien de tokens il doit traiter. Moins de 24, c'est une écriture ou une question très courte : il prend la petite équipe. Davantage, c'est une lecture : il prend la grande. Les threads de l'équipe qui ne sert pas s'endorment au bout de deux millisecondes. Il ne reste alors que quatre threads au travail, et macOS les place de lui-même sur les cœurs rapides.

Le seuil de 24 tokens n'a pas été inventé pour l'occasion. C'est celui où, depuis [l'épisode 13](13-l-atelier-d-a-cote.md), les multiplications quittent les noyaux qui calculent en nombres entiers, limités par la mémoire, pour passer par Accelerate, qui calcule beaucoup.

## Ce que ça donne

Le gain est modeste, mais il se répète à chaque mesure. Le modèle du service (4B) écrit à 18,2 à 18,6 tokens par seconde, contre 17,6 à 18,1 avec les huit threads, en quatre tours alternés. Le petit modèle (2B) passe de 37,4 à 37,9 tokens par seconde à 39,7 à 40,8. La lecture d'un document de 4 096 tokens garde la vitesse des huit threads : 162 à 167 tokens par seconde.

Une question de 22 tokens est maintenant lue à 88 tokens par seconde. Ce chiffre vient d'une seule mesure, pas d'une série alternée.

Un détail d'exactitude, pour être franc : depuis la version 1.0.12, l'auteur découpe l'attention de l'écriture selon le nombre de threads. Avec la petite équipe, les résultats sont donc ceux d'un moteur réglé sur quatre threads, à la dernière décimale près. C'est déjà le cas aujourd'hui pour qui règle le service sur quatre threads, et les réponses restent justes.

## Pour les curieux : la mesure

`bench_runtime <modèle> 8 64 64 1 q8`, 4B, en alternance : ancien moteur avec 8 threads, nouveau moteur, ancien moteur forcé à 4 threads.

| Tour | 8 threads | Deux équipes | 4 threads |
|---|---|---|---|
| 1 | 18,1 tokens/s | 18,6 tokens/s | 18,5 tokens/s |
| 2 | 17,9 tokens/s | 18,2 tokens/s | 18,3 tokens/s |
| 3 | 17,7 tokens/s | 18,4 tokens/s | 17,8 tokens/s |
| 4 | 17,6 tokens/s | 18,3 tokens/s | 18,2 tokens/s |

Dans le code de l'auteur, le changement tient en quelques lignes sous `#if defined(__APPLE__)` dans `thread_pool.h` et `cpu_runtime.cpp`, plus deux lignes où `pool.size()` devient `pool.active()`, le nombre de threads de l'équipe au travail. Le reste vit dans `thread_pool_apple.inc`, un fichier du fork.

## À suivre

Le processeur plafonne à environ 45 Go par seconde quand il relit les poids. Le GPU du M2, qui partage la même mémoire, sait en tirer presque le double. Le moteur GPU d'Albireo est écrit pour les cartes NVIDIA et ne tourne pas sur Mac : un moteur Metal serait un gros chantier, à peser avant de l'ouvrir.
