// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#include "engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <dlib/base64.h>
#include <dlib/privacy.h>
#include <dlib/privacy/pii_recognizers.h>

#include "../common/file_kinds.h"
#include "archives.h"

/*
    Les modèles compilés dans le moteur : l'en-tête qu'écrit la commande embed de l'exemple
    slm_document_anonymizer_ex (examples/slm_document_anonymizer_ex.h), dont le build donne le
    chemin.
*/
#ifndef PSEUDALIS_MODELS_HEADER
#define PSEUDALIS_MODELS_HEADER "slm_document_anonymizer_ex.h"
#endif
#include PSEUDALIS_MODELS_HEADER

namespace pseudalis
{

    namespace
    {
        using namespace dlib;

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string base_name(const std::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string sha256_hex(const std::string& data)
        {
            const std::array<unsigned char, 32> d = sha256_digest(data);
            return to_hex(d.data(), d.size());
        }

        std::string decode_model(const std::string& text)
        {
            base64 coder;
            std::istringstream in(text);
            std::ostringstream packed;
            coder.decode(in, packed);
            return zlib_decompress(packed.str());
        }

        struct model_set
        {
            std::shared_ptr<const pii_sequence_tagger> tagger;
            std::shared_ptr<const pii_span_recognizer> span;
        };

        // Les modèles compilés dans le moteur, lus une seule fois pour tous les moteurs du processus.
        const model_set& shared_models()
        {
            static const model_set m = []() {
                model_set r;
                const std::string tagger_text = anonymizer_models::tagger();
                if (!tagger_text.empty())
                {
                    auto t = std::make_shared<pii_sequence_tagger>();
                    std::istringstream in(decode_model(tagger_text));
                    deserialize(*t, in);
                    r.tagger = t;
                }
                const std::string span_text = anonymizer_models::span_model();
                if (!span_text.empty())
                {
                    std::istringstream in(decode_model(span_text));
                    r.span = std::make_shared<pii_span_recognizer>(in);
                }
                return r;
            }();
            return m;
        }

        /*
            Un nom et son extension, ".docx" ou ".tar.gz", qui garde ses lettres : l'extension d'un
            type que Pseudalis traite, puisque la fin d'un dossier ou d'une adresse e-mail
            ("jean.dupont@example.fr") n'est pas une extension.
        */
        void split_extension(const std::string& base, std::string& stem, std::string& ext)
        {
            ext = pseudalis::kind_of_name(base) == name_kind::unsupported ? std::string() : name_extension(base);
            if (ext.size() >= base.size()) ext.clear();
            ext = base.substr(base.size() - ext.size());
            stem = base.substr(0, base.size() - ext.size());
        }

        // Un substitut dont les caractères que Windows refuse dans un nom sont remplacés par '_'.
        std::string windows_safe(std::string s)
        {
            for (char& c : s)
                if (static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr) c = '_';
            return s;
        }

        // Le nombre d'unités UTF-16 d'un texte UTF-8, qui borne un nom sous Windows.
        std::size_t utf16_units(const std::string& s)
        {
            std::size_t n = 0;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
            }
            return n;
        }

        /*
            Un nom protégé que Windows accepte : un radical qui désigne un périphérique (CON, NUL,
            COM1...) reçoit '_', et un nom de plus de 255 unités UTF-16 perd la fin de son radical,
            son extension gardée.  Le nom d'origine d'un fichier, que l'appelant enregistre, le
            rend en entier.
        */
        std::string windows_name(std::string stem, const std::string& ext)
        {
            std::string head = lower(stem.substr(0, stem.find('.')));
            while (!head.empty() && head.back() == ' ') head.pop_back();
            const bool device = head == "con" || head == "prn" || head == "aux" || head == "nul" ||
                (head.size() == 4 && (head.compare(0, 3, "com") == 0 || head.compare(0, 3, "lpt") == 0) &&
                 head[3] >= '1' && head[3] <= '9');
            if (device) stem.insert(stem.find('.') == std::string::npos ? stem.size() : stem.find('.'), "_");
            while (!stem.empty() && utf16_units(stem + ext) > 255)
            {
                stem.pop_back();
                while (!stem.empty() && (static_cast<unsigned char>(stem.back()) & 0xC0) == 0x80) stem.pop_back();
                if (!stem.empty() && static_cast<unsigned char>(stem.back()) >= 0xC0) stem.pop_back();
            }
            while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
            return stem.empty() ? "_" + ext : stem + ext;
        }

        json report_json(const document_report& r)
        {
            json j = json::object();
            json values = json::object();
            for (const auto& e : r.entities) values[e.first] = static_cast<unsigned long long>(e.second);
            j["values"] = values;
            j["total"] = static_cast<unsigned long long>(r.total_entities());
            json removed = json::array();
            for (const std::string& s : r.removed) removed.push_back(s);
            j["removed"] = removed;
            json notes = json::array();
            for (const std::string& s : r.notes) notes.push_back(s);
            j["notes"] = notes;
            j["tags_restored"] = static_cast<unsigned long long>(r.tags_restored);
            j["tags_rejected"] = static_cast<unsigned long long>(r.tags_rejected);
            j["dates_shifted"] = static_cast<unsigned long long>(r.dates_shifted);
            j["substitutes"] = static_cast<unsigned long long>(r.substitutions.size());
            j["substitutes_restored"] = static_cast<unsigned long long>(r.substitutes_restored);
            j["pictures_removed"] = static_cast<unsigned long long>(r.pictures_removed);
            j["dropped"] = json::array();
            return j;
        }

        // Ajoute à whole les comptes et les listes de part, une entrée d'une archive.
        void merge_report(json& whole, const json& part, const std::string& entry)
        {
            for (const auto& kv : part["values"].items())
                whole["values"][kv.first] =
                    static_cast<unsigned long long>(whole["values"][kv.first].as_int(0) + kv.second.as_int(0));
            for (const char* key : { "total", "tags_restored", "tags_rejected", "dates_shifted", "substitutes",
                                     "substitutes_restored", "pictures_removed" })
                whole[key] = static_cast<unsigned long long>(whole[key].as_int(0) + part[key].as_int(0));
            for (const char* key : { "removed", "notes", "dropped" })
                for (const json& v : part[key].elements())
                    whole[key].push_back(entry.empty() ? v.str() : entry + ": " + v.str());
        }

        /*
            Les marqueurs d'image d'un texte, sous forme de plages d'octets : "[Image@<16 digits>]"
            et "[Image@<16 digits> : <description>]", dont la description peut contenir des
            étiquettes, et un marqueur qu'un PDF a écrit abrégé par-dessus son cadre gris, finissant
            par "..." à la fin du texte.  Une espace d'un côté ou de l'autre d'un marqueur part une
            fois avec lui, pour que "voir [Image@...] ci-dessous" donne "voir ci-dessous".
        */
        std::vector<std::pair<std::size_t, std::size_t>> picture_markers(const std::string& text)
        {
            std::vector<std::pair<std::size_t, std::size_t>> out;
            std::size_t p = 0;
            while ((p = text.find("[Image@", p)) != std::string::npos)
            {
                std::size_t q = p + 7, digits = 0;
                while (q < text.size() && digits < 16 &&
                       ((text[q] >= '0' && text[q] <= '9') || (text[q] >= 'a' && text[q] <= 'f')))
                {
                    ++q;
                    ++digits;
                }
                std::size_t end = std::string::npos;
                if (digits == 16 && q < text.size() && text[q] == ']')
                {
                    end = q + 1;
                }
                else if (digits == 16 && text.compare(q, 3, " : ") == 0)
                {
                    // La description, jusqu'au crochet qui ferme le marqueur, ses étiquettes lues
                    // en entier.
                    for (q += 3; q < text.size() && q - p < 8192;)
                    {
                        if (text.compare(q, 2, "[[") == 0)
                        {
                            const std::size_t close = text.find("]]", q + 2);
                            if (close == std::string::npos || close - q > 1024) break;
                            q = close + 2;
                            continue;
                        }
                        if (text[q] == ']') { end = q + 1; break; }
                        if (text[q] == '[') break;
                        ++q;
                    }
                    // Écrit abrégé par-dessus le cadre gris d'un PDF : "...", puis la fin du texte.
                    if (end == std::string::npos)
                    {
                        std::size_t last = text.size();
                        while (last > p && (text[last - 1] == ' ' || text[last - 1] == '\n' || text[last - 1] == '\r'))
                            --last;
                        if (last - p < 8192 && last >= p + 3 && text.compare(last - 3, 3, "...") == 0 &&
                            text.find('[', p + 1) == std::string::npos)
                            end = last;
                    }
                }
                if (end == std::string::npos)
                {
                    p += 7;
                    continue;
                }
                // Une espace part avec le marqueur : celle d'après, ou celle d'avant en fin de ligne.
                std::size_t b = p;
                const bool space_before = b > 0 && text[b - 1] == ' ';
                const bool line_before = b == 0 || text[b - 1] == '\n' || text[b - 1] == '\r';
                const bool space_after = end < text.size() && text[end] == ' ';
                const bool line_after = end == text.size() || text[end] == '\n' || text[end] == '\r';
                if (space_after && (space_before || line_before)) ++end;
                else if (space_before && line_after) --b;
                out.push_back(std::make_pair(b, end));
                p = end;
            }
            return out;
        }

        // Indique si un texte est un marqueur d'image seul, le fichier texte d'une image.
        bool marker_alone(const std::string& text)
        {
            std::size_t b = 0, e = text.size();
            if (e >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) b = 3;
            while (b < e && (text[b] == ' ' || text[b] == '\r' || text[b] == '\n' || text[b] == '\t')) ++b;
            while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\r' || text[e - 1] == '\n' || text[e - 1] == '\t'))
                --e;
            if (e - b < 24 || e - b > 65536) return false;
            const std::vector<std::pair<std::size_t, std::size_t>> found = picture_markers(text.substr(b, e - b));
            return found.size() == 1 && found[0].first == 0 && found[0].second == e - b;
        }

