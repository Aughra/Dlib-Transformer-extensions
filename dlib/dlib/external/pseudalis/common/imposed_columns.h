// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_IMPOSED_COLUMNS_H_
#define PSEUDALIS_COMMON_IMPOSED_COLUMNS_H_

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include <dlib/privacy/unicode_text.h>

#include "json.h"

namespace pseudalis
{
    /*
        Les colonnes que les administrateurs imposent aux fichiers CSV à en-tête : la colonne des
        noms des clients ou de leurs adresses e-mail dans un export, par exemple.  Chaque valeur
        sous le nom d'une telle colonne dans l'en-tête d'un fichier prend le substitut du type de
        la colonne, sans détection et sans étiquette, quelles que soient les étiquettes du reste du
        fichier (dlib::pii_imposed_column) ; l'enregistrement du fichier garde chaque substitut avec
        son étiquette, comme pour un fichier protégé sans étiquettes.  Un nom se compare sans tenir
        compte de la casse ni des accents, une fois retirées les espaces à ses bouts.  Le service
        garde les colonnes dans sa base de données privée au compte qui l'exécute, et les donne au
        moteur.
    */
    const std::size_t max_imposed_columns = 100;
    // En caractères (points de code), une fois retirées les espaces aux deux bouts.
    const std::size_t max_imposed_column_length = 200;

    struct imposed_column
    {
        std::string name;
        std::string type = "PERSON";
    };

    /*
        Les types qu'une colonne imposée peut prendre, dans l'ordre où l'interface les liste.  IP
        désigne les adresses IP des deux formes, chaque valeur prenant le substitut de la sienne,
        IPv4 ou IPv6, en gardant sa longueur de préfixe, sa zone ou son port.
    */
    inline const std::vector<std::string>& imposed_column_types()
    {
        static const std::vector<std::string> types = { "PERSON", "ORG", "LOCATION", "ADDRESS", "EMAIL", "PHONE",
                                                        "ID", "DATE", "IBAN", "CARD", "NIR", "SIRET", "URL", "IP",
                                                        "MAC", "PLATE", "GEO", "TITLE", "GENDER", "NATIONALITY" };
        return types;
    }

    /*
        Lit une liste [{"column", "type"}], chaque nom débarrassé des espaces à ses bouts.  Renvoie
        false avec un message, un code et la position de la colonne fautive (-1 pour la liste
        elle-même) quand la liste n'est pas de celles que le service accepte :
            columns_invalid    pas une liste, une entrée sans nom, un nom vide, plus long que
                               max_imposed_column_length, sur plusieurs lignes, avec un caractère de
                               contrôle ou de l'UTF-8 invalide, ou sans lettre ni chiffre ;
            columns_too_many   plus de max_imposed_columns ;
            columns_type       un type hors de imposed_column_types() ;
            columns_duplicate  un nom donné deux fois, sans tenir compte de la casse ni des accents.
    */
    inline bool parse_imposed_columns(const json& list, std::vector<imposed_column>& columns, std::string& message,
                                      std::string& code, long long& index)
    {
        using namespace dlib::privacy_impl;
        columns.clear();
        index = -1;
        if (!list.is_array())
        {
            message = "the columns must be a list";
            code = "columns_invalid";
            return false;
        }
        if (list.size() > max_imposed_columns)
        {
            message = "at most " + std::to_string(max_imposed_columns) + " columns may be imposed";
            code = "columns_too_many";
            return false;
        }
        std::set<std::string> seen;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const json& item = list[i];
            index = static_cast<long long>(i);
            imposed_column c;
            const std::string raw = item.is_object() ? item["column"].str() : std::string();
            std::size_t b = 0, e = raw.size();
            while (b < e && (raw[b] == ' ' || raw[b] == '\t')) ++b;
            while (e > b && (raw[e - 1] == ' ' || raw[e - 1] == '\t')) --e;
            c.name = raw.substr(b, e - b);
            bool ok = !c.name.empty() && utf8_valid(c.name);
            std::size_t length = 0;
            bool word = false;
            for (std::size_t k = 0; ok && k < c.name.size();)
            {
                const char32_t ch = utf8_next(c.name, k);
                ++length;
                if (ch < 0x20 || ch == 0x7F || is_line_break(ch)) ok = false;
                if (is_word_char(ch)) word = true;
            }
            if (!ok || !word || length > max_imposed_column_length)
            {
                message = "each column must be named by a text of one line, of 1 to " +
                          std::to_string(max_imposed_column_length) + " characters, with a letter or a digit";
                code = "columns_invalid";
                return false;
            }
            c.type = item["type"].str("PERSON");
            bool known = false;
            for (const std::string& k : imposed_column_types()) known = known || k == c.type;
            if (!known)
            {
                message = "the type of a column must be one of the types of the tags";
                code = "columns_type";
                return false;
            }
            if (!seen.insert(fold_key(c.name)).second)
            {
                message = "the column \"" + c.name + "\" is given twice";
                code = "columns_duplicate";
                return false;
            }
            columns.push_back(c);
        }
        index = -1;
        return true;
    }

    inline json imposed_columns_json(const std::vector<imposed_column>& columns)
    {
        json list = json::array();
        for (const imposed_column& c : columns)
        {
            json item = json::object();
            item["column"] = c.name;
            item["type"] = c.type;
            list.push_back(item);
        }
        return list;
    }
}

#endif // PSEUDALIS_COMMON_IMPOSED_COLUMNS_H_
