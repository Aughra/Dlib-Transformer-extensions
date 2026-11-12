// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "crypto.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <CommonCrypto/CommonDigest.h>
#include <psa/crypto.h>

#include "../common/platform.h"

namespace albireo
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

    std::string base64_encode(const std::string& data)
    {
        size_t n = 0;
        mbedtls_base64_encode(nullptr, 0, &n, reinterpret_cast<const unsigned char*>(data.data()), data.size());
        std::vector<unsigned char> buf(n + 1);
        if (mbedtls_base64_encode(buf.data(), buf.size(), &n, reinterpret_cast<const unsigned char*>(data.data()),
                data.size()) != 0)
            return std::string();
        return std::string(reinterpret_cast<const char*>(buf.data()), n);
    }

    std::string base64_decode(const std::string& text)
    {
        std::string clean;
        for (char c : text)
            if (c != '\n' && c != '\r' && c != ' ' && c != '\t') clean += c;
        size_t n = 0;
        mbedtls_base64_decode(nullptr, 0, &n, reinterpret_cast<const unsigned char*>(clean.data()), clean.size());
        std::vector<unsigned char> buf(n + 1);
        if (mbedtls_base64_decode(buf.data(), buf.size(), &n, reinterpret_cast<const unsigned char*>(clean.data()),
                clean.size()) != 0)
            return std::string();
        return std::string(reinterpret_cast<const char*>(buf.data()), n);
    }

    namespace
    {
        std::string hex_of(const unsigned char* p, size_t n)
        {
            static const char digits[] = "0123456789abcdef";
            std::string out;
            for (size_t i = 0; i < n; ++i)
            {
                out += digits[p[i] >> 4];
                out += digits[p[i] & 15];
            }
            return out;
        }

        const unsigned password_iterations = 200000;

        std::string pbkdf2_hex(const std::string& password, const std::string& salt, unsigned iterations)
        {
            psa_crypto_init();
            unsigned char key[32];
            if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password.data()),
                    password.size(), reinterpret_cast<const unsigned char*>(salt.data()), salt.size(), iterations,
                    sizeof(key), key) != 0)
                return std::string();
            return hex_of(key, sizeof(key));
        }
    }

    bool same_secret(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        unsigned char d = 0;
        for (size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
        return d == 0;
    }

    std::string password_hash(const std::string& password)
    {
        const std::string salt = random_hex(16);
        const std::string h = pbkdf2_hex(password, salt, password_iterations);
        if (h.empty()) return std::string();
        return "pbkdf2-sha256$" + std::to_string(password_iterations) + "$" + salt + "$" + h;
    }

    bool password_matches(const std::string& password, const std::string& stored)
    {
        const std::string head = "pbkdf2-sha256$";
        if (stored.compare(0, head.size(), head) != 0) return false;
        const size_t a = head.size(), b = stored.find('$', a), c = b == std::string::npos ? b : stored.find('$', b + 1);
        if (c == std::string::npos) return false;
        const unsigned long iterations = std::strtoul(stored.substr(a, b - a).c_str(), nullptr, 10);
        if (iterations < 1000 || iterations > 10000000) return false;
        const std::string h = pbkdf2_hex(password, stored.substr(b + 1, c - b - 1), static_cast<unsigned>(iterations));
        return !h.empty() && same_secret(h, stored.substr(c + 1));
    }

    std::string hmac_sha256_hex(const std::string& key, const std::string& message)
    {
        psa_crypto_init();
        unsigned char mac[32];
        if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(key.data()),
                key.size(), reinterpret_cast<const unsigned char*>(message.data()), message.size(), mac) != 0)
            return std::string();
        return hex_of(mac, sizeof(mac));
    }

}
