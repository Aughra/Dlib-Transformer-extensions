// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// GGUF -> Dlib weight import.
//
// Repacking stage of the import pipeline: dequantizes the tensors of a Llama-family
// GGUF file and copies them, with the required transpositions and RoPE permutations,
// into a network built from decoder_transformer_config. See the abstract header for
// the weight layout and the validation procedure.

#ifndef DLIB_GGUF_WEIGHT_LOADER_H_
#define DLIB_GGUF_WEIGHT_LOADER_H_

#include <cmath>
#include "gguf_weight_loader_abstract.h"

#include <vector>
#include <string>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cstring>
#include <iostream>

#include "gguf_reader.h"
#include "gguf_dequantize.h"
#include "gguf_model_spec.h"
#include "../dnn.h"

namespace dlib
{
    struct gguf_load_options
    {
        bool rope_permute = false;
        bool swap_gate_up = false;
        bool verbose = true;
    };

    namespace gguf_load_impl
    {
        /* Transpose a GGUF [out_dim, in_dim] row-major matrix into a Dlib [in_dim, out_dim]
           row-major buffer. Optionally permute the output rows per head for RoPE. */
        inline void transpose_into(const std::vector<float>& src, long out_dim, long in_dim,
            float* dst, long num_heads, long head_dim, bool rope_permute)
        {
            /* The loop reads out_dim * in_dim values: a smaller tensor in the file would be read
               past its end. */
            if (src.size() != static_cast<size_t>(out_dim) * static_cast<size_t>(in_dim))
                throw std::runtime_error("import_gguf_weights: a tensor holds " + std::to_string(src.size())
                    + " values where " + std::to_string(out_dim) + " x " + std::to_string(in_dim) + " are expected");
            for (long o = 0; o < out_dim; ++o)
            {
                long od = o;
                if (rope_permute && head_dim > 0 && num_heads > 0)
                {
                    /* Map GGUF (split-half) head ordering to interleaved head ordering. */
                    const long h = o / head_dim;
                    const long r = o % head_dim;
                    const long half = head_dim / 2;
                    const long ri = (r < half) ? (2 * r) : (2 * (r - half) + 1);
                    od = h * head_dim + ri;
                }
                const float* srow = src.data() + static_cast<size_t>(o) * in_dim;
                for (long i = 0; i < in_dim; ++i)
                    dst[static_cast<size_t>(i) * out_dim + od] = srow[i];
            }
        }

        inline std::vector<float> fetch(gguf_reader& g, const std::string& name)
        {
            const gguf_tensor_info* t = g.find_tensor(name);
            if (!t) throw std::runtime_error("import_gguf_weights: missing tensor '" + name + "'");
            std::vector<float> out;
            gguf_read_dequantized(g, *t, out);
            return out;
        }

        /* Copy src into the layer parameter tensor. Llama weights carry no bias, so when the
           Dlib layer is larger than the source (a linear layer keeps weights followed by a
           bias), the weight part is filled and the trailing bias is zeroed. */
        inline void copy_into(tensor& dst, const std::vector<float>& src, const std::string& what)
        {
            if (src.size() > dst.size())
                throw std::runtime_error("import_gguf_weights: source larger than layer for " + what
                    + " (layer " + std::to_string(dst.size()) + " vs source " + std::to_string(src.size()) + ")");
            float* h = dst.host();
            std::memcpy(h, src.data(), src.size() * sizeof(float));
            if (src.size() < dst.size())
                std::memset(h + src.size(), 0, (dst.size() - src.size()) * sizeof(float));
        }

