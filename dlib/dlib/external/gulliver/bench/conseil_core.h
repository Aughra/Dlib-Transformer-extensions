// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_CONSEIL_CORE_H_
#define GULLIVER_BENCH_CONSEIL_CORE_H_

#include <set>
#include <string>
#include <vector>

#include "../common/json.h"

namespace gulliver
{

    // Les options de ligne de commande de conseil_modeles.py qui changent la sortie JSON.
    // (Le tableau Markdown et update_doc sont portés à part ; ici seul le conseil.json.)
    struct conseil_options
    {
        std::string previous;                 // --previous : le choix de la semaine dernière
        std::set<std::string> fresh;          // --new : les modèles nouveaux cette semaine
        std::set<std::string> keep;           // --keep : les modèles actifs, jamais retirés
        std::vector<std::string> removed;     // --removed : déjà retirés, recopiés tels quels
        bool late = false;                    // --late
    };

    // Porte le cœur de conseil_modeles.py : lit les lignes brutes du banc (format
    // id|nom|image|contexte|kv|lecture|écriture|où, plus ppl|id|val et quiz|id|note),
    // choisit les modèles conseillés (pour tout faire / le plus juste / le plus rapide /
    // cybersécurité) et le modèle à retirer, et rend l'objet conseil.json — aux MÊMES
    // clés que l'outil Python (colonnes cyber ajoutées ailleurs).  `now` est la date
    // écrite dans le champ "date" (injectée pour que le résultat soit reproductible).
    json build_conseil(const std::string& raw_text, const conseil_options& opt,
                       const std::string& now);

}

#endif  // GULLIVER_BENCH_CONSEIL_CORE_H_
