// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// L'enregistrement par modèle et la comparaison entre modèles.  Tout est pur : à partir
// des verdicts de sondes et de la note déjà calculée, on produit des documents JSON.
// Le taux de passage exclut les sondes « recorded » (gardées pour comparaison, sans
// verdict) et se calcule par catégorie, comme dans l'outil d'origine.

#include "model_compare.h"

#include <cmath>
#include <string>
#include <vector>

namespace gulliver
{

    namespace
    {

        int js_round(double x) { return static_cast<int>(std::floor(x + 0.5)); }

        // Le taux de passage d'un ensemble de sondes : les « recorded » ne sont pas
        // jugées, donc hors du dénominateur.  pct vaut -1 quand rien n'est jugé.
        struct rate { int judged = 0; int passed = 0; int pct = -1; };

        rate pass_rate(const std::vector<const probe_outcome*>& set)
        {
            rate r;
            for (const probe_outcome* p : set)
            {
                if (p->outcome == "recorded") continue;
                ++r.judged;
                if (p->outcome == "pass") ++r.passed;
            }
            if (r.judged) r.pct = js_round(static_cast<double>(r.passed) / r.judged * 100.0);
            return r;
        }

        // Les catégories présentes dans un lot de sondes, dans l'ordre de première vue.
        std::vector<std::string> categories_in(const std::vector<probe_outcome>& probes)
        {
            std::vector<std::string> cats;
            for (const auto& p : probes)
            {
                bool seen = false;
                for (const auto& c : cats) if (c == p.category) { seen = true; break; }
                if (!seen) cats.push_back(p.category);
            }
            return cats;
        }

        json rate_json(const rate& r)
        {
            json o = json::object();
            o["passed"] = json(static_cast<long long>(r.passed));
            o["judged"] = json(static_cast<long long>(r.judged));
            if (r.pct < 0) o["pct"] = json(nullptr);        // aucune sonde jugée
            else           o["pct"] = json(static_cast<long long>(r.pct));
            return o;
        }

    }  // namespace

    json model_to_json(const model_report& report)
    {
        json out = json::object();
        out["model"] = json(report.model);
        out["grade"] = report.grade.to_json();

        // Taux de passage par catégorie.
        json cats = json::object();
        for (const auto& cat : categories_in(report.probes))
        {
            std::vector<const probe_outcome*> set;
            for (const auto& p : report.probes) if (p.category == cat) set.push_back(&p);
            cats[cat] = rate_json(pass_rate(set));
        }
        out["categories"] = cats;
        return out;
    }

    json compare_models(const std::vector<model_report>& reports)
    {
        json out = json::object();

        // Notes côte à côte.
        json models = json::array();
        for (const auto& r : reports)
        {
            json m = json::object();
            m["model"] = json(r.model);
            m["level"] = json(static_cast<long long>(r.grade.level));
            m["score_100"] = json(static_cast<long long>(r.grade.score_100()));
            m["band"] = json(r.grade.band);
            json lim = json::array();
            for (const auto& k : r.grade.limited_by) lim.push_back(json(k));
            m["limited_by"] = lim;
            models.push_back(m);
        }
        out["models"] = models;

        // Plafonds côte à côte : pour chaque plafond, la valeur de chaque modèle.
        const char* cap_names[] = { "findings", "coverage", "evidence", "campaigns" };
        json caps = json::object();
        for (const char* name : cap_names)
        {
            json per_model = json::object();
            for (const auto& r : reports)
            {
                const std::string n = name;
                const int v = n == "findings" ? r.grade.cap_findings
                            : n == "coverage" ? r.grade.cap_coverage
                            : n == "evidence" ? r.grade.cap_evidence
                            :                   r.grade.cap_campaigns;
                per_model[r.model] = json(static_cast<long long>(v));
            }
            caps[name] = per_model;
        }
        out["caps"] = caps;

        // Taux par catégorie : union des catégories, puis le taux de chaque modèle.
        std::vector<std::string> all_cats;
        for (const auto& r : reports)
            for (const auto& cat : categories_in(r.probes))
            {
                bool seen = false;
                for (const auto& c : all_cats) if (c == cat) { seen = true; break; }
                if (!seen) all_cats.push_back(cat);
            }
        json categories = json::object();
        for (const auto& cat : all_cats)
        {
            json per_model = json::object();
            for (const auto& r : reports)
            {
                std::vector<const probe_outcome*> set;
                for (const auto& p : r.probes) if (p.category == cat) set.push_back(&p);
                const rate rt = pass_rate(set);
                per_model[r.model] = (rt.pct < 0) ? json(nullptr)
                                                  : json(static_cast<long long>(rt.pct));
            }
            categories[cat] = per_model;
        }
        out["categories"] = categories;

        // Sondes divergentes : union des identifiants, et pour chacun le verdict de
        // chaque modèle qui l'a passée.  On ne garde que celles dont les verdicts
        // diffèrent entre modèles — c'est là que se joue le choix.
        std::vector<std::string> all_ids;
        for (const auto& r : reports)
            for (const auto& p : r.probes)
            {
                bool seen = false;
                for (const auto& id : all_ids) if (id == p.id) { seen = true; break; }
                if (!seen) all_ids.push_back(p.id);
            }

        json divergent = json::array();
        for (const auto& id : all_ids)
        {
            json outcomes = json::object();
            std::string category, severity, first_outcome;
            bool diverges = false, first = true;
            for (const auto& r : reports)
            {
                const probe_outcome* found = nullptr;
                for (const auto& p : r.probes) if (p.id == id) { found = &p; break; }
                if (!found) continue;
                if (category.empty()) { category = found->category; severity = found->severity; }
                outcomes[r.model] = json(found->outcome);
                if (first) { first_outcome = found->outcome; first = false; }
                else if (found->outcome != first_outcome) diverges = true;
            }
            if (!diverges) continue;
            json d = json::object();
            d["id"] = json(id);
            d["category"] = json(category);
            d["severity"] = json(severity);
            d["outcomes"] = outcomes;
            divergent.push_back(d);
        }
        out["divergent_probes"] = divergent;

        return out;
    }

}  // namespace gulliver
