# Dlib Transformer Extensions sur macOS, épisode 69 : le sceau et la langue unique

> **Repère de vitesse** (Mac mini M5 Pro) : inchangé depuis [l'épisode 68](68-la-tete-trop-large.md) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384.

Suite de [l'épisode 68](68-la-tete-trop-large.md), qui s'achevait sur deux chantiers annoncés : la signature des modèles et l'écriture au bout d'une longue conversation. Le premier a commencé à cette étape. Entre-temps, l'humain a pris une décision qui touche tout le dépôt : une seule langue de programmation, le C++.

## Le sceau qu'on avait brisé

Gulliver choisit ses modèles dans un catalogue, une liste publiée par l'auteur de l'original, avec pour chaque fichier son adresse, sa taille et son empreinte. L'empreinte vérifie le fichier téléchargé, comme le numéro de série d'un billet. Mais qui vérifie le catalogue lui-même ? Si quelqu'un pouvait glisser une fausse liste, il pourrait y mettre ses propres fichiers avec leurs propres empreintes, et tout paraîtrait en règle.

L'original avait la réponse : il signe son catalogue. La signature est un sceau de cire numérique : seul l'auteur possède le cachet, mais tout le monde peut reconnaître son empreinte grâce à une clé publique, publiée avec le programme. Un catalogue modifié d'une seule lettre ne correspond plus au sceau.

Le portage avait retiré ce sceau, onze jours plus tôt, en version 4.0.0. L'humain voulait alors un fork « sans clé », et la vérification s'en est allée avec le reste. À cette étape, il a demandé l'inverse : « il faut que les modèles vérifient leur signature ». On a donc remis le sceau, avec la même clé publique que l'original, mais en confiant la vérification à macOS lui-même, par son framework Security, plutôt qu'à la bibliothèque qu'utilise l'original.

Le premier essai s'est fait sur le vrai catalogue de l'auteur, publié le 20 novembre : sceau reconnu. Le même fichier modifié d'un seul bit : refusé. Un catalogue signé par une autre clé, ou sans signature du tout : refusé, et Gulliver garde le précédent. Un paquet de modèles apporté sur une clé USB, dont le catalogue n'est pas signé, n'installe plus rien. L'onglet Modèles affiche maintenant « (signé par l'original) » à côté de la date du catalogue, et c'est ce qu'il affiche sur le Mac de l'humain depuis le redémarrage du service.

Un détail a demandé réflexion. Pour tester le service entier, il faut des catalogues signés, et personne ne possède le cachet de l'auteur à part lui. Le service de test reçoit donc une clé de test par une variable d'environnement. Une telle porte dérobée inquiète à juste titre ; on l'a rendue bruyante plutôt que secrète : si elle servait un jour sur le vrai service, son journal le dirait, et la page afficherait « ATTENTION : signatures vérifiées avec une clé de TEST ». Qui peut changer cette variable peut de toute façon changer le programme lui-même : elle n'ouvre aucune porte de plus, mais elle ne passera pas inaperçue.

Ce n'est que la première moitié du chantier. Les modèles venus d'ailleurs, de Hugging Face ou d'un fichier envoyé, n'ont pas de sceau de l'auteur. Ils seront bloqués jusqu'à ce que l'humain les valide, et sa validation sera scellée à son tour par une clé gardée dans la puce de sécurité du Mac.

## Une seule langue

L'humain l'a dit en peu de mots : « tout en C++, plus de Python ». Le dépôt comptait environ vingt-cinq outils en Python pour Gulliver, plus les exemples d'entraînement de dlib. Remplacer d'un coup tant de code serait le meilleur moyen de casser ce qui marche ; on avance donc un outil à la fois, et chacun doit donner exactement les mêmes résultats que l'ancien avant de le remplacer.

La première marche était la norme du langage : Gulliver passe de C++14 à C++23, neuf ans plus jeune. Le compilateur n'a relevé qu'une chose, à trois endroits : on combinait deux sortes d'options de CoreGraphics dans une même opération, ce que les versions récentes du langage signalent, comme on refuserait d'additionner des pommes et des kilomètres. La correction tient en un mot par endroit, et la justesse du moteur ne bouge pas d'un chiffre.

Viennent ensuite deux outils qui servaient à chaque version. Le premier change le numéro de version aux quatre endroits que le build compare ; sa version en C++ a été essayée sur une copie, montée puis redescendue, et redonne les fichiers à l'octet près. Le second régénérait la description de l'interface du service, un long document de 1 800 lignes de Python. On a pris le chemin inverse : la description devient le document de référence, écrit à la main, et un test en C++ vérifie qu'elle répond au code, route par route.

Ce test a trouvé dès son premier passage quatre routes que personne n'avait décrites : celles des « index nommés » d'un profil, ajoutées quelques semaines plus tôt. L'ancien outil ne pouvait pas les voir, puisqu'il décrivait ce qu'on lui disait de décrire. Le test a aussi pris en faute son propre auteur : un renvoi vers une rubrique qui n'existait pas, dans les lignes écrites pour ces quatre routes. On a ajouté au test un contrôle des 288 renvois du document, pour que l'erreur ne revienne pas.

Par curiosité, on a fait passer le même test sur le code de l'original : ses 36 routes sont toutes décrites. Le mérite de cette rigueur lui revient.

## Une deuxième session à l'établi

Une autre session de travail, appelée Temper, porte de son côté un module de notation des modèles en cybersécurité : seize questions-pièges réparties en cinq familles, qui disent si un modèle se laisse détourner de ses consignes. Elle porte aussi le conseil du lundi, l'outil qui choisit chaque semaine le meilleur modèle.

La règle entre les deux est celle d'un atelier partagé : Temper travaille sur son propre établi et ne touche pas au dépôt ; cette session relit, vérifie et range. Les premiers modules sont entrés à cette étape, dans un dossier à eux, avec six tests. Le cœur du conseil, réécrit en C++, a été comparé à l'outil Python sur une vraie mesure, celle du 24 novembre : il désigne les mêmes modèles, avec les mêmes chiffres. Il ne remplace pas encore l'ancien outil, car il lui manque la page de la semaine ; il le remplacera quand il saura tout faire.

## Pour les curieux : les chiffres et les fichiers

- Versions : 7.13.1 (C++23), 7.14.0 (signature du catalogue), 7.15.0 (banc de Temper, première partie), 7.16.0 (outils de version en C++). Test complet : 71, puis 77, puis 78 étapes, sans échec.
- La vérification de la signature : ECDSA sur la courbe P-256 et SHA-256, signature au format DER en base64 (`catalog.json.sig`). Le point public de 65 octets est tiré de la clé PEM de l'original, puis `SecKeyCreateWithData` et `SecKeyVerifySignature` avec `kSecKeyAlgorithmECDSASignatureMessageX962SHA256`. L'essai :

  ```
  vraie: 1, modifiee: 0, aller-retour pem: 1
  ```

- Les refus, tels que les note le test :

  ```
  ok   a catalog changed after its signature is refused, and the one in use stays
  ok   a catalog signed by another key is refused
  ok   a catalog without signature is refused
  ```

- L'outil de version : `build/macos-gulliver/tools/bump_version patch|minor|major`, à la place de `python3 tools/bump_version.py`.
- Le test de la description : `build/macos-gulliver/tests/test_openapi dlib/dlib/external/gulliver`. Avant l'ajout des quatre routes :

  ```
  FAIL the route get /gulliver/v1/profile/indexes is in web/openapi.json
  FAIL the route post /gulliver/v1/profile/indexes is in web/openapi.json
  ```

- Les trois appels corrigés pour C++23 : `CGBitmapContextCreate` dans `engine/image_input.cpp`, `host/pdf_render.cpp` et `tests/test_host.cpp` ; la combinaison passe par `CGBitmapInfo`. Justesse de Gemma 4 E2B sur le GPU : 69,686, avant comme après.
- Fichiers principaux : `host/crypto.{h,cpp}`, `host/models.{h,cpp}`, `host/service.cpp`, `web/js/models.js`, `bench/` (nouveau), `tools/bump_version.cpp`, `tests/test_openapi.cpp` ; `tools/bump_version.py` et `tools/make_openapi.py` supprimés.
- dlib n'est pas touché.

## La suite

La signature continue : chaque modèle installé recevra un état, « signé » ou « non signé », et les modèles non signés attendront la validation de l'humain, scellée par la puce de sécurité du Mac. Le Python continue de reculer, outil par outil, de part et d'autre de l'atelier.
