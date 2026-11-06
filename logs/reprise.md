# Reprise — portage Mac de Dlib-Transformer-extensions

> À lire en premier à chaque session (règle de CLAUDE.md). Dernière mise à jour : 2026-11-06, sur le Mac M2.

## Objectif

Faire tourner le projet sur Mac Apple Silicon : d'abord dlib et les exemples SLM, puis le moteur et le serveur d'Albireo et de Pseudalis (leur variante Linux, lancée avec `--console`). Les parties purement Windows (icône, installateur, service, lecteur, Explorateur) sont hors du périmètre.

## Fait

- 2026-11-05 : remote `upstream` ajouté (Cydral). Les 4 commits de l'original fusionnés dans `main` (Pseudalis 1.0.1 et 1.0.2, détection en IT/ES/DE), sans conflit. Commit de fusion `0abb591`, **pas poussé** (voir Problèmes ouverts).
- 2026-11-05 : build CPU de référence (dlib + exemples) : **compilé, 0 erreur**, 24 exemples `slm_*` en Mach-O arm64, dans `build/macos-arm64-cpu/`. Pas encore lancés.
- 2026-11-05 : Albireo et Pseudalis configurés et compilés avec `-k 0` pour relever toutes les erreurs : 5 causes identifiées (liste ci-dessous). Logs : `build/macos-albireo/build.log` et `build/macos-pseudalis/build.log`.
- 2026-11-05 : résumé des erreurs envoyé à la session « Mode mock sans API » (Remote Control, autre machine) ; lecture non confirmée.
- 2026-11-06 : CLAUDE.md, ce fichier et `logs/journal-modifications.md` créés ; section « État constaté » ajoutée au mémo de portage ; blog ouvert en français (`blog/`, pages 00 et 01). L'humain a validé CLAUDE.md et autorisé commit et push ; commit fait, push refusé (403, voir Problèmes ouverts).

- 2026-11-06 : 10 nouveaux commits de l'original fusionnés (commit `037a32e`, 78 fichiers, sans conflit) : Albireo 1.0.9 à 1.0.11, Pseudalis 1.0.3, GGUF sur CPU (`dlib/dlib/cuda/cpu_gguf.*`). La fusion touche `albireo/CMakeLists.txt` (+50 lignes) et `pseudalis/CMakeLists.txt`.

- 2026-11-06 : le push vers PapyTortueTech restant refusé (403), l'humain a choisi de déménager le fork sur `Aughra`. Fork de Cydral créé sur Aughra (public), `origin` repointé, `main` poussé : le Mac et le fork en ligne sont à égalité.

- 2026-11-06 : blog refait à partir des 3 épisodes du carnet TortueTech (pages 01 à 03), plus l'épisode 4 « Le fork déménage ». Prochain épisode : la nouvelle compilation et les premières corrections.

## En cours

Rien. Prochaine étape : **recompiler Albireo et Pseudalis** pour revoir l'état après la fusion du 2026-11-06 (le CMake d'Albireo a changé : les erreurs 2 à 5 peuvent avoir bougé), puis corriger les erreurs une par une.

## À faire, dans l'ordre proposé

1. **Pseudalis, `st_mtim`** : `dlib/dlib/external/pseudalis/host/drive_folders.cpp`, lignes 448, 485 et 518. Sur macOS, le champ s'appelle `st_mtimespec`. Critère : `pseudalis-service` compile.
2. **Albireo, frameworks Apple** : lier `CoreFoundation` et `Security` (via `-framework`) à la cible qui contient `http_fetch.cpp`, sous `if (APPLE)`. Critère : plus de symboles `_CF*` ni `_Sec*` manquants.
3. **Albireo, libpng NEON** : soit compiler les sources `arm/` de libpng, soit définir `PNG_ARM_NEON_OPT=0` pour la cible `albireo_image`. Critère : plus de symboles `_png_*_neon` manquants.
4. **Albireo, `endian.h`** : `dlib/dlib/external/svox_pico/lib/picopltf.h:78`. Sur macOS, utiliser `<machine/endian.h>` ou `<libkern/OSByteOrder.h>`, sous `#ifdef __APPLE__`. Critère : `albireo_pico` compile.
5. **Moteurs `avx2`** (Albireo et Pseudalis) : ne pas construire les cibles `*-avx2` sur arm64, et vérifier que le moteur portable (ou un moteur NEON) prend leur place. C'est le point le plus important pour la vitesse. Critère : les deux builds finissent sans erreur.
6. Lancer les tests : `dtest` (tests dlib), `test_engine` et `test_host` (Pseudalis), les tests d'Albireo, puis le service en `--console`.

Après chaque point : mettre à jour la section « État constaté » du mémo, ce fichier et le journal, puis commiter. Quand Albireo ou Pseudalis compile en entier pour la première fois, c'est une grande étape : elle a sa page de blog.

## Décisions

- Mac Apple Silicon uniquement ; Windows hors du périmètre.
- Modifier le moins possible les fichiers de l'original, de préférence sous `if (APPLE)` / `#ifdef __APPLE__`, pour garder des fusions faciles avec upstream.

## Problèmes ouverts

- L'ancien fork `PapyTortueTech/Dlib-Transformer-extensions` n'est plus utilisé ; l'humain s'occupe de l'arrêter. Ne plus jamais y pousser.
- Les 24 exemples `slm_*` n'ont pas été lancés ; `dtest` n'est pas encore construit.

## Commandes pour reprendre

```sh
cd ~/dev/Dlib-Transformer-extensions
git status -sb && git fetch upstream && git log --oneline main..upstream/main
cmake --build build/macos-pseudalis -- -k 0   # puis albireo, après chaque correction
```
