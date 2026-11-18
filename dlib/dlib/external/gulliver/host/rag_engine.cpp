// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "rag_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace gulliver
{
    namespace
    {
        // Les paramètres du découpage et de la recherche, mêmes valeurs que web/js/rag.js.
        const long long passage_chars = 1200;
        const long long overlap_chars = 240;
        const double min_meaning = 0.35;
        const double meaning_spread = 0.2;
        const double words_share = 0.3;
        const int followed = 3;

        // Les mots vides, repris de documents.js (anglais, français, allemand, italien, espagnol).
        const std::unordered_set<std::string>& stop_words()
        {
            static const std::unordered_set<std::string> s = {
                "the","a","an","and","or","of","to","in","on","for","with","is","are","was","were","be","by","as","at",
                "it","this","that","from","not",
                "le","la","les","un","une","des","et","ou","de","du","en","dans","pour","par","sur","avec","est","sont",
                "au","aux","ce","cette","qui","que",
                "der","die","das","und","oder","ein","eine","zu","im","mit","ist","sind","von","den","dem",
                "il","lo","gli","e","di","da","per","con","che",
                "el","los","las","y","del","para","por","es","son"
            };
            return s;
        }

        // ----------------------------------------------------------------------------------------
        //  Tokenisation latine (rag_terms)

        // Une lettre latine accentuée majuscule (Latin-1 supplément, et Latin étendu A où les codes
        // pairs sont les majuscules), et sa mise en minuscule.
        bool u_is_upper_latin(uint32_t cp)
        {
            if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return true;   // À..Þ sauf ×
            if (cp >= 0x0100 && cp <= 0x017F) return (cp % 2) == 0;          // Latin étendu A : pairs = majuscules (approx.)
            return false;
        }
        uint32_t to_lower_latin(uint32_t cp)
        {
            if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 0x20;
            if (cp >= 0x0100 && cp <= 0x017F && (cp % 2) == 0) return cp + 1;
            return cp;
        }

        /*
            Un caractère du texte, lu en UTF-8 à partir de i (avancé au caractère suivant), rendu sous
            forme d'une lettre latine minuscule sans accent, d'un chiffre ou de '_' quand c'en est un,
            sinon '\0' (un séparateur de mots).  Couvre l'ASCII et le latin-1/latin étendu courant
            (accents du français et des langues voisines) ; une écriture non latine donne '\0' (choix
            de l'humain : les documents visés sont en français et en anglais).
        */
        char latin_token_char(const std::string& s, size_t& i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80)
            {
                ++i;
                if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') return static_cast<char>(c);
                return '\0';
            }
            // Séquence UTF-8 : décoder le point de code (jusqu'à 4 octets), puis le ramener à une lettre latine.
            uint32_t cp = 0;
            int extra = 0;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
            else { ++i; return '\0'; }  // octet de continuation isolé ou invalide
            ++i;
            for (int k = 0; k < extra && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80; ++k)
            {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
                ++i;
            }
            // Les lettres latines accentuées ramenées à leur lettre de base (é→e, ç→c, ñ→n…).  Latin-1
            // supplément (U+00C0–U+00FF) et une poignée de Latin étendu A courants.
            auto base = [](uint32_t u) -> char {
                switch (u)
                {
                    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
                    case 0x0101: case 0x0103: case 0x0105: return 'a';
                    case 0x00E7: case 0x0107: case 0x0109: case 0x010D: return 'c';
                    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: case 0x0113: case 0x0115:
                    case 0x0117: case 0x0119: case 0x011B: return 'e';
                    case 0x011F: case 0x0121: case 0x0123: return 'g';
                    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: case 0x0129: case 0x012B:
                    case 0x012D: case 0x012F: return 'i';
                    case 0x0135: return 'j';
                    case 0x013A: case 0x013C: case 0x013E: case 0x0142: return 'l';
                    case 0x00F1: case 0x0144: case 0x0146: case 0x0148: return 'n';
                    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
                    case 0x014D: case 0x014F: case 0x0151: return 'o';
                    case 0x0155: case 0x0157: case 0x0159: return 'r';
                    case 0x015B: case 0x015D: case 0x015F: case 0x0161: return 's';
                    case 0x0163: case 0x0165: case 0x0167: return 't';
                    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: case 0x0169: case 0x016B:
                    case 0x016D: case 0x016F: case 0x0171: case 0x0173: return 'u';
                    case 0x00FD: case 0x00FF: return 'y';
                    case 0x017A: case 0x017C: case 0x017E: return 'z';
                    default: return '\0';
                }
            };
            // Majuscules latines accentuées : les ramener d'abord en minuscule (décalage de 0x20 dans
            // Latin-1 supplément et par parité dans Latin étendu A).
            if (u_is_upper_latin(cp)) cp = to_lower_latin(cp);
            return base(cp);
        }
    }

    std::vector<std::string> rag_terms(const std::string& text)
    {
        std::vector<std::string> out;
        std::string word;
        size_t i = 0;
        while (i < text.size())
        {
            const char c = latin_token_char(text, i);
            if (c == '\0')
            {
                if (word.size() > 1 && !stop_words().count(word)) out.push_back(word);
                word.clear();
            }
            else
            {
                word.push_back(c);
            }
        }
        if (word.size() > 1 && !stop_words().count(word)) out.push_back(word);
        return out;
    }

    // ----------------------------------------------------------------------------------------
    //  Découpage en passages (rag_chunk), fidèle à chunk() de web/js/rag.js

    namespace
    {
        // Les utilitaires de chaîne du découpage, qui reproduisent ceux de JavaScript utilisés par chunk().
        bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

        std::string trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && is_space(s[a])) ++a;
            while (b > a && is_space(s[b - 1])) --b;
            return s.substr(a, b - a);
        }
        std::string trim_end(const std::string& s)
        {
            size_t b = s.size();
            while (b > 0 && is_space(s[b - 1])) --b;
            return s.substr(0, b);
        }
        std::string trim_start(const std::string& s)
        {
            size_t a = 0;
            while (a < s.size() && is_space(s[a])) ++a;
            return s.substr(a);
        }

        // Le motif d'un emplacement : "## Page N", "## Slide ...", "## Sheet: ..." ; rend le contenu après "## ".
        bool match_place(const std::string& line, std::string& inner)
        {
            if (line.rfind("## ", 0) != 0) return false;
            const std::string rest = line.substr(3);
            auto starts = [&](const char* p) { return rest.rfind(p, 0) == 0; };
            if (starts("Sheet: ") || starts("Slide ")) { inner = rest; return true; }
            if (starts("Page "))
            {
                // "Page " suivi d'au moins un chiffre, et rien d'autre.
                size_t k = 5;
                while (k < rest.size() && rest[k] >= '0' && rest[k] <= '9') ++k;
                if (k > 5 && k == rest.size()) { inner = rest; return true; }
            }
            return false;
        }

        // Le motif d'un titre : 1 à 6 '#', une espace, puis le titre ; rend le titre.
        bool match_heading(const std::string& line, std::string& title)
        {
            size_t h = 0;
            while (h < line.size() && line[h] == '#') ++h;
            if (h < 1 || h > 6 || h >= line.size() || line[h] != ' ') return false;
            title = line.substr(h + 1);
            return !title.empty();
        }

        // Le début d'une phrase ou d'une ligne dans tail, comme tail.search(/\n|[.!?;]\s/) de rag.js :
        // la position du premier '\n', ou d'un '.', '!', '?' ou ';' suivi d'une espace.  -1 si aucun.
        long long search_break(const std::string& tail)
        {
            for (size_t k = 0; k < tail.size(); ++k)
            {
                if (tail[k] == '\n') return static_cast<long long>(k);
                if ((tail[k] == '.' || tail[k] == '!' || tail[k] == '?' || tail[k] == ';')
                    && k + 1 < tail.size() && is_space(tail[k + 1]))
                    return static_cast<long long>(k);
            }
            return -1;
        }

        // La dernière occurrence de needle dans s.substr(0, upto), ou -1.
        long long last_index_of(const std::string& s, const std::string& needle, size_t upto)
        {
            const std::string slice = s.substr(0, upto);
            const size_t p = slice.rfind(needle);
            return p == std::string::npos ? -1 : static_cast<long long>(p);
        }
    }

    std::vector<rag_piece> rag_chunk(const std::string& text)
    {
        std::vector<rag_piece> out;
        std::string location, section, header;
        std::string buffer;
        long long buffer_start = 0;
        std::string buffer_where;
        long long pos = 0;

        auto where = [&]() -> std::string {
            if (!location.empty() && !section.empty()) return location + " \xC2\xB7 " + section;  // " · "
            return location.empty() ? section : location;
        };
        auto emit = [&](bool keep_tail) {
            const std::string body = trim(buffer);
            if (!body.empty())
            {
                std::string prefix;
                if (!header.empty() && buffer.rfind(header, 0) != 0) prefix = header + "\n";
                rag_piece p;
                p.start = buffer_start;
                p.end = pos;
                p.where = buffer_where;
                p.text = prefix + body;
                out.push_back(p);
            }
            std::string tail;
            if (keep_tail && static_cast<long long>(buffer.size()) > overlap_chars)
            {
                tail = buffer.substr(buffer.size() - static_cast<size_t>(overlap_chars));
                const long long cut = search_break(tail);
                if (cut >= 0 && cut < static_cast<long long>(tail.size()) - 40)
                    tail = trim_start(tail.substr(static_cast<size_t>(cut) + 1));
            }
            buffer = tail;
            buffer_start = pos - static_cast<long long>(tail.size());
            buffer_where = where();
        };
        auto add = [&](const std::string& piece) {
            if (buffer.empty())
            {
                buffer_start = pos;
                buffer_where = where();
            }
            buffer += piece;
        };

        // Les lignes, en gardant le '\n' comme la page (line = ligne + '\n' sauf la dernière).
        std::vector<std::string> lines;
        {
            size_t start = 0;
            for (size_t k = 0; k <= text.size(); ++k)
            {
                if (k == text.size()) { lines.push_back(text.substr(start)); break; }
                if (text[k] == '\n') { lines.push_back(text.substr(start, k - start)); start = k + 1; }
            }
        }

        for (size_t li = 0; li < lines.size(); ++li)
        {
            const std::string line = lines[li] + (li + 1 < lines.size() ? "\n" : "");
            const std::string line_trimmed = trim_end(line);

            std::string place_inner;
            if (match_place(line_trimmed, place_inner))
            {
                emit(false);
                location = place_inner;
                section.clear();
                header.clear();
                pos += static_cast<long long>(line.size());
                buffer_start = pos;
                buffer_where = where();
                continue;
            }

            std::string title;
            if (match_heading(line_trimmed, title))
            {
                if (static_cast<long long>(buffer.size()) > passage_chars / 3) emit(false);
                section = trim(title).substr(0, 80);
                if (trim(buffer).empty()) buffer_where = where();
            }
            if (location.rfind("Sheet: ", 0) == 0 && header.empty() && !trim(line).empty()) header = trim_end(line);

            std::string rest = line;
            while (!rest.empty())
            {
                const long long room = passage_chars - static_cast<long long>(buffer.size());
                if (static_cast<long long>(rest.size()) > room && rest == line
                    && static_cast<long long>(line.size()) <= passage_chars / 4 && !trim(buffer).empty())
                {
                    emit(true);
                    continue;
                }
                if (static_cast<long long>(rest.size()) <= room)
                {
                    add(rest);
                    pos += static_cast<long long>(rest.size());
                    rest.clear();
                }
                else
                {
                    long long cut = room;
                    const std::string slice = rest.substr(0, static_cast<size_t>(room > 0 ? room : 0));
                    const long long sentence = std::max({ last_index_of(slice, ". ", slice.size()),
                        last_index_of(slice, "! ", slice.size()), last_index_of(slice, "? ", slice.size()) });
                    if (sentence > room / 2) cut = sentence + 2;
                    else
                    {
                        const long long space = last_index_of(slice, " ", slice.size());
                        if (space > room / 2) cut = space + 1;
                    }
                    if (cut <= 0) cut = room > 0 ? room : 0;
                    if (cut > 0)
                    {
                        add(rest.substr(0, static_cast<size_t>(cut)));
                        pos += cut;
                        rest = rest.substr(static_cast<size_t>(cut));
                    }
                    emit(true);
                }
            }
            if (static_cast<long long>(buffer.size()) >= passage_chars) emit(true);
        }
        emit(false);
        return out;
    }

    // ----------------------------------------------------------------------------------------
    //  Scores, fusion et assemblage des extraits (communs aux moteurs)

    namespace
    {
        // Les termes de chaque passage (sac de mots), calculés une fois par recherche.
        struct sparse_row
        {
            std::unordered_map<std::string, int> tf;
            int length = 0;
        };

        std::vector<sparse_row> sparse_of(const std::vector<rag_passage>& ps)
        {
            std::vector<sparse_row> out(ps.size());
            for (size_t i = 0; i < ps.size(); ++i)
            {
                const std::vector<std::string> ws = rag_terms(ps[i].text);
                out[i].length = static_cast<int>(ws.size());
                for (const std::string& w : ws) out[i].tf[w] += 1;
            }
            return out;
        }

        // BM25 de la question sur chaque passage (formule de rag.js).
        std::vector<double> bm25_scores(const std::vector<sparse_row>& sparse, const std::string& question)
        {
            const size_t n = sparse.size();
            std::vector<double> words(n, 0.0);
            // Les termes uniques de la question.
            std::vector<std::string> q;
            {
                std::unordered_set<std::string> seen;
                for (const std::string& w : rag_terms(question)) if (seen.insert(w).second) q.push_back(w);
            }
            if (q.empty() || n == 0) return words;
            std::unordered_map<std::string, int> df;
            for (const sparse_row& s : sparse) for (const std::string& w : q) if (s.tf.count(w)) df[w] += 1;
            double total_len = 0;
            for (const sparse_row& s : sparse) total_len += s.length;
            const double avg = total_len / static_cast<double>(n) ? total_len / static_cast<double>(n) : 1.0;
            for (size_t i = 0; i < n; ++i)
            {
                double score = 0;
                for (const std::string& w : q)
                {
                    auto it = sparse[i].tf.find(w);
                    if (it == sparse[i].tf.end()) continue;
                    const double f = it->second;
                    const double d = df.count(w) ? df[w] : 0;
                    score += std::log(1 + (n - d + 0.5) / (d + 0.5))
                        * (f * 2.2) / (f + 1.2 * (0.25 + 0.75 * sparse[i].length / avg));
                }
                words[i] = score;
            }
            return words;
        }

        double cosine(const std::vector<float>& a, const std::vector<float>& b)
        {
            if (a.empty() || a.size() != b.size()) return 0.0;
            double dot = 0;
            for (size_t k = 0; k < a.size(); ++k) dot += static_cast<double>(a[k]) * b[k];
            return dot;
        }

        /*
            À partir des scores fusionnés (fused[i] > 0 pour les candidats), choisit les extraits par
            pertinence marginale maximale, prolonge les meilleurs par le passage suivant, fond les
            passages consécutifs et ordonne les documents — fidèle à la fin de search() de rag.js.
        */
        std::vector<rag_excerpt> assemble(const std::vector<rag_passage>& all, std::vector<double>& fused,
                                          const rag_search_options& options, const std::string& model)
        {
            // L'indice d'un passage par son id, pour trouver le passage suivant (doc:ordinal+1).
            std::unordered_map<std::string, size_t> at;
            for (size_t i = 0; i < all.size(); ++i) at[all[i].id] = i;

            std::vector<size_t> candidates;
            for (size_t i = 0; i < all.size(); ++i) if (fused[i] > 0) candidates.push_back(i);
            std::stable_sort(candidates.begin(), candidates.end(),
                [&](size_t a, size_t b) { return fused[a] > fused[b]; });
            if (candidates.size() > 60) candidates.resize(60);
            if (candidates.empty()) return {};

            const double top = fused[candidates[0]];
            auto similarity = [&](size_t a, size_t b) -> double {
                if (!all[a].vector.empty() && all[a].vector.size() == all[b].vector.size()
                    && all[a].model == model && all[b].model == model)
                    return cosine(all[a].vector, all[b].vector);
                return (all[a].doc == all[b].doc && std::llabs(all[a].ordinal - all[b].ordinal) <= 1) ? 0.6 : 0.0;
            };

            std::vector<size_t> chosen;
            std::vector<size_t> pool = candidates;
            long long used = 0;
            while (!pool.empty() && static_cast<int>(chosen.size()) < options.limit)
            {
                size_t best_k = 0;
                double best_score = -1e300;
                for (size_t k = 0; k < pool.size(); ++k)
                {
                    double like = 0;
                    for (size_t j : chosen) like = std::max(like, similarity(pool[k], j));
                    const double score = 0.75 * (fused[pool[k]] / top) - 0.25 * like;
                    if (score > best_score) { best_score = score; best_k = k; }
                }
                const size_t i = pool[best_k];
                pool.erase(pool.begin() + static_cast<long>(best_k));
                if (used + static_cast<long long>(all[i].text.size()) > options.budget && !chosen.empty()) continue;
                chosen.push_back(i);
                used += static_cast<long long>(all[i].text.size());
            }

            // Le passage qui suit chacun des meilleurs le prolonge, dans le budget et la limite.
            std::set<size_t> taken(chosen.begin(), chosen.end());
            const size_t n_follow = std::min<size_t>(followed, chosen.size());
            const std::vector<size_t> heads(chosen.begin(), chosen.begin() + static_cast<long>(n_follow));
            for (size_t i : heads)
            {
                auto it = at.find(all[i].doc + ":" + std::to_string(all[i].ordinal + 1));
                if (it == at.end()) continue;
                const size_t next = it->second;
                if (taken.count(next) || static_cast<int>(chosen.size()) >= options.limit) continue;
                if (used + static_cast<long long>(all[next].text.size()) > options.budget) continue;
                chosen.push_back(next);
                taken.insert(next);
                used += static_cast<long long>(all[next].text.size());
                fused[next] = std::max(fused[next], fused[i] * 0.5);
            }

            // Les extraits de chaque document dans son ordre, passages consécutifs fondus.
            std::stable_sort(chosen.begin(), chosen.end(), [&](size_t a, size_t b) {
                if (all[a].doc != all[b].doc) return all[a].doc < all[b].doc;
                return all[a].ordinal < all[b].ordinal;
            });
            std::vector<rag_excerpt> out;
            std::vector<long long> last_ordinal;
            for (size_t i : chosen)
            {
                const rag_passage& r = all[i];
                if (!out.empty() && out.back().doc == r.doc && last_ordinal.back() + 1 == r.ordinal)
                {
                    out.back().text += "\n" + r.text;
                    last_ordinal.back() = r.ordinal;
                    out.back().score = std::max(out.back().score, fused[i]);
                    out.back().passages.push_back(r.id);
                    continue;
                }
                rag_excerpt e;
                e.doc = r.doc; e.name = r.name; e.where = r.where; e.text = r.text;
                e.score = fused[i]; e.passages.push_back(r.id);
                out.push_back(e);
                last_ordinal.push_back(r.ordinal);
            }
            // Les documents dans l'ordre de leur meilleur extrait.
            std::unordered_map<std::string, double> best_of;
            for (const rag_excerpt& e : out) best_of[e.doc] = std::max(best_of.count(e.doc) ? best_of[e.doc] : 0.0, e.score);
            std::vector<std::string> order;
            for (auto& kv : best_of) order.push_back(kv.first);
            std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
                return best_of[a] > best_of[b];
            });
            std::unordered_map<std::string, int> rank_of;
            for (size_t k = 0; k < order.size(); ++k) rank_of[order[k]] = static_cast<int>(k);
            // Garder l'ordinal du premier passage de chaque extrait pour le tri final.
            std::vector<long long> first_ordinal(out.size());
            for (size_t k = 0; k < out.size(); ++k)
                first_ordinal[k] = out[k].passages.empty() ? 0 : [&] {
                    auto it = at.find(out[k].passages.front());
                    return it == at.end() ? 0LL : all[it->second].ordinal;
                }();
            std::vector<size_t> idx(out.size());
            for (size_t k = 0; k < idx.size(); ++k) idx[k] = k;
            std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                if (rank_of[out[a].doc] != rank_of[out[b].doc]) return rank_of[out[a].doc] < rank_of[out[b].doc];
                return first_ordinal[a] < first_ordinal[b];
            });
            std::vector<rag_excerpt> sorted;
            for (size_t k : idx) sorted.push_back(out[k]);
            return sorted;
        }

        // La fusion par rang réciproque : les passages dont le score dépasse floor, classés, reçoivent
        // 1/(60+rang) ajouté à fused (fidèle à rank() de rag.js).
        void reciprocal_rank(const std::vector<double>& scores, double floor, std::vector<double>& fused)
        {
            std::vector<size_t> order;
            for (size_t i = 0; i < scores.size(); ++i) if (scores[i] > floor) order.push_back(i);
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return scores[a] > scores[b]; });
            if (order.size() > 60) order.resize(60);
            for (size_t r = 0; r < order.size(); ++r) fused[order[r]] += 1.0 / (60.0 + r);
        }
    }

    // ----------------------------------------------------------------------------------------
    //  Les trois moteurs

    namespace
    {
        // Les scores par les mots (BM25), rangés par rang réciproque sur les passages proches du meilleur.
        void rank_words(const std::vector<rag_passage>& all, const std::string& question, std::vector<double>& fused)
        {
            const std::vector<sparse_row> sparse = sparse_of(all);
            const std::vector<double> words = bm25_scores(sparse, question);
            double best = 0;
            for (double w : words) best = std::max(best, w);
            if (best > 0) reciprocal_rank(words, std::max(0.0, words_share * best), fused);
        }

        // Les scores par le sens (cosinus du vecteur de la question), rangés sur les passages d'un
        // cosinus d'au moins min_meaning et à moins de meaning_spread du meilleur.  Sans vecteur de
        // question, ne fait rien.
        void rank_meaning(const std::vector<rag_passage>& all, const rag_search_options& options,
                          std::vector<double>& fused)
        {
            if (options.question_vector.empty()) return;
            const size_t d = options.question_vector.size();
            std::vector<double> meaning(all.size(), -2.0);
            bool any = false;
            for (size_t i = 0; i < all.size(); ++i)
            {
                if (all[i].vector.size() != d || all[i].model != options.model) continue;
                meaning[i] = cosine(all[i].vector, options.question_vector);
                any = true;
            }
            if (!any) return;
            double best = -1;
            for (double m : meaning) best = std::max(best, m);
            if (best < min_meaning) return;
            reciprocal_rank(meaning, std::max(min_meaning, best - meaning_spread) - 1e-9, fused);
        }

        class lexical_engine : public rag_engine
        {
        public:
            std::string name() const override { return "lexical"; }
            std::vector<rag_excerpt> search(const std::vector<rag_passage>& all, const std::string& question,
                                            const rag_search_options& options) const override
            {
                if (all.empty() || trim(question).empty()) return {};
                std::vector<double> fused(all.size(), 0.0);
                rank_words(all, question, fused);
                return assemble(all, fused, options, options.model);
            }
        };

        class dense_engine : public rag_engine
        {
        public:
            std::string name() const override { return "dense"; }
            std::vector<rag_excerpt> search(const std::vector<rag_passage>& all, const std::string& question,
                                            const rag_search_options& options) const override
            {
                if (all.empty() || trim(question).empty()) return {};
                std::vector<double> fused(all.size(), 0.0);
                rank_meaning(all, options, fused);
                return assemble(all, fused, options, options.model);
            }
        };

        class hybrid_engine : public rag_engine
        {
        public:
            std::string name() const override { return "hybrid"; }
            std::vector<rag_excerpt> search(const std::vector<rag_passage>& all, const std::string& question,
                                            const rag_search_options& options) const override
            {
                if (all.empty() || trim(question).empty()) return {};
                std::vector<double> fused(all.size(), 0.0);
                rank_words(all, question, fused);
                rank_meaning(all, options, fused);
                return assemble(all, fused, options, options.model);
            }
        };
    }

    std::unique_ptr<rag_engine> engine_for(const std::string& name)
    {
        if (name == "lexical") return std::unique_ptr<rag_engine>(new lexical_engine());
        if (name == "dense") return std::unique_ptr<rag_engine>(new dense_engine());
        return std::unique_ptr<rag_engine>(new hybrid_engine());  // hybrid par défaut (nom inconnu compris)
    }
}
