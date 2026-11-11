// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_IMPOSED_TERMS_H_
#define PSEUDALIS_COMMON_IMPOSED_TERMS_H_

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include <dlib/privacy/unicode_text.h>

#include "json.h"

namespace pseudalis
{
    /*
        Les termes que les administrateurs imposent : le nom d'un client, d'un projet, d'un
        contrat ou d'une organisation que le détecteur ne connaît pas.  Chaque occurrence d'un
        terme est remplacée par l'étiquette de son type avant que le détecteur ne lise quoi que ce
        soit d'autre du texte (dlib::pii_imposed_term) : ses mots et ses nombres dans le même
        ordre, en mots entiers, séparés par au plus deux caractères d'une même ligne, en
        respectant leur casse et leurs accents quand case_sensitive vaut true, en ignorant les deux
        sinon.  Le service les garde dans sa base de données, que seuls SYSTEM et les
        administrateurs lisent, et les donne au moteur.
    */
    const std::size_t max_imposed_terms = 100;
    // En caractères (points de code), une fois retirées les espaces aux deux bouts.
    const std::size_t max_imposed_term_length = 200;

    struct imposed_term
    {
        std::string term;
        std::string type = "ORG";
        bool case_sensitive = false;
    };

    // Les types qu'un terme imposé peut prendre, dans l'ordre où l'interface les liste.
    inline const std::vector<std::string>& imposed_term_types()
    {
        static const std::vector<std::string> types = { "ORG", "PERSON", "LOCATION", "ADDRESS", "ID", "EMAIL",
                                                        "PHONE", "URL" };
        return types;
    }

    /*
        Lit une liste [{"term", "type", "case_sensitive"}], chaque terme débarrassé des espaces à
        ses bouts.  Renvoie false avec un message, un code et la position du terme fautif (-1 pour
        la liste elle-même) quand la liste n'est pas de celles que le service accepte :
            terms_invalid    pas une liste, une entrée sans terme, un terme vide, plus long que
                             max_imposed_term_length, sur plusieurs lignes, avec un caractère de
                             contrôle ou de l'UTF-8 invalide, ou sans lettre ni chiffre ;
            terms_too_many   plus de max_imposed_terms ;
            terms_type       un type hors de imposed_term_types() ;
            terms_duplicate  un terme donné deux fois avec la même sensibilité à la casse (sans
                             elle, deux termes qui diffèrent par leur casse ou leurs accents sont
                             identiques).
    */
    inline bool parse_imposed_terms(const json& list, std::vector<imposed_term>& terms, std::string& message,
                                    std::string& code, long long& index)
    {
        using namespace dlib::privacy_impl;
        terms.clear();
        index = -1;
        if (!list.is_array())
        {
            message = "the terms must be a list";
            code = "terms_invalid";
            return false;
        }
        if (list.size() > max_imposed_terms)
        {
            message = "at most " + std::to_string(max_imposed_terms) + " terms may be imposed";
            code = "terms_too_many";
            return false;
        }
        std::set<std::string> seen;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const json& item = list[i];
            index = static_cast<long long>(i);
            imposed_term t;
            const std::string raw = item.is_object() ? item["term"].str() : std::string();
            // Les espaces aux deux bouts s'en vont ; ce qui reste est le terme.
            std::size_t b = 0, e = raw.size();
            while (b < e && (raw[b] == ' ' || raw[b] == '\t')) ++b;
            while (e > b && (raw[e - 1] == ' ' || raw[e - 1] == '\t')) --e;
            t.term = raw.substr(b, e - b);
            bool ok = !t.term.empty() && utf8_valid(t.term);
            std::size_t length = 0;
            bool word = false;
            for (std::size_t k = 0; ok && k < t.term.size();)
            {
                const char32_t c = utf8_next(t.term, k);
                ++length;
                if (c < 0x20 || c == 0x7F || is_line_break(c)) ok = false;
                if (is_word_char(c)) word = true;
            }
            if (!ok || !word || length > max_imposed_term_length)
            {
                message = "each term must be a text of one line, of 1 to " + std::to_string(max_imposed_term_length) +
                          " characters, with a letter or a digit";
                code = "terms_invalid";
                return false;
            }
            t.type = item["type"].str("ORG");
            bool known = false;
            for (const std::string& k : imposed_term_types()) known = known || k == t.type;
            if (!known)
            {
                message = "the type of a term must be one of ORG, PERSON, LOCATION, ADDRESS, ID, EMAIL, PHONE, URL";
                code = "terms_type";
                return false;
            }
            t.case_sensitive = item["case_sensitive"].as_bool(false);
            const std::string key = (t.case_sensitive ? "1" : "0") + (t.case_sensitive ? t.term : fold_key(t.term));
            if (!seen.insert(key).second)
            {
                message = "the term \"" + t.term + "\" is given twice";
                code = "terms_duplicate";
                return false;
            }
            terms.push_back(t);
        }
        index = -1;
        return true;
    }

    inline json imposed_terms_json(const std::vector<imposed_term>& terms)
    {
        json list = json::array();
        for (const imposed_term& t : terms)
        {
            json item = json::object();
            item["term"] = t.term;
            item["type"] = t.type;
            item["case_sensitive"] = t.case_sensitive;
            list.push_back(item);
        }
        return list;
    }
}

#endif // PSEUDALIS_COMMON_IMPOSED_TERMS_H_
