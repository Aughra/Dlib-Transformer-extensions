// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "epreuve_core.h"

#include <cmath>
#include <cstdlib>

#import <Foundation/Foundation.h>
#include <unicode/uchar.h>

#include "../common/platform.h"

namespace gulliver
{
    namespace
    {
        NSString* ns(const std::string& s)
        {
            NSString* r = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
            return r ? r : @"";
        }

        std::string str(NSString* s)
        {
            const char* p = [s UTF8String];
            return p ? std::string(p) : std::string();
        }

        NSRegularExpression* rx(NSString* pattern, NSRegularExpressionOptions options = 0)
        {
            NSError* error = nil;
            NSRegularExpression* r = [NSRegularExpression regularExpressionWithPattern:pattern options:options error:&error];
            return r;
        }

        NSString* replace(NSString* text, NSRegularExpression* r, NSString* with)
        {
            if (!r) return text;
            return [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:with];
        }

        // Les blancs de str.strip() de Python : les espaces Unicode et les fins de ligne.
        bool python_space(UChar32 c)
        {
            return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 || u_isUWhiteSpace(c);
        }

        NSString* strip(NSString* s)
        {
            NSUInteger a = 0, b = s.length;
            // Les blancs de Python sont tous dans le plan de base : une unité UTF-16 chacun.
            while (a < b && python_space([s characterAtIndex:a])) ++a;
            while (b > a && python_space([s characterAtIndex:b - 1])) --b;
            return [s substringWithRange:NSMakeRange(a, b - a)];
        }

        NSString* rstrip_chars(NSString* s, NSString* chars)
        {
            NSUInteger b = s.length;
            while (b > 0 && [chars rangeOfString:[s substringWithRange:NSMakeRange(b - 1, 1)]].location != NSNotFound) --b;
            return [s substringToIndex:b];
        }

        // Python : float(re.sub("[   ]", "", s).replace(",", ".")).
        double to_number(NSString* s)
        {
            NSString* t = replace(s, rx(@"[   ]"), @"");
            t = [t stringByReplacingOccurrencesOfString:@"," withString:@"."];
            std::string text = str(t);
            // Des chiffres d'autres écritures (\d d'ICU et de Python) : ramenés aux chiffres ASCII.
            std::string ascii;
            NSString* back = ns(text);
            for (NSUInteger i = 0; i < back.length; ++i)
            {
                const unichar c = [back characterAtIndex:i];
                const int32_t d = u_charDigitValue(c);
                if (d >= 0 && c > 0x7f) ascii += static_cast<char>('0' + d);
                else ascii += static_cast<char>(c < 0x80 ? c : '?');
            }
            return std::strtod(ascii.c_str(), nullptr);
        }

        bool matches_whole_word(NSString* text, NSString* word)
        {
            NSString* pattern = [NSString stringWithFormat:@"(?<![a-z0-9])%@(?![a-z0-9])",
                [NSRegularExpression escapedPatternForString:word]];
            NSRegularExpression* r = rx(pattern);
            return r && [r firstMatchInString:text options:0 range:NSMakeRange(0, text.length)] != nil;
        }
    }

    std::vector<epreuve_question> load_epreuve_questions(const std::string& path)
    {
        std::vector<epreuve_question> out;
        std::string text;
        json j;
        if (!read_file(path, text, 16u << 20) || !json::parse(text, j)) return out;
        for (const json& q : j["questions"].elements())
        {
            epreuve_question e;
            e.id = q["id"].str();
            e.theme = q["theme"].str();
            e.type = q["type"].str();
            e.question = q["question"].str();
            e.expected = q["attendu"];
            e.forbidden = q["interdit"];
            out.push_back(e);
        }
        return out;
    }

    std::vector<std::string> epreuve_blocks(const std::string& output, const std::vector<epreuve_question>& questions)
    {
        // Python : output.split("\n=== ")[1:], puis head, _, body = part.partition("\n").
        std::vector<std::pair<std::string, std::string>> found;
        const std::string sep = "\n=== ";
        size_t at = output.find(sep);
        while (at != std::string::npos)
        {
            const size_t start = at + sep.size();
            const size_t next = output.find(sep, start);
            const std::string part = output.substr(start, next == std::string::npos ? std::string::npos : next - start);
            const size_t nl = part.find('\n');
            std::string head = nl == std::string::npos ? part : part.substr(0, nl);
            std::string body = nl == std::string::npos ? std::string() : part.substr(nl + 1);
            const size_t end = body.rfind("\n--- ");
            if (end != std::string::npos) body = body.substr(0, end);
            found.push_back({str(strip(ns(head))), body});
            at = next;
        }
        std::vector<std::string> out;
        for (const epreuve_question& q : questions)
        {
            std::string block;
            // Le dernier bloc d'une même question l'emporte, comme dans le dictionnaire de Python.
            for (const auto& f : found)
                if (f.first == q.question) block = f.second;
            out.push_back(block);
        }
        return out;
    }

