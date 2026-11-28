// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les débordements de registres des noyaux Metal, lus dans une trace d'Instruments (Gulliver 7.25.0, à la place du
    programme Python glissé dans tools/gpu_debordements.sh).

        build/macos-gulliver/bin/debordements encoders.xml spills.xml

    Les deux fichiers sont les tables exportées par « xctrace export » : metal-application-encoders-list (colonne 8 le
    nom de l'encodeur, 12 son identifiant) et graphics-compiler-spill-events (colonne 2 l'encodeur, 3 les octets).  Dans
    ces exports, une valeur déjà écrite plus haut est remplacée par une référence (attribut ref) à l'élément qui la
    porte (attribut id).  Écrit, pour chaque noyau qui déborde, le nombre d'encodeurs touchés et les octets débordés,
    du plus touché au moins touché ; « aucun débordement » sinon.  Le XML est lu par NSXMLDocument, de macOS.
*/

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#import <Foundation/Foundation.h>

namespace
{
    std::string str(NSString* s)
    {
        const char* p = s ? [s UTF8String] : nullptr;
        return p ? std::string(p) : std::string();
    }

    // Le texte propre d'un élément, avant son premier enfant (Element.text d'ElementTree) ; nil s'il n'en a pas.
    NSString* own_text(NSXMLElement* e)
    {
        NSMutableString* out = nil;
        for (NSXMLNode* c in e.children)
        {
            if (c.kind == NSXMLElementKind) break;
            if (c.kind == NSXMLTextKind)
            {
                if (!out) out = [NSMutableString string];
                [out appendString:c.stringValue ?: @""];
            }
        }
        return out;
    }

    void register_ids(NSXMLElement* e, std::map<std::string, NSXMLElement*>& ids)
    {
        if (NSXMLNode* id = [e attributeForName:@"id"]) ids[str(id.stringValue)] = e;
        for (NSXMLNode* c in e.children)
            if (c.kind == NSXMLElementKind) register_ids(static_cast<NSXMLElement*>(c), ids);
    }

    void collect_rows(NSXMLElement* e, std::vector<NSXMLElement*>& rows)
    {
        if ([e.name isEqualToString:@"row"]) rows.push_back(e);
        for (NSXMLNode* c in e.children)
            if (c.kind == NSXMLElementKind) collect_rows(static_cast<NSXMLElement*>(c), rows);
    }

    // Les lignes d'une table exportée, les références (ref) remplacées par l'élément qu'elles désignent (id) ; rend le
    // document (qui garde les éléments en vie), ou nil.
    NSXMLDocument* rows_of(const char* path, std::vector<std::vector<NSXMLElement*>>& out)
    {
        NSError* error = nil;
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSXMLDocument* keep = [[NSXMLDocument alloc] initWithContentsOfURL:url options:0 error:&error];
        if (!keep)
        {
            std::fprintf(stderr, "debordements: cannot read %s: %s\n", path, str(error.localizedDescription).c_str());
            return nil;
        }
        std::vector<NSXMLElement*> rows;
        collect_rows(keep.rootElement, rows);
        std::map<std::string, NSXMLElement*> ids;
        for (NSXMLElement* row : rows)
        {
            std::vector<NSXMLElement*> values;
            for (NSXMLNode* c in row.children)
            {
                if (c.kind != NSXMLElementKind) continue;
                NSXMLElement* el = static_cast<NSXMLElement*>(c);
                if (NSXMLNode* ref = [el attributeForName:@"ref"])
                {
                    const auto it = ids.find(str(ref.stringValue));
                    if (it == ids.end())
                    {
                        std::fprintf(stderr, "debordements: unknown ref %s in %s\n", str(ref.stringValue).c_str(), path);
                        return nil;
                    }
                    el = it->second;
                }
                else register_ids(el, ids);
                values.push_back(el);
            }
            out.push_back(values);
        }
        return keep;
    }

    // Le nombre de caractères (points de code) d'un texte UTF-8.
    size_t width(const std::string& s)
    {
        size_t n = 0;
        for (const unsigned char c : s) n += (c & 0xc0) != 0x80;
        return n;
    }
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc != 3)
        {
            std::fprintf(stderr, "usage: debordements encoders.xml spills.xml\n");
            return 2;
        }
        std::vector<std::vector<NSXMLElement*>> encoders, spills;
        NSXMLDocument* keep_encoders = rows_of(argv[1], encoders);
        if (!keep_encoders) return 1;
        NSXMLDocument* keep_spills = rows_of(argv[2], spills);
        if (!keep_spills) return 1;
        std::map<std::string, std::string> names;
        for (const auto& v : encoders)
        {
            if (v.size() <= 12) continue;
            NSString* name = own_text(v[8]);
            if (name.length == 0) name = [v[8] attributeForName:@"fmt"].stringValue;
            names[str(own_text(v[12]))] = name ? str(name) : "None";
        }
        // Les noyaux dans l'ordre de leur premier débordement, comme collections.Counter.
        std::vector<std::string> order;
        std::map<std::string, int> count;
        std::map<std::string, std::string> size;
        for (const auto& v : spills)
        {
            if (v.size() <= 3) continue;
            NSString* key_text = own_text(v[2]);
            const std::string key = str(key_text);
            const auto it = names.find(key);
            const std::string name = it != names.end() ? it->second : "encodeur " + (key_text ? key : std::string("None"));
            if (!count.count(name)) order.push_back(name);
            ++count[name];
            size[name] = std::to_string(std::stoll(str(own_text(v[3]))));
        }
        if (order.empty()) std::printf("aucun débordement\n");
        std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) { return count[a] > count[b]; });
        for (const std::string& name : order)
        {
            std::string padded = name;
            for (size_t w = width(name); w < 32; ++w) padded += ' ';
            std::printf("%s %5d encodeurs, %s octets\n", padded.c_str(), count[name], size[name].c_str());
        }
        return 0;
    }
}
