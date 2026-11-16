# Pseudalis sur macOS, épisode 3 : les colonnes désignées

> **Repère de débit** (MacBook Air M2, mode économie d'énergie coupé, charge 2,5 à 3,4) : protection d'un texte de 30 000 octets en **1,44 à 1,46 s**, soit **20,5 à 20,9 Ko/s**. Le texte d'essai n'est pas celui de l'épisode 2 : les deux chiffres ne se comparent pas.

Suite de [l'épisode 2](02-des-pseudonymes-sans-retour.md), où les pseudonymes de Pseudalis étaient devenus sans retour. Entre-temps, l'original a continué d'avancer sous Windows, de la version 1.0.6 à 1.0.11. On en a repris ce qui sert au Mac.

## Le ménage d'abord

La version 1.0.6 de l'original touchait surtout à Windows : l'icône de la barre des tâches, la langue du navigateur. Le fork n'a ni l'une ni l'autre, puisqu'il ne parle que français. Elle retirait aussi du code que plus rien n'appelait, comme on vide un placard des outils d'un ancien bricolage.

Le fork en gardait encore dix-sept fonctions, dont chacune n'apparaissait que deux fois dans tout le programme : sa déclaration et sa définition, jamais un appel. On les a retirées, 451 lignes, sans aucun changement visible. Le compilateur a d'ailleurs signalé lui-même deux fonctions devenues orphelines en chemin.

## La feuille de calcul qu'on connaît déjà

Les versions 1.0.8 à 1.0.10 apportent une idée simple et utile. Un service qui exporte souvent les mêmes fichiers CSV, une liste de clients ou un journal de connexions, sait d'avance où se trouvent les données personnelles : la colonne « Nom », la colonne « Courriel », la colonne « ip_address ». Plutôt que de laisser le détecteur deviner, cellule par cellule, on peut lui désigner ces colonnes.

C'est le principe des **colonnes imposées**. L'administrateur nomme une colonne et son type, et chaque valeur de cette colonne prend le pseudonyme de ce type, sans hésitation. C'est un peu comme une enveloppe déjà marquée « confidentiel » : on n'a pas à l'ouvrir pour savoir qu'il faut la protéger. Pseudalis cherche l'en-tête parmi les seize premières lignes, même sous un titre ou sous la ligne `sep=` qu'ajoute Excel, sans tenir compte des majuscules ni des accents. Les valeurs ainsi trouvées sont aussi reconnues ailleurs dans le fichier.

Une colonne d'adresses IP prend les deux formes à la fois, IPv4 et IPv6, mélangées comme une base de données les exporte. Chaque adresse reçoit un pseudonyme de sa propre forme, et garde sa longueur de préfixe (`/8`), ses crochets et son port (`:443`) : un administrateur réseau reconnaît encore la structure de son fichier, sans y voir les vraies adresses.

## Ce qui change dans dlib

Les détecteurs de Pseudalis vivent dans dlib, dans le dossier `privacy`. Le cœur de la fonction s'y trouve donc : la lecture de l'en-tête, la colonne IP, et une petite finesse d'encodage. Un fichier écrit à l'origine en Windows-1252, dont les seules lettres accentuées se trouvaient dans une colonne protégée, devient du pur ASCII une fois protégé ; Pseudalis note l'encodage d'origine, pour qu'une restauration le rende tel qu'il était.

Ce code a été repris tel quel de l'original : il s'est appliqué sans conflit, par-dessus les changements que le fork avait déjà faits dans dlib. Les conflits sont venus de Pseudalis lui-même, où le fork a beaucoup changé : sa clé jetable, ses commentaires en français, sa page refaite. On les a résolus un par un. On y a gardé, par exemple, les nouvelles fonctions des colonnes, mais pas l'export ni l'import de la clé, que l'original garde et que le fork a retirés.

Dans le fork, les valeurs des colonnes imposées reçoivent des pseudonymes sans retour, comme tout le reste depuis l'épisode 2. Seul l'original conservé rend le fichier d'avant.

## Pour les curieux : les chiffres et les fichiers

- **Pseudalis 3.1.1** : dix-sept fonctions retirées (`copy_file`, `run_process`, `http_resolve`, `compare_versions`…) et la structure `process_options`, dans `common/platform.*`, `host/{certificates,crypto,http_fetch,store}.*` et `engine/engine.*`.
- **Pseudalis 3.2.0** : `common/imposed_columns.h` (nouveau), routes `GET` et `PUT /v1/columns`, `service::set_columns()`, section « Colonnes imposées » de la page (`web/index.html`, `web/js/app.js`, `web/js/i18n.js`).
- **dlib**, repris de l'original (commits `61da899`, `b2d9bf4`, `1c9318d`, `5408cbc`) : `privacy/document_anonymizer.h`, `document_anonymizer_abstract.h`, `document_report.h`, `document_report_abstract.h`, `pii_detector.h`, `pii_detector_abstract.h`, et `test/privacy.cpp`. Le test de confidentialité de dlib passe de 1 254 à 1 377 vérifications.
- Tests : test complet de Pseudalis, 37 sur 37, avec deux étapes nouvelles, adaptées aux pseudonymes sans retour (colonnes imposées sous un titre, colonne IP des deux formes) ; tests du moteur et du service.
- Les commentaires anglais du code repris ont été traduits en français par un agent ; le code est resté identique, vérifié jeton par jeton.

## À suivre

La version 1.0.11 de l'original : des détecteurs réentraînés, qui reconnaissent mieux les noms seuls dans les cellules, les numéros de médecins ou les dates d'expiration d'une carte.
