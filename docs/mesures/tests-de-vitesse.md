# Tests de vitesse : Gulliver contre les autres moteurs

Ce document compare Gulliver à deux autres moteurs qui tournent sur le Mac, avec les mêmes modèles : **llama.cpp**, le moteur de référence des fichiers GGUF, et **MLX**, le moteur d'Apple. Il mesure la vitesse, puis la justesse, et explique d'où viennent les écarts. Le but est que Gulliver devienne le plus rapide sans rien perdre en justesse.

- **Mesures de ce document** : 21 novembre 2026, Mac mini M5 Pro (64 Go), Gulliver 6.14.2, llama.cpp 1.0.0 (Homebrew), MLX 0.29.3.
- **Chaque dimanche** : les mêmes mesures sont refaites par le Mac lui-même, à 4 h ; elles s'ajoutent, datées et la plus récente en tête, à la page « Mesures du dimanche », juste à côté dans le carnet (mode d'emploi : `macos/bench-hebdo/README.md`).

## En bref

| | Gulliver | llama.cpp | MLX |
|---|---|---|---|
| Lire une question courte (512 tokens) | 2e | **1er**, 1,2 fois plus rapide que Gulliver | 3e, environ 2,4 fois plus lent que Gulliver |
| Lire une longue question (16 384 tokens) | 3e | **1er**, 2,2 fois plus rapide que Gulliver | 2e |
| Écrire la réponse, après une question courte | 3e | 2e, 1,3 fois plus rapide que Gulliver | **1er**, 1,5 fois plus rapide que Gulliver |
| Écrire la réponse, après une longue question | 3e | **1er**, 1,6 fois plus rapide que Gulliver | 2e |
| Justesse, même fichier | identique à llama.cpp | identique à Gulliver | autre fichier, de meilleure qualité |

Aujourd'hui, **Gulliver n'est le plus rapide nulle part**, mais il calcule **aussi juste** que llama.cpp. L'écart tient en deux familles de causes, expliquées plus bas : l'attention des longues questions, et l'écriture d'un mot, qui n'atteint pas le plafond de la mémoire.

## Deux vitesses, deux limites

Un moteur fait deux choses différentes, qui ne butent pas sur la même limite.

- **La lecture de la question** (« lecture », en jetons par seconde). Tous les mots de la question passent ensemble dans le modèle. C'est un travail de calcul pur : comme une usine qui reçoit un camion entier de pièces, elle va aussi vite que ses machines. La limite est la puissance de calcul du GPU, et l'art est de bien l'occuper.
- **L'écriture de la réponse** (« écriture »). Le modèle écrit un mot à la fois, et pour chaque mot il doit relire **tous** ses poids dans la mémoire. C'est comme relire tout un dictionnaire pour écrire chaque mot : on va aussi vite qu'on tourne les pages. La limite est la vitesse de la mémoire.

Le plafond de la mémoire se calcule. La sonde Metal du portage a mesuré que le GPU du Mac mini lit au plus **254 à 260 Go/s**. Qwen3-4B en Q4_K_M pèse **2,50 Go** : il ne peut donc pas écrire plus d'environ **100 à 104 mots par seconde**, quel que soit le moteur.

| Qwen3-4B, après 512 tokens | Écriture | Part du plafond de la mémoire |
|---|---|---|
| MLX (fichier de 2,26 Go, plafond environ 112 à 115) | 106 mots/s | environ 93 % |
| llama.cpp (2,50 Go) | 92 mots/s | environ 89 % |
| Gulliver (2,50 Go) | 70 à 71 mots/s | environ 69 % |

Les deux autres moteurs tournent donc presque aussi vite que la mémoire le permet. **Gulliver laisse environ 30 % de côté** : c'est la marge à gagner à l'écriture.

## Les mesures de vitesse

Sur le GPU, deux essais en alternance, en jetons par seconde : la lecture de la question, puis l'écriture de 64 jetons après elle. Les tableaux complets, réglage par réglage, sont dans `build/bench/` sur le Mac et dans les mesures du dimanche ; ici, le réglage du service pour Gulliver (mémoire de conversation en 8 bits) et les réglages par défaut de llama.cpp.

