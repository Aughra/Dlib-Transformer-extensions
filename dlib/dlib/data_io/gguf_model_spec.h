// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// GGUF -> Dlib model specification.
//
// Reads the architecture metadata of a GGUF file into a neutral model_spec, checks
// compatibility against the available Dlib layers, and emits a Dlib model header.
// This is the detection and header-generation stage of the import pipeline; weight
// dequantization/repacking and tokenizer extraction are separate stages.

#ifndef DLIB_GGUF_MODEL_SPEC_H_
#define DLIB_GGUF_MODEL_SPEC_H_

#include "gguf_model_spec_abstract.h"

#include "gguf_reader.h"
#include "gguf_vision_spec.h"
#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <stdexcept>

namespace dlib
{
    enum class arch_family { llama, mistral, gemma, gemma2, gemma3, qwen2, qwen3, mixtral, granite_moe, lfm2, qwen35, mistral3, unknown };

    inline arch_family arch_family_from_name(const std::string& a)
    {
        if (a == "llama")   return arch_family::llama;     // Llama 1/2/3 (and Mistral in many GGUFs)
        if (a == "mistral") return arch_family::mistral;
        if (a == "gemma")   return arch_family::gemma;
        if (a == "gemma2")  return arch_family::gemma2;
        if (a == "gemma3")  return arch_family::gemma3;
        if (a == "qwen2")   return arch_family::qwen2;
        if (a == "qwen3")   return arch_family::qwen3;
        if (a == "mixtral") return arch_family::mixtral;
        if (a == "granitemoe") return arch_family::granite_moe;
        if (a == "lfm2")    return arch_family::lfm2;
        if (a == "qwen35")  return arch_family::qwen35;
        if (a == "mistral3") return arch_family::mistral3;  // Ministral 3: a Llama with YaRN and a temperature
        return arch_family::unknown;
    }

    // Neutral description of a transformer model, independent of any framework.
    struct model_spec
    {
        std::string arch_name;
        std::string model_name;                 // human-readable identity (general.name)
        arch_family family = arch_family::unknown;
        long n_layers = 0, n_heads = 0, n_kv_heads = 0;
        long d_model = 0, d_ffn = 0, head_dim = 0, vocab_size = 0;
        long ffn_num = 0, ffn_den = 1;          // d_ffn = d_model * ffn_num / ffn_den (reduced)
        double rms_eps = 1e-5, rope_freq_base = 10000.0;
        long n_experts = 0, n_experts_used = 0; // > 0 => mixture of experts

        /* Global scale factors declared by some families (granitemoe). The sentinel
           values mean "not declared": embedding and residual scales default to 1
           (identity), attention_scale 0 selects the standard 1/sqrt(head_dim), and
           logit_scale 0 leaves the logits unscaled. */
        double embedding_scale = 1.0;
        double residual_scale  = 1.0;
        double attention_scale = 0.0;
        double logit_scale     = 0.0;
        std::string rope_scaling_type;          // "", "none", "linear" or "yarn"
        double rope_scaling_factor = 0.0;       // position scaling factor when declared
        long rope_orig_ctx = 0;                 // original context length when declared
        bool tied_embeddings = false;           // no separate output.weight tensor

        /* How a sequence becomes one vector, when the container is an embedding model.

           An embedding container declares <arch>.pooling_type, following the convention
           llama.cpp established: 0 none, 1 mean, 2 the first token, 3 the last token. The
           value matters: a decoder trained with a causal mask has only one position that
           has seen the whole text, the last one, and averaging over positions that each saw
           a prefix would produce a different vector than the model was trained to produce.

           embedding_model is set when the container has no output.weight and declares a
           pooling type, which together mean the vocabulary projection was removed. */
        long pooling_type = -1;
        bool embedding_model = false;
        bool quantized = false;                 // at least one tensor is neither F32 nor F16
        bool qk_norm = false;                   // per-head RMSNorm on Q and K (Qwen3)

        /* Gemma 3: five layers out of six attend over a sliding window of sliding_window
           positions and rotate with rope_freq_base_swa; the sixth attends globally with
           rope_freq_base. The blocks carry two more norms (gemma_blocks) and the input is
           multiplied by sqrt(d_model). */
        bool gemma_blocks = false;
        long sliding_window = 0;
        long sliding_window_pattern = 0;        // one global layer every this many
        double rope_freq_base_swa = 10000.0;

        /* LFM2: the layers whose key/value head count is zero are short-convolution blocks
           of conv_kernel taps (conv_blocks); bit i of attention_mask is set for the layers
           that attend. The final norm is stored as token_embd_norm. */
        bool conv_blocks = false;
        unsigned long long attention_mask = 0;
        long conv_kernel = 0;

