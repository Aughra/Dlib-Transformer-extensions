// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.


#include <dlib/external/httplib/httplib.h>

#include "service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>

#include "../common/builtin_models.h"
#include "../common/engine_loader.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/system_info.h"
#include "../common/version.h"
#include "certificates.h"
#include "crypto.h"
#include "http_fetch.h"
#include "web_access.h"

namespace albireo
{

    namespace
    {
        const int64_t catalog_period = 6 * 3600;
        const int64_t certificate_period = 24 * 3600;
        const int64_t inbox_period = 60;

        // Les erreurs des requêtes auxquelles le service met fin.
        const char* const stopping_reason = "the service stops";
        const char* const offline_reason = "offline mode: no request leaves this computer";

        bool ends_with(const std::string& s, const std::string& suffix)
        {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        bool is_role(const std::string& r)
        {
            for (const char* m : model_roles)
                if (r == m) return true;
            return r == embedding_role;
        }

        // Vrai quand l'entrée du catalogue d'un modèle nomme le rôle parmi ses usages.
        bool has_role(const json& descriptor, const std::string& role)
        {
            for (const auto& r : descriptor["roles"].elements())
                if (r.as_string() == role) return true;
            return false;
        }

        // Les noms d'un chemin entre ses séparateurs.
        std::vector<std::string> path_names(const std::string& p)
        {
            const char* const separators = "/";
            std::vector<std::string> names;
            for (size_t at = 0; at <= p.size();)
            {
                size_t end = p.find_first_of(separators, at);
                if (end == std::string::npos) end = p.size();
                if (end > at) names.push_back(p.substr(at, end - at));
                at = end + 1;
            }
            return names;
        }

        bool same_name(const std::string& a, const std::string& b)
        {
            return a == b;
        }

        /*
            Indique si path, tel qu'écrit, est folder ou se trouve sous lui, en comparant exactement
            les composants ; within reçoit folder suivi des noms de path au-delà de lui. Le système
            de fichiers n'est pas lu.
        */
        bool path_under(const std::string& path, const std::string& folder, std::string& within)
        {
            const std::vector<std::string> p = path_names(path), f = path_names(folder);
            if (f.empty() || p.size() < f.size()) return false;
            for (size_t i = 0; i < f.size(); ++i)
                if (!same_name(p[i], f[i])) return false;
            within = folder;
            for (size_t i = f.size(); i < p.size(); ++i) within = join_path(within, p[i]);
            return true;
        }

        /*
            Le code d'un chemin que les imports par chemin refusent d'après son seul texte, ou une
            chaîne vide : relatif, sur un partage, un périphérique ou un espace de noms du système, ou
            invalide.  Le système de fichiers n'est pas lu, pour qu'il n'en soit rien révélé.
        */
        std::string path_problem(const std::string& p)
        {
            if (p.empty() || p.size() > 4096) return "import_path_invalid";
            for (char c : p)
                if (static_cast<unsigned char>(c) < 0x20) return "import_path_invalid";
            if (p[0] != '/') return "import_path_relative";
            if (p.compare(0, 2, "//") == 0) return "import_path_network";
            for (const auto& n : path_names(p))
                if (n == "." || n == "..") return "import_path_invalid";
            const std::vector<std::string> names = path_names(p);
            if (!names.empty() && (names[0] == "dev" || names[0] == "proc" || names[0] == "sys"))
                return "import_path_device";
            return std::string();
        }

        // Prépare les certificats locaux ; le choix de confiance n'a pas d'effet sur macOS.
        bool prepare_certificates(const std::string& dir, int trust_choice, bool& renewed, std::string& error)
        {
            if (!ensure_certificates(dir, renewed, error)) return false;
            (void)trust_choice;
            return true;
        }

        // Indique si une erreur de SQLite signale une base abîmée, qui ne se rouvre jamais d'elle-même.
        bool damaged_database(const std::string& what)
        {
            return what.find("not a database") != std::string::npos || what.find("malformed") != std::string::npos;
        }

        // Garde de côté une base abîmée sous un nom à elle, avec son journal ; false quand elle ne peut pas
        // être renommée.
        bool set_aside_database(const std::string& db_path, const std::string& what)
        {
            const std::string aside = db_path + ".corrupt-" + std::to_string(now_unix());
            log_error("service: the database " + db_path + " is damaged (" + what + "): it is kept as " + aside
                + ", and a new database records again the models of the models folder");
            if (!rename_file(db_path, aside)) return false;
            for (const char* part : { "-wal", "-shm" })
                if (file_exists(db_path + part)) rename_file(db_path + part, aside + part);
            return true;
        }

        // Les options de la socket sur laquelle le serveur écoute.
        void set_listen_options(httplib::Server& server)
        {
            server.set_socket_options([](socket_t sock) {
                httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
            });
        }
    }

    bool port_available(int port, bool every_address)
    {
        httplib::Server probe;
        set_listen_options(probe);
        const bool ok = probe.bind_to_port(every_address ? "0.0.0.0" : "127.0.0.1", port);
        probe.stop();
        return ok;
    }

    std::vector<engine_choice> engine_options(const std::vector<engine_candidate>& candidates)
    {
        std::vector<engine_choice> out;
        for (const auto& c : candidates)
        {
            if (!c.usable || c.variant != "cpu") continue;
            engine_choice e;
            e.candidate = &c;
            e.key = "cpu";
            out.push_back(e);
        }
        return out;
    }

    engine_choice choose_engine(const std::vector<engine_candidate>& candidates)
    {
        const std::vector<engine_choice> options = engine_options(candidates);
        return options.empty() ? engine_choice() : options.front();
    }

    service::service() : program_dir_(executable_dir()), data_dir_(albireo::data_dir())
    {
    }

    service::~service()
    {
        stop();
        if (background_.joinable()) background_.join();
        if (restarter_.joinable()) restarter_.join();
    }

    std::string service::engines_dir() const
    {
        const std::string d = join_path(program_dir_, "engines");
        return dir_exists(d) ? d : program_dir_;
    }

    json service::engine_config(const host_settings& s, const std::string& device) const
    {
        json c = json::object();
        c["threads"] = s.threads;
        c["context"] = s.context;
        c["max_sequences"] = s.max_sequences;
        c["kv_mb"] = s.kv_mb;
        c["kv_type"] = s.kv_type;
        c["offload_mb"] = s.device_mb;
        // Le GPU du Mac (Metal), sauf si le réglage accelerator dit none.
        c["metal"] = s.accelerator != "none";
        c["gpu_prompt_tokens"] = s.gpu_prompt_tokens;
        c["gpu_prompt_context"] = s.gpu_prompt_context;
        c["warm_history"] = s.warm_history;
        c["spin_us"] = s.spin_us;
        c["device"] = device;
        c["cache_dir"] = join_path(data_dir_, "engine-cache");
        return c;
    }

