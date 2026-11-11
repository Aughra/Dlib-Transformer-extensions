# Dlib Transformer Extensions sur macOS, épisode 18 : le grand ménage

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 16) : écriture **39,5 à 40,5 tokens/s** (2B), **18,3 à 18,4 tokens/s** (4B) ; lecture de 4 096 tokens 158 tokens/s (2B).

Suite de [l'épisode 17](17-la-sonde.md) : la sonde a montré que le GPU du Mac lirait les poids un peu plus vite que le processeur, à condition de calculer tout un token d'une traite. Avant d'écrire ce moteur Metal, deux décisions ont changé la nature du projet.

## On ne suit plus l'original

Jusqu'ici, le fork vivait collé à l'original : chaque modification était la plus petite possible, cachée derrière une condition « seulement sur Mac », pour que la fusion suivante se passe sans heurt. C'est la règle du locataire qui ne perce pas les murs, pour pouvoir rendre l'appartement en l'état.

Mais l'auteur avance vite : seize nouveaux commits en deux jours, dont certains réécrivent justement les parties du moteur que le fork accélère. Et un moteur Metal demande de percer des murs. L'humain a donc tranché : le fork ne fusionne plus l'original. Il reste consultable, pour s'en inspirer à la main, mais on ne le recopie plus. La dernière fusion date de l'épisode 15, avec la version 1.0.12 d'Albireo.

## Du pur Apple

Deuxième décision, qui découle de la première : puisqu'on ne rendra pas l'appartement, autant le meubler pour nous. Albireo et Pseudalis étaient chez l'auteur des programmes Windows, avec une variante Linux. Sur le Mac, on traînait tout ce bagage sans s'en servir : l'icône de la barre des tâches, l'installateur, les moteurs pour processeurs Intel et AMD, le moteur pour cartes NVIDIA, les mises à jour automatiques.

Tout cela est parti. La bibliothèque dlib, elle, reste telle quelle : elle est générique, et la nettoyer n'apporterait rien. On s'en éloignera peut-être plus tard, mais pas maintenant.

## Un correcteur de livre bilingue

L'essentiel du code à retirer était mêlé au reste, sous des conditions du type « si on compile pour Windows » ou « si le processeur sait faire AVX2 ». Les supprimer à la main, fichier par fichier, aurait été long et risqué.

macOS fournit un outil fait pour cela, `unifdef`. On lui dit quelles conditions sont fausses (Windows, Linux, les processeurs x86, CUDA) et lesquelles sont vraies (macOS), et il retire les passages concernés en gardant le reste intact. C'est un correcteur qui ôterait d'un livre bilingue toutes les pages en anglais, sans toucher aux pages en français.

Il a retiré près de 8 000 lignes en une passe. Le reste a demandé plus d'attention : des fichiers entiers à supprimer, deux fichiers de construction à réécrire (celui d'Albireo est passé de 864 à 307 lignes), et quelques fonctions dont le rôle n'existait que sous Windows.

## Les mises à jour qui auraient téléchargé du Windows

L'exemple le plus parlant est celui des mises à jour. Albireo et Pseudalis vérifiaient régulièrement, sur le site de l'auteur, si une nouvelle version existait, la téléchargeaient puis lançaient son installateur. Or cet installateur est un programme Windows. Sur le Mac, le service l'aurait téléchargé à chaque nouvelle version, pour refuser ensuite de l'appliquer : hors de Windows, le code laissait ce travail au gestionnaire de paquets du système, qui n'a jamais entendu parler d'Albireo. Le défaut ne se voyait pas, parce qu'on avait coupé les mises à jour dans nos réglages.

Elles sont parties, avec leurs routes, leurs réglages et, pour Pseudalis, la case à cocher de sa page web, traduite en cinq langues. Le catalogue des modèles, lui, reste : il ne contient rien de propre à Windows.

## Ce qui a été vérifié

Le bilan tient en deux nombres : 170 fichiers touchés, 37 566 lignes retirées pour 234 ajoutées, sans compter les installateurs du dossier `dist/`.

Après chaque morceau, les deux applications ont été recompilées et leurs tests relancés. Les scripts de test complets, écrits pour Linux, réclamaient deux outils absents du Mac, qu'on leur fournissait jusqu'ici par un détour. Ils ont été réécrits pour macOS. Et pour la première fois, celui d'Albireo passe en entier sur le Mac : vingt étapes sur vingt, du test des noyaux jusqu'à l'arrêt du service, en passant par une conversation et une phrase lue à voix haute. Celui de Pseudalis passe ses trente-quatre étapes.

La vitesse n'a pas bougé, ce qui était attendu : on a retiré du code qui ne servait pas, sans changer le calcul.

Au passage, le moteur décrit enfin la puce telle qu'elle est. Il cherchait des instructions de processeur Intel ; il annonce maintenant « Apple M2 », ses quatre cœurs rapides et ses quatre cœurs lents, et les extensions que macOS déclare. Parmi elles figure I8MM, une instruction qui multiplie des petits tableaux d'octets d'un coup et pourra servir un jour à la lecture des prompts.

## Pour les curieux : les chiffres

La commande de `unifdef` sur Albireo, en substance :

```
unifdef -m -U_WIN32 -U_MSC_VER -U__x86_64__ -U__AVX2__ -UALBIREO_QK_AVX2 -UALBIREO_QK_VNNI \
  -UDLIB_USE_CUDA -UDLIB_USE_NPU -UALBIREO_TENSOR_BACKEND -U__linux__ -D__APPLE__ <fichiers>
```

| Commit | Contenu | Lignes retirées |
|---|---|---|
| `5056eb5` | Albireo : fichiers Windows, CUDA, `dist/`, CMake, `unifdef` | 21 686 |
| `94c59b6` | Pseudalis : fichiers Windows, CMake, `unifdef` | 13 499 |
| `951e76e` | Albireo : mises à jour par installateur | 1 197 |
| `23cf2ca` | Pseudalis : mises à jour par installateur | 881 |
| `76540c2` | Albireo : description de la puce, réglage V²RAM de CUDA | 214 |
| `86a5d11` | Pseudalis : chargeur de moteur | 40 |
| `426317d` | Tests complets pour macOS | 64 |

Ce que dit maintenant le moteur de la machine :

```
{"type": "cpu", "name": "Apple M2", "cores": 8, "threads": 8, "memory": 25769803776,
 "features": ["DotProd", "I8MM", "BF16", "FP16"]}
```

## À suivre

Le chantier du moteur Metal reprend, sur une base plus simple. Sa première brique est déjà posée : les clés et valeurs de l'attention vivent désormais dans une seule grande zone de mémoire, que le GPU pourra lire sans copie, comme le processeur.
