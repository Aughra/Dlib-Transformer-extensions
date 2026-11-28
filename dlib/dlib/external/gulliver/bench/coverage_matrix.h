// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_COVERAGE_MATRIX_H_
#define GULLIVER_BENCH_COVERAGE_MATRIX_H_

#include <string>
#include <vector>

#include "../common/json.h"

namespace gulliver
{

    // Les quatre référentiels du périmètre.  nist_ai_rmf, présent dans le catalogue,
    // est hors périmètre des sondes et n'est pas compté, comme dans l'outil d'origine.
    extern const char* const framework_keys[4];   // atlas, owasp_llm, atos_blocks, atos_build

    // Plie les déclarations de couverture des sondes (et des outils, s'il y en a) contre
    // le catalogue des référentiels, et rend la matrice : par référentiel, ce qui est
    // couvert, ce qui ne l'est pas (les lacunes, le point important) et ce qui est hors
    // de portée d'une sonde.  Un résumé par référentiel porte le ratio couvert / périmètre.
    //
    //   packs       chaque élément est un pack {"probes":[{id, coverage:{atlas:[...], ...}}]}
    //   tools       catalogue d'outils, chacun {name, coverage:{...}} ; vide si aucun
    //   frameworks  le catalogue chargé depuis frameworks.json
    json build_coverage(const std::vector<json>& packs,
                        const std::vector<json>& tools,
                        const json& frameworks);

    // Les ratios de couverture, un par référentiel, tirés du résumé de build_coverage.
    // C'est ce que la notation moyenne pour obtenir la portée (reach).
    std::vector<double> coverage_ratios(const json& coverage);

}

#endif  // GULLIVER_BENCH_COVERAGE_MATRIX_H_