        /*
            Le nom d'une entrée pas encore pris parmi names : "a.png.txt", puis "a.png (2).txt"...
            next retient, pour chaque nom, le numéro à essayer d'abord, pour qu'une archive qui
            répète un nom des milliers de fois ne soit pas reparcourue pour chacun d'eux.
        */
        std::string free_name(const std::string& name, std::set<std::string>& names, std::map<std::string, int>& next)
        {
            std::string candidate = name;
            const size_t dot = name.rfind('.');
            int& k = next[lower(name)];
            if (k < 2) k = 2;
            for (; names.count(lower(candidate)); ++k)
                candidate = dot == std::string::npos
                    ? name + " (" + std::to_string(k) + ")"
                    : name.substr(0, dot) + " (" + std::to_string(k) + ")" + name.substr(dot);
            names.insert(lower(candidate));
            return candidate;
        }

        /*
            Les octets que les archives et les documents Office d'un même fichier peuvent
            décompresser, tous niveaux confondus : 64 fois le fichier, de 16 Mo à 2 Go.  La mémoire
            et le temps du traitement croissent avec eux : un petit fichier qui contient des
            centaines de mégaoctets de texte est refusé plutôt que lu pendant des heures.
        */
        uint64_t decompression_budget(size_t file_size)
        {
            const uint64_t wanted = static_cast<uint64_t>(file_size) * 64;
            return std::min<uint64_t>(2048ull << 20, std::max<uint64_t>(16ull << 20, wanted));
        }

        // La famille d'un document d'après son contenu : un texte, un document Office, un PDF, ou none.
        document_family family_of(document_format f)
        {
            switch (f)
            {
                case document_format::docx:
                case document_format::xlsx:
                case document_format::pptx: return document_family::office;
                case document_format::pdf: return document_family::pdf;
                case document_format::text:
                case document_format::delimited:
                case document_format::markup: return document_family::text;
                default: return document_family::none;
            }
        }

        // Les classes des octets pour les encodages, qu'aucune locale ne modifie.
        enum : unsigned char { base64_class = 1, base64url_class = 2, hex_class = 4, alnum_class = 8 };

        const unsigned char* char_classes()
        {
            struct table
            {
                unsigned char of[256];
                table()
                {
                    for (int c = 0; c < 256; ++c)
                    {
                        const bool digit = c >= '0' && c <= '9';
                        const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
                        const bool hex = digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                        of[c] = static_cast<unsigned char>((digit || alpha || c == '+' || c == '/' || c == '=' ?
                                                            base64_class : 0) |
                                                           (c == '-' || c == '_' ? base64url_class : 0) |
                                                           (hex ? hex_class : 0) | (digit || alpha ? alnum_class : 0));
                    }
                }
            };
            static const table t;
            return t.of;
        }

        // Un caractère de base64, qui comprend aussi les chiffres hexadécimaux, ou de base64url avec url.
        bool base64_char(unsigned char c, bool url = true)
        {
            return (char_classes()[c] & (url ? base64_class | base64url_class : base64_class)) != 0;
        }

        bool alnum_char(unsigned char c)
        {
            return (char_classes()[c] & alnum_class) != 0;
        }

        unsigned char ascii_lower(unsigned char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c;
        }

        // Indique si text contient needle, écrit en minuscules, en i, sans tenir compte de la casse.
        bool at_without_case(const std::string& text, size_t i, const char* needle)
        {
            for (; *needle; ++needle, ++i)
                if (i >= text.size() || ascii_lower(static_cast<unsigned char>(text[i])) != *needle) return false;
            return true;
        }

        // La fin de la ligne de text qui commence en b : son premier '\r' ou '\n', ou la fin du texte.
        size_t line_end(const std::string& text, size_t b)
        {
            const char* const s = text.data();
            const size_t n = text.size();
            while (b < n && s[b] != '\n' && s[b] != '\r') ++b;
            return b;
        }

        // Le début de la ligne qui suit une fin de ligne en e, "\r\n", '\n' ou '\r', ou la fin du texte.
        size_t after_end(const std::string& text, size_t e)
        {
            if (e >= text.size()) return text.size();
            return text[e] == '\r' && e + 1 < text.size() && text[e + 1] == '\n' ? e + 2 : e + 1;
        }

        size_t next_line(const std::string& text, size_t b)
        {
            return after_end(text, line_end(text, b));
        }

        /*
            Une variété de caractères au sein d'une même suite : un bloc de base64 en contient des
            dizaines, une ligne de tirets ou de signes égal un seul.  begin() ouvre une suite, add()
            compte un caractère.
        */
        class variety
        {
        public:
            void begin()
            {
                ++run_;
                count_ = 0;
            }
            size_t add(unsigned char c)
            {
                if (seen_[c] != run_)
                {
                    seen_[c] = run_;
                    ++count_;
                }
                return count_;
            }

        private:
            size_t seen_[256] = {};
            size_t run_ = 1;
            size_t count_ = 0;
        };

        /*
            Indique si la ligne [b, e) donne une valeur de LDIF en base64 : "cn:: SMOpbMOobmUgRHVwcsOp",
            le nom de l'attribut, deux deux-points, des espaces ou aucune, puis du base64 jusqu'à la
            fin de la ligne, là où une valeur longue se replie.
        */
        bool ldif_base64(const std::string& text, size_t b, size_t e)
        {
            size_t i = b;
            if (i >= e || !alnum_char(static_cast<unsigned char>(text[i])) || (text[i] >= '0' && text[i] <= '9'))
                return false;
            while (i < e && (alnum_char(static_cast<unsigned char>(text[i])) || text[i] == '-' || text[i] == ';')) ++i;
            if (e - i < 2 || text.compare(i, 2, "::") != 0) return false;
            i += 2;
            while (i < e && text[i] == ' ') ++i;
            const size_t value = i;
            while (i < e && base64_char(static_cast<unsigned char>(text[i]), false)) ++i;
            return i == e && e - value >= 4;
        }

        // Indique si la ligne qui commence en b est une ligne de uuencode : son caractère de longueur,
        // puis ses groupes.
        bool uuencode_body(const std::string& text, size_t b)
        {
            const size_t e = line_end(text, b);
            if (b >= e) return false;
            const int count = static_cast<unsigned char>(text[b]) - 32;
            return count >= 1 && count <= 45 && e - b >= 1 + static_cast<size_t>((count + 2) / 3 * 4);
        }

        // Le début du contenu de la ligne [b, e), après ses marques d'ordre des octets, blancs, sauts de
        // page et '>'.
        size_t line_content(const std::string& text, size_t b, size_t e)
        {
            for (;;)
            {
                if (e - b >= 3 && text.compare(b, 3, "\xEF\xBB\xBF") == 0) b += 3;
                else if (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\f' || text[b] == '>')) ++b;
                else return b;
            }
        }

        /*
            Indique si une suite de 24 caractères de base64 ou plus, mêlant majuscules, minuscules
            et chiffres, s'ouvre dans les 512 octets de text à partir de i : le corps d'un bloc
            blindé, qui suit son en-tête.
        */
        bool base64_follows(const std::string& text, size_t i)
        {
            const unsigned char* const cls = char_classes();
            const size_t end = std::min(text.size(), i + 512);
            size_t run = 0;
            bool capital = false, small = false, digit = false;
            for (; i < end; ++i)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (!(cls[c] & base64_class))
                {
                    run = 0;
                    capital = small = digit = false;
                    continue;
                }
                capital = capital || (c >= 'A' && c <= 'Z');
                small = small || (c >= 'a' && c <= 'z');
                digit = digit || (c >= '0' && c <= '9');
                if (++run >= 24 && capital && small && digit) return true;
            }
            return false;
        }

        // Indique si la ligne qui commence en b, son contenu en c, ouvre un bloc de uuencode suivi d'une de
        // ses lignes.
        bool uuencode_begin(const std::string& text, size_t b, size_t c, size_t e)
        {
            if (e - c < 10 || text.compare(c, 6, "begin ") != 0) return false;
            size_t i = c + 6;
            while (i < e && text[i] >= '0' && text[i] <= '7') ++i;
            return (i - c == 9 || i - c == 10) && i + 1 < e && text[i] == ' ' &&
                   uuencode_body(text, next_line(text, b));
        }

        /*
            Indique si text contient en i, dans une ligne dont le contenu commence en content et qui
            finit en e, une ouverture qui cache ce qui suit, y compris dans une chaîne d'un
            programme : l'en-tête d'un bloc blindé par PEM ou OpenPGP ("-----BEGIN CERTIFICATE-----",
            "-----BEGIN PGP MESSAGE, PART 1/2-----"), que son corps doit suivre quand il est au milieu
            d'une ligne, un document RTF ("{\rtf", ou "{\\rtf" échappé), ou un bloc de base64 pour
            uudecode ("begin-base64 644 ").  Un programme qui ne fait que nommer l'en-tête d'un bloc,
            sous forme de constante, n'est pas un bloc.
        */
        bool encoded_opening(const std::string& text, size_t i, size_t content, size_t e)
        {
            const char c = text[i];
            if (c == '{') return text.compare(i, 5, "{\\rtf") == 0 || text.compare(i, 6, "{\\\\rtf") == 0;
            if (c == '-')
            {
                if (e - i < 17 || text.compare(i, 11, "-----BEGIN ") != 0) return false;
                // Le libellé : des caractères imprimables jusqu'à "-----".
                size_t j = i + 11;
                while (j < e && j - i < 100 && text.compare(j, 5, "-----") != 0 &&
                       static_cast<unsigned char>(text[j]) >= 0x20 && static_cast<unsigned char>(text[j]) < 0x7F)
                    ++j;
                if (j == i + 11 || e - j < 5 || text.compare(j, 5, "-----") != 0) return false;
                return i == content || base64_follows(text, j + 5);
            }
            if (c == 'b')
            {
                if (e - i < 17 || text.compare(i, 13, "begin-base64 ") != 0) return false;
                size_t j = i + 13;
                while (j < e && text[j] >= '0' && text[j] <= '7') ++j;
                return j - i >= 16 && j - i <= 17 && j < e && text[j] == ' ';
            }
            return false;
        }

