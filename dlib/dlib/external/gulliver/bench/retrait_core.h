// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_RETRAIT_CORE_H_
#define GULLIVER_BENCH_RETRAIT_CORE_H_

/*
    Le retrait du dernier du classement, après la mesure du lundi (Gulliver 7.20.0, à la place de
    tools/retrait_modeles.py ; décision de l'humain du 2026-11-24 : « tu le retires seul », et un menu des modèles
    retirés avec leur date).

    conseil.json (programme conseil) désigne dans « remove » le dernier du classement : la plus basse note à
    l'épreuve, puis la plus lente écriture, hors des modèles conseillés, du choix précédent et des modèles actifs.  Ce
    cœur décide, sans rien toucher, ce qu'il faut faire de ce modèle à partir de conseil.json, de la liste des modèles
    déjà retirés (retires.json) et de la liste des modèles du service (GET /gulliver/v1/models) ; le programme
    retrait_modeles (bench/retrait_main.cpp) fait le reste : l'effacement par le service, les deux fichiers, la ligne
    du mail.
*/

#include <ctime>
#include <string>

#include "../common/json.h"

namespace gulliver
{
    struct retrait_plan
    {
        enum class action
        {
            nothing,         // rien dans « remove »
            this_week,       // un retrait a déjà eu lieu cette semaine
            absent,          // le modèle n'est plus installé
            active,          // le modèle sert à un rôle : laissé
            remove           // à retirer : entry est la ligne de retires.json
        };
        action what = action::nothing;
        std::string id;
        json entry;          // date, id, name, origin, quiz, write_512, why (et repo, file, projector de Hugging Face)
        std::string message; // la ligne écrite par le programme (la dernière sert au mail), sauf pour remove
        std::string note;    // « épreuve N/100 » ou la raison du retrait
    };

    // Ce qu'il faut faire du modèle désigné par advice["remove"], à la date now (heure locale).
    retrait_plan plan_removal(const json& advice, const json& removed, const json& models, std::time_t now);

    // La date « AAAA-MM-JJ HH:MM » de now, en heure locale, comme dans retires.json.
    std::string removal_date(std::time_t now);
}

#endif // GULLIVER_BENCH_RETRAIT_CORE_H_