        /* Qwen 3.5: one attention layer every full_attention_interval, the others Gated
           DeltaNet blocks (delta_blocks) of ssm_k_heads key heads and ssm_v_heads value heads
           of ssm_head_dim values, their convolution of conv_kernel taps; the attention layers
           carry an output gate and rotate rope_dims of their head_dim dimensions. The layers
           of multi-token prediction (nextn_predict_layers) are left out of n_layers. */
        bool delta_blocks = false;
        long ssm_k_heads = 0;
        long ssm_v_heads = 0;
        long ssm_head_dim = 0;
        long rope_dims = 0;
        long nextn_layers = 0;

        /* YaRN as llama.cpp computes it (rope.scaling.type = yarn): its ramp, the extension
           factor (1) and the amplitude of the cosines and sines, from rope.scaling.attn_factor
           and yarn_log_multiplier as llama.cpp combines them. The attention temperature of
           Mistral 3 multiplies the query of position p by 1 + attn_temperature_scale *
           log(1 + floor(p / attn_temperature_length)). */
        double yarn_beta_fast = 32.0;
        double yarn_beta_slow = 1.0;
        double yarn_mscale = 1.0;
        double attn_temperature_scale = 0.0;
        long attn_temperature_length = 0;
    };

    inline long gcd_long(long a, long b)
    {
        while (b) { long t = a % b; a = b; b = t; }
        return a < 0 ? -a : a;
    }

