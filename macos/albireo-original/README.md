# L'Albireo d'origine, à côté de Gulliver

Pour comparer Gulliver à l'original sur les mêmes tests (demande de l'humain, 2026-11-21), le moteur de l'Albireo de Cydral est construit à part, **sans rien changer à son code**, sur une version figée.

| Fichier | Rôle |
|---|---|
| `COMMIT` | La version de l'original mesurée (`upstream/main` du 21 novembre 2026 : Albireo 1.0.27, `af5c811`). La changer, c'est changer le point de comparaison : le noter au journal. |
| `build.sh` | Crée la copie de l'original (un `git worktree` d'`upstream`, avec son propre dlib) dans un dossier à part, `../albireo-original` à côté du dépôt (ou `ALBIREO_ORIGINAL_DIR`), puis compile ses outils dans `<copie>/build-mac/`. |
| `perplexity_albireo.cpp` | La perplexité calculée par le moteur de l'original (code du fork, compilé contre le sien), comme `perplexity_runtime` de Gulliver. |

**Ce qu'il a fallu changer dans l'original pour qu'il tourne sur Mac : rien.** Son `tests/bench_runtime.cpp` et son moteur compilent tels quels avec AppleClang, en leur donnant le dossier `dlib` de la copie comme chemin d'en-têtes ; seul le choix des noyaux diffère de son CMake : les « portables » (ceux des processeurs sans AVX2), puisque ses noyaux rapides sont écrits pour les processeurs Intel et AMD.

**Ce que mesure cette colonne** : le code de l'original **sur ce Mac**, processeur seul (il n'a que CUDA et OpenVINO pour accélérateurs, absents ici), avec ses calculs de secours. Ce n'est pas l'original sur un bon PC Windows, qui irait bien plus vite. Il ne lit pas les familles que seul le fork a ajoutées (LFM2, Gemma 3…) : les tableaux le disent.

Les scripts `gulliver/tools/bench_moteurs.sh` et `justesse_moteurs.sh` l'utilisent dès que ses outils sont construits ; le script du dimanche (`bench_hebdo.sh`) les construit avant de mesurer.
