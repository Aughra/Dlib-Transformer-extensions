// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Cœur de conseil_modeles.py porté en C++ : à partir des lignes brutes du banc, il
// désigne les modèles conseillés et le modèle à retirer, et rend soit l'objet
// conseil.json, soit le Markdown de la page de la semaine.  Le parse et la sélection
// sont partagés (compute) ; chaque sortie a son émetteur.  La logique suit l'outil
// d'origine à l'identique, pour qu'un test en or reproduise ses sorties.

#include "conseil_core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gulliver
{

    namespace
    {

        const int MIN_BILLIONS = 3;

        std::vector<std::string> split(const std::string& s, char sep)
        {
            std::vector<std::string> out; std::string cur;
            for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur.push_back(c); }
            out.push_back(cur); return out;
        }

        std::optional<double> to_double(const std::string& s)
        {
            if (s.empty()) return std::nullopt;
            try { size_t pos = 0; double v = std::stod(s, &pos);
                  while (pos < s.size() && s[pos] == ' ') ++pos;
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

        std::optional<double> billions(const std::string& name)
        {
            for (size_t i = 0; i < name.size(); ++i)
            {
                if (name[i] < '0' || name[i] > '9') continue;
                if (i > 0 && (is_word(name[i - 1]) || name[i - 1] == '.')) continue;
                size_t j = i;
                while (j < name.size() && name[j] >= '0' && name[j] <= '9') ++j;
                if (j < name.size() && name[j] == '.')
                {
                    size_t k = j + 1;
                    while (k < name.size() && name[k] >= '0' && name[k] <= '9') ++k;
                    if (k > j + 1) j = k;
                }
                if (j < name.size() && name[j] == 'B' && (j + 1 == name.size() || !is_word(name[j + 1])))
                    return to_double(name.substr(i, j - i));
            }
            return std::nullopt;
        }

        // Toutes les données du banc, plus le résultat de la sélection.
        struct cdata
        {
            std::vector<std::string> order;                          // ordre de première vue
            std::map<std::string, std::string> names, where;
            std::map<std::string, bool> image;
            std::map<std::string, double> ppl, quiz, write;
            std::map<std::pair<std::string, std::string>,
                     std::map<std::string, std::vector<std::pair<std::string, std::string>>>> runs;
            std::string previous;
            bool by_quiz = false;
            struct adv { std::string role, id, why; };
            std::vector<adv> advice;
            std::string remove;

            std::optional<double> get(const std::string& mid, const std::string& n,
                                      const std::string& kv, int i) const
            {
                auto it = runs.find({ mid, n });
                if (it == runs.end()) return std::nullopt;
                auto kit = it->second.find(kv);
                if (kit == it->second.end()) return std::nullopt;
                double sum = 0; int count = 0;
                for (const auto& pr : kit->second)
                    if (auto v = to_double(i == 0 ? pr.first : pr.second)) { sum += *v; ++count; }
                if (!count) return std::nullopt;
                return sum / count;
            }
            bool engine(const std::string& m) const { return m.find(':') != std::string::npos; }
            std::string kv_type(const std::string& mid) const
            {
                std::vector<double> dw, dr;
                for (const char* n : { "512", "4096" })
                {
                    auto a = get(mid, n, "q8", 1), b = get(mid, n, "f16", 1);
                    auto ra = get(mid, n, "q8", 0), rb = get(mid, n, "f16", 0);
                    if (a && b && *a != 0) dw.push_back(*b / *a - 1);
                    if (ra && rb && *ra != 0) dr.push_back(*rb / *ra - 1);
                }
                auto mean = [](const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s / v.size(); };
                return (!dw.empty() && mean(dw) >= 0.03 && (dr.empty() || mean(dr) >= 0)) ? "f16" : "q8";
            }
        };

        cdata compute(const std::string& raw_text, const conseil_options& opt)
        {
            cdata d;
            std::set<std::string> seen;
            std::istringstream in(raw_text);
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::vector<std::string> p = split(line, '|');
                if (p.size() == 3 && (p[0] == "ppl" || p[0] == "quiz"))
                {
                    if (auto v = to_double(p[2])) (p[0] == "ppl" ? d.ppl : d.quiz)[p[1]] = *v;
                    continue;
                }
                if (p.size() != 8) continue;
                const std::string& mid = p[0];
                if (!seen.count(mid)) { seen.insert(mid); d.order.push_back(mid); }
                d.names[mid] = p[1]; d.image[mid] = (p[2] == "1");
                auto rank = [](const std::string& x) { return x == "nonlu" ? 2 : x == "processeur" ? 1 : 0; };
                const std::string cur = d.where.count(mid) ? d.where[mid] : "gpu";
                if (rank(p[7]) >= rank(cur)) d.where[mid] = p[7];
                d.runs[{ mid, p[3] }][p[4]].push_back({ p[5], p[6] });
            }
            if (d.order.empty()) return d;

            auto truthy = [](std::optional<double> v) { return v && *v != 0.0; };
            for (const std::string& m : d.order)
                if (auto v = d.get(m, "512", "q8", 1); truthy(v)) d.write[m] = *v;

            std::vector<std::string> usable;
            for (const std::string& m : d.order)
                if (d.where[m] != "nonlu" && truthy(d.get(m, "512", "q8", 1)) && !d.engine(m)) usable.push_back(m);

            d.previous = d.names.count(opt.previous) ? opt.previous : "";

            auto noted_of = [&](const std::string& m) {
                return std::make_pair(d.quiz.count(m) ? d.quiz[m] : -1.0, d.write[m]); };
            auto argmax_noted = [&](const std::vector<std::string>& v) {
                const std::string* best = nullptr; std::pair<double, double> bk;
                for (const std::string& m : v) { auto k = noted_of(m); if (!best || k > bk) { best = &m; bk = k; } }
                return best ? *best : std::string(); };
            auto argmin_noted = [&](const std::vector<std::string>& v) {
                const std::string* best = nullptr; std::pair<double, double> bk;
                for (const std::string& m : v) { auto k = noted_of(m); if (!best || k < bk) { best = &m; bk = k; } }
                return best ? *best : std::string(); };
            auto argmax_write = [&](const std::vector<std::string>& v) {
                const std::string* best = nullptr; double bk = 0;
                for (const std::string& m : v) { double k = d.write[m]; if (!best || k > bk) { best = &m; bk = k; } }
                return best ? *best : std::string(); };
            auto argmin_ppl = [&](const std::vector<std::string>& v) {
                const std::string* best = nullptr; double bk = 0;
                for (const std::string& m : v) { double k = d.ppl[m]; if (!best || k < bk) { best = &m; bk = k; } }
                return best ? *best : std::string(); };

            std::vector<std::string> cyber_all, rest;
            for (const std::string& m : usable) (abliterated(d.names[m]) ? cyber_all : rest).push_back(m);
            if (!rest.empty()) usable = rest;

            std::vector<std::string> big;
            for (const std::string& m : usable)
                if (billions(d.names[m]).value_or(MIN_BILLIONS) >= MIN_BILLIONS) big.push_back(m);
            const std::string fastest = big.empty() ? std::string() : argmax_write(big);

            std::vector<std::string> family;
            for (const std::string& m : usable)
                if (comparable(d.names[m]) && d.ppl.count(m)) family.push_back(m);
            std::vector<std::string> fast_gpu;
            for (const std::string& m : usable)
                if (d.write[m] >= 40 && d.where[m] == "gpu") fast_gpu.push_back(m);

            for (const std::string& m : usable) if (d.quiz.count(m)) { d.by_quiz = true; break; }

            std::string accurate, general;
            if (d.by_quiz)
            {
                std::vector<std::string> nu;
                for (const std::string& m : usable) if (d.quiz.count(m)) nu.push_back(m);
                accurate = argmax_noted(nu);
                std::vector<std::string> fn;
                for (const std::string& m : fast_gpu) if (d.quiz.count(m)) fn.push_back(m);
                general = !fn.empty() ? argmax_noted(fn) : !fast_gpu.empty() ? argmax_write(fast_gpu)
                        : !fastest.empty() ? fastest : argmax_write(usable);
            }
            else
            {
                accurate = family.empty() ? std::string() : argmin_ppl(family);
                std::vector<std::string> ff;
                for (const std::string& m : fast_gpu) if (d.ppl.count(m) && comparable(d.names[m])) ff.push_back(m);
                general = !ff.empty() ? argmin_ppl(ff) : !fast_gpu.empty() ? argmax_write(fast_gpu)
                        : !fastest.empty() ? fastest : argmax_write(usable);
            }
            std::vector<std::string> cn;
            for (const std::string& m : cyber_all) if (d.quiz.count(m)) cn.push_back(m);
            const std::string cyber = cn.empty() ? std::string() : argmax_noted(cn);

            const std::string why_general = std::string("Pour tout faire : ") + (d.by_quiz
                ? "la meilleure note à l'épreuve des modèles qui écrivent au moins 40 jetons/s sur le GPU."
                : "le plus juste des modèles qui écrivent au moins 40 jetons/s sur le GPU.");
            d.advice.push_back({ "general", general, why_general });
            if (!accurate.empty() && accurate != general)
                d.advice.push_back({ "accurate", accurate, std::string("Les réponses les plus justes : ") + (d.by_quiz
                    ? "la meilleure note à l'épreuve de 40 questions, toutes familles confondues."
                    : "la perplexité la plus basse de la famille Qwen3, la seule dont les perplexités se comparent.") });
            if (!fastest.empty() && fastest != general && fastest != accurate)
                d.advice.push_back({ "fastest", fastest,
                    "Le plus rapide : la meilleure écriture après 512 tokens, parmi les modèles d'au moins "
                    "3 milliards de paramètres (les plus petits répètent la question au lieu d'y répondre)." });
            if (!cyber.empty())
                d.advice.push_back({ "cyber", cyber,
                    "Pour la cybersécurité : la meilleure note à l'épreuve des modèles « abliterated » "
                    "(sans réflexe de refus), à n'utiliser que pour un usage autorisé." });

            std::set<std::string> keep = opt.keep;
            for (const auto& a : d.advice) keep.insert(a.id);
            if (!d.previous.empty()) keep.insert(d.previous);
            std::vector<std::string> gulliver, unread, graded;
            for (const std::string& m : d.order) if (!d.engine(m) && !keep.count(m)) gulliver.push_back(m);
            for (const std::string& m : gulliver) if (d.where[m] == "nonlu") unread.push_back(m);
            for (const std::string& m : gulliver) if (d.quiz.count(m) && d.write.count(m)) graded.push_back(m);
            if (!unread.empty()) d.remove = unread[0];
            else if (graded.size() > 1) d.remove = argmin_noted(graded);
            return d;
        }

        // Les nombres à la française : >=100 arrondi à l'entier avec espace des milliers,
        // sinon une décimale ; virgule décimale.  Reproduit fmt() de l'outil d'origine.
        // Arrondi au pair (« banker's »), comme le formatage %.0f de Python : 7200,5 → 7200,
        // 2956,5 → 2956.  std::llround arrondit le demi au-dessus et donnerait 7201.
        long long round_half_even(double v)
        {
            const double fl = std::floor(v);
            const double diff = v - fl;
            if (diff < 0.5) return static_cast<long long>(fl);
            if (diff > 0.5) return static_cast<long long>(fl) + 1;
            const long long f = static_cast<long long>(fl);
            return (f % 2 == 0) ? f : f + 1;
        }

        // Les milliers séparés par une espace : 131072 → « 131 072 ».
        std::string group_thousands(long long r)
        {
            std::string digits = std::to_string(r < 0 ? -r : r), s;
            int c = 0;
            for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i)
            {
                s.insert(s.begin(), digits[i]);
                if (++c % 3 == 0 && i > 0) s.insert(s.begin(), ' ');
            }
            return (r < 0 ? "-" : "") + s;
        }

        std::string fmt(std::optional<double> ov)
        {
            if (!ov) return "—";
            const double v = *ov;
            if (v >= 100) return group_thousands(round_half_even(v));
            char buf[64]; std::snprintf(buf, sizeof buf, "%.1f", v);
            std::string s = buf; for (char& ch : s) if (ch == '.') ch = ',';
            return s;
        }
        std::string two(std::optional<double> ov)   // {:.2f} virgule, sans groupement
        {
            if (!ov) return "—";
            char buf[64]; std::snprintf(buf, sizeof buf, "%.2f", *ov);
            std::string s = buf; for (char& ch : s) if (ch == '.') ch = ',';
            return s;
        }
        std::string quiz_text(const cdata& d, const std::string& m)
        {
            if (!d.quiz.count(m)) return "—";
            char buf[64]; std::snprintf(buf, sizeof buf, "%.0f", d.quiz.at(m));
            return std::string(buf) + "/100";
        }

        json num_or_null(std::optional<double> v) { return v ? json(*v) : json(nullptr); }

        // ---- section « comme un utilisateur » (programme bench_service) ----------------

        // Descend root à travers une suite de clés ; nombre trouvé, sinon rien.
        std::optional<double> walk_num(const json& root, const std::vector<std::string>& keys)
        {
            const json* cur = &root;
            for (const std::string& k : keys)
            {
                if (!cur->is_object() || !cur->contains(k)) return std::nullopt;
                cur = &(*cur)[k];
            }
            return cur->is_number() ? std::optional<double>(cur->as_double()) : std::nullopt;
        }
        // None → « — » ; digits<0 → fmt() à la française ; sinon N décimales à virgule ; + unité.
        std::string svc_f(std::optional<double> v, const std::string& unit, int digits)
        {
            if (!v) return "—";
            std::string s;
            if (digits < 0) s = fmt(v);
            else { char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", digits, *v);
                   s = buf; for (char& c : s) if (c == '.') c = ','; }
            return s + unit;
        }
        std::vector<std::pair<std::string, std::vector<std::string>>> service_rows(
            const json& service, const std::vector<std::string>& engines = { "gulliver", "llama" })
        {
            struct spec { const char* label; std::vector<std::string> keys; const char* unit; int digits; };
            const std::vector<spec> specs = {
                { "Chargement du modèle (jusqu'à une première réponse)", { "load_s" }, " s", 1 },
                { "Conversation de 32k : lecture", { "long_drafts", "read" }, " jetons/s", -1 },
                { "Conversation de 32k : écriture, avec brouillons", { "long_drafts", "write" }, " jetons/s", -1 },
                { "Conversation de 32k : écriture, sans brouillons", { "long_plain", "write" }, " jetons/s", -1 },
                { "4 requêtes à la fois : écriture totale", { "four", "total_write" }, " jetons/s", -1 },
                { "3 documents de 4 000 tokens à la fois : temps total", { "three_docs", "total_s" }, " s", 1 },
                { "3 documents à la fois : attente moyenne du premier mot", { "three_docs", "mean_first_token_s" }, " s", 1 },
            };
            std::vector<std::pair<std::string, std::vector<std::string>>> rows;
            for (const spec& sp : specs)
            {
                std::vector<std::string> vals;
                for (const std::string& e : engines)
                {
                    std::optional<double> v = walk_num(service[e], sp.keys);
                    // llama ne mesure pas « avec brouillons » : sa lecture vient de long_plain.
                    if (e == "llama" && sp.keys.size() == 2
                        && sp.keys[0] == "long_drafts" && sp.keys[1] == "read")
                        v = walk_num(service[e], { "long_plain", "read" });
                    vals.push_back(svc_f(v, sp.unit, sp.digits));
                }
                rows.push_back({ sp.label, vals });
            }
            std::vector<std::string> big;
            for (const std::string& e : engines)
            {
                const json& b = service[e]["largest"];
                std::string text = "—";
                if (b.is_object() && b["context"].is_number())
                {
                    text = group_thousands(b["context"].as_int()) + " tokens";
                    if (b["refused"].is_number())
                        text += " (" + group_thousands(b["refused"].as_int()) + " refusés)";
                }
                big.push_back(text);
            }
            rows.push_back({ "Le plus grand contexte qui tient (essayés : 65 536 et 131 072)", big });
            return rows;
        }

        // ---- section RAG (programme test_rag) -----------------------------------------

        std::string rag_label(const std::string& engine)
        {
            if (engine == "hybrid") return "Hybride (mots et sens)";
            if (engine == "dense")  return "Sens seul";
            if (engine == "lexical") return "Mots seuls";
            return engine;
        }
        // Le réglage qui trouve le plus dans les 12 premiers, puis le meilleur rang.
        std::string rag_best(const json& rag)
        {
            const json& res = rag["results"];
            std::string best; std::pair<double, double> bk;
            for (const auto& kv : res.items())
            {
                std::pair<double, double> k = { kv.second["rappel_12"].as_double(),
                                                kv.second["rang_reciproque"].as_double() };
                if (best.empty() || k > bk) { best = kv.first; bk = k; }
            }
            return best;
        }
        void rag_split(const std::string& key, std::string& engine, bool& rerank)
        {
            const size_t p = key.find(" + ");
            engine = p == std::string::npos ? key : key.substr(0, p);
            rerank = p != std::string::npos;
        }
        std::string rag_text(const json& rag)
        {
            std::string engine; bool rerank;
            rag_split(rag_best(rag), engine, rerank);
            auto istr = [](const json& v) { return v.is_number() ? std::to_string(v.as_int()) : std::string("?"); };
            const std::string idx = rag["index_s"].is_number()
                ? std::to_string(round_half_even(rag["index_s"].as_double())) : "0";
            return istr(rag["questions"]) + " questions figées sur les " + istr(rag["documents"])
                + " épisodes du blog, cherchées par un service d'essai isolé ("
                + (rag["embedder"].is_string() ? rag["embedder"].as_string() : "?")
                + ", épisodes indexés en " + idx + " s) ; une question est trouvée quand le passage qui y répond "
                  "est rendu. Le meilleur réglage : moteur « " + rag_label(engine) + " », tri fin "
                + (rerank ? "allumé" : "éteint") + ", 12 passages.";
        }
        std::vector<std::vector<std::string>> rag_rows(const json& rag)
        {
            const std::string best = rag_best(rag);
            auto pct = [](double v) { return std::to_string(round_half_even(100.0 * v)) + " %"; };
            std::vector<std::vector<std::string>> rows;
            for (const auto& kv : rag["results"].items())
            {
                std::string engine; bool rerank; rag_split(kv.first, engine, rerank);
                std::string label = rag_label(engine) + (rerank ? " + tri fin" : "")
                                   + (kv.first == best ? " (le meilleur)" : "");
                const json& r = kv.second;
                char rr[32]; std::snprintf(rr, sizeof rr, "%.2f", r["rang_reciproque"].as_double());
                std::string rrs = rr; for (char& c : rrs) if (c == '.') c = ',';
                rows.push_back({ label, pct(r["rappel_6"].as_double()), pct(r["rappel_12"].as_double()),
                                 pct(r["rappel_20"].as_double()), rrs });
            }
            return rows;
        }

    }  // namespace

    json build_conseil(const std::string& raw_text, const conseil_options& opt, const std::string& now)
    {
        const cdata d = compute(raw_text, opt);
        if (d.order.empty()) return json::object();

        auto entry = [&](const cdata::adv& a) {
            const std::string& m = a.id;
            json e = json::object();
            e["role"] = json(a.role); e["id"] = json(m); e["name"] = json(d.names.at(m)); e["why"] = json(a.why);
            e["write_512"] = d.write.count(m) ? json(d.write.at(m)) : json(nullptr);
            e["read_4096"] = num_or_null(d.get(m, "4096", "q8", 0));
            e["perplexity"] = d.ppl.count(m) ? json(d.ppl.at(m)) : json(nullptr);
            e["quiz"] = d.quiz.count(m) ? json(d.quiz.at(m)) : json(nullptr);
            e["image"] = json(d.image.count(m) ? d.image.at(m) : false);
            json s = json::object();
            s["kv_type"] = json(d.kv_type(m)); s["draft_tokens"] = json(static_cast<long long>(7));
            e["settings"] = s;
            // Note de cybersécurité (épreuve), si elle a été fournie pour ce modèle : additive,
            // absente quand opt.cyber est vide — la sortie reste alors celle de Python.
            if (opt.cyber.is_object() && opt.cyber.contains(m)) e["cyber"] = opt.cyber[m];
            return e;
        };

        json advice = json::array();
        for (const auto& a : d.advice) advice.push_back(entry(a));

        json out = json::object();
        out["date"] = json(now);
        out["previous"] = d.previous.empty() ? json(nullptr) : json(d.previous);
        json fresh = json::array();
        {
            std::vector<std::string> f;
            for (const std::string& m : opt.fresh) if (d.names.count(m)) f.push_back(m);
            std::sort(f.begin(), f.end());
            for (const std::string& m : f) fresh.push_back(json(m));
        }
        out["new"] = fresh;
        out["late"] = json(opt.late);
        out["advice"] = advice;
        if (d.remove.empty()) out["remove"] = json(nullptr);
        else
        {
            json r = json::object();
            r["id"] = json(d.remove); r["name"] = json(d.names.at(d.remove));
            r["quiz"] = d.quiz.count(d.remove) ? json(d.quiz.at(d.remove)) : json(nullptr);
            r["write_512"] = d.write.count(d.remove) ? json(d.write.at(d.remove)) : json(nullptr);
            r["unread"] = json(d.where.at(d.remove) == "nonlu");
            out["remove"] = r;
        }
        out["removed"] = opt.removed.is_array() ? opt.removed : json::array();
        return out;
    }

    std::string build_conseil_markdown(const std::string& raw_text, const conseil_options& opt,
                                       const std::string& now)
    {
        const cdata d = compute(raw_text, opt);
        std::ostringstream o;
        auto line = [&](const std::string& s = "") { o << s << "\n"; };

        line("### Les modèles de Gulliver — " + now);
        line();
        // Table « Machine » (si --machine) : les libellés en regard des valeurs, puis la
        // charge de la puce/processeur/GPU pendant la mesure (si --temperature à 10 champs).
        if (!opt.machine.empty())
        {
            const char* labels[] = { "Modèle de Mac", "Puce", "macOS", "Gulliver", "Charge",
                                     "Puce, sonde la plus chaude" };
            const std::vector<std::string> parts = split(opt.machine, '|');
            line("| Machine | |");
            line("|---|---|");
            for (size_t i = 0; i < parts.size() && i < 6; ++i)
                line("| " + std::string(labels[i]) + " | " + parts[i] + " |");
            const std::vector<std::string> load = split(opt.temperature, '|');
            if (load.size() == 10)
            {
                auto num = [](std::string v) { for (char& c : v) if (c == '.') c = ','; return v; };
                struct row { const char* label; const char* unit; int off; };
                const row rows[] = { { "Température de la puce", "°C", 0 },
                                     { "Utilisation du processeur", "%", 3 },
                                     { "Utilisation du GPU", "%", 6 } };
                for (const row& r : rows)
                    line("| " + std::string(r.label) + " | " + num(load[r.off + 1]) + " " + r.unit
                         + " en moyenne, de " + num(load[r.off]) + " à " + num(load[r.off + 2]) + " " + r.unit + " |");
            }
            line();
        }
        // L'intro (une seule ligne, comme le print unique de l'outil d'origine).
        std::string intro =
            "Gulliver sur chacun de ses modèles installés, et llama.cpp et MLX sur le modèle de référence : "
            "lecture d'une question puis écriture de 64 jetons après elle, en jetons par seconde, mémoire de "
            "conversation en 8 bits, " + opt.rounds + " essais en alternance (moyenne) ; perplexité d'un même "
            "texte français (plus basse, mieux le modèle prévoit le texte) ; épreuve : la note sur 100 de 30 "
            "questions en français (calcul, logique, connaissances, langue, consignes, compréhension, code). ";
        if (!d.previous.empty())
            intro += "Référence : **" + d.names.at(d.previous) + "**, le choix de la semaine dernière ; l'écart "
                     "est celui de l'écriture après 512 tokens (+ : plus rapide que lui).";
        else
            intro += "Pas encore de choix de la semaine dernière pour référence.";
        line(intro);

        std::vector<std::string> fresh_in;
        for (const std::string& m : d.order) if (opt.fresh.count(m)) fresh_in.push_back(m);
        if (!fresh_in.empty())
        {
            line();
            std::string s; for (size_t i = 0; i < fresh_in.size(); ++i) s += (i ? ", " : "") + d.names.at(fresh_in[i]);
            line("Nouveaux cette semaine (téléchargés la nuit précédente) : " + s + ".");
        }
        if (opt.late)
        {
            line();
            line("L'heure limite est passée avant la fin : les modèles manquants n'ont pas été mesurés cette semaine.");
        }
        line();
        line("| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Écart | Perplexité "
             "| Épreuve | Mémoire conseillée |");
        line("|---|---|---|---|---|---|---|---|---|---|");

        // Tri par écriture décroissante, stable (l'ordre de première vue départage).
        std::vector<std::string> order = d.order;
        std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
            double wa = d.write.count(a) ? d.write.at(a) : 0.0;
            double wb = d.write.count(b) ? d.write.at(b) : 0.0;
            return wa > wb;
        });
        const std::optional<double> base = d.previous.empty() ? std::nullopt
            : (d.write.count(d.previous) ? std::optional<double>(d.write.at(d.previous)) : std::nullopt);
        for (const std::string& m : order)
        {
            std::string tag;
            if (opt.fresh.count(m)) tag += " (nouveau)";
            if (m == d.previous) tag += " (choix précédent)";
            if (d.where.at(m) == "nonlu")
            {
                line("| " + d.names.at(m) + tag + " | ne lit pas ce modèle | — | — | — | — | — | — | — | — |");
                continue;
            }
            std::string ecart = "—";
            if (base && *base != 0 && d.write.count(m) && m != d.previous)
            {
                long long pct = static_cast<long long>(std::llround(100.0 * (d.write.at(m) / *base - 1)));
                const char* sign = pct == 0 ? "±" : pct > 0 ? "+" : "−";
                ecart = std::string(sign) + std::to_string(std::llabs(pct)) + " %";
            }
            const std::string mem = d.engine(m) ? "—" : (d.kv_type(m) == "f16" ? "demi-précision" : "8 bits");
            line("| " + d.names.at(m) + tag + " | " + (d.where.at(m) == "gpu" ? "GPU" : "processeur")
                 + " | " + fmt(d.get(m, "512", "q8", 0)) + " | " + fmt(d.get(m, "4096", "q8", 0))
                 + " | " + fmt(d.get(m, "512", "q8", 1)) + " | " + fmt(d.get(m, "4096", "q8", 1))
                 + " | " + ecart + " | " + (d.ppl.count(m) ? two(d.ppl.at(m)) : "—")
                 + " | " + quiz_text(d, m) + " | " + mem + " |");
        }

        // Section « comme un utilisateur » (si --service).
        if (opt.service.is_object())
        {
            line();
            line("### Comme un utilisateur, par le service : "
                 + (opt.service["model"].is_string() ? opt.service["model"].as_string() : "?"));
            line();
            line("Un service d'essai isolé (jamais celui de l'utilisateur) et llama-server (llama.cpp) sur le même "
                 "fichier, par l'interface de discussion ; les temps viennent des réponses. La conversation de 32k : "
                 "un document d'environ 32 768 tokens, une réponse de 512 au plus.");
            line();
            line("| Mesure | Gulliver | llama.cpp |");
            line("|---|---|---|");
            for (const auto& row : service_rows(opt.service))
                line("| " + row.first + " | " + row.second[0] + " | " + row.second[1] + " |");
            for (const char* e : { "gulliver", "llama" })
            {
                const json& eng = opt.service[e];
                if (eng.is_object() && eng["error"].is_string())
                {
                    line();
                    line(std::string(std::string(e) == "gulliver" ? "Gulliver" : "llama.cpp")
                         + " : arrêté sur une erreur (" + eng["error"].as_string().substr(0, 160) + ").");
                }
            }
        }
        // Section RAG (si --rag avec des résultats).
        if (opt.rag.is_object() && opt.rag["results"].is_object() && !opt.rag["results"].empty())
        {
            line();
            line("### La recherche dans les documents (RAG)");
            line();
            line(rag_text(opt.rag));
            line();
            line("| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |");
            line("|---|---|---|---|---|");
            for (const auto& r : rag_rows(opt.rag))
                line("| " + r[0] + " | " + r[1] + " | " + r[2] + " | " + r[3] + " | " + r[4] + " |");
        }

        // Pas de modèle exploitable : l'outil d'origine s'arrête avant le conseil.
        bool any_usable = false;
        for (const std::string& m : d.order)
            if (d.where.at(m) != "nonlu" && d.write.count(m) && !d.engine(m)) { any_usable = true; break; }
        if (!any_usable) return o.str();

        const std::map<std::string, std::string> labels = {
            { "general", "Pour tout faire" }, { "accurate", "Pour les réponses les plus justes" },
            { "fastest", "Pour aller vite" }, { "cyber", "Pour la cybersécurité (usage autorisé)" } };
        line();
        line("### Conseil : quel modèle employer avec Gulliver");
        line();
        for (const auto& a : d.advice)
        {
            const std::string& m = a.id;
            std::string p;
            if (d.ppl.count(m)) p += ", perplexité " + two(d.ppl.at(m));
            if (d.quiz.count(m)) p += ", épreuve " + quiz_text(d, m);
            line("- **" + labels.at(a.role) + "** : **" + d.names.at(m) + "**, écriture " + fmt(d.write.at(m))
                 + " jetons/s après 512 tokens" + p + " ; mémoire de conversation conseillée : "
                 + (d.kv_type(m) == "f16" ? "demi-précision" : "8 bits") + ".");
        }
        if (!d.previous.empty())
        {
            line();
            const bool same = !d.advice.empty() && d.advice.front().id == d.previous;
            line("Par rapport à la semaine dernière : " + (same
                ? std::string("le conseil pour tout faire ne change pas.")
                : "le conseil pour tout faire passe de " + d.names.at(d.previous) + " à "
                  + d.names.at(d.advice.front().id) + "."));
        }
        if (!d.remove.empty())
        {
            line();
            const std::string why = d.where.at(d.remove) == "nonlu" ? "le moteur ne le lit pas"
                : "épreuve " + quiz_text(d, d.remove) + ", écriture " + fmt(d.write.count(d.remove)
                    ? std::optional<double>(d.write.at(d.remove)) : std::nullopt) + " jetons/s";
            line("Le dernier du classement, retiré de Gulliver : **" + d.names.at(d.remove) + "** (" + why
                 + ") ; la veille ne le retéléchargera plus, et le menu « Modèles retirés » de l'onglet Modèles le "
                   "remet d'un clic.");
        }
        line();
        line("Le bouton « Choisir » de l'onglet Modèles de Gulliver applique le modèle et ses réglages. À lire avec "
             "deux précautions : l'épreuve est courte (40 questions), et elle ne remplace pas l'essai sur vos propres "
             "questions ; l'écriture est mesurée sans brouillon (sur une recopie ou une question sur un document, les "
             "brouillons la multiplient par 1,3 à 3).");
        return o.str();
    }

    std::string build_conseil_doc(const std::string& raw_text, const conseil_options& opt,
                                  const std::string& when, const std::string& doc_text)
    {
        const std::string begin = "<!-- semaine : début -->", end = "<!-- semaine : fin -->";
        const size_t a = doc_text.find(begin), b = doc_text.find(end);
        if (a == std::string::npos || b == std::string::npos) return doc_text;   // marqueurs absents

        const cdata d = compute(raw_text, opt);

        // La charge de la machine, phrasée comme l'outil d'origine (libellés en minuscules,
        // pas d'unité après la fourchette).
        std::string temp_text;
        const std::vector<std::string> load = split(opt.temperature, '|');
        if (load.size() == 10)
        {
            auto num = [](std::string v) { for (char& c : v) if (c == '.') c = ','; return v; };
            struct L { const char* label; const char* unit; int off; };
            const L ls[] = { { "température de la puce", "°C", 0 },
                             { "utilisation du processeur", "%", 3 },
                             { "utilisation du gpu", "%", 6 } };
            std::string joined;
            for (int i = 0; i < 3; ++i)
                joined += (i ? " ; " : "") + std::string(ls[i].label) + " " + num(load[ls[i].off + 1]) + " "
                        + ls[i].unit + " en moyenne (de " + num(load[ls[i].off]) + " à " + num(load[ls[i].off + 2]) + ")";
            temp_text = "pendant la mesure (" + load[9] + " relevés, toutes les 20 secondes) : " + joined;
        }

        std::vector<std::string> lines;
        lines.push_back(begin);
        lines.push_back("");
        lines.push_back("## La mesure de la semaine (" + when + ")");
        lines.push_back("");
        std::string intro = "Écrite par le Mac à chaque mesure de la semaine, le lundi (`tools/bench_modeles.sh`) : "
            "Gulliver seul, sur chacun de ses modèles installés ; jetons par seconde, mémoire de conversation en 8 bits.";
        if (!temp_text.empty()) intro += " Charge de la machine " + temp_text + ".";
        if (opt.late) intro += " L'heure limite a coupé la mesure avant la fin.";
        lines.push_back(intro);
        lines.push_back("");
        lines.push_back("| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Perplexité | Épreuve |");
        lines.push_back("|---|---|---|---|---|---|---|---|");

        std::vector<std::string> order = d.order;
        std::stable_sort(order.begin(), order.end(), [&](const std::string& x, const std::string& y) {
            double wx = d.write.count(x) ? d.write.at(x) : 0.0;
            double wy = d.write.count(y) ? d.write.at(y) : 0.0;
            return wx > wy;
        });
        for (const std::string& m : order)
        {
            if (d.engine(m) || d.where.at(m) == "nonlu") continue;
            const std::string tag = (m == d.previous) ? " (choix précédent)" : "";
            lines.push_back("| " + d.names.at(m) + tag + " | " + (d.where.at(m) == "gpu" ? "GPU" : "processeur")
                + " | " + fmt(d.get(m, "512", "q8", 0)) + " | " + fmt(d.get(m, "4096", "q8", 0))
                + " | " + fmt(d.get(m, "512", "q8", 1)) + " | " + fmt(d.get(m, "4096", "q8", 1))
                + " | " + (d.ppl.count(m) ? two(d.ppl.at(m)) : "—") + " | " + quiz_text(d, m) + " |");
        }

        if (!d.advice.empty())
        {
            const std::map<std::string, std::string> shortl = {
                { "general", "pour tout faire" }, { "accurate", "le plus juste" },
                { "fastest", "le plus rapide" }, { "cyber", "pour la cybersécurité" } };
            std::string s = "Conseillé : ";
            for (size_t i = 0; i < d.advice.size(); ++i)
                s += (i ? " ; " : "") + shortl.at(d.advice[i].role) + ", " + d.names.at(d.advice[i].id);
            s += ". Les tableaux complets, avec llama.cpp et MLX, sont dans « Mesures de la semaine ».";
            lines.push_back("");
            lines.push_back(s);
        }

        if (opt.service.is_object() && opt.service["gulliver"].is_object())
        {
            lines.push_back("");
            lines.push_back("Comme un utilisateur, par le service, sur "
                + (opt.service["model"].is_string() ? opt.service["model"].as_string() : "?") + " :");
            lines.push_back("");
            lines.push_back("| Mesure | Gulliver |");
            lines.push_back("|---|---|");
            for (const auto& row : service_rows(opt.service, { "gulliver" }))
                lines.push_back("| " + row.first + " | " + row.second[0] + " |");
        }
        if (opt.rag.is_object() && opt.rag["results"].is_object() && !opt.rag["results"].empty())
        {
            lines.push_back("");
            lines.push_back("La recherche dans les documents (RAG) : " + rag_text(opt.rag));
            lines.push_back("");
            lines.push_back("| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |");
            lines.push_back("|---|---|---|---|---|");
            for (const auto& r : rag_rows(opt.rag))
                lines.push_back("| " + r[0] + " | " + r[1] + " | " + r[2] + " | " + r[3] + " | " + r[4] + " |");
        }
        lines.push_back("");
        lines.push_back(end);

        std::string joined;
        for (size_t i = 0; i < lines.size(); ++i) joined += (i ? "\n" : "") + lines[i];
        return doc_text.substr(0, a) + joined + doc_text.substr(b + end.size());
    }

}  // namespace gulliver
