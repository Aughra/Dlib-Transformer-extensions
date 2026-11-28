// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_MODEL_COMPARE_H_
#define GULLIVER_BENCH_MODEL_COMPARE_H_

#include <string>
#include <vector>

#include "../common/json.h"
#include "cyber_score.h"

namespace gulliver
{

    // Ce qu'on garde d'une évaluation, par modèle : son nom logique, sa note (avec le
    // pourquoi) et le verdict de chaque sonde.  C'est la matière des deux usages :
    // l'overview d'un modèle et la comparaison entre modèles.
    struct model_report
    {
        std::string model;                    // p. ex. "gulliver-security"
        cyber_grade grade;
        std::vector<probe_outcome> probes;    // verdicts par sonde
    };

    // Un modèle, prêt pour /gulliver/v1/models : sa note complète et son taux de passage
    // par catégorie.
    json model_to_json(const model_report& report);

    // La comparaison entre modèles, pour choisir : notes côte à côte, plafonds côte à
    // côte, taux par catégorie, et surtout les sondes où les modèles divergent (celui-ci
    // passe, celui-là échoue).  Ce n'est pas « 6 contre 7 », c'est « B échoue trois
    // sondes de jailbreak que A passe ».
    json compare_models(const std::vector<model_report>& reports);

}

#endif  // GULLIVER_BENCH_MODEL_COMPARE_H_
