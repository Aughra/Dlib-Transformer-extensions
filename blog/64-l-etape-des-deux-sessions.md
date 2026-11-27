# Dlib Transformer Extensions sur macOS, épisode 64 : l'étape des deux sessions

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 7.3.x, mesures du lundi 23 novembre, inchangées par ce chantier) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B).

Suite de [l'épisode 55](55-le-navigateur-se-vide.md), qui finissait sur une promesse : « plus aucun calcul dans le navigateur, seulement des affichages ». Cette promesse-là n'avait pas tout à fait été tenue. Il restait trois gros morceaux dans la page : les formules mathématiques (KaTeX), les polices de PDF.js et sa machine à dessiner, et les en-têtes de PDFium côté serveur. Une étape a suffi à les sortir.

Le fait rare, qu'il faut d'abord raconter : **à cette étape, deux sessions de Claude ont travaillé en même temps sur le même dépôt**. L'humain en a lancé une, puis une seconde, sans dire à chacune que l'autre existait. Elles se sont découvertes au premier `git status`.

## Deux sessions se croisent

C'est au moment de faire `git diff` que la première session voit arriver dans l'arbre de travail un fichier qu'elle n'a pas écrit : `host/pdf_render.cpp`, avec son `.h`, et quelques lignes ajoutées à `CMakeLists.txt`. L'auteur s'identifie en lisant les en-têtes : c'est une autre instance de Claude, lancée quelques heures plus tôt par l'humain pour « porter le dessin des pages PDF au serveur ». Pendant ce temps, la session présente, elle, remplaçait PDFium par PDFKit pour l'extraction du texte des PDF, autre moitié du même problème.

Ce qui aurait pu être une collision est devenu un partage. Un `ListAgents` révèle la session voisine, elle s'appelle « Gulliver [929ca4] ». Un `SendMessage` lui demande : « est-ce que ton pdf_render.h, qui dit « PDFium gardé », est à jour ? » La réponse arrive : « non, ta consigne [de cette étape] l'emporte ; je corrige mon en-tête, continue ta bascule ». Puis, en quelques échanges, le partage s'écrit :

| À la session présente | À la session voisine |
|---|---|
| `host/doc_extract_pdf.mm` (PDFKit, texte) | `host/pdf_render.cpp/h` (CoreGraphics, pages) |
| Suppression de `third_party/pdfium/`, `fetch_pdfium.sh` | Route `/gulliver/v1/extract/pages` dans `host/server.cpp` |
| Simplification d'`i18n.js` | Retrait de `pdf.js` du navigateur |
| Retrait de la clé `pdfReader` | Retrait des appels à `t('pdfReader', …)` |

Au total : six commits dans l'étape, quatre d'un côté, deux de l'autre. Et pas un conflit au `git add`.

## Le téléphone entre deux Claude

L'image de la vie courante : deux ouvriers rénovent le même appartement, l'un retape la cuisine, l'autre la salle de bains, ils n'ont jamais travaillé ensemble, et ils se parlent par talkie-walkie pour ne pas peindre le même mur deux fois de deux couleurs. Les messages de l'un à l'autre, dans l'historique de cette étape, ressemblent exactement à ça :

> « Tu touches à `files.js` ? »
>
> « Oui, ne le prends pas, ta `i18n.js` est à toi, mais pas `files.js`. »
>
> « OK, je laisse. »

Ce que le projet gagne dans cette façon de faire, ce n'est pas juste du parallélisme (deux fois plus de travail en même temps). C'est **de l'honnêteté** : chaque session tranche dans son domaine, voit tout de suite les conséquences, et reconnaît à l'autre son domaine. Un morceau de code qui aurait exigé une session pendant deux étapes en a demandé deux pendant une seule.

Il faut quand même nommer l'accident évité. La session voisine, qui travaillait depuis trois heures, avait écrit une phrase dans son en-tête : « le texte reste lu par PDFium, qui le rend mieux (comparaison du 2026-11-20) ». Ce n'était pas faux : le 20 novembre, un test comparatif avait fait pencher la balance pour PDFium, parce que PDFKit perdait des lettres sur du japonais. Mais à cette étape, la consigne de l'humain a repris le dessus, et la session présente n'a pas compris cela du premier coup : elle a reculé, puis la session voisine lui a dit « non, continue, ma phrase est périmée ». Sans ce dialogue, la bascule PDFKit aurait été annulée pour une mauvaise raison.

## Les formules disparaissent, et personne ne pleure

Premier geste, le plus simple : retirer KaTeX. La bibliothèque composait les formules mathématiques à la volée dans le navigateur, et pesait **596 Ko** de JavaScript plus **400 Ko** de polices et de styles. Au premier chargement, chaque visiteur téléchargeait **1 Mo** pour avoir l'air de pouvoir lire de belles fractions — qu'il n'écrirait jamais.

La consigne a été courte : « KaTeX on enlève ». Les formules s'afficheront désormais en texte brut, comme le reste du Markdown. Au lieu de la jolie fraction, une ligne qui dit `$E = mc^2$`. L'image de la vie courante : on a renvoyé le typographe à la maison, parce que personne ne lui apportait plus de livres à mettre en page.

Dans les faits, huit fichiers ont bougé, 48 lignes de code Markdown ont disparu, et le dossier des polices (seize fichiers `.woff2`) a été supprimé. La page est devenue plus légère sans que l'utilisateur voie la différence (il n'écrit pas de formules à Gulliver, il lui pose des questions en français).

