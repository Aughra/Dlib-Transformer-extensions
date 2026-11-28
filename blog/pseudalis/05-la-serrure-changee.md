# Pseudalis sur macOS, épisode 5 : la serrure changée sans prévenir

> **Repère de débit** : inchangé depuis [l'épisode 4](04-des-detecteurs-reentraines.md) (MacBook Air M2) : protection d'un texte de 30 000 octets en **1,46 à 1,48 s**, soit **20,3 à 20,6 Ko/s**. Pas de nouvelle mesure à cette étape.

Suite de [l'épisode 4](04-des-detecteurs-reentraines.md) : des détecteurs réentraînés. À cette étape, on voulait seulement retirer le Python des outils de Pseudalis, comme on l'avait fait pour Gulliver. En lançant le test, on a trouvé que Pseudalis ne démarrait plus du tout.

## Le service qui refuse de démarrer

Le premier essai s'est arrêté net, avant même la première question : « the models of the engine cannot be read: Error deserializing object of type bool ». Pseudalis trouve les données personnelles avec deux modèles appris, rangés dans le programme lui-même. Il ne savait plus les relire.

La cause n'était pas dans Pseudalis, mais dans dlib, la bibliothèque sur laquelle il repose et que le fork modifie pour Gulliver. Quelques étapes plus tôt, le 20 novembre, pour faire tourner Gemma 3, on avait appris à la couche d'attention une « fenêtre glissante ». Ce réglage s'enregistre avec la couche, à la fin de sa fiche. Deux autres réglages avaient suivi de la même façon.

Le défaut est là : la fiche gardait la même étiquette. C'est comme changer la serrure d'une porte en gardant la même clé dans le trousseau. Les modèles de l'original, écrits avant ces ajouts, avaient l'ancienne fiche. En les relisant, dlib cherchait les nouveaux réglages à la place du début de la couche suivante, et tout se décalait. L'erreur est facile à commettre, et on l'a commise : la note du registre disait même que « les anciennes archives de cette couche ne se relisent plus », sans voir que Pseudalis en embarquait deux.

## Deux étiquettes

La correction tient en quelques lignes. La couche relit l'ancienne fiche telle qu'elle est, au format de l'original, et donne aux nouveaux réglages leur valeur par défaut. Elle écrit désormais une fiche à part, sous une étiquette à elle, `gqa_attention_2`. Les modèles de l'original se relisent de nouveau, et ceux du fork gardent leurs réglages. Le test du moteur de Pseudalis est revenu au vert, puis son test complet : 37 étapes sur 37.

## Le Python, aussi

Le travail prévu a suivi.

- **Le numéro de version.** Le petit outil qui le change aux trois endroits où il est écrit est passé en C++. On l'a lancé à côté de l'ancien sur cinq changements de suite, et les fichiers sont restés identiques.
- **Le test complet.** Il contenait 27 commandes Python : lire une réponse JSON, encoder un nom pour une adresse, vérifier une séance du relais, fabriquer et lire des archives ZIP. Elles passent maintenant par un seul programme d'essai, en C++.
- **Les archives du test.** Le programme les fabrique avec son propre code, et la zlib de macOS, plutôt qu'avec celui de Pseudalis. On ne fait pas vérifier une serrure par celui qui l'a posée. L'utilitaire `unzip` de macOS les a d'ailleurs lues sans erreur.
- **L'outil des images.** Il fabriquait les icônes Windows et les images de l'installateur de l'original. Il a été retiré : la version pour Mac ne s'en sert pas.

## Pour les curieux : les chiffres et les fichiers

- Version 3.4.0. Programmes : `build/macos-pseudalis/tools/bump_version` et `build/macos-pseudalis/tools/essai`.
- L'erreur au démarrage, avant la correction :

  ```
  [error] service: the models of the engine cannot be read: Error deserializing object of type bool
  ```

- Ce qui change dans dlib : `dlib/dlib/dnn/transformer.h` et `transformer_abstract.h`. La couche `gqa_attention_` lit les étiquettes `gqa_attention_` (le format de l'original) et `gqa_attention_2` (le format du fork), et n'écrit plus que la seconde. Les tests de réseaux de dlib passent : 794 565 vérifications, sans échec.
- Fichiers de Pseudalis : `tools/bump_version.cpp`, `tools/essai.cpp`, `tools/test_macos.sh`, `CMakeLists.txt` ; `tools/bump_version.py` et `tools/make_images.py` supprimés.

## La suite

Avec cette étape, il ne reste plus de Python dans les outils du fork, à part les juges de la justesse (PyTorch et MLX), gardés par décision. Le prochain épisode de Pseudalis viendra avec sa prochaine fonction.