        /* The embedding layer keeps its table in a private member exposed by get_embeddings
           (and also by get_layer_params). This visitor targets exactly that layer (SFINAE on the
           presence of get_embeddings) and copies the table directly, no transpose, since GGUF
           token_embd is [vocab, d_model] and the Dlib table is [vocab, d_model] too. */
        struct embedding_loader
        {
            const std::vector<float>* src;
            bool* done;
            template <typename T>
            auto set(T& layer, int) -> decltype((void)layer.get_embeddings())
            {
                tensor& e = layer.get_embeddings();
                if (e.size() == src->size())
                {
                    std::memcpy(e.host(), src->data(), src->size() * sizeof(float));
                    *done = true;
                }
            }
            template <typename T> void set(T&, long) {}
            template <typename T> void operator()(T& layer) { set(layer, 0); }
        };

        /* Finds the embedding table, so that the parameter walk below can leave it out: the
           embeddings layer also exposes the table through get_layer_params, and the table is
           assigned on its own, through get_embeddings, after the walk. */
        struct embedding_finder
        {
            const tensor** found;
            template <typename T>
            auto get(T& layer, int) -> decltype((void)layer.get_embeddings())
            {
                *found = &layer.get_embeddings();
            }
            template <typename T> void get(T&, long) {}
            template <typename T> void operator()(T& layer) { get(layer, 0); }
        };

        /* Pushes the model's runtime attention settings into every attention layer that
           exposes them: the RoPE frequency base, the QK-Norm epsilon, and classical RoPE
           (YaRN disabled). SFINAE on set_rope_theta_base (only the fused attention layer
           has it) makes this a no-op for every other layer. Must run before the first
           forward pass, since the RoPE trig caches are built then and are not recomputed
           on a later base change. Disabling YaRN explicitly matters: the import pipeline
           runs a 1-token allocation forward, which would otherwise capture original_len=1
           and make any YaRN-style rescaling length-dependent and destructive. */
        struct attention_config_setter
        {
            float rope_base;
            double qk_eps;
            bool qkv_bias = false;
            /* Gemma 3: the layers alternate between two bases, one global layer every
               swa_pattern (the others use swa_base). The visit goes from the output to the
               input, so the k-th attention layer met is block n_layers - 1 - k; the counter
               lives outside, the visitor being passed by value. */
            long swa_pattern = 0;
            float swa_base = 0.f;
            long n_layers = 0;
            long* visited = nullptr;
            /* Linear position scaling of the global layers (Gemma 3 4B and up: 1 / 8); the
               sliding-window layers keep unscaled positions, as in llama.cpp. */
            float global_position_scale = 1.f;
            long swa_window = 0;     // window of the sliding-window layers
            /* Qwen 3.5: an output gate on every attention layer, and a rotation of the first
               rope_dims dimensions of each head (0: the whole head). */
            bool output_gate = false;
            long rope_dims = 0;
            /* YaRN as llama.cpp computes it (factor 0: none), and the attention temperature. */
            float yarn_freq_scale = 0, yarn_orig_ctx = 0, yarn_beta_fast = 32, yarn_beta_slow = 1, yarn_mscale = 1;
            float temperature_scale = 0;
            long temperature_length = 0;
            template <typename T>
            auto set(T& layer, int) -> decltype((void)layer.set_rope_theta_base(0.f))
            {
                float base = rope_base;
                float position_scale = global_position_scale;
                long window = 0;
                if (swa_pattern > 0 && visited)
                {
                    const long b = n_layers - 1 - (*visited)++;
                    if ((b + 1) % swa_pattern != 0)
                    {
                        base = swa_base;
                        position_scale = 1.f;
                        window = swa_window;
                    }
                }
                layer.set_rope_theta_base(base);
                layer.set_rope_position_scale(position_scale);
                layer.set_sliding_window(window);
                layer.set_qk_norm_eps(qk_eps);
                layer.set_yarn_params(0.0f, 0.0f, 0, false); // classical RoPE only
                layer.set_qkv_bias_enabled(qkv_bias);
                layer.set_output_gate_enabled(output_gate);
                layer.set_rope_rotary_dims(rope_dims);
                if (yarn_freq_scale > 0)
                    layer.set_rope_ggml_yarn(yarn_freq_scale, yarn_orig_ctx, yarn_beta_fast, yarn_beta_slow, 1.0f, yarn_mscale);
                layer.set_attention_temperature(temperature_scale, temperature_length);
            }
            template <typename T> void set(T&, long) {}
            template <typename T> void operator()(T& layer) { set(layer, 0); }
        };

