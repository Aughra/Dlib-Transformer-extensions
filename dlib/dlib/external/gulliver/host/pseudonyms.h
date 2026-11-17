// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_PSEUDONYMS_H_
#define GULLIVER_HOST_PSEUDONYMS_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../common/json.h"

namespace httplib { class Client; }

/*
    La pseudonymisation du relais (relay.h) : les textes qui partent vers un moteur externe
    passent par Pseudalis, le service d'anonymisation de l'ordinateur, qui remplace chaque valeur
    personnelle par un substitut de même forme, ou par une étiquette qui la porte chiffrée.  La
    table des substituts et de leurs valeurs ne vit que dans la mémoire du service : elle n'est
    jamais écrite, et les réponses du moteur externe y retrouvent leurs valeurs au fil de leur
    flux.
*/

namespace gulliver
{

    class cancellable
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'annulation d'une réponse du relais : son drapeau, et le client de la connexion
                que tient la réponse, vers Pseudalis ou vers le moteur externe, que cancel()
                arrête depuis un thread à lui, puisque httplib attend qu'une connexion en cours
                d'ouverture soit ouverte avant de l'arrêter : aucun appelant n'attend.
        !*/
    public:
        bool cancelled() const { return cancelled_; }
        const std::atomic<bool>& flag() const { return cancelled_; }
        void cancel();
        // Le client de la connexion qui commence, arrêté aussitôt quand la réponse est déjà annulée.
        void hold(const std::shared_ptr<httplib::Client>& c);
        void release();

