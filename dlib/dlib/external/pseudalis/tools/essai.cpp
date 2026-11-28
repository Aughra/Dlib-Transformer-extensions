// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les petits travaux du test de Pseudalis (tools/test_macos.sh), à la place de ses commandes Python (Pseudalis
    3.4.0, décision de l'humain du 28 novembre 2026 : tout en C++).  Un programme, une commande par besoin :

        essai get SOURCE CHEMIN [DÉFAUT]      une valeur d'un JSON (SOURCE : fichier ou « - »), écrite comme Python
        essai text-json FICHIER              {"text": contenu du fichier sans blancs de bord}
        essai name-json NOM                  {"name": NOM}
        essai restore-body                   {"text", "substitutions"} d'une réponse d'anonymisation lue sur l'entrée
        essai quote TEXTE                    urllib.parse.quote
        essai unquote                        urllib.parse.unquote de l'entrée, sans blancs de bord
        essai report EN-TÊTES                l'en-tête X-Pseudalis-Report d'une réponse, décodé
        essai session A B C TEXTE            la séance du relais : mêmes pseudonymes, valeurs rendues, rien sans séance
        essai record-of SHA ID               « id|removed|original_name » de la fiche, lue dans les fiches sur l'entrée
        essai tags-of ID                     « tags » de la fiche ID, ou « none »
        essai history-ok                     True si l'historique tient dans sa limite (/v1/info sur l'entrée)
        essai drive-state                    « scans idle » du lecteur (/v1/drive sur l'entrée)
        essai repeat K                       le gros texte « Note K de Jean Dupont… », 9 000 fois
        essai big-text                       un texte d'environ 360 Ko, 60 000 mots tirés au hasard (graine fixe)
        essai zip SORTIE stored|deflated NOM=FICHIER… NOM/…   une archive ZIP ; « NOM/ » est un répertoire
        essai zip-names ZIP                  les noms des entrées, une par ligne
        essai zip-check ANON EN-TÊTES        « ok » si l'archive du dossier est propre (noms, 3 entrées, 1 écartée)
        essai zip-extract ZIP DOSSIER AILLEURS   extrait les fichiers ; reste.txt va dans AILLEURS
        essai zip-album ZIP                  « noms|contenu de lettre.txt »

    L'écrivain et le lecteur de ZIP sont ceux de ce programme, avec la zlib de macOS : le test ne fabrique pas ses
    archives avec le code de Pseudalis qu'il vérifie.
*/

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <zlib.h>

namespace
{
    // ---- un JSON réduit à ce dont le test a besoin ----

    struct value
    {
        enum kind_t { null, boolean, number, string, array, object } kind = null;
        bool b = false;
        std::string text;   // le texte d'une chaîne, ou le nombre tel qu'écrit
        std::vector<value> items;
        std::vector<std::pair<std::string, value>> fields;

        const value& operator[](const std::string& k) const
        {
            static const value none;
            for (const auto& f : fields)
                if (f.first == k) return f.second;
            return none;
        }
        bool has(const std::string& k) const
        {
            for (const auto& f : fields)
                if (f.first == k) return true;
            return false;
        }
    };

    struct parser
    {
        const std::string& s;
        size_t i = 0;
        bool ok = true;

        void ws()
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        }

        static void put_utf8(std::string& out, uint32_t c)
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

        std::string str()
        {
            std::string out;
            ++i;
            while (i < s.size() && s[i] != '"')
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    const char e = s[++i];
                    if (e == 'n') out += '\n';
                    else if (e == 't') out += '\t';
                    else if (e == 'r') out += '\r';
                    else if (e == 'b') out += '\b';
                    else if (e == 'f') out += '\f';
                    else if (e == 'u' && i + 4 < s.size())
                    {
                        uint32_t c = static_cast<uint32_t>(std::stoul(s.substr(i + 1, 4), nullptr, 16));
                        i += 4;
                        if (c >= 0xd800 && c < 0xdc00 && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u')
                        {
                            const uint32_t lo = static_cast<uint32_t>(std::stoul(s.substr(i + 3, 4), nullptr, 16));
                            c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                            i += 6;
                        }
                        put_utf8(out, c);
                    }
                    else out += e;
                    ++i;
                }
                else out += s[i++];
            }
            if (i >= s.size()) ok = false;
            ++i;
            return out;
        }

        value parse()
        {
            value v;
            ws();
            if (i >= s.size())
            {
                ok = false;
                return v;
            }
            const char c = s[i];
            if (c == '{')
            {
                v.kind = value::object;
                ++i;
                ws();
                if (i < s.size() && s[i] == '}')
                {
                    ++i;
                    return v;
                }
                while (ok)
                {
                    ws();
                    if (i >= s.size() || s[i] != '"')
                    {
                        ok = false;
                        break;
                    }
                    const std::string k = str();
                    ws();
                    if (i >= s.size() || s[i] != ':')
                    {
                        ok = false;
                        break;
                    }
                    ++i;
                    v.fields.push_back({k, parse()});
                    ws();
                    if (i < s.size() && s[i] == ',') ++i;
                    else if (i < s.size() && s[i] == '}')
                    {
                        ++i;
                        break;
                    }
                    else ok = false;
                }
            }
            else if (c == '[')
            {
                v.kind = value::array;
                ++i;
                ws();
                if (i < s.size() && s[i] == ']')
                {
                    ++i;
                    return v;
                }
                while (ok)
                {
                    v.items.push_back(parse());
                    ws();
                    if (i < s.size() && s[i] == ',') ++i;
                    else if (i < s.size() && s[i] == ']')
                    {
                        ++i;
                        break;
                    }
                    else ok = false;
                }
            }
            else if (c == '"')
            {
                v.kind = value::string;
                v.text = str();
            }
            else if (s.compare(i, 4, "true") == 0)
            {
                v.kind = value::boolean;
                v.b = true;
                i += 4;
            }
            else if (s.compare(i, 5, "false") == 0)
            {
                v.kind = value::boolean;
                i += 5;
            }
            else if (s.compare(i, 4, "null") == 0) i += 4;
            else
            {
                v.kind = value::number;
                const size_t from = i;
                while (i < s.size() && std::strchr("+-0123456789.eE", s[i])) ++i;
                v.text = s.substr(from, i - from);
                if (v.text.empty()) ok = false;
            }
            return v;
        }
    };

    bool parse_json(const std::string& text, value& out)
    {
        parser p{text};
        out = p.parse();
        p.ws();
        return p.ok && p.i == text.size();
    }

    void quote_json(std::string& out, const std::string& s)
    {
        out += '"';
        for (const unsigned char c : s)
        {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else out += static_cast<char>(c);
        }
        out += '"';
    }

    std::string dump(const value& v)
    {
        std::string out;
        switch (v.kind)
        {
            case value::null: return "null";
            case value::boolean: return v.b ? "true" : "false";
            case value::number: return v.text;
            case value::string: quote_json(out, v.text); return out;
            case value::array:
                out = "[";
                for (size_t k = 0; k < v.items.size(); ++k) out += (k ? ", " : "") + dump(v.items[k]);
                return out + "]";
            case value::object:
                out = "{";
                for (size_t k = 0; k < v.fields.size(); ++k)
                {
                    if (k) out += ", ";
                    quote_json(out, v.fields[k].first);
                    out += ": " + dump(v.fields[k].second);
                }
                return out + "}";
        }
        return out;
    }

    // str() de Python sur une valeur lue par json.load.
    std::string python_str(const value& v)
    {
        if (v.kind == value::string) return v.text;
        if (v.kind == value::boolean) return v.b ? "True" : "False";
        if (v.kind == value::null) return "None";
        return dump(v);
    }

    bool truthy(const value& v)
    {
        switch (v.kind)
        {
            case value::null: return false;
            case value::boolean: return v.b;
            case value::number: return std::strtod(v.text.c_str(), nullptr) != 0;
            case value::string: return !v.text.empty();
            case value::array: return !v.items.empty();
            case value::object: return !v.fields.empty();
        }
        return false;
    }

    // ---- fichiers et textes ----

    std::string slurp(const std::string& path)
    {
        if (path == "-") return std::string(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        std::ifstream f(path, std::ios::binary);
        std::stringstream s;
        s << f.rdbuf();
        return s.str();
    }

    bool spit(const std::string& path, const std::string& data)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        return static_cast<bool>(f << data);
    }

    value input_json(const std::string& source = "-")
    {
        value v;
        if (!parse_json(slurp(source), v))
        {
            std::fprintf(stderr, "essai: not JSON\n");
            std::exit(1);
        }
        return v;
    }

    std::string strip(const std::string& s)
    {
        size_t a = 0, b = s.size();
        auto blank = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
        while (a < b && blank(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && blank(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    std::string lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // urllib.parse.quote (sûrs : lettres, chiffres, « _.-~ » et « / »).
    std::string quote(const std::string& s)
    {
        std::string out;
        char buf[4];
        for (const unsigned char c : s)
        {
            if (std::isalnum(c) || std::strchr("_.-~/", c)) out += static_cast<char>(c);
            else
            {
                std::snprintf(buf, sizeof(buf), "%%%02X", c);
                out += buf;
            }
        }
        return out;
    }

    std::string unquote(const std::string& s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(s[i + 2])))
            {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            }
            else out += s[i];
        }
        return out;
    }

    // Les en-têtes d'une réponse (curl -D) : la valeur de name, sans blancs de bord, ou "".
    std::string header(const std::string& headers_file, const std::string& name)
    {
        std::istringstream in(slurp(headers_file));
        std::string line, found;
        while (std::getline(in, line))
        {
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            if (lower(strip(line.substr(0, colon))) == lower(name)) found = strip(line.substr(colon + 1));
        }
        return found;
    }

    // ---- ZIP ----

    void le16(std::string& o, uint32_t v)
    {
        o += static_cast<char>(v & 0xff);
        o += static_cast<char>((v >> 8) & 0xff);
    }
    void le32(std::string& o, uint32_t v)
    {
        le16(o, v & 0xffff);
        le16(o, v >> 16);
    }
    uint32_t rd16(const std::string& s, size_t at)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8);
    }
    uint32_t rd32(const std::string& s, size_t at) { return rd16(s, at) | (rd16(s, at + 2) << 16); }

    struct zip_item
    {
        std::string name, data;
    };

    std::string raw_deflate(const std::string& data)
    {
        z_stream z{};
        deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        std::string out(deflateBound(&z, data.size()), '\0');
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        z.avail_in = static_cast<uInt>(data.size());
        z.next_out = reinterpret_cast<Bytef*>(&out[0]);
        z.avail_out = static_cast<uInt>(out.size());
        deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        deflateEnd(&z);
        return out;
    }

    std::string raw_inflate(const std::string& data, size_t size)
    {
        z_stream z{};
        inflateInit2(&z, -15);
        std::string out(size, '\0');
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        z.avail_in = static_cast<uInt>(data.size());
        z.next_out = reinterpret_cast<Bytef*>(&out[0]);
        z.avail_out = static_cast<uInt>(out.size());
        inflate(&z, Z_FINISH);
        inflateEnd(&z);
        return out;
    }

    // Une archive ZIP : chaque entrée stockée (0) ou compressée (8), un répertoire quand son nom finit par « / ».
    std::string write_zip(const std::vector<zip_item>& items, bool deflated)
    {
        std::string out, central;
        for (const zip_item& it : items)
        {
            const bool dir = !it.name.empty() && it.name.back() == '/';
            const uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(it.data.data()), static_cast<uInt>(it.data.size())));
            const uint32_t method = deflated && !dir ? 8 : 0;
            const std::string body = method == 8 ? raw_deflate(it.data) : it.data;
            const uint32_t offset = static_cast<uint32_t>(out.size());
            // L'en-tête local : version 2.0, drapeau UTF-8 (bit 11), date fixe (1er janvier 2026).
            const uint32_t flags = 0x0800, time = 0, date = ((2026 - 1980) << 9) | (1 << 5) | 1;
            out += "PK\x03\x04";
            le16(out, 20); le16(out, flags); le16(out, method); le16(out, time); le16(out, date);
            le32(out, crc); le32(out, static_cast<uint32_t>(body.size())); le32(out, static_cast<uint32_t>(it.data.size()));
            le16(out, static_cast<uint32_t>(it.name.size())); le16(out, 0);
            out += it.name + body;
            central += "PK\x01\x02";
            le16(central, 20); le16(central, 20); le16(central, flags); le16(central, method); le16(central, time);
            le16(central, date); le32(central, crc); le32(central, static_cast<uint32_t>(body.size()));
            le32(central, static_cast<uint32_t>(it.data.size())); le16(central, static_cast<uint32_t>(it.name.size()));
            le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0);
            le32(central, dir ? 0x10 : 0); le32(central, offset);
            central += it.name;
        }
        const uint32_t start = static_cast<uint32_t>(out.size());
        out += central;
        out += "PK\x05\x06";
        le16(out, 0); le16(out, 0); le16(out, static_cast<uint32_t>(items.size())); le16(out, static_cast<uint32_t>(items.size()));
        le32(out, static_cast<uint32_t>(central.size())); le32(out, start); le16(out, 0);
        return out;
    }

    // Les entrées d'une archive ZIP, par son répertoire central ; false si elle ne se lit pas.
    bool read_zip(const std::string& z, std::vector<zip_item>& out)
    {
        if (z.size() < 22) return false;
        size_t eocd = std::string::npos;
        for (size_t at = z.size() - 22;; --at)
        {
            if (z.compare(at, 4, "PK\x05\x06") == 0)
            {
                eocd = at;
                break;
            }
            if (at == 0 || z.size() - at > 22 + 65535) break;
        }
        if (eocd == std::string::npos) return false;
        const uint32_t count = rd16(z, eocd + 10);
        size_t at = rd32(z, eocd + 16);
        for (uint32_t k = 0; k < count; ++k)
        {
            if (at + 46 > z.size() || z.compare(at, 4, "PK\x01\x02") != 0) return false;
            const uint32_t method = rd16(z, at + 10), csize = rd32(z, at + 20), usize = rd32(z, at + 24);
            const uint32_t nlen = rd16(z, at + 28), xlen = rd16(z, at + 30), clen = rd16(z, at + 32), local = rd32(z, at + 42);
            zip_item it;
            it.name = z.substr(at + 46, nlen);
            at += 46 + nlen + xlen + clen;
            if (local + 30 > z.size()) return false;
            const size_t data = local + 30 + rd16(z, local + 26) + rd16(z, local + 28);
            if (data + csize > z.size()) return false;
            const std::string body = z.substr(data, csize);
            it.data = method == 8 ? raw_inflate(body, usize) : body;
            out.push_back(it);
        }
        return true;
    }

    std::vector<zip_item> zip_or_die(const std::string& path)
    {
        std::vector<zip_item> items;
        if (!read_zip(slurp(path), items))
        {
            std::fprintf(stderr, "essai: not a ZIP archive: %s\n", path.c_str());
            std::exit(1);
        }
        return items;
    }

    void make_dirs(const std::string& path)
    {
        for (size_t at = 1; (at = path.find('/', at)) != std::string::npos; ++at) mkdir(path.substr(0, at).c_str(), 0755);
        mkdir(path.c_str(), 0755);
    }

    std::string parent(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? std::string(".") : p.substr(0, s);
    }

    std::string base(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    int usage()
    {
        std::fprintf(stderr, "usage: essai COMMAND ... (see tools/essai.cpp)\n");
        return 2;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    std::vector<std::string> a(argv + 2, argv + argc);

    if (cmd == "get" && (a.size() == 2 || a.size() == 3))
    {
        value doc;
        if (!parse_json(slurp(a[0]), doc))
        {
            std::printf("%s\n", a.size() == 3 ? a[2].c_str() : "");
            return 0;
        }
        const value* v = &doc;
        std::stringstream path(a[1]);
        std::string key;
        while (std::getline(path, key, '.'))
        {
            if (v->kind == value::array && !key.empty() && key.find_first_not_of("0123456789") == std::string::npos)
            {
                static const value none;
                const size_t k = std::stoul(key);
                v = k < v->items.size() ? &v->items[k] : &none;
            }
            else v = &(*v)[key];
        }
        if (v->kind == value::null && a.size() == 3) std::printf("%s\n", a[2].c_str());
        else std::printf("%s\n", python_str(*v).c_str());
        return 0;
    }
    if (cmd == "text-json" && a.size() == 1)
    {
        std::string out = "{\"text\": ";
        quote_json(out, strip(slurp(a[0])));
        std::printf("%s}\n", out.c_str());
        return 0;
    }
    if (cmd == "name-json" && a.size() == 1)
    {
        std::string out = "{\"name\": ";
        quote_json(out, a[0]);
        std::printf("%s}\n", out.c_str());
        return 0;
    }
    if (cmd == "restore-body" && a.empty())
    {
        const value j = input_json();
        value subs = j["substitutions"];
        if (subs.kind == value::null) subs.kind = value::array;
        std::string out = "{\"text\": " + dump(j["text"]) + ", \"substitutions\": " + dump(subs) + "}";
        std::printf("%s\n", out.c_str());
        return 0;
    }
    if (cmd == "quote" && a.size() == 1)
    {
        std::printf("%s\n", quote(a[0]).c_str());
        return 0;
    }
    if (cmd == "unquote" && a.empty())
    {
        std::printf("%s\n", unquote(strip(slurp("-"))).c_str());
        return 0;
    }
    if (cmd == "report" && a.size() == 1)
    {
        // La première ligne « X-Pseudalis-Report: », comme la boucle de l'outil Python.
        std::istringstream in(slurp(a[0]));
        std::string line;
        while (std::getline(in, line))
            if (lower(line).rfind("x-pseudalis-report:", 0) == 0)
                std::printf("%s\n", unquote(strip(line.substr(line.find(':') + 1))).c_str());
        return 0;
    }
    if (cmd == "session" && a.size() == 4)
    {
        const value s1 = input_json(a[0]), s2 = input_json(a[1]), none = input_json(a[2]);
        std::vector<std::pair<std::string, std::string>> values;
        for (const value& v : s1["values"].items)
        {
            bool replaced = false;
            for (auto& kv : values)
                if (kv.first == v["substitute"].text)
                {
                    kv.second = v["value"].text;
                    replaced = true;
                }
            if (!replaced) values.push_back({v["substitute"].text, v["value"].text});
        }
        // Les substituts du plus long au plus court, pour qu'un substitut n'en coupe pas un autre.
        std::vector<std::pair<std::string, std::string>> order = values;
        std::stable_sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return x.first.size() > y.first.size(); });
        std::string back = s1["text"].text;
        for (const auto& [sub, val] : order)
            for (size_t at = 0; !sub.empty() && (at = back.find(sub, at)) != std::string::npos; at += val.size())
                back.replace(at, sub.size(), val);
        bool named = false;
        for (const auto& kv : values) named = named || kv.second == "Jean Dupont";
        const bool ok = s1["text"].text.find("Dupont") == std::string::npos && s1["text"].text == s2["text"].text && named &&
                        back == a[3] && !none.has("values") && none["text"].text != s1["text"].text;
        // a["text"][:80] : 80 caractères, pas 80 octets.
        const std::string& t = s1["text"].text;
        size_t cut = 0, chars = 0;
        while (cut < t.size() && chars < 80)
        {
            const unsigned char c = static_cast<unsigned char>(t[cut]);
            cut += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            ++chars;
        }
        std::printf("%s%zu values, %s\n", ok ? "session ok " : "session wrong ", values.size(), t.substr(0, cut).c_str());
        return 0;
    }
    if (cmd == "record-of" && a.size() == 2)
    {
        const value doc = input_json();
        for (const value& r : doc["records"].items)
            if (r["output_sha256"].text == a[0] || python_str(r["id"]) == a[1])
            {
                std::printf("%s|%s|%s\n", python_str(r["id"]).c_str(),
                    truthy(r["removed"]) ? python_str(r["removed"]).c_str() : "-", python_str(r["original_name"]).c_str());
                break;
            }
        return 0;
    }
    if (cmd == "tags-of" && a.size() == 1)
    {
        const value doc = input_json();
        for (const value& r : doc["records"].items)
            if (python_str(r["id"]) == a[0])
            {
                std::printf("%s\n", python_str(r["tags"]).c_str());
                return 0;
            }
        std::printf("none\n");
        return 0;
    }
    if (cmd == "history-ok" && a.empty())
    {
        const value doc = input_json();
        const value h = doc["history"];
        const double bytes = std::strtod(h["bytes"].text.c_str(), nullptr), limit = std::strtod(h["limit"].text.c_str(), nullptr);
        std::printf("%s\n", bytes <= limit ? "True" : "False");
        return 0;
    }
    if (cmd == "drive-state" && a.empty())
    {
        const value d = input_json();
        std::printf("%s %d\n", d.has("scans") ? python_str(d["scans"]).c_str() : "0", truthy(d["idle"]) ? 1 : 0);
        return 0;
    }
    if (cmd == "repeat" && a.size() == 1)
    {
        const std::string line = "Note " + a[0] + " de Jean Dupont, jean.dupont@exemple.fr\n";
        std::string out;
        out.reserve(line.size() * 9000);
        for (int k = 0; k < 9000; ++k) out += line;
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    if (cmd == "big-text" && a.empty())
    {
        // 5 000 mots de 3 à 9 lettres, puis 60 000 tirés parmi eux ; un générateur à graine fixe (le texte est le même
        // à chaque passage, sans être celui de l'outil Python : seule sa taille compte pour le test).
        uint64_t x = 5;
        auto next = [&]() {
            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<uint32_t>(x >> 33);
        };
        std::vector<std::string> words;
        for (int k = 0; k < 5000; ++k)
        {
            const int n = 3 + static_cast<int>(next() % 7);
            std::string w;
            for (int c = 0; c < n; ++c) w += static_cast<char>('a' + next() % 26);
            words.push_back(w);
        }
        std::string out = "Note de Jean Dupont, ";
        for (int k = 0; k < 60000; ++k) out += (k ? " " : "") + words[next() % words.size()];
        out += "\n";
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    if (cmd == "zip" && a.size() >= 2 && (a[1] == "stored" || a[1] == "deflated"))
    {
        std::vector<zip_item> items;
        for (size_t k = 2; k < a.size(); ++k)
        {
            const size_t eq = a[k].find('=');
            if (eq == std::string::npos) items.push_back({a[k], ""});
            else items.push_back({a[k].substr(0, eq), slurp(a[k].substr(eq + 1))});
        }
        return spit(a[0], write_zip(items, a[1] == "deflated")) ? 0 : 1;
    }
    if (cmd == "zip-names" && a.size() == 1)
    {
        for (const zip_item& it : zip_or_die(a[0])) std::printf("%s\n", it.name.c_str());
        return 0;
    }
    if (cmd == "zip-check" && a.size() == 2)
    {
        const std::vector<zip_item> items = zip_or_die(a[0]);
        value report;
        if (!parse_json(unquote(header(a[1], "x-pseudalis-report").empty() ? "{}" : header(a[1], "x-pseudalis-report")), report))
            report = value();
        const std::string name = unquote(header(a[1], "x-pseudalis-name"));
        bool clean = lower(name).find("dupont") == std::string::npos;
        std::string names;
        for (const zip_item& it : items)
        {
            clean = clean && lower(it.name).find("dupont") == std::string::npos;
            names += (names.empty() ? "" : ", ") + it.name;
        }
        const bool dropped = report["dropped"].kind == value::number && std::strtod(report["dropped"].text.c_str(), nullptr) == 1;
        if (clean && dropped && items.size() == 3) std::printf("ok\n");
        else std::printf("bad %s [%s] %s\n", name.c_str(), names.c_str(), dump(report).c_str());
        return 0;
    }
    if (cmd == "zip-extract" && a.size() == 3)
    {
        for (const zip_item& it : zip_or_die(a[0]))
        {
            if (!it.name.empty() && it.name.back() == '/') continue;
            const std::string path = (base(it.name) == "reste.txt" ? a[2] : a[1]) + "/" + it.name;
            make_dirs(parent(path));
            if (!spit(path, it.data)) return 1;
        }
        return 0;
    }
    if (cmd == "zip-album" && a.size() == 1)
    {
        std::string names, letter;
        for (const zip_item& it : zip_or_die(a[0]))
        {
            names += (names.empty() ? "" : ",") + it.name;
            if (it.name == "lettre.txt") letter = it.data;
        }
        std::printf("%s|%s\n", names.c_str(), letter.c_str());
        return 0;
    }
    return usage();
}
