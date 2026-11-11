# Sonde Metal : le GPU du Mac lit-il les poids plus vite que le processeur ?

Petit programme propre au fork, hors du moteur. Il enveloppe en place, sans copie (`newBufferWithBytesNoCopy`), les matrices Q4_K et Q6_K d'un modèle GGUF projeté en mémoire. Il les fait lire au GPU par des noyaux Metal (compilés par macOS au lancement, sans outil de plus), puis il mesure :

- l'exactitude, contre un produit en double précision des lignes décompressées (64 lignes par matrice) ;
- le temps pour lire tous les poids une fois (un token) : en une commande, en une commande attendue par couche, en une commande attendue par produit ;
- le plafond du GPU, avec une lecture pure sans calcul ;
- le coût d'un aller-retour (une commande minuscule, attendue) ;
- le processeur sur les mêmes produits (`qk_matmul`, noyaux NEON du moteur).

## Construire et lancer

Depuis la racine du dépôt :

```sh
E=dlib/dlib/external/albireo
clang++ -std=c++17 -O2 -fobjc-arc -Idlib -o build/metal-sonde macos/metal-sonde/probe.mm \
  $E/engine/qkernels.cpp $E/engine/model_file.cpp $E/common/system_info.cpp \
  -framework Metal -framework Foundation -framework Accelerate
Q4=mv3_q4_K ROWS=4 SGS=4 build/metal-sonde macos/metal-sonde/kernels.metal <modèle.gguf> 8
```

Variables : `Q4` et `Q6` choisissent les noyaux (`mv_q4_K`, `mv2_q4_K`, `mv3_q4_K` ; `mv_q6_K`, `mv2_q6_K`), `ROWS` le nombre de lignes par simdgroup, `SGS` le nombre de simdgroups par groupe, `QUICK=1` s'arrête après l'exactitude, le plafond et les mesures par format. Le dernier argument est le nombre de threads du processeur.

## Résultats sur le MacBook Air M2 (2026-11-11)

GPU de 10 cœurs, `lowpowermode 0`, charge 3 à 4. Meilleurs noyaux : `mv3_q4_K` et `mv_q6_K`, `ROWS=4`, `SGS=4`.

| Un token, tous les poids Q4_K et Q6_K | 4B (2 375 Mo) | 2B (1 050 Mo) |
|---|---|---|
| Processeur, 8 threads | 55,2 ms (45,1 Go/s) | 26,2 ms |
| Processeur, 4 threads | 50,9 ms (48,9 Go/s) | |
| GPU, une commande | 45,1 à 47,8 ms (52 à 55 Go/s) | 19,7 ms |
| GPU, plafond (lecture pure) | 35,3 à 35,7 ms (70 Go/s) | 17,0 ms |
| GPU, une commande attendue par couche | 56,7 à 59,7 ms | |
| GPU, une commande attendue par produit | 120,4 à 121,5 ms | |

- Aller-retour avec le GPU : 186 à 188 µs (médiane).
- Exactitude du GPU : écart de 2,8e-8 au plus, rapporté à la somme des |w x|. Le processeur, qui arrondit les activations en 8 bits, est à 8,8e-4.
- Q4_K seul : 38,6 Go/s avec `mv_q4_K`, 26,9 avec `mv2_q4_K` (lectures de 4 octets), 52,9 avec `mv3_q4_K` (16 octets alignés par thread, 8 threads par bloc). Q6_K seul : 57 à 60 Go/s avec `mv_q6_K`.
