# Bibliothèques d'autres projets

Le code de projets extérieurs que Gulliver compile avec lui. Chacune garde sa licence, copiée dans son dossier et dans `bin/licenses` au build.

| Dossier | Projet | D'où vient la copie |
|---|---|---|
| `httplib/` | cpp-httplib (HTTP et HTTPS, un seul en-tête) | publication 0.67.0, avec une retouche reprise de dlib (`httplib/README.txt`), 2026-11-20 |
| `mbedtls/` | Mbed TLS (le chiffrement du HTTPS ; réglages dans `config/`) | publication 3.6.8, somme SHA-256 contrôlée (`mbedtls/README.txt`), 2026-11-20 |
| `pdfium/` | PDFium (texte des PDF) ; le binaire, non versionné, vient de `tools/fetch_pdfium.sh` | `bblanchon/pdfium-binaries` |

Ces copies sont celles de Gulliver : dlib et Pseudalis gardent les leurs. On n'y touche que pour une correction nécessaire ou une mise à jour, notée au journal du fork (feuille de route, fil rouge, morceau 2). Dernière vérification des versions publiées : 2026-11-20 (cpp-httplib 0.63.0 → 0.67.0, Mbed TLS 3.6.7 → 3.6.8 ; SQLite, libpng et libjpeg déjà à jour). libpng et libjpeg sont retirées le même jour : les images sont décodées par ImageIO, de macOS (Gulliver 6.11.0). SQLite aussi : Gulliver utilise celui de macOS (Gulliver 6.12.0).