    /* GGUF files converted from HuggingFace often carry general.name in the
       "<organization>_<repository>" form; when the repository itself starts with the
       organization name (e.g. "tinyllama_tinyllama-1.1b-chat-v1.0"), the prefix is
       redundant and only the repository part is kept. */
    inline std::string clean_model_name(const std::string& name)
    {
        auto lower = [](std::string v) {
            for (char& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return v;
        };

        /* Redundant organization prefix ("<org>_<repo>" where the repository
           itself starts with the organization name). */
        std::string out = name;
        const size_t p = out.find('_');
        if (p != std::string::npos && p > 0 && p + 1 < out.size())
        {
            const std::string org = lower(out.substr(0, p));
            const std::string rest = out.substr(p + 1);
            if (rest.size() >= org.size() && lower(rest).compare(0, org.size(), org) == 0)
                out = rest;
        }

        /* Trailing quantization or container markers ("model.Q4_K_S",
           "model-GGUF", "model_f16"...), possibly stacked: strip them from the
           end until none remains. The tag list mirrors the storage formats of
           the GGUF ecosystem; matching is case-insensitive and requires a
           separator so a genuine name ending in, say, "v2" is left alone. */
        static const char* tags[] = {
            "iq1_s", "iq1_m", "iq2_xxs", "iq2_xs", "iq2_s", "iq2_m",
            "iq3_xxs", "iq3_xs", "iq3_s", "iq3_m", "iq4_nl", "iq4_xs",
            "q2_k", "q3_k_s", "q3_k_m", "q3_k_l", "q4_k_s", "q4_k_m",
            "q5_k_s", "q5_k_m", "q6_k", "q8_k",
            "q4_0", "q4_1", "q5_0", "q5_1", "q8_0",
            "f16", "f32", "bf16", "gguf"
        };
        for (bool changed = true; changed; )
        {
            changed = false;
            const std::string low = lower(out);
            for (const char* tag : tags)
            {
                const size_t n = std::char_traits<char>::length(tag);
                if (low.size() > n + 1 && low.compare(low.size() - n, n, tag) == 0)
                {
                    const char sep = low[low.size() - n - 1];
                    if (sep == '.' || sep == '-' || sep == '_')
                    {
                        out.erase(out.size() - n - 1);
                        changed = true;
                        break;
                    }
                }
            }
        }
        return out;
    }

    inline model_spec detect_model(const gguf_reader& g)
    {
        model_spec s;
        s.arch_name = g.get_str("general.architecture", "unknown");
        s.model_name = clean_model_name(g.get_str("general.name", s.arch_name));
        s.family = arch_family_from_name(s.arch_name);

        /* Metadata keys are prefixed by the architecture.  Every number is read within the
           bounds of the published models, generously: 1,024 layers and heads, a width of
           2^17, a feed-forward width of 2^20, heads of 4,096 values, 4,096 experts. */
        const std::string p = s.arch_name;
        s.n_layers   = static_cast<long>(gguf_bounded_int(g, p + ".block_count", 0, 0, 1024));
        s.n_heads    = static_cast<long>(gguf_bounded_int(g, p + ".attention.head_count", 0, 0, 1024));
        s.n_kv_heads = static_cast<long>(gguf_bounded_int(g, p + ".attention.head_count_kv", s.n_heads, 0, 1024));
        s.d_model    = static_cast<long>(gguf_bounded_int(g, p + ".embedding_length", 0, 0, int64_t(1) << 17));
        s.d_ffn      = static_cast<long>(gguf_bounded_int(g, p + ".feed_forward_length", 0, 0, int64_t(1) << 20));
        /* Qwen3 (and some others) decouple head_dim from d_model / n_heads; prefer the explicit
           attention.key_length metadata when present, else fall back to the derived value. */
        {
            const long kl = static_cast<long>(gguf_bounded_int(g, p + ".attention.key_length", 0, 0, 4096));
            s.head_dim = kl > 0 ? kl : (s.n_heads > 0 ? s.d_model / s.n_heads : 0);
        }
        s.rms_eps    = gguf_bounded_double(g, p + ".attention.layer_norm_rms_epsilon", 1e-5, 0.0, 1.0);
        s.rope_freq_base = gguf_bounded_double(g, p + ".rope.freq_base", 10000.0, 1e-3, 1e12);
        /* RoPE position scaling. Modern GGUFs declare it under rope.scaling.*; older
           files carry the legacy rope.scale_linear factor. Reference implementations apply a linear
           factor to every position, so ignoring a declared scaling silently diverges
           from the reference; the compatibility check below turns it into a blocker
           until the corresponding support lands in the rotary layer. */
        s.rope_scaling_type = g.get_str(p + ".rope.scaling.type", "");
        s.rope_scaling_factor = gguf_bounded_double(g, p + ".rope.scaling.factor", 0.0, 0.0, 1e6);
        s.rope_orig_ctx = static_cast<long>(gguf_bounded_int(g, p + ".rope.scaling.original_context_length", 0, 0,
            int64_t(1) << 26));
        if (s.rope_scaling_type.empty() && g.has(p + ".rope.scale_linear"))
        {
            s.rope_scaling_type = "linear";
            s.rope_scaling_factor = gguf_bounded_double(g, p + ".rope.scale_linear", 0.0, 0.0, 1e6);
        }
        if (s.rope_scaling_type == "yarn" && s.rope_scaling_factor > 0.0)
        {
            s.yarn_beta_fast = gguf_bounded_double(g, p + ".rope.scaling.yarn_beta_fast", 32.0, 0.0, 1e6);
            s.yarn_beta_slow = gguf_bounded_double(g, p + ".rope.scaling.yarn_beta_slow", 1.0, 0.0, 1e6);
            const double base = 1.0 + 0.1 * std::log(s.rope_scaling_factor);
            double mscale = base;
            if (g.has(p + ".rope.scaling.yarn_log_multiplier"))
            {
                const double mul = gguf_bounded_double(g, p + ".rope.scaling.yarn_log_multiplier", 1.0, -1e6, 1e6);
                mscale = base / (1.0 + 0.1 * mul * std::log(s.rope_scaling_factor));
            }
            s.yarn_mscale = mscale * gguf_bounded_double(g, p + ".rope.scaling.attn_factor", 1.0, 1e-6, 1e6);
        }
        s.attn_temperature_scale = gguf_bounded_double(g, p + ".attention.temperature_scale", 0.0, -1e6, 1e6);
        if (s.attn_temperature_scale != 0.0)
            s.attn_temperature_length = static_cast<long>(gguf_bounded_int(g, p + ".attention.temperature_length",
                s.rope_scaling_type == "yarn" && s.rope_orig_ctx > 0 ? s.rope_orig_ctx : 8192, 1, int64_t(1) << 26));
        s.n_experts  = static_cast<long>(gguf_bounded_int(g, p + ".expert_count", 0, 0, 4096));
        s.n_experts_used = static_cast<long>(gguf_bounded_int(g, p + ".expert_used_count", 0, 0, s.n_experts));
        s.embedding_scale = gguf_bounded_double(g, p + ".embedding_scale", 1.0, -1e6, 1e6);
        s.residual_scale  = gguf_bounded_double(g, p + ".residual_scale", 1.0, -1e6, 1e6);
        s.attention_scale = gguf_bounded_double(g, p + ".attention.scale", 0.0, -1e6, 1e6);
        s.logit_scale     = gguf_bounded_double(g, p + ".logit_scale", 0.0, -1e6, 1e6);

        if (g.has("tokenizer.ggml.tokens"))
            s.vocab_size = static_cast<long>(g.at("tokenizer.ggml.tokens").arr_str.size());
        else
            s.vocab_size = static_cast<long>(gguf_bounded_int(g, p + ".vocab_size", 0, 0, int64_t(1) << 24));

        if (s.d_model > 0 && s.d_ffn > 0)
        {
            const long gg = gcd_long(s.d_ffn, s.d_model);
            s.ffn_num = s.d_ffn / gg;
            s.ffn_den = s.d_model / gg;
        }

        s.tied_embeddings = (g.find_tensor("output.weight") == nullptr);
        s.pooling_type = static_cast<long>(gguf_bounded_int(g, p + ".pooling_type", -1, -1, 16));
        s.embedding_model = s.tied_embeddings && s.pooling_type >= 0;
        s.qk_norm = (g.find_tensor("blk.0.attn_q_norm.weight") != nullptr);
        if (s.family == arch_family::gemma3)
        {
            /* The pattern and the local base are not stored by the GGUF conversion of
               Gemma 3; llama.cpp hard-codes them (one global layer in six, base 10000). */
            s.gemma_blocks = true;
            s.sliding_window = static_cast<long>(gguf_bounded_int(g, p + ".attention.sliding_window", 0, 0,
                int64_t(1) << 26));
            s.sliding_window_pattern = 6;
            s.rope_freq_base_swa = gguf_bounded_double(g, p + ".rope.freq_base_swa", 10000.0, 1e-3, 1e12);
        }
        if (s.family == arch_family::lfm2)
        {
            /* One key/value head count per layer, zero for a convolution layer; the attention
               layers share one count. */
            const std::string key = p + ".attention.head_count_kv";
            if (!g.has(key) || !g.at(key).is_array() || s.n_layers > 64
                || static_cast<long>(g.at(key).arr_int.size()) < s.n_layers)
                throw std::runtime_error("gguf: the LFM2 model lacks the key/value heads of each of its "
                    "layers, or has more than 64 layers");
            s.conv_blocks = true;
            s.n_kv_heads = 0;
            for (long i = 0; i < s.n_layers; ++i)
            {
                const int64_t v = g.at(key).arr_int[static_cast<size_t>(i)];
                if (v == 0) continue;
                if (v < 0 || v > s.n_heads || (s.n_kv_heads != 0 && v != s.n_kv_heads))
                    throw std::runtime_error("gguf: the attention layers of this LFM2 model differ in their key/value heads");
                s.n_kv_heads = static_cast<long>(v);
                s.attention_mask |= 1ull << i;
            }
            s.conv_kernel = static_cast<long>(gguf_bounded_int(g, p + ".shortconv.l_cache", 0, 0, 64));
        }
        if (s.family == arch_family::qwen35)
        {
            s.nextn_layers = static_cast<long>(gguf_bounded_int(g, p + ".nextn_predict_layers", 0, 0,
                std::max<long>(0, s.n_layers - 1)));
            s.n_layers -= s.nextn_layers;
            const long interval = static_cast<long>(gguf_bounded_int(g, p + ".full_attention_interval", 4, 1,
                std::max<long>(1, s.n_layers)));
            if (s.n_layers > 64)
                throw std::runtime_error("gguf: this Qwen 3.5 model has more than 64 layers");
            s.delta_blocks = true;
            for (long i = 0; i < s.n_layers; ++i)
                if ((i + 1) % interval == 0) s.attention_mask |= 1ull << i;
            s.conv_kernel  = static_cast<long>(gguf_bounded_int(g, p + ".ssm.conv_kernel", 0, 0, 64));
            s.ssm_k_heads  = static_cast<long>(gguf_bounded_int(g, p + ".ssm.group_count", 0, 0, 1024));
            s.ssm_v_heads  = static_cast<long>(gguf_bounded_int(g, p + ".ssm.time_step_rank", 0, 0, 1024));
            s.ssm_head_dim = static_cast<long>(gguf_bounded_int(g, p + ".ssm.state_size", 0, 0, 4096));
            const long inner = static_cast<long>(gguf_bounded_int(g, p + ".ssm.inner_size", 0, 0, int64_t(1) << 20));
            if (inner != s.ssm_v_heads * s.ssm_head_dim)
                throw std::runtime_error("gguf: the Gated DeltaNet inner size of this Qwen 3.5 model does not "
                    "match its value heads");
            s.rope_dims = static_cast<long>(gguf_bounded_int(g, p + ".rope.dimension_count", s.head_dim, 0, 4096));
        }
        if (s.conv_blocks || s.delta_blocks)
        {
            /* Layer 0 does not attend: the QK-Norm is read on the first attention layer. */
            for (long i = 0; i < s.n_layers; ++i)
                if ((s.attention_mask >> i) & 1ull)
                {
                    s.qk_norm = g.find_tensor("blk." + std::to_string(i) + ".attn_q_norm.weight") != nullptr;
                    break;
                }
        }
        for (const auto& t : g.tensors()) if (t.is_quantized()) { s.quantized = true; break; }
        return s;
    }

    /* Lower-case a model name into a valid C++ identifier: alphanumerics are kept,
       everything else collapses into single underscores. "TinyLlama-1.1B-Chat-v1.0"
       becomes "tinyllama_1_1b_chat_v1_0". */
    inline std::string sanitize_identifier(const std::string& s)
    {
        std::string id;
        for (char c : s)
        {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) id += c;
            else if (c >= 'A' && c <= 'Z') id += static_cast<char>(c - 'A' + 'a');
            else if (!id.empty() && id.back() != '_') id += '_';
        }
        while (!id.empty() && id.back() == '_') id.pop_back();
        if (id.empty()) id = "model";
        if (id[0] >= '0' && id[0] <= '9') id = "m_" + id;
        return id;
    }

