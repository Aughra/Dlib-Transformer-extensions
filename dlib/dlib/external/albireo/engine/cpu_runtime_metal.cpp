// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
//
// The passes of the generation on the GPU of the Mac (macOS port): cpu_runtime::set_gpu() and
// the functions it adds.  A pass of up to metal_device::max_tokens tokens of a dense model runs
// whole on the GPU, in one command: the processor writes the embeddings of its tokens, the
// cosines and sines of their positions and the blocks of their sequences into buffers they
// share, the GPU computes every layer, the keys and values going into the region of the pool,
// where the processor reads them too, and the logits of the rows asked for.  A pass of more
// tokens (a prompt), of a mixture of experts or of the images stays on the processor.

#include "cpu_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "../common/engine_api.h"
#include "metal_device.h"

namespace albireo
{

    struct cpu_runtime::gpu_state
    {
        metal_device* device = nullptr;
        std::function<void(int, const std::string&)> log;
        enum { unchecked, ready, dropped } state = unchecked;
        // The passes after a check put off for lack of room, before the next one.
        int wait = 0;

        // The weights: the whole mapping of the file, without copy.
        metal_buffer weights;
        size_t weights_offset = 0;
        const uint8_t* file_base = nullptr;
        // The norms and the biases, copied once, and where each vector starts in them.
        metal_buffer vectors;
        struct layer_vectors
        {
            size_t attn_norm = 0, ffn_norm = 0, q_norm = 0, k_norm = 0, bq = 0, bk = 0, bv = 0;
        };
        std::vector<layer_vectors> layers;
        size_t output_norm = 0;
        // The region of the pool, taken again larger when a block lies beyond it.
        metal_buffer region;
        size_t region_covered = 0;
        // The work buffers of a pass, for capacity tokens.
        metal_buffer x, xn, q, k, v, ctx, gate, up, hidden, logits, cs, blocks, rows, lengths, table, partial;
        size_t capacity = 0, table_capacity = 0, partial_capacity = 0;
        // The weights of a product of a prompt expanded to floats, two buffers in turn.
        metal_buffer expanded[2];
        size_t expanded_floats = 0;
        int turn = 0;

        size_t weight(const weight_matrix& W) const
        {
            return weights_offset + static_cast<size_t>(W.data - file_base);
        }
    };

    cpu_runtime::~cpu_runtime() = default;

    namespace
    {
        bool gpu_format(const weight_matrix& W)
        {
            return W.type == dlib::ggml_type::Q4_K || W.type == dlib::ggml_type::Q6_K;
        }

        metal_buffer need(metal_device& d, size_t bytes)
        {
            metal_buffer b = d.allocate(bytes);
            if (!b.valid()) throw std::runtime_error("the GPU refuses a buffer of " + std::to_string(bytes >> 20) + " MB");
            return b;
        }
    }

    namespace
    {
        /*
            The passes of more tokens than metal_device::max_tokens (prompts) go to the GPU from
            qk_float_product_tokens tokens on, where the processor also turns to products in floats,
            and while the context they end at holds gpu_prompt_context_ positions at most.  Measured on
            the M2 (bench_runtime, keys and values in 8 bits): the GPU reads a prompt of 24 tokens
            about twice as fast as the processor (it expands every weight once a pass, about 200 ms for
            a model of 2B), of 1 024 tokens 1.7 to 2.1 times as fast, of 2 048 tokens 1.2 to 1.7
            times; beyond, its attention, one simdgroup reading the keys after the others for each
            token, falls behind on a model of 2B (4 096 tokens: 134 tokens/s against 173).
        */
        // The defaults of set_gpu_prompts(), which the settings gpu_prompt_tokens and gpu_prompt_context change.
    }

