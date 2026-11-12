# Dlib Transformer Extensions sur macOS, épisode 30 : la table de multiplication

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), inchangées ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens et **423 tokens/s** sur 4 096 tokens (2B), **152 tokens/s** sur un prompt d'environ 3 660 tokens (4B) ; Qwen3.5-4B : écriture **19,5 tokens/s**, lecture **139 tokens/s** sur 3 660 tokens.

Suite de [l'épisode 29](29-les-marque-pages.md) : Qwen 3.5 lisait ses prompts sur le GPU. Mais tous les modèles butaient sur la même limite : passé 2 048 positions, le GPU rendait la main au processeur, plus rapide que lui sur les longs textes.

## Le script du premier jour

D'abord, la promesse de l'épisode précédent. Un script, `tools/bench_macos.sh`, refait maintenant toutes les mesures du moteur en une seule commande. Il note la machine (puce, cœurs, mémoire, version de macOS, mode économie d'énergie, charge), puis fait lire et écrire chaque modèle présent, sur le processeur puis sur le GPU, en alternance. Le résultat est un tableau prêt à comparer. Le jour où le Mac mini arrivera, on le lancera avant toute chose, et le MacBook Air aura déjà sa colonne.

## Pourquoi le GPU s'essoufflait

Lire un prompt, pour un modèle de langue, c'est faire regarder à chaque mot tous les mots qui le précèdent : c'est l'attention. Le GPU le faisait mot par mot. Chaque mot allait relire, seul, toute la liste des précédents. C'est comme une classe où chaque élève irait à la bibliothèque consulter le même dictionnaire, chacun à son tour. Pour 4 000 mots, cela fait des millions d'allers-retours, et le processeur, plus malin sur ce point, finissait par gagner.

## Première marche : lire en groupe

La première idée a été de faire lire les élèves en groupe. Un groupe de 4 à 16 mots consécutifs charge une page de 32 mots précédents dans une petite mémoire partagée, tout près du calcul, et chacun s'en sert. Le dictionnaire n'est plus ouvert qu'une fois par groupe.

Une première version a déçu : la page était bien partagée, mais chaque comparaison entre deux mots se faisait à 32 mains, qui devaient ensuite mettre leurs résultats en commun. Le temps passait à se concerter. La seconde version confie à chaque main un mot entier de la page. Sur un prompt de 4 096 tokens, le petit modèle de 2 milliards est passé de 145 à 198 tokens par seconde, devant le processeur (171 à 181). C'était la version 4.10.0, et la limite du GPU est montée à 4 096 positions. Au-delà, à 8 192, le processeur restait encore devant.

## Seconde marche : la table de multiplication

Les GPU d'Apple savent faire une chose que le moteur n'utilisait pas : une équipe de 32 mains peut multiplier d'un coup deux petits tableaux de 8 nombres sur 8. C'est la différence entre faire ses multiplications une par une et connaître sa table par cœur. Or l'attention n'est faite que de ça : comparer un tableau de mots à un tableau d'autres mots, puis mélanger les seconds selon le résultat.

Le noyau a donc été réécrit avec ces multiplications de tableaux. Premier essai : 2 % de mieux seulement. L'erreur est facile à commettre et on l'a commise : on avait accéléré le calcul, alors que le temps se perdait ailleurs. Les mains passaient leur temps à recopier la page du dictionnaire dans la mémoire partagée, un nombre à la fois. En leur faisant recopier huit nombres d'un coup, le gain est enfin apparu :

| Qwen3-VL 2B, lecture d'un prompt | Processeur | GPU avant (4.9.0) | GPU en groupe (4.10.0) | GPU en tableaux (4.11.0) |
|---|---|---|---|---|
| 4 096 tokens | 170 à 177 | 145 | 198 | 423 à 424 |
| 8 192 tokens | 115 à 117 | — | 106 | 295 |
| 16 384 tokens | 67 | — | — | 171 |

Le GPU gagne maintenant à toutes les longueurs mesurées, de deux fois et demie environ. La limite n'a plus de raison d'être : le GPU lit désormais les prompts jusqu'à 262 144 positions, la longueur maximale des modèles. Elle reste un réglage, au cas où le Mac mini dirait autre chose.

## Le même texte, mot pour mot

Plus vite ne vaut rien si c'est faux. Le GPU ne calcule pas exactement comme le processeur : il arrondit en demi-précision, dans un autre ordre. On a donc fait lire à quatre modèles un long prompt d'environ 3 660 tokens, sur chacun des deux, en choix glouton, c'est-à-dire en prenant toujours le mot le plus probable. Les quatre réponses sont identiques d'un bout à l'autre. Et au chargement de chaque modèle, le moteur continue de comparer le GPU au processeur et repasse seul au processeur s'il trouve un écart.

## Pour les curieux : les chiffres

Mesures `bench_runtime` (8 threads, clés et valeurs en 8 bits), mode économie d'énergie coupé, charge 2,8 à 3,1, processeur et GPU en alternance, Albireo 4.11.0 :

| Qwen3-VL 2B Q4_K_M | Processeur | GPU |
|---|---|---|
| 512 tokens | 243,0 et 226,5 tokens/s | 608,1 et 603,4 tokens/s |
| 4 096 tokens | 177,3 et 170,0 tokens/s | 423,0 et 424,0 tokens/s |
| 8 192 tokens | 116,6 et 114,5 tokens/s | 295,4 et 295,1 tokens/s |
| 16 384 tokens (un essai) | 67,2 tokens/s | 170,9 tokens/s |

Les longs prompts d'environ 3 660 tokens, même réponse sur les deux :

| Modèle | GPU | Processeur |
|---|---|---|
| Qwen3-VL 2B | 423 tokens/s | 168 tokens/s |
| Qwen3-VL 4B | 152 tokens/s | 62 tokens/s |
| Qwen3.5-4B | 139 tokens/s | 80 tokens/s |
| Qwen3.5-0.8B | 493 tokens/s | 419 tokens/s |

Le noyau s'appelle `attention_mtile`. Un groupe de calcul prend une tuile de 2 à 32 tokens et une tête de clés ; les tranches de 32 positions passent en demi-précision dans la mémoire de threadgroup ; les scores et la somme pondérée sont des `simdgroup_multiply_accumulate` sur des matrices 8 × 8, et la remise à l'échelle du softmax en ligne, une multiplication par une matrice diagonale. L'écart du contrôle de chargement est de 0,29 % sur les scores du 2B.

## À suivre

Le rangement de la page web d'Albireo, dont le fichier principal compte plus de 7 000 lignes, en attendant le Mac mini.
