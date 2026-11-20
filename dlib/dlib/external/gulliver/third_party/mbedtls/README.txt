Mbed TLS, la copie de Gulliver
==============================

Version   : 3.6.8 (branche 3.6, maintenue à long terme)
Origine   : https://github.com/Mbed-TLS/mbedtls, publication mbedtls-3.6.8 ; archive
            mbedtls-3.6.8.tar.bz2 contrôlée contre mbedtls-3.6.8-sha256sum.txt de la publication
Licence   : Apache-2.0 OU GPL-2.0-or-later, voir LICENSE ; Gulliver l'utilise sous la licence
            Apache 2.0

Contenu
-------

   include/   les en-têtes publics de la publication (include/mbedtls, include/psa), tels quels
   library/   les sources de la bibliothèque (*.c, *.h), telles quelles
   config/    gulliver_mbedtls_config.h, les réglages de Gulliver (fils POSIX, sans MBEDTLS_NET_C)

Les fichiers de build, les tests, les programmes, les scripts et les pilotes tiers de la publication
sont laissés de côté : Gulliver compile la bibliothèque par sa propre cible CMake (gulliver_mbedtls),
que lie gulliver_httplib pour le HTTPS.  La configuration est publique : la cible définit
MBEDTLS_USER_CONFIG_FILE="gulliver_mbedtls_config.h" pour toute unité qui inclut Mbed TLS.

Pourquoi la branche 3.6
-----------------------

La branche 4 change l'interface de la bibliothèque, dont cpp-httplib se sert ; la 3.6 reçoit les
corrections de sécurité jusqu'à la fin de sa maintenance.  La 3.6.8 (16 novembre 2026) en corrige
plusieurs, dont des empreintes SHA fausses au-delà de 4 Gio de données en un appel.

Mise à jour
-----------

Télécharger l'archive d'une version plus récente de la branche, contrôler sa somme SHA-256 publiée,
remplacer include/ et library/ (sans CMakeLists.txt, Makefile, .gitignore), garder config/, mettre à
jour la version ci-dessus, puis lancer les tests de Gulliver.
