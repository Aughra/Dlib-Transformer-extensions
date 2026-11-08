// Products of the long passes through Accelerate, for the macOS port (Apple Silicon),
// included by qkernels.cpp inside its anonymous namespace before matmul_float() when
// ALBIREO_QK_ACCELERATE is set.

    /*
        Apple: the products of the long passes through cblas_sgemm of Accelerate, which runs
        them on the matrix unit of the processor, several times the rate of the NEON kernels.
        One call is far faster than many small ones from the threads of the pool (the unit is
        shared by the cores of a cluster): the weights are expanded to floats by slices of
        accelerate_slice rows, and while one call multiplies a slice, the other threads expand
        the next one.  Task 0 of each section, which the calling thread takes first, makes the
        call; the other tasks expand groups of rows.  The sums are those of Accelerate: they
        differ from qk_dot_f32() in the last bits, as the fused multiply-adds of the AVX2
        kernels do.
    */
    const long accelerate_slice = 256;
    const long accelerate_group = 16;
    /*
        Below this number of tokens, the expansion of the weights, whose cost does not depend
        on the tokens, makes the call slower than the NEON kernels (6144 rows of 2048 columns,
        Q4_K and Q8_0, Mac M2, 8 threads: 2.3 ms against 1.7 ms at 8 tokens, about even at 16,
        2.6 against 3.4 ms at 24).  It serves the experts of a mixture, which can receive a few
        tokens of a long pass.
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
        // Section s multiplies slice s - 1 and expands slice s.
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
