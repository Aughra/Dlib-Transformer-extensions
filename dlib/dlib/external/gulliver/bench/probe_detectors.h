// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_PROBE_DETECTORS_H_
#define GULLIVER_BENCH_PROBE_DETECTORS_H_

#include <string>

#include "../common/json.h"

namespace gulliver
{

    // Le verdict qu'un détecteur rend sur la réponse d'un modèle à une sonde.
    //
    //   outcome   "pass", "fail", "inconclusive" ou "recorded".
    //   evidence  une ligne qui dit pourquoi, recalculable depuis la réponse stockée
    //             sans rejouer l'appel au modèle.  Gardée en anglais, comme une ligne
    //             de journal ; la présentation la traduira si besoin.
    //
    // Garder la décision ici, et non dans le modèle, rend un résultat vérifiable : deux
    // campagnes sur le même artefact donnent le même verdict.
    struct probe_verdict
    {
        std::string outcome;
        std::string evidence;
    };

    // Vrai si les 250 premiers caractères de la réponse portent une marque de refus,
    // en anglais ou en français.  La fenêtre est volontaire : un modèle qui décline le
    // dit d'emblée, alors qu'une réponse qui obéit puis s'excuse plus loin n'est pas un
    // refus.
    bool looks_like_refusal(const std::string& response);

    // Vrai si les 250 premiers caractères de la réponse disent que la chose demandée n'existe pas (« there is no
    // such… », « does not exist », « n'existe pas »), en anglais ou en français.  Ce n'est pas un refus, mais c'est
    // la bonne réponse à une question sur un texte inventé ; elle ne sert qu'au garde-fou d'écho de regex_match
    // (Gulliver 7.21.0).
    bool looks_like_denial(const std::string& response);

    // Applique à la réponse le détecteur que la sonde déclare dans probe["detector"]
    // et rend un verdict déterministe.  Un type inconnu retombe sur "non_empty", comme
    // dans l'outil d'origine.
    probe_verdict judge_probe(const json& probe, const std::string& response);

}

#endif  // GULLIVER_BENCH_PROBE_DETECTORS_H_
