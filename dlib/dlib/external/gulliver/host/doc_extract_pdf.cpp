// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'extraction du texte d'un PDF par PDFium (third_party/pdfium, licence MIT), pour le service
    (fork pour macOS).  Portée depuis pdfText() de web/js/documents.js : le texte page par page,
    chaque page sous un titre "## Page N" ; un PDF sans aucune page lisible (un document numérisé,
    dont le texte est dans les pixels) lève doc_error("pdf-images"), et ses pages sont alors lues
    par le modèle de vision, côté page, comme avant.

    PDFium est chargé à la demande (dlopen) : sans la bibliothèque, le service démarre quand même
    et seuls les PDF lèvent "pdf-reader" (fetch_pdfium.sh la récupère).  Les chaînes de PDFium sont
    en UTF-16LE ; on les convertit en UTF-8 avec CoreFoundation (l'API native de macOS), sans
    <codecvt> (déprécié depuis C++17, et un avertissement que le build du fork refuse).
*/

#include "doc_extract.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

namespace gulliver
{
    namespace
    {
        // Les types et les fonctions de PDFium qu'on utilise (fpdfview.h, fpdf_text.h), résolus par
        // dlsym pour ne pas lier le binaire : le service tourne sans la bibliothèque.
        using FPDF_DOCUMENT = void*;
        using FPDF_PAGE = void*;
        using FPDF_TEXTPAGE = void*;

        struct library_config
        {
            int version;
            const char** user_font_paths;
            void* isolate;
            unsigned int v8_embedder_slot;
            const void* platform;
            const char* rfc_path;
        };

        struct pdfium_api
        {
            void (*InitLibraryWithConfig)(const library_config*) = nullptr;
            FPDF_DOCUMENT (*LoadMemDocument)(const void*, int, const char*) = nullptr;
            void (*CloseDocument)(FPDF_DOCUMENT) = nullptr;
            unsigned long (*GetLastError)() = nullptr;
            int (*GetPageCount)(FPDF_DOCUMENT) = nullptr;
            FPDF_PAGE (*LoadPage)(FPDF_DOCUMENT, int) = nullptr;
            void (*ClosePage)(FPDF_PAGE) = nullptr;
            FPDF_TEXTPAGE (*TextLoadPage)(FPDF_PAGE) = nullptr;
            void (*TextClosePage)(FPDF_TEXTPAGE) = nullptr;
            int (*TextCountChars)(FPDF_TEXTPAGE) = nullptr;
            int (*TextGetText)(FPDF_TEXTPAGE, int, int, unsigned short*) = nullptr;
            bool ready = false;
        };

        // Les codes d'erreur de FPDF_GetLastError qu'on distingue (fpdfview.h).
        constexpr unsigned long k_err_password = 4;
        constexpr unsigned long k_err_format = 3;

        // Le nombre minimal de lettres et chiffres pour qu'une page compte comme lisible (minPageChars de la page).
        constexpr int k_min_page_chars = 10;

        std::mutex& pdfium_mutex()
        {
            static std::mutex m;   // PDFium n'est pas réentrant : une extraction à la fois.
            return m;
        }

        // Charge la bibliothèque PDFium et résout ses fonctions, une seule fois.  Rend nullptr si
        // elle est absente (le service tourne sans ; les PDF lèvent "pdf-reader").
        const pdfium_api* load_pdfium()
        {
            static pdfium_api api;
            static std::once_flag once;
            std::call_once(once, []() {
                // La dylib est à côté de l'exécutable (RPATH @loader_path), ou récupérée par
                // fetch_pdfium.sh dans third_party/pdfium/lib.  On essaie les deux noms.
                void* h = dlopen("libpdfium.dylib", RTLD_NOW | RTLD_LOCAL);
                if (!h) h = dlopen("@loader_path/libpdfium.dylib", RTLD_NOW | RTLD_LOCAL);
                if (!h) return;
                auto sym = [h](const char* n) { return dlsym(h, n); };
                api.InitLibraryWithConfig = reinterpret_cast<decltype(api.InitLibraryWithConfig)>(sym("FPDF_InitLibraryWithConfig"));
                api.LoadMemDocument = reinterpret_cast<decltype(api.LoadMemDocument)>(sym("FPDF_LoadMemDocument"));
                api.CloseDocument = reinterpret_cast<decltype(api.CloseDocument)>(sym("FPDF_CloseDocument"));
                api.GetLastError = reinterpret_cast<decltype(api.GetLastError)>(sym("FPDF_GetLastError"));
                api.GetPageCount = reinterpret_cast<decltype(api.GetPageCount)>(sym("FPDF_GetPageCount"));
                api.LoadPage = reinterpret_cast<decltype(api.LoadPage)>(sym("FPDF_LoadPage"));
                api.ClosePage = reinterpret_cast<decltype(api.ClosePage)>(sym("FPDF_ClosePage"));
                api.TextLoadPage = reinterpret_cast<decltype(api.TextLoadPage)>(sym("FPDFText_LoadPage"));
                api.TextClosePage = reinterpret_cast<decltype(api.TextClosePage)>(sym("FPDFText_ClosePage"));
                api.TextCountChars = reinterpret_cast<decltype(api.TextCountChars)>(sym("FPDFText_CountChars"));
                api.TextGetText = reinterpret_cast<decltype(api.TextGetText)>(sym("FPDFText_GetText"));
                api.ready = api.InitLibraryWithConfig && api.LoadMemDocument && api.CloseDocument
                    && api.GetPageCount && api.LoadPage && api.ClosePage && api.TextLoadPage
                    && api.TextClosePage && api.TextCountChars && api.TextGetText;
                if (api.ready)
                {
                    library_config cfg{};
                    cfg.version = 2;
                    api.InitLibraryWithConfig(&cfg);
                }
            });
            return api.ready ? &api : nullptr;
        }

        // Le buffer UTF-16LE de PDFium (terminé par un \0) en std::string UTF-8, par CoreFoundation.
        std::string utf16le_to_utf8(const unsigned short* u16, int count)
        {
            if (count <= 0) return std::string();
            CFStringRef s = CFStringCreateWithBytes(kCFAllocatorDefault,
                reinterpret_cast<const UInt8*>(u16), static_cast<CFIndex>(count) * 2,
                kCFStringEncodingUTF16LE, false);
            if (!s) return std::string();
            CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
            std::string out(static_cast<size_t>(max), '\0');
            CFIndex used = 0;
            CFStringGetBytes(s, CFRangeMake(0, CFStringGetLength(s)), kCFStringEncodingUTF8, 0, false,
                reinterpret_cast<UInt8*>(&out[0]), max, &used);
            CFRelease(s);
            out.resize(static_cast<size_t>(used));
            return out;
        }

        // Le nombre de lettres et de chiffres (points de code) d'un texte UTF-8, pour mesurer si une
        // page est lisible.  Approximation : on compte les octets qui ne sont ni espace ni ponctuation
        // ASCII et les débuts de séquence multioctet (toute lettre non ASCII compte).
        int letters_and_digits(const std::string& s)
        {
            int n = 0;
            for (unsigned char c : s)
            {
                if (c >= 0x80) { if ((c & 0xC0) != 0x80) ++n; }          // début d'un caractère non ASCII
                else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++n;
            }
            return n;
        }
    }

    bool pdf_available()
    {
        return load_pdfium() != nullptr;
    }

    std::string extract_pdf_text(const std::string& bytes)
    {
        const pdfium_api* api = load_pdfium();
        if (!api) throw doc_error("pdf-reader", "PDFium is not available");

        std::lock_guard<std::mutex> lock(pdfium_mutex());
        FPDF_DOCUMENT doc = api->LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
        if (!doc)
        {
            const unsigned long err = api->GetLastError ? api->GetLastError() : 0;
            if (err == k_err_password) throw doc_error("pdf-password", "the PDF is password-protected");
            if (err == k_err_format) throw doc_error("pdf-invalid", "the PDF is invalid");
            throw doc_error("pdf-invalid", "the PDF could not be opened");
        }
        const int pages = api->GetPageCount(doc);
        if (pages <= 0)
        {
            api->CloseDocument(doc);
            throw doc_error("pdf-invalid", "the PDF has no pages");
        }

        std::string text;
        int readable = 0;          // les pages lisibles (au moins k_min_page_chars caractères)
        long long total_chars = 0; // tous les caractères, pour distinguer un PDF vide d'un PDF scanné
        for (int i = 0; i < pages; ++i)
        {
            FPDF_PAGE page = api->LoadPage(doc, i);
            std::string s;
            if (page)
            {
                FPDF_TEXTPAGE tp = api->TextLoadPage(page);
                if (tp)
                {
                    const int count = api->TextCountChars(tp);
                    if (count > 0)
                    {
                        std::vector<unsigned short> buf(static_cast<size_t>(count) + 1, 0);
                        const int got = api->TextGetText(tp, 0, count, buf.data());
                        // got compte le \0 final ; on convertit got-1 caractères.
                        if (got > 1) s = utf16le_to_utf8(buf.data(), got - 1);
                    }
                    api->TextClosePage(tp);
                }
                api->ClosePage(page);
            }
            const int n = letters_and_digits(s);
            total_chars += n;
            if (n >= k_min_page_chars) ++readable;
            if (!text.empty()) text += "\n\n";
            text += "## Page " + std::to_string(i + 1) + "\n" + s;
        }
        api->CloseDocument(doc);

        // Aucune page lisible : le PDF est fait d'images (document numérisé).  La page le rendra en
        // images pour le modèle de vision, comme avant ; on le signale par "pdf-images".
        if (readable == 0 || total_chars == 0)
            throw doc_error("pdf-images", "the PDF has " + std::to_string(pages) + " pages of images");
        return text;
    }
}
