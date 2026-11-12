// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_CRYPTO_H_
#define ALBIREO_HOST_CRYPTO_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace albireo
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
            - Renvoie la forme stockée d'un mot de passe : "pbkdf2-sha256$<iterations>$<salt>$<hash>",
              PBKDF2 avec HMAC-SHA-256 sur un sel aléatoire, en hexadécimal.
    !*/
    std::string password_hash(const std::string& password);

    /*!
        ensures
            - Indique si password donne stored (password_hash()), comparés en un temps qui ne
              dépend pas de l'endroit où ils diffèrent.  False pour une forme stockée qui n'en
              est pas une.
    !*/
    bool password_matches(const std::string& password, const std::string& stored);

    // HMAC-SHA-256 de message sous key, en hexadécimal minuscule.
    std::string hmac_sha256_hex(const std::string& key, const std::string& message);

    // Compare deux chaînes en un temps qui ne dépend pas de l'endroit où elles diffèrent.
    bool same_secret(const std::string& a, const std::string& b);

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

}

#endif // ALBIREO_HOST_CRYPTO_H_
