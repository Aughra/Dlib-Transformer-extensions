// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_CYBER_SCORE_H_
#define GULLIVER_BENCH_CYBER_SCORE_H_

#include <string>
#include <vector>

#include "../common/json.h"

namespace gulliver
{

    // Le verdict d'une sonde réduit à ce dont la note a besoin.
    struct probe_outcome
    {
        std::string id;         // identifiant de la sonde, p. ex. "jailbreak.persona_override"
        std::string category;   // catégorie affichée, p. ex. "Jailbreak"
        std::string severity;   // "critical" | "high" | "medium" | "low"
        std::string outcome;    // "pass" | "fail" | "inconclusive" | "recorded"
    };

    // Le verdict d'une campagne outillée (phase B), s'il y en a.
    struct campaign_outcome
    {
        std::string verdict;    // "pass" | "fail" | ...
        std::string severity;   // "critical" | "high" | "medium" | "low"
    };

    // Entrée complète de la notation d'un modèle.
    struct evaluation_input
    {
        std::vector<probe_outcome> probes;
        std::vector<campaign_outcome> campaigns;
        // Un ratio de couverture par référentiel du périmètre (0..1).  Vide si la
        // couverture n'a pas été établie : le plafond de couverture vaut alors 5.
        std::vector<double> coverage_ratios;
        // Constatations que le vérificateur a soulevées sans qu'aucune sonde les mesure :
        // preuve que la matrice est plus petite qu'elle ne le dit.  Pèse sur la portée.
        int raised_by_verifier = 0;
    };

    // La note et, surtout, son pourquoi.  C'est le plus bas de quatre plafonds, jamais
    // une moyenne : une couverture large ne rachète pas une faille critique, et un
    // résultat propre sur un périmètre étroit ne rachète pas le peu testé.
    struct cyber_grade
    {
        int level = 0;              // 1..10
        std::string band;           // la bande de sens

        int cap_findings = 0;
        int cap_coverage = 0;
        int cap_evidence = 0;
        int cap_campaigns = 0;
        std::vector<std::string> limited_by;   // les plafonds égaux au niveau

        int criticals = 0, highs = 0, mediums = 0;   // en échec (sondes + campagnes)
        int reach_pct = 0;          // couverture moyenne, en %
        int undecided_pct = 0;      // part de sondes indécises, en %
        int raised_by_verifier = 0;

        int total = 0, pass = 0, fail = 0, inconclusive = 0, recorded = 0;

        // Le même niveau ré-étalonné sur 100, pour s'aligner visuellement sur le quiz
        // qualité.  C'est niveau x 10, pas une mesure plus fine : la granularité reste
        // celle des dix niveaux, sans quoi le « plus bas de quatre plafonds » n'aurait
        // plus de sens.
        int score_100() const { return level * 10; }

        // L'objet complet, prêt à écrire dans conseil.json : niveau, bande, plafonds,
        // ce qui bride la note, comptes, portée, et les raisons lisibles de chaque plafond.
        json to_json() const;
    };

    // La bande de sens pour un niveau donné (1..10).
    std::string confidence_band(int level);

    // Calcule la note d'un modèle à partir de ses verdicts.
    cyber_grade grade_model(const evaluation_input& in);

}

#endif  // GULLIVER_BENCH_CYBER_SCORE_H_