        /*
            Indique si le nom de l'en-tête Content-Transfer-Encoding, qui finit en i, est suivi d'un
            deux-points et d'une valeur qui cache le texte : base64, quoted-printable ou uuencode.  La
            valeur peut se trouver sur la ligne suivante, l'en-tête étant replié.
        */
        bool transfer_encoding(const std::string& text, size_t i)
        {
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
            if (i >= text.size() || text[i] != ':') return false;
            ++i;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
            return at_without_case(text, i, "base64") || at_without_case(text, i, "quoted-printable") ||
                   at_without_case(text, i, "x-uuencode") || at_without_case(text, i, "uuencode") ||
                   at_without_case(text, i, "x-uue");
        }

        /*
            Indique si le ';' en i, dans une ligne qui finit en e, donne l'encodage d'une valeur d'une
            vCard ou d'un iCalendar : ";ENCODING=b:", ";ENCODING=QUOTED-PRINTABLE;", ou le paramètre nu
            de vCard 2.1, ";QUOTED-PRINTABLE:" ou ";BASE64:".
        */
        bool value_encoding(const std::string& text, size_t i, size_t e)
        {
            const bool named = at_without_case(text, i + 1, "encoding=");
            const size_t v = named ? i + 10 : i + 1;
            for (const char* x : { "quoted-printable", "base64", "b" })
            {
                const size_t end = v + std::strlen(x);
                if (end >= e || !at_without_case(text, v, x)) continue;
                if (named ? text[end] == ':' || text[end] == ';' : text[end] == ':' && x[1] != '\0') return true;
            }
            return false;
        }

        // La fin de l'ouverture d'un mot encodé en i, "=?UTF-8?B?" ou "=?utf-8*fr?q?", ou npos.
        size_t encoded_word_opening(const std::string& text, size_t i, size_t e)
        {
            size_t j = i + 2;
            while (j < e && j - i - 2 <= 60 &&
                   (alnum_char(static_cast<unsigned char>(text[j])) || text[j] == '-' || text[j] == '_' ||
                    text[j] == '*'))
                ++j;
            if (j == i + 2 || e - j < 3 || text[j] != '?' || text[j + 2] != '?') return std::string::npos;
            const unsigned char x = ascii_lower(static_cast<unsigned char>(text[j + 1]));
            return x == 'b' || x == 'q' ? j + 3 : std::string::npos;
        }

        /*
            Indique si un texte est, ou contient, un format qui écrit ses valeurs encodées : un
            document RTF, une partie d'un message en base64, quoted-printable ou uuencode, un mot
            encodé d'un en-tête ("=?UTF-8?B?...?="), une vCard ou un iCalendar, dont les lignes se
            replient et dont les valeurs peuvent être encodées, l'encodage d'une valeur de vCard
            ailleurs, du LDIF avec des valeurs en base64, un bloc de uuencode ou d'ASCII85, un bloc
            blindé par PEM ou OpenPGP ("-----BEGIN"), y compris dans une chaîne d'un programme, un
            programme PostScript ou un export du registre de Windows.  Ses valeurs resteraient
            lisibles une fois décodées.  Chaque ligne est lue une fois, sans tenir compte de ses
            marques d'ordre des octets, de ses espaces et de ses marques de citation.
        */
        bool encoded_format(const std::string& text)
        {
            size_t start = 0;
            while (text.compare(start, 3, "\xEF\xBB\xBF") == 0) start += 3;
            while (start < text.size() && (text[start] == ' ' || (text[start] >= '\t' && text[start] <= '\r')))
                ++start;
            static const char* const openings[] = { "%!ps", "windows registry editor version", "regedit4" };
            for (const char* o : openings)
                if (at_without_case(text, start, o)) return true;
            bool ldif_dn = false, ldif_value = false;
            long ascii85 = -1;          // les caractères du bloc d'ASCII85 que "<~" a ouvert
            for (size_t b = 0, e = 0; b < text.size(); b = after_end(text, e))
            {
                e = line_end(text, b);
                const size_t c = line_content(text, b, e);
                // Une vCard ou un iCalendar, y compris dans un message.
                if (at_without_case(text, c, "begin:vcard") || at_without_case(text, c, "begin:vcalendar")) return true;
                if (uuencode_begin(text, b, c, e)) return true;
                // LDIF : une valeur en base64 dans un fichier d'entrées, qui s'ouvrent par "dn:".
                ldif_dn = ldif_dn || at_without_case(text, c, "dn:");
                ldif_value = ldif_value || ldif_base64(text, c, e);
                if (ldif_dn && ldif_value) return true;
                size_t opened = std::string::npos;      // la fin de l'ouverture d'un mot encodé sur cette ligne
                bool base64_word = false;               // son encodage : base64, sinon quoted-printable
                for (size_t i = b; i < e; ++i)
                {
                    const char x = text[i];
                    if ((x == '-' || x == '{' || x == 'b') && encoded_opening(text, i, c, e)) return true;
                    // L'en-tête d'un encodage de transfert, y compris dans une ligne citée, sa valeur
                    // peut-être sur une ligne repliée.
                    if ((x == 'c' || x == 'C') && e - i >= 25 &&
                        at_without_case(text, i, "content-transfer-encoding") && transfer_encoding(text, i + 25))
                        return true;
                    if (x == ';' && value_encoding(text, i, e)) return true;
                    if (ascii85 >= 0)
                    {
                        if ((x >= '!' && x <= 'u') || x == 'z')
                        {
                            if (++ascii85 >= 64) return true;
                            continue;
                        }
                        ascii85 = -1;
                    }
                    if (x == '<' && i + 1 < e && text[i + 1] == '~')
                    {
                        ascii85 = 0;
                        ++i;
                    }
                    else if (x == '=' && i + 1 < e && text[i + 1] == '?' && opened == std::string::npos)
                    {
                        const size_t end = encoded_word_opening(text, i, e);
                        if (end != std::string::npos)
                        {
                            opened = end;
                            base64_word = ascii_lower(static_cast<unsigned char>(text[end - 2])) == 'b';
                            i = end - 1;
                        }
                    }
                    else if (opened != std::string::npos)
                    {
                        // Le mot finit par "?=" après au moins deux caractères de son encodage.
                        if (x == '?' && i + 1 < e && text[i + 1] == '=' && i - opened >= 2) return true;
                        const bool valid = base64_word ? base64_char(static_cast<unsigned char>(x), false)
                                                       : x != ' ' && x != '\t' && x != '?';
                        if (!valid || i - opened > 1000) opened = std::string::npos;
                    }
                }
            }
            return false;
        }

        /*
            Indique si un texte contient des octets écrits en hexadécimal, un vidage ou un tableau :
            64 d'affilée avec 8 lettres ou marques au milieu, ou 4 lignes d'affilée de 8 octets ou
            plus, en au moins 4 tokens chacune, avec 4 lettres ou marques au milieu ("4d 5a 90 00",
            "0x41, 0x42", "43,00,3a,00", "fffe 4a00 6500" de xxd, "\x4a\x65" dans une chaîne).  Un
            octet fait deux chiffres ; dans une ligne d'un vidage, qui s'ouvre par son décalage, un
            groupe de 4 ou 8 entre des espaces compte ses octets ; une marque, "0x" ou "\x", ou le
            décalage de xxd, compte comme une lettre.  L'empreinte d'une clé, sur une ligne, et les
            listes de points de code Unicode passent ; les octets écrits avec des chiffres seuls, sans
            lettre ni marque au milieu, passent pour un tableau de nombres.
        */
        bool hex_bytes(const std::string& text)
        {
            const unsigned char* const cls = char_classes();
            const unsigned char* const d = reinterpret_cast<const unsigned char*>(text.data());
            auto hex = [&](size_t k) { return (cls[d[k]] & hex_class) != 0; };
            auto separator = [](unsigned char c) { return c == ' ' || c == ',' || c == ':' || c == '-' || c == '\t'; };
            size_t rows = 0, rows_marked = 0;       // les lignes d'affilée de 8 octets ou plus, leurs octets marqués
            for (size_t b = 0, e = 0; b < text.size(); b = after_end(text, e))
            {
                e = line_end(text, b);
                // Les groupes de 4 ou 8 chiffres ne comptent que dans les lignes d'un vidage, qui s'ouvrent par
                // un décalage de 6 à 8.
                size_t k = b;
                while (k < e && (d[k] == ' ' || d[k] == '\t')) ++k;
                const size_t offset = k;
                while (k < e && hex(k)) ++k;
                const bool dump = k - offset >= 6 && k - offset <= 8 && k < e && (d[k] == ':' || d[k] == ' ');
                // Le décalage de xxd, "00000010:", marque sa ligne comme le ferait une lettre.
                const bool colon = dump && d[k] == ':';
                size_t best = 0, best_tokens = 0, best_marked = 0, bytes = 0, tokens = 0, marked = 0, i = b;
                // 8 octets et leurs séparateurs occupent au moins 16 caractères.
                while (e - b >= 16 && i < e)
                {
                    size_t from = i, to = i;
                    bool mark = false;
                    if (d[i] == '\\' && e - i >= 4 && (d[i + 1] == 'x' || d[i + 1] == 'X') && hex(i + 2) && hex(i + 3))
                    {
                        // Un octet échappé, qui n'a pas besoin de séparateur.
                        from = i + 2;
                        to = i + 4;
                        mark = true;
                    }
                    else if (hex(i) && (i == b || !(cls[d[i - 1]] & alnum_class)))
                    {
                        if (d[i] == '0' && e - i > 2 && (d[i + 1] == 'x' || d[i + 1] == 'X') && hex(i + 2))
                        {
                            from = i + 2;
                            mark = true;
                        }
                        to = from;
                        while (to < e && to - from < 9 && hex(to)) ++to;
                        const unsigned char after = to < e ? d[to] : ' ';
                        const bool spaced = after == ' ' || after == '\t';
                        if (to - from == 2 ? !separator(after)
                                           : mark || !dump || !spaced || (to - from != 4 && to - from != 8))
                            to = from;
                    }
                    if (to == from)
                    {
                        bytes = tokens = marked = 0;
                        ++i;
                        continue;
                    }
                    bool letter = mark;
                    for (size_t j = from; j < to; ++j) letter = letter || d[j] > '9';
                    bytes += (to - from) / 2;
                    ++tokens;
                    if (letter) ++marked;
                    if (bytes >= 64 && marked >= 8) return true;
                    if (bytes > best)
                    {
                        best = bytes;
                        best_tokens = tokens;
                        best_marked = marked;
                    }
                    i = to;
                    while (i < e && separator(d[i])) ++i;
                }
                if (best >= 8 && best_tokens >= 4)
                {
                    ++rows;
                    rows_marked += best_marked + (colon ? 1 : 0);
                }
                else
                    rows = rows_marked = 0;
                if (rows >= 4 && rows_marked >= 4) return true;
            }
            return false;
        }

