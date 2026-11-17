// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_RELAY_H_
#define GULLIVER_HOST_RELAY_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/json.h"
#include "pseudonyms.h"
#include "settings.h"

/*
    Le relais de l'inférence : le modèle gulliver-relay de l'interface envoie une discussion à un
    moteur externe, que nomment les réglages (relay_settings), et rend sa réponse comme le moteur
    de l'ordinateur rend la sienne.  Le service sert ainsi d'intermédiaire (proxy) entre
    l'application web, ou tout client de son interface, et le moteur externe, dont les clients ne
    voient jamais ni l'adresse ni la clé ; les autres modèles gardent le moteur de l'ordinateur.

    Trois fournisseurs sont connus : « openai », tout moteur à l'interface d'OpenAI (la base de
    son interface dans les réglages, une clé, un utilisateur et un mot de passe, ou rien),
    « ollama », le nuage d'Ollama (https://ollama.com/v1, interface d'OpenAI, une clé), et
    « claude », l'interface d'Anthropic (https://api.anthropic.com/v1/messages, une clé).  Les
    modèles des deux derniers sont connus avec leur fenêtre de contexte, les tokens de leurs
    réponses, leurs images et leur raisonnement ; ceux d'un moteur « openai » viennent des
    réglages.

    Avec la pseudonymisation, les textes d'une requête sortent en passant par Pseudalis
    (pseudonyms.h) : chaque valeur personnelle est remplacée par son substitut, ou par son
    étiquette, et la réponse retrouve les valeurs au fil de son flux.  Les images ne sortent
    alors jamais : un modèle de l'ordinateur qui lit les images décrit chacune d'elles, et la
    description prend sa place, protégée comme le sont les textes.  Il en va de même sans
    pseudonymisation quand le modèle externe ne lit pas les images.
*/

namespace gulliver
{

    // Le modèle de l'interface qui relaie une discussion au moteur externe.
    extern const char* const relay_model_id;   // "gulliver-relay"

    struct relay_model
    {
        std::string id;                     // le modèle tel que le fournisseur le nomme
        std::string name;                   // son nom pour les humains
        long context = 0;                   // tokens de sa fenêtre de contexte
        long max_output = 0;                // tokens d'une réponse au plus, raisonnement compris
        bool images = false;                // lit les images
        /*
            Le raisonnement : « none », « toggle » (allumé ou éteint par la requête), « always »
            (toujours allumé), avec les niveaux d'effort que prend le modèle, s'il y en a, et le
            niveau qu'il prend quand la requête n'en donne aucun.
        */
        std::string thinking = "none";
        std::vector<std::string> efforts;
        std::string default_effort;
    };

    // Les modèles que relay.cpp connaît pour un fournisseur (« claude », « ollama »), les plus récents d'abord ;
    // aucun pour « openai ».
    std::vector<relay_model> known_relay_models(const std::string& provider);

    // Si url peut être la base de l'interface d'un moteur « openai » : http:// ou https://, sans requête.
    bool valid_relay_url(const std::string& url);

    // La base de l'interface du fournisseur de r : url pour « openai », sans son dernier '/'.
    std::string relay_base_url(const relay_settings& r);

    /*!
        ensures
            - Donne dans m le modèle que nomme r, avec ses caractéristiques : celles que relay.cpp
              connaît pour « claude » et « ollama », celles des réglages pour « openai ».
            - Renvoie false quand r ne nomme aucun modèle, ou un modèle que son fournisseur
              n'offre pas ici.
    !*/
    bool relay_model_of(const relay_settings& r, relay_model& m);

    /*!
        ensures
            - Renvoie true quand r est activé et a ce dont une requête a besoin : un fournisseur
              connu, un modèle, une adresse valide pour « openai », et son secret quand son
              authentification en demande un.  Sinon, why dit ce qui manque.
    !*/
    bool relay_ready(const relay_settings& r, std::string& why);

    /*!
        ensures
            - Renvoie true quand le relais de r peut répondre en mode hors ligne, qui ne laisse à
              portée que cet ordinateur et son réseau local : un moteur « openai » dont
              l'adresse est « localhost » ou un nom sous .localhost, joint en 127.0.0.1, une
              adresse IP du réseau local (boucle locale, privée ou lien local), ou un nom du
              réseau, que chaque requête résout, refuse quand l'une de ses adresses se trouve
              ailleurs, et joint à une adresse qu'elle a vérifiée.  Les fournisseurs « ollama »
              et « claude » ont besoin d'Internet.  Sinon, why le dit.
    !*/
    bool relay_offline_ready(const relay_settings& r, std::string& why);

    // {"providers": [{"id", "name", "url", "auths", "models": [...]}]} : ce que propose l'icône de notification.
    json relay_catalog();

    // Le modèle de r et ses caractéristiques tels que l'interface les donne, sans l'adresse ni le secret.
    json relay_description(const relay_settings& r);

    class relay_service
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les réponses du moteur externe : chaque requête tourne sur un thread à elle,
                max_jobs au plus à la fois, et donne ses événements comme le fait le moteur de
                l'ordinateur (common/engine_api.h) : le texte, le raisonnement, puis la fin de
                la réponse avec sa consommation, ou son erreur.  Le secret ne va que dans les
                en-têtes des requêtes, et jamais dans un journal, une réponse ou une erreur.

