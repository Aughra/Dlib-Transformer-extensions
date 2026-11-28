avant
<!-- semaine : début -->

## La mesure de la semaine (28/11/2026 à 09 h 17)

Écrite par le Mac à chaque mesure de la semaine, le lundi (`tools/bench_modeles.sh`) : Gulliver seul, sur chacun de ses modèles installés ; jetons par seconde, mémoire de conversation en 8 bits. Charge de la machine pendant la mesure (3 relevés, toutes les 20 secondes) : température de la puce 60,0 °C en moyenne (de 50,0 à 70,0) ; utilisation du processeur 19 % en moyenne (de 5 à 40) ; utilisation du gpu 63 % en moyenne (de 0 à 99).

| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Perplexité | Épreuve |
|---|---|---|---|---|---|---|---|
| Qwen2.5 Coder 1.5B Q4_K_M | GPU | 6 510 | 7 200 | 185 | 153 | 21,92 | 22/100 |
| Qwen3-VL 2B Instruct Q4_K_M | GPU | 6 206 | 6 704 | 182 | 142 | 18,65 | 50/100 |
| Qwen3-VL 2B Thinking Q4_K_M | GPU | 6 109 | 6 675 | 181 | 142 | 20,32 | 78/100 |
| Qwen2.5-0.5B-Instruct Q4_K_M | processeur | 1 415 | 1 303 | 118 | 118 | 25,79 | 18/100 |
| Ministral 3 3B Q4_K_M | GPU | 3 612 | 3 381 | 110 | 92,4 | 12,51 | 45/100 |
| Granite 4.2 3B Q4_K_M | GPU | 3 388 | 3 381 | 102 | 69,5 | 17,85 | 88/100 |
| Qwen3-VL 30B A3B Thinking Q4_K_M (MoE) | GPU | 1 677 | 1 960 | 94,4 | 78,8 | 10,93 | 95/100 |
| LFM2.5-24B-A2B Q8_0 (MoE) | GPU | 1 188 | 1 204 | 92,1 | 87,3 | 19,57 | 98/100 |
| Qwen3-VL 4B Instruct Q4_K_M | GPU | 2 956 | 2 689 | 91,6 | 75,7 | 14,83 | 78/100 |
| Qwen3-4B-Instruct-2507 Q4_K_M | GPU | 3 006 | 2 602 | 91,5 | 75,2 | 14,89 | 82/100 |
| Qwen3-VL 4B Thinking Q4_K_M | GPU | 2 948 | 2 652 | 90,8 | 75,5 | 15,37 | 95/100 |
| CyberSecQwen 4B Q4_K_M | GPU | 2 867 | 2 792 | 89,1 | 74,5 | 13,67 | 70/100 |
| Phi-4-mini Q4_K_M | GPU | 2 967 | 3 095 | 86,5 | 67,5 | 20,52 | 42/100 |
| gemma-4-E2B-it Q4_K_M | GPU | 4 841 | 2 035 | 78,5 | 61,0 | 69,69 | 85/100 |
| Qwen3.5-4B Q4_K_M | GPU | 1 485 | 1 526 | 73,2 | 71,7 | 11,50 | 92/100 |
| Qwen3.6-35B-A3B-MTP Q4_K_M (MoE) | GPU | 850 | 869 | 59,5 | 56,3 | 8,66 | 98/100 |
| Qwen2.5 Coder 7B Q4_K_M | GPU | 1 684 | 1 739 | 58,8 | 50,2 | 16,45 | 58/100 |
| gemma-4-E4B-it Q4_K_M | GPU | 2 410 | 1 244 | 55,9 | 53,8 | 36,61 | 90/100 |
| Foundation-Sec 1.1 8B Q4_K_M | GPU | 1 599 | 1 570 | 55,2 | 49,5 | 18,57 | 35/100 |
| DeepSeek-R1 Distill Llama 8B Q4_K_M | GPU | 1 449 | 1 550 | 54,8 | 49,0 | 30,01 | 48/100 |
| Foundation-Sec 8B Reasoning Q4_K_M | GPU | 1 464 | 1 583 | 54,7 | 49,5 | 13,28 | 58/100 |
| Qwen3-VL 8B Instruct Q4_K_M | GPU | 1 539 | 1 526 | 54,0 | 47,8 | 12,14 | 78/100 |
| Qwen3-VL 8B Thinking Q4_K_M | GPU | 1 557 | 1 559 | 53,8 | 47,8 | 12,87 | 98/100 |
| Ministral 3 8B Q4_K_M | GPU | 1 501 | 1 475 | 52,3 | 46,5 | 10,00 | 78/100 |
| Granite 4.2 8B Q4_K_M | GPU | 1 360 | 1 345 | 49,2 | 43,2 | 19,55 | 100/100 |
| EuroVLM 9B Preview Q4_K_M | GPU | 1 358 | 1 363 | 47,4 | 41,9 | 13,33 | 42/100 |
| EuroLLM 9B Instruct Q4_K_M | GPU | 1 254 | 1 343 | 46,6 | 41,8 | 13,54 | 32/100 |
| Qwen3.5-9B Q4_K_M | GPU | 988 | 1 086 | 45,4 | 44,9 | 9,84 | 92/100 |
| MiMo-V2.6-Distill-Qwen-9B Q4_K_M | GPU | 1 006 | 1 095 | 45,2 | 44,6 | 8,94 | 88/100 |
| gemma-4-26B-A4B-it Q4_K_M | processeur | 134 | 188 | 23,9 | 22,3 | 2828,56 | 100/100 |
| gemma-4-12b-it Q4_K_M | processeur | 56,5 | 76,2 | 21,9 | 21,1 | 411,86 | 98/100 |
| Mistral Small 3.2 24B Q4_K_M | GPU | 500 | 371 | 19,6 | 18,4 | 8,60 | 88/100 |
| Devstral Small 2 Q4_K_M | GPU | 457 | 391 | 19,3 | 18,3 | 8,87 | 82/100 |

Conseillé : pour tout faire, Granite 4.2 8B Q4_K_M ; le plus rapide, Ministral 3 3B Q4_K_M. Les tableaux complets, avec llama.cpp et MLX, sont dans « Mesures de la semaine ».

Comme un utilisateur, par le service, sur Qwen3-VL 4B A :

| Mesure | Gulliver |
|---|---|
| Chargement du modèle (jusqu'à une première réponse) | 1,7 s |
| Conversation de 32k : lecture | 729 jetons/s |
| Conversation de 32k : écriture, avec brouillons | 33,8 jetons/s |
| Conversation de 32k : écriture, sans brouillons | 36,1 jetons/s |
| 4 requêtes à la fois : écriture totale | 235 jetons/s |
| 3 documents de 4 000 tokens à la fois : temps total | 15,4 s |
| 3 documents à la fois : attente moyenne du premier mot | 6,8 s |
| Le plus grand contexte qui tient (essayés : 65 536 et 131 072) | 131 072 tokens (262 144 refusés) |

La recherche dans les documents (RAG) : 40 questions figées sur les 61 épisodes du blog, cherchées par un service d'essai isolé (qwen3-embedding-0.6b, épisodes indexés en 136 s) ; une question est trouvée quand le passage qui y répond est rendu. Le meilleur réglage : moteur « Sens seul », tri fin allumé, 12 passages.

| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |
|---|---|---|---|---|
| Hybride (mots et sens) | 88 % | 95 % | 98 % | 0,71 |
| Sens seul + tri fin (le meilleur) | 92 % | 95 % | 95 % | 0,73 |
| Mots seuls | 82 % | 90 % | 92 % | 0,62 |

<!-- semaine : fin -->
après
