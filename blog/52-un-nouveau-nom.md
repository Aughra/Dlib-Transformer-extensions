# Dlib Transformer Extensions sur macOS, épisode 52 : un nouveau nom

> **Repère de vitesse** (MacBook Air M2) : le moteur n'a pas bougé depuis l'épisode 51 — écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B). Ce qui change ici n'est pas la vitesse, mais le nom.

Suite de [l'épisode 51](51-un-gpu-pour-apprendre.md), qui apprenait à dlib à calculer sur le GPU. Cette fois, pas de nouveau calcul : le service de chat change de nom. **Albireo devient Gulliver.**

## Pourquoi débaptiser un programme

Le projet d'origine, celui de Cydral, appelle son service de chat « Albireo ». Le portage sur Mac vit sa propre vie depuis qu'il s'est détaché de l'original ; lui donner un nom à lui, c'est dire clairement que ce n'est plus tout à fait le même programme. On a choisi Gulliver, le voyageur qui débarque chez des géants puis chez des gens minuscules : un petit modèle de langage qui tient sur un Mac, et qui lit des documents bien plus grands que lui.

Renommer un programme, ça paraît anodin — on change un mot. En vrai, le nom était partout.

## Un mot qui s'était glissé dans tous les tiroirs

Quand on cherche « albireo » dans le dépôt, on en trouve près de quatre mille, dans plus de deux cents fichiers. Mais tous ne sont pas la même chose, et c'est là que le ménage demande de l'attention. Le même mot jouait plusieurs rôles :

| Où | Exemple | Fallait-il changer ? |
|---|---|---|
| Le dossier du projet | `external/albireo/` | Oui, renommé `external/gulliver/` |
| Les adresses de l'interface | `/albireo/v1/status` | Oui → `/gulliver/v1/status` |
| Le nom de la base de données | `albireo.db` | Oui → `gulliver.db` |
| Les noms internes du code | `namespace albireo`, `ALBIREO_HOME` | Oui → `gulliver`, `GULLIVER_HOME` |
| Les noms de modèles de l'interface | `albireo-general`, `albireo-relay` | Oui → `gulliver-general`, `gulliver-relay` |
| Les étiquettes dans les fichiers de modèles | `albireo.id`, `albireo.roles` | **Non** — c'est un format déjà écrit |
| Le nom de l'auteur, le copyright | `Cydral Technology` | **Non** — c'est l'original |
| Les anciennes versions citées | « Albireo 1.0.12 » | **Non** — c'est une version de l'original |

Les deux dernières lignes sont le cœur du soin. Un remplacement brutal « albireo → gulliver » aurait écrasé le nom de l'auteur du code, réécrit l'histoire des versions, et surtout cassé la lecture des modèles déjà préparés.

## Le casier étiqueté qu'il ne faut pas repeindre

Les fichiers de modèles, en format GGUF, portent de petites étiquettes que le programme a écrites dedans : « ce fichier est le modèle général », « il lit des images ». Ces étiquettes s'appellent `albireo.id`, `albireo.roles`, et le programme les relit à chaque ouverture. Si on repeint l'étiquette sur la boîte qui lit (le programme) mais pas sur les boîtes déjà rangées (les modèles), plus rien ne se retrouve : le programme cherche `gulliver.id` et ne trouve que `albireo.id`. On a donc laissé ces étiquettes-là telles quelles. Un nom de format, une fois écrit sur les données, ne se change pas sans réécrire toutes les données.

## Deux pièges attrapés au passage

Renommer, c'est aussi découvrir les endroits où un nom servait de lien entre deux morceaux. Deux liens se sont cassés en silence, et les tests les ont rattrapés.

Le premier : le service charge son moteur de calcul depuis un fichier nommé d'après le programme, `libalbireo-engine-cpu.dylib`. En renommant la cible de construction, ce fichier est devenu `libgulliver-engine-...`, mais le code qui va le chercher demandait encore l'ancien nom. Le service compilait, démarrait, répondait même aux premières requêtes — mais au moment de charger un modèle, il n'aurait pas trouvé son moteur. L'erreur est facile à commettre et on l'a commise ; un essai de chargement l'a montrée.

Le second, du même genre : le fichier de licence servi par le programme, et les certificats qu'il crée pour parler en chiffré. Tous nommés d'après le programme, tous à suivre.

## Pseudalis, qui habite à côté

Pseudalis, l'outil d'anonymisation, partage le dépôt et sait parler à son voisin de chat pour lui faire décrire les images. Il avait donc tout un carnet d'adresses vers « Albireo » : où le trouver, quel modèle lui demander. Pseudalis garde son nom — ce n'est pas lui qu'on renomme — mais tout ce carnet pointe désormais vers Gulliver. Un voisin déménage, l'autre met à jour son répertoire.

## Ce qui ne change pas

Deux choses restent, volontairement. L'**auteur** : le code vient de Cydral, sous une licence qui demande de garder la mention ; le portage s'ajoute, il n'efface pas. Et les **adresses compatibles OpenAI** (`/v1/chat/completions` et les autres) : ce sont des adresses standard, que des programmes extérieurs connaissent ; les renommer n'aurait eu aucun sens. Gulliver répond toujours à ces adresses-là comme avant.

## La suite

Le renommage était la première étape d'un chantier plus large : donner à Gulliver un vrai **profil** — un simple nom, sans mot de passe — pour que les discussions, les documents et leur index soient gardés par le service lui-même, et se retrouvent depuis n'importe quel navigateur, sans rien exporter. C'est l'épisode suivant.

## Pour les curieux : les chiffres et la méthode

- **Ampleur** : ~3 982 occurrences de « albireo » dans 237 fichiers (hors dossier de construction et `.git`) ; 204 adresses `/albireo/v1` dans le service et la page.
- **Méthode** : renommage en plusieurs passes relues (dossier, puis identifiants du code, puis adresses et noms de modèles d'un bloc service + page + documentation d'interface, puis textes), chacune suivie d'une reconstruction et des tests. Un nom oublié dans le code casse la compilation ; une adresse oubliée casse la page, que le banc d'essai attrape.
- **Gardé tel quel** : les étiquettes de modèles `albireo.id`/`.roles`/`.inputs`/`.name` (format des fichiers existants), l'auteur et le copyright Cydral, les versions de l'original (« Albireo 1.0.x »), l'adresse du catalogue de l'original.
- **Version** : le service passe en **6.0.0** (rupture de nom et d'adresses d'interface). Les sections antérieures des notes de version gardent le nom « Albireo ».
- **Vérifié** : le service et les 10 programmes de test compilent sans un avertissement ; `test_host` *all checks passed* ; `test_macos.sh` **55 étapes sur 55** ; le banc de la page passe ; Pseudalis compile et se relie à Gulliver.

## À suivre

[Épisode 53](53-un-nom-pour-chaque-espace.md) : le profil côté service, pour ne plus rien perdre en changeant de navigateur.
