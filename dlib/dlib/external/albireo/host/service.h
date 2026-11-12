// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_SERVICE_H_
#define ALBIREO_HOST_SERVICE_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../common/json.h"
#include "../common/platform.h"
#include "engine_host.h"
#include "models.h"
#include "settings.h"
#include "store.h"

namespace httplib { class SSLServer; class Server; }

namespace albireo
{

    // Un moteur utilisable et, s'il en offre plusieurs, le périphérique choisi.  key sert
    // à retrouver ses mesures.
    struct engine_choice
    {
        const engine_candidate* candidate = nullptr;
        std::string device;
        std::string key;
    };

    /*!
        ensures
            - Renvoie un candidat utilisable selon les réglages engine et accelerator.
              "best" choisit le candidat le plus rapide parmi ceux mesurés ; "auto" choisit
              parmi les candidats disponibles et revient au processeur si aucun accélérateur
              ne convient.
            - Le candidat renvoyé est nul quand aucun moteur n'est utilisable.
    !*/
    engine_choice choose_engine(const std::vector<engine_candidate>& candidates, const host_settings& s);

    /*!
        ensures
            - Renvoie les choix de moteur disponibles dans les candidats.
    !*/
    std::vector<engine_choice> engine_options(const std::vector<engine_candidate>& candidates);
    bool is_openvino_variant(const std::string& variant);

    class service
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le service Albireo : le moteur et ses modèles, le serveur HTTPS local avec
                l'application web, l'interface compatible OpenAI et l'interface
                d'administration de l'application web, le catalogue et les
                téléchargements des modèles.

                run() démarre tout et bloque jusqu'à stop() ou une demande de redémarrage ;
                l'appelant (la console) démarre alors un nouveau service quand
                restart_requested() le dit, et c'est ainsi que s'applique un changement de
                port ou de moteur.

                Le serveur répond dès que son port est ouvert.  Les moteurs sont décrits et
                celui choisi démarre en arrière-plan, puis les modèles actifs se chargent :
                jusque-là, starting_phase() nomme l'étape en cours, l'état le dit, et les
                requêtes pour un modèle répondent 503 avec le code "starting", pour que
                l'application web et les clients distinguent un démarrage lent d'un échec.

            RÉSEAU
                Le catalogue et les téléchargements tournent sur des threads à eux, chaque
                requête bornée dans le temps, pour que le travail local (les modèles, les
                imports, les requêtes des utilisateurs) n'attende jamais le réseau.  Avec le
                réglage "offline", aucune requête ne quitte la machine : le catalogue et les
                modèles viennent de dossiers, le dossier d'import du dossier des données ou
                un dossier donné à l'interface d'import (import_folder()).
        !*/
    public:
        service();
        ~service();

        // Démarre et sert jusqu'à l'arrêt ; renvoie false quand il n'a pas pu démarrer.
        bool run(std::string& error);
        void stop();
        bool stopping() const { return stopping_; }
        bool restart_requested() const { return restart_; }
        void request_restart();

        // Les programmes et les données.
        std::string program_dir() const { return program_dir_; }
        std::string data_dir() const { return data_dir_; }
        // Les documents du Mac que l'application web joint : <data>/documents.
        std::string documents_dir() const { return join_path(data_dir_, "documents"); }
        int port() const { return port_; }

        // Ce dont les interfaces ont besoin.
        settings_store& settings() { return *settings_; }
        store& db() { return *db_; }
        model_manager& models() { return *models_; }
        engine_host& engine() { return engine_; }

