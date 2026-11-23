# Dlib Transformer Extensions sur macOS, épisode 62 : une nuit à l'atelier

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.42.1, mesures de la nuit du 22 au 23 novembre) : écriture sur le GPU **90 tokens/s** (4B, mémoire de conversation en 8 bits), le 2B n'a pas été remesuré (164 en 6.22.0) ; lecture sur le GPU **2 920 à 3 075 tokens/s** sur 512 tokens (4B), **1 534 à 1 571** sur 16 384 tokens (jusqu'à 1 898 au calme).

Suite de [l'épisode 61](61-la-course.md), où Gulliver passait devant llama.cpp à la lecture, mais restait loin derrière MLX sur les longues questions. Entre les deux, la journée du 22 novembre a surtout donné un mot de passe à chaque profil (6.31.0). Le soir, l'humain est allé se coucher en laissant une consigne : continuer seul jusqu'à 7 heures, trancher seul les questions, et faire le point au réveil. Cet épisode raconte cette nuit, de la version 6.33.0 à la 6.42.1.

## La porte du jardin

Le premier travail n'a rien à voir avec la vitesse. Depuis la 6.31.0, chaque profil a son mot de passe, mais une question était restée ouverte : depuis l'iPad, un profil quelconque pouvait encore fermer l'accès au réseau, supprimer un modèle ou lire le journal du service. C'est comme une maison où chacun a sa clé de chambre, mais où le tableau électrique reste ouvert à tous.

La version 6.33.0 réserve dix-neuf actions à l'administrateur quand elles arrivent par le Wi-Fi : installer ou supprimer un modèle, changer les réglages, redémarrer, lire le journal. Depuis le Mac lui-même, elles restent libres. Le choix a une raison simple : les agents qui mettent les modèles à jour et mesurent le moteur chaque lundi tournent sur le Mac sans session, et les y obliger les aurait cassés cette nuit même. Un programme du Mac peut de toute façon lire les fichiers de Gulliver ; la serrure n'aurait rien protégé de plus.

## Le guichet qui traitait deux lignes à la fois

Le deuxième travail venait d'une mesure de la veille. Quand trois personnes envoient chacune un long document en même temps, chacune attendait son premier mot environ 6 secondes chez Gulliver, deux fois plus que chez llama.cpp. Le premier diagnostic, posé à 21 heures, était faux : on pensait qu'un document seul se lisait plus lentement que trois ensemble. Mesuré, il se lit aussi vite.

La vraie cause tenait en un nombre, hérité de l'original. Dès qu'une réponse s'écrit, le moteur ne lisait plus que 64 tokens des questions en attente à chaque passe, au lieu de 512. L'idée était de garder la réponse fluide. L'effet était inverse : c'est comme un guichet qui, dès qu'un client signe, ne traiterait plus que deux lignes du dossier suivant à chaque tour. Le dossier traîne, et le client qui signe attend aussi, parce que le dossier qui traîne le gêne plus longtemps.

La version 6.34.0 relève le frein à 512 et sert d'abord la question la plus courte :

| Situation (modèle du service, GPU) | Avant | 6.34.0 |
|---|---|---|
| Une discussion écrit pendant qu'un document de 8 000 tokens arrive | 19 mots/s, premier mot du document après 11,4 s | **31 mots/s**, **4,8 s** |
| Trois documents de 4 000 tokens envoyés ensemble | premier mot après 6,0 s en moyenne | **4,2 s** (le premier servi en 2,0 s) |

Avec les noyaux du reste de la nuit, l'attente moyenne est descendue à 3,8 secondes. Llama.cpp reste un peu devant (3,2 s) : il lit chaque document un peu plus vite.

## Des miettes, gardées quand même

Venaient ensuite deux petits gains. Le premier : à chaque mot écrit, la première couche du modèle part au GPU dès qu'elle est prête, pendant que le processeur prépare les suivantes, au lieu d'attendre que tout soit écrit. C'est 1 % de plus à l'écriture (6.35.0). On l'a gardé parce qu'il gagnait huit mesures sur huit et que les calculs restaient les mêmes au chiffre près.

Le second : avant chaque multiplication, la lecture convertit ses entrées en demi-précision, le format que le GPU multiplie le plus vite. Trois produits qui lisent la même entrée la convertissaient trois fois, comme on photocopierait trois fois la même page pour trois lecteurs assis à la même table. Une seule copie sert désormais à tous (6.36.0) : la lecture d'une question de 512 tokens gagne 5,5 %.

Un troisième essai a été écarté alors qu'il gagnait environ 2 % : réécrire un petit noyau qui normalise et fait tourner les requêtes. Le résultat n'aurait plus été identique au bit près, car le compilateur de Metal arrondit la même formule différemment selon le noyau qui la contient. La règle du projet ne laisse rien perdre en justesse, et 2 % ne valaient pas une exception.

## Le cartable trop plein

Restait le gros morceau : sur les longues questions, l'**attention**, la partie du calcul où chaque mot regarde tous ceux qui le précèdent. Mesurée seule sur 8 192 tokens, celle de MLX passait 19 millisecondes par couche, celle de Gulliver environ 52. Pourtant, les deux noyaux étaient bâtis sur le même plan, avec les mêmes tuiles et les mêmes accélérateurs.

L'outil qui a trouvé la différence existe sur tous les Mac : la trace « Metal System Trace » d'Instruments, qui se lance aussi en ligne de commande. Elle relève les **débordements de registres**. Les registres sont les cases de travail les plus rapides du GPU, comme les poches d'un élève ; quand elles sont pleines, il range le reste dans son cartable, et chaque aller-retour coûte du temps. Le noyau de Gulliver rangeait 272 octets dans le cartable, au cœur de sa boucle, celui de MLX 128. Un petit script, `tools/gpu_debordements.sh`, fait désormais ce relevé en une commande (6.36.1).

Six retouches de l'ancien noyau ont été essayées ; toutes allaient moins vite. Plutôt que de continuer à le rapiécer, on a repris l'organisation entière de celui de MLX, dont la licence le permet, en le citant dans le code. Avant chaque couche, les clés et valeurs sont remises à la suite, en demi-précision, comme les fiches d'un classeur remises en ordre avant la lecture. Puis chaque groupe de calcul les lit directement, en restant au même pas que ses trois voisins pour que ce que l'un vient de lire serve aussi aux autres. Deux détails de MLX ont fait l'essentiel du gain : dérouler entièrement les petites boucles, pour que le compilateur sache où ranger chaque valeur, et une halte commune au milieu du calcul.

L'attention seule est passée de 52 à 21 millisecondes par couche (MLX : 19). Sur le modèle du service :

| Question | Avant (6.36.0) | 6.37.0 | llama.cpp | MLX |
|---|---|---|---|---|
| 4 096 tokens | 2 223 à 2 334 | **2 493 à 2 614** (+12 %) | 2 057 | 3 099 |
| 16 384 tokens | 996 à 1 148 | **1 534 à 1 571** (+45 %) | 1 205 | 2 076 |

Au calme, une question de 16 384 tokens se lit à 1 898 tokens par seconde, à 9 % de MLX. Une trace complète montre que le GPU n'y reste jamais inactif : 8,40 secondes de calcul sur 8,46. Ce qui reste à gagner est dans les noyaux eux-mêmes, plus dans l'attente.

La justesse n'a pas bougé. La perplexité est la même à l'incertitude près sur quatre modèles, et le juge de référence, le modèle d'origine calculé par PyTorch, retrouve le même premier choix que Gulliver pour 90,74 % des positions, contre 90,71 % avant. Le mérite de l'organisation revient à MLX ; le portage l'a recopiée et ajustée.

Les versions 6.41.0 et 6.42.0 ont ensuite étendu cette attention aux têtes de 256 valeurs (Qwen 3.5, Gemma 3), avec deux groupes de calcul qui se partagent les dimensions de chaque tête, puis aux fenêtres glissantes, où chaque mot ne regarde que ses 1 024 derniers voisins. Qwen3.5-4B lit 16 384 tokens 15 % plus vite, Gemma 3 4B lit 4 096 tokens 7 % plus vite.

## La même leçon, ailleurs

Le déroulement des boucles a servi deux autres fois. Les modèles à experts, qui choisissent pour chaque mot quelques spécialistes parmi des dizaines, passaient 81 % de leur lecture dans un seul produit groupé. Déroulé (6.38.0), puis nourri par morceaux de 16 colonnes au lieu de 64 (6.40.0), il laisse le Qwen3-VL 30B-A3B lire une question de 512 tokens à **1 695-1 701** tokens par seconde, au lieu de 1 168-1 194 : plus de 40 % en deux versions. Le second réglage avait été rejeté plus tôt dans la soirée ; après le déroulement, il gagnait. Un réglage écarté n'est pas écarté pour toujours.

Le produit des petits lots, celui qui sert à écrire plusieurs réponses à la fois et à vérifier les brouillons, déroulé à son tour (6.39.0), gagne 4,5 % à quatre réponses et 5,6 % à huit.

La leçon a aussi ses limites. Déroulée, l'attention de l'écriture allait jusqu'à deux fois moins vite : ce noyau tenait déjà dans ses poches, et le déroulement l'a fait déborder.

## Le modèle de l'humain

L'humain se sert surtout de Ministral 3 8B. Mesuré à 23 h 35, en alternance avec llama.cpp, dans le réglage du service :

| Ministral 3 8B Q4_K_M | Gulliver | llama.cpp |
|---|---|---|
| Lecture, 512 tokens | 1 008 à 1 309 | 1 073 à 1 201 |
| Lecture, 4 096 tokens | **1 217 à 1 325** | 954 à 1 011 |
| Écriture | **49,9 à 50,4** | 47,2 |

Il lit les longues questions 25 à 30 % plus vite que chez llama.cpp, et il écrit 6 % plus vite.

## Dix-huit invités à une heure du matin

À 1 heure, l'agent du lundi a fait son travail habituel : il a installé dix-huit nouveaux modèles du catalogue. On les a tous passés au banc dans la foulée. Tous calculent sur le GPU, sauf le tout petit Qwen2.5-0.5B, dont les largeurs ne tombent pas juste pour les noyaux (c'était déjà connu). Sur cinq d'entre eux, Gulliver est devant llama.cpp ou à égalité, à quelques pour cent près ; Phi-4-mini lit 3 251 tokens par seconde contre 2 794.

