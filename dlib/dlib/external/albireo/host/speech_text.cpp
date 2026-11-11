// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les voix et le texte de la synthèse vocale, partagés par le service et albireo-speech.

#include "speech.h"

#include <algorithm>

namespace albireo
{

    namespace
    {
        std::string lower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        // Le point de code suivant du texte UTF-8 à i, en avançant i ; -1 pour une séquence invalide.
        long next_code_point(const std::string& s, size_t& i)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
            if (n == 0 || i + n > s.size())
            {
                ++i;
                return -1;
            }
            long cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
            for (size_t k = 1; k < n; ++k)
            {
                const unsigned char d = static_cast<unsigned char>(s[i + k]);
                if ((d >> 6) != 0x2)
                {
                    ++i;
                    return -1;
                }
                cp = (cp << 6) | (d & 0x3F);
            }
            // Les formes trop longues et les demi-codets sont refusés, comme les décodeurs des navigateurs les
            // refusent.
            const long least[] = { 0, 0, 0x80, 0x800, 0x10000 };
            if (cp < least[n] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
            {
                i += n;
                return -1;
            }
            i += n;
            return cp;
        }

        void append_utf8(std::string& out, long cp)
        {
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        // Une lettre des écritures latines que lisent les voix : Latin-1 et les blocs Latin Extended.
        bool latin_letter(long cp)
        {
            if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return true;
            if (cp >= 0xC0 && cp <= 0x24F) return cp != 0xD7 && cp != 0xF7;
            return false;
        }

        // Ce que devient un caractère dans le texte que lit une voix : lui-même, un remplaçant, ou une espace.
        std::string spoken(long cp)
        {
            if (latin_letter(cp) || (cp >= '0' && cp <= '9')) return std::string();
            switch (cp)
            {
            case '.': case ',': case ';': case ':': case '!': case '?': case '\'': case '"': case '-': case '(':
            case ')': case '/': case '%': case '+': case '=': case '$': case '\n': case 0xA3: case 0xB0: case 0x20AC:
            case 0xBF: case 0xA1:
                return std::string();
            case 0x2018: case 0x2019: case 0x02BC: return "'";
            case 0x201C: case 0x201D: case 0xAB: case 0xBB: return "\"";
            case 0x2013: case 0x2014: return ", ";
            case 0x2026: return "...";
            default: return " ";
            }
        }
    }

    const std::vector<speech_voice>& speech_voices()
    {
        // Le fork pour macOS ne parle que français.
        static const std::vector<speech_voice> voices = {
            { "fr-FR", "fr", "fr-FR_ta.bin", "fr-FR_nk0_sg.bin" },
        };
        return voices;
    }

    const speech_voice* find_voice(const std::string& name)
    {
        const std::string n = lower(name);
        for (const auto& v : speech_voices())
            if (lower(v.id) == n) return &v;
        for (const auto& v : speech_voices())
            if (v.language == n) return &v;
        return nullptr;
    }

    std::string speech_text(const std::string& text, size_t max_bytes)
    {
        std::string out;
        out.reserve(std::min(text.size(), max_bytes) + 4);
        size_t word = 0;
        bool space = true;
        for (size_t i = 0; i < text.size();)
        {
            const long cp = next_code_point(text, i);
            std::string piece;
            if (cp < 0) piece = " ";
            else
            {
                piece = spoken(cp);
                if (piece.empty()) append_utf8(piece, cp);
            }
            for (size_t k = 0; k < piece.size();)
            {
                const long c = next_code_point(piece, k);
                if (c == ' ' || c == '\n')
                {
                    // Un saut de ligne reste, les espaces qui le précèdent partent ; une suite d'espaces n'en fait
                    // qu'une.
                    if (c == '\n')
                    {
                        while (!out.empty() && out.back() == ' ') out.pop_back();
                        if (!out.empty() && out.back() != '\n') out += '\n';
                    }
                    else if (!space) out += ' ';
                    space = true;
                    word = 0;
                    continue;
                }
                const bool letter = latin_letter(c) || (c >= '0' && c <= '9');
                if (letter && ++word > 64)
                {
                    out += ' ';
                    word = 1;
                }
                if (!letter) word = 0;
                std::string bytes;
                append_utf8(bytes, c);
                if (out.size() + bytes.size() > max_bytes)
                {
                    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
                    return out;
                }
                out += bytes;
                space = false;
            }
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
        return out;
    }

}