        json status();
        json engines();
        // L'étape du démarrage en cours : "engines" (décrits, puis celui choisi démarre),
        // "models" (les modèles actifs se chargent), ou vide une fois le service prêt.
        std::string starting_phase() const;
        // Vrai pendant que le modèle d'un rôle, ou le modèle nommé, se charge dans le moteur.
        bool model_loading(const std::string& requested);
        // Le modèle du moteur d'une requête : un identifiant, un rôle ou son alias,
        // "background", ou auto.
        std::string resolve_model(const std::string& requested);
        /*!
            ensures
                - Renvoie le modèle du travail que les applications font en arrière-plan
                  (titres, résumés, mémoire) : le modèle de langage livré avec Albireo, qui
                  reste chargé quelles que soient les fonctions, ou vide s'il n'y en a pas,
                  les requêtes pour "background" allant alors au modèle général.
        !*/
        const std::string& background_model() const { return background_model_; }
        /*!
            ensures
                - Renvoie le modèle de chaque fonction : celui choisi pour elle, sinon le modèle
                  général, sinon le modèle de langage livré avec Albireo, pour que les trois
                  fonctions gardent un modèle une fois chaque choix défait ; le rôle d'embedding
                  garde le modèle choisi pour lui.
        !*/
        std::map<std::string, std::string> effective_models(const host_settings& st) const;
        // Le modèle d'embedding d'une requête : un modèle d'embedding chargé nommé par son
        // identifiant, sinon celui qui est actif ("auto", "embedding", "albireo-embedding").
        std::string resolve_embedding_model(const std::string& requested);
        /*!
            ensures
                - Renvoie, pour chaque modèle chargé dans le moteur, {"inputs", "publisher"} :
                  les entrées que le moteur lit pour lui telles que les rapporte son
                  interrogation "models" ("text", et "image" pour un modèle chargé avec son
                  projecteur), "text" seul quand le moteur ne le dit pas, et l'entreprise qui
                  a fait le modèle, d'après son entrée, vide quand elle est inconnue.
        !*/
        std::map<std::string, json> loaded_traits();
        /*!
            ensures
                - Donne la fonction role au modèle id, en le chargeant si besoin.  Le modèle
                  qu'utilisait la fonction auparavant quitte le moteur une fois ses requêtes
                  servies, sauf si une autre fonction l'utilise.  Un modèle activé pendant
                  qu'il attend de partir reste chargé.
        !*/
        bool activate(const std::string& id, const std::string& role, user_error& error);
        /*!
            ensures
                - Retire toutes ses fonctions au modèle id et le décharge une fois ses
                  requêtes servies.  Le modèle de la recherche dans les documents garde sa
                  fonction : la recherche a toujours son modèle d'embedding, qu'un autre
                  modèle d'embedding remplace par activate().
        !*/
        bool deactivate(const std::string& id, user_error& error);
        /*!
            ensures
                - Retire toutes ses fonctions au modèle id, la recherche dans les documents
                  comprise, et supprime son fichier.  Un modèle dans le moteur est supprimé
                  une fois ses requêtes servies et sorti du moteur : pending vaut alors true,
                  et le modèle reste listé, à l'état "removing", jusque-là.
                - Un modèle livré avec Albireo est refusé (code "builtin_model") : il laisse
                  ses fonctions à d'autres modèles et reste installé.
        !*/
        bool remove_model(const std::string& id, user_error& error, bool& pending);
        // Les modèles qui attendent leurs requêtes avant de partir : id -> "unloading" ou
        // "removing".
        json leaving_models();
        // Charge dans le moteur les modèles des rôles des réglages.
        void load_active_models();

        /*!
            ensures
                - Installe ce que contient path, pour la route des imports par chemin. Le
                  service lit avec les droits de son propre compte. path n'est accepté que
                  dans deux cas :
                    - un dossier ou un fichier du dossier d'import du dossier des données :
                      son catalogue,
                      ses modèles (*.gguf, dans le dossier et dans son sous-dossier "models"),
                      comme scan_inbox() les installe, les fichiers restant en place ;
                    - un paquet hors ligne sur un disque local : un dossier qui contient un
                      catalog.json (non signé).  Son catalogue et les modèles
                      que liste ce catalogue (trouvés par leurs noms, tailles et empreintes,
                      model_manager::import_bundle()) sont installés ; aucun autre fichier du
                      dossier n'est lu.
                  Chaque fichier est lu sans passer par aucun lien.  Un fichier de modèle de
                  l'utilisateur va au service par la route d'envoi, lu avec les droits de
                  l'utilisateur.
                - Renvoie {"imported", "already", "pending", "errors"} comme
                  model_manager::import_path(), avec "catalog" quand le dossier en contient un.
                - Refuse path, avec refusal rempli et un résultat vide, quand il est relatif
                  (code import_path_relative), sur un partage ou un disque réseau
                  (import_path_network), un périphérique ou un chemin des espaces de noms du
                  système (import_path_device), invalide (import_path_invalid : un flux, un
                  joker, un caractère de contrôle, un nom "." ou ".."), un fichier de modèle
                  hors du dossier d'import (import_use_upload), hors du dossier d'import et
                  qui n'est pas un paquet hors ligne atteint sans passer par aucun lien
                  (import_not_bundle : le même code, que le dossier manque, soit illisible,
                  sans catalog.json ou atteint par un lien, pour que la réponse ne distingue pas
                  ces cas), absent du dossier d'import (import_not_found),
                  atteint par un lien dans le dossier d'import (import_path_link), ou quand le
                  contrôle du dossier d'import échoue (import_folder_insecure). Chaque refus nomme
                  le chemin dans ses arguments ("path").
        !*/
        json import_folder(const std::string& path, user_error& refusal);

