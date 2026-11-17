# Plan — Renommer Albireo en Gulliver, puis profils côté service

**Trois blocs** : **0** renomme le projet **Albireo → Gulliver** (fait en premier, sur une
base propre), **A** ajoute le stockage **par profil**, **B** déplace le **RAG en C++** avec
moteurs et index au choix. Dans tout ce qui suit, le nouveau nom est **Gulliver** ; les
chemins `host/`, `web/js/`… sont ceux de l'ancien dossier `.../external/albireo/`, renommé
en `.../external/gulliver/` au bloc 0.

> **Reprise** : l'avancement (fait / à faire, par sous-étape) est dans
> [logs/chantier-gulliver.md](../logs/chantier-gulliver.md). Lire ce fichier d'avancement
> **et** ce plan avant de continuer le chantier.

## Suivi du chantier et non-régression (consignes 2026-11-17)

**Fichier de suivi.** `logs/chantier-gulliver.md` liste **chaque sous-étape** (0a…0e,
A1…A12, B13…B19) avec une case **☐ à faire / ☑ fait**, la date, le commit, et une ligne
« reprise » (où on en est, quoi ensuite). Il est **mis à jour à la fin de chaque
sous-étape**, avant la suivante — pour qu'une coupure (session fermée, contexte perdu) ne
fasse jamais reperdre plus d'une sous-étape. Il complète `logs/reprise.md` (vue d'ensemble)
sans le remplacer.

**Aucune fonctionnalité perdue.** Un changement d'architecture (renommage, stockage et RAG
côté service) ne doit **rien retirer ni oublier** des fonctions actuelles. La section
« Inventaire de non-régression » ci-dessous (dressée depuis le code) est la **liste de
contrôle** : à la fin de chaque bloc, on vérifie que **chaque** fonction y figure encore et
marche. Toute fonction qu'on devrait volontairement retirer est signalée et validée par
l'utilisateur d'abord — jamais par omission.

## Context

Aujourd'hui, les données que Gulliver (l'actuel Albireo) crée dans la page (discussions, projets, réglages de
page, images, **et l'index RAG : passages + vecteurs**) vivent dans la base **IndexedDB du
navigateur**. Conséquences que l'utilisateur veut supprimer :

1. Changer de navigateur ou de machine (bientôt le Mac mini) **perd tout**, sauf un
   export/import manuel — et l'export actuel n'emporte même pas l'index RAG.
2. Il n'y a **aucun utilisateur** : la base est liée au navigateur et à l'adresse de la
   page, pas à une personne.

**But** : ajouter au **service C++** un stockage **par profil** (un simple nom, **sans mot
de passe** — usage perso sur un seul Mac) qui garde tout. La page lit et écrit alors côté
service. Un profil « barbidou » retrouve ses discussions, projets et documents indexés
depuis n'importe quel navigateur, **sans rien exporter**.

**Vrai cas d'usage** : assistant cybersécurité local confidentiel — les documents client ne
quittent pas le Mac. D'où les décisions de sécurité ci-dessous.

### Décisions de l'utilisateur (2026-11-17)
- **Portée** : TOUT côté service — discussions, projets, réglages de page, index RAG
  (passages + vecteurs), images.
- **Profil** = un simple nom, sans mot de passe.
- **Accès réseau** : profils **accessibles depuis le LAN sans mot de passe**, comme le chat
  (« je veux accéder depuis mon tél, mais sans mot de passe, mon wifi est chez moi, je le
  maîtrise »). **Pas de garde `from_this_mac` sur les routes profil.**
- **Tout pilotable depuis le téléphone** (« je ne vais pas intervenir sur le Mac, donc tout
  est changeable par tél, celui-là et les autres si ça existe »). On **retire la garde
  « Mac seul » `from_this_mac`** qui bloquait les réglages du relais. Relevé du code : cette
  garde n'existe qu'à **deux endroits**, tous deux pour le relais —
  `POST /albireo/v1/relay/test` (`host/server.cpp:1835`) et la clé `relay` de
  `PUT /albireo/v1/settings` (`host/server.cpp:2276`). Aucun autre réglage n'est protégé.
  Après retrait, **tous** les réglages (relais compris) se changent depuis le téléphone.
- **Données actuelles** : **partir à vide** — aucune migration depuis l'IndexedDB existant.
  On crée un profil neuf. (L'ancien IndexedDB n'est ni lu ni effacé.)
- **Nom** : « **profil** » = l'espace de l'utilisateur (ses discussions, docs, index) ;
  l'ancien « profil » de l'onglet Service (instantané de réglages) est renommé
  « **instantané de réglages** » dans l'UI pour lever l'ambiguïté.
- **Construire maintenant sur le M2** (malgré la pause des tests du Mac mini : il faudra
  donc lancer les tests nécessaires à cette fonctionnalité).

### Deux blocs applicatifs (décision 2026-11-17)
L'utilisateur veut aussi **alléger le navigateur de JavaScript, déplacer la logique en
C++**, et une **option pour changer le moteur de RAG**. Ces deux demandes se rejoignent :
le RAG passe en C++, et derrière une « prise » (interface) on branche plusieurs moteurs.
Ampleur choisie : **profils d'abord (bloc A), puis RAG en C++ juste après (bloc B)**.
Rappel : **l'affichage reste en JavaScript** (un navigateur n'exécute que ça). « Alléger le
navigateur » = déplacer la *logique* (données, RAG) en C++, laisser au navigateur la
*coquille d'affichage* qui appelle le service.

