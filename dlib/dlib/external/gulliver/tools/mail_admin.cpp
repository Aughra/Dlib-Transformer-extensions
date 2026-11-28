// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Un mail à l'administrateur de Gulliver, pour les agents du Mac (à la place de tools/mail_admin.py, Gulliver
    7.20.0, décision de l'humain du 28 novembre 2026 : tout en C++).

        build/macos-gulliver/bin/mail_admin GULLIVER_HOME "Objet" < texte

    L'envoi est celui de tools/mail_admin.h (l'adresse de l'administrateur dans la base, le facteur du service).  Code 1
    et la raison sur la sortie d'erreur quand le mail ne part pas.
*/

#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>

#include "mail_admin.h"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: mail_admin GULLIVER_HOME SUBJECT < BODY\n");
        return 2;
    }
    const std::string body((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::string error;
    if (gulliver::essai::send_admin(argv[1], argv[2], body, error)) return 0;
    std::fprintf(stderr, "mail not sent: %s\n", error.c_str());
    return 1;
}