        // Applique aussitôt les réglages du réseau : le proxy, et le mode hors ligne, qui
        // met fin aux requêtes en cours.
        void apply_network_settings(const host_settings& s);

        // Compte l'utilisation d'une requête terminée.
        void record_usage(long prompt_tokens, long generated_tokens);

        /*
            Mesure chaque moteur utilisable sur cette machine avec le modèle général actif (ou
            le plus petit installé) et enregistre les mesures dans les réglages.  Une seule
            mesure tourne à la fois : un appel pendant une autre rend la main aussitôt avec le
            code "benchmark_running".  Chaque moteur est mesuré pendant que le moteur du
            service ne sert aucune requête : la mesure attend que ses requêtes se terminent,
            deux minutes au plus, et est refaite une fois quand le service a reçu des requêtes
            entre-temps, pour que les moteurs mesurés ne partagent pas le processeur avec lui.
        */
        json benchmark_engines();

        // Points d'entrée des processus enfants lancés par le service lui-même.
        static int probe_engine(const std::string& path);
        static int benchmark_engine(const std::string& path, const std::string& model, const std::string& config);

    private:
        void background_loop();
        // Décrit les moteurs à côté du service et démarre celui des réglages, un moteur du
        // processeur quand il échoue ; puis engines_ready_ devient vrai.
        void start_engines();
        /*!
            ensures
                - Enregistre les modèles livrés avec Albireo, dans le dossier des programmes.
                  Un nouveau qui prend la place d'un modèle de son genre (un modèle de langage,
                  ou un modèle d'embedding) qui n'est plus livré avec Albireo reçoit les
                  fonctions de ce modèle, comme le fait une mise à jour qui change le modèle
                  livré avec Albireo ; un autre nouveau prend les fonctions qu'aucun modèle ne
                  tient.  Les fonctions d'un modèle parti sans successeur sont libérées.
                - Le premier modèle de langage parmi eux sert le travail en arrière-plan.
        !*/
        void register_builtin_models();
        void ensure_default_model();
        // Journalise pourquoi un modèle par défaut du catalogue n'a pas pu être téléchargé, ce
        // que la fenêtre Models affiche comme l'erreur de son téléchargement jusqu'à ce qu'un
        // téléchargement démarre.
        void note_default_failure(const json& entry, const user_error& error, const std::string& what);
        void scan_inbox();
        // Sur macOS, folder_admin_only() ne vérifie pas les droits et cette fonction ne sécurise pas le dossier.
        bool secure_inbox(std::string& error);
        /*!
            ensures
                - Donne à un modèle qui arrive les fonctions que nomme son entrée et qu'aucun
                  modèle ne tient, et la fonction générale quand elle est libre.  Un modèle du
                  moteur dont les fichiers ont changé, un projecteur qu'on lui a donné par
                  exemple, se recharge une fois ses requêtes servies (retire() avec reload).
        !*/
        void on_installed(const installed_model& m);
        static bool is_embedding(const installed_model& m);
        // Charge un modèle dans le moteur, avec son projecteur quand il en a un (option "projector").
        bool load_model(const installed_model& m, std::string& error);
        /*!
            ensures
                - Décharge le modèle id sur un thread à lui une fois ses requêtes servies
                  (engine_host::unload_model), pour que la fonction qu'il tenait passe aussitôt
                  au modèle qui le remplace.
                - Avec reload, le modèle se recharge depuis son enregistrement une fois sorti
                  du moteur, quand une fonction ou le travail en arrière-plan l'utilise encore ;
                  le modèle du travail en arrière-plan est alors déchargé lui aussi.
        !*/
        void retire(const std::string& id, bool reload = false);
        // Recharge le modèle id après son retrait pour rechargement, quand une fonction ou le travail en arrière-plan
        // l'utilise.
        void reload_model(const std::string& id);
        // Attend que le modèle id finisse de se décharger, s'il est en cours de déchargement.
        void wait_retired(const std::string& id);
        /*!
            ensures
                - Exécute place, qui renomme de nouveaux fichiers par-dessus ceux du modèle
                  installé id : le modèle est déchargé après ses requêtes pour que place puisse
                  renommer les fichiers ; il se recharge depuis les nouveaux fichiers une fois
                  ceux-ci enregistrés (on_installed()), ou depuis les anciens si place échoue.
                - Renvoie ce que renvoie place ; false sans l'exécuter quand le modèle est en
                  cours de suppression, sert le travail en arrière-plan, ou quand le service
                  s'arrête.
        !*/
        bool replace_model_files(const std::string& id, const std::function<bool()>& place);
        json engine_config(const host_settings& s, const std::string& device) const;
        std::string engines_dir() const;

