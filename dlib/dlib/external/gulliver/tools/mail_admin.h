// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_TOOLS_MAIL_ADMIN_H_
#define GULLIVER_TOOLS_MAIL_ADMIN_H_

/*
    Un mail à l'administrateur de Gulliver, pour les agents du Mac (Gulliver 7.20.0 ; sorti de mail_admin.cpp en
    7.22.0, pour servir aussi à la veille du lundi et au surveillant quand ils seront en C++).

    L'adresse est celle du profil administrateur, lue dans la base du service (gulliver.db, en lecture seule) ; le
    mail part par le facteur du service lui-même (host/mailer.cpp : Proton Mail Bridge sur 127.0.0.1), avec les
    réglages « Mail des comptes » (mail_from et mail_port de settings.json, mot de passe de mail.secret, jamais
    affiché, lu seulement s'il a été donné pour ces mêmes adresse et port, comme le fait le service).
*/

#include <string>

#include <sqlite3.h>

#include "../common/json.h"
#include "../common/platform.h"
#include "../host/mailer.h"

namespace gulliver::essai
{
    // L'adresse du premier administrateur qui en a une, ou "".
    inline std::string admin_address(const std::string& home)
    {
        const std::string uri = "file:" + join_path(home, "gulliver.db") + "?mode=ro";
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

    /*!
        ensures
            - Envoie subject et body à l'administrateur et renvoie true quand le serveur l'a accepté.
            - Sinon renvoie false et décrit l'échec dans error (en anglais), sans jamais y mettre le mot de passe.
    !*/
    inline bool send_admin(const std::string& home, const std::string& subject, const std::string& body, std::string& error)
    {
        const std::string to = admin_address(home);
        if (to.empty())
        {
            error = "the administrator has no mail address";
            return false;
        }
        std::string text;
        if (!read_file(join_path(home, "settings.json"), text, 1 << 20))
        {
            error = "no settings.json";
            return false;
        }
        const json settings = json::parse_or_null(text);
        mail_server server;
        server.from = settings["mail_from"].str();
        server.port = static_cast<int>(settings["mail_port"].as_int(1025));
        if (server.from.empty())
        {
            error = "no mail_from in the settings";
            return false;
        }
        if (!read_file(join_path(home, "mail.secret"), text, 64 * 1024))
        {
            error = "no mail.secret";
            return false;
        }
        const json record = json::parse_or_null(text);
        if (record["scope"].str() != server.from + "|" + std::to_string(server.port))
        {
            error = "mail.secret was given for another sender or port";
            return false;
        }
        server.secret = record["secret"].str();
        return send_mail(server, to, subject, body, error);
    }
}

#endif // GULLIVER_TOOLS_MAIL_ADMIN_H_