    void cpu_runtime::set_gpu(metal_device& device, std::function<void(int, const std::string&)> log)
    {
        gpu_.reset();
        const long hd = cfg_.head_dim;
        const long rep = cfg_.n_kv_heads > 0 ? cfg_.n_heads / cfg_.n_kv_heads : 0;
        // The models the kernels cover: dense, their heads of 64 to 256 values, every product in Q4_K or Q6_K.
        std::string why;
        if (cfg_.is_moe()) why = "a mixture of experts";
        else if (hd != 64 && hd != 128 && hd != 256) why = "heads of " + std::to_string(hd) + " values";
        else if ((rep != 1 && rep != 2 && rep != 4 && rep != 8) || cfg_.n_heads % cfg_.n_kv_heads != 0)
            why = "query heads grouped otherwise";
        else if (cfg_.attn_temp_scale != 0.0) why = "a temperature of the attention";
        else if (cfg_.d_model % 256 != 0 || cfg_.d_ffn % 256 != 0 || cfg_.q_dim() % 256 != 0)
            why = "widths that are not multiples of 256";
        else if (!gpu_format(output_)) why = "an output matrix in another format than Q4_K or Q6_K";
        else
            for (const layer& L : layers_)
                for (const weight_matrix* W : { &L.wq, &L.wk, &L.wv, &L.wo, &L.w_gate, &L.w_up, &L.w_down })
                    if (!gpu_format(*W)) why = "weights in other formats than Q4_K and Q6_K";
        if (!why.empty())
        {
            if (log) log(ALBIREO_LOG_INFO, "GPU: the passes stay on the processor, the kernels do not cover " + why);
            note_gpu("the kernels do not cover " + why);
            return;
        }
        std::unique_ptr<gpu_state> g(new gpu_state);
        g->device = &device;
        g->log = std::move(log);
        g->file_base = file_->data();
        g->weights = device.wrap(file_->data(), static_cast<size_t>(file_->size()), g->weights_offset);
        if (!g->weights.valid())
        {
            if (g->log) g->log(ALBIREO_LOG_WARNING, "GPU: the weights cannot be taken without copy, the passes stay on the processor");
            note_gpu("the weights cannot be taken without copy");
            return;
        }
        // The norms and the biases, one after the other.
        std::vector<float> all;
        auto add = [&](const std::vector<float>& v) {
            const size_t at = all.size() * sizeof(float);
            all.insert(all.end(), v.begin(), v.end());
            // Each vector starts on 16 bytes.
            while (all.size() % 4) all.push_back(0.0f);
            return at;
        };
        for (const layer& L : layers_)
        {
            gpu_state::layer_vectors lv;
            lv.attn_norm = add(L.attn_norm);
            lv.ffn_norm = add(L.ffn_norm);
            lv.q_norm = add(L.q_norm);
            lv.k_norm = add(L.k_norm);
            lv.bq = add(L.bq);
            lv.bk = add(L.bk);
            lv.bv = add(L.bv);
            g->layers.push_back(lv);
        }
        g->output_norm = add(output_norm_);
        g->vectors = need(device, all.size() * sizeof(float));
        std::memcpy(g->vectors.contents(), all.data(), all.size() * sizeof(float));
        // The logits and the hidden states of the rows asked for, max_tokens at most.
        const size_t R = metal_device::max_tokens;
        g->hidden = need(device, R * cfg_.d_model * sizeof(float));
        g->logits = need(device, R * cfg_.vocab * sizeof(float));
        gpu_ = std::move(g);
        grow_gpu(metal_device::max_tokens);
    }

    // The work buffers of the passes, grown to tokens tokens.
    void cpu_runtime::grow_gpu(size_t tokens)
    {
        gpu_state& g = *gpu_;
        if (g.capacity >= tokens) return;
        metal_device& device = *g.device;
        const size_t T = tokens, f = sizeof(float);
        g.x = need(device, T * cfg_.d_model * f);
        g.xn = need(device, T * cfg_.d_model * f);
        g.q = need(device, T * cfg_.q_dim() * f);
        g.k = need(device, T * cfg_.kv_dim() * f);
        g.v = need(device, T * cfg_.kv_dim() * f);
        g.ctx = need(device, T * cfg_.q_dim() * f);
        g.gate = need(device, T * cfg_.d_ffn * f);
        g.up = need(device, T * cfg_.d_ffn * f);
        g.cs = need(device, T * std::max<long>(cfg_.rope_dims, 2) * f);
        g.blocks = need(device, T * sizeof(int));
        g.rows = need(device, T * sizeof(int));
        g.lengths = need(device, T * sizeof(int));
        g.capacity = T;
        note_gpu();
    }

