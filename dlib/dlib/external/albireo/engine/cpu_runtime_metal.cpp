// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Les passes de la génération sur le GPU du Mac (portage macOS) : cpu_runtime::set_gpu() et les
// fonctions qu'il ajoute.  Une passe d'au plus metal_device::max_tokens tokens d'un modèle dense
// s'exécute entière sur le GPU, en une seule commande : le processeur écrit les embeddings de ses
// tokens, les cosinus et sinus de leurs positions et les blocs de leurs séquences dans des tampons
// qu'ils partagent, le GPU calcule chaque couche, les clés et valeurs allant dans la région du pool,
// où le processeur les lit aussi, ainsi que les logits des lignes demandées.  Une passe de plus de
// tokens (un prompt), d'un mélange d'experts ou des images reste sur le processeur.

#include "cpu_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
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
        // Les passes après un contrôle reporté par manque de place, avant le suivant.
        int wait = 0;

        // Les poids : tout le mappage du fichier, sans copie.
        metal_buffer weights;
        size_t weights_offset = 0;
        const uint8_t* file_base = nullptr;
        // Les normes et les biais, copiés une fois, et l'endroit où chaque vecteur y commence.
        metal_buffer vectors;
        struct layer_vectors
        {
            size_t attn_norm = 0, ffn_norm = 0, q_norm = 0, k_norm = 0, bq = 0, bk = 0, bv = 0;
            // Les couches Gated DeltaNet (qwen35).
            size_t conv1d = 0, dt_bias = 0, ssm_a = 0, ssm_norm = 0;
            // La porte de l'expert partagé (qwen35moe).
            size_t sh_gate_inp = 0;
        };
        std::vector<layer_vectors> layers;
        size_t output_norm = 0;
        // La région du pool, reprise plus grande quand un bloc se trouve au-delà.
        metal_buffer region;
        size_t region_covered = 0;
        // Les tampons de travail d'une passe, pour capacity tokens.
        metal_buffer x, xn, q, k, v, ctx, gate, up, hidden, logits, cs, blocks, rows, lengths, table, partial;
        // qwen35 : les canaux de la convolution, la porte de sortie, l'oubli et la force, les sorties des têtes,
        // la requête et sa porte, la porte de l'attention, les segments du lot ; l'état des séquences, pris
        // sans copie dans la zone de cpu_runtime.
        metal_buffer mix, z, alpha, beta, lin_out, qg, attn_gate, segments, states;
        // Le mélange d'experts : logits du routeur, experts choisis et leurs poids, porte et branche up des experts,
        // leurs sorties ; l'expert partagé et sa porte.
        metal_buffer moe_logits, moe_ids, moe_w, moe_g, moe_u, moe_y, sh_g, sh_u, sh, sh_gate;
        // Les tuiles de l'attention des prompts : (premier token, nombre de tokens).
        metal_buffer tiles;
        size_t capacity = 0, table_capacity = 0, partial_capacity = 0;
        // Les poids d'un produit d'un prompt développés en floats, deux tampons à tour de rôle.
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

        // Les formats des produits d'une étape : Q5_K et Q8_0 en plus, qu'emploient les modèles qwen35.
        bool step_format(const weight_matrix& W)
        {
            return gpu_format(W) || W.type == dlib::ggml_type::Q5_K || W.type == dlib::ggml_type::Q8_0
                || W.type == dlib::ggml_type::Q5_0;
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
            Les passes de plus de tokens que metal_device::max_tokens (les prompts) vont au GPU à partir de
            qk_float_product_tokens tokens, là où le processeur passe lui aussi aux produits en floats, et
            tant que le contexte où elles finissent contient au plus gpu_prompt_context_ positions.  Mesuré
            sur le M2 (bench_runtime, clés et valeurs en 8 bits) : le GPU lit un prompt de 24 tokens environ
            deux fois plus vite que le processeur (il développe chaque poids une fois par passe, environ
            200 ms pour un modèle 2B), un prompt de 1 024 tokens 1,7 à 2,1 fois plus vite, de 2 048 tokens
            1,2 à 1,7 fois.  Son attention de prompt lit les clés par tuiles de tokens (attention_tile, Albireo
            4.10.0) : 4 096 tokens 198 tokens/s contre 171 à 181 sur le processeur (2B) ; à 8 192, 106 contre 118 à
            123, d'où la limite de 4 096 positions par défaut.
        */
        // Les valeurs par défaut de set_gpu_prompts(), que changent les réglages gpu_prompt_tokens et
        // gpu_prompt_context.
    }

    void cpu_runtime::set_gpu(metal_device& device, std::function<void(int, const std::string&)> log)
    {
        gpu_.reset();
        const long hd = cfg_.head_dim;
        const long rep = cfg_.n_kv_heads > 0 ? cfg_.n_heads / cfg_.n_kv_heads : 0;
        // Les modèles que couvrent les noyaux : denses, leurs têtes de 64 à 256 valeurs, chaque produit en
        // Q4_K ou Q6_K.
        std::string why;
        /*
            Les modèles à couches linéaires (qwen35) et les mélanges d'experts ne passent au GPU que pour leurs
            étapes, dont les produits lisent aussi Q5_K et Q8_0 ; les autres modèles y passent aussi leurs prompts,
            dont les poids développés en floats sont en Q4_K ou Q6_K.
        */
        const bool steps_only = cfg_.recurrent() || cfg_.is_moe();
        auto formats = [&](const weight_matrix& W) { return steps_only ? step_format(W) : gpu_format(W); };
        const std::string formats_why = steps_only ? "weights in other formats than Q4_K, Q5_K, Q6_K, Q5_0 and Q8_0"
            : "weights in other formats than Q4_K and Q6_K";
        // Une matrice d'experts lue par colonnes de 256 valeurs, ou de 32 en Q8_0.
        auto expert_columns = [&](const weight_matrix& W) {
            return W.cols % (W.type == dlib::ggml_type::Q8_0 || W.type == dlib::ggml_type::Q5_0 ? 32 : 256) == 0;
        };
        if (cfg_.is_moe() && cfg_.n_experts_used > 32) why = "more than 32 experts per token";
        // qwen35 : des têtes linéaires d'au plus 256 valeurs, et un état dans la zone réservée.
        else if (cfg_.recurrent() && (cfg_.ssm_head_dim > 256 || !states_.data))
            why = "linear attention layers of this geometry";
        else if (hd != 64 && hd != 128 && hd != 256) why = "heads of " + std::to_string(hd) + " values";
        else if ((rep != 1 && rep != 2 && rep != 4 && rep != 8) || cfg_.n_heads % cfg_.n_kv_heads != 0)
            why = "query heads grouped otherwise";
        else if (cfg_.attn_temp_scale != 0.0) why = "a temperature of the attention";
        else if (cfg_.d_model % 256 != 0 || cfg_.d_ffn % 256 != 0 || cfg_.q_dim() % 256 != 0)
            why = "widths that are not multiples of 256";
        else if (!formats(output_)) why = "an output matrix in " + formats_why.substr(11);
        else
            for (long li = 0; li < cfg_.n_layers && why.empty(); ++li)
            {
                const layer& L = layers_[static_cast<size_t>(li)];
                std::vector<const weight_matrix*> all;
                if (cfg_.linear_layer(li)) all = { &L.w_mix, &L.w_z, &L.w_alpha, &L.w_beta, &L.w_ssm_out };
                else all = { &L.wq, &L.wk, &L.wv, &L.wo };
                if (cfg_.is_moe())
                {
                    for (const weight_matrix* W : { &L.e_gate[0], &L.e_up[0], &L.e_down[0] }) all.push_back(W);
                    if (!L.sh_gate_inp.empty()) for (const weight_matrix* W : { &L.sh_gate, &L.sh_up, &L.sh_down }) all.push_back(W);
                    if (L.router.type != dlib::ggml_type::F32) all.push_back(&L.router);
                    if (!expert_columns(L.e_gate[0]) || !expert_columns(L.e_down[0]))
                        why = "experts of other widths than its kernels take";
                }
                else
                    for (const weight_matrix* W : { &L.w_gate, &L.w_up, &L.w_down }) all.push_back(W);
                for (const weight_matrix* W : all)
                    if (!formats(*W)) why = formats_why;
            }
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
        // Les normes et les biais, l'un après l'autre.
        std::vector<float> all;
        auto add = [&](const std::vector<float>& v) {
            const size_t at = all.size() * sizeof(float);
            all.insert(all.end(), v.begin(), v.end());
            // Chaque vecteur commence sur 16 octets.
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
            lv.conv1d = add(L.conv1d);
            lv.dt_bias = add(L.dt_bias);
            lv.ssm_a = add(L.ssm_a);
            lv.ssm_norm = add(L.ssm_norm);
            lv.sh_gate_inp = add(L.sh_gate_inp);
            g->layers.push_back(lv);
        }
        g->output_norm = add(output_norm_);
        g->vectors = need(device, all.size() * sizeof(float));
        std::memcpy(g->vectors.contents(), all.data(), all.size() * sizeof(float));
        // Les logits et les états cachés des lignes demandées, max_tokens au plus.
        const size_t R = metal_device::max_tokens;
        g->hidden = need(device, R * cfg_.d_model * sizeof(float));
        g->logits = need(device, R * cfg_.vocab * sizeof(float));
        if (cfg_.recurrent())
        {
            // L'état des séquences, la zone réservée entière, sans copie.
            size_t at = 0;
            g->states = device.wrap(states_.data, states_.bytes, at);
            if (!g->states.valid() || at != 0)
            {
                if (g->log) g->log(ALBIREO_LOG_WARNING, "GPU: the states of the linear attention layers cannot be "
                    "taken without copy, the passes stay on the processor");
                note_gpu("the states cannot be taken without copy");
                return;
            }
        }
        gpu_ = std::move(g);
        grow_gpu(metal_device::max_tokens);
    }

    // Les tampons de travail des passes, agrandis à tokens tokens.
    void cpu_runtime::grow_gpu(size_t tokens)
    {
        gpu_state& g = *gpu_;
        if (g.capacity >= tokens) return;
        metal_device& device = *g.device;
        // Au-delà d'une étape, un multiple de 32 lignes : les produits des prompts se font sur ce nombre de
        // lignes (voir product() dans run_gpu()).
        const size_t T = tokens <= metal_device::max_tokens ? tokens : (tokens + 31) / 32 * 32, f = sizeof(float);
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
        g.tiles = need(device, T * 2 * sizeof(int));
        if (cfg_.recurrent())
        {
            g.mix = need(device, T * cfg_.ssm_conv_dim() * f);
            g.z = need(device, T * cfg_.ssm_value_dim() * f);
            g.alpha = need(device, T * cfg_.ssm_v_heads * f);
            g.beta = need(device, T * cfg_.ssm_v_heads * f);
            g.lin_out = need(device, T * cfg_.ssm_value_dim() * f);
            g.segments = need(device, T * 3 * sizeof(int));
        }
        if (cfg_.is_moe())
        {
            const size_t K = static_cast<size_t>(cfg_.n_experts_used), de = static_cast<size_t>(cfg_.d_ffn_expert);
            g.moe_logits = need(device, T * cfg_.n_experts * f);
            g.moe_ids = need(device, T * K * sizeof(int));
            g.moe_w = need(device, T * K * f);
            g.moe_g = need(device, T * K * de * f);
            g.moe_u = need(device, T * K * de * f);
            g.moe_y = need(device, T * K * cfg_.d_model * f);
            if (cfg_.d_ffn_shared > 0)
            {
                g.sh_g = need(device, T * cfg_.d_ffn_shared * f);
                g.sh_u = need(device, T * cfg_.d_ffn_shared * f);
                g.sh = need(device, T * cfg_.d_model * f);
                g.sh_gate = need(device, T * f);
            }
        }
        if (cfg_.attn_output_gate)
        {
            g.qg = need(device, T * 2 * cfg_.q_dim() * f);
            g.attn_gate = need(device, T * cfg_.q_dim() * f);
        }
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
        // qwen35 : les étapes seulement ; ses prompts restent sur le processeur.
        // Les prompts des mélanges d'experts restent sur le processeur.
        if (cfg_.is_moe()) long_pass = false;
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
                     &g.expanded[0], &g.expanded[1], &g.mix, &g.z, &g.alpha, &g.beta, &g.lin_out, &g.qg,
                     &g.attn_gate, &g.segments, &g.moe_logits, &g.moe_ids, &g.moe_w, &g.moe_g, &g.moe_u, &g.moe_y,
                     &g.sh_g, &g.sh_u, &g.sh, &g.sh_gate, &g.tiles })
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
        Le contrôle du GPU, à sa première passe : deux séquences lisent le même prompt sur le
        processeur, puis font les deux mêmes étapes, l'une sur le GPU, l'autre sur le processeur.
        Le processeur calcule ses produits en floats simple précision (float_products_), comme le
        fait le GPU : avec les activations 8 bits de ses propres noyaux, les logits d'un modèle de
        28 couches diffèrent des valeurs exactes de quelques pour cent, ce qui masquerait un défaut
        du GPU ou en refuserait un sain.  Les logits de chaque étape concordent à 1 % près et donnent
        le même token le plus probable, sinon les passes restent sur le processeur.  Un contrôle qui
        manque de place pour ses séquences attend quelques passes.
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
        // Un prompt assez long pour le chemin des prompts sur le GPU, et dont la longueur n'est pas un multiple de
        // 32 : les produits des prompts arrondissent leurs lignes (voir product() dans run_gpu()).
        const long n = std::max<long>(29, gpu_prompt_min_), V = cfg_.vocab;
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
        // Les logits d'une passe sur le GPU contre ceux du processeur, dans detail.
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
            // Écrit de sorte qu'une valeur qui n'est pas un nombre échoue.
            if (!(relative <= limit) || ig != ic) agree = false;
        };
        // Le prompt : sur le GPU pour la première séquence quand les prompts y vont, sur le processeur
        // sinon.
        const bool gpu_prompt = !cfg_.is_moe() && n >= gpu_prompt_min_ && n <= gpu_prompt_context_;
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
            Un prompt écrit les clés et valeurs de tous ses tokens : en 8 bits, une valeur qu'une différence
            d'arrondi de 1e-7 fait passer de l'autre côté du milieu de deux entiers prend l'autre entier, un
            pas d'environ 1 % de son groupe, que les couches suivantes portent et multiplient.  Ses logits
            diffèrent alors de ceux du processeur d'un ou deux pour cent avec des noyaux sains (1,5 % sur
            le 2B, 16 tokens ; 0,04 % avec les clés et valeurs en demi-précision).  Un défaut donne bien
            plus, et un autre token le plus probable.
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

        // Les positions des tokens, qui prolongent leurs séquences, telles que run() les vérifie.
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
        // Les positions rotatives que donne le lot (les modèles multimodaux les donnent pour chaque token,
        // les trois mêmes composantes pour un token de texte), telles que run() les prend.
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

        // Les cosinus et sinus de chaque token, le bloc et la ligne que chacun écrit, la longueur qu'il
        // voit, et les blocs de sa séquence.
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
        // La région du pool, couvrant chaque bloc de ces séquences.
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
        // L'attention d'une étape se fait par parties de attention_part positions ; un prompt a assez de
        // tokens pour remplir le GPU avec une partie chacun.
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
        // Les poids d'un prompt développés en floats : la plus grande matrice d'une couche, deux fois.
        if (!step && g.expanded_floats == 0)
        {
            size_t largest = 0;
            for (const layer& L : layers_)
                for (const weight_matrix* M : { &L.wq, &L.wk, &L.wv, &L.wo, &L.w_gate, &L.w_up, &L.w_down, &L.w_mix,
                         &L.w_z, &L.w_alpha, &L.w_beta, &L.w_ssm_out })
                    largest = std::max(largest, static_cast<size_t>(M->rows) * static_cast<size_t>(M->cols));
            g.expanded[0] = need(dev, largest * sizeof(float));
            g.expanded[1] = need(dev, largest * sizeof(float));
            g.expanded_floats = largest;
            note_gpu();
        }

        // Les segments du lot, pour les couches linéaires de qwen35 : (séquence, premier token, nombre de tokens).
        metal_device::commands::gdn_layout gl;
        if (cfg_.recurrent())
        {
            int* sg = static_cast<int*>(g.segments.contents());
            for (size_t i = 0; i < batch.segments.size(); ++i)
            {
                sg[3 * i] = batch.segments[i].sequence;
                sg[3 * i + 1] = static_cast<int>(batch.segments[i].start);
                sg[3 * i + 2] = static_cast<int>(batch.segments[i].count);
            }
            gl.C = static_cast<unsigned>(cfg_.ssm_conv_dim());
            gl.K = static_cast<unsigned>(cfg_.ssm_conv_kernel);
            gl.Hk = static_cast<unsigned>(cfg_.ssm_k_heads);
            gl.Hv = static_cast<unsigned>(cfg_.ssm_v_heads);
            gl.S = static_cast<unsigned>(cfg_.ssm_head_dim);
            gl.slot_floats = static_cast<unsigned>(state_slot_bytes_ / sizeof(float));
            gl.conv_floats = static_cast<unsigned>(cfg_.ssm_conv_state());
            gl.eps = eps;
        }
        const unsigned n_segments = static_cast<unsigned>(batch.segments.size());
        // Les prompts lisent leurs clés par tuiles de tokens consécutifs d'une même séquence (attention_tiles()).
        const long per_tile = static_cast<long>(metal_device::tile_tokens(static_cast<unsigned>(rep),
            static_cast<unsigned>(hd)));
        const bool tiled = !step && per_tile > 0;
        unsigned n_tiles = 0;
        if (tiled)
        {
            int* tl = static_cast<int*>(g.tiles.contents());
            for (const auto& s : batch.segments)
                for (long j = 0; j < s.count; j += per_tile)
                {
                    tl[2 * n_tiles] = static_cast<int>(s.start + j);
                    tl[2 * n_tiles + 1] = static_cast<int>(std::min(per_tile, s.count - j));
                    ++n_tiles;
                }
        }
        const float scale = cfg_.attention_scale > 0.0 ? static_cast<float>(cfg_.attention_scale)
            : 1.0f / std::sqrt(static_cast<float>(hd));
        const unsigned uT = static_cast<unsigned>(T);
        auto c = dev.begin();
        auto at = [&](const metal_buffer& b, size_t offset) { return metal_view(b, offset); };
        // Un produit de la passe : par blocs en place pour une étape, développé puis via MPS pour un prompt.
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
            /*
                Metal Performance Shaders calcule sur un nombre de lignes multiple de 32, que les tampons contiennent
                (grow_gpu()) : avec 24 à 31 lignes, ou 40, un produit de Qwen3.5-0.8B (attn_qkv, 6 144 x 1 024 en
                floats) rendait des NaN.  Les lignes au-delà des tokens sont calculées pour rien.
            */
            c->gemm(x, static_cast<unsigned>((T + 31) / 32 * 32), M.cols, x_stride, e, M.rows, M.cols, y, y_stride, alpha, beta);
        };
        for (long li = 0; li < cfg_.n_layers; ++li)
        {
            const layer& L = layers_[static_cast<size_t>(li)];
            const gpu_state::layer_vectors& lv = g.layers[static_cast<size_t>(li)];
            const long slot = kv_slot_[static_cast<size_t>(li)];
            if (slot < 0)
            {
                // Couche Gated DeltaNet (qwen35), puis la somme résiduelle dans x.
                const long C = cfg_.ssm_conv_dim(), VD = cfg_.ssm_value_dim(), Hv = cfg_.ssm_v_heads;
                gl.layer_offset = static_cast<unsigned>(linear_slot_[static_cast<size_t>(li)] * cfg_.ssm_state_floats());
                c->rms_norm(g.x, at(g.vectors, lv.attn_norm), g.xn, uT, d, d, d, eps);
                product(L.w_mix, g.xn, d, g.mix, C, 1.0f, 0.0f);
                product(L.w_z, g.xn, d, g.z, VD, 1.0f, 0.0f);
                product(L.w_alpha, g.xn, d, g.alpha, Hv, 1.0f, 0.0f);
                product(L.w_beta, g.xn, d, g.beta, Hv, 1.0f, 0.0f);
                c->gdn_conv(g.mix, at(g.vectors, lv.conv1d), g.states, g.segments, n_segments, gl);
                c->gdn_l2norm(g.mix, uT, gl);
                c->gdn_gates(g.alpha, g.beta, at(g.vectors, lv.dt_bias), at(g.vectors, lv.ssm_a), uT, gl);
                c->gdn_delta(g.mix, g.alpha, g.beta, g.states, g.segments, n_segments, g.lin_out, gl);
                c->gdn_norm(g.lin_out, g.z, at(g.vectors, lv.ssm_norm), uT, gl);
                product(L.w_ssm_out, g.lin_out, VD, g.x, d, rs, 1.0f);
            }
            else
            {
            metal_device::commands::kv_layout kl;
            kl.hd = static_cast<unsigned>(hd);
            kl.heads = static_cast<unsigned>(KVH);
            kl.positions = static_cast<unsigned>(B);
            kl.q8 = q8;
            kl.block_stride = kv_.block_offset(1);
            kl.k_slab = kv_.slab_offset(false, slot, 0);
            kl.v_slab = kv_.slab_offset(true, slot, 0);
            kl.head_stride = kv_.slab_offset(false, slot, 1) - kv_.slab_offset(false, slot, 0);

            // Bloc d'attention.
            c->rms_norm(g.x, at(g.vectors, lv.attn_norm), g.xn, uT, d, d, d, eps);
            if (cfg_.attn_output_gate)
            {
                // qwen35 : la requête et sa porte, tête par tête, puis séparées.
                product(L.wq, g.xn, d, g.qg, 2 * qd, 1.0f, 0.0f);
                c->split_gate(g.qg, g.q, g.attn_gate, uT, static_cast<unsigned>(H), static_cast<unsigned>(hd));
                product(L.wk, g.xn, d, g.k, kvd, 1.0f, 0.0f);
            }
            else if (step && L.wq.type == L.wk.type && qd % 16 == 0)
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
            if (tiled)
                c->attention_tiles(g.q, g.region, g.table, g.lengths, static_cast<unsigned>(table_stride), g.tiles, n_tiles,
                    static_cast<unsigned>(rep), static_cast<unsigned>(qd), g.ctx, kl);
            else
                c->attention(g.q, g.region, g.table, g.lengths, static_cast<unsigned>(table_stride), uT, rep, parts, qd,
                    g.partial, g.ctx, kl, part_len);
            if (cfg_.attn_output_gate) c->sigmoid_mul(g.ctx, g.attn_gate, static_cast<unsigned>(T * qd));
            product(L.wo, g.ctx, qd, g.x, d, rs, 1.0f);
            }

            // Bloc feed-forward.
            c->rms_norm(g.x, at(g.vectors, lv.ffn_norm), g.xn, uT, d, d, d, eps);
            if (cfg_.is_moe())
            {
                // Le mélange d'experts : routage, experts choisis par indirection, expert partagé, puis la somme
                // pondérée ajoutée à x.
                const long E = cfg_.n_experts, K = cfg_.n_experts_used, de = cfg_.d_ffn_expert;
                const unsigned slots = uT * static_cast<unsigned>(K);
                if (L.router.type == dlib::ggml_type::F32)
                    c->matvec_f32(at(g.weights, g.weight(L.router)), static_cast<unsigned>(E), static_cast<unsigned>(d),
                        g.xn, uT, static_cast<unsigned>(d), g.moe_logits, static_cast<unsigned>(E));
                else
                    product(L.router, g.xn, d, g.moe_logits, E, 1.0f, 0.0f);
                c->moe_route(g.moe_logits, static_cast<unsigned>(E), static_cast<unsigned>(K), cfg_.expert_weights_norm,
                    g.moe_ids, g.moe_w, uT);
                auto experts = [&](const std::vector<weight_matrix>& M, const metal_buffer& x, long x_stride, unsigned x_div,
                                   const metal_buffer& y, long y_stride) {
                    const weight_matrix& W = M[0];
                    c->matvec_id(W.type, at(g.weights, g.weight(W)), W.rows, W.cols,
                        static_cast<uint64_t>(W.row_bytes) * static_cast<uint64_t>(W.rows), x,
                        static_cast<unsigned>(x_stride), x_div, g.moe_ids, slots, y, static_cast<unsigned>(y_stride));
                };
                experts(L.e_gate, g.xn, d, static_cast<unsigned>(K), g.moe_g, de);
                experts(L.e_up, g.xn, d, static_cast<unsigned>(K), g.moe_u, de);
                c->silu_mul(g.moe_g, g.moe_u, slots * static_cast<unsigned>(de));
                experts(L.e_down, g.moe_g, de, 1, g.moe_y, d);
                const bool shared = !L.sh_gate_inp.empty();
                if (shared)
                {
                    const long ds = cfg_.d_ffn_shared;
                    product(L.sh_gate, g.xn, d, g.sh_g, ds, 1.0f, 0.0f);
                    product(L.sh_up, g.xn, d, g.sh_u, ds, 1.0f, 0.0f);
                    c->silu_mul(g.sh_g, g.sh_u, static_cast<unsigned>(T * ds));
                    product(L.sh_down, g.sh_g, ds, g.sh, d, 1.0f, 0.0f);
                    c->shared_gate(g.xn, at(g.vectors, lv.sh_gate_inp), g.sh_gate, static_cast<unsigned>(d), uT);
                }
                const metal_view sh(g.sh);
                c->moe_combine(g.x, g.moe_y, g.moe_w, shared ? &sh : nullptr, g.sh_gate, static_cast<unsigned>(d),
                    static_cast<unsigned>(K), uT, rs);
            }
            else
            {
                if (step && L.w_gate.type == L.w_up.type && df % 16 == 0)
                    c->matvec2(L.w_gate.type, at(g.weights, g.weight(L.w_gate)), df, at(g.weights, g.weight(L.w_up)), df,
                        d, g.xn, uT, d, g.gate, df, g.up, df);
                else
                {
                    product(L.w_gate, g.xn, d, g.gate, df, 1.0f, 0.0f);
                    product(L.w_up, g.xn, d, g.up, df, 1.0f, 0.0f);
                }
                c->silu_mul(g.gate, g.up, static_cast<unsigned>(T * df));
                product(L.w_down, g.gate, df, g.x, d, rs, 1.0f);
            }
        }
        // Normalisation finale et logits des seules lignes demandées.
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

    void cpu_runtime::run_gpu_checkpointed(const forward_batch& batch, std::vector<float>& logits)
    {
        const long T = static_cast<long>(batch.tokens.size());
        const long d = cfg_.d_model, V = cfg_.vocab;
        // La position de chaque token, et sa séquence.
        std::vector<long> pos(static_cast<size_t>(T), 0);
        std::vector<int> seq_of(static_cast<size_t>(T), 0);
        for (const auto& s : batch.segments)
            for (long j = 0; j < s.count; ++j)
            {
                pos[static_cast<size_t>(s.start + j)] = sequences_.at(static_cast<size_t>(s.sequence))->length + j;
                seq_of[static_cast<size_t>(s.start + j)] = s.sequence;
            }
        auto cut = [&](long t) { return (pos[static_cast<size_t>(t)] + 1) % checkpoint_interval == 0; };
        std::vector<long> row_of(static_cast<size_t>(T), -1);
        for (size_t r = 0; r < batch.logit_rows.size(); ++r) row_of[static_cast<size_t>(batch.logit_rows[r])] = static_cast<long>(r);
        const long R = static_cast<long>(batch.logit_rows.size());
        std::vector<float> all_logits(batch.hidden_only ? 0 : static_cast<size_t>(R * V)), all_hidden(static_cast<size_t>(R * d));
        std::vector<float> part;
        const size_t floats = static_cast<size_t>(cfg_.n_layers - cfg_.n_attention_layers())
            * static_cast<size_t>(cfg_.ssm_state_floats());
        for (long start = 0; start < T;)
        {
            long end = start;
            while (end + 1 < T && !cut(end)) ++end;
            forward_batch sub;
            sub.hidden_only = batch.hidden_only;
            sub.tokens.assign(batch.tokens.begin() + start, batch.tokens.begin() + end + 1);
            for (const auto& s : batch.segments)
            {
                const long a = std::max(s.start, start), b = std::min(s.start + s.count, end + 1);
                if (a < b) sub.segments.push_back({ s.sequence, a - start, b - a });
            }
            for (long t = start; t <= end; ++t)
                if (row_of[static_cast<size_t>(t)] >= 0) sub.logit_rows.push_back(t - start);
            run_gpu(sub, part);
            for (size_t k = 0; k < sub.logit_rows.size(); ++k)
            {
                const long r = row_of[static_cast<size_t>(sub.logit_rows[k] + start)];
                if (!batch.hidden_only)
                    std::copy(part.begin() + static_cast<long>(k) * V, part.begin() + static_cast<long>(k + 1) * V,
                        all_logits.begin() + r * V);
                std::copy(hidden_out_.begin() + static_cast<long>(k) * d, hidden_out_.begin() + static_cast<long>(k + 1) * d,
                    all_hidden.begin() + r * d);
            }
            // Le point de reprise que ce morceau atteint : l'état de sa séquence, que le GPU vient d'écrire.
            if (cut(end))
            {
                checkpoint c;
                c.position = pos[static_cast<size_t>(end)] + 1;
                c.stamp = ++checkpoint_stamp_;
                c.state.assign(state_of(seq_of[static_cast<size_t>(end)]), state_of(seq_of[static_cast<size_t>(end)]) + floats);
                pending_checkpoints_.clear();
                pending_checkpoints_.emplace_back(seq_of[static_cast<size_t>(end)], std::move(c));
                keep_checkpoints();
            }
            start = end + 1;
        }
        logits.swap(all_logits);
        hidden_out_.swap(all_hidden);
    }

}
