# Dlib Transformer Extensions sur macOS, épisode 61 : la course

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.22.0, mesure du 22 novembre) : écriture sur le GPU **164 tokens/s** (2B), **82 tokens/s** (4B) après 512 tokens, sans brouillon ; lecture sur le GPU **5 315 tokens/s** sur 512 tokens (2B), **2 679** (4B).

Suite de [l'épisode 60](60-un-modele-qui-a-lu-le-blog.md), où un modèle affiné sur ce blog se mettait à en imiter le ton. Ce jour-là, une autre question est arrivée, plus terre à terre : Gulliver est-il rapide ? Pas « plus rapide que l'original », ce qu'on savait déjà, mais rapide **à côté des autres**. La réponse a été franche, et elle a changé la priorité du projet.

## Trois moteurs sur la même piste

Sur un Mac, deux moteurs font référence pour faire tourner un modèle de langage chez soi. **llama.cpp**, le plus répandu, lit les mêmes fichiers GGUF que Gulliver. **MLX**, la bibliothèque d'Apple, a son propre format de fichier. On a posé les trois sur la même piste : mêmes modèles, mêmes longueurs de question, mesures en alternance, et la justesse vérifiée par la perplexité, c'est-à-dire la surprise moyenne du modèle devant un texte qu'il n'a jamais lu. Plus elle est basse, mieux le modèle devine la suite.

Une quatrième colonne s'est ajoutée à la demande : l'**Albireo d'origine**, construit à part, sans en changer une ligne, avec son propre dlib. Sur ce Mac, il tourne sur le processeur seul avec ses noyaux portables. Il ne dit donc rien de l'original sur un bon PC Windows ; il dit seulement ce que le portage a apporté sur cette machine.

Le premier relevé, le matin du 21 novembre, était sans appel :

| Modèle du service, question de 512 tokens | Lecture (jetons/s) | Écriture (jetons/s) |
|---|---|---|
| Gulliver 6.15.0 | 2 153 | 70,7 |
| llama.cpp | 2 590 | 92,2 |
| MLX | 2 930 | 106 |
| Albireo d'origine (processeur) | 19,2 | 21,4 |

Gulliver arrivait troisième partout, derrière llama.cpp et loin derrière MLX ; à 16 384 tokens, MLX lisait près de quatre fois plus vite. La seule bonne nouvelle était la justesse : sur le même fichier, Gulliver, llama.cpp et l'original donnaient la même perplexité, à quelques dixièmes de pour cent près. Le moteur calculait juste ; il calculait lentement.

L'humain a tranché le soir même : la priorité devient **le Gulliver le plus rapide possible sur le Mac mini**, sans rien perdre en justesse, en apprenant de MLX (sa licence MIT permet de lire ses noyaux et d'en reprendre les idées, en le citant) sans l'embarquer.

## Deux vitesses, deux limites

Un moteur fait deux choses qui ne butent pas sur la même limite. **Lire la question**, c'est faire passer tous ses mots ensemble dans le modèle : un travail de calcul pur, comme une usine qui reçoit un camion entier de pièces et va aussi vite que ses machines. **Écrire la réponse**, c'est un mot à la fois, et pour chaque mot le modèle relit tous ses poids dans la mémoire : comme relire tout un dictionnaire pour écrire chaque mot. On va aussi vite qu'on tourne les pages.

Ce plafond-là se calcule. Le GPU du Mac mini lit au plus 254 à 260 Go par seconde ; le modèle du service pèse 2,5 Go ; il ne peut donc pas écrire plus d'une centaine de mots par seconde, quel que soit le moteur. MLX en tirait 93 %, llama.cpp 89 %, Gulliver 69 %. Il y avait près d'un tiers de la route à rattraper à l'écriture, et bien davantage à la lecture.

## Le dictionnaire déplié une seule fois

