// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "image_input.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>

#include <memory>
#include <type_traits>

#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

namespace gulliver
{

    namespace
    {
        [[noreturn]] void refuse(const std::string& message)
        {
            throw std::invalid_argument(message);
        }

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        uint32_t read_be32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16
                | static_cast<uint32_t>(p[2]) << 8 | p[3];
        }

        uint16_t read_be16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] << 8 | p[1]);
        }

        // Le nom d'un format que le service ne lit pas, d'après sa signature.
        std::string other_format(const std::string& bytes)
        {
            auto starts = [&](const char* s, size_t at) {
                const size_t n = std::strlen(s);
                return bytes.size() >= at + n && bytes.compare(at, n, s) == 0;
            };
            if (starts("GIF8", 0)) return "GIF";
            if (starts("RIFF", 0) && starts("WEBP", 8)) return "WebP";
            if (starts("BM", 0)) return "BMP";
            if (starts("II*", 0) || starts("MM", 0)) return "TIFF";
            if (starts("ftyp", 4)) return "HEIF or AVIF";
            return "";
        }

        /*
            L'orientation d'un bloc EXIF (un en-tête TIFF et son premier répertoire), 1 quand
            le bloc n'en contient pas ou ne peut pas être lu.
        */
        int exif_orientation(const uint8_t* p, size_t n)
        {
            if (n < 8) return 1;
            const bool little = p[0] == 'I' && p[1] == 'I';
            if (!little && !(p[0] == 'M' && p[1] == 'M')) return 1;
            auto u16 = [&](size_t at) -> unsigned {
                return little ? static_cast<unsigned>(p[at] | p[at + 1] << 8)
                    : static_cast<unsigned>(p[at] << 8 | p[at + 1]);
            };
            auto u32 = [&](size_t at) -> uint32_t {
                return little ? static_cast<uint32_t>(p[at]) | static_cast<uint32_t>(p[at + 1]) << 8
                        | static_cast<uint32_t>(p[at + 2]) << 16 | static_cast<uint32_t>(p[at + 3]) << 24
                    : read_be32(p + at);
            };
            if (u16(2) != 42) return 1;
            const uint32_t ifd = u32(4);
            if (ifd > n - 2) return 1;
            const unsigned count = u16(ifd);
            for (unsigned i = 0; i < count; ++i)
            {
                const size_t e = ifd + 2 + static_cast<size_t>(i) * 12;
                if (e + 12 > n) break;
                if (u16(e) == 0x0112 && u16(e + 2) == 3)
                {
                    const unsigned v = u16(e + 8);
                    return v >= 1 && v <= 8 ? static_cast<int>(v) : 1;
                }
            }
            return 1;
        }

        bool swaps_axes(int orientation)
        {
            return orientation >= 5 && orientation <= 8;
        }

        void check_size(long width, long height, const image_limits& limits)
        {
            if (width <= 0 || height <= 0) refuse("the image has no pixels");
            if (width > limits.max_side || height > limits.max_side)
                refuse("the image measures " + std::to_string(width) + " x " + std::to_string(height)
                    + " pixels; a side holds " + std::to_string(limits.max_side) + " pixels at most");
            if (static_cast<double>(width) * height > static_cast<double>(limits.max_pixels))
                refuse("the image holds " + std::to_string(static_cast<long long>(width) * height)
                    + " pixels; an image holds " + std::to_string(limits.max_pixels / 1000000) + " megapixels at most");
        }

        image_header png_header(const std::string& bytes)
        {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
            const size_t n = bytes.size();
            if (n < 33 || std::memcmp(p + 12, "IHDR", 4) != 0) refuse("the PNG image is damaged (no header)");
            image_header h;
            h.format = image_format::png;
            h.width = static_cast<long>(std::min<uint32_t>(read_be32(p + 16), 0x7FFFFFFF));
            h.height = static_cast<long>(std::min<uint32_t>(read_be32(p + 20), 0x7FFFFFFF));
            // Un chunk eXIf avant les pixels donne l'orientation.
            size_t at = 8;
            while (at + 12 <= n)
            {
                const uint32_t len = read_be32(p + at);
                if (len > n - at - 12) break;
                if (std::memcmp(p + at + 4, "IDAT", 4) == 0 || std::memcmp(p + at + 4, "IEND", 4) == 0) break;
                if (std::memcmp(p + at + 4, "eXIf", 4) == 0)
                {
                    h.orientation = exif_orientation(p + at + 8, len);
                    break;
                }
                at += 12 + static_cast<size_t>(len);
            }
            return h;
        }

        image_header jpeg_header(const std::string& bytes)
        {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
            const size_t n = bytes.size();
            image_header h;
            h.format = image_format::jpeg;
            size_t at = 2;
            bool sized = false;
            while (at + 4 <= n)
            {
                if (p[at] != 0xFF) refuse("the JPEG image is damaged (invalid marker)");
                const uint8_t marker = p[at + 1];
                if (marker == 0xFF) { ++at; continue; }                   // octet de remplissage
                if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { at += 2; continue; }
                if (marker == 0xD9 || marker == 0xDA) break;               // fin d'image, début de scan
                const size_t len = read_be16(p + at + 2);
                if (len < 2 || at + 2 + len > n) refuse("the JPEG image is damaged (truncated header)");
                const uint8_t* seg = p + at + 4;
                const size_t seg_len = len - 2;
                if (marker == 0xE1 && seg_len > 6 && std::memcmp(seg, "Exif\0\0", 6) == 0)
                    h.orientation = exif_orientation(seg + 6, seg_len - 6);
                const bool sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
                if (sof && !sized)
                {
                    if (seg_len < 5) refuse("the JPEG image is damaged (frame header)");
                    h.height = read_be16(seg + 1);
                    h.width = read_be16(seg + 3);
                    sized = true;
                }
                at += 2 + len;
            }
            if (!sized) refuse("the JPEG image is damaged (no frame header)");
            return h;
        }

        // Pixels transparents composés sur du blanc, avec l'arithmétique entière de
        // l'alpha_composite de Pillow, qu'applique le prétraitement de référence.
        void composite_on_white(const uint8_t* rgba, uint8_t* rgb, size_t pixels)
        {
            for (size_t i = 0; i < pixels; ++i)
            {
                const uint32_t a = rgba[4 * i + 3];
                for (int c = 0; c < 3; ++c)
                {
                    if (a == 255) { rgb[3 * i + c] = rgba[4 * i + c]; continue; }
                    // coef1 = a << 7, coef2 = (255 - a) << 7, division arrondie par 255.
                    const uint32_t t = rgba[4 * i + c] * (a << 7) + 255u * ((255u - a) << 7) + (0x80u << 7);
                    rgb[3 * i + c] = static_cast<uint8_t>((((t >> 8) + t) >> 8) >> 7);
                }
            }
        }

        /*
            Le décodage, par ImageIO (fork pour macOS : au lieu des copies de libpng et de libjpeg).
            Les pixels sont lus tels que le fichier les stocke, sans gestion des couleurs, comme
            le fait Pillow, que suit le prétraitement de référence : RGB et niveaux de gris sur 8
            ou 16 bits (16 bits : l'octet de poids fort, comme png_set_strip_16), palette,
            transparence posée sur du blanc par composite_on_white.  ImageIO n'applique pas
            l'orientation EXIF, que decode_image applique ensuite.  Un fichier tronqué ou
            incomplet est refusé.  Une disposition que cette lecture ne connaît pas (CMJN,
            transparence prémultipliée, flottants) passe par un rendu dans un contexte sRGB.
        */
        struct cf_release
        {
            void operator()(const void* p) const { if (p) CFRelease(p); }
        };
        template <typename T>
        using cf_ptr = std::unique_ptr<std::remove_pointer_t<T>, cf_release>;

        // Rendu dans un contexte sRGB sur 8 bits (transparence prémultipliée, que l'on retire).
        rgb_image render_srgb(CGImageRef img, long w, long h)
        {
            cf_ptr<CGColorSpaceRef> cs(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
            std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
            cf_ptr<CGContextRef> ctx(CGBitmapContextCreate(rgba.data(), static_cast<size_t>(w), static_cast<size_t>(h), 8,
                static_cast<size_t>(w) * 4, cs.get(), kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
            if (!ctx) throw std::bad_alloc();
            CGContextSetBlendMode(ctx.get(), kCGBlendModeCopy);
            CGContextDrawImage(ctx.get(), CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)), img);
            const size_t n = static_cast<size_t>(w) * h;
            for (size_t i = 0; i < n; ++i)
            {
                uint8_t* px = rgba.data() + 4 * i;
                const unsigned a = px[3];
                if (a == 0 || a == 255) continue;
                for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(std::min(255u, (px[c] * 255u + a / 2) / a));
            }
            rgb_image out;
            out.width = w;
            out.height = h;
            out.pixels.resize(n * 3);
            composite_on_white(rgba.data(), out.pixels.data(), n);
            return out;
        }

        rgb_image decode_imageio(const std::string& bytes, const image_limits& limits, const char* format, long w, long h)
        {
            const std::string what = std::string("the ") + format + " image cannot be read";
            cf_ptr<CFDataRef> data(CFDataCreateWithBytesNoCopy(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                static_cast<CFIndex>(bytes.size()), kCFAllocatorNull));
            if (!data) throw std::bad_alloc();
            const void* keys[] = { kCGImageSourceShouldCache, kCGImageSourceShouldAllowFloat };
            const void* values[] = { kCFBooleanFalse, kCFBooleanFalse };
            cf_ptr<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks));
            cf_ptr<CGImageSourceRef> source(CGImageSourceCreateWithData(data.get(), options.get()));
            if (!source || CGImageSourceGetStatus(source.get()) != kCGImageStatusComplete
                || CGImageSourceGetCount(source.get()) < 1
                || CGImageSourceGetStatusAtIndex(source.get(), 0) != kCGImageStatusComplete)
                refuse(what + " (damaged or truncated file)");
            cf_ptr<CGImageRef> img(CGImageSourceCreateImageAtIndex(source.get(), 0, options.get()));
            if (!img) refuse(what);
            // La taille décodée doit être celle que l'en-tête a annoncée et que check_size a bornée.
            if (static_cast<long>(CGImageGetWidth(img.get())) != w || static_cast<long>(CGImageGetHeight(img.get())) != h)
                refuse(what + " (its size differs from its header)");

            const size_t bpc = CGImageGetBitsPerComponent(img.get());
            const size_t bpp = CGImageGetBitsPerPixel(img.get());
            const size_t stride = CGImageGetBytesPerRow(img.get());
            const CGBitmapInfo info = CGImageGetBitmapInfo(img.get());
            const CGImageAlphaInfo alpha = CGImageGetAlphaInfo(img.get());
            CGColorSpaceRef cs = CGImageGetColorSpace(img.get());
            const CGColorSpaceModel model = cs ? CGColorSpaceGetModel(cs) : kCGColorSpaceModelUnknown;
            const uint32_t order = info & kCGBitmapByteOrderMask;
            const bool floats = (info & kCGBitmapFloatComponents) != 0;
            const bool premultiplied = alpha == kCGImageAlphaPremultipliedFirst || alpha == kCGImageAlphaPremultipliedLast;
            const bool has_alpha = alpha == kCGImageAlphaFirst || alpha == kCGImageAlphaLast || premultiplied;
            const bool skips = alpha == kCGImageAlphaNoneSkipFirst || alpha == kCGImageAlphaNoneSkipLast;
            const bool alpha_first = alpha == kCGImageAlphaFirst || alpha == kCGImageAlphaPremultipliedFirst
                || alpha == kCGImageAlphaNoneSkipFirst;
            const size_t colors = model == kCGColorSpaceModelRGB ? 3 : model == kCGColorSpaceModelMonochrome ? 1
                : model == kCGColorSpaceModelIndexed ? 1 : 0;
            const size_t comps = colors + (has_alpha || skips ? 1 : 0);
            const bool readable = colors != 0 && !floats && !premultiplied && (bpc == 8 || bpc == 16)
                && bpp == comps * bpc && !(model == kCGColorSpaceModelIndexed && (bpc != 8 || has_alpha || skips))
                && (order == kCGBitmapByteOrderDefault || order == kCGBitmapByteOrder32Big
                    || order == kCGBitmapByteOrder16Big || (bpc == 16 && order == kCGBitmapByteOrder16Little)
                    || (bpc == 8 && comps == 4 && order == kCGBitmapByteOrder32Little));
            if (!readable) return render_srgb(img.get(), w, h);

            cf_ptr<CFDataRef> raw(CGDataProviderCopyData(CGImageGetDataProvider(img.get())));
            if (!raw || static_cast<size_t>(CFDataGetLength(raw.get())) < stride * static_cast<size_t>(h - 1)
                    + static_cast<size_t>(w) * bpp / 8)
                refuse(what);
            const uint8_t* base = CFDataGetBytePtr(raw.get());

            // La table des couleurs d'une palette, en RGB.
            std::vector<uint8_t> table;
            if (model == kCGColorSpaceModelIndexed)
            {
                CGColorSpaceRef under = CGColorSpaceGetBaseColorSpace(cs);
                if (!under || CGColorSpaceGetModel(under) != kCGColorSpaceModelRGB) return render_srgb(img.get(), w, h);
                table.resize(CGColorSpaceGetColorTableCount(cs) * 3);
                CGColorSpaceGetColorTable(cs, table.data());
            }

            const size_t n = static_cast<size_t>(w) * h;
            std::vector<uint8_t> rgba(n * 4);
            const bool swap32 = bpc == 8 && order == kCGBitmapByteOrder32Little;
            for (long y = 0; y < h; ++y)
            {
                const uint8_t* row = base + static_cast<size_t>(y) * stride;
                for (long x = 0; x < w; ++x)
                {
                    uint8_t v[4];
                    for (size_t c = 0; c < comps; ++c)
                    {
                        const size_t k = swap32 ? comps - 1 - c : c;
                        const uint8_t* q = row + (static_cast<size_t>(x) * comps + k) * (bpc / 8);
                        // 16 bits : l'octet de poids fort.
                        v[c] = bpc == 16 ? (order == kCGBitmapByteOrder16Little ? q[1] : q[0]) : q[0];
                    }
                    const uint8_t* colour = v + (alpha_first && comps > colors ? 1 : 0);
                    const uint8_t a = has_alpha ? (alpha_first ? v[0] : v[colors]) : 255;
                    uint8_t* out = rgba.data() + 4 * (static_cast<size_t>(y) * w + x);
                    if (model == kCGColorSpaceModelIndexed)
                    {
                        const size_t i = static_cast<size_t>(colour[0]) * 3;
                        if (i + 2 >= table.size()) refuse(what + " (palette index out of range)");
                        out[0] = table[i]; out[1] = table[i + 1]; out[2] = table[i + 2];
                    }
                    else if (colors == 1)
                    {
                        out[0] = out[1] = out[2] = colour[0];
                    }
                    else
                    {
                        out[0] = colour[0]; out[1] = colour[1]; out[2] = colour[2];
                    }
                    out[3] = a;
                }
            }
            rgb_image result;
            result.width = w;
            result.height = h;
            result.pixels.resize(n * 3);
            composite_on_white(rgba.data(), result.pixels.data(), n);
            return result;
        }

        // L'image telle qu'affichée pour une orientation EXIF, comme la produit exif_transpose de Pillow.
        rgb_image orient(const rgb_image& src, int orientation)
        {
            if (orientation <= 1 || orientation > 8) return src;
            const long W = src.width, H = src.height;
            rgb_image out;
            out.width = swaps_axes(orientation) ? H : W;
            out.height = swaps_axes(orientation) ? W : H;
            out.pixels.resize(src.pixels.size());
            for (long y = 0; y < out.height; ++y)
                for (long x = 0; x < out.width; ++x)
                {
                    long sx = x, sy = y;
                    switch (orientation)
                    {
                    case 2: sx = W - 1 - x; break;                              // en miroir
                    case 3: sx = W - 1 - x; sy = H - 1 - y; break;              // tournée de 180
                    case 4: sy = H - 1 - y; break;                              // retournée
                    case 5: sx = y; sy = x; break;                              // transposée
                    case 6: sx = y; sy = H - 1 - x; break;                      // tournée de 90 en sens horaire
                    case 7: sx = W - 1 - y; sy = H - 1 - x; break;              // transversée
                    default: sx = W - 1 - y; sy = x; break;                     // tournée de 90 en sens antihoraire
                    }
                    const uint8_t* s = src.pixels.data() + 3 * (static_cast<size_t>(sy) * W + sx);
                    uint8_t* d = out.pixels.data() + 3 * (static_cast<size_t>(y) * out.width + x);
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
            return out;
        }

        // Le rééchantillonnage de Pillow : les coefficients d'un axe.
        const int resample_bits = 22;

        double bicubic_filter(double x)
        {
            const double a = -0.5;
            if (x < 0.0) x = -x;
            if (x < 1.0) return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
            if (x < 2.0) return (((x - 5.0) * x + 8.0) * x - 4.0) * a;
            return 0.0;
        }

        double bilinear_filter(double x)
        {
            if (x < 0.0) x = -x;
            return x < 1.0 ? 1.0 - x : 0.0;
        }

        long resample_coefficients(long in_size, long out_size, std::vector<long>& bounds,
            std::vector<int32_t>& weights, bool bilinear)
        {
            const double scale = static_cast<double>(in_size) / static_cast<double>(out_size);
            const double filterscale = std::max(1.0, scale);
            const double support = (bilinear ? 1.0 : 2.0) * filterscale;
            const long ksize = static_cast<long>(std::ceil(support)) * 2 + 1;
            std::vector<double> k(static_cast<size_t>(ksize));
            bounds.resize(static_cast<size_t>(out_size) * 2);
            weights.assign(static_cast<size_t>(out_size * ksize), 0);
            for (long xx = 0; xx < out_size; ++xx)
            {
                const double center = (static_cast<double>(xx) + 0.5) * scale;
                const double ss = 1.0 / filterscale;
                long xmin = static_cast<long>(center - support + 0.5);
                if (xmin < 0) xmin = 0;
                long xmax = static_cast<long>(center + support + 0.5);
                if (xmax > in_size) xmax = in_size;
                xmax -= xmin;
                double ww = 0.0;
                for (long x = 0; x < xmax; ++x)
                {
                    const double t = (static_cast<double>(x + xmin) - center + 0.5) * ss;
                    const double w = bilinear ? bilinear_filter(t) : bicubic_filter(t);
                    k[static_cast<size_t>(x)] = w;
                    ww += w;
                }
                for (long x = 0; x < xmax; ++x)
                {
                    const double w = ww != 0.0 ? k[static_cast<size_t>(x)] / ww : k[static_cast<size_t>(x)];
                    // Pillow ajoute ou retranche une moitié, puis tronque vers zéro.
                    weights[static_cast<size_t>(xx * ksize + x)] = static_cast<int32_t>(
                        w < 0.0 ? -0.5 + w * (1 << resample_bits) : 0.5 + w * (1 << resample_bits));
                }
                bounds[static_cast<size_t>(xx * 2)] = xmin;
                bounds[static_cast<size_t>(xx * 2 + 1)] = xmax;
            }
            return ksize;
        }

        inline uint8_t clip8(int32_t v)
        {
            v >>= resample_bits;
            return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }

    std::string decode_image_data_url(const std::string& url, size_t max_bytes)
    {
        size_t start = 0;
        while (start < url.size() && std::isspace(static_cast<unsigned char>(url[start]))) ++start;
        const std::string head = lower(url.substr(start, 16));
        if (head.compare(0, 5, "data:") != 0)
        {
            if (head.compare(0, 7, "http://") == 0 || head.compare(0, 8, "https://") == 0
                || head.compare(0, 5, "file:") == 0)
                refuse("images are given as data URLs (data:image/png;base64,...); the service reads nothing from "
                    "the network or the disk for a request, so http, https and file addresses are refused");
            refuse("an image is given as a data URL (data:image/png;base64,... or data:image/jpeg;base64,...)");
        }
        const size_t comma = url.find(',', start);
        if (comma == std::string::npos) refuse("the data URL of an image holds no data");
        const std::string meta = lower(url.substr(start + 5, comma - start - 5));
        const size_t semi = meta.find(';');
        const std::string type = meta.substr(0, semi);
        if (type.compare(0, 6, "image/") != 0) refuse("the data URL does not hold an image (" + type + ")");
        if (meta.size() < 7 || meta.compare(meta.size() - 7, 7, ";base64") != 0)
            refuse("the data URL of an image must be encoded in base64");
        // La taille décodée est vérifiée avant tout décodage : trois octets pour quatre caractères.
        const size_t text = url.size() - comma - 1;
        if (text / 4 * 3 > max_bytes + 3)
            refuse("the image takes more than " + std::to_string(max_bytes >> 20) + " MB");
        std::string out;
        out.reserve(text / 4 * 3 + 3);
        uint32_t value = 0;
        int bits = 0;
        bool padding = false;
        for (size_t i = comma + 1; i < url.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(url[i]);
            int d;
            if (c >= 'A' && c <= 'Z') d = c - 'A';
            else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
            else if (c >= '0' && c <= '9') d = c - '0' + 52;
            else if (c == '+' || c == '-') d = 62;
            else if (c == '/' || c == '_') d = 63;
            else if (c == '=') { padding = true; continue; }
            else if (std::isspace(c)) continue;
            else refuse("the data URL of an image holds characters outside base64");
            if (padding) refuse("the data URL of an image holds characters after its padding");
            value = (value << 6) | static_cast<uint32_t>(d);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out += static_cast<char>((value >> bits) & 0xFF);
            }
        }
        if (out.size() > max_bytes) refuse("the image takes more than " + std::to_string(max_bytes >> 20) + " MB");
        if (out.empty()) refuse("the data URL of an image holds no data");
        return out;
    }

    image_header read_image_header(const std::string& bytes, const image_limits& limits)
    {
        if (bytes.size() > limits.max_bytes)
            refuse("the image takes more than " + std::to_string(limits.max_bytes >> 20) + " MB");
        static const unsigned char png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        image_header h;
        if (bytes.size() >= 8 && std::memcmp(bytes.data(), png_sig, 8) == 0)
        {
            h = png_header(bytes);
        }
        else if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF
            && static_cast<unsigned char>(bytes[1]) == 0xD8 && static_cast<unsigned char>(bytes[2]) == 0xFF)
        {
            h = jpeg_header(bytes);
        }
        else
        {
            const std::string other = other_format(bytes);
            refuse(other.empty() ? std::string("the image is neither a PNG nor a JPEG file")
                : "the image is a " + other + " file; the service reads PNG and JPEG images");
        }
        check_size(h.width, h.height, limits);
        if (swaps_axes(h.orientation)) std::swap(h.width, h.height);
        return h;
    }

    rgb_image decode_image(const std::string& bytes, const image_limits& limits, long fit_width, long fit_height)
    {
        const image_header h = read_image_header(bytes, limits);
        // L'en-tête donne la taille stockée (avant l'orientation), que le décodage doit retrouver.
        const bool swapped = swaps_axes(h.orientation);
        rgb_image img = decode_imageio(bytes, limits, h.format == image_format::png ? "PNG" : "JPEG",
            swapped ? h.height : h.width, swapped ? h.width : h.height);
        // L'image stockée est redimensionnée selon les axes stockés de la cible, puis orientée.
        if (fit_width > 0 && fit_height > 0)
        {
            const bool swap = swaps_axes(h.orientation);
            rgb_image small = resize_bicubic(img, swap ? fit_height : fit_width, swap ? fit_width : fit_height);
            img.clear();
            img = std::move(small);
        }
        if (h.orientation > 1) img = orient(img, h.orientation);
        return img;
    }

    void fit_image_size(long width, long height, long factor, long min_pixels, long max_pixels, long& out_width,
        long& out_height)
    {
        if (width <= 0 || height <= 0 || factor <= 0) refuse("the image has no pixels");
        if (static_cast<double>(std::max(width, height)) / static_cast<double>(std::min(width, height)) > 200.0)
            refuse("the sides of the image differ by a ratio over 200");
        const double f = static_cast<double>(factor);
        const double h = static_cast<double>(height), w = static_cast<double>(width);
        // Le round() de Python, moitiés au pair, dans le mode d'arrondi par défaut.
        long h_bar = static_cast<long>(std::nearbyint(h / f)) * factor;
        long w_bar = static_cast<long>(std::nearbyint(w / f)) * factor;
        if (static_cast<double>(h_bar) * w_bar > static_cast<double>(max_pixels))
        {
            const double beta = std::sqrt(h * w / static_cast<double>(max_pixels));
            h_bar = std::max(factor, static_cast<long>(std::floor(h / beta / f)) * factor);
            w_bar = std::max(factor, static_cast<long>(std::floor(w / beta / f)) * factor);
        }
        else if (static_cast<double>(h_bar) * w_bar < static_cast<double>(min_pixels))
        {
            const double beta = std::sqrt(static_cast<double>(min_pixels) / (h * w));
            h_bar = static_cast<long>(std::ceil(h * beta / f)) * factor;
            w_bar = static_cast<long>(std::ceil(w * beta / f)) * factor;
        }
        out_width = w_bar;
        out_height = h_bar;
    }

    rgb_image resize_bicubic(const rgb_image& src, long width, long height)
    {
        return resize_pillow(src, width, height, false);
    }

    rgb_image resize_bilinear(const rgb_image& src, long width, long height)
    {
        return resize_pillow(src, width, height, true);
    }

    rgb_image resize_pillow(const rgb_image& src, long width, long height, bool bilinear)
    {
        if (width <= 0 || height <= 0 || src.width <= 0 || src.height <= 0)
            throw std::invalid_argument("resize_pillow: empty image");
        if (width == src.width && height == src.height) return src;
        const int32_t round = 1 << (resample_bits - 1);

        // Passe horizontale, dans une image de la largeur cible et de la hauteur source.
        rgb_image mid;
        const rgb_image* in = &src;
        if (width != src.width)
        {
            std::vector<long> bounds;
            std::vector<int32_t> k;
            const long ksize = resample_coefficients(src.width, width, bounds, k, bilinear);
            mid.width = width;
            mid.height = src.height;
            mid.pixels.resize(static_cast<size_t>(width) * src.height * 3);
            for (long y = 0; y < src.height; ++y)
            {
                const uint8_t* row = src.pixels.data() + static_cast<size_t>(y) * src.width * 3;
                uint8_t* out = mid.pixels.data() + static_cast<size_t>(y) * width * 3;
                for (long x = 0; x < width; ++x)
                {
                    const long xmin = bounds[static_cast<size_t>(2 * x)];
                    const long xcnt = bounds[static_cast<size_t>(2 * x + 1)];
                    const int32_t* w = k.data() + x * ksize;
                    int32_t s0 = round, s1 = round, s2 = round;
                    const uint8_t* p = row + xmin * 3;
                    for (long i = 0; i < xcnt; ++i, p += 3)
                    {
                        s0 += p[0] * w[i];
                        s1 += p[1] * w[i];
                        s2 += p[2] * w[i];
                    }
                    out[3 * x] = clip8(s0);
                    out[3 * x + 1] = clip8(s1);
                    out[3 * x + 2] = clip8(s2);
                }
            }
            in = &mid;
        }
        if (height == in->height) return in == &mid ? mid : *in;

        // Passe verticale, une ligne entière à la fois.
        std::vector<long> bounds;
        std::vector<int32_t> k;
        const long ksize = resample_coefficients(in->height, height, bounds, k, bilinear);
        rgb_image out;
        out.width = width;
        out.height = height;
        const size_t row_values = static_cast<size_t>(width) * 3;
        out.pixels.resize(row_values * static_cast<size_t>(height));
        std::vector<int32_t> acc(row_values);
        for (long y = 0; y < height; ++y)
        {
            const long ymin = bounds[static_cast<size_t>(2 * y)];
            const long ycnt = bounds[static_cast<size_t>(2 * y + 1)];
            const int32_t* w = k.data() + y * ksize;
            std::fill(acc.begin(), acc.end(), round);
            for (long i = 0; i < ycnt; ++i)
            {
                const uint8_t* row = in->pixels.data() + static_cast<size_t>(ymin + i) * row_values;
                const int32_t wi = w[i];
                for (size_t e = 0; e < row_values; ++e) acc[e] += row[e] * wi;
            }
            uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * row_values;
            for (size_t e = 0; e < row_values; ++e) dst[e] = clip8(acc[e]);
        }
        return out;
    }

    namespace
    {
        // CC_SHA256_Update prend une longueur sur 32 bits : les grands blocs passent par morceaux.
        void sha256_update(CC_SHA256_CTX& ctx, const uint8_t* data, size_t n)
        {
            while (n > 0)
            {
                const CC_LONG part = static_cast<CC_LONG>(std::min<size_t>(n, size_t(1) << 30));
                CC_SHA256_Update(&ctx, data, part);
                data += part;
                n -= part;
            }
        }

        std::array<uint8_t, 32> sha256_final(CC_SHA256_CTX& ctx)
        {
            std::array<uint8_t, 32> h{};
            CC_SHA256_Final(h.data(), &ctx);
            return h;
        }
    }

    // L'empreinte passe par CommonCrypto, qui se sert des instructions SHA de la puce Apple (fil rouge,
    // morceau 1 : Gulliver ne prend plus le SHA-256 de dlib).
    std::array<uint8_t, 32> sha256(const uint8_t* data, size_t n)
    {
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        sha256_update(ctx, data, n);
        return sha256_final(ctx);
    }

    std::array<uint8_t, 32> image_digest(const rgb_image& image)
    {
        // La taille d'abord, pour que deux images aux mêmes pixels sous d'autres formes diffèrent.
        uint8_t size[16];
        for (int b = 0; b < 8; ++b)
        {
            size[b] = static_cast<uint8_t>(static_cast<uint64_t>(image.width) >> (8 * b));
            size[8 + b] = static_cast<uint8_t>(static_cast<uint64_t>(image.height) >> (8 * b));
        }
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        sha256_update(ctx, size, sizeof(size));
        if (!image.pixels.empty()) sha256_update(ctx, image.pixels.data(), image.pixels.size());
        return sha256_final(ctx);
    }

}