                Les requêtes suivent le proxy et l'annulation des téléchargements
                (http_fetch.h), et l'arrêt du service les termine.  En mode hors ligne, qui
                annule les téléchargements, le relais ne joint qu'un moteur « openai » de cet
                ordinateur ou de son réseau local, directement, sans le proxy : chaque adresse
                de son nom est vérifiée avant toute connexion, qui va à l'une d'elles
                (relay_offline_ready()).
        !*/
    public:
        typedef std::function<void(int kind, const std::string& data)> sink_type;
        typedef std::function<host_settings()> settings_source;
        /*
            Décrit une image, une URL de données, avec un modèle de l'ordinateur qui lit les
            images : renvoie false avec error quand aucun ne le peut, ou une fois que cancelled
            passe à true.
        */
        typedef std::function<bool(const std::string& data_url, const std::atomic<bool>& cancelled,
            std::string& description, std::string& error)> image_describer;

        relay_service(settings_source settings, image_describer describe);
        ~relay_service();
        relay_service(const relay_service&) = delete;
        relay_service& operator=(const relay_service&) = delete;

        /*!
            requires
                - request est le corps d'une complétion de chat de l'interface d'OpenAI, dont le
                  serveur a vérifié les messages (check_media()).
            ensures
                - Lance la réponse à request avec le moteur externe des réglages ; ses
                  événements vont à sink, depuis un autre thread, jusqu'à la fin ou l'erreur.
                  L'échantillonnage ne passe que sous forme de nombres, et les séquences d'arrêt
                  seulement sans pseudonymisation ; les réponses de Claude prennent 64000 tokens
                  au plus quand la requête ne donne pas de limite.
                - Renvoie false, avec error, le statut HTTP et le code du refus, quand le relais
                  refuse la requête d'emblée : éteint ou incomplet, un moteur hors de portée du
                  mode hors ligne, trop de réponses en cours, des outils, ou plusieurs choix.
        !*/
        bool submit(const json& request, sink_type sink, uint64_t& id, std::string& error, int& status,
            std::string& code);

        // Termine la réponse id, dont le dernier événement suit.
        void cancel(uint64_t id);

        // Termine les réponses en cours et attend leurs threads ; submit() refuse ensuite.
        void stop();

        /*!
            ensures
                - Demande au moteur externe de r ses modèles, ce qui dit si son adresse et son
                  secret sont bons : {"ok", "models": [...]}, avec "url" quand l'interface d'un
                  moteur « openai » répond à une autre base que l'adresse saisie (avec /v1, ou
                  sans le chemin d'une opération), ou {"ok": false, "error", "code"}, le code
                  nommant l'échec pour l'icône de notification ("relay_address",
                  "relay_http_key", ...), avec "args": {"s": status} et "detail", le message du
                  moteur, pour un statut HTTP.  Les réglages de r valent tels qu'ils sont, au
                  masque du secret près, que l'appelant remplace par le secret enregistré.
        !*/
        json test(const relay_settings& r);

        // L'état de la pseudonymisation : {"found", "reason", "memory": {...}}.
        json pseudonymization_status();

        // Oublie les valeurs gardées par la pseudonymisation (pseudonym_memory) et les descriptions des images.
        void forget_pseudonyms();

        static const size_t max_jobs = 8;

    private:
        struct job;
        void run(const std::shared_ptr<job>& j);
        /*
            La base de l'interface du fournisseur de r : pour « openai », l'adresse saisie, ou la
            même avec /v1 quand c'est seulement là qu'une liste de modèles répond, trouvée une
            fois par adresse et gardée ; un moteur qui répond sans lister ses modèles est
            interrogé de nouveau après unlisted_seconds, un moteur hors de portée à la requête
            suivante.  La connexion de la recherche est tenue par control.
        */
        std::string base_of(const relay_settings& r, cancellable* control);
        // Garde base pour l'adresse saisie, jusqu'à l'instant until (secondes Unix), 0 pour toute la durée du service.
        void remember_base(const std::string& typed, const std::string& base, int64_t until);
        // Rejoint les threads des réponses terminées ; jobs_m_ est tenu.
        void reap();

        settings_source settings_;
        image_describer describe_;
        pseudalis_client pseudalis_;
        pseudonym_memory memory_;

        std::mutex jobs_m_;
        std::list<std::shared_ptr<job>> jobs_;
        uint64_t next_id_ = 1;
        bool stopped_ = false;

        // Les bases trouvées pour les adresses des moteurs « openai », max_bases au plus, avec la fin de leur usage.
        struct known_base
        {
            std::string base;
            int64_t until = 0;
        };
        std::mutex bases_m_;
        std::map<std::string, known_base> bases_;
        static const size_t max_bases = 32;
        static const int unlisted_seconds = 600;

        // Les descriptions des images, par le SHA-256 de leur URL de données, max_descriptions au plus.
        std::mutex descriptions_m_;
        std::list<std::pair<std::string, std::string>> descriptions_;
        static const size_t max_descriptions = 64;
    };

    /*!
        Réservé aux tests (tests/test_host.cpp).  Aucun réglage ni aucune route n'y mène.
        ensures
            - Envoie les requêtes du fournisseur « claude » ou « ollama » à origin
              ("http://127.0.0.1:port"), où un serveur de test répond comme le fournisseur ; une
              origine vide leur rend leur adresse.
    !*/
    void relay_origin_for_tests(const std::string& provider, const std::string& origin);

    /*!
        Réservé aux tests (tests/test_host.cpp).  Aucun réglage ni aucune route n'y mène.
        ensures
            - Le mode hors ligne résout name, en minuscules, en addresses plutôt que par le
              résolveur du système ; des adresses vides rendent le nom au résolveur.
    !*/
    void relay_name_for_tests(const std::string& name, const std::vector<std::string>& addresses);

}

#endif // GULLIVER_HOST_RELAY_H_