    /* The canonical identifier of a model: sanitized general.name, falling back to the
       architecture name. Used to derive the generated namespace, the header guard and
       the default output file names, so successive imports do not collide. */
    inline std::string model_identifier(const model_spec& s)
    {
        return sanitize_identifier(s.model_name.empty() ? s.arch_name : s.model_name);
    }

    inline std::string describe(const model_spec& s)
    {
        std::ostringstream o;
        o << "Model name         : " << s.model_name << "\n"
          << "Architecture       : " << s.arch_name << "\n"
          << "Layers             : " << s.n_layers << "\n"
          << "Attention heads    : " << s.n_heads << "\n"
          << "KV heads           : " << s.n_kv_heads
          << " (GQA repeat " << (s.n_kv_heads ? s.n_heads / s.n_kv_heads : 0) << "x)\n"
          << "Embedding dim      : " << s.d_model << "\n"
          << "Head dim           : " << s.head_dim << "\n"
          << "FFN hidden         : " << s.d_ffn
          << " (= d_model * " << s.ffn_num << " / " << s.ffn_den << ")\n"
          << "Vocab size         : " << s.vocab_size << "\n"
          << "RMSNorm epsilon    : " << s.rms_eps << "\n"
          << "RoPE freq base     : " << s.rope_freq_base << "\n"
          << "RoPE scaling       : "
          << (s.rope_scaling_type.empty() || s.rope_scaling_type == "none"
              ? std::string("none")
              : s.rope_scaling_type + " (factor " + std::to_string(s.rope_scaling_factor)
                + (s.rope_orig_ctx ? ", original ctx " + std::to_string(s.rope_orig_ctx) : "") + ")")
          << "\n"
          << "Experts            : " << s.n_experts
          << (s.n_experts ? " (used " + std::to_string(s.n_experts_used) + ")" : "") << "\n";
        if (s.embedding_scale != 1.0 || s.residual_scale != 1.0
            || s.attention_scale != 0.0 || s.logit_scale != 0.0)
            o << "Scales             : embedding " << s.embedding_scale
              << ", residual " << s.residual_scale
              << ", attention " << s.attention_scale
              << ", logit " << s.logit_scale << "\n";
        if (s.conv_blocks || s.delta_blocks)
        {
            o << "Attention layers   :";
            for (long i = 0; i < s.n_layers; ++i) if ((s.attention_mask >> i) & 1ull) o << " " << i;
            if (s.conv_blocks) o << " (the others: short convolution of " << s.conv_kernel << " taps)\n";
            else o << " (the others: Gated DeltaNet, " << s.ssm_k_heads << "/" << s.ssm_v_heads << " heads of "
                   << s.ssm_head_dim << ", convolution " << s.conv_kernel << ")\n";
        }
        if (s.delta_blocks)
            o << "Attention          : output gate, rotation of " << s.rope_dims << " of " << s.head_dim
              << " dimensions" << (s.nextn_layers ? ", " + std::to_string(s.nextn_layers)
                  + " multi-token prediction layer(s) left out" : std::string()) << "\n";
        o << "QK-Norm            : " << (s.qk_norm ? "yes" : "no") << "\n"
          << "Tied embeddings    : " << (s.tied_embeddings ? "yes" : "no") << "\n"
          << (s.embedding_model
                 ? "Embedding model    : yes (pooling "
                       + std::string(s.pooling_type == 1 ? "mean"
                                   : s.pooling_type == 2 ? "first token"
                                   : s.pooling_type == 3 ? "last token" : "none")
                       + ", " + std::to_string(s.d_model) + " dimensions)\n"
                 : std::string())
          << "Quantized weights  : " << (s.quantized ? "yes" : "no") << "\n";
        return o.str();
    }

    struct compat_result
    {
        bool ok = true;
        std::vector<std::string> blockers;
        std::vector<std::string> notes;
    };

    inline compat_result check_compatibility(const model_spec& s, bool fused_attention_path = true)
    {
        /* fused_attention_path distinguishes the two consumers, the template network
           and the runtime engine, which passes false. Both hold the QKV biases of the
           qwen2 family, so no check depends on it at present. */
        compat_result r;
        /* An architecture outside the known families would otherwise be read as a generic
           llama: its own tensors (extra norms, convolutions, recurrent states...) left
           unread, and a network computing something else than the model, without a word. */
        if (s.family == arch_family::unknown)
            r.blockers.push_back("architecture '" + s.arch_name + "' is not one of the supported families "
                "(llama, mistral, mistral3, qwen2, qwen3, qwen35, gemma3, lfm2, mixtral, granitemoe)");
        if (s.family == arch_family::gemma || s.family == arch_family::gemma2)
            r.blockers.push_back("Gemma family needs (1+w) RMSNorm, embedding scaling and GeGLU, not available yet");
        /* Gemma 3 as converted by llama.cpp: the conversion already adds 1 to every norm
           weight, so the plain RMSNorm reads them as they are. */
        if (s.conv_blocks && (s.conv_kernel < 1 || s.n_kv_heads == 0))
            r.blockers.push_back("LFM2 metadata without a convolution width or without attention layers");
        if (s.delta_blocks && (s.conv_kernel < 1 || s.ssm_k_heads < 1 || s.ssm_head_dim < 1
                || s.ssm_v_heads % s.ssm_k_heads != 0 || s.attention_mask == 0
                || s.rope_dims < 2 || s.rope_dims > s.head_dim || s.rope_dims % 2 != 0))
            r.blockers.push_back("Qwen 3.5 metadata with an unsupported Gated DeltaNet or rotation geometry");
        /* The multimodal rotation sections of Qwen 3.5 give every section the same position
           for text, which is the plain rotation. */
        if (s.delta_blocks)
            r.notes.push_back("text only: the multimodal rotation sections reduce to the plain rotation");
        if (s.family == arch_family::gemma3 && s.sliding_window > 0)
            r.notes.push_back("sliding window of " + std::to_string(s.sliding_window)
                + " positions on five layers out of six");
        /* qwen2 QKV biases are held by the bias slots the fused attention packing
           allocates for the families that carry them; the first support tranche applies
           them in the inference forwards and freezes them during training (their
           gradients are zeroed). */
        (void)fused_attention_path;
        if (s.n_experts > 0)
        {
            /* MoE support scope: the runtime engine implements the canonical routing
               (softmax over the router logits, top-k selection, renormalization of
               the selected weights) with independent SwiGLU experts, which covers
               the mixtral and granitemoe conventions; llama/mistral cover Mixtral
               files that declare the base architecture. Families with a shared
               expert or a different gating (qwen2moe, deepseek) stay blocked, and
               the template network path has no MoE-capable feed-forward packing. */
            const bool runtime_moe_family =
                s.family == arch_family::llama || s.family == arch_family::mistral
                || s.family == arch_family::mixtral || s.family == arch_family::granite_moe;
            if (fused_attention_path)
                r.blockers.push_back("MoE models import only through the runtime engine (no expert packing in the template network)");
            else if (!runtime_moe_family)
                r.blockers.push_back("MoE routing convention of architecture '" + s.arch_name + "' is not supported");
            else if (s.n_experts_used <= 0 || s.n_experts_used > s.n_experts)
                r.blockers.push_back("invalid expert_used_count in metadata");
        }
        /* Linear scaling is applied by the import for Gemma 3 (global layers only, the
           convention of the family); elsewhere it stays a blocker. */
        if (s.rope_scaling_type == "linear" && s.rope_scaling_factor > 0.0 && s.family == arch_family::gemma3)
            r.notes.push_back("linear RoPE scaling (factor " + std::to_string(s.rope_scaling_factor)
                + ") applied to the global layers");
        else if (s.rope_scaling_type == "linear" && s.rope_scaling_factor > 0.0)
            r.blockers.push_back("declared linear RoPE scaling (factor "
                + std::to_string(s.rope_scaling_factor)
                + ") is applied to every position by the reference and is not supported yet");
        /* YaRN changes the angles of the low frequencies at every position, not only beyond
           the original context: the import applies it as llama.cpp does. */
        if (s.rope_scaling_type == "yarn" && s.rope_scaling_factor > 0.0)
        {
            if (s.rope_orig_ctx <= 0)
                r.blockers.push_back("YaRN RoPE scaling without its original context length");
            else
                r.notes.push_back("YaRN RoPE scaling (factor " + std::to_string(s.rope_scaling_factor)
                    + ", original context " + std::to_string(s.rope_orig_ctx) + ") applied as llama.cpp does");
        }
        if (s.attn_temperature_scale != 0.0)
            r.notes.push_back("attention temperature " + std::to_string(s.attn_temperature_scale) + " every "
                + std::to_string(s.attn_temperature_length) + " positions applied");
        if (s.head_dim <= 0 || (s.head_dim % 2) != 0)
            r.blockers.push_back("head_dim must be even for RoPE");
        if (s.n_kv_heads <= 0 || (s.n_heads % s.n_kv_heads) != 0)
            r.blockers.push_back("n_heads must be a multiple of n_kv_heads");
        if (s.d_model <= 0 || s.n_layers <= 0 || s.vocab_size <= 0)
            r.blockers.push_back("incomplete metadata (missing dimensions)");
        if (s.quantized)
            r.notes.push_back("Weights are quantized; the converter dequantizes legacy (Q4_0/Q4_1/Q5_0/Q5_1/Q8_0) k-quant (Q2_K..Q6_K) and IQ4_NL formats; grid-based i-quants (IQ1/IQ2/IQ3) are not supported");
        r.ok = r.blockers.empty();
        return r;
    }

    // Emit a Dlib model header. The network uses the unified GQA stack with ACT disabled
    // and the exact FFN ratio, so it depends on the FFN-ratio parameters added to
    // gqa_transformer_unified::transformer_stack. The namespace and the include guard are
    // derived from the model identity unless an explicit namespace is given, so several
    // generated headers can coexist in a program embedding a catalog of imported models.
    /* Passing a vision spec turns the emitted header multimodal: it then also declares the
       tower's geometry, the fusion policy that carries it, and hands that policy to the
       decoder configuration. A text-only header is emitted exactly as before, the policy
       defaulting to the identity alias, so its network type and its archives are unchanged.

       HAS_VISION is always declared so that one program can be compiled against either
       kind of header without the source having to know which. */
    inline void emit_header(const model_spec& s, const std::string& path,
        const std::string& ns = "", const vision_spec* vs = nullptr)
    {
        if (vs && vs->projection_dim != s.d_model)
            throw std::runtime_error("emit_header: the projector emits "
                + std::to_string(vs->projection_dim) + "-wide vectors and the decoder "
                "expects " + std::to_string(s.d_model)
                + "; the two files do not belong to the same model");

        if (vs && (s.gemma_blocks || s.conv_blocks || s.delta_blocks))
            throw std::runtime_error("emit_header: the multimodal configuration has no Gemma "
                "or short-convolution blocks yet; import the text model without its projector");

        std::ofstream out(path);
        if (!out) throw std::runtime_error("emit_header: cannot write " + path);

        const std::string id = ns.empty() ? model_identifier(s) : ns;
        std::string guard = "DLIB_IMPORTED_";
        for (char c : id)
            guard += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        guard += "_H_";

        out << "// Generated by slm_gguf_import. Source architecture: " << s.arch_name << ".\n"
            << "// Edit with care; regenerate from the source model when the spec changes.\n"
            << "#ifndef " << guard << "\n#define " << guard << "\n\n"
            << "#include <dlib/dnn/decoder_transformer_config.h>\n\n"
            << "namespace " << id << "\n{\n"
            << "    // Identity of the imported model, for display and catalog selection.\n"
            << "    static const char* const MODEL_NAME = \"" << s.model_name << "\";\n\n"
            << "    static constexpr long VOCAB_SIZE    = " << s.vocab_size << ";\n"
            << "    static constexpr long NUM_LAYERS    = " << s.n_layers << ";\n"
            << "    static constexpr long NUM_HEADS     = " << s.n_heads << ";\n"
            << "    static constexpr long NUM_KV_HEADS  = " << s.n_kv_heads << ";\n"
            << "    static constexpr long EMBEDDING_DIM = " << s.d_model << ";\n"
            << "    static constexpr long HEAD_DIM      = " << s.head_dim << ";\n"
            << "    static constexpr bool USE_QK_NORM   = " << (s.qk_norm ? "true" : "false") << ";\n"
            << "    static constexpr long FFN_NUM       = " << s.ffn_num
            << ";  // hidden = EMBEDDING_DIM * FFN_NUM / FFN_DEN = " << s.d_ffn << "\n"
            << "    static constexpr long FFN_DEN       = " << s.ffn_den << ";\n"
            << "    static constexpr bool GEMMA_BLOCKS  = " << (s.gemma_blocks ? "true" : "false") << ";\n"
            << "    // Bit i set: layer i attends; clear: a short-convolution block (LFM2).\n"
            << "    static constexpr unsigned long long ATTENTION_MASK = " << s.attention_mask << "ull;\n"
            << "    static constexpr long CONV_KERNEL   = " << s.conv_kernel << ";\n"
            << "    // Gated DeltaNet geometry and rotated dimensions (Qwen 3.5).\n"
            << "    static constexpr long SSM_K_HEADS   = " << s.ssm_k_heads << ";\n"
            << "    static constexpr long SSM_V_HEADS   = " << s.ssm_v_heads << ";\n"
            << "    static constexpr long SSM_HEAD_DIM  = " << s.ssm_head_dim << ";\n"
            << "    static constexpr long ROPE_DIMS     = " << s.rope_dims << ";\n\n"
            << "    static constexpr bool HAS_VISION    = " << (vs ? "true" : "false") << ";\n\n";

        if (vs)
        {
            out << "    // Geometry of the vision tower carried by this model.\n"
                << "    static constexpr long IMAGE_SIZE    = " << vs->image_size << ";\n"
                << "    static constexpr long PATCH_SIZE    = " << vs->patch_size << ";\n"
                << "    static constexpr long VISION_WIDTH  = " << vs->d_model << ";\n"
                << "    static constexpr long VISION_LAYERS = " << vs->n_layers << ";\n"
                << "    static constexpr long VISION_HEADS  = " << vs->n_heads << ";\n"
                << "    static constexpr long VISION_FFN    = " << vs->d_ffn << ";\n"
                << "    static constexpr long SHUFFLE       = " << vs->scale_factor << ";\n\n"
                << "    using config = dlib::multimodal_transformer_config<\n"
                << "        VOCAB_SIZE, NUM_LAYERS, NUM_HEADS, NUM_KV_HEADS, EMBEDDING_DIM, FFN_NUM, FFN_DEN,\n"
                << "        IMAGE_SIZE, PATCH_SIZE, VISION_WIDTH, VISION_LAYERS, VISION_HEADS, VISION_FFN,\n"
                << "        SHUFFLE, HEAD_DIM, USE_QK_NORM>;\n\n"
                << "    // Positions a prompt must reserve for one image.\n"
                << "    static constexpr long VISUAL_TOKENS = config::VISUAL_TOKENS;\n"
                << "    using vision_tower = config::vision_tower;\n\n";
        }
        else if (s.delta_blocks)
        {
            out << "    using config = dlib::delta_net_transformer_config<\n"
                << "        VOCAB_SIZE, NUM_LAYERS, NUM_HEADS, NUM_KV_HEADS, EMBEDDING_DIM, FFN_NUM, FFN_DEN,\n"
                << "        HEAD_DIM, USE_QK_NORM, ATTENTION_MASK, SSM_K_HEADS, SSM_V_HEADS, SSM_HEAD_DIM,\n"
                << "        CONV_KERNEL>;\n\n";
        }
        else if (s.conv_blocks)
        {
            out << "    using config = dlib::short_conv_transformer_config<\n"
                << "        VOCAB_SIZE, NUM_LAYERS, NUM_HEADS, NUM_KV_HEADS, EMBEDDING_DIM, FFN_NUM, FFN_DEN,\n"
                << "        HEAD_DIM, USE_QK_NORM, ATTENTION_MASK, CONV_KERNEL>;\n\n";
        }
        else
        {
            out << "    using config = dlib::decoder_transformer_config<\n"
                << "        VOCAB_SIZE, NUM_LAYERS, NUM_HEADS, NUM_KV_HEADS, EMBEDDING_DIM, FFN_NUM, FFN_DEN,\n"
                << "        HEAD_DIM, USE_QK_NORM"
                << (s.gemma_blocks ? ", dlib::no_modality, dlib::norm_placement::gemma" : "") << ">;\n\n";
        }

        out
            << "    // Strict model definition. network_type<false> is the inference network\n"
            << "    // (generation / chatbot); network_type<true> is the training network used\n"
            << "    // for fine-tuning. The imported weights are loaded into this exact type.\n"
            << "    template <bool is_training>\n"
            << "    using network_type = config::network_type<is_training>;\n\n"
            << "    // GGUF tensor names consumed by the weight-repacking stage:\n"
            << "    //   token_embd.weight, output_norm.weight"
            << (s.tied_embeddings ? " (output tied to token_embd)" : ", output.weight") << "\n"
            << "    //   blk.{i}.attn_norm.weight\n"
            << "    //   blk.{i}.attn_q.weight, blk.{i}.attn_k.weight, blk.{i}.attn_v.weight, blk.{i}.attn_output.weight\n"
            << (s.qk_norm ? "    //   blk.{i}.attn_q_norm.weight, blk.{i}.attn_k_norm.weight\n" : "")
            << "    //   blk.{i}.ffn_norm.weight\n"
            << "    //   blk.{i}.ffn_gate.weight, blk.{i}.ffn_up.weight, blk.{i}.ffn_down.weight\n"
            << (s.gemma_blocks ? "    //   blk.{i}.post_attention_norm.weight, blk.{i}.post_ffw_norm.weight\n"
                                 "    // The embedding table is stored multiplied by sqrt(EMBEDDING_DIM) (Gemma input scale).\n" : "")
            << (s.conv_blocks ? "    // Convolution layers: blk.{i}.shortconv.in_proj.weight, blk.{i}.shortconv.conv.weight,\n"
                                "    //   blk.{i}.shortconv.out_proj.weight in place of the attention tensors;\n"
                                "    //   the final norm is token_embd_norm.weight.\n" : "")
            << (s.delta_blocks ? "    // Gated DeltaNet layers: blk.{i}.attn_qkv, attn_gate, ssm_alpha, ssm_beta (fused into\n"
                                 "    //   one projection), ssm_conv1d, ssm_dt.bias, ssm_a, ssm_norm, ssm_out; attention\n"
                                 "    //   layers: attn_q holds the query and the output gate of each head; the norm\n"
                                 "    //   before the feed-forward is post_attention_norm.\n" : "")
            << "}\n\n"
            << "/* The first imported-model header included in a translation unit claims the\n"
            << "   generic `imported_model` alias, so single-model programs compile unchanged\n"
            << "   whatever the model-specific namespace is. Programs embedding several models\n"
            << "   include several generated headers and use the explicit namespaces. */\n"
            << "#ifndef DLIB_IMPORTED_MODEL_ALIAS_DEFINED\n"
            << "#define DLIB_IMPORTED_MODEL_ALIAS_DEFINED\n"
            << "namespace imported_model = " << id << ";\n"
            << (vs ? "/* A macro as well as a constant: code that names the tower's types cannot be\n"
                     "   compiled at all against a text-only header, and the preprocessor is what\n"
                     "   keeps it out. HAS_VISION serves the runtime tests, this serves the\n"
                     "   compiler. */\n"
                     "#define DLIB_IMPORTED_MODEL_HAS_VISION 1\n" : "")
            << "#endif\n"
            << "\n#endif // " << guard << "\n";

        out.flush();
        if (!out) throw std::runtime_error("emit_header: failed writing " + path);
    }
}

#endif // DLIB_GGUF_MODEL_SPEC_H_
