# Dlib Transformer Extensions sur macOS, épisode 8 : seize à la fois

Suite de [l'épisode 7](07-le-mode-economie.md) : une fois le mode économie d'énergie coupé, Albireo écrit environ huit tokens par seconde sur le Mac. Mais son moteur calcule toujours un nombre à la fois. Cet épisode lui apprend à en calculer davantage d'un seul geste.

## Deux formats seulement

Un modèle de langage, c'est surtout un très gros tableau de nombres, ses « poids ». Pour qu'il tienne en 1,1 Go au lieu de plusieurs, on range ces poids en format compressé : chaque nombre y est réduit à quelques bits, un peu comme une photo qu'on enregistre en JPEG plutôt qu'en brut. Le moteur d'Albireo connaît une quinzaine de ces formats.

Avant d'écrire la moindre ligne, on a donc ouvert le fichier du modèle pour compter ce qu'il contient vraiment. Il n'y a que deux formats : Q4_K, environ 4 bits par nombre, pour 70 % des poids ; et Q6_K, environ 6 bits, pour les 30 % restants. Deux calculs bien faits couvrent donc presque toute l'écriture d'une réponse. Inutile de réécrire les treize autres.

## Seize multiplications d'un geste

Pour écrire chaque token, le moteur multiplie les poids du modèle par une rangée de nombres qui résume le texte déjà écrit, et additionne les résultats. Des milliards de fois. C'est ce « produit scalaire » qui occupait presque tout le temps du moteur.

Sur un PC Intel, Albireo le fait avec AVX2. Sur une puce Apple, il le faisait jusqu'ici à la main, nombre après nombre. Or les puces Apple ont une instruction faite exactement pour cela : `sdot` prend seize petits nombres entiers d'un côté, seize de l'autre, les multiplie deux à deux et additionne les produits, d'un seul geste. C'est la caissière qui pèse tout le panier d'un coup au lieu de passer les fruits un par un.

On a écrit deux petits calculs avec cette instruction, un pour chaque format. Et on les a rangés dans un fichier neuf, propre au fork. Le fichier de l'auteur ne reçoit que quelques lignes pour les brancher, et seulement quand la puce sait faire `sdot`. Ce sont des notes dans la marge, pas une page réécrite.

## Plus vite, et pas un bit de différence

Accélérer un calcul a souvent un prix discret. En changeant l'ordre des additions, on change un peu les arrondis, et un modèle de langage, sensible comme il l'est, peut alors choisir un autre mot quelque part au milieu de sa réponse. On voulait éviter cela.

Le calcul d'origine a une particularité qui l'a permis. Pour chaque paquet de 256 nombres, il additionne d'abord des nombres entiers, et une somme d'entiers donne toujours le même résultat, dans n'importe quel ordre. Les nombres à virgule n'arrivent qu'à la fin du paquet. Les nouveaux calculs font donc les mêmes sommes entières, par seize au lieu d'une, puis terminent chaque paquet avec exactement la même formule que l'original. Le résultat est le même, au bit près.

Ça se vérifie. Sur 12 000 produits tirés au hasard, comparés à ceux du calcul d'origine, il n'y a eu aucune différence, pas même au dernier chiffre. Et la réponse du modèle à notre question habituelle est la même, octet pour octet.

## Trois fois plus vite

Les mesures suivent la règle de l'épisode 7 : mode économie coupé, charge du Mac relevée, et les deux versions à tour de rôle. Le verdict, à 8 fils :

| | Moteur d'origine | Avec `sdot` |
|---|---|---|
| Écriture de la réponse | 11 tokens/s | 33 à 37 tokens/s |
| Deux réponses en même temps | 11 tokens/s au total | 42 tokens/s au total |

Trente-cinq tokens par seconde, c'est plus vite qu'on ne lit. Le mérite en revient d'abord à l'auteur, dont le calcul en nombres entiers a rendu l'opération si propre : on n'a fait que le recopier seize fois plus vite.

## Ce qui reste lent

La lecture de la question, elle, n'a pas bougé : une douzaine de tokens par seconde. Ce n'est pas un oubli. Quand la question compte au moins 8 tokens, le moteur prend un autre chemin, pensé pour traiter plusieurs tokens d'un coup : il développe les poids compressés en nombres ordinaires, puis les multiplie avec un calcul à virgule, que les puces Intel accélèrent et que le Mac fait encore à la main. Pour une question courte, cela se voit à peine. Pour un long document collé dans la conversation, c'est désormais l'étape qui se fait attendre.

## Pour les curieux : le noyau Q4_K

Le cœur du calcul pour Q4_K, dans `engine/qkernels_neon.inc`. Chaque octet de poids porte deux nombres de 4 bits : les quatre bits du bas vont avec les 32 premières valeurs de la rangée, les quatre du haut avec les 32 suivantes. `vdotq_s32` fait les seize multiplications et les additions :

```cpp
const uint8x16_t q0 = vld1q_u8(qs);
const uint8x16_t q1 = vld1q_u8(qs + 16);
// The low four bits against the first 32 activations, the high four against the next 32.
int32x4_t a1 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vandq_u8(q0, m4)), vld1q_s8(q8));
a1 = vdotq_s32(a1, vreinterpretq_s8_u8(vandq_u8(q1, m4)), vld1q_s8(q8 + 16));
int32x4_t a2 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vshrq_n_u8(q0, 4)), vld1q_s8(q8 + 32));
a2 = vdotq_s32(a2, vreinterpretq_s8_u8(vshrq_n_u8(q1, 4)), vld1q_s8(q8 + 48));
const int s1 = vaddvq_s32(a1);
const int s2 = vaddvq_s32(a2);
isum += sc1 * s1 + sc2 * s2;
```

La fin de chaque paquet, recopiée telle quelle du calcul d'origine, ce qui garantit le même arrondi :

```cpp
sum += d * static_cast<float>(isum) - dmin * static_cast<float>(summ);
```

Le branchement dans `qk_dot`, dans le fichier de l'auteur :

```cpp
#if ALBIREO_QK_NEON
        switch (t)
        {
        case ggml_type::Q4_K: return dot_q4_K_neon(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q6_K: return dot_q6_K_neon(w, static_cast<const block_q8_K*>(act), n);
        default: break;
        }
#endif
```

Les vérifications :

```
albireo quantized kernels (NEON+dotprod)
  dot Q4_K    worst relative error 9.32e-09
  dot Q6_K    worst relative error 8.43e-09
all checks passed

kernels NEON+dotprod: 0 of 12000 products differ from the portable kernel
```

Les mesures alternées, `lowpowermode 0`, charge moyenne de 5 à 7, 8 fils :

| Programme | Essai | Version | Lecture de la question | Écriture de la réponse | 2 réponses à la fois |
|---|---|---|---|---|---|
| `engine_chat` | 1 | origine | 7,8 tokens/s | 10,9 tokens/s | |
| `engine_chat` | 1 | `sdot` | 10,3 tokens/s | 32,5 tokens/s | |
| `engine_chat` | 2 | origine | 12,8 tokens/s | 11,0 tokens/s | |
| `engine_chat` | 2 | `sdot` | 12,7 tokens/s | 36,9 tokens/s | |
| `bench_runtime` | 1 | origine | 8,8 tokens/s | 6,5 tokens/s | 11,3 tokens/s |
| `bench_runtime` | 1 | `sdot` | 13,7 tokens/s | 33,7 tokens/s | 43,2 tokens/s |
| `bench_runtime` | 2 | origine | 13,4 tokens/s | 10,8 tokens/s | 10,5 tokens/s |
| `bench_runtime` | 2 | `sdot` | 13,2 tokens/s | 32,9 tokens/s | 41,5 tokens/s |

Le moteur d'origine est plus rapide ici qu'à l'épisode 7 (11 tokens/s contre 8), parce que le Mac était moins chargé ce jour-là. C'est la raison même des mesures alternées : seule la comparaison faite dans la même séance compte.

## À suivre

L'écriture va vite ; la lecture des longues questions, pas encore. Le prochain chantier apprend au Mac le calcul à virgule par paquets, pour que la lecture d'un document suive le rythme de l'écriture.
