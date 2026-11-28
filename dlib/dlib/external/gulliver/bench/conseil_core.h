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
        json removed;                         // --removed : la liste déjà retirée (objets), recopiée telle quelle
        std::string rounds = "2";             // --rounds : le nombre d'essais, cité dans l'intro
        std::string machine;                  // --machine : « modèle|puce|macOS|Gulliver|charge|sonde »
        std::string temperature;              // --temperature : « t_min|t_moy|t_max|c_…|g_…|relevés »
        json service;                         // --service : le JSON de bench_service.py (null si absent)
        json rag;                             // --rag : le JSON de test_rag.py (null si absent)
        bool late = false;                    // --late
        // --cyber : la note de cybersécurité par modèle, { "id" : <cyber_grade.to_json()> }.
        // Alimenté par l'épreuve (host/cyber_score) ; vide ici, la sortie reste celle de Python.
        json cyber;
    };

    // Porte le cœur de conseil_modeles.py : lit les lignes brutes du banc (format
    // id|nom|image|contexte|kv|lecture|écriture|où, plus ppl|id|val et quiz|id|note),
    // choisit les modèles conseillés (pour tout faire / le plus juste / le plus rapide /
    // cybersécurité) et le modèle à retirer, et rend l'objet conseil.json — aux MÊMES
    // clés que l'outil Python (colonnes cyber ajoutées ailleurs).  `now` est la date
    // écrite dans le champ "date" (injectée pour que le résultat soit reproductible).  Si
    // opt.cyber porte la note d'un modèle conseillé, elle est ajoutée à son entrée
    // ("cyber") ; sans elle, la sortie est identique à celle de l'outil Python.
    json build_conseil(const std::string& raw_text, const conseil_options& opt,
                       const std::string& now);

    // La même matière, rendue en Markdown sur stdout comme conseil_modeles.py : le
    // tableau de tous les modèles (trié par écriture décroissante) puis la section
    // « Conseil ».  Doit être identique à la sortie Python.
    std::string build_conseil_markdown(const std::string& raw_text, const conseil_options& opt,
                                       const std::string& now);

    // Réécrit la section de la semaine du document de vitesse (entre « <!-- semaine :
    // début --> » et « <!-- semaine : fin --> ») : Gulliver seul, les modèles mesurés.
    // `when` est la date « JJ/MM/AAAA à HH h MM ».  Rend le document entier ; inchangé si
    // les marqueurs sont absents.
    std::string build_conseil_doc(const std::string& raw_text, const conseil_options& opt,
                                  const std::string& when, const std::string& doc_text);

}

#endif  // GULLIVER_BENCH_CONSEIL_CORE_H_
