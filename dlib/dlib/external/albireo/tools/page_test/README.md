# Banc d'essai de la page d'Albireo (fork pour macOS)

Le banc vérifie la page web d'Albireo de bout en bout, comme un utilisateur, sans toucher au service dont on se sert. Il a servi au découpage d'`app.js` (Albireo 4.11.1 à 4.11.11) et sert à tout changement de la page.

```sh
bash dlib/dlib/external/albireo/tools/page_test/page_test.sh            # tous les scénarios
bash dlib/dlib/external/albireo/tools/page_test/page_test.sh regress    # un seul
PAGE_TEST_KEEP=1 bash dlib/dlib/external/albireo/tools/page_test/page_test.sh   # le service reste ouvert à la fin
```

## Ce qu'il fait

1. Il démarre un **service d'essai** isolé : une copie du programme construit (`build/macos-albireo`), qui sert la page des sources (`web/`), avec ses données dans `build/page-test/home`, hors ligne, sur le port 18491.
2. Au premier lancement, il y installe des modèles **clonés** depuis le Mac (copies APFS, sans place prise), par un catalogue local et la réserve : le Qwen3-VL 2B (`build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf`, ou `PAGE_TEST_MODEL`), et, s'il est là, le Qwen3-VL 4B avec son projecteur, pris dans `build/albireo-home/models`.
3. Il passe **ESLint** sur les modules de la page (noms non définis, imports réaffectés, déclarations en double). ESLint s'installe une fois depuis npm dans `build/page-test/lint` ; sans npm, l'étape est sautée.
4. Il joue les **scénarios** dans Chrome sans fenêtre, piloté par `cdp.mjs` (protocole DevTools, sans autre dépendance que Node.js). Chaque scénario part d'un profil de navigateur neuf. Toute erreur JavaScript de la page fait échouer le scénario.

## Les scénarios

| Scénario | Vérifications | Ce qui est essayé |
|---|---|---|
| `regress` | 40 | Deux questions, paramètres et leurs sept onglets, notes de version, licences, projet, brouillon gardé après rechargement, recherche dans les discussions et dans les projets, discussion privée, bloc de code, agent et calculatrice, noms des fichiers de l'agent, mémoire des discussions après 45 s d'inactivité |
| `docs` | 12 | Fichier texte joint à une question, document d'un projet, réponses tirées de l'un et de l'autre |
| `images` | 12 | Avec le 4B : image jointe (réponse « rouge »), vignette, visionneuse, image d'un projet décrite par le modèle d'arrière-plan |
| `actions` | 24 | Copier, régénérer, exporter une réponse, modifier une question, la reposer, la supprimer, arrêter une réponse, renommer, épingler, exporter en Markdown et en JSON, supprimer une discussion |
| `tabs` | 11 | Deux onglets : la réponse, le renommage et la suppression faits dans l'un apparaissent dans l'autre |
| `phone` | 7 | Largeur d'un téléphone (390 points) : zone de saisie visible, pas de défilement horizontal, une question, bouton du panneau latéral, paramètres à la largeur de l'écran |

Les réponses attendues (« Paris », « Rome », « ORNITHORYNQUE »…) sont celles du 2B en choix habituel ; un autre modèle peut demander d'adapter un scénario.

## Écrire une étape

Un scénario est une liste d'étapes JSON, jouées dans l'ordre :

| Clé | Effet |
|---|---|
| `eval` | Corps d'une fonction `async` évaluée dans la page ; sa valeur de retour est affichée |
| `expect` | La valeur attendue de `eval` (`true` : toute valeur vraie) ; sinon l'étape échoue |
| `until` | Comme `eval`, répété toutes les 0,5 s jusqu'à une valeur vraie ou `timeout` (ms) |
| `wait` | Attente en millisecondes |
| `reload` | Recharge la page, puis attend ce nombre de millisecondes |
| `shot` | Capture d'écran dans `build/page-test/shots/` |
| `tab` | Rend actif l'onglet n (l'ouvre la première fois) |
| `viewport` | Taille de la page `[largeur, hauteur, mobile]`, puis rechargement |
| `name` | Le nom affiché de l'étape |

Les résultats détaillés sont dans `build/page-test/<scénario>.txt`, les fichiers exportés dans `build/page-test/downloads/`.

## Limites

- La compression des échanges anciens n'est pas essayée : il faudrait dépasser le contexte de travail du modèle (16 384 tokens au moins).
- Le glisser-déposer et le collage de fichiers ne sont pas essayés (seuls les champs de fichiers le sont).
- Les accès au Web et aux documents du Mac ne sont pas essayés (le service d'essai est hors ligne).
- Constat (page d'origine, pas un effet du découpage) : à la largeur d'un téléphone, le panneau latéral est ouvert au premier affichage et recouvre la page, le réglage `sidebar` valant vrai par défaut.