    bool cpu_runtime::takes_gpu(const forward_batch& batch) const
    {
        const long T = static_cast<long>(batch.tokens.size());
        const bool step = T > 0 && T <= static_cast<long>(metal_device::max_tokens);
        bool long_pass = T >= gpu_prompt_min_ && T <= gpu_prompt_context_;
        if (long_pass)
            for (const auto& s : batch.segments)
            {
                const size_t i = static_cast<size_t>(s.sequence);
                if (i >= sequences_.size() || !sequences_[i] || sequences_[i]->length + s.count > gpu_prompt_context_)
                    long_pass = false;
            }
        return gpu_ && gpu_->state != gpu_state::dropped && (step || long_pass)
            && batch.embedding_rows.empty() && !batch.hidden_only
            && batch.logit_rows.size() <= metal_device::max_tokens;
    }

    void cpu_runtime::drop_gpu(const std::string& why)
    {
        if (!gpu_ || gpu_->state == gpu_state::dropped) return;
        gpu_->state = gpu_state::dropped;
        if (gpu_->log) gpu_->log(ALBIREO_LOG_WARNING, "GPU: the passes go back to the processor: " + why);
        note_gpu(why);
    }

    void cpu_runtime::note_gpu(const std::string& reason)
    {
        gpu_usage u;
        if (gpu_ && gpu_->state != gpu_state::dropped)
        {
            const gpu_state& g = *gpu_;
            u.state = g.state == gpu_state::ready ? "on" : "checking";
            for (const metal_buffer* b : { &g.vectors, &g.x, &g.xn, &g.q, &g.k, &g.v, &g.ctx, &g.gate, &g.up,
                     &g.hidden, &g.logits, &g.cs, &g.blocks, &g.rows, &g.lengths, &g.table, &g.partial,
                     &g.expanded[0], &g.expanded[1] })
                u.bytes += b->bytes();
        }
        else
            u.reason = reason;
        std::lock_guard<std::mutex> lock(gpu_report_m_);
        gpu_report_ = u;
    }

    cpu_runtime::gpu_usage cpu_runtime::gpu_use() const
    {
        std::lock_guard<std::mutex> lock(gpu_report_m_);
        return gpu_report_;
    }

