// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Un mail à l'administrateur de Gulliver, pour les agents du Mac (à la place de tools/mail_admin.py, Gulliver
    7.20.0, décision de l'humain du 28 novembre 2026 : tout en C++).

        build/macos-gulliver/bin/mail_admin GULLIVER_HOME "Objet" < texte

    L'adresse est celle du profil administrateur, lue dans la base du service (gulliver.db, en lecture seule) ; le
    mail part par le facteur du service lui-même (host/mailer.cpp : Proton Mail Bridge sur 127.0.0.1), avec les
    réglages « Mail des comptes » (mail_from et mail_port de settings.json, mot de passe de mail.secret, jamais
    affiché, lu seulement s'il a été donné pour ces mêmes adresse et port, comme le fait le service).  Code 1 et la
    raison sur la sortie d'erreur quand le mail ne part pas.
*/

#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>

#include <sqlite3.h>

#include "../common/json.h"
#include "../common/platform.h"
#include "../host/mailer.h"

using gulliver::json;

namespace
{
    std::string admin_address(const std::string& home)
    {
        const std::string uri = "file:" + gulliver::join_path(home, "gulliver.db") + "?mode=ro";
        sqlite3* db = nullptr;
        std::string out;
        if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) == SQLITE_OK)
        {
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT email FROM profiles WHERE admin = 1 AND email <> '' ORDER BY created, rowid LIMIT 1",
                    -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
                if (const unsigned char* t = sqlite3_column_text(st, 0)) out = reinterpret_cast<const char*>(t);
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
        return out;
    }

    bool fail(const std::string& why)
    {
        std::fprintf(stderr, "mail not sent: %s\n", why.c_str());
        return false;
    }

    bool send_admin(const std::string& home, const std::string& subject, const std::string& body)
    {
        const std::string to = admin_address(home);
        if (to.empty()) return fail("the administrator has no mail address");
        std::string text;
        if (!gulliver::read_file(gulliver::join_path(home, "settings.json"), text, 1 << 20)) return fail("no settings.json");
        const json settings = json::parse_or_null(text);
        gulliver::mail_server server;
        server.from = settings["mail_from"].str();
        server.port = static_cast<int>(settings["mail_port"].as_int(1025));
        if (server.from.empty()) return fail("no mail_from in the settings");
        if (!gulliver::read_file(gulliver::join_path(home, "mail.secret"), text, 64 * 1024)) return fail("no mail.secret");
        const json record = json::parse_or_null(text);
        if (record["scope"].str() != server.from + "|" + std::to_string(server.port))
            return fail("mail.secret was given for another sender or port");
        server.secret = record["secret"].str();
        std::string error;
        return gulliver::send_mail(server, to, subject, body, error) || fail(error);
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: mail_admin GULLIVER_HOME SUBJECT < BODY\n");
        return 2;
    }
    const std::string body((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    return send_admin(argv[1], argv[2], body) ? 0 : 1;
}
