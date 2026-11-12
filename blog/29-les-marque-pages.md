# Dlib Transformer Extensions sur macOS, épisode 29 : les marque-pages

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B), inchangés depuis l'épisode 20 ; Qwen3.5-4B : écriture **19,5 tokens/s**, lecture **148 tokens/s** sur le GPU ; Qwen 3.6 11B-A3B : écriture **26 tokens/s**.

Suite de [l'épisode 28](28-le-comite-d-experts.md) : les modèles Qwen 3.5 et 3.6 écrivaient sur le GPU, mais deux défauts restaient. On ne pouvait pas reprendre une conversation au milieu, et la lecture d'un long prompt se faisait sur le processeur seul.

## Des marque-pages dans le carnet

Le carnet de notes des couches de Qwen 3.5, vu à l'épisode 26, a un défaut de naissance : on ne peut pas revenir en arrière. Si l'on modifie le dernier message d'une longue conversation, le moteur doit tout relire depuis le début. Pour un échange de 10 000 tokens lu par le processeur, cela fait environ deux minutes d'attente.

Le remède est celui d'un lecteur prudent : poser des marque-pages. Pendant la lecture d'un prompt, le moteur photocopie le carnet toutes les 256 positions et garde les quatre photocopies les plus récentes de chaque conversation, dans une limite d'un gigaoctet en tout. Une reprise repart du marque-page le plus proche. L'essai : deux questions qui partagent un prompt système de 794 tokens. La seconde reprend 768 tokens déjà lus au lieu de zéro, et sa réponse est la même, au caractère près, que lorsqu'elle est posée seule.

## Lire aussi sur le GPU

Le GPU lisait déjà les prompts des modèles classiques. Pour Qwen 3.5, il lui fallait savoir développer en nombres ordinaires les poids aux formats Q5_K, Q8_0 et Q5_0, et respecter les marque-pages. Or le GPU ne s'arrête pas au milieu d'un calcul pour qu'on photocopie le carnet. Le moteur coupe donc un long prompt aux frontières des 256 positions, le fait lire morceau par morceau, et photocopie le carnet entre deux morceaux. Sur un prompt de 783 tokens, le Qwen3.5-4B lit à 148 tokens par seconde sur le GPU, contre 86 sur le processeur.

## Le point d'exclamation

En lançant les tests complets, une étape a échoué. À « What is the capital of France? », le petit Qwen3.5-0.8B répondait « !!!!!!!!!!!!!!! ». Le contrôle fait au chargement du modèle était pourtant passé, à 0,1 % près.

L'enquête a pris quelques étapes.
- **Le défaut tient à la longueur du prompt** : juste sous 24 tokens, où le processeur lit seul ; faux de 24 à 31 tokens, et à 40 ; juste à 32. Or le contrôle de chargement lisait justement un prompt de 32 tokens, la seule longueur qui passait.
- **Une trace couche par couche** a montré des valeurs non numériques dès le premier produit de la première couche. Ses entrées étaient saines, sa matrice aussi ; seul le résultat était faux.
- **Une première hypothèse s'est révélée fausse** : une lecture au-delà de la fin de la matrice. On l'a vérifiée, elle n'expliquait rien, et on l'a abandonnée.
- **La bonne** : la bibliothèque d'Apple qui fait ces produits, Metal Performance Shaders, découpe le calcul en tuiles de lignes. Pour cette forme de matrice-là, celle du petit modèle, la dernière tuile incomplète sortait fausse quand le nombre de lignes n'était pas un multiple de 32.

La correction est simple : les produits portent désormais sur un nombre de lignes arrondi au multiple de 32 supérieur, et les lignes en trop sont calculées pour rien. Le contrôle de chargement lit maintenant un prompt de 29 tokens, une longueur choisie pour faire apparaître ce genre de cas. Le Qwen3-VL de 4 milliards qu'utilise le service n'était pas touché, ses matrices ayant d'autres formes. Il donne toujours la même réponse sur le GPU et sur le processeur.

## Pour les curieux : les chiffres

Lecture d'un prompt de 783 tokens par Qwen3.5-4B Q4_K_M, mode économie d'énergie coupé, mesures alternées :

| Essai | Processeur | GPU |
|---|---|---|
| 1 | 86,2 tokens/s | 149,4 tokens/s |
| 2 | 86,1 tokens/s | 148,3 tokens/s |

Le contrôle de chargement du petit modèle, selon la longueur du prompt, avant et après la correction :

| Tokens | Avant | Après |
|---|---|---|
| 24, 26, 31 | valeurs non numériques | 0,03 à 0,06 % |
| 32 | 0,10 % | 0,10 % |
| 40 | valeurs non numériques | 0,04 % |
| 100 | valeurs non numériques | 0,39 % |

## À suivre

Avant l'arrivée du Mac mini, un script qui refera toutes les mesures en une seule commande, pour comparer les deux machines dès le premier jour.