        // Les caractères de [from, to) de d, hors barres obliques inverses des échappements, avec autant de
        // caractères différents que kinds en compte.
        size_t run_variety(const unsigned char* d, size_t from, size_t to, variety& kinds)
        {
            kinds.begin();
            size_t count = 0;
            for (size_t k = from; k < to; ++k)
                if (d[k] != '\\') count = kinds.add(d[k]);
            return count;
        }

        /*
            Indique si une suite de base64 de length caractères en [from, to) de d peut être une
            ligne d'un bloc encodé : 60 caractères ou plus, mêlant majuscules, minuscules et
            chiffres, dont au moins 16 différents, ou des chiffres hexadécimaux seuls, dont au moins
            10 différents.  Une ligne de tirets, un mot, un chemin sans majuscules ni chiffres n'en
            est pas une.
        */
        bool block_run(const unsigned char* d, size_t from, size_t to, size_t length, variety& kinds)
        {
            if (length < 60) return false;
            const unsigned char* const cls = char_classes();
            bool capital = false, small = false, digit = false, hex = true;
            for (size_t k = from; k < to; ++k)
            {
                const unsigned char c = d[k];
                if (c == '\\') continue;
                capital = capital || (c >= 'A' && c <= 'Z');
                small = small || (c >= 'a' && c <= 'z');
                digit = digit || (c >= '0' && c <= '9');
                hex = hex && (cls[c] & hex_class);
            }
            const size_t count = run_variety(d, from, to, kinds);
            return (capital && small && digit && count >= 16) || (hex && count >= 10);
        }

        /*
            Indique si un texte contient un bloc encodé : une suite de base64 ou de base64url de 1024
            caractères dont au moins 16 différents, ou 16 suites d'affilée qui peuvent chacune être
            une ligne d'un bloc (block_run()), quel que soit ce qui les sépare, une fin de ligne, un
            guillemet, ou "\n" échappé dans une chaîne de JSON ou d'un programme, où "\/" tient lieu
            de barre oblique ; des données en base64 après ";base64," ; du JSON en base64 ("eyJ...",
            dont les tokens d'OpenID Connect) ; ou des octets en hexadécimal (hex_bytes()).
        */
        bool encoded_block(const std::string& text)
        {
            const unsigned char* const cls = char_classes();
            const unsigned char* const d = reinterpret_cast<const unsigned char*>(text.data());
            const size_t n = text.size();
            variety kinds;
            size_t lines = 0;               // les suites d'affilée qui peuvent être des lignes d'un bloc
            size_t start = 0, run = 0;      // la suite lue : son premier octet, ses caractères
            for (size_t i = 0; i < n; ++i)
            {
                const unsigned char c = d[i];
                const bool slash = c == '\\' && i + 1 < n && d[i + 1] == '/';
                if (slash || (cls[c] & (base64_class | base64url_class)))
                {
                    if (run++ == 0) start = i;
                    if (slash) ++i;
                    if (run >= 1024 && (run == 1024 ? run_variety(d, start, i + 1, kinds) : kinds.add(d[i])) >= 16)
                        return true;
                    // Le JSON en base64 s'ouvre par "eyJ", l'encodage de '{"', après "=" ou une espace par
                    // exemple.
                    if (c == 'e' && n - i >= 16 && (i == 0 || !(cls[d[i - 1]] & alnum_class)) && d[i + 1] == 'y' &&
                        d[i + 2] == 'J')
                    {
                        size_t k = i + 3;
                        while (k < n && k - i < 16 && (cls[d[k]] & (alnum_class | base64url_class))) ++k;
                        if (k - i == 16) return true;
                    }
                    continue;
                }
                if (run > 0)
                {
                    lines = block_run(d, start, i, run, kinds) ? lines + 1 : 0;
                    if (lines >= 16) return true;
                    run = 0;
                }
                // "\n", "\r" ou "\t" échappé dans une chaîne sépare deux suites comme le fait le caractère
                // lui-même.
                if (c == '\\' && i + 1 < n && (d[i + 1] == 'n' || d[i + 1] == 'r' || d[i + 1] == 't'))
                    ++i;
                else if (c == ',' && i >= 7 && at_without_case(text, i - 7, ";base64,"))
                {
                    size_t k = i + 1;
                    while (k < n && k - i <= 64 && (cls[d[k]] & base64_class)) ++k;
                    if (k - i > 64) return true;
                }
            }
            if (run > 0 && block_run(d, start, n, run, kinds) && lines + 1 >= 16) return true;
            return hex_bytes(text);
        }

        // Le texte de bytes en UTF-8 : bytes eux-mêmes quand ils sont en UTF-8, sinon leur décodage, dans
        // decoded.
        const std::string& utf8_of(const std::string& bytes, std::string& decoded)
        {
            const bool decode = bytes.compare(0, 2, "\xFF\xFE") == 0 || bytes.compare(0, 2, "\xFE\xFF") == 0 ||
                                privacy_impl::utf16_without_bom(bytes) != 0 || !privacy_impl::utf8_valid(bytes);
            if (!decode) return bytes;
            privacy_impl::text_encoding encoding;
            decoded = privacy_impl::decode_text(bytes, encoding);
            return decoded;
        }

        // Un octet d'un texte : aucun caractère de contrôle hormis la tabulation, les fins de ligne et le
        // saut de page.
        bool text_byte(unsigned char c)
        {
            return c >= 0x20 ? c != 0x7F : (c == '\t' || c == '\n' || c == '\r' || c == '\f');
        }

        enum class text_verdict { text, encoded, other };

        /*
            Indique si bytes est du texte de bout en bout, ce qu'exige un nom sans type : aucune
            signature binaire que dlib connaisse (dlib/data_io), un texte selon les mesures de dlib,
            ses caractères imprimables et son entropie sous 6,5, sur son début comme sur tout son
            contenu, chaque caractère décodé (UTF-8, UTF-16, ou Windows-1252 pour les autres octets)
            sans caractère de contrôle hormis la tabulation, les fins de ligne et le saut de page,
            ni non-caractère U+FFFE ou U+FFFF, ni U+FFFD là où le décodage de l'UTF-16 a rencontré
            un caractère cassé, et aucune partie encodée (encoded_format(), encoded_block()), sinon
            encoded.  L'UTF-8 valide est lu sur place, sans copie, U+FFFD qui y est écrit compris.
        */
        text_verdict through_and_through(const std::string& bytes, const std::string& base)
        {
            if (bytes.empty()) return text_verdict::text;
            for (const auto& sig : dlib::impl::signatures)
            {
                // "BM" et "MZ" ouvrent aussi des textes : une image bitmap ou un programme échoue à la
                // lecture ci-dessous.
                if (sig.length < 3 && sig.bytes[0] >= 0x20 && sig.bytes[0] < 0x7F) continue;
                if (bytes.size() >= sig.offset + sig.length &&
                    std::memcmp(bytes.data() + sig.offset, sig.bytes, sig.length) == 0)
                    return text_verdict::other;
            }
            if (family_of(detect_document_format(bytes, base)) != document_family::text) return text_verdict::other;
            std::string decoded;
            const std::string& text = utf8_of(bytes, decoded);
            // Une marque d'ordre des octets UTF-8 suivie d'autres octets les laisse non décodés.
            if (&text == &decoded && !privacy_impl::utf8_valid(text)) return text_verdict::other;
            const unsigned char* d = reinterpret_cast<const unsigned char*>(text.data());
            const size_t n = text.size();
            if (n == 0) return text_verdict::text;
            if (!dlib::impl::is_text_content(d, n) || dlib::impl::calculate_entropy(d, n) >= 6.5)
                return text_verdict::other;
            // U+FFFE et U+FFFF, et U+FFFD que le décodage a écrit pour un caractère qu'il n'a pas pu lire.
            const unsigned char last = &text == &decoded ? 0xBD : 0xBE;
            for (size_t i = 0; i < n; ++i)
            {
                if (!text_byte(d[i])) return text_verdict::other;
                // Un caractère de contrôle de la plage U+0080 à U+009F.
                if (d[i] == 0xC2 && i + 1 < n && d[i + 1] < 0xA0) return text_verdict::other;
                if (d[i] == 0xEF && i + 2 < n && d[i + 1] == 0xBF && d[i + 2] >= last) return text_verdict::other;
            }
            return encoded_format(text) || encoded_block(text) ? text_verdict::encoded : text_verdict::text;
        }

