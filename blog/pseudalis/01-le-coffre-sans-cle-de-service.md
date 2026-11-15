# Pseudalis sur macOS, épisode 1 : le coffre sans clé de service

> **Repère de débit** (MacBook Air M2, mode économie d'énergie coupé, charge 3,1 à 3,7) : protection d'un texte de 30 000 octets en **1,44 à 1,47 s**, soit **20 à 21 Ko/s**.

Pseudalis avait, jusqu'ici, une place discrète dans [le blog du portage](../README.md) : il partage le dépôt d'Albireo, et son histoire s'est racontée en marge de celle du moteur. Il a désormais son propre carnet. Ce premier épisode rattrape le chemin parcouru.

## Ce que fait Pseudalis

Pseudalis protège les données personnelles des documents. Dans un contrat, un compte rendu ou un tableur, chaque nom, adresse, numéro de téléphone, identifiant ou date est remplacé par une balise, comme `[[PERSON:...]]`, qui garde la valeur chiffrée. Seule la clé de l'ordinateur sait la rendre. On peut alors confier le document protégé à un tiers, ou à un modèle de langage, sans lui livrer les personnes qu'il cite. Le fichier garde sa mise en page : un Word reste un Word, un PDF garde ses pages.

C'est un peu un notaire qui caviarde un dossier : il noircit les noms avant de le photocopier, mais garde dans son coffre de quoi les remettre.

## Le portage, du 6 au 15 novembre

Pseudalis a suivi Albireo pas à pas :

- **6 novembre** : il compile sur le Mac, avec deux retouches (le chemin du programme, les dates des fichiers) ;
- **8 novembre** : son test complet passe, 28 étapes sur 28 ;
- **11 novembre** : tout ce qui était propre à Windows part (l'installateur, l'icône de la barre des tâches, les commandes de l'Explorateur, les mises à jour automatiques), avec les langues autres que le français et l'anglais ;
- **12 et 13 novembre** : les contrôles de dossiers vérifient vraiment le propriétaire et les droits, là où l'original, hors de Windows, répondait toujours « oui » ;
- **14 novembre** : le test complet attendait parfois un temps fixe au lieu du travail fini ; le lecteur dit maintenant quand il a fini, et le test passe à 34 étapes sur 34.

## Le jeton qui n'ouvrait rien

Sous Windows, les sections les plus sensibles de Pseudalis (les originaux conservés, la clé, les réglages) ne s'affichent qu'avec un jeton d'administration, que l'icône de la barre des tâches glisse dans l'adresse de la page. Sur le Mac, il n'y a plus d'icône : ces sections restaient donc invisibles, comme un coffre dont la clé serait restée dans l'ancien bureau.

On a d'abord écrit une petite commande pour ouvrir la page avec le jeton. L'humain a tranché autrement : plus de jeton du tout, comme pour Albireo, dont les routes s'ouvrent sur le Mac sans mot de passe. Le prix est connu et noté : sur ce Mac, tout programme qui joint le service peut lire les originaux. Restent les gardes qui empêchent un site web de piloter le service par le navigateur.

## Un nouveau rangement

L'application a été réorganisée. Au lieu d'une longue page à faire défiler, un menu à gauche mène à une section à la fois : protéger des fichiers, restaurer, protéger un texte, et désormais toujours les originaux, la clé, les réglages et les termes imposés. Elle s'affiche en français par défaut, avec un thème sombre. Le menu montre en direct le débit du moteur, en kilo-octets lus et écrits par seconde, et un sélecteur mène à Albireo quand il tourne sur le même Mac. Le README de l'original est traduit en français.

## Pour les curieux : les chiffres et les fichiers

- Pseudalis passe en **2.0.0** : le fork le numérote désormais à part de l'original (dont la dernière version fusionnée est la 1.0.5), avec ses [notes de version](../../dlib/dlib/external/pseudalis/web/changelog.md) ; `tools/bump_version.py` fixe la version.
- Plus de jeton : la vérification de `host/server.cpp` laisse tout passer, le service ne crée plus `admin.token`, la page n'attend plus de fragment `#admin=`, et `web/openapi.json` perd son schéma de sécurité (13 opérations).
- Débit : `engine_client::speed()` (`common/engine_loader.cpp`), route `GET /v1/speed`.
- Tests : 34 étapes sur 34 avec `PSEUDALIS_TEST_PORT=18474`, le vrai service occupant 18444.
- Les documents de Pseudalis : [guide](../../docs/pseudalis/guide.md), [notes pour l'auteur](../../docs/pseudalis/notes-pour-l-auteur.md), [différences avec Windows](../../docs/pseudalis/differences-mac-windows.md).

## À suivre

Mesurer Pseudalis sur de vrais documents, un Word, un PDF de plusieurs pages, une archive, maintenant que le Mac n'est plus bridé.
