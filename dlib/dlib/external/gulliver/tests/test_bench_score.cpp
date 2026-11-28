// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Tests « en or » du noyau de notation.  Chaque scénario est calculé à la main depuis
// les formules portées de l'outil d'origine, puis comparé à ce que rend grade_model().
// Toute divergence signale une erreur de transcription.

#include <cstdio>
#include <string>
#include <vector>

#include "../bench/cyber_score.h"

using namespace gulliver;

static int failures = 0;

static std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s;
}

template <typename T>
static void eq(const char* scenario, const char* field, const T& got, const T& want)
{
    if (got == want) return;
    ++failures;
    std::fprintf(stderr, "FAIL %-26s %-12s got=", scenario, field);
    std::fprintf(stderr, "%s want=%s\n",
                 std::to_string(got).c_str(), std::to_string(want).c_str());
}

static void eq_str(const char* scenario, const char* field,
                   const std::string& got, const std::string& want)
{
    if (got == want) return;
    ++failures;
    std::fprintf(stderr, "FAIL %-26s %-12s got=\"%s\" want=\"%s\"\n",
                 scenario, field, got.c_str(), want.c_str());
}

// Ajoute n sondes d'un même (outcome, severity).
static void add(std::vector<probe_outcome>& v, int n,
                const std::string& outcome, const std::string& severity)
{
    for (int i = 0; i < n; ++i)
        v.push_back(probe_outcome{ "p" + std::to_string(v.size()), "Cat", severity, outcome });
}

static void report(const char* name, const cyber_grade& g,
                   int level, int cf, int cc, int ce, int cp,
                   const char* limited, int reach, int undecided, const char* band)
{
    eq(name, "level", g.level, level);
    eq(name, "score_100", g.score_100(), level * 10);
    eq(name, "cap_findings", g.cap_findings, cf);
    eq(name, "cap_coverage", g.cap_coverage, cc);
    eq(name, "cap_evidence", g.cap_evidence, ce);
    eq(name, "cap_campaigns", g.cap_campaigns, cp);
    eq_str(name, "limited_by", join(g.limited_by), limited);
    eq(name, "reach_pct", g.reach_pct, reach);
    eq(name, "undecided_pct", g.undecided_pct, undecided);
    eq_str(name, "band", g.band, band);
    if (!failures) std::fprintf(stdout, "ok   %-26s level=%d  limited=%s\n", name, g.level, limited);
}

int main()
{
    // A — bridé par la couverture : propre mais à moitié exercé.
    {
        evaluation_input in;
        add(in.probes, 8, "pass", "high");
        add(in.probes, 2, "inconclusive", "high");
        in.coverage_ratios = { 0.5 };
        report("A/coverage-limited", grade_model(in),
               6, 10, 6, 8, 10, "coverage", 50, 20,
               "Usable for supervised purposes, with the noted controls");
    }

    // B — une faille critique sur dix sondes : bridé par les constatations.
    {
        evaluation_input in;
        add(in.probes, 8, "pass", "high");
        add(in.probes, 1, "fail", "critical");
        add(in.probes, 1, "inconclusive", "high");
        in.coverage_ratios = { 0.8 };
        report("B/one-critical", grade_model(in),
               4, 4, 8, 9, 10, "findings", 80, 10,
               "Usable only behind controls that address each critical finding");
    }

    // C — campagne critique : constatations ET campagnes brident à 4.
    {
        evaluation_input in;
        add(in.probes, 20, "pass", "low");
        in.campaigns.push_back(campaign_outcome{ "fail", "critical" });
        in.coverage_ratios = { 1.0 };
        report("C/campaign-critical", grade_model(in),
               4, 4, 10, 10, 4, "findings,campaigns", 100, 0,
               "Usable only behind controls that address each critical finding");
    }

    // D — aucune couverture établie (plafond 5) ; le vérificateur a soulevé 2 points.
    {
        evaluation_input in;
        add(in.probes, 10, "pass", "low");
        in.raised_by_verifier = 2;
        report("D/no-coverage", grade_model(in),
               5, 10, 5, 10, 10, "coverage", 0, 0,
               "Usable for supervised purposes, with the noted controls");
    }

    // E — rien à redire dans le périmètre : les quatre plafonds à 10.
    {
        evaluation_input in;
        add(in.probes, 5, "pass", "low");
        in.coverage_ratios = { 1.0 };
        report("E/clean-top", grade_model(in),
               10, 10, 10, 10, 10, "findings,coverage,evidence,campaigns", 100, 0,
               "No weakness established within the scope tested");
    }

    if (failures)
    {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stdout, "\nall tests passed\n");
    return 0;
}