    std::string epreuve_final_answer(const std::string& block_text)
    {
        @autoreleasepool
        {
            NSString* block = ns(block_text);
            NSRegularExpression* mark = rx(@"r[ée]ponse(?:\\s+finale)?\\s*:\\s*", NSRegularExpressionCaseInsensitive);
            NSArray<NSTextCheckingResult*>* marks = [mark matchesInString:block options:0 range:NSMakeRange(0, block.length)];
            NSString* line = @"";
            if (marks.count > 0)
            {
                const NSRange r = marks.lastObject.range;
                NSString* rest = strip([block substringFromIndex:r.location + r.length]);
                if (rest.length > 0)
                {
                    const NSRange nl = [rest rangeOfString:@"\n"];
                    line = strip(nl.location == NSNotFound ? rest : [rest substringToIndex:nl.location]);
                }
            }
            else
            {
                for (NSString* x in [strip(block) componentsSeparatedByString:@"\n"])
                    if (strip(x).length > 0) line = strip(x);
            }
            // Les formules : \frac{1}{6} se lit 1/6, et les délimiteurs \( \) $ et les chevrons s'effacent.
            line = replace(line, rx(@"\\\\d?frac\\{([^{}]*)\\}\\{([^{}]*)\\}"), @"$1/$2");
            line = replace(line, rx(@"\\\\[()\\[\\]]|\\$|\\\\boxed\\{([^{}]*)\\}"), @"$1");
            line = strip(replace(line, rx(@"[*_`«»\"“”<>]"), @""));
            return str(strip(rstrip_chars(line, @" .!")));
        }
    }

    std::string epreuve_plain(const std::string& text)
    {
        @autoreleasepool
        {
            NSString* t = [[ns(text) stringByReplacingOccurrencesOfString:@"’" withString:@"'"] lowercaseString];
            t = [t decomposedStringWithCanonicalMapping];
            NSMutableString* out = [NSMutableString stringWithCapacity:t.length];
            [t enumerateSubstringsInRange:NSMakeRange(0, t.length) options:NSStringEnumerationByComposedCharacterSequences
                usingBlock:^(NSString* piece, NSRange, NSRange, BOOL*) {
                    // Chaque point de code de la séquence, sans les marques non espaçantes (catégorie Mn).
                    for (NSUInteger i = 0; i < piece.length;)
                    {
                        UChar32 c = [piece characterAtIndex:i];
                        NSUInteger n = 1;
                        if (CFStringIsSurrogateHighCharacter(static_cast<UniChar>(c)) && i + 1 < piece.length)
                        {
                            c = CFStringGetLongCharacterForSurrogatePair(static_cast<UniChar>(c), [piece characterAtIndex:i + 1]);
                            n = 2;
                        }
                        if (u_charType(c) != U_NON_SPACING_MARK) [out appendString:[piece substringWithRange:NSMakeRange(i, n)]];
                        i += n;
                    }
                }];
            return str(out);
        }
    }

    bool epreuve_grade(const epreuve_question& q, const std::string& answer_text)
    {
        @autoreleasepool
        {
            NSString* answer = ns(answer_text);
            if (q.type == "nombre")
            {
                NSRegularExpression* number =
                    rx(@"-?\\d{1,3}(?:[   ]\\d{3})+(?:[.,]\\d+)?|-?\\d+(?:[.,]\\d+)?");
                NSTextCheckingResult* m = [number firstMatchInString:answer options:0 range:NSMakeRange(0, answer.length)];
                return m && std::fabs(to_number([answer substringWithRange:m.range]) - q.expected.as_double()) < 1e-6;
            }
            if (q.type == "exact")
            {
                for (const json& e : q.expected.elements())
                    if (e.str() == answer_text) return true;
                return false;
            }
            if (q.type == "motif")
            {
                NSRegularExpression* r = rx(ns(q.expected.str()));
                return r && [r firstMatchInString:answer options:NSMatchingAnchored range:NSMakeRange(0, answer.length)] != nil;
            }
            NSString* text = ns(epreuve_plain(answer_text));
            auto has = [&](const json& w) { return matches_whole_word(text, ns(epreuve_plain(w.str()))); };
            bool any = false;
            for (const json& w : q.expected.elements()) any = any || has(w);
            if (!any) return false;
            for (const json& w : q.forbidden.elements())
                if (has(w)) return false;
            return true;
        }
    }

    std::string epreuve_prefix(const std::string& text, size_t n)
    {
        size_t i = 0, count = 0;
        while (i < text.size() && count < n)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            i += c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            ++count;
        }
        return text.substr(0, std::min(i, text.size()));
    }

    epreuve_result epreuve_score(const std::string& output, const std::vector<epreuve_question>& questions)
    {
        epreuve_result r;
        r.total = static_cast<int>(questions.size());
        r.details = json::array();
        const std::vector<std::string> blocks = epreuve_blocks(output, questions);
        for (size_t i = 0; i < questions.size(); ++i)
        {
            const std::string answer = blocks[i].empty() ? std::string() : epreuve_final_answer(blocks[i]);
            const bool ok = !blocks[i].empty() && epreuve_grade(questions[i], answer);
            if (ok) ++r.right;
            json d = json::object();
            d["id"] = questions[i].id;
            d["theme"] = questions[i].theme;
            d["ok"] = ok;
            d["answer"] = epreuve_prefix(answer, 200);
            if (!d["answer"].str().empty()) r.any_answer = true;
            r.details.elements().push_back(d);
        }
        // round() de Python : au pair.
        r.score = r.total ? static_cast<int>(std::nearbyint(100.0 * r.right / r.total)) : 0;
        return r;
    }
}
