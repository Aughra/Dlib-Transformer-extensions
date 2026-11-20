// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// memset_s (C11, annexe K), qui efface un secret sans que le compilateur puisse retirer l'écriture.
#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>

#include "certificates.h"

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>

#include "crypto.h"
#include "../common/log.h"
#include "../common/platform.h"

/*
    Les certificats du serveur local, par Security et CommonCrypto (fork pour macOS, 2026-11-20 : à
    la place de Mbed TLS).  Les clés sont des clés EC P-256 de Security, gardées en mémoire, jamais
    dans le trousseau ; les certificats X.509 sont écrits en DER ici et signés par SecKeyCreateSignature
    (ECDSA avec SHA-256).  Les fichiers restent ceux qu'écrivait Mbed TLS : certificats en PEM, clé
    du serveur en PEM « EC PRIVATE KEY » (SEC1), de sorte que des certificats déjà en place, et une
    autorité déjà approuvée par la machine, restent valables.
*/

namespace gulliver
{

    namespace
    {
        // La validité des deux certificats, et le délai avant leur fin où ils sont renouvelés.
        const long validity_days = 397;
        const long long renewal_seconds = 86400LL * 30;

        using bytes = std::vector<unsigned char>;

        template <typename T>
        struct cf_hold
        {
            T ref = nullptr;
            cf_hold() = default;
            explicit cf_hold(T r) : ref(r) {}
            ~cf_hold() { if (ref) CFRelease(ref); }
            cf_hold(const cf_hold&) = delete;
            cf_hold& operator=(const cf_hold&) = delete;
            explicit operator bool() const { return ref != nullptr; }
        };

        void wipe(bytes& b)
        {
            if (!b.empty()) memset_s(b.data(), b.size(), 0, b.size());
            b.clear();
        }

        // --- DER : écriture -------------------------------------------------------------------

        void der_append(bytes& out, unsigned char tag, const bytes& content)
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

        bytes der(unsigned char tag, const bytes& content)
        {
            bytes out;
            der_append(out, tag, content);
            return out;
        }

        bytes cat(std::initializer_list<bytes> parts)
        {
            bytes out;
            for (const bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
            return out;
        }

        bytes text(const std::string& s) { return bytes(s.begin(), s.end()); }

        // Les identifiants d'objets, déjà encodés.
        const bytes oid_ec_public_key = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01 };        // 1.2.840.10045.2.1
        const bytes oid_prime256v1 = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };     // 1.2.840.10045.3.1.7
        const bytes oid_ecdsa_sha256 = { 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02 };   // 1.2.840.10045.4.3.2
        const bytes oid_common_name = { 0x55, 0x04, 0x03 };
        const bytes oid_organization = { 0x55, 0x04, 0x0A };
        const bytes oid_country = { 0x55, 0x04, 0x06 };
        const bytes oid_basic_constraints = { 0x55, 0x1D, 0x13 };
        const bytes oid_subject_key_id = { 0x55, 0x1D, 0x0E };
        const bytes oid_authority_key_id = { 0x55, 0x1D, 0x23 };
        const bytes oid_key_usage = { 0x55, 0x1D, 0x0F };
        const bytes oid_name_constraints = { 0x55, 0x1D, 0x1E };
        const bytes oid_ext_key_usage = { 0x55, 0x1D, 0x25 };
        const bytes oid_subject_alt_name = { 0x55, 0x1D, 0x11 };
        const bytes oid_server_auth = { 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };    // 1.3.6.1.5.5.7.3.1

        struct name_part { bytes oid; std::string value; bool printable; };

        // Un nom distinctif, dans l'ordre où l'écrivait Mbed TLS (CN, O, C).
        bytes distinguished_name(const std::vector<name_part>& parts)
        {
            bytes rdns;
            for (const name_part& p : parts)
            {
                const bytes atv = der(0x30, cat({ der(0x06, p.oid), der(p.printable ? 0x13 : 0x0C, text(p.value)) }));
                der_append(rdns, 0x31, atv);
            }
            return der(0x30, rdns);
        }

        const std::vector<name_part> authority_name = {
            { oid_common_name, "Gulliver Local Authority", false },
            { oid_organization, "Cydral Technology", false },
            { oid_country, "FR", true } };
        const std::vector<name_part> server_name = {
            { oid_common_name, "localhost", false },
            { oid_organization, "Cydral Technology", false } };

