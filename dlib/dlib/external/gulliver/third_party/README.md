# Bibliothèques d'autres projets

Le code de projets extérieurs que Gulliver compile avec lui. Chacune garde sa licence, copiée dans son dossier et dans `bin/licenses` au build.

| Dossier | Projet | D'où vient la copie |
|---|---|---|
| `httplib/` | cpp-httplib (HTTP et HTTPS, un seul en-tête) | `dlib/external/httplib`, sans changement (2026-11-20) |
| `mbedtls/` | Mbed TLS (le chiffrement du HTTPS ; réglages dans `dlib_config/`) | `dlib/external/mbedtls`, sans changement (2026-11-20) |
| `sqlite/` | SQLite (la base `gulliver.db`) | `dlib/external/sqlite`, sans changement (2026-11-20) |
| `libpng/`, `libjpeg/` | décodeurs des images envoyées au moteur | `dlib/external/libpng`, `dlib/external/libjpeg`, sans changement (2026-11-20) |
| `pdfium/` | PDFium (texte des PDF) ; le binaire, non versionné, vient de `tools/fetch_pdfium.sh` | `bblanchon/pdfium-binaries` |

Ces copies sont celles de Gulliver : dlib et Pseudalis gardent les leurs. On n'y touche que pour une correction nécessaire, notée au journal du fork (feuille de route, fil rouge, morceau 2).
