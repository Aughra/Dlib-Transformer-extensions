// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Test « en or » du Markdown de l'assembleur conseil : sur le .raw réel, le C++ doit
// produire EXACTEMENT la même sortie stdout que conseil_modeles.py (référence
// tests/fixtures/conseil_stdout_ref.txt, capturée au 2026-11-28 08:45).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../bench/conseil_core.h"

using gulliver::json;

static std::string slurp(const std::string& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
static std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == '\n') { v.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

// Compare deux textes ligne à ligne ; rend le nombre d'écarts (0 = identique).
static int compare(const std::string& got, const std::string& ref, const char* label)
{
    const std::vector<std::string> g = lines(got), r = lines(ref);
    const size_t n = std::max(g.size(), r.size());
    int bad = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const std::string a = i < g.size() ? g[i] : "<absente>";
        const std::string b = i < r.size() ? r[i] : "<absente>";
        if (a != b)
        {
            ++bad;
            if (bad <= 5) std::fprintf(stderr, "FAIL %s ligne %zu\n  obtenu : %s\n  réf    : %s\n",
                                       label, i + 1, a.c_str(), b.c_str());
        }
    }
    if (bad) std::fprintf(stderr, "%s : %d ligne(s) différentes (%zu obtenues, %zu réf)\n", label, bad, g.size(), r.size());
    else     std::fprintf(stdout, "ok   %s : Markdown identique à Python (%zu lignes)\n", label, g.size());
    return bad;
}

// La date écrite dans la 1re ligne de la référence (« … — AAAA-MM-JJ hh:mm »), pour
// l'injecter et comparer le reste à l'identique.
static std::string date_of(const std::string& ref)
{
    const std::string first = lines(ref).empty() ? "" : lines(ref)[0];
    const size_t p = first.rfind(" — ");
    return p == std::string::npos ? "" : first.substr(p + std::string(" — ").size());
}
static json load(const std::string& p) { json j; json::parse(slurp(p), j); return j; }

int main(int argc, char** argv)
{
    const std::string raw_path = (argc > 1) ? argv[1]
        : "bench/tests/fixtures/modeles-20261124-1350.raw";
    const std::string dir = (argc > 2) ? argv[2] : "bench/tests/fixtures";
    const std::string raw = slurp(raw_path);
    int bad = 0;

    // Cas 1 : sans option (défauts).
    {
        const std::string ref = slurp(dir + "/conseil_stdout_ref.txt");
        gulliver::conseil_options opt;
        bad += compare(gulliver::build_conseil_markdown(raw, opt, date_of(ref)), ref, "défaut");
    }
    // Cas 2 : avec --machine et --temperature.
    {
        const std::string ref = slurp(dir + "/conseil_stdout_machine_ref.txt");
        gulliver::conseil_options opt;
        opt.machine = "Mac|M5 Pro, 64 Go|27.2|7.15.0|1 au début, 2 à la fin|30 au début";
        opt.temperature = "50.0|60.0|70.0|5|19|40|0|63|99|3";
        bad += compare(gulliver::build_conseil_markdown(raw, opt, date_of(ref)), ref, "machine");
    }
    // Cas 3 : avec --service et --rag.
    {
        const std::string ref = slurp(dir + "/conseil_stdout_servicerag_ref.txt");
        gulliver::conseil_options opt;
        opt.service = load(dir + "/service.json");
        opt.rag = load(dir + "/rag.json");
        bad += compare(gulliver::build_conseil_markdown(raw, opt, date_of(ref)), ref, "service+rag");
    }
    // Cas 4 : update_doc (réécriture du document entre les marqueurs).
    {
        const std::string ref = slurp(dir + "/conseil_doc_ref.md");
        const std::string in = slurp(dir + "/conseil_doc_in.md");
        // La date « JJ/MM/AAAA à HH h MM » entre parenthèses du titre de la semaine.
        std::string when;
        const std::string tag = "## La mesure de la semaine (";
        const size_t p = ref.find(tag);
        if (p != std::string::npos) { const size_t q = ref.find(')', p); when = ref.substr(p + tag.size(), q - p - tag.size()); }
        gulliver::conseil_options opt;
        opt.machine = "Mac|M5 Pro, 64 Go|27.2|7.15.0|1 au début, 2 à la fin|30 au début";
        opt.temperature = "50.0|60.0|70.0|5|19|40|0|63|99|3";
        opt.service = load(dir + "/service.json");
        opt.rag = load(dir + "/rag.json");
        bad += compare(gulliver::build_conseil_doc(raw, opt, when, in), ref, "update_doc");
    }

    if (bad) { std::fprintf(stderr, "\n%d écart(s)\n", bad); return 1; }
    std::fprintf(stdout, "\nall tests passed\n");
    return 0;
}
