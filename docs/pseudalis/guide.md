# Pseudalis sur Mac — guide

Pseudalis protège les données personnelles des documents : chaque nom, adresse, numéro, identifiant ou date est remplacé par une balise que seule la clé de cet ordinateur sait rendre. Ce guide décrit la version du fork pour macOS à puce Apple (Pseudalis **2.0.0**) : la construire, la lancer, s'en servir, la tester. Il suit le code ; les choix et l'historique sont dans les [notes de version](../../dlib/dlib/external/pseudalis/web/changelog.md), le [tableau des différences avec Windows](differences-mac-windows.md) et le [blog de Pseudalis](../../blog/pseudalis/README.md).

Pseudalis vit dans le même dépôt qu'Albireo et repose, comme lui, sur dlib : sur [`dlib/privacy`](../../dlib/dlib/privacy.h) et sur les modèles de reconnaissance de l'exemple `slm_document_anonymizer_ex`, compilés dans le service.

## Construire

```sh
cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis
```

La construction donne `build/macos-pseudalis/bin/pseudalis-service`, l'application web à côté de lui (`bin/web`, avec la page de l'interface `bin/web/docs`, dont Swagger UI, la feuille de style et le thème viennent de la copie d'Albireo du dépôt), et les tests `test_engine` et `test_host`. Elle ne doit produire aucun avertissement.

La version se change par `python3 dlib/dlib/external/pseudalis/tools/bump_version.py patch|minor|major` (`common/version.h`, `project()` du `CMakeLists.txt`, `web/openapi.json`) ; chaque version a sa section dans les notes de version.

## Lancer

```sh
(PSEUDALIS_HOME=$PWD/build/pseudalis-home ALBIREO_HOME=$PWD/build/albireo-home \
  nohup build/macos-pseudalis/bin/pseudalis-service --console > build/pseudalis-home/logs/console.log 2>&1 &)
```

- `PSEUDALIS_HOME` est le dossier de données ; sans lui, `~/.local/share/pseudalis`. Attention : le service n'a pas d'option `--help`, et tout lancement sans `PSEUDALIS_HOME` crée un dossier de données par défaut, avec une clé neuve.
- `ALBIREO_HOME` dit où trouver Albireo, qui décrit les images des documents (voir plus bas) ; sans lui, `~/.local/share/albireo`.
- Le port vient de `settings.json` : 8444 par défaut, mais c'est aussi le port par défaut de l'accès réseau d'Albireo. Sur ce Mac, le dossier `build/pseudalis-home` règle **18444**.
- Arrêt : `kill -INT` ou `kill -TERM` sur le service ; il s'arrête proprement.

L'application s'ouvre à `https://localhost:18444/`, la page de l'interface à `https://localhost:18444/docs/`. Le certificat vient de l'autorité locale du dossier de données (`certificates/pseudalis-authority.crt`).

## L'application

| Section du menu | Ce qu'elle fait |
|---|---|
| Protéger des fichiers | Word, Excel, PowerPoint, PDF, textes, CSV, images, archives ZIP, TAR ou TAR.GZ, et dossiers entiers (rendus en archive ZIP) ; les originaux peuvent être conservés |
| Restaurer des fichiers | Rend l'original d'un fichier protégé, ou d'une copie de celui-ci |
| Protéger un texte | Protège ou restaure un texte collé, ou montre les valeurs trouvées, en couleurs |
| Originaux conservés | La liste des originaux gardés, à télécharger ou à oublier |
| Clé de cet ordinateur | L'empreinte de la clé, son export et son import protégés par une phrase de passe |
| Réglages | Balises par défaut, langue, lettre du lecteur, ordre des dates, taille maximale d'un fichier, durée et espace des originaux, description des images par Albireo |
| Termes imposés | Des termes toujours protégés, quel que soit le détecteur |

Le menu donne aussi l'état du service, le débit du moteur en direct (documents lus et copies protégées écrites, en Ko/s, sur les cinq dernières secondes), le lien vers la page de l'interface, la langue (français par défaut) et le thème (système, clair, sombre). Le sélecteur « Albireo | Pseudalis » mène à Albireo quand il tourne sur le Mac.

