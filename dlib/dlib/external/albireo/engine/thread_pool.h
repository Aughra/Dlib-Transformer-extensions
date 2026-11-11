// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_THREAD_POOL_H_
#define ALBIREO_ENGINE_THREAD_POOL_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#define ALBIREO_CPU_RELAX() std::this_thread::yield()

#include <memory>
#include <sys/sysctl.h>

namespace albireo
{

    class thread_pool
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les threads qui calculent les produits, l'attention et les normalisations
                d'une passe avant.  Une étape de génération exécute quelques centaines de
                sections parallèles, une par produit, si bien que ce qui compte est le coût
                du démarrage de l'une d'elles : les workers tournent sur un compteur atomique
                un court moment après chaque section et ne s'endorment qu'ensuite, ce qui
                garde la passation de l'ordre de la microseconde pendant la production des
                tokens et laisse le processeur se reposer quand le moteur est inactif.

                parallel_for() répartit une plage de tâches entre les workers et le thread
                appelant, les distribue une à une par un compteur atomique pour que les
                tâches inégales s'équilibrent d'elles-mêmes, et revient une fois chaque
                tâche terminée.  Le thread pool exécute une section à la fois : un thread qui
                appelle parallel_for() pendant que la section d'un autre thread s'exécute
                attend la fin de cette section.  Une tâche ne doit pas ouvrir de section dans
                son propre thread pool.  Une exception levée par une tâche fait sauter les
                tâches pas encore commencées et parvient à l'appelant une fois que chaque
                worker a quitté la section.
        !*/

    public:

        explicit thread_pool(int threads)
        {
            const int n = threads > 0 ? threads : 1;
            /*
                Chaque worker part de la génération 0, celle d'un thread pool sans aucune
                section, pour qu'un worker prenne part à une section ouverte avant que le
                système ne l'exécute pour la première fois.  Un thread qui ne peut pas être
                démarré arrête les autres.
            */
            try
            {
                workers_.reserve(static_cast<size_t>(n - 1));
                for (int i = 1; i < n; ++i)
                    workers_.emplace_back([this, i]() { worker_loop(i, 0); });
            }
            catch (...)
            {
                stop_workers();
                throw;
            }
            try
            {
                make_fast_team(n);
            }
            catch (...)
            {
                stop_workers();
                throw;
            }
        }

        ~thread_pool()
        {
            stop_workers();
        }

        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;

        int size() const { return static_cast<int>(workers_.size()) + 1; }

#include "thread_pool_apple.inc"

        // Durée pendant laquelle les workers tournent après une section avant de s'endormir.
        void set_spin_time(std::chrono::microseconds t) { spin_us_.store(t.count(), std::memory_order_relaxed); }

