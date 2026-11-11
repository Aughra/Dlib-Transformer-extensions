// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_USER_ERROR_H_
#define PSEUDALIS_HOST_USER_ERROR_H_

#include <string>

#include "../common/json.h"

namespace pseudalis
{

    struct user_error
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'échec d'une opération qu'un utilisateur a demandée : son texte en anglais,
                pour le journal et pour les clients de l'interface, et le code avec lequel
                les fenêtres l'expliquent dans la langue de l'utilisateur (les textes
                err_<code> de windows/strings.cpp), avec ses valeurs et ses tailles en
                octets, que les fenêtres écrivent dans les unités de cette langue.
        !*/
        std::string text;
        std::string code;
        json args = json::object();
        json sizes = json::object();

        user_error& set(const std::string& english, const std::string& error_code = std::string())
        {
            text = english;
            code = error_code;
            return *this;
        }

        user_error& arg(const std::string& name, const std::string& value)
        {
            args[name] = value;
            return *this;
        }

        user_error& size(const std::string& name, unsigned long long bytes)
        {
            sizes[name] = bytes;
            return *this;
        }

        // Ajoute error, et code avec ses valeurs quand il y en a un, à la réponse d'une requête.
        void add_to(json& answer) const
        {
            answer["error"] = text;
            if (code.empty()) return;
            answer["code"] = code;
            if (args.size() > 0) answer["args"] = args;
            if (sizes.size() > 0) answer["sizes"] = sizes;
        }
    };

}

#endif // PSEUDALIS_HOST_USER_ERROR_H_
