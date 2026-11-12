// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#include "keys.h"

#include <algorithm>
#include <array>

#include <dlib/crypto/aes.h>
#include <dlib/crypto/crypto_utils.h>
#include <dlib/crypto/secure_random.h>
#include <dlib/crypto/sha256.h>

#include "../common/json.h"
#include "../common/platform.h"
#include "crypto.h"

namespace pseudalis
{
    namespace
    {
        const char* const salt = "Pseudalis key derivation 1";
        const unsigned long export_iterations = 600000;

        std::string key_path(const std::string& dir)
        {
            return join_path(dir, "master.key");
        }

    }

    void wipe(std::string& s)
    {
        if (!s.empty()) dlib::secure_wipe(&s[0], s.size());
        s.clear();
    }

    void aes256_ctr_into(const std::string& key, const char* iv, const char* in, char* out, size_t size)
    {
        const dlib::aes_block_cipher aes(key.data(), key.size());
        unsigned char counter[16], stream[16];
        for (int k = 0; k < 16; ++k) counter[k] = static_cast<unsigned char>(iv[k]);
        for (size_t at = 0; at < size; at += 16)
        {
            aes.encrypt_block(counter, stream);
            const size_t n = std::min<size_t>(16, size - at);
            for (size_t k = 0; k < n; ++k)
                out[at + k] = static_cast<char>(static_cast<unsigned char>(in[at + k]) ^ stream[k]);
            for (int k = 15; k >= 0 && ++counter[k] == 0; --k) {}
        }
        dlib::secure_wipe(stream, sizeof(stream));
    }

    std::string aes256_ctr(const std::string& key, const std::string& iv, const std::string& data)
    {
        std::string out(data.size(), '\0');
        if (!data.empty()) aes256_ctr_into(key, iv.data(), data.data(), &out[0], data.size());
        return out;
    }

    std::string hmac_sha256(const std::string& key, const std::string& data)
    {
        const std::array<unsigned char, 32> d = dlib::hmac_sha256_digest(key, data);
        return std::string(reinterpret_cast<const char*>(d.data()), d.size());
    }

    key_set::~key_set()
    {
        wipe(pseudonymization);
        wipe(vault_encryption);
        wipe(vault_authentication);
    }

    key_set derive_keys(const std::string& master)
    {
        key_set k;
        k.pseudonymization = dlib::hkdf_sha256(master, salt, "pseudonymization", 32);
        k.vault_encryption = dlib::hkdf_sha256(master, salt, "vault encryption", 32);
        k.vault_authentication = dlib::hkdf_sha256(master, salt, "vault authentication", 32);
        return k;
    }

    bool private_folder(const std::string& path, std::string& error)
    {
        return secure_folder(path, error);
    }

    bool save_master_key(const std::string& keys_dir, const std::string& master, std::string& error)
    {
        if (!private_folder(keys_dir, error)) return false;
        std::string stored;
        stored = master;
        const bool ok = write_file_restricted(key_path(keys_dir), "PSDK1" + stored);
        wipe(stored);
        if (!ok) error = "cannot write " + key_path(keys_dir);
        return ok;
    }

    bool load_master_key(const std::string& keys_dir, std::string& master, bool& created, std::string& error)
    {
        created = false;
        if (!private_folder(keys_dir, error)) return false;
        std::string data;
        if (!file_exists(key_path(keys_dir)))
        {
            master = dlib::generate_secure_random_bytes(32);
            created = true;
            return save_master_key(keys_dir, master, error);
        }
        if (!read_file(key_path(keys_dir), data, 64 * 1024) || data.compare(0, 5, "PSDK1") != 0)
        {
            error = key_path(keys_dir) + " cannot be read";
            return false;
        }
        std::string stored = data.substr(5);
        wipe(data);
        master = stored;
        wipe(stored);
        if (master.size() != 32)
        {
            wipe(master);
            error = key_path(keys_dir) + " holds no key of 32 bytes";
            return false;
        }
        return true;
    }

    std::string export_master_key(const std::string& master, const std::string& passphrase)
    {
        const std::string salt_bytes = dlib::generate_secure_random_bytes(16);
        const std::string iv = dlib::generate_secure_random_bytes(16);
        std::string keys = dlib::pbkdf2_hmac_sha256(passphrase, salt_bytes, export_iterations, 64);
        const std::string enc = keys.substr(0, 32), mac = keys.substr(32);
        wipe(keys);
        const std::string sealed = aes256_ctr(enc, iv, master);
        json j = json::object();
        j["format"] = "pseudalis-master-key";
        j["version"] = 1;
        j["kdf"] = "pbkdf2-sha256";
        j["iterations"] = static_cast<unsigned long long>(export_iterations);
        j["salt"] = base64_encode(salt_bytes);
        j["iv"] = base64_encode(iv);
        j["key"] = base64_encode(sealed);
        j["mac"] = base64_encode(hmac_sha256(mac, salt_bytes + iv + sealed));
        return j.dump_formatted(2) + "\n";
    }

    bool import_master_key(const std::string& exported, const std::string& passphrase, std::string& master,
                           std::string& error)
    {
        json j;
        if (!json::parse(exported, j) || j["format"].str() != "pseudalis-master-key" || j["version"].as_int() != 1)
        {
            error = "not an export of a Pseudalis key";
            return false;
        }
        const long long iterations = j["iterations"].as_int(0);
        const std::string salt_bytes = base64_decode(j["salt"].str()), iv = base64_decode(j["iv"].str());
        const std::string sealed = base64_decode(j["key"].str()), tag = base64_decode(j["mac"].str());
        if (iterations < 100000 || iterations > 10000000 || salt_bytes.size() != 16 || iv.size() != 16 ||
            sealed.size() != 32 || tag.size() != 32)
        {
            error = "the export is damaged";
            return false;
        }
        std::string keys = dlib::pbkdf2_hmac_sha256(passphrase, salt_bytes, static_cast<unsigned long>(iterations), 64);
        const std::string enc = keys.substr(0, 32), mac = keys.substr(32);
        wipe(keys);
        const std::string expected = hmac_sha256(mac, salt_bytes + iv + sealed);
        if (!dlib::constant_time_equal(expected.data(), tag.data(), 32))
        {
            error = "wrong passphrase, or a damaged export";
            return false;
        }
        master = aes256_ctr(enc, iv, sealed);
        return true;
    }
}