        /*
            Le type d'un fichier (engine::kind_of()) de nom de base base.  verdict reçoit le verdict
            sur son texte quand il a été lu pour cela, pour un nom sans type ou sans extension, sinon
            text_verdict::other.
        */
        file_kind kind_of_content(const std::string& bytes, const std::string& base, text_verdict& verdict)
        {
            verdict = text_verdict::other;
            if (name_extension(base).empty())
            {
                // Sans extension : une image, un document Office, une archive ou un PDF d'après sa signature.
                if (!image_media_type(bytes).empty()) return file_kind::picture;
                const document_family family = family_of(detect_document_format(bytes, base));
                if (family == document_family::office) return file_kind::document;
                if (archive_kind_of(bytes) != archive_kind::none) return file_kind::archive;
                if (family == document_family::pdf) return file_kind::document;
            }
            else
            {
                switch (kind_of_name(base))
                {
                    case name_kind::picture:
                        return image_media_type(bytes).empty() ? file_kind::unsupported : file_kind::picture;
                    case name_kind::archive:
                        // Une archive contient ce qu'elle contient : un document Office renommé en .zip est
                        // lu comme une archive.
                        return archive_kind_of(bytes) == archive_kind::none ? file_kind::unsupported
                                                                            : file_kind::archive;
                    case name_kind::document:
                        return family_of(detect_document_format(bytes, base)) == document_family_of_name(base)
                            ? file_kind::document : file_kind::unsupported;
                    default:
                        break;
                }
            }
            // Un nom sans type, ou sans extension, est un texte quand son contenu est du texte de bout en bout.
            verdict = through_and_through(bytes, base);
            return verdict == text_verdict::text ? file_kind::document : file_kind::unsupported;
        }

        /*
            Les valeurs d'un nom, sans son extension connue, écrites sous forme de substituts : le
            nom est lu en entier, puisque la fin d'une adresse e-mail ou d'un numéro de téléphone
            ressemble à une extension, et, quand il finit par ce qui ressemble à une extension,
            aussi sans elle, ce qui peut cacher une valeur au détecteur
            ("jean.dupont@example.fr.log2") ; la lecture qui trouve le plus de valeurs l'emporte, le
            nom entier en cas d'égalité.  r reçoit les substitutions de cette lecture.
        */
        std::string name_values(document_anonymizer& a, document_report& r, const std::string& name)
        {
            const std::string ext = name_extension(name);
            if (ext.empty() || ext.size() >= name.size()) return a.anonymize_name(name, r);
            const std::string stem = name.substr(0, name.size() - ext.size());
            document_report cut = r;
            const std::string without = a.anonymize_name(stem, cut) + name.substr(stem.size());
            const std::string whole = a.anonymize_name(name, r);
            if (cut.substitutions.size() <= r.substitutions.size()) return whole;
            r = std::move(cut);
            return without;
        }

        // Indique si bytes se lit comme un texte, qui peut contenir des étiquettes quel que soit le nom
        // de son fichier.
        bool text_content(const std::string& bytes, const std::string& base)
        {
            return archive_kind_of(bytes) == archive_kind::none &&
                   family_of(detect_document_format(bytes, base)) == document_family::text;
        }

        /*
            Ce qu'un appel demande en plus de ses octets : des étiquettes ou des substituts pour les
            valeurs du texte, et les substituts du texte qu'une restauration rend, ou qu'une
            protection laisse tels quels, pour un fichier sans étiquettes protégé à nouveau.
        */
        struct call_options
        {
            bool tags = true;
            pseudonymizer::substitute_resolver text;
        };

        // Le résolveur d'une liste de substituts : les étiquettes d'un substitut, dans l'ordre de la liste.
        pseudonymizer::substitute_resolver resolver_of_list(const std::vector<substitution>& list)
        {
            if (list.empty()) return pseudonymizer::substitute_resolver();
            auto table = std::make_shared<std::map<std::string, std::vector<std::string>>>();
            for (const substitution& s : list)
            {
                if (s.substitute.empty() || s.tag.empty()) continue;
                std::vector<std::string>& tags = (*table)[s.substitute];
                if (std::find(tags.begin(), tags.end(), s.tag) == tags.end()) tags.push_back(s.tag);
            }
            if (table->empty()) return pseudonymizer::substitute_resolver();
            return [table](const std::string& candidate) {
                const auto it = table->find(candidate);
                return it == table->end() ? std::vector<std::string>() : it->second;
            };
        }
    }

// ----------------------------------------------------------------------------------------

    struct engine::impl
    {
        std::unique_ptr<pseudonymizer> p;
        pii_detector_options dopt;
        model_set m;
        // La clé des identifiants des images, dérivée de la clé du moteur.
        std::string picture_key;
        // Les termes imposés, que set_terms() remplace pendant que des fichiers sont traités.
        mutable std::mutex terms_m;
        std::vector<pii_imposed_term> terms;

        // Les options d'un détecteur : celles du moteur, avec les termes imposés du moment.
        pii_detector_options options() const
        {
            pii_detector_options o = dopt;
            std::lock_guard<std::mutex> lock(terms_m);
            o.imposed_terms = terms;
            return o;
        }

        ~impl()
        {
            if (!picture_key.empty()) secure_wipe(&picture_key[0], picture_key.size());
        }

        void attach(pii_detector& d) const
        {
            attach_pii_recognizers(d, m.tagger, m.span, 0.5, 0.9);
        }

        /*
            La forme protégée d'une description, qui peut nommer ce que l'image montre : une ligne,
            sans crochets, lue par le détecteur comme le texte d'un document, ses valeurs remplacées
            par des étiquettes avant qu'elle atteigne le fichier, puis coupée à 1000 octets en dehors
            de ses étiquettes.  known, le détecteur du document qui contient l'image, ajoute ce que
            le document lui a appris : un nom, un lieu ou une organisation de son texte que la
            description répète tel quel.  found compte les valeurs.
        */
        std::string protect_description(const std::string& description, const pii_detector* known,
                                        document_report& found, const call_options& c) const
        {
            std::string line;
            bool bounded = false;
            for (char ch : description)
            {
                if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
                if (ch == '[') ch = '(';
                if (ch == ']') ch = ')';
                if (ch == ' ' && (line.empty() || line.back() == ' ')) continue;
                line.push_back(ch);
                // Les réponses d'Albireo restent bien en dessous ; au-delà de 1000 octets, rien n'est écrit.
                if (line.size() >= 16384)
                {
                    bounded = true;
                    break;
                }
            }
            // Le dernier caractère, que la borne a pu couper, s'en va en entier.
            if (bounded)
            {
                while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) line.pop_back();
                if (!line.empty() && (static_cast<unsigned char>(line.back()) & 0x80)) line.pop_back();
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            if (line.empty()) return line;
            pii_detector d(options());
            attach(d);
            d.begin_document();
            d.learn(line);
            std::vector<pii_entity> values;
            for (const pii_entity& e : d.detect(line))
                if (!p->keeps_in_clear(e.type)) values.push_back(e);
            if (known)
                for (const pii_entity& e : known->detect(line))
                    if (!p->keeps_in_clear(e.type)) values.push_back(e);
            // Les valeurs qui se chevauchent n'en font plus qu'une, dont l'étiquette les couvre toutes.
            std::sort(values.begin(), values.end(), [](const pii_entity& a, const pii_entity& b) {
                return a.begin < b.begin || (a.begin == b.begin && a.end > b.end);
            });
            std::vector<pii_entity> joined;
            for (const pii_entity& e : values)
            {
                if (e.end <= e.begin || e.end > line.size()) continue;
                if (!joined.empty() && e.begin < joined.back().end)
                {
                    if (e.end > joined.back().end) joined.back().end = e.end;
                    continue;
                }
                joined.push_back(e);
            }
            for (const pii_entity& e : joined) found.count(e.type);
            if (!c.tags)
            {
                // Sans étiquettes, les valeurs prennent leurs substituts, que l'appelant garde, tous en
                // entier.
                std::vector<pseudonymizer::substitution> made;
                std::string out = p->apply_substitutes(line, joined, &made);
                size_t cut = out.size() > 1000 ? 1000 : out.size(), pos = 0;
                std::size_t kept = 0;
                for (const pseudonymizer::substitution& s : made)
                {
                    const size_t at = out.find(s.substitute, pos);
                    if (at == std::string::npos || at >= cut) break;
                    if (at + s.substitute.size() > cut)
                    {
                        cut = at;
                        break;
                    }
                    pos = at + s.substitute.size();
                    ++kept;
                }
                for (std::size_t k = 0; k < kept; ++k)
                    found.substitutions.push_back(document_substitution{ made[k].substitute, made[k].tag, true });
                if (cut < out.size())
                {
                    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
                    out = out.substr(0, cut) + "...";
                }
                return out;
            }
            std::string out = p->apply(line, joined);
            if (out.size() > 1000)
            {
                size_t cut = 1000;
                // Jamais à l'intérieur d'une étiquette, qu'une coupure casserait.
                const size_t open = out.rfind("[[", cut);
                if (open != std::string::npos)
                {
                    const size_t close = out.find("]]", open);
                    if (close == std::string::npos || close + 2 > cut) cut = open;
                }
                while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
                out = out.substr(0, cut) + "...";
            }
            return out;
        }

