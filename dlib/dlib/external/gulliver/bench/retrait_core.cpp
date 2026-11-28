// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "retrait_core.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace gulliver
{
    namespace
    {
        // La date d'une ligne de retires.json, ou 0 quand elle ne se lit pas (Python : « 1970-01-01 00:00 »).
        std::time_t parse_date(const std::string& text)
        {
            std::tm t{};
            if (!strptime(text.c_str(), "%Y-%m-%d %H:%M", &t)) return 0;
            t.tm_isdst = -1;
            return std::mktime(&t);
        }

        std::string base_name(const std::string& path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        // Le premier texte non vide, comme « a or b or c » en Python.
        std::string first_of(std::initializer_list<std::string> values)
        {
            for (const std::string& v : values)
                if (!v.empty()) return v;
            return std::string();
        }
    }

    std::string removal_date(std::time_t now)
    {
        std::tm t{};
        localtime_r(&now, &t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
        return buf;
    }

    retrait_plan plan_removal(const json& advice, const json& removed, const json& models, std::time_t now)
    {
        retrait_plan p;
        const json& target = advice["remove"];
        p.id = target["id"].str();
        if (p.id.empty())
        {
            p.message = "rien à retirer cette semaine";
            return p;
        }
        // Un seul retrait par semaine : une mesure relancée dans la semaine ne retire pas un deuxième modèle.
        const json* recent = nullptr;
        for (const json& x : removed.elements())
        {
            const std::string date = x["date"].str().empty() ? "1970-01-01 00:00" : x["date"].str();
            if (std::difftime(now, parse_date(date)) < 6 * 86400) recent = &x;
        }
        if (recent)
        {
            p.what = retrait_plan::action::this_week;
            p.message = "déjà un retrait cette semaine (" + (*recent)["name"].str() + ", le " + (*recent)["date"].str() +
                ") : rien de plus";
            return p;
        }
        const json* model = nullptr;
        for (const json& m : models["models"].elements())
            if (m["id"].str() == p.id)
            {
                model = &m;
                break;
            }
        if (!model || !(*model)["installed"].as_bool())
        {
            p.what = retrait_plan::action::absent;
            p.message = p.id + " : déjà absent de Gulliver";
            return p;
        }
        for (const auto& kv : models["active"].items())
            if (kv.second.is_string() && kv.second.as_string() == p.id)
            {
                p.what = retrait_plan::action::active;
                p.message = p.id + " : actif, laissé";
                return p;
            }
        const json& m = *model;
        json e = json::object();
        e["date"] = removal_date(now);
        e["id"] = p.id;
        e["name"] = first_of({target["name"].str(), m["name"].str(), p.id});
        e["origin"] = first_of({m["origin"].str(), "catalog"});
        e["quiz"] = target["quiz"];
        e["write_512"] = target["write_512"];
        e["why"] = target["unread"].as_bool() ? "le moteur ne le lit pas" : "le dernier du classement";
        if (e["origin"].str() == "huggingface")
        {
            const std::string url = m["files"][0]["url"].str();
            std::string source = m["source"].str();
            const std::string hub = "https://huggingface.co/";
            for (size_t at; (at = source.find(hub)) != std::string::npos;) source.erase(at, hub.size());
            e["repo"] = source;
            std::string file = base_name(m["file"].str());
            const size_t resolve = url.find("/resolve/");
            if (resolve != std::string::npos)
            {
                const std::string after = url.substr(resolve + 9);
                const size_t slash = after.find('/');
                file = slash == std::string::npos ? std::string() : after.substr(slash + 1);
            }
            e["file"] = file;
            e["projector"] = m["projector"]["name"].str();
        }
        if (e["quiz"].is_number())
        {
            // « {quiz:.0f} » de Python : arrondi au pair, comme le conseil.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.0f", std::nearbyint(e["quiz"].as_double()));
            p.note = std::string("épreuve ") + buf + "/100";
        }
        else p.note = e["why"].str();
        p.entry = e;
        p.what = retrait_plan::action::remove;
        return p;
    }
}
