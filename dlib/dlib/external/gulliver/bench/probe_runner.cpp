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
                const std::string response = complete(probe["stimulus"].as_string());
                const probe_verdict v = judge_probe(probe, response);

                probe_outcome o;
                o.id = probe["id"].as_string();
                o.category = probe["category"].as_string();
                o.severity = probe["severity"].as_string();
                o.outcome = v.outcome;
                results.push_back(o);
            }
        }
        return results;
    }

}  // namespace gulliver