        /*
            Le marqueur d'une image, sa description protégée (protect_description(), known et
            found), que written reçoit sous le SHA-256 de l'image quand il n'est pas nul.  Son
            identifiant est une empreinte de l'image calculée avec la clé du moteur : il ne dit rien
            de l'image à qui ne détient pas la clé, et ne relie aucune image d'une installation à
            une autre.
        */
        std::string marker_of(const std::string& bytes, const std::string& media_type, const std::string& location,
                              const picture_handler& handler, std::vector<std::string>& ids,
                              const pii_detector* known, document_report& found, const call_options& c,
                              std::map<std::string, std::string>* written = nullptr) const
        {
            picture_found f;
            f.sha256 = sha256_hex(bytes);
            const std::array<unsigned char, 32> keyed = hmac_sha256_digest(picture_key, f.sha256);
            f.id = to_hex(keyed.data(), keyed.size()).substr(0, 16);
            f.media_type = media_type;
            f.location = location;
            f.bytes = &bytes;
            if (std::find(ids.begin(), ids.end(), f.id) == ids.end()) ids.push_back(f.id);
            const std::string description = protect_description(handler(f), known, found, c);
            if (written) (*written)[f.sha256] = description;
            return engine::picture_marker(f.id, description);
        }

        /*
            Le nom de base d'un fichier, ses valeurs remplacées par des substituts que Windows
            accepte, son extension gardée : a, avec le détecteur du document qu'il nomme, le lit.
        */
        std::string protect_base(document_anonymizer& a, document_report& r, const std::string& base,
                                 std::vector<substitution>& made) const
        {
            std::string stem, ext;
            split_extension(base, stem, ext);
            const std::size_t first = r.substitutions.size();
            std::string out = name_values(a, r, stem);
            for (std::size_t k = first; k < r.substitutions.size(); ++k)
            {
                document_substitution& s = r.substitutions[k];
                const std::string safe = windows_safe(s.substitute);
                if (safe != s.substitute)
                {
                    const std::size_t at = out.find(s.substitute);
                    if (at != std::string::npos) out.replace(at, s.substitute.size(), safe);
                    s.substitute = safe;
                }
                made.push_back(substitution{ s.substitute, s.tag });
            }
            if (out == stem) return base;
            // Une extension sans type que la lecture a laissée telle quelle garde sa place dans un nom
            // raccourci.
            const size_t kept = ext.empty() ? name_extension(stem).size() : 0;
            if (kept > 0 && kept < out.size() &&
                out.compare(out.size() - kept, kept, stem, stem.size() - kept, kept) == 0)
                return windows_name(out.substr(0, out.size() - kept), out.substr(out.size() - kept));
            return windows_name(out, ext);
        }

        std::string restore_base(document_anonymizer& a, document_report& r, const std::string& base) const
        {
            std::string stem, ext;
            split_extension(base, stem, ext);
            return a.restore_name(stem, r) + ext;
        }

        // Le résolveur de l'anonymiseur de documents, à partir du lookup de l'appelant.
        static pseudonymizer::substitute_resolver resolver_of(const substitute_lookup& lookup)
        {
            if (!lookup) return pseudonymizer::substitute_resolver();
            return [lookup](const std::string& candidate) { return lookup(candidate); };
        }

        // Le nom protégé d'un fichier lu seul : une image, une archive, un dossier.
        std::string protect_alone(const std::string& base, const substitute_lookup& lookup,
                                  std::vector<substitution>& made, document_report& r) const
        {
            pii_detector d(options());
            attach(d);
            document_anonymizer a(*p, d);
            a.set_substitute_resolver(resolver_of(lookup));
            return protect_base(a, r, base, made);
        }

        std::string restore_alone(const std::string& base, const substitute_lookup& lookup, document_report& r) const
        {
            pii_detector d(options());
            document_anonymizer a(*p, d);
            a.set_substitute_resolver(resolver_of(lookup));
            return restore_base(a, r, base);
        }

        static std::string folder_of(const std::string& name)
        {
            return name.substr(0, name.size() - base_name(name).size());
        }

        /*
            Un document, protégé ou restauré.  Un document Office demande les descriptions de ses
            images pendant sa lecture, avant que son texte ait appris quoi que ce soit au détecteur,
            un PDF une fois que c'est fait : la description donnée pour chaque image est gardée,
            Albireo n'étant interrogé qu'une fois, et relue avec ce que le texte entier a appris ;
            quand cela révèle une valeur que la première lecture d'une description avait manquée, un
            nom du texte qu'elle répète tel quel, le document est protégé à nouveau, ses images
            prenant leurs descriptions lues avec le détecteur de la première protection.
        */
        engine_result document(const std::string& bytes, const std::string& name, document_format f,
                               const picture_handler& handler, bool restoring, const substitute_lookup& lookup,
                               uint64_t& budget, const call_options& c) const
        {
            pii_detector first(options());
            if (!handler || restoring)
                return read_document(bytes, name, f, handler, restoring, lookup, budget, c, first);
            std::map<std::string, std::string> given;
            const picture_handler once = [&](const picture_found& found) {
                const auto it = given.find(found.sha256);
                if (it != given.end()) return it->second;
                const std::string description = handler(found);
                given[found.sha256] = description;
                return description;
            };
            std::map<std::string, std::string> written;
            engine_result out = read_document(bytes, name, f, once, false, lookup, budget, c, first, nullptr, &written);
            bool again = false;
            document_report unused;
            for (const auto& w : written)
            {
                const auto g = given.find(w.first);
                if (g != given.end() && protect_description(g->second, &first, unused, c) != w.second)
                {
                    again = true;
                    break;
                }
            }
            if (!again) return out;
            out = engine_result();
            pii_detector second(options());
            return read_document(bytes, name, f, once, false, lookup, budget, c, second, &first);
        }

        /*
            Une lecture d'un document par le détecteur d, qui apprend son texte.  known, quand il
            n'est pas nul, lit les descriptions des images à sa place (document()) ; written reçoit
            les descriptions écrites, sous le SHA-256 de leurs images.
        */
        engine_result read_document(const std::string& bytes, const std::string& name, document_format f,
                                    const picture_handler& handler, bool restoring, const substitute_lookup& lookup,
                                    const uint64_t& budget, const call_options& c, pii_detector& d,
                                    const pii_detector* known = nullptr,
                                    std::map<std::string, std::string>* written = nullptr) const
        {
            engine_result out;
            if (!restoring) attach(d);
            // Un PDF garde ses pages : son texte est remplacé sur place (le défaut de l'anonymiseur).
            document_anonymizer a(*p, d);
            // Les propriétés du document prennent des substituts, qu'une fenêtre de propriétés montre tels
            // quels.
            a.set_untagged_properties(true);
            a.set_substitute_resolver(resolver_of(lookup));
            // Sans étiquettes, les valeurs du texte prennent aussi des substituts, que l'appelant garde.
            a.set_untagged_text(!restoring && !c.tags);
            a.set_text_substitute_resolver(c.text);
            // Une restauration ne peut pas remettre une image dans un document : son marqueur s'en va.
            if (restoring) a.set_picture_text_finder(picture_markers);
            /*
                Une partie d'un document Office occupe environ 26 octets de mémoire pour chacun de
                ses octets pendant sa lecture : chaque partie reste dans la limite de 32 fois le
                document, à partir de 32 Mo.
            */
            zip_limits limits;
            limits.max_total_size = std::min<uint64_t>(limits.max_total_size, budget);
            limits.max_entry_size = std::min<uint64_t>(std::min<uint64_t>(limits.max_entry_size, budget),
                std::max<uint64_t>(32ull << 20, static_cast<uint64_t>(bytes.size()) * 32));
            a.set_package_limits(limits);
            std::vector<std::string> ids;
            document_report described;
            if (handler && !restoring)
                a.set_image_describer([&](const document_image& img) {
                    return marker_of(img.bytes, img.media_type, base_name(name) + ": " + img.location, handler, ids,
                                     known ? known : &d, described, c, written);
                });
            document_report report;
            out.bytes = restoring ? a.restore(bytes, f, report, base_name(name))
                                  : a.anonymize(bytes, f, report, base_name(name));
            // Les valeurs des descriptions comptent avec celles du document.
            for (const auto& e : described.entities) report.entities[e.first] += e.second;
            report.dates_shifted += described.dates_shifted;
            for (const document_substitution& s : described.substitutions) report.substitutions.push_back(s);
            // Le nom est lu avec ce que le document a appris au détecteur.
            std::vector<substitution> named;
            out.name = folder_of(name) + (restoring ? restore_base(a, report, base_name(name))
                                                    : protect_base(a, report, base_name(name), named));
            out.kind = document_format_name(f);
            out.report = report_json(report);
            out.pictures = ids;
            // Les substituts des propriétés et du nom, tels qu'écrits.
            for (const document_substitution& s : report.substitutions)
                out.substitutions.push_back(substitution{ s.substitute, s.tag, s.text });
            return out;
        }

        engine_result picture(const std::string& bytes, const std::string& name, const picture_handler& handler,
                              const substitute_lookup& lookup, const call_options& c) const
        {
            engine_result out;
            std::vector<std::string> ids;
            const picture_handler none = [](const picture_found&) { return std::string(); };
            document_report report;
            out.bytes = marker_of(bytes, image_media_type(bytes), base_name(name), handler ? handler : none, ids,
                                  nullptr, report, c) + "\r\n";
            // Les substituts de la description, sans étiquettes.
            for (const document_substitution& s : report.substitutions)
                out.substitutions.push_back(substitution{ s.substitute, s.tag, s.text });
            out.name = folder_of(name) + protect_alone(base_name(name), lookup, out.substitutions, report) + ".txt";
            out.kind = "image";
            out.report = report_json(report);
            out.pictures = ids;
            return out;
        }

