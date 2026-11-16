# Dlib Transformer Extensions sur macOS, épisode 49 : les étages qui ne servent à personne

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Gemma 4 E2B : lecture de 512 tokens **671 à 685 tokens/s** sur le GPU et **463 à 471** sur le processeur, trois fois plus qu'avant cet épisode.

Suite de [l'épisode 48](48-le-messager-et-les-surnoms.md) : Albireo avait appris à confier une discussion à un moteur extérieur. Retour au moteur du Mac, et à un gain qu'on attendait depuis l'arrivée de Gemma 4.

## Un immeuble de 35 étages

Un modèle de langage lit un texte comme un immeuble que chaque mot traverse de bas en haut. À chaque étage, une couche, le mot regarde les mots précédents et se transforme un peu. Pour regarder les mots précédents, chaque étage garde de chacun une petite fiche, ses clés et ses valeurs : les mots suivants les relisent sans tout recalculer.

Gemma 4 E2B a 35 étages, et une particularité. Ses 20 derniers étages ne rédigent pas de fiches : ils relisent celles d'un étage plus bas. Ce qu'ils calculent pour un mot ne sert donc à aucun autre mot. Cela ne sert qu'au mot dont on veut la suite, c'est-à-dire, quand le modèle lit une question, le dernier.

Le moteur faisait pourtant monter tous les mots jusqu'au dernier étage. Pour une question de 512 tokens, 511 tokens gravissaient vingt étages pour rien. C'est un peu comme faire monter tout le public d'une conférence au dernier étage, alors que seul l'orateur y a rendez-vous.

## L'ascenseur réservé

Le changement est simple à dire. À l'entrée du premier étage sans fiches, on ne garde que les lignes dont la réponse a besoin, et la suite de la lecture ne travaille que sur elles. Une longue question se lit en plusieurs morceaux, et seul le dernier demande une suite : les autres morceaux s'arrêtent tout bonnement au quinzième étage. llama.cpp fait de même, et on lui doit l'idée.

Sur le processeur, il suffit de ranger ces lignes en tête et de continuer. Sur le GPU, c'est un peu plus délicat, car le GPU reçoit son travail par lots de commandes écrites d'avance. On exécute donc le lot jusqu'au quinzième étage, puis le processeur range les lignes gardées directement dans la mémoire du GPU. Sur un Mac, c'est la même mémoire pour les deux : pas de déménagement, seulement un rangement sur place. Puis le GPU reprend, avec moins de lignes.

## Les mêmes réponses, trois fois plus vite

Une optimisation qui change les réponses n'en est pas une. On a donc comparé l'ancien moteur et le nouveau, reconstruit à part pour l'occasion, sur une question courte et sur un journal de 2 823 tokens dont il faut retrouver un mot de code. Sur le processeur, les réponses sont identiques au token près. Sur le GPU aussi ; seul le contrôle fait au chargement bouge d'un cheveu, parce que les lignes gardées passent par un autre calcul, sans changer le mot choisi.

Le gain, mesuré en alternance, ancien moteur puis nouveau, deux fois chacun :

| Gemma 4 E2B, lecture | Avant | Après |
|---|---|---|
| 512 tokens, processeur | 154 à 156 tokens/s | 463 à 471 |
| 512 tokens, GPU | 233 à 237 | 671 à 685 |
| 4 096 tokens, processeur | 129 à 134 | 371 à 383 |
| 4 096 tokens, GPU | 93 à 95 | 241 à 249 |

Environ trois fois plus vite. L'écriture des réponses, elle, ne change pas : à chaque mot écrit, il n'y a qu'une ligne, et elle est utile.

Une curiosité reste pour le Mac mini : sur 4 096 tokens, le processeur du M2 lit Gemma 4 plus vite que son GPU. Les grandes têtes de Gemma 4 obligent le GPU à calculer ses scores deux fois (épisode 43). La question de savoir qui doit lire les longs prompts de Gemma 4 attend les mesures de la nouvelle machine.

## Pour les curieux : les chiffres et les fichiers

- Processeur : `cpu_runtime::narrow_rows()` dans `engine/cpu_runtime.cpp`. Les états cachés, les entrées par couche et les positions des lignes de `logit_rows` passent en tête, et l'attention reçoit un lot d'un segment d'un token par ligne, aux mêmes positions absolues.
- GPU : dans `run_gpu` (`engine/cpu_runtime_metal.cpp`), `c->finish()` à l'entrée de la première couche partagée, puis resserrement en place des tampons (états, cosinus et sinus, longueurs, blocs, table des blocs, entrées par couche des couches restantes), une tuile d'attention par ligne, et les produits par blocs des étapes pour huit lignes au plus.
- Mesures : `bench_runtime gemma-4-E2B-it-Q4_K_M.gguf 8 <512|4096> 16 1 q8 [metal]`, mode économie d'énergie coupé.
- Tests : réponses comparées à l'ancien moteur sur le processeur et le GPU ; test complet d'Albireo, 60 sur 60 (images, outils, 26B-A4B et long journal de Gemma 4 compris) ; `test_metal` passe.
- Albireo passe en **5.5.0**. Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

La suite de la liste faite en attendant le Mac mini : une panne rare de Qwen 3.5 sur le GPU, des boutons qui suivent ce que chaque modèle sait faire, Mistral Small 3.2, de nouveaux noyaux pour le processeur.