Le premier gain est presque gênant de simplicité. Les poids d'un modèle sont rangés compressés, en 4 bits ; pour lire une question, le GPU les dépliait en demi-précision à chaque passe, puis les jetait. Une longue question se lit en plusieurs passes : le même dictionnaire était donc déplié, rangé, redéplié. La version 6.17.0 garde les poids dépliés d'une passe à l'autre, tant que la mémoire le permet (un quart de la mémoire de l'appareil au plus). La lecture d'une question de 512 tokens passe de 2 060-2 159 à **2 593-2 708 jetons/s** : Gulliver lit désormais aussi vite que llama.cpp sur les questions courtes.

## Les accélérateurs cachés du M5

La puce M5 a une nouveauté discrète : dans chaque cœur de son GPU, un petit circuit spécialisé dans les produits de matrices, que Metal 4 rend accessible par une interface toute neuve. MLX s'en sert déjà ; c'était la piste la plus prometteuse pour expliquer son avance.

Une sonde a d'abord montré une surprise : les produits de matrices de Gulliver, confiés à la bibliothèque d'Apple, **passaient déjà** par ces accélérateurs, à 31,6 milliers de milliards d'opérations par seconde. L'avance de MLX tenait donc ailleurs, surtout à son **attention**, la partie du calcul où chaque mot de la question regarde tous ceux qui le précèdent. On a repris l'organisation de MLX : les scores gardés dans les registres du GPU, ses cases de travail les plus rapides, au lieu de faire l'aller-retour par la mémoire, et les produits confiés aux accélérateurs. En 6.18.0 puis 6.18.1, une question de 4 096 tokens passe de 1 467 à **2 186-2 280 jetons/s**, une question de 16 384 tokens de 524 à **1 093-1 255**. Le mérite de l'organisation revient à MLX ; le portage l'a recopiée et ajustée.

## L'écriture, page par page plutôt que mot par mot

Pour l'écriture, la capture d'une étape noyau par noyau a désigné deux coupables. L'attention de l'écriture d'abord : chaque groupe de 32 fils de calcul y lisait la conversation une position à la fois, comme 32 lecteurs qui se passeraient un seul livre. Elle ne lisait que quelques dizaines de mégaoctets, et pourtant elle prenait jusqu'à 29 % d'une étape : le GPU attendait. La version 6.19.0 la réécrit à la manière du « flash-decoding » : chaque fil prend sa position, 32 à la fois, et les lectures partent ensemble. L'écriture après 512 tokens passe de 70,7 à 78,4 jetons/s, après 16 384 tokens de 28-31 à **41-42**. Puis les produits par les poids, les trois quarts d'une étape : en 6.19.2, chaque groupe de 32 fils calcule une ligne au lieu de quatre, ce qui occupe mieux le GPU, et l'écriture gagne encore 5 %, à 81-82 jetons/s.

Les mêmes idées ont ensuite été étendues. Les têtes d'attention de 256 valeurs de Gemma 3 n'entraient pas dans les nouveaux noyaux ; elles y entrent en 6.19.3 et 6.20.0, et Gemma 3 4B lit une question de 4 096 tokens 66 % plus vite, au niveau de llama.cpp. Les modèles à experts, qui choisissent pour chaque mot quelques spécialistes parmi des dizaines, faisaient trois quarts de leur lecture dans un seul produit groupé ; en 6.21.0, ce produit passe à son tour par les accélérateurs, par paquets de 64 mots au lieu de 16. Qwen3-30B-A3B lit 36 à 58 % plus vite.

## Ce qui n'a pas marché

La règle de mesure ne garde que ce qui gagne en alternance, et elle a beaucoup jeté. Des tuiles de 128 mots pour les experts : −27 %. Des tuiles de 128 lignes : −31 %. Un double tampon et des pas de 64 positions dans l'attention : rien. Deux, puis quatre lignes par groupe dans les produits des experts de l'écriture : rien non plus.

