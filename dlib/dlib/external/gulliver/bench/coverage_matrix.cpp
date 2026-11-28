// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Le constructeur de couverture, porté depuis server/coverage.py (build_coverage).  Les
// lacunes sont le point : ce que rien n'exerce borne honnêtement la portée de la note.

#include "coverage_matrix.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gulliver
{

    const char* const framework_keys[4] = { "atlas", "owasp_llm", "atos_blocks", "atos_build" };

    namespace
    {

        // Le catalogue d'un référentiel : "techniques" sinon "items", sinon vide.
        const json& catalogue_of(const json& block)
        {
            if (block["techniques"].is_object()) return block["techniques"];
            return block["items"];   // objet, ou null partagé si absent
        }

        // Découpe un identifiant sur '.' et '-' en parties ; une partie tout en chiffres
        // est comparée comme un nombre.  Reproduit la clé de tri de l'outil d'origine.
        struct part { bool is_num; long long num; std::string str; };

        std::vector<part> split_id(const std::string& id)
        {
            std::vector<part> parts;
            std::string cur;
            auto flush = [&]() {
                part p;
                p.is_num = !cur.empty();
                for (char c : cur) if (c < '0' || c > '9') { p.is_num = false; break; }
                if (p.is_num) { p.num = std::stoll(cur); p.str = cur; }
                else { p.num = 0; p.str = cur; }
                parts.push_back(p);
                cur.clear();
            };
            for (char c : id) { if (c == '.' || c == '-') flush(); else cur.push_back(c); }
            flush();
            return parts;
        }

        bool id_less(const std::string& a, const std::string& b)
        {
            const std::vector<part> pa = split_id(a), pb = split_id(b);
            const size_t n = std::min(pa.size(), pb.size());
            for (size_t i = 0; i < n; ++i)
            {
                const part& x = pa[i];
                const part& y = pb[i];
                if (x.is_num && y.is_num) { if (x.num != y.num) return x.num < y.num; }
                else if (x.is_num != y.is_num) return x.is_num;   // nombre avant texte
                else if (x.str != y.str) return x.str < y.str;
            }
            return pa.size() < pb.size();
        }

        void sort_entries(json& arr)
        {
            std::vector<json> v;
            for (size_t i = 0; i < arr.size(); ++i) v.push_back(arr[i]);
            std::sort(v.begin(), v.end(), [](const json& a, const json& b) {
                return id_less(a["id"].as_string(), b["id"].as_string());
            });
            json out = json::array();
            for (auto& e : v) out.push_back(std::move(e));
            arr = out;
        }

        double round3(double x) { return std::floor(x * 1000.0 + 0.5) / 1000.0; }

    }  // namespace

    json build_coverage(const std::vector<json>& packs,
                        const std::vector<json>& tools,
                        const json& frameworks)
    {
        // sources[key][item] = liste de {kind, name}, dans l'ordre sondes puis outils.
        std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, std::string>>>> sources;
        for (const char* k : framework_keys) sources[k];

        auto record = [&](const std::string& key, const std::string& item,
                          const std::string& kind, const std::string& name) {
            sources[key][item].push_back({ kind, name });
        };

        for (const json& pack : packs)
        {
            const json& probes = pack["probes"];
            for (size_t i = 0; i < probes.size(); ++i)
            {
                const json& probe = probes[i];
                const json& cov = probe["coverage"];
                const std::string id = probe["id"].is_string() ? probe["id"].as_string() : "?";
                for (const char* key : framework_keys)
                {
                    const json& items = cov[key];
                    for (size_t j = 0; j < items.size(); ++j)
                        record(key, items[j].as_string(), "probe", id);
                }
            }
        }
        for (const json& tool : tools)
        {
            const json& cov = tool["coverage"];
            const std::string name = tool["name"].is_string() ? tool["name"].as_string() : "?";
            for (const char* key : framework_keys)
            {
                const json& items = cov[key];
                for (size_t j = 0; j < items.size(); ++j)
                    record(key, items[j].as_string(), "tool", name);
            }
        }

        json result = json::object();
        json fw_out = json::object();
        json summary = json::object();

        for (const char* key : framework_keys)
        {
            const json& block = frameworks[key];
            const json& catalogue = catalogue_of(block);

            const bool has_testable = block["probe_testable"].is_array();
            std::vector<std::string> testable;
            if (has_testable)
            {
                const json& t = block["probe_testable"];
                for (size_t i = 0; i < t.size(); ++i) testable.push_back(t[i].as_string());
            }
            auto is_testable = [&](const std::string& id) {
                for (const auto& t : testable) if (t == id) return true;
                return false;
            };
            const json& notes = block["out_of_probe_scope_note"];

            json covered = json::array(), gaps = json::array(), out_of_scope = json::array();

            // Parcourt le catalogue dans l'ordre, comme le dictionnaire Python.
            const auto& members = catalogue.items();
            for (const auto& kv : members)
            {
                const std::string& item = kv.first;
                const std::string label = kv.second.as_string();
                const auto& hits = sources[key].count(item) ? sources[key][item]
                                   : std::vector<std::pair<std::string, std::string>>();

                json entry = json::object();
                entry["id"] = json(item);
                entry["label"] = json(label);
                json covered_by = json::array();
                for (const auto& h : hits)
                {
                    json src = json::object();
                    src["kind"] = json(h.first);
                    src["name"] = json(h.second);
                    covered_by.push_back(src);
                }
                entry["covered_by"] = covered_by;
                entry["count"] = json(static_cast<long long>(hits.size()));

                if (has_testable && !is_testable(item) && hits.empty())
                {
                    entry["reason"] = notes[item].is_string()
                        ? json(notes[item].as_string())
                        : json(std::string("not observable from a model completion"));
                    out_of_scope.push_back(entry);
                }
                else if (!hits.empty()) covered.push_back(entry);
                else gaps.push_back(entry);
            }

            // Items déclarés par une sonde ou un outil mais absents du catalogue.
            json unknown = json::array();
            for (const auto& kv : sources[key])
            {
                bool in_cat = false;
                for (const auto& m : members) if (m.first == kv.first) { in_cat = true; break; }
                if (!in_cat) unknown.push_back(json(kv.first));
            }

            sort_entries(covered);
            sort_entries(gaps);
            sort_entries(out_of_scope);

            json fwb = json::object();
            fwb["name"] = block["name"].is_string() ? json(block["name"].as_string()) : json(std::string(key));
            fwb["url"]  = block["url"].is_string() ? json(block["url"].as_string()) : json(std::string());
            fwb["note"] = block["note"].is_string() ? json(block["note"].as_string()) : json(std::string());
            fwb["gap_notes"] = block["gap_notes"].is_object() ? block["gap_notes"] : json::object();
            fwb["covered"] = covered;
            fwb["gaps"] = gaps;
            fwb["out_of_scope"] = out_of_scope;
            fwb["unknown_declared"] = unknown;
            fw_out[key] = fwb;

            const int n_covered = static_cast<int>(covered.size());
            const int n_gaps = static_cast<int>(gaps.size());
            const int in_scope = n_covered + n_gaps;
            json s = json::object();
            s["total"] = json(static_cast<long long>(catalogue.is_object() ? catalogue.size() : 0));
            s["in_scope"] = json(static_cast<long long>(in_scope));
            s["covered"] = json(static_cast<long long>(n_covered));
            s["gaps"] = json(static_cast<long long>(n_gaps));
            s["out_of_scope"] = json(static_cast<long long>(out_of_scope.size()));
            s["ratio"] = json(in_scope ? round3(static_cast<double>(n_covered) / in_scope) : 1.0);
            summary[key] = s;
        }

        result["frameworks"] = fw_out;
        result["summary"] = summary;
        return result;
    }

    std::vector<double> coverage_ratios(const json& coverage)
    {
        std::vector<double> ratios;
        const json& summary = coverage["summary"];
        for (const char* key : framework_keys)
            if (summary[key].is_object())
                ratios.push_back(summary[key]["ratio"].as_double(0.0));
        return ratios;
    }

}  // namespace gulliver