### Le modèle du service, Qwen3-VL 4B Thinking (Q4_K_M)

| Question | Lecture, Gulliver | Lecture, llama.cpp | Écart | Écriture, Gulliver | Écriture, llama.cpp | Écart |
|---|---|---|---|---|---|---|
| 512 tokens | 2 117 à 2 127 | 2 567 à 2 584 | ×1,2 | 70,4 à 70,8 | 91,6 à 91,7 | ×1,3 |
| 4 096 tokens | 1 429 à 1 434 | 1 937 à 1 940 | ×1,35 | 58,5 | 77,8 à 77,9 | ×1,3 |
| 16 384 tokens (environ 40 pages) | 525 à 527 | 1 148 à 1 154 | ×2,2 | 31,4 à 31,5 | 50,3 à 50,4 | ×1,6 |

### Les autres modèles (question de 512, puis de 16 384 tokens)

| Modèle | Lecture, Gulliver | Lecture, llama.cpp | Écriture, Gulliver | Écriture, llama.cpp |
|---|---|---|---|---|
| Qwen3-4B Q4_K_M, 512 | 2 110 à 2 117 | 2 622 à 2 632 | 70,4 à 70,7 | 91,9 à 92,0 |
| Qwen3-4B Q4_K_M, 16 384 | 516 à 527 | 1 128 à 1 152 | 31,1 à 31,6 | 49,0 à 50,2 |
| Qwen3-VL 2B Q4_K_M, 512 | 4 706 à 5 044 | 5 942 à 5 949 | 132 à 134 | 183 |
| Qwen3-VL 2B Q4_K_M, 16 384 | 1 345 à 1 346 | 2 920 à 2 921 | 50,6 | 81,4 à 81,5 |
| Gemma 3 4B Q4_K_M, 512 | 2 173 à 2 263 | 2 982 à 2 988 | 70,3 à 70,6 | 91,4 à 91,5 |
| Gemma 3 4B Q4_K_M, 16 384 | 1 060 à 1 098 | 2 465 à 2 550 | 57,7 à 58,3 | 80,3 à 80,7 |
| LFM2.5-1.2B Q8_0, 512 | 5 437 à 5 642 | 8 474 à 8 982 | 179 à 182 | 194 à 198 |
| LFM2.5-1.2B Q8_0, 16 384 | 3 239 à 3 315 | 6 172 à 6 616 | 122 à 124 | 156 à 166 |
| Qwen3-30B-A3B Q4_K_M (mélange d'experts), 512 | 783 à 795 | 1 950 à 1 977 | 56,7 à 57,6 | 96,6 à 98,0 |
| Qwen3-30B-A3B Q4_K_M, 16 384 | 319 à 331 | 882 à 900 | 27,0 à 27,1 | 48,4 à 49,5 |

### MLX, sur Qwen3-4B (son propre fichier 4 bits)

| Question | Lecture | Écriture |
|---|---|---|
| 512 tokens | 869 | 106 |
| 4 096 tokens | 753 à 809 | 80,5 à 80,9 |
| 16 384 tokens | 576 | 43,5 à 43,6 |

Ces chiffres sont ceux de MLX 0.29.3, la dernière version que permettait le Python 3.9 du Mac. Avec un Python récent (Homebrew, le même jour), **MLX 0.32.4 lit une question de 512 tokens en 2 880 jetons/s au lieu de 869** (une mesure, machine encore chargée), plus vite que llama.cpp et que Gulliver, et écrit toujours environ 103 mots/s : l'ancienne version faussait la comparaison de la lecture. Les mesures du dimanche utilisent désormais la nouvelle version.

### Les réglages essayés

- **Mémoire de conversation (« clés et valeurs ») en 8 bits ou en demi-précision** : pour Gulliver, presque aucune différence sur le GPU ; la demi-précision écrit de 1 à 5 % plus vite. Pour llama.cpp, les 8 bits écrivent un peu moins vite sur les questions courtes (85 contre 92) et un peu plus vite sur les longues (53,8 contre 50,4).
- **Un piège trouvé** : sur le mélange d'experts (Qwen3-30B), Gulliver en demi-précision lit **6 fois moins vite** (environ 120 jetons/s contre 790). Son contrôle du GPU, qui compare chaque passe à celle du processeur avant de lui confier le travail, trouve des écarts de 1,2 à 4 % et renvoie tout le calcul au processeur. En 8 bits, le réglage du service, le contrôle passe. À regarder : le seuil du contrôle, ou l'origine de ces écarts.

## D'où viennent les écarts

### La lecture des longues questions : l'attention

L'écart de lecture grandit avec la longueur de la question : ×1,2 à 512 tokens, ×2,2 à 16 384. C'est la signature de l'**attention**, l'étape où chaque mot de la question regarde tous les mots qui le précèdent. Son coût grandit comme le carré de la longueur : deux fois plus de mots, quatre fois plus de travail. Elle finit par dominer tout le reste.

- **llama.cpp** fait ce calcul en **demi-précision**, d'un seul tenant (« attention fusionnée ») : les scores ne quittent jamais la mémoire rapide du GPU.
- **Gulliver** fait ses produits de matrices en demi-précision depuis la version 6.7.0 (c'est ce qui a fait passer l'écart de ×3 à ×1,2 sur les questions courtes), mais son attention calcule encore en **float32**, des nombres deux fois plus gros. C'est comme déménager les mêmes meubles avec une camionnette deux fois plus petite.

Piste : une attention fusionnée en demi-précision sur le GPU. C'est le plus gros gain possible pour les longues questions.

### L'écriture : 30 % sous le plafond de la mémoire

À l'écriture, Gulliver va à environ 69 % du plafond de la mémoire, llama.cpp à 89 %. La sonde Metal a montré que le noyau qui lit les poids Q4_K atteint seul 84 à 86 % du plafond : la perte n'est donc pas surtout dans la lecture des poids, mais **autour** d'elle. Pistes, **non vérifiées** à ce jour :

- le temps de chaque mot ne se résume pas aux poids : normes, attention, choix du mot, et le dialogue entre le processeur et le GPU à chaque mot (un mot en prend environ 14 ms) ;
- l'attention de l'écriture relit la mémoire de conversation en 8 bits et doit la « déballer », d'où l'écart qui grandit avec la longueur (×1,3 à ×1,6) ;
- llama.cpp et MLX enchaînent leurs noyaux en une seule commande au GPU par mot, avec moins d'attentes.

Piste : mesurer une étape d'écriture noyau par noyau (capture Metal), pour savoir où passent ces 30 %, avant de changer quoi que ce soit.

### Le mélange d'experts : les produits groupés

Sur Qwen3-30B-A3B, l'écart est le plus grand (×2,5 en lecture, ×1,7 en écriture). Dans un mélange d'experts, chaque mot n'active que 8 experts sur 128 : il faut trier les mots par expert et faire beaucoup de petits produits. llama.cpp a des noyaux mûrs pour ça ; le noyau `mm_id` de Gulliver est passé en demi-précision en 6.8.0, mais reste loin derrière.

### MLX : un autre fichier, et un autre pari

MLX ne lit pas les fichiers GGUF : il a son propre format 4 bits. Son fichier est plus petit (2,26 Go contre 2,50) et, on le verra, de meilleure qualité. Il écrit vite parce qu'il a moins à relire et qu'il est proche du plafond ; il lit lentement parce que cette version calcule ses questions moins efficacement que les deux autres.

## La justesse

Aller vite ne sert à rien si le calcul est faux. On mesure la **perplexité** : à quel point le modèle est « surpris » par un vrai texte, ici dix épisodes du blog du portage, en français, en 8 morceaux de 512 tokens, comme le fait l'outil `llama-perplexity` de llama.cpp (seule la seconde moitié de chaque morceau est notée). Plus elle est basse, mieux le modèle prévoit le texte. **Deux moteurs justes donnent la même perplexité sur le même fichier.**

| Modèle | Gulliver, GPU, 8 bits | Gulliver, processeur | llama.cpp, GPU | Écart Gulliver / llama.cpp |
|---|---|---|---|---|
| Qwen3-4B Q4_K_M | 17,705 | 17,703 | 17,709 | 0,02 % |
| Qwen3-4B Q8_0 (référence) | 16,515 | 16,513 | 16,510 | 0,03 % |
| Qwen3-VL 4B Thinking (service) | 15,365 | 15,362 | 15,379 | 0,1 % |
| Qwen3-VL 2B | 18,671 | 18,671 | 18,666 | 0,03 % |
| Gemma 3 4B | 21,912 | 21,887 | 21,836 | 0,2 à 0,35 % |
| LFM2.5-1.2B Q8_0 | 22,414 | 22,441 | 22,428 | 0,06 % |
| Qwen3-30B-A3B (mélange d'experts) | 10,938 | 10,902 | 10,926 | 0,1 à 0,2 % |

L'incertitude de chaque mesure est d'environ ±1 (±0,7 pour le mélange d'experts) : tous les écarts entre moteurs sont **au moins vingt fois plus petits** qu'elle. Gulliver et llama.cpp calculent la même chose. La part des positions où le mot suivant est le premier choix du modèle (« top-1 ») est elle aussi la même, à quelques dixièmes de point.

**Le fichier compte plus que le moteur.** Sur Qwen3-4B :

| Fichier | Taille | Perplexité |
|---|---|---|
| Q8_0 (8 bits, presque sans perte) | 4,28 Go | 16,51 |
| MLX 4 bits | 2,26 Go | 17,16 |
| Q4_K_M (4 bits, le format courant des GGUF) | 2,50 Go | 17,71 |

Le fichier 4 bits de MLX est à la fois plus petit et plus proche de l'original que le Q4_K_M. Pour Gulliver, cela ouvre une piste : une compression meilleure que Q4_K_M aiderait à la fois la vitesse d'écriture (moins à relire) et la justesse.

## Pour que Gulliver soit le plus rapide

Dans l'ordre du gain attendu :

1. **L'attention des questions en demi-précision, fusionnée, sur le GPU** : les longues questions, où l'écart est le plus grand (×2,2).
2. **L'étape d'écriture, mesurée noyau par noyau**, pour aller chercher les 30 % qui manquent sous le plafond de la mémoire (×1,3).
3. **Les produits groupés des mélanges d'experts** (×2,5), et le contrôle du GPU qui les renvoie au processeur en demi-précision.
4. **Un format de poids plus compact et plus juste**, comme celui de MLX.

Chaque changement sera mesuré en alternance contre la version d'avant, et sa justesse vérifiée par la perplexité, avant d'être gardé.

## Pour les curieux : refaire les mesures

| Mesure | Commande |
|---|---|
| Vitesse | `bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh 2` |
| Justesse | `bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh` |
| Les deux, comme le dimanche | `bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh` |
| Un seul modèle | `BENCH_ONLY="Gemma" bash …/bench_moteurs.sh 1` |

- Gulliver : `tests/bench_runtime` (vitesse) et `tests/perplexity_runtime` (justesse), du build `build/macos-gulliver`.
- llama.cpp : `llama-bench` et `llama-perplexity` de Homebrew, avec `-ngl 99` (tout sur le GPU).
- MLX : `mlx_lm.benchmark` et `tools/perplexity_mlx.py`, dans l'environnement `build/mlx-venv`.
- Les modèles de comparaison (Qwen3-4B en Q4_K_M, en Q8_0 et en MLX 4 bits) sont dans `build/bench-moteurs/models/`, téléchargés depuis Hugging Face (`Qwen/Qwen3-4B-GGUF`, empreintes SHA-256 vérifiées ; `mlx-community/Qwen3-4B-4bit`).
- La charge du Mac est montée de 1,3 à 5,6 pendant les mesures de vitesse (une discussion avec le service y a eu lieu) ; les essais alternés restent proches les uns des autres.
- Mesure précédente, avant Gulliver 6.7.0 : « Gulliver contre llama.cpp » (20 novembre), dans la même rubrique.