L'échec le plus instructif est celui des **encodeurs concurrents**. Le GPU reçoit des centaines de petits ordres par mot écrit (un millier pour Qwen3-30B-A3B), et les exécute l'un après l'autre. Une mesure à part promettait beaucoup : en laissant le GPU enchaîner les ordres sans attendre, chacun coûtait 0,6 microseconde au lieu de 3,4. Mais dans le moteur, presque chaque ordre dépend du précédent, et il faut alors poser une barrière entre eux ; avec ces barrières, l'écriture **perdait** 2 à 5 %. L'essai sans barrières, lui, n'a rien mesuré du tout : le contrôle du GPU a vu des résultats faux et renvoyé l'écriture au processeur. Le code a été retiré, et l'essai consigné au journal. Le garde-fou a fait son travail : un moteur rapide et faux ne serait pas passé.

## Plusieurs mots par passe

Restait une idée que l'original avait déjà : les **brouillons**. Une réponse recopie souvent des morceaux de la conversation, le nom d'un fichier, une phrase d'un document, une ligne de code. Quand les derniers mots écrits figurent déjà plus haut, le moteur propose leur suite comme brouillon, et le modèle vérifie tout le brouillon en une seule passe : il garde les mots qu'il aurait écrits lui-même, et reprend la main au premier qui diffère. Comme dicter une lettre à quelqu'un qui connaît déjà le texte : il écrit la phrase qu'il devine, et il suffit d'un « oui » d'un coup d'œil.

Repris de l'Albireo 1.0.27, le mécanisme a d'abord déçu : +34 % sur une recopie, mais 1 à 4 % de perte partout ailleurs. Un profil en a donné la raison : vérifier quatre mots coûtait 2,7 fois le prix d'un seul. Les produits d'une passe de plusieurs mots faisaient une multiplication par poids et par mot sur les unités ordinaires du GPU ; c'était le calcul, pas la mémoire, qui fixait le temps. L'erreur était de croire que lire les poids une fois suffisait à rendre la passe bon marché.

La version 6.22.0 fait passer ces petits lots par les accélérateurs du M5 : chaque poids est déplié une seule fois pour tous les mots, et les multiplications ne coûtent presque plus rien. Vérifier huit mots ne coûte plus que **1,37 fois** une passe d'un mot. Les brouillons deviennent rentables :

| Tâche (modèle du service, choix glouton) | Sans brouillon | Avec des brouillons de 7 mots |
|---|---|---|
| Recopier un texte | 78,5 | **245** |
| Répondre à une question sur un document | 76,6 | **96,3** |
| Écrire du code | 79,2 | **85,7** |
| Résumer un document | 76,3 | **82,5** |
| Réponse libre | 79,3 | 80,0 |

Et le même noyau sert à écrire plusieurs réponses à la fois : quatre réponses passent de 123 à **239 jetons/s** au total, huit de 130 à **465**.

Le texte reste celui que le modèle choisit, avec une nuance qu'il faut dire. Sur la réponse libre, il diverge au 2 335e caractère sur 3 586, à un endroit où deux mots étaient presque à égalité : les additions d'une passe de plusieurs mots ne se font pas tout à fait dans le même ordre, ni dans la même précision. La perplexité, elle, ne bouge pas : 15,372 en passes de huit mots, 15,368 mot à mot, pour une incertitude de ±0,95. Les modèles à experts, eux, n'en profitent pas encore : leurs passes de plusieurs mots prennent toujours l'ancien chemin, et Qwen3-30B-A3B perd 2,5 % en réponse libre.

## Où on en est

La mesure du dimanche, refaite le 22 novembre au matin, donne le bilan d'une journée de chantier. Elle a d'ailleurs dû être refaite : la première, lancée dans la nuit par l'agent du Mac, tournait avec la priorité d'une tâche de fond, que macOS cantonne à ses cœurs les plus lents. L'Albireo d'origine y lisait dix fois moins vite que la veille, et c'est ce chiffre trop constant pour être du bruit qui a mis sur la piste. L'erreur était dans l'installation de l'agent, et on l'a commise ; il tourne désormais en priorité normale.

