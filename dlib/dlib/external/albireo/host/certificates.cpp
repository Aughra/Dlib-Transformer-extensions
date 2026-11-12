// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "certificates.h"

#include <cstring>
#include <ctime>
#include <vector>

#include <mbedtls/asn1write.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>


#include "../common/log.h"
#include "../common/platform.h"

namespace albireo
{

    namespace
    {
        // La validité des deux certificats, et le délai avant leur fin où ils sont renouvelés.
        const long validity_days = 397;
        const long long renewal_seconds = 86400LL * 30;
        const char* const authority_name = "CN=Albireo Local Authority,O=Cydral Technology,C=FR";

        struct random_source
        {
            mbedtls_entropy_context entropy;
            mbedtls_ctr_drbg_context drbg;
            bool ok = false;

            random_source()
            {
                mbedtls_entropy_init(&entropy);
                mbedtls_ctr_drbg_init(&drbg);
                static const char personal[] = "albireo-certificates";
                ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                    reinterpret_cast<const unsigned char*>(personal), sizeof(personal) - 1) == 0;
            }
            ~random_source()
            {
                mbedtls_ctr_drbg_free(&drbg);
                mbedtls_entropy_free(&entropy);
            }
        };

        // Une clé de mbedtls, libérée avec son secret effacé (mbedtls_pk_free) sur tous les chemins.
        struct pk_key
        {
            mbedtls_pk_context ctx;
            pk_key() { mbedtls_pk_init(&ctx); }
            ~pk_key() { mbedtls_pk_free(&ctx); }
            pk_key(const pk_key&) = delete;
            pk_key& operator=(const pk_key&) = delete;
        };

        std::string time_string(std::time_t t)
        {
            std::tm tm;
            gmtime_r(&t, &tm);
            char buf[16];
            std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
            return buf;
        }

