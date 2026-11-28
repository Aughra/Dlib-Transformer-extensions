// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_CRYPTO_H_
#define GULLIVER_HOST_CRYPTO_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace gulliver
{

    class sha256_stream
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un SHA-256 calculé sur des données fournies par morceaux.
        !*/
    public:
        sha256_stream();
        ~sha256_stream();
        void update(const void* data, size_t n);
        std::string hex();   // termine le calcul

    private:
        struct impl;
        std::unique_ptr<impl> p_;
    };

    std::string sha256_hex(const std::string& data);

    /*!
        ensures
            - Renvoie le SHA-256 d'un fichier en hexadécimal minuscule, ou une chaîne vide
              quand il ne peut pas être lu ou quand cancel devient true.
            - progress, quand il est fourni, reçoit les octets lus jusque-là.
    !*/
    std::string sha256_file(const std::string& path, const std::function<void(uint64_t)>& progress = nullptr,
        const std::atomic<bool>* cancel = nullptr);

    class link_free_file;

    /*!
        ensures
            - Renvoie le SHA-256 des size() octets d'un fichier ouvert depuis son début, ou une
              chaîne vide quand ils ne peuvent pas être lus ou quand cancel devient true.
    !*/
    std::string sha256_open_file(link_free_file& f, const std::atomic<bool>* cancel = nullptr);

    /*!
        ensures
            - Copie les size() octets d'un fichier ouvert depuis son début dans to, un nouveau
              fichier (new_file), et renvoie le SHA-256 de ce qui a été écrit.
            - Renvoie une chaîne vide, et supprime to, quand la copie échoue ou est annulée, ou
              quand expected est fourni et diffère de ce hash.  Un nom to déjà pris est laissé
              tel quel.
    !*/
    std::string copy_open_file(link_free_file& from, const std::string& to, const std::string& expected,
        const std::atomic<bool>* cancel = nullptr);

    std::string base64_encode(const std::string& data);
    std::string base64_decode(const std::string& text);

    /*!
        ensures
            - Renvoie true quand signature_base64 (une signature ECDSA P-256 au format DER, en
              base64) signe data (sur son SHA-256) avec la clé de public_key_pem (une clé publique
              P-256 au format PEM, SubjectPublicKeyInfo) ; false sinon, y compris pour une clé ou
              une signature illisible.
    !*/
    bool verify_signature(const std::string& data, const std::string& signature_base64,
        const std::string& public_key_pem);

    // Le point public (65 octets : 04, X, Y) d'une clé P-256 au format PEM, vide si elle n'en est pas une.
    std::string p256_point_of_pem(const std::string& pem);
    // La clé publique P-256 au format PEM d'un point de 65 octets, vide s'il n'en est pas un.
    std::string pem_of_p256_point(const std::string& point);

    // La clé publique avec laquelle l'original signe son catalogue (dist/albireo/catalog.json.sig).
    extern const char* const original_catalog_key_pem;

}

#endif // GULLIVER_HOST_CRYPTO_H_
