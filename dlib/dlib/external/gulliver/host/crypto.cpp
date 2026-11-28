// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "crypto.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>

#include "../common/platform.h"

namespace gulliver
{

    // L'empreinte SHA-256 passe par CommonCrypto, qui se sert des instructions SHA de la puce Apple :
    // 2,1 Go/s contre 0,29 pour mbedtls en logiciel sur le M2 (même empreinte au bit près).
    struct sha256_stream::impl
    {
        CC_SHA256_CTX ctx;
    };

    sha256_stream::sha256_stream() : p_(new impl())
    {
        CC_SHA256_Init(&p_->ctx);
    }

    sha256_stream::~sha256_stream()
    {
    }

    void sha256_stream::update(const void* data, size_t n)
    {
        // CC_SHA256_Update prend une longueur sur 32 bits : les grands blocs passent par morceaux.
        const unsigned char* p = static_cast<const unsigned char*>(data);
        while (n > 0)
        {
            const CC_LONG part = static_cast<CC_LONG>(std::min<size_t>(n, 1u << 30));
            CC_SHA256_Update(&p_->ctx, p, part);
            p += part;
            n -= part;
        }
    }

    std::string sha256_stream::hex()
    {
        unsigned char h[32];
        CC_SHA256_Final(h, &p_->ctx);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (unsigned char b : h)
        {
            out += digits[b >> 4];
            out += digits[b & 15];
        }
        return out;
    }

    std::string sha256_hex(const std::string& data)
    {
        sha256_stream s;
        s.update(data.data(), data.size());
        return s.hex();
    }

