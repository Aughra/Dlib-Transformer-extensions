// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "cpu_runtime.h"

#include <sys/mman.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace albireo
{

    namespace
    {
        float yarn_corr_dim(long n_dims, double n_ctx_orig, double n_rot, double base)
        {
            return static_cast<float>(n_dims * std::log(n_ctx_orig / (n_rot * 2.0 * 3.14159265358979323846))
                / (2.0 * std::log(base)));
        }

        float yarn_ramp(float low, float high, long i0)
        {
            const float y = (static_cast<float>(i0 / 2) - low) / std::max(0.001f, high - low);
            return 1.0f - std::min(1.0f, std::max(0.0f, y));
        }

        // Les lignes [first, first + count) d'une matrice, comme une matrice à part entière dans sa mémoire.
        weight_matrix row_slice(const weight_matrix& w, long first, long count)
        {
            weight_matrix s = w;
            s.data = w.data + static_cast<size_t>(first) * w.row_bytes;
            s.rows = count;
            return s;
        }
    }

    cpu_runtime::cpu_runtime(std::shared_ptr<model_file> file, const model_config& config, thread_pool& pool,
        size_t kv_budget_bytes, long kv_block_positions, kv_format kv_type)
        : file_(std::move(file)), cfg_(config), pool_(pool),
          kv_(config.n_attention_layers(), config.n_kv_heads, config.head_dim, kv_block_positions, kv_budget_bytes,
              kv_type)
    {
        const model_file& f = *file_;
        const long d = cfg_.d_model;
        const long qd = cfg_.q_dim();
        const long kvd = cfg_.kv_dim();
        const long hd = cfg_.head_dim;

        // Le bloc feed-forward d'une couche : dense, à experts, et l'expert partagé de qwen35moe.
        auto load_ffn = [&](layer& L, const std::string& p) {
            if (cfg_.is_moe())
            {
                const long n_exp = cfg_.n_experts;
                const long df = cfg_.d_ffn_expert;
                L.router = f.matrix(p + "ffn_gate_inp.weight", n_exp, d);
                for (long e = 0; e < n_exp; ++e)
                {
                    L.e_gate.push_back(f.expert_matrix(p + "ffn_gate_exps.weight", df, d, e, n_exp));
                    L.e_up.push_back(f.expert_matrix(p + "ffn_up_exps.weight", df, d, e, n_exp));
                    L.e_down.push_back(f.expert_matrix(p + "ffn_down_exps.weight", d, df, e, n_exp));
                }
            }
            else if (cfg_.fused_gate_up)
            {
                // Les lignes de la porte, puis celles de la projection up.
                const weight_matrix gate_up = f.matrix(p + "ffn_up.weight", 2 * cfg_.d_ffn, d);
                L.w_gate = row_slice(gate_up, 0, cfg_.d_ffn);
                L.w_up = row_slice(gate_up, cfg_.d_ffn, cfg_.d_ffn);
                L.w_down = f.matrix(p + "ffn_down.weight", d, cfg_.d_ffn);
            }
            else
            {
                L.w_gate = f.matrix(p + "ffn_gate.weight", cfg_.d_ffn, d);
                L.w_up = f.matrix(p + "ffn_up.weight", cfg_.d_ffn, d);
                L.w_down = f.matrix(p + "ffn_down.weight", d, cfg_.d_ffn);
            }
            if (cfg_.d_ffn_shared > 0)
            {
                const long ds = cfg_.d_ffn_shared;
                L.sh_gate = f.matrix(p + "ffn_gate_shexp.weight", ds, d);
                L.sh_up = f.matrix(p + "ffn_up_shexp.weight", ds, d);
                L.sh_down = f.matrix(p + "ffn_down_shexp.weight", d, ds);
                L.sh_gate_inp = f.floats(p + "ffn_gate_inp_shexp.weight", static_cast<size_t>(d));
            }
        };

        layers_.resize(static_cast<size_t>(cfg_.n_layers));
        kv_slot_.assign(static_cast<size_t>(cfg_.n_layers), -1);
        linear_slot_.assign(static_cast<size_t>(cfg_.n_layers), -1);
        long n_attention = 0, n_linear = 0;
        for (long i = 0; i < cfg_.n_layers; ++i)
        {
            const std::string p = "blk." + std::to_string(i) + ".";
            layer& L = layers_[static_cast<size_t>(i)];
            L.attn_norm = f.floats(p + "attn_norm.weight", static_cast<size_t>(d));
            // qwen35 nomme post_attention_norm la norme d'avant le bloc feed-forward.
            const std::string ffn_norm = cfg_.recurrent() ? "post_attention_norm.weight" : "ffn_norm.weight";
            if (cfg_.linear_layer(i))
            {
                const long C = cfg_.ssm_conv_dim(), VD = cfg_.ssm_value_dim(), Hv = cfg_.ssm_v_heads;
                linear_slot_[static_cast<size_t>(i)] = n_linear++;
                L.w_mix = f.matrix(p + "attn_qkv.weight", C, d);
                L.w_z = f.matrix(p + "attn_gate.weight", VD, d);
                L.w_alpha = f.matrix(p + "ssm_alpha.weight", Hv, d);
                L.w_beta = f.matrix(p + "ssm_beta.weight", Hv, d);
                L.w_ssm_out = f.matrix(p + "ssm_out.weight", d, VD);
                L.conv1d = f.floats(p + "ssm_conv1d.weight", static_cast<size_t>(C * cfg_.ssm_conv_kernel));
                L.dt_bias = f.floats(p + "ssm_dt.bias", static_cast<size_t>(Hv));
                L.ssm_a = f.floats(p + "ssm_a", static_cast<size_t>(Hv));
                L.ssm_norm = f.floats(p + "ssm_norm.weight", static_cast<size_t>(cfg_.ssm_head_dim));
                L.ffn_norm = f.floats(p + ffn_norm, static_cast<size_t>(d));
                load_ffn(L, p);
                continue;
            }
            kv_slot_[static_cast<size_t>(i)] = n_attention++;
            if (cfg_.attn_output_gate)
            {
                // Les lignes de la requête puis celles de sa porte, tête par tête.
                L.wq = f.matrix(p + "attn_q.weight", 2 * qd, d);
                L.wk = f.matrix(p + "attn_k.weight", kvd, d);
                L.wv = f.matrix(p + "attn_v.weight", kvd, d);
            }
            else if (cfg_.fused_qkv)
            {
                // Trois vues d'une même matrice : chaque produit passe toujours par product().
                const weight_matrix qkv = f.matrix(p + "attn_qkv.weight", qd + 2 * kvd, d);
                L.wq = row_slice(qkv, 0, qd);
                L.wk = row_slice(qkv, qd, kvd);
                L.wv = row_slice(qkv, qd + kvd, kvd);
            }
            else
            {
                L.wq = f.matrix(p + "attn_q.weight", qd, d);
                L.wk = f.matrix(p + "attn_k.weight", kvd, d);
                L.wv = f.matrix(p + "attn_v.weight", kvd, d);
            }
            L.wo = f.matrix(p + "attn_output.weight", d, qd);
            f.optional_floats(p + "attn_q.bias", static_cast<size_t>(qd), L.bq);
            f.optional_floats(p + "attn_k.bias", static_cast<size_t>(kvd), L.bk);
            f.optional_floats(p + "attn_v.bias", static_cast<size_t>(kvd), L.bv);
            f.optional_floats(p + "attn_q_norm.weight", static_cast<size_t>(hd), L.q_norm);
            f.optional_floats(p + "attn_k_norm.weight", static_cast<size_t>(hd), L.k_norm);
            // La passe avant ajoute les trois biais ensemble et normalise les requêtes et les clés ensemble.
            if (L.bq.empty() != L.bk.empty() || L.bq.empty() != L.bv.empty())
                throw std::runtime_error("layer " + std::to_string(i) + " holds some of the attn_q, attn_k and "
                    "attn_v biases without the others");
            if (L.q_norm.empty() != L.k_norm.empty())
                throw std::runtime_error("layer " + std::to_string(i) + " holds one of attn_q_norm and attn_k_norm "
                    "without the other");
            L.ffn_norm = f.floats(p + ffn_norm, static_cast<size_t>(d));
            load_ffn(L, p);
        }
        if (cfg_.recurrent())
        {
            const size_t page = 16384;
            const size_t used = static_cast<size_t>(n_linear) * static_cast<size_t>(cfg_.ssm_state_floats()) * sizeof(float);
            state_slot_bytes_ = (used + page - 1) / page * page;
            const size_t bytes = state_slot_bytes_ * max_state_slots;
            void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (p == MAP_FAILED) throw std::runtime_error("cpu_runtime: the states of the linear attention layers "
                "cannot be reserved");
            states_.data = static_cast<float*>(p);
            states_.bytes = bytes;
        }
        output_norm_ = f.floats("output_norm.weight", static_cast<size_t>(d));
        token_embd_ = f.matrix("token_embd.weight", cfg_.vocab, d);
        output_ = cfg_.tied_embeddings ? token_embd_ : f.matrix("output.weight", cfg_.vocab, d);

        const long rep = cfg_.n_heads / cfg_.n_kv_heads;
        const size_t rows = static_cast<size_t>(std::max<long>(1, tile_rows / rep) * rep);
        attention_work_.resize(static_cast<size_t>(pool_.size()));
        for (auto& w : attention_work_)
        {
            w.q.resize(rows * static_cast<size_t>(hd));
            w.o.resize(rows * static_cast<size_t>(hd));
            w.s.resize(rows * static_cast<size_t>(kv_.block_positions()));
            w.max.resize(rows);
            w.sum.resize(rows);
            w.scratch.resize(static_cast<size_t>(qk_attention_scratch(hd)));
        }

        // La composante de position de chaque paire rotative : le temps, sauf pour les paires des sections
        // de ligne et de colonne, entrelacées comme les dispose l'implémentation de référence de
        // Qwen3-VL.
        pair_component_.assign(static_cast<size_t>(cfg_.rope_dims / 2), 0);
        if (cfg_.multimodal_rope())
            for (long j = 0; j < cfg_.rope_dims / 2; ++j)
            {
                if (j % 3 == 1 && j < 3 * cfg_.rope_sections[1]) pair_component_[static_cast<size_t>(j)] = 1;
                else if (j % 3 == 2 && j < 3 * cfg_.rope_sections[2]) pair_component_[static_cast<size_t>(j)] = 2;
            }
    }

    cpu_runtime::state_region::~state_region()
    {
        if (data) munmap(data, bytes);
    }

    void cpu_runtime::set_context(long positions)
    {
        std::vector<float> previous = cfg_.rope_freq_factors;
        select_rope_factors(cfg_, positions);
        if (cfg_.rope_freq_factors == previous) return;
        if (rope_positions_ > 0)
        {
            cfg_.rope_freq_factors.swap(previous);
            throw std::logic_error("cpu_runtime: the rotary divisors cannot change once positions are computed");
        }
    }

    void cpu_runtime::set_offload(std::unique_ptr<matmul_offload> offload)
    {
        layer_pass_.reset();
        offload_ = std::move(offload);
        layer_pass_checked_ = false;
        layer_pass_memory_failures_ = 0;
        layer_pass_wait_ = 0;
        if (!offload_ || cfg_.is_moe() || cfg_.recurrent()) return;
        dense_model m;
        m.d_model = cfg_.d_model;
        m.q_dim = cfg_.q_dim();
        m.kv_dim = cfg_.kv_dim();
        m.d_ffn = cfg_.d_ffn;
        m.rms_eps = static_cast<float>(cfg_.rms_eps);
        m.residual_scale = static_cast<float>(cfg_.residual_scale);
        for (const layer& L : layers_)
        {
            dense_layer l;
            l.attn_norm = L.attn_norm.data();
            l.ffn_norm = L.ffn_norm.data();
            l.wq = L.wq;
            l.wk = L.wk;
            l.wv = L.wv;
            l.wo = L.wo;
            l.w_gate = L.w_gate;
            l.w_up = L.w_up;
            l.w_down = L.w_down;
            m.layers.push_back(l);
        }
        layer_pass_ = offload_->make_layer_pass(m);
    }

    std::string cpu_runtime::description() const
    {
        std::ostringstream o;
        o << "processor runtime (" << qk_isa() << ", " << pool_.size() << " threads";
        if (offload_) o << ", products offloaded to " << offload_->description();
        o << "), " << describe(cfg_);
        return o.str();
    }

    // Séquences

    int cpu_runtime::create_sequence()
    {
        int s;
        if (!free_sequences_.empty())
        {
            s = free_sequences_.back();
            free_sequences_.pop_back();
            sequences_[static_cast<size_t>(s)].reset(new kv_sequence());
        }
        else
        {
            sequences_.emplace_back(new kv_sequence());
            s = static_cast<int>(sequences_.size() - 1);
        }
        // L'état récurrent d'une nouvelle séquence part de zéro.
        if (cfg_.recurrent())
        {
            if (s >= max_state_slots)
            {
                release_sequence(s);
                throw std::runtime_error("cpu_runtime: more than " + std::to_string(max_state_slots)
                    + " sequences of a linear attention model at once");
            }
            std::memset(state_of(s), 0, state_slot_bytes_);
        }
        return s;
    }

    void cpu_runtime::release_sequence(int seq)
    {
        if (seq < 0 || static_cast<size_t>(seq) >= sequences_.size() || !sequences_[static_cast<size_t>(seq)]) return;
        sequences_[static_cast<size_t>(seq)]->clear(kv_);
        sequences_[static_cast<size_t>(seq)].reset();
        // La mémoire de son état revient au système.
        if (cfg_.recurrent() && seq < max_state_slots) madvise(state_of(seq), state_slot_bytes_, MADV_FREE);
        free_sequences_.push_back(seq);
    }

    bool cpu_runtime::resumable(int seq, long length) const
    {
        return !cfg_.recurrent() || length == 0 || length == sequence_length(seq);
    }

    long cpu_runtime::sequence_length(int seq) const
    {
        return sequences_.at(static_cast<size_t>(seq))->length;
    }

    void cpu_runtime::truncate_sequence(int seq, long length)
    {
        kv_sequence& s = *sequences_.at(static_cast<size_t>(seq));
        if (cfg_.recurrent() && length < s.length)
        {
            // Un état récurrent ne revient pas en arrière : il ne repart que de zéro.
            if (length != 0)
                throw std::logic_error("cpu_runtime: the state of a linear attention model cannot go back to "
                    "position " + std::to_string(length));
            std::memset(state_of(seq), 0, state_slot_bytes_);
        }
        s.truncate(kv_, length);
    }

    bool cpu_runtime::reserve_sequence(int seq, long length)
    {
        return sequences_.at(static_cast<size_t>(seq))->reserve(kv_, length);
    }

    bool cpu_runtime::copy_sequence(int src, int dst, long length)
    {
        const kv_sequence& s = *sequences_.at(static_cast<size_t>(src));
        kv_sequence& d = *sequences_.at(static_cast<size_t>(dst));
        if (cfg_.recurrent())
        {
            // L'état de src ne vaut qu'à sa longueur ; à 0, celui de dst repart de zéro.
            if (length != 0 && length != s.length) return false;
            if (length == 0) std::memset(state_of(dst), 0, state_slot_bytes_);
            else std::memcpy(state_of(dst), state_of(src), state_slot_bytes_);
        }
        // Les blocs pleins sont partagés ; chaque séquence copie un bloc partagé avant d'y écrire.
        return d.share_prefix(kv_, s, length);
    }

    // Briques de base

    void cpu_runtime::rms_norm_rows(const float* x, const float* gamma, float* y, long rows, long width, float eps)
    {
        auto one = [&](long r) {
            const float* xr = x + r * width;
            float* yr = y + r * width;
            const float ss = qk_dot_f32(xr, xr, width);
            const float scale = 1.0f / std::sqrt(ss / static_cast<float>(width) + eps);
            for (long i = 0; i < width; ++i) yr[i] = xr[i] * scale * gamma[i];
        };
        if (rows * width >= 65536)
            pool_.parallel_for(rows, [&](long r, int) { one(r); });
        else
            for (long r = 0; r < rows; ++r) one(r);
    }

    void cpu_runtime::ensure_rope(long positions)
    {
        if (positions <= rope_positions_) return;
        const long half = cfg_.rope_dims / 2;
        const long n_dims = cfg_.rope_dims;
        // Croître par paliers, pour éviter de recalculer à chaque nouveau token.
        const long target = std::max<long>(positions, rope_positions_ + 1024);
        rope_cos_.resize(static_cast<size_t>(target * half));
        rope_sin_.resize(static_cast<size_t>(target * half));

        const double theta_scale = std::pow(cfg_.rope_base, -2.0 / static_cast<double>(n_dims));
        float low = 0, high = 0;
        if (cfg_.yarn)
        {
            const float fast = yarn_corr_dim(n_dims, cfg_.yarn_orig_ctx, cfg_.yarn_beta_fast, cfg_.rope_base);
            const float slow = yarn_corr_dim(n_dims, cfg_.yarn_orig_ctx, cfg_.yarn_beta_slow, cfg_.rope_base);
            low = std::max(0.0f, std::floor(fast));
            high = std::min(static_cast<float>(n_dims - 1), std::ceil(slow));
        }
        const float freq_scale = static_cast<float>(cfg_.rope_freq_scale);
        const float attn_factor = static_cast<float>(cfg_.rope_attn_factor);
        for (long pos = rope_positions_; pos < target; ++pos)
        {
            double theta_pow = 1.0;
            for (long i = 0; i < half; ++i)
            {
                float theta_extrap = static_cast<float>(static_cast<double>(pos) * theta_pow);
                if (!cfg_.rope_freq_factors.empty()) theta_extrap /= cfg_.rope_freq_factors[static_cast<size_t>(i)];
                const float theta_interp = freq_scale * theta_extrap;
                float theta = theta_interp;
                float mscale = attn_factor;
                if (cfg_.yarn)
                {
                    const float mix = yarn_ramp(low, high, 2 * i) * static_cast<float>(cfg_.yarn_ext_factor);
                    theta = theta_interp * (1.0f - mix) + theta_extrap * mix;
                    mscale = static_cast<float>(cfg_.yarn_attn_factor * cfg_.rope_attn_factor);
                }
                rope_cos_[static_cast<size_t>(pos * half + i)] = std::cos(theta) * mscale;
                rope_sin_[static_cast<size_t>(pos * half + i)] = std::sin(theta) * mscale;
                theta_pow *= theta_scale;
            }
        }
        rope_positions_ = target;
    }

    void cpu_runtime::apply_rope(float* rows, long n_rows, long n_heads, const std::vector<long>& positions)
    {
        const long hd = cfg_.head_dim;
        const long half = cfg_.rope_dims / 2;
        const bool neox = cfg_.rope == rope_style::neox;
        auto one = [&](long t) {
            const float* c = rope_cos_.data() + positions[static_cast<size_t>(t)] * half;
            const float* s = rope_sin_.data() + positions[static_cast<size_t>(t)] * half;
            for (long h = 0; h < n_heads; ++h)
            {
                float* x = rows + (t * n_heads + h) * hd;
                if (neox)
                {
                    for (long i = 0; i < half; ++i)
                    {
                        const float x0 = x[i], x1 = x[i + half];
                        x[i] = x0 * c[i] - x1 * s[i];
                        x[i + half] = x0 * s[i] + x1 * c[i];
                    }
                }
                else
                {
                    for (long i = 0; i < half; ++i)
                    {
                        const float x0 = x[2 * i], x1 = x[2 * i + 1];
                        x[2 * i] = x0 * c[i] - x1 * s[i];
                        x[2 * i + 1] = x0 * s[i] + x1 * c[i];
                    }
                }
            }
        };
        if (n_rows >= 16)
            pool_.parallel_for(n_rows, [&](long t, int) { one(t); });
        else
            for (long t = 0; t < n_rows; ++t) one(t);
    }

    void cpu_runtime::prepare_given_positions(const forward_batch& batch, long T)
    {
        const long half = cfg_.rope_dims / 2;
        const long C = forward_batch::position_components;
        given_cos_.resize(static_cast<size_t>(T * half));
        given_sin_.resize(static_cast<size_t>(T * half));
        for (long t = 0; t < T; ++t)
        {
            const long* p = batch.positions.data() + t * C;
            float* c = given_cos_.data() + t * half;
            float* s = given_sin_.data() + t * half;
            for (long i = 0; i < half; ++i)
            {
                const size_t at = static_cast<size_t>(p[pair_component_[static_cast<size_t>(i)]] * half + i);
                c[i] = rope_cos_[at];
                s[i] = rope_sin_[at];
            }
        }
    }

    void cpu_runtime::apply_given_rope(float* rows, long n_rows, long n_heads)
    {
        const long hd = cfg_.head_dim;
        const long half = cfg_.rope_dims / 2;
        const bool neox = cfg_.rope == rope_style::neox;
        auto one = [&](long t) {
            const float* c = given_cos_.data() + t * half;
            const float* s = given_sin_.data() + t * half;
            for (long h = 0; h < n_heads; ++h)
            {
                float* x = rows + (t * n_heads + h) * hd;
                if (neox)
                {
                    for (long i = 0; i < half; ++i)
                    {
                        const float x0 = x[i], x1 = x[i + half];
                        x[i] = x0 * c[i] - x1 * s[i];
                        x[i + half] = x0 * s[i] + x1 * c[i];
                    }
                }
                else
                {
                    for (long i = 0; i < half; ++i)
                    {
                        const float x0 = x[2 * i], x1 = x[2 * i + 1];
                        x[2 * i] = x0 * c[i] - x1 * s[i];
                        x[2 * i + 1] = x0 * s[i] + x1 * c[i];
                    }
                }
            }
        };
        if (n_rows >= 16)
            pool_.parallel_for(n_rows, [&](long t, int) { one(t); });
        else
            for (long t = 0; t < n_rows; ++t) one(t);
    }

    void cpu_runtime::check_given_embeddings(const forward_batch& batch, long T) const
    {
        const size_t W = static_cast<size_t>(cfg_.input_width());
        if (batch.embeddings.size() != batch.embedding_rows.size() * W)
            throw std::runtime_error("cpu_runtime: the embeddings of the batch do not match its rows");
        for (long r : batch.embedding_rows)
            if (r < 0 || r >= T) throw std::runtime_error("cpu_runtime: an embedding row lies outside the batch");
    }

    void cpu_runtime::add_given_embeddings(const forward_batch& batch, long slice)
    {
        // La tranche 0 remplace l'embedding du token ; la tranche k est ajoutée après la couche k - 1.
        const long d = cfg_.d_model;
        const long W = cfg_.input_width();
        for (size_t i = 0; i < batch.embedding_rows.size(); ++i)
        {
            float* dst = x_.data() + batch.embedding_rows[i] * d;
            const float* src = batch.embeddings.data() + static_cast<long>(i) * W + slice * d;
            if (slice == 0) std::memcpy(dst, src, sizeof(float) * static_cast<size_t>(d));
            else for (long j = 0; j < d; ++j) dst[j] += src[j];
        }
    }

    void cpu_runtime::product(const weight_matrix& W, const float* X, long n, long x_stride, float* Y, long y_stride,
        qk_path path)
    {
        if (float_products_) path = qk_path::floats;
        if (!offload_ || host_only_)
        {
            qk_matmul(W, X, n, x_stride, Y, y_stride, pool_, scratch_[0], false, path);
            return;
        }
        offload_->matmul(W, X, n, x_stride, Y, y_stride,
            [&]() { qk_matmul(W, X, n, x_stride, Y, y_stride, pool_, scratch_[0], false, path); });
    }

    void cpu_runtime::embed(const std::vector<int>& tokens)
    {
        const long d = cfg_.d_model;
        const long T = static_cast<long>(tokens.size());
        x_.resize(static_cast<size_t>(T * d));
        const float es = static_cast<float>(cfg_.embedding_scale);
        auto one = [&](long t) {
            const int id = tokens[static_cast<size_t>(t)];
            if (id < 0 || id >= cfg_.vocab) throw std::runtime_error("token identifier out of range");
            float* dst = x_.data() + t * d;
            qk_dequantize_row(token_embd_.type, token_embd_.data + static_cast<size_t>(id) * token_embd_.row_bytes,
                dst, d);
            if (es != 1.0f) qk_scale(dst, es, d);
        };
        for (long t = 0; t < T; ++t) one(t);
    }

    // Attention

    void cpu_runtime::store_keys_values(long li, const forward_batch& batch)
    {
        const long T = static_cast<long>(batch.tokens.size());
        const long KVH = cfg_.n_kv_heads;
        const long hd = cfg_.head_dim;
        const long kvd = cfg_.kv_dim();
        const long B = kv_.block_positions();
        const bool q8 = kv_.format() == kv_format::q8;
        const long scales_per_row = hd / kv_q8_group;
        token_sequences_.resize(static_cast<size_t>(T));
        for (const auto& s : batch.segments)
            for (long j = 0; j < s.count; ++j) token_sequences_[static_cast<size_t>(s.start + j)] = s.sequence;
        auto one = [&](long t, int thread) {
            const kv_sequence& seq = *sequences_[static_cast<size_t>(token_sequences_[static_cast<size_t>(t)])];
            const long pos = positions_[static_cast<size_t>(t)];
            const int block = seq.blocks[static_cast<size_t>(pos / B)];
            const long row = pos % B;
            float* rotated = attention_work_[static_cast<size_t>(thread)].scratch.data();
            for (long h = 0; h < KVH; ++h)
            {
                const float* k = k_.data() + t * kvd + h * hd;
                const float* v = v_.data() + t * kvd + h * hd;
                if (q8)
                {
                    // Les clés sont stockées tournées (qk_rotate_row), les requêtes étant tournées de même.
                    std::copy(k, k + hd, rotated);
                    qk_rotate_row(rotated, hd);
                    qk_float_to_q8_row(rotated, reinterpret_cast<int8_t*>(kv_.keys(block, li, h)) + row * hd,
                        kv_.key_scales(block, li, h) + row * scales_per_row, hd);
                    qk_float_to_q8_row(v, reinterpret_cast<int8_t*>(kv_.values(block, li, h)) + row * hd,
                        kv_.value_scales(block, li, h) + row * scales_per_row, hd);
                }
                else
                {
                    qk_float_to_half_row(k, reinterpret_cast<uint16_t*>(kv_.keys(block, li, h)) + row * hd, hd);
                    qk_float_to_half_row(v, reinterpret_cast<uint16_t*>(kv_.values(block, li, h)) + row * hd, hd);
                }
            }
        };
        // Chaque token écrit ses propres lignes.
        if (T >= 32)
            pool_.parallel_for(T, [&](long t, int thread) { one(t, thread); });
        else
            for (long t = 0; t < T; ++t) one(t, 0);
    }

    void cpu_runtime::attention(long layer_index, const forward_batch& batch, const std::vector<long>& positions)
    {
        const long T = static_cast<long>(batch.tokens.size());
        const long KVH = cfg_.n_kv_heads;
        const long hd = cfg_.head_dim;
        const long qd = cfg_.q_dim();
        const long rep = cfg_.n_heads / KVH;
        const long B = kv_.block_positions();
        const bool q8 = kv_.format() == kv_format::q8;
        const long scales_per_row = hd / kv_q8_group;
        const float scale = cfg_.attention_scale > 0.0
            ? static_cast<float>(cfg_.attention_scale)
            : 1.0f / std::sqrt(static_cast<float>(hd));

        // Les tuiles : des tokens consécutifs d'un segment, dont les positions sont consécutives aussi.
        const long per_tile = std::max<long>(1, tile_rows / rep);
        tiles_.clear();
        long span = 0;
        for (const auto& s : batch.segments)
            for (long j = 0; j < s.count; j += per_tile)
            {
                const long count = std::min(per_tile, s.count - j);
                tiles_.push_back({ s.sequence, s.start + j, count });
                span = std::max(span, positions[static_cast<size_t>(s.start + j + count - 1)] + 1);
            }
        /*
            Les parties d'une tuile : quand les tuiles et leurs têtes de clés et valeurs sont moins
            nombreuses que les threads, comme dans la génération d'une seule réponse, les positions
            de chaque tuile sont découpées en plages d'au moins part_positions, chaque plage lue par
            une tâche pour toutes les têtes du groupe, et les sommes partielles des plages sont
            fusionnées ensuite (le découpage de FlashDecoding).  Les clés et valeurs sont alors lues
            une seule fois pour toutes les têtes qui les partagent.
        */
        const long units = static_cast<long>(tiles_.size()) * KVH;
        long parts = 1;
        if (units < pool_.active())
            parts = std::max<long>(1, std::min<long>((2 * pool_.active() + units - 1) / units, span / part_positions));
        const long rows_cap = per_tile * rep;
        if (parts > 1) partial_.resize(static_cast<size_t>(units * parts * rows_cap * (hd + 2)));

        ctx_.resize(static_cast<size_t>(T * qd));
        pool_.parallel_for(units * parts, [&](long item, int thread) {
            const attention_tile& tile = tiles_[static_cast<size_t>(item / (KVH * parts))];
            const long kvh = item / parts % KVH;
            const long part = item % parts;
            const long h0 = kvh * rep;
            const kv_sequence& seq = *sequences_[static_cast<size_t>(tile.sequence)];
            attention_work& w = attention_work_[static_cast<size_t>(thread)];
            const long rows = tile.count * rep;
            const long last = positions[static_cast<size_t>(tile.start + tile.count - 1)];
            const long length = (last + parts) / parts;
            const long begin = part * length;
            const long end = std::min(last + 1, begin + length);

            // La ligne a * rep + i contient le token tile.start + a et la tête h0 + i, mise à l'échelle, et
            // tournée comme les clés 8 bits.
            for (long a = 0; a < tile.count; ++a)
            {
                const float* src = q_.data() + (tile.start + a) * qd + h0 * hd;
                float* dst = w.q.data() + a * rep * hd;
                for (long e = 0; e < rep * hd; ++e) dst[e] = src[e] * scale;
                if (q8)
                    for (long i = 0; i < rep; ++i) qk_rotate_row(dst + i * hd, hd);
            }
            std::fill(w.o.begin(), w.o.begin() + rows * hd, 0.0f);
            std::fill(w.max.begin(), w.max.begin() + rows, -INFINITY);
            std::fill(w.sum.begin(), w.sum.begin() + rows, 0.0f);

            for (long p = begin; p < end;)
            {
                const int block = seq.blocks[static_cast<size_t>(p / B)];
                const long first = p % B;
                const long m = std::min(B - first, end - p);
                if (q8)
                    qk_attention_scores_q8(w.q.data(), rows,
                        reinterpret_cast<const int8_t*>(kv_.keys(block, layer_index, kvh)) + first * hd,
                        kv_.key_scales(block, layer_index, kvh) + first * scales_per_row, m, hd, w.s.data(), B,
                        w.scratch.data());
                else
                    qk_attention_scores(w.q.data(), rows,
                        reinterpret_cast<const uint16_t*>(kv_.keys(block, layer_index, kvh)) + first * hd, m, hd,
                        w.s.data(), B, w.scratch.data());
                for (long a = 0; a < tile.count; ++a)
                {
                    // Chaque token voit les positions jusqu'à la sienne.
                    const long own = positions[static_cast<size_t>(tile.start + a)];
                    const long seen = std::max<long>(0, std::min(m, own + 1 - p));
                    for (long i = 0; i < rep; ++i)
                    {
                        const long r = a * rep + i;
                        float* sr = w.s.data() + r * B;
                        std::fill(sr + seen, sr + m, 0.0f);
                        if (seen == 0) continue;
                        const float old = w.max[static_cast<size_t>(r)];
                        const float mx = std::max(old, qk_max(sr, seen));
                        const float added = qk_exp_shift_sum(sr, mx, seen);
                        if (mx != old && w.sum[static_cast<size_t>(r)] > 0.0f)
                        {
                            // Un maximum plus grand : ce que la ligne a accumulé jusqu'ici diminue.
                            const float shrink = std::exp(old - mx);
                            w.sum[static_cast<size_t>(r)] *= shrink;
                            qk_scale(w.o.data() + r * hd, shrink, hd);
                        }
                        w.max[static_cast<size_t>(r)] = mx;
                        w.sum[static_cast<size_t>(r)] += added;
                    }
                }
                if (q8)
                    qk_attention_values_q8(w.s.data(), B, rows,
                        reinterpret_cast<const int8_t*>(kv_.values(block, layer_index, kvh)) + first * hd,
                        kv_.value_scales(block, layer_index, kvh) + first * scales_per_row, m, hd, w.o.data(),
                        w.scratch.data());
                else
                    qk_attention_values(w.s.data(), B, rows,
                        reinterpret_cast<const uint16_t*>(kv_.values(block, layer_index, kvh)) + first * hd, m, hd,
                        w.o.data(), w.scratch.data());
                p += m;
            }

            if (parts > 1)
            {
                // Les sommes de la plage, non normalisées, avec le maximum et la somme de chaque ligne.
                float* out = partial_.data() + item * rows_cap * (hd + 2);
                std::copy(w.o.begin(), w.o.begin() + rows * hd, out);
                std::copy(w.max.begin(), w.max.begin() + rows, out + rows_cap * hd);
                std::copy(w.sum.begin(), w.sum.begin() + rows, out + rows_cap * (hd + 1));
                return;
            }
            for (long a = 0; a < tile.count; ++a)
                for (long i = 0; i < rep; ++i)
                {
                    const long r = a * rep + i;
                    const float inv = 1.0f / w.sum[static_cast<size_t>(r)];
                    const float* src = w.o.data() + r * hd;
                    float* dst = ctx_.data() + (tile.start + a) * qd + (h0 + i) * hd;
                    for (long e = 0; e < hd; ++e) dst[e] = src[e] * inv;
                }
        });
        if (parts == 1) return;

        // Chaque ligne prend les plages de sa tuile avec le plus grand de leurs maximums comme référence.
        pool_.parallel_for(units, [&](long unit, int) {
            const attention_tile& tile = tiles_[static_cast<size_t>(unit / KVH)];
            const long h0 = unit % KVH * rep;
            const float* first = partial_.data() + unit * parts * rows_cap * (hd + 2);
            const long stride = rows_cap * (hd + 2);
            for (long a = 0; a < tile.count; ++a)
                for (long i = 0; i < rep; ++i)
                {
                    const long r = a * rep + i;
                    float mx = -INFINITY;
                    for (long q = 0; q < parts; ++q) mx = std::max(mx, first[q * stride + rows_cap * hd + r]);
                    float* dst = ctx_.data() + (tile.start + a) * qd + (h0 + i) * hd;
                    std::fill(dst, dst + hd, 0.0f);
                    float sum = 0.0f;
                    for (long q = 0; q < parts; ++q)
                    {
                        const float* range = first + q * stride;
                        const float s = range[rows_cap * (hd + 1) + r];
                        if (s <= 0.0f) continue;
                        const float f = std::exp(range[rows_cap * hd + r] - mx);
                        sum += s * f;
                        const float* o = range + r * hd;
                        for (long e = 0; e < hd; ++e) dst[e] += f * o[e];
                    }
                    const float inv = 1.0f / sum;
                    for (long e = 0; e < hd; ++e) dst[e] *= inv;
                }
        });
    }

    // Mélange d'experts

    void cpu_runtime::moe_ffn(const layer& L, long T)
    {
        const long d = cfg_.d_model;
        const long n_exp = cfg_.n_experts;
        const long top_k = cfg_.n_experts_used;
        const long df = cfg_.d_ffn_expert;

        router_logits_.resize(static_cast<size_t>(T * n_exp));
        product(L.router, xn_.data(), T, d, router_logits_.data(), n_exp);

        // Les listes des experts gardent leur mémoire d'une couche et d'une passe à l'autre.
        std::vector<std::vector<std::pair<long, float>>>& routed = routed_;
        routed.resize(static_cast<size_t>(n_exp));
        for (auto& list : routed) list.clear();
        std::vector<float>& probs = router_probs_;
        probs.resize(static_cast<size_t>(n_exp));
        std::vector<long>& order = router_order_;
        order.resize(static_cast<size_t>(n_exp));
        for (long t = 0; t < T; ++t)
        {
            const float* row = router_logits_.data() + t * n_exp;
            const float mx = *std::max_element(row, row + n_exp);
            float sum = 0;
            for (long e = 0; e < n_exp; ++e)
            {
                probs[static_cast<size_t>(e)] = std::exp(row[e] - mx);
                sum += probs[static_cast<size_t>(e)];
            }
            for (auto& p : probs) p /= sum;
            for (long e = 0; e < n_exp; ++e) order[static_cast<size_t>(e)] = e;
            std::partial_sort(order.begin(), order.begin() + top_k, order.end(),
                [&](long a, long b) { return probs[static_cast<size_t>(a)] > probs[static_cast<size_t>(b)]; });
            float selected = 0;
            for (long k = 0; k < top_k; ++k) selected += probs[static_cast<size_t>(order[static_cast<size_t>(k)])];
            for (long k = 0; k < top_k; ++k)
            {
                const long e = order[static_cast<size_t>(k)];
                float w = probs[static_cast<size_t>(e)];
                if (cfg_.expert_weights_norm && selected > 0) w /= selected;
                routed[static_cast<size_t>(e)].push_back(std::make_pair(t, w));
            }
        }

        tmp_.assign(static_cast<size_t>(T * d), 0.0f);
        aligned_floats& xe = expert_x_;
        aligned_floats& ge = expert_gate_;
        aligned_floats& ue = expert_up_;
        aligned_floats& ye = expert_y_;
        const qk_path path = T >= qk_float_product_tokens ? qk_path::floats : qk_path::integer;
        for (long e = 0; e < n_exp; ++e)
        {
            const auto& list = routed[static_cast<size_t>(e)];
            const long m = static_cast<long>(list.size());
            if (m == 0) continue;
            xe.resize(static_cast<size_t>(m * d));
            for (long i = 0; i < m; ++i)
                std::memcpy(xe.data() + i * d, xn_.data() + list[static_cast<size_t>(i)].first * d, sizeof(float) * d);
            ge.resize(static_cast<size_t>(m * df));
            ue.resize(static_cast<size_t>(m * df));
            product(L.e_gate[static_cast<size_t>(e)], xe.data(), m, d, ge.data(), df, path);
            product(L.e_up[static_cast<size_t>(e)], xe.data(), m, d, ue.data(), df, path);
            qk_silu_mul(ge.data(), ue.data(), static_cast<long>(ge.size()));
            ye.resize(static_cast<size_t>(m * d));
            product(L.e_down[static_cast<size_t>(e)], ge.data(), m, df, ye.data(), d, path);
            for (long i = 0; i < m; ++i)
            {
                const float w = list[static_cast<size_t>(i)].second;
                float* dst = tmp_.data() + list[static_cast<size_t>(i)].first * d;
                const float* src = ye.data() + i * d;
                for (long j = 0; j < d; ++j) dst[j] += w * src[j];
            }
        }

        // L'expert partagé de qwen35moe : chaque token, sa sortie dosée par sigmoid(xn . ffn_gate_inp_shexp).
        if (!L.sh_gate_inp.empty())
        {
            const long ds = cfg_.d_ffn_shared;
            ge.resize(static_cast<size_t>(T * ds));
            ue.resize(static_cast<size_t>(T * ds));
            ye.resize(static_cast<size_t>(T * d));
            product(L.sh_gate, xn_.data(), T, d, ge.data(), ds);
            product(L.sh_up, xn_.data(), T, d, ue.data(), ds);
            qk_silu_mul(ge.data(), ue.data(), T * ds);
            product(L.sh_down, ge.data(), T, ds, ye.data(), d);
            for (long t = 0; t < T; ++t)
            {
                const float g = 1.0f / (1.0f + std::exp(-qk_dot_f32(xn_.data() + t * d, L.sh_gate_inp.data(), d)));
                float* dst = tmp_.data() + t * d;
                const float* src = ye.data() + t * d;
                for (long j = 0; j < d; ++j) dst[j] += g * src[j];
            }
        }
    }

    // Passe avant

    bool cpu_runtime::takes_layer_pass(const forward_batch& batch) const
    {
        // Les tranches DeepStack des images sont ajoutées aux états cachés entre les premières couches.
        return !(cfg_.n_deepstack > 0 && !batch.embedding_rows.empty());
    }

    void cpu_runtime::drop_layer_pass(const std::string& why)
    {
        layer_pass_->report(false, why);
        layer_pass_.reset();
    }

    /*
        Fait passer n tokens, dans une séquence à part, par les couches du périphérique, puis par
        le processeur seul, et compare leurs logits : 1 quand ils concordent, 0 quand ils diffèrent,
        -1 quand les clés et valeurs ou le périphérique manquent de place pour la sonde.  detail
        décrit la comparaison.
    */
    int cpu_runtime::probe_layer_pass(long n, std::string& detail)
    {
        const long V = cfg_.vocab;
        const int seq = create_sequence();
        struct release
        {
            cpu_runtime& rt;
            int seq;
            ~release() { rt.release_sequence(seq); }
        } guard{ *this, seq };
        if (!reserve_sequence(seq, n)) return -1;
        forward_batch b;
        for (long i = 0; i < n; ++i) b.tokens.push_back(static_cast<int>((1009 + 7919 * i) % V));
        b.segments.push_back({ seq, 0, n });
        for (long i = 0; i < n; ++i) b.logit_rows.push_back(i);
        std::vector<float> reference, device;
        if (!run(b, device, true)) return -1;
        // La troncature rend les blocs.
        truncate_sequence(seq, 0);
        if (!reserve_sequence(seq, n)) return -1;
        {
            struct processor_only
            {
                bool& flag;
                ~processor_only() { flag = false; }
            } scope{ host_only_ };
            host_only_ = true;
            run(b, reference, false);
        }
        double worst = 0;
        long same = 0;
        bool apart = false;
        for (long r = 0; r < n; ++r)
        {
            const float* a = reference.data() + r * V;
            const float* d = device.data() + r * V;
            double diff = 0, norm = 0;
            long ia = 0, id = 0;
            for (long v = 0; v < V; ++v)
            {
                diff += (static_cast<double>(a[v]) - d[v]) * (static_cast<double>(a[v]) - d[v]);
                norm += static_cast<double>(a[v]) * a[v];
                if (a[v] > a[ia]) ia = v;
                if (d[v] > d[id]) id = v;
            }
            const double relative = std::sqrt(diff / std::max(norm, 1e-30));
            // Écrit de sorte qu'une valeur qui n'est pas un nombre mette la ligne à part.
            if (!(relative <= 0.01)) apart = true;
            if (relative > worst) worst = relative;
            if (ia == id) ++same;
        }
        std::ostringstream o;
        o << "logits of " << n << " probe tokens within " << worst * 100 << "% of those of the processor"
            << (apart && worst <= 0.01 ? ", some not numbers" : "") << ", " << same << " of " << n
            << " most likely tokens the same";
        detail = o.str();
        // Deux candidats dont les logits sont presque égaux peuvent échanger leurs places : une ligne sur seize
        // peut différer.
        return !apart && same >= n - std::max<long>(1, n / 16) ? 1 : 0;
    }

    /*
        Garde la layer pass quand des passes du périphérique concordent avec le processeur, un défaut
        du périphérique ou de ses noyaux atteignant sinon chaque réponse : une sonde de probe_tokens
        tokens, dont les produits lisent les blocs des poids en place, et une de tiled_tokens()
        tokens, dont les produits passent par les tuiles des poids développés.  L'état reste non
        vérifié quand les clés et valeurs ou le périphérique manquent de place pour une sonde, pour
        qu'une passe ultérieure vérifie à nouveau.
    */
    void cpu_runtime::check_layer_pass()
    {
        // Une copie de la constante, que std::max() prend par référence.
        long first = probe_tokens;
        first = std::max(first, layer_pass_->min_tokens());
        if (cfg_.ctx_train > 0) first = std::min(first, cfg_.ctx_train);
        std::vector<long> sizes(1, first);
        const long tiled = layer_pass_->tiled_tokens();
        if (tiled > first && (cfg_.ctx_train <= 0 || tiled <= cfg_.ctx_train)) sizes.push_back(tiled);
        std::string details;
        for (long n : sizes)
        {
            std::string detail;
            const int outcome = probe_layer_pass(n, detail);
            if (outcome < 0) return;
            details += (details.empty() ? "" : "; ") + detail;
            if (outcome == 0)
            {
                drop_layer_pass(details);
                return;
            }
        }
        layer_pass_checked_ = true;
        layer_pass_->report(true, details);
    }

    void cpu_runtime::forward(const forward_batch& batch, std::vector<float>& logits)
    {
        // Une passe de moins de tokens que n'en prennent les produits en floats, comme une étape de la
        // génération, s'exécute sur les seuls cœurs de performance (thread_pool_apple.inc).
        const thread_pool::fast_team_scope fast_team(static_cast<long>(batch.tokens.size()) < qk_float_product_tokens);
        if (gpu_ && takes_gpu(batch) && gpu_ready())
        {
            try
            {
                run_gpu(batch, logits);
                return;
            }
            catch (const std::exception& e)
            {
                // La passe est recalculée ci-dessous ; ses clés et valeurs sont réécrites.
                drop_gpu(std::string("the GPU failed: ") + e.what());
            }
        }
        if (layer_pass_ && !batch.tokens.empty() && takes_layer_pass(batch))
        {
            try
            {
                // Un contrôle que les clés et valeurs ou le périphérique ont reporté attend quelques passes.
                if (!layer_pass_checked_ && layer_pass_wait_ > 0)
                    --layer_pass_wait_;
                else if (!layer_pass_checked_)
                {
                    check_layer_pass();
                    if (!layer_pass_checked_) layer_pass_wait_ = layer_pass_check_interval;
                }
                if (layer_pass_ && layer_pass_checked_ && run(batch, logits, true))
                {
                    layer_pass_memory_failures_ = 0;
                    return;
                }
            }
            catch (const device_failure& e)
            {
                // La passe est recalculée ci-dessous ; ses clés et valeurs sont réécrites.
                if (!e.memory)
                    drop_layer_pass(std::string("the device failed: ") + e.what());
                else if (++layer_pass_memory_failures_ >= max_layer_pass_memory_failures)
                    drop_layer_pass(std::string("the device lacked memory several times in a row: ") + e.what());
            }
        }
        run(batch, logits, false);
    }

    /*
        Une couche Gated DeltaNet (qwen35), comme llama.cpp la calcule (src/models/qwen35.cpp,
        delta-net-base.cpp) : projections, convolution causale par canal et SiLU, normes L2 des requêtes et
        des clés, puis, par tête de valeurs, la règle delta sur l'état S (ssm_head_dim x ssm_head_dim) :
            S *= exp(g),  delta = beta * (v - S k),  S += delta k^T,  o = S q / sqrt(ssm_head_dim),
        avec g = softplus(alpha + dt_bias) * ssm_a et beta = sigmoid(beta) ; enfin la norme RMS de chaque
        tête multipliée par SiLU(z), et la projection de sortie.  S est rangé par lignes de valeurs : la
        ligne r ne dépend que de v[r], ce qui fait de chaque ligne un calcul indépendant.  Les tokens d'une
        séquence passent dans l'ordre ; les séquences et les têtes se répartissent entre les threads.
    */
    void cpu_runtime::linear_attention(const layer& L, long linear, const forward_batch& batch)
    {
        const long T = static_cast<long>(batch.tokens.size());
        const long d = cfg_.d_model;
        const long Hk = cfg_.ssm_k_heads, Hv = cfg_.ssm_v_heads, S = cfg_.ssm_head_dim;
        const long C = cfg_.ssm_conv_dim(), VD = cfg_.ssm_value_dim(), K = cfg_.ssm_conv_kernel;
        const long window = K - 1;
        const size_t per_sequence = static_cast<size_t>(cfg_.ssm_state_floats());
        const size_t offset = static_cast<size_t>(linear) * per_sequence;
        const float eps = static_cast<float>(cfg_.rms_eps);

        mix_.resize(static_cast<size_t>(T * C));
        z_.resize(static_cast<size_t>(T * VD));
        alpha_.resize(static_cast<size_t>(T * Hv));
        beta_.resize(static_cast<size_t>(T * Hv));
        lin_out_.resize(static_cast<size_t>(T * VD));
        product(L.w_mix, xn_.data(), T, d, mix_.data(), C);
        product(L.w_z, xn_.data(), T, d, z_.data(), VD);
        product(L.w_alpha, xn_.data(), T, d, alpha_.data(), Hv);
        product(L.w_beta, xn_.data(), T, d, beta_.data(), Hv);

        // Convolution causale de chaque canal sur la fenêtre de sa séquence, puis SiLU : les canaux sont
        // indépendants, les tokens d'une séquence se suivent.
        auto convolve = [&](long first, long last) {
            for (long c = first; c < last; ++c)
            {
                const float* w = L.conv1d.data() + c * K;
                for (const auto& s : batch.segments)
                {
                    float* win = state_of(s.sequence) + offset + c * window;
                    for (long j = 0; j < s.count; ++j)
                    {
                        float* x = mix_.data() + (s.start + j) * C + c;
                        const float in = *x;
                        float sum = 0.0f;
                        for (long i = 0; i < window; ++i) sum += win[i] * w[i];
                        sum += in * w[window];
                        for (long i = 0; i + 1 < window; ++i) win[i] = win[i + 1];
                        if (window > 0) win[window - 1] = in;
                        *x = sum / (1.0f + std::exp(-sum));
                    }
                }
            }
        };
        const long blocks = (C + 63) / 64;
        pool_.parallel_for(blocks, [&](long b, int) { convolve(b * 64, std::min(C, b * 64 + 64)); });

        // Normes L2 des requêtes et des clés (x / sqrt(somme des carrés + eps)), oubli et force d'écriture.
        for (long t = 0; t < T; ++t)
        {
            float* row = mix_.data() + t * C;
            for (long h = 0; h < 2 * Hk; ++h)
            {
                float* x = row + h * S;
                const float scale = 1.0f / std::sqrt(qk_dot_f32(x, x, S) + eps);
                for (long i = 0; i < S; ++i) x[i] *= scale;
            }
            for (long h = 0; h < Hv; ++h)
            {
                float& a = alpha_[static_cast<size_t>(t * Hv + h)];
                const float biased = a + L.dt_bias[static_cast<size_t>(h)];
                const float softplus = biased > 20.0f ? biased : std::log1p(std::exp(biased));
                a = softplus * L.ssm_a[static_cast<size_t>(h)];
                float& b = beta_[static_cast<size_t>(t * Hv + h)];
                b = 1.0f / (1.0f + std::exp(-b));
            }
        }

        // La règle delta, par séquence et par tête de valeurs.
        const float qscale = 1.0f / std::sqrt(static_cast<float>(S));
        const long n_segments = static_cast<long>(batch.segments.size());
        pool_.parallel_for(n_segments * Hv, [&](long job, int) {
            const auto& s = batch.segments[static_cast<size_t>(job / Hv)];
            const long h = job % Hv, kh = h % Hk;
            float* state = state_of(s.sequence) + offset + static_cast<size_t>(cfg_.ssm_conv_state())
                + static_cast<size_t>(h * S * S);
            std::vector<float> kv(static_cast<size_t>(S)), delta(static_cast<size_t>(S));
            for (long j = 0; j < s.count; ++j)
            {
                const long t = s.start + j;
                const float* q = mix_.data() + t * C + kh * S;
                const float* k = mix_.data() + t * C + (Hk + kh) * S;
                const float* v = mix_.data() + t * C + 2 * Hk * S + h * S;
                const float decay = std::exp(alpha_[static_cast<size_t>(t * Hv + h)]);
                const float b = beta_[static_cast<size_t>(t * Hv + h)];
                float* o = lin_out_.data() + t * VD + h * S;
                // S rangé par clés : la ligne i contient S[i][r] pour chaque valeur r.  D'abord l'oubli et S k.
                std::fill(kv.begin(), kv.end(), 0.0f);
                for (long i = 0; i < S; ++i)
                {
                    float* line = state + i * S;
                    const float ki = k[i];
                    for (long r = 0; r < S; ++r)
                    {
                        line[r] *= decay;
                        kv[static_cast<size_t>(r)] += ki * line[r];
                    }
                }
                for (long r = 0; r < S; ++r) delta[static_cast<size_t>(r)] = (v[r] - kv[static_cast<size_t>(r)]) * b;
                // Puis la correction et la lecture par q.
                std::fill(o, o + S, 0.0f);
                for (long i = 0; i < S; ++i)
                {
                    float* line = state + i * S;
                    const float ki = k[i], qi = q[i];
                    for (long r = 0; r < S; ++r)
                    {
                        line[r] += ki * delta[static_cast<size_t>(r)];
                        o[r] += qi * line[r];
                    }
                }
                for (long r = 0; r < S; ++r) o[r] *= qscale;
            }
        });

        // Norme RMS de chaque tête, multipliée par SiLU(z), puis la projection de sortie.
        for (long t = 0; t < T; ++t)
            for (long h = 0; h < Hv; ++h)
            {
                float* o = lin_out_.data() + t * VD + h * S;
                const float* z = z_.data() + t * VD + h * S;
                const float scale = 1.0f / std::sqrt(qk_dot_f32(o, o, S) / static_cast<float>(S) + eps);
                for (long i = 0; i < S; ++i)
                    o[i] = o[i] * scale * L.ssm_norm[static_cast<size_t>(i)] * (z[i] / (1.0f + std::exp(-z[i])));
            }
        product(L.w_ssm_out, lin_out_.data(), T, VD, tmp_.data(), d);
    }

    bool cpu_runtime::run(const forward_batch& batch, std::vector<float>& logits, bool on_device)
    {
        const long T = static_cast<long>(batch.tokens.size());
        if (T == 0) { logits.clear(); return true; }
        const long d = cfg_.d_model;
        const long qd = cfg_.q_dim();
        const long kvd = cfg_.kv_dim();
        const long hd = cfg_.head_dim;
        const long H = cfg_.n_heads;
        const long KVH = cfg_.n_kv_heads;
        const long B = kv_.block_positions();
        const float eps = static_cast<float>(cfg_.rms_eps);
        const float rs = static_cast<float>(cfg_.residual_scale);
        if (scratch_.empty()) scratch_.resize(1);

        // Positions des tokens, qui prolongent leurs séquences.
        positions_.assign(static_cast<size_t>(T), 0);
        long max_pos = 0;
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
                max_pos = std::max(max_pos, seq.length + j);
            }
        }
        // Les positions et les embeddings que donne le lot, pour les tokens des images.
        const bool given = !batch.positions.empty();
        if (given)
        {
            if (static_cast<long>(batch.positions.size()) != T * forward_batch::position_components)
                throw std::runtime_error("cpu_runtime: the batch gives positions for other tokens than its own");
            for (long p : batch.positions)
            {
                if (p < 0) throw std::runtime_error("cpu_runtime: the batch gives a negative position");
                max_pos = std::max(max_pos, p);
            }
        }
        if (!batch.embedding_rows.empty()) check_given_embeddings(batch, T);
        ensure_rope(max_pos + 1);
        if (given) prepare_given_positions(batch, T);

        embed(batch.tokens);
        if (!batch.embedding_rows.empty()) add_given_embeddings(batch, 0);
        xn_.resize(static_cast<size_t>(T * d));
        q_.resize(static_cast<size_t>(T * qd));
        k_.resize(static_cast<size_t>(T * kvd));
        v_.resize(static_cast<size_t>(T * kvd));
        tmp_.resize(static_cast<size_t>(T * d));
        if (on_device && !layer_pass_->begin(x_.data(), T)) return false;

        for (long li = 0; li < cfg_.n_layers; ++li)
        {
            const layer& L = layers_[static_cast<size_t>(li)];
            const long slot = kv_slot_[static_cast<size_t>(li)];

            if (slot < 0)
            {
                // Couche linéaire (qwen35) : Gated DeltaNet, puis la somme résiduelle.
                rms_norm_rows(x_.data(), L.attn_norm.data(), xn_.data(), T, d, eps);
                linear_attention(L, linear_slot_[static_cast<size_t>(li)], batch);
                for (size_t i = 0; i < x_.size(); ++i) x_[i] += rs * tmp_[i];
            }
            else
            {
            // Bloc d'attention.
            if (on_device)
                layer_pass_->attention_inputs(li, q_.data(), k_.data(), v_.data());
            else
            {
                rms_norm_rows(x_.data(), L.attn_norm.data(), xn_.data(), T, d, eps);
                if (cfg_.attn_output_gate)
                {
                    // attn_q donne, tête par tête, la requête puis sa porte.
                    qg_.resize(static_cast<size_t>(T * 2 * qd));
                    attn_gate_.resize(static_cast<size_t>(T * qd));
                    product(L.wq, xn_.data(), T, d, qg_.data(), 2 * qd);
                    for (long t = 0; t < T; ++t)
                        for (long h = 0; h < H; ++h)
                        {
                            const float* src = qg_.data() + t * 2 * qd + h * 2 * hd;
                            std::copy(src, src + hd, q_.data() + t * qd + h * hd);
                            std::copy(src + hd, src + 2 * hd, attn_gate_.data() + t * qd + h * hd);
                        }
                }
                else
                    product(L.wq, xn_.data(), T, d, q_.data(), qd);
                product(L.wk, xn_.data(), T, d, k_.data(), kvd);
                product(L.wv, xn_.data(), T, d, v_.data(), kvd);
            }
            if (!L.bq.empty())
                for (long t = 0; t < T; ++t)
                {
                    for (long i = 0; i < qd; ++i) q_[static_cast<size_t>(t * qd + i)] += L.bq[static_cast<size_t>(i)];
                    for (long i = 0; i < kvd; ++i) k_[static_cast<size_t>(t * kvd + i)] += L.bk[static_cast<size_t>(i)];
                    for (long i = 0; i < kvd; ++i) v_[static_cast<size_t>(t * kvd + i)] += L.bv[static_cast<size_t>(i)];
                }
            if (!L.q_norm.empty())
            {
                rms_norm_rows(q_.data(), L.q_norm.data(), q_.data(), T * H, hd, eps);
                rms_norm_rows(k_.data(), L.k_norm.data(), k_.data(), T * KVH, hd, eps);
            }
            if (given)
            {
                apply_given_rope(q_.data(), T, H);
                apply_given_rope(k_.data(), T, KVH);
            }
            else
            {
                apply_rope(q_.data(), T, H, positions_);
                apply_rope(k_.data(), T, KVH, positions_);
            }
            if (cfg_.attn_temp_scale != 0.0)
                for (long t = 0; t < T; ++t)
                {
                    const double pos = static_cast<double>(positions_[static_cast<size_t>(t)]);
                    const float f = static_cast<float>(1.0 + cfg_.attn_temp_scale
                        * std::log(1.0 + std::floor(pos / cfg_.attn_temp_length)));
                    if (f != 1.0f) qk_scale(q_.data() + t * qd, f, qd);
                }

            // Les clés et valeurs du lot vont dans les séquences avant que l'attention ne les lise,
            // pour que chaque token voie les tokens précédents de son propre morceau.
            store_keys_values(slot, batch);

            attention(slot, batch, positions_);
            if (cfg_.attn_output_gate)
                for (long i = 0; i < T * qd; ++i)
                    ctx_[static_cast<size_t>(i)] *= 1.0f / (1.0f + std::exp(-attn_gate_[static_cast<size_t>(i)]));
            if (on_device)
            {
                // La projection, le bloc feed-forward et les deux sommes résiduelles.
                layer_pass_->attention_output(li, ctx_.data());
                continue;
            }
            product(L.wo, ctx_.data(), T, qd, tmp_.data(), d);
            for (size_t i = 0; i < x_.size(); ++i) x_[i] += rs * tmp_[i];
            }

            // Bloc feed-forward.
            rms_norm_rows(x_.data(), L.ffn_norm.data(), xn_.data(), T, d, eps);
            if (cfg_.is_moe())
            {
                moe_ffn(L, T);
            }
            else
            {
                const long df = cfg_.d_ffn;
                gate_.resize(static_cast<size_t>(T * df));
                up_.resize(static_cast<size_t>(T * df));
                product(L.w_gate, xn_.data(), T, d, gate_.data(), df);
                product(L.w_up, xn_.data(), T, d, up_.data(), df);
                const long n = T * df;
                auto gated = [&](long a, long b) { qk_silu_mul(gate_.data() + a, up_.data() + a, b - a); };
                if (n >= 65536)
                    pool_.parallel_for((n + 4095) / 4096, [&](long c, int) {
                        gated(c * 4096, std::min(n, c * 4096 + 4096));
                    });
                else
                    gated(0, n);
                product(L.w_down, gate_.data(), T, df, tmp_.data(), d);
            }
            for (size_t i = 0; i < x_.size(); ++i) x_[i] += rs * tmp_[i];
            if (li < cfg_.n_deepstack && !batch.embedding_rows.empty()) add_given_embeddings(batch, li + 1);
        }

        if (on_device) layer_pass_->end(batch.logit_rows, x_.data());
        for (const auto& s : batch.segments)
            sequences_[static_cast<size_t>(s.sequence)]->length += s.count;

        // Normalisation finale et logits des seules lignes demandées.
        const long R = static_cast<long>(batch.logit_rows.size());
        hidden_out_.resize(static_cast<size_t>(R * d));
        for (long r = 0; r < R; ++r)
        {
            const long t = batch.logit_rows[static_cast<size_t>(r)];
            rms_norm_rows(x_.data() + t * d, output_norm_.data(), hidden_out_.data() + r * d, 1, d, eps);
        }
        if (batch.hidden_only)
        {
            logits.clear();
            return true;
        }
        logits.resize(static_cast<size_t>(R * cfg_.vocab));
        if (R > 0)
        {
            product(output_, hidden_out_.data(), R, d, logits.data(), cfg_.vocab);
            if (cfg_.logit_scale != 1.0)
                qk_scale(logits.data(), static_cast<float>(1.0 / cfg_.logit_scale), R * cfg_.vocab);
        }
        return true;
    }

}
