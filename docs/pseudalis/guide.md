# Pseudalis sur Mac — guide

Pseudalis protège les données personnelles des documents : chaque nom, adresse, numéro, identifiant ou date est remplacé par un pseudonyme, le même dans tout le document et un autre ailleurs, sans retour possible ; seul l'original conservé rend un fichier (3.0.0). Ce guide décrit la version du fork pour macOS à puce Apple (Pseudalis **3.0.0**) : la construire, la lancer, s'en servir, la tester. Il suit le code ; les choix et l'historique sont dans les [notes de version](../../dlib/dlib/external/pseudalis/web/changelog.md), le [tableau des différences avec Windows](differences-mac-windows.md) et le [blog de Pseudalis](../../blog/pseudalis/README.md).

Pseudalis vit dans le même dépôt que Gulliver et repose, comme lui, sur dlib : sur [`dlib/privacy`](../../dlib/dlib/privacy.h) et sur les modèles de reconnaissance de l'exemple `slm_document_anonymizer_ex`, compilés dans le service.

## Construire

```sh
cmake -S dlib/dlib/external/pseudalis -B build/macos-pseudalis -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/macos-pseudalis
```

La construction donne `build/macos-pseudalis/bin/pseudalis-service`, l'application web à côté de lui (`bin/web`, avec la page de l'interface `bin/web/docs`, dont Swagger UI, la feuille de style et le thème viennent de la copie de Gulliver du dépôt), et les tests `test_engine` et `test_host`. Elle ne doit produire aucun avertissement.

La version se change par `python3 dlib/dlib/external/pseudalis/tools/bump_version.py patch|minor|major` (`common/version.h`, `project()` du `CMakeLists.txt`, `web/openapi.json`) ; chaque version a sa section dans les notes de version.

## Lancer

```sh
(PSEUDALIS_HOME=$PWD/build/pseudalis-home GULLIVER_HOME=$PWD/build/gulliver-home \
  nohup build/macos-pseudalis/bin/pseudalis-service --console > build/pseudalis-home/logs/console.log 2>&1 &)
```

- `PSEUDALIS_HOME` est le dossier de données ; sans lui, `~/.local/share/pseudalis`. Attention : le service n'a pas d'option `--help`, et tout lancement sans `PSEUDALIS_HOME` crée un dossier de données par défaut, avec une clé neuve.
- `GULLIVER_HOME` dit où trouver Gulliver, qui décrit les images des documents (voir plus bas) ; sans lui, `~/.local/share/gulliver`.
- Le port vient de `settings.json` : 8444 par défaut, mais c'est aussi le port par défaut de l'accès réseau de Gulliver. Sur ce Mac, le dossier `build/pseudalis-home` règle **18444**.
- Arrêt : `kill -INT` ou `kill -TERM` sur le service ; il s'arrête proprement.

L'application s'ouvre à `https://localhost:18444/`, la page de l'interface à `https://localhost:18444/docs/`. Le certificat vient de l'autorité locale du dossier de données (`certificates/pseudalis-authority.crt`).

### Depuis l'iPad ou le téléphone

Dans la section Réglages, « Ouvrir Pseudalis aux appareils de la maison » ouvre un second serveur, en http, sur chaque adresse du Mac, au port réseau (8445 par défaut, réglages `lan` et `lan_port`) ; le service redémarre. Pseudalis s'ouvre alors à `http://<adresse du Mac>:8445/` (sur ce Mac : `http://192.168.1.12:8445/`, adresse donnée par la box). Le sélecteur « Gulliver | Pseudalis » mène à l'accès réseau de Gulliver (port 8444) quand celui-ci est ouvert.

## L'application

