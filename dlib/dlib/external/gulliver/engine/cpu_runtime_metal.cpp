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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_map>

#include <sys/sysctl.h>

#include "../common/engine_api.h"
#include "metal_device.h"

namespace gulliver
{

    struct cpu_runtime::gpu_state
    {
        metal_device* device = nullptr;
        std::function<void(int, const std::string&)> log;
        enum { unchecked, ready, dropped } state = unchecked;
        // Les passes après un contrôle reporté par manque de place, avant le suivant.
        int wait = 0;
        // Les prompts d'un mélange d'experts passent au GPU (produits groupés, matmul_id) : qwen35moe seulement.
        bool moe_prompts = false;

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
            // La porte de l'expert partagé (qwen35moe), le biais du choix des experts (LFM2-MoE).
            size_t sh_gate_inp = 0, expert_bias = 0;
            // Les normes après l'attention et après le bloc feed-forward (Gemma 3 et 4), celle de l'entrée de la couche
            // (Gemma 4).
            size_t post_attn_norm = 0, post_ffn_norm = 0, ple_norm = 0;
        };
        std::vector<layer_vectors> layers;
        size_t output_norm = 0;
        // Des uns, le poids de la norme des valeurs de Gemma 4, qui n'en a pas.
        size_t ones = 0;
        // La région du pool, reprise plus grande quand un bloc se trouve au-delà.
        metal_buffer region;
        size_t region_covered = 0;
        // Les tampons de travail d'une passe, pour capacity tokens.
        metal_buffer x, xn, q, k, v, ctx, gate, up, hidden, logits, cs, blocks, rows, lengths, table, partial;
        // Gemma 3 et 4 : les cosinus et sinus des couches à fenêtre glissante, et la sortie d'un bloc avant sa norme ;
        // Gemma 4 : les entrées par couche, rangées couche par couche, et leur porte.
        metal_buffer cs_swa, out, ple, ple_g;
        // Mistral 3 : le facteur de température de l'attention de chaque token (attn_temp_scale), pour prepare_queries.
        metal_buffer qtemp;
        // qwen35 : les canaux de la convolution, la porte de sortie, l'oubli et la force, les sorties des têtes,
        // la requête et sa porte, la porte de l'attention, les segments du lot ; l'état des séquences, pris
        // sans copie dans la zone de cpu_runtime.
        metal_buffer mix, z, alpha, beta, lin_out, qg, attn_gate, segments, states;
        // Le mélange d'experts : logits du routeur, experts choisis et leurs poids, porte et branche up des experts,
        // leurs sorties ; l'expert partagé et sa porte.
        metal_buffer moe_logits, moe_ids, moe_w, moe_g, moe_u, moe_y, sh_g, sh_u, sh, sh_gate;
        // Les produits groupés des prompts : les cases de chaque expert, leurs nombres, les tuiles de travail et les
        // arguments des lancements indirects.
        metal_buffer moe_list, moe_counts, moe_work, moe_dispatch;
        // Les tuiles de l'attention des prompts : (premier token, nombre de tokens).
        metal_buffer tiles;
        // Les clés et valeurs d'une séquence rangées en demi-précision pour attention_flat, et leur taille en octets.
        metal_buffer flat_k, flat_v;
        size_t flat_bytes = 0;
        size_t capacity = 0, table_capacity = 0, partial_capacity = 0;
        // Les poids d'un produit d'un prompt développés en floats, deux tampons à tour de rôle.
        metal_buffer expanded[2];
        size_t expanded_floats = 0;
        // Les entrées d'un produit de prompt en demi-précision (Gulliver 6.7.0) : 32 lignes de plus que les tokens au plus.
        metal_buffer xh;
        // Les produits des prompts en demi-précision ; false quand le contrôle ne passe qu'en floats (gpu_ready()).
        bool half_prompts = true;
        /* Les poids développés en demi-précision gardés d'une passe à l'autre (essai du chantier Vitesse, 2026-11-21) :
           développer chaque matrice à chaque passe coûte, pour un 4B, environ 17 Go de mémoire lus et écrits par passe.
           Une matrice développée une fois est reprise aux passes suivantes, tant que half_kept ne dépasse pas
           half_limit : un quart de la mémoire du Mac (16 Go sur le Mac mini, de quoi garder un 8B), ou
           GULLIVER_GPU_HALF_WEIGHTS_MB pour les mesures (0 coupe).  Mesuré sur le Mac mini (Qwen3-VL 4B, prompt de
           512 tokens, mesures alternées) : 2 060-2 159 -> 2 613-2 622 tokens/s ; perplexité identique. */
        std::unordered_map<size_t, metal_buffer> half_weights;
        size_t half_kept = 0, half_limit = 0;
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
            return W.type == gulliver::ggml_type::Q4_K || W.type == gulliver::ggml_type::Q6_K;
        }

        // Les formats des produits d'une étape : Q5_K et Q8_0 en plus, qu'emploient les modèles qwen35.
        bool step_format(const weight_matrix& W)
        {
            return gpu_format(W) || W.type == gulliver::ggml_type::Q5_K || W.type == gulliver::ggml_type::Q8_0
                || W.type == gulliver::ggml_type::Q5_0;
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
            1,2 à 1,7 fois.  Son attention de prompt (attention_mtile, instructions matricielles, Gulliver 4.11.0)
            gagne à toutes les longueurs mesurées (2B, mesures alternées : 4 096 tokens 423 à 424 tokens/s contre
            170 à 177 sur le processeur, 8 192 tokens 295 contre 115 à 117, 16 384 tokens 171 contre 67) : la limite
            par défaut est la longueur des modèles, 262 144 positions.
        */
        // Les valeurs par défaut de set_gpu_prompts(), que changent les réglages gpu_prompt_tokens et
        // gpu_prompt_context.
    }

    void cpu_runtime::set_gpu(metal_device& device, std::function<void(int, const std::string&)> log)
    {
        gpu_.reset();
        const long hd = cfg_.head_dim;
        // Les modèles que couvrent les noyaux : leurs têtes de 64 à 256 valeurs, chaque produit en Q4_K ou Q6_K
        // (et les formats de step_format pour les modèles ci-dessous).
        std::string why;
        /*
            Les modèles à couches linéaires (qwen35) et les mélanges d'experts lisent aussi Q5_K, Q8_0 et Q5_0
            (step_format) : leurs étapes vont au GPU, et les prompts de qwen35 aussi, coupés aux points de reprise
            (run_gpu_checkpointed).  Les prompts des mélanges d'experts y vont avec leurs experts en produits groupés
            (matmul_id) : qwen35moe et LFM2-MoE d'abord, qwen3moe, granitemoe et mixtral depuis Gulliver 6.6.0.
        */
        /*
            Les modèles denses lisent aussi Q5_K, Q8_0 et Q5_0 depuis Gulliver 6.32.0 (fork pour macOS, 2026-11-22) : les
            étapes ont leurs produits dans ces formats (mv_*_1, mv_*_n, mv_nax), et les prompts les développent en
            demi-précision (dequant_h_*) avant le produit de MPS, comme pour Q4_K et Q6_K.  Qwen3-Embedding 0,6B (Q8_0)
            restait sur le processeur pour cette seule raison.
        */
        auto formats = [&](const weight_matrix& W) { return step_format(W); };
        const std::string formats_why = "weights in other formats than Q4_K, Q5_K, Q6_K, Q5_0 and Q8_0";
        // Une matrice d'experts lue par colonnes de 256 valeurs, ou de 32 en Q8_0.
        auto expert_columns = [&](const weight_matrix& W) {
            return W.cols % (W.type == gulliver::ggml_type::Q8_0 || W.type == gulliver::ggml_type::Q5_0 ? 32 : 256) == 0;
        };
        /*
            Gemma 3 passe au GPU depuis Gulliver 4.22.0, les étapes de Gemma 4 depuis Gulliver 4.26.0 (têtes de 256 et
            de 512, clés et valeurs partagées, norme des valeurs, entrées par couche, échelle de sortie, plafond des
            logits), ses prompts depuis Gulliver 4.27.0 (attention_mtile en deux tranches de 256 pour les têtes de 512).
            Les mélanges d'experts de Gemma 4 (26B-A4B) restent sur le processeur.
        */
        const bool gemma4 = cfg_.gemma4 && !cfg_.gemma3;
        auto layer_widths = [&]() {
            for (long li = 0; li < cfg_.n_layers; ++li)
                if (cfg_.layer_ffn(li) % 256 != 0 || cfg_.layer_q_dim(li) % 256 != 0) return false;
            return true;
        };
        if (gemma4 && cfg_.is_moe()) why = "the mixtures of experts of Gemma 4";
        else if (cfg_.is_moe() && cfg_.n_experts_used > 32) why = "more than 32 experts per token";
        // qwen35 : des têtes linéaires d'au plus 256 valeurs, et un état dans la zone réservée.
        else if (cfg_.recurrent() && (cfg_.ssm_head_dim > 256 || !states_.data))
            why = "linear attention layers of this geometry";
        else if (hd != 64 && hd != 128 && hd != 256 && !(gemma4 && hd == 512)) why = "heads of " + std::to_string(hd) + " values";
        else if (gemma4 && cfg_.head_dim_swa != 256 && cfg_.head_dim_swa != hd)
            why = "sliding window heads of " + std::to_string(cfg_.head_dim_swa) + " values";
        // Les regroupements que les noyaux ne prennent pas passent par une tête de requête par groupe (regroup(),
        // metal_device.mm) ; les têtes doivent seulement se partager exactement.
        else if (cfg_.n_heads % cfg_.n_kv_heads != 0)
            why = "query heads grouped otherwise";
        else if (cfg_.d_model % 256 != 0 || cfg_.d_ffn % 256 != 0 || cfg_.q_dim() % 256 != 0 || !layer_widths())
            why = "widths that are not multiples of 256";
        else if (gemma4 && cfg_.d_per_layer > 0 && (cfg_.d_per_layer % 4 != 0 || layers_[0].ple_gate.type != gulliver::ggml_type::F32
            || layers_[0].ple_proj.type != gulliver::ggml_type::F32))
            why = "per-layer inputs of this form";
        // Un modèle de recherche (embedding) rend les états de ses tokens, jamais de logits : sa matrice de sortie
        // (souvent la table des tokens, en Q8_0 ou F16) n'est pas lue, son format n'empêche pas le GPU (fork pour macOS,
        // 2026-11-22 : Qwen3-Embedding restait sur le processeur pour cette seule raison).
        else if (!cfg_.is_embedding() && !formats(output_)) why = "an output matrix in " + formats_why.substr(11);
        else
            for (long li = 0; li < cfg_.n_layers && why.empty(); ++li)
            {
                const layer& L = layers_[static_cast<size_t>(li)];
                std::vector<const weight_matrix*> all;
                if (cfg_.lfm2 && cfg_.linear_layer(li)) all = { &L.w_mix, &L.w_ssm_out };
                else if (cfg_.linear_layer(li)) all = { &L.w_mix, &L.w_z, &L.w_alpha, &L.w_beta, &L.w_ssm_out };
                // Une couche qui partage les clés et valeurs d'une autre (Gemma 4) n'a ni attn_k ni attn_v.
                else if (!cfg_.owns_kv(li)) all = { &L.wq, &L.wo };
                else all = { &L.wq, &L.wk, &L.wv, &L.wo };
                if (cfg_.moe_layer(li))
                {
                    for (const weight_matrix* W : { &L.e_gate[0], &L.e_up[0], &L.e_down[0] }) all.push_back(W);
                    if (!L.sh_gate_inp.empty()) for (const weight_matrix* W : { &L.sh_gate, &L.sh_up, &L.sh_down }) all.push_back(W);
                    if (L.router.type != gulliver::ggml_type::F32) all.push_back(&L.router);
                    if (!expert_columns(L.e_gate[0]) || !expert_columns(L.e_down[0]))
                        why = "experts of other widths than its kernels take";
                }
                else
                    for (const weight_matrix* W : { &L.w_gate, &L.w_up, &L.w_down }) all.push_back(W);
                // ssm_alpha et ssm_beta (2 048 x 32) restent en floats dans certains fichiers (Qwen3.6-35B-A3B
                // UD-Q4_K_M d'unsloth) : le GPU les lit tels quels, comme le routeur (Gulliver 6.5.0).
                for (const weight_matrix* W : all)
                    if (!formats(*W) && !(W->type == gulliver::ggml_type::F32 && (W == &L.w_alpha || W == &L.w_beta)))
                        why = formats_why;
            }
        if (!why.empty())
        {
            if (log) log(GULLIVER_LOG_INFO, "GPU: the passes stay on the processor, the kernels do not cover " + why);
            note_gpu("the kernels do not cover " + why);
            return;
        }
        std::unique_ptr<gpu_state> g(new gpu_state);
        g->device = &device;
        // Les prompts des mélanges d'experts, par produits groupés : qwen35moe et LFM2-MoE d'abord, tous ceux que le
        // GPU accepte depuis Gulliver 6.6.0 (qwen3moe, granitemoe, mixtral, essayés contre llama.cpp).
        if (cfg_.is_moe())
        {
            // Les tuiles de matmul_id : 32 lignes, 64 colonnes.
            g->moe_prompts = cfg_.d_ffn_expert % 64 == 0 && cfg_.d_model % 64 == 0;
            for (long li = 0; li < cfg_.n_layers && g->moe_prompts; ++li)
            {
                const layer& L = layers_[static_cast<size_t>(li)];
                // Les premières couches denses de LFM2-MoE n'ont pas d'experts.
                if (!cfg_.moe_layer(li)) continue;
                if (L.e_gate.empty() || L.e_up.empty() || L.e_down.empty() || L.e_down[0].rows % 32 != 0
                    || L.e_gate[0].cols % 64 != 0 || L.e_down[0].cols % 64 != 0)
                    g->moe_prompts = false;
            }
        }
        g->log = std::move(log);
        g->file_base = file_->data();
        g->weights = device.wrap(file_->data(), static_cast<size_t>(file_->size()), g->weights_offset);
        if (!g->weights.valid())
        {
            if (g->log) g->log(GULLIVER_LOG_WARNING, "GPU: the weights cannot be taken without copy, the passes stay on the processor");
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
            lv.expert_bias = add(L.expert_bias);
            lv.post_attn_norm = add(L.post_attn_norm);
            lv.post_ffn_norm = add(L.post_ffn_norm);
            lv.ple_norm = add(L.ple_norm);
            g->layers.push_back(lv);
        }
        g->output_norm = add(output_norm_);
        if (cfg_.value_norm) g->ones = add(std::vector<float>(static_cast<size_t>(cfg_.head_dim), 1.0f));
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
                if (g->log) g->log(GULLIVER_LOG_WARNING, "GPU: the states of the linear attention layers cannot be "
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
        if (cfg_.attn_temp_scale != 0.0) g.qtemp = need(device, T * f);
        g.blocks = need(device, T * sizeof(int));
        g.rows = need(device, T * sizeof(int));
        g.lengths = need(device, T * sizeof(int));
        // Deux listes de tuiles (attention_tiles()) : pour les têtes du modèle, puis pour celles des couches à fenêtre
        // quand elles sont d'une autre largeur (Gemma 4 : 512 et 256) ; au plus une tuile par token chacune.
        g.tiles = need(device, T * 4 * sizeof(int));
        if (tokens > metal_device::max_tokens)
        {
            // Les entrées des produits d'un prompt, en demi-précision : la plus large des matrices, T lignes.
            size_t widest = static_cast<size_t>(output_.cols);
            for (const layer& L : layers_)
                for (const weight_matrix* M : { &L.wq, &L.wk, &L.wv, &L.wo, &L.w_gate, &L.w_up, &L.w_down, &L.w_mix,
                         &L.w_z, &L.w_alpha, &L.w_beta, &L.w_ssm_out, &L.sh_gate, &L.sh_up, &L.sh_down })
                    widest = std::max(widest, static_cast<size_t>(M->cols));
            g.xh = need(device, T * widest * 2);
        }
        if (cfg_.gemma4)
        {
            g.cs_swa = need(device, T * std::max<long>(cfg_.rope_dims_swa, 2) * f);
            g.out = need(device, T * cfg_.d_model * f);
            if (cfg_.d_per_layer > 0)
            {
                g.ple = need(device, T * cfg_.n_layers * cfg_.d_per_layer * f);
                g.ple_g = need(device, T * cfg_.d_per_layer * f);
            }
        }
        if (cfg_.lfm2)
        {
            // LFM2 : b, c et x de chaque token, puis la sortie de la convolution.
            g.mix = need(device, T * 3 * cfg_.d_model * f);
            g.lin_out = need(device, T * cfg_.d_model * f);
            g.segments = need(device, T * 3 * sizeof(int));
        }
        else if (cfg_.recurrent())
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
            if (g.moe_prompts)
            {
                const unsigned E = static_cast<unsigned>(cfg_.n_experts);
                g.moe_list = need(device, T * E * sizeof(int));
                g.moe_counts = need(device, E * sizeof(int));
                g.moe_work = need(device, metal_device::commands::moe_tiles_most(static_cast<unsigned>(T * K), E) * 2 * sizeof(int));
                g.moe_dispatch = need(device, 6 * sizeof(uint32_t));
            }
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
        // Les prompts des mélanges d'experts dont les experts n'ont pas la forme des produits groupés restent sur le processeur.
        if (cfg_.is_moe() && !(gpu_ && gpu_->moe_prompts)) long_pass = false;
        return gpu_ && gpu_->state != gpu_state::dropped && (step || long_pass)
            && batch.embedding_rows.empty() && (!batch.hidden_only || cfg_.is_embedding())
            && batch.logit_rows.size() <= metal_device::max_tokens;
    }

    void cpu_runtime::drop_gpu(const std::string& why)
    {
        if (!gpu_ || gpu_->state == gpu_state::dropped) return;
        gpu_->state = gpu_state::dropped;
        if (gpu_->log) gpu_->log(GULLIVER_LOG_WARNING, "GPU: the passes go back to the processor: " + why);
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
                     &g.hidden, &g.logits, &g.cs, &g.qtemp, &g.blocks, &g.rows, &g.lengths, &g.table, &g.partial,
                     &g.expanded[0], &g.expanded[1], &g.mix, &g.z, &g.alpha, &g.beta, &g.lin_out, &g.qg,
                     &g.attn_gate, &g.segments, &g.moe_logits, &g.moe_ids, &g.moe_w, &g.moe_g, &g.moe_u, &g.moe_y,
                     &g.sh_g, &g.sh_u, &g.sh, &g.sh_gate, &g.tiles, &g.cs_swa, &g.out })
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
        int a = create_sequence(), b = create_sequence();
        struct release
        {
            cpu_runtime& rt;
            int& a;
            int& b;
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
        /*
            Deux essais : les produits des prompts en demi-précision, puis, si le contrôle ne passe pas alors que
            le prompt va au GPU, en floats, avec deux séquences neuves (Gulliver 6.7.0 : LFM2.5-8B-A1B, dont les
            experts en 8 bits basculent sur un écart d'arrondi, ne passe qu'en floats).
        */
        std::string checked;          // le détail du contrôle qui a passé
        bool prompt_checked = false;  // le prompt du contrôle est passé par le GPU
        for (int attempt = 0;; ++attempt)
        {
            gpu_->half_prompts = attempt == 0;
            if (attempt > 0)
            {
                release_sequence(a);
                release_sequence(b);
                a = create_sequence();
                b = create_sequence();
                if (!reserve_sequence(a, n + 2) || !reserve_sequence(b, n + 2))
                {
                    gpu_->wait = 64;
                    return false;
                }
            }
            /*
                Un modèle de recherche (embedding) ne rend que les états de ses tokens (hidden_only) : le contrôle
                compare alors ces états, ceux qui deviennent les vecteurs, et pas de token le plus probable (fork pour
                macOS, 2026-11-22).
            */
            const bool states = cfg_.is_embedding();
            const long width = states ? cfg_.d_model : V;
            forward_batch prompt;
            prompt.hidden_only = states;
            for (long i = 0; i < n; ++i) prompt.tokens.push_back(static_cast<int>((1009 + 7919 * i) % V));
            std::vector<float> on_gpu, on_cpu;
            auto keep_states = [&](std::vector<float>& out) { if (states) out = hidden_out_; };
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
                for (long i = 0; i < width; ++i)
                {
                    diff += (static_cast<double>(on_gpu[i]) - on_cpu[i]) * (static_cast<double>(on_gpu[i]) - on_cpu[i]);
                    norm += static_cast<double>(on_cpu[i]) * on_cpu[i];
                    if (on_gpu[i] > on_gpu[ig]) ig = i;
                    if (on_cpu[i] > on_cpu[ic]) ic = i;
                }
                const double relative = std::sqrt(diff / std::max(norm, 1e-30));
                detail << (detail.tellp() > 0 ? "; " : "") << what << (states ? ": states within " : ": logits within ")
                    << relative * 100 << "% of those of the processor";
                if (!states) detail << ", most likely token " << (ig == ic ? "the same" : "different");
                // Écrit de sorte qu'une valeur qui n'est pas un nombre échoue.
                if (!(relative <= limit) || (!states && ig != ic)) agree = false;
            };
            // Le prompt : sur le GPU pour la première séquence quand les prompts y vont, sur le processeur
            // sinon.
            const bool gpu_prompt = (!cfg_.is_moe() || gpu_->moe_prompts) && n >= gpu_prompt_min_
                && n <= gpu_prompt_context_;
            prompt.logit_rows.assign(1, n - 1);
            prompt.segments.assign(1, { a, 0, n });
            try
            {
                if (gpu_prompt) run_gpu(prompt, on_gpu);
                else run(prompt, on_gpu, false);
                keep_states(on_gpu);
            }
            catch (const std::exception& e)
            {
                drop_gpu(std::string("the check failed on the GPU: ") + e.what());
                return false;
            }
            prompt.segments.assign(1, { b, 0, n });
            run(prompt, on_cpu, false);
            keep_states(on_cpu);
            /*
                Un prompt écrit les clés et valeurs de tous ses tokens : en 8 bits, une valeur qu'une différence
                d'arrondi de 1e-7 fait passer de l'autre côté du milieu de deux entiers prend l'autre entier, un
                pas d'environ 1 % de son groupe, que les couches suivantes portent et multiplient.  Ses logits
                diffèrent alors de ceux du processeur d'un ou deux pour cent avec des noyaux sains (1,5 % sur
                le 2B, 16 tokens ; 0,04 % avec les clés et valeurs en demi-précision).  Un défaut donne bien
                plus, et un autre token le plus probable.
            */
            /*
                Un mélange d'experts en 8 bits (fork pour macOS) : une différence d'arrondi d'une clé ou d'une valeur suffit à
                faire changer un token d'expert, ce qui déplace ses logits d'un coup ; le prompt du contrôle, des tokens pris
                au hasard, met souvent des experts à égalité.  LFM2.5-8B-A1B : 7,7 % sur le prompt et 13,6 % à la première
                étape en 8 bits, 0,13 % et 0,11 % en demi-précision, le même token le plus probable chaque fois.  Un défaut
                donne des logits sans rapport et un autre token.
            */
            /*
                En demi-précision aussi, un mélange d'experts change d'expert à une quasi-égalité (fork pour macOS, 2026-11-22) :
                Qwen3-30B-A3B (128 experts, 8 par token), clés et valeurs en demi-précision, 1,2 % sur le prompt, 1,4 % et 4,1 %
                aux deux étapes, le même token le plus probable chaque fois ; la limite de 1 % le renvoyait au processeur (lecture
                de 512 tokens 119 jetons/s au lieu de 1 136).  Les mélanges d'experts gardent donc 25 % dans les deux formats.
            */
            const bool q8_check = kv_.format() == kv_format::q8;
            const double q8_limit = cfg_.is_moe() ? 0.25 : 0.05;
            const double f16_limit = cfg_.is_moe() ? 0.25 : 0.01;
            if (gpu_prompt) compare("prompt", q8_check ? q8_limit : f16_limit);
            for (int step = 0; step < 2; ++step)
            {
                forward_batch one;
                one.hidden_only = states;
                one.tokens.push_back(static_cast<int>((4243 + 104729 * step) % V));
                one.logit_rows.push_back(0);
                one.segments.assign(1, { a, 0, 1 });
                try
                {
                    run_gpu(one, on_gpu);
                    keep_states(on_gpu);
                }
                catch (const std::exception& e)
                {
                    drop_gpu(std::string("the check failed on the GPU: ") + e.what());
                    return false;
                }
                one.segments.assign(1, { b, 0, 1 });
                run(one, on_cpu, false);
                keep_states(on_cpu);
                /*
                    Les étapes lisent les clés et valeurs que le prompt a écrites : après un prompt sur le GPU en 8 bits,
                    elles en héritent les différences d'arrondi, et reçoivent la même limite (fork pour macOS ; LFM2.5-1.2B,
                    dont six couches d'attention portent seules la mémoire du texte : 2,2 % à la première étape en 8 bits,
                    0,10 % en demi-précision).
                */
                compare(step == 0 ? "step 1" : "step 2", gpu_prompt && q8_check ? q8_limit : f16_limit);
            }
            if (!agree && attempt == 0 && gpu_prompt)
            {
                if (gpu_->log)
                    gpu_->log(GULLIVER_LOG_INFO, "GPU: the check disagrees with half precision prompt products (" + detail.str()
                        + "), checked again with float products");
                continue;
            }
            if (!agree)
            {
                drop_gpu("its check disagrees with the processor (" + detail.str() + ")");
                return false;
            }
            checked = detail.str();
            prompt_checked = gpu_prompt;
            break;
        }
        gpu_->state = gpu_state::ready;
        note_gpu();
        if (gpu_->log)
            gpu_->log(GULLIVER_LOG_INFO, "GPU " + gpu_->device->name() + ": the generation steps"
                + std::string(prompt_checked ? " and the prompts of " + std::to_string(gpu_prompt_min_)
                    + " tokens or more, up to " + std::to_string(gpu_prompt_context_) + " positions"
                + std::string(gpu_->half_prompts ? "," : " (float products),") : "")
                + " run on it (" + checked + ")");
        return true;
    }

    void cpu_runtime::run_gpu(const forward_batch& batch, std::vector<float>& logits)
    {
        gpu_state& g = *gpu_;
        metal_device& dev = *g.device;
        // Le nombre de lignes de la passe, que les couches à clés partagées réduisent (comme run()).
        long T = static_cast<long>(batch.tokens.size());
        const long d = cfg_.d_model, qd = cfg_.q_dim(), kvd = cfg_.kv_dim(), hd = cfg_.head_dim;
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
        if (cfg_.d_per_layer > 0)
        {
            // Les entrées par couche de Gemma 4, calculées par le processeur, rangées couche par couche pour que
            // chaque couche lise les siennes d'un seul tenant.
            per_layer_inputs(batch.tokens);
            const long P = cfg_.d_per_layer, n = cfg_.n_layers * P;
            float* dst = static_cast<float*>(g.ple.contents());
            for (long li = 0; li < cfg_.n_layers; ++li)
                for (long t = 0; t < T; ++t)
                    std::memcpy(dst + (li * T + t) * P, ple_.data() + t * n + li * P, sizeof(float) * P);
        }

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
            // La température de l'attention (Mistral 3), même formule que le processeur (cpu_runtime.cpp).
            if (cfg_.attn_temp_scale != 0.0)
                static_cast<float*>(g.qtemp.contents())[t] = static_cast<float>(1.0 + cfg_.attn_temp_scale
                    * std::log(1.0 + std::floor(static_cast<double>(pos) / cfg_.attn_temp_length)));
            const float* c = given ? given_cos_.data() + t * half : rope_cos_.data() + pos * half;
            const float* s = given ? given_sin_.data() + t * half : rope_sin_.data() + pos * half;
            std::memcpy(cs + t * 2 * half, c, sizeof(float) * half);
            std::memcpy(cs + t * 2 * half + half, s, sizeof(float) * half);
            if (cfg_.gemma4)
            {
                // La table des couches à fenêtre glissante (rope_base_swa, sans mise à l'échelle).
                const long hs = cfg_.rope_dims_swa / 2;
                float* csw = static_cast<float*>(g.cs_swa.contents());
                std::memcpy(csw + t * 2 * hs, rope_swa_cos_.data() + pos * hs, sizeof(float) * hs);
                std::memcpy(csw + t * 2 * hs + hs, rope_swa_sin_.data() + pos * hs, sizeof(float) * hs);
            }
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
        bool step = T <= static_cast<long>(metal_device::max_tokens);
        /* Les parties d'une étape (attention_dec) : 128 positions jusqu'à 2 048 de contexte, 256 au-delà ; mesuré sur le
           Mac mini (Qwen3-VL 4B, mesures alternées) : 512 positions 73-74 tokens/s en parties de 256, 78 en 128 ;
           4 096 : 66 en 256, 65,5 en 128 ; 16 384 : 42-45 en 256, 40-41 en 128, 39-40 en 512.  GULLIVER_ATT_PART
           impose une taille pour les mesures. */
        static const unsigned step_part = [] {
            const char* v = std::getenv("GULLIVER_ATT_PART");
            return v ? static_cast<unsigned>(std::max(8, std::atoi(v))) : 0u;
        }();
        const unsigned part_len = !step ? static_cast<unsigned>(max_pos + 1)
            : step_part ? step_part : max_pos + 1 <= 2048 ? 128u : 256u;
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
            if (const char* mb = std::getenv("GULLIVER_GPU_HALF_WEIGHTS_MB"))
                g.half_limit = static_cast<size_t>(std::atoll(mb)) << 20;
            else
            {
                uint64_t memory = 0;
                size_t length = sizeof memory;
                if (sysctlbyname("hw.memsize", &memory, &length, nullptr, 0) == 0) g.half_limit = memory / 4;
            }
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
            gl.C = static_cast<unsigned>(cfg_.lfm2 ? cfg_.d_model : cfg_.ssm_conv_dim());
            gl.K = static_cast<unsigned>(cfg_.lfm2 ? cfg_.conv_kernel : cfg_.ssm_conv_kernel);
            gl.Hk = static_cast<unsigned>(cfg_.ssm_k_heads);
            gl.Hv = static_cast<unsigned>(cfg_.ssm_v_heads);
            gl.S = static_cast<unsigned>(cfg_.ssm_head_dim);
            gl.slot_floats = static_cast<unsigned>(state_slot_bytes_ / sizeof(float));
            gl.conv_floats = static_cast<unsigned>(cfg_.ssm_conv_state());
            gl.eps = eps;
        }
        const unsigned n_segments = static_cast<unsigned>(batch.segments.size());
        /*
            Les prompts lisent leurs clés par tuiles de tokens consécutifs d'une même séquence (attention_tiles()), dont
            le nombre de tokens dépend de la largeur des têtes : une liste pour celles du modèle (hd), une seconde, à la
            suite dans g.tiles, pour les couches à fenêtre quand leurs têtes sont d'une autre largeur (Gemma 4).
        */
        const long hd_swa = cfg_.head_dim_swa > 0 ? cfg_.head_dim_swa : hd;
        bool tiled = !step;
        unsigned n_tiles[2] = { 0, 0 };
        for (int w = 0; w < 2 && tiled; ++w)
        {
            if (w == 1 && hd_swa == hd) break;
            const long per_tile = static_cast<long>(g.device->tile_tokens(static_cast<unsigned>(rep),
                static_cast<unsigned>(w == 0 ? hd : hd_swa)));
            if (per_tile == 0) tiled = false;
            int* tl = static_cast<int*>(g.tiles.contents()) + w * 2 * T;
            for (const auto& s : batch.segments)
                for (long j = 0; j < s.count && tiled; j += per_tile)
                {
                    tl[2 * n_tiles[w]] = static_cast<int>(s.start + j);
                    tl[2 * n_tiles[w] + 1] = static_cast<int>(std::min(per_tile, s.count - j));
                    ++n_tiles[w];
                }
        }
        unsigned uT = static_cast<unsigned>(T);
        // GULLIVER_GPU_TIMING : pour les mesures du chantier Vitesse, le temps d'écriture des commandes, de la passe
        // entière et du GPU seul, en moyenne sur 32 pas d'écriture, dans la sortie d'erreur.
        static const bool timing = std::getenv("GULLIVER_GPU_TIMING") != nullptr;
        const auto t_begin = std::chrono::steady_clock::now();
        auto c = dev.begin();
        /*
            Les couches qui lisent les clés et valeurs d'une autre (Gemma 4) ne servent que les lignes dont la passe
            rend les logits (voir run()) : à la première d'entre elles, les commandes déjà écrites s'exécutent, ces
            lignes passent en tête des tampons, que le processeur lit et écrit en place (mémoire unifiée), et la
            passe continue sur elles seules ; un morceau de prompt qui n'en demande aucune s'arrête là (fork pour
            macOS).  out_rows donne la ligne de chaque logit demandé.
        */
        const long first_shared = cfg_.n_layers - cfg_.n_kv_shared;
        const bool narrowing = cfg_.n_kv_shared > 0 && !given && !cfg_.recurrent();
        std::vector<long> out_rows = batch.logit_rows;
        bool stopped = false;
        auto narrow = [&]() {
            std::vector<long> kept = batch.logit_rows;
            std::sort(kept.begin(), kept.end());
            kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
            if (kept.empty())
            {
                stopped = true;
                return;
            }
            const long n = static_cast<long>(kept.size());
            if (n == T) return;
            c->finish();
            const long P = cfg_.d_per_layer, hs = cfg_.rope_dims_swa / 2;
            float* xs = static_cast<float*>(g.x.contents());
            float* csw = cfg_.gemma4 ? static_cast<float*>(g.cs_swa.contents()) : nullptr;
            float* pl = P > 0 ? static_cast<float*>(g.ple.contents()) : nullptr;
            for (long i = 0; i < n; ++i)
            {
                const long t = kept[static_cast<size_t>(i)];
                if (t < 0 || t >= T) throw std::runtime_error("cpu_runtime: a logit row lies outside the batch");
                if (t == i) continue;
                std::memcpy(xs + i * d, xs + t * d, sizeof(float) * d);
                std::memcpy(cs + i * 2 * half, cs + t * 2 * half, sizeof(float) * 2 * half);
                if (csw) std::memcpy(csw + i * 2 * hs, csw + t * 2 * hs, sizeof(float) * 2 * hs);
                blk[i] = blk[t];
                row[i] = row[t];
                len[i] = len[t];
                std::memcpy(table + i * table_stride, table + t * table_stride, sizeof(int) * table_stride);
                positions_[static_cast<size_t>(i)] = positions_[static_cast<size_t>(t)];
                seq_of[static_cast<size_t>(i)] = seq_of[static_cast<size_t>(t)];
            }
            /*
                Les entrées par couche des couches restantes, rangées couche par couche pour n lignes : chaque
                destination (l n + i) précède sa source (l T + t), et toute source encore à lire la suit.
            */
            if (pl)
                for (long l = first_shared; l < cfg_.n_layers; ++l)
                    for (long i = 0; i < n; ++i)
                        std::memmove(pl + (l * n + i) * P, pl + (l * T + kept[static_cast<size_t>(i)]) * P,
                            sizeof(float) * P);
            // Une tuile par ligne gardée, chacune seule de sa séquence dans la passe.
            for (int w = 0; w < 2 && tiled; ++w)
            {
                if (w == 1 && hd_swa == hd) break;
                int* tl = static_cast<int*>(g.tiles.contents()) + w * 2 * n;
                for (long i = 0; i < n; ++i)
                {
                    tl[2 * i] = static_cast<int>(i);
                    tl[2 * i + 1] = 1;
                }
                n_tiles[w] = static_cast<unsigned>(n);
            }
            for (long& r : out_rows) r = std::lower_bound(kept.begin(), kept.end(), r) - kept.begin();
            T = n;
            uT = static_cast<unsigned>(n);
            step = T <= static_cast<long>(metal_device::max_tokens);
            c = dev.begin();
        };
        auto at = [&](const metal_buffer& b, size_t offset) { return metal_view(b, offset); };
        /*
            L'entrée du prochain produit d'un prompt est déjà dans g.xh, en demi-précision (chantier Vitesse,
            2026-11-22) : écrite par silu_mul_half, ou convertie pour le produit d'avant, qui lisait la même entrée
            (les requêtes, clés et valeurs lisent toutes xn, comme la porte et la montée).  GULLIVER_NO_SHARED_XH
            convertit l'entrée à chaque produit, pour les mesures.
        */
        bool xh_ready = false;
        static const bool no_shared_xh = std::getenv("GULLIVER_NO_SHARED_XH") != nullptr;
        // Un produit de la passe : par blocs en place pour une étape, développé puis via MPS pour un prompt.
        auto product = [&](const weight_matrix& M, const metal_buffer& x, long x_stride, const metal_buffer& y, long y_stride,
                           float alpha, float beta) {
            // Une matrice en floats dans le fichier (ssm_alpha, ssm_beta) : sans développement, par mv_f32 pour
            // une étape, par MPS pour un prompt.
            if (M.type == gulliver::ggml_type::F32)
            {
                if (step && alpha == 1.0f && beta == 0.0f)
                    c->matvec_f32(at(g.weights, g.weight(M)), static_cast<unsigned>(M.rows), static_cast<unsigned>(M.cols),
                        x, uT, static_cast<unsigned>(x_stride), y, static_cast<unsigned>(y_stride));
                else
                    c->gemm(x, static_cast<unsigned>((T + 31) / 32 * 32), static_cast<unsigned>(M.cols),
                        static_cast<unsigned>(x_stride), at(g.weights, g.weight(M)), static_cast<unsigned>(M.rows),
                        static_cast<unsigned>(M.cols), y, static_cast<unsigned>(y_stride), alpha, beta);
                return;
            }
            if (step)
            {
                c->matvec(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, x, uT, x_stride, y, y_stride, alpha, beta);
                return;
            }
            /*
                Les poids développés en demi-précision, les entrées aussi, le résultat en floats (Gulliver 6.7.0) :
                Metal Performance Shaders multiplie alors sur les unités matricielles du GPU, comme le fait
                llama.cpp.  Sur le Mac mini, la lecture d'un prompt du 4B passe de 655-764 à 1 224-2 165 tokens/s
                (512 tokens) ; le contrôle du chargement compare toujours au processeur.
            */
            const metal_buffer& e = g.expanded[g.turn];
            g.turn ^= 1;
            /*
                Metal Performance Shaders calcule sur un nombre de lignes multiple de 32, que les tampons contiennent
                (grow_gpu()) : avec 24 à 31 lignes, ou 40, un produit de Qwen3.5-0.8B (attn_qkv, 6 144 x 1 024 en
                floats) rendait des NaN.  Les lignes au-delà des tokens sont calculées pour rien.
            */
            const unsigned R = static_cast<unsigned>((T + 31) / 32 * 32);
            if (g.half_prompts)
            {
                // La matrice développée gardée d'une passe à l'autre quand la limite le permet, sinon développée ici.
                const metal_buffer* w = &e;
                const size_t key = g.weight(M), bytes = static_cast<size_t>(M.rows) * static_cast<size_t>(M.cols) * 2;
                auto kept = g.half_weights.find(key);
                if (kept != g.half_weights.end())
                    w = &kept->second;
                else if (g.half_kept + bytes <= g.half_limit)
                {
                    metal_buffer b = dev.allocate(bytes);
                    if (b.valid())
                    {
                        c->dequantize_half(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, b, M.cols);
                        g.half_kept += bytes;
                        w = &g.half_weights.emplace(key, std::move(b)).first->second;
                    }
                }
                if (w == &e) c->dequantize_half(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, e, M.cols);
                if (!xh_ready)
                    c->to_half(x, R, static_cast<unsigned>(M.cols), static_cast<unsigned>(x_stride), g.xh,
                        static_cast<unsigned>(M.cols));
                xh_ready = false;
                c->gemm_half(g.xh, R, M.cols, M.cols, *w, M.rows, M.cols, y, y_stride, alpha, beta);
            }
            else
            {
                // En floats, quand le contrôle l'a demandé (gpu_ready()), comme avant Gulliver 6.7.0.
                c->dequantize(M.type, at(g.weights, g.weight(M)), M.rows, M.cols, e, M.cols);
                c->gemm(x, R, M.cols, x_stride, e, M.rows, M.cols, y, y_stride, alpha, beta);
            }
        };
        static const bool no_flush = std::getenv("GULLIVER_NO_EARLY_FLUSH") != nullptr;
        for (long li = 0; li < cfg_.n_layers; ++li)
        {
            /*
                Une étape envoie au GPU sa première couche dès qu'elle est écrite (chantier Vitesse, 2026-11-22) : le
                GPU la calcule pendant que le processeur écrit les suivantes, au lieu d'attendre la passe entière
                (0,1 ms d'écriture pour 36 couches).  GULLIVER_NO_EARLY_FLUSH garde un seul envoi, pour les mesures.
            */
            if (step && li == 1 && !no_flush) c->flush();
            if (narrowing && li == first_shared)
            {
                narrow();
                if (stopped) break;
            }
            const layer& L = layers_[static_cast<size_t>(li)];
            const gpu_state::layer_vectors& lv = g.layers[static_cast<size_t>(li)];
            const long slot = kv_slot_[static_cast<size_t>(li)];
            if (slot < 0 && cfg_.lfm2)
            {
                // Convolution courte (lfm2), puis la somme résiduelle dans x.
                gl.layer_offset = static_cast<unsigned>(linear_slot_[static_cast<size_t>(li)] * cfg_.ssm_state_floats());
                c->rms_norm(g.x, at(g.vectors, lv.attn_norm), g.xn, uT, d, d, d, eps);
                product(L.w_mix, g.xn, d, g.mix, 3 * d, 1.0f, 0.0f);
                c->short_conv(g.mix, at(g.vectors, lv.conv1d), g.states, g.segments, n_segments, g.lin_out, gl);
                product(L.w_ssm_out, g.lin_out, d, g.x, d, rs, 1.0f);
            }
            else if (slot < 0)
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
            // Les têtes de la couche (Gemma 4 en a de deux largeurs) ; une couche qui partage les clés et valeurs d'une
            // autre (Gemma 4) ne calcule que ses requêtes et lit l'emplacement de sa couche source.
            const long lhd = cfg_.layer_head_dim(li), lqd = H * lhd, lkvd = KVH * lhd;
            const bool owns = cfg_.owns_kv(li);
            const float lscale = cfg_.attention_scale > 0.0 ? static_cast<float>(cfg_.attention_scale)
                : 1.0f / std::sqrt(static_cast<float>(lhd));
            metal_device::commands::kv_layout kl;
            kl.hd = static_cast<unsigned>(lhd);
            kl.heads = static_cast<unsigned>(KVH);
            kl.positions = static_cast<unsigned>(B);
            kl.q8 = q8;
            kl.block_stride = kv_.block_offset(1);
            kl.k_slab = kv_.slab_offset(false, slot, 0);
            kl.v_slab = kv_.slab_offset(true, slot, 0);
            kl.head_stride = kv_.slab_offset(false, slot, 1) - kv_.slab_offset(false, slot, 0);
            kl.scales = kv_.scales_offset(false, slot, 0) - kv_.slab_offset(false, slot, 0);
            const bool swa = cfg_.swa_layer(li);
            kl.window = swa ? static_cast<unsigned>(cfg_.sliding_window) : 0;

            // Bloc d'attention.
            c->rms_norm(g.x, at(g.vectors, lv.attn_norm), g.xn, uT, d, d, d, eps);
            if (cfg_.attn_output_gate)
            {
                // qwen35 : la requête et sa porte, tête par tête, puis séparées.
                product(L.wq, g.xn, d, g.qg, 2 * qd, 1.0f, 0.0f);
                c->split_gate(g.qg, g.q, g.attn_gate, uT, static_cast<unsigned>(H), static_cast<unsigned>(hd));
                product(L.wk, g.xn, d, g.k, kvd, 1.0f, 0.0f);
            }
            else if (!owns)
                product(L.wq, g.xn, d, g.q, lqd, 1.0f, 0.0f);
            else if (step && L.wq.type == L.wk.type && lqd % 16 == 0)
                c->matvec2(L.wq.type, at(g.weights, g.weight(L.wq)), lqd, at(g.weights, g.weight(L.wk)), lkvd, d, g.xn, uT,
                    d, g.q, lqd, g.k, lkvd);
            else
            {
                product(L.wq, g.xn, d, g.q, lqd, 1.0f, 0.0f);
                // La même entrée que le produit d'avant : sa conversion en demi-précision sert encore (prompts).
                xh_ready = !step && g.half_prompts && L.wq.type != gulliver::ggml_type::F32
                    && L.wk.type != gulliver::ggml_type::F32 && !no_shared_xh;
                product(L.wk, g.xn, d, g.k, lkvd, 1.0f, 0.0f);
            }
            if (owns)
            {
                xh_ready = !step && g.half_prompts && L.wk.type != gulliver::ggml_type::F32
                    && L.wv.type != gulliver::ggml_type::F32 && !no_shared_xh && !cfg_.attn_output_gate
                    && !(step && L.wq.type == L.wk.type && lqd % 16 == 0);
                product(L.wv, g.xn, d, g.v, lkvd, 1.0f, 0.0f);
            }
            if (!L.bq.empty())
            {
                c->add_bias(g.q, at(g.vectors, lv.bq), qd, qd, uT);
                c->add_bias(g.k, at(g.vectors, lv.bk), kvd, kvd, uT);
                c->add_bias(g.v, at(g.vectors, lv.bv), kvd, kvd, uT);
            }
            /*
                Les normes par tête et les positions rotatives des requêtes et des clés en un seul lancement
                (qk_norm_rope, chantier Vitesse, 2026-11-22), mêmes résultats au bit près que rms_norm puis rope ;
                GULLIVER_NO_QK_FUSE garde les quatre lancements pour les mesures.
            */
            static const bool no_qk_fuse = std::getenv("GULLIVER_NO_QK_FUSE") != nullptr;
            const bool qk_fused = !L.q_norm.empty() && !no_qk_fuse;
            if (!L.q_norm.empty() && !qk_fused)
            {
                c->rms_norm(g.q, at(g.vectors, lv.q_norm), g.q, uT * H, lhd, lhd, lhd, eps);
                if (owns) c->rms_norm(g.k, at(g.vectors, lv.k_norm), g.k, uT * KVH, lhd, lhd, lhd, eps);
            }
            // Gemma 4 normalise aussi ses valeurs, sans poids.
            if (cfg_.value_norm && owns) c->rms_norm(g.v, at(g.vectors, g.ones), g.v, uT * KVH, lhd, lhd, lhd, eps);
            const bool neox = cfg_.rope == rope_style::neox;
            if (qk_fused)
            {
                const long rh = swa ? cfg_.rope_dims_swa / 2 : half;
                c->qk_norm_rope(g.q, g.k, at(g.vectors, lv.q_norm), at(g.vectors, owns ? lv.k_norm : lv.q_norm),
                    swa ? g.cs_swa : g.cs, uT, H, owns ? KVH : 0, lhd, rh, neox, lqd, lkvd, 2 * rh, eps);
            }
            else if (swa)
            {
                const long hs = cfg_.rope_dims_swa / 2;
                c->rope(g.q, g.cs_swa, uT, H, lhd, hs, neox, lqd, 2 * hs);
                if (owns) c->rope(g.k, g.cs_swa, uT, KVH, lhd, hs, neox, lkvd, 2 * hs);
            }
            else
            {
                c->rope(g.q, g.cs, uT, H, lhd, half, neox, lqd, 2 * half);
                if (owns) c->rope(g.k, g.cs, uT, KVH, lhd, half, neox, lkvd, 2 * half);
            }
            if (owns) c->kv_store(g.k, g.v, lkvd, g.region, g.blocks, g.rows, uT, kl);
            const metal_view qtemp(g.qtemp);
            c->prepare_queries(g.q, uT, H, lhd, lqd, lscale, q8, cfg_.attn_temp_scale != 0.0 ? &qtemp : nullptr);
            /*
                Un prompt dont les têtes ont 64, 128 ou 256 valeurs, avec ou sans fenêtre glissante : l'attention de MLX sur les clés
                et valeurs de chaque séquence rangées en demi-précision (attention_flat, chantier Vitesse, 2026-11-22).
                GULLIVER_NO_FLAT_ATT garde attention_tiles, pour les mesures.
            */
            static const bool no_flat = std::getenv("GULLIVER_NO_FLAT_ATT") != nullptr;
            static const bool no_window = std::getenv("GULLIVER_NO_FLAT_WINDOW") != nullptr;
            bool flat = tiled && !no_flat && (kl.window == 0 || !no_window) && lhd == hd && (lhd == 64 || lhd == 128 || lhd == 256)
                && metal_device::commands::flat_available(dev);
            if (flat)
            {
                long most = 0;
                for (const auto& s : batch.segments)
                    most = std::max(most, sequences_[static_cast<size_t>(s.sequence)]->length + s.count);
                const size_t bytes = metal_device::commands::flat_bytes(static_cast<unsigned>(most), static_cast<unsigned>(KVH),
                    static_cast<unsigned>(lhd));
                if (g.flat_bytes < bytes)
                {
                    g.flat_bytes = bytes + bytes / 2;
                    g.flat_k = need(dev, g.flat_bytes);
                    g.flat_v = need(dev, g.flat_bytes);
                    note_gpu();
                }
                // Les requêtes en demi-précision, comme les lit attention_flat (g.xh est libre entre deux produits).
                c->to_half(g.q, uT, static_cast<unsigned>(lqd), static_cast<unsigned>(lqd), g.xh, static_cast<unsigned>(lqd));
                for (const auto& s : batch.segments)
                {
                    if (s.count == 0) continue;
                    const unsigned kv_len = static_cast<unsigned>(sequences_[static_cast<size_t>(s.sequence)]->length + s.count);
                    flat = flat && c->attention_flat(at(g.xh, 2 * static_cast<size_t>(s.start * lqd)), g.region,
                        at(g.table, sizeof(int) * static_cast<size_t>(s.start) * table_stride), static_cast<unsigned>(s.count),
                        kv_len, static_cast<unsigned>(H), static_cast<unsigned>(lqd), kl, g.flat_k, g.flat_v,
                        at(g.ctx, sizeof(float) * static_cast<size_t>(s.start * lqd)));
                }
            }
            if (flat) {}
            else if (tiled)
            {
                const int w = lhd != hd ? 1 : 0;
                c->attention_tiles(g.q, g.region, g.table, g.lengths, static_cast<unsigned>(table_stride),
                    at(g.tiles, sizeof(int) * static_cast<size_t>(w * 2 * T)), n_tiles[w], static_cast<unsigned>(rep),
                    static_cast<unsigned>(lqd), g.ctx, kl);
            }
            else
                c->attention(g.q, g.region, g.table, g.lengths, static_cast<unsigned>(table_stride), uT, rep, parts, lqd,
                    g.partial, g.ctx, kl, part_len);
            if (q8) c->restore_outputs(g.ctx, uT, H, lhd, lqd);
            if (cfg_.attn_output_gate) c->sigmoid_mul(g.ctx, g.attn_gate, static_cast<unsigned>(T * lqd));
            if (cfg_.gemma4)
            {
                // La norme après l'attention, puis la somme résiduelle (Gemma 3 et 4).
                product(L.wo, g.ctx, lqd, g.out, d, 1.0f, 0.0f);
                c->rms_norm(g.out, at(g.vectors, lv.post_attn_norm), g.out, uT, d, d, d, eps);
                c->add_rows(g.x, g.out, static_cast<unsigned>(T * d), rs);
            }
            else
                product(L.wo, g.ctx, lqd, g.x, d, rs, 1.0f);
            }

            // Bloc feed-forward.
            c->rms_norm(g.x, at(g.vectors, lv.ffn_norm), g.xn, uT, d, d, d, eps);
            if (cfg_.moe_layer(li))
            {
                // Le mélange d'experts : routage, experts choisis par indirection, expert partagé, puis la somme
                // pondérée ajoutée à x.
                const long E = cfg_.n_experts, K = cfg_.n_experts_used, de = cfg_.d_ffn_expert;
                const unsigned slots = uT * static_cast<unsigned>(K);
                if (L.router.type == gulliver::ggml_type::F32)
                {
                    // Le routeur, en floats dans le fichier : par MPS pour un prompt, sans développement.
                    if (step)
                        c->matvec_f32(at(g.weights, g.weight(L.router)), static_cast<unsigned>(E), static_cast<unsigned>(d),
                            g.xn, uT, static_cast<unsigned>(d), g.moe_logits, static_cast<unsigned>(E));
                    else
                        c->gemm(g.xn, static_cast<unsigned>((T + 31) / 32 * 32), static_cast<unsigned>(d),
                            static_cast<unsigned>(d), at(g.weights, g.weight(L.router)), static_cast<unsigned>(E),
                            static_cast<unsigned>(d), g.moe_logits, static_cast<unsigned>(E));
                }
                else
                    product(L.router, g.xn, d, g.moe_logits, E, 1.0f, 0.0f);
                c->moe_route(g.moe_logits, static_cast<unsigned>(E), static_cast<unsigned>(K),
                    cfg_.expert_weights_norm || cfg_.expert_sigmoid, g.moe_ids, g.moe_w, uT, cfg_.expert_sigmoid,
                    at(g.vectors, lv.expert_bias));
                // Un prompt range d'abord les cases de chaque expert, que ses produits groupés multiplient d'un coup.
                if (!step)
                    c->moe_group(g.moe_ids, slots, static_cast<unsigned>(E), uT, g.moe_list, g.moe_counts, g.moe_work,
                        g.moe_dispatch, static_cast<unsigned>(de), static_cast<unsigned>(d));
                auto experts = [&](const std::vector<weight_matrix>& M, const metal_buffer& x, long x_stride, unsigned x_div,
                                   const metal_buffer& y, long y_stride) {
                    const weight_matrix& W = M[0];
                    const uint64_t expert_bytes = static_cast<uint64_t>(W.row_bytes) * static_cast<uint64_t>(W.rows);
                    if (step)
                        c->matvec_id(W.type, at(g.weights, g.weight(W)), W.rows, W.cols, expert_bytes, x,
                            static_cast<unsigned>(x_stride), x_div, g.moe_ids, slots, y, static_cast<unsigned>(y_stride));
                    else
                        c->matmul_id(W.type, at(g.weights, g.weight(W)), W.rows, W.cols, expert_bytes,
                            static_cast<uint64_t>(W.row_bytes), x, static_cast<unsigned>(x_stride), x_div, g.moe_list,
                            g.moe_counts, g.moe_work, uT, g.moe_dispatch, W.rows == de ? 0 : 1, y,
                            static_cast<unsigned>(y_stride));
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
                // La largeur du bloc feed-forward de la couche (Gemma 4 en a deux).
                const long df = cfg_.layer_ffn(li);
                if (step && L.w_gate.type == L.w_up.type && df % 16 == 0)
                    c->matvec2(L.w_gate.type, at(g.weights, g.weight(L.w_gate)), df, at(g.weights, g.weight(L.w_up)), df,
                        d, g.xn, uT, d, g.gate, df, g.up, df);
                else
                {
                    product(L.w_gate, g.xn, d, g.gate, df, 1.0f, 0.0f);
                    xh_ready = !step && g.half_prompts && L.w_gate.type != gulliver::ggml_type::F32
                        && L.w_up.type != gulliver::ggml_type::F32 && !no_shared_xh;
                    product(L.w_up, g.xn, d, g.up, df, 1.0f, 0.0f);
                }
                if (cfg_.gemma4)
                {
                    // GELU, puis la norme après le bloc feed-forward et la somme résiduelle (Gemma 3 et 4).
                    c->gelu_mul(g.gate, g.up, static_cast<unsigned>(T * df));
                    product(L.w_down, g.gate, df, g.out, d, 1.0f, 0.0f);
                    c->rms_norm(g.out, at(g.vectors, lv.post_ffn_norm), g.out, uT, d, d, d, eps);
                    c->add_rows(g.x, g.out, static_cast<unsigned>(T * d), rs);
                }
                else
                {
                    /*
                        Un prompt dont les produits passent en demi-précision : l'activation va droit à l'entrée du
                        produit de descente, en demi-précision, sans écrire gate en floats puis le relire (chantier
                        Vitesse, 2026-11-22).  Les lignes au-delà des tokens, que MPS calcule pour rien, comme to_half.
                    */
                    static const bool no_fused_act = std::getenv("GULLIVER_NO_SILU_HALF") != nullptr;
                    if (!step && g.half_prompts && L.w_down.type != gulliver::ggml_type::F32 && !no_fused_act)
                    {
                        c->silu_mul_half(g.gate, g.up, static_cast<unsigned>((T + 31) / 32 * 32 * df), g.xh);
                        xh_ready = true;
                    }
                    else c->silu_mul(g.gate, g.up, static_cast<unsigned>(T * df));
                    product(L.w_down, g.gate, df, g.x, d, rs, 1.0f);
                }
            }
            if (cfg_.d_per_layer > 0)
            {
                // L'entrée de la couche (Gemma 4) : GELU(inp_gate x) multiplié par elle, projeté, normalisé, ajouté,
                // les deux matrices en floats dans le fichier.
                const unsigned P = static_cast<unsigned>(cfg_.d_per_layer), ud = static_cast<unsigned>(d);
                c->matvec_f32(at(g.weights, g.weight(L.ple_gate)), P, ud, g.x, uT, ud, g.ple_g, P);
                c->gelu_mul(g.ple_g, at(g.ple, sizeof(float) * static_cast<size_t>(li) * T * P), uT * P);
                c->matvec_f32(at(g.weights, g.weight(L.ple_proj)), ud, P, g.ple_g, uT, P, g.out, ud);
                c->rms_norm(g.out, at(g.vectors, lv.ple_norm), g.out, uT, d, d, d, eps);
                c->add_rows(g.x, g.out, static_cast<unsigned>(T * d), 1.0f);
            }
            // L'échelle de sortie de la couche (Gemma 4) : x + (s - 1) x.
            if (L.out_scale != 1.0f) c->add_rows(g.x, g.x, static_cast<unsigned>(T * d), L.out_scale - 1.0f);
        }
        // Normalisation finale et logits des seules lignes demandées.
        const long R = static_cast<long>(batch.logit_rows.size());
        for (long r = 0; r < R; ++r)
            c->rms_norm(at(g.x, sizeof(float) * out_rows[static_cast<size_t>(r)] * d), at(g.vectors, g.output_norm),
                at(g.hidden, sizeof(float) * r * d), 1, d, d, d, eps);
        const float logit = cfg_.logit_scale != 1.0 ? static_cast<float>(1.0 / cfg_.logit_scale) : 1.0f;
        // Une passe « états seulement » (modèle de recherche) s'arrête aux états normalisés, sans la matrice de sortie.
        if (R > 0 && !batch.hidden_only)
            c->matvec(output_.type, at(g.weights, g.weight(output_)), cfg_.vocab, d, g.hidden, static_cast<unsigned>(R), d,
                g.logits, cfg_.vocab, logit);
        const auto t_encoded = std::chrono::steady_clock::now();
        c->finish();
        if (timing && T == 1)
        {
            static double enc = 0, wall = 0, gpu = 0;
            static int n = 0;
            const auto t_end = std::chrono::steady_clock::now();
            enc += std::chrono::duration<double, std::milli>(t_encoded - t_begin).count();
            wall += std::chrono::duration<double, std::milli>(t_end - t_begin).count();
            gpu += c->gpu_milliseconds();
            if (++n == 32)
            {
                std::fprintf(stderr, "gpu timing: step %.3f ms, of which encoding %.3f ms, GPU %.3f ms\n",
                    wall / n, enc / n, gpu / n);
                enc = wall = gpu = 0;
                n = 0;
            }
        }

        for (const auto& s : batch.segments)
            sequences_[static_cast<size_t>(s.sequence)]->length += s.count;
        if (batch.hidden_only) logits.clear();
        else
        {
            logits.resize(static_cast<size_t>(R * cfg_.vocab));
            if (R > 0) std::memcpy(logits.data(), g.logits.contents(), sizeof(float) * R * cfg_.vocab);
        }
        // Le plafond des logits (Gemma 4) : softcap * tanh(logits / softcap), comme le processeur.
        if (cfg_.final_softcap > 0.0)
        {
            const float cap = static_cast<float>(cfg_.final_softcap), inv = 1.0f / cap;
            for (float& v : logits) v = cap * std::tanh(v * inv);
        }
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
            // Les positions rotatives données (modèles multimodaux) : celles des tokens du morceau, qui diffèrent
            // de la longueur du cache après une image.
            if (!batch.positions.empty())
            {
                const long C = forward_batch::position_components;
                sub.positions.assign(batch.positions.begin() + start * C, batch.positions.begin() + (end + 1) * C);
            }
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
