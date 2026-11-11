// Produits des longues passes via Accelerate, pour le portage macOS (Apple Silicon),
// inclus par qkernels.cpp dans son espace de noms anonyme avant matmul_float() quand
// ALBIREO_QK_ACCELERATE est défini.

    /*
        Apple : les produits des longues passes via cblas_sgemm d'Accelerate, qui les exécute
        sur l'unité matricielle du processeur, à plusieurs fois le débit des noyaux NEON.
        Un appel est bien plus rapide que de nombreux petits appels depuis les threads du thread
        pool (l'unité est partagée par les cœurs d'un cluster) : les poids sont développés en
        floats par tranches de accelerate_slice lignes, et pendant qu'un appel multiplie une
        tranche, les autres threads développent la suivante.  La tâche 0 de chaque section, que
        le thread appelant prend en premier, fait l'appel ; les autres tâches développent des
        groupes de lignes.  Les sommes sont celles d'Accelerate : elles diffèrent de qk_dot_f32()
        dans les derniers bits, comme le font les multiplications-additions fusionnées des noyaux
        AVX2.
    */
    const long accelerate_slice = 256;
    const long accelerate_group = 16;
    /*
        En dessous de ce nombre de tokens, le développement des poids, dont le coût ne dépend
        pas des tokens, rend l'appel plus lent que les noyaux NEON (6144 lignes de 2048 colonnes,
        Q4_K et Q8_0, Mac M2, 8 threads : 2,3 ms contre 1,7 ms à 8 tokens, à peu près égal à 16,
        2,6 contre 3,4 ms à 24).  Il sert les experts d'un mélange, qui peuvent recevoir quelques
        tokens d'une longue passe.
    */
    const long accelerate_min_tokens = 16;

    void matmul_accelerate(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
        float* Y, long y_stride, thread_pool& pool, bool accumulate)
    {
        const long rows = W.rows, K = W.cols;
        const float beta = accumulate ? 1.0f : 0.0f;
        auto product = [&](const float* w, long r0, long n) {
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, static_cast<int>(n_tokens),
                static_cast<int>(n), static_cast<int>(K), 1.0f, X, static_cast<int>(x_stride),
                w, static_cast<int>(K), beta, Y + r0, static_cast<int>(y_stride));
        };
        if (W.type == ggml_type::F32)
        {
            product(reinterpret_cast<const float*>(W.data), 0, rows);
            return;
        }
        static thread_local std::vector<float> buffers[2];
        const long slice_floats = accelerate_slice * K;
        for (auto& b : buffers)
            if (b.size() < static_cast<size_t>(slice_floats)) b.resize(static_cast<size_t>(slice_floats));
        const long n_slices = (rows + accelerate_slice - 1) / accelerate_slice;
        const long groups = accelerate_slice / accelerate_group;
        // La section s multiplie la tranche s - 1 et développe la tranche s.
        for (long s = 0; s <= n_slices; ++s)
        {
            float* next = buffers[s % 2].data();
            const float* ready = buffers[(s + 1) % 2].data();
            const long r_next = s * accelerate_slice;
            const long n_next = s < n_slices ? std::min(accelerate_slice, rows - r_next) : 0;
            pool.parallel_for(1 + groups, [&](long task, int) {
                if (task == 0)
                {
                    if (s > 0)
                    {
                        const long r0 = (s - 1) * accelerate_slice;
                        product(ready, r0, std::min(accelerate_slice, rows - r0));
                    }
                    return;
                }
                const long g0 = (task - 1) * accelerate_group;
                const long g1 = std::min(n_next, g0 + accelerate_group);
                for (long r = g0; r < g1; ++r)
                    qk_dequantize_row(W.type, W.data + static_cast<size_t>(r_next + r) * W.row_bytes,
                        next + r * K, K);
            });
        }
    }

    /*
        Les deux produits d'une tuile de l'attention de l'encodeur (qk_encoder_attention), via
        Accelerate également : les scores S = Q Kt de rows requêtes contre n clés, et les
        valeurs pondérées O += P V.  Les tuiles sont petites (64 requêtes, 128 clés) et les threads
        du thread pool les appellent ensemble, ce qui coûte à Accelerate une bonne part de son
        débit, mais il reste loin devant les boucles portables, qui lisent Kt en travers de ses
        lignes un float à la fois.
    */
    void encoder_scores_accelerate(const float* Q, long rows, long hd, const float* Kt, long ldk, long n,
        float* S, long lds)
    {
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, static_cast<int>(rows), static_cast<int>(n),
            static_cast<int>(hd), 1.0f, Q, static_cast<int>(hd), Kt, static_cast<int>(ldk), 0.0f, S,
            static_cast<int>(lds));
    }

    void encoder_values_accelerate(const float* P, long lds, long rows, const float* V, long ldv, long n,
        long hd, float* O, long ldo)
    {
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, static_cast<int>(rows), static_cast<int>(hd),
            static_cast<int>(n), 1.0f, P, static_cast<int>(lds), V, static_cast<int>(ldv), 1.0f, O,
            static_cast<int>(ldo));
    }
