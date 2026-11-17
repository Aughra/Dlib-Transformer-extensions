# Documents du portage, dans Gulliver

Les documents du fork se lisent dans Gulliver lui-même, depuis le Mac, le téléphone ou l'iPad : menu ☰, « Documents du portage », ou directement `/carnet/` (par exemple `http://<adresse du Mac>:8444/carnet/` sur le réseau de la maison). Le sommaire, les documents de `docs/`, les journaux de `logs/`, les deux blogs, les notes de version et les README d'Gulliver et de Pseudalis y sont. Il n'y a plus de site à lancer à part (l'ancien `serve.py`, port 8650, est retiré depuis Gulliver 5.2.0).

Ce dossier ne garde que le sommaire, [`sommaire.json`](sommaire.json), que lit le service (`host/carnet.cpp`) :

- **`motifs`** : la liste fermée des fichiers lisibles, chacun un dossier exact et un nom (`*`, `*.extension` ou un nom exact). Le reste du dépôt, les dossiers cachés, les chemins avec `..` et les liens symboliques ne sont jamais servis.
- **`sections`** : les cartes du sommaire (chemin, titre, description). Un nouveau document de `docs/` se lit dès qu'il répond à un motif, mais il faut l'ajouter ici pour qu'il ait sa carte.
- **`blogs`** : les dossiers des épisodes `NN-titre.md` ; un nouvel épisode apparaît tout seul.

Le service lit le dépôt où il a été construit (`GULLIVER_FORK_ROOT_DEFAULT`, donné par la construction), ou celui que nomme la variable `GULLIVER_FORK_ROOT`. Les pages Markdown sont mises en forme dans le navigateur par marked et DOMPurify, ceux d'Gulliver ; rien n'est chargé depuis Internet.

**Sans mot de passe** : comme Gulliver, tout appareil branché sur la box peut lire ces pages. Elles ne contiennent pas de secret (c'est une règle du fork), mais il ne faut pas ouvrir le port réseau d'Gulliver vers Internet.
