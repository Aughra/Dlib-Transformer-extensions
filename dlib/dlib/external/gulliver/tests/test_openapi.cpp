// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    La description de l'interface (web/openapi.json) contre les routes du service (host/server.cpp).
    Depuis la 7.16.0, web/openapi.json est le fichier de référence, modifié à la main : l'outil Python qui le
    réécrivait (tools/make_openapi.py) est retiré (décision de l'humain du 28 novembre 2026 : tout en C++).
    Ce test reprend ses contrôles :
      - chaque route que déclare server.cpp (srv.Get, srv.Post…) a son opération dans openapi.json, et
        chaque opération d'openapi.json a sa route ;
      - une route réservée à l'administrateur depuis le réseau (require_manager) donne ses réponses 401 et 403 ;
      - le document se lit, et sa version est celle de common/version.h.

        test_openapi <dossier de Gulliver>
*/

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../common/json.h"
#include "../common/version.h"

using gulliver::json;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    }

    std::string slurp(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    // Un chemin où chaque paramètre, {id} d'OpenAPI ou groupe (…) d'une expression de httplib, devient « * ».
    std::string shape(const std::string& path)
    {
        std::string out;
        int depth = 0;
        for (char c : path)
        {
            if (c == '{' || c == '(')
            {
                if (depth++ == 0) out += '*';
            }
            else if (c == '}' || c == ')') --depth;
            else if (depth == 0) out += c;
        }
        return out;
    }

    struct route
    {
        std::string method;   // get, post…
        std::string path;     // tel que déclaré
        bool manager = false; // son code appelle require_manager
    };

    // Les routes de server.cpp : srv.Get("…") ou srv.Get(R"(…)"), et le code jusqu'à la route suivante.
    std::vector<route> routes_of(const std::string& src)
    {
        static const char* const methods[] = {"Get", "Post", "Put", "Delete", "Patch"};
        struct at { size_t pos; route r; };
        std::vector<at> found;
        for (const char* m : methods)
        {
            const std::string key = std::string("srv.") + m + "(";
            for (size_t p = src.find(key); p != std::string::npos; p = src.find(key, p + 1))
            {
                size_t q = p + key.size();
                std::string path;
                if (src.compare(q, 3, "R\"(") == 0)
                {
                    const size_t end = src.find(")\"", q + 3);
                    if (end == std::string::npos) continue;
                    path = src.substr(q + 3, end - q - 3);
                }
                else if (src[q] == '"')
                {
                    const size_t end = src.find('"', q + 1);
                    if (end == std::string::npos) continue;
                    path = src.substr(q + 1, end - q - 1);
                }
                else
                {
                    // Un nom : la constante qui donne le chemin (const char* const upload_route = "…").
                    size_t end = q;
                    while (end < src.size() && (std::isalnum(static_cast<unsigned char>(src[end])) || src[end] == '_')) ++end;
                    const std::string name = src.substr(q, end - q);
                    const size_t def = name.empty() ? std::string::npos : src.find("const " + name + " = \"");
                    if (def == std::string::npos) continue;
                    const size_t start = src.find('"', def) + 1;
                    path = src.substr(start, src.find('"', start) - start);
                }
                // Seule l'interface est décrite : /v1 et /gulliver ; les pages (/docs, /carnet…) n'y sont pas.
                if (path.compare(0, 4, "/v1/") != 0 && path.compare(0, 10, "/gulliver/") != 0) continue;
                route r;
                r.method = m;
                for (char& c : r.method) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                r.path = path;
                found.push_back({p, r});
            }
        }
        std::sort(found.begin(), found.end(), [](const at& a, const at& b) { return a.pos < b.pos; });
        std::vector<route> out;
        for (size_t i = 0; i < found.size(); ++i)
        {
            const size_t end = i + 1 < found.size() ? found[i + 1].pos : src.size();
            found[i].r.manager = src.substr(found[i].pos, end - found[i].pos).find("require_manager(") != std::string::npos;
            out.push_back(found[i].r);
        }
        return out;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: test_openapi <folder of Gulliver>\n");
        return 2;
    }
    const std::string root = argv[1];
    json doc;
    std::string error;
    check(json::parse(slurp(root + "/web/openapi.json"), doc, &error) && doc["paths"].is_object(),
        "web/openapi.json reads (" + error + ")");
    check(doc["info"]["version"].as_string() == GULLIVER_VERSION,
        "web/openapi.json gives the version of common/version.h (" + doc["info"]["version"].as_string() + ")");

    // Les opérations documentées, par méthode et forme de chemin.
    std::map<std::string, const json*> documented;
    for (const auto& p : doc["paths"].items())
        for (const auto& op : p.second.items())
            documented[op.first + " " + shape(p.first)] = &op.second;

    const std::vector<route> routes = routes_of(slurp(root + "/host/server.cpp"));
    check(routes.size() > 50, "host/server.cpp declares its routes (" + std::to_string(routes.size()) + ")");
    std::set<std::string> served;
    for (const route& r : routes)
    {
        const std::string key = r.method + " " + shape(r.path);
        served.insert(key);
        const auto it = documented.find(key);
        check(it != documented.end(), "the route " + r.method + " " + r.path + " is in web/openapi.json");
        if (it != documented.end() && r.manager)
        {
            const json& replies = (*it->second)["responses"];
            check(replies.contains("401") && replies.contains("403"),
                "the route " + r.method + " " + r.path + " of the administrator documents 401 and 403");
        }
    }
    for (const auto& d : documented)
        check(served.count(d.first) > 0, "the operation " + d.first + " of web/openapi.json has its route");

    // Chaque renvoi ($ref "#/components/…/nom") nomme une entrée qui existe.
    std::vector<std::string> refs;
    std::vector<const json*> todo{&doc};
    while (!todo.empty())
    {
        const json* j = todo.back();
        todo.pop_back();
        for (const auto& kv : j->items())
        {
            if (kv.first == "$ref") refs.push_back(kv.second.as_string());
            todo.push_back(&kv.second);
        }
        for (const auto& e : j->elements()) todo.push_back(&e);
    }
    std::set<std::string> missing;
    for (const std::string& r : refs)
    {
        const std::string prefix = "#/components/";
        const size_t slash = r.find('/', prefix.size());
        if (r.compare(0, prefix.size(), prefix) != 0 || slash == std::string::npos
            || !doc["components"][r.substr(prefix.size(), slash - prefix.size())].contains(r.substr(slash + 1)))
            missing.insert(r);
    }
    std::string names;
    for (const std::string& m : missing) names += " " + m;
    check(!refs.empty() && missing.empty(), "each of the " + std::to_string(refs.size())
        + " references of web/openapi.json names an entry of its components" + names);

    if (failures == 0) std::printf("all checks passed\n");
    else std::printf("%d checks FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
