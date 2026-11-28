// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Cœur de conseil_modeles.py porté en C++ : à partir des lignes brutes du banc, il
// désigne les modèles conseillés et le modèle à retirer, et rend l'objet conseil.json.
// La logique (sélection des rôles, tri « noted », retrait) suit l'outil d'origine à
// l'identique, pour qu'un test en or reproduise sa sortie sur les mêmes données.

#include "conseil_core.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gulliver
{

    namespace
    {

        const int MIN_BILLIONS = 3;

        std::vector<std::string> split(const std::string& s, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
            out.push_back(cur);
            return out;
        }

        std::optional<double> to_double(const std::string& s)
        {
            if (s.empty()) return std::nullopt;
            try { size_t pos = 0; double v = std::stod(s, &pos);
                  while (pos < s.size() && (s[pos] == ' ')) ++pos;
                  if (pos != s.size()) return std::nullopt; return v; }
            catch (...) { return std::nullopt; }
        }

        bool icontains(const std::string& hay, const std::string& needle)
        {
            auto low = [](std::string x) { for (char& c : x) if (c >= 'A' && c <= 'Z') c += 32; return x; };
            return low(hay).find(low(needle)) != std::string::npos;
        }

        bool abliterated(const std::string& name)
        {
            return icontains(name, "abliterat") || icontains(name, "uncensored")
                || icontains(name, "heretic")  || icontains(name, "dolphin");
        }

        bool comparable(const std::string& name)
        {
            return name.rfind("Qwen3-", 0) == 0 || name.rfind("Qwen3 ", 0) == 0;
        }

        bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        // Le nombre de paramètres en milliards, lu dans le nom (« 8B », « 30B A3B » → 30),
        // sans std::regex : un nombre non précédé de [\w.], suivi de 'B' et d'une frontière.
        std::optional<double> billions(const std::string& name)
        {
            for (size_t i = 0; i < name.size(); ++i)
            {
                const char c = name[i];
                if (c < '0' || c > '9') continue;
                if (i > 0 && (is_word(name[i - 1]) || name[i - 1] == '.')) continue;  // (?<![\w.])
                size_t j = i;
                while (j < name.size() && name[j] >= '0' && name[j] <= '9') ++j;
                if (j < name.size() && name[j] == '.')
                {
                    size_t k = j + 1;
                    while (k < name.size() && name[k] >= '0' && name[k] <= '9') ++k;
                    if (k > j + 1) j = k;
                }
                if (j < name.size() && name[j] == 'B'
                    && (j + 1 == name.size() || !is_word(name[j + 1])))            // B\b
                    return to_double(name.substr(i, j - i));
            }
            return std::nullopt;
        }

    }  // namespace

    json build_conseil(const std::string& raw_text, const conseil_options& opt,
                       const std::string& now)
    {
        std::vector<std::string> model_order;
        std::set<std::string> seen;
        std::map<std::string, std::string> names;
        std::map<std::string, bool> image;
        std::map<std::string, std::string> where;
        std::map<std::string, double> ppl, quiz;
        // (id, contexte) -> kv -> liste de (lecture, écriture) en texte.
        std::map<std::pair<std::string, std::string>,
                 std::map<std::string, std::vector<std::pair<std::string, std::string>>>> runs;

        std::istringstream in(raw_text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::vector<std::string> p = split(line, '|');
            if (p.size() == 3 && (p[0] == "ppl" || p[0] == "quiz"))
            {
                if (auto v = to_double(p[2])) (p[0] == "ppl" ? ppl : quiz)[p[1]] = *v;
                continue;
            }
            if (p.size() != 8) continue;
            const std::string& mid = p[0]; const std::string& name = p[1];
            const std::string& img = p[2]; const std::string& n = p[3];
            const std::string& kv = p[4]; const std::string& r = p[5];
            const std::string& w = p[6]; const std::string& calc = p[7];
            if (!seen.count(mid)) { seen.insert(mid); model_order.push_back(mid); }
            names[mid] = name; image[mid] = (img == "1");
            // Le pire de ses essais : ne lit pas > processeur > GPU.
            auto rank = [](const std::string& x) { return x == "nonlu" ? 2 : x == "processeur" ? 1 : 0; };
            const std::string cur = where.count(mid) ? where[mid] : "gpu";
            if (rank(calc) >= rank(cur)) where[mid] = calc;
            runs[{mid, n}][kv].push_back({r, w});
        }
        if (model_order.empty()) return json::object();

        auto get = [&](const std::string& mid, const std::string& n,
                       const std::string& kv, int i) -> std::optional<double> {
            auto it = runs.find({mid, n});
            if (it == runs.end()) return std::nullopt;
            auto kit = it->second.find(kv);
            if (kit == it->second.end()) return std::nullopt;
            double sum = 0; int count = 0;
            for (const auto& pr : kit->second)
                if (auto v = to_double(i == 0 ? pr.first : pr.second)) { sum += *v; ++count; }
            if (!count) return std::nullopt;
            return sum / count;
        };
        auto get1 = [&](const std::string& mid, const std::string& n) { return get(mid, n, "q8", 1); };

        auto kv_type = [&](const std::string& mid) -> std::string {
            std::vector<double> dw, dr;
            for (const char* n : { "512", "4096" })
            {
                auto a = get(mid, n, "q8", 1), b = get(mid, n, "f16", 1);
                auto ra = get(mid, n, "q8", 0), rb = get(mid, n, "f16", 0);
                if (a && b && *a != 0) dw.push_back(*b / *a - 1);
                if (ra && rb && *ra != 0) dr.push_back(*rb / *ra - 1);
            }
            auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s / v.size(); };
            const bool fast = !dw.empty() && mean(dw) >= 0.03 && (dr.empty() || mean(dr) >= 0);
            return fast ? "f16" : "q8";
        };

        auto engine = [](const std::string& m) { return m.find(':') != std::string::npos; };
        auto truthy = [](std::optional<double> v) { return v && *v != 0.0; };

        std::map<std::string, double> write;
        for (const std::string& m : model_order)
            if (auto v = get1(m, "512"); truthy(v)) write[m] = *v;

        std::vector<std::string> usable;
        for (const std::string& m : model_order)
            if (where[m] != "nonlu" && truthy(get1(m, "512")) && !engine(m)) usable.push_back(m);

        const std::string previous = names.count(opt.previous) ? opt.previous : "";

        // noted = (quiz sinon -1 ; puis write) ; comparaisons « max/min » stables (1er).
        auto noted_of = [&](const std::string& m) {
            return std::make_pair(quiz.count(m) ? quiz[m] : -1.0, write[m]);
        };
        auto argmax_noted = [&](const std::vector<std::string>& v) {
            const std::string* best = nullptr; std::pair<double, double> bk;
            for (const std::string& m : v) { auto k = noted_of(m);
                if (!best || k > bk) { best = &m; bk = k; } }
            return best ? *best : std::string();
        };
        auto argmin_noted = [&](const std::vector<std::string>& v) {
            const std::string* best = nullptr; std::pair<double, double> bk;
            for (const std::string& m : v) { auto k = noted_of(m);
                if (!best || k < bk) { best = &m; bk = k; } }
            return best ? *best : std::string();
        };
        auto argmax_write = [&](const std::vector<std::string>& v) {
            const std::string* best = nullptr; double bk = 0;
            for (const std::string& m : v) { double k = write[m];
                if (!best || k > bk) { best = &m; bk = k; } }
            return best ? *best : std::string();
        };
        auto argmin_ppl = [&](const std::vector<std::string>& v) {
            const std::string* best = nullptr; double bk = 0;
            for (const std::string& m : v) { double k = ppl[m];
                if (!best || k < bk) { best = &m; bk = k; } }
            return best ? *best : std::string();
        };

        // cyber mis à part ; s'il ne reste qu'eux, on les garde comme usable.
        std::vector<std::string> cyber_all, rest;
        for (const std::string& m : usable) (abliterated(names[m]) ? cyber_all : rest).push_back(m);
        if (!rest.empty()) usable = rest;

        std::vector<std::string> big;
        for (const std::string& m : usable)
            if (billions(names[m]).value_or(MIN_BILLIONS) >= MIN_BILLIONS) big.push_back(m);
        const std::string fastest = big.empty() ? std::string() : argmax_write(big);

        std::vector<std::string> family;
        for (const std::string& m : usable)
            if (comparable(names[m]) && ppl.count(m)) family.push_back(m);

        std::vector<std::string> fast_gpu;
        for (const std::string& m : usable)
            if (write[m] >= 40 && where[m] == "gpu") fast_gpu.push_back(m);

        bool by_quiz = false;
        for (const std::string& m : usable) if (quiz.count(m)) { by_quiz = true; break; }

        std::string accurate, general;
        if (by_quiz)
        {
            std::vector<std::string> noted_usable;
            for (const std::string& m : usable) if (quiz.count(m)) noted_usable.push_back(m);
            accurate = argmax_noted(noted_usable);
            std::vector<std::string> fast_noted;
            for (const std::string& m : fast_gpu) if (quiz.count(m)) fast_noted.push_back(m);
            general = !fast_noted.empty() ? argmax_noted(fast_noted)
                    : !fast_gpu.empty()   ? argmax_write(fast_gpu)
                    : !fastest.empty()    ? fastest : argmax_write(usable);
        }
        else
        {
            accurate = family.empty() ? std::string() : argmin_ppl(family);
            std::vector<std::string> fast_family;
            for (const std::string& m : fast_gpu) if (ppl.count(m) && comparable(names[m])) fast_family.push_back(m);
            general = !fast_family.empty() ? argmin_ppl(fast_family)
                    : !fast_gpu.empty()    ? argmax_write(fast_gpu)
                    : !fastest.empty()     ? fastest : argmax_write(usable);
        }
        std::vector<std::string> cyber_noted;
        for (const std::string& m : cyber_all) if (quiz.count(m)) cyber_noted.push_back(m);
        const std::string cyber = cyber_noted.empty() ? std::string() : argmax_noted(cyber_noted);

        auto num_or_null = [](std::optional<double> v) { return v ? json(*v) : json(nullptr); };
        auto entry = [&](const std::string& role, const std::string& m, const std::string& why) {
            json e = json::object();
            e["role"] = json(role); e["id"] = json(m); e["name"] = json(names[m]); e["why"] = json(why);
            e["write_512"] = write.count(m) ? json(write[m]) : json(nullptr);
            e["read_4096"] = num_or_null(get(m, "4096", "q8", 0));
            e["perplexity"] = ppl.count(m) ? json(ppl[m]) : json(nullptr);
            e["quiz"] = quiz.count(m) ? json(quiz[m]) : json(nullptr);
            e["image"] = json(image.count(m) ? image[m] : false);
            json s = json::object();
            s["kv_type"] = json(kv_type(m)); s["draft_tokens"] = json(static_cast<long long>(7));
            e["settings"] = s;
            return e;
        };

        json advice = json::array();
        advice.push_back(entry("general", general, std::string("Pour tout faire : ") + (by_quiz
            ? "la meilleure note à l'épreuve des modèles qui écrivent au moins 40 jetons/s sur le GPU."
            : "le plus juste des modèles qui écrivent au moins 40 jetons/s sur le GPU.")));
        if (!accurate.empty() && accurate != general)
            advice.push_back(entry("accurate", accurate, std::string("Les réponses les plus justes : ") + (by_quiz
                ? "la meilleure note à l'épreuve de 40 questions, toutes familles confondues."
                : "la perplexité la plus basse de la famille Qwen3, la seule dont les perplexités se comparent.")));
        if (!fastest.empty() && fastest != general && fastest != accurate)
            advice.push_back(entry("fastest", fastest,
                "Le plus rapide : la meilleure écriture après 512 tokens, parmi les modèles d'au moins "
                "3 milliards de paramètres (les plus petits répètent la question au lieu d'y répondre)."));
        if (!cyber.empty())
            advice.push_back(entry("cyber", cyber,
                "Pour la cybersécurité : la meilleure note à l'épreuve des modèles « abliterated » "
                "(sans réflexe de refus), à n'utiliser que pour un usage autorisé."));

        // Le dernier du classement, à retirer.
        std::set<std::string> keep = opt.keep;
        for (size_t i = 0; i < advice.size(); ++i) keep.insert(advice[i]["id"].as_string());
        if (!previous.empty()) keep.insert(previous);
        std::vector<std::string> gulliver, unread, graded;
        for (const std::string& m : model_order)
            if (!engine(m) && !keep.count(m)) gulliver.push_back(m);
        for (const std::string& m : gulliver) if (where[m] == "nonlu") unread.push_back(m);
        for (const std::string& m : gulliver) if (quiz.count(m) && write.count(m)) graded.push_back(m);
        std::string remove;
        if (!unread.empty()) remove = unread[0];
        else if (graded.size() > 1) remove = argmin_noted(graded);

        json out = json::object();
        out["date"] = json(now);
        out["previous"] = previous.empty() ? json(nullptr) : json(previous);
        json fresh = json::array();
        {
            std::vector<std::string> f;
            for (const std::string& m : opt.fresh) if (names.count(m)) f.push_back(m);
            std::sort(f.begin(), f.end());
            for (const std::string& m : f) fresh.push_back(json(m));
        }
        out["new"] = fresh;
        out["late"] = json(opt.late);
        out["advice"] = advice;
        if (remove.empty()) out["remove"] = json(nullptr);
        else
        {
            json r = json::object();
            r["id"] = json(remove); r["name"] = json(names[remove]);
            r["quiz"] = quiz.count(remove) ? json(quiz[remove]) : json(nullptr);
            r["write_512"] = write.count(remove) ? json(write[remove]) : json(nullptr);
            r["unread"] = json(where[remove] == "nonlu");
            out["remove"] = r;
        }
        json removed = json::array();
        for (const std::string& m : opt.removed) removed.push_back(json(m));
        out["removed"] = removed;
        return out;
    }

}  // namespace gulliver
