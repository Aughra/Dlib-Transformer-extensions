// L'équipe des cœurs de performance, pour le portage macOS (Apple Silicon) : incluse par
// thread_pool.h dans la classe thread_pool, dans sa partie publique, quand __APPLE__ est défini.
//
// Un processeur Apple a des cœurs de performance et des cœurs d'efficacité (4 et 4 sur un M2).  Une
// passe de quelques tokens, comme une étape de la génération, lit chaque poids du modèle pour peu de
// calcul : la mémoire est vite saturée, et à la fin de chaque produit les cœurs rapides attendent le
// dernier groupe de lignes qu'un cœur lent a pris.  Une telle passe va plus vite sur les seuls cœurs
// de performance (sur un M2, 19,4 tokens/s avec 4 threads contre 18,2 avec 8 pour un modèle 4B),
// alors qu'un long prompt, où le calcul domine, va plus vite sur tous les cœurs (163 à 169 tokens/s
// avec 8 threads contre 143 à 145 avec 4 pour 4 096 tokens).
//
// Un thread pool de plus de threads qu'il n'y a de cœurs de performance garde donc un second thread
// pool, plus petit, l'équipe rapide, d'un thread par cœur de performance.  Le thread qui ouvre les
// sections d'une passe la demande avec un fast_team_scope ; ses sections s'exécutent alors sur
// l'équipe rapide, toujours une section à la fois sur les deux équipes, et les workers de l'autre
// équipe, inactifs, s'endorment vite : le système garde alors les threads qui calculent sur les
// cœurs de performance.

        // Le nombre de threads qu'utilisent les sections du thread appelant.
        int active() const { return fast_team_ && fast_team_requested() ? fast_team_->size() : size(); }

        /*!
            Tant qu'un objet de ce type existe, les sections qu'ouvre le thread qui l'a créé
            s'exécutent sur l'équipe rapide quand fast vaut true, sur tout le thread pool sinon.
            Rien ne change pour un thread pool sans équipe rapide.
        !*/
        class fast_team_scope
        {
        public:
            explicit fast_team_scope(bool fast) : previous_(fast_team_requested()) { fast_team_requested() = fast; }
            ~fast_team_scope() { fast_team_requested() = previous_; }
            fast_team_scope(const fast_team_scope&) = delete;
            fast_team_scope& operator=(const fast_team_scope&) = delete;

        private:
            bool previous_;
        };

    private:

        static bool& fast_team_requested()
        {
            thread_local bool fast = false;
            return fast;
        }

        // Les cœurs de performance du processeur, 0 quand le système ne le dit pas.
        static int performance_cores()
        {
            int n = 0;
            size_t length = sizeof(n);
            if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &length, nullptr, 0) != 0) return 0;
            return n;
        }

        void make_fast_team(int threads)
        {
            const int fast = performance_cores();
            if (fast > 1 && fast < threads) fast_team_.reset(new thread_pool(fast));
        }

        // Exécute la section sur l'équipe rapide, si le thread appelant l'a demandé.
        template <typename F>
        bool run_on_fast_team(long n_tasks, F& fn)
        {
            if (!fast_team_ || !fast_team_requested()) return false;
            std::lock_guard<std::mutex> serial(section_mutex_);
            fast_team_->spin_us_.store(spin_us_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            fast_team_->parallel_for(n_tasks, fn);
            return true;
        }

        std::unique_ptr<thread_pool> fast_team_;

    public:
