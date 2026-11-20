cpp-httplib, la copie de Gulliver
=================================

Version   : 0.67.0
Origine   : https://github.com/yhirose/cpp-httplib, étiquette v0.67.0
SHA-256   : 3ec770608b295d12bf7bb6cca47f4b8249f6e15261e2587ba0b288f4bfc397ce (httplib.h publié,
            avant la retouche ci-dessous)
Licence   : MIT, voir LICENSE dans ce dossier

Usage
-----

Gulliver inclut <httplib.h> par la cible CMake gulliver_httplib, qui porte tout ce qu'il faut à
l'en-tête : le HTTPS par la copie de Mbed TLS de third_party/mbedtls (CPPHTTPLIB_MBEDTLS_SUPPORT),
les certificats du trousseau de macOS (frameworks CoreFoundation et Security), les fils POSIX.

Retouche de l'en-tête publié
----------------------------

Marquée dans le source par un commentaire qui commence par « gulliver: ».

1. Lignes d'en-tête terminées par un LF seul.  read_headers() saute une telle ligne et poursuit.
   Un intermédiaire qui lit le LF comme une fin de ligne voit alors un en-tête de plus que le
   serveur : c'est le terrain de la contrebande de requêtes.  La ligne rend désormais la requête
   invalide (400), sauf si CPPHTTPLIB_ALLOW_LF_AS_LINE_TERMINATOR est défini.  Retouche reprise de
   la copie de dlib (dlib/external/httplib), toujours nécessaire en 0.67.0.

Les deux autres retouches de la copie de dlib ne sont pas reprises : celle des chemins sous Windows
est sans objet sur macOS, et celle des codages de transfert est faite par la 0.67.0 elle-même
(un Transfer-Encoding autre qu'un seul « chunked » reçoit 501).

Mise à jour
-----------

Remplacer httplib.h par une version plus récente, y reporter la retouche si elle n'y est pas déjà,
mettre à jour la version, l'origine et l'empreinte ci-dessus, puis lancer les tests de Gulliver
(build, test_macos.sh, banc de la page) et les essais au curl du journal.
