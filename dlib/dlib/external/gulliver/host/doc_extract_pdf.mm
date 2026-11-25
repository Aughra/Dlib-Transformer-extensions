// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'extraction du texte d'un PDF par PDFKit (cadriciel natif de macOS), pour le service
    (fork pour macOS).  Remplace PDFium (abandonnée en 7.3.0) ; Gulliver n'embarque plus de
    bibliothèque PDF, le système fournit tout.

    Texte page par page, chaque page sous un titre "## Page N" ; un PDF sans aucune page lisible
    (un document numérisé, dont le texte est dans les pixels) lève doc_error("pdf-images"), et
    ses pages sont alors lues par le modèle de vision, côté page, comme avant (le dessin des
    pages passe par host/pdf_render.cpp, CoreGraphics, sans PDFium ni PDFKit).

    Le texte vient de PDFPage.attributedString.string (plus fidèle que .string, qui perdait des
    lettres doublées sur des PDF comme la licence japonaise de macOS : « Hollyhill » →
    « Holyhil », « iii » → « i »), normalisé en NFKC (CFStringNormalize) pour ramener l'arabe
    aux formes génériques (U+0600-06FF) plutôt qu'aux formes de présentation (U+FB50-FDFF,
    U+FE70-FEFF) que PDFKit rend par défaut.

    PDFKit n'est pas réentrant selon notre essai (extractions concurrentes très lentes) : un
    mutex sérialise les extractions, comme avec PDFium.
*/

#include "doc_extract.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#import <Foundation/Foundation.h>
#import <PDFKit/PDFKit.h>
#import <CoreFoundation/CoreFoundation.h>

namespace gulliver
{
    namespace
    {
        // Le nombre minimal de lettres et chiffres pour qu'une page compte comme lisible
        // (seuil hérité de la version PDFium, minPageChars côté page).
        constexpr int k_min_page_chars = 10;

        std::mutex& pdfkit_mutex()
        {
            static std::mutex m;
            return m;
        }

        // Le nombre de lettres et de chiffres (points de code) d'un texte UTF-8, pour mesurer
        // si une page est lisible.  Approximation : on compte les octets qui ne sont ni espace
        // ni ponctuation ASCII, et les débuts de séquence multioctet (toute lettre non ASCII
        // compte comme une).
        int letters_and_digits(const std::string& s)
        {
            int n = 0;
            for (unsigned char c : s)
            {
                if (c >= 0x80) { if ((c & 0xC0) != 0x80) ++n; }
                else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++n;
            }
            return n;
        }

        // Une NSString normalisée en NFKC, en std::string UTF-8.  Si la chaîne est nil ou vide,
        // rend "".
        std::string ns_to_utf8_nfkc(NSString* s)
        {
            if (!s || s.length == 0) return std::string();
            NSMutableString* m = [s mutableCopy];
            CFStringNormalize((__bridge CFMutableStringRef)m, kCFStringNormalizationFormKC);
            const char* u = [m UTF8String];
            return u ? std::string(u) : std::string();
        }
    }

    // PDFKit est un cadriciel du système : toujours là sur un Mac qui fait tourner Gulliver.
    bool pdf_available()
    {
        return true;
    }

    std::string extract_pdf_text(const std::string& bytes)
    {
        @autoreleasepool
        {
            std::lock_guard<std::mutex> lock(pdfkit_mutex());

            NSData* data = [NSData dataWithBytesNoCopy:(void*)bytes.data()
                                                length:bytes.size()
                                          freeWhenDone:NO];
            PDFDocument* doc = [[PDFDocument alloc] initWithData:data];
            if (!doc)
                throw doc_error("pdf-invalid", "the PDF could not be opened");
            if (doc.isLocked || doc.isEncrypted)
                throw doc_error("pdf-password", "the PDF is password-protected");

            const NSUInteger pages = doc.pageCount;
            if (pages == 0)
                throw doc_error("pdf-invalid", "the PDF has no pages");

            std::string text;
            int readable = 0;
            long long total_chars = 0;
            for (NSUInteger i = 0; i < pages; ++i)
            {
                PDFPage* page = [doc pageAtIndex:i];
                std::string page_text;
                if (page)
                {
                    NSAttributedString* attr = page.attributedString;
                    NSString* s = attr ? attr.string : page.string;
                    page_text = ns_to_utf8_nfkc(s);
                }
                const int n = letters_and_digits(page_text);
                total_chars += n;
                if (n >= k_min_page_chars) ++readable;
                if (!text.empty()) text += "\n\n";
                text += "## Page " + std::to_string(i + 1) + "\n" + page_text;
            }

            if (readable == 0 || total_chars == 0)
                throw doc_error("pdf-images",
                    "the PDF has " + std::to_string(pages) + " pages of images");
            return text;
        }
    }
}
