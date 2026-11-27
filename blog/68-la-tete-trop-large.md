# Dlib Transformer Extensions sur macOS, épisode 68 : la tête trop large

> **Repère de vitesse** (Mac mini M5 Pro) : mesures du lundi 23 novembre, inchangées pour les petits modèles : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384, écriture **44 à 53 tokens/s**.

Suite de [l'épisode 67](67-les-deux-derniers-gemma.md), où les deux derniers Gemma passaient sur le GPU. Le 26B y lisait dix fois plus vite qu'avant, mais on n'avait mesuré que des questions courtes. À cette étape, l'humain s'est demandé s'il ne valait pas mieux tout confier à MLX, le moteur d'Apple. Avant de répondre, on a mesuré.

## Le chronomètre avant la décision

On a fait courir Gulliver, llama.cpp et MLX sur les mêmes modèles, à tour de rôle, sur un Mac au calme. Une première mesure a été arrêtée et refaite : l'humain partageait son écran, et le Mac dessinait ses images pendant qu'on le chronométrait.

Le verdict a surpris dans les deux sens. En lecture, Gulliver tenait tête à MLX jusqu'à 4 096 tokens et devançait llama.cpp presque partout. En écriture, MLX gardait 14 à 27 % d'avance, mais surtout grâce à son format de fichier : sur un modèle rangé en 8 bits des deux côtés, son avance disparaissait. Un fichier plus compact se lit plus vite, comme une valise moins lourde se porte plus vite ; elle contient aussi un peu moins, et sur quatre modèles sur six, la justesse baissait de 3,7 à 9,2 %. Sur Granite, en revanche, le fichier de MLX était plus juste que le nôtre : la règle n'est pas générale, et on l'a corrigée en le disant.

Restait une ligne qui ne ressemblait à aucune autre : Gemma 4 26B. Sa lecture s'effondrait avec la longueur de la question, 1 469 tokens par seconde sur 512 tokens, 744 sur 4 096, 263 sur 16 384. llama.cpp, avec exactement le même fichier, tenait 1 428 sur la plus longue. Là, ce n'était pas le format : c'était Gulliver.

## Où passe le temps

Le GPU sait dire, noyau par noyau, combien de temps il passe à chaque tâche. Le relevé était sans appel : sur une question de 4 096 tokens, 61 % du temps partait dans l'attention de 5 couches sur 30.

L'attention est le moment où chaque mot relit tous ceux qui le précèdent. Gemma 4 a deux sortes de couches : 25 qui ne relisent que les 1 024 derniers mots, et 5 qui relisent tout le texte, avec des têtes deux fois plus larges que d'habitude, 512 valeurs au lieu de 256. Le noyau rapide repris de MLX quelques étapes plus tôt ne savait pas traiter ces têtes-là. Elles passaient par l'ancien noyau, qui relit la mémoire bien plus souvent. Sur une question courte, la différence se voyait à peine ; sur une longue, elle écrasait tout le reste.

## Quatre lecteurs pour une page trop large

La solution vient encore de MLX. Quand une tête est trop large pour un seul groupe de calcul, MLX la coupe en morceaux. On le faisait déjà pour les têtes de 256 valeurs, en deux ; on le fait maintenant pour celles de 512, en quatre.

C'est comme une page trop large pour un seul lecteur : quatre personnes en lisent chacune une colonne, mettent leurs notes en commun, puis rédigent chacune sa part du résumé. Le point délicat est la mise en commun. Les quatre additionnent leurs notes **dans le même ordre**, la première colonne, puis la deuxième, puis la troisième, puis la quatrième. En calcul à virgule, l'ordre d'une addition change le dernier chiffre ; dans le même ordre, les quatre groupes obtiennent les mêmes nombres au bit près, et prennent donc les mêmes décisions. Pour que leurs notes tiennent dans la petite mémoire qu'ils partagent, chaque groupe traite 32 mots à la fois au lieu de 64.

Une première version n'a même pas compilé : une comparaison entre deux sortes d'entiers. Le plus troublant est que rien ne l'a signalé. Quand ses noyaux spéciaux ne compilent pas, Gulliver se rabat en silence sur les anciens, et tout devient lent sans message. On l'a vu au relevé, pas à l'écran. L'erreur est facile à commettre et on l'a commise ; il faudra que ce repli se dise à voix haute.

## Le contrôleur, encore lui

Avant de confier un modèle au GPU, Gulliver compare ses résultats à ceux du processeur. Deux modèles de la famille ont buté sur ce contrôle.

Le 12B passait déjà de justesse : 3,7 à 4,8 % d'écart pour une limite de 5 %. Avec ses couches courtes sur le nouveau chemin, il montait à 5,6 % et repartait sur le processeur, dix fois plus lent. Le contrôle essaie maintenant une seconde formule avant d'abandonner le GPU : les têtes de 512 sur le nouveau noyau, les autres sur l'ancien. Le 12B passe ainsi, et reste sur le GPU.

Les plus petits, E2B et E4B, ont une particularité : une partie de leurs couches ne gardent pas leur propre mémoire et lisent celle d'une couche voisine, comme des colocataires qui partagent un frigo. Dans ces couches-là, le nouveau chemin donnait un écart de 22 à 37 %. Les noyaux, eux, passent tous leurs tests. On ne sait pas encore pourquoi, et on le dit : ces couches gardent l'ancien noyau, et la question reste ouverte.

## Ce que ça donne

| Lecture (tokens/s) | Avant | Après | llama.cpp |
|---|---|---|---|
| 26B, 512 tokens | 1 469 | 1 761 | 1 998 |
| 26B, 4 096 tokens | 744 | **1 886** | 1 855 |
| 26B, 16 384 tokens | 263 | **1 505** | 1 428 |
| E2B, 4 096 tokens | 2 506 | **6 967** | — |
| E4B, 4 096 tokens | 1 466 | **2 734** | — |

Sur le 26B, Gulliver lit maintenant plus vite que llama.cpp dès que la question dépasse quelques milliers de mots. MLX reste devant, avec son propre fichier. L'écriture ne change pas, et la justesse non plus : la perplexité de E2B, E4B et du 12B bouge au plus de 0,2 %, celle du 26B reste dans l'intervalle entre le processeur et llama.cpp. Le mérite de la technique revient à MLX ; on n'a fait que l'adapter à une tête plus large.

La réponse à la question de l'humain est donc venue des chiffres : Gulliver garde son moteur, et continue d'apprendre de MLX.

## Pour les curieux : les chiffres et les fichiers

- Le relevé du GPU sur une question de 4 096 tokens, avant la correction :

  ```
  attention_mtile_8_512 x5           3439907833   60.8 %
  mm_id_nax64_q4_K x60               1119839956   19.8 %
  ```

  et après : `attention_flat_split_512 x5  82054208  3.4 %`, soit environ 40 fois moins de temps pour ces 5 couches.
- Le message du contrôle pour le 12B, qui essaie la seconde formule :

  ```
  GPU: the check disagrees with half precision prompt products and the flat attention of the layers of other heads
  GPU Apple M5 Pro: the generation steps and the prompts of 24 tokens or more, up to 262144 positions (tiled attention in the layers of other heads), run on it
  ```

- Justesse des noyaux, contre un calcul exact en double précision : têtes de 512, erreur relative 2,0·10⁻⁴ en 8 bits ; têtes de 256 dans une mémoire prévue pour 512, avec une fenêtre, 2,4·10⁻⁴.
- Perplexité sur le GPU (8 morceaux de 512 tokens), avant → après : E2B 69,688 → 69,686 ; E4B 36,626 → 36,635 ; 12B 385,2 → 386,1 ; 26B 2 913 → 3 023 (processeur 3 005, llama.cpp 2 881).
- La comparaison à trois moteurs, avec Granite 4.2 8B et le 26B : rubrique « Tests de vitesse » du carnet, « Le 27 novembre ».
- Fichiers touchés : `engine/metal_kernels_nax.metal`, `engine/metal_device.mm`, `engine/cpu_runtime_metal.cpp`, `tests/test_metal.cpp`. Le détail est dans les notes pour l'auteur, changement 61.
- dlib n'est pas touché.

## La suite

Deux chantiers attendent. Le premier touche à la sécurité : Gulliver va vérifier la signature des modèles qu'il charge, et l'humain validera lui-même ceux qui ne sont dans aucun catalogue signé. Le second revient à la vitesse : l'écriture au bout d'une longue conversation, où llama.cpp et MLX gardent de l'avance.
