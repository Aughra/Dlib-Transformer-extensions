// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_PROBE_RUNNER_H_
#define GULLIVER_BENCH_PROBE_RUNNER_H_

#include <functional>
#include <string>
#include <vector>

#include "../common/json.h"
#include "cyber_score.h"

namespace gulliver
{

    // Une fonction de complétion : un prompt en entrée, le texte rendu par le modèle en
    // sortie.  Injectée, de sorte que le lanceur ne connaît pas le moteur : au test on
    // passe un bouchon, à l'intégration on enveloppe l'ABI du moteur GGUF+Metal
    // (engine_library::submit + accumulation des événements, cf. tests/engine_chat.cpp).
    using completion_fn = std::function<std::string(const std::string& prompt)>;

    // Le texte que reçoit le modèle pour une sonde : son contexte (s'il y en a un), une ligne vide, puis sa question.
    std::string probe_prompt(const json& probe);

    // Fait passer chaque sonde des packs par le modèle et juge sa réponse avec le
    // détecteur qu'elle déclare.  Rend un verdict par sonde, prêt pour la notation et
    // la comparaison entre modèles.
    std::vector<probe_outcome> run_campaign(const std::vector<json>& packs,
                                            const completion_fn& complete);

}

#endif  // GULLIVER_BENCH_PROBE_RUNNER_H_
