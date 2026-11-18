// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'extraction du texte des documents, côté service (fork pour macOS).  Portée fidèlement
    depuis la page (web/js/documents.js), pour que le navigateur n'ait plus qu'à envoyer les
    octets du fichier : le service rend le texte, avec la même structure "## " (page,
    diapositive, feuille, section) que la page produisait, car la recherche documentaire (RAG)
    nomme ses passages d'après ces lignes et le texte doit concorder.

    Les formats Office et OpenDocument sont des archives ZIP : on les lit avec le lecteur ZIP
    en mémoire de dlib (dlib::zip_archive_reader), puis on lit leur XML avec un petit extracteur
    de texte linéaire, suffisant pour reproduire le RÉSULTAT des regex et du DOMParser du JS
    (on n'imite pas la technique, seulement la sortie).  Le PDF est délégué à extract_pdf_text()
    (host/doc_extract_pdf.cpp), le RTF, l'HTML et le texte brut sont lus directement.
*/

#include "doc_extract.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <dlib/zip.h>

namespace gulliver
{
    namespace
    {
        // Les mêmes bornes que la page (web/js/documents.js).
        const std::size_t max_file_bytes = 100ULL * 1024 * 1024;   // maxFileBytes
        const std::size_t max_text_chars = 4ULL * 1024 * 1024;     // maxTextChars
        const std::size_t max_sheet_rows = 20000;                  // maxSheetRows

        // -------------------------------------------------------------------------------------
        //  Petits outils de texte
        // -------------------------------------------------------------------------------------

        std::string to_lower(std::string s)
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        // Teste si s se termine par le suffixe donné (utilisé pour les types de relations).
        bool ends_with(const std::string& s, const std::string& suffix)
        {
            return s.size() >= suffix.size()
                && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // L'extension d'un nom de fichier (ce qui suit le dernier point), en minuscules.  Reproduit
        // extension() (L62) : quelques noms sans extension sont traités comme du texte.
        std::string extension(const std::string& name)
        {
            const std::string base = to_lower(name);
            if (base == "dockerfile" || base == "makefile" || base == "cmakelists.txt") return "txt";
            const std::size_t i = base.find_last_of('.');
            return i == std::string::npos ? std::string() : base.substr(i + 1);
        }

        // Les mêmes espaces que String.prototype.trim() de JS sur nos usages (blancs ASCII).
        bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        std::string trim(const std::string& s)
        {
            std::size_t a = 0, b = s.size();
            while (a < b && is_space(s[a])) ++a;
            while (b > a && is_space(s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        bool has_text(const std::string& s)
        {
            for (char c : s) if (!is_space(c)) return true;
            return false;
        }

        // Remplace les suites de 3 sauts de ligne ou plus par exactement deux (comme .replace(/\n{3,}/g, '\n\n')).
        std::string collapse_blank_lines(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            std::size_t run = 0;
            for (char c : s)
            {
                if (c == '\n')
                {
                    ++run;
                    if (run <= 2) out.push_back(c);
                }
                else
                {
                    run = 0;
                    out.push_back(c);
                }
            }
            return out;
        }

        // Réduit toute suite de blancs à une seule espace et retire les blancs de bord (comme .replace(/\s+/g, ' ').trim()).
        std::string squeeze_spaces(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            bool pending = false;
            for (char c : s)
            {
                if (is_space(c)) { pending = !out.empty(); }
                else { if (pending) out.push_back(' '); pending = false; out.push_back(c); }
            }
            return out;
        }

        // L'encodage UTF-8 d'un point de code, ajouté à out.  Sert au décodage des entités &#NNN;.
        void append_utf8(std::string& out, unsigned long cp)
        {
            if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
            else if (cp <= 0x7ff)
            {
                out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else if (cp <= 0xffff)
            {
                out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
            else if (cp <= 0x10ffff)
            {
                out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
        }

        // Décode les entités XML &amp; &lt; &gt; &quot; &apos; et &#NNN; / &#xHH; ; une entité
        // inconnue reste telle quelle, comme le ferait un lecteur tolérant.
        std::string decode_entities(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); )
            {
                if (s[i] != '&') { out.push_back(s[i++]); continue; }
                const std::size_t semi = s.find(';', i + 1);
                // Une entité raisonnablement courte ; au-delà, c'est un '&' isolé.
                if (semi == std::string::npos || semi - i > 12) { out.push_back(s[i++]); continue; }
                const std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") out.push_back('&');
                else if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "quot") out.push_back('"');
                else if (ent == "apos") out.push_back('\'');
                else if (ent.size() >= 2 && ent[0] == '#')
                {
                    unsigned long cp = 0;
                    bool ok = false;
                    try
                    {
                        if (ent[1] == 'x' || ent[1] == 'X')
                            { if (ent.size() > 2) { cp = std::stoul(ent.substr(2), nullptr, 16); ok = true; } }
                        else
                            { cp = std::stoul(ent.substr(1), nullptr, 10); ok = true; }
                    }
                    catch (...) { ok = false; }
                    if (ok && cp > 0 && cp <= 0x10ffff) append_utf8(out, cp);
                    else { out.push_back('&'); i += 1; continue; }
                }
                else { out.push_back('&'); i += 1; continue; }
                i = semi + 1;
            }
            return out;
        }

        // Décode le texte d'octets comme decodeText() (L69) : UTF-8 si c'est de l'UTF-8 valide,
        // sinon la page de code Windows-1252 (Europe de l'Ouest).  On ne touche pas aux octets
        // UTF-8 valides ; pour le repli, on convertit chaque octet 0x80-0xFF en son point de code.
        bool is_valid_utf8(const std::string& s)
        {
            std::size_t i = 0;
            const std::size_t n = s.size();
            while (i < n)
            {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                if (c < 0x80) { ++i; continue; }
                std::size_t extra;
                unsigned long cp;
                if ((c & 0xe0) == 0xc0) { extra = 1; cp = c & 0x1f; }
                else if ((c & 0xf0) == 0xe0) { extra = 2; cp = c & 0x0f; }
                else if ((c & 0xf8) == 0xf0) { extra = 3; cp = c & 0x07; }
                else return false;
                if (i + extra >= n) return false;
                for (std::size_t k = 1; k <= extra; ++k)
                {
                    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc & 0xc0) != 0x80) return false;
                    cp = (cp << 6) | (cc & 0x3f);
                }
                // Surlongs et points de code hors plage.
                if (extra == 1 && cp < 0x80) return false;
                if (extra == 2 && cp < 0x800) return false;
                if (extra == 3 && cp < 0x10000) return false;
                if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
                i += extra + 1;
            }
            return true;
        }

        // La table Windows-1252 pour les 0x80-0x9F (le reste coïncide avec Latin-1 / Unicode).
        unsigned long cp1252_to_unicode(unsigned char c)
        {
            static const unsigned short high[32] = {
                0x20ac, 0x81,   0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d,   0x017d, 0x8f,
                0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d,   0x017e, 0x0178
            };
            if (c >= 0x80 && c <= 0x9f) return high[c - 0x80];
            return c;   // 0x00-0x7F et 0xA0-0xFF : mêmes points de code qu'en Unicode.
        }

        std::string decode_text(const std::string& bytes)
        {
            if (is_valid_utf8(bytes)) return bytes;
            std::string out;
            out.reserve(bytes.size());
            for (char ch : bytes)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x80) out.push_back(static_cast<char>(c));
                else append_utf8(out, cp1252_to_unicode(c));
            }
            return out;
        }

        // Reproduit looksBinary() (L78) : un octet nul dans les 8 premiers Ko.
        bool looks_binary(const std::string& bytes)
        {
            const std::size_t n = std::min<std::size_t>(bytes.size(), 8192);
            for (std::size_t i = 0; i < n; ++i) if (bytes[i] == '\0') return true;
            return false;
        }

        // Reproduit isPdf() (L860) : les octets commencent par "%PDF-".
        bool is_pdf(const std::string& bytes)
        {
            return bytes.size() >= 5 && bytes.compare(0, 5, "%PDF-") == 0;
        }

        // Les ensembles d'extensions de la page (L30 et L37).
        const std::set<std::string>& text_extensions()
        {
            static const std::set<std::string> set = {
                "txt","md","markdown","rst","csv","tsv","json","jsonl","ndjson","xml","yaml","yml",
                "toml","ini","cfg","conf","log","env","c","h","cc","cpp","cxx","hpp","hh","hxx","inl",
                "cu","cuh","py","pyw","pyi","js","mjs","cjs","ts","tsx","jsx","java","kt","kts","scala",
                "go","rs","swift","m","mm","cs","fs","vb","php","rb","pl","pm","lua","r","jl","sh","bash",
                "zsh","fish","ps1","psm1","bat","cmd","sql","graphql","proto","cmake","mk","make","gradle",
                "properties","tex","bib","css","scss","less","vue","svelte","dart","hs","ml","clj","ex",
                "exs","erl","asm","s","v","sv","vhd","tf","hcl","dockerfile","gitignore","editorconfig",
                "nix","srt","vtt","diff","patch"
            };
            return set;
        }

        const std::set<std::string>& legacy_extensions()
        {
            static const std::set<std::string> set = { "doc","dot","xls","xlt","ppt","pot","pps" };
            return set;
        }

        // -------------------------------------------------------------------------------------
        //  Un petit arbre XML, suffisant pour reproduire la sortie du DOMParser du JS
        // -------------------------------------------------------------------------------------

        /*
            On lit le XML en un arbre d'éléments avec, pour chaque nœud, son nom local (sans
            préfixe d'espace de noms), ses attributs (nom local -> valeur) et ses enfants (texte
            ou éléments).  C'est tout ce dont les extracteurs ont besoin : ils cherchent des
            éléments par nom local, lisent des attributs quel que soit leur préfixe, et
            concatènent le texte.  Un XML mal formé lève doc_error("xml-invalid"), comme la page
            (parseXml() lève "xml-invalid").
        */
        struct xml_node
        {
            bool is_text = false;              // vrai pour un nœud texte
            std::string text;                  // le texte, déjà décodé des entités
            std::string name;                  // le nom local d'un élément (sans préfixe)
            std::vector<std::pair<std::string, std::string>> attrs;   // (nom local, valeur)
            std::vector<xml_node> children;

            // L'attribut de nom local donné, quel que soit son préfixe (comme attr() L201) ; "" si absent.
            const std::string* attr(const std::string& key) const
            {
                for (const auto& a : attrs) if (a.first == key) return &a.second;
                return nullptr;
            }
        };

        // Retire le préfixe d'espace de noms d'un nom qualifié (w:p -> p).
        std::string local_name(const std::string& qname)
        {
            const std::size_t c = qname.find(':');
            return c == std::string::npos ? qname : qname.substr(c + 1);
        }

        // Un analyseur XML descendant simple et défensif.  Il ne valide pas tout, mais il borne
        // chaque accès et ne plante jamais ; un document manifestement mal formé lève xml-invalid.
        struct xml_parser
        {
            const std::string& s;
            std::size_t i = 0;
            explicit xml_parser(const std::string& text) : s(text) {}

            [[noreturn]] void fail() { throw doc_error("xml-invalid", "XML mal forme"); }

            bool eof() const { return i >= s.size(); }
            char peek() const { return i < s.size() ? s[i] : '\0'; }

            void skip_space() { while (i < s.size() && is_space(s[i])) ++i; }

            // Saute une déclaration, un commentaire, une instruction ou une section CDATA.  Rend
            // le texte d'une section CDATA (sinon une chaîne vide), qui est du texte littéral.
            bool skip_special(std::string& cdata)
            {
                cdata.clear();
                if (peek() != '<') return false;
                if (s.compare(i, 4, "<!--") == 0)
                {
                    const std::size_t e = s.find("-->", i + 4);
                    i = (e == std::string::npos) ? s.size() : e + 3;
                    return true;
                }
                if (s.compare(i, 9, "<![CDATA[") == 0)
                {
                    const std::size_t e = s.find("]]>", i + 9);
                    const std::size_t end = (e == std::string::npos) ? s.size() : e;
                    cdata = s.substr(i + 9, end - (i + 9));
                    i = (e == std::string::npos) ? s.size() : e + 3;
                    return true;
                }
                if (peek() == '<' && i + 1 < s.size() && (s[i + 1] == '?' || s[i + 1] == '!'))
                {
                    // Déclaration <?...?>, <!DOCTYPE ...> : on va jusqu'au '>' correspondant (DOCTYPE
                    // peut contenir des crochets, qu'on saute en bloc).
                    if (s.compare(i, 2, "<?") == 0)
                    {
                        const std::size_t e = s.find("?>", i + 2);
                        i = (e == std::string::npos) ? s.size() : e + 2;
                        return true;
                    }
                    int depth = 0;
                    for (; i < s.size(); ++i)
                    {
                        if (s[i] == '[') ++depth;
                        else if (s[i] == ']') { if (depth > 0) --depth; }
                        else if (s[i] == '>' && depth == 0) { ++i; break; }
                    }
                    return true;
                }
                return false;
            }

            // Lit le nom d'une balise ou d'un attribut (lettres, chiffres, ':', '-', '_', '.').
            std::string read_name()
            {
                const std::size_t start = i;
                while (i < s.size())
                {
                    const char c = s[i];
                    if (is_space(c) || c == '>' || c == '/' || c == '=') break;
                    ++i;
                }
                return s.substr(start, i - start);
            }

            // Lit les attributs d'une balise ouvrante jusqu'à '>' ou '/>'.  self_close dit si la
            // balise se ferme d'elle-même.
            void read_attrs(xml_node& node, bool& self_close)
            {
                self_close = false;
                for (;;)
                {
                    skip_space();
                    const char c = peek();
                    if (c == '\0') fail();
                    if (c == '>') { ++i; return; }
                    if (c == '/')
                    {
                        ++i;
                        if (peek() == '>') { ++i; self_close = true; return; }
                        // Un '/' isolé : on tolère.
                        continue;
                    }
                    const std::string aname = read_name();
                    if (aname.empty()) { ++i; continue; }   // on avance pour ne pas boucler
                    skip_space();
                    std::string value;
                    if (peek() == '=')
                    {
                        ++i;
                        skip_space();
                        const char q = peek();
                        if (q == '"' || q == '\'')
                        {
                            ++i;
                            const std::size_t start = i;
                            while (i < s.size() && s[i] != q) ++i;
                            value = s.substr(start, i - start);
                            if (i < s.size()) ++i;   // saute le guillemet fermant
                            value = decode_entities(value);
                        }
                        else
                        {
                            // Valeur non entre guillemets (rare, toléré).
                            const std::size_t start = i;
                            while (i < s.size() && !is_space(s[i]) && s[i] != '>' && s[i] != '/') ++i;
                            value = decode_entities(s.substr(start, i - start));
                        }
                    }
                    node.attrs.emplace_back(local_name(aname), value);
                }
            }

            // Lit le contenu d'un élément (ses enfants) jusqu'à sa balise fermante.
            void read_children(xml_node& node)
            {
                std::string run;   // le texte accumulé entre deux balises
                auto flush_text = [&]() {
                    if (!run.empty())
                    {
                        xml_node t;
                        t.is_text = true;
                        t.text = decode_entities(run);
                        node.children.push_back(std::move(t));
                        run.clear();
                    }
                };
                for (;;)
                {
                    if (eof()) fail();
                    if (s[i] != '<') { run.push_back(s[i++]); continue; }
                    // Balise fermante ?
                    if (s.compare(i, 2, "</") == 0)
                    {
                        flush_text();
                        i += 2;
                        read_name();               // le nom fermant (on ne le vérifie pas strictement)
                        skip_space();
                        if (peek() == '>') ++i; else if (!eof()) ++i;
                        return;
                    }
                    std::string cdata;
                    const std::size_t before = i;
                    if (skip_special(cdata))
                    {
                        if (!cdata.empty()) run += cdata;
                        if (i == before) ++i;   // garde-fou anti-boucle
                        continue;
                    }
                    flush_text();
                    node.children.push_back(read_element());
                }
            }

            // Lit un élément à partir de '<'.
            xml_node read_element()
            {
                if (peek() != '<') fail();
                ++i;
                xml_node node;
                node.name = local_name(read_name());
                bool self_close = false;
                read_attrs(node, self_close);
                if (!self_close) read_children(node);
                return node;
            }

            // Analyse le document entier et rend son élément racine.
            xml_node parse()
            {
                for (;;)
                {
                    skip_space();
                    if (eof()) fail();
                    if (peek() != '<') fail();
                    std::string cdata;
                    if (skip_special(cdata)) continue;   // déclaration, commentaire, PI avant la racine
                    return read_element();
                }
            }
        };

        xml_node parse_xml(const std::string& text)
        {
            xml_parser p(text);
            return p.parse();
        }

        // Tous les descendants (et le nœud lui-même) de nom local donné, en ordre de document
        // (comme getElementsByTagNameNS('*', name), byLocalName() L191).
        void collect_by_name(const xml_node& node, const std::string& name, std::vector<const xml_node*>& out)
        {
            // byLocalName part de la racine passée mais ne l'inclut pas elle-même (c'est getElements…
            // sur un élément) ; on parcourt donc les descendants.
            for (const auto& c : node.children)
            {
                if (c.is_text) continue;
                if (c.name == name) out.push_back(&c);
                collect_by_name(c, name, out);
            }
        }

        std::vector<const xml_node*> by_local_name(const xml_node& root, const std::string& name)
        {
            std::vector<const xml_node*> out;
            collect_by_name(root, name, out);
            return out;
        }

        // Le premier enfant direct de nom local donné (comme child() L195) ; nullptr si absent.
        const xml_node* child(const xml_node* node, const std::string& name)
        {
            if (!node) return nullptr;
            for (const auto& c : node->children)
                if (!c.is_text && c.name == name) return &c;
            return nullptr;
        }

        // Le texte concaténé de tous les nœuds texte d'un sous-arbre (comme la propriété textContent).
        void gather_text(const xml_node& node, std::string& out)
        {
            if (node.is_text) { out += node.text; return; }
            for (const auto& c : node.children) gather_text(c, out);
        }

        std::string text_content(const xml_node& node)
        {
            std::string out;
            gather_text(node, out);
            return out;
        }

        // Le texte de tous les éléments <t> descendants, concaténé (motif fréquent dans le JS).
        std::string t_text(const xml_node& root)
        {
            std::string out;
            for (const xml_node* t : by_local_name(root, "t")) out += text_content(*t);
            return out;
        }

        // -------------------------------------------------------------------------------------
        //  Les relations OOXML (_rels/*.rels)  — reproduit relationships() (L223)
        // -------------------------------------------------------------------------------------

        struct relation { std::string target; std::string type; };

        // La valeur de l'attribut r:id d'un élément (comme relId() L208) : un attribut "id" dont
        // le préfixe est celui des relations.  Notre arbre ne garde pas les URI d'espace de noms,
        // mais dans ces fichiers seul l'attribut de relation porte le nom local "id" sur ces
        // éléments (sldId, sheet…), leur propre identifiant se nommant "id" sans préfixe sur
        // d'autres éléments.  On lit donc l'attribut de nom local "id".
        const std::string* rel_id(const xml_node& node)
        {
            return node.attr("id");
        }

        // Résout une cible relative depuis le dossier d'une partie (comme relationships() L233).
        std::string resolve_target(const std::string& dir, std::string target)
        {
            if (!target.empty() && target[0] == '/') return target.substr(1);
            std::vector<std::string> resolved;
            std::string path = dir + target;
            std::size_t start = 0;
            while (start <= path.size())
            {
                std::size_t slash = path.find('/', start);
                std::string seg = (slash == std::string::npos)
                    ? path.substr(start) : path.substr(start, slash - start);
                if (seg == "..") { if (!resolved.empty()) resolved.pop_back(); }
                else if (seg != "." && !seg.empty()) resolved.push_back(seg);
                if (slash == std::string::npos) break;
                start = slash + 1;
            }
            std::string out;
            for (std::size_t k = 0; k < resolved.size(); ++k)
            {
                if (k) out.push_back('/');
                out += resolved[k];
            }
            return out;
        }

        // L'interface ZIP qu'attendent les extracteurs : un lecteur dlib enveloppé pour donner un
        // accès par nom et par liste, avec décodage du texte.
        struct archive
        {
            const dlib::zip_archive_reader& zip;

            explicit archive(const dlib::zip_archive_reader& z) : zip(z) {}

            bool has(const std::string& name) const { return zip.contains(name); }

            std::vector<std::string> names() const
            {
                std::vector<std::string> out;
                const auto& e = zip.entries();
                out.reserve(e.size());
                for (const auto& info : e) out.push_back(info.name);
                return out;
            }

            // Le texte d'une entrée (décompressée et décodée), ou "" si l'entrée est absente.
            // Comme zip.text() du JS : rend "" pour une entrée absente, lève pour une archive
            // endommagée.
            std::string text(const std::string& name) const
            {
                if (!zip.contains(name)) return std::string();
                std::string bytes;
                try { bytes = zip.read(name); }
                catch (const dlib::zip_error& e) { throw doc_error("zip-invalid", e.info); }
                catch (const std::exception& e) { throw doc_error("zip-invalid", e.what()); }
                return decode_text(bytes);
            }
        };

        // Les relations d'une partie : id -> {target, type}.  Reproduit relationships() (L223).
        std::map<std::string, relation> relationships(const archive& zip, const std::string& part)
        {
            std::map<std::string, relation> out;
            const std::size_t slash = part.find_last_of('/');
            const std::string dir = (slash == std::string::npos) ? std::string() : part.substr(0, slash + 1);
            const std::string base = (slash == std::string::npos) ? part : part.substr(slash + 1);
            const std::string file = dir + "_rels/" + base + ".rels";
            const std::string text = zip.text(file);
            if (text.empty()) return out;
            const xml_node doc = parse_xml(text);
            for (const xml_node* r : by_local_name(doc, "Relationship"))
            {
                const std::string* mode = r->attr("TargetMode");
                if (mode && *mode == "External") continue;
                const std::string* id = r->attr("Id");
                if (!id) continue;
                const std::string* tgt = r->attr("Target");
                const std::string* type = r->attr("Type");
                relation rel;
                rel.target = resolve_target(dir, tgt ? *tgt : std::string());
                rel.type = type ? *type : std::string();
                out[*id] = rel;
            }
            return out;
        }

        // Classe deux noms "…N.xml" par leur numéro (comme numericOrder() L216).
        int trailing_number(const std::string& name)
        {
            // Le nombre juste avant ".xml" à la fin du nom.
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".xml") != 0) return 0;
            std::size_t end = name.size() - 4;
            std::size_t start = end;
            while (start > 0 && name[start - 1] >= '0' && name[start - 1] <= '9') --start;
            if (start == end) return 0;
            try { return std::stoi(name.substr(start, end - start)); }
            catch (...) { return 0; }
        }

        void sort_numeric(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
                return trailing_number(a) < trailing_number(b);
            });
        }

        // Teste une expression régulière simple du JS par du code équivalent (on ne porte pas de
        // moteur regex) : vrai si val vaut "0", "false" ou "off" (insensible à la casse),
        // c.-à-d. /^(0|false|off)$/i — utilisé pour pageBreakBefore.
        bool is_off(const std::string& val)
        {
            const std::string v = to_lower(trim(val));
            return v == "0" || v == "false" || v == "off";
        }

        // -------------------------------------------------------------------------------------
        //  Les graphiques OOXML  — reproduit chartText() (L427)
        // -------------------------------------------------------------------------------------

        std::string chart_text(const archive& zip, const std::string& part)
        {
            const std::string text = zip.text(part);
            if (text.empty()) return std::string();
            const xml_node doc = parse_xml(text);

            // Le titre : le texte des <t> sous les <title>.
            std::string title;
            {
                std::vector<std::string> pieces;
                for (const xml_node* tl : by_local_name(doc, "title"))
                {
                    const std::string s = t_text(*tl);
                    if (!s.empty()) pieces.push_back(s);
                }
                for (std::size_t k = 0; k < pieces.size(); ++k) { if (k) title += ' '; title += pieces[k]; }
                title = trim(title);
            }
            // Les séries : le texte des <v> sous <tx> (ou sous la série), sans doublon.
            std::vector<std::string> series;
            for (const xml_node* ser : by_local_name(doc, "ser"))
            {
                const xml_node* tx = child(ser, "tx");
                const xml_node& from = tx ? *tx : *ser;
                std::string name;
                const auto vs = by_local_name(from, "v");
                for (std::size_t k = 0; k < vs.size(); ++k) { if (k) name += ' '; name += text_content(*vs[k]); }
                name = trim(name);
                if (!name.empty() && std::find(series.begin(), series.end(), name) == series.end())
                    series.push_back(name);
            }
            // Les catégories : les <v> du premier <cat>, sans doublon.
            std::vector<std::string> categories;
            for (const xml_node* cat : by_local_name(doc, "cat"))
            {
                for (const xml_node* v : by_local_name(*cat, "v"))
                {
                    const std::string s = text_content(*v);
                    if (std::find(categories.begin(), categories.end(), s) == categories.end())
                        categories.push_back(s);
                }
                break;   // seulement le premier <cat>, comme le JS
            }
            std::vector<std::string> parts;
            if (!title.empty()) parts.push_back(title);
            if (!series.empty())
            {
                std::string s = "series: ";
                for (std::size_t k = 0; k < series.size(); ++k) { if (k) s += ", "; s += series[k]; }
                parts.push_back(s);
            }
            if (!categories.empty())
            {
                std::string s = "categories: ";
                const std::size_t lim = std::min<std::size_t>(categories.size(), 40);
                for (std::size_t k = 0; k < lim; ++k) { if (k) s += ", "; s += categories[k]; }
                parts.push_back(s);
            }
            if (parts.empty()) return std::string();
            std::string out = "Chart: ";
            for (std::size_t k = 0; k < parts.size(); ++k) { if (k) out += "; "; out += parts[k]; }
            return out;
        }

        // -------------------------------------------------------------------------------------
        //  Word (docx/docm/dotx)  — reproduit docxText() (L252)
        // -------------------------------------------------------------------------------------

        // Une ligne du texte intermédiaire : soit un marqueur de page (page=true, n=numéro),
        // soit du texte.
        struct line { bool is_page = false; int n = 0; std::string text; };

        // Teste le style d'un titre (headingStyle L250) : rend le niveau 1..9, ou 0 si ce n'est
        // pas un style de titre.
        int heading_level(const std::string& style_lc)
        {
            // /^(heading|titre|berschrift|überschrift|titolo|título|titulo)\s*([1-9])$/i
            static const std::vector<std::string> prefixes = {
                "heading", "titre", "berschrift", "\xc3\xbc" "berschrift", "titolo",
                "t\xc3\xad" "tulo", "titulo"
            };
            for (const std::string& p : prefixes)
            {
                if (style_lc.compare(0, p.size(), p) != 0) continue;
                std::size_t k = p.size();
                while (k < style_lc.size() && is_space(style_lc[k])) ++k;
                if (k + 1 == style_lc.size() && style_lc[k] >= '1' && style_lc[k] <= '9')
                    return style_lc[k] - '0';
            }
            return 0;
        }

        // Teste le style d'un titre principal (L281) : /^(title|titre|titel|titolo|título)$/i
        bool is_title_style(const std::string& style_lc)
        {
            return style_lc == "title" || style_lc == "titre" || style_lc == "titel"
                || style_lc == "titolo" || style_lc == "t\xc3\xad" "tulo";
        }

        class docx_reader
        {
        public:
            explicit docx_reader(const archive& z) : zip(z) {}

            std::string run()
            {
                const std::string body_xml = zip.text("word/document.xml");
                // parseXml(await zip.text('word/document.xml')) lèverait sur un texte vide.
                const xml_node doc = parse_xml(body_xml);
                const auto bodies = by_local_name(doc, "body");
                const xml_node& root = bodies.empty() ? doc : *bodies[0];
                blocks(root);

                std::vector<std::string> out;
                int current = 1;
                if (page_marks) out.push_back("## Page 1");
                for (const auto& l : lines)
                {
                    if (l.is_page) { current = l.n; out.push_back(""); out.push_back("## Page " + std::to_string(current)); }
                    else out.push_back(l.text);
                }
                // Retire les titres de page vides en fin de document.
                while (!out.empty() && is_page_heading(out.back())) out.pop_back();

                // Les parties hors du corps.
                extra(out, "Headers and footers", &docx_reader::is_header_footer, std::string());
                extra(out, "Footnotes", &docx_reader::is_footnotes, "footnote");
                extra(out, "Endnotes", &docx_reader::is_endnotes, "endnote");
                extra(out, "Comments", &docx_reader::is_comments, "comment");

                std::string joined;
                for (std::size_t k = 0; k < out.size(); ++k) { if (k) joined.push_back('\n'); joined += out[k]; }
                return collapse_blank_lines(joined);
            }

        private:
            const archive& zip;
            std::vector<line> lines;
            bool page_marks = false;
            int page = 1;
            long long size = 0;

            static bool is_page_heading(const std::string& s)
            {
                // /^## Page \d+$/
                if (s.compare(0, 8, "## Page ") != 0) return false;
                if (s.size() == 8) return false;
                for (std::size_t k = 8; k < s.size(); ++k) if (s[k] < '0' || s[k] > '9') return false;
                return true;
            }

            // Reproduit pageBreak() (L261) : ouvre une page seulement si du texte a été écrit
            // depuis le dernier saut.
            void page_break()
            {
                bool text = false;
                for (std::size_t k = lines.size(); k-- > 0 && !lines[k].is_page; )
                    text = text || has_text(lines[k].text);
                if (!text) return;
                page += 1;
                page_marks = true;
                line m; m.is_page = true; m.n = page;
                lines.push_back(m);
            }

            // Le préfixe "## "/"# "/"- " d'un paragraphe selon son pPr (logique de paragraph() L271).
            std::string paragraph_prefix(const xml_node* ppr)
            {
                if (!ppr) return std::string();
                const xml_node* p_style = child(ppr, "pStyle");
                const std::string style = p_style ? (p_style->attr("val") ? *p_style->attr("val") : "") : "";
                const std::string style_lc = to_lower(style);
                const xml_node* out_lvl = child(ppr, "outlineLvl");
                const std::string* outline = out_lvl ? out_lvl->attr("val") : nullptr;

                const int lvl = heading_level(style_lc);
                if (lvl)
                    return std::string(std::min(6, lvl + 1), '#') + " ";
                if (is_title_style(style_lc))
                    return "# ";
                if (outline)
                {
                    const std::string o = *outline;
                    if (o.size() == 1 && o[0] >= '0' && o[0] <= '8')
                        return std::string(std::min(6, (o[0] - '0') + 2), '#') + " ";
                }
                if (child(ppr, "numPr")) return "- ";
                return std::string();
            }

            // Le texte d'un paragraphe (paragraph() L271).
            void paragraph(const xml_node& p)
            {
                const xml_node* ppr = child(&p, "pPr");
                const xml_node* before = ppr ? child(ppr, "pageBreakBefore") : nullptr;
                if (before)
                {
                    const std::string* v = before->attr("val");
                    if (!v || !is_off(*v)) page_break();
                }
                const std::string prefix = paragraph_prefix(ppr);

                std::string s;
                auto flush = [&]() {
                    if (has_text(s))
                    {
                        line l; l.text = prefix + s;
                        lines.push_back(l);
                        size += static_cast<long long>(prefix.size() + s.size() + 1);
                    }
                    s.clear();
                };
                // walk() (L293) : parcourt les runs et traduit les éléments de texte.
                std::function<void(const xml_node&)> walk = [&](const xml_node& n) {
                    for (const auto& c : n.children)
                    {
                        if (c.is_text) continue;
                        const std::string& name = c.name;
                        if (name == "t") s += text_content(c);
                        else if (name == "tab") s += '\t';
                        else if (name == "br")
                        {
                            const std::string* type = c.attr("type");
                            if (type && *type == "page") { flush(); page_break(); }
                            else s += '\n';
                        }
                        else if (name == "cr") s += '\n';
                        else if (name == "lastRenderedPageBreak") { flush(); page_break(); }
                        else if (name == "delText" || name == "instrText" || name == "Fallback"
                              || name == "pPr" || name == "rPr") continue;
                        else if (name == "p") { s += '\n'; walk(c); s += '\n'; }
                        else walk(c);
                    }
                };
                walk(p);
                flush();
                // Une section qui commence sur une nouvelle page (L322).
                const xml_node* sect = ppr ? child(ppr, "sectPr") : nullptr;
                if (sect)
                {
                    const xml_node* tp = child(sect, "type");
                    const std::string* kind = tp ? tp->attr("val") : nullptr;
                    if (!kind || *kind == "nextPage" || *kind == "oddPage" || *kind == "evenPage")
                        page_break();
                }
            }

            // Un tableau (table() L327).
            void table(const xml_node& tbl)
            {
                for (const auto& row : tbl.children)
                {
                    if (row.is_text || row.name != "tr") continue;
                    if (size > static_cast<long long>(max_text_chars)) break;
                    std::vector<std::string> cells;
                    for (const auto& cell : row.children)
                    {
                        if (cell.is_text || cell.name != "tc") continue;
                        const std::size_t before = lines.size();
                        blocks(cell);
                        // Le texte des lignes ajoutées par cette cellule (hors marqueurs de page), joint.
                        std::string joined;
                        bool first = true;
                        for (std::size_t k = before; k < lines.size(); ++k)
                        {
                            if (lines[k].is_page) continue;
                            if (!first) joined.push_back(' ');
                            joined += lines[k].text;
                            first = false;
                        }
                        lines.resize(before);   // splice : on retire les lignes de la cellule
                        cells.push_back(squeeze_spaces(joined));
                    }
                    bool any = false;
                    for (const auto& c : cells) if (!c.empty()) any = true;
                    if (any)
                    {
                        std::string joined;
                        for (std::size_t k = 0; k < cells.size(); ++k) { if (k) joined += " | "; joined += cells[k]; }
                        line l; l.text = joined; lines.push_back(l);
                    }
                }
                line empty; lines.push_back(empty);   // lines.push('')
            }

            // Les paragraphes et tableaux d'un conteneur (blocks() L343).
            void blocks(const xml_node& container)
            {
                for (const auto& node : container.children)
                {
                    if (size > static_cast<long long>(max_text_chars)) return;
                    if (node.is_text) continue;
                    if (node.name == "p") paragraph(node);
                    else if (node.name == "tbl") table(node);
                    else if (node.name == "sdt")
                    {
                        const xml_node* content = child(&node, "sdtContent");
                        if (content) blocks(*content);
                    }
                    else if (node.name == "customXml" || node.name == "smartTag") blocks(node);
                }
            }

            // Les filtres de parties (les motifs des regex du JS, écrits en code).
            static bool is_header_footer(const std::string& n)
            {
                // /^word\/(header|footer)\d*\.xml$/
                if (n.compare(0, 5, "word/") != 0) return false;
                std::size_t k = 5;
                if (n.compare(k, 6, "header") == 0) k += 6;
                else if (n.compare(k, 6, "footer") == 0) k += 6;
                else return false;
                std::size_t d = k;
                while (d < n.size() && n[d] >= '0' && n[d] <= '9') ++d;
                return n.compare(d, std::string::npos, ".xml") == 0 && d == n.size() - 4;
            }
            static bool is_footnotes(const std::string& n) { return n == "word/footnotes.xml"; }
            static bool is_endnotes(const std::string& n) { return n == "word/endnotes.xml"; }
            static bool is_comments(const std::string& n) { return n == "word/comments.xml"; }

            // extra() (L371) : agrège le texte de parties annexes sous un titre "## ".
            void extra(std::vector<std::string>& out, const std::string& title,
                       bool (*pattern)(const std::string&), const std::string& item)
            {
                std::vector<std::string> matched;
                for (const std::string& n : zip.names()) if (pattern(n)) matched.push_back(n);
                sort_numeric(matched);
                std::vector<std::string> texts;
                for (const std::string& name : matched)
                {
                    if (size > static_cast<long long>(max_text_chars)) break;
                    const xml_node part = parse_xml(zip.text(name));
                    lines.clear();
                    std::vector<const xml_node*> items;
                    if (!item.empty()) items = by_local_name(part, item);
                    else items.push_back(&part);
                    for (const xml_node* it : items)
                    {
                        const std::string* type = it->attr("type");
                        // /separator|continuation/i
                        if (type)
                        {
                            const std::string t = to_lower(*type);
                            if (t.find("separator") != std::string::npos
                             || t.find("continuation") != std::string::npos) continue;
                        }
                        blocks(*it);
                    }
                    std::string joined;
                    bool first = true;
                    for (const auto& l : lines)
                    {
                        if (l.is_page) continue;
                        if (!first) joined.push_back('\n');
                        joined += l.text;
                        first = false;
                    }
                    joined = trim(joined);
                    if (!joined.empty() && std::find(texts.begin(), texts.end(), joined) == texts.end())
                        texts.push_back(joined);
                }
                if (!texts.empty())
                {
                    out.push_back("");
                    out.push_back("## " + title);
                    std::string joined;
                    for (std::size_t k = 0; k < texts.size(); ++k) { if (k) joined += "\n\n"; joined += texts[k]; }
                    out.push_back(joined);
                }
            }
        };

        // -------------------------------------------------------------------------------------
        //  PowerPoint (pptx/pptm/ppsx/potx)  — reproduit pptxText() (L449)
        // -------------------------------------------------------------------------------------

        // Le type d'un espace réservé d'une forme (placeholderType() L422) : "" si absent, sinon
        // le type, ou "body" par défaut quand l'élément <ph> n'a pas d'attribut type.
        std::string placeholder_type(const xml_node& sp)
        {
            const auto phs = by_local_name(sp, "ph");
            if (phs.empty()) return std::string();   // null en JS -> on rend "" (jamais égal à un type)
            const std::string* type = phs[0]->attr("type");
            return type ? *type : std::string("body");
        }

        // Les paragraphes de chaque forme et tableau d'une racine (shapeTexts() L397).
        std::vector<std::string> shape_texts(const xml_node& root)
        {
            std::vector<std::string> out;
            std::function<void(const xml_node&)> walk = [&](const xml_node& n) {
                for (const auto& c : n.children)
                {
                    if (c.is_text) continue;
                    if (c.name == "tbl")
                    {
                        for (const xml_node* row : by_local_name(c, "tr"))
                        {
                            std::vector<std::string> cells;
                            for (const xml_node* cell : by_local_name(*row, "tc"))
                            {
                                std::string s;
                                const auto ps = by_local_name(*cell, "p");
                                for (std::size_t k = 0; k < ps.size(); ++k)
                                {
                                    if (k) s += ' ';
                                    s += t_text(*ps[k]);
                                }
                                cells.push_back(trim(s));
                            }
                            bool any = false;
                            for (const auto& x : cells) if (!x.empty()) any = true;
                            if (any)
                            {
                                std::string joined;
                                for (std::size_t k = 0; k < cells.size(); ++k) { if (k) joined += " | "; joined += cells[k]; }
                                out.push_back(joined);
                            }
                        }
                    }
                    else if (c.name == "txBody")
                    {
                        for (const auto& p : c.children)
                        {
                            if (p.is_text || p.name != "p") continue;
                            const std::string text = t_text(p);
                            if (has_text(text)) out.push_back(text);
                        }
                    }
                    else if (c.name != "Fallback") walk(c);
                }
            };
            walk(root);
            return out;
        }

        std::string pptx_text(const archive& zip)
        {
            // Les diapositives dans l'ordre de la présentation.
            std::vector<std::string> slides;
            const std::string pres = zip.text("ppt/presentation.xml");
            if (!pres.empty())
            {
                const auto rels = relationships(zip, "ppt/presentation.xml");
                const xml_node doc = parse_xml(pres);
                for (const xml_node* id : by_local_name(doc, "sldId"))
                {
                    const std::string* rid = rel_id(*id);
                    if (!rid) continue;
                    auto it = rels.find(*rid);
                    if (it != rels.end() && zip.has(it->second.target)) slides.push_back(it->second.target);
                }
            }
            if (slides.empty())
            {
                for (const std::string& n : zip.names())
                {
                    // /^ppt\/slides\/slide\d+\.xml$/  ("ppt/slides/slide" fait 16 caractères)
                    if (n.compare(0, 16, "ppt/slides/slide") != 0) continue;
                    std::size_t d = 16;
                    while (d < n.size() && n[d] >= '0' && n[d] <= '9') ++d;
                    if (d == 16) continue;
                    if (n.compare(d, std::string::npos, ".xml") != 0 || d != n.size() - 4) continue;
                    slides.push_back(n);
                }
                sort_numeric(slides);
            }

            std::vector<std::string> out;
            int i = 0;
            long long size = 0;
            for (const std::string& s : slides)
            {
                if (size > static_cast<long long>(max_text_chars)) break;
                const xml_node doc = parse_xml(zip.text(s));
                const std::string* show = doc.attr("show");
                const bool hidden = show && *show == "0";

                std::string title;
                for (const xml_node* sp : by_local_name(doc, "sp"))
                {
                    const std::string type = placeholder_type(*sp);
                    if (type == "title" || type == "ctrTitle")
                    {
                        const auto texts = shape_texts(*sp);
                        std::string joined;
                        for (std::size_t k = 0; k < texts.size(); ++k) { if (k) joined += ' '; joined += texts[k]; }
                        title = trim(joined);
                        break;
                    }
                }
                std::vector<std::string> body = shape_texts(doc);
                if (!title.empty() && !body.empty() && trim(body[0]) == title)
                    body.erase(body.begin());

                const auto rels = relationships(zip, s);
                std::vector<std::string> extras;
                for (const auto& kv : rels)
                {
                    const relation& r = kv.second;
                    if (ends_with(r.type, "/chart"))
                    {
                        const std::string c = chart_text(zip, r.target);
                        if (!c.empty()) extras.push_back(c);
                    }
                    else if (ends_with(r.type, "/diagramData"))
                    {
                        const std::string text = zip.text(r.target);
                        if (!text.empty())
                        {
                            const xml_node dg = parse_xml(text);
                            std::vector<std::string> items;
                            for (const xml_node* p : by_local_name(dg, "p"))
                            {
                                const std::string s2 = t_text(*p);
                                if (has_text(s2)) items.push_back(s2);
                            }
                            if (!items.empty())
                            {
                                std::string joined;
                                for (std::size_t k = 0; k < items.size(); ++k) { if (k) joined += '\n'; joined += items[k]; }
                                extras.push_back(joined);
                            }
                        }
                    }
                }
                std::string notes;
                for (const auto& kv : rels)
                {
                    const relation& r = kv.second;
                    if (!ends_with(r.type, "/notesSlide")) continue;
                    const std::string text = zip.text(r.target);
                    if (text.empty()) continue;
                    const xml_node nd = parse_xml(text);
                    std::vector<std::string> parts;
                    for (const xml_node* sp : by_local_name(nd, "sp"))
                    {
                        const std::string type = placeholder_type(*sp);
                        if (type == "sldImg" || type == "sldNum" || type == "hdr" || type == "ftr" || type == "dt")
                            continue;
                        const auto st = shape_texts(*sp);
                        parts.insert(parts.end(), st.begin(), st.end());
                    }
                    std::string joined;
                    for (std::size_t k = 0; k < parts.size(); ++k) { if (k) joined += '\n'; joined += parts[k]; }
                    notes = trim(joined);
                }

                std::string head = "## Slide " + std::to_string(++i);
                if (!title.empty()) head += ": " + title;
                if (hidden) head += " (hidden)";
                std::vector<std::string> block;
                block.push_back(head);
                block.insert(block.end(), body.begin(), body.end());
                block.insert(block.end(), extras.begin(), extras.end());
                if (!notes.empty()) block.push_back("Notes: " + notes);
                std::string text;
                for (std::size_t k = 0; k < block.size(); ++k) { if (k) text.push_back('\n'); text += block[k]; }
                out.push_back(text);
                size += static_cast<long long>(text.size() + 2);
            }
            std::string joined;
            for (std::size_t k = 0; k < out.size(); ++k) { if (k) joined += "\n\n"; joined += out[k]; }
            return joined;
        }

        // -------------------------------------------------------------------------------------
        //  Excel (xlsx/xlsm/xltx)  — reproduit xlsxText() (L551)
        // -------------------------------------------------------------------------------------

        // L'indice de colonne d'une référence (A -> 0, B -> 1, AA -> 26…).  columnIndex() (L544).
        int column_index(const std::string& ref)
        {
            long long n = 0;
            for (char c : ref)
            {
                const char up = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
                if (up < 'A' || up > 'Z') continue;
                n = n * 26 + (up - 'A' + 1);
            }
            return static_cast<int>(n - 1);
        }

        // Entoure une valeur de guillemets si elle contient une virgule, un guillemet ou un saut
        // de ligne (csvField() L540).
        std::string csv_field(const std::string& value)
        {
            bool quote = false;
            for (char c : value) if (c == '"' || c == ',' || c == '\n' || c == '\r') { quote = true; break; }
            if (!quote) return value;
            std::string out = "\"";
            for (char c : value) { if (c == '"') out += "\"\""; else out.push_back(c); }
            out.push_back('"');
            return out;
        }

        /*
            Note de portage : la page convertit les dates Excel (serialDate() L530) et reconnaît
            les formats de date (isDateFormat() L523).  On reproduit ici le comportement du JS :
            un nombre dont le style est un format de date est rendu en ISO ("AAAA-MM-JJ" ou avec
            l'heure).  On lit la table des styles comme le JS.  Voir la note "approximation" du
            rapport pour la seule différence (l'arrondi d'une seconde).
        */
        // Le quantième de jour d'une date grégorienne depuis l'époque Unix (jours depuis 1970-01-01).
        long long days_from_civil(long long y, unsigned m, unsigned d)
        {
            y -= m <= 2;
            const long long era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<long long>(doe) - 719468;
        }

        // La date grégorienne d'un quantième de jour Unix.
        void civil_from_days(long long z, long long& y, unsigned& m, unsigned& d)
        {
            z += 719468;
            const long long era = (z >= 0 ? z : z - 146096) / 146097;
            const unsigned doe = static_cast<unsigned>(z - era * 146097);
            const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            y = static_cast<long long>(yoe) + era * 400;
            const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const unsigned mp = (5 * doy + 2) / 153;
            d = doy - (153 * mp + 2) / 5 + 1;
            m = mp + (mp < 10 ? 3 : -9);
            y += (m <= 2);
        }

        std::string two(long long v) { std::string s = std::to_string(v); return s.size() < 2 ? "0" + s : s; }

        // serialDate() (L530).  base1904 décale l'origine.
        std::string serial_date(double serial, bool date1904)
        {
            // L'origine, en jours Unix : 1904-01-01 ou 1899-12-30.
            const long long base_days = date1904 ? days_from_civil(1904, 1, 1) : days_from_civil(1899, 12, 30);
            // Les millisecondes depuis l'époque, arrondies (Math.round(serial * 86400000)).
            const long long ms = static_cast<long long>(std::llround(serial * 86400000.0))
                               + base_days * 86400000LL;
            long long day = ms / 86400000LL;
            long long rem = ms % 86400000LL;
            if (rem < 0) { rem += 86400000LL; --day; }
            long long y; unsigned mo, dd;
            civil_from_days(day, y, mo, dd);
            const long long secs = rem / 1000;
            const long long hh = secs / 3600, mm = (secs % 3600) / 60, ss = secs % 60;
            const std::string time = two(hh) + ":" + two(mm) + ":" + two(ss);
            const std::string date = std::to_string(y) + "-" + two(mo) + "-" + two(dd);
            if (serial < 1) return time;                       // seulement l'heure
            if (time == "00:00:00") return date;               // seulement la date
            // iso.slice(0,10) + ' ' + time.replace(/:00$/, '') : retire les secondes nulles finales.
            std::string t = time;
            if (t.size() >= 3 && t.compare(t.size() - 3, 3, ":00") == 0) t = t.substr(0, t.size() - 3);
            return date + " " + t;
        }

        const std::set<int>& builtin_date_formats()
        {
            static const std::set<int> set = { 14,15,16,17,18,19,20,21,22,45,46,47 };
            return set;
        }

        // isDateFormat() (L523), porté.  On nettoie les textes entre guillemets, les caractères
        // échappés et les crochets (hors [h]/[m]/[s]), puis on cherche un d/m/y/h/s, le format
        // "General" mis à part.
        bool is_date_format(const std::string& code)
        {
            std::string s;
            s.reserve(code.size());
            for (std::size_t k = 0; k < code.size(); )
            {
                const char c = code[k];
                if (c == '"')
                {
                    // Retire "…" (le texte littéral).
                    std::size_t e = code.find('"', k + 1);
                    k = (e == std::string::npos) ? code.size() : e + 1;
                }
                else if (c == '\\')
                {
                    // Retire \x (le caractère échappé et son antislash).
                    k += 2;
                }
                else if (c == '[')
                {
                    // Retire […] sauf [h]/[m]/[mm]/[s]/[ss] (durées, qui portent une unité de temps).
                    std::size_t e = code.find(']', k + 1);
                    const std::string inside = (e == std::string::npos)
                        ? std::string() : code.substr(k + 1, e - k - 1);
                    const std::string low = to_lower(inside);
                    const bool keep = (low == "h" || low == "m" || low == "mm" || low == "s" || low == "ss");
                    if (keep) { s += code.substr(k, (e - k) + 1); }
                    k = (e == std::string::npos) ? code.size() : e + 1;
                }
                else { s.push_back(c); ++k; }
            }
            // /[dmyhs]/i sur le résultat, et pas "general".
            bool has_dt = false;
            for (char c : s)
            {
                const char l = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
                if (l == 'd' || l == 'm' || l == 'y' || l == 'h' || l == 's') { has_dt = true; break; }
            }
            return has_dt && to_lower(trim(s)) != "general";
        }

        bool parse_bool_attr(const std::string& v)
        {
            // /^(1|true)$/i
            const std::string l = to_lower(v);
            return l == "1" || l == "true";
        }

        std::string xlsx_text(const archive& zip)
        {
            // Les chaînes partagées.
            std::vector<std::string> shared;
            const std::string sst = zip.text("xl/sharedStrings.xml");
            if (!sst.empty())
            {
                const xml_node doc = parse_xml(sst);
                for (const xml_node* si : by_local_name(doc, "si"))
                {
                    // Concatène les <t> qui ne sont pas sous un <rPh> (guide phonétique).
                    std::string s;
                    std::function<void(const xml_node&, bool)> walk = [&](const xml_node& n, bool in_rph) {
                        for (const auto& c : n.children)
                        {
                            if (c.is_text) continue;
                            const bool rph = in_rph || c.name == "rPh";
                            if (c.name == "t" && !rph) s += text_content(c);
                            else walk(c, rph);
                        }
                    };
                    walk(*si, false);
                    shared.push_back(s);
                }
            }

            // Les styles qui affichent une date.
            std::set<int> date_styles;
            const std::string styles = zip.text("xl/styles.xml");
            if (!styles.empty())
            {
                const xml_node sd = parse_xml(styles);
                std::map<int, std::string> custom;
                for (const xml_node* f : by_local_name(sd, "numFmt"))
                {
                    const std::string* id = f->attr("numFmtId");
                    const std::string* code = f->attr("formatCode");
                    if (id) { try { custom[std::stoi(*id)] = code ? *code : ""; } catch (...) {} }
                }
                const auto cell_xfs = by_local_name(sd, "cellXfs");
                if (!cell_xfs.empty())
                {
                    int k = 0;
                    for (const auto& xf : cell_xfs[0]->children)
                    {
                        if (xf.is_text || xf.name != "xf") continue;
                        const std::string* idp = xf.attr("numFmtId");
                        int id = 0;
                        if (idp) { try { id = std::stoi(*idp); } catch (...) { id = 0; } }
                        const bool builtin = builtin_date_formats().count(id) != 0;
                        bool custom_date = false;
                        auto it = custom.find(id);
                        if (it != custom.end()) custom_date = is_date_format(it->second);
                        if (builtin || custom_date) date_styles.insert(k);
                        ++k;
                    }
                }
            }

            // Les feuilles dans l'ordre de leurs onglets.
            struct sheet_info { std::string name; std::string state; std::string part; bool has_part = false; };
            std::vector<sheet_info> sheets;
            bool date1904 = false;
            const std::string wb = zip.text("xl/workbook.xml");
            if (!wb.empty())
            {
                const xml_node wd = parse_xml(wb);
                const auto prs = by_local_name(wd, "workbookPr");
                if (!prs.empty())
                {
                    const std::string* d = prs[0]->attr("date1904");
                    date1904 = d && parse_bool_attr(*d);
                }
                const auto rels = relationships(zip, "xl/workbook.xml");
                for (const xml_node* s : by_local_name(wd, "sheet"))
                {
                    sheet_info info;
                    const std::string* nm = s->attr("name");
                    info.name = nm ? *nm : "";
                    const std::string* st = s->attr("state");
                    info.state = st ? *st : "visible";
                    const std::string* rid = rel_id(*s);
                    if (rid) { auto it = rels.find(*rid); if (it != rels.end()) { info.part = it->second.target; info.has_part = true; } }
                    sheets.push_back(info);
                }
            }
            // Repli : si aucune feuille déclarée ne pointe vers une partie présente.
            bool any_sheet = false;
            for (const auto& s : sheets) if (s.has_part && zip.has(s.part)) any_sheet = true;
            if (!any_sheet)
            {
                std::vector<std::string> parts;
                for (const std::string& n : zip.names())
                {
                    // /^xl\/worksheets\/sheet\d+\.xml$/  ("xl/worksheets/sheet" fait 19 caractères)
                    if (n.compare(0, 19, "xl/worksheets/sheet") != 0) continue;
                    std::size_t d = 19;
                    while (d < n.size() && n[d] >= '0' && n[d] <= '9') ++d;
                    if (d == 19) continue;
                    if (n.compare(d, std::string::npos, ".xml") != 0 || d != n.size() - 4) continue;
                    parts.push_back(n);
                }
                sort_numeric(parts);
                int k = 0;
                for (const std::string& n : parts)
                {
                    sheet_info info;
                    info.name = "Sheet " + std::to_string(++k);
                    info.state = "visible";
                    info.part = n; info.has_part = true;
                    sheets.push_back(info);
                }
            }

            std::vector<std::string> result;
            long long size = 0;
            for (const auto& sheet : sheets)
            {
                if (size > static_cast<long long>(max_text_chars)) break;
                if (!sheet.has_part || !zip.has(sheet.part)) continue;
                if (sheet.part.find("worksheets/") == std::string::npos)
                {
                    // Une feuille de graphique : son graphique.
                    const auto rels = relationships(zip, sheet.part);
                    std::vector<std::string> charts;
                    for (const auto& kv : rels)
                    {
                        if (!ends_with(kv.second.type, "/drawing")) continue;
                        const auto drels = relationships(zip, kv.second.target);
                        for (const auto& d : drels)
                            if (ends_with(d.second.type, "/chart")) charts.push_back(chart_text(zip, d.second.target));
                    }
                    std::string text = "## Sheet: " + sheet.name + "\n";
                    bool first = true;
                    for (const std::string& c : charts)
                    {
                        if (c.empty()) continue;
                        if (!first) text.push_back('\n');
                        text += c; first = false;
                    }
                    result.push_back(text);
                    size += static_cast<long long>(text.size() + 2);
                    continue;
                }
                const xml_node doc = parse_xml(zip.text(sheet.part));
                std::vector<std::string> lines;
                long long rows = 0, skipped = 0;
                for (const xml_node* row : by_local_name(doc, "row"))
                {
                    std::map<int, std::string> cells;
                    int next = 0;
                    int max_col = -1;
                    for (const auto& c : row->children)
                    {
                        if (c.is_text || c.name != "c") continue;
                        const std::string* tp = c.attr("t");
                        const std::string type = tp ? *tp : "n";
                        const xml_node* v = child(&c, "v");
                        std::string value;
                        if (type == "s" && v)
                        {
                            try { const int idx = std::stoi(text_content(*v)); if (idx >= 0 && idx < static_cast<int>(shared.size())) value = shared[idx]; }
                            catch (...) {}
                        }
                        else if (type == "inlineStr") value = t_text(c);
                        else if (type == "b" && v) value = (text_content(*v) == "1") ? "TRUE" : "FALSE";
                        else if (v)
                        {
                            value = text_content(*v);
                            const std::string* sp = c.attr("s");
                            int style = 0;
                            if (sp) { try { style = std::stoi(*sp); } catch (...) { style = 0; } }
                            if (type == "n" && date_styles.count(style) && !value.empty())
                            {
                                // isFinite(Number(value)) : un nombre fini.
                                try
                                {
                                    std::size_t pos = 0;
                                    const double num = std::stod(value, &pos);
                                    if (pos == value.size()) value = serial_date(num, date1904);
                                }
                                catch (...) {}
                            }
                        }
                        else
                        {
                            const xml_node* f = child(&c, "f");
                            if (f) { const std::string ftext = text_content(*f); if (!ftext.empty()) value = "=" + ftext; }
                        }
                        const std::string* ref = c.attr("r");
                        const int k = ref ? column_index(*ref) : next;
                        next = k + 1;
                        if (k >= 0 && k < 16384 && !value.empty())
                        {
                            cells[k] = csv_field(value);
                            if (k > max_col) max_col = k;
                        }
                    }
                    if (cells.empty()) continue;
                    if (rows >= static_cast<long long>(max_sheet_rows) || size > static_cast<long long>(max_text_chars))
                    {
                        ++skipped;
                        continue;
                    }
                    // Array.from(cells, (x) => x || '').join(',') : les trous deviennent vides, jusqu'au dernier défini.
                    std::string line;
                    for (int k = 0; k <= max_col; ++k)
                    {
                        if (k) line.push_back(',');
                        auto it = cells.find(k);
                        if (it != cells.end()) line += it->second;
                    }
                    lines.push_back(line);
                    size += static_cast<long long>(line.size() + 1);
                    ++rows;
                }
                if (skipped) lines.push_back("(" + std::to_string(skipped) + " more rows)");
                const std::string state = (sheet.state != "visible") ? " (hidden)" : "";
                std::string text = "## Sheet: " + sheet.name + state + "\n";
                for (std::size_t k = 0; k < lines.size(); ++k) { if (k) text.push_back('\n'); text += lines[k]; }
                result.push_back(text);
            }
            std::string joined;
            for (std::size_t k = 0; k < result.size(); ++k) { if (k) joined += "\n\n"; joined += result[k]; }
            return joined;
        }

        // -------------------------------------------------------------------------------------
        //  OpenDocument (odt/ods/odp)  — reproduit odfText() (L663)
        // -------------------------------------------------------------------------------------

        std::string odf_text(const archive& zip)
        {
            const xml_node doc = parse_xml(zip.text("content.xml"));
            std::vector<std::string> out;
            int slide = 0;
            long long size = 0;
            auto add = [&](const std::string& l) { out.push_back(l); size += static_cast<long long>(l.size() + 1); };
            std::function<void(const xml_node&)> walk = [&](const xml_node& n) {
                for (const auto& c : n.children)
                {
                    if (size > static_cast<long long>(max_text_chars)) return;
                    if (c.is_text) continue;
                    if (c.name == "h" || c.name == "p") add(text_content(c));
                    else if (c.name == "table-row")
                    {
                        std::string joined;
                        const auto cells = by_local_name(c, "table-cell");
                        for (std::size_t k = 0; k < cells.size(); ++k)
                        {
                            if (k) joined += " | ";
                            joined += trim(text_content(*cells[k]));
                        }
                        add(joined);
                    }
                    else if (c.name == "table" && c.attr("name"))
                    {
                        // Le JS exige aussi que l'URI d'espace de noms contienne "table" ; notre
                        // arbre ne garde pas les URI, mais l'élément de nom local "table" avec un
                        // attribut "name" est la table d'un classeur (la seule à nommer ses onglets).
                        add("");
                        add("## Sheet: " + *c.attr("name"));
                        walk(c);
                    }
                    else if (c.name == "page" && c.attr("name"))
                    {
                        add("");
                        add("## Slide " + std::to_string(++slide) + ": " + *c.attr("name"));
                        walk(c);
                    }
                    else walk(c);
                }
            };
            walk(doc);
            std::string joined;
            for (std::size_t k = 0; k < out.size(); ++k) { if (k) joined.push_back('\n'); joined += out[k]; }
            return joined;
        }

        // -------------------------------------------------------------------------------------
        //  RTF et HTML  — reproduit rtfText() (L865) et htmlText() (L870)
        // -------------------------------------------------------------------------------------

        // rtfText() (L865), portée séquentiellement (les regex du JS, dans l'ordre) :
        //   \par / \pard -> saut de ligne ; {\*...} supprimé ; \'HH -> l'octet HH ;
        //   \mot[-]NNN[ ] (mot de contrôle) supprimé ; { } supprimés ; trim.
        std::string rtf_text(const std::string& input)
        {
            // 1) \par et \pard deviennent "\n".
            std::string a;
            a.reserve(input.size());
            for (std::size_t k = 0; k < input.size(); )
            {
                if (input[k] == '\\' && input.compare(k, 4, "\\par") == 0)
                {
                    std::size_t end = k + 4;
                    if (end < input.size() && input[end] == 'd') ++end;   // \pard
                    a.push_back('\n');
                    k = end;
                }
                else { a.push_back(input[k]); ++k; }
            }
            // 2) Supprime les groupes {\*...} sans accolade imbriquée (/\{\\\*[^{}]*\}/g).
            std::string b;
            b.reserve(a.size());
            for (std::size_t k = 0; k < a.size(); )
            {
                if (a[k] == '{' && k + 2 < a.size() && a[k + 1] == '\\' && a[k + 2] == '*')
                {
                    std::size_t j = k + 3;
                    bool inner = false;
                    while (j < a.size() && a[j] != '}')
                    {
                        if (a[j] == '{') { inner = true; break; }   // [^{}] : pas d'accolade interne
                        ++j;
                    }
                    if (!inner && j < a.size() && a[j] == '}') { k = j + 1; continue; }
                }
                b.push_back(a[k]); ++k;
            }
            // 3) \'HH -> l'octet de valeur HH (/\\'([0-9a-f]{2})/gi).
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            std::string c;
            c.reserve(b.size());
            for (std::size_t k = 0; k < b.size(); )
            {
                if (b[k] == '\\' && k + 3 < b.size() && b[k + 1] == '\'')
                {
                    const int h1 = hexv(b[k + 2]), h2 = hexv(b[k + 3]);
                    if (h1 >= 0 && h2 >= 0) { c.push_back(static_cast<char>(h1 * 16 + h2)); k += 4; continue; }
                }
                c.push_back(b[k]); ++k;
            }
            // 4) Supprime les mots de contrôle restants (/\\[a-z]+-?\d* ?/gi) : \ + lettres + ['-'] + chiffres + espace.
            std::string d;
            d.reserve(c.size());
            for (std::size_t k = 0; k < c.size(); )
            {
                if (c[k] == '\\' && k + 1 < c.size()
                    && ((c[k + 1] >= 'a' && c[k + 1] <= 'z') || (c[k + 1] >= 'A' && c[k + 1] <= 'Z')))
                {
                    std::size_t j = k + 1;
                    while (j < c.size() && ((c[j] >= 'a' && c[j] <= 'z') || (c[j] >= 'A' && c[j] <= 'Z'))) ++j;
                    if (j < c.size() && c[j] == '-') ++j;
                    while (j < c.size() && c[j] >= '0' && c[j] <= '9') ++j;
                    if (j < c.size() && c[j] == ' ') ++j;   // une espace facultative après le mot
                    k = j;
                    continue;
                }
                d.push_back(c[k]); ++k;
            }
            // 5) Retire les accolades restantes (/[{}]/g) puis trim.
            std::string e;
            e.reserve(d.size());
            for (char ch : d) if (ch != '{' && ch != '}') e.push_back(ch);
            return trim(e);
        }

        /*
            htmlText() (L870) : le texte visible d'un HTML, scripts et styles retirés.  Le JS
            s'appuie sur innerText du DOMParser ; sans navigateur, on retire les balises et on
            décode les entités.  On retire d'abord le contenu de <script>, <style> et <noscript>,
            on insère un saut de ligne aux balises de bloc fréquentes pour garder la mise en
            lignes, et on réduit 3 sauts ou plus à 2, puis trim.  Voir la note "approximation" du
            rapport : le découpage en lignes n'est pas identique au byte près à innerText.
        */
        std::string html_text(const std::string& input)
        {
            // 1) Retire <script>…</script>, <style>…</style>, <noscript>…</noscript> (contenu compris).
            auto strip_element = [](std::string s, const std::string& tag) {
                std::string out;
                out.reserve(s.size());
                const std::string open = "<" + tag;
                const std::string close = "</" + tag + ">";
                std::size_t k = 0;
                while (k < s.size())
                {
                    // Cherche "<tag" sans tenir compte de la casse.
                    std::size_t found = std::string::npos;
                    for (std::size_t j = k; j + open.size() <= s.size(); ++j)
                    {
                        if (to_lower(s.substr(j, open.size())) == open)
                        {
                            const char after = (j + open.size() < s.size()) ? s[j + open.size()] : '\0';
                            if (after == '\0' || after == '>' || is_space(after) || after == '/')
                            { found = j; break; }
                        }
                    }
                    if (found == std::string::npos) { out += s.substr(k); break; }
                    out += s.substr(k, found - k);
                    // Cherche la fermeture correspondante.
                    std::size_t end = std::string::npos;
                    for (std::size_t j = found; j + close.size() <= s.size(); ++j)
                        if (to_lower(s.substr(j, close.size())) == close) { end = j; break; }
                    if (end == std::string::npos) { k = s.size(); break; }
                    k = end + close.size();
                }
                return out;
            };
            std::string s = input;
            s = strip_element(s, "script");
            s = strip_element(s, "style");
            s = strip_element(s, "noscript");

            // 2) Retire les balises, en insérant un saut de ligne aux balises de bloc.
            static const std::set<std::string> block = {
                "p","div","br","li","tr","h1","h2","h3","h4","h5","h6","section","article",
                "header","footer","table","ul","ol","blockquote","pre","hr","figure","figcaption",
                "main","nav","aside","dd","dt","dl","td","th"
            };
            std::string out;
            out.reserve(s.size());
            for (std::size_t k = 0; k < s.size(); )
            {
                if (s[k] == '<')
                {
                    // Un commentaire <!-- … -->.
                    if (s.compare(k, 4, "<!--") == 0)
                    {
                        std::size_t e = s.find("-->", k + 4);
                        k = (e == std::string::npos) ? s.size() : e + 3;
                        continue;
                    }
                    std::size_t e = s.find('>', k + 1);
                    if (e == std::string::npos) { break; }   // balise non terminée : on ignore la fin
                    // Le nom de la balise (sans '/').
                    std::size_t nstart = k + 1;
                    if (nstart < s.size() && s[nstart] == '/') ++nstart;
                    std::size_t nend = nstart;
                    while (nend < e && !is_space(s[nend]) && s[nend] != '/' && s[nend] != '>') ++nend;
                    const std::string tag = to_lower(s.substr(nstart, nend - nstart));
                    if (block.count(tag)) out.push_back('\n');
                    k = e + 1;
                }
                else { out.push_back(s[k]); ++k; }
            }
            out = decode_entities(out);
            out = collapse_blank_lines(out);
            return trim(out);
        }

        // Ouvre une archive et exécute un extracteur, en traduisant les erreurs ZIP de dlib.
        template <class Fn>
        std::string with_archive(const std::string& bytes, Fn&& fn)
        {
            try
            {
                dlib::zip_archive_reader reader(bytes);
                archive zip(reader);
                return fn(zip);
            }
            catch (const doc_error&) { throw; }
            catch (const dlib::zip_error& e) { throw doc_error("zip-invalid", e.info); }
            catch (const std::exception& e) { throw doc_error("zip-invalid", e.what()); }
        }
    } // namespace

    // -----------------------------------------------------------------------------------------
    //  L'aiguillage public
    // -----------------------------------------------------------------------------------------

    std::string extract_text(const std::string& name, const std::string& bytes)
    {
        // Les bornes, comme extractText() (L888).
        if (bytes.size() > max_file_bytes) throw doc_error("too-large", "Fichier trop volumineux");

        const std::string ext = extension(name);
        if (legacy_extensions().count(ext))
            throw doc_error("legacy", "Format Office anterieur a 2007");

        std::string text;
        if (ext == "docx" || ext == "docm" || ext == "dotx")
            text = with_archive(bytes, [](archive& z) { docx_reader r(z); return r.run(); });
        else if (ext == "pptx" || ext == "pptm" || ext == "ppsx" || ext == "potx")
            text = with_archive(bytes, [](archive& z) { return pptx_text(z); });
        else if (ext == "xlsx" || ext == "xlsm" || ext == "xltx")
            text = with_archive(bytes, [](archive& z) { return xlsx_text(z); });
        else if (ext == "odt" || ext == "ods" || ext == "odp")
            text = with_archive(bytes, [](archive& z) { return odf_text(z); });
        else if (ext == "pdf" || is_pdf(bytes))
            return extract_pdf_text(bytes);   // déjà borné à maxTextChars par son appelant côté page
        else if (ext == "rtf")
            text = rtf_text(decode_text(bytes));
        else if (ext == "html" || ext == "htm" || ext == "xhtml")
            text = html_text(decode_text(bytes));
        else if (text_extensions().count(ext) || !looks_binary(bytes))
        {
            text = decode_text(bytes);
            // Retire un BOM UTF-8 en tête (le JS retire le caractère U+FEFF décodé).
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
                && static_cast<unsigned char>(text[1]) == 0xBB
                && static_cast<unsigned char>(text[2]) == 0xBF)
                text = text.substr(3);
        }
        else
            throw doc_error("unsupported", "Format non pris en charge");

        // Tronque le texte final (maxTextChars).  On compte des unités UTF-16 côté page ; ici on
        // tronque en octets mais sans couper une séquence UTF-8 au milieu.
        if (text.size() > max_text_chars)
        {
            std::size_t cut = max_text_chars;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xc0) == 0x80) --cut;
            text = text.substr(0, cut);
        }
        return text;
    }
}
