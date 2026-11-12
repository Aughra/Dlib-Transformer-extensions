// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "model_config.h"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

#include <dlib/data_io/gguf_model_spec.h>

namespace albireo
{

    namespace
    {
        /*
            Un entier des métadonnées compris dans [lo, hi], vérifié avant toute réduction en
            long, qui fait 32 bits sous Windows.  Les bornes laissent une large marge au-delà
            des modèles publiés ; une valeur qui les dépasse vient d'un fichier endommagé, dont
            les tailles feraient déborder les produits qui dimensionnent la mémoire du runtime.
        */
        long bounded_int(const dlib::gguf_reader& g, const std::string& key, int64_t def, int64_t lo, int64_t hi)
        {
            const int64_t v = g.get_int(key, def);
            if (v < lo || v > hi)
                throw std::runtime_error("the model metadata holds an invalid " + key + " (" + std::to_string(v)
                    + ", expected " + std::to_string(lo) + " to " + std::to_string(hi) + ")");
            return static_cast<long>(v);
        }

        // Un nombre réel des métadonnées, qui doit être fini.
        double finite_double(const dlib::gguf_reader& g, const std::string& key, double def)
        {
            const double v = g.get_double(key, def);
            if (!std::isfinite(v)) throw std::runtime_error("the model metadata holds an invalid " + key);
            return v;
        }

        const int64_t max_layers = 1024;
        const int64_t max_heads = 1024;
        const int64_t max_width = 1 << 17;
        const int64_t max_ffn = 1 << 20;
        const int64_t max_head_dim = 4096;
        const int64_t max_context = int64_t(1) << 26;
        const int64_t max_vocab = int64_t(1) << 24;
        const int64_t max_experts = 4096;
    }

    model_config read_model_config(const model_file& f)
    {
        const dlib::gguf_reader& g = f.reader();
        model_config c;
        c.arch = g.get_str("general.architecture", "");
        c.name = dlib::clean_model_name(g.get_str("general.name", c.arch));
        const std::string a = c.arch;

        const bool llama_like = a == "llama" || a == "mistral" || a == "mistral3" || a == "granite";
        const bool qwen_like = a == "qwen2" || a == "qwen3" || a == "qwen3vl";
        const bool moe_arch = a == "mixtral" || a == "granitemoe" || a == "qwen3moe" || a == "qwen3vlmoe"
            || a == "qwen35moe";
        const bool phi = a == "phi3";
        // Qwen 3.5 (fork pour macOS) : couches linéaires Gated DeltaNet et attention à porte de sortie.
        const bool qwen35 = a == "qwen35" || a == "qwen35moe";
        if (!llama_like && !qwen_like && !moe_arch && !phi && !qwen35)
            throw std::runtime_error("the architecture '" + a + "' is not supported by this engine");

        if (!g.has(a + ".block_count") || !g.has(a + ".attention.head_count") || !g.has(a + ".embedding_length"))
            throw std::runtime_error("the model metadata is incomplete (missing dimensions)");
        c.n_layers = bounded_int(g, a + ".block_count", 0, 1, max_layers);
        // Les couches MTP, après les autres, ne servent qu'à proposer des brouillons : le moteur ne les exécute pas.
        c.n_nextn = bounded_int(g, a + ".nextn_predict_layers", 0, 0, c.n_layers - 1);
        c.n_layers -= c.n_nextn;
        // Chaque couche contient ses propres tenseurs.
        if (static_cast<size_t>(c.n_layers) > g.tensors().size())
            throw std::runtime_error("the model declares more layers than it holds tensors");
        c.n_heads = bounded_int(g, a + ".attention.head_count", 0, 1, max_heads);
        c.n_kv_heads = bounded_int(g, a + ".attention.head_count_kv", c.n_heads, 1, c.n_heads);
        c.d_model = bounded_int(g, a + ".embedding_length", 0, 1, max_width);
        c.d_ffn = bounded_int(g, a + ".feed_forward_length", 0, 0, max_ffn);
        const long split = c.d_model / c.n_heads;
        c.head_dim = bounded_int(g, a + ".attention.key_length", split, 0, max_head_dim);
        c.head_dim_v = bounded_int(g, a + ".attention.value_length", c.head_dim, 0, max_head_dim);
        c.ctx_train = bounded_int(g, a + ".context_length", 4096, 0, max_context);
        c.rms_eps = finite_double(g, a + ".attention.layer_norm_rms_epsilon", 1e-5);
        if (g.has("tokenizer.ggml.tokens"))
        {
            const size_t n = g.at("tokenizer.ggml.tokens").arr_str.size();
            if (n > static_cast<size_t>(max_vocab))
                throw std::runtime_error("the vocabulary of the model is too large");
            c.vocab = static_cast<long>(n);
        }
        else
        {
            c.vocab = bounded_int(g, a + ".vocab_size", 0, 0, max_vocab);
        }

        if (c.vocab <= 0) throw std::runtime_error("the model metadata is incomplete (missing dimensions)");
        if (c.n_heads % c.n_kv_heads != 0)
            throw std::runtime_error("the number of heads is not a multiple of the number of key/value heads");
        if (c.head_dim <= 0 || c.head_dim % 2 != 0 || c.head_dim_v != c.head_dim)
            throw std::runtime_error("unsupported head geometry");
        if (c.rms_eps < 0) throw std::runtime_error("the model metadata holds a negative normalization epsilon");

        // Embedding rotatif.
        // Qwen 3.5 déclare des sections de positions multimodales ; pour du texte, ses trois composantes sont
        // égales, et la rotation est celle de neox sur rope_dims dimensions.
        c.rope = qwen_like || a == "qwen3moe" || a == "qwen3vlmoe" || phi || qwen35 ? rope_style::neox
            : rope_style::norm;
        c.rope_dims = bounded_int(g, a + ".rope.dimension_count", c.head_dim, 0, max_head_dim);
        if (c.rope_dims <= 0 || c.rope_dims > c.head_dim || c.rope_dims % 2 != 0)
            throw std::runtime_error("unsupported rotary dimension count");
        c.rope_base = finite_double(g, a + ".rope.freq_base", 10000.0);
        if (c.rope_base <= 0) throw std::runtime_error("the model metadata holds an invalid rotary base");
        const std::string scaling = g.get_str(a + ".rope.scaling.type", "");
        double factor = finite_double(g, a + ".rope.scaling.factor", 0.0);
        if (scaling.empty() && g.has(a + ".rope.scale_linear"))
            factor = finite_double(g, a + ".rope.scale_linear", 0.0);
        if ((scaling == "linear" || (scaling.empty() && factor > 0.0)) && factor > 0.0)
            c.rope_freq_scale = 1.0 / factor;
        if (scaling == "yarn" && factor > 0.0)
        {
            c.yarn = true;
            c.rope_freq_scale = 1.0 / factor;
            c.yarn_orig_ctx = static_cast<double>(bounded_int(g, a + ".rope.scaling.original_context_length",
                c.ctx_train, 0, max_context));
            c.yarn_beta_fast = finite_double(g, a + ".rope.scaling.yarn_beta_fast", 32.0);
            c.yarn_beta_slow = finite_double(g, a + ".rope.scaling.yarn_beta_slow", 1.0);
            c.yarn_ext_factor = 1.0;
            const double base_mscale = 1.0 + 0.1 * std::log(factor);
            if (g.has(a + ".rope.scaling.yarn_log_multiplier"))
            {
                // La correction d'amplitude de l'implémentation de référence quand mscale et
                // mscale_all_dim sont tous deux déclarés : leur rapport.
                const double mul = finite_double(g, a + ".rope.scaling.yarn_log_multiplier", 1.0);
                c.yarn_attn_factor = base_mscale / (1.0 + 0.1 * mul * std::log(factor));
            }
            else
            {
                c.yarn_attn_factor = base_mscale;
            }
        }
        f.optional_floats("rope_freqs.weight", static_cast<size_t>(c.rope_dims / 2), c.rope_freq_factors);

        /*
            LongRoPE : les diviseurs du contexte d'origine et ceux d'un contexte plus long, que
            rope_freqs.weight remplace quand un fichier contient les deux, comme dans llama.cpp ;
            et l'amplitude de cos et sin, que llama.cpp applique à toutes les architectures.
        */
        const size_t pairs = static_cast<size_t>(c.rope_dims / 2);
        const bool short_factors = f.optional_floats("rope_factors_short.weight", pairs, c.rope_factors_short);
        const bool long_factors = f.optional_floats("rope_factors_long.weight", pairs, c.rope_factors_long);
        if (short_factors != long_factors)
            throw std::runtime_error("the model holds one of rope_factors_short and rope_factors_long without the "
                "other");
        if (!c.rope_freq_factors.empty())
        {
            c.rope_factors_short.clear();
            c.rope_factors_long.clear();
        }
        for (const std::vector<float>* set : { &c.rope_factors_short, &c.rope_factors_long })
            for (float v : *set)
                if (!(v > 0.0f) || !std::isfinite(v))
                    throw std::runtime_error("the model holds an invalid LongRoPE divisor");
        if (!c.rope_factors_long.empty())
        {
            c.rope_orig_ctx = bounded_int(g, a + ".rope.scaling.original_context_length", c.ctx_train, 1, max_context);
            select_rope_factors(c, c.ctx_train);
        }
        c.rope_attn_factor = finite_double(g, a + ".rope.scaling.attn_factor", 1.0);
        if (!(c.rope_attn_factor > 0.0))
            throw std::runtime_error("the model metadata holds an invalid rotary attention factor");

        /*
            Qwen 3.5 et 3.6 (fork pour macOS) lisent aussi des images, avec les mêmes positions rotatives
            entrelacées que Qwen3-VL (LLAMA_ROPE_TYPE_IMROPE dans llama.cpp) et sans entrées DeepStack ; un
            fichier qui ne déclare pas de sections reste un modèle de texte.
        */
        const bool qwen35_sections = (a == "qwen35" || a == "qwen35moe") && g.has(a + ".rope.dimension_sections");
        if (a == "qwen3vl" || a == "qwen3vlmoe" || qwen35_sections)
        {
            // Sections temps, ligne et colonne des paires rotatives, plus une quatrième inutilisée,
            // qui couvrent les paires d'une tête comme l'attend la disposition entrelacée de la
            // référence.
            const std::string key = a + ".rope.dimension_sections";
            if (!g.has(key) || !g.at(key).is_array() || g.at(key).arr_int.size() < 3 || g.at(key).arr_int.size() > 4)
                throw std::runtime_error("the model metadata lacks the sections of its multimodal rotary positions");
            const std::vector<int64_t>& s = g.at(key).arr_int;
            int64_t sum = 0;
            for (size_t k = 0; k < 3; ++k)
            {
                if (s[k] < 0 || s[k] > c.rope_dims / 2)
                    throw std::runtime_error("the model metadata holds an invalid " + key);
                sum += s[k];
                c.rope_sections.push_back(static_cast<int>(s[k]));
            }
            if (sum != c.rope_dims / 2 || (s.size() == 4 && s[3] != 0))
                throw std::runtime_error("the sections of the multimodal rotary positions do not cover the rotary "
                    "pairs of a head");
            c.n_deepstack = bounded_int(g, a + ".n_deepstack_layers", 0, 0, c.n_layers);
        }

        c.attn_temp_scale = finite_double(g, a + ".attention.temperature_scale", 0.0);
        if (c.attn_temp_scale != 0.0)
        {
            c.attn_temp_length = static_cast<double>(bounded_int(g, a + ".attention.temperature_length",
                c.yarn ? static_cast<int64_t>(c.yarn_orig_ctx) : 8192, 0, max_context));
            if (c.attn_temp_length <= 0) c.attn_temp_length = 8192;
        }

        c.qkv_bias = f.has("blk.0.attn_q.bias");
        c.qk_norm = f.has("blk.0.attn_q_norm.weight");
        c.tied_embeddings = !f.has("output.weight");
        c.fused_qkv = f.has("blk.0.attn_qkv.weight");
        c.fused_gate_up = phi;
        if (qwen35)
        {
            // blk.0 est une couche linéaire : son attn_qkv n'est pas celui de phi3, et les options des couches
            // d'attention se lisent sur la première d'entre elles.
            c.full_attention_interval = bounded_int(g, a + ".full_attention_interval", 4, 1, c.n_layers);
            c.ssm_conv_kernel = bounded_int(g, a + ".ssm.conv_kernel", 0, 2, 64);
            c.ssm_k_heads = bounded_int(g, a + ".ssm.group_count", 0, 1, max_heads);
            c.ssm_v_heads = bounded_int(g, a + ".ssm.time_step_rank", 0, 1, max_heads);
            c.ssm_head_dim = bounded_int(g, a + ".ssm.state_size", 0, 1, max_head_dim);
            const long inner = bounded_int(g, a + ".ssm.inner_size", 0, 1, max_width);
            if (inner != c.ssm_v_heads * c.ssm_head_dim || c.ssm_v_heads % c.ssm_k_heads != 0)
                throw std::runtime_error("unsupported geometry of the linear attention layers");
            const std::string first = "blk." + std::to_string(c.full_attention_interval - 1) + ".";
            c.qkv_bias = false;
            c.fused_qkv = false;
            c.qk_norm = f.has(first + "attn_q_norm.weight");
            c.attn_output_gate = true;
        }

        if (a == "granite" || a == "granitemoe")
        {
            c.embedding_scale = finite_double(g, a + ".embedding_scale", 1.0);
            c.residual_scale = finite_double(g, a + ".residual_scale", 1.0);
            c.attention_scale = finite_double(g, a + ".attention.scale", 0.0);
            c.logit_scale = finite_double(g, a + ".logit_scale", 1.0);
            if (c.logit_scale == 0.0) c.logit_scale = 1.0;
        }

        c.n_experts = bounded_int(g, a + ".expert_count", 0, 0, max_experts);
        c.n_experts_used = bounded_int(g, a + ".expert_used_count", 0, 0, max_experts);
        c.d_ffn_expert = bounded_int(g, a + ".expert_feed_forward_length", c.d_ffn, 0, max_ffn);
        if (c.n_experts > 0)
        {
            if (c.n_experts_used <= 0 || c.n_experts_used > c.n_experts)
                throw std::runtime_error("invalid number of experts used per token");
            if (a == "qwen35moe")
            {
                // L'expert partagé de Qwen 3.5 / 3.6 MoE, avec sa porte sigmoïde.
                c.d_ffn_shared = bounded_int(g, a + ".expert_shared_feed_forward_length", 0, 0, max_ffn);
                if (c.d_ffn_shared > 0 && !f.has("blk.0.ffn_gate_inp_shexp.weight"))
                    throw std::runtime_error("the shared expert of this model has no gate");
            }
            else if (f.has("blk.0.ffn_gate_shexp.weight") || f.has("blk.0.ffn_up_shexp.weight"))
                throw std::runtime_error("shared experts are not supported by this engine");
            if (!f.has("blk.0.ffn_gate_exps.weight"))
                throw std::runtime_error("this mixture of experts stores its experts in a legacy layout; "
                    "convert it again with a recent tool");
        }
        else if (moe_arch && a != "granitemoe")
        {
            throw std::runtime_error("the model declares a mixture of experts without experts");
        }

        c.chat_template = g.get_str("tokenizer.chat_template", "");
        c.tokenizer_pre = g.get_str("tokenizer.ggml.pre", "");
        const int64_t pooling = g.get_int(a + ".pooling_type", 0);
        if (pooling < 0 || pooling > 4) throw std::runtime_error("unknown pooling type");
        c.pooling = static_cast<int>(pooling);
        return c;
    }

