# Gulliver contre llama.cpp — Mac mini M5 Pro, 2026-11-20

Point de la feuille de route « comparer Gulliver à d'autres moteurs locaux sur les longues questions ». Cette première mesure ne compare que llama.cpp, déjà installé (Homebrew, version 1.0.0, build 12359, commit `b1b770d90`) ; Ollama et un moteur MLX ne sont pas installés (Ollama ajoute un service permanent au Mac : choix laissé à l'humain).

## Méthode

- Mêmes fichiers GGUF, sur le GPU, deux tours, machine au repos (charge 1,5 au début, 0,3 à la fin).
- **Gulliver 6.6.0** : `bench_runtime <modèle> 18 <N> 64 1 q8 metal`, clés et valeurs en 8 bits (le réglage du service), `BENCH_GPU_CONTEXT=262144`. L'écriture est mesurée après le prompt de N tokens.
- **llama.cpp** : `llama-bench -m <modèle> -p N -n 0` pour la lecture, `-p 0 -n 64 -d N` pour l'écriture après N tokens de contexte ; réglages par défaut (clés et valeurs en demi-précision, attention fusionnée automatique, 6 threads, lots de 512).
- Les deux réglages de clés et valeurs diffèrent (8 bits contre 16) : c'est le choix par défaut de chacun.

## Résultats (tokens par seconde)

| Modèle | Contexte | Lecture, Gulliver | Lecture, llama.cpp | Écriture, Gulliver | Écriture, llama.cpp |
|---|---|---|---|---|---|
| Qwen3-VL 2B Instruct Q4_K_M | 512 | 1 826 à 1 835 | 5 765 à 5 930 | 129 à 132 | 183 à 184 |
| Qwen3-VL 2B Instruct Q4_K_M | 4 096 | 1 712 à 1 750 | 4 786 à 4 918 | 105 à 106 | 126 à 142 |
| Qwen3-VL 2B Instruct Q4_K_M | 16 384 | 982 à 985 | 2 857 à 2 923 | 51 | 74 à 82 |
| Qwen3-VL 4B Thinking Q4_K_M (modèle du service) | 512 | 765 à 767 | 2 494 à 2 564 | 71 | 92 |
| Qwen3-VL 4B Thinking Q4_K_M | 4 096 | 681 à 694 | 1 801 à 1 972 | 59 | 78 |
| Qwen3-VL 4B Thinking Q4_K_M | 16 384 | 377 à 386 | 1 136 à 1 172 | 32 | 50 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 512 | 608 | 1 993 à 1 994 | 58 | 99 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 4 096 | 556 à 557 | 1 595 | 50 | 76 à 81 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 16 384 | 301 | 854 à 858 | 27 | 46 à 49 |

**Temps avant le premier mot**, modèle du service (4B Thinking), question de 16 384 tokens (environ 40 pages) : 42 à 43 s avec Gulliver, 14 s avec llama.cpp.

## Ce qu'on en tire

- **Lecture : llama.cpp va environ 3 fois plus vite** (×2,6 à ×3,3). Première hypothèse, écrite avant les essais : le développement des poids en floats de 32 bits avant le produit. **Corrigé après les essais de la même nuit** : un produit compressé sans développement n'allait pas plus vite ; la cause est la précision du produit, en float32 chez Gulliver (environ 6 000 milliards d'opérations par seconde, le plafond du GPU), en demi-précision chez llama.cpp. D'où Gulliver 6.7.0, ci-dessous.
- **Écriture : llama.cpp va 1,3 à 1,8 fois plus vite**, l'écart grandissant avec le contexte (×1,4 à 512 tokens pour le 2B, ×1,5 à 1,6 à 16 384). L'attention de l'écriture de Gulliver lit des clés et valeurs en 8 bits qu'elle doit développer ; llama.cpp les lit en demi-précision avec une attention fusionnée.
- Ces écarts ne disent rien de la justesse : sur les modèles comparés, les deux moteurs écrivent les mêmes réponses (voir les notes pour l'auteur).

## Après Gulliver 6.7.0 (produits des prompts en demi-précision), même nuit

Lecture seule (l'écriture ne change pas), mêmes réglages ; 2B et 4B en mesures alternées avec la 6.6.0 (deux tours), Qwen3-30B en un tour.

| Modèle | Contexte | Gulliver 6.6.0 | Gulliver 6.7.0 | llama.cpp |
|---|---|---|---|---|
| Qwen3-VL 2B Instruct Q4_K_M | 512 | 1 803 à 1 870 | 4 567 à 4 772 | 5 765 à 5 930 |
| Qwen3-VL 2B Instruct Q4_K_M | 4 096 | 1 706 à 1 743 | 3 445 à 3 477 | 4 786 à 4 918 |
| Qwen3-VL 2B Instruct Q4_K_M | 16 384 | 966 à 970 | 1 282 à 1 326 | 2 857 à 2 923 |
| Qwen3-VL 4B Thinking Q4_K_M | 512 | 750 à 761 | 2 041 à 2 100 | 2 494 à 2 564 |
| Qwen3-VL 4B Thinking Q4_K_M | 4 096 | 649 à 674 | 1 304 à 1 367 | 1 801 à 1 972 |
| Qwen3-VL 4B Thinking Q4_K_M | 16 384 | 367 à 377 | 530 à 532 | 1 136 à 1 172 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 512 | 608 | 683 | 1 993 à 1 994 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 4 096 | 556 à 557 | 601 | 1 595 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 16 384 | 301 | 300 | 854 à 858 |

- Sur les contextes courts et moyens des modèles denses, l'écart tombe de ×3 à ×1,2-1,4.
- Il reste grand sur les longs contextes (×2,2 pour le 4B à 16 384 tokens) : l'attention des prompts de Gulliver calcule en float32 ; c'est la piste suivante.
- Il reste grand pour le mélange d'experts (×2,9) : les experts passent par le noyau `mm_id` de Gulliver, en float32, pas par MPS ; à porter en demi-précision ensuite.