    private:
        std::atomic<bool> cancelled_{false};
        std::mutex m_;
        std::shared_ptr<httplib::Client> client_;
    };

    class pseudalis_client
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Pseudalis tel que le relais le joint, quand il tourne sur le même Mac : son
                dossier de données (PSEUDALIS_HOME, sinon ~/.local/share/pseudalis, fork pour
                macOS), le port de son settings.json (8444 par défaut) et son autorité locale
                (certificates/pseudalis-authority.crt), qui vérifie son certificat, joint en
                127.0.0.1 sous le nom localhost.  Le dossier, son dossier de certificats,
                l'autorité et les réglages doivent appartenir au compte du service ou à root, sans
                droit d'écriture pour les autres comptes, ni par leurs permissions ni par une ACL
                (owned_by_system()), pour qu'aucun autre compte ne place un service à lui sur le
                chemin des textes.  L'état trouvé est gardé une minute, vérifié de
                nouveau par un seul appelant à la fois, hors du verrou de l'état, les autres
                appelants attendant son résultat.
        !*/
    public:
        /*
            {"found", "port", "reason", "code"} : code nomme la raison pour laquelle Pseudalis n'est pas joint,
            not_installed, no_authority (son autorité locale manque), unprotected (son dossier pourrait être écrit
            par un autre compte) ou failed, et il est vide quand Pseudalis est trouvé.
        */
        json status();

        /*!
            ensures
                - Donne dans out le texte protégé par Pseudalis (/v1/text/anonymize, avec
                  "session": true) : chaque valeur personnelle remplacée par son pseudonyme, sous
                  la clé de séance de Pseudalis (fork pour macOS, Pseudalis 3.1.0).  tags est
                  ignoré : Pseudalis n'écrit plus d'étiquettes.
                - Donne dans values chaque pseudonyme écrit dans out avec la valeur qu'il
                  représente, telle que la réponse de Pseudalis la donne (values), dans l'ordre
                  de out.  Un Pseudalis qui ne donne pas ces valeurs (antérieur à 3.1.0) fait
                  échouer la protection.
                - Attend Pseudalis tant qu'il est occupé à un autre travail, jusqu'à ce que
                  cancel, s'il est donné, soit annulé, ce qui arrête aussi la requête en cours.
                - Renvoie false avec error quand Pseudalis n'est pas trouvé, ne répond pas ou
                  refuse le texte : le texte ne part alors jamais.
        !*/
        bool protect(const std::string& text, bool tags, cancellable* cancel, std::string& out,
            std::vector<std::pair<std::string, std::string>>& values, std::string& error);

        // Le dossier de données de Pseudalis sur cet ordinateur, vide quand il n'en a pas.
        static std::string pseudalis_data_dir();

        /*!
            Réservé aux tests (tests/test_host.cpp).  Aucun réglage ni aucune route n'y mène.
            ensures
                - Envoie les requêtes à origin ("http://127.0.0.1:port"), où un serveur de test
                  répond comme Pseudalis ; une origine vide les rend à Pseudalis.
        !*/
        static void endpoint_for_tests(const std::string& origin);

    private:
        struct endpoint
        {
            std::string origin;     // "https://localhost:port", ou l'origine des tests
            std::string ca;         // l'autorité de Pseudalis
        };
        bool reach(endpoint& e, std::string& error);
        static bool probe(endpoint& e, std::string& reason);
        bool post(const endpoint& e, const std::string& path, const json& body, cancellable* cancel, json& answer,
            std::string& error);

        std::mutex m_;
        std::condition_variable checked_cv_;
        bool checking_ = false;
        int64_t checked_ = 0;
        bool found_ = false;
        endpoint endpoint_;
        std::string reason_;
    };

    class pseudonym_memory
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La mémoire des protections du relais, tenue par le service seul et jamais
                écrite : pour chaque texte protégé, par le SHA-256 du texte et de son mode, sa
                forme protégée et les substituts qu'il contient, pour que les messages d'une
                discussion, renvoyés à chacun de ses tours, ne passent par Pseudalis qu'une
                fois ; pour chaque substitut, sa valeur.

                Les deux tables sont bornées, en entrées et en octets, les entrées les moins
                récemment utilisées partant les premières quand une nouvelle a besoin de place,
                et elles oublient les entrées restées inutilisées pendant max_idle secondes.  Un
                texte dont les substituts sont partis est protégé de nouveau.  Les valeurs
                oubliées sont effacées de la mémoire avant que leur place ne soit libérée.
        !*/
    public:
        struct limits
        {
            size_t texts = 4096;
            size_t text_bytes = size_t(32) << 20;
            size_t values = 16384;
            size_t value_bytes = size_t(4) << 20;
            int64_t max_idle = 7200;        // secondes
        };

        pseudonym_memory();
        explicit pseudonym_memory(const limits& l);
        ~pseudonym_memory();
        pseudonym_memory(const pseudonym_memory&) = delete;
        pseudonym_memory& operator=(const pseudonym_memory&) = delete;

        /*!
            ensures
                - Donne dans out la forme protégée de text, depuis la mémoire ou depuis client,
                  et ajoute à values chaque substitut de out avec sa valeur.
                - Renvoie false avec error quand client échoue.
        !*/
        bool protect(const std::string& text, bool tags, pseudalis_client& client, cancellable* cancel,
            std::string& out, std::map<std::string, std::string>& values, std::string& error);

        // Oublie tout, les valeurs effacées.
        void clear();

        // {"texts", "values", "bytes"}
        json stats();

        // Réservé aux tests : l'heure de la mémoire, en secondes, 0 rendant l'horloge.
        void set_time_for_tests(int64_t t);

    private:
        struct text_entry
        {
            std::string out;
            std::vector<std::string> tokens;
            size_t bytes = 0;
            std::list<std::string>::iterator lru;
            int64_t used = 0;
        };
        struct value_entry
        {
            std::string value;
            size_t bytes = 0;
            std::list<std::string>::iterator lru;
            int64_t used = 0;
        };
        int64_t now() const;
        // Oublie les entrées restées inutilisées pendant max_idle secondes, puis les plus anciennes au-delà des
        // bornes ; m_ est tenu.
        void trim();
        void drop_text(std::map<std::string, text_entry>::iterator it);
        void drop_value(std::map<std::string, value_entry>::iterator it);

        limits limits_;
        std::mutex m_;
        std::map<std::string, text_entry> texts_;
        std::list<std::string> text_lru_;           // la plus récente d'abord
        size_t text_bytes_ = 0;
        std::map<std::string, value_entry> values_;
        std::list<std::string> value_lru_;
        size_t value_bytes_ = 0;
        int64_t time_for_tests_ = 0;
    };

    class stream_restorer
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les valeurs d'une réponse remises à la place de leurs substituts au fil du flux
                de la réponse.  feed() rend le texte d'un morceau jusqu'aux substituts que sa fin
                peut commencer, et que le morceau suivant complète ; finish() rend le reste.

                Un substitut est trouvé entier, le plus long d'abord, et seulement entre des
                limites de mots quand il commence ou finit par une lettre ou un chiffre, pour
                qu'un substitut court ne remplace jamais une partie d'un autre mot.  Les mots
                d'un substitut de plusieurs mots sont aussi trouvés seuls, chacun rendant le mot
                de sa valeur à la même place, quand ils ont quatre octets ou plus et ne
                représentent qu'un mot d'une seule valeur : une réponse qui nomme une personne
                par son seul nom de famille retrouve le nom de famille.  Un substitut entier
                garde sa valeur quand il est aussi un mot d'un autre substitut.  Le texte d'une
                étiquette (son corps, entre ':' et '#') est trouvé comme l'étiquette l'est.
        !*/
    public:
        explicit stream_restorer(const std::map<std::string, std::string>& values);
        ~stream_restorer();
        stream_restorer(const stream_restorer&) = delete;
        stream_restorer& operator=(const stream_restorer&) = delete;

        std::string feed(const std::string& chunk);
        std::string finish();

    private:
        std::string run(bool end);
        void add(const std::string& token, const std::string& value, bool whole);

        // Les tokens, chacun avec sa valeur ; by_byte_ les range par leur premier octet, le plus long d'abord.
        std::vector<std::pair<std::string, std::string>> tokens_;
        std::vector<std::vector<size_t>> by_byte_;
        std::map<std::string, size_t> index_;
        std::vector<bool> ambiguous_, whole_;
        std::string pending_;
        std::string tail_;                          // les derniers octets de la source rendus
    };

}

#endif // GULLIVER_HOST_PSEUDONYMS_H_
