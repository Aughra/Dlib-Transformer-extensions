// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les modèles installés dans Gulliver, pour la mesure du lundi (à la place de tools/modeles_installes.py,
    Gulliver 7.20.0, décision de l'humain du 28 novembre 2026 : tout en C++).

        build/macos-gulliver/bin/modeles_installes [GULLIVER_HOME]

    Demande au service qui tourne sur ce Mac (https://127.0.0.1:<port de ses réglages>, certificat vérifié avec
    l'autorité du service) la liste de ses modèles, et écrit une ligne par modèle installé qui répond aux questions
    (les modèles de la recherche dans les documents, rôle « embedding », sont écartés) :

        nom affiché|fichier GGUF|identifiant|actif (1 ou 0)|lit les images (1 ou 0)

    Rien si le service ne répond pas : les mesures se font alors sur leur liste fixe seulement.
*/

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "client_service.h"

using gulliver::json;
using namespace gulliver::essai;

namespace
{
    bool has(const json& list, const std::string& value)
    {
        for (const json& v : list.elements())
            if (v.is_string() && v.as_string() == value) return true;
        return false;
    }
}

int main(int argc, char** argv)
{
    const char* env = std::getenv("GULLIVER_HOME");
    service_target t;
    t.home = argc > 1 ? argv[1] : (env ? env : "");
    t.port = service_port(t.home);
    t.timeout_seconds = 10;
    const service_reply r = service_call(t, "GET", "/gulliver/v1/models");
    if (r.status != 200) return 0;
    std::set<std::string> active;
    for (const auto& kv : r.body["active"].items())
        if (kv.second.is_string()) active.insert(kv.second.as_string());
    for (const json& m : r.body["models"].elements())
    {
        const std::string file = m["file"].str(), id = m["id"].str();
        if (!m["installed"].as_bool() || file.empty() || has(m["roles"], "embedding") || !gulliver::file_exists(file)) continue;
        std::string name = m["name"].str().empty() ? id : m["name"].str();
        const std::string quant = m["quantization"].str();
        if (!quant.empty() && name.find(quant) == std::string::npos) name += " " + quant;
        if (m["arch"].str().find("moe") != std::string::npos) name += " (MoE)";
        for (char& c : name)
            if (c == '|') c = '/';
        std::printf("%s|%s|%s|%d|%d\n", name.c_str(), file.c_str(), id.c_str(), active.count(id) ? 1 : 0,
            has(m["inputs"], "image") ? 1 : 0);
    }
    return 0;
}
