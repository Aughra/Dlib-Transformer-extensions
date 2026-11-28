// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le programme du retrait du dernier du classement (Gulliver 7.20.0, à la place de tools/retrait_modeles.py).

        build/macos-gulliver/bin/retrait_modeles GULLIVER_HOME [--essai]

    Lit build/bench/hebdo/conseil.json et retires.json (ou ceux du dossier RETRAIT_HEBDO, pour un essai), décide avec
    bench/retrait_core.h, retire le modèle de Gulliver par le service qui tourne sur ce Mac (DELETE
    /gulliver/v1/models/<id> : le fichier est effacé ; certificat vérifié avec l'autorité du service), l'ajoute à
    retires.json avec sa date, sa note et de quoi le remettre, et recopie la liste dans « removed » de conseil.json :
    le menu « Modèles retirés » de l'onglet Modèles la montre, avec un bouton pour le remettre.  La veille du lundi
    ne retélécharge jamais un modèle de cette liste.  Écrit une ligne « retiré : … » pour le mail de la mesure ; rien
    sans « remove », et un seul retrait par semaine.  Avec --essai, il dit seulement ce qu'il ferait.
*/

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "retrait_core.h"
#include "../common/platform.h"
#include "../tools/client_service.h"

using gulliver::json;

namespace
{
    json load(const std::string& path, json fallback)
    {
        std::string text;
        if (!gulliver::read_file(path, text, 64u << 20)) return fallback;
        json j;
        return json::parse(text, j) ? j : fallback;
    }

    bool write(const std::string& path, const json& value)
    {
        return gulliver::write_file_atomic(path, value.dump_formatted(1));
    }
}

int main(int argc, char** argv)
{
    std::string home;
    bool trial = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--essai") trial = true;
        else if (a.rfind("--", 0) != 0 && home.empty()) home = a;
    }
    if (home.empty())
        if (const char* env = std::getenv("GULLIVER_HOME")) home = env;
    std::string hebdo;
    if (const char* env = std::getenv("RETRAIT_HEBDO"); env && *env) hebdo = env;
    else
    {
        // build/macos-gulliver/bin → build/bench/hebdo
        const std::string build = gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(gulliver::executable_path())));
        hebdo = gulliver::join_path(build, "bench/hebdo");
    }
    const std::string advice_path = gulliver::join_path(hebdo, "conseil.json");
    const std::string removed_path = gulliver::join_path(hebdo, "retires.json");
    json advice = load(advice_path, json::object());
    json removed = load(removed_path, json::array());
    if (!removed.is_array()) removed = json::array();

    gulliver::essai::service_target service;
    service.home = home;
    service.port = gulliver::essai::service_port(home);
    json models = json::object();
    // Le service n'est appelé que s'il y a un modèle à retirer.
    if (!advice["remove"]["id"].str().empty())
        models = gulliver::essai::service_call(service, "GET", "/gulliver/v1/models").body;

    const std::time_t now = std::time(nullptr);
    const gulliver::retrait_plan plan = gulliver::plan_removal(advice, removed, models, now);
    using action = gulliver::retrait_plan::action;
    if (plan.what == action::this_week)
    {
        advice["removed"] = removed;
        write(advice_path, advice);
    }
    if (plan.what != action::remove)
    {
        std::printf("%s\n", plan.message.c_str());
        return 0;
    }
    if (trial)
    {
        std::printf("essai : retirerait %s (%s)\n", plan.entry["name"].str().c_str(), plan.note.c_str());
        return 0;
    }
    const gulliver::essai::service_reply r = gulliver::essai::service_call(service, "DELETE", "/gulliver/v1/models/" + plan.id);
    if (!r.body["ok"].as_bool())
    {
        // Le code de l'erreur, sinon son texte (Python : r.get("code") or r.get("error")), sinon le statut HTTP.
        std::string why = r.body["code"].str();
        if (why.empty()) why = r.body["error"].str();
        if (why.empty()) why = std::to_string(r.status);
        std::printf("%s : pas retiré (%s)\n", plan.id.c_str(), why.c_str());
        return 1;
    }
    json kept = json::array();
    for (const json& x : removed.elements())
        if (x["id"].str() != plan.id) kept.elements().push_back(x);
    kept.elements().push_back(plan.entry);
    write(removed_path, kept);
    advice["removed"] = kept;
    write(advice_path, advice);
    std::printf("retiré : %s (%s) ; la veille ne le retéléchargera plus, le menu « Modèles retirés » de l'onglet Modèles "
                "le remet d'un clic\n", plan.entry["name"].str().c_str(), plan.note.c_str());
    return 0;
}
