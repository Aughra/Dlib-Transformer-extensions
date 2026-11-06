# Dlib Transformer Extensions sur macOS, épisode 7 : le mode économie

Suite de [l'épisode 6](06-albireo-parle-lentement.md) : Albireo répond juste sur le Mac, mais à moins d'un token par seconde, et son moteur semble n'occuper qu'un cœur sur huit. On était parti chercher un défaut dans le code. On a trouvé un réglage.

## Les fils tout au bout de la file

Première étape de l'enquête : regarder chaque fil de calcul séparément, pendant que le moteur écrit une réponse. macOS dit, pour chacun, quelle part du processeur il reçoit et avec quelle priorité. Le fil qui pilote le calcul était à la priorité normale, 31. Les trois fils qui l'aident étaient à 0, la plus basse qui existe, et ne recevaient chacun qu'un cinquième de cœur.

L'explication semblait toute trouvée. Entre deux morceaux de travail, un fil d'aide attend en boucle. Sur un PC Intel, il fait une courte pause sur place, avec une instruction prévue pour cela. Sur une puce ARM, le code d'Albireo lui fait céder son tour au système. Or macOS a une règle : un fil qui cède son tour voit sa priorité tomber à zéro un moment. À force de céder sa place, l'élève poli se retrouve toujours au bout de la file de la cantine, et quand le travail arrive, il est servi le dernier.

## Une correction qui marche, et ne sert à rien

Les puces ARM ont leur propre pause courte, l'instruction `isb`, qui ne demande rien au système. C'est celle que le langage Rust emploie pour la même attente sur ces puces. Une ligne a suffi pour qu'Albireo l'utilise sur Mac.

Le résultat était exactement celui prévu, et il n'a servi à rien. Les fils d'aide sont bien remontés à la priorité 31. Mais le moteur écrivait toujours à 0,77 token par seconde, et chaque fil ne recevait plus que quelques pour cent d'un cœur, alors qu'ils étaient tous prêts à travailler. Ils n'étaient plus au bout de la file : c'est la cantine entière qui servait au ralenti.

## Le sac à dos

La réponse n'était pas dans le code. Le Mac était en **mode économie d'énergie**, alors même qu'il était branché sur le secteur. Ce mode bride la puce pour que la batterie dure, et plus encore sur un MacBook Air, qui n'a pas de ventilateur pour la refroidir. Le Mac était en plus bien occupé ailleurs : l'affichage, l'éditeur de code, le logiciel de la souris. On chronométrait un coureur qui portait un sac à dos plein, au milieu d'une foule.

Ce réglage, c'est à l'humain de le changer, pas au programme. Il l'a coupé. La même question, au même moteur, sans rien recompiler :

| | Mode économie | Puissance normale |
|---|---|---|
| Écriture de la réponse, 8 fils | 0,78 token/s | 7,9 tokens/s |
| Cœurs occupés en moyenne | 0,9 | 4,6 |

Dix fois plus vite, et sans changer une seule ligne de code. Le « moteur qui n'occupe qu'un cœur » de l'épisode 6 n'existait pas : c'était le Mac qui ne lui en donnait pas plus. L'écart avec le banc d'essai a disparu du même coup, et les quatre cœurs économes du M2 aident maintenant au lieu de rester sur le banc.

L'erreur est la nôtre. On a mesuré sans vérifier les conditions de la mesure, et l'épisode 6 a tiré une conclusion de chiffres faussés. Il porte désormais une note qui renvoie ici.

## Garder ou jeter la correction

Restait la question de la petite correction. Elle remettait les fils à la bonne priorité, ce qui semble mieux sur le papier. Mais une optimisation se juge au chronomètre, pas au papier.

On a donc construit deux moteurs, l'un avec la correction et l'autre sans, et on les a fait répondre à tour de rôle : avec, sans, avec, sans. En alternant, les deux versions subissent les mêmes conditions, y compris l'App Store qui faisait ses mises à jour pendant ce temps. Le verdict est sans appel : la version sans la correction est un peu plus rapide, deux fois sur deux. Une explication possible, qu'on n'a pas vérifiée : la pause courte garde les fils éveillés et prêts, mais ils occupent alors le processeur pour rien, au détriment de ceux qui calculent.

La correction est partie. Le moteur d'Albireo est revenu, octet pour octet, à celui de l'auteur. Ce n'est pas un échec : c'est une piste de moins, et une règle de plus pour la suite. Avant toute mesure, on vérifie le mode d'énergie et la charge du Mac ; pour comparer deux versions, on les alterne dans la même séance ; et on ne garde que ce qui gagne.

## Pour les curieux : les relevés

Les fils du moteur pendant l'écriture d'une réponse, en mode économie, avant la correction. Le dernier est le fil qui pilote le calcul, les trois premiers sont ses aides (colonnes `%CPU` et `PRI`) :

```
%CPU STAT PRI     STIME     UTIME
16,0 R     0T   0:00.35   0:09.39
17,1 R     0T   0:00.36   0:09.47
15,7 R     0T   0:00.35   0:09.40
32,8 R    31T   0:00.45   0:13.15
```

Le code qui fait patienter un fil, dans `engine/thread_pool.h` :

```cpp
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define ALBIREO_CPU_RELAX() _mm_pause()
#else
#define ALBIREO_CPU_RELAX() std::this_thread::yield()
#endif
```

La correction essayée, puis retirée :

```cpp
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define ALBIREO_CPU_RELAX() __asm__ __volatile__("isb" ::: "memory")
```

Après elle, toujours en mode économie, les aides sont à la priorité 31, mais presque sans processeur :

```
%CPU STAT PRI
 2,9 R    31T
 4,0 R    31T
 2,4 R    31T
12,1 R    31T
```

Le réglage coupable, et comment le vérifier :

```
$ pmset -g | grep lowpowermode
 lowpowermode         1
```

Les quatre mesures alternées, puissance normale, `engine_chat` à 4 fils :

| Essai | Version | Lecture de la question | Écriture de la réponse |
|---|---|---|---|
| 1 | avec `isb` | 6,8 tokens/s | 5,2 tokens/s |
| 1 | sans | 8,4 tokens/s | 5,7 tokens/s |
| 2 | avec `isb` | 6,9 tokens/s | 5,5 tokens/s |
| 2 | sans | 7,7 tokens/s | 6,4 tokens/s |

Les mesures de référence, puissance normale, moteur d'origine, Qwen3-VL 2B Instruct :

| Programme | Fils | Lecture de la question | Écriture de la réponse | Cœurs occupés |
|---|---|---|---|---|
| `engine_chat` | 4 | 7,7 tokens/s | 6,0 tokens/s | 2,9 |
| `engine_chat` | 8 | 9,2 tokens/s | 7,9 tokens/s | 4,6 |
| `bench_runtime` | 1 | 2,7 tokens/s | 2,2 tokens/s | 1,0 |
| `bench_runtime` | 4 | 9,6 tokens/s | 7,0 tokens/s | 3,7 |
| `bench_runtime` | 8 | 11,3 tokens/s | 8,3 tokens/s | 5,9 |

## À suivre

Huit tokens par seconde, c'est déjà une réponse qui se lit en s'écrivant. Mais le moteur calcule toujours un nombre à la fois. Le prochain chantier est celui de l'épisode 6, qui reste entier : apprendre NEON au moteur, pour qu'il en calcule quatre d'un geste. Cette fois, le chronomètre partira d'une base honnête.
