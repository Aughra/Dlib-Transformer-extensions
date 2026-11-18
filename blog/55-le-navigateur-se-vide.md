# Dlib Transformer Extensions sur macOS, épisode 55 : le navigateur se vide

> **Repère de vitesse** (MacBook Air M2) : le moteur de langage est inchangé depuis l'épisode 51 — écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B). Ce qui change ici n'est pas la vitesse, mais l'endroit du travail : le dernier calcul quitte le navigateur.

Suite de [l'épisode 54](54-la-recherche-passe-cote-moteur.md), qui avait déménagé la recherche dans les documents du navigateur vers le service. Restaient deux morceaux de travail dans la page : **découper** les documents à l'indexation, et **en lire le texte** (un PDF, un fichier Word). Cet épisode les sort tous les deux. Après lui, le navigateur ne calcule plus rien pour les documents ; il envoie des fichiers et affiche des réponses.

## Le cahier qu'on ne photocopie plus à la main

Pour chercher dans un document, il faut d'abord le couper en petits passages, puis résumer chacun en une liste de nombres — son « vecteur ». Jusqu'ici, le navigateur faisait ce découpage lui-même, puis demandait les vecteurs un paquet après l'autre, en tenant les comptes. C'est comme photocopier un gros classeur page par page avant de le ranger : faisable, mais c'est à la personne de tourner les feuilles.

Maintenant, le navigateur envoie le document entier au service, en un seul geste, et le service fait tout : il découpe, il calcule les vecteurs, il range. Pour que la barre de progression et le bouton « annuler » ne disparaissent pas — un gros document met du temps — le service raconte son avancement au fil de l'eau, comme le fait déjà le chat quand il écrit mot à mot. La personne voit la barre monter ; mais plus une seule ligne de calcul ne tourne de son côté.

## Lire un PDF sans le navigateur : l'outillage qui manquait

Le point le plus surprenant concerne la **lecture du texte**. Un fichier Word ou un PDF, ce n'est pas du texte brut : il faut l'ouvrir, en comprendre la structure, en extraire les mots. Le navigateur sait faire cela — c'est même une de ses spécialités. On avait d'abord cru qu'il était le seul à pouvoir : lire un PDF demande une grosse machinerie, et on s'était dit qu'elle n'existait qu'au navigateur.

C'était faux, et il faut le dire franchement. Cette machinerie existe aussi en dehors, et de bonne qualité. Pour les formats de bureautique — Word, Excel, PowerPoint, OpenDocument — c'est même simple : ce sont des dossiers compressés remplis de petits fichiers de description, et dlib sait déjà ouvrir ce genre d'archive. Le service les lit donc sans rien ajouter.

Le PDF, lui, demande un outil à part. On a repris **PDFium**, la bibliothèque de lecture de PDF de Chrome, publiée séparément sous une licence libre. C'est elle qui, dans des millions de navigateurs, affiche les PDF ; on s'en sert ici juste pour en tirer le texte. Comparaison honnête : au lieu de demander au navigateur de faire le travail, on a pris le moteur qu'il aurait utilisé et on l'a branché directement sur le service. Le mérite du résultat revient à cet outil, pas au portage.

## Le document numérisé reste un cas à part

Un PDF n'est pas toujours du texte. Quand on scanne une feuille, le PDF ne contient que la **photo** de la page — aucun mot à lire, juste des pixels. Le service s'en aperçoit (il ne trouve aucun texte) et le signale. La page bascule alors sur l'autre voie, inchangée : elle dessine les pages en images et les confie au modèle qui sait lire une image. Ce dessin-là reste au navigateur, parce qu'il faut bien peindre les pages quelque part, et c'est lui qui a le pinceau. C'est la seule part de la lecture d'un PDF qui ne déménage pas, et pour une bonne raison.

## Un meilleur rangement des extraits

En passant, on a ajouté un petit réglage : **affiner le tri des extraits**. Quand la recherche a trouvé les bouts de documents les plus proches d'une question, ce tri les repasse une fois pour mettre devant celui qui répond le mieux à **tous** les mots de la question, et pas seulement à un seul. C'est un tri par les mots, soigné, pas un second cerveau : un vrai modèle de reclassement viendra peut-être, mais il demande sa propre machine, et on le remet au Mac mini. Le réglage est éteint par défaut ; on le mesurera avant de conseiller de l'allumer.

Au passage d'un détail amusant du réglage : la première version récompensait trop la **répétition**. Un extrait qui répétait dix fois le même mot de la question passait devant un extrait qui, lui, contenait tous les mots cherchés une fois chacun. L'erreur est facile à commettre, et on l'a commise ; la correction tient en une phrase — ne compter comme « proches » que deux mots *différents* de la question, jamais le même mot répété.

## Le navigateur, désormais, n'est qu'une vitrine

C'était la demande de fond : « plus aucun calcul dans le navigateur, seulement des boutons qui appellent des API. » Elle est tenue, à une exception près, assumée. Les documents qu'on joint à une discussion pour qu'ils soient lus **en entier** (de petits fichiers, par définition) gardent un tri par les mots en JavaScript : leur texte est déjà dans le navigateur, et l'envoyer au service juste pour le trier serait un aller-retour pour rien. Tout le reste — découper, lire, chercher, reclasser — se fait côté service.

Le navigateur est devenu ce qu'on voulait : une vitrine. Il affiche, il recueille des clics, il envoie des fichiers. Le travail, lui, est passé de l'autre côté de la rue.

## La suite

Le moteur de recherche et la lecture des documents sont maintenant au service, en C++. Reste l'usage, le vrai : charger un modèle sans réflexe de refus et s'en servir pour la cybersécurité, et, bientôt, mesurer tout cela sérieusement sur le Mac mini.

## Pour les curieux : les fichiers et la méthode

- **Indexation côté service** : route `POST /gulliver/v1/profile/documents/<doc>/index` (flux `text/event-stream`), dans `host/server.cpp` ; elle appelle `rag_chunk` (découpage) et `embed_texts` (vecteurs, calcul interne sans repasser par HTTP), émet la progression (`: progress {done,total}`) puis `{passages,vectors}`. La page (`web/js/rag.js`) est réduite à un client : plus de découpage ni de boucle d'embedding en JavaScript.
- **Lecture du texte** : `host/doc_extract.cpp` (Office/OpenDocument/RTF/HTML par le lecteur ZIP de dlib, `dlib/zip.h`, et la lecture du XML) et `host/doc_extract_pdf.cpp` (PDF par PDFium, chargé par `dlopen`, conversion UTF-16LE → UTF-8 par CoreFoundation). Route `POST /gulliver/v1/extract {name, bytes}` → `{text, format}`, ou une erreur avec son code (`pdf-images`, `pdf-invalid`, `legacy`, `unsupported`…). La page (`web/js/files.js`, `web/js/web.js`) envoie les octets ; `web/js/documents.js` passe de 1015 à 231 lignes, et ne garde que le rendu des PDF scannés en images et l'aide au tri des documents lus en entier.
- **PDFium** : binaires de `bblanchon/pdfium-binaries` (licence MIT), version arm64 sans V8 (~7,6 Mo), récupérés par `tools/fetch_pdfium.sh` ; la bibliothèque n'est pas versionnée dans le dépôt, seulement ses en-têtes, sa licence et le script. Si elle manque, le service tourne et seuls les PDF renvoient « lecteur indisponible ».
- **Reranker** : `rerank_excerpts` dans `host/rag_engine.cpp` ; score = 2·couverture + 1·densité + 0,5·min(proximité, couverture) des mots de la question, tri stable. Réglage `rag_rerank` (défaut éteint).
- **Vérifié** : `test_host` passe ; le banc de la page exerce la lecture du texte et du PDF de bout en bout (un PDF joint, sa phrase secrète retrouvée dans la réponse). La vitesse n'est pas mesurée : le gain visé est « tout au service », pas le temps ; les mesures justes attendent le Mac mini.
- **Version** : Gulliver **6.3.0**.

## À suivre

Le Mac mini M5 Pro, et le premier vrai usage cyber avec un modèle sans garde-fous.
