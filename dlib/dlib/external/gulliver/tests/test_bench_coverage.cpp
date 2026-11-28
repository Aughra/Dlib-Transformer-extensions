// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Test « en or » du constructeur de couverture : on charge les vrais packs et le vrai
// frameworks.json, on construit la matrice, et on la compare à la sortie de référence
// produite par le build_coverage Python de l'outil d'origine (tests/fixtures/coverage_ref.json).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../common/json.h"
#include "../bench/coverage_matrix.h"

using gulliver::json;

static int failures = 0;

static std::string slurp(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static json load(const std::string& path)
{
    json j;
    std::string err;
    if (!json::parse(slurp(path), j, &err))
        std::fprintf(stderr, "parse %s: %s\n", path.c_str(), err.c_str());
    return j;
}

static void want(const std::string& what, bool ok, const std::string& detail = "")
{
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FAIL %s  %s\n", what.c_str(), detail.c_str());
}

// La référence porte des chaînes d'id ; la matrice porte des objets {id,label,...}.
static std::vector<std::string> ids(const json& arr)       // référence : chaînes
{
    std::vector<std::string> v;
    for (size_t i = 0; i < arr.size(); ++i) v.push_back(arr[i].as_string());
    return v;
}
static std::vector<std::string> entry_ids(const json& arr)  // matrice : objets
{
    std::vector<std::string> v;
    for (size_t i = 0; i < arr.size(); ++i) v.push_back(arr[i]["id"].as_string());
    return v;
}

int main(int argc, char** argv)
{
    const std::string base = (argc > 1) ? argv[1] : ".";   // le dossier bench/ de Gulliver

    const char* pack_files[] = { "agentic", "jailbreak", "model_supply_chain",
                                 "output_handling", "owasp_llm" };
    std::vector<json> packs;
    for (const char* p : pack_files)
        packs.push_back(load(base + "/data/packs/" + p + ".json"));
    const json frameworks = load(base + "/data/frameworks.json");

    const json cov = gulliver::build_coverage(packs, {}, frameworks);
    const json ref = load(base + "/tests/fixtures/coverage_ref.json");

    for (const char* key : gulliver::framework_keys)
    {
        const json& s = cov["summary"][key];
        const json& rs = ref["summary"][key];
        for (const char* f : { "total", "in_scope", "covered", "gaps", "out_of_scope" })
            want(std::string(key) + "." + f, s[f].as_int() == rs[f].as_int(),
                 "got " + std::to_string(s[f].as_int()) + " want " + std::to_string(rs[f].as_int()));

        const double r = s["ratio"].as_double(), rr = rs["ratio"].as_double();
        want(std::string(key) + ".ratio", (r - rr < 1e-9 && rr - r < 1e-9),
             "got " + std::to_string(r) + " want " + std::to_string(rr));

        const json& rids = ref["ids"][key];
        want(std::string(key) + ".covered ids", entry_ids(cov["frameworks"][key]["covered"]) == ids(rids["covered"]));
        want(std::string(key) + ".gaps ids", entry_ids(cov["frameworks"][key]["gaps"]) == ids(rids["gaps"]));
        want(std::string(key) + ".out_of_scope ids", entry_ids(cov["frameworks"][key]["out_of_scope"]) == ids(rids["out_of_scope"]));
        if (!failures) std::fprintf(stdout, "ok   %-12s covered=%lld gaps=%lld oos=%lld ratio=%.3f\n",
                                    key, s["covered"].as_int(), s["gaps"].as_int(),
                                    s["out_of_scope"].as_int(), s["ratio"].as_double());
    }

    // La portée moyennée, telle que la notation la consomme.
    const std::vector<double> ratios = gulliver::coverage_ratios(cov);
    double sum = 0; for (double x : ratios) sum += x;
    const double reach = ratios.empty() ? 0 : sum / ratios.size();
    want("reach ~ 0.939", reach > 0.938 && reach < 0.940, "reach=" + std::to_string(reach));
    std::fprintf(stdout, "reach (avg of %zu ratios) = %.3f\n", ratios.size(), reach);

    if (failures) { std::fprintf(stderr, "\n%d check(s) failed\n", failures); return 1; }
    std::fprintf(stdout, "\nall tests passed\n");
    return 0;
}
