// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_ACCOUNTS_H_
#define GULLIVER_HOST_ACCOUNTS_H_

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

    // La longueur permise d'un mot de passe neuf, en octets (UTF-8).  Les mots de passe plus
    // anciens, enregistrés avant que cette règle existe, restent valables à la connexion : seule
    // la création ou le changement d'un mot de passe appelle password_valid.
    constexpr size_t password_min = 20;
    constexpr size_t password_max = 2048;

    /*
        La règle du mot de passe choisi (par « mot de passe oublié » ou dans l'onglet Profils),
        demandée par l'humain (2026-11-26) :
          - entre password_min et password_max octets,
          - au moins une minuscule ASCII (a-z),
          - au moins une majuscule ASCII (A-Z),
          - au moins un chiffre (0-9),
          - au moins un caractère spécial : tout octet ASCII imprimable qui n'est ni une lettre,
            ni un chiffre, ni un espace ; tout octet au-dessus de 0x7F compte aussi (lettres
            accentuées, symboles, caractères non ASCII).
        Rend "" quand c'est bon ; sinon rend un code court (same que web) : "password_too_short",
        "password_too_long", "password_needs_lowercase", "password_needs_uppercase",
        "password_needs_digit", "password_needs_special".
    */
    std::string password_valid(const std::string& password);

    // L'empreinte d'un mot de passe, avec un sel neuf.
    std::string hash_password(const std::string& password);

    // Vrai quand password correspond à l'empreinte stored (comparaison en temps constant).  Une
    // empreinte vide ou mal formée ne correspond à rien.  N'applique pas password_valid : un mot
    // de passe plus ancien, qui ne respecte pas la nouvelle règle, reste valable à la connexion.
    bool verify_password(const std::string& password, const std::string& stored);

    // Un jeton de session neuf (64 caractères hexadécimaux).
    std::string new_session_token();

    // L'empreinte d'un jeton, telle que la base la range.
    std::string session_key(const std::string& token);

    class login_throttle
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le frein aux essais répétés de mot de passe, par profil (demande de l'humain,
                2026-11-26) : après le 1er échec, le prochain essai attend 10 secondes, puis 30 s,
                1 min, 3 min, 9 min et 10 min après les suivants ; au 7e échec, le profil est bloqué
                jusqu'à ce que son mot de passe soit remis (lien du mail ou administrateur).  Un
                succès remet le compte à zéro.  En mémoire seulement : un redémarrage du service le
                remet à zéro (il ne se provoque pas depuis la page de connexion).
        !*/
    public:
        static constexpr int block_after = 7;
        // Le nombre de secondes à attendre avant le prochain essai pour ce profil (0 : permis) ;
        // un profil bloqué attend toujours.
        int64_t wait(const std::string& profile, int64_t now);
        // Note un échec ; vrai quand cet échec bloque le profil (une seule fois).
        bool failed(const std::string& profile, int64_t now);
        void succeeded(const std::string& profile);

    private:
        struct entry { int failures = 0; int64_t until = 0; bool blocked = false; };
        std::mutex m_;
        std::map<std::string, entry> entries_;
    };

    class login_codes
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les codes de connexion à 8 chiffres envoyés par mail (fork pour macOS, 2026-11-23) :
                après un bon mot de passe, la page reçoit un « défi » (32 octets au hasard, en
                hexadécimal) et la personne tape le code reçu.  Un code vaut 10 minutes et 5 essais ;
                un nouveau code pour le même profil remplace l'ancien.  Ne sont gardés que le SHA-256
                du défi et celui du code, en mémoire seulement : un redémarrage les efface (il suffit
                de se reconnecter).  pending_email, quand il n'est pas vide, est l'adresse que le code
                vérifie avant qu'elle soit rangée (la première adresse de l'administrateur).
        !*/
    public:
        static constexpr int64_t lifetime = 600;
        static constexpr int tries = 5;

        // Un code neuf pour profile : rend le défi et met le code dans code.
        std::string issue(const std::string& profile, const std::string& pending_email, int64_t now, std::string& code);

        enum result { accepted, wrong, unknown };
        /*!
            ensures
                - accepted : le code est le bon ; profile et pending_email sont remplis et le défi
                  est retiré (un code ne sert qu'une fois).
                - wrong : le code est faux ; left dit combien d'essais il reste (le défi est retiré
                  quand il n'en reste plus).
                - unknown : pas de défi valable (inconnu, expiré ou épuisé).
        !*/
        result check(const std::string& challenge, const std::string& code, int64_t now, std::string& profile,
            std::string& pending_email, int& left);

    private:
        struct entry { std::string profile, pending_email, code_key; int64_t expires = 0; int left = 0; };
        void prune(int64_t now);
        std::mutex m_;
        std::map<std::string, entry> entries_;   // SHA-256 du défi -> code
    };

    class reset_links
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les liens « mot de passe oublié » (et de la première connexion) : un jeton de 32
                octets au hasard, mis dans le lien du mail, qui permet une fois, pendant 30 minutes,
                de choisir le mot de passe d'un profil.  Un nouveau lien pour le même profil remplace
                l'ancien.  Seul le SHA-256 du jeton est gardé, en mémoire seulement.
        !*/
    public:
        static constexpr int64_t lifetime = 1800;

        std::string issue(const std::string& profile, int64_t now);
        // Le profil du jeton, et le jeton retiré ; false quand il est inconnu ou expiré.
        bool consume(const std::string& token, int64_t now, std::string& profile);
        // Retire les liens d'un profil (son mot de passe vient de changer autrement).
        void forget(const std::string& profile);

    private:
        struct entry { std::string profile; int64_t expires = 0; };
        std::mutex m_;
        std::map<std::string, entry> entries_;
    };

    class mail_limiter
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le frein aux mails : un mail par minute au plus vers une même adresse, et 30 par
                heure au plus pour tout le service, pour que la page « mot de passe oublié » ne
                serve jamais à inonder une boîte.  En mémoire seulement.
        !*/
    public:
        // Vrai quand un mail vers address peut partir maintenant ; le note alors.
        bool allow(const std::string& address, int64_t now);

    private:
        std::mutex m_;
        std::map<std::string, int64_t> last_;
        std::vector<int64_t> sent_;
    };
}

#endif // GULLIVER_HOST_ACCOUNTS_H_
