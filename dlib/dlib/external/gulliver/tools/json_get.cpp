// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Une valeur d'un document JSON, pour les scripts shell (Gulliver 7.24.0, à la place des « python3 -c 'import json…' »
    des scripts de mesure).

        build/macos-gulliver/bin/json_get SOURCE CHEMIN [--join SÉPARATEUR] [--round N] [--default TEXTE]

    SOURCE est un fichier, ou « - » pour l'entrée standard.  CHEMIN suit les clés et les rangs séparés par des points
    (« advice.0.id », « 0.avg_ts ») ; « . » désigne le document entier.  Écrit la valeur : un texte tel quel, un nombre
    comme Python l'écrirait (« 62.0 », « 1234.5 »), une liste de textes ou de nombres jointe par --join (une espace par
    défaut).  --round N arrondit un nombre à N décimales, comme round() de Python.  Une valeur absente ou nulle, ou un
    document illisible, écrit --default (rien par défaut) ; le code de sortie est toujours 0, comme les « try / except »
    des commandes qu'il remplace.
*/

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

#include "../common/json.h"
#include "../common/platform.h"

using gulliver::json;

namespace
{
    // Un nombre écrit comme str() de Python : un entier tel quel, un flottant au plus court, avec « .0 » s'il est rond.
    std::string python_number(const json& v)
    {
        if (v.is_integer()) return std::to_string(v.as_int());
        const double x = v.as_double();
        char buf[64];
        for (int p = 1; p <= 17; ++p)
        {
            std::snprintf(buf, sizeof(buf), "%.*g", p, x);
            if (std::strtod(buf, nullptr) == x) break;
        }
        std::string s = buf;
        if (s.find_first_of(".ein") == std::string::npos) s += ".0";
        return s;
    }

    std::string text_of(const json& v)
    {
        if (v.is_string()) return v.as_string();
        if (v.is_number()) return python_number(v);
        if (v.is_bool()) return v.as_bool() ? "True" : "False";
        return v.dump();
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: json_get SOURCE PATH [--join SEP] [--round N] [--default TEXT]\n");
        return 2;
    }
    const std::string source = argv[1], path = argv[2];
    std::string sep = " ", fallback;
    int digits = -1;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        const std::string a = argv[i];
        if (a == "--join") sep = argv[i + 1];
        else if (a == "--round") digits = std::atoi(argv[i + 1]);
        else if (a == "--default") fallback = argv[i + 1];
    }
    std::string text;
    if (source == "-") text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    else if (!gulliver::read_file(source, text, 256u << 20)) text.clear();
    json doc;
    if (!json::parse(text, doc))
    {
        std::printf("%s\n", fallback.c_str());
        return 0;
    }
    const json* v = &doc;
    if (path != ".")
    {
        size_t from = 0;
        while (from <= path.size())
        {
            size_t end = path.find('.', from);
            if (end == std::string::npos) end = path.size();
            const std::string key = path.substr(from, end - from);
            const bool index = !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
            if (index && v->is_array()) v = &(*v)[static_cast<size_t>(std::stoul(key))];
            else v = &(*v)[key];
            from = end + 1;
        }
    }
    json value = *v;
    if (digits >= 0 && value.is_number())
    {
        const double scale = std::pow(10.0, digits);
        value = json(std::nearbyint(value.as_double() * scale) / scale);
    }
    if (value.is_null())
    {
        std::printf("%s\n", fallback.c_str());
        return 0;
    }
    if (value.is_array())
    {
        std::string out;
        for (size_t i = 0; i < value.size(); ++i) out += (i ? sep : "") + text_of(value[i]);
        std::printf("%s\n", out.c_str());
        return 0;
    }
    std::printf("%s\n", text_of(value).c_str());
    return 0;
}