## PDFium s'en va, Apple fait le tour

Deuxième geste, le vrai sujet de cette étape. **PDFium**, la bibliothèque de lecture de PDF reprise de Chrome, sortait du navigateur en épisode 55 pour aller côté serveur, lire le texte des documents envoyés à Gulliver. C'était un pari réussi, mais qui laissait Gulliver avec une bibliothèque de 7,6 Mo embarquée.

Le pari de cette étape est de la supprimer, en misant sur ce que macOS fournit déjà. L'image : au lieu de transporter son propre tire-bouchon, on utilise celui du restaurant. macOS en a deux :

- **PDFKit** sait lire le texte d'un PDF.
- **CoreGraphics** sait en dessiner les pages.

Ensemble, ils remplacent entièrement PDFium, sans rien à embarquer. Le dossier `third_party/pdfium/` disparaît du dépôt. Le script `fetch_pdfium.sh` qui la récupérait au premier build disparaît aussi. Depuis la 7.3.0, **Gulliver n'embarque plus qu'une seule bibliothèque d'autres projets** : cpp-httplib, pour parler HTTP sur une prise locale. Tout le reste — chiffrement, décodage d'images, base de données, zlib, lecture de PDF, dessin de pages — vient du système.

Et PDF.js, dans le navigateur, qui servait encore à dessiner les pages des PDF scannés pour un modèle de vision ? Il part aussi. La session voisine a ajouté une nouvelle route de service, `POST /gulliver/v1/extract/pages`, qui rend les premières pages d'un PDF en JPEG. Le navigateur appelle la route, reçoit les images, les joint à la question. Il n'a plus besoin de **3,4 Mo** de code qui redessinait des pages.

## Le défaut qu'on a corrigé avant de dormir

PDFKit fait presque tout aussi bien que PDFium, mais pas tout à fait. L'essai du 20 novembre avait montré trois défauts :