        bytes utc_time(std::time_t t)
        {
            std::tm tm;
            gmtime_r(&t, &tm);
            char buf[16];
            std::strftime(buf, sizeof(buf), "%y%m%d%H%M%SZ", &tm);
            return der(0x17, text(buf));
        }

        bytes extension(const bytes& oid, bool critical, const bytes& value)
        {
            bytes content = der(0x06, oid);
            if (critical) { content.push_back(0x01); content.push_back(0x01); content.push_back(0xFF); }
            der_append(content, 0x04, value);
            return der(0x30, content);
        }

        /*  Les contraintes de nom de l'autorité locale (RFC 5280 4.2.1.10) : elle ne peut
            certifier que localhost, les adresses de bouclage d'IPv4 et ::1.  L'autorité est
            approuvée par toute la machine, et c'est la contrainte qui empêche un certificat
            qu'elle a signé de jamais se porter garant d'un autre site.  */
        bytes loopback_name_constraints()
        {
            bytes subtrees;
            der_append(subtrees, 0x30, der(0x82, text("localhost")));                  // dNSName
            der_append(subtrees, 0x30, der(0x87, { 127, 0, 0, 0, 255, 0, 0, 0 }));     // 127.0.0.0/8
            bytes v6(32, 0);
            v6[15] = 1;                                                                 // ::1/128
            for (size_t i = 16; i < 32; ++i) v6[i] = 0xFF;
            der_append(subtrees, 0x30, der(0x87, v6));
            return der(0x30, der(0xA0, subtrees));                                      // permittedSubtrees [0]
        }

        // --- DER : lecture --------------------------------------------------------------------

        struct der_reader
        {
            const unsigned char* p;
            const unsigned char* end;
            bool ok = true;

            // Lit un élément : son étiquette, et son contenu dans [*body, *body_end).
            bool next(unsigned char& tag, const unsigned char*& body, const unsigned char*& body_end)
            {
                if (!ok || end - p < 2) return ok = false;
                tag = *p++;
                size_t n = *p++;
                if (n & 0x80)
                {
                    const size_t k = n & 0x7F;
                    if (k == 0 || k > 3 || static_cast<size_t>(end - p) < k) return ok = false;
                    n = 0;
                    for (size_t i = 0; i < k; ++i) n = n << 8 | *p++;
                }
                if (static_cast<size_t>(end - p) < n) return ok = false;
                body = p;
                body_end = p + n;
                p += n;
                return true;
            }
        };

        // --- PEM ------------------------------------------------------------------------------

        std::string pem_wrap(const std::string& label, const bytes& der_bytes)
        {
            const std::string b64 = base64_encode(std::string(der_bytes.begin(), der_bytes.end()));
            std::string out = "-----BEGIN " + label + "-----\n";
            for (size_t i = 0; i < b64.size(); i += 64) out += b64.substr(i, 64) + "\n";
            return out + "-----END " + label + "-----\n";
        }

        // Le contenu DER du premier bloc PEM de label, vide en cas d'échec.
        bytes pem_unwrap(const std::string& pem, const std::string& label)
        {
            const std::string begin = "-----BEGIN " + label + "-----", end = "-----END " + label + "-----";
            const size_t a = pem.find(begin);
            if (a == std::string::npos) return bytes();
            const size_t b = pem.find(end, a + begin.size());
            if (b == std::string::npos) return bytes();
            std::string decoded = base64_decode(pem.substr(a + begin.size(), b - a - begin.size()));
            bytes out(decoded.begin(), decoded.end());
            if (!decoded.empty()) memset_s(&decoded[0], decoded.size(), 0, decoded.size());
            return out;
        }

        // --- Clés -----------------------------------------------------------------------------