La mesure de la semaine ne juge que la vitesse. On a donc aussi posé une question simple à chacun. Dix-sept ont répondu juste, et ceux qui lisent les images ont bien décrit le perroquet des tests. Phi-4-mini, lui, rendait une réponse vide. Ce modèle adapte ses facteurs de rotation, qui disent au modèle où se trouve chaque mot, à la longueur de son contexte. Or la vérification du GPU au chargement calculait déjà des positions avant que ce contexte soit fixé, et le moteur refusait ensuite de changer les facteurs. C'est comme graduer une règle avant de savoir quelle longueur elle mesurera. La 6.42.1 fixe le contexte d'abord, et une étape du test complet le vérifie désormais.

## Ce qui n'a pas marché

La règle de mesure a beaucoup jeté cette nuit : une quinzaine d'essais. On en cite quelques-uns :

- laisser le GPU enchaîner les ordres sans attendre : plus lent ;
- faire attendre le processeur en boucle plutôt qu'en sommeil : rien ;
- fondre trois petits noyaux en un : plus lent, et pas identique au bit près ;
- des tuiles de 32 ou de 128 cases pour les experts : plus lent ;
- éviter la rotation des requêtes en 8 bits : 1 % de moins à 16 384 tokens, sur cinq tours.

Ce dernier essai a été trompeur. Le relevé noyau par noyau promettait un gain, parce qu'il grossit le coût des petits noyaux ; la mesure en alternance l'a démenti. Les temps dérivaient d'ailleurs à mesure que la puce chauffait, jusqu'à 78 °C, et une mesure prise pendant que deux autres projets occupaient le Mac a dû être jetée et refaite au calme.