Depuis la 2.0.0, il n'y a plus de jeton d'administration : toutes les sections s'affichent, et toutes les routes répondent sur le Mac sans en-tête particulier.

## Le lecteur

Le service surveille un dossier de ses données, `drive` : un fichier déposé y est protégé sur place, et un fichier refusé part dans `refused`. Sous Windows, ce dossier apparaît sous une lettre de lecteur ; sur le Mac, c'est un dossier ordinaire, que l'on ouvre dans le Finder. `GET /v1/drive` dit où il en est (`scans`, `idle`).

## L'interface

Les routes principales, décrites en détail par `web/openapi.json` et la page de l'interface :

| Route | Rôle |
|---|---|
| `POST /v1/anonymize`, `POST /v1/restore` | Protéger ou restaurer un fichier, envoyé tel quel dans le corps |
| `POST /v1/text/anonymize`, `/v1/text/restore`, `/v1/text/detect` | La même chose pour un texte, en JSON |
| `POST /v1/names/anonymize`, `/v1/names/restore` | Les noms de fichiers |
| `GET /v1/lookup` | Si un fichier est un fichier protégé connu de cette installation |
| `GET /v1/records`, `GET /v1/records/{id}/original`, `DELETE /v1/records/{id}` | Les originaux conservés |
| `GET`/`PUT /v1/settings`, `GET`/`PUT /v1/terms` | Réglages et termes imposés |
| `POST /v1/key/export`, `/v1/key/import` | La clé |
| `GET /v1/info`, `/v1/health`, `/v1/speed`, `/v1/drive`, `/v1/albireo`, `/v1/logs`, `/v1/events` | État, débit, lecteur, Albireo, journaux, événements |

Exemple :

```sh
curl --cacert build/pseudalis-home/certificates/pseudalis-authority.crt \
  https://localhost:18444/v1/text/anonymize \
  -H "Content-Type: application/json" -d '{"text": "Jean Dupont habite 12 rue des Lilas à Rennes."}'
```

## Avec Albireo

Quand Albireo tourne et que l'un de ses modèles lit les images, Pseudalis lui envoie chaque image d'un document et en écrit une description factuelle, qui ne nomme personne (réglage « description des images », `albireo`). Il trouve Albireo par `ALBIREO_HOME`, le port de son `settings.json` et son autorité locale (`GET /v1/albireo` dit ce qu'il a trouvé). Albireo trouve Pseudalis de la même façon par `PSEUDALIS_HOME` (`GET /albireo/v1/pseudalis`), pour son sélecteur.

## Le dossier de données

| Élément | Contenu |
|---|---|
| `settings.json` | Les réglages |
| `keys/` | La clé maîtresse de l'ordinateur (0700) |
| `vault/` | Les originaux conservés, chiffrés |
| `db/` | La base des enregistrements, des termes et de l'historique |
| `drive/`, `refused/` | Le lecteur et ses fichiers refusés |
| `certificates/` | L'autorité locale et le certificat du serveur |
| `logs/` | Le journal du service |

Un `admin.token` laissé par une version antérieure n'est plus lu.

## Tester

```sh
build/macos-pseudalis/tests/test_engine            # le moteur
build/macos-pseudalis/tests/test_host <dossier>    # le service et ses fichiers
PSEUDALIS_TEST_PORT=18474 bash dlib/dlib/external/pseudalis/tools/test_macos.sh   # 34 étapes
```

`test_macos.sh` lance un service d'essai isolé ; il prend le port 18444 par défaut, celui du vrai service : quand celui-ci tourne, `PSEUDALIS_TEST_PORT` en donne un autre. Résultat attendu : **34 étapes sur 34**.

## Sécurité

- Sans jeton d'administration, tout programme du Mac qui joint `https://localhost` au port du service peut lire les originaux conservés, exporter la clé avec une phrase de passe de son choix, et changer les réglages. C'est le choix de l'humain (2026-11-15), le même que pour Albireo ; il convient à un Mac à un seul utilisateur.
- Restent les gardes de toutes les routes : l'en-tête Host doit nommer le serveur local, et une requête venue d'une page doit venir de ce serveur. Aucun site web ne peut donc piloter le service par le navigateur.
- Le service n'écoute que sur `127.0.0.1` : les autres appareils du réseau ne le joignent pas.
