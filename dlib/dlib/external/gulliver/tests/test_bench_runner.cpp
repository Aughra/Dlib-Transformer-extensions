// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Test de bout en bout du lanceur, au bouchon : on charge les vrais packs, on branche
// un moteur factice (une fonction qui rend un texte fixe), on fait tourner la campagne,
// puis on calcule la note.  Deux bouchons : l'un qui refuse toujours (aucun échec
// attendu), l'autre qui rend du vide (les sondes non_empty doivent échouer).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../common/json.h"
#include "../bench/coverage_matrix.h"
#include "../bench/cyber_score.h"
#include "../bench/probe_runner.h"

using gulliver::json;

static int failures = 0;

static std::string slurp(const std::string& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
static json load(const std::string& p) { json j; json::parse(slurp(p), j); return j; }

static void want(const std::string& what, bool ok, const std::string& detail = "")
{
    if (ok) { std::fprintf(stdout, "ok   %s\n", what.c_str()); return; }
    ++failures;
    std::fprintf(stderr, "FAIL %s  %s\n", what.c_str(), detail.c_str());
}

static int count_outcome(const std::vector<gulliver::probe_outcome>& v, const std::string& o)
{
    int n = 0; for (const auto& p : v) if (p.outcome == o) ++n; return n;
}

int main(int argc, char** argv)
{
    const std::string base = (argc > 1) ? argv[1] : ".";
    const char* pack_files[] = { "agentic", "jailbreak", "model_supply_chain",
                                 "output_handling", "owasp_llm" };
    std::vector<json> packs;
    size_t total_probes = 0;
    for (const char* p : pack_files)
    {
        packs.push_back(load(base + "/data/packs/" + p + ".json"));
        total_probes += packs.back()["probes"].size();
    }
    const json frameworks = load(base + "/data/frameworks.json");

    // Bouchon « sûr » : le modèle refuse toujours.
    auto refusing = [](const std::string&) -> std::string {
        return "I'm sorry, I cannot help with that request.";
    };
    // Bouchon « muet » : le modèle ne rend rien.
    auto empty = [](const std::string&) -> std::string { return ""; };

    const std::vector<gulliver::probe_outcome> safe = gulliver::run_campaign(packs, refusing);
    const std::vector<gulliver::probe_outcome> mute = gulliver::run_campaign(packs, empty);

    want("campaign covers every probe", safe.size() == total_probes,
         "got " + std::to_string(safe.size()) + " want " + std::to_string(total_probes));
    want("refusing model yields no failure", count_outcome(safe, "fail") == 0,
         std::to_string(count_outcome(safe, "fail")) + " fails");
    want("mute model fails at least one probe", count_outcome(mute, "fail") >= 1,
         std::to_string(count_outcome(mute, "fail")) + " fails");

    // Chaîne complète : couverture -> ratios -> note, sur le modèle qui refuse.
    const json cov = gulliver::build_coverage(packs, {}, frameworks);
    gulliver::evaluation_input in;
    in.probes = safe;
    in.coverage_ratios = gulliver::coverage_ratios(cov);
    const gulliver::cyber_grade g = gulliver::grade_model(in);

    want("end-to-end grade is in range", g.level >= 1 && g.level <= 10,
         "level=" + std::to_string(g.level));

    std::fprintf(stdout,
        "\nbouchon « refus » : %zu sondes -> pass=%d inconclusive=%d recorded=%d fail=%d"
        "\n  note = %d/10 (%d/100), bridee par ", safe.size(),
        count_outcome(safe, "pass"), count_outcome(safe, "inconclusive"),
        count_outcome(safe, "recorded"), count_outcome(safe, "fail"),
        g.level, g.score_100());
    for (size_t i = 0; i < g.limited_by.size(); ++i)
        std::fprintf(stdout, "%s%s", i ? "," : "", g.limited_by[i].c_str());
    std::fprintf(stdout, "\n");

    if (failures) { std::fprintf(stderr, "\n%d check(s) failed\n", failures); return 1; }
    std::fprintf(stdout, "\nall tests passed\n");
    return 0;
}
