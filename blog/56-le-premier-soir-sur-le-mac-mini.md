# Dlib Transformer Extensions sur macOS, épisode 56 : le premier soir sur le Mac mini

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.4.0) : écriture sur le GPU **114 à 155 tokens/s** (2B), **62 à 80 tokens/s** (4B), selon la longueur du contexte ; lecture sur le GPU **1 510 à 1 805 tokens/s** sur 512 tokens (2B). Sur le MacBook Air M2, au même moment : 33 à 53 (2B), 17 à 25 (4B) et 512 à 529.

Suite de [l'épisode 55](55-le-navigateur-se-vide.md), où le navigateur cessait de calculer. Depuis des semaines, chaque mesure fine finissait par « à refaire sur le Mac mini ». Il est arrivé. Ce soir, on l'a mis à l'épreuve, et il a fait mieux que prévu sur un point qu'on n'attendait pas.

## Le contrôle technique de la voiture neuve

Avant de chronométrer une voiture, on vérifie qu'elle roule. Les outils installés, le dépôt récupéré, les modèles copiés du M2 (53 Go), on a lancé tous les tests du portage : ceux du service, ceux de Pseudalis, ceux qui comparent le GPU au processeur, et le banc qui joue la page comme un utilisateur.

Tout a passé, sauf deux vérifications : celles qui joignent un PDF. Le service répondait « 422 », c'est-à-dire « je ne sais pas lire ça ». La raison était bête : la bibliothèque qui lit les PDF n'est pas rangée dans le dépôt (elle pèse 7,6 Mo et se télécharge à part), et le guide du premier jour oubliait de la faire venir. C'est comme déménager en laissant ses lunettes de lecture dans l'ancienne maison. Le guide a gagné sa ligne, et les 136 vérifications du banc passent.

## Deux machines, une même course

Pour comparer honnêtement, on a lancé le même script, sur la même version du code et les mêmes fichiers de modèles, sur les deux machines, à dix minutes d'écart. Le Mac mini pilotait le M2 à distance, par le réseau de la maison.

| Mac mini divisé par M2 | Plage | Médiane |
|---|---|---|
| Lecture d'un prompt, GPU | 2,6 à 4,6 | 3,4 |
| Écriture d'une réponse, GPU | 2,5 à 3,6 | 2,8 |
| Lecture, processeur | 1,5 à 3,0 | 1,9 |
| Écriture, processeur | 1,7 à 2,4 | 2,1 |

Le GPU gagne plus que le processeur. Écrire un mot oblige à relire tous les poids du modèle : c'est la vitesse de la mémoire qui commande, et celle du M5 Pro est bien plus large. L'écart grandit aussi avec la longueur du texte : sur un prompt de 64 tokens, le temps fixe de chaque passe pèse encore ; sur 3 584, il ne reste que le calcul. Pour le modèle du service, une question de dix pages se lit en cinq secondes au lieu d'une vingtaine.

## L'équipe rapide qui ralentissait

La surprise est venue du nombre de threads, ces ouvriers entre lesquels le moteur partage le calcul. Le M2 a deux sortes de cœurs : quatre rapides et quatre économes. Le moteur du fork avait appris, il y a plusieurs semaines, à confier l'écriture aux seuls cœurs rapides, parce qu'attendre les économes faisait perdre du temps. C'est une « équipe rapide », comme on enverrait les meilleurs coureurs d'un relais sans attendre les promeneurs.

Le M5 Pro, lui, n'a pas de promeneurs. macOS décrit ses cœurs comme six « Super » et douze « Performance » : tous courent. Mais le moteur, qui ne regardait que la première famille, laissait douze coureurs sur le banc pendant chaque mot écrit. Sur le modèle du service, il écrivait à 47 tokens par seconde avec l'équipe de six, et à 65 avec tout le monde.

La correction tient en une condition : on ne forme l'équipe rapide que si macOS appelle la seconde famille « Efficiency », comme sur le M2. Rien ne change sur l'ancienne machine ; sur la nouvelle, l'écriture sur le processeur gagne de 15 à 60 % selon le modèle. L'erreur était facile à commettre, et on l'a commise : une règle juste sur une puce, écrite comme si elle valait pour toutes.

## Deux réponses à la même question

La correction a eu un effet de bord, et les tests l'ont vu. Un essai demande à un modèle de 8 milliards de paramètres qui voyage dans l'enregistrement 3 d'un long journal de 120 lignes. Avec la nouvelle version, il répondait « Inès », un voyage qui n'existe pas, au lieu de « Gaëlle ».

Rien n'était cassé ; c'est plus subtil. Pour relire le contexte en écrivant, le moteur le coupe en tranches qu'il lit en parallèle, puis additionne les résultats. Le nombre de tranches suivait le nombre de threads. Or une addition de nombres à virgule ne donne pas tout à fait le même résultat selon l'ordre où on la fait, comme une addition de prix arrondis au centime. Sur une réponse au bord du basculement, ce dernier centime suffit : avec 6 ou 8 threads, le modèle disait Gaëlle ; avec 10, 12 ou 18, Inès.

On a donc fixé le nombre de tranches, quel que soit le nombre de threads. La même question reçoit désormais la même réponse sur le M2, sur le Mac mini, et quel que soit le réglage, sans perte de vitesse mesurable. Le défaut vient du code d'origine, qui découpe de la même façon : il est signalé à son auteur.

## Les réglages, vérifiés un par un

La nouvelle machine aurait pu demander d'autres réglages. On les a mesurés plutôt que supposés :

- **le nombre de threads** : tous les cœurs (18) restent le meilleur choix, devant 14, 12, 10 et 6 ;
- **le seuil à partir duquel le GPU lit les prompts** : le GPU gagne déjà à 24 tokens, la plus petite valeur permise ; on la garde ;
- **la longueur jusqu'à laquelle le GPU lit** : sur 16 384 tokens, il lit 3,6 fois plus vite que le processeur ; il garde tous les prompts ;
- **les modèles à experts** : sur le M2, GPU et processeur faisaient presque jeu égal ; sur le Mac mini, le GPU l'emporte nettement. Rien à changer.

## Pour les curieux : les chiffres et les fichiers

- **Machines** : MacBook Air M2 (`Mac14,15`, 4 + 4 cœurs, 24 Go, 8 threads) et Mac mini M5 Pro (`Mac17,16`, 6 « Super » + 12 « Performance », 64 Go, 18 threads), macOS 27.2.1, Gulliver 6.3.0 au commit `982fc30` pour la comparaison.
- **Tableaux complets** : `docs/mesures/2026-11-19-m2.md`, `docs/mesures/2026-11-19-mac-mini-m5-pro.md`, et leur comparaison, `docs/mesures/2026-11-19-m2-contre-mac-mini.md`.
- **L'équipe rapide** : `make_fast_team` dans `engine/thread_pool_apple.inc` lit désormais `hw.perflevel1.name` ; mesures en alternance 6.3.0 / 6.4.0, trois tours, processeur, 18 threads :

| Modèle | Contexte | Écriture 6.3.0 | Écriture 6.4.0 |
|---|---|---|---|
| Qwen3-VL 2B Q4_K_M | 64 | 87,7 à 102,5 tokens/s | 130,6 à 137,5 |
| Qwen3-VL 4B Thinking Q4_K_M | 512 | 38,4 à 41,4 | 64,0 |
| Qwen3.5-0.8B Q8_0 | 512 | 137,5 à 141,2 | 181,5 à 184,4 |
| Qwen 3.6 11B-A3B Q4_K_M | 512 | 42,5 à 45,9 | 52,6 à 53,5 |

- **Le découpage de l'attention** : `part_workers` = 16 dans `engine/cpu_runtime.h`, à la place du nombre de threads actifs ; une cible de 8 perdait 15 % sur le Mac mini, 16 va aussi vite que l'ancien calcul sur les deux machines.
- **Tests sur le Mac mini** : `test_metal` passe, `dtest --test_metal` 4 041 vérifications, `test_macos.sh` de Gulliver et de Pseudalis sans échec, banc de la page 136 vérifications. dlib n'a pas changé.
- **Version** : Gulliver **6.4.0**.

## À suivre

Le premier morceau de l'indépendance de Gulliver vis-à-vis de dlib : son empreinte SHA-256, confiée à la bibliothèque d'Apple.
