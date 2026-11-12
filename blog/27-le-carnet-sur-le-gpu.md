# Dlib Transformer Extensions sur macOS, épisode 27 : le carnet sur le GPU

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B), inchangés depuis l'épisode 20 ; Qwen3.5-4B : écriture **19,5 tokens/s** sur le GPU (11,7 à 12,2 sur le processeur).

Suite de [l'épisode 26](26-le-carnet-de-notes.md) : Qwen 3.5 lisait et répondait juste, mais sur le processeur seul, deux fois moins vite que le modèle habituel du service. Cet épisode lui ouvre le GPU.

## Trois pièces qui manquaient

Le GPU du Mac savait déjà écrire les réponses des modèles classiques. Pour Qwen 3.5, il lui manquait trois choses.

- **Le carnet de notes lui-même.** La règle qui met à jour la mémoire de taille fixe des couches Gated DeltaNet devait être traduite pour le GPU, avec sa convolution, ses normes et sa porte. Sept petits noyaux s'en chargent.
- **Deux formats de poids.** Un fichier Qwen 3.5 « Q4_K_M » n'est pas tout en Q4_K : ses grosses projections linéaires sont en Q5_K, et ses petites projections d'oubli en Q8_0. Le GPU ne lisait que Q4_K et Q6_K. Il lit maintenant les deux autres.
- **Un carnet que le GPU puisse ouvrir.** Chaque conversation gardait son carnet dans un coin à elle de la mémoire du processeur. Les carnets sont désormais rangés dans une seule grande zone réservée, comme les fiches des autres couches. La mémoire du Mac étant partagée entre le processeur et le GPU, ce dernier la lit et l'écrit sans rien copier.

## Ranger le carnet pour une équipe

Sur le GPU, une tête de la couche est confiée à une équipe de 128 petites mains, une par colonne du carnet. Pour qu'elles travaillent vite, il faut qu'à chaque instant elles lisent des cases voisines, comme des élèves assis côte à côte qui lisent chacun le mot d'en face sur la même ligne du tableau. Le carnet a donc été rangé dans l'autre sens, ligne par clé plutôt que ligne par valeur. Le processeur s'en accommode aussi bien.

Ce changement de rangement a eu un effet visible sur le petit modèle de 0,8 milliard : trois de ses réponses de test ont changé de mot en cours de route. Avant de s'en inquiéter, on les a comparées à nouveau à la référence llama.cpp. Les trois bifurcations tombent sur des quasi-égalités du modèle : 0,464 contre 0,446, 0,094 contre 0,085, 0,291 contre 0,271. Additionner les mêmes nombres dans un autre ordre déplace le dernier chiffre, et un modèle qui hésite au centième près bascule d'un côté ou de l'autre. Le modèle de 4 milliards, lui, n'a pas bougé d'un caractère.

## Du premier coup, mais vérifié

Les 36 noyaux du GPU se sont compilés au premier lancement, et le contrôle que fait le moteur à chaque chargement a donné son verdict : logits à 0,03 % et 0,04 % de ceux du processeur, même mot le plus probable. On ne s'en est pas contenté. Sur le GPU, la même question donne le même texte que sur le processeur. Trois questions traitées ensemble donnent les mêmes réponses qu'une à une. Et une conversation de trois tours donne les mêmes réponses avec et sans reprise du carnet.

Au passage, le modèle de 4 milliards fait mieux que le petit : à « What is my name? », après « My name is Léa », il répond « Your name is **Léa**. », puis l'épelle à l'envers : « Aél ».

## Un garde-fou

La grande zone des carnets a une taille fixée à l'avance : 64 conversations au plus. Pour qu'un réglage poussé à l'extrême ne la déborde pas, un modèle Qwen 3.5 traite au plus 16 questions à la fois et garde au plus 32 conversations en cache. Les réglages habituels du service, 4 questions à la fois, en sont loin.

## Pour les curieux : les mesures

Écriture de 200 tokens par Qwen3.5-4B Q4_K_M, mode économie d'énergie coupé, charge 4,1 à 4,9, mesures alternées :

| Essai | Processeur | GPU |
|---|---|---|
| 1 | 12,2 tokens/s | 19,5 tokens/s |
| 2 | 11,7 tokens/s | 19,5 tokens/s |

Le GPU écrit 1,6 fois plus vite. Le Qwen3-VL de 4 milliards que le service utilise écrit à 21 ou 22 tokens/s : Qwen 3.5 en est maintenant proche.

Le contrôle de chargement, tel que le moteur l'écrit dans son journal :

```
GPU Apple M2: the generation steps run on it (step 1: logits within 0.0329637% of those of the processor,
most likely token the same; step 2: logits within 0.0390318% of those of the processor, most likely token the same)
```

## À suivre

La lecture des prompts de Qwen 3.5 reste sur le processeur ; la porter au GPU demande de développer les poids Q5_K et Q8_0 en floats, comme pour les autres formats. Ensuite viendra la variante à experts, Qwen 3.6.