| Section du menu | Ce qu'elle fait |
|---|---|
| Protéger des fichiers | Word, Excel, PowerPoint, PDF, textes, CSV, images, archives ZIP, TAR ou TAR.GZ, et dossiers entiers (rendus en archive ZIP) ; les originaux peuvent être conservés |
| Restaurer des fichiers | Rend l'original conservé d'un fichier protégé, ou d'une copie de celui-ci |
| Protéger un texte | Protège un texte collé, ou montre les valeurs trouvées, en couleurs |
| Originaux conservés | La liste des originaux gardés, à télécharger ou à oublier |
| Réglages | Balises par défaut, lettre du lecteur, ordre des dates, taille maximale d'un fichier, durée et espace des originaux, description des images par Gulliver |
| Termes imposés | Des termes toujours protégés, quel que soit le détecteur |
| Colonnes imposées | Dans un fichier CSV à en-tête, les colonnes nommées (et leur type : personne, e-mail, IP…) dont chaque valeur prend un pseudonyme sans détection (3.2.0) |

Le menu donne aussi l'état du service, le débit du moteur en direct (documents lus et copies protégées écrites, en Ko/s, sur les cinq dernières secondes, avec un « ? » qui explique la mesure), le lien vers la page de l'interface et le thème (système, clair, sombre). L'application et la page de l'interface sont en français seulement ; le service répond en français (descriptions d'images comprises), et son ancien réglage `language` n'a plus d'effet. Le sélecteur « Gulliver | Pseudalis » mène à Gulliver quand il tourne sur le Mac.

Depuis la 2.0.0, il n'y a plus de jeton d'administration : toutes les sections s'affichent, et toutes les routes répondent sur le Mac sans en-tête particulier.

## Le lecteur

Le service surveille un dossier de ses données, `drive` : un fichier déposé y est protégé sur place, et un fichier refusé part dans `refused`. Sous Windows, ce dossier apparaît sous une lettre de lecteur ; sur le Mac, c'est un dossier ordinaire, que l'on ouvre dans le Finder. `GET /v1/drive` dit où il en est (`scans`, `idle`).

## L'interface

Les routes principales, décrites en détail par `web/openapi.json` et la page de l'interface :

| Route | Rôle |
|---|---|
| `POST /v1/anonymize`, `POST /v1/restore` | Protéger ou restaurer un fichier, envoyé tel quel dans le corps |
| `POST /v1/text/anonymize`, `/v1/text/restore`, `/v1/text/detect` | La même chose pour un texte, en JSON ; avec `"session": true`, la clé de séance du relais de Gulliver (3.1.0), et les valeurs des pseudonymes dans la réponse (`values`) |
| `POST /v1/names/anonymize`, `/v1/names/restore` | Les noms de fichiers |
| `GET /v1/lookup` | Si un fichier est un fichier protégé connu de cette installation |
| `GET /v1/records`, `GET /v1/records/{id}/original`, `DELETE /v1/records/{id}` | Les originaux conservés |
| `GET`/`PUT /v1/settings`, `GET`/`PUT /v1/terms`, `GET`/`PUT /v1/columns` | Réglages, termes imposés et colonnes imposées |
| `GET /v1/info`, `/v1/health`, `/v1/speed`, `/v1/drive`, `/v1/albireo`, `/v1/logs`, `/v1/events` | État, débit, lecteur, Gulliver, journaux, événements |

Exemple :

```sh
curl --cacert build/pseudalis-home/certificates/pseudalis-authority.crt \
  https://localhost:18444/v1/text/anonymize \
  -H "Content-Type: application/json" -d '{"text": "Jean Dupont habite 12 rue des Lilas à Rennes."}'
```

## Avec Gulliver

Quand Gulliver tourne et que l'un de ses modèles lit les images, Pseudalis lui envoie chaque image d'un document et en écrit une description factuelle, qui ne nomme personne (réglage « description des images », `gulliver`). Il trouve Gulliver par `GULLIVER_HOME`, le port de son `settings.json` et son autorité locale (`GET /v1/gulliver` dit ce qu'il a trouvé). Gulliver trouve Pseudalis de la même façon par `PSEUDALIS_HOME` (`GET /gulliver/v1/pseudalis`), pour son sélecteur.

## Le dossier de données

| Élément | Contenu |
|---|---|
| `settings.json` | Les réglages |
| `keys/` | La clé maîtresse de l'ordinateur, en clair (dossier 0700, fichier 0600) |
| `vault/` | Les originaux conservés, leurs images et leurs descriptions, en clair depuis la 2.2.0 |
| `db/` | La base des enregistrements, des termes et de l'historique |
| `drive/`, `refused/` | Le lecteur et ses fichiers refusés |
| `certificates/` | L'autorité locale et le certificat du serveur |
| `logs/` | Le journal du service |

Un `admin.token` laissé par une version antérieure n'est plus lu.

## Tester

```sh
build/macos-pseudalis/bin/test_engine            # le moteur
build/macos-pseudalis/bin/test_host <dossier>    # le service et ses fichiers
PSEUDALIS_TEST_PORT=18474 bash dlib/dlib/external/pseudalis/tools/test_macos.sh   # 37 étapes
```

`test_macos.sh` lance un service d'essai isolé ; il prend le port 18444 par défaut, celui du vrai service : quand celui-ci tourne, `PSEUDALIS_TEST_PORT` en donne un autre. Résultat attendu : **37 étapes sur 37**.

## Sécurité

- **Plus de chiffrement en sortie** (2.2.0, décision de l'humain du 2026-11-15) : le coffre garde les originaux en clair (les blobs chiffrés d'une version antérieure sont réécrits en clair au démarrage), et l'export de la clé est la clé en clair. Tout programme du Mac qui lit le dossier de données lit les originaux ; quiconque lit un export de la clé peut restaurer les copies protégées. Un export protégé d'une version antérieure s'importe encore, avec sa phrase secrète, par l'interface. Depuis la 3.0.0, plus rien n'est chiffré en sortie : les copies portent des pseudonymes faits sous une clé jetable par document, effacée aussitôt ; la clé de `keys/` ne sert plus qu'à relire les copies et les blobs d'une version antérieure et aux identifiants des images. Plus d'export ni d'import de la clé.
- **Clé de séance du relais de Gulliver** (3.1.0) : un texte envoyé avec `"session": true` est protégé sous une clé tirée au démarrage du moteur, gardée en mémoire seulement, et la réponse donne la valeur de chaque pseudonyme. Ces pseudonymes-là reviennent donc, le temps que le service tourne : c'est ce qui permet au relais de remettre les vrais noms dans la réponse d'un moteur extérieur. La route ne rend que les valeurs du texte qu'on lui envoie : elle n'ouvre aucun pseudonyme d'un document.
- Sans jeton d'administration, tout programme du Mac qui joint `https://localhost` au port du service peut lire les originaux conservés, exporter la clé avec une phrase de passe de son choix, et changer les réglages. C'est le choix de l'humain (2026-11-15), le même que pour Gulliver ; il convient à un Mac à un seul utilisateur.
- Restent les gardes de toutes les routes : l'en-tête Host doit nommer le serveur local, et une requête venue d'une page doit venir de ce serveur. Aucun site web ne peut donc piloter le service par le navigateur.
- Sans l'accès réseau, le service n'écoute que sur `127.0.0.1` : les autres appareils ne le joignent pas.
- Avec l'accès réseau (2.1.0), **tout appareil du Wi-Fi** peut s'en servir sans mot de passe : protéger, restaurer, lire les originaux conservés, exporter la clé, changer les réglages, et le trafic passe en clair (http). C'est le choix de l'humain (2026-11-15), comme pour Gulliver ; à ne pas activer sur un Wi-Fi partagé ou public. Le serveur du réseau n'accepte que les requêtes qui nomment le Mac par une adresse IPv4 ou un nom en `.local` (421 sinon) et refuse une requête venue d'une autre origine (403), pour qu'un site web ne puisse pas le piloter par le navigateur (rebinding DNS compris).
