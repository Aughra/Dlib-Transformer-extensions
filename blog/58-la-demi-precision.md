# Dlib Transformer Extensions sur macOS, épisode 58 : la demi-précision

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.7.0) : écriture sur le GPU **114 à 155 tokens/s** (2B), **62 à 80 tokens/s** (4B) ; lecture sur le GPU **4 567 à 4 772 tokens/s** sur 512 tokens (2B), **2 041 à 2 100** (4B).

Suite de [l'épisode 57](57-les-grands-modeles-a-experts.md), consacré aux grands modèles à experts. On avait promis de comparer Gulliver à un autre moteur sur les longues questions. On l'a fait à la même étape, et la comparaison n'était pas flatteuse.

## Trois fois plus lent

L'autre moteur, llama.cpp, est la référence du domaine. Sur les mêmes fichiers, sur le même Mac mini, il lisait les questions environ **trois fois plus vite** que Gulliver, et écrivait ses réponses 1,3 à 1,8 fois plus vite. Pour le modèle du service, une question de quarante pages se lisait en 14 secondes d'un côté, en 43 de l'autre.

On le dit tel quel, parce que c'est le but d'une comparaison. Les deux moteurs écrivent les mêmes réponses ; l'un les écrit plus vite.

## La fausse piste

La première explication semblait évidente. Pour lire une question, Gulliver déballe chaque matrice compressée du modèle en nombres complets avant de la multiplier, alors que llama.cpp multiplie les nombres encore compressés. Comme décongeler tout le congélateur avant de choisir son plat.

On a donc écrit un noyau qui multiplie sans déballer, en reprenant celui qui servait déjà aux experts. Première version : 30 à 40 % plus lente. Deuxième version, plus soignée : à égalité sur les questions courtes, plus lente sur les longues. La règle du portage est simple : une optimisation n'est gardée que si elle gagne, en mesures alternées. Les deux versions sont parties à la corbeille, et l'erreur de raisonnement a été corrigée dans le document de mesure.

## Le calcul au dos de l'enveloppe

C'est un calcul d'ordre de grandeur qui a remis sur la voie. Lire 512 tokens avec le modèle du service demande environ 4 000 milliards de multiplications et d'additions. À la vitesse de Gulliver, cela fait environ 6 000 milliards par seconde : à peu près le plafond du GPU quand il calcule en nombres complets, sur 32 bits. llama.cpp en tient environ 20 000 milliards par seconde. Ce n'est possible qu'en **demi-précision**, sur 16 bits, où le GPU du M5 a des unités de calcul faites pour ces tableaux.

L'image est celle d'un comptable qui fait toutes ses additions au centime de centime : juste, mais lent. Faire les multiplications au centime et garder les totaux au centime de centime donne le même bilan, trois fois plus vite.

## Le changement, et son filet

Le changement tient en peu de lignes. Les poids et les entrées des produits sont préparés en demi-précision, la bibliothèque de calcul d'Apple les multiplie sur ses unités dédiées, et les sommes restent en pleine précision. La lecture des questions va **1,2 à 2,7 fois plus vite**, sur les huit modèles mesurés ; le modèle du service lit 2 041 à 2 100 tokens par seconde au lieu de 750 à 761, et une question de quarante pages en 31 secondes au lieu de 44.

Il fallait un filet, et le moteur en avait un : à chaque chargement, il compare le GPU au processeur sur une petite question, et renvoie le modèle au processeur au moindre désaccord. Un modèle y est tombé, LFM2.5-8B-A1B, un modèle à experts dont l'aiguilleur hésite sur des différences d'arrondi. Plutôt que de le perdre, le contrôle fait maintenant deux essais : en demi-précision d'abord, en pleine précision ensuite. Ce modèle garde son GPU, à la vitesse d'avant ; tous les autres prennent le gain.

## Ce qu'il reste

L'écart avec llama.cpp passe de ×3 à ×1,2-1,4 sur les questions courtes et moyennes. Il reste grand sur les très longues questions (×2,2 à 16 384 tokens), où l'attention, encore en pleine précision, prend le dessus, et sur les modèles à experts (×2,9), dont le noyau est encore en pleine précision. Ce sont les deux pistes suivantes.

## Pour les curieux : les chiffres et les fichiers

- **Comparaison** : `docs/mesures/2026-11-20-gulliver-contre-llama-cpp.md` (llama.cpp 1.0.0, build 12359, `llama-bench`, réglages par défaut).
- **Avant et après** (Mac mini, `bench_runtime … q8 metal`, 6.6.0 et 6.7.0 en alternance, deux tours, tokens/s en lecture) :

| Modèle | 512 tokens | 4 096 tokens | 16 384 tokens |
|---|---|---|---|
| Qwen3-VL 2B Q4_K_M | 1 803-1 870 → 4 567-4 772 | 1 706-1 743 → 3 445-3 477 | 966-970 → 1 282-1 326 |
| Qwen3-VL 4B Thinking Q4_K_M | 750-761 → 2 041-2 100 | 649-674 → 1 304-1 367 | 367-377 → 530-532 |
| Gemma 3 4B Q4_K_M | 838-847 → 2 167-2 196 | 778-792 → 1 597-1 643 | 620-628 → 1 000-1 010 |
| LFM2.5 1.2B Q8_0 | 1 960-2 032 → 4 985-5 206 | 1 962-1 976 → 4 909-4 911 | 1 633-1 641 → 3 185-3 201 |

- **Code** : noyaux `dequant_h_*` et `to_half` (`engine/metal_kernels.metal`), `gemm_half` (`MPSMatrixMultiplication`, entrées `MPSDataTypeFloat16`, résultat `MPSDataTypeFloat32`, `engine/metal_device.mm`), `product()` et le contrôle en deux essais (`engine/cpu_runtime_metal.cpp`). dlib n'a pas changé.
- **Le contrôle de LFM2.5-8B-A1B** en demi-précision : « prompt: logits within 5.99926% … step 2: logits within 7.1724% …, most likely token different » ; repris en floats, il passe.
- **Tests** : `test_macos.sh` 70 étapes, `test_metal`, banc de la page 136 vérifications.
- **Version** : Gulliver **6.7.0**.

## À suivre

L'attention des longues questions, et le noyau des experts, en demi-précision eux aussi.
