// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

#include "markdown.h"

#include "highlight.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace gulliver
{
    namespace
    {
        // ------------------------------------------------------------------------------------------
        //  Petits outils

        std::string escape(const std::string& s)
        {
            std::string out;
            out.reserve(s.size() + s.size() / 8);
            for (const char c : s)
            {
                switch (c)
                {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break;
                default: out += c;
                }
            }
            return out;
        }

        bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

        bool is_punct(char c)
        {
            return static_cast<unsigned char>(c) < 0x80 && std::ispunct(static_cast<unsigned char>(c));
        }

        bool blank(const std::string& line)
        {
            return std::all_of(line.begin(), line.end(), [](char c) { return is_space(c); });
        }

        size_t indent_of(const std::string& line)
        {
            size_t n = 0;
            while (n < line.size() && line[n] == ' ') ++n;
            return n;
        }

        std::string trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && is_space(s[a])) ++a;
            while (b > a && is_space(s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // Une étiquette de lien de référence, comparée sans casse ni espaces répétés.
        std::string label_key(const std::string& s)
        {
            std::string out;
            bool space = false;
            for (const char c : trim(s))
            {
                if (is_space(c)) { space = true; continue; }
                if (space && !out.empty()) out += ' ';
                space = false;
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        // Les tabulations du début d'une ligne deviennent des espaces (jusqu'au multiple de 4 suivant).
        std::string expand_tabs(const std::string& line)
        {
            std::string out;
            size_t i = 0;
            for (; i < line.size() && (line[i] == ' ' || line[i] == '\t'); ++i)
            {
                if (line[i] == '\t') out.append(4 - out.size() % 4, ' ');
                else out += ' ';
            }
            out.append(line, i, std::string::npos);
            return out;
        }

        // Une adresse dans un attribut : échappée, ses espaces encodées.
        std::string url_attr(const std::string& url)
        {
            std::string out;
            for (const char c : url)
            {
                if (c == ' ') out += "%20";
                else out += c;
            }
            return escape(out);
        }

        // Une adresse qu'un lien peut prendre : http, https, mailto, ou une adresse de la page.
        bool safe_href(const std::string& url)
        {
            size_t i = 0;
            if (i < url.size() && std::isalpha(static_cast<unsigned char>(url[i])))
            {
                while (i < url.size() && (std::isalnum(static_cast<unsigned char>(url[i])) || url[i] == '+'
                       || url[i] == '.' || url[i] == '-'))
                    ++i;
                if (i < url.size() && url[i] == ':')
                {
                    const std::string scheme = lower(url.substr(0, i));
                    return scheme == "http" || scheme == "https" || scheme == "mailto";
                }
            }
            // Sans schéma : une adresse relative à la page.  Les caractères de contrôle sont refusés.
            return std::none_of(url.begin(), url.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
        }

        // Une image que la page peut charger : une adresse de la page, ou des données qu'elle détient.
        bool local_image(const std::string& url)
        {
            if (url.compare(0, 2, "//") == 0) return false;
            const std::string l = lower(url.substr(0, 16));
            if (l.compare(0, 11, "data:image/") == 0 || l.compare(0, 5, "blob:") == 0) return true;
            for (const char c : url)
            {
                if (c == ':') return false;            // un autre schéma
                if (c == '/' || c == '?' || c == '#') break;
            }
            return !url.empty() && std::none_of(url.begin(), url.end(),
                [](char c) { return static_cast<unsigned char>(c) < 0x20; });
        }

        // ------------------------------------------------------------------------------------------
        //  Le texte des lignes : code, liens, emphase

        using refs_t = std::map<std::string, std::pair<std::string, std::string>>;

        struct inline_node
        {
            enum kind_t { text, html, delim, bracket } kind = text;
            std::string s;            // le texte (déjà échappé) ou le HTML
            char c = 0;               // le signe d'un délimiteur ('*', '_', '~', '=')
            int count = 0;            // les signes qui restent
            int orig = 0;             // les signes au départ
            bool can_open = false, can_close = false, active = true;
            bool image = false;       // un crochet ouvrant précédé de !
            size_t at = 0;            // la place, dans le texte, du texte qui suit le crochet
            std::string opens, closes;
        };

        class inline_parser
        {
        public:
            inline_parser(const std::string& src, const refs_t& refs) : src_(src), refs_(refs) {}

            std::string run()
            {
                parse();
                process_emphasis(0);
                return render(0, nodes_.size());
            }

        private:
            const std::string& src_;
            const refs_t& refs_;
            std::vector<inline_node> nodes_;
            std::vector<size_t> brackets_;    // les crochets ouvrants encore en attente, par leur nœud
            size_t pos_ = 0;

            void add_text(const std::string& escaped)
            {
                if (!nodes_.empty() && nodes_.back().kind == inline_node::text) nodes_.back().s += escaped;
                else
                {
                    inline_node n;
                    n.s = escaped;
                    nodes_.push_back(n);
                }
            }

            void add_html(const std::string& h)
            {
                inline_node n;
                n.kind = inline_node::html;
                n.s = h;
                nodes_.push_back(n);
            }

            char at(size_t i) const { return i < src_.size() ? src_[i] : '\n'; }

            static std::string link_open(const std::string& href, const std::string& title)
            {
                return "<a href=\"" + url_attr(href) + "\"" + (title.empty() ? std::string() : " title=\"" + escape(title) + "\"")
                    + " target=\"_blank\" rel=\"noopener noreferrer\">";
            }

            // Une entité HTML valable (&amp;, &#233;, &#x41;) : gardée telle quelle.
            size_t entity_length(size_t i) const
            {
                size_t j = i + 1;
                if (j < src_.size() && src_[j] == '#')
                {
                    ++j;
                    const bool hex = j < src_.size() && (src_[j] == 'x' || src_[j] == 'X');
                    if (hex) ++j;
                    const size_t start = j;
                    while (j < src_.size() && (hex ? std::isxdigit(static_cast<unsigned char>(src_[j]))
                                                   : std::isdigit(static_cast<unsigned char>(src_[j]))) && j - start < 7)
                        ++j;
                    if (j == start || j >= src_.size() || src_[j] != ';') return 0;
                    return j + 1 - i;
                }
                const size_t start = j;
                while (j < src_.size() && std::isalnum(static_cast<unsigned char>(src_[j])) && j - start < 32) ++j;
                if (j == start || !std::isalpha(static_cast<unsigned char>(src_[start])) || j >= src_.size() || src_[j] != ';')
                    return 0;
                return j + 1 - i;
            }

            // Un lien nu (http://, https://, www.) qui commence en i ; sa longueur, 0 sinon.
            size_t bare_link(size_t i) const
            {
                if (i > 0)
                {
                    const char p = src_[i - 1];
                    if (!(is_space(p) || p == '(' || p == '*' || p == '_' || p == '~' || p == '"' || p == '\'')) return 0;
                }
                const std::string head = lower(src_.substr(i, 8));
                size_t skip = 0;
                if (head.compare(0, 8, "https://") == 0) skip = 8;
                else if (head.compare(0, 7, "http://") == 0) skip = 7;
                else if (head.compare(0, 4, "www.") == 0) skip = 4;
                else return 0;
                size_t j = i + skip;
                while (j < src_.size() && !is_space(src_[j]) && src_[j] != '<') ++j;
                // La ponctuation de fin de phrase et une parenthèse fermante sans ouvrante ne sont pas du lien.
                for (;;)
                {
                    if (j <= i + skip) break;
                    const char last = src_[j - 1];
                    if (std::strchr("?!.,:*_~'\"", last)) { --j; continue; }
                    if (last == ')')
                    {
                        const std::string body = src_.substr(i, j - i);
                        if (std::count(body.begin(), body.end(), ')') > std::count(body.begin(), body.end(), '(')) { --j; continue; }
                    }
                    if (last == ';')
                    {
                        size_t k = j - 1;
                        while (k > i && std::isalnum(static_cast<unsigned char>(src_[k - 1]))) --k;
                        if (k > i && src_[k - 1] == '&') { j = k - 1; continue; }
                    }
                    break;
                }
                if (j <= i + skip) return 0;
                if (skip == 4 && src_.find('.', i + 4) >= j) return 0;
                return j - i;
            }

            void parse()
            {
                while (pos_ < src_.size())
                {
                    const char c = src_[pos_];
                    if (c == '\\' && pos_ + 1 < src_.size())
                    {
                        const char n = src_[pos_ + 1];
                        if (n == '\n')
                        {
                            add_html("<br>");
                            pos_ += 2;
                            continue;
                        }
                        if (is_punct(n))
                        {
                            add_text(escape(std::string(1, n)));
                            pos_ += 2;
                            continue;
                        }
                    }
                    if (c == '`')
                    {
                        if (code_span()) continue;
                        size_t run = 0;
                        while (at(pos_ + run) == '`') ++run;
                        add_text(std::string(run, '`'));
                        pos_ += run;
                        continue;
                    }
                    if (c == '<' && autolink()) continue;
                    if (c == '!' && at(pos_ + 1) == '[')
                    {
                        inline_node n;
                        n.kind = inline_node::bracket;
                        n.image = true;
                        n.s = "![";
                        n.at = pos_ + 2;
                        brackets_.push_back(nodes_.size());
                        nodes_.push_back(n);
                        pos_ += 2;
                        continue;
                    }
                    if (c == '[')
                    {
                        inline_node n;
                        n.kind = inline_node::bracket;
                        n.s = "[";
                        n.at = pos_ + 1;
                        brackets_.push_back(nodes_.size());
                        nodes_.push_back(n);
                        ++pos_;
                        continue;
                    }
                    if (c == ']')
                    {
                        close_bracket();
                        continue;
                    }
                    if (c == '*' || c == '_' || c == '~' || c == '=')
                    {
                        delimiter_run(c);
                        continue;
                    }
                    if (c == '\n')
                    {
                        // Deux espaces en fin de ligne : un retour à la ligne ; sinon, une fin de ligne simple.
                        size_t spaces = 0;
                        if (!nodes_.empty() && nodes_.back().kind == inline_node::text)
                        {
                            std::string& t = nodes_.back().s;
                            while (!t.empty() && t.back() == ' ') { t.pop_back(); ++spaces; }
                        }
                        add_html(spaces >= 2 ? "<br>\n" : "\n");
                        ++pos_;
                        while (pos_ < src_.size() && src_[pos_] == ' ') ++pos_;
                        continue;
                    }
                    if (c == '&')
                    {
                        const size_t n = entity_length(pos_);
                        add_text(n ? src_.substr(pos_, n) : std::string("&amp;"));
                        pos_ += n ? n : 1;
                        continue;
                    }
                    if (c == 'h' || c == 'H' || c == 'w' || c == 'W')
                    {
                        const size_t n = bare_link(pos_);
                        if (n)
                        {
                            const std::string url = src_.substr(pos_, n);
                            const std::string href = lower(url.substr(0, 4)) == "www." ? "http://" + url : url;
                            add_html(link_open(href, std::string()) + escape(url) + "</a>");
                            pos_ += n;
                            continue;
                        }
                    }
                    add_text(escape(std::string(1, c)));
                    ++pos_;
                }
            }

            bool code_span()
            {
                size_t run = 0;
                while (at(pos_ + run) == '`') ++run;
                size_t i = pos_ + run;
                while (i < src_.size())
                {
                    const size_t found = src_.find('`', i);
                    if (found == std::string::npos) return false;
                    size_t r = 0;
                    while (found + r < src_.size() && src_[found + r] == '`') ++r;
                    if (r == run)
                    {
                        std::string body = src_.substr(pos_ + run, found - pos_ - run);
                        std::replace(body.begin(), body.end(), '\n', ' ');
                        if (body.size() >= 2 && body.front() == ' ' && body.back() == ' ' && !blank(body))
                            body = body.substr(1, body.size() - 2);
                        add_html("<code>" + escape(body) + "</code>");
                        pos_ = found + r;
                        return true;
                    }
                    i = found + r;
                }
                return false;
            }

            bool autolink()
            {
                const size_t end = src_.find('>', pos_ + 1);
                if (end == std::string::npos) return false;
                const std::string inner = src_.substr(pos_ + 1, end - pos_ - 1);
                if (inner.empty() || std::any_of(inner.begin(), inner.end(), [](char c) { return is_space(c) || c == '<'; }))
                    return false;
                const size_t colon = inner.find(':');
                const bool mail = colon == std::string::npos && inner.find('@') != std::string::npos
                    && inner.find('@') > 0 && inner.find('.', inner.find('@')) != std::string::npos;
                if (!mail && (colon == std::string::npos || colon < 2)) return false;
                const std::string href = mail ? "mailto:" + inner : inner;
                if (!safe_href(href)) return false;
                add_html(link_open(href, std::string()) + escape(inner) + "</a>");
                pos_ = end + 1;
                return true;
            }

            void delimiter_run(char c)
            {
                size_t run = 0;
                while (at(pos_ + run) == c) ++run;
                const char before = pos_ > 0 ? src_[pos_ - 1] : '\n';
                const char after = at(pos_ + run);
                const bool left = !is_space(after) && (!is_punct(after) || is_space(before) || is_punct(before));
                const bool right = !is_space(before) && (!is_punct(before) || is_space(after) || is_punct(after));
                inline_node n;
                n.kind = inline_node::delim;
                n.c = c;
                n.count = n.orig = static_cast<int>(run);
                if (c == '_')
                {
                    n.can_open = left && (!right || is_punct(before));
                    n.can_close = right && (!left || is_punct(after));
                }
                else
                {
                    n.can_open = left;
                    n.can_close = right;
                }
                // Le surligné prend deux signes ; le barré, un ou deux.
                if ((c == '=' && run != 2) || (c == '~' && run > 2)) n.can_open = n.can_close = false;
                nodes_.push_back(n);
                pos_ += run;
            }

            // La destination et le titre d'un lien, après "]" : "(url "titre")".  Faux s'il n'y en a pas.
            bool link_target(size_t& i, std::string& url, std::string& title) const
            {
                if (at(i) != '(') return false;
                size_t j = i + 1;
                while (j < src_.size() && is_space(src_[j])) ++j;
                if (at(j) == '<')
                {
                    const size_t e = src_.find('>', j + 1);
                    if (e == std::string::npos || src_.substr(j, e - j).find('\n') != std::string::npos) return false;
                    url = src_.substr(j + 1, e - j - 1);
                    j = e + 1;
                }
                else
                {
                    int depth = 0;
                    const size_t start = j;
                    while (j < src_.size() && !is_space(src_[j]))
                    {
                        if (src_[j] == '\\' && j + 1 < src_.size()) { j += 2; continue; }
                        if (src_[j] == '(' && ++depth > 32) return false;
                        else if (src_[j] == ')') { if (depth == 0) break; --depth; }
                        ++j;
                    }
                    url = src_.substr(start, j - start);
                }
                size_t k = j;
                while (k < src_.size() && is_space(src_[k])) ++k;
                if (k > j && (at(k) == '"' || at(k) == '\'' || at(k) == '('))
                {
                    const char close = at(k) == '(' ? ')' : at(k);
                    const size_t e = src_.find(close, k + 1);
                    if (e == std::string::npos) return false;
                    title = src_.substr(k + 1, e - k - 1);
                    k = e + 1;
                    while (k < src_.size() && is_space(src_[k])) ++k;
                }
                if (at(k) != ')') return false;
                // Les barres obliques inverses d'une adresse retirées devant la ponctuation.
                std::string clean;
                for (size_t a = 0; a < url.size(); ++a)
                {
                    if (url[a] == '\\' && a + 1 < url.size() && is_punct(url[a + 1])) continue;
                    clean += url[a];
                }
                url = clean;
                i = k + 1;
                return true;
            }

            // Le texte brut des nœuds [from, to) : celui d'une étiquette de référence ou du texte d'une image.
            std::string plain(size_t from, size_t to) const
            {
                std::string out;
                for (size_t k = from; k < to; ++k)
                {
                    const inline_node& n = nodes_[k];
                    if (n.kind == inline_node::delim) out += std::string(n.count, n.c);
                    else if (n.kind == inline_node::html)
                    {
                        // Le texte d'un morceau de HTML, sans ses balises.
                        bool tag = false;
                        for (const char c : n.s)
                        {
                            if (c == '<') tag = true;
                            else if (c == '>') tag = false;
                            else if (!tag) out += c;
                        }
                    }
                    else out += n.s;
                }
                return out;
            }

            void close_bracket()
            {
                // Le crochet ouvrant en attente le plus proche ; un crochet sans lien possible redevient du texte.
                if (brackets_.empty() || !nodes_[brackets_.back()].active)
                {
                    if (!brackets_.empty())
                    {
                        nodes_[brackets_.back()].kind = inline_node::text;
                        brackets_.pop_back();
                    }
                    add_text("]");
                    ++pos_;
                    return;
                }
                const size_t open = brackets_.back();
                brackets_.pop_back();
                size_t i = pos_ + 1;
                std::string url, title;
                bool found = link_target(i, url, title);
                if (!found)
                {
                    // Un lien de référence : [texte][étiquette], [texte][] ou [texte].
                    std::string label;
                    size_t j = i;
                    if (at(j) == '[')
                    {
                        const size_t e = src_.find(']', j + 1);
                        if (e != std::string::npos)
                        {
                            label = src_.substr(j + 1, e - j - 1);
                            j = e + 1;
                        }
                    }
                    if (label.empty())
                    {
                        // Le texte entre les crochets, tel qu'écrit (999 caractères au plus, comme CommonMark).
                        if (pos_ - nodes_[open].at <= 999) label = src_.substr(nodes_[open].at, pos_ - nodes_[open].at);
                        if (at(i) == '[' && at(i + 1) == ']') j = i + 2;
                        else j = i;
                    }
                    const auto it = refs_.empty() || label.empty() || label.size() > 999 ? refs_.end() : refs_.find(label_key(label));
                    if (it != refs_.end())
                    {
                        url = it->second.first;
                        title = it->second.second;
                        i = j;
                        found = true;
                    }
                }
                if (!found)
                {
                    nodes_[open].kind = inline_node::text;
                    add_text("]");
                    ++pos_;
                    return;
                }
                pos_ = i;
                // Les crochets ouverts à l'intérieur du lien ne sont plus en attente.
                while (!brackets_.empty() && brackets_.back() > open) brackets_.pop_back();
                process_emphasis(open + 1);
                if (nodes_[open].image)
                {
                    // Le texte de l'image, déjà échappé.
                    const std::string alt = plain(open + 1, nodes_.size());
                    nodes_.resize(open);
                    if (local_image(url))
                        add_html("<img src=\"" + url_attr(url) + "\" alt=\"" + alt + "\""
                            + (title.empty() ? std::string() : " title=\"" + escape(title) + "\"") + ">");
                    else if (safe_href(url))
                        add_html(link_open(url, title) + (alt.empty() ? escape(url) : alt) + "</a>");
                    else
                        add_text(alt);
                    return;
                }
                const std::string inner = render(open + 1, nodes_.size());
                nodes_.resize(open);
                if (safe_href(url)) add_html(link_open(url, title) + inner + "</a>");
                else add_html(inner);
                // Pas de lien dans un lien : les crochets ouvrants d'avant ne font plus de lien.
                for (const size_t k : brackets_)
                    if (!nodes_[k].image) nodes_[k].active = false;
            }

            void process_emphasis(size_t bottom)
            {
                for (size_t ci = bottom; ci < nodes_.size(); ++ci)
                {
                    inline_node& closer = nodes_[ci];
                    if (closer.kind != inline_node::delim || !closer.can_close || !closer.active) continue;
                    while (closer.count > 0)
                    {
                        size_t oi = ci;
                        bool found = false;
                        while (oi-- > bottom)
                        {
                            inline_node& o = nodes_[oi];
                            if (o.kind != inline_node::delim || o.c != closer.c || !o.can_open || !o.active || o.count == 0)
                                continue;
                            if (closer.c == '*' || closer.c == '_')
                            {
                                if ((o.can_close || closer.can_open) && (o.orig + closer.orig) % 3 == 0
                                    && !(o.orig % 3 == 0 && closer.orig % 3 == 0))
                                    continue;
                            }
                            if (closer.c == '~' && o.count != closer.count) continue;
                            found = true;
                            break;
                        }
                        if (!found) break;
                        inline_node& o = nodes_[oi];
                        int use = 1;
                        std::string tag;
                        if (closer.c == '~') { use = closer.count; tag = "del"; }
                        else if (closer.c == '=') { use = 2; tag = "mark"; }
                        else { use = o.count >= 2 && closer.count >= 2 ? 2 : 1; tag = use == 2 ? "strong" : "em"; }
                        o.count -= use;
                        closer.count -= use;
                        o.opens = "<" + tag + ">" + o.opens;
                        closer.closes += "</" + tag + ">";
                        // Les délimiteurs entre les deux restent du texte.
                        for (size_t k = oi + 1; k < ci; ++k)
                            if (nodes_[k].kind == inline_node::delim) nodes_[k].active = false;
                    }
                }
                for (size_t k = bottom; k < nodes_.size(); ++k)
                    if (nodes_[k].kind == inline_node::delim) nodes_[k].active = false;
            }

            std::string render(size_t from, size_t to) const
            {
                std::string out;
                for (size_t k = from; k < to; ++k)
                {
                    const inline_node& n = nodes_[k];
                    if (n.kind == inline_node::delim) out += n.closes + std::string(n.count, n.c) + n.opens;
                    else if (n.kind == inline_node::bracket) out += n.s;
                    else out += n.s;
                }
                return out;
            }
        };

        std::string render_inline(const std::string& text, const refs_t& refs)
        {
            inline_parser p(text, refs);
            return p.run();
        }

        // ------------------------------------------------------------------------------------------
        //  Les blocs

        struct fence_info { size_t indent = 0; char c = 0; size_t length = 0; std::string info; };

        bool fence_open(const std::string& line, fence_info& f)
        {
            const size_t ind = indent_of(line);
            if (ind > 3 || ind >= line.size()) return false;
            const char c = line[ind];
            if (c != '`' && c != '~') return false;
            size_t n = ind;
            while (n < line.size() && line[n] == c) ++n;
            if (n - ind < 3) return false;
            const std::string info = trim(line.substr(n));
            if (c == '`' && info.find('`') != std::string::npos) return false;
            f.indent = ind;
            f.c = c;
            f.length = n - ind;
            f.info = info;
            return true;
        }

        bool fence_close(const std::string& line, const fence_info& f)
        {
            const size_t ind = indent_of(line);
            if (ind > 3) return false;
            size_t n = ind;
            while (n < line.size() && line[n] == f.c) ++n;
            return n - ind >= f.length && blank(line.substr(n));
        }

        bool heading_line(const std::string& line, int& level, std::string& text)
        {
            const size_t ind = indent_of(line);
            if (ind > 3) return false;
            size_t n = ind;
            while (n < line.size() && line[n] == '#') ++n;
            const size_t hashes = n - ind;
            if (hashes == 0 || hashes > 6 || (n < line.size() && line[n] != ' ' && line[n] != '\t')) return false;
            std::string t = trim(line.substr(n));
            // Les # de fin, précédés d'une espace, ne font pas partie du titre.
            size_t e = t.size();
            while (e > 0 && t[e - 1] == '#') --e;
            if (e == 0) t.clear();
            else if (e < t.size() && (t[e - 1] == ' ' || t[e - 1] == '\t')) t = trim(t.substr(0, e));
            level = static_cast<int>(hashes);
            text = t;
            return true;
        }

        bool rule_line(const std::string& line)
        {
            if (indent_of(line) > 3) return false;
            char c = 0;
            int n = 0;
            for (const char ch : line)
            {
                if (ch == ' ' || ch == '\t') continue;
                if (ch != '-' && ch != '*' && ch != '_') return false;
                if (c && ch != c) return false;
                c = ch;
                ++n;
            }
            return n >= 3;
        }

        struct item_marker { bool ordered = false; char bullet = 0; long start = 1; size_t width = 0; bool empty = false; };

        // Une ligne qui commence un élément de liste ; width est le retrait de son contenu.
        bool list_marker(const std::string& line, item_marker& m)
        {
            const size_t ind = indent_of(line);
            if (ind > 3 || ind >= line.size()) return false;
            size_t n = ind;
            if (line[n] == '-' || line[n] == '+' || line[n] == '*')
            {
                m.ordered = false;
                m.bullet = line[n];
                ++n;
            }
            else
            {
                const size_t d = n;
                while (n < line.size() && std::isdigit(static_cast<unsigned char>(line[n])) && n - d < 9) ++n;
                if (n == d || n >= line.size() || (line[n] != '.' && line[n] != ')')) return false;
                m.ordered = true;
                m.start = std::stol(line.substr(d, n - d));
                m.bullet = line[n];
                ++n;
            }
            if (n < line.size() && line[n] != ' ' && line[n] != '\t') return false;
            size_t spaces = 0;
            while (n + spaces < line.size() && line[n + spaces] == ' ') ++spaces;
            m.empty = n + spaces >= line.size();
            if (m.empty || spaces > 4) spaces = 1;
            m.width = n + spaces;
            return true;
        }

        std::vector<std::string> split_cells(const std::string& row)
        {
            std::string r = trim(row);
            if (!r.empty() && r.front() == '|') r.erase(0, 1);
            if (!r.empty() && r.back() == '|' && (r.size() < 2 || r[r.size() - 2] != '\\')) r.pop_back();
            std::vector<std::string> cells;
            std::string cur;
            bool code = false;
            for (size_t i = 0; i < r.size(); ++i)
            {
                const char c = r[i];
                if (c == '\\' && i + 1 < r.size() && r[i + 1] == '|') { cur += '|'; ++i; continue; }
                if (c == '`') code = !code;
                if (c == '|' && !code) { cells.push_back(trim(cur)); cur.clear(); continue; }
                cur += c;
            }
            cells.push_back(trim(cur));
            return cells;
        }

        bool delimiter_row(const std::string& line, std::vector<std::string>& aligns)
        {
            if (line.find('-') == std::string::npos || indent_of(line) > 3) return false;
            const std::vector<std::string> cells = split_cells(line);
            aligns.clear();
            for (const std::string& c : cells)
            {
                if (c.empty()) return false;
                const bool l = c.front() == ':', r = c.back() == ':';
                const std::string mid = c.substr(l ? 1 : 0, c.size() - (l ? 1 : 0) - (r && c.size() > 1 ? 1 : 0));
                if (mid.empty() || mid.find_first_not_of('-') != std::string::npos) return false;
                aligns.push_back(l && r ? "center" : r ? "right" : l ? "left" : "");
            }
            return true;
        }

        class block_parser
        {
        public:
            block_parser(const refs_t& refs, const markdown_options& o) : refs_(refs), opt_(o) {}

            std::string blocks(const std::vector<std::string>& lines, bool tight = false)
            {
                // Au-delà de 40 niveaux de listes ou de citations, le reste est du texte simple.
                if (depth_ > 40)
                {
                    std::string text;
                    for (const std::string& l : lines) text += escape(l) + "\n";
                    return "<p>" + text + "</p>\n";
                }
                struct guard { int& d; explicit guard(int& x) : d(x) { ++d; } ~guard() { --d; } } g(depth_);
                std::string out;
                size_t i = 0;
                while (i < lines.size())
                {
                    const std::string& line = lines[i];
                    if (blank(line)) { ++i; continue; }
                    fence_info f;
                    int level = 0;
                    std::string text;
                    item_marker m;
                    std::vector<std::string> aligns;
                    if (indent_of(line) >= 4)
                    {
                        std::vector<std::string> code;
                        while (i < lines.size() && (blank(lines[i]) || indent_of(lines[i]) >= 4))
                        {
                            code.push_back(blank(lines[i]) ? std::string() : lines[i].substr(4));
                            ++i;
                        }
                        while (!code.empty() && code.back().empty()) code.pop_back();
                        out += code_block(join(code), std::string(), true);
                    }
                    else if (fence_open(line, f))
                    {
                        std::vector<std::string> code;
                        bool closed = false;
                        ++i;
                        for (; i < lines.size(); ++i)
                        {
                            if (fence_close(lines[i], f)) { closed = true; ++i; break; }
                            const size_t strip = std::min(f.indent, indent_of(lines[i]));
                            code.push_back(lines[i].substr(strip));
                        }
                        std::string lang = f.info;
                        const size_t sp = lang.find_first_of(" \t");
                        if (sp != std::string::npos) lang = lang.substr(0, sp);
                        out += code_block(join(code), lower(lang), closed);
                    }
                    else if (heading_line(line, level, text))
                    {
                        out += "<h" + std::to_string(level) + ">" + render_inline(text, refs_) + "</h" + std::to_string(level) + ">\n";
                        ++i;
                    }
                    else if (rule_line(line))
                    {
                        out += "<hr>\n";
                        ++i;
                    }
                    else if (quote_line(line))
                    {
                        std::vector<std::string> inner;
                        bool paragraph = false;
                        while (i < lines.size())
                        {
                            const std::string& l = lines[i];
                            if (quote_line(l))
                            {
                                const std::string body = strip_quote(l);
                                inner.push_back(body);
                                paragraph = !blank(body) && !starts_block(body);
                                ++i;
                            }
                            else if (paragraph && !blank(l) && !starts_block(l))
                            {
                                inner.push_back(l);
                                ++i;
                            }
                            else break;
                        }
                        out += "<blockquote>\n" + blocks(inner) + "</blockquote>\n";
                    }
                    else if (list_marker(line, m))
                    {
                        out += list(lines, i, m);
                    }
                    else if (i + 1 < lines.size() && line.find('|') != std::string::npos && delimiter_row(lines[i + 1], aligns)
                             && split_cells(line).size() == aligns.size())
                    {
                        out += table(lines, i, aligns);
                    }
                    else
                    {
                        out += paragraph(lines, i, tight);
                    }
                }
                return out;
            }

        private:
            const refs_t& refs_;
            const markdown_options& opt_;
            int depth_ = 0;

            static std::string join(const std::vector<std::string>& lines)
            {
                std::string out;
                for (size_t k = 0; k < lines.size(); ++k)
                {
                    if (k) out += '\n';
                    out += lines[k];
                }
                return out;
            }

            static bool quote_line(const std::string& line)
            {
                const size_t ind = indent_of(line);
                return ind <= 3 && ind < line.size() && line[ind] == '>';
            }

            static std::string strip_quote(const std::string& line)
            {
                size_t n = indent_of(line) + 1;
                if (n < line.size() && line[n] == ' ') ++n;
                return line.substr(n);
            }

            // Une ligne qui interrompt un paragraphe.
            static bool starts_block(const std::string& line)
            {
                fence_info f;
                int level = 0;
                std::string text;
                item_marker m;
                if (fence_open(line, f) || heading_line(line, level, text) || rule_line(line) || quote_line(line)) return true;
                if (list_marker(line, m) && !m.empty && (!m.ordered || m.start == 1)) return true;
                return false;
            }

            std::string code_block(const std::string& code, const std::string& lang, bool closed) const
            {
                const bool plain_lang = lang == "text" || lang == "txt" || lang == "plain" || lang == "plaintext"
                    || lang == "output";
                const bool plain = plain_lang || (opt_.live && !closed) || (lang.empty() && (opt_.live || code.size() >= 20000));
                const std::string body = !plain && !code.empty() ? highlight_code(code, lang) : escape(code);
                return "<pre><div class=\"code-head\"><span>" + escape(lang.empty() ? std::string("text") : lang)
                    + "</span><button type=\"button\" data-action=\"copy-code\"><span>Copy</span></button></div>"
                    + "<code class=\"hljs\">" + body + "</code></pre>\n";
            }

            std::string paragraph(const std::vector<std::string>& lines, size_t& i, bool tight) const
            {
                std::vector<std::string> para;
                while (i < lines.size() && !blank(lines[i]))
                {
                    const std::string& l = lines[i];
                    if (!para.empty())
                    {
                        // Un titre souligné de = ou de - (setext).
                        const std::string t = trim(l);
                        if (indent_of(l) <= 3 && !t.empty()
                            && (t.find_first_not_of('=') == std::string::npos || t.find_first_not_of('-') == std::string::npos))
                        {
                            const char lvl = t[0] == '=' ? '1' : '2';
                            ++i;
                            return std::string("<h") + lvl + ">" + render_inline(trim(join(para)), refs_) + "</h" + lvl + ">\n";
                        }
                        if (starts_block(l)) break;
                        // Un tableau collé sous le paragraphe le coupe.
                        std::vector<std::string> aligns;
                        if (l.find('|') != std::string::npos && i + 1 < lines.size() && delimiter_row(lines[i + 1], aligns)
                            && split_cells(l).size() == aligns.size())
                            break;
                    }
                    para.push_back(para.empty() ? l.substr(std::min<size_t>(indent_of(l), 3)) : l);
                    ++i;
                }
                // Les espaces de fin du paragraphe ne font pas de retour à la ligne.
                std::string text = join(para);
                while (!text.empty() && is_space(text.back())) text.pop_back();
                const std::string html = render_inline(text, refs_);
                return tight ? html + "\n" : "<p>" + html + "</p>\n";
            }

            std::string list(const std::vector<std::string>& lines, size_t& i, const item_marker& first)
            {
                struct item { std::vector<std::string> lines; };
                std::vector<item> items;
                bool loose = false;
                while (i < lines.size())
                {
                    item_marker cur;
                    if (!list_marker(lines[i], cur) || cur.ordered != first.ordered || cur.bullet != first.bullet) break;
                    // Un tiret seul sous un paragraphe est un trait, pas un élément.
                    if (rule_line(lines[i]) && !first.ordered) break;
                    item it;
                    const size_t width = cur.width;
                    it.lines.push_back(cur.empty ? std::string() : lines[i].substr(std::min(width, lines[i].size())));
                    ++i;
                    bool saw_blank = false;
                    bool paragraph = !cur.empty;
                    // Un bloc de code ouvert dans l'élément : ses lignes vides ne séparent rien.
                    fence_info fence;
                    bool in_fence = fence_open(it.lines.front(), fence);
                    while (i < lines.size())
                    {
                        const std::string& l = lines[i];
                        if (in_fence)
                        {
                            if (!blank(l) && indent_of(l) < width) break;
                            const std::string body = blank(l) ? std::string() : l.substr(width);
                            it.lines.push_back(body);
                            if (!blank(l) && fence_close(body, fence)) in_fence = false;
                            paragraph = false;
                            ++i;
                            continue;
                        }
                        if (blank(l))
                        {
                            saw_blank = true;
                            it.lines.push_back(std::string());
                            paragraph = false;
                            ++i;
                            continue;
                        }
                        if (indent_of(l) >= width)
                        {
                            if (saw_blank) loose = true;
                            it.lines.push_back(l.substr(width));
                            in_fence = fence_open(it.lines.back(), fence);
                            paragraph = !starts_block(l.substr(width));
                            saw_blank = false;
                            ++i;
                            continue;
                        }
                        if (!saw_blank && paragraph && !starts_block(l))
                        {
                            item_marker other;
                            if (list_marker(l, other)) break;
                            it.lines.push_back(trim(l));
                            ++i;
                            continue;
                        }
                        break;
                    }
                    // Les lignes vides de fin d'un élément le séparent du suivant : la liste est lâche.
                    size_t trailing = 0;
                    while (!it.lines.empty() && it.lines.back().empty()) { it.lines.pop_back(); ++trailing; }
                    items.push_back(it);
                    if (trailing)
                    {
                        item_marker next;
                        if (i < lines.size() && list_marker(lines[i], next) && next.ordered == first.ordered
                            && next.bullet == first.bullet)
                            loose = true;
                        else break;
                    }
                }
                std::string out = first.ordered
                    ? (first.start != 1 ? "<ol start=\"" + std::to_string(first.start) + "\">\n" : std::string("<ol>\n"))
                    : std::string("<ul>\n");
                for (item& it : items)
                {
                    std::string task;
                    if (!it.lines.empty())
                    {
                        std::string& head = it.lines.front();
                        if (head.size() >= 3 && head[0] == '[' && head[2] == ']' && (head.size() == 3 || head[3] == ' ')
                            && (head[1] == ' ' || head[1] == 'x' || head[1] == 'X'))
                        {
                            task = head[1] == ' ' ? "<span class=\"task\">☐</span> " : "<span class=\"task done\">☑</span> ";
                            head = head.size() > 4 ? head.substr(4) : std::string();
                        }
                    }
                    std::string body = blocks(it.lines, !loose);
                    while (!body.empty() && body.back() == '\n') body.pop_back();
                    out += "<li>" + task + body + "</li>\n";
                }
                out += first.ordered ? "</ol>\n" : "</ul>\n";
                return out;
            }

            std::string table(const std::vector<std::string>& lines, size_t& i, const std::vector<std::string>& aligns) const
            {
                const auto cell = [&](const char* tag, const std::string& text, size_t k) {
                    const std::string align = k < aligns.size() && !aligns[k].empty() ? " align=\"" + aligns[k] + "\"" : std::string();
                    return std::string("<") + tag + align + ">" + render_inline(text, refs_) + "</" + tag + ">";
                };
                std::string out = "<table>\n<thead>\n<tr>";
                const std::vector<std::string> head = split_cells(lines[i]);
                for (size_t k = 0; k < head.size(); ++k) out += cell("th", head[k], k);
                out += "</tr>\n</thead>\n";
                i += 2;
                std::string body;
                while (i < lines.size() && !blank(lines[i]) && !starts_block(lines[i]))
                {
                    const std::vector<std::string> cells = split_cells(lines[i]);
                    body += "<tr>";
                    for (size_t k = 0; k < aligns.size(); ++k) body += cell("td", k < cells.size() ? cells[k] : std::string(), k);
                    body += "</tr>\n";
                    ++i;
                }
                if (!body.empty()) out += "<tbody>\n" + body + "</tbody>\n";
                return out + "</table>\n";
            }
        };

        // Les définitions de liens de référence ([étiquette]: adresse "titre"), retirées du texte.
        std::vector<std::string> take_references(const std::vector<std::string>& lines, refs_t& refs)
        {
            std::vector<std::string> out;
            fence_info f;
            bool in_fence = false;
            bool after_blank = true;
            for (const std::string& line : lines)
            {
                if (in_fence)
                {
                    if (fence_close(line, f)) in_fence = false;
                    out.push_back(line);
                    continue;
                }
                if (fence_open(line, f))
                {
                    in_fence = true;
                    out.push_back(line);
                    after_blank = false;
                    continue;
                }
                const size_t ind = indent_of(line);
                if (after_blank && ind <= 3 && ind < line.size() && line[ind] == '[')
                {
                    const size_t e = line.find("]:", ind);
                    if (e != std::string::npos && e > ind + 1)
                    {
                        const std::string label = line.substr(ind + 1, e - ind - 1);
                        std::string rest = trim(line.substr(e + 2));
                        std::string url, title;
                        const size_t sp = rest.find_first_of(" \t");
                        url = sp == std::string::npos ? rest : rest.substr(0, sp);
                        if (url.size() >= 2 && url.front() == '<' && url.back() == '>') url = url.substr(1, url.size() - 2);
                        if (sp != std::string::npos)
                        {
                            const std::string t = trim(rest.substr(sp));
                            if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'' || t.front() == '(')) title = t.substr(1, t.size() - 2);
                        }
                        if (!url.empty())
                        {
                            const std::string key = label_key(label);
                            if (!refs.count(key)) refs[key] = { url, title };
                            continue;
                        }
                    }
                }
                after_blank = blank(line) || after_blank;
                if (!blank(line)) after_blank = false;
                out.push_back(line);
            }
            return out;
        }
    }

    std::string markdown_to_html(const std::string& text, const markdown_options& options)
    {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Le caractère nul ne passe pas.
            line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
            lines.push_back(expand_tabs(line));
            if (end == text.size()) break;
            start = end + 1;
        }
        refs_t refs;
        lines = take_references(lines, refs);
        block_parser parser(refs, options);
        return parser.blocks(lines);
    }
}
