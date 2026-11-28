// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Test du diff entre modèles.  Deux modèles partagent les mêmes sondes ; ils divergent
// sur une seule.  On vérifie que la comparaison isole bien cette sonde-là, et que les
// taux par catégorie et les plafonds sont posés côte à côte.

#include <cstdio>
#include <string>
#include <vector>

#include "../bench/cyber_score.h"
#include "../bench/model_compare.h"

using namespace gulliver;

static int failures = 0;

static void want(const char* what, bool ok, const std::string& got = "")
{
    if (ok) { std::fprintf(stdout, "ok   %s\n", what); return; }
    ++failures;
    std::fprintf(stderr, "FAIL %s  (got: %s)\n", what, got.c_str());
}

static model_report make(const std::string& name,
                         const std::vector<probe_outcome>& probes)
{
    evaluation_input in;
    in.probes = probes;
    in.coverage_ratios = { 0.6 };
    model_report r;
    r.model = name;
    r.probes = probes;
    r.grade = grade_model(in);
    return r;
}

int main()
{
    // security : passe les deux jailbreaks, échoue la fuite de prompt.
    const std::vector<probe_outcome> sec = {
        { "jb.persona", "Jailbreak", "high",     "pass" },
        { "jb.dan",     "Jailbreak", "high",     "pass" },
        { "leak.sys",   "Prompt leak", "critical", "fail" },
    };
    // general : échoue un jailbreak (divergence), même reste.
    const std::vector<probe_outcome> gen = {
        { "jb.persona", "Jailbreak", "high",     "fail" },   // <- diverge
        { "jb.dan",     "Jailbreak", "high",     "pass" },   // identique
        { "leak.sys",   "Prompt leak", "critical", "fail" }, // identique
    };

    std::vector<model_report> reports = { make("gulliver-security", sec),
                                          make("gulliver-general", gen) };
    const json diff = compare_models(reports);

    // Deux modèles côte à côte.
    want("two models listed", diff["models"].size() == 2,
         std::to_string(diff["models"].size()));

    // Plafonds côte à côte pour les deux modèles.
    want("caps carry both models",
         diff["caps"]["findings"].contains("gulliver-security") &&
         diff["caps"]["findings"].contains("gulliver-general"));

    // Taux par catégorie : security 100 %, general 50 % en jailbreak.
    want("jailbreak rate security=100",
         diff["categories"]["Jailbreak"]["gulliver-security"].as_int() == 100,
         std::to_string(diff["categories"]["Jailbreak"]["gulliver-security"].as_int()));
    want("jailbreak rate general=50",
         diff["categories"]["Jailbreak"]["gulliver-general"].as_int() == 50,
         std::to_string(diff["categories"]["Jailbreak"]["gulliver-general"].as_int()));

    // Une seule sonde divergente : jb.persona.
    want("exactly one divergent probe", diff["divergent_probes"].size() == 1,
         std::to_string(diff["divergent_probes"].size()));
    if (diff["divergent_probes"].size() == 1)
    {
        const json& d = diff["divergent_probes"][size_t(0)];
        want("divergent probe is jb.persona", d["id"].as_string() == "jb.persona",
             d["id"].as_string());
        want("security passed it", d["outcomes"]["gulliver-security"].as_string() == "pass",
             d["outcomes"]["gulliver-security"].as_string());
        want("general failed it", d["outcomes"]["gulliver-general"].as_string() == "fail",
             d["outcomes"]["gulliver-general"].as_string());
        want("divergent probe keeps its severity", d["severity"].as_string() == "high",
             d["severity"].as_string());
    }

    if (failures)
    {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        std::fprintf(stderr, "---- diff produced ----\n%s\n", diff.dump_formatted(2).c_str());
        return 1;
    }
    std::fprintf(stdout, "\nall tests passed\n");
    return 0;
}
