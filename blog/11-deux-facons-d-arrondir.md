# Dlib Transformer Extensions sur macOS, épisode 11 : deux façons d'arrondir

Suite de [l'épisode 10](10-le-programme-qui-ne-savait-pas-ou-il-habitait.md) : le service d'Albireo tourne sur le Mac, et la page web répond dans le navigateur. Le premier vrai document qu'on lui a donné, un PDF de 4,6 Mo, a fait apparaître la dernière partie lente du moteur.

## Le PDF qui ralentit tout

Le document était le guide d'hygiène informatique de l'ANSSI. Albireo l'a résumé correctement : 42 règles, dix chapitres, les bons numéros de page. Mais la réponse arrivait à 3,4 tokens par seconde, quatre fois moins vite que sur une question courte.

Une photographie du moteur pendant l'écriture a désigné le coupable. Les deux tiers du temps passaient dans « l'attention ». Avant d'écrire chaque mot, le modèle relit tout ce qui précède pour décider sur quoi s'appuyer. Avec une question de trente tokens, c'est un coup d'œil. Avec un document de vingt mille, c'est comme relire tout le livre avant d'écrire chaque mot de la fiche de lecture. Les épisodes 8 et 9 avaient accéléré les calculs sur les poids du modèle, pas cette relecture, qui tournait encore en version de secours, un nombre à la fois.

## Le même calcul, quatre fois à la fois

L'attention fait deux choses. Elle donne d'abord une note à chaque token déjà lu : ce sont les « scores ». Puis elle mélange ces tokens selon leurs notes : ce sont les « valeurs ». Ce que le modèle relit est gardé en nombres de demi-taille, sur 16 bits au lieu de 32, pour tenir en mémoire.

Le nouveau calcul convertit ces demi-nombres avec une instruction de la puce, et traite quatre tokens de front pour les scores, quatre positions de front pour les valeurs. Comme dans les épisodes précédents, chaque case du registre suit une seule somme, du premier terme au dernier, dans le même ordre que le code d'origine. Pour les scores, il a fallu ranger les tokens relus « en travers », comme on retourne un tableau pour lire une colonne comme une ligne.

## Deux façons d'arrondir

Le premier contrôle a été à moitié bon. Les scores tombaient juste, au bit près, pour toutes les tailles qu'ont les vrais modèles (seule une taille de test, 6, faisait exception). Les valeurs, non : 28 907 sur 49 152 différaient, au dernier chiffre.

On connaissait le suspect depuis l'épisode 9. Un processeur peut faire « multiplier puis additionner » en deux gestes, avec un arrondi après chacun, ou en un seul, avec un arrondi à la fin. À l'épisode 9, le code d'origine faisait deux gestes, et notre premier essai un seul. On avait donc écrit l'attention en deux gestes. Cette fois, c'était faux, et pas partout.

En lisant le code machine que le compilateur d'Apple produit pour le code d'origine, on a trouvé la règle exacte. Il n'en a pas une, il en a deux, selon la boucle :

- **pour les scores**, il découpe la somme en paquets de quatre, fait les multiplications en bloc puis les additions une à une, en deux gestes. Les un à trois derniers termes, qui ne remplissent pas un paquet, se font en un seul geste ;
- **pour les valeurs**, dont les nombres sont éparpillés en mémoire, il renonce aux paquets et fait toute la somme en un seul geste.

C'est le même calcul écrit de la même façon dans le code source, et pourtant il est arrondi de deux manières. On peut le comparer à deux employés qui font la même addition, l'un sur une calculatrice qui arrondit à chaque étape, l'autre sur une calculatrice qui garde tout en tête jusqu'au résultat. Le compilateur choisit l'employé selon la forme de la boucle, sans le dire.

Une fois cette double règle reproduite, le contrôle est tombé à zéro : 0 différence sur 549 760 résultats, avec des têtes de cinq tailles et de 1 à 2 049 tokens relus. L'erreur de départ était de croire qu'un compilateur arrondit toujours de la même façon. On l'a commise, et c'est le contrôle qui l'a montrée.

Le résultat a un prix. Notre calcul reproduit le code d'origine tel que cette version du compilateur d'Apple le traduit. Un autre compilateur pourrait choisir autrement, et le contrôle serait alors à refaire. Il est gardé pour ça.

## Deux fois plus vite sur un long texte

Mesures à tour de rôle, sur un texte de 4 096 tokens, avec le modèle 2B des épisodes précédents :

| | Avant | Après |
|---|---|---|
| Lecture du texte | 25 tokens/s (2 min 40 s) | 50 à 53 tokens/s (1 min 20 s) |
| Écriture après ce texte | 7,5 à 9 tokens/s | 20 à 22 tokens/s |

Pour finir, on a demandé au modèle de résumer les épisodes 8 et 9 de ce blog, soit 4 329 tokens, avec l'ancien et le nouveau moteur. Les deux réponses sont identiques, mot pour mot. Le résumé n'est pas parfait, puisque le modèle mêle un peu les deux épisodes, mais il est le même des deux côtés. L'un des deux l'écrit simplement deux fois plus vite.

## Pour les curieux : le code machine et les chiffres

La boucle des valeurs, telle qu'Apple clang 21 compile le code d'origine. La somme est faite en un seul geste (`fmadd`), clé après clé :

```
LBB37_76:
	ldr	s1, [x19, x16]
	ldr	s2, [x0]
	fmadd	s0, s1, s2, s0
	add	x0, x0, x17
	add	x16, x16, #4
	subs	x14, x14, #1
	b.ne	LBB37_76
```

Celle des scores, en deux gestes : quatre multiplications en bloc (`fmul.4s`), puis quatre additions une à une (`fadd`) :

```
	fmul.4s	v1, v1, v2
	mov	s2, v1[3]
	mov	s3, v1[2]
	mov	s4, v1[1]
	fadd	s0, s0, s1
	fadd	s0, s0, s4
	fadd	s0, s0, s3
	fadd	s0, s0, s2
```

Le cœur du nouveau calcul des scores, dans `engine/qkernels_neon.inc`, avec les deux règles :

```cpp
if (i < hd4)
    for (int b = 0; b < NB; ++b) acc[r][b] = vaddq_f32(acc[r][b], vmulq_f32(x, k[b]));
else
    for (int b = 0; b < NB; ++b) acc[r][b] = vfmaq_f32(acc[r][b], x, k[b]);
```

Les trois versions du contrôle, pour une tête de 128 (les vrais modèles) :

| Règle essayée | Scores différents | Valeurs différentes |
|---|---|---|
| Tout en deux gestes | 0 sur 83 072 | 28 907 sur 49 152 |
| Paquets en deux gestes, reste en un, partout | 0 sur 83 072 | 25 676 sur 49 152 |
| Celle du compilateur : scores en paquets, valeurs en un geste | 0 sur 83 072 | 0 sur 49 152 |

Les mesures alternées, `bench_runtime` à 8 fils, 4 096 tokens de prompt puis 32 tokens écrits, `lowpowermode 0`, charge moyenne de 4 à 16 :

| Essai | Version | Lecture | Écriture |
|---|---|---|---|
| 1 | avant | 25,5 tokens/s | 9,0 tokens/s |
| 1 | NEON | 52,8 tokens/s | 19,6 tokens/s |
| 2 | avant | 24,5 tokens/s | 7,5 tokens/s |
| 2 | NEON | 50,0 tokens/s | 22,0 tokens/s |

Une première série a été coupée par la fin d'une session de travail, après un seul essai. On l'a refaite entière plutôt que de compléter l'ancienne : deux mesures prises à des moments différents ne se comparent pas.

## À suivre

Le PDF de l'ANSSI, à refaire dans la page web pour voir ce que donne le gain sur un vrai document. Puis Pseudalis, qui attend toujours sa boîte aux lettres.
