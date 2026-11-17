# Avancement — Chantier Gulliver (renommage + profils + RAG en C++)

> **À lire pour reprendre** : ce fichier (où on en est) **et** le plan
> [docs/chantier-gulliver-plan.md](../docs/chantier-gulliver-plan.md). Mettre ce fichier à
> jour **à la fin de chaque sous-étape**, avant la suivante : une coupure ne doit jamais
> faire reperdre plus d'une sous-étape.

**Légende** : ☐ à faire · ⏳ en cours · ☑ fait (date, commit).

## Reprise (où on en est, quoi ensuite)

- **2026-11-17 20:15** — **0a fait** : dossier renommé (`git mv .../albireo → .../gulliver`),
  chemins `external/albireo` → `external/gulliver` corrigés dans les scripts du projet
  (`tools/bench_macos.sh`, `test_macos.sh`, `page_test/page_test.sh` + README) et la commande
  d'usage du `CMakeLists.txt`. **Build `build/macos-gulliver` compile sans avertissement**,
  binaires produits. Report à 0d : répertoires de sortie `build/macos-albireo`/`albireo-home`,
  README de l'original (Windows/CUDA/OpenVINO). Cibles/variables `ALBIREO_*` → **0b**.
  **Prochaine action : 0b** — renommer les identifiants techniques C++ (routes `/gulliver/v1`,
  `gulliver.db`, `GULLIVER_HOME`, `~/.local/share/gulliver`, macros `ALBIREO_*` → `GULLIVER_*`,
  cibles CMake `albireo-*`). Attention : `DB_NAME` de la page = 0c ; ici c'est le C++.
- **2026-11-17 20:07** — Plan validé (go de l'utilisateur). Plan copié dans le dépôt, ce
  fichier d'avancement créé, pointeur ajouté à CLAUDE.md.

## Décisions clés (rappel)

- Renommer **Albireo → Gulliver** partout (API, base, dossiers, textes). Dossier renommé,
  **dépôt GitHub gardé**.
- Stockage **par profil** côté service (un nom, sans mot de passe), **tout ouvert sur le
  LAN**, **départ à vide**, **garde « Mac seul » du relais retirée**.
- **SQLite** (accès isolé derrière `store.cpp`).
- RAG en **C++** au bloc B, **3 moteurs** (hybride/vecteurs/mots) + **index nommés** + réglages.
- **Aucune fonctionnalité perdue** : voir l'inventaire de non-régression du plan, vérifié à
  la fin de chaque bloc.

---

## Bloc 0 — Renommer Albireo → Gulliver

- ☑ **0a** Dossier + chemins de build (`git mv`, chemins `external/gulliver`). 2026-11-17,
  commit à venir. `cmake` configure et `cmake --build build/macos-gulliver` compile sans
  avertissement ; binaires produits (`albireo-service`, moteur, 10 tests). Cibles/variables
  `ALBIREO_*` et répertoires de sortie `build/macos-albireo`/`albireo-home` : reportés (0b/0d).
- ☐ **0b** Identifiants techniques C++ (routes `/gulliver/v1`, `gulliver.db`,
  `GULLIVER_HOME`, macros `ALBIREO_*`).
- ☐ **0c** Page JS (`DB_NAME`, API, clés, openapi, textes).
- ☐ **0d** Texte, doc, outils (interface, bump_version, README, docs du fork, blog, CLAUDE.md).
- ☐ **0e** Version (majeure) + changelog + blog + journal + reprise + commit.

## Bloc A — Profils côté service

- ☐ **A1** Schéma + accès profil (service).
- ☐ **A2** Routes de gestion + `resolve_profile`.
- ☐ **A3** Conversations + projets.
- ☐ **A4** Réglages de page.
- ☐ **A5** Chunks RAG + vecteurs BLOB.
- ☐ **A6** Images sur disque.
- ☐ **A7** Réécriture `store.js` en client HTTP + `profile.js`.
- ☐ **A8** Sélecteur de profil (UI).
- ☐ **A9** RAG : décodage des vecteurs rapatriés (transitoire).
- ☐ **A10** Retirer la garde « Mac seul » du relais.
- ☐ **A11** Renommer « profil » → « instantané de réglages » (UI).
- ☐ **A12** Doc + version (mineure).

## Bloc B — RAG en C++ + moteurs + index

- ☐ **B13** Interface `rag_engine` + moteur `lexical` (mots seuls).
- ☐ **B14** Moteur `dense` (vecteurs seuls).
- ☐ **B15** Moteur `hybrid` + parité avec le JS.
- ☐ **B16** Index nommés + bascule (service).
- ☐ **B17** Réglages RAG (service + UI).
- ☐ **B18** Basculer la page sur le RAG du service, retirer le RAG JS.
- ☐ **B19** Mesure + doc + version.

---

## Journal du chantier (du plus récent au plus ancien)

### 2026-11-17 20:07 — Démarrage
- Plan validé et copié dans le dépôt (`docs/chantier-gulliver-plan.md`).
- Fichier d'avancement créé. Pointeur ajouté à CLAUDE.md.
- Aucune modification de code encore. Prochaine étape : 0a.
