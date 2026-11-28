// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les tableaux Markdown des comparaisons de Gulliver avec les autres moteurs (Gulliver 7.25.0, à la place des
    programmes Python glissés dans tools/bench_moteurs.sh et tools/justesse_moteurs.sh).

        build/macos-gulliver/bin/tableaux vitesse  moteurs-<date>.raw    # lignes « modèle|taille|réglage|lecture|écriture »
        build/macos-gulliver/bin/tableaux justesse justesse-<date>.raw   # lignes « modèle|moteur|perplexité|incertitude|top-1 »

    vitesse : la moyenne des essais de chaque réglage, la lecture puis l'écriture, avec l'écart à Gulliver (clés et
    valeurs en 8 bits) en pour cent arrondi au pair ; les autres réglages (clés en demi-précision, llama.cpp en 8 bits)
    à part.  justesse : la perplexité de chaque moteur et son écart à Gulliver sur le GPU, à deux décimales.  Les
    nombres s'écrivent à la française (espace des milliers, virgule), au caractère près comme l'outil Python.
*/

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    std::vector<std::string> split(const std::string& s, char c)
    {
        std::vector<std::string> out;
        size_t from = 0;
        for (;;)
        {
            const size_t at = s.find(c, from);
            out.push_back(s.substr(from, at == std::string::npos ? std::string::npos : at - from));
            if (at == std::string::npos) return out;
            from = at + 1;
        }
    }

    std::vector<std::string> read_lines(const char* path)
    {
        std::ifstream f(path, std::ios::binary);
        std::vector<std::string> out;
        std::string line;
        while (std::getline(f, line)) out.push_back(line);
        return out;
    }

    // float() de Python sur un texte : faux s'il ne se lit pas en entier.
    bool to_double(const std::string& s, double& out)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        if (a == b) return false;
        const std::string t = s.substr(a, b - a);
        char* stop = nullptr;
        out = std::strtod(t.c_str(), &stop);
        return *stop == '\0';
    }

    std::string replace_all(std::string s, const std::string& from, const std::string& to)
    {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
        return s;
    }

    // Un dictionnaire ordonné, comme collections.OrderedDict de Python.
    template <typename K, typename V>
    struct ordered
    {
        std::vector<std::pair<K, V>> items;
        V& operator[](const K& k)
        {
            for (auto& kv : items)
                if (kv.first == k) return kv.second;
            items.push_back({k, V()});
            return items.back().second;
        }
        const V* find(const K& k) const
        {
            for (const auto& kv : items)
                if (kv.first == k) return &kv.second;
            return nullptr;
        }
    };

    // ---- vitesse ----

    // La moyenne d'une colonne : absente, « nonlu » (le moteur ne lit pas ce modèle) ou un nombre.
    struct mean_t
    {
        enum kind_t { none, nonlu, value } kind = none;
        double v = 0;
    };

    mean_t mean(const std::vector<std::pair<std::string, std::string>>& vals, int idx)
    {
        mean_t m;
        bool all_nonlu = !vals.empty();
        double sum = 0;
        int n = 0;
        for (const auto& pg : vals)
        {
            const std::string& x = idx == 0 ? pg.first : pg.second;
            all_nonlu = all_nonlu && x == "nonlu";
            double d;
            if (x != "—" && !x.empty() && x != "nonlu")
            {
                if (!to_double(x, d))
                {
                    std::fprintf(stderr, "tableaux: not a number: %s\n", x.c_str());
                    std::exit(1);
                }
                sum += d;
                ++n;
            }
        }
        if (all_nonlu) m.kind = mean_t::nonlu;
        else if (n)
        {
            m.kind = mean_t::value;
            m.v = sum / n;
        }
        return m;
    }

    std::string num(const mean_t& x)
    {
        if (x.kind != mean_t::value) return "—";
        char buf[64];
        if (x.v >= 100)
        {
            std::snprintf(buf, sizeof(buf), "%.0f", x.v);
            std::string digits = buf, out;
            const bool negative = !digits.empty() && digits[0] == '-';
            if (negative) digits.erase(0, 1);
            for (size_t i = 0; i < digits.size(); ++i)
            {
                if (i && (digits.size() - i) % 3 == 0) out += ' ';
                out += digits[i];
            }
            return (negative ? "-" : "") + out;
        }
        std::snprintf(buf, sizeof(buf), "%.1f", x.v);
        return replace_all(buf, ".", ",");
    }

    std::string cell(const mean_t& x, const mean_t& base)
    {
        if (x.kind == mean_t::nonlu) return "ne lit pas ce modèle";
        if (x.kind == mean_t::none) return "—";
        if (base.kind != mean_t::value || base.v == 0) return num(x);
        const long long d = static_cast<long long>(std::nearbyint(100.0 * (x.v / base.v - 1.0)));
        return num(x) + " (" + (d == 0 ? "±" : d > 0 ? "+" : "−") + std::to_string(std::llabs(d)) + " %)";
    }

    using samples = std::vector<std::pair<std::string, std::string>>;
    using by_key = ordered<std::string, samples>;
    using by_size = ordered<long long, by_key>;

    int vitesse(const char* path)
    {
        ordered<std::string, by_size> data;
        for (const std::string& line : read_lines(path))
        {
            const auto f = split(line, '|');
            if (f.size() != 5)
            {
                std::fprintf(stderr, "tableaux: bad line: %s\n", line.c_str());
                return 1;
            }
            data[f[0]][std::stoll(f[1])][f[2]].push_back({f[3], f[4]});
        }
        const std::vector<std::pair<std::string, std::string>> main_cols = {{"gulliver_q8", "Gulliver"},
            {"llama", "llama.cpp"}, {"mlx", "MLX"}};
        const std::vector<std::pair<std::string, std::string>> other = {{"gulliver_f16", "Gulliver, clés en demi-précision"},
            {"llama_q8", "llama.cpp, clés en 8 bits"}};
        auto present = [&](const std::string& k) {
            for (const auto& [name, sizes] : data.items)
                for (const auto& [n, cfg] : sizes.items)
                    if (cfg.find(k)) return true;
            return false;
        };
        static const samples empty;
        auto get = [&](const by_key& cfg, const std::string& k) -> const samples& {
            const samples* s = cfg.find(k);
            return s ? *s : empty;
        };
        for (const auto& [what, idx] : {std::pair<const char*, int>{"Lecture de la question", 0}, {"Écriture de la réponse", 1}})
        {
            std::vector<std::pair<std::string, std::string>> cols;
            for (const auto& c : main_cols)
                if (present(c.first)) cols.push_back(c);
            std::printf("\n## %s\n\n", what);
            std::string head = "| Modèle | Question | ", rule = "|---|---|";
            for (size_t i = 0; i < cols.size(); ++i)
            {
                head += (i ? " | " : "") + cols[i].second;
                rule += "---|";
            }
            std::printf("%s |\n%s\n", head.c_str(), rule.c_str());
            for (const auto& [name, sizes] : data.items)
                for (const auto& [n, cfg] : sizes.items)
                {
                    const mean_t base = mean(get(cfg, "gulliver_q8"), idx);
                    std::string row = num(base);
                    for (size_t i = 1; i < cols.size(); ++i)
                        row += " | " + (cfg.find(cols[i].first) ? cell(mean(get(cfg, cols[i].first), idx), base) : std::string("—"));
                    std::printf("| %s | %lld | %s |\n", name.c_str(), n, row.c_str());
                }
        }
        std::printf("\n## Les autres réglages\n\n");
        std::printf("Écart au réglage du service de Gulliver (clés et valeurs en 8 bits), lecture puis écriture.\n\n");
        std::string head = "| Modèle | Question | ", rule = "|---|---|";
        for (size_t i = 0; i < other.size(); ++i)
        {
            head += (i ? " | " : "") + other[i].second;
            rule += "---|";
        }
        std::printf("%s |\n%s\n", head.c_str(), rule.c_str());
        for (const auto& [name, sizes] : data.items)
            for (const auto& [n, cfg] : sizes.items)
            {
                std::string out;
                for (size_t i = 0; i < other.size(); ++i)
                {
                    out += i ? " | " : "";
                    if (!cfg.find(other[i].first))
                    {
                        out += "—";
                        continue;
                    }
                    for (int idx = 0; idx < 2; ++idx)
                        out += (idx ? " / " : "") + cell(mean(get(cfg, other[i].first), idx), mean(get(cfg, "gulliver_q8"), idx));
                }
                std::printf("| %s | %lld | %s |\n", name.c_str(), n, out.c_str());
            }
        return 0;
    }

    // ---- justesse ----

    int justesse(const char* path)
    {
        ordered<std::string, ordered<std::string, std::string>> data;
        for (const std::string& line : read_lines(path))
        {
            const auto parts = split(line, '|');
            if (parts.size() < 2)
            {
                std::fprintf(stderr, "tableaux: bad line: %s\n", line.c_str());
                return 1;
            }
            data[parts[0]][parts[1]] = parts.size() > 2 ? parts[2] : "";
        }
        const std::vector<std::pair<std::string, std::string>> all = {{"gulliver_gpu", "Gulliver, GPU"},
            {"gulliver_cpu", "Gulliver, processeur"}, {"llama", "llama.cpp, GPU"}, {"mlx", "MLX (son propre fichier)"}};
        std::vector<std::pair<std::string, std::string>> cols;
        for (const auto& c : all)
        {
            bool found = false;
            for (const auto& [name, m] : data.items) found = found || m.find(c.first);
            if (found) cols.push_back(c);
        }
        auto fmt3 = [](double x) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.3f", x);
            return replace_all(buf, ".", ",");
        };
        std::string head = "\n| Modèle | ", rule = "|---|";
        for (size_t i = 0; i < cols.size(); ++i)
        {
            head += (i ? " | " : "") + cols[i].second;
            rule += "---|";
        }
        std::printf("%s |\n%s\n", head.c_str(), rule.c_str());
        for (const auto& [name, m] : data.items)
        {
            double base = 0;
            const std::string* b = m.find("gulliver_gpu");
            const bool has_base = b && to_double(*b, base);
            std::string row;
            for (size_t i = 0; i < cols.size(); ++i)
            {
                row += i ? " | " : "";
                const std::string* v = m.find(cols[i].first);
                double x;
                if (!v) row += "—";
                else if (*v == "nonlu") row += "ne lit pas ce modèle";
                else if (!to_double(*v, x)) row += "—";
                else if (cols[i].first == "gulliver_gpu" || !has_base) row += fmt3(x);
                else
                {
                    const double d = 100.0 * (x / base - 1.0);
                    const std::string sign = std::fabs(d) < 0.005 ? "±" : d > 0 ? "+" : "−";
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.2f", std::fabs(d));
                    row += replace_all(fmt3(x) + " (" + sign + buf + " %)", ".", ",");
                }
            }
            std::printf("| %s | %s |\n", name.c_str(), row.c_str());
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "vitesse") return vitesse(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "justesse") return justesse(argv[2]);
    std::fprintf(stderr, "usage: tableaux vitesse|justesse RAW\n");
    return 2;
}
