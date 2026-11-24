# Tests de vitesse : Gulliver contre les autres moteurs

Ce document compare Gulliver à trois autres moteurs, avec les mêmes modèles :

- **llama.cpp**, le moteur de référence des fichiers GGUF ;
- **MLX**, le moteur d'Apple ;
- **l'Albireo d'origine**, le programme Windows dont Gulliver est issu, construit à part et sans modification.

Il mesure la vitesse, puis la justesse, et explique d'où viennent les écarts. Le but est que Gulliver devienne le plus rapide sans rien perdre en justesse (priorité du projet depuis le 21 novembre 2026, chantier « Vitesse » de la feuille de route).

- **Mesures de ce document** : sauf la partie « Aujourd'hui », 21 novembre 2026, à partir de 11 h 30, Mac mini M5 Pro (64 Go), Gulliver 6.16.0, llama.cpp 1.0.0 (Homebrew), MLX 0.32.4, Albireo d'origine 1.0.27 (`af5c811`).
- **Comme un utilisateur** (depuis Gulliver 6.30.0) : chaque lundi, le modèle de référence est aussi mesuré par le vrai service, sur un service d'essai isolé et sur llama-server : chargement, conversation de 32k (avec et sans brouillons), 4 requêtes à la fois, 3 documents de 4 000 tokens à la fois, plus grand contexte. Premier essai (Qwen3-VL 4B Thinking, 22 novembre) : à égalité avec llama.cpp sur la conversation longue et à 4 requêtes, mais l'attente du premier mot avec 3 longs documents à la fois est deux fois plus longue (6,8 s contre 3,2) : Gulliver lit les questions l'une après l'autre.
- **La recherche dans les documents** (depuis le 22 novembre) : chaque lundi aussi, `tools/test_rag.py` (40 questions figées sur les épisodes du blog) donne la part des questions dont le bon passage est retrouvé, par moteur ; premier relevé : 95 % dans les 12 premiers passages avec le réglage par défaut.
- **Chaque semaine**, le lundi : les modèles sont mis à jour la nuit (1 h, nouveautés prévenues par mail), puis le Mac mesure lui-même tous les modèles de Gulliver, avec llama.cpp et MLX sur le modèle de référence (8 h, jusqu'au bout) ; la mesure s'ajoute, datée et la plus récente en tête, à la page « Mesures de la semaine », juste à côté dans le carnet, et réécrit la section « La mesure de la semaine » ci-dessous (mode d'emploi : `macos/bench-hebdo/README.md`).

> **Depuis le 22 novembre 2026** (décisions de l'humain), la mesure de la semaine (le lundi) mesure Gulliver sur **tous ses modèles installés**, et llama.cpp et MLX sur le modèle de référence comme deux lignes de plus, comparés au modèle choisi la semaine précédente, pour conseiller chaque semaine le meilleur (onglet Modèles, bouton « Choisir »). L'Albireo d'origine n'est plus mesuré. Les comparaisons plus bas restent valables à leur date ; celles sur six modèles fixes se refont à la main (`tools/bench_moteurs.sh`, `tools/justesse_moteurs.sh`).

<!-- semaine : début -->

## La mesure de la semaine (24/11/2026 à 11 h 04)

Écrite par le Mac à chaque mesure de la semaine, le lundi (`tools/bench_modeles.sh`) : Gulliver seul, sur chacun de ses modèles installés ; jetons par seconde, mémoire de conversation en 8 bits. Charge de la machine pendant la mesure (239 relevés, toutes les 20 secondes) : température de la puce 88,6 °C en moyenne (de 68,0 à 108,0) ; utilisation du processeur 42 % en moyenne (de 12 à 100) ; utilisation du gpu 64 % en moyenne (de 0 à 100).

| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Perplexité | Épreuve |
|---|---|---|---|---|---|---|---|
| Qwen2.5 Coder 1.5B Q4_K_M | GPU | 6 325 | 7 071 | 184 | 152 | 21,92 | 43/100 |
| Qwen3-VL 2B Instruct Q4_K_M | GPU | 6 063 | 6 259 | 181 | 142 | 18,65 | 67/100 |
| Qwen3-VL 2B Thinking Q4_K_M | GPU | 6 007 | 6 250 | 181 | 142 | 20,32 | 83/100 |
| Qwen2.5-0.5B-Instruct Q4_K_M | processeur | 1 480 | 1 243 | 134 | 121 | 25,79 | 37/100 |
| Ministral 3 3B Q4_K_M | GPU | 3 488 | 3 053 | 110 | 91,5 | 12,51 | 73/100 |
| Granite 4.2 3B Q4_K_M | GPU | 3 412 | 3 131 | 102 | 68,5 | 17,85 | 97/100 |
| Qwen3-VL 30B A3B Thinking Q4_K_M (MoE) (choix précédent) | GPU | 1 691 | 1 943 | 93,8 | 78,8 | 10,93 | 100/100 |
| LFM2.5-24B-A2B Q8_0 (MoE) | GPU | 1 086 | 1 196 | 91,9 | 88,2 | 19,57 | 100/100 |
| Qwen3-VL 4B Instruct Q4_K_M | GPU | 2 897 | 2 493 | 91,8 | 75,0 | 14,83 | 83/100 |
| Qwen3-VL 4B Thinking Q4_K_M | GPU | 2 886 | 2 432 | 91,7 | 74,7 | 15,37 | 100/100 |
| CyberSecQwen 4B Q4_K_M | GPU | 2 967 | 2 750 | 91,5 | 73,8 | 13,67 | 87/100 |
| Qwen3-4B-Instruct-2507 Q4_K_M | GPU | 2 853 | 2 374 | 91,3 | 74,2 | 14,89 | 90/100 |
| Phi-4-mini Q4_K_M | GPU | 2 965 | 3 016 | 86,0 | 67,1 | 20,52 | 70/100 |
| gemma-4-E2B-it Q4_K_M | GPU | 4 655 | 2 015 | 77,4 | 67,5 | 69,69 | 93/100 |
| Qwen3.5-4B Q4_K_M | GPU | 1 442 | 1 501 | 72,9 | 71,4 | 11,50 | 100/100 |
| gemma-4-E4B-it Q4_K_M | GPU | 2 358 | 1 248 | 62,2 | 54,6 | 36,61 | 97/100 |
| Qwen3.6-35B-A3B-MTP Q4_K_M (MoE) | GPU | 846 | 865 | 59,3 | 56,3 | 8,66 | 97/100 |
| Qwen2.5 Coder 7B Q4_K_M | GPU | 1 628 | 1 638 | 58,5 | 50,1 | 16,45 | 70/100 |
| Foundation-Sec 1.1 8B Q4_K_M | GPU | 1 588 | 1 476 | 54,2 | 48,6 | 18,57 | 57/100 |
| DeepSeek-R1 Distill Llama 8B Q4_K_M | GPU | 1 440 | 1 490 | 54,0 | 48,0 | 30,01 | 57/100 |
| Foundation-Sec 8B Reasoning Q4_K_M | GPU | 1 543 | 1 461 | 54,0 | 48,7 | 13,28 | 63/100 |
| Qwen3-VL 8B Thinking Q4_K_M | GPU | 1 473 | 1 416 | 53,8 | 46,7 | 12,87 | 100/100 |
| Qwen3-VL 8B Instruct Q4_K_M | GPU | 1 487 | 1 408 | 52,8 | 47,2 | 12,14 | 93/100 |
| Ministral 3 8B Q4_K_M | GPU | 1 250 | 1 347 | 51,7 | 46,3 | 10,00 | 80/100 |
| Granite 4.2 8B Q4_K_M | GPU | 1 341 | 1 299 | 49,0 | 43,3 | 19,55 | 97/100 |
| EuroVLM 9B Preview Q4_K_M | GPU | 1 291 | 1 293 | 46,6 | 41,2 | 13,33 | 67/100 |
| EuroLLM 9B Instruct Q4_K_M | GPU | 1 147 | 1 258 | 45,1 | 40,4 | 13,54 | 63/100 |
| MiMo-V2.6-Distill-Qwen-9B Q4_K_M | GPU | 917 | 1 029 | 45,0 | 44,3 | 8,94 | 90/100 |
| Qwen3.5-9B Q4_K_M | GPU | 846 | 986 | 44,5 | 44,1 | 9,84 | 93/100 |
| gemma-4-26B-A4B-it Q4_K_M | processeur | 138 | 167 | 25,4 | 23,4 | 2828,56 | 100/100 |
| gemma-4-12b-it Q4_K_M | processeur | 51,9 | 74,7 | 21,0 | 19,6 | 411,86 | 100/100 |
| Mistral Small 3.2 24B Q4_K_M | GPU | 470 | 367 | 19,6 | 17,3 | 8,60 | 97/100 |
| Devstral Small 2 Q4_K_M | GPU | 366 | 338 | 19,1 | 18,0 | 8,87 | 90/100 |
| Qwen3.8-27B Q4_K_M | GPU | — | — | — | — | — | 0/100 |

Conseillé : pour tout faire, Qwen3-VL 30B A3B Thinking Q4_K_M (MoE) ; le plus rapide, Ministral 3 3B Q4_K_M. Les tableaux complets, avec llama.cpp et MLX, sont dans « Mesures de la semaine ».

Comme un utilisateur, par le service, sur Qwen3-VL 30B A3B Thinking Q4_K_M (MoE) :

| Mesure | Gulliver |
|---|---|
| Chargement du modèle (jusqu'à une première réponse) | 8,6 s |
| Conversation de 32k : lecture | 858 jetons/s |
| Conversation de 32k : écriture, avec brouillons | 33,8 jetons/s |
| Conversation de 32k : écriture, sans brouillons | 37,6 jetons/s |
| 4 requêtes à la fois : écriture totale | 142 jetons/s |
| 3 documents de 4 000 tokens à la fois : temps total | 13,2 s |
| 3 documents à la fois : attente moyenne du premier mot | 3,8 s |
| Le plus grand contexte qui tient (essayés : 65 536 et 131 072) | 131 072 tokens |

La recherche dans les documents (RAG) : 40 questions figées sur les 63 épisodes du blog, cherchées par un service d'essai isolé (qwen3-embedding-0.6b, épisodes indexés en 12 s) ; une question est trouvée quand le passage qui y répond est rendu. Le meilleur réglage : moteur « Sens seul », tri fin allumé, 12 passages.

| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |
|---|---|---|---|---|
| Hybride (mots et sens) | 88 % | 95 % | 98 % | 0,71 |
| Hybride (mots et sens) + tri fin | 78 % | 95 % | 98 % | 0,65 |
| Sens seul | 88 % | 92 % | 95 % | 0,67 |
| Sens seul + tri fin (le meilleur) | 95 % | 95 % | 95 % | 0,72 |
| Mots seuls | 82 % | 90 % | 92 % | 0,61 |
| Mots seuls + tri fin | 82 % | 88 % | 92 % | 0,66 |

<!-- semaine : fin -->

## Aujourd'hui (22 novembre, Gulliver 6.22.0)

Mesure du dimanche 22 novembre à 6 h 58 (page « Mesures de la semaine »), après une journée de chantier. Écart à Gulliver : **+** quand l'autre moteur est plus rapide, **−** quand il est plus lent. L'écriture est mesurée sans brouillon, pour les trois moteurs.

| Modèle du service (Qwen3-VL 4B) | Gulliver | llama.cpp | MLX |
|---|---|---|---|
| Lecture, 512 tokens | 2 679 | 2 579 (−4 %) | 2 781 (+4 %) |
| Lecture, 4 096 tokens | 2 258 | 2 057 (−9 %) | 3 099 (+37 %) |
| Lecture, 16 384 tokens | 1 256 | 1 205 (−4 %) | 2 076 (+65 %) |
| Écriture après 512 | 82,0 | 92,0 (+12 %) | 106 (+29 %) |
| Écriture après 16 384 | 47,4 | 50,2 (+6 %) | 55,9 (+18 %) |

- **Lecture : Gulliver passe devant llama.cpp** sur presque tous les modèles et toutes les longueurs (de 1 à 9 % devant), sauf les questions courtes du 2B et de LFM2 (12 et 16 % derrière) et le mélange d'experts Qwen3-30B-A3B (69 % derrière à 512 tokens, 4 à 9 % au-delà). Hier matin, il était derrière partout, jusqu'à 2,3 fois. MLX reste devant, surtout sur les longues questions (+23 à +116 %).
- **Écriture : Gulliver reste derrière llama.cpp**, de 6 à 18 % en général (24 à 36 % sur le 30B à 512 et 4 096 tokens, 47 % sur Gemma 3 après 16 384), fait jeu égal sur LFM2 et passe devant sur le 2B après 16 384 tokens (5 %). Hier matin, l'écart était de 30 à 70 %. Les brouillons (6.22.0) ne comptent pas dans ces tableaux : sur une recopie ou une question sur un document, ils font écrire Gulliver 1,3 à 3 fois plus vite (voir plus bas).
- **Justesse : inchangée.** Sur le même fichier, Gulliver, llama.cpp et l'original restent à 0,35 % au plus les uns des autres.
- **Corrigé en 6.22.1** : dans cette mesure, Qwen3-30B-A3B avec la mémoire de conversation en demi-précision lisait à 120 jetons/s au lieu de 1 136 (−89 %). Le contrôle du GPU au chargement lui appliquait la limite des modèles ordinaires, et une quasi-égalité entre deux experts renvoyait tout au processeur. Il lit désormais à 1 208 jetons/s et écrit à 75, avec la même perplexité (réglage non employé par le service, qui est en 8 bits).
- **La place du Mac compte** : une série de six essais d'affilée (Gemma 3 4B après 16 384 tokens) a vu l'écriture baisser de 69,3 à 59,9 jetons/s, puis revenir à 71,5 après deux minutes de repos. Refaite après que le Mac mini a été surélevé (il aspire son air par-dessous), elle ne baisse plus du tout (74,8 à 75,0), et la puce ne dépasse pas 43 °C. La cause la plus probable est donc la chaleur, sans preuve : la première série n'avait pas de relevé de température. Depuis, chaque campagne note la température de la puce au début et à la fin.
- **Une mesure écartée** : celle de la nuit du 21 au 22, lancée par l'agent en priorité de tâche de fond. macOS l'avait cantonnée aux cœurs lents, et ses chiffres étaient faux (l'Albireo d'origine dix fois plus lent). L'agent tourne désormais en priorité normale.

## Quel modèle employer avec Gulliver (conseil)

D'après la mesure du 22 novembre (Gulliver 6.22.0) et, pour l'écriture, Gulliver 6.23.0 (produits par les poids repris de llama.cpp, +5 à +8 %). Chaque semaine, la page « Mesures de la semaine » refait ce conseil à partir de ses propres chiffres, en mesurant aussi tous les modèles installés dans Gulliver, et l'onglet « Modèles » l'affiche avec un bouton « Choisir » qui applique le modèle et ses réglages (Gulliver 6.25.0).

| Besoin | Modèle conseillé | Pourquoi | Ce qu'il coûte |
|---|---|---|---|
| **Tout faire, images comprises** (le réglage par défaut) | **Qwen3-VL 4B Thinking** (le modèle du service, 2,5 Go) | Voit les images, raisonne, écrit vite : 89 jetons/s après 512 tokens en 6.23.0, lit une question de 4 096 tokens à 2 258 jetons/s | Rien de plus |
| **Les réponses les plus justes, en texte seul** | **Qwen3-30B-A3B Instruct** (18,6 Go) | Perplexité 10,94 contre 15,37 pour le modèle du service, 29 % plus basse sur le même découpage en mots : le modèle le plus juste mesuré. Ses 128 experts n'en font travailler que 8 par mot : il écrit presque aussi vite qu'un 4B (72 à 76 jetons/s) | Plus de mémoire (le fichier entier reste chargé, à l'aise sur 64 Go) ; lit les longues questions 1,5 fois moins vite (1 447 jetons/s à 4 096 tokens) ; ne voit pas les images |
| **Aller vite sur des tâches simples** | **Qwen3-VL 2B Instruct** (1,1 Go) | Deux fois plus rapide que le 4B (164 jetons/s en 6.22.0, 175 en 6.23.0), même famille, voit les images | Se trompe davantage (perplexité 18,67) |
| Le plus rapide de tous | LFM2.5-1.2B (1,3 Go) | 194 jetons/s en écriture | Petit modèle, à réserver aux tâches très simples |

En une phrase : **garder le modèle du service par défaut, et passer à Qwen3-30B-A3B quand la justesse de la réponse compte plus que les images et la lecture de très longs documents.** Le Mac mini a la mémoire qu'il faut pour les deux.

Deux précautions. La perplexité mesure la façon dont un modèle prévoit un texte français (dix épisodes du blog), pas la qualité de ses raisonnements ; elle ne se compare qu'entre modèles qui découpent le texte de la même façon (les Qwen3 entre eux : Gemma 3 et LFM2 ont leur propre découpage, leurs chiffres ne se rangent pas sur la même échelle). Et l'écriture est mesurée sans brouillon : sur une recopie ou une question sur un document, les brouillons la multiplient par 1,3 à 3 pour tous ces modèles.

## En bref, au départ (21 novembre, Gulliver 6.16.0)

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

> **Contre le modèle d'origine (depuis Gulliver 6.31.1)** : `tools/justesse_reference.py` compare Gulliver au modèle publié par ses auteurs sur Hugging Face, calculé en pleine précision (float32) par PyTorch sur le GPU du Mac. Qwen3-VL 4B Thinking, 20 morceaux de 512 tokens du texte de mesure, 22 novembre : origine 13,532 ; Gulliver (Q4_K_M) 13,802 sur le processeur, 13,802 sur le GPU ; llama.cpp (même fichier) 13,811. Gulliver fait le même premier choix que l'origine pour 89,8 % des positions, et découpe le texte exactement comme elle. L'écart de 2 % est donc celui de la compression en 4 bits, que llama.cpp paie autant : le moteur n'y ajoute rien.

> **Correction du 22 novembre 2026** : dans les tableaux de justesse de ce document et des mesures jusqu'au 22 novembre, la colonne « Gulliver, GPU » a en fait été calculée **sur le processeur** : une passe du GPU ne rend au plus que 8 lignes de logits, et l'outil (`perplexity_runtime`), qui en demandait des milliers, retombait sur le processeur sans le dire. Ses chiffres restent justes, mais pour le processeur. Depuis Gulliver 6.29.1, l'outil passe sur le GPU par paquets de 8 tokens et le dit ; le GPU y donne les mêmes perplexités que le processeur à quelques dix-millièmes près (Qwen2.5-Coder 1,5B : 12,5959 contre 12,5970 ; Ministral 3 8B : 5,8183 contre 5,8187).

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
- **Gulliver 6.18.0** fait l'attention de la lecture sur les accélérateurs neuronaux du M5 (Metal 4, organisation reprise de MLX) : sur le modèle du service, 4 096 tokens passent de 1 467 à **2 100-2 128 jetons/s** (devant llama.cpp, 2 063), 16 384 tokens de 524 à **984-1 021** (llama.cpp 1 209, MLX 2 074) ; en 6.18.1, avec 128 lignes de requête par tuile, **2 186-2 280** et **1 093-1 255**, à égalité avec llama.cpp sur les longues questions. Justesse identique.
- **Gulliver 6.19.0** réécrit l'attention de l'écriture (32 positions à la fois, lectures groupées) : sur le modèle du service, l'écriture passe de 70,7 à **78,4 jetons/s** après 512 tokens, de 58,3 à **66** après 4 096, de 28-31 à **41-42** après 16 384 (llama.cpp : 92, 78, 51 ; MLX : 106, 88, 56). Un profil par noyau montre ce qui reste : les produits par les poids, environ 75 % d'un pas.
- Une sonde a montré que les produits de matrices de Gulliver (par MPS) passent **déjà** par les accélérateurs neuronaux du M5 (31,6 Tflop/s) : l'avance de MLX en lecture tient donc surtout à son attention et à ce qui entoure les produits, pas aux produits eux-mêmes.

### Après la sieste du 21 novembre (Gulliver 6.19.2, mesure de 17 h 50)

Mêmes mesures que plus haut, deux essais en alternance, sans l'Albireo d'origine (ses chiffres ne bougent pas). Charge du Mac : 4 à 9 pendant la mesure. Écart à Gulliver entre parenthèses.

| Modèle | Question | Lecture : Gulliver | llama.cpp | MLX | Écriture : Gulliver | llama.cpp | MLX |
|---|---|---|---|---|---|---|---|
| Qwen3-VL 4B Thinking (service) | 512 | **2 744** | 2 590 (−6 %) | 2 901 (+6 %) | 82,1 | 92,1 (+12 %) | 106 (+29 %) |
| Qwen3-VL 4B Thinking (service) | 4 096 | **2 263** | 2 046 (−10 %) | 2 917 (+29 %) | 69,2 | 77,9 (+13 %) | 88,3 (+28 %) |
| Qwen3-VL 4B Thinking (service) | 16 384 | **1 208** | 1 183 (−2 %) | 2 036 (+69 %) | 44,0 | 50,5 (+15 %) | 56,2 (+28 %) |
| Qwen3-VL 2B | 4 096 | **5 457** | 4 896 (−10 %) | 7 572 (+39 %) | 131 | 142 (+9 %) | 168 (+28 %) |
| Qwen3-VL 2B | 16 384 | **3 111** | 2 911 (−6 %) | 5 018 (+61 %) | **84,7** | 81,5 (−4 %) | 90,5 (+7 %) |
| LFM2.5-1.2B Q8_0 | 4 096 | **8 567** | 8 331 (−3 %) | 11 590 (+35 %) | 187 | 188 (+1 %) | 189 (+1 %) |
| Gemma 3 4B | 4 096 | 1 731 | 2 757 (+59 %) | 3 207 (+85 %) | 69,0 | 87,2 (+26 %) | 101 (+46 %) |
| Qwen3-30B-A3B (mélange d'experts) | 4 096 | 870 | 1 564 (+80 %) | 2 769 (+218 %) | 65,7 | 81,3 (+24 %) | 90,3 (+38 %) |

En une phrase : **Gulliver lit désormais plus vite que llama.cpp** sur les modèles Qwen et sur LFM2 aux longues questions, et l'écart d'écriture avec llama.cpp est passé d'environ +30 % à +12 % ; MLX reste devant, et Gulliver reste loin sur Gemma 3 (têtes de 256 valeurs, que les nouveaux noyaux ne prennent pas encore) et sur le mélange d'experts (V3). Tableau complet : page « Mesures de la semaine ».

### Plusieurs mots par passe (Gulliver 6.22.0)

Écrire, c'est une passe par mot, et chaque passe relit tous les poids du modèle : c'est la mémoire qui fixe la cadence, comme un livreur qui ferait l'aller-retour à l'entrepôt pour chaque colis. Si l'on pouvait vérifier plusieurs mots d'une seule passe, le même aller-retour servirait plusieurs fois. Deux changements le permettent :

- **Les passes de plusieurs mots sur les accélérateurs du M5** (`mv_nax`). Jusque-là, une passe de 4 mots coûtait 2,7 fois une passe d'un mot : le GPU faisait une multiplication par poids et par mot sur ses unités ordinaires, et le calcul, plus que la mémoire, fixait le temps. Désormais chaque poids est développé une fois, et les multiplications passent par les unités matricielles. Une passe de 8 mots ne coûte plus que 1,37 fois une passe d'un mot. Plusieurs réponses à la fois en profitent directement : 4 réponses passent de 123 à **239 jetons/s** au total, 8 de 130 à **465**.
- **Les brouillons** (repris de l'Albireo d'origine 1.0.27) : quand les derniers mots écrits figurent déjà dans la conversation, le moteur propose leur suite, et le modèle vérifie jusqu'à 7 mots d'un coup. Sur le modèle du service, en choix glouton :

| Tâche | Sans brouillon | Avec (7 mots) |
|---|---|---|
| Recopier un texte | 78,5 | **245** (×3,1) |
| Répondre à une question sur un document | 76,6 | **96,3** (+26 %) |
| Écrire du code | 79,2 | **85,7** (+9 %) |
| Résumer un document | 76,3 | **82,5** (+8 %) |
| Réponse libre | 79,3 | 80,0 (+1 %) |

Le texte écrit est le même avec et sans brouillon sur quatre tâches sur cinq ; sur la réponse libre, il diverge au 2 335e caractère sur 3 586, à un endroit où deux mots étaient presque à égalité : les sommes d'une passe de plusieurs mots ne se font pas tout à fait dans le même ordre. La perplexité calculée par passes de 8 mots (15,372) est celle d'un mot à la fois (15,368), à l'incertitude près (±0,95).

Les tableaux d'écriture plus haut sont mesurés **sans** brouillon, pour Gulliver comme pour llama.cpp et MLX, qui savent aussi en faire (llama.cpp par `llama-lookup` ou un petit modèle, MLX par un petit modèle) mais pas dans leur réglage par défaut : la comparaison reste à faire à armes égales. Reste aussi à faire passer les mélanges d'experts par le même chemin : Qwen3-30B-A3B gagne 6 % sur une question sur un document mais perd 2,5 % en réponse libre.

### Plusieurs personnes à la fois (Gulliver 6.34.0)

La mesure « comme un utilisateur » (6.30.0) envoie trois documents de 4 000 tokens à la fois : chacun recevait son premier mot après 6 s environ, deux fois plus tard que chez llama.cpp (3,2 s en moyenne). La cause tenait en un réglage de l'original : tant qu'une réponse s'écrit, une passe ne lisait que 64 tokens de prompts, au lieu de 512. C'est comme un guichet qui, dès qu'un client signe, ne traiterait plus que deux lignes du dossier suivant à chaque tour : le dossier traîne, et le client qui signe attend lui aussi. Avec 512 tokens par passe, et le prompt le plus court servi d'abord (Qwen3-VL 4B, GPU, mesures alternées) :

| Situation | Avant | 6.34.0 |
|---|---|---|
| Une discussion écrit pendant qu'un document de 8 000 tokens arrive | 19 jetons/s ; premier mot du document après 11,4 s | **31** jetons/s ; **4,8 à 5,1 s** |
| Trois documents de 4 000 tokens ensemble, premier mot | 6,0 s en moyenne, 6,5 s au pire | **4,2 s** en moyenne (le premier en 2,0 s), 6,3 s au pire |

Llama.cpp reste devant sur la seconde ligne (3,2 s) : il lit chaque document un peu plus vite. Avec les noyaux de la nuit (6.40.0, mesure du 23 novembre à 0 h 15, deux tours), l'attente moyenne descend à **3,8-3,9 s** (1,8-1,9 s pour le premier servi, 5,7-6,1 s pour le dernier). À comparer de nouveau dans la mesure de la semaine.

### Les modèles en Q8_0 sur le GPU (Gulliver 6.34.0)

Depuis la 6.32.0, les modèles denses compressés en Q8_0 (8 bits par poids, plus fidèles que Q4_K mais deux fois plus lourds) calculent sur le GPU. Mesure du 22 novembre au soir (`bench_runtime`, question de 512 tokens, 64 mots écrits, mémoire de conversation en 8 bits ; deux tours en alternance avec llama.cpp, `llama-bench -fa 1 -ctk q8_0 -ctv q8_0` ; charge 4,4 à 6,6) :

| Modèle (Q8_0) | Lecture, Gulliver | Lecture, llama.cpp | Écriture, Gulliver | Écriture, llama.cpp | Gulliver sur le processeur (18 fils) |
|---|---|---|---|---|---|
| Qwen3-4B | 2 755 à 2 896 | 2 750 | 58,5 | 57,7 à 58,7 | lecture 168 à 188, écriture 42 à 46 |
| Ministral 3 3B | 3 359 à 3 364 | 3 198 à 3 243 | 69,5 | 65,3 à 66,4 | lecture 197 à 217, écriture 46 à 52 |

En Q8_0, Gulliver fait jeu égal avec llama.cpp (Qwen3-4B) ou passe devant de 4 à 5 % (Ministral 3 3B), en lecture comme en écriture. L'écart d'écriture qui reste tient donc aux formats en 4 et 6 bits (Q4_K, Q6_K), ceux des modèles du service : c'est là que porte V2.

### L'écriture du modèle du service, réglage contre réglage (Gulliver 6.35.0)

La mesure de la semaine compare Gulliver, mémoire de conversation en 8 bits (le réglage du service), à llama.cpp dans son réglage par défaut, en demi-précision. À réglage égal (22 novembre au soir, `bench_runtime` et `llama-bench -fa 1`, question de 512 tokens, deux tours en alternance) :

| Qwen3-VL 4B Thinking Q4_K_M | Gulliver | llama.cpp |
|---|---|---|
| Lecture, mémoire en 8 bits | 2 748 à 2 900 | 2 523 à 2 529 |
| Écriture, mémoire en 8 bits | 89,7 à 89,8 | 87,9 à 88,2 |
| Lecture, mémoire en demi-précision | 2 860 à 2 893 | 2 569 à 2 595 |
| Écriture, mémoire en demi-précision | 90,8 à 90,9 (90,7 de moyenne en 6.35.0, mesuré à part) | 94,5 à 94,8 |

En 8 bits, Gulliver devance llama.cpp en lecture (+9 à +15 %) et en écriture (+2 %). En demi-précision, il lit plus vite mais écrit encore 4 % moins vite : son GPU passe 10,45 ms par mot, quand llama.cpp boucle tout le mot en 10,57 ms. La 6.35.0 gagne 1 % en envoyant la première couche au GPU sans attendre l'écriture des autres ; quatre autres essais de la nuit, plus lents, ont été écartés (détail dans les notes pour l'auteur, changement 48).

### La lecture, des conversions en moins (Gulliver 6.36.0)

Avant chaque produit, la lecture d'une question convertit son entrée en demi-précision, le format que le GPU multiplie le plus vite. Les requêtes, les clés et les valeurs d'une couche lisent la même entrée, convertie jusqu'ici trois fois ; la porte et la montée, deux fois. C'est comme photocopier trois fois la même page pour trois lecteurs assis à la même table. Une seule conversion sert désormais à tous, et l'activation de la porte s'écrit directement en demi-précision. Modèle du service (trois tours en alternance) : 512 tokens lus à **2 920-3 075** jetons/s au lieu de 2 790-2 853 (+5,5 %), 4 096 tokens à **2 384-2 409** au lieu de 2 341-2 355 (+2,2 %). À 16 384 tokens, rien ne change : l'attention y prend l'essentiel du temps, et c'est là que MLX garde son avance.

### Ce que la nuit du 22 novembre a appris sur l'attention

Sur les longues questions, l'écart avec MLX tient à l'attention. Seule, celle de MLX passe 19,1 ms par couche pour 8 192 tokens (près de 29 Tflop/s), celle de Gulliver environ 52 (10,6 Tflop/s). Les deux noyaux sont bâtis pareil (tuiles de 64 lignes, pas de 32 positions, quatre simdgroups, produits par les accélérateurs). Une différence mesurée : celui de Gulliver manque de registres et en range 272 octets en mémoire, au cœur de sa boucle, celui de MLX 128 (`tools/gpu_debordements.sh`). Six réglages simples ont été essayés et mesurés ; tous allaient moins vite (journal du 22 novembre). La suite demande une refonte du noyau, vérifiée trace à l'appui. Même constat pour le mélange d'experts à 512 tokens : son produit groupé prend 81 % du temps, et trois réglages simples n'y ont rien gagné.

### L'attention des longues questions, à la façon de MLX (Gulliver 6.37.0)

La même nuit, l'attention a été réécrite d'après celle de MLX (licence MIT). À chaque couche, les clés et valeurs de la question sont d'abord rangées à la suite, en demi-précision, comme les fiches d'un classeur remises dans l'ordre avant la lecture. Ensuite, chaque groupe de calcul du GPU les lit directement, en restant au même pas que ses trois voisins, pour que ce qu'un groupe vient de lire serve aussi aux autres. Deux détails de MLX ont fait la différence : dérouler entièrement les petites boucles, et cette halte commune au milieu du calcul. L'attention seule passe de 52 à 21 ms par couche à 8 192 tokens (MLX : 19). Modèle du service, en alternance : 4 096 tokens lus à **2 493-2 614** jetons/s (2 223-2 334 avant), 16 384 tokens à **1 534-1 571** (996-1 148), désormais devant llama.cpp (1 205) ; MLX reste devant (2 076). Justesse inchangée.

En 6.41.0, la même attention couvre les têtes de 256 valeurs, sans fenêtre (Qwen 3.5, couches globales de Gemma 3) : deux groupes de calcul s'y partagent les dimensions, comme dans MLX. Qwen3.5-4B lit 4 096 tokens 5 % plus vite et 16 384 tokens 15 % plus vite (1 391-1 397 jetons/s). En 6.42.0, elle prend aussi les couches à fenêtre glissante : Gemma 3 4B +7 % à 4 096 tokens.

### Le mélange d'experts, même leçon (Gulliver 6.38.0)

Le produit groupé des experts, qui faisait 81 % de la lecture d'une question sur Qwen3-VL 30B-A3B, déroule désormais entièrement ses petites boucles, comme l'attention de MLX. Sans rien changer aux calculs, ce modèle lit 512 tokens à **1 452-1 466** jetons/s au lieu de 1 168-1 194 (+23 %), et 4 096 tokens à **1 708-1 795** au lieu de 1 548-1 639 (+10 %), devant llama.cpp (1 345-1 516). À 512 tokens, llama.cpp reste devant (1 835-1 950). En 6.40.0, le même produit lit ses entrées par morceaux de 16 colonnes au lieu de 64, ce qui laisse tourner plus de calculs à la fois : **1 695-1 701** jetons/s à 512 tokens (+19 % de plus), LFM2.5-8B-A1B +13 %, Qwen3.6-35B-A3B +6 %.

### Le modèle de l'humain, Ministral 3 8B (Gulliver 6.38.0)

Mesure du 22 novembre à 23 h 35, deux tours en alternance avec llama.cpp (`llama-bench -fa 1 -ctk q8_0 -ctv q8_0`), mémoire de conversation en 8 bits, charge 2 à 6 :

| Ministral 3 8B Q4_K_M | Gulliver | llama.cpp |
|---|---|---|
| Lecture, 512 tokens | 1 008 à 1 309 | 1 073 à 1 201 |
| Lecture, 4 096 tokens | **1 217 à 1 325** | 954 à 1 011 |
| Écriture (128 mots) | **49,9 à 50,4** | 47,2 |

Gulliver lit 25 à 30 % plus vite les longues questions et écrit 6 % plus vite ; sur les questions courtes, les deux se valent.

### L'écriture du modèle à experts rattrape llama.cpp (Gulliver 6.43.0 et 6.44.0)

Le 23 novembre au matin, Qwen3-VL 30B-A3B écrivait 8 % moins vite que llama.cpp. Remesuré à format égal de la mémoire de conversation, l'écart n'existait qu'en demi-précision (Gulliver 82, llama.cpp 90) ; en 8 bits, les deux faisaient jeu égal (81 contre 77 à 81). La trace « Metal System Trace » d'un pas a désigné trois noyaux qui attendaient plus qu'ils ne calculaient :

- la descente des experts (768 colonnes) : un groupe de 32 fils avait trop peu à lire ; deux lignes par groupe (6.43.0, +1,6 %) ;
- l'attention de l'écriture : avec quatre têtes de clés et une conversation courte, 16 groupes de calcul seulement pour tout le GPU ; les têtes de requête se partagent entre plusieurs groupes tant qu'il y en a moins de 64 (6.44.0, +3 %) ;
- le routeur des experts : 64 additions à la chaîne par fil pour lire 1 Mo ; quatre valeurs à la fois et quatre sommes en parallèle (6.44.0, +4 %).

| Qwen3-VL 30B-A3B, écriture après 512 tokens (mémoire en 8 bits) | Tokens/s |
|---|---|
| Gulliver 6.42.2 | 82,8 à 85,6 |
| Gulliver 6.44.0 | **90,6 à 91,7** |
| llama.cpp, demi-précision / 8 bits | 89,5 à 92 / 77 à 81 |

Mesures alternées, trois tours, charge 4 à 7 (un autre projet tournait). Les deux changements de la 6.44.0 donnent aussi +3,5 % à Qwen3.6-11B-A3B et +4 % à LFM2.5-8B-A1B. La justesse est inchangée (perplexité token par token du 30B 10,9345 contre 10,9347 ; juge de référence du 4B identique).

### Ce que l'avance de MLX à l'écriture doit au format (23 novembre)

Écrire, c'est relire tous les poids à chaque mot. Sur Qwen3-VL 4B Thinking, les deux fichiers ne pèsent pas pareil :

| Qwen3-VL 4B | Lu par mot | Écriture (tokens/s) | Débit tiré de la mémoire |
|---|---|---|---|
| Gulliver 6.44.0 (GGUF Q4_K_M) | 2,49 Go | 88 | 219 Go/s |
| llama.cpp (même fichier) | 2,49 Go | 92 | 229 Go/s |
| MLX (son fichier 4 bits, groupes de 64) | 2,26 Go | 106 | 240 Go/s |

Le Q4_K_M garde 0,73 Go de poids en 6 bits (la sortie, et la moitié des valeurs et des descentes) ; le fichier de MLX met tout en 4 bits, vision à part. Sur les 20 % d'avance de MLX, environ 10 % tiennent donc au fichier plus léger, qui est aussi moins juste (perplexité 16,78 contre 15,36 à 15,38 pour le GGUF, mesure de la semaine du 22 novembre), et environ 10 % à l'efficacité (240 Go/s contre 219, pour un plafond d'environ 254). C'est cette seconde moitié que le chantier peut gagner sans rien perdre en justesse ; llama.cpp, sur le même fichier, est à mi-chemin.

### Les modèles arrivés le 23 novembre (Gulliver 6.42.0)

L'agent du lundi à 1 h a installé 18 nouveaux modèles du catalogue (empreintes vérifiées). Passés au banc dans la foulée (`bench_runtime`, 512 tokens lus, mémoire de conversation en 8 bits) : **tous calculent sur le GPU**, sauf Qwen2.5-0.5B (déjà connu : des largeurs qui ne sont pas des multiples de 256). Contre llama.cpp, sur cinq d'entre eux (un tour, charge 2 à 8 ; Granite et EuroLLM remesurés sur deux tours en alternance, charge 3 à 4,6) :

| Modèle | Lecture, Gulliver | Lecture, llama.cpp | Écriture, Gulliver | Écriture, llama.cpp |
|---|---|---|---|---|
| Phi-4-mini | 3 251 | 2 794 | 90,9 | 89,8 |
| DeepSeek-R1-Distill-Llama 8B | 1 343 | 1 168 | 53,0 | 53,5 |
| Granite 4.2 8B | 1 222-1 365 | 1 112-1 212 | 48,5-48,8 | 48,0 |
| EuroLLM 9B | 950-1 216 | 980-1 036 | 46,4-46,7 | 46,0-46,3 |
| Mistral Small 3.2 24B | 428-467 (au calme) | 378 | 19,4 | 19,0 |

La mesure de la semaine (lundi 8 h) les reprendra tous, en alternance et au calme.

## Pour que Gulliver soit le plus rapide

Le chantier « Vitesse » de la feuille de route, dans l'ordre du gain attendu (deux pistes au même but sont essayées toutes les deux, mesurées, et la meilleure est gardée) :

1. **V1, l'attention des questions en demi-précision, d'un seul tenant, sur le GPU** : les longues questions, où l'écart est le plus grand. *Fait pour les têtes de 64 et 128 valeurs (6.37.0) ; reste les têtes de 256 et 512 valeurs et les fenêtres glissantes.*
2. **V2, l'étape d'écriture mesurée noyau par noyau**, pour aller chercher les 30 % qui manquent sous le plafond de la mémoire. *Mesurée le 22 novembre : le GPU passe 10,45 ms par mot, les produits par les poids en font 82 % à environ 85 % du débit de la mémoire ; avec la mémoire de conversation en 8 bits, Gulliver écrit déjà plus vite que llama.cpp.*
3. **V3, les mélanges d'experts**, et le contrôle du GPU qui les renvoie au processeur en demi-précision. *Produit groupé +23 % à 512 tokens (6.38.0) ; llama.cpp reste devant sur les questions courtes (1 835-1 950 contre 1 452-1 466).*
4. **V4, un format de poids plus compact** : à juger sur la vitesse et sur la perplexité, modèle par modèle, puisque celui de MLX n'est pas toujours meilleur.
5. **V5 ou V6, pour la lecture** : les accélérateurs neuronaux du M5 par Metal 4, ou le Neural Engine.
6. **V7 ou V8, pour l'écriture** : le décodage spéculatif (un petit modèle propose plusieurs mots, le gros les vérifie d'un coup) ou la devinette par répétition. La devinette (V8) est faite en 6.22.0 (ci-dessus) ; reste le petit modèle (V7), à comparer.
7. **V9, une seule commande au GPU par mot**, et **V10, lire mistral.rs**, en plus de MLX et llama.cpp.

Chaque changement sera mesuré en alternance contre la version d'avant, et sa justesse vérifiée par la perplexité, avant d'être gardé.

## Pour les curieux : refaire les mesures

| Mesure | Commande |
|---|---|
| La mesure de la semaine, tous les modèles de Gulliver | `bash dlib/dlib/external/gulliver/tools/bench_modeles.sh 2` |
| Comme le lundi (attente d'une machine au repos, page datée) | `bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh` |
| Les mises à jour des modèles, sans rien lancer | `python3 dlib/dlib/external/gulliver/tools/modeles_semaine.py build/gulliver-home --essai` |
| Moteurs comparés sur six modèles fixes, vitesse | `bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh 2` |
| Moteurs comparés sur six modèles fixes, justesse | `bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh` |
| Un seul modèle | `BENCH_ONLY="Gemma" bash …/bench_moteurs.sh 1` |

- Gulliver : `tests/bench_runtime` (vitesse) et `tests/perplexity_runtime` (justesse), du build `build/macos-gulliver`.
- llama.cpp : `llama-bench` et `llama-perplexity` de Homebrew, avec `-ngl 99` (tout sur le GPU).
- MLX : `mlx_lm.benchmark` et `tools/perplexity_mlx.py`, dans l'environnement `build/mlx-venv` (Python 3.13 de Homebrew).
- L'Albireo d'origine (mesuré jusqu'au 22 novembre 2026) : son propre `tests/bench_runtime.cpp`, compilé tel quel, et un `perplexity_albireo.cpp` du fork, retirés depuis.
- Les modèles de comparaison (Qwen3-4B en Q4_K_M, en Q8_0, et les versions MLX des six modèles) sont dans `build/bench-moteurs/models/`, téléchargés depuis Hugging Face (`Qwen/Qwen3-4B-GGUF`, empreintes SHA-256 vérifiées ; `mlx-community`).
- Charge du Mac : 1,2 au début des mesures de vitesse, 9,5 à la fin, pendant les mesures de l'original sur le processeur ; les essais alternés restent proches les uns des autres.
- Mesure précédente, avant Gulliver 6.7.0 : « Gulliver contre llama.cpp » (20 novembre), dans la même rubrique.