Deux chantiers restent ouverts. Le Qwen3-VL 30B-A3B écrit encore 8 % moins vite que chez llama.cpp (85 mots par seconde contre 92), et deux essais exacts n'y ont rien changé : la cause reste à trouver. L'entraînement économe en mémoire (QLoRA) n'a pas été commencé : il demande de refondre une pièce centrale de dlib, trop gros pour une nuit sans relecture.

## Pour les curieux : les commandes et les chiffres

Les relevés de la nuit se refont ainsi :

```
bash dlib/dlib/external/gulliver/tools/gpu_debordements.sh <modèle> 2048   # débordements de registres, par noyau
build/macos-gulliver/tests/bench_runtime <modèle> 18 16384 4 1 q8 metal     # lecture et écriture
build/macos-gulliver/tests/perplexity_runtime <modèle> <texte> 4096 8 q8 metal steps8
build/torch-venv/bin/python dlib/dlib/external/gulliver/tools/justesse_reference.py <gguf> build/hf-models/<modèle>
```

Chaque nouvelle attention peut être coupée par une variable, pour comparer en alternance : `GULLIVER_NO_FLAT_ATT`, `GULLIVER_NO_FLAT_256`, `GULLIVER_NO_FLAT_WINDOW`. Par exemple, Gemma 3 4B sur 4 096 tokens, trois tours en alternance :

