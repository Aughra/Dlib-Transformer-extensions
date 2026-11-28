// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// La ligne de commande de l'assembleur conseil, portée de tools/conseil_modeles.py : mêmes
// options (voir tools/bench_modeles.sh), même ordre de sortie — le Markdown de la semaine
// sur stdout, puis, s'ils sont demandés, la réécriture du document (--doc) et le conseil.json
// (--json).  La matière est produite par host/conseil_core ; ici, seuls l'analyse des
// arguments, la lecture des fichiers et l'horodatage.

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../common/json.h"
#include "conseil_core.h"

using gulliver::json;

namespace
{
    std::string slurp(const std::string& p)
    {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream ss; ss << in.rdbuf(); return ss.str();
    }

    // Les mots séparés par des espaces d'une option (--new, --keep), comme str.split() de Python.
    std::set<std::string> words(const std::string& s)
    {
        std::set<std::string> out; std::istringstream in(s); std::string w;
        while (in >> w) out.insert(w);
        return out;
    }

    // Un fichier JSON, ou null si absent ou illisible (comme load_json de l'outil d'origine).
    json load_json(const std::string& path)
    {
        if (path.empty()) return json(nullptr);
        json j; std::string err;
        if (!json::parse(slurp(path), j, &err)) return json(nullptr);
        return j;
    }

    std::string now_stamp(const char* fmt)
    {
        std::time_t t = std::time(nullptr);
        std::tm lt{}; localtime_r(&t, &lt);
        char buf[64]; std::strftime(buf, sizeof buf, fmt, &lt);
        return buf;
    }
}

int main(int argc, char** argv)
{
    std::string raw_path, json_path, doc_path, service_path, rag_path, removed_path, cyber_path;
    gulliver::conseil_options opt;

    auto next = [&](int& i) -> std::string {
        if (i + 1 >= argc) { std::fprintf(stderr, "valeur manquante après %s\n", argv[i]); std::exit(2); }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--previous")         opt.previous = next(i);
        else if (a == "--new")         opt.fresh = words(next(i));
        else if (a == "--keep")        opt.keep = words(next(i));
        else if (a == "--rounds")      opt.rounds = next(i);
        else if (a == "--machine")     opt.machine = next(i);
        else if (a == "--temperature") opt.temperature = next(i);
        else if (a == "--late")        opt.late = true;
        else if (a == "--json")        json_path = next(i);
        else if (a == "--doc")         doc_path = next(i);
        else if (a == "--service")     service_path = next(i);
        else if (a == "--rag")         rag_path = next(i);
        else if (a == "--removed")     removed_path = next(i);
        else if (a == "--cyber")       cyber_path = next(i);
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "option inconnue : %s\n", a.c_str()); return 2; }
        else if (raw_path.empty())     raw_path = a;
        else { std::fprintf(stderr, "argument en trop : %s\n", a.c_str()); return 2; }
    }
    if (raw_path.empty()) { std::fprintf(stderr, "usage : conseil <raw> [options]\n"); return 2; }

    opt.service = load_json(service_path);
    opt.rag     = load_json(rag_path);
    opt.cyber   = load_json(cyber_path);
    { json r = load_json(removed_path); opt.removed = r.is_array() ? r : json::array(); }

    const std::string raw = slurp(raw_path);

    // 1) Le Markdown de la semaine, sur stdout (comme les print de l'outil d'origine).
    std::fputs(gulliver::build_conseil_markdown(raw, opt, now_stamp("%Y-%m-%d %H:%M")).c_str(), stdout);

    // 2) La section de la semaine du document (--doc), réécrite entre ses marqueurs.
    if (!doc_path.empty())
    {
        const std::string doc = slurp(doc_path);
        if (!doc.empty())
        {
            const std::string out = gulliver::build_conseil_doc(raw, opt, now_stamp("%d/%m/%Y à %H h %M"), doc);
            std::ofstream(doc_path, std::ios::binary) << out;
        }
    }

    // 3) Le conseil.json (--json), lu par l'onglet Modèles (GET /gulliver/v1/models/advice).
    if (!json_path.empty())
    {
        const json out = gulliver::build_conseil(raw, opt, now_stamp("%Y-%m-%d %H:%M"));
        std::ofstream(json_path, std::ios::binary) << out.dump_formatted(1) << "\n";
    }
    return 0;
}
