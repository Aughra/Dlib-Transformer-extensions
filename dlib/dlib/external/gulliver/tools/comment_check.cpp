// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Vérifie que seuls des commentaires ont changé entre une version de git (HEAD par défaut) et les fichiers du dossier
    de travail (Gulliver 7.25.0, à la place de macos/outils/comment_check.py ; outil de la traduction des commentaires).

        build/macos-gulliver/tools/comment_check [--rev REV] fichier…
        build/macos-gulliver/tools/comment_check --normalize fichier      # le code sans commentaires, pour vérifier

    Chaque fichier est réduit à son code, commentaires retirés et blancs resserrés, selon son type : C, C++,
    Objective-C++, Metal, JavaScript et CSS (commentaires de ligne et de bloc, chaînes, chaînes brutes R"(…)" du C++, expressions régulières
    du JavaScript), shell, Python et CMake (#, commentaires entre crochets de CMake), HTML (<!-- -->).  Code de sortie :
    le nombre de fichiers dont le code diffère ; pour chacun, les 80 caractères de part et d'autre du premier écart.
*/

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../common/platform.h"

namespace
{
    using text = std::u32string;

    text decode(const std::string& s)
    {
        text out;
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
            for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
            out += cp;
            i += n;
        }
        return out;
    }

    std::string encode(const text& s)
    {
        std::string out;
        for (const char32_t c : s)
        {
            if (c < 0x80) out += static_cast<char>(c);
            else if (c < 0x800)
            {
                out += static_cast<char>(0xc0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
            else if (c < 0x10000)
            {
                out += static_cast<char>(0xe0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
            else
            {
                out += static_cast<char>(0xf0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        return out;
    }

    // str.isspace() et \s de Python.
    bool space(char32_t c)
    {
        return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
               (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
    }

    // \w de Python, approché : lettres et chiffres ASCII, soulignement, et tout caractère non ASCII qui n'est pas un blanc
    // ni une ponctuation courante (les mots-clés cherchés sont ASCII).
    bool word(char32_t c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               (c >= 0xc0 && !space(c) && c != 0xd7 && c != 0xf7);
    }

    bool starts(const text& s, size_t i, const char* lit)
    {
        for (size_t k = 0; lit[k]; ++k)
            if (i + k >= s.size() || s[i + k] != static_cast<char32_t>(static_cast<unsigned char>(lit[k]))) return false;
        return true;
    }

    bool in_set(char32_t c, const char* set)
    {
        for (size_t k = 0; set[k]; ++k)
            if (c == static_cast<char32_t>(static_cast<unsigned char>(set[k]))) return true;
        return false;
    }

    // re.sub(r"\s+", " ", t)
    text squeeze(const text& t, bool tabs_and_spaces_only = false)
    {
        text out;
        bool run = false;
        for (const char32_t c : t)
        {
            const bool blank = tabs_and_spaces_only ? (c == ' ' || c == '\t') : space(c);
            if (blank)
            {
                if (!run) out += U' ';
                run = true;
            }
            else
            {
                out += c;
                run = false;
            }
        }
        return out;
    }

    // re.search(r"\b(return|typeof|case|in|of)\s*$", tail)
    bool ends_with_keyword(const text& tail)
    {
        size_t end = tail.size();
        while (end > 0 && space(tail[end - 1])) --end;
        for (const char* kw : {"return", "typeof", "case", "in", "of"})
        {
            const size_t n = std::char_traits<char>::length(kw);
            if (end < n) continue;
            const size_t at = end - n;
            if (!starts(tail, at, kw)) continue;
            if (at == 0 || !word(tail[at - 1])) return true;
        }
        return false;
    }

    text strip_c(const text& s, bool js)
    {
        std::vector<text> out;   // les morceaux écrits, comme la liste « out » de l'outil Python
        size_t i = 0;
        const size_t n = s.size();
        char32_t last = 0;   // le dernier caractère significatif du code, pour les expressions régulières du JavaScript
        auto piece = [&](size_t a, size_t b) { return s.substr(a, b > n ? std::string::npos : b - a); };
        while (i < n)
        {
            const char32_t c = s[i];
            if (starts(s, i, "//") && !(i > 0 && s[i - 1] == U':'))   // pas http://
            {
                const size_t j = s.find(U'\n', i);
                i = j == text::npos ? n : j;
                continue;
            }
            if (starts(s, i, "/*"))
            {
                const size_t j = s.find(U"*/", i + 2);
                i = j == text::npos ? n : j + 2;
                out.push_back(U" ");
                continue;
            }
            if (!js && starts(s, i, "R\""))
            {
                // R"([^(\s]{0,16})\(
                size_t k = i + 2, len = 0;
                while (k < n && len < 16 && s[k] != U'(' && !space(s[k]))
                {
                    ++k;
                    ++len;
                }
                if (k < n && s[k] == U'(')
                {
                    const text end = U")" + s.substr(i + 2, len) + U"\"";
                    size_t j = s.find(end, k + 1);
                    j = j == text::npos ? n : j + end.size();
                    out.push_back(piece(i, j));
                    i = j;
                    last = U'"';
                    continue;
                }
            }
            if (c == U'"' || c == U'\'' || (js && c == U'`'))
            {
                size_t j = i + 1;
                while (j < n && s[j] != c)
                {
                    if (s[j] == U'\\') ++j;
                    else if (s[j] == U'\n' && c != U'`') break;
                    ++j;
                }
                out.push_back(piece(i, j + 1));
                i = j + 1;
                last = c;
                continue;
            }
            if (js && c == U'/')
            {
                text tail;
                for (size_t k = out.size() >= 3 ? out.size() - 3 : 0; k < out.size(); ++k) tail += out[k];
                if (last == 0 || in_set(last, "(,=:[!&|?{};+-*%<>~^") || ends_with_keyword(tail))
                {
                    size_t j = i + 1;
                    bool in_class = false;
                    while (j < n && s[j] != U'\n')
                    {
                        if (s[j] == U'\\')
                        {
                            j += 2;
                            continue;
                        }
                        if (s[j] == U'[') in_class = true;
                        else if (s[j] == U']') in_class = false;
                        else if (s[j] == U'/' && !in_class) break;
                        ++j;
                    }
                    out.push_back(piece(i, j + 1));
                    i = j + 1;
                    last = U'/';
                    continue;
                }
            }
            out.push_back(text(1, c));
            if (!space(c)) last = c;
            ++i;
        }
        text joined;
        for (const text& p : out) joined += p;
        return joined;
    }

    text strip_hash(const text& s, const std::string& kind)
    {
        std::vector<text> lines;
        size_t from = 0;
        for (;;)
        {
            const size_t nl = s.find(U'\n', from);
            const text line = s.substr(from, nl == text::npos ? text::npos : nl - from);
            char32_t q = 0;
            size_t cut = line.size(), k = 0;
            while (k < line.size())
            {
                const char32_t ch = line[k];
                if (q)
                {
                    if (ch == U'\\')
                    {
                        k += 2;
                        continue;
                    }
                    if (ch == q) q = 0;
                }
                else if (ch == U'"' || ch == U'\'') q = ch;
                else if (ch == U'#' && (k == 0 || space(line[k - 1]) || kind != "sh"))
                {
                    if (kind == "sh" && k > 0 && (line[k - 1] == U'$' || line[k - 1] == U'{'))
                    {
                        ++k;
                        continue;
                    }
                    if (starts(line, 0, "#!") && k == 0) break;
                    cut = k;
                    break;
                }
                ++k;
            }
            text kept = line.substr(0, cut);
            while (!kept.empty() && space(kept.back())) kept.pop_back();
            lines.push_back(kept);
            if (nl == text::npos) break;
            from = nl + 1;
        }
        text out;
        bool first = true;
        for (const text& l : lines)
        {
            bool blank = true;
            for (const char32_t c : l) blank = blank && space(c);
            if (blank) continue;
            if (!first) out += U'\n';
            out += l;
            first = false;
        }
        return out;
    }

    // re.sub(open + ".*?" + close, " ", t, flags=re.S) pour des délimiteurs fixes.
    text drop_between(const text& t, const text& open, const text& close)
    {
        text out;
        size_t i = 0;
        for (;;)
        {
            const size_t a = t.find(open, i);
            if (a == text::npos) break;
            const size_t b = t.find(close, a + open.size());
            if (b == text::npos) break;
            out += t.substr(i, a - i) + U" ";
            i = b + close.size();
        }
        return out + t.substr(i);
    }

    // re.sub(r"#\[(=*)\[.*?\]\1\]", " ", t, flags=re.S) : les commentaires entre crochets de CMake.
    text drop_cmake_brackets(const text& t)
    {
        text out;
        size_t i = 0;
        while (i < t.size())
        {
            if (starts(t, i, "#["))
            {
                size_t k = i + 2;
                while (k < t.size() && t[k] == U'=') ++k;
                if (k < t.size() && t[k] == U'[')
                {
                    const text close = U"]" + t.substr(i + 2, k - i - 2) + U"]";
                    const size_t b = t.find(close, k + 1);
                    if (b != text::npos)
                    {
                        out += U' ';
                        i = b + close.size();
                        continue;
                    }
                }
            }
            out += t[i++];
        }
        return out;
    }

    std::string base_name(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    // os.path.splitext(path)[1]
    std::string extension(const std::string& path)
    {
        const std::string base = base_name(path);
        const size_t dot = base.rfind('.');
        return dot == std::string::npos || dot == 0 ? std::string() : base.substr(dot);
    }

    text normalize(const text& t, const std::string& path)
    {
        const std::string ext = extension(path), base = base_name(path);
        if (base == "CMakeLists.txt" || ext == ".cmake") return squeeze(strip_hash(drop_cmake_brackets(t), "cmake"));
        if (ext == ".py") return strip_hash(t, "py");
        if (ext == ".sh") return squeeze(strip_hash(t, "sh"), true);
        if (ext == ".html") return squeeze(drop_between(t, U"<!--", U"-->"));
        for (const char* e : {".cpp", ".h", ".hpp", ".c", ".inc", ".mm", ".metal", ".js", ".css", ".in", ".mjs"})
            if (ext == e) return squeeze(strip_c(t, ext == ".js" || ext == ".mjs"));
        return squeeze(t);
    }

    std::string output_of(const std::string& command)
    {
        std::string out;
        if (FILE* p = popen(command.c_str(), "r"))
        {
            char buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
            pclose(p);
        }
        return out;
    }

    std::string shell_quote(const std::string& s)
    {
        std::string out = "'";
        for (const char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return out + "'";
    }

    // Un extrait comme repr() de Python (approché : guillemets simples, \n, \t, \\ et \' échappés).
    std::string repr(const text& t)
    {
        std::string out = "'";
        for (const char32_t c : t)
        {
            if (c == U'\\') out += "\\\\";
            else if (c == U'\'') out += "\\'";
            else if (c == U'\n') out += "\\n";
            else if (c == U'\t') out += "\\t";
            else if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned>(c));
                out += buf;
            }
            else out += encode(text(1, c));
        }
        return out + "'";
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--normalize")
    {
        std::string data;
        if (!gulliver::read_file(args[1], data)) return 1;
        std::printf("%s", encode(normalize(decode(data), args[1])).c_str());
        return 0;
    }
    std::string rev = "HEAD";
    if (args.size() >= 2 && args[0] == "--rev")
    {
        rev = args[1];
        args.erase(args.begin(), args.begin() + 2);
    }
    int bad = 0;
    for (const std::string& path : args)
    {
        std::string rel = output_of("git ls-files --full-name " + shell_quote(path));
        while (!rel.empty() && (rel.back() == '\n' || rel.back() == ' ')) rel.pop_back();
        const std::string old = output_of("git show " + shell_quote(rev + ":" + rel) + " 2>/dev/null");
        std::string now;
        if (!gulliver::read_file(path, now))
        {
            std::fprintf(stderr, "comment_check: cannot read %s\n", path.c_str());
            return 255;
        }
        const text a = normalize(decode(old), path), b = normalize(decode(now), path);
        if (a != b)
        {
            ++bad;
            size_t k = 0;
            const size_t m = std::min(a.size(), b.size());
            while (k < m && a[k] == b[k]) ++k;
            const size_t from = k > 80 ? k - 80 : 0;
            std::printf("CODE CHANGED: %s\n  avant: %s\n  après: %s\n", path.c_str(), repr(a.substr(from, k + 80 - from)).c_str(),
                repr(b.substr(from, k + 80 - from)).c_str());
        }
    }
    std::printf("%zu fichier(s) vérifié(s), %d avec du code modifié\n", args.size(), bad);
    return bad;
}
