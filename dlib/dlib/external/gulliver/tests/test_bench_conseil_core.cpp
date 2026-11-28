// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Test « en or » du cœur de l'assembleur conseil : sur le .raw réel du banc, le C++ doit
// produire le MÊME conseil.json que conseil_modeles.py (référence tests/fixtures/conseil_ref.json),
// au champ « date » près (il dépend de l'heure).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../common/json.h"
#include "../bench/conseil_core.h"

using gulliver::json;

static std::string slurp(const std::string& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}
static json load(const std::string& p) { json j; std::string e;
    if (!json::parse(slurp(p), j, &e)) std::fprintf(stderr, "parse %s: %s\n", p.c_str(), e.c_str());
    return j; }

// Égalité profonde, nombres à epsilon près ; rend le chemin du premier écart.
static bool jeq(const json& a, const json& b, std::string path, std::string& diff)
{
    auto fail = [&](const std::string& why) { diff = path + ": " + why; return false; };
    if (a.is_object() || b.is_object())
    {
        if (!a.is_object() || !b.is_object()) return fail("type (objet)");
        if (a.size() != b.size()) return fail("nb de clés " + std::to_string(a.size()) + " vs " + std::to_string(b.size()));
        for (const auto& kv : a.items())
        {
            if (!b.contains(kv.first)) return fail("clé manquante " + kv.first);
            if (!jeq(kv.second, b[kv.first], path + "." + kv.first, diff)) return false;
        }
        return true;
    }
    if (a.is_array() || b.is_array())
    {
        if (!a.is_array() || !b.is_array()) return fail("type (tableau)");
        if (a.size() != b.size()) return fail("taille " + std::to_string(a.size()) + " vs " + std::to_string(b.size()));
        for (size_t i = 0; i < a.size(); ++i)
            if (!jeq(a[i], b[i], path + "[" + std::to_string(i) + "]", diff)) return false;
        return true;
    }
    if (a.is_number() || b.is_number())
    {
        if (!a.is_number() || !b.is_number()) return fail("type (nombre)");
        const double x = a.as_double(), y = b.as_double();
        return (x - y < 1e-6 && y - x < 1e-6) ? true : fail("nombre " + std::to_string(x) + " vs " + std::to_string(y));
    }
    if (a.is_string() || b.is_string())
    {
        if (!a.is_string() || !b.is_string()) return fail("type (chaîne)");
        return a.as_string() == b.as_string() ? true : fail("chaîne \"" + a.as_string() + "\" vs \"" + b.as_string() + "\"");
    }
    if (a.is_bool() || b.is_bool())
    {
        if (!a.is_bool() || !b.is_bool()) return fail("type (booléen)");
        return a.as_bool() == b.as_bool() ? true : fail("booléen");
    }
    if (a.is_null() && b.is_null()) return true;
    return fail("type incomparable");
}

int main(int argc, char** argv)
{
    const std::string raw_path = (argc > 1) ? argv[1]
        : "bench/tests/fixtures/modeles-20261124-1350.raw";
    const std::string ref_path = (argc > 2) ? argv[2] : "bench/tests/fixtures/conseil_ref.json";

    const gulliver::conseil_options opt;   // défauts : comme le run Python de référence
    json out = gulliver::build_conseil(slurp(raw_path), opt, "2026-00-00 00:00");
    json ref = load(ref_path);
    ref["date"] = out["date"];             // ignorer la date

    std::string diff;
    if (jeq(out, ref, "conseil", diff))
    {
        std::fprintf(stdout, "ok   conseil.json identique à la référence Python\n");
        json a = out["advice"];
        for (size_t i = 0; i < a.size(); ++i)
            std::fprintf(stdout, "     %-9s %s\n", a[i]["role"].as_string().c_str(), a[i]["name"].as_string().c_str());
        std::fprintf(stdout, "     remove    %s\n",
                     out["remove"].is_object() ? out["remove"]["name"].as_string().c_str() : "(aucun)");
        std::fprintf(stdout, "\nall tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "FAIL %s\n", diff.c_str());
    std::fprintf(stderr, "--- obtenu ---\n%s\n", out.dump_formatted(1).c_str());
    return 1;
}
