// The team of the performance cores, for the macOS port (Apple Silicon): included by
// thread_pool.h inside class thread_pool, in its public part, when __APPLE__ is defined.
//
// An Apple processor has performance cores and efficiency cores (4 and 4 on an M2).  A pass of
// a few tokens, as a step of the generation, reads every weight of the model for little
// computation: the memory is soon saturated, and at the end of each product the fast cores
// wait for the last group of rows that a slow core took.  Such a pass goes faster on the
// performance cores alone (on an M2, 19.4 tokens/s with 4 threads against 18.2 with 8 for a
// 4B model), while a long prompt, where the computation dominates, goes faster on all the
// cores (163 to 169 tokens/s with 8 threads against 143 to 145 with 4 for 4 096 tokens).
//
// A pool of more threads than performance cores therefore keeps a second, smaller pool, the
// fast team, of one thread per performance core.  The thread that opens the sections of a
// pass asks for it with a fast_team_scope; its sections then run on the fast team, still one
// section at a time over both teams, and the workers of the other team, idle, soon sleep: the
// system then keeps the threads that compute on the performance cores.

        // The number of threads that the sections of the calling thread use.
        int active() const { return fast_team_ && fast_team_requested() ? fast_team_->size() : size(); }

        /*!
            While an object of this type lives, the sections that the thread that made it opens
            run on the fast team when fast is true, on the whole pool otherwise.  Nothing
            changes for a pool without a fast team.
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

        // The performance cores of the processor, 0 when the system does not tell.
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

        // Runs the section on the fast team, if the calling thread asked for it.
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
