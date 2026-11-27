# Dlib Transformer Extensions sur macOS, épisode 43 : la tête en deux tranches

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B et Gemma 4 E2B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 42](42-le-grand-tri-de-dlib.md) : on avait rangé dlib. On revient au chantier laissé à l'épisode 41. Gemma 4 écrivait ses réponses sur le GPU, mais lisait encore ses prompts sur le processeur.

## L'établi trop petit

Pour lire un long prompt d'un coup, le GPU découpe le travail en tuiles : un petit groupe de calcul prend quelques mots du prompt et les compare à tous les mots qui les précèdent. Pour aller vite, chaque groupe pose ce qu'il manipule sur une mémoire rapide qui lui est propre, une sorte d'établi. Sur le M2, l'établi fait 32 Ko.

Les têtes d'attention de Gemma 4 ont, dans ses couches globales, 512 valeurs, deux fois plus que les autres modèles du fork. Le noyau des tuiles aurait dû poser 8 Ko de requêtes et 32 Ko de clés : plus que l'établi tout entier. Il aurait aussi gardé en main 64 morceaux de résultat à la fois, bien trop pour les registres d'un groupe. C'est pour cela que l'épisode 41 avait laissé les prompts de Gemma 4 au processeur.

## Deux traducteurs pour un texte

La solution retenue coupe chaque tête en deux tranches de 256 valeurs. Deux groupes se partagent une tête : chacun calcule la moitié du résultat. Mais pour savoir quels mots regarder, il faut comparer les têtes entières. Chaque groupe calcule donc les scores sur les 512 valeurs, en posant les clés sur l'établi en deux fois, une moitié après l'autre, et en additionnant. Puis il ne charge que sa moitié des valeurs, et n'écrit que sa moitié de la sortie.

C'est le travail de deux traducteurs qui se partagent un long texte. Chacun n'écrit que sa moitié de la traduction, mais chacun doit d'abord lire le texte en entier pour savoir de quoi il parle. La lecture est faite deux fois ; c'est le prix de la place. En échange, tout tient : 8 Ko de requêtes, 16 Ko de clés ou de valeurs, moins de 2 Ko pour les scores, et pas plus de morceaux en main que pour une tête de 256.

Le coût ne vaut que pour 7 couches sur 35 dans le E2B. Les 28 autres, celles qui ne regardent que les 512 derniers mots, ont des têtes de 256 et passent par les tuiles habituelles. D'où un second ajustement : une tuile ne contient pas le même nombre de mots selon la largeur des têtes, alors la passe prépare maintenant deux listes de tuiles, une par largeur, comme on prévoit des cartons de deux tailles pour un déménagement.

## Un test qui manquait

En préparant la vérification, on a constaté une lacune : le test des noyaux du GPU vérifiait l'attention des réponses, mot après mot, mais pas celle des prompts en tuiles. Cette dernière n'était contrôlée qu'au chargement de chaque modèle, en comparant ses résultats à ceux du processeur. Le test la vérifie désormais directement, contre un calcul en double précision, pour des têtes de 128, de 256 et de 512.

Les tuiles calculent en demi-précision, pour aller plus vite. Leur erreur la plus grande est de 1,4 dix-millième pour les têtes de 512, du même ordre que pour les têtes de 128 et de 256 déjà en service.

Au chargement de Gemma 4 E2B, le contrôle accepte maintenant le GPU pour les prompts. Ses scores s'écartent de 0,48 % de ceux du processeur, avec le même mot le plus probable. C'est plus que pour les étapes seules, mais dans l'ordre des autres modèles dont les prompts passent déjà par le GPU : 0,23 % pour Qwen3-VL 2B, 2,5 % pour Gemma 3 4B. Sur le long journal des épisodes précédents, lu cette fois par le GPU, la réponse reste la même : « The secret code word is **PELICAN** ». Et le modèle prend toujours l'enregistrement 3 pour celui d'Inès, comme sur le processeur.

Combien de temps cela fait-il gagner ? On ne le sait pas encore. Le Mac est en mode économie d'énergie, où toute mesure de vitesse serait fausse, et les scores calculés deux fois pourraient manger une partie du gain. Le Mac mini tranchera.

## Pour les curieux : les chiffres et les fichiers

- Noyau : `attention_mtile<REP, HD, NSG, SPLIT>` dans `engine/metal_kernels.metal` ; pour 512, 1 simdgroup (8 lignes de requête) et `SPLIT = 2`, instances `attention_mtile_{1,2,4,8}_512`. Le GPU compile maintenant 81 noyaux (77 avant).
- Hôte : `engine/metal_device.mm` (`tile_tokens` : 8 / rep tokens par tuile pour 512, grille de trois dimensions) ; `engine/cpu_runtime_metal.cpp` (deux listes de tuiles, `steps_only` retiré).
- `test_metal`, attention en tuiles contre la double précision :

  | Têtes | rep | 8 bits | Demi-précision |
  |---|---|---|---|
  | 128 | 4 | 1,17e-4 | 7,70e-5 |
  | 256 (dans un pool de 512) | 8 | 1,29e-4 | 7,31e-5 |
  | 512 | 8 | 1,43e-4 | 8,02e-5 |

- Contrôle au chargement de gemma-4-E2B-it Q4_K_M : prompt à 0,482 % du processeur, premier pas à 0,190 %, même token.
- Journal de 2 622 tokens (2 823 avec le gabarit de conversation), prompt lu par le GPU : « The secret code word is **PELICAN** ».
- Test complet d'Albireo : **53 étapes sur 53** ; l'étape « gemma 4 on the GPU » vérifie maintenant que les prompts y vont aussi.
- Albireo passe en **4.27.0**. Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Les mesures sur le Mac mini, attendu bientôt, diront si les deux traducteurs vont plus vite que le processeur seul. D'ici là, les images de Gemma 3.