        // known : le type du fichier quand l'appelant l'a déjà trouvé.
        engine_result any(const std::string& bytes, const std::string& name, const picture_handler& handler,
                          bool restoring, const substitute_lookup& lookup, int depth, uint64_t& budget,
                          const call_options& c, const file_kind* known = nullptr) const
        {
            const std::string base = base_name(name);
            text_verdict verdict = text_verdict::other;
            file_kind k = known ? *known : kind_of_content(bytes, base, verdict);
            // Un texte sous un autre nom peut contenir des étiquettes, que sa restauration remet.
            if (k == file_kind::unsupported && restoring && text_content(bytes, base)) k = file_kind::document;
            if (k == file_kind::picture)
            {
                if (!restoring) return picture(bytes, name, handler, lookup, c);
                engine_result out;
                document_report report;
                out.bytes = bytes;
                out.name = folder_of(name) + restore_alone(base_name(name), lookup, report);
                out.kind = "image";
                out.report = report_json(report);
                return out;
            }
            if (k == file_kind::archive) return archive(bytes, name, handler, restoring, lookup, depth, budget, c);
            if (k == file_kind::document)
                return document(bytes, name, detect_document_format(bytes, base_name(name)), handler, restoring,
                                lookup, budget, c);
            if (supported_name(base))
                throw std::runtime_error(base + ": the content of this file does not match its name");
            if (known) verdict = through_and_through(bytes, base);
            if (verdict == text_verdict::encoded)
                throw std::runtime_error(base + ": this file holds encoded parts that the detector cannot read");
            throw std::runtime_error(base + ": this kind of file is not supported");
        }

        /*
            Les dossiers du chemin d'une entrée, chacun protégé, ou restauré, une seule fois pour
            toute l'archive : "Clients/Jean Dupont/" donne le même dossier à toutes ses entrées.
        */
        std::string entry_folders(const std::string& folders, bool restoring, const substitute_lookup& lookup,
                                  std::map<std::string, std::string>& done, engine_result& out) const
        {
            std::string result;
            std::size_t start = 0;
            while (start < folders.size())
            {
                std::size_t slash = folders.find('/', start);
                if (slash == std::string::npos) slash = folders.size();
                const std::string part = folders.substr(start, slash - start);
                if (!part.empty() && part != "." && part != "..")
                {
                    auto it = done.find(part);
                    if (it == done.end())
                    {
                        document_report r;
                        std::string changed;
                        if (restoring)
                        {
                            pii_detector d(options());
                            document_anonymizer a(*p, d);
                            a.set_substitute_resolver(resolver_of(lookup));
                            changed = a.restore_name(part, r);
                        }
                        else
                        {
                            pii_detector d(options());
                            attach(d);
                            document_anonymizer a(*p, d);
                            a.set_substitute_resolver(resolver_of(lookup));
                            // Un dossier n'a pas d'extension : son nom entier est lu.
                            const std::size_t first = r.substitutions.size();
                            changed = name_values(a, r, part);
                            for (std::size_t k = first; k < r.substitutions.size(); ++k)
                            {
                                document_substitution& s = r.substitutions[k];
                                const std::string safe = windows_safe(s.substitute);
                                if (safe != s.substitute)
                                {
                                    const std::size_t at = changed.find(s.substitute);
                                    if (at != std::string::npos) changed.replace(at, s.substitute.size(), safe);
                                    s.substitute = safe;
                                }
                                out.substitutions.push_back(substitution{ s.substitute, s.tag });
                            }
                            if (changed != part) changed = windows_name(changed, std::string());
                        }
                        merge_report(out.report, report_json(r), std::string());
                        it = done.insert(std::make_pair(part, changed)).first;
                    }
                    result += it->second;
                }
                else
                {
                    result += part;
                }
                if (slash < folders.size()) result += '/';
                start = slash + 1;
            }
            return result;
        }

        engine_result archive(const std::string& bytes, const std::string& name, const picture_handler& handler,
                              bool restoring, const substitute_lookup& lookup, int depth, uint64_t& budget,
                              const call_options& c) const
        {
            if (depth > 3) throw std::runtime_error(base_name(name) + ": archives nested too deep");
            archive_kind ak = archive_kind_of(bytes);
            if (ak == archive_kind::none) ak = archive_kind_of_name(name);
            // Les entrées sont prises sur ce qui reste du budget du fichier.
            archive_limits limits;
            limits.max_total_size = std::min<uint64_t>(limits.max_total_size, budget);
            limits.max_entry_size = std::min<uint64_t>(limits.max_entry_size, budget);
            std::vector<archive_entry> entries = read_archive(ak, bytes, limits);
            uint64_t read = 0;
            for (const archive_entry& e : entries) read += e.data.size();
            budget -= std::min(budget, read);
            engine_result out;
            out.kind = archive_kind_name(ak);
            out.report = report_json(document_report());
            {
                document_report r;
                out.name = folder_of(name) + (restoring ? restore_alone(base_name(name), lookup, r)
                                                        : protect_alone(base_name(name), lookup, out.substitutions, r));
                merge_report(out.report, report_json(r), std::string());
            }
            std::set<std::string> names;
            std::map<std::string, int> next_number;
            std::set<std::string> pictures;
            std::map<std::string, std::string> folders;
            for (const archive_entry& e : entries) names.insert(lower(e.name));
            std::vector<archive_entry> kept;
            for (archive_entry& e : entries)
            {
                if (e.directory)
                {
                    const std::string renamed = entry_folders(e.name, restoring, lookup, folders, out);
                    if (renamed != e.name)
                    {
                        names.erase(lower(e.name));
                        e.name = renamed;
                        names.insert(lower(e.name));
                    }
                    kept.push_back(std::move(e));
                    continue;
                }
                // Une entrée laissée de côté rend aussitôt sa mémoire.
                const file_kind kind =
                    e.regular && !restoring ? engine::kind_of(e.data, e.name) : file_kind::unsupported;
                if (!e.regular || (!restoring && kind == file_kind::unsupported))
                {
                    out.report["dropped"].push_back(e.name);
                    std::string().swap(e.data);
                    continue;
                }
                // Le fichier texte d'une image, image qu'une restauration ne peut pas rendre, s'en va.
                if (restoring && e.data.size() <= 65536 + 3 && marker_alone(e.data))
                {
                    out.report["pictures_removed"] =
                        static_cast<unsigned long long>(out.report["pictures_removed"].as_int(0) + 1);
                    std::string().swap(e.data);
                    continue;
                }
                try
                {
                    engine_result r = any(e.data, e.name, handler, restoring, lookup, depth + 1, budget, c,
                                          restoring ? nullptr : &kind);
                    const std::string read_as = e.name;
                    const std::string renamed =
                        entry_folders(folder_of(e.name), restoring, lookup, folders, out) + base_name(r.name);
                    if (renamed != e.name)
                    {
                        names.erase(lower(e.name));
                        e.name = free_name(renamed, names, next_number);
                    }
                    // L'entrée écrite, grâce à laquelle une copie extraite de l'archive est reconnue.
                    if (!restoring)
                    {
                        entry_written w;
                        w.original = read_as;
                        w.name = e.name;
                        w.sha256 = sha256_hex(r.bytes);
                        w.kind = r.kind;
                        out.entries.push_back(std::move(w));
                        for (entry_written& inner : r.entries)
                        {
                            inner.original = read_as + "!" + inner.original;
                            inner.name = e.name + "!" + inner.name;
                            out.entries.push_back(std::move(inner));
                        }
                    }
                    e.data = std::move(r.bytes);
                    merge_report(out.report, r.report, e.name);
                    for (const std::string& id : r.pictures)
                        if (pictures.insert(id).second) out.pictures.push_back(id);
                    for (substitution& s : r.substitutions) out.substitutions.push_back(std::move(s));
                    kept.push_back(std::move(e));
                }
                catch (std::exception& x)
                {
                    /*
                        Une restauration garde telle quelle une entrée qu'elle ne peut pas lire, qui ne
                        contient aucune étiquette, dans son dossier restauré avec les autres et sous son
                        nom restauré.  Une protection la laisse de côté : elle pourrait contenir des
                        données personnelles.
                    */
                    if (restoring)
                    {
                        document_report r;
                        const std::string renamed = entry_folders(folder_of(e.name), true, lookup, folders, out) +
                                                    restore_alone(base_name(e.name), lookup, r);
                        merge_report(out.report, report_json(r), std::string());
                        if (renamed != e.name)
                        {
                            names.erase(lower(e.name));
                            e.name = free_name(renamed, names, next_number);
                        }
                        kept.push_back(std::move(e));
                        continue;
                    }
                    out.report["dropped"].push_back(e.name);
                    out.report["notes"].push_back(e.name + ": " + x.what());
                    std::string().swap(e.data);
                }
            }
            out.bytes = write_archive(ak, kept);
            return out;
        }

