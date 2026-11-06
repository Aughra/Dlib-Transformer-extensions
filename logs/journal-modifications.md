# Journal des modifications — fork Mac

Le plus récent en haut. Une entrée par intervention ; on n'en réécrit jamais une ancienne.

## 2026-11-06 — Poser les règles du fork et les documents de suivi

- **Quoi** : création de CLAUDE.md (règles du fork : ton doux et concis, but Mac seul, deux remotes, règle de fusion facile, reprise, journal, la documentation suit le code, blog en français, Git), de `logs/reprise.md` et de ce journal ; ajout au mémo de portage des commandes de build d'Albireo et de Pseudalis et d'une section « État constaté » ; blog ouvert avec ses deux premières pages (00 « Deux cahiers », 01 « Première compilation »).
- **Fichiers** : `CLAUDE.md`, `logs/reprise.md`, `logs/journal-modifications.md`, `docs/memo-portage-macos-arm64.md`, `blog/README.md`, `blog/00-prologue.md`, `blog/01-premiere-compilation.md`.
- **Pourquoi** : reprendre le portage d'une séance à l'autre sans rien perdre, sur le modèle du CLAUDE.md du projet miaou ; garder un mémo qui dit ce qui a vraiment été vérifié ; raconter le portage étape par étape.
- **Étapes** : rédaction seule, aucun build. Blog d'abord écrit en deux langues, puis ramené au français seul à la demande de l'humain.

## 2026-11-05 — Fusionner l'original et compiler pour la première fois sur Mac

- **Quoi** : remote `upstream` (Cydral) ajouté ; fusion de 4 commits de l'original (commit `0abb591`, sans conflit, pas poussé : 403). Premier build sur Mac M2 : dlib + exemples compilés sans erreur ; Albireo et Pseudalis configurés, compilation en échec sur 5 causes (moteurs avx2, `endian.h`, libpng NEON, frameworks Apple non liés, `st_mtim`).
- **Fichiers** : aucun fichier source modifié ; builds dans `build/` (ignoré par git).
- **Pourquoi** : suivre l'original et mesurer ce qui reste à porter.
- **Étapes** : `git merge upstream/main` ; `cmake` + `ninja -k 0` pour `dlib/examples`, `albireo` et `pseudalis` ; résumé des erreurs envoyé à la session « Mode mock sans API ».
