# Tests de vitesse : Gulliver contre les autres moteurs

Ce document compare Gulliver à trois autres moteurs, avec les mêmes modèles :

- **llama.cpp**, le moteur de référence des fichiers GGUF ;
- **MLX**, le moteur d'Apple ;
- **l'Albireo d'origine**, le programme Windows dont Gulliver est issu, construit à part et sans modification.

Il mesure la vitesse, puis la justesse, et explique d'où viennent les écarts. Le but est que Gulliver devienne le plus rapide sans rien perdre en justesse (priorité du projet depuis le 21 novembre 2026, chantier « Vitesse » de la feuille de route).

- **Mesures de ce document** : 21 novembre 2026, à partir de 11 h 30, Mac mini M5 Pro (64 Go), Gulliver 6.16.0, llama.cpp 1.0.0 (Homebrew), MLX 0.32.4, Albireo d'origine 1.0.27 (`af5c811`).
- **Chaque dimanche** : les mêmes mesures sont refaites par le Mac lui-même, à 4 h ; elles s'ajoutent, datées et la plus récente en tête, à la page « Mesures du dimanche », juste à côté dans le carnet (mode d'emploi : `macos/bench-hebdo/README.md`). Les tableaux complets, modèle par modèle, sont là.

## En bref

| | Gulliver | llama.cpp | MLX | Albireo d'origine (sur ce Mac) |
|---|---|---|---|---|
| Lire une question | 3e | 2e : +19 % à +174 % | **1er** : +32 % à +460 % | 4e : environ 100 fois plus lent |
| Écrire la réponse | 3e | 2e : +8 % à +84 % | **1er** (sauf un cas) : +13 % à +129 % | 4e : −62 % à −79 % |
| Justesse, même fichier | référence | identique (0,02 à 0,35 %) | autre fichier : de −3 % à +9 % | identique (0,02 à 0,23 %) |

En clair :

- **MLX est aujourd'hui le plus rapide partout**, sauf l'écriture après une longue question sur LFM2, où llama.cpp le devance.
- **Gulliver est troisième** : environ 1,3 à 5,6 fois derrière MLX en lecture, 1,1 à 2,3 fois en écriture.
- **Gulliver calcule aussi juste que llama.cpp et que l'original** : sur un même fichier, leurs perplexités se confondent.
- **Le portage a déjà beaucoup gagné sur l'original** : sur ce Mac, l'Albireo d'origine lit environ cent fois moins vite et écrit 2,6 à 4,8 fois moins vite que Gulliver. Ce n'est pas l'original sur un bon PC Windows (voir plus bas), mais c'est la mesure de ce que le fork a apporté sur cette machine.

## Deux vitesses, deux limites

Un moteur fait deux choses différentes, qui ne butent pas sur la même limite.

- **La lecture de la question** (« lecture », en jetons par seconde). Tous les mots de la question passent ensemble dans le modèle. C'est un travail de calcul pur : comme une usine qui reçoit un camion entier de pièces, elle va aussi vite que ses machines. La limite est la puissance de calcul du GPU, et l'art est de bien l'occuper.
- **L'écriture de la réponse** (« écriture »). Le modèle écrit un mot à la fois, et pour chaque mot il doit relire **tous** ses poids dans la mémoire. C'est comme relire tout un dictionnaire pour écrire chaque mot : on va aussi vite qu'on tourne les pages. La limite est la vitesse de la mémoire.

Le plafond de la mémoire se calcule. La sonde Metal du portage a mesuré que le GPU du Mac mini lit au plus **254 à 260 Go/s**. Qwen3-4B en Q4_K_M pèse **2,50 Go** : il ne peut donc pas écrire plus d'environ **100 à 104 mots par seconde**, quel que soit le moteur.

| Qwen3-4B, après 512 tokens | Écriture | Part du plafond de la mémoire |
|---|---|---|
| MLX (fichier de 2,26 Go, plafond environ 112 à 115) | 106 mots/s | environ 93 % |
| llama.cpp (2,50 Go) | 92 mots/s | environ 89 % |
| Gulliver (2,50 Go) | 71 mots/s | environ 69 % |

