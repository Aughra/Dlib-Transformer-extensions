// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_ENGINE_H_
#define ALBIREO_ENGINE_ENGINE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../common/json.h"

namespace albireo
{
    class thread_pool;
    class metal_device;
    class tensor_device;
    struct loaded_model;
    struct request_state;
    struct embedding_job;

    // Reçoit les événements d'une requête : un type d'engine_api.h et ses données.
    typedef std::function<void(int kind, const std::string& data)> event_sink;

    // Reçoit les messages du moteur : une gravité d'engine_api.h et un texte.
    typedef std::function<void(int level, const std::string& message)> log_sink;

    // Une requête refusée pour un temps, le moteur contenant tout ce qu'il peut contenir : ALBIREO_E_BUSY.
    struct engine_busy : std::runtime_error
    {
        explicit engine_busy(const std::string& what) : std::runtime_error(what) {}
    };

    class engine
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un moteur d'inférence : les modèles qu'il a chargés, les requêtes soumises
                à ces modèles et le thread qui les calcule.

                Un seul thread ordonnanceur pilote tous les modèles.  À chaque étape, il
                prend, pour un modèle, le token suivant de chaque requête en cours de
                génération et une tranche des prompts en cours de lecture, jusqu'à un
                nombre de tokens par passe, et les calcule en une seule passe avant : les
                poids sont lus une fois pour toutes les requêtes, de sorte que servir
                plusieurs conversations coûte à peine plus que d'en servir une.  Les
                prompts prennent les tranches à tour de rôle, et tant que des réponses
                s'écrivent, dans n'importe quel modèle, une passe lit au plus 64 tokens de
                prompts, de sorte qu'un long prompt ne retarde les tokens des réponses que
                de la durée d'une passe courte.

                Les clés et valeurs d'une requête terminée restent en mémoire avec ses
                tokens.  Une nouvelle requête qui commence par les mêmes tokens, comme le
                fait le tour suivant d'une conversation, ou comme deux requêtes partageant
                un prompt système, les reprend ou les copie au lieu de les recalculer.  Les
                séquences en cache sont abandonnées, les moins récemment utilisées d'abord,
                quand la mémoire vient à manquer.

                Le tour suivant rend la réponse d'une requête de chat autrement que les
                tokens que le modèle a générés : les formats des modèles de raisonnement
                retirent le raisonnement des tours précédents, et le texte de la réponse est
                tokenisé à nouveau.  Une fois une réponse terminée, son historique est donc
                rendu comme la requête suivante le rendra et lu en arrière-plan, pendant
                que l'utilisateur lit la réponse, de sorte que la requête suivante ne
                calcule que sa question.  Le réglage "warm_history" (true par défaut)
                désactive ce mécanisme.

                Un modèle d'embedding répond aux requêtes dont "input" liste des textes :
                chaque texte est lu comme un prompt à part entière, les textes de toutes les
                requêtes partageant les passes, et l'état caché normalisé de son dernier
                token devient son vecteur.  Tant qu'un modèle de langage répond, les passes
                des modèles d'embedding restent courtes, de sorte que l'indexation de
                documents ne bloque jamais une conversation.

            THREAD SAFETY
                Chaque fonction membre peut être appelée depuis n'importe quel thread.  Les
                récepteurs d'événements s'exécutent sur le thread ordonnanceur et doivent
                rendre la main rapidement.
        !*/
    public:
        engine(const json& config, log_sink sink);
        ~engine();

        engine(const engine&) = delete;
        engine& operator=(const engine&) = delete;

        // Décrit ce build et les périphériques qu'il utiliserait (voir engine_api.h).
        static json describe();

        void load_model(const std::string& id, const std::string& path, const json& options);
        void unload_model(const std::string& id);

        json query(const json& request);

        // Valide et met en file une requête ; lève std::invalid_argument pour une requête invalide.
        uint64_t submit(const json& request, event_sink sink);
        void cancel(uint64_t id);

    private:
        void run();
        // Une passe de m ; answering indique si un modèle écrit des réponses, ce qui plafonne les
        // tokens de prompt de la passe.
        bool step(const std::shared_ptr<loaded_model>& m, long budget, bool answering);
        // Exécute une tranche de l'encodage de l'image qu'attend un prompt de m.
        void advance_encoding(loaded_model& m);
        uint64_t submit_embeddings(const std::shared_ptr<loaded_model>& m, const json& request, event_sink sink);
        void complete_part(loaded_model& m, request_state& r, const float* hidden);
        void end_job(embedding_job& job, const std::string& code, const std::string& message);
        // Termine une requête d'embedding que son client a annulée : DONE avec la raison "cancelled".
        void cancel_job(embedding_job& job);
        void admit(loaded_model& m);
        bool assign_sequence(loaded_model& m, request_state& r);
        bool reserve(loaded_model& m, int seq, long length);
        bool preempt(loaded_model& m, const request_state* keep = nullptr);
        void requeue_preempted(loaded_model& m);
        void drop_cancelled(loaded_model& m);
        void abandon(loaded_model& m, const std::string& code, const std::string& message);
        void trim_after_failure(loaded_model& m);
        void trim_idle(const std::vector<std::shared_ptr<loaded_model>>& models);
        void handle_token(loaded_model& m, request_state& r, int token);
        void emit_text(loaded_model& m, request_state& r, const std::string& text);
        void finish(loaded_model& m, request_state& r, const std::string& reason, bool keep_cache = true);
        // Met en file la lecture de l'historique de la réponse de r, dont la séquence contient les
        // tokens cached.
        void warm_history(loaded_model& m, const request_state& r, const std::vector<int>& cached);
        // Termine les préchauffages par l'historique desquels commence une requête en attente d'un
        // client.
        void yield_warm_ups(loaded_model& m);
        void fail(request_state& r, const std::string& code, const std::string& message);
        void log(int level, const std::string& message);
        json model_info(const loaded_model& m) const;
        json stats();

        // Lecture seule : les accesseurs const de json n'insèrent jamais les clés qu'ils cherchent.
        const json settings_;
        log_sink log_;
        std::unique_ptr<thread_pool> pool_;
        int threads_ = 1;
        // Le GPU du Mac (Metal), nul quand le réglage "metal" vaut false ou que Metal manque.
        std::unique_ptr<metal_device> gpu_;
        // Ce qui manque à Metal, quand gpu_ est nul alors que le réglage "metal" vaut true.
        std::string gpu_off_;
        // Indique si les historiques des réponses sont lus en avance sur les questions suivantes.
        bool warm_history_ = true;

        // Sérialise le chargement des modèles.
        std::mutex loading_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable unloaded_;
        std::map<std::string, std::shared_ptr<loaded_model>> models_;
        std::map<uint64_t, std::shared_ptr<request_state>> requests_;
        std::map<uint64_t, std::shared_ptr<embedding_job>> jobs_;
        uint64_t next_id_ = 1;
        bool stop_ = false;
        std::thread worker_;

        // Débit sur le passé récent, pour les statistiques.
        std::deque<std::pair<double, long>> generated_window_;
        std::deque<std::pair<double, long>> prompt_window_;
        std::mutex stats_mutex_;
        uint64_t total_generated_ = 0;
        uint64_t total_prompt_ = 0;
        uint64_t total_requests_ = 0;
    };

}

#endif // ALBIREO_ENGINE_ENGINE_H_