```
 4096 1 GULLIVER_NO_FLAT_WINDOW=1 3055.6 tokens/s
 4096 1 X=1                       3268.1 tokens/s
 4096 2 GULLIVER_NO_FLAT_WINDOW=1 3042.2 tokens/s
 4096 2 X=1                       3273.9 tokens/s
```

| Version | Changement | Gain mesuré (en alternance) |
|---|---|---|
| 6.33.0 | réglages et modèles réservés à l'administrateur depuis le Wi-Fi | — |
| 6.34.0 | frein de lecture 64 → 512, la question la plus courte d'abord | premier mot de trois documents : 6,0 → 4,2 s |
| 6.35.0 | première couche envoyée au GPU sans attendre | écriture +1 % |
| 6.36.0 | une seule conversion en demi-précision par entrée | lecture 512 tokens +5,5 % |
| 6.36.1 | outil des débordements de registres | — |
| 6.37.0 | attention de la lecture façon MLX | 4 096 tokens +12 %, 16 384 +45 % |
| 6.38.0 | produit des experts déroulé | 30B-A3B, 512 tokens +23 % |
| 6.39.0 | produit des petits lots déroulé | 4 réponses +4,5 %, 8 +5,6 % |
| 6.40.0 | produit des experts par morceaux de 16 colonnes | 30B-A3B +19 %, LFM2.5-8B-A1B +13 % |
| 6.41.0 | attention façon MLX pour les têtes de 256 | Qwen3.5-4B, 16 384 tokens +15 % |
| 6.42.0 | … et pour les fenêtres glissantes | Gemma 3 4B, 4 096 tokens +7 % |
| 6.42.1 | Phi-4-mini se charge | — |

Fichiers touchés, tous dans Gulliver : `engine/metal_kernels_nax.metal` (le rangement des clés et valeurs, les nouvelles attentions, le produit des experts et celui des petits lots), `engine/metal_kernels.metal` (l'activation en demi-précision), `engine/metal_device.h` et `engine/metal_device.mm`, `engine/cpu_runtime_metal.cpp`, `engine/engine.cpp`, `host/server.cpp`, `web/js/profile.js`, `web/js/settings.js`, `tests/test_metal.cpp`, les outils `tools/gpu_debordements.sh`, `tools/make_openapi.py`, `tools/test_comptes.py` et `tools/test_macos.sh`. **dlib n'a pas changé** dans cette étape. Le détail de chaque gain, avec ses mesures avant et après, est dans les notes pour l'auteur, changements 47 à 55.

## La suite

Le lundi à 8 heures, la mesure de la semaine reprend tout cela de son côté, sur tous les modèles installés, au calme et en alternance. Le prochain épisode dira ce qu'elle en pense, et s'attaquera à ce qui reste : l'écriture du modèle à experts, les têtes de 512 valeurs de Gemma 4, et les 9 % qui séparent encore Gulliver de MLX sur les très longues questions.