Les deux autres moteurs tournent donc presque aussi vite que la mémoire le permet. **Gulliver laisse environ 30 % de côté** : c'est la marge à gagner à l'écriture.

## Les mesures de vitesse

Jetons par seconde, deux essais en alternance (moyenne). Gulliver avec le réglage du service (mémoire de conversation en 8 bits), llama.cpp avec ses réglages par défaut, tous deux sur le GPU, comme MLX ; l'Albireo d'origine sur le processeur. Entre parenthèses, l'écart à Gulliver : **+** quand le moteur est plus rapide que Gulliver, **−** quand il est plus lent.

### Le modèle du service, Qwen3-VL 4B Thinking (Q4_K_M)

| Question | Lecture, Gulliver | llama.cpp | MLX | Albireo d'origine |
|---|---|---|---|---|
| 512 tokens | 2 153 | 2 590 (+20 %) | 2 930 (+36 %) | 19,2 (−99 %) |
| 4 096 tokens | 1 461 | 2 063 (+41 %) | 3 055 (+109 %) | 16,1 (−99 %) |
| 16 384 tokens (environ 40 pages) | 535 | 1 209 (+126 %) | 2 074 (+288 %) | non mesuré |

| Question | Écriture, Gulliver | llama.cpp | MLX | Albireo d'origine |
|---|---|---|---|---|
| 512 tokens | 70,7 | 92,2 (+30 %) | 106 (+50 %) | 21,4 (−70 %) |
| 4 096 tokens | 58,6 | 78,1 (+33 %) | 88,4 (+51 %) | 12,1 (−79 %) |
| 16 384 tokens | 32,0 | 50,7 (+58 %) | 56,4 (+76 %) | non mesuré |

### Les autres modèles, en résumé