        /* Pushes the model's RMSNorm epsilon into every rms_norm layer of the network
           (attention norms, FFN norms and the final norm). SFINAE on set_eps makes this
           a no-op for every other layer. Imported models carry their own value (e.g.
           1e-6 for Qwen3) which may differ from the library default. */
        struct norm_eps_setter
        {
            double eps;
            template <typename T>
            auto set(T& layer, int) -> decltype((void)layer.set_eps(0.0))
            {
                layer.set_eps(eps);
            }
            template <typename T> void set(T&, long) {}
            template <typename T> void operator()(T& layer) { set(layer, 0); }
        };

        /* Reads back the RoPE state actually held by the first attention layer: the
           theta base and three cosine values of the trig cache at position 1. Used by
           the verbose import diagnostic; a member-template visitor must live at
           namespace scope, since local classes cannot hold template members. */
        struct rope_probe_reader
        {
            float* theta; float* c0; float* c1; float* cl;
            template <typename T>
            auto rd(T& layer, int) -> decltype((void)layer.get_rope_cos_probe(0, 0))
            {
                if (*theta < 0.0f)
                {
                    *theta = layer.get_rope_theta_base();
                    *c0 = layer.get_rope_cos_probe(1, 0);
                    *c1 = layer.get_rope_cos_probe(1, 1);
                    *cl = layer.get_rope_cos_probe(1, -1);
                }
            }
            template <typename T> void rd(T&, long) {}
            template <typename T> void operator()(T& layer) { rd(layer, 0); }
        };
    }