### Hors sujet (ne pas toucher)
- Le **backend Metal de dlib** (8 phases, fini et commité `fc0760b`).
- L'inférence, le relais, Pseudalis : inchangés (sauf retrait de la garde « Mac seul »).

---

## Ce qui existe déjà (à réutiliser)

**Côté service** (`host/`) :
- Base SQLite déjà en place : `host/store.{h,cpp}` via `dlib/sqlite` (`dlib::database`,
  `dlib::statement`), fichier `<data_dir>/albireo.db` (→ `gulliver.db`), PRAGMA WAL +
  `foreign_keys=ON`. Patron table (`CREATE TABLE IF NOT EXISTS`), migration
  (`PRAGMA table_info` + `ALTER TABLE ADD COLUMN`), lecture
  (`bind`/`exec`/`move_next`/`get_column_as_*`), écriture (`INSERT OR REPLACE`, JSON en TEXT
  via `json::dump()`). Chaque méthode sous `std::lock_guard<std::mutex>`, écritures via
  `writing()`. `bind_blob`/`get_column_as_blob` disponibles, inutilisés aujourd'hui.
- Routes HTTP (`host/server.cpp`, `install_routes`) : `srv.Get/Post/Put/Delete`,
  `parse_body`, `send_json`, `send_error`. Routes paramétrées par regex + `req.matches[1]`.
- Garde « Mac seul » : `from_this_mac(svc, req)` (`host/server.cpp:1098`) +
  `refuse_relay_elsewhere` → 403. **Retirée** par ce chantier.
- Documents déjà sur disque : `documents_dir()` = `<data>/documents`, upload en flux
  dédupliqué SHA-256, validation `document_name()`.
- Embeddings : `POST /v1/embeddings`, sortie base64 → floats via `floats_of()`.
- `data_dir()` = `$ALBIREO_HOME` (→ `$GULLIVER_HOME`) sinon `$HOME/.local/share/albireo`
  (→ `/gulliver`).

**Côté navigateur** (`web/js/`) :
- `store.js` est le **seul** accès aux données (15 modules l'importent ; aucun accès direct
  à `indexedDB` ailleurs). ~30 fonctions, **toutes déjà `async`/Promise**.
- RAG : tout le scoring (BM25 + cosinus + MMR) se fait **au navigateur** sur des
  `Float32Array`, avec cache LRU de 8 owners (`rag.js:206-238`).
- Incognito : purement client ; les discussions incognito ne touchent jamais `store.js`
  (Map mémoire dans `rag.js:206`, `images.js:34`). Gardes `!c.incognito`/`persistent`.
- Fallback : IndexedDB en panne → `state.incognito = true`, tout en mémoire (`app.js:105`).

---

## Architecture retenue

