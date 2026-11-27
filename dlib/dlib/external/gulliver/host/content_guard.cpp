// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "content_guard.h"

#include <algorithm>
#include <cstdint>
#include <set>

namespace gulliver
{
    namespace
    {
        // Le point de code UTF-8 qui commence en s[i] et sa longueur ; faux pour un octet invalide.
        bool next_code(const std::string& s, size_t i, uint32_t& cp, size_t& len)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) { cp = c; len = 1; return true; }
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else return false;
            if (i + len > s.size()) return false;
            for (size_t k = 1; k < len; ++k)
            {
                const unsigned char d = static_cast<unsigned char>(s[i + k]);
                if ((d & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (d & 0x3F);
            }
            return true;
        }

        // Un caractère qui sert à cacher un texte : étiquettes, sélecteurs étendus, forçages de sens.
        bool hiding(uint32_t cp)
        {
            return (cp >= 0xE0000 && cp <= 0xE007F) || (cp >= 0xE0100 && cp <= 0xE01EF)
                || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069);
        }

        // Un caractère que le lecteur ne voit pas et que le texte n'a pas besoin de garder.
        bool invisible(uint32_t cp)
        {
            if (cp == '\n' || cp == '\t' || cp == '\r') return false;
            if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) return true;
            if (cp == 0x00AD || cp == 0x180E || cp == 0x200B || cp == 0x200E || cp == 0x200F) return true;
            if (cp >= 0x2060 && cp <= 0x2064) return true;
            if (cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB)) return true;
            return hiding(cp);
        }

        void add_sign(guard_report& r, const char* sign)
        {
            if (std::find(r.signs.begin(), r.signs.end(), sign) == r.signs.end()) r.signs.push_back(sign);
        }

        // La lettre latine de base d'un point de code accentué courant (é → e), 0 sinon.
        char fold(uint32_t cp)
        {
            if (cp < 0x80) return static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp);
            switch (cp)
            {
            case 0xE0: case 0xE1: case 0xE2: case 0xE4: case 0xC0: case 0xC2: return 'a';
            case 0xE7: case 0xC7: return 'c';
            case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xC8: case 0xC9: case 0xCA: return 'e';
            case 0xEE: case 0xEF: case 0xCE: return 'i';
            case 0xF4: case 0xF6: case 0xD4: return 'o';
            case 0xF9: case 0xFB: case 0xFC: case 0xD9: case 0xDB: return 'u';
            default: return 0;
            }
        }

        // Les mots du texte en minuscules sans accents, coupés sur tout ce qui n'est ni lettre ni chiffre.
        std::vector<std::string> plain_words(const std::string& text)
        {
            std::vector<std::string> words;
            std::string w;
            for (size_t i = 0; i < text.size();)
            {
                uint32_t cp = 0;
                size_t len = 1;
                if (!next_code(text, i, cp, len)) { cp = 0; len = 1; }
                i += len;
                const char c = fold(cp);
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { w += c; continue; }
                if (!w.empty()) { words.push_back(w); w.clear(); }
                if (words.size() > 2000000) break;
            }
            if (!w.empty()) words.push_back(w);
            return words;
        }

        const std::set<std::string>& verbs()
        {
            static const std::set<std::string> v{ "ignore", "ignorez", "ignorer", "ignores", "ignoring", "disregard",
                "forget", "oublie", "oubliez", "oublier", "override", "bypass", "contourne", "contournez", "outrepasse",
                "outrepassez" };
            return v;
        }

        const std::set<std::string>& nouns()
        {
            static const std::set<std::string> v{ "instruction", "instructions", "consigne", "consignes", "prompt",
                "rules", "regles", "directives", "guidelines" };
            return v;
        }

        const std::set<std::string>& qualifiers()
        {
            static const std::set<std::string> v{ "previous", "prior", "above", "earlier", "preceding", "all", "any",
                "your", "system", "precedentes", "precedente", "anterieures", "dessus", "toutes", "tous", "tes",
                "vos", "systeme" };
            return v;
        }
    }

    std::string guard_clean(const std::string& text, guard_report& report)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();)
        {
            uint32_t cp = 0;
            size_t len = 1;
            if (!next_code(text, i, cp, len))
            {
                out += text[i++];
                continue;
            }
            if (invisible(cp))
            {
                ++report.removed;
                if (hiding(cp)) ++report.hidden;
                // Un saut de ligne d'un autre système (U+0085, séparateurs) n'existe pas ici : on ne
                // retire que des caractères sans place à l'écran.
            }
            else
            {
                out.append(text, i, len);
            }
            i += len;
        }
        if (report.hidden) add_sign(report, "hidden");
        return out;
    }

    void guard_inspect(const std::string& text, guard_report& report)
    {
        // Le balisage d'un modèle de discussion ou des extraits de Gulliver.
        std::string low(text);
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        for (const char* mark : { "<|im_start|>", "<|im_end|>", "<|system|>", "<|user|>", "<|assistant|>",
                                  "<|start_header_id|>", "<|eot_id|>", "<|endoftext|>", "<start_of_turn>",
                                  "<end_of_turn>", "[inst]", "<<sys>>", "<|start_of_role|>", "</excerpt",
                                  "</excerpts", "</document" })
        {
            if (low.find(mark) != std::string::npos)
            {
                add_sign(report, "markup");
                break;
            }
        }
        // Une demande d'ignorer les consignes : un verbe, puis, à huit mots au plus, un nom de consigne,
        // avec entre eux ou juste après le nom un mot qui les désigne (précédentes, toutes, vos, previous, all…).
        const std::vector<std::string> w = plain_words(text);
        for (size_t i = 0; i < w.size(); ++i)
        {
            const bool verb = verbs().count(w[i])
                // « ne tiens pas compte des consignes »
                || (w[i] == "compte" && i > 0 && w[i - 1] == "pas");
            if (!verb) continue;
            bool qualified = false;
            for (size_t k = i + 1; k < w.size() && k <= i + 8; ++k)
            {
                if (qualifiers().count(w[k])) qualified = true;
                if (!nouns().count(w[k])) continue;
                // Le qualificatif vient aussi après le nom, en français : « les consignes précédentes ».
                for (size_t q = k + 1; q < w.size() && q <= k + 2 && !qualified; ++q)
                    qualified = qualifiers().count(w[q]) > 0;
                if (qualified || w[i] == "compte")
                {
                    add_sign(report, "instructions");
                    return;
                }
            }
        }
    }

    std::string guard_document(const std::string& text, guard_report& report)
    {
        std::string clean = guard_clean(text, report);
        guard_inspect(clean, report);
        return clean;
    }

    std::string guard_codes(const guard_report& report)
    {
        std::string s;
        for (const std::string& x : report.signs) s += (s.empty() ? "" : ",") + x;
        return s;
    }
}