| Modèle du service (Qwen3-VL 4B) | Gulliver 6.15.0 (21 novembre) | Gulliver 6.22.0 (22 novembre) | llama.cpp | MLX |
|---|---|---|---|---|
| Lecture, 512 tokens | 2 153 | **2 679** | 2 579 | 2 781 |
| Lecture, 4 096 tokens | 1 461 | **2 258** | 2 057 | 3 099 |
| Lecture, 16 384 tokens | 535 | **1 256** | 1 205 | 2 076 |
| Écriture après 512 tokens | 70,7 | **82,0** | 92,0 | 106 |
| Écriture après 16 384 tokens | 32,0 | **47,4** | 50,2 | 55,9 |

À la lecture, Gulliver est passé devant llama.cpp sur presque tous les modèles mesurés, de 1 à 9 %, sauf les questions courtes des plus petits et le modèle à experts. À l'écriture sans brouillon, il reste derrière, de 6 à 18 % le plus souvent. MLX garde la tête, nettement sur les longues questions. La justesse, elle, n'a pas bougé : sur le même fichier, les perplexités de Gulliver, de llama.cpp et de l'original restent à 0,35 % au plus les unes des autres.

## Pour les curieux : les outils et les chiffres

Les mesures se refont en une commande chacune :

```
bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh      # vitesse, Gulliver contre llama.cpp, MLX et l'original
bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh   # perplexité des mêmes modèles
GULLIVER_GPU_PROFILE=64 build/macos-gulliver/tests/bench_runtime <modèle> 18 512 64 4 q8 metal
build/macos-gulliver/tests/perplexity_runtime <modèle> <texte> 512 8 q8 metal steps8
```

Un agent du Mac les relance chaque dimanche à 4 heures et ajoute la semaine, datée, en tête de la page « Mesures du dimanche » du carnet.

Le profil d'une étape de quatre séquences, avant la 6.22.0, montrait le produit Q4_K à 52 600 unités de temps par appel pour un mot, 149 000 pour quatre. Le nouveau noyau, `mv_nax`, range les colonnes d'un morceau de 32 de façon que chaque fil de calcul déplie huit valeurs consécutives du fichier d'un coup : l'ordre des colonnes d'une somme est libre, pourvu que les poids et les mots le partagent.

| Version | Changement | Gain mesuré (modèle du service sauf mention) |
|---|---|---|
| 6.17.0 | poids dépliés gardés d'une passe à l'autre | lecture 512 tokens +21 à +27 % |
| 6.18.0, 6.18.1 | attention de la lecture sur les accélérateurs | 4 096 tokens +49 à +55 %, 16 384 ×2,1 à ×2,4 |
| 6.19.0 | attention de l'écriture en tranches parallèles | écriture +11 % (512) à +45 % (16 384) |
| 6.19.2 | une ligne par groupe de 32 fils | écriture +5 % |
| 6.19.3, 6.20.0 | têtes de 256 valeurs (Gemma 3) | Gemma 3 4B : écriture +5 à +10 %, lecture 4 096 +66 % |
| 6.21.0, 6.21.1 | produits des experts sur les accélérateurs | Qwen3-30B-A3B : lecture +36 à +68 % |
| 6.22.0 | petits lots sur les accélérateurs, brouillons | 4 réponses ×1,9, recopie ×3,1 |

Fichiers touchés, tous dans le moteur de Gulliver : `engine/metal_kernels_nax.metal` (nouveau : les noyaux des accélérateurs, compilés à part en Metal 4), `engine/metal_formats.metal` (nouveau : le dépliage des formats, partagé), `engine/metal_kernels.metal`, `engine/metal_device.mm`, `engine/cpu_runtime_metal.cpp`, `engine/engine.cpp`, `engine/sampler.cpp`. **dlib n'a pas changé** dans cette étape. Le détail de chaque changement, avec ses mesures avant et après, est dans les notes pour l'auteur, changements 33 à 39.

## La suite

La route n'est pas finie : MLX garde de l'avance à la lecture des très longues questions, et l'écriture sans brouillon reste derrière les deux autres moteurs. Les prochaines étapes sont les passes de plusieurs mots des modèles à experts, un petit modèle qui proposerait les brouillons quand la conversation n'en offre pas, et la fusion des petits ordres envoyés au GPU. La course continue dans le prochain épisode.