    /*
        The check of the GPU, at its first pass: two sequences read the same prompt on the
        processor, then take the same two steps, one on the GPU, the other on the processor.
        The processor computes its products in single precision floats (float_products_), as the
        GPU does: with the 8-bit activations of its own kernels, the logits of a model of 28
        layers differ from exact ones by a few percent, which would hide a defect of the GPU or
        refuse a sound one.  The logits of each step agree within 1 % and give the same most
        likely token, or the passes stay on the processor.  A check that lacks room for its
        sequences waits a few passes.
    */
    bool cpu_runtime::gpu_ready()
    {
        if (!gpu_ || gpu_->state == gpu_state::dropped) return false;
        if (gpu_->state == gpu_state::ready) return true;
        if (gpu_->wait > 0)
        {
            --gpu_->wait;
            return false;
        }
        // A prompt long enough to take the path of the prompts on the GPU.
        const long n = std::max<long>(32, gpu_prompt_min_), V = cfg_.vocab;
        const int a = create_sequence(), b = create_sequence();
        struct release
        {
            cpu_runtime& rt;
            int a, b;
            ~release()
            {
                rt.release_sequence(a);
                rt.release_sequence(b);
            }
        } guard{ *this, a, b };
        if (!reserve_sequence(a, n + 2) || !reserve_sequence(b, n + 2))
        {
            gpu_->wait = 64;
            return false;
        }
        forward_batch prompt;
        for (long i = 0; i < n; ++i) prompt.tokens.push_back(static_cast<int>((1009 + 7919 * i) % V));
        std::vector<float> on_gpu, on_cpu;
        struct floats_scope
        {
            bool& flag;
            ~floats_scope() { flag = false; }
        } floats{ float_products_ };
        float_products_ = true;
        std::ostringstream detail;
        bool agree = true;
        // The logits of a pass on the GPU against those of the processor, into detail.
        auto compare = [&](const char* what, double limit) {
            double diff = 0, norm = 0;
            long ig = 0, ic = 0;
            for (long i = 0; i < V; ++i)
            {
                diff += (static_cast<double>(on_gpu[i]) - on_cpu[i]) * (static_cast<double>(on_gpu[i]) - on_cpu[i]);
                norm += static_cast<double>(on_cpu[i]) * on_cpu[i];
                if (on_gpu[i] > on_gpu[ig]) ig = i;
                if (on_cpu[i] > on_cpu[ic]) ic = i;
            }
            const double relative = std::sqrt(diff / std::max(norm, 1e-30));
            detail << (detail.tellp() > 0 ? "; " : "") << what << ": logits within " << relative * 100
                << "% of those of the processor, most likely token " << (ig == ic ? "the same" : "different");
            // Written so that a value that is not a number fails.
            if (!(relative <= limit) || ig != ic) agree = false;
        };
        // The prompt: on the GPU for the first sequence when the prompts go to it, on the processor otherwise.
        const bool gpu_prompt = n >= gpu_prompt_min_ && n <= gpu_prompt_context_;
        prompt.logit_rows.assign(1, n - 1);
        prompt.segments.assign(1, { a, 0, n });
        try
        {
            if (gpu_prompt) run_gpu(prompt, on_gpu);
            else run(prompt, on_gpu, false);
        }
        catch (const std::exception& e)
        {
            drop_gpu(std::string("the check failed on the GPU: ") + e.what());
            return false;
        }
        prompt.segments.assign(1, { b, 0, n });
        run(prompt, on_cpu, false);
        /*
            A prompt writes the keys and values of all its tokens: in 8 bits, a value that a difference
            of rounding of 1e-7 moves across the middle of two integers takes the other one, a step of
            about 1 % of its group, which the next layers carry and multiply.  Its logits then differ
            from those of the processor by one or two percent with sound kernels (1.5 % on the 2B,
            16 tokens; 0.04 % with the keys and values in half precision).  A defect gives far more,
            and another most likely token.
        */
        if (gpu_prompt) compare("prompt", kv_.format() == kv_format::q8 ? 0.05 : 0.01);
        for (int step = 0; step < 2; ++step)
        {
            forward_batch one;
            one.tokens.push_back(static_cast<int>((4243 + 104729 * step) % V));
            one.logit_rows.push_back(0);
            one.segments.assign(1, { a, 0, 1 });
            try
            {
                run_gpu(one, on_gpu);
            }
            catch (const std::exception& e)
            {
                drop_gpu(std::string("the check failed on the GPU: ") + e.what());
                return false;
            }
            one.segments.assign(1, { b, 0, 1 });
            run(one, on_cpu, false);
            compare(step == 0 ? "step 1" : "step 2", 0.01);
        }
        if (!agree)
        {
            drop_gpu("its check disagrees with the processor (" + detail.str() + ")");
            return false;
        }
        gpu_->state = gpu_state::ready;
        note_gpu();
        if (gpu_->log)
            gpu_->log(ALBIREO_LOG_INFO, "GPU " + gpu_->device->name() + ": the generation steps"
                + std::string(gpu_prompt ? " and the prompts of " + std::to_string(gpu_prompt_min_)
                    + " tokens or more, up to " + std::to_string(gpu_prompt_context_) + " positions," : "")
                + " run on it (" + detail.str() + ")");
        return true;
    }

