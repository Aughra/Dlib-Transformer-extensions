// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le profil et la session des scénarios du test de la page (page_test.sh), à la place de session.py (Gulliver
    7.20.0, décision de l'humain du 28 novembre 2026 : tout en C++).

        build/macos-gulliver/tools/page_session <GULLIVER_HOME du service d'essai> <port> <fichier du mot de passe>

    On se connecte par adresse mail, mot de passe et code reçu par mail : un faux facteur (tools/faux_smtp.h) garde
    les mails du service d'essai, dont l'adresse d'envoi et le port sont réglés ici.  L'administrateur « banc »
    (banc@essai.invalid) choisit son mot de passe par le lien du premier passage (ou, pour un dossier d'essai d'avant
    les adresses, redonne son mot de passe avec son adresse), puis se connecte ; il supprime les profils
    « scenarios-* » d'avant (avec leurs données), en crée un neuf, qui choisit son mot de passe par le lien et s'y
    connecte.  Écrit « <identifiant du profil> <jeton de session> » sur la sortie ; code 1 et un message sur la
    sortie d'erreur au premier échec.
*/

#include <cstdio>
#include <stdexcept>
#include <string>

#include "../client_service.h"
#include "../faux_smtp.h"

using gulliver::json;
using namespace gulliver::essai;

namespace
{
    service_target target;

    json call(const std::string& method, const std::string& path, const json* body = nullptr, const std::string& token = "")
    {
        const service_reply r = service_call(target, method, path, body, token);
        if (r.status < 200 || r.status >= 300)
            throw std::runtime_error(method + " " + path + " -> " + std::to_string(r.status) + " " + r.text);
        return r.body;
    }

    json call(const std::string& method, const std::string& path, const json& body, const std::string& token = "")
    {
        return call(method, path, &body, token);
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: page_session GULLIVER_HOME PORT PASSWORD_FILE\n");
        return 2;
    }
    target.home = argv[1];
    target.port = std::atoi(argv[2]);
    std::string password;
    if (!gulliver::read_file(argv[3], password, 4096))
    {
        std::fprintf(stderr, "page_session: cannot read %s\n", argv[3]);
        return 1;
    }
    while (!password.empty() && std::isspace(static_cast<unsigned char>(password.back()))) password.pop_back();
    const std::string admin_address = "banc@essai.invalid";
    try
    {
        fake_smtp smtp;
        call("PUT", "/gulliver/v1/settings",
            object_of({{"mail_from", "gulliver@essai.invalid"}, {"mail_port", smtp.port}, {"mail_link", ""}}));
        const json listing = call("GET", "/gulliver/v1/profiles");
        std::string admin;
        if (listing["setup"].as_bool() && listing["setup_password"].as_bool())
        {
            // Un dossier d'essai d'avant les adresses : l'administrateur redonne son mot de passe avec son adresse.
            const json c = call("POST", "/gulliver/v1/setup", object_of({{"email", admin_address}, {"password", password}}));
            admin = call("POST", "/gulliver/v1/login/code",
                object_of({{"challenge", c["challenge"]}, {"code", smtp.code_for(admin_address)}}))["token"].str();
        }
        else if (listing["setup"].as_bool())
        {
            call("POST", "/gulliver/v1/setup", object_of({{"name", "banc"}, {"email", admin_address}}));
            admin = call("POST", "/gulliver/v1/password/reset",
                object_of({{"token", smtp.reset_for(admin_address)}, {"password", password}}))["token"].str();
        }
        else
        {
            const int before = smtp.count(admin_address);
            const json c = call("POST", "/gulliver/v1/login", object_of({{"email", admin_address}, {"password", password}}));
            admin = call("POST", "/gulliver/v1/login/code",
                object_of({{"challenge", c["challenge"]}, {"code", smtp.code_for(admin_address, before)}}))["token"].str();
        }
        for (const json& p : call("GET", "/gulliver/v1/profiles", nullptr, admin)["profiles"].elements())
            if (p["name"].str().rfind("scenarios-", 0) == 0)
                call("DELETE", "/gulliver/v1/profiles/" + p["id"].str(), nullptr, admin);
        const std::string run = gulliver::random_hex(4);
        const std::string address = "scenarios-" + run + "@essai.invalid";
        const json p = call("POST", "/gulliver/v1/profiles", object_of({{"name", "scenarios-" + run}, {"email", address}}), admin);
        call("POST", "/gulliver/v1/password/forgot", object_of({{"email", address}}));
        const json s = call("POST", "/gulliver/v1/password/reset",
            object_of({{"token", smtp.reset_for(address)}, {"password", "Scenario-1-" + password + run}}));
        std::printf("%s %s\n", p["id"].str().c_str(), s["token"].str().c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "page_session: %s\n", e.what());
        return 1;
    }
}