| Modèle | Lecture : écart de llama.cpp / de MLX | Écriture : écart de llama.cpp / de MLX |
|---|---|---|
| Qwen3-4B Q4_K_M | +19 à +128 % / +32 à +287 % | +30 à +58 % / +50 à +76 % |
| Qwen3-VL 2B Q4_K_M | +22 à +116 % / +45 à +281 % | +33 à +62 % / +56 à +79 % |
| Gemma 3 4B Q4_K_M | +36 à +127 % / +40 à +173 % | +29 à +39 % / +50 à +59 % |
| LFM2.5-1.2B Q8_0 | +58 à +97 % / +73 à +201 % | +8 à +37 % / +13 à +18 % |
| Qwen3-30B-A3B Q4_K_M (mélange d'experts) | +133 à +174 % / +171 à +460 % | +66 à +84 % / +85 à +129 % |

Chaque plage va de la question la plus courte (512 tokens) à la plus longue (16 384), ou du plus petit écart au plus grand. Le mélange d'experts est le point le plus faible de Gulliver.

### Les réglages essayés

- **Mémoire de conversation (« clés et valeurs ») en 8 bits ou en demi-précision** : pour Gulliver, presque aucune différence sur le GPU ; la demi-précision va de 0 à 5 % plus vite. Pour llama.cpp, les 8 bits écrivent un peu moins vite sur les questions courtes et un peu plus vite sur les longues.
- **Un piège trouvé** : sur le mélange d'experts (Qwen3-30B), Gulliver en demi-précision lit **74 à 84 % moins vite** qu'en 8 bits. Son contrôle du GPU, qui compare chaque passe à celle du processeur avant de lui confier le travail, trouve des écarts de 1,2 à 4 % et renvoie tout le calcul au processeur. En 8 bits, le réglage du service, le contrôle passe. À regarder : le seuil du contrôle, ou l'origine de ces écarts.

## D'où viennent les écarts

### La lecture : l'attention, et le calcul lui-même

L'écart de lecture grandit avec la longueur de la question : de +20 % à 512 tokens à +126 % (llama.cpp) et +288 % (MLX) à 16 384, sur le modèle du service. C'est la signature de l'**attention**, l'étape où chaque mot de la question regarde tous les mots qui le précèdent. Son coût grandit comme le carré de la longueur : deux fois plus de mots, quatre fois plus de travail. Elle finit par dominer tout le reste.

- Les trois moteurs font ce calcul **d'un seul tenant** (« attention fusionnée »), avec des entrées en demi-précision et des sommes en float32. Une première version de ce document disait l'attention de Gulliver « en float32 » : c'était faux, son noyau `attention_mtile` (Gulliver 4.11.0) charge déjà ses entrées en demi-précision.
- L'écart tient à **l'organisation du noyau** (lecture du code, à confirmer par la mesure) : Gulliver avance par pas de 32 positions avec 4 à 5 attentes entre les fils du GPU à chaque pas ; ses scores font un aller-retour par la mémoire partagée pour le softmax, ligne par ligne ; et ses tuiles sont petites (8 mots de la question pour Qwen3-4B), si bien que la mémoire de conversation est relue des milliers de fois. MLX garde ses scores dans les registres, la mémoire la plus rapide du GPU. C'est comme un cuisinier qui reposerait chaque ingrédient dans le placard entre deux gestes, quand l'autre garde tout sur le plan de travail.

Même sur les questions courtes, MLX garde +32 à +73 % d'avance. **La lecture de son code (MLX 0.32.4) en donne la raison principale** : sur un M5 avec macOS 26.2 ou plus, il fait ses calculs de matrices sur les **accélérateurs neuronaux** de la puce (« NAX » dans son code : des circuits de calcul de matrices dans chaque cœur du GPU, accessibles par la bibliothèque `MetalPerformancePrimitives` de Metal 4). Il s'en sert pour les produits de matrices, les poids compressés en 4 bits, les mélanges d'experts, l'attention et la couche delta de Qwen 3.5. Gulliver n'utilise pas encore ces circuits : c'est comme avoir un robot ménager dans le placard et tout faire au fouet. C'est la piste V5, désormais la première à essayer.

### L'écriture : 30 % sous le plafond de la mémoire

À l'écriture, Gulliver va à environ 69 % du plafond de la mémoire, llama.cpp à 89 %, MLX à 93 %. La sonde Metal a montré que le noyau qui lit les poids Q4_K atteint seul 84 à 86 % du plafond : la perte n'est donc pas surtout dans la lecture des poids, mais **autour** d'elle. Pistes, **non vérifiées** à ce jour :

- le temps de chaque mot ne se résume pas aux poids : normes, attention, choix du mot, et le dialogue entre le processeur et le GPU à chaque mot (un mot en prend environ 14 ms) ;
- l'attention de l'écriture relit la mémoire de conversation en 8 bits et doit la « déballer », d'où l'écart qui grandit avec la longueur ;
- llama.cpp et MLX enchaînent leurs noyaux en une seule commande au GPU par mot, avec moins d'attentes.

Piste : mesurer une étape d'écriture noyau par noyau (capture Metal), pour savoir où passent ces 30 %, avant de changer quoi que ce soit (V2).

### Le mélange d'experts : les produits groupés

Sur Qwen3-30B-A3B, l'écart est le plus grand (lecture +133 à +460 %, écriture +66 à +129 %). Dans un mélange d'experts, chaque mot n'active que 8 experts sur 128 : il faut trier les mots par expert et faire beaucoup de petits produits. llama.cpp et MLX ont des noyaux mûrs pour ça ; le noyau `mm_id` de Gulliver est passé en demi-précision en 6.8.0, mais reste loin derrière (V3).

### L'Albireo d'origine : le même code, sans accélération

L'original compile tel quel sur Mac, mais il n'a pour accélérateurs que CUDA et OpenVINO, absents ici : il tourne sur le processeur seul. Ses calculs rapides sont écrits pour les processeurs Intel et AMD (AVX2) ; sur la puce Apple, il prend sa version « de secours », sans instructions spéciales. D'où ses 16 à 48 jetons/s en lecture. **Ce n'est pas l'original sur un bon PC Windows**, qui irait bien plus vite : c'est l'original sur ce Mac, et donc la mesure de ce que le portage a apporté ici. Il ne lit pas les familles que seul le fork a ajoutées (LFM2, Gemma 3), et ses mesures s'arrêtent à 4 096 tokens : au-delà, une seule prendrait plus d'un quart d'heure.

### MLX : un autre fichier, et un autre moteur

MLX ne lit pas les fichiers GGUF : il a son propre format, et chaque modèle a sa version MLX (4 bits, ou 8 bits pour LFM2). Il est rapide à la fois parce que ses noyaux sont très bien écrits pour la puce Apple, et parce que ses fichiers sont un peu plus petits (2,26 Go contre 2,50 pour Qwen3-4B). Une mise en garde : jusqu'au matin du 21 novembre, nos mesures utilisaient MLX 0.29.3 (le Python 3.9 du Mac ne permettait pas mieux), qui lisait 3,3 fois moins vite ; MLX 0.32.4 tourne sur un Python récent de Homebrew.

## La justesse

Aller vite ne sert à rien si le calcul est faux. On mesure la **perplexité** : à quel point le modèle est « surpris » par un vrai texte, ici dix épisodes du blog du portage, en français, en 8 morceaux de 512 tokens, comme le fait l'outil `llama-perplexity` de llama.cpp (seule la seconde moitié de chaque morceau est notée). Plus elle est basse, mieux le modèle prévoit le texte. **Deux moteurs justes donnent la même perplexité sur le même fichier.**

| Modèle | Gulliver, GPU | Gulliver, processeur | llama.cpp, GPU | Albireo d'origine | MLX (son fichier) |
|---|---|---|---|---|---|
| Qwen3-4B Q4_K_M | 17,705 | 17,703 (−0,01 %) | 17,709 (+0,02 %) | 17,713 (+0,05 %) | 17,172 (−3,01 %) |
| Qwen3-4B Q8_0 (référence) | 16,515 | 16,513 (−0,01 %) | 16,510 (−0,03 %) | 16,506 (−0,05 %) | — |
| Qwen3-VL 4B Thinking (service) | 15,365 | 15,362 (−0,01 %) | 15,379 (+0,10 %) | 15,357 (−0,05 %) | 16,778 (+9,20 %) |
| Qwen3-VL 2B | 18,671 | 18,671 (±0,00 %) | 18,666 (−0,03 %) | 18,674 (+0,02 %) | 19,754 (+5,80 %) |
| Gemma 3 4B | 21,912 | 21,887 (−0,11 %) | 21,836 (−0,35 %) | ne lit pas ce modèle | 22,706 (+3,62 %) |
| LFM2.5-1.2B Q8_0 | 22,414 | 22,441 (+0,12 %) | 22,428 (+0,06 %) | ne lit pas ce modèle | 22,449 (+0,15 %) |
| Qwen3-30B-A3B (mélange d'experts) | 10,938 | 10,902 (−0,34 %) | 10,926 (−0,12 %) | 10,963 (+0,23 %) | 11,549 (+5,58 %) |

- **Gulliver, llama.cpp et l'Albireo d'origine calculent la même chose** : sur un même fichier, leurs écarts (0,35 % au plus) sont au moins vingt fois plus petits que l'incertitude de la mesure (environ ±1, ±0,7 pour le mélange d'experts).
- **Les fichiers de MLX ne sont pas toujours meilleurs.** Sur Qwen3-4B, le sien prévoit mieux le texte que le Q4_K_M (17,17 contre 17,71, la référence 8 bits étant à 16,51) ; sur les autres, il fait moins bien, jusqu'à +9 % sur le modèle du service. Une partie de la vitesse de MLX se paie donc en justesse sur ces modèles-là. Une correction de ce document : sa première version, écrite avec le seul Qwen3-4B, disait le format de MLX « à la fois plus petit et plus juste ». Ce n'est vrai que pour ce modèle.

## Depuis ces mesures

- **Gulliver 6.17.0** garde les poids développés en demi-précision d'une passe de lecture à l'autre, au lieu de les redévelopper à chaque passe : la lecture d'une question de 512 tokens passe de 2 060-2 159 à **2 593-2 708 jetons/s** sur le modèle du service, **devant llama.cpp** (2 590), encore derrière MLX (2 930). À 4 096 tokens, le gain est de 1 % : l'attention domine (V1).
- **Gulliver 6.18.0** fait l'attention de la lecture sur les accélérateurs neuronaux du M5 (Metal 4, organisation reprise de MLX) : sur le modèle du service, 4 096 tokens passent de 1 467 à **2 100-2 128 jetons/s** (devant llama.cpp, 2 063), 16 384 tokens de 524 à **984-1 021** (llama.cpp 1 209, MLX 2 074). Justesse identique.
- Une sonde a montré que les produits de matrices de Gulliver (par MPS) passent **déjà** par les accélérateurs neuronaux du M5 (31,6 Tflop/s) : l'avance de MLX en lecture tient donc surtout à son attention et à ce qui entoure les produits, pas aux produits eux-mêmes.

## Pour que Gulliver soit le plus rapide

Le chantier « Vitesse » de la feuille de route, dans l'ordre du gain attendu (deux pistes au même but sont essayées toutes les deux, mesurées, et la meilleure est gardée) :

1. **V1, l'attention des questions en demi-précision, d'un seul tenant, sur le GPU** : les longues questions, où l'écart est le plus grand.
2. **V2, l'étape d'écriture mesurée noyau par noyau**, pour aller chercher les 30 % qui manquent sous le plafond de la mémoire.
3. **V3, les mélanges d'experts**, et le contrôle du GPU qui les renvoie au processeur en demi-précision.
4. **V4, un format de poids plus compact** : à juger sur la vitesse et sur la perplexité, modèle par modèle, puisque celui de MLX n'est pas toujours meilleur.
5. **V5 ou V6, pour la lecture** : les accélérateurs neuronaux du M5 par Metal 4, ou le Neural Engine.
6. **V7 ou V8, pour l'écriture** : le décodage spéculatif (un petit modèle propose plusieurs mots, le gros les vérifie d'un coup) ou la devinette par répétition ; tous deux gardent la sortie identique.
7. **V9, une seule commande au GPU par mot**, et **V10, lire mistral.rs**, en plus de MLX et llama.cpp.

