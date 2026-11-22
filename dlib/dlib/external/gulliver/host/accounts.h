// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_ACCOUNTS_H_
#define GULLIVER_HOST_ACCOUNTS_H_

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace gulliver
{
    /*
        Les comptes des profils (fork pour macOS, 2026-11-22) : chaque profil a un mot de passe, et le
        premier profil est l'administrateur, le seul qui crée, renomme ou supprime les autres et change
        leur mot de passe.  Ce fichier tient ce qui ne dépend pas de la base : l'empreinte des mots de
        passe, les jetons de session et le frein aux essais répétés.

        Empreinte : PBKDF2-HMAC-SHA256 de CommonCrypto, 600 000 tours (la recommandation de l'OWASP en
        2023 pour cette fonction), sel de 16 octets tiré par arc4random_buf, rangée sous la forme
        « pbkdf2-sha256$<tours>$<sel hex>$<empreinte hex> ».  Le mot de passe lui-même n'est jamais
        rangé ni écrit au journal.

        Jeton de session : 32 octets au hasard (64 caractères hexadécimaux), donnés une fois à la page ;
        la base n'en garde que le SHA-256, pour qu'une copie de la base ne serve pas à se connecter.
    */

    // La longueur permise d'un mot de passe, en octets (UTF-8).
    constexpr size_t password_min = 8;
    constexpr size_t password_max = 256;

    // L'empreinte d'un mot de passe, avec un sel neuf.
    std::string hash_password(const std::string& password);

    // Vrai quand password correspond à l'empreinte stored (comparaison en temps constant).  Une
    // empreinte vide ou mal formée ne correspond à rien.
    bool verify_password(const std::string& password, const std::string& stored);

    // Un jeton de session neuf (64 caractères hexadécimaux).
    std::string new_session_token();

    // L'empreinte d'un jeton, telle que la base la range.
    std::string session_key(const std::string& token);

    class login_throttle
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le frein aux essais répétés de mot de passe, par profil : après 5 échecs de suite,
                chaque nouvel essai attend 30 secondes, puis le double à chaque échec (une heure au
                plus).  Un succès remet le compte à zéro.  En mémoire seulement : un redémarrage du
                service le remet à zéro, ce qui reste lent pour qui devine (un redémarrage ne se
                provoque pas depuis la page de connexion).
        !*/
    public:
        // Le nombre de secondes à attendre avant le prochain essai pour ce profil (0 : permis).
        int64_t wait(const std::string& profile, int64_t now);
        void failed(const std::string& profile, int64_t now);
        void succeeded(const std::string& profile);

    private:
        struct entry { int failures = 0; int64_t until = 0; };
        std::mutex m_;
        std::map<std::string, entry> entries_;
    };
}

#endif // GULLIVER_HOST_ACCOUNTS_H_