        // Une clé privée EC P-256 de Security, en mémoire seulement.
        SecKeyRef generate_key()
        {
            const int bits = 256;
            cf_hold<CFNumberRef> size(CFNumberCreate(nullptr, kCFNumberIntType, &bits));
            const void* keys[] = { kSecAttrKeyType, kSecAttrKeySizeInBits, kSecAttrIsPermanent };
            const void* values[] = { kSecAttrKeyTypeECSECPrimeRandom, size.ref, kCFBooleanFalse };
            cf_hold<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks));
            return SecKeyCreateRandomKey(attrs.ref, nullptr);
        }

        // La représentation externe d'une clé : 04 || X || Y pour une clé publique, suivis de K pour une privée.
        bytes external(SecKeyRef key)
        {
            cf_hold<CFDataRef> d(SecKeyCopyExternalRepresentation(key, nullptr));
            if (!d) return bytes();
            const unsigned char* p = CFDataGetBytePtr(d.ref);
            return bytes(p, p + CFDataGetLength(d.ref));
        }

        bytes public_point(SecKeyRef private_key)
        {
            cf_hold<SecKeyRef> pub(SecKeyCopyPublicKey(private_key));
            return pub ? external(pub.ref) : bytes();
        }

        // La clé privée au format SEC1 (RFC 5915), celui qu'écrivait Mbed TLS : « EC PRIVATE KEY ».
        bool key_pem(SecKeyRef key, std::string& pem)
        {
            bytes ext = external(key);
            if (ext.size() != 97) { wipe(ext); return false; }
            bytes scalar(ext.begin() + 65, ext.end());
            const bytes point(ext.begin(), ext.begin() + 65);
            bytes bit = { 0x00 };
            bit.insert(bit.end(), point.begin(), point.end());
            bytes body = cat({ der(0x02, { 0x01 }), der(0x04, scalar), der(0xA0, der(0x06, oid_prime256v1)),
                der(0xA1, der(0x03, bit)) });
            bytes seq = der(0x30, body);
            pem = pem_wrap("EC PRIVATE KEY", seq);
            wipe(ext);
            wipe(scalar);
            wipe(body);
            wipe(seq);
            return true;
        }

        /*  Relit une clé privée PEM : SEC1 (« EC PRIVATE KEY », celle qu'écrivent Gulliver et Mbed TLS)
            ou PKCS#8 (« PRIVATE KEY ») qui enveloppe du SEC1.  Renvoie la clé de Security, ou nullptr. */
        SecKeyRef read_key(const std::string& pem)
        {
            bytes sec1 = pem_unwrap(pem, "EC PRIVATE KEY");
            if (sec1.empty())
            {
                bytes pk8 = pem_unwrap(pem, "PRIVATE KEY");
                der_reader r{ pk8.data(), pk8.data() + pk8.size() };
                unsigned char tag;
                const unsigned char *b, *e;
                if (r.next(tag, b, e) && tag == 0x30)
                {
                    der_reader in{ b, e };
                    const unsigned char *vb, *ve, *ab, *ae, *kb, *ke;
                    if (in.next(tag, vb, ve) && tag == 0x02 && in.next(tag, ab, ae) && tag == 0x30
                        && in.next(tag, kb, ke) && tag == 0x04)
                        sec1.assign(kb, ke);
                }
                wipe(pk8);
            }
            SecKeyRef key = nullptr;
            der_reader r{ sec1.data(), sec1.data() + sec1.size() };
            unsigned char tag;
            const unsigned char *b, *e;
            if (r.next(tag, b, e) && tag == 0x30)
            {
                der_reader in{ b, e };
                const unsigned char *vb, *ve, *kb, *ke;
                bytes scalar, point;
                if (in.next(tag, vb, ve) && tag == 0x02 && in.next(tag, kb, ke) && tag == 0x04 && ke - kb <= 32)
                {
                    scalar.assign(32 - static_cast<size_t>(ke - kb), 0);
                    scalar.insert(scalar.end(), kb, ke);
                    const unsigned char *xb, *xe;
                    while (in.next(tag, xb, xe))
                        if (tag == 0xA1)
                        {
                            der_reader pr{ xb, xe };
                            const unsigned char *pb, *pe;
                            if (pr.next(tag, pb, pe) && tag == 0x03 && pe - pb == 66 && pb[0] == 0) point.assign(pb + 1, pe);
                        }
                }
                if (scalar.size() == 32 && point.size() == 65 && point[0] == 0x04)
                {
                    bytes ext = cat({ point, scalar });
                    cf_hold<CFDataRef> data(CFDataCreate(nullptr, ext.data(), static_cast<CFIndex>(ext.size())));
                    const int bits = 256;
                    cf_hold<CFNumberRef> size(CFNumberCreate(nullptr, kCFNumberIntType, &bits));
                    const void* keys[] = { kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits };
                    const void* values[] = { kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPrivate, size.ref };
                    cf_hold<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, values, 3,
                        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
                    key = SecKeyCreateWithData(data.ref, attrs.ref, nullptr);
                    wipe(ext);
                }
                wipe(scalar);
            }
            wipe(sec1);
            return key;
        }

        // --- Certificats ----------------------------------------------------------------------

        // Le TBSCertificate d'un certificat DER, et la clé publique (04 || X || Y) qu'il porte.
        bool certificate_parts(const bytes& der_bytes, const unsigned char*& tbs_b, const unsigned char*& tbs_e,
            bytes& point, std::time_t* not_after)
        {
            der_reader r{ der_bytes.data(), der_bytes.data() + der_bytes.size() };
            unsigned char tag;
            const unsigned char *b, *e;
            if (!r.next(tag, b, e) || tag != 0x30) return false;
            der_reader cert{ b, e };
            if (!cert.next(tag, tbs_b, tbs_e) || tag != 0x30) return false;
            der_reader tbs{ tbs_b, tbs_e };
            const unsigned char *fb, *fe;
            if (!tbs.next(tag, fb, fe)) return false;
            if (tag == 0xA0 && !tbs.next(tag, fb, fe)) return false;        // version, puis numéro de série
            if (!tbs.next(tag, fb, fe) || !tbs.next(tag, fb, fe)) return false;   // algorithme, émetteur
            if (!tbs.next(tag, fb, fe) || tag != 0x30) return false;              // validité
            der_reader validity{ fb, fe };
            const unsigned char *t1b, *t1e, *t2b, *t2e;
            unsigned char t1, t2;
            if (!validity.next(t1, t1b, t1e) || !validity.next(t2, t2b, t2e)) return false;
            if (not_after)
            {
                const std::string t(reinterpret_cast<const char*>(t2b), static_cast<size_t>(t2e - t2b));
                std::tm tm = {};
                if (t2 == 0x17 && t.size() == 13)
                {
                    tm.tm_year = std::stoi(t.substr(0, 2));
                    if (tm.tm_year < 50) tm.tm_year += 100;
                    tm.tm_mon = std::stoi(t.substr(2, 2)) - 1;
                    tm.tm_mday = std::stoi(t.substr(4, 2));
                    tm.tm_hour = std::stoi(t.substr(6, 2));
                    tm.tm_min = std::stoi(t.substr(8, 2));
                    tm.tm_sec = std::stoi(t.substr(10, 2));
                }
                else if (t2 == 0x18 && t.size() == 15)
                {
                    tm.tm_year = std::stoi(t.substr(0, 4)) - 1900;
                    tm.tm_mon = std::stoi(t.substr(4, 2)) - 1;
                    tm.tm_mday = std::stoi(t.substr(6, 2));
                    tm.tm_hour = std::stoi(t.substr(8, 2));
                    tm.tm_min = std::stoi(t.substr(10, 2));
                    tm.tm_sec = std::stoi(t.substr(12, 2));
                }
                else return false;
                *not_after = timegm(&tm);
            }
            if (!tbs.next(tag, fb, fe) || !tbs.next(tag, fb, fe) || tag != 0x30) return false;   // sujet, clé
            der_reader spki{ fb, fe };
            const unsigned char *ab, *ae, *kb, *ke;
            if (!spki.next(tag, ab, ae) || !spki.next(tag, kb, ke) || tag != 0x03 || ke - kb < 2 || kb[0] != 0)
                return false;
            point.assign(kb + 1, ke);
            return true;
        }

        bytes read_certificate_der(const std::string& path)
        {
            std::string pem;
            if (!read_file(path, pem)) return bytes();
            return pem_unwrap(pem, "CERTIFICATE");
        }

        // L'identifiant d'une clé : les 160 premiers bits du SHA-256 de son point (RFC 7093, méthode 1).
        bytes key_identifier(const bytes& point)
        {
            unsigned char h[CC_SHA256_DIGEST_LENGTH];
            CC_SHA256(point.data(), static_cast<CC_LONG>(point.size()), h);
            return bytes(h, h + 20);
        }

        /*  Un certificat X.509 v3 signé par issuer_key : pour l'autorité (is_ca) ou pour le serveur
            local.  Les extensions et leur caractère critique sont ceux qu'écrivait Mbed TLS. */
        bool write_certificate(SecKeyRef subject_key, SecKeyRef issuer_key, const std::vector<name_part>& subject,
            const std::vector<name_part>& issuer, bool is_ca, long days, std::string& pem)
        {
            const bytes point = public_point(subject_key), issuer_point = public_point(issuer_key);
            if (point.size() != 65 || issuer_point.size() != 65) return false;
            bytes serial(16);
            if (SecRandomCopyBytes(kSecRandomDefault, serial.size(), serial.data()) != errSecSuccess) return false;
            serial[0] = static_cast<unsigned char>((serial[0] & 0x7F) | 0x01);   // positif, sans zéro de tête
            const bytes algorithm = der(0x30, der(0x06, oid_ecdsa_sha256));
            const std::time_t now = std::time(nullptr) - 3600;
            bytes bit_point = { 0x00 };
            bit_point.insert(bit_point.end(), point.begin(), point.end());
            const bytes spki = der(0x30, cat({ der(0x30, cat({ der(0x06, oid_ec_public_key), der(0x06, oid_prime256v1) })),
                der(0x03, bit_point) }));
            const bytes ski = key_identifier(point), aki = key_identifier(issuer_point);
            const bytes eku = der(0x30, der(0x06, oid_server_auth));
            bytes exts;
            if (is_ca)
            {
                // CA:TRUE, longueur de chemin 0 ; signature de certificats et de listes de révocation.
                exts = cat({ extension(oid_basic_constraints, true, der(0x30, cat({ { 0x01, 0x01, 0xFF }, der(0x02, { 0x00 }) }))),
                    extension(oid_subject_key_id, false, der(0x04, ski)),
                    extension(oid_authority_key_id, false, der(0x30, der(0x80, aki))),
                    extension(oid_key_usage, true, der(0x03, { 0x01, 0x06 })),
                    /* Marquée non critique, pour que les vérificateurs qui n'implémentent pas
                       l'extension acceptent quand même l'autorité ; ceux des navigateurs et du
                       système l'appliquent de toute façon. */
                    extension(oid_name_constraints, false, loopback_name_constraints()),
                    /* L'autorité locale ne signe que des certificats de serveur TLS. */
                    extension(oid_ext_key_usage, true, eku) });
            }
            else
            {
                bytes san;
                const bytes v6 = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
                der_append(san, 0x87, v6);
                der_append(san, 0x87, { 127, 0, 0, 1 });
                der_append(san, 0x82, text("localhost"));
                exts = cat({ extension(oid_basic_constraints, false, der(0x30, bytes())),
                    extension(oid_subject_key_id, false, der(0x04, ski)),
                    extension(oid_authority_key_id, false, der(0x30, der(0x80, aki))),
                    extension(oid_key_usage, true, der(0x03, { 0x07, 0x80 })),
                    extension(oid_ext_key_usage, true, eku),
                    extension(oid_subject_alt_name, false, der(0x30, san)) });
            }
            const bytes tbs = der(0x30, cat({ der(0xA0, der(0x02, { 0x02 })), der(0x02, serial), algorithm,
                distinguished_name(issuer), der(0x30, cat({ utc_time(now), utc_time(now + static_cast<std::time_t>(days) * 86400) })),
                distinguished_name(subject), spki, der(0xA3, der(0x30, exts)) }));
            cf_hold<CFDataRef> tbs_data(CFDataCreate(nullptr, tbs.data(), static_cast<CFIndex>(tbs.size())));
            cf_hold<CFDataRef> sig(SecKeyCreateSignature(issuer_key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
                tbs_data.ref, nullptr));
            if (!sig) return false;
            bytes bit_sig = { 0x00 };
            bit_sig.insert(bit_sig.end(), CFDataGetBytePtr(sig.ref), CFDataGetBytePtr(sig.ref) + CFDataGetLength(sig.ref));
            pem = pem_wrap("CERTIFICATE", der(0x30, cat({ tbs, algorithm, der(0x03, bit_sig) })));
            return true;
        }

        // Secondes avant l'expiration d'un certificat, négatives quand il ne peut pas être lu.
        long long seconds_left(const std::string& path)
        {
            const bytes d = read_certificate_der(path);
            const unsigned char *b, *e;
            bytes point;
            std::time_t expiry = 0;
            if (d.empty() || !certificate_parts(d, b, e, point, &expiry)) return -1;
            return static_cast<long long>(expiry - std::time(nullptr));
        }

        // Indique si une clé privée au format PEM est celle du certificat d'un fichier PEM.
        bool key_matches(const std::string& key, const std::string& cert_path)
        {
            const bytes d = read_certificate_der(cert_path);
            const unsigned char *b, *e;
            bytes cert_point;
            if (d.empty() || !certificate_parts(d, b, e, cert_point, nullptr)) return false;
            cf_hold<SecKeyRef> k(read_key(key));
            return k && public_point(k.ref) == cert_point;
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
        if (!s.empty()) memset_s(&s[0], s.size(), 0, s.size());
        s.clear();
    }

    certificate_files certificate_paths(const std::string& dir)
    {
        certificate_files f;
        f.ca_cert = join_path(dir, "gulliver-authority.crt");
        f.ca_key = join_path(dir, "gulliver-authority.key");
        f.server_cert = join_path(dir, "gulliver-server.crt");
        f.plain_key = join_path(dir, "gulliver-server.key");
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
        const certificate_files f = certificate_paths(dir);
        if (!make_dirs(dir))
        {
            error = "cannot create " + dir;
            return false;
        }
        const bool exposed = migrate(f);
        if (exposed) log_warning("certificates: the local authority kept its key on disk, a new authority replaces it");

        // Les certificats en place servent tant qu'il leur reste du temps et que la clé s'ouvre et correspond.
        if (!exposed && file_exists(f.ca_cert) && file_exists(f.server_cert)
            && seconds_left(f.ca_cert) > renewal_seconds && seconds_left(f.server_cert) > renewal_seconds)
        {
            std::string pem;
            const bool ok = read_server_key(dir, pem) && key_matches(pem, f.server_cert);
            wipe_secret(pem);
            if (ok) return true;
            log_warning("certificates: the key of the server cannot be read here, new certificates are made");
        }

        // Une nouvelle autorité signe un nouveau certificat du serveur, puis sa clé disparaît.
        std::string ca_pem, server_pem, key_text;
        bool ok = false;
        {
            cf_hold<SecKeyRef> ca_key(generate_key()), key(generate_key());
            ok = ca_key && key
                && write_certificate(ca_key.ref, ca_key.ref, authority_name, authority_name, true, validity_days + 1, ca_pem)
                && write_certificate(key.ref, ca_key.ref, server_name, authority_name, false, validity_days, server_pem)
                && key_pem(key.ref, key_text);
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
        const bytes d = read_certificate_der(pem_path);
        return std::string(d.begin(), d.end());
    }

    std::string certificate_thumbprint(const std::string& pem_path)
    {
        const std::string der_text = certificate_der(pem_path);
        if (der_text.empty()) return std::string();
        unsigned char h[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(der_text.data(), static_cast<CC_LONG>(der_text.size()), h);
        static const char* hex = "0123456789abcdef";
        std::string out;
        for (unsigned char c : h)
        {
            out += hex[c >> 4];
            out += hex[c & 15];
        }
        return out;
    }

    /*!
        ensures
            - Une identité de Security (le certificat du serveur et sa clé, en mémoire) pour le
              serveur TLS, ou nullptr.  L'appelant la libère par CFRelease.
    !*/
    SecIdentityRef server_identity(const std::string& dir)
    {
        const certificate_files f = certificate_paths(dir);
        const bytes d = read_certificate_der(f.server_cert);
        std::string pem;
        if (d.empty() || !read_server_key(dir, pem)) return nullptr;
        cf_hold<SecKeyRef> key(read_key(pem));
        wipe_secret(pem);
        cf_hold<CFDataRef> data(CFDataCreate(nullptr, d.data(), static_cast<CFIndex>(d.size())));
        cf_hold<SecCertificateRef> cert(SecCertificateCreateWithData(nullptr, data.ref));
        if (!key || !cert) return nullptr;
        return SecIdentityCreate(nullptr, cert.ref, key.ref);
    }

}