Chaque changement sera mesuré en alternance contre la version d'avant, et sa justesse vérifiée par la perplexité, avant d'être gardé.

## Pour les curieux : refaire les mesures

| Mesure | Commande |
|---|---|
| Vitesse | `bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh 2` |
| Justesse | `bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh` |
| Les deux, comme le dimanche | `bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh` |
| Un seul modèle | `BENCH_ONLY="Gemma" bash …/bench_moteurs.sh 1` |
| L'Albireo d'origine | `bash macos/albireo-original/build.sh` (copie figée sur `macos/albireo-original/COMMIT`, dans `../albireo-original`) |

- Gulliver : `tests/bench_runtime` (vitesse) et `tests/perplexity_runtime` (justesse), du build `build/macos-gulliver`.
- llama.cpp : `llama-bench` et `llama-perplexity` de Homebrew, avec `-ngl 99` (tout sur le GPU).
- MLX : `mlx_lm.benchmark` et `tools/perplexity_mlx.py`, dans l'environnement `build/mlx-venv` (Python 3.13 de Homebrew).
- L'Albireo d'origine : son propre `tests/bench_runtime.cpp`, compilé tel quel, et `macos/albireo-original/perplexity_albireo.cpp`.
- Les modèles de comparaison (Qwen3-4B en Q4_K_M, en Q8_0, et les versions MLX des six modèles) sont dans `build/bench-moteurs/models/`, téléchargés depuis Hugging Face (`Qwen/Qwen3-4B-GGUF`, empreintes SHA-256 vérifiées ; `mlx-community`).
- Charge du Mac : 1,2 au début des mesures de vitesse, 9,5 à la fin, pendant les mesures de l'original sur le processeur ; les essais alternés restent proches les uns des autres.
- Mesure précédente, avant Gulliver 6.7.0 : « Gulliver contre llama.cpp » (20 novembre), dans la même rubrique.
