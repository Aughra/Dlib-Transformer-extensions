# Journal des modifications — fork Mac

Le plus récent en haut. Une entrée par intervention ; on n'en réécrit jamais une ancienne.

## 2026-11-06 — Faire compiler Albireo et Pseudalis sur ARM64

- **Quoi** : correction des 5 causes d'échec : moteurs `avx2` non construits sur ARM64 (tests d'Albireo en noyaux portables), `<machine/endian.h>` pour `svox_pico` sur macOS, sources NEON de libpng pour `albireo_image` sur ARM64, `CoreFoundation` et `Security` liés à `dlib_httplib` sur macOS, `st_mtim` → `st_mtimespec` sur macOS. Albireo et Pseudalis compilent en entier.
- **Fichiers** : `dlib/dlib/CMakeLists.txt`, `dlib/dlib/external/albireo/CMakeLists.txt`, `dlib/dlib/external/pseudalis/CMakeLists.txt`, `dlib/dlib/external/pseudalis/host/drive_folders.cpp`, `dlib/dlib/external/svox_pico/lib/picopltf.h` ; `docs/memo-portage-macos-arm64.md`, `logs/reprise.md`, ce journal, `blog/05-cinq-corrections.md`, `blog/README.md`.
- **Pourquoi** : porter les deux applications sur Mac, avec des changements limités au Mac ou à ARM64 pour ne pas gêner les fusions avec l'original.
- **Étapes** : recompilation d'abord sans rien changer (mêmes 5 causes après la fusion) ; corrections ; vérifié que `PICO_ENDIANNESS` vaut bien `ENDIANNESS_LITTLE` sur macOS (le premier essai de la correction de `endian.h` aurait donné « big endian » : macOS ne définit pas `__BYTE_ORDER`) ; recompilation : 0 erreur ; tests unitaires des deux applications passés ; `test_linux.sh` de Pseudalis : 26 étapes sur 28 (`nproc` et `stat` de GNU fournis par le `PATH`). Exemples dlib recompilés après la modification de `dlib/dlib/CMakeLists.txt`.

## 2026-11-06 — Reprendre les trois épisodes du carnet TortueTech en tête du blog

- **Quoi** : les épisodes 1 à 3 du carnet TortueTech (`/carnet/dlib-transformer-extensions-macos`, `-compilation`, `-albireo-pseudalis`) deviennent les pages 01 à 03 du blog ; ils remplacent les pages 00 et 01 écrites par Claude, qui racontaient la même chose. Épisode 4 « Le fork déménage » écrit dans le même ton. Règles du blog de CLAUDE.md réécrites d'après ces épisodes (épisodes, ton au « on », images, « Pour les curieux », « La suite »).
- **Fichiers** : `blog/01-le-fork-et-l-original.md`, `blog/02-compiler-dlib-et-les-exemples.md`, `blog/03-albireo-pseudalis-et-cinq-obstacles.md`, `blog/04-le-fork-demenage.md`, `blog/README.md`, `CLAUDE.md` ; supprimés : `blog/00-prologue.md`, `blog/01-premiere-compilation.md`.
- **Pourquoi** : demande de l'humain : partir des épisodes déjà publiés et continuer dans leur ton.
- **Étapes** : pages lues sur le serveur local du carnet ; texte repris tel quel, sans le menu, le pied de page ni les liens du glossaire du site, liens entre épisodes changés en liens vers les fichiers du blog. Vérifié : titres, sommaire, ni e-mail ni chemin privé.

## 2026-11-06 — Déménager le fork sur le compte Aughra

- **Quoi** : nouveau fork de `Cydral/Dlib-Transformer-extensions` sur le compte `Aughra` (public, seule possibilité pour un fork d'un dépôt public) ; `origin` pointe désormais sur `Aughra/Dlib-Transformer-extensions` ; tout `main` y est poussé (`f0f9f91..50c2c96`), commit `pour mac` compris.
- **Fichiers** : `CLAUDE.md` (table des dépôts distants), `logs/reprise.md`, ce journal.
- **Pourquoi** : le compte connecté sur le Mac (Aughra) n'avait pas le droit d'écrire sur le fork de PapyTortueTech (403). L'humain arrête ce fork-là.
- **Étapes** : `gh repo fork --clone=false --default-branch-only`, `git remote set-url origin`, `git push -u origin main`. Fork plutôt que dépôt privé : choix de l'humain (bouton « Sync fork », envoi léger).

## 2026-11-06 — Fusionner les 10 nouveaux commits de l'original

- **Quoi** : fusion de `upstream/main` dans `main` (commit `037a32e`, 78 fichiers, sans conflit) : Albireo 1.0.9 à 1.0.11, Pseudalis 1.0.3, modèles Qwen3-VL-Thinking, GGUF sur CPU.
- **Fichiers** : ceux de l'original ; côté fork, `logs/reprise.md` et ce journal.
- **Pourquoi** : rester proche de l'original, à la demande de l'humain.
- **Étapes** : `git fetch --all`, `git merge upstream/main` ; vérifié que la fusion touche `albireo/CMakeLists.txt` et `pseudalis/CMakeLists.txt`, mais pas les lignes des erreurs `st_mtim` et `endian.h`. Pas recompilé. Push tenté.

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