1. Avec `PDFPage.string`, PDFKit perdait des lettres doublées sur la licence japonaise de macOS (« Hollyhill » devenait « Holyhil », « Specially » devenait « Specialy »). Correctif : utiliser `PDFPage.attributedString.string` à la place, qui préserve le crénage et les ligatures.
2. L'arabe était rendu en « formes de présentation » (les variantes graphiques que l'imprimerie choisit pour l'affichage), au lieu des lettres génériques. Correctif : normaliser la sortie en **NFKC** (`CFStringNormalize`), qui ramène les formes de présentation à leurs lettres de base.
3. Un PDF **chiffré avec un mot de passe d'ouverture vide** (un PDF qui n'interdit que la copie ou l'impression) était refusé comme « protégé par un mot de passe ». Correctif : essayer d'abord `unlockWithPassword:@""` avant de refuser. Ce défaut-là, c'est la session voisine qui l'a repéré en relisant le code de la session présente — le genre de prise que seul un autre œil peut faire.

Les trois défauts corrigés, PDFKit passe à parité avec PDFium sur les tests du 20 novembre, sans rien embarquer.

## Pour les curieux : les chiffres et les commandes

| Chantier | Version | Fichiers touchés | Gain |
|---|---|---|---|
| KaTeX retiré | 7.2.3 | 8 fichiers + suppression de `web/vendor/katex/` | 1 Mo au premier chargement, `web/vendor/` de 4,3 à 3,7 Mo |
| PDFium → PDFKit + CoreGraphics | 7.3.0 | 5 fichiers service + suppression de `third_party/pdfium/` + `fetch_pdfium.sh` | 7,6 Mo de binaire en moins, plus aucune bibliothèque PDF embarquée |
| `pdf.js` retiré du navigateur | 7.3.1 (session voisine) | `documents.js`, `files.js`, `images.js`, suppression de `web/vendor/pdfjs/` (168 tables de caractères CJK et le moteur de rendu) | **3,4 Mo** au premier chargement, `web/vendor/` de **3,7 à 0,3 Mo** |
| PDF empty-password corrigé | 7.3.2 | `host/doc_extract_pdf.mm` (1 ligne de condition) | pas de régression sur les PDF des épisodes 55 à 63 |
| `i18n.js` simplifié | 7.3.3 | `web/js/i18n.js` seul (18 lignes nettes retirées) | machinerie multi-langues héritée de l'original retirée |
| Clé `pdfReader` retirée | 7.3.4 | `web/js/i18n.js` seul (clé obsolète) | morceau mort après la 7.3.0 |

Ce que le serveur conserve, embarqué dans le dépôt : **cpp-httplib** (un seul fichier d'en-têtes, licence MIT). Tout le reste vient de macOS : Network.framework (TLS), Security (certificats), Accelerate (produits matriciels), Metal (GPU), PDFKit (texte des PDF), CoreGraphics (dessin des PDF), ImageIO (images PNG et JPEG), zlib, SQLite.

Les tests de l'étape : `tests/test_host` « all checks passed » après chaque commit (y compris les dix scénarios du rendu CoreGraphics ajoutés par la session voisine) ; banc de la page avec 169 contrôles, aucun échec dans ce que touche la session présente.

Pour reproduire la bascule PDFKit sur un Mac en Apple Silicon :

```
cmake --build build/macos-gulliver
build/macos-gulliver/tests/test_host /tmp/scratch 18498
```

Les trois scénarios qui touchent PDFKit (`a PDF locked by a password is refused`, `bytes that are not a PDF are refused`, `a truncated PDF is refused`) doivent dire « ok ».

## À suivre

Il reste des gros morceaux à sortir du navigateur, mais la logique est désormais claire : **ce qui calcule part côté service, ce qui affiche reste côté navigateur**. Le prochain chantier est le plus difficile de la file : le rendu du Markdown en HTML (bibliothèque `marked`, 52 Ko) et la coloration du code (`highlight.js`, 140 Ko) — à porter en C++, avec le défi du streaming (rendre un HTML sûr à partir d'un Markdown qui arrive mot à mot). Ensuite, la logique du fil de conversation (`thread.js`, 1 005 lignes) descendra aussi au service.

Rendez-vous à l'épisode 65 pour ce chantier-là. L'image qu'on prend, celle qui reste quand tout est fini : un navigateur qui n'est plus qu'un écran, et un service qui fait tout le reste avec les outils d'Apple.
