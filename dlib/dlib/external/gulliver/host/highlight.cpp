// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

// La coloration du code des réponses (voir highlight.h).

#include "highlight.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace gulliver
{
    namespace
    {
        // ------------------------------------------------------------------------------------------
        // Le HTML.

        void escape_to(std::string& out, const std::string& s, size_t from, size_t to)
        {
            for (size_t i = from; i < to; ++i)
            {
                const char c = s[i];
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
        }

        // Écrit s[from, to) dans une <span> de la classe cls (« keyword » donne hljs-keyword), ou tel quel sans classe.
        void emit(std::string& out, const std::string& s, size_t from, size_t to, const char* cls)
        {
            if (from >= to) return;
            if (!cls || !*cls) { escape_to(out, s, from, to); return; }
            out += "<span class=\"hljs-";
            out += cls;
            out += "\">";
            escape_to(out, s, from, to);
            out += "</span>";
        }

        bool ident_start(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
        bool ident_char(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

        std::string lower(std::string s)
        {
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool starts_at(const std::string& s, size_t i, const std::string& w)
        {
            return w.size() && s.compare(i, w.size(), w) == 0;
        }

        // La fin d'un nombre qui commence en i : chiffres, lettres (0x, suffixes, exposants), points et '_'.
        size_t number_end(const std::string& s, size_t i)
        {
            size_t j = i;
            while (j < s.size())
            {
                const unsigned char c = static_cast<unsigned char>(s[j]);
                if (std::isalnum(c) || c == '_' || c == '\'') { ++j; continue; }
                if (c == '.' && j + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[j + 1]))) { ++j; continue; }
                if ((c == '+' || c == '-') && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E')
                    && !(s.size() > i + 1 && (s[i + 1] == 'x' || s[i + 1] == 'X'))) { ++j; continue; }
                break;
            }
            return j;
        }

        // ------------------------------------------------------------------------------------------
        // La fiche d'un langage, pour le découpeur commun.

        struct spec
        {
            std::string name;
            std::set<std::string> keywords, types, literals, builtins;
            std::vector<std::string> line_comments;                       // "//", "#", "--"
            std::vector<std::pair<std::string, std::string>> block_comments;
            std::string quotes = "\"'";                                    // délimiteurs des chaînes
            bool triple_quotes = false;                                    // """…""" et '''…''' (Python)
            bool backtick_strings = false;                                 // `…` (JavaScript, Go, shell)
            bool preprocessor = false;                                     // # en début de ligne (C, C++)
            bool annotations = false;                                      // @nom (Python, Java, Swift…)
            bool dollar_variables = false;                                 // $nom, ${…} (shell, PHP, Perl)
            bool case_insensitive = false;                                 // SQL
            bool char_literals = false;                                    // 'a' et '\n' seulement (Rust)
            std::set<std::string> title_after, class_after;                // def f, class C
        };

        std::set<std::string> words(const char* list)
        {
            std::set<std::string> out;
            std::string w;
            for (const char* p = list; ; ++p)
            {
                if (*p == ' ' || *p == '\0') { if (!w.empty()) out.insert(w); w.clear(); if (!*p) break; }
                else w += *p;
            }
            return out;
        }

        const std::map<std::string, spec>& specs()
        {
            static const std::map<std::string, spec> all = [] {
                std::map<std::string, spec> m;
                const char* c_kw = "auto break case const continue default do else enum extern for goto if inline register "
                    "restrict return sizeof static struct switch typedef union volatile while _Alignas _Alignof _Atomic "
                    "_Generic _Noreturn _Static_assert _Thread_local";
                const char* c_types = "char double float int long short signed unsigned void bool size_t ssize_t ptrdiff_t "
                    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t wchar_t FILE";
                {
                    spec s; s.name = "c";
                    s.keywords = words(c_kw); s.types = words(c_types); s.literals = words("NULL true false");
                    s.builtins = words("printf fprintf sprintf snprintf malloc calloc realloc free memcpy memset memmove "
                        "strlen strcmp strncmp strcpy fopen fclose fread fwrite exit abort assert");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.preprocessor = true;
                    s.class_after = words("struct union enum");
                    m["c"] = s;
                }
                {
                    spec s; s.name = "cpp";
                    s.keywords = words(std::string(c_kw).append(" alignas alignof and asm catch class co_await co_return "
                        "co_yield concept consteval constexpr constinit const_cast decltype delete dynamic_cast explicit "
                        "export final friend mutable namespace new noexcept not operator or override private protected public "
                        "reinterpret_cast requires static_assert static_cast template this thread_local throw try typeid "
                        "typename using virtual xor @interface @implementation @end @property @class @protocol "
                        "@autoreleasepool @selector").c_str());
                    s.types = words(std::string(c_types).append(" char8_t char16_t char32_t string vector map set "
                        "unordered_map unordered_set array pair tuple optional variant unique_ptr shared_ptr weak_ptr "
                        "function thread mutex atomic id NSString NSArray NSDictionary NSObject").c_str());
                    s.literals = words("true false nullptr NULL nil YES NO");
                    s.builtins = words("std cout cerr cin endl printf malloc free move forward make_unique make_shared "
                        "size begin end push_back emplace_back");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.preprocessor = true;
                    s.class_after = words("class struct union enum namespace concept");
                    m["cpp"] = s;
                }
                {
                    spec s; s.name = "python";
                    s.keywords = words("and as assert async await break class continue def del elif else except finally for "
                        "from global if import in is lambda nonlocal not or pass raise return try while with yield match case");
                    s.literals = words("True False None");
                    s.builtins = words("print len range int str float list dict set tuple bool open enumerate zip map filter "
                        "sorted reversed sum min max abs isinstance type super self cls object Exception ValueError "
                        "TypeError KeyError RuntimeError input repr iter next hasattr getattr setattr");
                    s.line_comments = {"#"}; s.triple_quotes = true; s.annotations = true;
                    s.title_after = words("def"); s.class_after = words("class");
                    m["python"] = s;
                }
                {
                    spec s; s.name = "javascript";
                    s.keywords = words("as async await break case catch class const continue debugger default delete do else "
                        "export extends finally for from function get if import in instanceof let new of return set static "
                        "super switch this throw try typeof var void while with yield");
                    s.literals = words("true false null undefined NaN Infinity");
                    s.builtins = words("console window document Math JSON Object Array String Number Boolean Promise Map Set "
                        "Date RegExp Error Symbol fetch require module exports process globalThis setTimeout setInterval");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.backtick_strings = true;
                    s.title_after = words("function"); s.class_after = words("class extends");
                    m["javascript"] = s;
                    spec t = s; t.name = "typescript";
                    t.keywords.insert({"abstract", "declare", "enum", "implements", "interface", "keyof", "namespace",
                        "private", "protected", "public", "readonly", "type", "is", "infer", "satisfies"});
                    t.types = words("string number boolean any unknown never void object bigint symbol");
                    t.annotations = true;
                    t.class_after.insert({"interface", "type", "enum", "implements"});
                    m["typescript"] = t;
                }
                {
                    spec s; s.name = "rust";
                    s.keywords = words("as async await break const continue crate dyn else enum extern fn for if impl in let "
                        "loop match mod move mut pub ref return self Self static struct super trait type unsafe use where "
                        "while union macro_rules");
                    s.types = words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec "
                        "Option Result Box Rc Arc HashMap HashSet BTreeMap");
                    s.literals = words("true false None Some Ok Err");
                    s.builtins = words("println! print! eprintln! format! vec! panic! assert! assert_eq! write! writeln!");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.quotes = "\""; s.char_literals = true;
                    s.annotations = false;
                    s.title_after = words("fn"); s.class_after = words("struct enum trait impl type union");
                    m["rust"] = s;
                }
                {
                    spec s; s.name = "go";
                    s.keywords = words("break case chan const continue default defer else fallthrough for func go goto if "
                        "import interface map package range return select struct switch type var");
                    s.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                        "string uint uint8 uint16 uint32 uint64 uintptr any");
                    s.literals = words("true false nil iota");
                    s.builtins = words("append cap close copy delete len make new panic print println recover fmt");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.backtick_strings = true;
                    s.title_after = words("func"); s.class_after = words("type");
                    m["go"] = s;
                }
                {
                    spec s; s.name = "java";
                    s.keywords = words("abstract assert break case catch class continue default do else enum extends final "
                        "finally for if implements import instanceof interface native new package private protected public "
                        "return static strictfp super switch synchronized this throw throws transient try volatile while "
                        "var record sealed permits yield");
                    s.types = words("boolean byte char double float int long short void String Integer Long Double List "
                        "Map Set Object");
                    s.literals = words("true false null");
                    s.builtins = words("System out println");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.annotations = true;
                    s.class_after = words("class interface enum extends implements record");
                    m["java"] = s;
                    spec k = s; k.name = "kotlin";
                    k.keywords = words("as break class continue do else false for fun if in interface is null object package "
                        "return super this throw true try typealias val var when while by constructor data enum import "
                        "init internal lateinit open override private protected public sealed suspend companion");
                    k.title_after = words("fun"); k.class_after = words("class interface object");
                    k.backtick_strings = false;
                    m["kotlin"] = k;
                    spec c = s; c.name = "csharp";
                    c.keywords = words("abstract as async await base break case catch checked class const continue default "
                        "delegate do else enum event explicit extern finally fixed for foreach goto if implicit in interface "
                        "internal is lock namespace new operator out override params private protected public readonly ref "
                        "return sealed sizeof stackalloc static struct switch this throw try typeof unchecked unsafe using "
                        "var virtual volatile while record get set init");
                    c.types = words("bool byte char decimal double float int long object sbyte short string uint ulong "
                        "ushort void dynamic");
                    c.annotations = false;
                    c.class_after = words("class interface struct enum record namespace");
                    m["csharp"] = c;
                }
                {
                    spec s; s.name = "swift";
                    s.keywords = words("associatedtype class deinit enum extension fileprivate func import init inout internal "
                        "let open operator private protocol public rethrows static struct subscript typealias var break case "
                        "continue default defer do else fallthrough for guard if in repeat return switch where while as "
                        "catch is throw throws try async await some any self Self actor");
                    s.types = words("Int Double Float Bool String Character Array Dictionary Set Optional Void UInt Int64");
                    s.literals = words("true false nil");
                    s.builtins = words("print debugPrint fatalError precondition");
                    s.line_comments = {"//"}; s.block_comments = {{"/*", "*/"}}; s.quotes = "\""; s.annotations = true;
                    s.title_after = words("func"); s.class_after = words("class struct enum protocol extension actor");
                    m["swift"] = s;
                }
                {
                    spec s; s.name = "bash";
                    s.keywords = words("if then else elif fi case esac for select while until do done in function time "
                        "return local export readonly declare unset shift break continue");
                    s.literals = words("true false");
                    s.builtins = words("echo printf cd ls pwd cat grep sed awk find mkdir rm cp mv chmod chown source exit "
                        "read test set eval exec trap kill sudo curl git brew make cmake ninja python3 pip npm node tar "
                        "ssh rsync wc head tail sort uniq xargs tee touch");
                    s.line_comments = {"#"}; s.backtick_strings = true; s.dollar_variables = true;
                    s.title_after = words("function");
                    m["bash"] = s;
                }
                {
                    spec s; s.name = "sql";
                    s.keywords = words("select from where and or not insert into values update set delete create table drop "
                        "alter add column index view join inner left right outer full on group by order having limit offset "
                        "as distinct union all case when then else end primary key foreign references unique default "
                        "begin commit rollback transaction if exists in is like between with returning pragma");
                    s.types = words("integer int text real blob varchar char boolean date timestamp numeric float bigint");
                    s.literals = words("null true false");
                    s.builtins = words("count sum avg min max coalesce length lower upper substr round now");
                    s.line_comments = {"--"}; s.block_comments = {{"/*", "*/"}}; s.case_insensitive = true;
                    m["sql"] = s;
                }
                {
                    spec s; s.name = "php";
                    s.keywords = words("abstract and as break case catch class clone const continue declare default do echo "
                        "else elseif empty enddeclare endfor endforeach endif endswitch endwhile extends final finally fn for "
                        "foreach function global if implements include instanceof interface isset list match namespace new "
                        "or print private protected public readonly require return static switch throw trait try unset use "
                        "var while yield");
                    s.literals = words("true false null TRUE FALSE NULL");
                    s.line_comments = {"//", "#"}; s.block_comments = {{"/*", "*/"}}; s.dollar_variables = true;
                    s.title_after = words("function"); s.class_after = words("class interface trait extends implements");
                    m["php"] = s;
                }
                {
                    spec s; s.name = "ruby";
                    s.keywords = words("alias and begin break case class def defined? do else elsif end ensure for if in "
                        "module next not or redo rescue retry return self super then undef unless until when while yield "
                        "require attr_accessor attr_reader puts");
                    s.literals = words("true false nil");
                    s.line_comments = {"#"};
                    s.title_after = words("def"); s.class_after = words("class module");
                    m["ruby"] = s;
                }
                {
                    spec s; s.name = "lua";
                    s.keywords = words("and break do else elseif end for function goto if in local not or repeat return then "
                        "until while");
                    s.literals = words("true false nil");
                    s.builtins = words("print pairs ipairs require table string math tostring tonumber type");
                    s.line_comments = {"--"}; s.block_comments = {{"--[[", "]]"}};
                    s.title_after = words("function");
                    m["lua"] = s;
                }
                {
                    spec s; s.name = "dockerfile";
                    s.keywords = words("FROM RUN CMD LABEL EXPOSE ENV ADD COPY ENTRYPOINT VOLUME USER WORKDIR ARG ONBUILD "
                        "STOPSIGNAL HEALTHCHECK SHELL AS");
                    s.line_comments = {"#"}; s.dollar_variables = true;
                    m["dockerfile"] = s;
                }
                {
                    spec s; s.name = "cmake";
                    s.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction macro "
                        "endmacro return set unset option project add_executable add_library target_link_libraries "
                        "target_include_directories target_compile_options target_compile_definitions find_package "
                        "include message cmake_minimum_required add_subdirectory install list string file "
                        "set_source_files_properties add_custom_command add_custom_target add_dependencies");
                    s.literals = words("ON OFF TRUE FALSE PRIVATE PUBLIC INTERFACE REQUIRED STATUS FATAL_ERROR WARNING");
                    s.line_comments = {"#"}; s.quotes = "\""; s.dollar_variables = true;
                    m["cmake"] = s;
                }
                {
                    spec s; s.name = "ini";
                    s.line_comments = {"#", ";"};
                    s.literals = words("true false on off yes no");
                    m["ini"] = s;
                }
                return m;
            }();
            return all;
        }

        // ------------------------------------------------------------------------------------------
        // Le découpeur commun.

        // La fin d'une chaîne ouverte par q en i (i sur le délimiteur), échappements par '\' compris.
        size_t string_end(const std::string& s, size_t i, char q, bool multiline)
        {
            size_t j = i + 1;
            while (j < s.size())
            {
                if (s[j] == '\\') { j += 2; continue; }
                if (s[j] == q) return j + 1;
                if (s[j] == '\n' && !multiline) return j;
                ++j;
            }
            return s.size();
        }

        // Les variables $nom, ${…} et $(…) d'une chaîne du shell, colorées dans la chaîne.
        void emit_shell_string(std::string& out, const std::string& s, size_t from, size_t to)
        {
            out += "<span class=\"hljs-string\">";
            size_t i = from;
            while (i < to)
            {
                if (s[i] == '$' && i + 1 < to && s[from] == '"')
                {
                    size_t j = i + 1;
                    if (s[j] == '{') { while (j < to && s[j] != '}') ++j; if (j < to) ++j; }
                    else while (j < to && ident_char(static_cast<unsigned char>(s[j]))) ++j;
                    if (j > i + 1) { emit(out, s, i, j, "variable"); i = j; continue; }
                }
                size_t j = i + 1;
                while (j < to && s[j] != '$') ++j;
                escape_to(out, s, i, j);
                i = j;
            }
            out += "</span>";
        }

        void run_spec(std::string& out, const std::string& s, const spec& sp)
        {
            const size_t n = s.size();
            size_t i = 0;
            bool line_start = true;
            const char* pending = nullptr;   // la classe du prochain nom : title.function_ après def, title.class_ après class
            while (i < n)
            {
                const char c = s[i];
                const unsigned char uc = static_cast<unsigned char>(c);
                if (c == '\n') { out += '\n'; ++i; line_start = true; continue; }
                if (c == ' ' || c == '\t' || c == '\r') { out += c; ++i; continue; }

                // Le préprocesseur du C : une ligne qui commence par #, jusqu'à sa fin (lignes continuées par \ comprises).
                if (sp.preprocessor && line_start && c == '#')
                {
                    size_t j = i;
                    while (j < n && !(s[j] == '\n' && s[j - 1] != '\\'))
                    {
                        if (starts_at(s, j, "//")) break;
                        ++j;
                    }
                    emit(out, s, i, j, "meta");
                    i = j; line_start = false; continue;
                }
                line_start = false;

                // Les commentaires de bloc, puis de ligne.
                bool done = false;
                for (const auto& bc : sp.block_comments)
                {
                    if (starts_at(s, i, bc.first))
                    {
                        const size_t e = s.find(bc.second, i + bc.first.size());
                        const size_t j = e == std::string::npos ? n : e + bc.second.size();
                        emit(out, s, i, j, "comment"); i = j; done = true; break;
                    }
                }
                if (done) continue;
                for (const auto& lc : sp.line_comments)
                {
                    // Le # du shell n'ouvre un commentaire qu'en début de mot (pas dans $# ni dans a#b).
                    if (starts_at(s, i, lc) && !(lc == "#" && i > 0 && !std::isspace(static_cast<unsigned char>(s[i - 1]))
                        && s[i - 1] != ';' && s[i - 1] != '(' ))
                    {
                        size_t j = s.find('\n', i);
                        if (j == std::string::npos) j = n;
                        emit(out, s, i, j, "comment"); i = j; done = true; break;
                    }
                }
                if (done) continue;

                // Les chaînes.
                if (sp.triple_quotes && (starts_at(s, i, "\"\"\"") || starts_at(s, i, "'''")))
                {
                    const std::string q = s.substr(i, 3);
                    const size_t e = s.find(q, i + 3);
                    const size_t j = e == std::string::npos ? n : e + 3;
                    emit(out, s, i, j, "string"); i = j; continue;
                }
                if (sp.char_literals && c == '\'')
                {
                    // Rust : 'a', '\n', '\u{…}' sont des caractères ; 'a seul est une durée de vie.
                    size_t j = i + 1;
                    if (j < n && s[j] == '\\') { j += 2; while (j < n && s[j] != '\'' && s[j] != '\n' && j - i < 12) ++j; }
                    else if (j < n) ++j;
                    if (j < n && s[j] == '\'') { emit(out, s, i, j + 1, "string"); i = j + 1; continue; }
                    size_t k = i + 1;
                    while (k < n && ident_char(static_cast<unsigned char>(s[k]))) ++k;
                    emit(out, s, i, k, "symbol"); i = k; continue;
                }
                if (sp.quotes.find(c) != std::string::npos || (sp.backtick_strings && c == '`'))
                {
                    const size_t j = string_end(s, i, c, c == '`' || sp.name == "bash");
                    if (sp.dollar_variables && sp.name != "php") emit_shell_string(out, s, i, j);
                    else emit(out, s, i, j, "string");
                    i = j; continue;
                }

                // Les variables $nom (shell, PHP, CMake).
                if (sp.dollar_variables && c == '$' && i + 1 < n)
                {
                    size_t j = i + 1;
                    if (s[j] == '{' || (sp.name == "cmake" && s[j] == '{'))
                    {
                        while (j < n && s[j] != '}' && s[j] != '\n') ++j;
                        if (j < n && s[j] == '}') ++j;
                    }
                    else if (ident_char(static_cast<unsigned char>(s[j]))) { while (j < n && ident_char(static_cast<unsigned char>(s[j]))) ++j; }
                    else if (std::strchr("@#?*!$-0123456789", s[j])) ++j;
                    if (j > i + 1) { emit(out, s, i, j, "variable"); i = j; continue; }
                }

                // Les annotations et décorateurs : @nom.
                if (sp.annotations && c == '@' && i + 1 < n && ident_start(static_cast<unsigned char>(s[i + 1])))
                {
                    size_t j = i + 1;
                    while (j < n && (ident_char(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
                    emit(out, s, i, j, "meta"); i = j; continue;
                }

                // Les nombres.
                if (std::isdigit(uc) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
                {
                    if (i == 0 || !ident_char(static_cast<unsigned char>(s[i - 1])))
                    {
                        const size_t j = number_end(s, i);
                        emit(out, s, i, j, "number"); i = j; continue;
                    }
                }

                // Les mots.
                if (ident_start(uc) || (c == '@' && sp.name == "cpp"))
                {
                    size_t j = i + 1;
                    while (j < n && ident_char(static_cast<unsigned char>(s[j]))) ++j;
                    // Les macros de Rust (println!) et les mots du shell qui finissent par ? (Ruby : defined?).
                    if (j < n && (s[j] == '!' && sp.name == "rust")) ++j;
                    std::string w = s.substr(i, j - i);
                    const std::string key = sp.case_insensitive ? lower(w) : w;
                    const char* cls = nullptr;
                    if (pending && !sp.keywords.count(key)) { cls = pending; pending = nullptr; }
                    else if (sp.keywords.count(key))
                    {
                        cls = "keyword";
                        if (sp.title_after.count(key)) pending = "title function_";
                        else if (sp.class_after.count(key)) pending = "title class_";
                    }
                    else if (sp.types.count(key)) cls = "type";
                    else if (sp.literals.count(key)) cls = "literal";
                    else if (sp.builtins.count(key)) cls = "built_in";
                    else if ((sp.name == "python" || sp.name == "javascript" || sp.name == "typescript")
                        && (w == "self" || w == "this")) cls = "variable language_";
                    emit(out, s, i, j, cls);
                    i = j; continue;
                }

                // Un signe : le nom attendu après def ou class ne vient plus après une parenthèse ou une accolade.
                if (c == '(' || c == '{' || c == ';' || c == '=' || c == ':') pending = nullptr;
                escape_to(out, s, i, i + 1);
                ++i;
            }
        }

        // ------------------------------------------------------------------------------------------
        // Les découpeurs à part.

        // HTML et XML : balises, attributs, chaînes, commentaires, entités, <!DOCTYPE> et <?xml ?>.
        void run_xml(std::string& out, const std::string& s)
        {
            const size_t n = s.size();
            size_t i = 0;
            while (i < n)
            {
                if (starts_at(s, i, "<!--"))
                {
                    const size_t e = s.find("-->", i + 4);
                    const size_t j = e == std::string::npos ? n : e + 3;
                    emit(out, s, i, j, "comment"); i = j; continue;
                }
                if (starts_at(s, i, "<!") || starts_at(s, i, "<?"))
                {
                    const size_t e = s.find('>', i);
                    const size_t j = e == std::string::npos ? n : e + 1;
                    emit(out, s, i, j, "meta"); i = j; continue;
                }
                if (s[i] == '<' && i + 1 < n && (std::isalpha(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '/'))
                {
                    out += "<span class=\"hljs-tag\">";
                    size_t j = i + 1;
                    if (s[j] == '/') ++j;
                    escape_to(out, s, i, j);
                    size_t k = j;
                    while (k < n && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '-' || s[k] == ':' || s[k] == '_' || s[k] == '.')) ++k;
                    emit(out, s, j, k, "name");
                    i = k;
                    while (i < n && s[i] != '>')
                    {
                        const char c = s[i];
                        if (c == '"' || c == '\'')
                        {
                            const size_t e = s.find(c, i + 1);
                            const size_t j2 = e == std::string::npos ? n : e + 1;
                            emit(out, s, i, j2, "string"); i = j2; continue;
                        }
                        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '@')
                        {
                            size_t j2 = i;
                            while (j2 < n && (std::isalnum(static_cast<unsigned char>(s[j2])) || s[j2] == '-' || s[j2] == ':' || s[j2] == '_' || s[j2] == '.' || s[j2] == '@')) ++j2;
                            emit(out, s, i, j2, "attr"); i = j2; continue;
                        }
                        escape_to(out, s, i, i + 1); ++i;
                    }
                    if (i < n) { escape_to(out, s, i, i + 1); ++i; }
                    out += "</span>";
                    continue;
                }
                if (s[i] == '&')
                {
                    size_t j = i + 1;
                    while (j < n && j - i < 12 && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '#')) ++j;
                    if (j < n && s[j] == ';' && j > i + 1) { emit(out, s, i, j + 1, "symbol"); i = j + 1; continue; }
                }
                escape_to(out, s, i, i + 1); ++i;
            }
        }

        // CSS (et SCSS, Less, à peu près) : sélecteurs, propriétés, valeurs, @règles, commentaires.
        void run_css(std::string& out, const std::string& s)
        {
            const size_t n = s.size();
            size_t i = 0;
            int depth = 0;
            bool in_value = false;
            while (i < n)
            {
                const char c = s[i];
                if (starts_at(s, i, "/*"))
                {
                    const size_t e = s.find("*/", i + 2);
                    const size_t j = e == std::string::npos ? n : e + 2;
                    emit(out, s, i, j, "comment"); i = j; continue;
                }
                if (starts_at(s, i, "//") && (i == 0 || s[i - 1] != ':')) // SCSS ; pas les url(http://…)
                {
                    size_t j = s.find('\n', i); if (j == std::string::npos) j = n;
                    emit(out, s, i, j, "comment"); i = j; continue;
                }
                if (c == '"' || c == '\'')
                {
                    const size_t j = string_end(s, i, c, false);
                    emit(out, s, i, j, "string"); i = j; continue;
                }
                if (c == '{') { ++depth; in_value = false; out += c; ++i; continue; }
                if (c == '}') { depth = std::max(0, depth - 1); in_value = false; out += c; ++i; continue; }
                if (c == ';') { in_value = false; out += c; ++i; continue; }
                if (c == '@')
                {
                    size_t j = i + 1;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-')) ++j;
                    emit(out, s, i, j, "keyword"); i = j; continue;
                }
                if (in_value)
                {
                    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])))
                        || (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
                    {
                        size_t j = i + 1;
                        while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == '%')) ++j;
                        emit(out, s, i, j, "number"); i = j; continue;
                    }
                    if (c == '#')
                    {
                        size_t j = i + 1;
                        while (j < n && std::isxdigit(static_cast<unsigned char>(s[j]))) ++j;
                        emit(out, s, i, j, "number"); i = j; continue;
                    }
                    if (std::isalpha(static_cast<unsigned char>(c)) || c == '-')
                    {
                        size_t j = i;
                        while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-' || s[j] == '_')) ++j;
                        const bool fn = j < n && s[j] == '(';
                        const std::string w = s.substr(i, j - i);
                        emit(out, s, i, j, fn ? "built_in" : (w == "important" ? "meta" : nullptr)); i = j; continue;
                    }
                    escape_to(out, s, i, i + 1); ++i; continue;
                }
                // Hors d'une valeur : une propriété (nom suivi de ':' dans un bloc) ou un sélecteur.
                if (depth > 0 && (std::isalpha(static_cast<unsigned char>(c)) || c == '-'))
                {
                    size_t j = i;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-' || s[j] == '_')) ++j;
                    size_t k = j;
                    while (k < n && (s[k] == ' ' || s[k] == '\t')) ++k;
                    if (k < n && s[k] == ':' && !(k + 1 < n && std::isalpha(static_cast<unsigned char>(s[k + 1])) && s.find('{', k) < s.find(';', k)))
                    {
                        emit(out, s, i, j, "attribute"); i = j;
                        escape_to(out, s, i, k + 1); i = k + 1; in_value = true; continue;
                    }
                }
                if (c == '.' || c == '#')
                {
                    size_t j = i + 1;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-' || s[j] == '_')) ++j;
                    emit(out, s, i, j, c == '.' ? "selector-class" : "selector-id"); i = j; continue;
                }
                if (c == ':')
                {
                    size_t j = i + 1;
                    if (j < n && s[j] == ':') ++j;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-')) ++j;
                    emit(out, s, i, j, "selector-pseudo"); i = j; continue;
                }
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    size_t j = i;
                    while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-')) ++j;
                    emit(out, s, i, j, "selector-tag"); i = j; continue;
                }
                escape_to(out, s, i, i + 1); ++i;
            }
        }

        // JSON : clés (une chaîne suivie de ':'), chaînes, nombres, true/false/null.
        void run_json(std::string& out, const std::string& s)
        {
            const size_t n = s.size();
            size_t i = 0;
            while (i < n)
            {
                const char c = s[i];
                if (c == '"')
                {
                    const size_t j = string_end(s, i, '"', false);
                    size_t k = j;
                    while (k < n && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
                    emit(out, s, i, j, k < n && s[k] == ':' ? "attr" : "string"); i = j; continue;
                }
                if (starts_at(s, i, "//"))
                {
                    size_t j = s.find('\n', i); if (j == std::string::npos) j = n;
                    emit(out, s, i, j, "comment"); i = j; continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1]))))
                {
                    const size_t j = number_end(s, i + (c == '-' ? 1 : 0));
                    emit(out, s, i, j, "number"); i = j; continue;
                }
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    size_t j = i;
                    while (j < n && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
                    const std::string w = s.substr(i, j - i);
                    emit(out, s, i, j, (w == "true" || w == "false" || w == "null") ? "literal" : nullptr); i = j; continue;
                }
                escape_to(out, s, i, i + 1); ++i;
            }
        }

        // YAML : clés, commentaires, chaînes, nombres, littéraux, ancres, séparateurs de documents.
        void run_yaml(std::string& out, const std::string& s)
        {
            const size_t n = s.size();
            size_t i = 0;
            while (i < n)
            {
                size_t e = s.find('\n', i);
                if (e == std::string::npos) e = n;
                // Une ligne : son retrait, puis une clé « nom: » éventuelle, puis la valeur.
                size_t j = i;
                while (j < e && (s[j] == ' ' || s[j] == '\t')) ++j;
                if (starts_at(s, j, "- ")) { escape_to(out, s, i, j + 2); i = j + 2; j = i; }
                else { escape_to(out, s, i, j); i = j; }
                if (starts_at(s, i, "---") || starts_at(s, i, "..."))
                {
                    emit(out, s, i, e, "meta"); i = e;
                }
                else
                {
                    size_t k = i;
                    while (k < e && s[k] != ':' && s[k] != '#' && s[k] != '"' && s[k] != '\'') ++k;
                    if (k < e && s[k] == ':' && (k + 1 == e || s[k + 1] == ' ') && k > i)
                    {
                        emit(out, s, i, k, "attr"); i = k;
                    }
                    while (i < e)
                    {
                        const char c = s[i];
                        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) { emit(out, s, i, e, "comment"); i = e; break; }
                        if (c == '"' || c == '\'')
                        {
                            const size_t q = string_end(s, i, c, false);
                            emit(out, s, i, std::min(q, e), "string"); i = std::min(q, e); continue;
                        }
                        if ((c == '&' || c == '*') && i + 1 < e && ident_start(static_cast<unsigned char>(s[i + 1])))
                        {
                            size_t q = i + 1;
                            while (q < e && ident_char(static_cast<unsigned char>(s[q]))) ++q;
                            emit(out, s, i, q, "symbol"); i = q; continue;
                        }
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')
                        {
                            size_t q = i;
                            while (q < e && !std::isspace(static_cast<unsigned char>(s[q])) && s[q] != ',' && s[q] != ']' && s[q] != '}') ++q;
                            const std::string w = lower(s.substr(i, q - i));
                            bool number = !w.empty();
                            for (char d : w) if (!(std::isdigit(static_cast<unsigned char>(d)) || d == '.' || d == '-' || d == '+' || d == 'e' || d == '_')) number = false;
                            if (w == "-" || w == ".") number = false;
                            const char* cls = number ? "number"
                                : (w == "true" || w == "false" || w == "null" || w == "yes" || w == "no" || w == "~") ? "literal" : nullptr;
                            emit(out, s, i, q, cls); i = q; continue;
                        }
                        escape_to(out, s, i, i + 1); ++i;
                    }
                }
                if (e < n) out += '\n';
                i = e + 1;
            }
        }

        // diff et patch : ajouts, retraits, en-têtes et morceaux.
        void run_diff(std::string& out, const std::string& s)
        {
            const size_t n = s.size();
            size_t i = 0;
            while (i < n)
            {
                size_t e = s.find('\n', i);
                if (e == std::string::npos) e = n;
                const char* cls = nullptr;
                if (starts_at(s, i, "+++") || starts_at(s, i, "---") || starts_at(s, i, "diff ") || starts_at(s, i, "index ")) cls = "meta";
                else if (starts_at(s, i, "@@")) cls = "meta";
                else if (s[i] == '+') cls = "addition";
                else if (s[i] == '-') cls = "deletion";
                emit(out, s, i, e, cls);
                if (e < n) out += '\n';
                i = e + 1;
            }
        }

        // ------------------------------------------------------------------------------------------
        // Les noms des langages.

        const std::map<std::string, std::string>& aliases()
        {
            static const std::map<std::string, std::string> all = {
                {"c", "c"}, {"h", "c"},
                {"cpp", "cpp"}, {"c++", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"hh", "cpp"},
                {"hxx", "cpp"}, {"objc", "cpp"}, {"objective-c", "cpp"}, {"objectivec", "cpp"}, {"objc++", "cpp"},
                {"objective-c++", "cpp"}, {"mm", "cpp"}, {"metal", "cpp"}, {"cuda", "cpp"}, {"cu", "cpp"},
                {"python", "python"}, {"py", "python"}, {"python3", "python"}, {"py3", "python"}, {"gyp", "python"},
                {"javascript", "javascript"}, {"js", "javascript"}, {"jsx", "javascript"}, {"mjs", "javascript"},
                {"cjs", "javascript"}, {"node", "javascript"},
                {"typescript", "typescript"}, {"ts", "typescript"}, {"tsx", "typescript"}, {"mts", "typescript"},
                {"json", "json"}, {"jsonc", "json"}, {"json5", "json"}, {"jsonl", "json"},
                {"rust", "rust"}, {"rs", "rust"},
                {"go", "go"}, {"golang", "go"},
                {"java", "java"}, {"kotlin", "kotlin"}, {"kt", "kotlin"}, {"kts", "kotlin"},
                {"csharp", "csharp"}, {"cs", "csharp"}, {"c#", "csharp"},
                {"swift", "swift"},
                {"bash", "bash"}, {"sh", "bash"}, {"shell", "bash"}, {"zsh", "bash"}, {"console", "bash"},
                {"shellsession", "bash"}, {"terminal", "bash"}, {"ksh", "bash"},
                {"sql", "sql"}, {"sqlite", "sql"}, {"postgres", "sql"}, {"postgresql", "sql"}, {"mysql", "sql"}, {"psql", "sql"},
                {"php", "php"}, {"ruby", "ruby"}, {"rb", "ruby"}, {"lua", "lua"},
                {"dockerfile", "dockerfile"}, {"docker", "dockerfile"}, {"cmake", "cmake"},
                {"ini", "ini"}, {"toml", "ini"}, {"cfg", "ini"}, {"conf", "ini"}, {"properties", "ini"},
                {"html", "xml"}, {"htm", "xml"}, {"xml", "xml"}, {"xhtml", "xml"}, {"svg", "xml"}, {"plist", "xml"},
                {"vue", "xml"}, {"xsl", "xml"},
                {"css", "css"}, {"scss", "css"}, {"sass", "css"}, {"less", "css"},
                {"yaml", "yaml"}, {"yml", "yaml"},
                {"diff", "diff"}, {"patch", "diff"},
            };
            return all;
        }

        // Le nombre de fois que w apparaît dans s comme mot entier (au plus cap).
        int word_count(const std::string& s, const std::string& w, int cap = 5)
        {
            int count = 0;
            size_t p = 0;
            while (count < cap && (p = s.find(w, p)) != std::string::npos)
            {
                const bool before = p == 0 || !ident_char(static_cast<unsigned char>(s[p - 1]));
                const size_t e = p + w.size();
                const bool after = e >= s.size() || !ident_char(static_cast<unsigned char>(s[e])) || !ident_char(static_cast<unsigned char>(w.back()));
                if (before && after) ++count;
                p = e;
            }
            return count;
        }

        bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }
    }

    std::string highlight_language(const std::string& name)
    {
        std::string n = lower(name);
        // Le premier mot seulement (```python title="x.py"), sans le point d'un nom de fichier (.py).
        const size_t sp = n.find_first_of(" \t{");
        if (sp != std::string::npos) n = n.substr(0, sp);
        if (!n.empty() && n[0] == '.') n = n.substr(1);
        const auto it = aliases().find(n);
        return it == aliases().end() ? std::string() : it->second;
    }

    std::vector<std::string> highlight_languages()
    {
        std::set<std::string> names;
        for (const auto& a : aliases()) names.insert(a.second);
        return std::vector<std::string>(names.begin(), names.end());
    }

    std::string detect_language(const std::string& code)
    {
        // Le début du texte suffit, comme pour highlight.js, qui ne devinait que sous 20 000 caractères.
        const std::string s = code.substr(0, 20000);
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        const std::string head = s.substr(b, 200);
        if (starts_at(head, 0, "#!"))
        {
            const std::string line = head.substr(0, head.find('\n'));
            if (has(line, "python")) return "python";
            if (has(line, "node")) return "javascript";
            if (has(line, "ruby")) return "ruby";
            if (has(line, "php")) return "php";
            if (has(line, "sh")) return "bash";
        }
        if (starts_at(head, 0, "<?php")) return "php";
        if (starts_at(head, 0, "<?xml") || starts_at(lower(head), 0, "<!doctype") || starts_at(lower(head), 0, "<html")) return "xml";
        if (starts_at(head, 0, "diff --git") || starts_at(head, 0, "--- ") || starts_at(head, 0, "@@ ")) return "diff";
        if ((head[0] == '{' || head[0] == '[') && has(s, "\"") && !has(s, ";") && !has(s, "=>") && !has(s, "function"))
        {
            // Un objet ou un tableau JSON : des clés entre guillemets suivies de ':', ou un tableau de valeurs.
            if (has(s, "\":") || has(s, "\" :") || head[0] == '[') return "json";
        }

        std::map<std::string, int> score;
        auto add = [&](const char* lang, int pts) { score[lang] += pts; };
        // Les signes forts.
        if (has(s, "#include")) { add("cpp", 3); add("c", 3); }
        if (has(s, "std::") || has(s, "template<") || has(s, "template <") || has(s, "nullptr") || has(s, "namespace ")) add("cpp", 6);
        if (has(s, "cout <<") || has(s, "->") ) { add("cpp", 1); add("c", 1); add("rust", 1); }
        if (has(s, "printf(") && !has(s, "std::")) add("c", 2);
        if (has(s, "def ") && has(s, ":\n")) add("python", 5);
        if (has(s, "import ") && !has(s, ";") && !has(s, "{")) add("python", 2);
        if (has(s, "self.") || has(s, "elif ") || has(s, "print(") || has(s, "__init__")) add("python", 3);
        if (has(s, "fn ") && (has(s, "let ") || has(s, "->"))) add("rust", 6);
        if (has(s, "let mut ") || has(s, "impl ") || has(s, "println!") || has(s, "use std::")) add("rust", 6);
        if (has(s, "func ") && (has(s, ":=") || has(s, "package "))) add("go", 7);
        if (has(s, "package main") || has(s, "fmt.")) add("go", 5);
        if (has(s, "=>") || has(s, "console.") || has(s, "document.") || has(s, "require(") || has(s, "export default")) add("javascript", 4);
        if (has(s, "const ") || has(s, "let ") || has(s, "function ")) add("javascript", 2);
        if (has(s, ": string") || has(s, ": number") || has(s, "interface ") || has(s, ": boolean")) add("typescript", 6);
        if (score["typescript"] > 0) add("typescript", score["javascript"]);
        if (has(s, "public static void") || has(s, "System.out") || has(s, "public class")) add("java", 7);
        if (has(s, "using System") || has(s, "Console.Write")) add("csharp", 8);
        if (has(s, "import SwiftUI") || has(s, "import Foundation") || (has(s, "func ") && has(s, "var ") && has(s, "let "))) add("swift", 5);
        if (has(s, "fun ") && (has(s, "val ") || has(s, "println("))) add("kotlin", 6);
        if (word_count(s, "echo") || word_count(s, "fi") || word_count(s, "done") || has(s, "$(") || has(s, "${") || has(s, "sudo ")
            || has(s, "brew ") || has(s, "git ") || has(s, "cd ") || has(s, "export ")) add("bash", 3);
        if (has(s, "$ ") && head[0] == '$') add("bash", 5);
        {
            const std::string l = lower(s);
            if (word_count(l, "select") && word_count(l, "from")) add("sql", 7);
            if (has(l, "create table") || has(l, "insert into") || (word_count(l, "update") && word_count(l, "set") && word_count(l, "where"))) add("sql", 7);
        }
        if (has(s, "<div") || has(s, "</") || has(s, "/>")) add("xml", 5);
        {
            // Du CSS : sélecteur { propriété: valeur; }
            const size_t o = s.find('{');
            if (o != std::string::npos && s.find(';', o) != std::string::npos && s.find(':', o) != std::string::npos
                && !has(s, "(") && !has(s, "=")) add("css", 6);
        }
        if (has(s, "end\n") && (has(s, "def ") || has(s, "puts "))) add("ruby", 6);
        if (has(s, "local ") && has(s, "end")) add("lua", 3);
        if (has(s, "FROM ") && (has(s, "RUN ") || has(s, "COPY "))) add("dockerfile", 8);
        if (has(s, "cmake_minimum_required") || has(s, "add_executable(") || has(s, "target_link_libraries(")) add("cmake", 9);
        {
            // Du YAML : plusieurs lignes « clé: valeur » sans accolades ni points-virgules.
            int keys = 0;
            size_t p = 0;
            while (p < s.size() && keys < 3)
            {
                size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size();
                const std::string line = s.substr(p, e - p);
                const size_t c = line.find(": ");
                if (c != std::string::npos && c > 0 && line.find_first_of("{};()=") == std::string::npos) ++keys;
                p = e + 1;
            }
            if (keys >= 3 && !has(s, "{")) add("yaml", 5);
        }
        // Les mots-clés de chaque fiche, en appoint.
        for (const auto& kv : specs())
        {
            if (kv.first == "ini" || kv.first == "dockerfile") continue;
            int hits = 0;
            for (const auto& k : kv.second.keywords) if (k.size() > 2 && word_count(s, k, 1)) ++hits;
            score[kv.first] += std::min(hits, 8) / 2;
        }
        std::string best;
        int top = 0;
        for (const auto& kv : score) if (kv.second > top) { top = kv.second; best = kv.first; }
        // Un score faible ne colore pas : du texte brut vaut mieux qu'une couleur fausse.
        return top >= 5 ? best : std::string();
    }

    std::string highlight_code(const std::string& code, const std::string& language, std::string* used)
    {
        std::string lang = language.empty() ? detect_language(code) : highlight_language(language);
        if (used) *used = lang;
        std::string out;
        out.reserve(code.size() + code.size() / 2);
        if (lang.empty()) { escape_to(out, code, 0, code.size()); return out; }
        if (lang == "xml") run_xml(out, code);
        else if (lang == "css") run_css(out, code);
        else if (lang == "json") run_json(out, code);
        else if (lang == "yaml") run_yaml(out, code);
        else if (lang == "diff") run_diff(out, code);
        else
        {
            const auto it = specs().find(lang);
            if (it == specs().end()) { if (used) used->clear(); escape_to(out, code, 0, code.size()); return out; }
            run_spec(out, code, it->second);
        }
        return out;
    }
}
