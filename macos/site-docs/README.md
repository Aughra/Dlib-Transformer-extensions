# Site des documents du fork

Un petit site, pour lire les documents du fork depuis un téléphone sur le réseau de la maison : le sommaire, les documents de `docs/`, les journaux de `logs/`, le blog, les notes de version et les README d'Albireo et de Pseudalis.

```sh
python3 macos/site-docs/serve.py            # adresse du Mac sur la box, port 8650
python3 macos/site-docs/serve.py --port 8651 --host 127.0.0.1
```

Pour qu'il continue de tourner une fois le terminal fermé :

```sh
nohup python3 macos/site-docs/serve.py > build/site-docs.log 2>&1 &
pkill -f macos/site-docs/serve.py           # pour l'arrêter
```

- **Python seul**, sans paquet à installer. Les pages Markdown sont mises en forme dans le navigateur par marked et DOMPurify, pris dans `dlib/dlib/external/albireo/web/vendor/`. Rien n'est chargé depuis Internet.
- **Lecture seule, liste fermée** : seuls les fichiers des motifs `MOTIFS` de `serve.py` sont servis. Le reste du dépôt et les dossiers cachés ne sont pas servis, et les liens symboliques sont ignorés. Un lien vers un fichier source donne « Introuvable ».
- **Sans mot de passe** : n'importe quel appareil branché sur la box peut lire ces pages. Ils ne contiennent pas de secret (c'est une règle du fork), mais il ne faut pas ouvrir ce port vers Internet.
- Un nouvel épisode du blog apparaît tout seul dans le sommaire. Un nouveau document de `docs/` peut être lu, mais il faut l'ajouter à `SOMMAIRE` pour qu'il ait sa carte.
