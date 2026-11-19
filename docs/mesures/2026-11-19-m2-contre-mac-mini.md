# MacBook Air M2 contre Mac mini M5 Pro — 2026-11-19

Même commit (`982fc30`, Gulliver 6.3.0), mêmes fichiers de modèles, même script (`tools/bench_macos.sh 2`), lancés le même soir à dix minutes d'écart, chaque machine sur secteur, sans service lancé, mode économie d'énergie coupé sur le M2. Tableaux complets : [2026-11-19-m2.md](2026-11-19-m2.md) et [2026-11-19-mac-mini-m5-pro.md](2026-11-19-mac-mini-m5-pro.md).

| | MacBook Air M2 | Mac mini M5 Pro |
|---|---|---|
| Cœurs | 4 « Performance » + 4 « Efficiency » | 6 « Super » + 12 « Performance » |
| Threads des mesures | 8 | 18 |
| Mémoire | 24 Go | 64 Go |
| Charge au début / à la fin | 1,97 / 4,83 | 1,28 / 9,06 |

## Résultat : de 2,5 à 4,6 fois plus vite sur le GPU

Tokens par seconde, meilleur des deux essais. « × » = Mac mini divisé par M2.

| Modèle | Prompt | Lecture GPU M2 → mini | × | Écriture GPU M2 → mini | × | Lecture proc. × | Écriture proc. × |
|---|---|---|---|---|---|---|---|
| Qwen3-VL 2B Instruct Q4_K_M | 64 | 276 → 774 | 2,8 | 53,0 → 155 | 2,9 | 1,7 | 2,4 |
| Qwen3-VL 2B Instruct Q4_K_M | 512 | 529 → 1804 | 3,4 | 47,7 → 134 | 2,8 | 1,8 | 2,3 |
| Qwen3-VL 2B Instruct Q4_K_M | 1536 | 470 → 2019 | 4,3 | 42,7 → 127 | 3,0 | 2,2 | 2,2 |
| Qwen3-VL 2B Instruct Q4_K_M | 3584 | 394 → 1812 | 4,6 | 33,0 → 114 | 3,4 | 3,0 | 2,1 |
| Qwen3-VL 4B Thinking Q4_K_M (modèle du service) | 64 | 112 → 328 | 2,9 | 24,8 → 80,2 | 3,2 | 1,7 | 2,2 |
| Qwen3-VL 4B Thinking Q4_K_M | 512 | 208 → 767 | 3,7 | 23,1 → 71,2 | 3,1 | 1,9 | 2,1 |
| Qwen3-VL 4B Thinking Q4_K_M | 1536 | 200 → 800 | 4,0 | 21,5 → 68,6 | 3,2 | 2,3 | 1,9 |
| Qwen3-VL 4B Thinking Q4_K_M | 3584 | 158 → 681 | 4,3 | 17,4 → 61,9 | 3,6 | 2,2 | 1,8 |
| Qwen3.5-0.8B Q8_0 | 64 | 452 → 1208 | 2,7 | 68,9 → 180 | 2,6 | 1,9 | 2,1 |
| Qwen3.5-0.8B Q8_0 | 512 | 634 → 2000 | 3,2 | 65,5 → 171 | 2,6 | 1,6 | 2,1 |
| Qwen3.5-0.8B Q8_0 | 1536 | 567 → 1913 | 3,4 | 63,4 → 164 | 2,6 | 1,8 | 2,0 |
| Qwen3.5-0.8B Q8_0 | 3584 | 494 → 1758 | 3,6 | 63,1 → 168 | 2,7 | 2,2 | 2,1 |
| Qwen3.5-4B Q4_K_M | 64 | 94,3 → 304 | 3,2 | 19,7 → 65,1 | 3,3 | 1,5 | 1,9 |
| Qwen3.5-4B Q4_K_M | 512 | 165 → 564 | 3,4 | 19,5 → 63,0 | 3,2 | 1,5 | 1,9 |
| Qwen3.5-4B Q4_K_M | 1536 | 156 → 528 | 3,4 | 19,2 → 55,4 | 2,9 | 2,1 | 2,1 |
| Qwen3.5-4B Q4_K_M | 3584 | 134 → 517 | 3,9 | 18,3 → 62,0 | 3,4 | 2,3 | 2,1 |
| Qwen 3.6 11B-A3B Q4_K_M (mélange d'experts) | 64 | 124 → 391 | 3,1 | 26,8 → 67,5 | 2,5 | 2,0 | 1,9 |
| Qwen 3.6 11B-A3B Q4_K_M | 512 | 196 → 718 | 3,7 | 25,3 → 63,8 | 2,5 | 1,7 | 1,7 |
| Qwen 3.6 11B-A3B Q4_K_M | 1536 | 183 → 696 | 3,8 | 24,8 → 63,6 | 2,6 | 1,7 | 1,7 |
| Qwen 3.6 11B-A3B Q4_K_M | 3584 | 152 → 624 | 4,1 | 24,0 → 63,2 | 2,6 | 2,1 | 1,8 |
| Qwen3.5-0.8B Q5_K_M | 64 | 457 → 1200 | 2,6 | 62,2 → 176 | 2,8 | 1,9 | 2,0 |
| Qwen3.5-0.8B Q5_K_M | 512 | 629 → 1968 | 3,1 | 59,7 → 167 | 2,8 | 1,7 | 2,2 |
| Qwen3.5-0.8B Q5_K_M | 1536 | 585 → 1918 | 3,3 | 58,9 → 161 | 2,7 | 1,9 | 2,1 |
| Qwen3.5-0.8B Q5_K_M | 3584 | 498 → 1757 | 3,5 | 57,8 → 162 | 2,8 | 2,2 | 1,9 |

| Rapport Mac mini / M2 | Plage | Médiane |
|---|---|---|
| Lecture, GPU | 2,6 à 4,6 | 3,4 |
| Écriture, GPU | 2,5 à 3,6 | 2,8 |
| Lecture, processeur | 1,5 à 3,0 | 1,9 |
| Écriture, processeur | 1,7 à 2,4 | 2,1 |

## Ce qu'on en tire

- **Le GPU gagne plus que le processeur** : il a plus de cœurs et une mémoire plus rapide, et l'écriture d'un token lit tous les poids du modèle : c'est la vitesse de la mémoire qui la borne.
- **L'écart grandit avec la longueur du prompt** (de ×2,6-3,2 à 64 tokens à ×3,5-4,6 à 3 584) : à 64 tokens, le temps fixe de chaque passe (lancer les noyaux) pèse encore ; sur un long prompt, c'est le calcul pur.
- **Sur le Mac mini, le GPU gagne aussi l'écriture partout** (×1,3 à ×2,6 sur le processeur), alors que sur le M2 les deux étaient proches (Qwen3.5-0.8B Q5_K_M écrivait même plus vite sur le processeur).
- **Les chiffres « processeur » du Mac mini ci-dessus sont ceux de Gulliver 6.3.0**, dont l'écriture n'utilisait que les 6 cœurs « Super » : Gulliver 6.4.0 corrige cela (voir les notes pour l'auteur, changement 26).