    std::string sha256_file(const std::string& path, const std::function<void(uint64_t)>& progress,
        const std::atomic<bool>* cancel)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::string();
        sha256_stream s;
        std::vector<char> buf(1 << 20);
        uint64_t total = 0;
        while (in)
        {
            if (cancel && cancel->load()) return std::string();
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if (n <= 0) break;
            s.update(buf.data(), static_cast<size_t>(n));
            total += static_cast<uint64_t>(n);
            if (progress) progress(total);
        }
        return s.hex();
    }

    std::string sha256_open_file(link_free_file& f, const std::atomic<bool>* cancel)
    {
        if (!f.seek(0)) return std::string();
        sha256_stream s;
        std::vector<char> buf(1 << 20);
        uint64_t done = 0;
        while (done < f.size())
        {
            if (cancel && cancel->load()) return std::string();
            const long long n = f.read(buf.data(),
                static_cast<size_t>(std::min<uint64_t>(buf.size(), f.size() - done)));
            if (n <= 0) return std::string();
            s.update(buf.data(), static_cast<size_t>(n));
            done += static_cast<uint64_t>(n);
        }
        return s.hex();
    }

    std::string copy_open_file(link_free_file& from, const std::string& to, const std::string& expected,
        const std::atomic<bool>* cancel)
    {
        // Sur le même volume APFS, un clone remplace la copie : instantané, et sans place prise tant
        // qu'aucun des deux fichiers ne change.  Le clone est relu et haché comme une copie.
        if (from.clone_to(to))
        {
            link_free_file cloned;
            std::string why;
            const bool opened = cloned.open(to, parent_path(to), why) && cloned.links() == 1
                && cloned.size() == from.size();
            const std::string sha = opened ? sha256_open_file(cloned, cancel) : std::string();
            cloned.close();
            if (sha.empty() || (!expected.empty() && sha != expected))
            {
                remove_file(to);
                return std::string();
            }
            return sha;
        }
        new_file out;
        if (!from.seek(0) || !out.create(to)) return std::string();
        sha256_stream s;
        std::vector<char> buf(1 << 20);
        uint64_t done = 0;
        bool ok = true;
        while (ok && done < from.size())
        {
            const long long n = from.read(buf.data(),
                static_cast<size_t>(std::min<uint64_t>(buf.size(), from.size() - done)));
            ok = n > 0 && !(cancel && cancel->load()) && out.write(buf.data(), static_cast<size_t>(n));
            if (!ok) break;
            s.update(buf.data(), static_cast<size_t>(n));
            done += static_cast<uint64_t>(n);
        }
        ok = out.close() && ok;
        const std::string sha = ok ? s.hex() : std::string();
        if (sha.empty() || (!expected.empty() && sha != expected))
        {
            remove_file(to);
            return std::string();
        }
        return sha;
    }

    /*
        Base64 (RFC 4648), sans Mbed TLS (fork, 2026-11-20).  Le décodage est strict, comme celui
        qu'il remplace : les blancs sont ignorés, mais un caractère hors de l'alphabet, un bourrage
        mal placé ou une longueur qui n'est pas un multiple de 4 rendent une chaîne vide.
    */
    std::string base64_encode(const std::string& data)
    {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3)
        {
            const uint32_t v = static_cast<uint32_t>(p[i]) << 16 | static_cast<uint32_t>(p[i + 1]) << 8 | p[i + 2];
            out += alphabet[v >> 18 & 63];
            out += alphabet[v >> 12 & 63];
            out += alphabet[v >> 6 & 63];
            out += alphabet[v & 63];
        }
        if (i < data.size())
        {
            const uint32_t v = static_cast<uint32_t>(p[i]) << 16 | (i + 1 < data.size() ? static_cast<uint32_t>(p[i + 1]) << 8 : 0u);
            out += alphabet[v >> 18 & 63];
            out += alphabet[v >> 12 & 63];
            out += i + 1 < data.size() ? alphabet[v >> 6 & 63] : '=';
            out += '=';
        }
        return out;
    }

    std::string base64_decode(const std::string& text)
    {
        std::string clean;
        clean.reserve(text.size());
        for (char c : text)
            if (c != '\n' && c != '\r' && c != ' ' && c != '\t') clean += c;
        if (clean.size() % 4 != 0) return std::string();
        auto value = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        std::string out;
        out.reserve(clean.size() / 4 * 3);
        for (size_t i = 0; i < clean.size(); i += 4)
        {
            const bool last = i + 4 == clean.size();
            const int pad = (clean[i + 3] == '=' ? 1 : 0) + (clean[i + 2] == '=' ? 1 : 0);
            if (pad > 0 && !last) return std::string();
            if (clean[i + 2] == '=' && clean[i + 3] != '=') return std::string();
            int v[4];
            for (int k = 0; k < 4 - pad; ++k)
                if ((v[k] = value(clean[i + k])) < 0) return std::string();
            for (int k = 4 - pad; k < 4; ++k) v[k] = 0;
            const uint32_t n = static_cast<uint32_t>(v[0]) << 18 | static_cast<uint32_t>(v[1]) << 12
                | static_cast<uint32_t>(v[2]) << 6 | static_cast<uint32_t>(v[3]);
            out += static_cast<char>(n >> 16 & 0xFF);
            if (pad < 2) out += static_cast<char>(n >> 8 & 0xFF);
            if (pad < 1) out += static_cast<char>(n & 0xFF);
        }
        return out;
    }

    /*
        La clé publique avec laquelle l'original (Cydral Technology) signe son catalogue
        (dist/albireo/catalog.json.sig), recopiée de host/crypto.cpp de l'original
        (feed_public_key_pem).  Une clé publique n'a rien de secret : elle ne sert qu'à vérifier.
    */
    const char* const original_catalog_key_pem =
        "-----BEGIN PUBLIC KEY-----\n"
        "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEJ5dUhne+WUdHXTPxWFhgRfy8aANq\n"
        "nkkeFLVzToFfiWTl9sGwuRkECPcyhYVP3dC1/yhsV/KCYxxhlwGcmeAevw==\n"
        "-----END PUBLIC KEY-----\n";

    namespace
    {
        // L'en-tête DER d'une clé publique P-256 (SubjectPublicKeyInfo : id-ecPublicKey, prime256v1),
        // suivi du point non compressé de 65 octets (04, X, Y), la forme que lit SecKeyCreateWithData.
        const unsigned char p256_spki_prefix[] = {
            0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
            0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

        // Libère un objet de CoreFoundation en fin de portée.
        struct cf_release
        {
            CFTypeRef ref;
            explicit cf_release(CFTypeRef r) : ref(r) {}
            ~cf_release() { if (ref) CFRelease(ref); }
            cf_release(const cf_release&) = delete;
            cf_release& operator=(const cf_release&) = delete;
        };
    }

    std::string p256_point_of_pem(const std::string& pem)
    {
        std::string body;
        size_t at = 0;
        bool inside = false;
        while (at < pem.size())
        {
            size_t end = pem.find('\n', at);
            if (end == std::string::npos) end = pem.size();
            std::string line = pem.substr(at, end - at);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "-----BEGIN PUBLIC KEY-----") inside = true;
            else if (line == "-----END PUBLIC KEY-----") break;
            else if (inside) body += line;
            at = end + 1;
        }
        const std::string der = base64_decode(body);
        const size_t n = sizeof(p256_spki_prefix);
        if (der.size() != n + 65 || der.compare(0, n, reinterpret_cast<const char*>(p256_spki_prefix), n) != 0
            || der[n] != 0x04)
            return std::string();
        return der.substr(n);
    }

    std::string pem_of_p256_point(const std::string& point)
    {
        if (point.size() != 65 || point[0] != 0x04) return std::string();
        const std::string b64 = base64_encode(std::string(reinterpret_cast<const char*>(p256_spki_prefix),
            sizeof(p256_spki_prefix)) + point);
        std::string pem = "-----BEGIN PUBLIC KEY-----\n";
        for (size_t i = 0; i < b64.size(); i += 64) pem += b64.substr(i, 64) + "\n";
        return pem + "-----END PUBLIC KEY-----\n";
    }

    bool verify_signature(const std::string& data, const std::string& signature_base64,
        const std::string& public_key_pem)
    {
        // La vérification passe par le framework Security de macOS (ECDSA P-256 sur SHA-256, signature
        // DER), à la place de Mbed TLS dans l'original.
        const std::string point = p256_point_of_pem(public_key_pem);
        const std::string sig = base64_decode(signature_base64);
        if (point.empty() || sig.empty()) return false;
        cf_release key_data(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(point.data()),
            static_cast<CFIndex>(point.size())));
        const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass};
        const void* values[] = {kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPublic};
        cf_release attributes(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks));
        if (!key_data.ref || !attributes.ref) return false;
        cf_release key(SecKeyCreateWithData(static_cast<CFDataRef>(key_data.ref),
            static_cast<CFDictionaryRef>(attributes.ref), nullptr));
        if (!key.ref) return false;
        cf_release message(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(data.data()),
            static_cast<CFIndex>(data.size())));
        cf_release signature(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(sig.data()),
            static_cast<CFIndex>(sig.size())));
        if (!message.ref || !signature.ref) return false;
        return SecKeyVerifySignature(static_cast<SecKeyRef>(const_cast<void*>(key.ref)),
            kSecKeyAlgorithmECDSASignatureMessageX962SHA256, static_cast<CFDataRef>(message.ref),
            static_cast<CFDataRef>(signature.ref), nullptr);
    }

}
