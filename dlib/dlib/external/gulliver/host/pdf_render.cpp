// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

// Le dessin des pages d'un PDF par CoreGraphics (voir pdf_render.h).

#include "pdf_render.h"
#include "doc_extract.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace gulliver
{
    namespace
    {
        // Libère un objet de CoreFoundation en sortant de portée.
        template <typename T>
        struct cf_ref
        {
            T ref;
            explicit cf_ref(T r) : ref(r) {}
            ~cf_ref() { if (ref) CFRelease(ref); }
            cf_ref(const cf_ref&) = delete;
            cf_ref& operator=(const cf_ref&) = delete;
        };

        // L'image JPEG d'un bitmap (qualité 0,9, comme la page encodait ses images).
        std::string encode_jpeg(CGImageRef image)
        {
            cf_ref<CFMutableDataRef> data(CFDataCreateMutable(kCFAllocatorDefault, 0));
            cf_ref<CGImageDestinationRef> dest(CGImageDestinationCreateWithData(data.ref, CFSTR("public.jpeg"), 1, nullptr));
            if (!dest.ref) throw doc_error("pdf-render", "JPEG encoder unavailable");
            const double quality = 0.9;
            cf_ref<CFNumberRef> q(CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &quality));
            const void* keys[] = { kCGImageDestinationLossyCompressionQuality };
            const void* values[] = { q.ref };
            cf_ref<CFDictionaryRef> options(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
            CGImageDestinationAddImage(dest.ref, image, options.ref);
            if (!CGImageDestinationFinalize(dest.ref)) throw doc_error("pdf-render", "JPEG encoding failed");
            return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data.ref)),
                static_cast<size_t>(CFDataGetLength(data.ref)));
        }
    }

    std::vector<rendered_page> render_pdf_pages(const std::string& bytes, int side, int limit, int& total)
    {
        total = 0;
        side = std::min(std::max(side, 64), 4096);
        if (bytes.size() < 5 || bytes.compare(0, 5, "%PDF-") != 0) throw doc_error("pdf-invalid", "not a PDF file");
        // Les octets ne sont pas copiés : la source les lit dans la chaîne, qui vit jusqu'au retour.
        cf_ref<CGDataProviderRef> provider(CGDataProviderCreateWithData(nullptr, bytes.data(), bytes.size(), nullptr));
        cf_ref<CGPDFDocumentRef> doc(provider.ref ? CGPDFDocumentCreateWithProvider(provider.ref) : nullptr);
        if (!doc.ref) throw doc_error("pdf-invalid", "unreadable PDF");
        // Un PDF chiffré est refusé, qu'il demande un mot de passe à l'ouverture ou qu'il
        // interdise seulement la copie ou l'impression.  Décision de l'humain (2026-11-26) :
        // Gulliver ne lit pas les PDF protégés (ni leur texte ni leurs pages dessinées).
        if (CGPDFDocumentIsEncrypted(doc.ref))
            throw doc_error("pdf-password", "the PDF is protected by a password");
        total = static_cast<int>(CGPDFDocumentGetNumberOfPages(doc.ref));
        if (total <= 0) throw doc_error("pdf-invalid", "the PDF has no pages");

        cf_ref<CGColorSpaceRef> rgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
        std::vector<rendered_page> out;
        const int count = std::min(total, std::max(0, limit));
        for (int i = 1; i <= count; ++i)
        {
            CGPDFPageRef page = CGPDFDocumentGetPage(doc.ref, static_cast<size_t>(i));  // emprunté au document
            if (!page) throw doc_error("pdf-render", "page " + std::to_string(i) + " is missing");
            // La boîte de recadrage, tournée comme la page s'affiche.
            const CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
            const int rotation = ((CGPDFPageGetRotationAngle(page) % 360) + 360) % 360;
            const bool turned = rotation == 90 || rotation == 270;
            const double w = turned ? box.size.height : box.size.width;
            const double h = turned ? box.size.width : box.size.height;
            if (!(w > 0 && h > 0)) throw doc_error("pdf-render", "page " + std::to_string(i) + " has no size");
            const double scale = side / std::max(w, h);
            const int pw = std::max(1, static_cast<int>(std::lround(w * scale)));
            const int ph = std::max(1, static_cast<int>(std::lround(h * scale)));

            cf_ref<CGContextRef> ctx(CGBitmapContextCreate(nullptr, pw, ph, 8, 0, rgb.ref,
                kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big));
            if (!ctx.ref) throw doc_error("pdf-render", "no bitmap for page " + std::to_string(i));
            // Fond blanc, puis la page, mise à l'échelle et tournée dans le bitmap entier.
            CGContextSetRGBFillColor(ctx.ref, 1, 1, 1, 1);
            CGContextFillRect(ctx.ref, CGRectMake(0, 0, pw, ph));
            CGContextSetInterpolationQuality(ctx.ref, kCGInterpolationHigh);
            CGContextSetShouldAntialias(ctx.ref, true);
            CGContextSetShouldSmoothFonts(ctx.ref, true);
            const CGAffineTransform fit = CGPDFPageGetDrawingTransform(page, kCGPDFCropBox,
                CGRectMake(0, 0, w, h), 0, true);
            CGContextScaleCTM(ctx.ref, scale, scale);
            CGContextConcatCTM(ctx.ref, fit);
            CGContextClipToRect(ctx.ref, box);
            CGContextDrawPDFPage(ctx.ref, page);

            cf_ref<CGImageRef> image(CGBitmapContextCreateImage(ctx.ref));
            if (!image.ref) throw doc_error("pdf-render", "page " + std::to_string(i) + " was not drawn");
            out.push_back({ encode_jpeg(image.ref), pw, ph });
        }
        return out;
    }
}