        /*!
            ensures
                - Appelle fn(task, thread_index) une fois pour chaque tâche de [0, n_tasks),
                  thread_index étant dans [0, size()), et revient quand tous les appels sont revenus.
                - Exécute tout sur le thread appelant quand n_tasks == 1 ou que le thread pool
                  n'a qu'un seul thread.
                - Relance la première exception levée par une tâche, une fois chaque worker terminé.
        !*/
        template <typename F>
        void parallel_for(long n_tasks, F&& fn)
        {
            if (n_tasks <= 0) return;
            if (n_tasks > 1 && run_on_fast_team(n_tasks, fn)) return;
            if (n_tasks == 1 || workers_.empty())
            {
                for (long t = 0; t < n_tasks; ++t) fn(t, 0);
                return;
            }
            // Les champs de la section ci-dessous appartiennent à un seul appelant à la fois.
            std::lock_guard<std::mutex> serial(section_mutex_);
            job_ctx_ = static_cast<void*>(&fn);
            job_fn_ = &invoke<typename std::remove_reference<F>::type>;
            n_tasks_ = n_tasks;
            next_.value.store(0, std::memory_order_relaxed);
            pending_.value.store(static_cast<long>(workers_.size()), std::memory_order_relaxed);
            // Séquentiellement cohérent, comme le compte des endormis ci-dessous : un worker sur
            // le point de s'endormir soit voit la nouvelle génération, soit est vu endormi.
            generation_.fetch_add(1);
            if (sleeping_.load() > 0)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cv_.notify_all();
            }
            run_tasks(0);
            // Tourner pendant que les workers finissent, puis céder le processeur : un worker que
            // le système a préempté doit pouvoir s'y exécuter.
            unsigned spins = 0;
            while (pending_.value.load(std::memory_order_acquire) != 0)
            {
                if (++spins < 4096) ALBIREO_CPU_RELAX();
                else std::this_thread::yield();
            }
            if (failed_.load(std::memory_order_acquire))
            {
                std::exception_ptr e;
                {
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    e = error_;
                    error_ = nullptr;
                    failed_.store(false, std::memory_order_relaxed);
                }
                std::rethrow_exception(e);
            }
        }

    private:

        template <typename F>
        static void invoke(void* ctx, long task, int thread)
        {
            (*static_cast<F*>(ctx))(task, thread);
        }

        void run_tasks(int thread)
        {
            for (;;)
            {
                const long t = next_.value.fetch_add(1, std::memory_order_relaxed);
                if (t >= n_tasks_) break;
                try
                {
                    job_fn_(job_ctx_, t, thread);
                }
                catch (...)
                {
                    // Les tâches pas encore distribuées sont sautées ; la première exception est gardée
                    // pour l'appelant.
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    if (!error_) error_ = std::current_exception();
                    failed_.store(true, std::memory_order_release);
                    next_.value.store(n_tasks_, std::memory_order_relaxed);
                }
            }
        }

        void stop_workers()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_.store(true, std::memory_order_release);
                generation_.fetch_add(1, std::memory_order_release);
            }
            cv_.notify_all();
            for (auto& t : workers_) t.join();
        }

        void worker_loop(int thread, uint64_t seen)
        {
            for (;;)
            {
                // Tourner un moment, puis dormir jusqu'à la section suivante.
                const auto spin_until = std::chrono::steady_clock::now()
                    + std::chrono::microseconds(spin_us_.load(std::memory_order_relaxed));
                uint64_t g = generation_.load(std::memory_order_acquire);
                unsigned polls = 0;
                while (g == seen)
                {
                    // Après une courte attente active, céder de temps en temps, pour qu'une machine
                    // chargée exécute quand même le thread qui démarrera la section suivante.
                    if (polls > 2048 && (polls & 63) == 0) std::this_thread::yield();
                    else ALBIREO_CPU_RELAX();
                    g = generation_.load(std::memory_order_acquire);
                    if (g != seen) break;
                    if ((++polls & 255) == 0 && std::chrono::steady_clock::now() > spin_until)
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        sleeping_.fetch_add(1);
                        cv_.wait(lock, [&]() {
                            return generation_.load() != seen
                                || stop_.load(std::memory_order_acquire);
                        });
                        sleeping_.fetch_sub(1, std::memory_order_acq_rel);
                        g = generation_.load(std::memory_order_acquire);
                        break;
                    }
                }
                seen = g;
                if (stop_.load(std::memory_order_acquire)) return;
                run_tasks(thread);
                pending_.value.fetch_sub(1, std::memory_order_acq_rel);
            }
        }

        // Un compteur seul sur sa ligne de cache : les compteurs ci-dessous changent à chaque tâche
        // et à chaque worker, tandis que les workers en attente active continuent de lire les champs
        // de la section.
        struct counter_line
        {
            char before[64];
            std::atomic<long> value{0};
            char after[64];
        };

        std::vector<std::thread> workers_;
        // Tenu par le thread dont la section s'exécute, pendant toute la section.
        std::mutex section_mutex_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic<uint64_t> generation_{0};
        std::atomic<int> sleeping_{0};
        std::atomic<long long> spin_us_{2000};
        long n_tasks_ = 0;
        void* job_ctx_ = nullptr;
        void (*job_fn_)(void*, long, int) = nullptr;
        std::atomic<bool> stop_{false};
        std::atomic<bool> failed_{false};
        std::mutex error_mutex_;
        std::exception_ptr error_;
        counter_line next_;
        counter_line pending_;
    };

}

#endif // ALBIREO_ENGINE_THREAD_POOL_H_
