// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_ENGINE_HOST_H_
#define ALBIREO_HOST_ENGINE_HOST_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "../common/engine_loader.h"
#include "../common/json.h"

namespace albireo
{

    struct engine_candidate
    {
        // "cpu", "cpu-avx2", "cpu-avxvnni", "openvino", "openvino-avxvnni", "cuda"
        std::string variant;
        std::string path;
        json description;
        bool usable = false;
    };

    // Reçoit les événements d'une requête (types de engine_api.h).
    typedef std::function<void(int kind, const std::string& data)> request_sink;

    class engine_host
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le moteur que fait tourner le service : les bibliothèques trouvées à côté du
                service, décrites chacune dans un processus enfant pour qu'une bibliothèque qui
                ne peut pas se charger (un pilote graphique manquant, par exemple) ne fasse
                jamais tomber le service, celle qui est choisie pour cette machine, et les
                modèles qu'elle contient.

            SÉLECTION
                Le service choisit le moteur à démarrer parmi les candidats que décrit
                discover() (choose_engine() dans service.h).

            CONCURRENCE
                Un modèle en cours de chargement ou de déchargement ne retarde jamais les
                autres opérations : les requêtes, les interrogations et les annulations
                continuent pendant un chargement, qui peut lire plusieurs gigaoctets.  Seuls
                start() et stop() les attendent.

            DÉCHARGEMENT
                Un modèle quitte le moteur une fois ses requêtes servies : dès le début de
                unload_model(), submit() refuse les nouvelles requêtes pour lui, celles en cours
                et celles en attente sont calculées, éventuellement avec celles des autres
                modèles, et le modèle est déchargé quand la dernière se termine.  stop() met fin
                aux attentes.
        !*/
    public:
        engine_host();
        ~engine_host();

        /*
            Liste et décrit les bibliothèques de moteur d'un dossier, par probe_program, en les
            sondant toutes à la fois.  Une bibliothèque dont les noyaux demandent des
            instructions que le processeur n'a pas est décrite sans être chargée.  Les sondes
            s'arrêtent dès que stop, s'il est fourni, devient vrai.
        */
        std::vector<engine_candidate> discover(const std::string& dir, const std::string& probe_program,
            const std::atomic<bool>* stop = nullptr);

        bool start(const engine_candidate& c, const json& config, std::string& error);
        void stop();

        /*!
            ensures
                - Renvoie true dès que ce processus a démarré un moteur dont la bibliothèque
                  reste chargée jusqu'à la fin du processus ("pinned" dans sa description) : les
                  moteurs des périphériques, dont les environnements d'exécution gardent des
                  threads et un état à eux.  Un changement des réglages du moteur demande alors
                  un nouveau processus.
        !*/
        static bool pinned_engine_started();
        bool running() const;
        std::string variant() const;
        json description() const;

        bool load_model(const std::string& id, const std::string& path, const json& options, std::string& error);
        /*!
            ensures
                - Décharge le modèle id une fois ses requêtes servies (voir DÉCHARGEMENT) et
                  renvoie true, ou renvoie false quand keep_model() a annulé le déchargement
                  pendant son attente.  Les requêtes encore en cours après drain_seconds se
                  terminent avec le déchargement.
        !*/
        bool unload_model(const std::string& id, int drain_seconds = 1800);
        /*!
            ensures
                - Les deux étapes de unload_model() : begin_unload() rend la main aussitôt, le
                  modèle ne prenant plus de nouvelle requête dès lors, et finish_unload() attend
                  ses requêtes et le décharge, sauf si keep_model() a été appelé entre-temps.
        !*/
        void begin_unload(const std::string& id);
        bool finish_unload(const std::string& id, int drain_seconds = 1800);
        /*!
            ensures
                - Annule le déchargement de id pendant qu'il attend les requêtes du modèle :
                  le modèle reprend des requêtes, et finish_unload() renvoie false.
                - Renvoie false quand id n'attend pas de partir (pas en cours de déchargement,
                  ou son déchargement a déjà commencé).
        !*/
        bool keep_model(const std::string& id);
        std::vector<std::string> loaded_models() const;

        /*
            Soumet une requête ; le sink reçoit ses événements jusqu'au dernier.  code, s'il est
            fourni, reçoit le résultat de albireo_engine_submit() : ALBIREO_OK, ou le code de
            son refus, ALBIREO_E_BUSY pour une requête à soumettre de nouveau sous peu ; il
            reçoit ALBIREO_E_NOT_FOUND quand aucun moteur ne tourne ou quand le modèle quitte
            le moteur.
        */
        bool submit(const json& request, request_sink sink, uint64_t& id, std::string& error, int* code = nullptr);
        void cancel(uint64_t id);
        json query(const json& request);

        /*!
            ensures
                - Renvoie le nombre de requêtes que le moteur a acceptées depuis la création de
                  cet objet, et donne dans active le nombre de celles encore en cours.
        !*/
        unsigned long long activity(long& active) const;

    private:
        static void ALBIREO_ENGINE_CALL on_event(void* user, int kind, const char* data, size_t size);
        void request_ended(const std::string& model);
        static void ALBIREO_ENGINE_CALL on_log(void* user, int level, const char* message);

        // Le moteur lui-même : tenu en exclusivité pour le démarrer et l'arrêter, partagé par
        // les opérations qui l'utilisent.
        mutable std::shared_timed_mutex life_;
        std::unique_ptr<engine_library> lib_;
        albireo_engine* engine_ = nullptr;
        // Ce que le service demande souvent : l'état, la description et les modèles.
        mutable std::mutex m_;
        bool running_ = false;
        engine_candidate current_;
        std::vector<std::string> models_;

        // Les requêtes en cours de chaque modèle, qu'un déchargement attend, et les modèles en
        // cours de déchargement, qui ne prennent plus de nouvelle requête : false pendant
        // qu'ils attendent leurs requêtes, true une fois qu'ils quittent le moteur.
        mutable std::mutex flight_m_;
        std::condition_variable flight_cv_;
        std::map<std::string, long> in_flight_;
        std::map<std::string, bool> draining_;
        bool stopping_ = false;
        // Les requêtes acceptées depuis la création de cet objet, sous flight_m_.
        unsigned long long submitted_ = 0;
    };

}

#endif // ALBIREO_HOST_ENGINE_HOST_H_
