# Ce que le fork Mac change dans Pseudalis, et pourquoi

Un tableau par thème. Chaque ligne dit ce que fait l'original (Pseudalis pour Windows, de Cydral), ce que fait le fork sur Mac, pourquoi, et le gain mesuré quand il y en a un. Le détail se trouve dans les [notes de version de Pseudalis](../../dlib/dlib/external/pseudalis/web/changelog.md) (« v. x.y.z »), les [notes pour l'auteur de Pseudalis](notes-pour-l-auteur.md) et le [journal](../../logs/journal-modifications.md). Ce qui touche dlib et Albireo est dans le [tableau général](../differences-mac-windows.md).

Base de comparaison : l'original au commit `184ea00` (Pseudalis 1.0.5), dernière fusion. Dernière mise à jour de ce tableau : Pseudalis 2.1.0, 15 novembre 2026.

## 1. Système

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré | Où |
|---|---|---|---|---|---|
| Plateforme | Windows 11, avec des variantes Linux de développement | macOS sur puce Apple seulement ; le code de Windows, de Linux et des x86 est retiré | Le but du fork | — | v. 1.0.5 (fork) |
| Moteur | Bibliothèques de moteur chargées à l'exécution (variantes, dont AVX2) | Le moteur est compilé dans le service | Une seule puce visée | — | v. 1.0.5 (fork) |
| Lecteur | Un dossier des données, montré par Windows sous une lettre | Le même dossier (`drive`), ouvert dans le Finder | macOS n'a pas de lettres de lecteur | — | v. 1.0.5 (fork) |
| Icône, Explorateur, installateur, mises à jour | Icône de la zone de notification, commandes de l'Explorateur, installateur signé, flux de mises à jour | Retirés ; le service se lance en ligne de commande | Propres à Windows | — | v. 1.0.5 (fork) |
| Contrôles de dossiers | Complets sous Windows ; « toujours oui » ailleurs | Propriétaire, droits POSIX et ACL de macOS | Sécurité (notes générales, entrée 21) | — | v. 1.0.5 (fork) |
| zlib | Copie compilée avec dlib | zlib de macOS | Moins de code embarqué | — | v. 1.0.5 (fork) |

## 2. Accès et sécurité

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré | Où |
|---|---|---|---|---|---|
| Administration | Jeton d'administration (`admin.token`), donné par l'icône dans l'adresse de la page | Plus de jeton : l'administration est ouverte sur le Mac, comme Albireo ; gardes Host et origine conservées | Demande de l'humain : rien sur le Mac n'ouvrait la page avec le jeton | — | v. 2.0.0 |
| Port par défaut | 8444 | 8444, réglé à 18444 sur le Mac du fork | L'accès réseau d'Albireo prend aussi 8444 | — | guide |
| Accès depuis le réseau | Aucun : le service n'écoute que sur 127.0.0.1 | Réglage `lan` : second serveur en http au port `lan_port` (8445), sans mot de passe, adresses du Mac seulement (IPv4 ou `.local`) | Ouvrir Pseudalis depuis l'iPad (demande de l'humain) | — | v. 2.1.0 |
| Langues | Anglais, français, allemand, italien, espagnol | Français seulement, page de l'interface comprise ; les détecteurs lisent toujours les autres langues | Le fork ne fait que du français (demande de l'humain : « tout en français ») | — | v. 1.0.5 (fork), v. 2.0.0, v. 2.0.1 |

## 3. Interface

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré | Où |
|---|---|---|---|---|---|
| Application | Une page en colonne, à faire défiler | Menu à gauche, une section à la fois, état dans le menu, thème sombre | Plus pratique (demande de l'humain) | — | v. 2.0.0 |
| Débit | — | Débit du moteur en direct dans le menu (Ko/s lus et écrits, `GET /v1/speed`) | Voir le travail du moteur (demande de l'humain) | — | v. 2.0.0 |
| Passage à Albireo | — | Sélecteur « Albireo \| Pseudalis » quand Albireo tourne sur le Mac, aussi depuis un autre appareil par les accès réseau | Le même Mac, la même famille d'outils | — | v. 2.0.0, v. 2.1.0 |
| Page de l'interface | Swagger UI brut | En-tête en français, démarrage rapide à copier, thème sombre, filtre | Plus pratique et plus lisible | — | v. 2.0.0 |
| README | En anglais | Traduit en français | Demande de l'humain | — | v. 2.0.0 |

## 4. Tests

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré | Où |
|---|---|---|---|---|---|
| Test complet | `tools/test_linux.sh`, avec des attentes à temps fixe | `tools/test_macos.sh`, qui attend le repos du lecteur (`scans`, `idle`) ; port réglable (`PSEUDALIS_TEST_PORT`) | Des étapes échouaient sur une machine lente | 34 étapes sur 34, en mode économie d'énergie compris | notes, P1 |