    template <typename net_type>
    void import_gguf_weights(net_type& net, gguf_reader& g, const model_spec& spec,
        const gguf_load_options& opt = gguf_load_options())
    {
        /* QKV bias tensors present (qwen2 family)? Detected up front from the reader,
           since the config setter pushing the flag into the attention layers runs
           before the packing loop. */
        const bool has_qkv_bias = g.find_tensor("blk.0.attn_q.bias") != nullptr;
        using namespace gguf_load_impl;

        const long d = spec.d_model;
        const long kv = spec.n_kv_heads * spec.head_dim;
        const long ff = spec.d_ffn;
        const long nh = spec.n_heads;
        const long hd = spec.head_dim;
        const long qd = nh * hd;          // query/output projection width (decoupled head_dim)
        const long V = spec.vocab_size;
        const long L = spec.n_layers;

        /* Push RoPE base and QK-Norm epsilon into the attention layers before the first
           forward, so the RoPE caches are built with the right base (they are not rebuilt on a
           later change) and QK-Norm uses the model's epsilon. No-op for other layers. */
        {
            long visited = 0;
            attention_config_setter cfg{ static_cast<float>(spec.rope_freq_base), spec.rms_eps,
                has_qkv_bias, spec.gemma_blocks ? spec.sliding_window_pattern : 0,
                static_cast<float>(spec.rope_freq_base_swa), spec.n_layers, &visited,
                spec.gemma_blocks && spec.rope_scaling_type == "linear" && spec.rope_scaling_factor > 0
                    ? static_cast<float>(1.0 / spec.rope_scaling_factor) : 1.f,
                spec.gemma_blocks ? spec.sliding_window : 0,
                spec.delta_blocks, spec.delta_blocks && spec.rope_dims < spec.head_dim ? spec.rope_dims : 0,
                spec.rope_scaling_type == "yarn" && spec.rope_scaling_factor > 0
                    ? static_cast<float>(1.0 / spec.rope_scaling_factor) : 0.f,
                static_cast<float>(spec.rope_orig_ctx), static_cast<float>(spec.yarn_beta_fast),
                static_cast<float>(spec.yarn_beta_slow), static_cast<float>(spec.yarn_mscale),
                static_cast<float>(spec.attn_temperature_scale), spec.attn_temperature_length };
            visit_computational_layers(net, cfg);
            if (spec.gemma_blocks && visited != spec.n_layers)
                throw std::runtime_error("import_gguf_weights: met " + std::to_string(visited)
                    + " attention layers for " + std::to_string(spec.n_layers) + " blocks");
            norm_eps_setter neps{ spec.rms_eps };
            visit_computational_layers(net, neps);
        }

        /* Read back the RoPE state actually held by the first attention layer: the theta
           base and three cosine values of the trig cache at position 1. cos(dim 0) must
           equal cos(1) ~= 0.5403 whatever the base (that frequency is base^0 = 1); a
           printed 1.0 means the rotation is effectively the identity. cos(dim 1) equals
           cos(base^(-2/head_dim)) and therefore identifies the base numerically. */
        if (opt.verbose)
        {
            float applied = -1.0f, c0 = 0, c1 = 0, cl = 0;
            rope_probe_reader pr{ &applied, &c0, &c1, &cl };
            visit_computational_layers(net, pr);
            std::cout << "  RoPE theta applied : " << applied
                      << "  cos@pos1: dim0=" << c0 << " dim1=" << c1 << " last=" << cl << "\n";
        }

        /* Allocate every parameter tensor with a single forward pass on one token. */
        network_context::reset();
        network_context::set_kv_cache_capacity(1);
        network_context::set_inference_mode(network_context::inference_mode::prefill);
        {
            matrix<int, 0, 1> dummy(1, 1);
            dummy(0) = 0;
            net(dummy);
        }
        network_context::reset();

        /* Collect the writable parameter tensors of the parameter-bearing computational
           layers, in network visit order. The vector is captured by reference so the result
           survives even though visit_computational_layers takes the visitor by value. */
        const tensor* embedding_table = nullptr;
        visit_computational_layers(net, embedding_finder{ &embedding_table });
        std::vector<tensor*> params;
        visit_computational_layers(net, [&params, embedding_table](auto& layer) {
            tensor& p = layer.get_layer_params();
            if (p.size() > 0 && &p != embedding_table) params.push_back(&p);
        });

        /* Expected order, from the output side inward (the order visit_computational_layers
           produces): lm_head, output_norm, then for each block from the last to the first
           [ffn_down, ffn_a, ffn_b, ffn_norm, attention, attn_norm], then the embedding. The
           Gemma blocks add post_ffw_norm before ffn_down and post_attention_norm before the
           attention. */
        size_t k = 0;
        auto next = [&](const std::string& what) -> tensor& {
            if (k >= params.size())
                throw std::runtime_error("import_gguf_weights: ran out of layers at " + what);
            return *params[k++];
        };

        /* The SwiGLU is down( silu(gate(x)) * up(x) ). In visit order (output to input) the
           two inner linears appear as up first, then gate. swap_gate_up flips the mapping. */
        const std::string up_inner   = opt.swap_gate_up ? "ffn_gate" : "ffn_up";
        const std::string gate_inner = opt.swap_gate_up ? "ffn_up" : "ffn_gate";

        /* lm_head: output.weight is [vocab, d_model] in GGUF -> transpose to [d_model, vocab]. */
        {
            const std::string name = spec.tied_embeddings ? "token_embd.weight" : "output.weight";
            std::vector<float> src = fetch(g, name), dst(static_cast<size_t>(d) * V);
            transpose_into(src, V, d, dst.data(), 0, 0, false);
            copy_into(next("lm_head"), dst, "lm_head");
        }
        /* LFM2 stores its final norm as token_embd_norm (llama.cpp, LLM_TENSOR_OUTPUT_NORM_LFM2). */
        copy_into(next("output_norm"), fetch(g, spec.conv_blocks ? "token_embd_norm.weight" : "output_norm.weight"),
            "output_norm");

        for (long b = L - 1; b >= 0; --b)
        {
            const std::string p = "blk." + std::to_string(b) + ".";

            if (spec.gemma_blocks)
                copy_into(next("post_ffw_norm"), fetch(g, p + "post_ffw_norm.weight"), p + "post_ffw_norm");
            /* ffn_down: [d_model, ff] in GGUF -> [ff, d_model]. */
            {
                std::vector<float> src = fetch(g, p + "ffn_down.weight"), dst(static_cast<size_t>(ff) * d);
                transpose_into(src, d, ff, dst.data(), 0, 0, false);
                copy_into(next("ffn_down"), dst, p + "ffn_down");
            }
            /* up then gate: the visit order of the two inner SwiGLU linears. Each is
               [ff, d_model] in GGUF -> [d_model, ff]. */
            {
                std::vector<float> src = fetch(g, p + up_inner + ".weight"), dst(static_cast<size_t>(d) * ff);
                transpose_into(src, ff, d, dst.data(), 0, 0, false);
                copy_into(next("ffn_up"), dst, p + up_inner);
            }
            {
                std::vector<float> src = fetch(g, p + gate_inner + ".weight"), dst(static_cast<size_t>(d) * ff);
                transpose_into(src, ff, d, dst.data(), 0, 0, false);
                copy_into(next("ffn_gate"), dst, p + gate_inner);
            }
            /* Qwen 3.5 names post_attention_norm the norm before the feed-forward. */
            copy_into(next("ffn_norm"), fetch(g, p + (spec.delta_blocks ? "post_attention_norm.weight"
                : "ffn_norm.weight")), p + "ffn_norm");

            /* A Gated DeltaNet block of Qwen 3.5: ssm_out [VD, d]; the layer parameters (the
               convolution as the file lays it out, dt_bias, A, the norm weights); the fused input
               projection [d, C + VD + 2 Hv]: attn_qkv, attn_gate, ssm_alpha, ssm_beta side by side. */
            if (spec.delta_blocks && !((spec.attention_mask >> b) & 1ull))
            {
                const long Hv = spec.ssm_v_heads, VD = Hv * spec.ssm_head_dim;
                const long C = (2 * spec.ssm_k_heads + Hv) * spec.ssm_head_dim;
                const long W = C + VD + 2 * Hv;
                auto sized = [&](const std::string& name, size_t n) {
                    std::vector<float> v = fetch(g, name);
                    if (v.size() != n)
                        throw std::runtime_error("import_gguf_weights: " + name + " holds " + std::to_string(v.size())
                            + " values, expected " + std::to_string(n));
                    return v;
                };
                {
                    std::vector<float> src = sized(p + "ssm_out.weight", static_cast<size_t>(VD) * d),
                        dst(static_cast<size_t>(VD) * d);
                    transpose_into(src, d, VD, dst.data(), 0, 0, false);
                    copy_into(next("ssm_out"), dst, p + "ssm_out");
                }
                {
                    tensor& t = next("gated_delta_net");
                    std::vector<float> all = sized(p + "ssm_conv1d.weight", static_cast<size_t>(C * spec.conv_kernel));
                    for (const char* part : { "ssm_dt.bias", "ssm_a" })
                    {
                        const std::vector<float> v = sized(p + part, static_cast<size_t>(Hv));
                        all.insert(all.end(), v.begin(), v.end());
                    }
                    const std::vector<float> nw = sized(p + "ssm_norm.weight", static_cast<size_t>(spec.ssm_head_dim));
                    all.insert(all.end(), nw.begin(), nw.end());
                    if (all.size() != t.size())
                        throw std::runtime_error("import_gguf_weights: the Gated DeltaNet of " + p
                            + " holds " + std::to_string(t.size()) + " parameters, the file " + std::to_string(all.size()));
                    copy_into(t, all, p + "gated_delta_net");
                }
                {
                    std::vector<float> dst(static_cast<size_t>(d) * W);
                    long off = 0;
                    for (const auto& part : { std::make_pair("attn_qkv.weight", C), std::make_pair("attn_gate.weight", VD),
                                              std::make_pair("ssm_alpha.weight", Hv), std::make_pair("ssm_beta.weight", Hv) })
                    {
                        const long n = part.second;
                        const std::vector<float> src = sized(p + part.first, static_cast<size_t>(n) * d);
                        for (long o = 0; o < n; ++o)
                            for (long i = 0; i < d; ++i)
                                dst[static_cast<size_t>(i) * W + off + o] = src[static_cast<size_t>(o) * d + i];
                        off += n;
                    }
                    copy_into(next("delta_in_proj"), dst, p + "delta_in_proj");
                }
                copy_into(next("attn_norm"), fetch(g, p + "attn_norm.weight"), p + "attn_norm");
                continue;
            }

            /* A short-convolution block of LFM2: out_proj [d, d], the D x kernel weights as the
               file lays them out (one row of taps per channel), in_proj [d, 3d]. */
            if (spec.conv_blocks && !((spec.attention_mask >> b) & 1ull))
            {
                /* transpose_into reads out_dim * in_dim values: each source is checked first. */
                auto sized = [&](const std::string& name, size_t n) {
                    std::vector<float> v = fetch(g, name);
                    if (v.size() != n)
                        throw std::runtime_error("import_gguf_weights: " + name + " holds " + std::to_string(v.size())
                            + " values, expected " + std::to_string(n));
                    return v;
                };
                const size_t dd = static_cast<size_t>(d);
                {
                    std::vector<float> src = sized(p + "shortconv.out_proj.weight", dd * dd), dst(dd * dd);
                    transpose_into(src, d, d, dst.data(), 0, 0, false);
                    copy_into(next("out_proj"), dst, p + "shortconv.out_proj");
                }
                tensor& conv = next("short_conv");
                copy_into(conv, sized(p + "shortconv.conv.weight", conv.size()), p + "shortconv.conv");
                {
                    std::vector<float> src = sized(p + "shortconv.in_proj.weight", 3 * dd * dd), dst(3 * dd * dd);
                    transpose_into(src, 3 * d, d, dst.data(), 0, 0, false);
                    copy_into(next("in_proj"), dst, p + "shortconv.in_proj");
                }
                copy_into(next("attn_norm"), fetch(g, p + "attn_norm.weight"), p + "attn_norm");
                continue;
            }
            if (spec.gemma_blocks)
                copy_into(next("post_attention_norm"), fetch(g, p + "post_attention_norm.weight"),
                    p + "post_attention_norm");

            /* Attention: one packed tensor [wq | wk | wv | wo (| gamma_q | gamma_k)]. wq is
               [d, qd] and wo is [qd, d] with qd = n_heads * head_dim, which equals d for the
               usual head_dim = d / n_heads and differs only when head_dim is decoupled (Qwen3). */
            {
                const size_t nq = static_cast<size_t>(d) * qd;
                const size_t nkv = static_cast<size_t>(d) * kv;
                const size_t ng = spec.qk_norm ? static_cast<size_t>(hd) : 0;
                /* The bias slots [bq|bk|bv] close the packed blob of the families that
                   carry QKV biases; a bias-free layer holds none. */
                const size_t nb = has_qkv_bias
                    ? static_cast<size_t>(qd) + 2 * static_cast<size_t>(kv) : 0;
                /* The output gate of Qwen 3.5 closes the blob, after the bias slots. */
                const size_t ngate = spec.delta_blocks ? nq : 0;
                std::vector<float> fused(2 * nq + 2 * nkv + 2 * ng + nb + ngate, 0.0f);
                std::vector<float> sq = fetch(g, p + "attn_q.weight");
                std::vector<float> sk = fetch(g, p + "attn_k.weight");
                /* Qwen 3.5: attn_q holds, head by head, the query then its gate; the rotation is
                   NeoX over the first rope_dims dimensions of each head. The query and key rows
                   are put in the interleaved order of the layer here, over those dimensions only
                   (the gate multiplies the merged heads, which are not rotated, and keeps its order);
                   partial(r) maps a row of a head to its place. */
                const long rd = spec.delta_blocks ? spec.rope_dims : hd;
                auto partial = [&](long r) { return r >= rd ? r : (r < rd / 2 ? 2 * r : 2 * (r - rd / 2) + 1); };
                std::vector<float> sgate;
                if (spec.delta_blocks)
                {
                    if (sq.size() != 2 * nq || sk.size() != nkv)
                        throw std::runtime_error("import_gguf_weights: unexpected query or key size in " + p);
                    std::vector<float> q(nq), k(nkv);
                    sgate.resize(nq);
                    for (long h = 0; h < nh; ++h)
                        for (long r = 0; r < hd; ++r)
                        {
                            const float* qrow = sq.data() + (static_cast<size_t>(h) * 2 * hd + r) * d;
                            const float* grow = sq.data() + (static_cast<size_t>(h) * 2 * hd + hd + r) * d;
                            std::copy(qrow, qrow + d, q.data() + (static_cast<size_t>(h) * hd + partial(r)) * d);
                            std::copy(grow, grow + d, sgate.data() + (static_cast<size_t>(h) * hd + r) * d);
                        }
                    for (long h = 0; h < spec.n_kv_heads; ++h)
                        for (long r = 0; r < hd; ++r)
                        {
                            const float* krow = sk.data() + (static_cast<size_t>(h) * hd + r) * d;
                            std::copy(krow, krow + d, k.data() + (static_cast<size_t>(h) * hd + partial(r)) * d);
                        }
                    sq.swap(q);
                    sk.swap(k);
                }
                std::vector<float> sv = fetch(g, p + "attn_v.weight");
                std::vector<float> so = fetch(g, p + "attn_output.weight");
                /* qwen families keep the split-half (NeoX) Q/K layout; llama-family
                   files come pre-permuted for the interleaved kernel. */
                const bool neox_layout = !spec.delta_blocks && (opt.rope_permute
                    || spec.arch_name == "qwen2" || spec.arch_name == "qwen3" || spec.arch_name == "gemma3"
                    || spec.arch_name == "lfm2");
                transpose_into(sq, qd, d, fused.data(), nh, hd, neox_layout);          // wq (RoPE)
                transpose_into(sk, kv, d, fused.data() + nq, spec.n_kv_heads, hd, neox_layout); // wk (RoPE)
                transpose_into(sv, kv, d, fused.data() + nq + nkv, 0, 0, false);            // wv
                transpose_into(so, d, qd, fused.data() + nq + nkv + nkv, 0, 0, false);      // wo
                if (spec.qk_norm)
                {
                    /* Per-head Q/K RMSNorm gammas (head_dim each), stored after W_O to match the
                       attention [wq|wk|wv|wo|gamma_q|gamma_k] parameter packing. Two adjustments
                       are applied here, both consequences of where the normalization sits in the
                       attention block.

                       The gamma indexes the same output space as the projection rows it scales,
                       so it carries the same rotary layout permutation. Copied in the container's
                       order while the rows are permuted, each coefficient would land on the wrong
                       channel.

                       No scale is folded in: the attention layer applies the
                       1 / sqrt(head_dim) factor at the score product, downstream of the
                       normalization, so the gamma stored here is exactly the gamma the source
                       model declares. */
                    std::vector<float> gq = fetch(g, p + "attn_q_norm.weight");
                    std::vector<float> gk = fetch(g, p + "attn_k_norm.weight");
                    if (static_cast<long>(gq.size()) != hd || static_cast<long>(gk.size()) != hd)
                        throw std::runtime_error("import_gguf_weights: unexpected QK-Norm gamma size in " + p);
                    const long half = hd / 2;
                    float* dst_gq = fused.data() + 2 * nq + 2 * nkv;
                    float* dst_gk = dst_gq + ng;
                    for (long r = 0; r < hd; ++r)
                    {
                        const long ri = spec.delta_blocks ? partial(r)
                            : neox_layout ? ((r < half) ? (2 * r) : (2 * (r - half) + 1)) : r;
                        dst_gq[ri] = gq[static_cast<size_t>(r)];
                        dst_gk[ri] = gk[static_cast<size_t>(r)];
                    }
                }
                if (has_qkv_bias)
                {
                    /* QKV biases (qwen2 family). The Q and K vectors live in the same
                       output space as the permuted projection rows, so the NeoX layout
                       reordering applies to them identically; V is rotation-free. */
                    std::vector<float> bq = fetch(g, p + "attn_q.bias");
                    std::vector<float> bk = fetch(g, p + "attn_k.bias");
                    std::vector<float> bv = fetch(g, p + "attn_v.bias");
                    if (static_cast<long>(bq.size()) != qd || static_cast<long>(bk.size()) != kv
                        || static_cast<long>(bv.size()) != kv)
                        throw std::runtime_error("import_gguf_weights: unexpected QKV bias size in " + p);
                    const size_t bb = 2 * nq + 2 * nkv + 2 * ng;
                    auto put = [&](const std::vector<float>& src, size_t off)
                    {
                        const long half = hd / 2;
                        for (long o = 0; o < static_cast<long>(src.size()); ++o)
                        {
                            const long h = o / hd, r = o % hd;
                            const long ri = neox_layout ? ((r < half) ? 2 * r : 2 * (r - half) + 1) : r;
                            fused[off + static_cast<size_t>(h) * hd + ri] = src[static_cast<size_t>(o)];
                        }
                    };
                    put(bq, bb);
                    put(bk, bb + static_cast<size_t>(qd));
                    /* V is rotation-free: copied verbatim, never permuted. */
                    std::memcpy(fused.data() + bb + qd + kv, bv.data(), bv.size() * sizeof(float));
                }
                if (spec.delta_blocks)
                    transpose_into(sgate, qd, d, fused.data() + 2 * nq + 2 * nkv + 2 * ng + nb, 0, 0, false);  // wg
                copy_into(next("attention"), fused, p + "attention");
            }
            copy_into(next("attn_norm"), fetch(g, p + "attn_norm.weight"), p + "attn_norm");

            if (opt.verbose && (b % 8 == 0 || b == L - 1))
                std::cout << "  loaded block " << b << "\r" << std::flush;
        }

        if (k != params.size())
            throw std::runtime_error("import_gguf_weights: assigned " + std::to_string(k)
                + " of " + std::to_string(params.size())
                + " parameter tensors; the layer order assumption is wrong");

        /* The embedding table, left out of the walk above, is assigned separately through
           get_embeddings. */
        {
            std::vector<float> emb = fetch(g, "token_embd.weight");
            /* Gemma multiplies the embeddings by sqrt(d_model) before the first block. The
               factor goes into the table: the output projection is a tensor of its own in the
               network (filled above from the unscaled table), so it is not affected. An
               export back to GGUF divides it out. */
            if (spec.gemma_blocks)
            {
                const float scale = std::sqrt(static_cast<float>(d));
                for (float& v : emb) v *= scale;
            }
            bool done = false;
            embedding_loader el{ &emb, &done };
            visit_computational_layers(net, el);
            if (!done)
                throw std::runtime_error("import_gguf_weights: failed to set the embedding table");
        }

        if (opt.verbose)
            std::cout << "  loaded " << k << " parameter tensors plus the embedding table\n";
    }
}

#endif // DLIB_GGUF_WEIGHT_LOADER_H_