        /*
            L'entrée située à path d'une archive (engine::extract()).  On entre dans une archive
            contenue par son nom et '!' : le plus long nom d'entrée qui commence le chemin, pour
            qu'un nom qui contient '!' soit lu en entier.
        */
        std::string extract(const std::string& bytes, const std::string& name, const std::string& path) const
        {
            std::string data = bytes, label = name, rest = path;
            for (int depth = 0; depth <= 3; ++depth)
            {
                archive_kind ak = archive_kind_of(data);
                if (ak == archive_kind::none) ak = archive_kind_of_name(label);
                if (ak == archive_kind::none) break;
                std::vector<archive_entry> entries = read_archive(ak, data, archive_limits());
                std::size_t inner = entries.size();
                for (std::size_t k = 0; k < entries.size(); ++k)
                {
                    const archive_entry& e = entries[k];
                    if (e.directory || !e.regular) continue;
                    if (e.name == rest) return std::move(entries[k].data);
                    const bool starts = rest.size() > e.name.size() + 1 && rest[e.name.size()] == '!' &&
                                        rest.compare(0, e.name.size(), e.name) == 0;
                    if (starts && (inner == entries.size() || e.name.size() > entries[inner].name.size())) inner = k;
                }
                if (inner == entries.size()) break;
                rest = rest.substr(entries[inner].name.size() + 1);
                label = entries[inner].name;
                data = std::move(entries[inner].data);
            }
            throw std::runtime_error(base_name(name) + ": the archive holds no such entry");
        }
    };

// ----------------------------------------------------------------------------------------

    engine::engine(const std::string& key, const engine_options& options) : p_(std::make_shared<impl>())
    {
        if (key.size() != 32) throw std::runtime_error("the key of the engine must have 32 bytes");
        pseudonymizer_options popt;
        popt.context = options.context.empty() ? "default" : options.context;
        popt.dates = date_policy::shift;
        popt.date_order = options.date_order == "mdy" ? numeric_date_order::month_day_year
                                                      : numeric_date_order::day_month_year;
        p_->p.reset(new pseudonymizer(pseudonymization_key::from_bytes(key), popt));
        p_->dopt.date_order = popt.date_order;
        p_->picture_key = hkdf_sha256(key, "pseudalis.pictures", "identifier", 32);
        try
        {
            p_->m = shared_models();
        }
        catch (std::exception& e)
        {
            throw std::runtime_error(std::string("the models of the engine cannot be read: ") + e.what());
        }
    }

    file_kind engine::kind_of_name(const std::string& name)
    {
        switch (pseudalis::kind_of_name(name))
        {
            case name_kind::document: return file_kind::document;
            case name_kind::picture: return file_kind::picture;
            case name_kind::archive: return file_kind::archive;
            default: return file_kind::unsupported;
        }
    }

    file_kind engine::kind_of(const std::string& bytes, const std::string& name)
    {
        text_verdict verdict;
        return kind_of_content(bytes, base_name(name), verdict);
    }

    std::string engine::extract(const std::string& bytes, const std::string& name, const std::string& path) const
    {
        return p_->extract(bytes, name, path);
    }

    std::string engine::picture_marker(const std::string& id, const std::string& description)
    {
        return description.empty() ? "[Image@" + id + "]" : "[Image@" + id + " : " + description + "]";
    }

    engine_result engine::anonymize(const std::string& bytes, const std::string& name,
                                    const picture_handler& pictures, const substitute_lookup& lookup, bool tags,
                                    const std::vector<substitution>& known) const
    {
        uint64_t budget = decompression_budget(bytes.size());
        call_options c;
        c.tags = tags;
        c.text = resolver_of_list(known);
        return p_->any(bytes, name, pictures, false, lookup, 0, budget, c);
    }

    engine_result engine::restore(const std::string& bytes, const std::string& name,
                                  const substitute_lookup& lookup, const std::vector<substitution>& text) const
    {
        uint64_t budget = decompression_budget(bytes.size());
        call_options c;
        c.text = resolver_of_list(text);
        return p_->any(bytes, name, picture_handler(), true, lookup, 0, budget, c);
    }

    std::string engine::anonymize_name(const std::string& name, const substitute_lookup& lookup,
                                       std::vector<substitution>& made, json& report) const
    {
        document_report r;
        const std::string out = impl::folder_of(name) + p_->protect_alone(base_name(name), lookup, made, r);
        report = report_json(r);
        return out;
    }

    std::string engine::restore_name(const std::string& name, const substitute_lookup& lookup, json& report) const
    {
        document_report r;
        const std::string out = impl::folder_of(name) + p_->restore_alone(base_name(name), lookup, r);
        report = report_json(r);
        return out;
    }

    std::string engine::anonymize_text(const std::string& text, json& report, bool tags,
                                       std::vector<substitution>* made) const
    {
        pii_detector d(p_->options());
        p_->attach(d);
        document_anonymizer a(*p_->p, d);
        a.set_untagged_text(!tags);
        document_report r;
        const std::string out = a.anonymize_text(text, r);
        report = report_json(r);
        if (made)
            for (const document_substitution& s : r.substitutions)
                made->push_back(substitution{ s.substitute, s.tag, s.text });
        return out;
    }

    std::string engine::restore_text(const std::string& text, json& report,
                                     const std::vector<substitution>& substitutes) const
    {
        pii_detector d(p_->options());
        document_anonymizer a(*p_->p, d);
        a.set_text_substitute_resolver(resolver_of_list(substitutes));
        document_report r;
        const std::string out = a.restore(text, document_format::text, r);
        report = report_json(r);
        return out;
    }

    /* Les emplacements des empans d'un texte ne sont donnés que pour l'UTF-8, le texte que le service
       renvoie, et jusqu'à 100000, que le service lit dans le résultat du moteur : au-delà, aucun. */
    namespace
    {
        const std::size_t max_spans = 100000;
    }

    namespace
    {
        // Une valeur d'un texte protégé : sa plage d'octets, son type et son original.
        struct protected_value
        {
            std::size_t begin, end;
            pii_type type;
            std::string original;
        };

        /* Les étiquettes d'un texte, puis les substituts que resolve connaît en dehors d'elles, dans
           leur ordre : les valeurs protégées telles que restore_text() les remet. */
        std::vector<protected_value> protected_values(const pseudonymizer& p, const std::string& text,
                                                      const pseudonymizer::substitute_resolver& resolve)
        {
            std::vector<protected_value> out;
            for (const pseudonymizer::tag_occurrence& o : p.find_tags(text))
                out.push_back(protected_value{ o.begin, o.end, o.type, o.original });
            if (resolve)
            {
                const std::size_t tags = out.size();
                for (const pseudonymizer::substitute_occurrence& o : p.find_substitutes(text, resolve))
                {
                    bool inside = false;
                    for (std::size_t k = 0; k < tags; ++k)
                        if (o.begin < out[k].end && out[k].begin < o.end) inside = true;
                    if (!inside) out.push_back(protected_value{ o.begin, o.end, o.type, o.original });
                }
                std::sort(out.begin(), out.end(),
                          [](const protected_value& a, const protected_value& b) { return a.begin < b.begin; });
            }
            return out;
        }
    }

    json engine::tag_spans(const std::string& text, const std::vector<substitution>& substitutes) const
    {
        json spans = json::array();
        if (!privacy_impl::utf8_valid(text)) return spans;
        for (const protected_value& o : protected_values(*p_->p, text, resolver_of_list(substitutes)))
        {
            if (spans.size() == max_spans) return json::array();
            json s = json::object();
            s["begin"] = static_cast<unsigned long long>(o.begin);
            s["end"] = static_cast<unsigned long long>(o.end);
            s["type"] = pii_type_keyword(o.type);
            spans.push_back(s);
        }
        return spans;
    }

    json engine::restored_spans(const std::string& tagged, const std::string& restored,
                                const std::vector<substitution>& substitutes) const
    {
        json spans = json::array();
        if (!privacy_impl::utf8_valid(restored)) return spans;
        std::string rebuilt;
        std::size_t pos = 0;
        for (const protected_value& o : protected_values(*p_->p, tagged, resolver_of_list(substitutes)))
        {
            if (o.begin < pos || spans.size() == max_spans) return json::array();
            rebuilt.append(tagged, pos, o.begin - pos);
            json s = json::object();
            s["begin"] = static_cast<unsigned long long>(rebuilt.size());
            rebuilt += o.original;
            s["end"] = static_cast<unsigned long long>(rebuilt.size());
            s["type"] = pii_type_keyword(o.type);
            spans.push_back(s);
            pos = o.end;
        }
        rebuilt.append(tagged, pos, std::string::npos);
        // Les étiquettes qu'un lien encode sont restaurées aussi : les emplacements sont alors inconnus.
        if (rebuilt != restored) return json::array();
        return spans;
    }

    void engine::set_terms(const std::vector<imposed_term>& terms)
    {
        std::vector<pii_imposed_term> converted;
        for (const imposed_term& t : terms)
        {
            pii_imposed_term c;
            c.text = t.term;
            c.type = pii_type_from_keyword(t.type);
            c.case_sensitive = t.case_sensitive;
            if (c.type != pii_type::none && !c.text.empty()) converted.push_back(c);
        }
        std::lock_guard<std::mutex> lock(p_->terms_m);
        p_->terms.swap(converted);
    }

    std::size_t engine::term_count() const
    {
        std::lock_guard<std::mutex> lock(p_->terms_m);
        return p_->terms.size();
    }

    json engine::detect(const std::string& text) const
    {
        pii_detector d(p_->options());
        p_->attach(d);
        d.begin_document();
        d.learn(text);
        json spans = json::array();
        for (const pii_entity& e : d.detect(text))
        {
            json s = json::object();
            s["begin"] = static_cast<unsigned long long>(e.begin);
            s["end"] = static_cast<unsigned long long>(e.end);
            s["type"] = pii_type_keyword(e.type);
            s["confidence"] = e.confidence;
            s["text"] = text.substr(e.begin, e.end - e.begin);
            spans.push_back(s);
        }
        return spans;
    }

    std::string engine::key_fingerprint() const
    {
        return p_->p->key_fingerprint();
    }

    json engine::models()
    {
        json j = json::object();
        j["tagger"] = anonymizer_models::tagger_source();
        j["span_model"] = anonymizer_models::span_model_source();
        j["size"] = anonymizer_models::span_model_size();
        return j;
    }

}