### Base de données : SQLite (choix acté 2026-11-17)
On **reste sur SQLite** (déjà en place, zéro installation, un fichier local). Raisons (un
seul Mac, un utilisateur) : volume modeste (vecteurs ≈ 1024 dims × 4 o ≈ 4 Ko/passage ;
~50 passages/document ; 1000 documents ≈ ~200 Mo ; SQLite tient plusieurs Go) ; PostgreSQL
n'apporte rien (concurrence multi-serveurs qu'on n'a pas) et coûte un serveur à maintenir ;
pas besoin de recherche vectorielle native (cosinus en mémoire). **Garder la porte
ouverte** : tout l'accès aux données passe par les méthodes de `store.cpp` ; si le volume
explose, on ajoute **sqlite-vec** ou on migre, sans toucher au reste. Images hors base
(fichiers) pour ne pas gonfler le `.db` ni ralentir `quick_check`.

### Stockage par catégorie
| Catégorie | Stockage | Pourquoi |
|---|---|---|
| conversations | SQLite `profile_conversations` (JSON entier + colonne `profile`) | Déjà en JSON ; patron `settings` |
| projects | SQLite `profile_projects` (JSON entier) | Idem |
| réglages de page | SQLite `profile_page_settings` (clé/valeur **par profil**) | Distinct des `settings` service |
| passages RAG | SQLite `profile_chunks` (texte), lié à un **index** | Petits, nombreux, source |
| vecteurs RAG | **BLOB** dans `profile_chunks.vector` (nullable) | Recalcul = 15 s sur le chemin critique → inacceptable |
| index RAG (nommés) | table `profile_indexes` (bloc B) | Plusieurs index par profil, bascule sans réindexer |
| images | **Fichiers** sous `<data>/profiles/<id>/images/` + table `profile_images` | Cohérent avec `documents_dir()` |

Clé de partition : **`profile` = id opaque** (hex, comme `uid()`), jamais le nom.
Catalogue `profiles(id, name UNIQUE, created, last_used)`. Toutes les tables de données ont
`profile TEXT NOT NULL` en tête de clé + `FOREIGN KEY … ON DELETE CASCADE`.

### Transport du profil
En-tête HTTP **`X-Gulliver-Profile: <id>`** sur les routes de données. Dernier profil dans
`localStorage['gulliver.profile']` (juste un id). Helper service `resolve_profile(svc, req)`
: lit l'en-tête, valide, met à jour `last_used`, sinon 400 `profile_required` / 404
`profile_unknown`.