        bool generate_key(mbedtls_pk_context& key, random_source& rng)
        {
            if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) return false;
            return mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random,
                &rng.drbg) == 0;
        }

        bool key_pem(mbedtls_pk_context& key, std::string& pem)
        {
            std::vector<unsigned char> buf(4096);
            const bool ok = mbedtls_pk_write_key_pem(&key, buf.data(), buf.size()) == 0;
            if (ok) pem = reinterpret_cast<const char*>(buf.data());
            mbedtls_platform_zeroize(buf.data(), buf.size());
            return ok;
        }

        // Secondes avant l'expiration d'un certificat, négatives quand il ne peut pas être lu.
        long long seconds_left(const std::string& path)
        {
            mbedtls_x509_crt crt;
            mbedtls_x509_crt_init(&crt);
            std::string pem;
            long long left = -1;
            if (read_file(path, pem) &&
                mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1) == 0)
            {
                std::tm tm = {};
                tm.tm_year = crt.valid_to.year - 1900;
                tm.tm_mon = crt.valid_to.mon - 1;
                tm.tm_mday = crt.valid_to.day;
                tm.tm_hour = crt.valid_to.hour;
                tm.tm_min = crt.valid_to.min;
                tm.tm_sec = crt.valid_to.sec;
                const std::time_t expiry = timegm(&tm);
                left = static_cast<long long>(expiry - std::time(nullptr));
            }
            mbedtls_x509_crt_free(&crt);
            return left;
        }

        // Indique si une clé privée au format PEM est celle du certificat d'un fichier PEM.
        bool key_matches(const std::string& key, const std::string& cert_path, random_source& rng)
        {
            std::string pem;
            if (!read_file(cert_path, pem)) return false;
            mbedtls_x509_crt crt;
            mbedtls_x509_crt_init(&crt);
            pk_key k;
            const bool ok =
                mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1) == 0
                && mbedtls_pk_parse_key(&k.ctx, reinterpret_cast<const unsigned char*>(key.c_str()), key.size() + 1,
                    nullptr, 0, mbedtls_ctr_drbg_random, &rng.drbg) == 0
                && mbedtls_pk_check_pair(&crt.pk, &k.ctx, mbedtls_ctr_drbg_random, &rng.drbg) == 0;
            mbedtls_x509_crt_free(&crt);
            return ok;
        }

        void der_append(std::vector<unsigned char>& out, unsigned char tag, const std::vector<unsigned char>& content)
        {
            out.push_back(tag);
            const size_t n = content.size();
            if (n < 128) out.push_back(static_cast<unsigned char>(n));
            else if (n < 256) { out.push_back(0x81); out.push_back(static_cast<unsigned char>(n)); }
            else
            {
                out.push_back(0x82);
                out.push_back(static_cast<unsigned char>(n >> 8));
                out.push_back(static_cast<unsigned char>(n));
            }
            out.insert(out.end(), content.begin(), content.end());
        }

        /*  Les contraintes de nom de l'autorité locale (RFC 5280 4.2.1.10) : elle ne peut
            certifier que localhost, les adresses de bouclage d'IPv4 et ::1.  L'autorité est
            approuvée par toute la machine, et c'est la contrainte qui empêche un certificat
            qu'elle a signé de jamais se porter garant d'un autre site.  */
        std::vector<unsigned char> loopback_name_constraints()
        {
            std::vector<unsigned char> subtrees, subtree, name;
            const std::string dns = "localhost";
            name.assign(dns.begin(), dns.end());
            der_append(subtree, 0x82, name);                   // dNSName
            der_append(subtrees, 0x30, subtree);
            subtree.clear();
            name = { 127, 0, 0, 0, 255, 0, 0, 0 };              // 127.0.0.0/8
            der_append(subtree, 0x87, name);                   // iPAddress
            der_append(subtrees, 0x30, subtree);
            subtree.clear();
            name.assign(32, 0);
            name[15] = 1;                                      // ::1/128
            for (size_t i = 16; i < 32; ++i) name[i] = 0xFF;
            der_append(subtree, 0x87, name);
            der_append(subtrees, 0x30, subtree);
            std::vector<unsigned char> permitted, out;
            der_append(permitted, 0xA0, subtrees);             // permittedSubtrees [0]
            der_append(out, 0x30, permitted);
            return out;
        }

        bool write_certificate(mbedtls_pk_context& subject_key, mbedtls_pk_context& issuer_key,
            const std::string& subject, const std::string& issuer, bool is_ca, long days,
            random_source& rng, std::string& pem)
        {
            mbedtls_x509write_cert crt;
            mbedtls_x509write_crt_init(&crt);
            bool ok = false;
            do
            {
                mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
                mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
                mbedtls_x509write_crt_set_subject_key(&crt, &subject_key);
                mbedtls_x509write_crt_set_issuer_key(&crt, &issuer_key);
                if (mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str()) != 0) break;
                if (mbedtls_x509write_crt_set_issuer_name(&crt, issuer.c_str()) != 0) break;
                unsigned char serial[16];
                mbedtls_ctr_drbg_random(&rng.drbg, serial, sizeof(serial));
                serial[0] &= 0x7F;   // un entier positif
                serial[0] |= 0x01;
                if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) != 0) break;
                const std::time_t now = std::time(nullptr) - 3600;
                if (mbedtls_x509write_crt_set_validity(&crt, time_string(now).c_str(),
                        time_string(now + static_cast<std::time_t>(days) * 86400).c_str()) != 0) break;
                if (mbedtls_x509write_crt_set_basic_constraints(&crt, is_ca ? 1 : 0, is_ca ? 0 : -1) != 0) break;
                if (mbedtls_x509write_crt_set_subject_key_identifier(&crt) != 0) break;
                if (mbedtls_x509write_crt_set_authority_key_identifier(&crt) != 0) break;
                if (is_ca)
                {
                    if (mbedtls_x509write_crt_set_key_usage(&crt,
                        MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN) != 0)
                        break;
                    /* Marquée non critique, pour que les vérificateurs qui n'implémentent pas
                       l'extension, Mbed TLS parmi eux, acceptent quand même l'autorité ; ceux
                       des navigateurs et du système l'appliquent de toute façon. */
                    const std::vector<unsigned char> nc = loopback_name_constraints();
                    static const char oid_name_constraints[] = MBEDTLS_OID_NAME_CONSTRAINTS;
                    if (mbedtls_x509write_crt_set_extension(&crt, oid_name_constraints,
                            MBEDTLS_OID_SIZE(MBEDTLS_OID_NAME_CONSTRAINTS), 0, nc.data(), nc.size()) != 0)
                        break;
                    /* L'autorité locale ne signe que des certificats de serveur TLS. */
                    mbedtls_asn1_sequence eku;
                    std::memset(&eku, 0, sizeof(eku));
                    eku.buf.tag = MBEDTLS_ASN1_OID;
                    eku.buf.p = reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));
                    eku.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
                    if (mbedtls_x509write_crt_set_ext_key_usage(&crt, &eku) != 0) break;
                }
                else
                {
                    if (mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0) break;
                    mbedtls_asn1_sequence eku;
                    std::memset(&eku, 0, sizeof(eku));
                    eku.buf.tag = MBEDTLS_ASN1_OID;
                    eku.buf.p = reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));
                    eku.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
                    if (mbedtls_x509write_crt_set_ext_key_usage(&crt, &eku) != 0) break;

                    static unsigned char ipv4[4] = { 127, 0, 0, 1 };
                    static unsigned char ipv6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
                    static const char dns[] = "localhost";
                    mbedtls_x509_san_list n3, n2, n1;
                    std::memset(&n1, 0, sizeof(n1));
                    std::memset(&n2, 0, sizeof(n2));
                    std::memset(&n3, 0, sizeof(n3));
                    n1.node.type = MBEDTLS_X509_SAN_DNS_NAME;
                    n1.node.san.unstructured_name.p = reinterpret_cast<unsigned char*>(const_cast<char*>(dns));
                    n1.node.san.unstructured_name.len = sizeof(dns) - 1;
                    n1.next = &n2;
                    n2.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
                    n2.node.san.unstructured_name.p = ipv4;
                    n2.node.san.unstructured_name.len = sizeof(ipv4);
                    n2.next = &n3;
                    n3.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
                    n3.node.san.unstructured_name.p = ipv6;
                    n3.node.san.unstructured_name.len = sizeof(ipv6);
                    n3.next = nullptr;
                    if (mbedtls_x509write_crt_set_subject_alternative_name(&crt, &n1) != 0) break;
                }
                std::vector<unsigned char> buf(8192);
                if (mbedtls_x509write_crt_pem(&crt, buf.data(), buf.size(), mbedtls_ctr_drbg_random, &rng.drbg) != 0)
                    break;
                pem = reinterpret_cast<const char*>(buf.data());
                ok = true;
            } while (false);
            mbedtls_x509write_crt_free(&crt);
            return ok;
        }

        // La clé privée reste au format PEM ; le fichier reçoit des permissions restreintes.
        bool seal_key(const std::string& pem, std::string& data)
        {
            data = pem;
            return true;
        }

        bool open_key(const std::string& data, std::string& pem)
        {
            pem = data;
            return true;
        }

        bool write_key(const std::string& path, const std::string& pem)
        {
            std::string data;
            const bool ok = seal_key(pem, data) && write_file_restricted(path, data);
            wipe_secret(data);
            return ok;
        }

        /*
            Les fichiers des versions antérieures à 1.0.8 : la clé de leur autorité, qui n'est
            plus jamais conservée, et une éventuelle ancienne clé serveur distincte, déplacée
            vers le chemin courant.  Renvoie true quand la clé de l'autorité a été trouvée :
            une copie a pu quitter l'ordinateur, et l'autorité est remplacée aussitôt.
        */
        bool migrate(const certificate_files& f)
        {
            const bool exposed = file_exists(f.ca_key);
            if (exposed)
            {
                if (remove_file(f.ca_key)) log_info("certificates: the key of the earlier local authority is deleted");
                else log_warning("certificates: the key of the earlier local authority cannot be deleted");
            }
            if (f.plain_key == f.server_key || !file_exists(f.plain_key)) return exposed;
            std::string pem;
            if (!file_exists(f.server_key) && read_file(f.plain_key, pem, 64 * 1024)
                && pem.compare(0, 11, "-----BEGIN ") == 0 && write_key(f.server_key, pem))
                log_info("certificates: the key of the server is now stored with restricted file permissions");
            wipe_secret(pem);
            remove_file(f.plain_key);
            return exposed;
        }
    }

    void wipe_secret(std::string& s)
    {
        if (!s.empty()) mbedtls_platform_zeroize(&s[0], s.size());
        s.clear();
    }

    certificate_files certificate_paths(const std::string& dir)
    {
        certificate_files f;
        f.ca_cert = join_path(dir, "albireo-authority.crt");
        f.ca_key = join_path(dir, "albireo-authority.key");
        f.server_cert = join_path(dir, "albireo-server.crt");
        f.plain_key = join_path(dir, "albireo-server.key");
        f.server_key = f.plain_key;
        return f;
    }

    bool read_server_key(const std::string& dir, std::string& pem)
    {
        std::string data;
        const bool ok = read_file(certificate_paths(dir).server_key, data, 64 * 1024) && open_key(data, pem);
        wipe_secret(data);
        return ok && !pem.empty();
    }

    bool ensure_certificates(const std::string& dir, bool& renewed, std::string& error)
    {
        renewed = false;
        psa_crypto_init();
        const certificate_files f = certificate_paths(dir);
        if (!make_dirs(dir))
        {
            error = "cannot create " + dir;
            return false;
        }
        random_source rng;
        if (!rng.ok)
        {
            error = "no entropy source";
            return false;
        }
        const bool exposed = migrate(f);
        if (exposed) log_warning("certificates: the local authority kept its key on disk, a new authority replaces it");

        // Les certificats en place servent tant qu'il leur reste du temps et que la clé s'ouvre et correspond.
        if (!exposed && file_exists(f.ca_cert) && file_exists(f.server_cert)
            && seconds_left(f.ca_cert) > renewal_seconds && seconds_left(f.server_cert) > renewal_seconds)
        {
            std::string pem;
            const bool ok = read_server_key(dir, pem) && key_matches(pem, f.server_cert, rng);
            wipe_secret(pem);
            if (ok) return true;
            log_warning("certificates: the key of the server cannot be read here, new certificates are made");
        }

        // Une nouvelle autorité signe un nouveau certificat du serveur, puis sa clé disparaît.
        std::string ca_pem, server_pem, key_text;
        bool ok = false;
        {
            pk_key ca_key, key;
            ok = generate_key(ca_key.ctx, rng) && generate_key(key.ctx, rng)
                && write_certificate(ca_key.ctx, ca_key.ctx, authority_name, authority_name, true, validity_days + 1,
                    rng, ca_pem)
                && write_certificate(key.ctx, ca_key.ctx, "CN=localhost,O=Cydral Technology", authority_name, false,
                    validity_days, rng, server_pem)
                && key_pem(key.ctx, key_text);
        }
        // La clé d'abord, puis le certificat du serveur, puis celui de l'autorité.
        ok = ok && write_key(f.server_key, key_text) && write_file_atomic(f.server_cert, server_pem)
            && write_file_atomic(f.ca_cert, ca_pem);
        wipe_secret(key_text);
        if (!ok)
        {
            error = "cannot create the certificates of the local server in " + dir;
            return false;
        }
        renewed = true;
        log_info("certificates: new local authority and server certificate in " + dir);
        return true;
    }

    bool remove_certificate_files(const std::string& dir)
    {
        const certificate_files f = certificate_paths(dir);
        bool ok = true;
        for (const std::string& p : { f.server_key, f.plain_key, f.ca_key, f.server_cert, f.ca_cert })
            if (file_exists(p) && !remove_file(p)) ok = false;
        return ok;
    }

    std::string certificate_der(const std::string& pem_path)
    {
        std::string pem;
        if (!read_file(pem_path, pem)) return std::string();
        mbedtls_x509_crt crt;
        mbedtls_x509_crt_init(&crt);
        std::string der;
        if (mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1) == 0)
            der.assign(reinterpret_cast<const char*>(crt.raw.p), crt.raw.len);
        mbedtls_x509_crt_free(&crt);
        return der;
    }

    std::string certificate_thumbprint(const std::string& pem_path)
    {
        const std::string der = certificate_der(pem_path);
        if (der.empty()) return std::string();
        unsigned char h[32];
        mbedtls_sha256(reinterpret_cast<const unsigned char*>(der.data()), der.size(), h, 0);
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (unsigned char b : h)
        {
            out += hex[b >> 4];
            out += hex[b & 15];
        }
        return out;
    }


}