        std::string program_dir_;
        std::string data_dir_;
        int port_ = 0;
        std::unique_ptr<settings_store> settings_;
        std::unique_ptr<store> db_;
        std::unique_ptr<model_manager> models_;
        engine_host engine_;
        // Écrit une fois par start_engines(), puis lu par les routes une fois engines_ready_
        // vrai.
        std::vector<engine_candidate> candidates_;
        engine_choice choice_;
        std::string engine_error_;
        // Fixé par register_builtin_models() avant le démarrage du serveur, puis seulement lu.
        std::string background_model_;
        std::atomic<bool> engines_ready_{false};
        std::atomic<bool> models_ready_{false};
        // Fixé par run() quand une base abîmée a été mise de côté et qu'une nouvelle a commencé.
        bool database_restarted_ = false;

        // Protège le serveur et le gestionnaire de modèles pendant que run() les crée et les
        // libère, pour stop(), qu'un thread du système peut appeler à tout moment.
        std::mutex server_m_;
        std::unique_ptr<httplib::SSLServer> server_;
        // Le serveur HTTP du réseau local (réglages lan, lan_port), et son thread.
        std::unique_ptr<httplib::Server> lan_server_;
        std::thread lan_thread_;
        void start_lan_server(const host_settings& st);

        std::atomic<bool> stopping_{false};
        std::atomic<bool> restart_{false};
        std::mutex m_;
        std::condition_variable cv_;
        std::thread background_;
        std::thread restarter_;
        std::mutex models_m_;   // sérialise les activations et les chargements
        std::mutex import_m_;   // sérialise les imports de dossiers
        std::mutex benchmark_m_;   // une seule mesure des moteurs à la fois
        // modèle -> "loading", "ready", "unloading", "removing" ou l'erreur
        std::map<std::string, std::string> load_state_;
        // Les modèles qui quittent le moteur, chacun sur un thread de retirers_, et ceux dont
        // le fichier est supprimé une fois qu'ils sont partis.
        std::mutex retire_m_;
        std::condition_variable retire_cv_;
        std::set<std::string> retiring_;
        std::set<std::string> removing_;
        // Les modèles qui se rechargent une fois sortis du moteur (retire() avec reload).
        std::set<std::string> reloading_;
        std::vector<std::thread> retirers_;
        // Les retirers qui ont fini, rejoints par le prochain retrait.
        std::vector<std::thread::id> ended_retirers_;
        int64_t started_at_ = 0;
        std::atomic<long long> usage_prompt_{0};
        std::atomic<long long> usage_generated_{0};
        std::atomic<long long> usage_requests_{0};
    };

    /*
        Installe les routes sur un serveur (server.cpp) : le serveur HTTPS du Mac, ou, avec lan,
        le serveur HTTP du réseau local, qui demande le mot de passe de l'accès réseau, quand il y
        en a un (un cookie que pose sa route de connexion), avant toute autre route.
    */
    void install_routes(httplib::Server& server, service& svc, bool lan = false);

    // Indique si le serveur peut ouvrir port sur l'interface de bouclage, ou sur toutes les
    // adresses avec every_address, avec les options de sa propre socket ; le port est refermé
    // aussitôt.
    bool port_available(int port, bool every_address = false);

}

#endif // ALBIREO_HOST_SERVICE_H_