### Accès réseau : tout ouvert sur le LAN (sans mot de passe)
Wi-Fi personnel maîtrisé, pilotage depuis le téléphone. Routes `/gulliver/v1/profiles` et
`/gulliver/v1/profile/*` accessibles depuis le LAN comme le chat, **sans** `from_this_mac`.
La garde « Mac seul » du relais est **retirée** (supprimer les deux appels `from_this_mac`,
`host/server.cpp:1835` et `:2276`, les helpers + code `relay_mac_only`). Trace doc :
`web/changelog.md`, `web/openapi.json`, `tools/make_openapi.py`, `web/js/service.js`, entrée
notes-pour-l-auteur (« aussi sous Windows », inversion d'un choix de sécurité).

### Bascule de la page
**Réécrire `store.js` en client HTTP**, **API inchangée** (mêmes ~30 signatures async). Le
scoring RAG reste au navigateur au bloc A (vecteurs rapatriés base64 → `Float32Array`,
cache LRU conservé) ; passe en C++ au bloc B.
- `updateConversations` → **révision optimiste** (`updated`, 409 si périmé, relire+rejouer).
- `replaceChunks` → **un PUT par document** (DELETE+INSERT transactionnel, `bind_blob`).
- `claimLease`/`releaseLease` → **pas** côté service ; garder `navigator.locks` +
  `BroadcastChannel`.

### Incognito
Inchangé (gardes `persistent`/`!c.incognito` court-circuitent `store.*`). Garde-fou
défensif : en incognito, les fonctions de persistance de `store.js` sont des no-op.
Fallback « service injoignable → mode mémoire ».

---

## Bloc 0 — Renommer Albireo → Gulliver (fait en premier)

- **Tout renommer, API comprise** : routes `/gulliver/v1/*`, base `gulliver.db`,
  `~/.local/share/gulliver`, `GULLIVER_HOME`, `DB_NAME='gulliver'`, clés `gulliver.*`.
- **Renommer le dossier** `dlib/dlib/external/albireo/` → `.../gulliver/`, **garder le nom du
  dépôt** GitHub. (Renommer le dépôt plus tard reste possible et simple : renommage GitHub,
  l'ancienne URL redirige, `git remote set-url origin …` ; fork et `upstream` intacts.)
- **D'abord le renommage**, puis A et B sur le nom propre.

**Ampleur** : ~3982 occurrences de « albireo » dans 237 fichiers (hors `build/`, `.git/`) ;
204 chemins `/albireo/v1`. **Casse** : `albireo` (ids, routes, chemins, clés), `Albireo`
(texte FR), `ALBIREO` (`ALBIREO_HOME`, macros), `ALBIREO_EVENT_*`. Ne **pas** toucher : nom
du dépôt/remotes, fichiers de dlib « pur » non liés à Albireo, noms de modèles tiers.
**Garde-fou** : passe scriptée mais **relue**, puis build + tests (un id oublié casse la
compilation, une route oubliée est attrapée par `page_test.sh`).

0a. **Dossier + build.** `git mv .../albireo .../gulliver` ; `CMakeLists.txt`, cibles, mémo,
    commandes de build (`build/macos-albireo` → `build/macos-gulliver`). *Vérif* : `cmake`
    configure ; le service compile.
0b. **Identifiants techniques (C++).** `host/`, `common/` : `/gulliver/v1`, `gulliver.db`,
    `GULLIVER_HOME` + `~/.local/share/gulliver`, macros `ALBIREO_*` → `GULLIVER_*`, symboles
    `*_EVENT_*`. *Vérif* : compile et démarre ; `curl /gulliver/v1/status` répond.
0c. **Page (JS).** `web/js/` : `DB_NAME='gulliver'`, API `/gulliver/v1`, clés `gulliver.*`,
    `openapi.json` + `make_openapi.py`, titres/textes. *Vérif* : `page_test.sh` passe.
0d. **Texte, doc, outils.** Interface FR, `bump_version.py`, README, docs du fork, blog
    (nouveaux épisodes ; ne pas réécrire les publiés), `macos/site-docs/sommaire.json`,
    CLAUDE.md. *Vérif* : plus d'occurrence parasite de « albireo » (hors historique assumé).
0e. **Version + journal + commit.** `bump_version.py` (**majeur** — rupture de nom et d'URL),
    `changelog.md` (Plateforme), épisode de blog, journal, reprise. *Vérif* : `version.h` a
    sa section.

---

## Bloc A — Profils côté service (12 sous-étapes)

Versionnage : **mineur** à la fin. Doc à jour à chaque sous-étape. Service (1–6) testé au
`curl` + test C++ ; page (7–12) par `tools/page_test/page_test.sh`.

1. **Schéma + accès profil (service).** `host/store.{h,cpp}` : struct `profile`, tables +
   migration, `profiles/find_profile/put_profile/remove_profile/touch_profile`. *Vérif* :
   CRUD profil, `quick_check` ok.
2. **Routes de gestion + `resolve_profile`.** `host/server.cpp` : `GET/POST/PUT/DELETE
   /gulliver/v1/profiles`, `resolve_profile`, erreurs. *Vérif* : `curl` CRUD.
3. **Conversations + projets.** store + routes `/gulliver/v1/profile/conversations…` et
   `…/projects…`, révision optimiste. *Vérif* : isolation entre profils.
4. **Réglages de page.** `profile_page_settings`, routes `GET/PUT …/page-settings`.
   *Vérif* : clé sous A, absente sous B.
5. **Chunks RAG + vecteurs BLOB.** `replace_chunks` (transactionnel, `bind_blob`),
   `chunks_of/chunk_status/chunk_documents/delete_chunks`. *Vérif* : round-trip vecteurs.
6. **Images sur disque.** `host/service.h` (`profiles_dir()`), `profile_images`, routes
   upload/blob/thumb/usage/delete. *Vérif* : round-trip, `usage`, cascade.
7. **Réécriture `store.js` en client HTTP + `profile.js`.** `web/js/store.js`,
   `web/js/profile.js`, `web/js/api.js`. Leases → Web Locks. Garde incognito. *Vérif* :
   ESLint + persistance via `curl`.
8. **Sélecteur de profil (UI).** `web/js/dialogs.js`, `layout.js`, `app.js`, `i18n.js`,
   `navigation.js`. *Vérif* : sélecteur au démarrage, bascule recharge.
9. **RAG : décodage des vecteurs rapatriés (transitoire — remplacé au bloc B).**
   `web/js/rag.js`. *Vérif* : scénario RAG du banc d'essai.
10. **Retirer la garde « Mac seul » du relais.** `host/server.cpp` (L1835, L2276, helpers),
    `openapi.json` + `make_openapi.py`, `service.js`, `changelog.md`, notes-pour-l-auteur.
    *Vérif* : réglage relais depuis le LAN réussit ; `page_test.sh` passe.
11. **Renommer « profil » → « instantané de réglages » (UI).** `web/js/service.js` + textes.
    *Vérif* : l'onglet Service dit « instantané de réglages ».
12. **Doc + version.** `changelog.md`, `bump_version.py minor`, `openapi.json`, blog,
    journal, reprise, différences, notes-pour-l-auteur. *Vérif* : critères CLAUDE.md.

---

## Bloc B — RAG en C++ + moteurs + index (étapes 13–19)

**But** : déplacer le moteur de recherche documentaire en C++, derrière une **interface**
`rag_engine` (`index`, `search`), trois implémentations : **`hybrid`** (défaut, mots +
vecteurs + MMR, porté fidèlement), **`dense`** (vecteurs seuls), **`lexical`** (mots seuls,
sans modèle d'embedding). Moteur = réglage de service.

**Index nommés et bascule** (« plusieurs indexations, sauvegarde nominative pour switch »).
Piège central : **vecteurs de deux modèles d'embedding incomparables** ; changer le
découpage rend les passages caducs. Donc chaque **index** = `{modèle, passage_chars,
overlap_chars}`, nommé, conservé à part. Table `profile_indexes(profile, id, name,
embed_model, passage_chars, overlap_chars, created, active)` ; `profile_chunks.index_id`.
Un index actif par profil, bascule instantanée. **Au changement de modèle/découpage**,
Gulliver **demande** : garder l'ancien index à côté, ou remplacer. Jamais de mélange
silencieux.

**Réglages RAG exposés** : moteur (hybride/vecteurs/mots), modèle d'embedding, découpage
(`passage_chars`+`overlap_chars`), top-k, reranker. Fiche 🦉🦫🐝 + schéma dans `service.js`,
`validate_settings`, `recommended`, `make_openapi.py`.

**Parité d'abord** : `hybrid` C++ doit retrouver les mêmes passages que `rag.js` (test de
parité) avant de retirer le JS. **Vecteurs** : plus rapatriés au navigateur (l'étape 9
tombe) ; lus côté service.

13. **Interface `rag_engine` + `lexical`.** `host/rag_engine.{h,cpp}`, découpage (port de
    `chunk()`, `passageChars=1200`/`overlapChars=240`) + BM25, route
    `POST /gulliver/v1/profile/search`. *Vérif* : parité découpage+BM25, `curl`.
14. **Moteur `dense`.** Cosinus sur BLOB. *Vérif* : test C++.
15. **Moteur `hybrid` + parité JS.** Fusion + MMR. *Vérif* : **parité** avec `rag.js`.
16. **Index nommés + bascule (service).** `profile_indexes`, `index_id`, méthodes + routes.
    *Vérif* : deux index coexistent, bascule sans recalcul, cascade.
17. **Réglages RAG (service + UI).** `host/settings.{h,cpp}`, `service.js`, `recommended`,
    `make_openapi.py`, question « garder/remplacer ». *Vérif* : chaque clé a sa carte.
18. **Basculer la page sur le RAG du service, retirer le RAG JS.** `web/js/rag.js` réduit,
    `indexing.js`. *Vérif* : scénario RAG inchangé, ESLint, taille JS baisse.
19. **Mesure + doc + version.** Comparaison JS vs C++ (mesure alternée), `bump_version.py
    minor`, changelog, openapi, blog, journal, reprise, différences, notes-pour-l-auteur.

---

## Inventaire de non-régression (dressé depuis le code, 2026-11-17)

Liste de contrôle : **chaque** fonction doit encore exister et marcher à la fin de chaque
bloc. `[IDB]` = stockage navigateur (déménagé au bloc A) ; `[RAG-nav]` = calcul au
navigateur (déplacé au bloc B). Rien n'est retiré sans accord explicite.

**1. Chat** : envoi (Entrée configurable), streaming + Markdown incrémental, arrêt,
régénération, édition d'une question (tronque la suite), rejouer, supprimer une question,
copier/exporter (MD/JSON), réflexion repliable + chrono, étapes d'agent, appels d'outils,
mode agent (budgets, garde anti-répétition), accès Web, mémoire `[IDB]`, résumé/compaction,
épinglage, titres automatiques, incognito (mémoire), multi-onglets `[IDB]`, warmup, réponses
en arrière-plan + « non lu », effort de raisonnement, ligne contexte + débit.

**2. Projets** : créer (couleur), éditer (nom/desc/instructions), joindre documents (≤100)
`[IDB]`, joindre images (≤10) + description auto, état d'indexation, mode « entier/recherche »,
rattacher/détacher discussions, discussions du projet, supprimer (détache), glisser-déposer.

**3. Documents / RAG** `[IDB]` `[RAG-nav]` : formats texte/code, Office (docx/pptx/xlsx),
ODF, PDF (pdf.js, scannés→images), RTF, HTML ; refus hérités ; limites (100 Mo / 4 M car,
anti-bombe ZIP) ; découpage (nommage page/diapo/feuille) ; indexation (`/v1/embeddings`) ;
recherche hybride BM25+cosinus+MMR ; repli mots seuls ; extraits au contexte (« entier » vs
« recherche ») ; statut d'indexation ; outils `search_documents`/`document_outline`/
`read_document` ; documents du Mac ; élagage des passages orphelins.

**4. Images** `[IDB]` : joindre (PNG/JPEG/WebP/GIF/BMP/AVIF), décodage+redimension+vignette,
limites (15 Mo, refus HEIC/TIFF), places de contexte (≤10), vignettes + visionneuse
(clavier), grisées hors contexte, collage + glisser-déposer, description auto + réessayer,
PDF scanné en vision, envoi data-URL (ou `[Image: nom]`), mesure tokens (`/count`), usage,
élagage des images orphelines.

**5. Modèles** : catalogue (fiches, conseillé/possible/déconseillé selon RAM), choisir
(3 rôles), télécharger/annuler + progression, supprimer/vérifier, rafraîchir, importer
(flux + SHA-256), bibliothèque du Mac (clone APFS + projecteur), Hugging Face
(recherche/fichiers/récents/ajout mmproj), rôles (general/code/security, embedding, vision,
arrière-plan), projecteurs mmproj.

**6. Relais** : cadenas par discussion, modèle `albireo-relay` (→ `gulliver-relay`),
réglages (fournisseur, adresse, modèle, auth, contexte, max_output, images, reasoning), test,
pseudonymisation Pseudalis + « garder les pseudonymes », secret masqué, images décrites
localement, web natif du modèle relayé. **NB** : garde « Mac seul » retirée (A10), la
fonction relais reste entière.

**7. Réglages de service** : `accelerator`, `gpu_prompt_tokens/context`, `threads`,
`spin_us`, `max_sequences`, `context`, `kv_mb`, `kv_type`, `warm_history`, `models_dir`,
`library_dir`, `reserve_gb`, `lan`, `lan_port`, `port`, `offline`, `web_access`,
`web_search`, `proxy`, `relay` ; réglages conseillés (`recommended` + écarts) ; instantanés
de réglages (ex-« profils ») ; réserve (place/vidage) ; appliquer+redémarrer ; backing côté
service.

**8. Interface** : navigation par hash (`#/c`, `#/p`, `#/licenses`), panneau latéral (repli
persisté), barre du haut (titre éditable, menus), sélecteur Albireo|Pseudalis, thème
clair/sombre/système `[IDB]`, Markdown (marked+DOMPurify+KaTeX+highlight, copier), champ
Markdown, recherche discussions/projets (surlignage) `[IDB]`, export/import, effacer `[IDB]`,
bannières d'état, toasts, sauts début/fin + suivi auto, brouillons par discussion `[LS]`,
accessibilité (aria/focus-trap), PWA, français seul, sandbox JS (`run_javascript`).

**9. Carnet** : page `/carnet/` (sommaire + docs Markdown), thème propre `[LS]`, routes
`/carnet` + `/carnet/file`, accès LAN, « sitelog », accès depuis le menu.

**10. API / routes** : compat OpenAI (`/v1/models`, `/chat/completions`, `/completions`,
`/embeddings`) ; Albireo→Gulliver (`/status`, `/models`, `/speed`, `/pseudalis`, `/carnet*`,
`/reserve`, `/usage`, `/engines*`, `/licenses*`, `/count`, `/web/search|fetch`, `/relay*`,
`/models/refresh`, `/hub/*`, `/models/{id}/download|activate|deactivate|verify`,
`/models/{id}` DELETE, `/import`, `/models/import|upload`, `/library*`, `/documents*`,
`/settings/profiles`, `/settings`, `/restart`, `/logs`, statique `/`, `/docs`,
`/openapi.json`). **Toutes** renommées `/albireo/` → `/gulliver/` au bloc 0.

**11. Données (onglet Data)** `[IDB]` : contenu stocké (compte + octets), export/import,
effacer, persistance (`navigator.storage.persist`), estimation d'espace. Après le bloc A,
ces vues portent sur le **service** (par profil).

**12. Autres** : onglets Général (thème, nom, Entrée, prompt système), Génération
(température, top-p, top-k, max_tokens auto), Mémoire (activer, texte, taille, reset),
À propos (version, moteur, CPU/RAM/GPU, modèles actifs, usage 30 j, auteurs/dépôt), Licences ;
prompt système + nom ; outils agent locaux (`calculator`, `run_javascript`,
`search_documents`, `document_outline`, `read_document`, `search_discussions`,
`current_datetime`, `create_file`) ; outils web (`web_search`, `fetch_page`) ; estimation de
tokens ; détection GPU ; fiches d'aide 🦉🦫🐝 ; garde anti-dépôt de fichiers ; Swagger UI.

**Garde-fou RAG (bloc B)** : l'extraction de texte des documents (docx/pptx/xlsx/odf/pdf/
rtf/html) et le décodage/redimensionnement des images restent **au navigateur** ; seuls le
**découpage, l'index et la recherche** passent en C++. On ne perd pas la lecture des formats.

---

## Vérification de bout en bout (sur le Mac)

1. Lancer le service, ouvrir `https://localhost:<port>` dans Safari.
2. Créer le profil « barbidou », une discussion, poser une question.
3. Joindre un document à un projet, l'indexer, vérifier les extraits.
4. Ouvrir dans **Chrome**, choisir « barbidou » → retrouver tout (RAG **sans réindexation**).
5. 2ᵉ profil « test » → app vide ; revenir à « barbidou » → tout est là.
6. Depuis le téléphone (Wi-Fi) : choisir « barbidou » → mêmes données (accès ouvert).
7. (Bloc B) Changer le moteur de RAG, vérifier que les extraits changent, que « mots seuls »
   marche sans modèle d'embedding.

**Automatisé** : test C++ (CRUD profil + cascade + round-trip vecteur/image) ;
`page_test.sh` avec un scénario `profiles.json` (créer, indexer, isolation, persistance ;
adapter le harnais pour choisir/créer un profil par défaut au chargement).

---

## Risques et points de vigilance
- **Chemin critique (deadline 15 s)** : localhost rapide ; première recherche d'un owner
  télécharge ses vecteurs en une requête, cache LRU conservé.
- **Taille de la base** : vecteurs BLOB ; images dehors. `quick_check` au démarrage parcourt
  la base — surveiller.
- **Robustesse** : service injoignable → mode mémoire ; écritures idempotentes.
- **Discussion courante** : dans l'URL + `state.current` ; au changement de profil,
  réinitialiser le hash.
- **Ordre de démarrage** : réglages de page + liste d'abord, prune en arrière-plan.
- **Emplacement du moteur RAG C++** : sous `host/` (pas `dlib/dlib`) → hors registre dlib.

### Fichiers les plus touchés
**Bloc A** : `host/store.cpp`, `host/store.h`, `host/server.cpp`, `host/service.h`,
`web/js/store.js`, `web/js/profile.js` (nouveau), `web/js/api.js`, `web/js/rag.js`,
`web/js/app.js`, `web/js/dialogs.js`, `web/js/navigation.js`, `web/js/service.js`,
`web/js/i18n.js`, `tools/page_test/scenarios/profiles.json` (nouveau), `web/changelog.md`.
**Bloc B** : `host/rag_engine.{h,cpp}` (nouveau), `host/server.cpp`, `host/settings.{h,cpp}`,
`web/js/rag.js`, `web/js/indexing.js`, `web/js/service.js`, `tools/make_openapi.py`, test de
parité (C++).
