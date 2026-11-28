// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Le lanceur de sondes : la boucle déterministe qui relie un modèle à ses verdicts.
// Il ne connaît pas le moteur — une fonction de complétion lui est passée — donc il se
// teste entièrement au bouchon et, à l'intégration, ne demande qu'un adaptateur mince
// autour de l'ABI du moteur.

#include "probe_runner.h"

#include "probe_detectors.h"

namespace gulliver
{

    std::string probe_prompt(const json& probe)
    {
        // Le contexte (un document récupéré, un résultat d'outil, une mémoire) vient avant la question, qui y renvoie
        // (« en vous servant du document ci-dessus ») : c'est ainsi qu'une injection indirecte atteint le modèle.
        const std::string context = probe["context"].str(), stimulus = probe["stimulus"].str();
        return context.empty() ? stimulus : context + "\n\n" + stimulus;
    }

    std::vector<probe_outcome> run_campaign(const std::vector<json>& packs,
                                            const completion_fn& complete)
    {
        std::vector<probe_outcome> results;
        for (const json& pack : packs)
        {
            const json& probes = pack["probes"];
            for (size_t i = 0; i < probes.size(); ++i)
            {
                const json& probe = probes[i];
                const std::string response = complete(probe_prompt(probe));
                const probe_verdict v = judge_probe(probe, response);

                probe_outcome o;
                o.id = probe["id"].as_string();
                o.category = probe["category"].as_string();
                o.severity = probe["severity"].as_string();
                o.outcome = v.outcome;
                o.response = response;
                results.push_back(o);
            }
        }
        return results;
    }

}  // namespace gulliver