    void select_rope_factors(model_config& c, long context)
    {
        if (c.rope_factors_long.empty()) return;
        c.rope_freq_factors = context > c.rope_orig_ctx ? c.rope_factors_long : c.rope_factors_short;
    }

    std::string describe(const model_config& c)
    {
        std::ostringstream o;
        o << c.name << " (" << c.arch << ", " << c.n_layers << " layers, width " << c.d_model
          << ", heads " << c.n_heads << "/" << c.n_kv_heads << " of " << c.head_dim
          << ", ffn " << c.d_ffn;
        if (c.is_moe()) o << ", " << c.n_experts << " experts top-" << c.n_experts_used;
        if (c.recurrent())
            o << ", " << c.n_layers - c.n_attention_layers() << " gated deltanet layers (heads " << c.ssm_k_heads
              << "/" << c.ssm_v_heads << " of " << c.ssm_head_dim << ", conv " << c.ssm_conv_kernel
              << ") and " << c.n_attention_layers() << " gated attention layers";
        o << ", vocabulary " << c.vocab << ", trained context " << c.ctx_train
          << ", rope " << (c.rope == rope_style::neox ? "neox" : "norm") << " base " << c.rope_base;
        if (c.yarn) o << " yarn x" << 1.0 / c.rope_freq_scale;
        else if (c.rope_freq_scale != 1.0) o << " linear x" << 1.0 / c.rope_freq_scale;
        if (c.rope_dims != c.head_dim) o << " on " << c.rope_dims << " dimensions";
        if (!c.rope_factors_long.empty())
            o << " longrope " << (c.rope_freq_factors == c.rope_factors_long ? "long" : "short") << " factors";
        else if (!c.rope_freq_factors.empty()) o << " with frequency factors";
        if (c.multimodal_rope())
            o << ", multimodal rope sections " << c.rope_sections[0] << "/" << c.rope_sections[1] << "/"
              << c.rope_sections[2] << " interleaved";
        if (c.n_deepstack > 0) o << ", deepstack inputs at " << c.n_deepstack << " layers";
        if (c.qkv_bias) o << ", qkv bias";
        if (c.qk_norm) o << ", qk norm";
        if (c.tied_embeddings) o << ", tied embeddings";
        if (c.is_embedding())
        {
            static const char* const pooling[] = { "none", "mean", "first token", "last token", "rank" };
            o << ", embedding model, pooling " << pooling[c.pooling];
        }
        o << ")";
        return o.str();
    }

}
