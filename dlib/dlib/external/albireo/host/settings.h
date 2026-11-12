// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_SETTINGS_H_
#define ALBIREO_HOST_SETTINGS_H_

#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "../common/json.h"

namespace albireo
{

    // Les rôles pour lesquels un modèle de langage peut être activé : un modèle au plus par rôle.
    extern const char* const model_roles[3];   // "general", "code", "security"
    // Le rôle du modèle d'embedding, qui calcule les vecteurs de la recherche dans les documents
    // de l'application web ; il y en a toujours un d'actif.
    extern const char* const embedding_role;   // "embedding"

    // Le texte qui tient lieu du mot de passe du proxy dans settings.json et dans les réponses de
    // l'interface d'administration ; settings_store garde le mot de passe lui-même à part.
    extern const char* const proxy_password_mask;   // "********"
    // Le réglage du proxy avec son mot de passe, s'il en a un, remplacé par proxy_password_mask.
    std::string masked_proxy(const std::string& proxy);
    // Indique si le mot de passe d'un réglage de proxy est proxy_password_mask.
    bool proxy_password_masked(const std::string& proxy);

    struct host_settings
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les réglages du service, gardés dans settings.json dans le dossier des
                données.  L'application web les modifie (son onglet Service), et le service
                les applique, en redémarrant quand un changement l'exige (les ports, le GPU,
                les réglages de mémoire).
        !*/
        int port = 8443;
        std::string models_dir;                 // vide : <data>/models
        std::string language = "auto";          // auto, en, fr, de, it, es
        std::string engine = "auto";            // auto : moteur CPU construit pour le Mac
        std::string accelerator = "auto";       // auto : le GPU du Mac (Metal), none : le processeur seul
        long long device_mb = 0;                // copies des poids sur le périphérique, 0 : automatique
        int threads = 0;                        // 0 : cœurs physiques
        long context = 0;                       // 0 : automatique par modèle
        int max_sequences = 4;                  // requêtes calculées ensemble par modèle
        long long kv_mb = 0;                    // 0 : automatique
        std::string kv_type = "auto";           // clés et valeurs : auto (8 bits), q8 ou f16
        bool warm_history = true;               // l'historique d'une réponse lu avant la question suivante
        int spin_us = 2000;                     // l'attente des threads entre deux passes, en microsecondes
        int gpu_prompt_tokens = 24;             // les prompts d'autant de tokens ou plus vont au GPU...
        int gpu_prompt_context = 262144;          // ...s'ils finissent en deçà d'autant de positions (0 : jamais)
        std::map<std::string, std::string> active;   // rôle -> identifiant du modèle
        bool default_model = true;              // télécharger le modèle par défaut du catalogue au premier démarrage
        int reserve_gb = 200;                   // la réserve des modèles non installés, en Go (0 : rien de plus)
        // Une machine sans accès à Internet : aucune requête ne quitte la machine, le catalogue
        // et les modèles viennent de fichiers (dossier d'import, supports amovibles).
        bool offline = false;
        std::string proxy;                      // "" : le proxy du système, "none", ou http://host:port
        /*
            L'accès au web du bouton Web (web_access.h) : les routes par lesquelles l'application
            web cherche sur le web et lit ses pages, coupées par web_access = false.  web_search
            nomme le fournisseur de la recherche : "wikipedia" ou "none".
        */
        bool web_access = true;
        std::string web_search = "wikipedia";
        json engine_benchmarks = json::object();     // variante -> tokens par seconde mesurés
        /*
            Le choix de l'installation pour l'autorité locale dans les racines de confiance de
            la machine : 1 approuvée, 0 non, -1 inconnu (une installation antérieure à 1.0.8),
            où une nouvelle autorité suit la confiance de celle qu'elle remplace
            (certificates.h).
        */
        int trust_authority = -1;
        /*
            L'accès depuis le réseau local : un second serveur, en HTTP simple, sur chaque adresse
            du Mac au port lan_port, qui demande le mot de passe (lan_password_hash, sous la
            forme de password_hash()) avant toute chose quand il y en a un, et qui est ouvert au
            réseau sinon.
        */
        bool lan = false;
        int lan_port = 8444;
        std::string lan_password_hash;

        /*
            Les réglages tels que settings.json et l'interface d'administration les donnent, le
            mot de passe du proxy masqué.  from_json() lit sans erreur les documents des
            versions antérieures : web_search "auto", "brave" ou "searxng" devient
            "wikipedia", et les réglages qu'elles avaient et que cette version n'a pas
            (public_link, web_search_url, web_search_key, web_search_key_set) sont laissés de
            côté.
        */
        json to_json() const;
        void from_json(const json& j);
        std::string effective_models_dir() const;
    };

    class settings_store
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le fichier des réglages, lu au démarrage et écrit de façon atomique à chaque
                changement, avec une copie, settings.json.bak, écrite avec lui et à chaque
                démarrage qui lit un settings.json valide.  La copie est lue à sa place quand
                settings.json n'est plus du JSON valide (un disque défaillant, une modification
                à la main), puis réécrite.

                settings.json est lisible par les utilisateurs de la machine, et donne le mot
                de passe du proxy sous la forme proxy_password_mask. Le proxy avec son mot de
                passe est gardé à côté, dans proxy.secret, lisible par le compte du service
                seul. Un mot de passe trouvé dans settings.json, écrit par
                une version antérieure ou à la main, passe aussitôt dans proxy.secret.

                Un settings.json d'une version antérieure qui contient des réglages que cette
                version n'a pas, ou un autre fournisseur de recherche, est réécrit au démarrage
                sans eux, et web_search.secret, la clé d'un fournisseur de recherche de ces
                versions, est supprimé.
        !*/
    public:
        explicit settings_store(const std::string& path);

        host_settings get() const;
        bool set(const host_settings& s);
        /*!
            ensures
                - Applique change aux réglages actuels et les écrit, en une seule étape : deux
                  changements faits en même temps, par une route et une tâche de fond par
                  exemple, sont conservés tous les deux.
                - Renvoie les réglages écrits.
        !*/
        host_settings update(const std::function<void(host_settings&)>& change);
        const std::string& path() const { return path_; }

    private:
        // Écrit proxy.secret, puis settings.json ; m_ est tenu.
        bool write();

        std::string path_;
        mutable std::mutex m_;
        host_settings s_;
    };

}

#endif // ALBIREO_HOST_SETTINGS_H_