    bool service::run(std::string& error)
    {
        started_at_ = now_unix();
        if (!make_dirs(data_dir_))
        {
            error = "cannot create the data folder " + data_dir_;
            return false;
        }
        const std::string logs = join_path(data_dir_, "logs");
        make_dirs(logs);
        log_file::instance().open(join_path(logs, "service.log"));
        log_info(std::string(ALBIREO_FULL_NAME) + " " + ALBIREO_VERSION + " starting");
        log_info("programs in " + program_dir_ + ", data in " + data_dir_);

        const std::string settings_path = join_path(data_dir_, "settings.json");
        const bool first_run = !file_exists(settings_path);
        settings_.reset(new settings_store(settings_path));
        const host_settings st = settings_->get();
        if (first_run) settings_->set(st);
        port_ = st.port;
        apply_network_settings(st);
        log_info("web search: " + web_search_provider(st));

        const std::string db_path = join_path(data_dir_, "albireo.db");
        try
        {
            db_.reset(new store(db_path));
        }
        catch (const std::exception& e)
        {
            /*
                Un fichier qui n'est pas une base, ou une base abîmée, ne se rouvre jamais de lui-même : il
                est gardé de côté sous un nom à lui, et une nouvelle base commence, dans laquelle les
                modèles du dossier des modèles sont enregistrés de nouveau (background_loop()).  Les autres
                échecs, un fichier en lecture seule, tenu par un autre programme ou sur un disque plein,
                peuvent passer : le démarrage échoue, et la tentative suivante réessaie.
            */
            const std::string what = e.what();
            if (!damaged_database(what))
            {
                error = "the database cannot be opened: " + what;
                return false;
            }
            if (!set_aside_database(db_path, what))
            {
                error = "the database " + db_path + " is damaged and cannot be set aside";
                return false;
            }
            try
            {
                db_.reset(new store(db_path));
                database_restarted_ = true;
            }
            catch (const std::exception& again)
            {
                error = std::string("the database cannot be opened: ") + again.what();
                return false;
            }
        }


        // Les documents du Mac que l'application web joint.
        if (!dir_exists(documents_dir()) && !make_dirs(documents_dir()))
            log_warning("documents: cannot create " + documents_dir());

        const std::string cert_dir = join_path(data_dir_, "certificates");
        bool renewed = false;
        if (!prepare_certificates(cert_dir, st.trust_authority, renewed, error)) return false;

        /*
            Les modèles et les modèles livrés avec Albireo lisent et écrivent la base : une base
            trouvée abîmée seulement maintenant est mise de côté comme à son ouverture, et l'étape se
            refait une fois sur une nouvelle base.  Tout autre échec fait échouer le démarrage, retenté.
        */
        for (int attempt = 0;; ++attempt)
        {
            try
            {
                std::unique_ptr<model_manager> m(new model_manager(*db_, st.effective_models_dir(),
                    join_path(data_dir_, "reserve")));
                m->on_installed([this](const installed_model& im) { on_installed(im); });
                m->on_replace([this](const std::string& id, const std::function<bool()>& place) {
                    return replace_model_files(id, place);
                });
                {
                    std::lock_guard<std::mutex> lock(server_m_);
                    if (stopping_) m->stop();
                    models_ = std::move(m);
                }
                register_builtin_models();
                break;
            }
            catch (const std::exception& e)
            {
                const std::string what = e.what();
                {
                    std::lock_guard<std::mutex> lock(server_m_);
                    models_.reset();
                }
                if (attempt > 0 || !damaged_database(what))
                {
                    error = "the database cannot be read: " + what;
                    return false;
                }
                db_.reset();
                if (!set_aside_database(db_path, what))
                {
                    error = "the database " + db_path + " is damaged and cannot be set aside";
                    return false;
                }
                try
                {
                    db_.reset(new store(db_path));
                    database_restarted_ = true;
                }
                catch (const std::exception& again)
                {
                    error = std::string("the database cannot be opened: ") + again.what();
                    return false;
                }
            }
        }

        // Les moteurs démarrent en arrière-plan (start_engines()), une fois que le serveur répond.
        // La clé du serveur parvient au serveur TLS depuis la mémoire, déchiffrée ici seulement.
        std::string cert_pem, key_pem;
        if (!read_file(certificate_paths(cert_dir).server_cert, cert_pem) || !read_server_key(cert_dir, key_pem))
        {
            wipe_secret(key_pem);
            error = "the certificate or the key of the local server cannot be read in " + cert_dir;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(server_m_);
            if (stopping_)
            {
                wipe_secret(key_pem);
                return true;
            }
            httplib::SSLServer::PemMemory pem = {};
            pem.cert_pem = cert_pem.c_str();
            pem.cert_pem_len = cert_pem.size();
            pem.key_pem = key_pem.c_str();
            pem.key_pem_len = key_pem.size();
            server_.reset(new httplib::SSLServer(pem));
        }
        wipe_secret(key_pem);
        if (!server_->is_valid())
        {
            error = "the TLS configuration of the server failed";
            return false;
        }
        /*
            Les limites des corps sont celles de chaque route (server.cpp).  Une attente d'un client
            dure 30 secondes au plus, pendant sa poignée de main TLS, sa requête ou l'écriture d'une
            réponse : un client qui cale libère son thread et la place de sa requête dans le moteur.
            Huit threads servent les connexions, jusqu'à 64 quand ils sont occupés, car une réponse
            en flux tient son thread ; 128 connexions au plus en attendent un, et les suivantes sont
            fermées aussitôt, pour qu'un afflux de connexions tienne un nombre borné de sockets.  Une
            connexion gardée entre deux requêtes tient aussi son thread : elle est fermée après
            5 secondes sans requête, pour que les connexions inactives des navigateurs de plusieurs
            utilisateurs laissent les threads aux requêtes.
        */
        server_->set_payload_max_length(0);
        server_->set_keep_alive_timeout(5);
        server_->set_read_timeout(30);
        server_->set_write_timeout(30);
        server_->new_task_queue = []() { return new httplib::ThreadPool(8, 64, 128); };
        set_listen_options(*server_);
        if (!server_->bind_to_port("127.0.0.1", port_))
        {
            /*
                Le port des réglages est pris par un autre programme, ou réservé par le système depuis
                qu'il a été choisi : le serveur ouvre le dernier port sur lequel il a servi, que les
                réglages reprennent pour retrouver le service à ce port.
            */
            int last = 0;
            try
            {
                last = std::atoi(db_->value("server_port").c_str());
            }
            catch (const std::exception& e)
            {
                log_warning(std::string("server: the last port served on cannot be read: ") + e.what());
            }
            // Un bind qui a échoué laisse le serveur refuser le suivant jusqu'à stop().
            if (last != port_ && last >= 1024 && last <= 65535) server_->stop();
            if (last == port_ || last < 1024 || last > 65535 || !server_->bind_to_port("127.0.0.1", last))
            {
                error = "the port " + std::to_string(port_) + " is in use or cannot be opened";
                return false;
            }
            log_warning("server: the port " + std::to_string(port_) + " is in use or cannot be opened, the service "
                "answers on " + std::to_string(last) + ", its last port");
            port_ = last;
            settings_->update([last](host_settings& s) { s.port = last; });
        }
        try
        {
            db_->set_value("server_port", std::to_string(port_));
        }
        catch (const std::exception& e)
        {
            log_warning(std::string("server: the port served on cannot be recorded: ") + e.what());
        }
        install_routes(*server_, *this);
        start_lan_server(settings_->get());
        {
            // Un arrêt venu avant le bind n'a trouvé aucune socket à fermer : celle qui est liée est
            // fermée ici, et listen_after_bind() rend la main aussitôt.
            std::lock_guard<std::mutex> lock(server_m_);
            if (stopping_)
            {
                server_->stop();
                if (lan_server_) lan_server_->stop();
            }
        }
        log_info("server: https://localhost:" + std::to_string(port_) + "/");

        background_ = std::thread([this]() {
            try
            {
                background_loop();
            }
            catch (const std::exception& e)
            {
                log_error("service: the background tasks stopped: " + std::string(e.what()));
            }
            catch (...)
            {
                log_error("service: the background tasks stopped");
            }
            // Un démarrage interrompu par une erreur met tout de même fin à l'état de démarrage.
            engines_ready_ = true;
            models_ready_ = true;
        });
        server_->listen_after_bind();
        const bool stopped = stopping_;

        // Arrêt : les requêtes vers le réseau se terminent d'abord, pour que rien ne les attende,
        // puis les tâches de fond, les téléchargements (leur progression est gardée), le moteur.
        stop();
        if (background_.joinable()) background_.join();
        if (lan_thread_.joinable()) lan_thread_.join();
        std::unique_ptr<model_manager> models;
        {
            std::lock_guard<std::mutex> lock(server_m_);
            server_.reset();
            lan_server_.reset();
            models = std::move(models_);
        }
        models.reset();
        engine_.stop();
        {
            // Les déchargements se terminent avec le moteur.
            std::vector<std::thread> retirers;
            {
                std::lock_guard<std::mutex> lock(retire_m_);
                retirers.swap(retirers_);
            }
            retire_cv_.notify_all();
            for (auto& t : retirers)
                if (t.joinable()) t.join();
        }
        log_info("service stopped");
        if (!stopped)
        {
            error = "the server stopped unexpectedly";
            return false;
        }
        return true;
    }

