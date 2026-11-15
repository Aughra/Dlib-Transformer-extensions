# Dlib Transformer Extensions sur macOS, épisode 46 : l'étagère du Mac

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B, Gemma 4 E2B sur le GPU, les images de Gemma 3 et Gemma 4 26B-A4B : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 45](45-l-orchestre-et-les-solistes.md) : Gemma 4 26B-A4B tournait. Mais les modèles des épisodes 44 et 45 avaient été téléchargés à la main, dans le dossier des essais, et Albireo ne les voyait pas.

## Deux boutons, deux métiers

L'onglet Modèles avait déjà deux boutons : « Rafraîchir depuis Hugging Face » et « Rafraîchir depuis le Mac ». Le second ne relisait que ce qu'Albireo savait déjà : sa liste, sa réserve, ses dernières recherches. Un fichier arrivé sur le Mac par un autre chemin restait invisible. Ce n'était pas l'idée : un bouton va chercher les modèles sur Internet et les pose sur le Mac, l'autre regarde ce qu'il y a sur le Mac et permet de le charger dans Albireo, même quand Hugging Face ne répond plus.

Albireo avait bien un dossier d'import, mais c'est une boîte aux lettres : au démarrage, le service importe tout ce qu'il y trouve et la vide. On voulait plutôt une étagère qu'on regarde avant de choisir.

## L'étagère

La bibliothèque du Mac est un dossier de fichiers GGUF, réglable dans l'onglet Service, que le service lit sans jamais y écrire. « Rafraîchir depuis le Mac » la montre dans une nouvelle section, « Sur ce Mac » : chaque modèle avec son architecture, sa taille, le projecteur d'images qui va avec lui, et la mention « déjà dans Albireo » quand c'est le cas. Ceux que le moteur ne sait pas lire sont en gris.

« Charger dans Albireo » installe le modèle et son projecteur. La copie est un clone du système de fichiers d'Apple : comme une photocopie qui partagerait l'encre de l'original, elle ne prend aucune place tant qu'aucun des deux fichiers ne change. Gemma 3 4B et son projecteur ont été chargés en 20 secondes, le temps de vérifier leurs empreintes, et la place libre du disque n'a pas bougé d'un octet.

## Le mauvais projecteur

En remplissant l'étagère du vrai service avec les modèles des essais, la liste a montré une erreur : Gemma 3 4B se voyait attribuer le projecteur de Qwen 3.5 4B, et son propre projecteur restait seul. Les deux projecteurs sortent des vecteurs de même largeur, 2 560 valeurs, et la règle d'appariement, reprise de l'original, ne comparait que cette largeur, puis la ressemblance des noms. Le projecteur de Qwen, qui n'avait qu'un candidat, l'a pris le premier. C'est comme deux clés de même taille pour deux serrures différentes : il faut aussi regarder la marque. Un projecteur d'un type connu ne va plus qu'avec un modèle de sa famille, et le test du service vérifie désormais ce cas.

## Les fiches relues

On a aussi relu toutes les fiches de l'onglet Service, celles qui expliquent chaque réglage avec le hibou, la marmotte et l'abeille. Chaque réglage avait bien sa fiche, mais trois disaient des choses devenues fausses : la fiche du GPU annonçait encore que Gemma 4 lit ses prompts sur le processeur, ce qui n'est plus vrai depuis l'épisode 43. Plus ennuyeux, l'avis donné sur chaque modèle dans l'onglet Modèles affirmait qu'un mélange d'experts tourne sur le processeur seulement, alors que ceux de Qwen 3.6 et de LFM2 passent par le GPU depuis longtemps. Seuls les experts de Gemma 4 restent sur le processeur.

Au passage, une petite erreur d'addition a été trouvée dans le mémo : le banc d'essai de la page faisait 112 vérifications, pas 113. Il en fait 114 avec les deux nouvelles, qui déposent un petit modèle sur l'étagère puis le chargent.

## Pour les curieux : les chiffres et les fichiers

- Réglage `library_dir` (par défaut `<données>/library`) ; routes `GET /albireo/v1/library` et `POST /albireo/v1/library/load` ; fichiers lus sans lien et par un seul nom, dossier refusé si d'autres comptes du Mac peuvent y écrire.
- Fichiers : `host/{settings,server,service,models,hub}.*`, `web/js/{models,service}.js`, `tools/make_openapi.py`, `tools/page_test/`.
- Appariement : `projector_fits()` dans `pair_projectors()` (`gemma3`, `gemma4v`, `qwen3vl_merger`) ; nouveau contrôle de `test_host`.
- Banc de la page : 114 vérifications (regress 45, docs 12, images 12, actions 26, tabs 11, phone 8).
- Albireo passe en **4.30.0**, puis **4.30.1** (projecteurs) et **4.30.2** (fiches). Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Le texte n'est pas fini : un défaut rare de Qwen 3.5 sur le GPU, le format de Mistral Small 3.2, et des boutons de la page qui devraient suivre ce que chaque modèle sait faire. Mais d'abord, les pages des deux applications ([épisode 47](47-les-deux-vitrines.md)).