    void cpu_runtime::run_gpu(const forward_batch& batch, std::vector<float>& logits)
    {
        gpu_state& g = *gpu_;
        metal_device& dev = *g.device;
        const long T = static_cast<long>(batch.tokens.size());
        const long d = cfg_.d_model, qd = cfg_.q_dim(), kvd = cfg_.kv_dim(), hd = cfg_.head_dim, df = cfg_.d_ffn;
        const long H = cfg_.n_heads, KVH = cfg_.n_kv_heads, rep = H / KVH, B = kv_.block_positions();
        const long half = cfg_.rope_dims / 2;
        const float eps = static_cast<float>(cfg_.rms_eps), rs = static_cast<float>(cfg_.residual_scale);
        const bool q8 = kv_.format() == kv_format::q8;

        // The positions of the tokens, which continue their sequences, as run() checks them.
        positions_.assign(static_cast<size_t>(T), 0);
        long max_pos = 0;
        std::vector<int> seq_of(static_cast<size_t>(T));
        for (const auto& s : batch.segments)
        {
            kv_sequence& seq = *sequences_.at(static_cast<size_t>(s.sequence));
            if (static_cast<long>(seq.blocks.size()) * B < seq.length + s.count)
                throw std::runtime_error("cpu_runtime: sequence storage was not reserved");
            if (s.count > 0 && kv_.shared(seq.blocks[static_cast<size_t>(seq.length / B)]))
                throw std::runtime_error("cpu_runtime: a sequence would write in a block other sequences hold");
            for (long j = 0; j < s.count; ++j)
            {
                positions_[static_cast<size_t>(s.start + j)] = seq.length + j;
                seq_of[static_cast<size_t>(s.start + j)] = s.sequence;
                max_pos = std::max(max_pos, seq.length + j);
            }
        }
        // The rotary positions the batch gives (the multimodal models give them for every token, the
        // same three components for a token of text), as run() takes them.
        const bool given = !batch.positions.empty();
        long max_rope = max_pos;
        if (given)
        {
            if (static_cast<long>(batch.positions.size()) != T * forward_batch::position_components)
                throw std::runtime_error("cpu_runtime: the batch gives positions for other tokens than its own");
            for (long p : batch.positions)
            {
                if (p < 0) throw std::runtime_error("cpu_runtime: the batch gives a negative position");
                max_rope = std::max(max_rope, p);
            }
        }
        ensure_rope(max_rope + 1);
        if (given) prepare_given_positions(batch, T);
        embed(batch.tokens);
        grow_gpu(static_cast<size_t>(T));
        std::memcpy(g.x.contents(), x_.data(), sizeof(float) * T * d);

        // The cosines and sines of each token, the block and the row each one writes, the length it
        // sees, and the blocks of its sequence.
        float* cs = static_cast<float*>(g.cs.contents());
        int* blk = static_cast<int*>(g.blocks.contents());
        int* row = static_cast<int*>(g.rows.contents());
        int* len = static_cast<int*>(g.lengths.contents());
        size_t table_stride = 1, last_block = 0;
        for (long t = 0; t < T; ++t)
            table_stride = std::max(table_stride, sequences_[static_cast<size_t>(seq_of[static_cast<size_t>(t)])]->blocks.size());
        if (g.table_capacity < table_stride * T)
        {
            g.table_capacity = table_stride * T * 2;
            g.table = need(dev, g.table_capacity * sizeof(int));
            note_gpu();
        }
        int* table = static_cast<int*>(g.table.contents());
        for (long t = 0; t < T; ++t)
        {
            const long pos = positions_[static_cast<size_t>(t)];
            const float* c = given ? given_cos_.data() + t * half : rope_cos_.data() + pos * half;
            const float* s = given ? given_sin_.data() + t * half : rope_sin_.data() + pos * half;
            std::memcpy(cs + t * 2 * half, c, sizeof(float) * half);
            std::memcpy(cs + t * 2 * half + half, s, sizeof(float) * half);
            const kv_sequence& seq = *sequences_[static_cast<size_t>(seq_of[static_cast<size_t>(t)])];
            blk[t] = seq.blocks[static_cast<size_t>(pos / B)];
            row[t] = static_cast<int>(pos % B);
            len[t] = static_cast<int>(pos + 1);
            for (size_t j = 0; j < table_stride; ++j)
            {
                const int b = j < seq.blocks.size() ? seq.blocks[j] : seq.blocks[0];
                table[t * table_stride + j] = b;
                if (j < seq.blocks.size()) last_block = std::max(last_block, static_cast<size_t>(b));
            }
        }
        // The region of the pool, covering every block of these sequences.
        const size_t covered = kv_.block_offset(static_cast<int>(last_block) + 1);
        if (g.region_covered < covered)
        {
            const size_t want = std::min(kv_.region_bytes(), std::max(covered * 3 / 2, static_cast<size_t>(256) << 20));
            size_t at = 0;
            metal_buffer r = dev.wrap(kv_.region(), want, at);
            if (!r.valid() || at != 0) throw std::runtime_error("the GPU cannot take the keys and values without copy");
            g.region = r;
            g.region_covered = want;
        }
        // The attention of a step goes in parts of attention_part positions; a prompt has enough
        // tokens to fill the GPU with one part each.
        const bool step = T <= static_cast<long>(metal_device::max_tokens);
        const unsigned part_len = step ? metal_device::attention_part : static_cast<unsigned>(max_pos + 1);
        const unsigned parts = static_cast<unsigned>((max_pos + 1 + part_len - 1) / part_len);
        const size_t partial = parts > 1 ? static_cast<size_t>(T) * KVH * parts * rep * (hd + 2) : 1;
        if (g.partial_capacity < partial)
        {
            g.partial_capacity = partial * 2;
            g.partial = need(dev, g.partial_capacity * sizeof(float));
            note_gpu();
        }
        // The weights of a prompt expanded to floats: the largest matrix of a layer, twice.
        if (!step && g.expanded_floats == 0)
        {
            size_t largest = 0;
            for (const layer& L : layers_)
                for (const weight_matrix* M : { &L.wq, &L.wk, &L.wv, &L.wo, &L.w_gate, &L.w_up, &L.w_down })
                    largest = std::max(largest, static_cast<size_t>(M->rows) * static_cast<size_t>(M->cols));
            g.expanded[0] = need(dev, largest * sizeof(float));
            g.expanded[1] = need(dev, largest * sizeof(float));
            g.expanded_floats = largest;
            note_gpu();
        }

        const float scale = cfg_.attention_scale > 0.0 ? static_cast<float>(cfg_.attention_scale)
            : 1.0f / std::sqrt(static_cast<float>(hd));
        const unsigned uT = static_cast<unsigned>(T);
        auto c = dev.begin();
        auto at = [&](const metal_buffer& b, size_t offset) { return metal_view(b, offset); };
        // A product of the pass: by blocks in place for a step, expanded then through MPS for a prompt.
        auto product = [&](const weight_matrix& M, const metal_buffer& x, long x_stride, const metal_buffer& y, long y_stride,
                           float alpha, float beta) {
            if (step)
            {
                c->matvec(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, x, uT, x_stride, y, y_stride, alpha, beta);
                return;
            }
            const metal_buffer& e = g.expanded[g.turn];
            g.turn ^= 1;
            c->dequantize(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, e, M.cols);
            c->gemm(x, uT, M.cols, x_stride, e, M.rows, M.cols, y, y_stride, alpha, beta);
        };
        for (long li = 0; li < cfg_.n_layers; ++li)
        {
            const layer& L = layers_[static_cast<size_t>(li)];
            const gpu_state::layer_vectors& lv = g.layers[static_cast<size_t>(li)];
            metal_device::commands::kv_layout kl;
            kl.hd = static_cast<unsigned>(hd);
            kl.heads = static_cast<unsigned>(KVH);
            kl.positions = static_cast<unsigned>(B);
            kl.q8 = q8;
            kl.block_stride = kv_.block_offset(1);
            kl.k_slab = kv_.slab_offset(false, li, 0);
            kl.v_slab = kv_.slab_offset(true, li, 0);
            kl.head_stride = kv_.slab_offset(false, li, 1) - kv_.slab_offset(false, li, 0);

            // Attention block.
            c->rms_norm(g.x, at(g.vectors, lv.attn_norm), g.xn, uT, d, d, d, eps);
            if (step && L.wq.type == L.wk.type && qd % 16 == 0)
                c->matvec2(L.wq.type, at(g.weights, g.weight(L.wq)), qd, at(g.weights, g.weight(L.wk)), kvd, d, g.xn, uT, d,
                    g.q, qd, g.k, kvd);
            else
            {
                product(L.wq, g.xn, d, g.q, qd, 1.0f, 0.0f);
                product(L.wk, g.xn, d, g.k, kvd, 1.0f, 0.0f);
            }
            product(L.wv, g.xn, d, g.v, kvd, 1.0f, 0.0f);
            if (!L.bq.empty())
            {
                c->add_bias(g.q, at(g.vectors, lv.bq), qd, qd, uT);
                c->add_bias(g.k, at(g.vectors, lv.bk), kvd, kvd, uT);
                c->add_bias(g.v, at(g.vectors, lv.bv), kvd, kvd, uT);
            }
            if (!L.q_norm.empty())
            {
                c->rms_norm(g.q, at(g.vectors, lv.q_norm), g.q, uT * H, hd, hd, hd, eps);
                c->rms_norm(g.k, at(g.vectors, lv.k_norm), g.k, uT * KVH, hd, hd, hd, eps);
            }
            const bool neox = cfg_.rope == rope_style::neox;
            c->rope(g.q, g.cs, uT, H, hd, half, neox, qd, 2 * half);
            c->rope(g.k, g.cs, uT, KVH, hd, half, neox, kvd, 2 * half);
            c->kv_store(g.k, g.v, kvd, g.region, g.blocks, g.rows, uT, kl);
            c->prepare_queries(g.q, uT, H, hd, qd, scale, q8);
            c->attention(g.q, g.region, g.table, g.lengths, static_cast<unsigned>(table_stride), uT, rep, parts, qd,
                g.partial, g.ctx, kl, part_len);
            product(L.wo, g.ctx, qd, g.x, d, rs, 1.0f);

            // Feed-forward block.
            c->rms_norm(g.x, at(g.vectors, lv.ffn_norm), g.xn, uT, d, d, d, eps);
            if (step && L.w_gate.type == L.w_up.type && df % 16 == 0)
                c->matvec2(L.w_gate.type, at(g.weights, g.weight(L.w_gate)), df, at(g.weights, g.weight(L.w_up)), df, d,
                    g.xn, uT, d, g.gate, df, g.up, df);
            else
            {
                product(L.w_gate, g.xn, d, g.gate, df, 1.0f, 0.0f);
                product(L.w_up, g.xn, d, g.up, df, 1.0f, 0.0f);
            }
            c->silu_mul(g.gate, g.up, static_cast<unsigned>(T * df));
            product(L.w_down, g.gate, df, g.x, d, rs, 1.0f);
        }
        // Final normalization and logits of the requested rows only.
        const long R = static_cast<long>(batch.logit_rows.size());
        for (long r = 0; r < R; ++r)
            c->rms_norm(at(g.x, sizeof(float) * batch.logit_rows[static_cast<size_t>(r)] * d), at(g.vectors, g.output_norm),
                at(g.hidden, sizeof(float) * r * d), 1, d, d, d, eps);
        const float logit = cfg_.logit_scale != 1.0 ? static_cast<float>(1.0 / cfg_.logit_scale) : 1.0f;
        if (R > 0)
            c->matvec(output_.type, at(g.weights, g.weight(output_)), cfg_.vocab, d, g.hidden, static_cast<unsigned>(R), d,
                g.logits, cfg_.vocab, logit);
        c->finish();

        for (const auto& s : batch.segments)
            sequences_[static_cast<size_t>(s.sequence)]->length += s.count;
        logits.resize(static_cast<size_t>(R * cfg_.vocab));
        if (R > 0) std::memcpy(logits.data(), g.logits.contents(), sizeof(float) * R * cfg_.vocab);
        hidden_out_.resize(static_cast<size_t>(R * d));
        if (R > 0) std::memcpy(hidden_out_.data(), g.hidden.contents(), sizeof(float) * R * d);
    }

}