    void service::stop()
    {
        {
            // Sous le verrou des attentes, pour qu'une attente sur le point de commencer voie le drapeau.
            std::lock_guard<std::mutex> lock(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        {
            std::lock_guard<std::mutex> lock(retire_m_);
        }
        retire_cv_.notify_all();
        std::lock_guard<std::mutex> lock(server_m_);
        // Les téléchargements, les imports et les vérifications s'arrêtent aussitôt ; leur progression est gardée.
        http_cancel_all(stopping_reason);
        if (models_) models_->stop();
        if (server_) server_->stop();
        if (lan_server_) lan_server_->stop();
    }

    /*
        Le serveur du réseau local, en HTTP simple sur chaque adresse du Mac, avec les mêmes routes,
        derrière le mot de passe de l'accès réseau quand il y en a un.  Un échec laisse le service au
        Mac seul, avec un avertissement.
    */
    void service::start_lan_server(const host_settings& st)
    {
        if (!st.lan) return;
        if (st.lan_password_hash.empty())
            log_warning("server: the network access has no password: every device of the network may use the service");
        std::unique_ptr<httplib::Server> lan(new httplib::Server);
        lan->set_payload_max_length(0);
        lan->set_keep_alive_timeout(5);
        lan->set_read_timeout(30);
        lan->set_write_timeout(30);
        lan->new_task_queue = []() { return new httplib::ThreadPool(4, 32, 64); };
        set_listen_options(*lan);
        if (!lan->bind_to_port("0.0.0.0", st.lan_port))
        {
            log_warning("server: the network port " + std::to_string(st.lan_port) + " is in use or cannot be opened");
            return;
        }
        install_routes(*lan, *this, true);
        {
            std::lock_guard<std::mutex> lock(server_m_);
            lan_server_ = std::move(lan);
        }
        log_info("server: http://<address of the Mac>:" + std::to_string(st.lan_port) + "/ for the local network");
        httplib::Server* const raw = lan_server_.get();
        lan_thread_ = std::thread([raw]() { raw->listen_after_bind(); });
    }

    void service::apply_network_settings(const host_settings& s)
    {
        set_http_proxy(s.proxy);
        {
            std::lock_guard<std::mutex> lock(server_m_);
            if (stopping_) return;
            if (s.offline) http_cancel_all(offline_reason);
            else http_restore();
        }
        // Les tâches de fond commencent ou arrêtent aussitôt leur travail réseau.
        cv_.notify_all();
    }

    void service::request_restart()
    {
        if (restart_.exchange(true)) return;
        log_info("service: restart requested");
        // Depuis un gestionnaire du serveur : la réponse est écrite avant l'arrêt du serveur.
        restarter_ = std::thread([this]() {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait_for(lock, std::chrono::milliseconds(300), [this]() { return stopping_.load(); });
            lock.unlock();
            stop();
        });
    }

    void service::start_engines()
    {
        // L'état signale le démarrage jusqu'à la fin de cette fonction, quoi qu'il arrive.
        struct ready_at_exit
        {
            std::atomic<bool>& flag;
            ~ready_at_exit() { flag = true; }
        } ready{ engines_ready_ };
        // Le moteur CPU construit pour cette machine.
        const host_settings st = settings_->get();
        // Les descriptions s'arrêtent aussitôt quand le service s'arrête.
        candidates_ = engine_.discover(engines_dir(), executable_path(), &stopping_);
        choice_ = choose_engine(candidates_);
        if (stopping_) return;
        if (choice_.candidate)
        {
            std::string e;
            if (!engine_.start(*choice_.candidate, engine_config(st, choice_.device), e))
            {
                engine_error_ = choice_.key + ": " + e;
                log_error("engines: " + engine_error_);
                for (const auto& o : engine_options(candidates_))
                {
                    if (engine_.start(*o.candidate, engine_config(st, o.device), e))
                    {
                        choice_ = o;
                        break;
                    }
                    log_error("engines: " + o.key + ": " + e);
                }
            }
        }
        else
        {
            engine_error_ = "no usable engine in " + engines_dir();
            log_error("engines: " + engine_error_);
        }
    }

    std::string service::starting_phase() const
    {
        if (!engines_ready_) return "engines";
        if (!models_ready_) return "models";
        return std::string();
    }

    bool service::model_loading(const std::string& requested)
    {
        const host_settings st = settings_->get();
        std::string id = requested;
        std::string role = requested;
        if (role.compare(0, 8, "albireo-") == 0) role = role.substr(8);
        if (requested.empty() || requested == "auto" || requested == "albireo") role = "general";
        if (role == "background")
        {
            if (!background_model_.empty()) id = background_model_;
            else role = "general";
        }
        if (is_role(role))
        {
            const std::map<std::string, std::string> models = effective_models(st);
            const auto it = models.find(role);
            if (it == models.end()) return false;
            id = it->second;
        }
        std::lock_guard<std::mutex> lock(m_);
        const auto state = load_state_.find(id);
        return state != load_state_.end() && state->second == "loading";
    }

    void service::background_loop()
    {
        /*
            Les tâches réseau tournent sur des threads à elles et démarrent d'abord ; le travail local
            suit aussitôt et ne les attend jamais : les moteurs, pendant que le serveur répond déjà,
            les modèles des rôles, les fichiers du dossier d'import, le modèle par défaut parmi ceux
            qui sont installés.
        */
        bool online = false;
        int64_t next_catalog = 0;
        auto network = [&](const host_settings& st) {
            if (st.offline)
            {
                online = false;
                return;
            }
            const int64_t now = now_unix();
            if (!online)
            {
                // Au démarrage, et au retour en ligne : les téléchargements interrompus reprennent, le
                // catalogue est vérifié aussitôt.
                online = true;
                models_->resume_pending();
                next_catalog = now;
            }
            if (now >= next_catalog)
            {
                next_catalog = now + catalog_period;
                models_->refresh_catalog_async();
            }
        };
        // Une tâche qui échoue, sur une base qui ne peut pas être écrite par exemple, est journalisée
        // et revient à son tour suivant, et les autres tâches continuent.
        auto attempt = [](const char* task, const std::function<void()>& work) {
            try
            {
                work();
            }
            catch (const std::exception& e)
            {
                log_error(std::string("service: ") + task + ": " + e.what());
            }
            catch (...)
            {
                log_error(std::string("service: ") + task + " failed");
            }
        };
        attempt("network", [&]() { network(settings_->get()); });
        attempt("engines", [&]() { start_engines(); });
        // Une base recommencée enregistre de nouveau les modèles du dossier des modèles, avant leur chargement.
        if (database_restarted_)
            attempt("models folder", [&]() {
                const json r = models_->import_path(models_->models_dir(), models_->models_dir(), true);
                log_info("models: " + std::to_string(r["imported"].size()) + " models of " + models_->models_dir()
                    + " recorded again");
                for (const auto& e : r["errors"].elements()) log_warning("models: " + e.as_string());
            });
        attempt("models", [&]() { load_active_models(); });
        models_ready_ = true;
        if (stopping_) return;
        attempt("import folder", [&]() { scan_inbox(); });
        unsigned catalog_seen = models_->catalog_generation();
        attempt("default models", [&]() { ensure_default_model(); });

        int64_t next_certificate = now_unix() + certificate_period;
        int64_t next_inbox = now_unix() + inbox_period;
        std::unique_lock<std::mutex> lock(m_);
        while (!stopping_)
        {
            cv_.wait_for(lock, std::chrono::seconds(5));
            if (stopping_) break;
            lock.unlock();
            const bool was_online = online;
            attempt("network", [&]() { network(settings_->get()); });
            // Un nouveau catalogue, téléchargé ou importé, peut nommer le modèle par défaut, qui
            // peut être téléchargé une fois de retour en ligne.
            if (models_->catalog_generation() != catalog_seen || (online && !was_online))
            {
                catalog_seen = models_->catalog_generation();
                attempt("default models", [&]() { ensure_default_model(); });
            }
            const int64_t now = now_unix();
            if (now >= next_inbox)
            {
                next_inbox = now + inbox_period;
                attempt("import folder", [&]() { scan_inbox(); });
            }
            if (now >= next_certificate)
            {
                next_certificate = now + certificate_period;
                attempt("certificates", [&]() {
                    const std::string cert_dir = join_path(data_dir_, "certificates");
                    bool renewed = false;
                    std::string error;
                    if (prepare_certificates(cert_dir, settings_->get().trust_authority, renewed, error) && renewed)
                    {
                        log_info("certificates: the certificates were renewed, restart");
                        request_restart();
                    }
                });
            }
            lock.lock();
        }
    }

    void service::scan_inbox()
    {
        /*
            Ce qui est laissé dans le dossier d'import du dossier des données, par l'installateur depuis
            son support ou par un administrateur, avec la disposition d'un paquet hors ligne : un
            catalogue (non signé) et des modèles (aussi dans un sous-dossier "models").  Ce qui est
            installé quitte le dossier ; un fichier qui ne peut pas être installé est renommé avec le
            suffixe ".rejected", pour qu'il ne soit lu qu'une fois.
        */
        const std::string inbox = join_path(data_dir_, "import");
        if (!dir_exists(inbox)) return;
        std::lock_guard<std::mutex> serial(import_m_);
        std::string insecure;
        if (!secure_inbox(insecure))
        {
            log_error("models: import folder: " + insecure + "; nothing is imported from it");
            return;
        }
        // Le sous-dossier models est lu quand il est atteint sans passer par aucun lien ; un lien n'est jamais
        // ouvert.
        const std::string sub = join_path(inbox, "models");
        std::vector<std::string> dirs = { inbox };
        if (link_free_folder(sub, inbox)) dirs.push_back(sub);
        // Une copie en cours : le dossier attend la passe suivante.
        for (const std::string& dir : dirs)
            for (const auto& n : list_dir(dir))
            {
                const std::string p = join_path(dir, n);
                if (!is_link(p) && file_being_written(p)) return;
            }
        // Ce qui ne peut pas être installé est renommé à part, un lien en tant que tel.
        auto set_aside = [](const std::string& f) {
            if (file_exists(f) || is_link(f)) rename_file(f, f + ".rejected");
        };

        const std::string catalog = join_path(inbox, "catalog.json");
        if (file_exists(catalog) || is_link(catalog))
        {
            const json r = models_->import_catalog(inbox, inbox);
            if (r.contains("error"))
            {
                log_warning("models: import folder: " + r["error"].as_string());
                set_aside(catalog);
            }
            else
            {
                remove_file(catalog);
            }
        }

        /*
            Les modèles du dossier et de son sous-dossier models en un seul import, pour que chaque
            projecteur aille avec son décodeur.  Un fichier installé, déjà installé (un modèle livré
            avec Albireo par exemple) ou gardé comme projecteur en attente a quitté le dossier ; celui
            qui n'a pas pu être installé est mis de côté avec sa description annexe.
        */
        bool models = false;
        for (const std::string& dir : dirs)
            for (const auto& n : list_dir(dir)) models = models || ends_with(n, ".gguf");
        if (models && !stopping_)
        {
            const json r = models_->import_path(inbox, inbox, true);
            for (const auto& e : r["errors"].elements()) log_warning("models: import folder: " + e.as_string());
            for (const auto& f : r["files"].elements())
            {
                const std::string file = f["file"].as_string();
                const std::string result = f["result"].as_string();
                // Un fichier que l'import n'a pas atteint, le service s'arrêtant, reste pour le prochain démarrage.
                if (!ends_with(file, ".gguf") || result.empty()) continue;
                const std::string side = file.substr(0, file.size() - 5) + ".json";
                if (result != "error") remove_file(side);
                else if (!stopping_)
                {
                    set_aside(file);
                    set_aside(side);
                }
            }
        }
        if (stopping_) return;

    }

    bool service::secure_inbox(std::string& error)
    {
        const std::string inbox = join_path(data_dir_, "import");
        if (folder_admin_only(inbox)) return true;
        // Sur macOS, ce contrôle vérifie les propriétaires et permissions POSIX, mais pas les ACL étendues.
        log_warning("models: the import folder " + inbox + " lets other accounts than the administrators write in "
            "it: it is secured, and what they placed is set aside");
        return secure_folder(inbox, error, true);
    }

    json service::import_folder(const std::string& path, user_error& refusal)
    {
        std::lock_guard<std::mutex> serial(import_m_);
        auto refuse = [&](const std::string& code, const std::string& english) {
            refusal.set(english, code).arg("path", path);
            log_warning("import: " + english);
            return json::object();
        };
        // Les refus que décide le texte du chemin, avant toute lecture.
        const std::string problem = path_problem(path);
        if (problem == "import_path_relative")
            return refuse(problem, "the path " + path + " is relative: an import names a folder by its full path");
        if (problem == "import_path_network")
            return refuse(problem, "the path " + path + " is on a network share: an import reads local drives only");
        if (problem == "import_path_device")
            return refuse(problem, "the path " + path + " names a device or a namespace of the system");
        if (!problem.empty())
            return refuse(problem, "the path " + path + " is not valid for an import: it holds a stream, a wildcard, "
                "a control character or a name . or ..");

        json result = json::object();
        const std::string inbox = join_path(data_dir_, "import");
        std::string within;
        if (path_under(path, inbox, within))
        {
            // Le dossier d'import du service.
            std::string insecure;
            if (dir_exists(inbox) && !secure_inbox(insecure))
                return refuse("import_folder_insecure", "the import folder lets other accounts than the administrators "
                    "write in it and cannot be secured: " + insecure);
            const bool whole = within == inbox;
            const bool folder = whole ? dir_exists(inbox) : link_free_folder(within, inbox);
            if (!folder)
            {
                link_free_file probe;
                std::string why;
                if (whole || !probe.open(within, inbox, why))
                {
                    if (why == "link")
                        return refuse("import_path_link", "the path " + path + " is reached through a link, which "
                            "imports never follow");
                    return refuse("import_not_found", "the path " + path + " is not found in the import folder");
                }
            }
            if (folder && file_exists(join_path(within, "catalog.json")))
                result["catalog"] = models_->import_catalog(within, inbox);
            const json r = models_->import_path(within, inbox, false);
            result["imported"] = r["imported"];
            result["already"] = r["already"];
            result["pending"] = r["pending"];
            result["errors"] = r["errors"];
            log_info("import: " + path + ", " + std::to_string(r["imported"].size()) + " models");
            return result;
        }

        // Ailleurs, un paquet hors ligne seulement ; un fichier de modèle d'un utilisateur passe par la
        // route d'envoi.
        if (path.size() > 5 && same_name(path.substr(path.size() - 5), ".gguf"))
            return refuse("import_use_upload", "a model file outside the import folder is sent to the service by its "
                "user: Import..., then Model files (*.gguf), in the Models window, or the route "
                "/albireo/v1/models/upload");
        /*
            Le catalogue décide, lu sans passer par aucun lien depuis la racine du disque ; la réponse
            est la même, que le dossier manque, soit illisible ou atteint par un lien, pour qu'elle ne
            révèle rien d'un dossier d'un autre compte.
        */
        std::string catalog_body;
        const bool catalog = read_file_link_free(join_path(path, "catalog.json"), std::string(), catalog_body,
            16u << 20);
        if (!catalog)
            return refuse("import_not_bundle", "the path " + path + " is no offline bundle of a local drive "
                "reached through no link: a folder with catalog.json");
        result["imported"] = json::array();
        result["already"] = json::array();
        result["pending"] = json::array();
        result["errors"] = json::array();
        if (catalog)
        {
            result["catalog"] = models_->import_catalog(path, std::string());
            // Les modèles que liste le catalogue, avec leurs projecteurs, quel que soit le catalogue qui
            // reste en usage.
            const json r = models_->import_bundle(path, json::parse_or_null(catalog_body));
            result["imported"] = r["imported"];
            result["already"] = r["already"];
            result["pending"] = r["pending"];
            result["errors"] = r["errors"];
        }
        log_info("import: offline bundle " + path + ", " + std::to_string(result["imported"].size()) + " models");
        return result;
    }

    bool service::is_embedding(const installed_model& m)
    {
        return m.descriptor["embedding"].as_bool(false) || has_role(m.descriptor, embedding_role);
    }

    void service::register_builtin_models()
    {
        std::vector<installed_model> removed;
        const std::vector<installed_model> added =
            models_->register_builtin(join_path(program_dir_, builtin_models_folder), removed);
        /*
            Un modèle qui n'est plus livré avec Albireo laisse ses fonctions au nouveau modèle de son
            genre, un modèle de langage à un modèle de langage et un modèle d'embedding à un modèle
            d'embedding : une mise à jour qui change le modèle livré avec Albireo garde les fonctions
            là où l'utilisateur les avait.
        */
        std::map<std::string, std::string> successor;
        std::set<std::string> successors;
        for (const auto& r : removed)
            for (const auto& a : added)
                if (is_embedding(a) == is_embedding(r) && !successors.count(a.id))
                {
                    successor[r.id] = a.id;
                    successors.insert(a.id);
                    log_info("models: " + a.id + " takes the place of " + r.id);
                    break;
                }
        if (!removed.empty())
        {
            settings_->update([&](host_settings& s) {
                for (auto it = s.active.begin(); it != s.active.end();)
                {
                    const auto next = successor.find(it->second);
                    const bool left = std::find_if(removed.begin(), removed.end(),
                        [&](const installed_model& r) { return r.id == it->second; }) != removed.end();
                    if (next != successor.end())
                    {
                        it->second = next->second;
                        ++it;
                    }
                    else if (left)
                    {
                        it = s.active.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            });
        }
        // Le moteur n'est pas encore démarré : les fonctions sont données maintenant, les modèles
        // se chargent avec les autres.
        for (const auto& m : added)
            if (!successors.count(m.id)) on_installed(m);
        // Le modèle de langage livré avec Albireo sert le travail en arrière-plan.
        background_model_.clear();
        for (const auto& m : db_->models())
            if (m.source == "builtin" && !is_embedding(m) && file_exists(m.file) && background_model_.empty())
                background_model_ = m.id;
        if (!background_model_.empty()) log_info("models: " + background_model_ + " serves the work in the background");
    }

    void service::note_default_failure(const json& entry, const user_error& error, const std::string& what)
    {
        log_warning("models: " + what + " cannot be downloaded: " + error.text);
        models_->note_failure(entry["id"].as_string(), error);
    }

    void service::ensure_default_model()
    {
        const host_settings st = settings_->get();
        const std::vector<installed_model> installed = db_->models();
        const json catalog = models_->catalog();

        // La recherche dans les documents a toujours son modèle d'embedding : un modèle installé,
        // sinon celui par défaut du catalogue, téléchargé quelles que soient les options de
        // l'installation (les ordinateurs sans réseau le reçoivent dans leur paquet hors ligne).
        if (!st.active.count(embedding_role))
        {
            const installed_model* pick = nullptr;
            for (const auto& m : installed)
                if (is_embedding(m) && (!pick || m.descriptor["default"].as_bool(false))) pick = &m;
            user_error error;
            if (pick)
            {
                if (!activate(pick->id, embedding_role, error)) log_warning("models: " + pick->id + ": " + error.text);
            }
            else if (!st.offline)
            {
                for (const auto& e : catalog["models"].elements())
                {
                    if (!has_role(e, embedding_role) || !e["default"].as_bool(false)) continue;
                    if (models_->start_download(e["id"].as_string(), error))
                        log_info("models: download of the embedding model " + e["id"].as_string());
                    else
                        note_default_failure(e, error, "the embedding model");
                    break;
                }
            }
        }

        if (st.active.count("general")) return;
        const installed_model* pick = nullptr;
        for (const auto& m : installed)
        {
            // Le modèle du catalogue destiné à l'usage général, sinon le premier modèle de langage.
            if (is_embedding(m)) continue;
            if (!pick || has_role(m.descriptor, "general") || m.descriptor["default"].as_bool(false)) pick = &m;
        }
        if (pick)
        {
            user_error error;
            if (!activate(pick->id, "general", error)) log_warning("models: " + pick->id + ": " + error.text);
            return;
        }
        // Aucun modèle de langage installé : le modèle par défaut du catalogue est téléchargé, une
        // fois, sauf si l'installation a demandé autrement ou si l'ordinateur est hors ligne.
        if (!st.default_model || st.offline || db_->value("default_model_requested") == "1") return;
        for (const auto& e : catalog["models"].elements())
        {
            if (!e["default"].as_bool(false) || has_role(e, embedding_role)) continue;
            user_error error;
            if (models_->start_download(e["id"].as_string(), error))
            {
                db_->set_value("default_model_requested", "1");
                log_info("models: download of the default model " + e["id"].as_string());
            }
            else
            {
                note_default_failure(e, error, "the default model");
            }
            return;
        }
    }

    void service::on_installed(const installed_model& m)
    {
        if (stopping_) return;
        // Un modèle du moteur dont les fichiers ont changé, un projecteur qu'on lui a donné par exemple, se
        // recharge avec eux.
        const std::vector<std::string> loaded = engine_.loaded_models();
        if (std::find(loaded.begin(), loaded.end(), m.id) != loaded.end())
        {
            log_info("models: " + m.id + " loads again with its files once its requests are served");
            retire(m.id, true);
        }
        else
        {
            // Un modèle dont les fichiers ont été remplacés pendant qu'il était hors du moteur
            // (replace_model_files()) se recharge.
            reload_model(m.id);
        }
        if (is_embedding(m))
        {
            // Le premier modèle d'embedding sert la recherche dans les documents.
            user_error error;
            if (!settings_->get().active.count(embedding_role) && !activate(m.id, embedding_role, error))
                log_warning("models: " + m.id + ": " + error.text);
            return;
        }
        // Un nouveau modèle prend les fonctions que nomme son entrée du catalogue et qu'aucun modèle
        // ne tient encore, et la fonction générale quand elle est libre, pour que chaque fonction ait
        // un modèle.
        std::vector<std::string> roles;
        for (const auto& r : m.descriptor["roles"].elements())
        {
            const std::string role = r.as_string();
            if (role == "general" || role == "code" || role == "security") roles.push_back(role);
        }
        roles.push_back("general");
        for (const auto& role : roles)
        {
            if (settings_->get().active.count(role)) continue;
            user_error error;
            if (!activate(m.id, role, error)) log_warning("models: " + m.id + ": " + error.text);
        }
    }

    bool service::load_model(const installed_model& m, std::string& error)
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            load_state_[m.id] = "loading";
        }
        /*
            Le moteur analyse les fichiers du modèle avec les droits du service : leurs dossiers doivent être
            ceux dont folder_admin_only() accepte le dossier parent.
        */
        for (const std::string& file : { m.file, m.projector })
        {
            if (file.empty() || folder_admin_only(parent_path(file))) continue;
            error = "the folder of " + file + " lets accounts other than the administrators change its files";
            std::lock_guard<std::mutex> lock(m_);
            load_state_[m.id] = error;
            log_error("models: " + m.id + " not loaded: " + error);
            return false;
        }
        const host_settings st = settings_->get();
        json options = json::object();
        // Les réglages des conversations s'appliquent aux modèles de langage ; un modèle
        // d'embedding garde les valeurs par défaut du moteur pour ses entrées courtes.
        if (!is_embedding(m))
        {
            if (st.context > 0) options["context"] = st.context;
            options["max_sequences"] = st.max_sequences;
            if (st.kv_mb > 0) options["kv_mb"] = st.kv_mb;
            options["kv_type"] = st.kv_type;
        }
        // L'encodeur de vision d'un modèle qui lit les images.
        if (!m.projector.empty())
        {
            if (file_exists(m.projector)) options["projector"] = m.projector;
            else
                log_warning("models: the projector " + m.projector + " of " + m.id + " is missing: it reads text only");
        }
        const bool ok = engine_.load_model(m.id, m.file, options, error);
        std::lock_guard<std::mutex> lock(m_);
        load_state_[m.id] = ok ? std::string("ready") : error;
        if (ok) log_info("models: " + m.id + " loaded");
        else log_error("models: " + m.id + " not loaded: " + error);
        return ok;
    }

    void service::load_active_models()
    {
        std::lock_guard<std::mutex> serial(models_m_);
        if (!engine_.running()) return;
        const host_settings st = settings_->get();
        std::vector<std::string> done;
        for (const auto& kv : st.active)
        {
            if (stopping_) return;
            if (std::find(done.begin(), done.end(), kv.second) != done.end()) continue;
            done.push_back(kv.second);
            installed_model m;
            if (!db_->find_model(kv.second, m) || !file_exists(m.file))
            {
                log_warning("models: the active model " + kv.second + " is not installed");
                std::lock_guard<std::mutex> lock(m_);
                load_state_[kv.second] = "not installed";
                continue;
            }
            std::string error;
            load_model(m, error);
        }
        // Le modèle du travail en arrière-plan, chargé avec ou sans fonctions.
        if (!background_model_.empty() && !stopping_
            && std::find(done.begin(), done.end(), background_model_) == done.end())
        {
            installed_model m;
            std::string error;
            if (db_->find_model(background_model_, m) && file_exists(m.file)) load_model(m, error);
        }
    }

    bool service::activate(const std::string& id, const std::string& role, user_error& error)
    {
        if (!is_role(role))
        {
            error.set("unknown role '" + role + "' (general, code, security or embedding)", "unknown_role")
                .arg("role", role);
            return false;
        }
        std::lock_guard<std::mutex> serial(models_m_);
        installed_model m;
        if (!db_->find_model(id, m) || !file_exists(m.file))
        {
            error.set("the model " + id + " is not installed", "not_installed").arg("m", id);
            return false;
        }
        const std::string name = m.name.empty() ? id : m.name;
        {
            std::lock_guard<std::mutex> lock(retire_m_);
            if (removing_.count(id) != 0)
            {
                error.set("the model " + id + " is being deleted", "being_deleted").arg("m", name);
                return false;
            }
        }
        if ((role == embedding_role) != is_embedding(m))
        {
            if (role == embedding_role)
                error.set("the model " + id + " writes text; the document search needs an embedding model",
                    "needs_embedding_model").arg("m", name);
            else
                error.set("the model " + id + " computes the vectors of the document search and answers no "
                    "conversation", "embedding_model_only").arg("m", name);
            return false;
        }
        // Un modèle qui attend ses requêtes avant de partir reste.
        if (engine_.keep_model(id)) log_info("models: " + id + " stays loaded");
        wait_retired(id);
        {
            const host_settings st = settings_->get();
            const auto current = st.active.find(role);
            if (current != st.active.end() && current->second == id) return true;
        }
        const std::vector<std::string> loaded = engine_.loaded_models();
        std::string load_error;
        if (engine_.running() && std::find(loaded.begin(), loaded.end(), id) == loaded.end()
            && !load_model(m, load_error))
        {
            error.set(load_error, "load_failed").arg("m", name).arg("e", load_error);
            return false;
        }
        // Les réglages ont pu changer pendant le chargement du modèle : la fonction est fixée dans
        // les réglages tels qu'ils sont maintenant.
        std::string previous;
        const host_settings st = settings_->update([&](host_settings& s) {
            const auto current = s.active.find(role);
            if (current != s.active.end()) previous = current->second;
            s.active[role] = id;
        });
        log_info("models: " + id + " active for " + role);
        // Le modèle qu'utilisait le rôle auparavant est libéré quand aucun autre rôle ne l'utilise.
        if (!previous.empty() && previous != id)
        {
            bool used = false;
            for (const auto& kv : st.active) used = used || kv.second == previous;
            if (!used) retire(previous);
        }
        return true;
    }

    void service::retire(const std::string& id, bool reload)
    {
        // Le modèle du travail en arrière-plan reste chargé sans fonctions ; il ne part que pour se recharger.
        if (!reload && !background_model_.empty() && id == background_model_) return;
        std::lock_guard<std::mutex> lock(retire_m_);
        // Les threads des retraits terminés, qui ne prennent plus ce verrou.
        for (const auto& ended : ended_retirers_)
        {
            const auto t = std::find_if(retirers_.begin(), retirers_.end(),
                [&ended](const std::thread& r) { return r.get_id() == ended; });
            if (t == retirers_.end()) continue;
            t->join();
            retirers_.erase(t);
        }
        ended_retirers_.clear();
        if (stopping_) return;
        // Un modèle déjà en partance se recharge une fois parti, s'il est encore utilisé à ce moment-là.
        if (reload) reloading_.insert(id);
        if (!retiring_.insert(id).second) return;
        {
            std::lock_guard<std::mutex> state(m_);
            load_state_[id] = removing_.count(id) != 0 ? "removing" : "unloading";
        }
        // Aucune nouvelle requête ne va au modèle ; le thread attend celles qu'il a.
        engine_.begin_unload(id);
        retirers_.emplace_back([this, id]() {
            try
            {
                bool unloaded = engine_.finish_unload(id);
                if (unloaded) log_info("models: " + id + " unloaded");
                for (;;)
                {
                    bool remove = false;
                    {
                        // La dernière vérification et la fin du retrait en une seule étape, pour qu'une
                        // suppression demandée entre-temps soit vue ici, soit faite par remove_model.
                        std::lock_guard<std::mutex> l(retire_m_);
                        if (removing_.count(id) == 0)
                        {
                            std::lock_guard<std::mutex> state(m_);
                            if (unloaded) load_state_.erase(id);
                            else load_state_[id] = "ready";
                            retiring_.erase(id);
                            break;
                        }
                        remove = unloaded;
                        if (remove) removing_.erase(id);
                    }
                    if (!remove)
                    {
                        // Gardé par une activation, puis supprimé : il part finalement.
                        engine_.begin_unload(id);
                        unloaded = engine_.finish_unload(id);
                        if (unloaded) log_info("models: " + id + " unloaded");
                        continue;
                    }
                    std::string error;
                    std::lock_guard<std::mutex> server(server_m_);
                    if (models_ && !models_->remove(id, error)) log_warning("models: " + error);
                }
                /*
                    Un modèle parti pour se recharger avec ses fichiers : les activations qui attendent la
                    fin de son retrait passent d'abord, puis il se charge quand une fonction ou le travail
                    en arrière-plan l'utilise encore.
                */
                bool again = false;
                {
                    std::lock_guard<std::mutex> l(retire_m_);
                    again = reloading_.erase(id) != 0;
                }
                if (again && unloaded)
                {
                    retire_cv_.notify_all();
                    reload_model(id);
                }
            }
            catch (const std::exception& e)
            {
                // Une base qui ne peut pas être écrite, par exemple : le retrait se termine tout de même,
                // pour qu'aucune activation ne l'attende.
                log_error("models: " + id + ": " + e.what());
                std::lock_guard<std::mutex> l(retire_m_);
                removing_.erase(id);
                retiring_.erase(id);
                reloading_.erase(id);
                std::lock_guard<std::mutex> state(m_);
                load_state_.erase(id);
            }
            {
                std::lock_guard<std::mutex> l(retire_m_);
                ended_retirers_.push_back(std::this_thread::get_id());
            }
            retire_cv_.notify_all();
        });
    }

    void service::reload_model(const std::string& id)
    {
        // Sérialisé avec les activations, qui chargent aussi les modèles.
        std::lock_guard<std::mutex> serial(models_m_);
        if (stopping_ || !engine_.running()) return;
        {
            // Un modèle qui quitte encore le moteur se recharge une fois parti (retire()).
            std::lock_guard<std::mutex> l(retire_m_);
            if (retiring_.count(id) != 0)
            {
                reloading_.insert(id);
                return;
            }
        }
        const host_settings st = settings_->get();
        bool used = id == background_model_;
        for (const auto& kv : st.active) used = used || kv.second == id;
        const std::vector<std::string> loaded = engine_.loaded_models();
        installed_model m;
        if (!used || std::find(loaded.begin(), loaded.end(), id) != loaded.end() || !db_->find_model(id, m)
            || !file_exists(m.file))
            return;
        std::string error;
        load_model(m, error);
    }

    bool service::replace_model_files(const std::string& id, const std::function<bool()>& place)
    {
        // Le modèle du travail en arrière-plan reste chargé, et est livré avec Albireo : aucun fichier ne
        // remplace le sien.
        if (!background_model_.empty() && id == background_model_) return false;
        bool retired = false;
        for (int attempt = 0; attempt < 3 && !stopping_; ++attempt)
        {
            bool placed = false, ok = false;
            {
                // Aucune activation ni aucun chargement ne prend le modèle entre la vérification et les renommages.
                std::lock_guard<std::mutex> serial(models_m_);
                bool leaving = false;
                {
                    std::lock_guard<std::mutex> l(retire_m_);
                    // Un modèle en cours de suppression ne garde aucun nouveau fichier : sa suppression prend les
                    // fichiers qu'il nomme.
                    if (removing_.count(id) != 0) return false;
                    leaving = retiring_.count(id) != 0;
                }
                const std::vector<std::string> loaded = engine_.loaded_models();
                if (!leaving && std::find(loaded.begin(), loaded.end(), id) == loaded.end())
                {
                    ok = place();
                    placed = true;
                }
            }
            if (placed)
            {
                // Un remplacement qui a échoué laisse le modèle avec ses fichiers : il revient tel qu'il était.
                if (!ok && retired) reload_model(id);
                return ok;
            }
            log_info("models: " + id + " leaves the engine once its requests are served, its files being replaced");
            retire(id);
            wait_retired(id);
            retired = true;
        }
        if (retired) reload_model(id);
        return false;
    }

    json service::leaving_models()
    {
        json o = json::object();
        std::lock_guard<std::mutex> state(m_);
        for (const auto& kv : load_state_)
            if (kv.second == "unloading" || kv.second == "removing") o[kv.first] = kv.second;
        return o;
    }

    void service::wait_retired(const std::string& id)
    {
        std::unique_lock<std::mutex> lock(retire_m_);
        retire_cv_.wait(lock, [&]() { return stopping_.load() || retiring_.count(id) == 0; });
    }

    bool service::deactivate(const std::string& id, user_error& error)
    {
        std::lock_guard<std::mutex> serial(models_m_);
        bool refused = false;
        bool changed = false;
        settings_->update([&](host_settings& st) {
            const auto embedding = st.active.find(embedding_role);
            if (embedding != st.active.end() && embedding->second == id)
            {
                refused = true;
                return;
            }
            for (auto it = st.active.begin(); it != st.active.end();)
            {
                if (it->second == id)
                {
                    it = st.active.erase(it);
                    changed = true;
                }
                else
                {
                    ++it;
                }
            }
        });
        if (refused)
        {
            installed_model m;
            const std::string name = db_->find_model(id, m) && !m.name.empty() ? m.name : id;
            error.set("the document search needs an embedding model: activate another one to replace " + id,
                "embedding_needed").arg("m", name);
            return false;
        }
        if (changed) log_info("models: " + id + " deactivated");
        retire(id);
        return true;
    }

    bool service::remove_model(const std::string& id, user_error& error, bool& pending)
    {
        std::lock_guard<std::mutex> serial(models_m_);
        pending = false;
        installed_model m;
        if (!db_->find_model(id, m))
        {
            error.set("the model '" + id + "' is not installed", "not_installed").arg("m", id);
            return false;
        }
        const std::string name = m.name.empty() ? id : m.name;
        if (m.source == "builtin")
        {
            error.set("the model " + id + " comes with Albireo: it can leave its functions to other models, it "
                "cannot be deleted", "builtin_model").arg("m", name);
            return false;
        }
        bool changed = false;
        settings_->update([&](host_settings& st) {
            for (auto it = st.active.begin(); it != st.active.end();)
            {
                if (it->second == id)
                {
                    it = st.active.erase(it);
                    changed = true;
                }
                else
                {
                    ++it;
                }
            }
        });
        if (changed) log_info("models: " + id + " deactivated");
        const std::vector<std::string> loaded = engine_.loaded_models();
        const bool in_engine = std::find(loaded.begin(), loaded.end(), id) != loaded.end();
        {
            std::lock_guard<std::mutex> lock(retire_m_);
            if (in_engine || retiring_.count(id) != 0)
            {
                // Supprimé par son retrait, une fois ses requêtes servies.
                removing_.insert(id);
                pending = true;
            }
        }
        if (!pending)
        {
            std::string why;
            if (models_->remove(id, why)) return true;
            error.set(why, "delete_failed").arg("m", name).arg("file", m.file);
            return false;
        }
        retire(id);
        std::lock_guard<std::mutex> state(m_);
        load_state_[id] = "removing";
        log_info("models: " + id + " deleted once its requests are served");
        return true;
    }

    std::map<std::string, std::string> service::effective_models(const host_settings& st) const
    {
        std::map<std::string, std::string> out = st.active;
        const auto general = st.active.find("general");
        for (const char* role : model_roles)
        {
            if (out.count(role)) continue;
            if (general != st.active.end()) out[role] = general->second;
            else if (!background_model_.empty()) out[role] = background_model_;
        }
        return out;
    }

    std::string service::resolve_model(const std::string& requested)
    {
        const host_settings st = settings_->get();
        const std::vector<std::string> loaded = engine_.loaded_models();
        auto is_loaded = [&](const std::string& id) {
            return std::find(loaded.begin(), loaded.end(), id) != loaded.end();
        };
        std::string role = requested;
        if (role.compare(0, 8, "albireo-") == 0) role = role.substr(8);
        if (requested.empty() || requested == "auto" || requested == "albireo") role = "general";
        // Le travail en arrière-plan va au modèle livré avec Albireo, sinon au modèle
        // général.
        if (role == "background")
        {
            if (!background_model_.empty() && is_loaded(background_model_)) return background_model_;
            role = "general";
        }
        const auto embedding = st.active.find(embedding_role);
        const std::string embedder = embedding == st.active.end() ? std::string() : embedding->second;
        if (role == embedding_role || requested == embedder) return std::string();
        if (is_role(role))
        {
            auto it = st.active.find(role);
            if (it != st.active.end() && is_loaded(it->second)) return it->second;
            // Un rôle sans modèle à lui est servi par le modèle général, sinon par le modèle
            // livré avec Albireo, sinon par le premier modèle de langage chargé.
            it = st.active.find("general");
            if (it != st.active.end() && is_loaded(it->second)) return it->second;
            if (!background_model_.empty() && is_loaded(background_model_)) return background_model_;
            for (const auto& id : loaded)
                if (id != embedder) return id;
            return std::string();
        }
        return is_loaded(requested) ? requested : std::string();
    }

    std::string service::resolve_embedding_model(const std::string& requested)
    {
        const host_settings st = settings_->get();
        const std::vector<std::string> loaded = engine_.loaded_models();
        auto is_loaded = [&](const std::string& id) {
            return std::find(loaded.begin(), loaded.end(), id) != loaded.end();
        };
        if (!requested.empty() && requested != "auto" && requested != embedding_role
            && requested != std::string("albireo-") + embedding_role)
        {
            installed_model m;
            return is_loaded(requested) && db_->find_model(requested, m) && is_embedding(m) ? requested : std::string();
        }
        const auto it = st.active.find(embedding_role);
        return it != st.active.end() && is_loaded(it->second) ? it->second : std::string();
    }

    std::map<std::string, json> service::loaded_traits()
    {
        std::map<std::string, json> out;
        json query = json::object();
        query["op"] = "models";
        const json answer = engine_.running() ? engine_.query(query) : json();
        for (const auto& id : engine_.loaded_models())
        {
            // Les entrées que le moteur lit pour le modèle : le texte seul, sauf si son interrogation en dit plus.
            json inputs = json::array();
            inputs.push_back("text");
            for (const auto& m : answer["models"].elements())
                if (m["id"].as_string() == id && m["inputs"].is_array() && m["inputs"].size() > 0) inputs = m["inputs"];
            json t = json::object();
            t["inputs"] = inputs;
            installed_model record;
            const json& entry = record.descriptor;
            t["publisher"] = db_->find_model(id, record) ? entry["publisher"].str() : std::string();
            out[id] = t;
        }
        return out;
    }

    void service::record_usage(long prompt_tokens, long generated_tokens)
    {
        usage_prompt_ += prompt_tokens;
        usage_generated_ += generated_tokens;
        ++usage_requests_;
        try
        {
            db_->add_usage(prompt_tokens, generated_tokens, 1);
        }
        catch (const std::exception& e)
        {
            log_warning(std::string("usage: ") + e.what());
        }
    }

    json service::status()
    {
        const host_settings st = settings_->get();
        json s = json::object();
        s["product"] = ALBIREO_PRODUCT_NAME;
        s["edition"] = ALBIREO_EDITION;
        s["version"] = ALBIREO_VERSION;
        s["author"] = ALBIREO_AUTHOR_NAME;
        s["email"] = ALBIREO_AUTHOR_EMAIL;
        s["copyright"] = ALBIREO_COPYRIGHT;
        s["project"] = ALBIREO_PROJECT_URL;
        s["started_at"] = static_cast<long long>(started_at_);
        s["uptime"] = static_cast<long long>(now_unix() - started_at_);
        const std::string phase = starting_phase();
        s["state"] = phase.empty() ? "ready" : "starting";
        if (!phase.empty()) s["starting"] = phase;
        s["port"] = port_;
        s["url"] = "https://localhost:" + std::to_string(port_) + "/";
        s["data_dir"] = data_dir_;
        s["models_dir"] = st.effective_models_dir();
        s["models_dir_free"] = static_cast<unsigned long long>(free_disk_space(st.effective_models_dir()));

        json host = json::object();
        const cpu_info cpu = read_cpu_info();
        const memory_info mem = read_memory_info();
        host["name"] = host_name();
        host["os"] = operating_system_name();
        json c = json::object();
        c["name"] = cpu.brand;
        c["cores"] = cpu.physical_cores;
        c["performance_cores"] = cpu.performance_cores;
        c["efficiency_cores"] = cpu.efficiency_cores;
        c["threads"] = cpu.logical_cores;
        json features = json::array();
        for (const auto& f : cpu.features) features.push_back(f);
        c["features"] = features;
        host["cpu"] = c;
        json m = json::object();
        m["total"] = static_cast<unsigned long long>(mem.total);
        m["available"] = static_cast<unsigned long long>(mem.available);
        m["commit_total"] = static_cast<unsigned long long>(mem.commit_total);
        m["commit_available"] = static_cast<unsigned long long>(mem.commit_available);
        host["memory"] = m;
        s["host"] = host;

        json e = json::object();
        e["running"] = engine_.running();
        e["variant"] = engine_.variant();
        const bool chosen = engines_ready_;
        e["key"] = chosen ? choice_.key : std::string();
        e["device"] = chosen ? choice_.device : std::string();
        e["error"] = chosen ? engine_error_ : std::string();
        e["description"] = engine_.description();
        json query = json::object();
        query["op"] = "stats";
        e["stats"] = engine_.running() ? engine_.query(query) : json();
        s["engine"] = e;
        query["op"] = "models";
        s["loaded_models"] = engine_.running() ? engine_.query(query)["models"] : json::array();

        json active = json::object(), selected = json::object();
        for (const auto& kv : effective_models(st)) active[kv.first] = kv.second;
        for (const auto& kv : st.active) selected[kv.first] = kv.second;
        s["active"] = active;
        s["selected"] = selected;
        s["background"] = background_model_;
        json loading = json::object();
        {
            std::lock_guard<std::mutex> lock(m_);
            for (const auto& kv : load_state_) loading[kv.first] = kv.second;
        }
        s["models_state"] = loading;
        json usage = json::object();
        usage["requests"] = usage_requests_.load();
        usage["prompt_tokens"] = usage_prompt_.load();
        usage["completion_tokens"] = usage_generated_.load();
        s["usage"] = usage;
        s["downloads"] = models_->downloads();
        s["catalog"] = models_->catalog_status();
        s["offline"] = st.offline;
        json settings = json::object();
        settings["engine"] = st.engine;
        settings["accelerator"] = st.accelerator;
        settings["device_mb"] = st.device_mb;
        settings["language"] = st.language;
        settings["offline"] = st.offline;
        s["settings"] = settings;
        return s;
    }

    json service::engines()
    {
        const host_settings st = settings_->get();
        if (!engines_ready_)
        {
            // Les moteurs sont en cours de description : rien à lister encore.
            json out = json::object();
            out["starting"] = true;
            out["engines"] = json::array();
            out["options"] = json::array();
            out["current"] = "";
            out["setting"] = st.engine;
            out["accelerator"] = st.accelerator;
            return out;
        }
        json list = json::array();
        for (const auto& c : candidates_)
        {
            json j = json::object();
            j["variant"] = c.variant;
            j["path"] = c.path;
            j["usable"] = c.usable;
            j["version"] = c.description["version"];
            j["build"] = c.description["build"];
            j["devices"] = c.description["devices"];
            if (c.description["missing"].is_string()) j["missing"] = c.description["missing"];
            if (!c.usable)
            {
                j["reason"] = c.description["reason"];
                if (c.description.contains("reason_code"))
                {
                    j["reason_code"] = c.description["reason_code"];
                    j["reason_args"] = c.description["reason_args"];
                }
            }
            j["current"] = choice_.candidate && choice_.candidate->path == c.path;
            list.push_back(j);
        }
        json options = json::array();
        for (const auto& o : engine_options(candidates_))
        {
            json j = json::object();
            j["key"] = o.key;
            j["variant"] = o.candidate->variant;
            j["device"] = o.device;
            j["current"] = o.key == choice_.key;
            j["measure"] = st.engine_benchmarks[o.key];
            // Le genre du choix et les noms de ses périphériques, que montrent les menus.
            j["kind"] = "cpu";
            json names = json::array();
            for (const auto& d : o.candidate->description["devices"].elements())
            {
                if (d["type"].as_string() == "cpu") names.push_back(d["name"]);
            }
            j["names"] = names;
            options.push_back(j);
        }
        json out = json::object();
        out["engines"] = list;
        out["options"] = options;
        out["current"] = choice_.key;
        out["setting"] = st.engine;
        out["accelerator"] = st.accelerator;
        return out;
    }

    json service::benchmark_engines()
    {
        // Une seule mesure à la fois : les moteurs d'une seconde tourneraient à côté de ceux de la première
        // et fausseraient les deux.
        std::unique_lock<std::mutex> running(benchmark_m_, std::try_to_lock);
        if (!running.owns_lock())
        {
            json busy = json::object();
            busy["error"] = "a measure of the engines is in progress";
            busy["code"] = "benchmark_running";
            return busy;
        }
        // Le modèle de la mesure : le modèle général, sinon le plus petit installé.
        const host_settings st = settings_->get();
        installed_model model;
        bool found = st.active.count("general") && db_->find_model(st.active.at("general"), model);
        if (!found)
        {
            for (const auto& m : db_->models())
                if (!found || m.size < model.size)
                {
                    model = m;
                    found = true;
                }
        }
        json out = json::object();
        if (!engines_ready_)
        {
            out["error"] = "the service is starting";
            out["code"] = "starting";
            return out;
        }
        if (!found)
        {
            out["error"] = "no model is installed";
            return out;
        }
        json results = json::object();
        // Attend que le moteur du service ne serve aucune requête, deux minutes au plus.
        auto wait_idle = [&]() {
            for (int i = 0; i < 240 && !stopping_; ++i)
            {
                long active = 0;
                engine_.activity(active);
                if (active == 0) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        };
        for (const auto& o : engine_options(candidates_))
        {
            if (stopping_) break;
            json config = engine_config(st, o.device);
            config["context"] = 2048;
            // Les graphes d'un périphérique se compilent avant les produits qu'ils servent, pour que
            // la mesure reflète le périphérique.
            config["benchmark"] = true;
            std::string output;
            int code = -1;
            const std::vector<std::string> args = { "--bench-engine", o.candidate->path, model.file, config.dump() };
            for (int attempt = 0; attempt < 2 && !stopping_; ++attempt)
            {
                wait_idle();
                long active = 0;
                const unsigned long long before = engine_.activity(active);
                log_info("engines: measuring " + o.key + " with " + model.id);
                output.clear();
                // La mesure se termine avec le service, qui n'attend alors aucun processus enfant.
                code = run_process(executable_path(), args, &output, 600000, &stopping_);
                // Une mesure pendant laquelle le service a calculé a partagé le processeur avec lui.
                if (engine_.activity(active) == before && active == 0) break;
                log_info("engines: the service served requests during the measure of " + o.key
                    + (attempt == 0 ? ", which is made again" : ", which is kept"));
            }
            const size_t start = output.rfind("\n{");
            const json r = json::parse_or_null(start == std::string::npos ? output : output.substr(start + 1));
            if (code == 0 && r["tokens_per_second"].as_double(0) > 0)
            {
                json m = r;
                m["model"] = model.id;
                m["measured_at"] = static_cast<long long>(now_unix());
                results[o.key] = m;
                log_info("engines: " + o.key + " " + std::to_string(r["tokens_per_second"].as_double()) + " tokens/s");
            }
            else
            {
                json m = json::object();
                const std::string failed = "the measure failed (code " + std::to_string(code) + ")";
                m["error"] = r["error"].is_string() ? r["error"] : json(failed);
                results[o.key] = m;
                log_warning("engines: measure of " + o.key + " failed");
            }
        }
        // Les mesures rejoignent les réglages tels qu'ils sont maintenant, qui ont pu changer entre-temps.
        const host_settings updated = settings_->update([&](host_settings& s) {
            for (const auto& kv : results.items())
                if (!kv.second.contains("error")) s.engine_benchmarks[kv.first] = kv.second;
        });
        out["model"] = model.id;
        out["results"] = results;
        // Le choix reste le moteur CPU unique ; la mesure est conservée à titre informatif.
        const engine_choice next = choose_engine(candidates_);
        out["restart"] = next.key != choice_.key;
        if (next.key != choice_.key) request_restart();
        return out;
    }

    int service::probe_engine(const std::string& path)
    {
        engine_library lib;
        std::string error;
        json d;
        if (!lib.load(path, error))
        {
            d = json::object();
            d["usable"] = false;
            d["reason"] = error;
            std::cout << d.dump() << std::endl;
            return 2;
        }
        // L'environnement d'exécution d'un périphérique démarre avec la description ; le processus
        // qui se termine après elle (main.cpp) laisse la bibliothèque chargée.
        lib.keep_loaded();
        d = lib.description();
        if (!d.is_object())
        {
            d = json::object();
            d["usable"] = false;
            d["reason"] = "the library gives no description";
        }
        std::cout << d.dump() << std::endl;
        return 0;
    }

    int service::benchmark_engine(const std::string& path, const std::string& model, const std::string& config)
    {
        auto fail = [](const std::string& message) {
            json r = json::object();
            r["error"] = message;
            std::cout << r.dump() << std::endl;
            return 2;
        };
        engine_library lib;
        std::string error;
        if (!lib.load(path, error)) return fail(error);
        lib.keep_loaded();
        char err[2048] = { 0 };
        albireo_engine* e = lib.create(config.c_str(), nullptr, nullptr, err, sizeof(err));
        if (!e) return fail(err);
        json options = json::object();
        options["context"] = 2048;
        options["max_sequences"] = 1;
        if (lib.load_model(e, "bench", model.c_str(), options.dump().c_str(), err, sizeof(err)) != ALBIREO_OK)
        {
            lib.destroy(e);
            return fail(err);
        }

        struct waiter
        {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::string result;
            static void ALBIREO_ENGINE_CALL on_event(void* user, int kind, const char* data, size_t size)
            {
                waiter* w = static_cast<waiter*>(user);
                if (kind != ALBIREO_EVENT_DONE && kind != ALBIREO_EVENT_ERROR) return;
                std::lock_guard<std::mutex> lock(w->m);
                w->done = true;
                w->result.assign(data, size);
                w->cv.notify_all();
            }
        };
        auto run = [&](int tokens, json& done) {
            json req = json::object();
            req["model"] = "bench";
            json messages = json::array();
            json msg = json::object();
            msg["role"] = "user";
            msg["content"] = "Write a detailed explanation of how a refracting telescope forms an image, "
                "covering the objective lens, the focal length, the eyepiece and the magnification.";
            messages.push_back(msg);
            req["messages"] = messages;
            req["max_tokens"] = tokens;
            req["temperature"] = 0;
            req["reasoning"] = false;
            json stop_none = json::array();
            req["stop"] = stop_none;
            waiter w;
            uint64_t id = 0;
            if (lib.submit(e, req.dump().c_str(), &waiter::on_event, &w, &id, err, sizeof(err)) != ALBIREO_OK)
                return false;
            std::unique_lock<std::mutex> lock(w.m);
            w.cv.wait(lock, [&]() { return w.done; });
            done = json::parse_or_null(w.result);
            return done["usage"].is_object();
        };
        /*
            Une première requête lit le modèle du disque dans la mémoire du système, laisse un
            périphérique compiler ses graphes et copier ses matrices, et laisse les moteurs des
            périphériques mesurer où chaque produit tourne le plus vite.  Deux requêtes sont ensuite
            mesurées : la plus rapide des deux est gardée, pour qu'une pause du système pendant l'une
            d'elles ne fixe pas la mesure.
        */
        json warm, done, again;
        const bool ok = run(16, warm) && run(64, done) && run(64, again);
        lib.unload_model(e, "bench");
        lib.destroy(e);
        if (!ok)
        {
            const json& failed = again.is_object() ? again : done.is_object() ? done : warm;
            return fail(failed["message"].str("the measure did not complete"));
        }
        const json& t = done["timings"];
        const json& u = again["timings"];
        json r = json::object();
        r["tokens_per_second"] = std::max(t["generation_per_second"].as_double(0),
            u["generation_per_second"].as_double(0));
        r["prompt_tokens_per_second"] = t["prompt_per_second"];
        r["generated"] = done["usage"]["completion_tokens"];
        std::cout << r.dump() << std::endl;
        return 0;
    }

}
