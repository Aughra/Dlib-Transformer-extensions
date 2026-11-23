// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#include "accounts.h"

#include "crypto.h"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
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

    void login_codes::prune(int64_t now)
    {
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.expires <= now || it->second.left <= 0 ? entries_.erase(it) : std::next(it);
    }

    std::string login_codes::issue(const std::string& profile, const std::string& pending_email, int64_t now,
        std::string& code)
    {
        // 8 chiffres tirés uniformément (arc4random_uniform évite le biais d'un modulo).
        char digits[16];
        std::snprintf(digits, sizeof(digits), "%08u", arc4random_uniform(100000000u));
        code = digits;
        const std::string challenge = new_session_token();
        std::lock_guard<std::mutex> lock(m_);
        prune(now);
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.profile == profile ? entries_.erase(it) : std::next(it);
        entry& e = entries_[sha256_hex(challenge)];
        e.profile = profile;
        e.pending_email = pending_email;
        e.code_key = sha256_hex(code);
        e.expires = now + lifetime;
        e.left = tries;
        return challenge;
    }

    login_codes::result login_codes::check(const std::string& challenge, const std::string& code, int64_t now,
        std::string& profile, std::string& pending_email, int& left)
    {
        std::lock_guard<std::mutex> lock(m_);
        prune(now);
        const auto it = entries_.find(sha256_hex(challenge));
        if (challenge.size() != 64 || it == entries_.end()) return unknown;
        // Les deux empreintes se comparent en temps constant.
        const std::string given = sha256_hex(code), & expected = it->second.code_key;
        unsigned char diff = given.size() != expected.size();
        for (size_t i = 0; i < given.size() && i < expected.size(); ++i)
            diff |= static_cast<unsigned char>(given[i] ^ expected[i]);
        if (diff == 0 && code.size() == 8)
        {
            profile = it->second.profile;
            pending_email = it->second.pending_email;
            entries_.erase(it);
            return accepted;
        }
        left = --it->second.left;
        if (left <= 0) entries_.erase(it);
        return wrong;
    }

    std::string reset_links::issue(const std::string& profile, int64_t now)
    {
        const std::string token = new_session_token();
        std::lock_guard<std::mutex> lock(m_);
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.profile == profile || it->second.expires <= now ? entries_.erase(it) : std::next(it);
        entries_[sha256_hex(token)] = entry{ profile, now + lifetime };
        return token;
    }

    bool reset_links::consume(const std::string& token, int64_t now, std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (token.size() != 64) return false;
        const auto it = entries_.find(sha256_hex(token));
        if (it == entries_.end()) return false;
        const bool valid = it->second.expires > now;
        profile = it->second.profile;
        entries_.erase(it);
        return valid;
    }

    void reset_links::forget(const std::string& profile)
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto it = entries_.begin(); it != entries_.end();)
            it = it->second.profile == profile ? entries_.erase(it) : std::next(it);
    }

    bool mail_limiter::allow(const std::string& address, int64_t now)
    {
        std::lock_guard<std::mutex> lock(m_);
        sent_.erase(std::remove_if(sent_.begin(), sent_.end(), [now](int64_t t) { return t <= now - 3600; }), sent_.end());
        for (auto it = last_.begin(); it != last_.end();)
            it = it->second <= now - 60 ? last_.erase(it) : std::next(it);
        if (sent_.size() >= 30 || last_.count(address)) return false;
        last_[address] = now;
        sent_.push_back(now);
        return true;
    }
}
