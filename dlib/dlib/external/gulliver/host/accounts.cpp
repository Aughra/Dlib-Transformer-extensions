// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "accounts.h"

#include "crypto.h"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace gulliver
{
    namespace
    {
        constexpr unsigned rounds = 600000;
        constexpr size_t salt_bytes = 16, key_bytes = 32;

        std::string to_hex(const unsigned char* p, size_t n)
        {
            static const char* hex = "0123456789abcdef";
            std::string out;
            out.reserve(2 * n);
            for (size_t i = 0; i < n; ++i)
            {
                out += hex[p[i] >> 4];
                out += hex[p[i] & 15];
            }
            return out;
        }

        bool from_hex(const std::string& s, std::vector<unsigned char>& out)
        {
            if (s.size() % 2) return false;
            auto v = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            out.clear();
            for (size_t i = 0; i < s.size(); i += 2)
            {
                const int a = v(s[i]), b = v(s[i + 1]);
                if (a < 0 || b < 0) return false;
                out.push_back(static_cast<unsigned char>(a * 16 + b));
            }
            return true;
        }

        bool derive(const std::string& password, const std::vector<unsigned char>& salt, unsigned n,
                    unsigned char* key)
        {
            return CCKeyDerivationPBKDF(kCCPBKDF2, password.data(), password.size(), salt.data(), salt.size(),
                                        kCCPRFHmacAlgSHA256, n, key, key_bytes) == kCCSuccess;
        }
    }

    std::string hash_password(const std::string& password)
    {
        std::vector<unsigned char> salt(salt_bytes);
        arc4random_buf(salt.data(), salt.size());
        unsigned char key[key_bytes];
        if (!derive(password, salt, rounds, key)) return std::string();
        return "pbkdf2-sha256$" + std::to_string(rounds) + "$" + to_hex(salt.data(), salt.size()) + "$"
            + to_hex(key, key_bytes);
    }

    bool verify_password(const std::string& password, const std::string& stored)
    {
        // « pbkdf2-sha256$<tours>$<sel>$<empreinte> »
        const std::string prefix = "pbkdf2-sha256$";
        if (stored.compare(0, prefix.size(), prefix) != 0) return false;
        const size_t a = prefix.size(), b = stored.find('$', a), c = b == std::string::npos ? b : stored.find('$', b + 1);
        if (b == std::string::npos || c == std::string::npos) return false;
        const std::string n_text = stored.substr(a, b - a);
        if (n_text.empty() || n_text.size() > 8 || !std::all_of(n_text.begin(), n_text.end(), ::isdigit)) return false;
        const unsigned n = static_cast<unsigned>(std::stoul(n_text));
        std::vector<unsigned char> salt, expected;
        if (n == 0 || !from_hex(stored.substr(b + 1, c - b - 1), salt) || !from_hex(stored.substr(c + 1), expected)
            || expected.size() != key_bytes || salt.empty())
            return false;
        unsigned char key[key_bytes];
        if (!derive(password, salt, n, key)) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < key_bytes; ++i) diff |= static_cast<unsigned char>(key[i] ^ expected[i]);
        return diff == 0;
    }

    std::string new_session_token()
    {
        unsigned char bytes[32];
        arc4random_buf(bytes, sizeof(bytes));
        return to_hex(bytes, sizeof(bytes));
    }

    std::string session_key(const std::string& token)
    {
        return sha256_hex(token);
    }

    int64_t login_throttle::wait(const std::string& profile, int64_t now)
    {
        std::lock_guard<std::mutex> lock(m_);
        const auto it = entries_.find(profile);
        if (it == entries_.end() || it->second.until <= now) return 0;
        return it->second.until - now;
    }

    void login_throttle::failed(const std::string& profile, int64_t now)
    {
        std::lock_guard<std::mutex> lock(m_);
        entry& e = entries_[profile];
        ++e.failures;
        if (e.failures >= 5)
        {
            const int64_t delay = std::min<int64_t>(3600, int64_t(30) << std::min(e.failures - 5, 7));
            e.until = now + delay;
        }
    }

    void login_throttle::succeeded(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        entries_.erase(profile);
    }
}
