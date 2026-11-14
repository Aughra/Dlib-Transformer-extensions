// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "vision_encoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace albireo
{

    namespace
    {
        // Les lignes de patchs normalisées et multipliées à la fois : les produits d'un bloc prennent
        // quelques mégaoctets de mémoire de travail quelle que soit la taille de l'image.
        const long chunk_rows = 512;

        std::string type_name(dlib::ggml_type t)
        {
            switch (t)
            {
            case dlib::ggml_type::F32: return "F32";
            case dlib::ggml_type::F16: return "F16";
            case dlib::ggml_type::BF16: return "BF16";
            case dlib::ggml_type::Q8_0: return "Q8_0";
            case dlib::ggml_type::Q4_0: return "Q4_0";
            case dlib::ggml_type::Q4_1: return "Q4_1";
            case dlib::ggml_type::Q5_0: return "Q5_0";
            case dlib::ggml_type::Q5_1: return "Q5_1";
            case dlib::ggml_type::Q4_K: return "Q4_K";
            case dlib::ggml_type::Q5_K: return "Q5_K";
            case dlib::ggml_type::Q6_K: return "Q6_K";
            default: return "quantized";
            }
        }

        long metadata_int(const dlib::gguf_reader& g, const std::string& key, long lo, long hi)
        {
            if (!g.has(key)) throw std::runtime_error("the image encoder lacks the metadata " + key);
            const int64_t v = g.get_int(key, 0);
            if (v < lo || v > hi)
                throw std::runtime_error("the image encoder holds an invalid " + key + " (" + std::to_string(v) + ")");
            return static_cast<long>(v);
        }

        double seconds_since(std::chrono::steady_clock::time_point t)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
        }
    }

    bool vision_encoder::present(const model_file& f)
    {
        return f.has("v.patch_embd.weight") && f.reader().has("clip.vision.embedding_length");
    }

    vision_encoder::vision_encoder(std::shared_ptr<model_file> file, thread_pool& pool)
        : file_(std::move(file)), pool_(pool)
    {
        const model_file& f = *file_;
        const dlib::gguf_reader& g = f.reader();
        // Un projecteur qui lit aussi le son (Gemma 4) nomme le type de sa partie images à part.
        const std::string type = g.get_str("clip.projector_type", g.get_str("clip.vision.projector_type", ""));
        if (type == "qwen3vl_merger" || type == "qwen3vl")
            kind_ = family::qwen3vl;
        else if (type == "mlp" && g.has("clip.vision.image_grid_pinpoints"))
            kind_ = family::llava_next;
        else if (type == "pixtral")
            kind_ = family::pixtral;
        else if (type == "gemma4v")
            kind_ = family::gemma4;
        else
            throw std::runtime_error(type.empty() ? std::string("the file holds no image encoder (clip.projector_type)")
                : "the image encoder of type '" + type + "' is not supported by this engine, which reads those of "
                  "Qwen3-VL (qwen3vl_merger), of LLaVA-NeXT (mlp with pinpoints), of Pixtral (pixtral) and of Gemma 4 "
                  "(gemma4v)");
        if (!present(f)) throw std::runtime_error("the file holds no vision tensors (v.patch_embd.weight)");
        const bool llava = kind_ == family::llava_next;
        const bool pixtral = kind_ == family::pixtral;
        const bool gemma = kind_ == family::gemma4;

        hidden_ = metadata_int(g, "clip.vision.embedding_length", 16, 1 << 14);
        heads_ = metadata_int(g, "clip.vision.attention.head_count", 1, 256);
        ffn_ = metadata_int(g, "clip.vision.feed_forward_length", 1, 1 << 17);
        const long layers = metadata_int(g, "clip.vision.block_count", 1, 256);
        patch_ = metadata_int(g, "clip.vision.patch_size", 1, 64);
        merge_ = llava ? 1 : g.has("clip.vision.spatial_merge_size")
            ? metadata_int(g, "clip.vision.spatial_merge_size", 1, 8) : pixtral ? 1 : 2;
        // Gemma 4 : le noyau du regroupement final, 3 quand le fichier ne le donne pas (clip.cpp de llama.cpp).
        if (gemma)
            merge_ = g.has("clip.vision.projector.scale_factor")
                ? metadata_int(g, "clip.vision.projector.scale_factor", 1, 8) : 3;
        // La largeur de sortie de Pixtral est celle de sa dernière couche : son projection_dim en nomme une autre.
        if (kind_ == family::qwen3vl) out_dim_ = metadata_int(g, "clip.vision.projection_dim", 1, 1 << 17);
        const double eps = g.get_double("clip.vision.attention.layer_norm_epsilon", 1e-6);
        if (!(eps >= 0.0 && eps < 1.0)) throw std::runtime_error("the image encoder holds an invalid epsilon");
        eps_ = static_cast<float>(eps);
        if (hidden_ % heads_ != 0)
            throw std::runtime_error("the width of the image encoder is not a multiple of its heads");
        head_dim_ = hidden_ / heads_;
        // Les deux axes prennent chacun un quart des dimensions, tournées par moitiés.
        if (!llava && head_dim_ % 4 != 0) throw std::runtime_error("unsupported head geometry of the image encoder");
        /*
            Les blocs par lesquels passe un patch : tous, ou pour LLaVA-NeXT ceux jusqu'à la
            couche de caractéristiques du fichier (la valeur par défaut de la référence,
            l'avant-dernière, quand il n'en nomme aucune).
        */
        feature_blocks_ = layers;
        if (llava)
        {
            feature_blocks_ = layers - 1;
            if (g.has("clip.vision.feature_layer"))
            {
                const std::vector<int64_t>& fl = g.at("clip.vision.feature_layer").arr_int;
                if (fl.size() != 1 || fl[0] < 1 || fl[0] > layers)
                    throw std::runtime_error("the image encoder reads its features from several layers, which this "
                        "engine does not do");
                feature_blocks_ = static_cast<long>(fl[0]);
            }
            if (feature_blocks_ < 1) throw std::runtime_error("the image encoder has no block");
            image_size_ = metadata_int(g, "clip.vision.image_size", patch_, 4096);
            if (!g.has("clip.vision.image_grid_pinpoints"))
                throw std::runtime_error("the image encoder lacks its pinpoints");
            const std::vector<int64_t>& pins = g.at("clip.vision.image_grid_pinpoints").arr_int;
            if (pins.empty() || pins.size() % 2 != 0)
                throw std::runtime_error("the pinpoints of the image encoder are not pairs");
            // Des paires de hauteur et largeur, telles que la configuration de la référence les liste.
            for (size_t i = 0; i < pins.size(); i += 2)
            {
                const long h = static_cast<long>(pins[i]), w = static_cast<long>(pins[i + 1]);
                if (h < image_size_ || w < image_size_ || h % image_size_ != 0 || w % image_size_ != 0 || h > 16384
                    || w > 16384)
                    throw std::runtime_error("a pinpoint of the image encoder is not a grid of whole views");
                pinpoints_.push_back(std::make_pair(w, h));
            }
        }
        // Le plus long côté d'une image lue par Pixtral.
        if (pixtral) image_size_ = metadata_int(g, "clip.vision.image_size", factor(), 16384);
        for (int c = 0; c < 3; ++c)
        {
            if (g.has("clip.vision.image_mean") && g.at("clip.vision.image_mean").arr_float.size() == 3)
                mean_[c] = static_cast<float>(g.at("clip.vision.image_mean").arr_float[static_cast<size_t>(c)]);
            if (g.has("clip.vision.image_std") && g.at("clip.vision.image_std").arr_float.size() == 3)
                std_[c] = static_cast<float>(g.at("clip.vision.image_std").arr_float[static_cast<size_t>(c)]);
            if (!(std_[c] > 0.0f) || !std::isfinite(mean_[c]))
                throw std::runtime_error("the image encoder holds an invalid normalization of the pixels");
        }

        // L'embedding des patchs : les deux noyaux temporels de la convolution, additionnés, en une seule
        // matrice de hidden lignes de 3 x patch x patch valeurs (canal, ligne, colonne).
        const long K = 3 * patch_ * patch_;
        patch_w_.assign(static_cast<size_t>(hidden_ * K), 0.0f);
        std::vector<float> row(static_cast<size_t>(K));
        for (const char* name : { "v.patch_embd.weight", "v.patch_embd.weight.1" })
        {
            const tensor_view* t = f.find(name);
            if (!t) continue;
            if (t->elements() != static_cast<uint64_t>(hidden_ * K) || t->dims.empty()
                || static_cast<long>(t->dims[0]) != patch_ || (t->type != dlib::ggml_type::F32
                && t->type != dlib::ggml_type::F16 && t->type != dlib::ggml_type::BF16))
                throw std::runtime_error(std::string("tensor ") + name + " does not hold the patch embedding");
            const size_t row_bytes = qk_row_bytes(t->type, K);
            for (long o = 0; o < hidden_; ++o)
            {
                qk_dequantize_row(t->type, t->data + static_cast<size_t>(o) * row_bytes, row.data(), K);
                float* dst = patch_w_.data() + o * K;
                for (long k = 0; k < K; ++k) dst[k] += row[static_cast<size_t>(k)];
            }
        }
        patch_matrix_.type = dlib::ggml_type::F32;
        patch_matrix_.data = reinterpret_cast<const uint8_t*>(patch_w_.data());
        patch_matrix_.rows = hidden_;
        patch_matrix_.cols = K;
        patch_matrix_.row_bytes = static_cast<size_t>(K) * sizeof(float);
        // Pixtral convolue sans biais.
        if (pixtral || gemma) f.optional_floats("v.patch_embd.bias", static_cast<size_t>(hidden_), patch_b_);
        else patch_b_ = f.floats("v.patch_embd.bias", static_cast<size_t>(hidden_));

        // Les positions apprises, une grille carrée (Pixtral n'en a pas : ses têtes tournent selon les
        // seules lignes et colonnes).
        if (gemma)
        {
            // Deux tables bout à bout, [largeur, cases, 2] : celle des colonnes, puis celle des lignes.
            const tensor_view* pos = f.find("v.position_embd.weight");
            if (!pos || pos->dims.size() != 3 || static_cast<long>(pos->dims[0]) != hidden_ || pos->dims[2] != 2
                || pos->dims[1] < 1 || pos->dims[1] > (1 << 20))
                throw std::runtime_error("tensor v.position_embd.weight does not hold the two position tables of Gemma 4");
            pos_cells_ = static_cast<long>(pos->dims[1]);
            pos_table_ = f.matrix("v.position_embd.weight", 2 * pos_cells_, hidden_);
            if (pos_table_.type != dlib::ggml_type::F32)
            {
                pos_.resize(static_cast<size_t>(2 * pos_cells_ * hidden_));
                for (long c = 0; c < 2 * pos_cells_; ++c)
                    qk_dequantize_row(pos_table_.type, pos_table_.data + static_cast<size_t>(c) * pos_table_.row_bytes,
                        pos_.data() + c * hidden_, hidden_);
                pos_table_.type = dlib::ggml_type::F32;
                pos_table_.data = reinterpret_cast<const uint8_t*>(pos_.data());
                pos_table_.row_bytes = static_cast<size_t>(hidden_) * sizeof(float);
            }
        }
        else if (!pixtral)
        {
            const tensor_view* pos = f.find("v.position_embd.weight");
            if (!pos || pos->dims.size() < 2) throw std::runtime_error("missing tensor v.position_embd.weight");
            const long cells = static_cast<long>(pos->elements() / static_cast<uint64_t>(hidden_));
            side_ = static_cast<long>(std::lround(std::sqrt(static_cast<double>(cells))));
            if (side_ < 1 || side_ * side_ != cells)
                throw std::runtime_error("the position table is not a square grid");
            if (llava && side_ != image_size_ / patch_)
                throw std::runtime_error("the position table of the image encoder does not cover its views");
            const weight_matrix pm = f.matrix("v.position_embd.weight", cells, hidden_);
            pos_.resize(static_cast<size_t>(cells * hidden_));
            for (long c = 0; c < cells; ++c)
                qk_dequantize_row(pm.type, pm.data + static_cast<size_t>(c) * pm.row_bytes, pos_.data() + c * hidden_,
                    hidden_);
        }

        if (kind_ == family::qwen3vl)
        {
            // Fréquences des paires rotatives : chaque axe en prend head_dim / 4.
            const long quarter = head_dim_ / 4;
            for (long i = 0; i < quarter; ++i)
                inv_freq_.push_back(static_cast<float>(1.0 / std::pow(10000.0, 2.0 * static_cast<double>(i)
                    / static_cast<double>(head_dim_ / 2))));
        }
        else if (gemma)
        {
            // Chaque moitié de tête tourne comme une tête de head_dim / 2 dimensions de llama.cpp (néox), à la
            // base 100 : la paire i à 100^(-2i / (head_dim / 2)).
            const long quarter = head_dim_ / 4;
            for (long i = 0; i < quarter; ++i)
                inv_freq_.push_back(static_cast<float>(std::pow(100.0, -2.0 * static_cast<double>(i)
                    / static_cast<double>(head_dim_ / 2))));
        }
        else if (pixtral)
        {
            /*
                La paire p d'une tête tourne à theta^(-e / head_dim) : les paires de la première moitié,
                tournées selon la ligne du patch, prennent les exposants pairs e = 4p, celles de la
                seconde moitié, tournées selon sa colonne, les impairs e = 4(p - head_dim / 4) + 2.
            */
            const double theta = g.get_double("clip.rope.freq_base", 10000.0);
            if (!(theta > 1.0 && theta < 1e12))
                throw std::runtime_error("the image encoder holds an invalid rotary base");
            const long half = head_dim_ / 2, quarter = head_dim_ / 4;
            for (long p = 0; p < half; ++p)
            {
                const double e = p < quarter ? 4.0 * static_cast<double>(p)
                    : 4.0 * static_cast<double>(p - quarter) + 2.0;
                inv_freq_.push_back(static_cast<float>(std::pow(theta, -e / static_cast<double>(head_dim_))));
            }
        }

        std::vector<bool> deep(static_cast<size_t>(layers), false);
        if (g.has("clip.vision.is_deepstack_layers"))
        {
            const std::vector<int64_t>& flags = g.at("clip.vision.is_deepstack_layers").arr_int;
            if (flags.size() != static_cast<size_t>(layers))
                throw std::runtime_error("clip.vision.is_deepstack_layers does not name every block");
            for (size_t i = 0; i < flags.size(); ++i) deep[i] = flags[i] != 0;
        }

        std::map<std::string, uint64_t> by_type;
        auto count = [&](const weight_matrix& m) {
            by_type[type_name(m.type)] += m.row_bytes * static_cast<uint64_t>(m.rows);
        };
        const long D = hidden_, M4 = hidden_ * merge_ * merge_;
        for (long i = 0; i < feature_blocks_; ++i)
        {
            const std::string p = "v.blk." + std::to_string(i) + ".";
            block b;
            if (gemma)
            {
                // Des normes par moyenne quadratique sans biais, une porte GELU, et des bornes à chaque produit.
                b.fused = false;
                b.gated = true;
                b.ln1_w = f.floats(p + "ln1.weight", static_cast<size_t>(D));
                b.ln2_w = f.floats(p + "ln2.weight", static_cast<size_t>(D));
                b.attn_post = f.floats(p + "attn_post_norm.weight", static_cast<size_t>(D));
                b.ffn_post = f.floats(p + "ffn_post_norm.weight", static_cast<size_t>(D));
                b.q_norm = f.floats(p + "attn_q_norm.weight", static_cast<size_t>(head_dim_));
                b.k_norm = f.floats(p + "attn_k_norm.weight", static_cast<size_t>(head_dim_));
                b.q = readable(f.matrix(p + "attn_q.weight", D, D));
                b.k = readable(f.matrix(p + "attn_k.weight", D, D));
                b.v = readable(f.matrix(p + "attn_v.weight", D, D));
                b.out = readable(f.matrix(p + "attn_out.weight", D, D));
                b.gate = readable(f.matrix(p + "ffn_gate.weight", ffn_, D));
                b.up = readable(f.matrix(p + "ffn_up.weight", ffn_, D));
                b.down = readable(f.matrix(p + "ffn_down.weight", D, ffn_));
                for (const char* bias : { "attn_q.bias", "attn_k.bias", "attn_v.bias", "attn_out.bias", "ffn_gate.bias",
                    "ffn_up.bias", "ffn_down.bias" })
                    if (f.has(p + bias))
                        throw std::runtime_error("a block of the Gemma 4 image encoder has a bias, which this engine "
                            "does not read");
                b.cq = read_clamp(f, p + "attn_q");
                b.ck = read_clamp(f, p + "attn_k");
                b.cv = read_clamp(f, p + "attn_v");
                b.cout = read_clamp(f, p + "attn_out");
                b.cgate = read_clamp(f, p + "ffn_gate");
                b.cup = read_clamp(f, p + "ffn_up");
                b.cdown = read_clamp(f, p + "ffn_down");
                for (const weight_matrix* m : { &b.q, &b.k, &b.v, &b.out, &b.gate, &b.up, &b.down }) count(*m);
                blocks_.push_back(std::move(b));
                continue;
            }
            // Pixtral normalise par la moyenne quadratique : ses normes n'ont pas de biais.
            b.ln1_w = f.floats(p + "ln1.weight", static_cast<size_t>(D));
            if (!pixtral) b.ln1_b = f.floats(p + "ln1.bias", static_cast<size_t>(D));
            b.ln2_w = f.floats(p + "ln2.weight", static_cast<size_t>(D));
            if (!pixtral) b.ln2_b = f.floats(p + "ln2.bias", static_cast<size_t>(D));
            if (f.has(p + "attn_qkv.weight"))
            {
                b.qkv = readable(f.matrix(p + "attn_qkv.weight", 3 * D, D));
                b.qkv_b = f.floats(p + "attn_qkv.bias", static_cast<size_t>(3 * D));
                count(b.qkv);
            }
            else
            {
                // Les requêtes, clés et valeurs séparées (SigLIP, Pixtral), leurs biais bout à bout.
                b.fused = false;
                b.q = readable(f.matrix(p + "attn_q.weight", D, D));
                b.k = readable(f.matrix(p + "attn_k.weight", D, D));
                b.v = readable(f.matrix(p + "attn_v.weight", D, D));
                bool any = false;
                std::vector<float> biases(static_cast<size_t>(3 * D), 0.0f);
                long at = 0;
                for (const char* part : { "attn_q.bias", "attn_k.bias", "attn_v.bias" })
                {
                    std::vector<float> bias;
                    if (pixtral) any = f.optional_floats(p + part, static_cast<size_t>(D), bias) || any;
                    else
                    {
                        bias = f.floats(p + part, static_cast<size_t>(D));
                        any = true;
                    }
                    if (!bias.empty()) std::copy(bias.begin(), bias.end(), biases.begin() + at);
                    at += D;
                }
                if (any) b.qkv_b.swap(biases);
                count(b.q);
                count(b.k);
                count(b.v);
            }
            b.out = readable(f.matrix(p + "attn_out.weight", D, D));
            if (pixtral) f.optional_floats(p + "attn_out.bias", static_cast<size_t>(D), b.out_b);
            else b.out_b = f.floats(p + "attn_out.bias", static_cast<size_t>(D));
            if (pixtral)
            {
                // Une porte SiLU : silu(gate x) * (up x), puis la couche down.
                b.gated = true;
                b.gate = readable(f.matrix(p + "ffn_gate.weight", ffn_, D));
                b.up = readable(f.matrix(p + "ffn_up.weight", ffn_, D));
                b.down = readable(f.matrix(p + "ffn_down.weight", D, ffn_));
                f.optional_floats(p + "ffn_up.bias", static_cast<size_t>(ffn_), b.up_b);
                f.optional_floats(p + "ffn_down.bias", static_cast<size_t>(D), b.down_b);
                if (f.has(p + "ffn_gate.bias"))
                    throw std::runtime_error("the gate of the image encoder has a bias, which this engine does not "
                        "read");
                count(b.gate);
            }
            else
            {
                // Les fichiers des premiers outils de conversion nomment les deux couches de la partie
                // feed-forward à l'envers : la couche qui lit la largeur de l'encodeur vient en premier.
                const tensor_view* up = f.find(p + "ffn_up.weight");
                const bool swapped = up && !up->dims.empty() && static_cast<long>(up->dims[0]) != D;
                const std::string first = swapped ? "ffn_down" : "ffn_up", second = swapped ? "ffn_up" : "ffn_down";
                b.up = readable(f.matrix(p + first + ".weight", ffn_, D));
                b.up_b = f.floats(p + first + ".bias", static_cast<size_t>(ffn_));
                b.down = readable(f.matrix(p + second + ".weight", D, ffn_));
                b.down_b = f.floats(p + second + ".bias", static_cast<size_t>(D));
            }
            count(b.out);
            count(b.up);
            count(b.down);
            blocks_.push_back(std::move(b));
            if (deep[static_cast<size_t>(i)])
            {
                const std::string q = "v.deepstack." + std::to_string(i) + ".";
                merger m;
                m.after = i;
                m.norm_w = f.floats(q + "norm.weight", static_cast<size_t>(M4));
                m.norm_b = f.floats(q + "norm.bias", static_cast<size_t>(M4));
                m.fc1 = readable(f.matrix(q + "fc1.weight", M4, M4));
                m.fc1_b = f.floats(q + "fc1.bias", static_cast<size_t>(M4));
                m.fc2 = readable(f.matrix(q + "fc2.weight", out_dim_, M4));
                m.fc2_b = f.floats(q + "fc2.bias", static_cast<size_t>(out_dim_));
                count(m.fc1);
                count(m.fc2);
                deepstack_.push_back(std::move(m));
            }
        }
        if (llava)
        {
            // Le projecteur : deux couches linéaires, la largeur de leur milieu et de leur sortie
            // lue dans leurs tenseurs.
            const tensor_view* fc1 = f.find("mm.0.weight");
            const tensor_view* fc2 = f.find("mm.2.weight");
            if (!fc1 || !fc2 || fc1->dims.size() < 2 || fc2->dims.size() < 2)
                throw std::runtime_error("the image encoder lacks the two layers of its projector (mm.0, mm.2)");
            const long middle = static_cast<long>(fc1->dims[1]);
            out_dim_ = static_cast<long>(fc2->dims[1]);
            if (middle < 1 || middle > (1 << 17) || out_dim_ < 1 || out_dim_ > (1 << 17))
                throw std::runtime_error("the projector of the image encoder has an invalid width");
            final_.fc1 = readable(f.matrix("mm.0.weight", middle, D));
            final_.fc1_b = f.floats("mm.0.bias", static_cast<size_t>(middle));
            final_.fc2 = readable(f.matrix("mm.2.weight", out_dim_, middle));
            final_.fc2_b = f.floats("mm.2.bias", static_cast<size_t>(out_dim_));
            if (f.has("model.image_newline")) newline_ = f.floats("model.image_newline", static_cast<size_t>(out_dim_));
            count(final_.fc1);
            count(final_.fc2);
        }
        else if (gemma)
        {
            // La projection des patchs regroupés vers le modèle de langage, sans biais.
            const tensor_view* proj = f.find("mm.input_projection.weight");
            if (!proj || proj->dims.size() < 2 || static_cast<long>(proj->dims[0]) != D)
                throw std::runtime_error("the Gemma 4 image encoder lacks its projection (mm.input_projection)");
            out_dim_ = static_cast<long>(proj->dims[1]);
            if (out_dim_ < 1 || out_dim_ > (1 << 17))
                throw std::runtime_error("the projector of the image encoder has an invalid width");
            input_proj_ = readable(f.matrix("mm.input_projection.weight", out_dim_, D));
            count(input_proj_);
            if (f.has("v.std_bias") || f.has("v.std_scale"))
                throw std::runtime_error("the Gemma 4 image encoder standardizes its output, which this engine does "
                    "not do");
        }
        else if (pixtral)
        {
            // La norme après l'embedding des patchs, puis le projecteur des tokens fusionnés.
            f.optional_floats("v.pre_ln.weight", static_cast<size_t>(D), pre_ln_w_);
            const tensor_view* l1 = f.find("mm.1.weight");
            const tensor_view* l2 = f.find("mm.2.weight");
            if (!l1 || !l2 || l1->dims.size() < 2 || l2->dims.size() < 2)
                throw std::runtime_error("the image encoder lacks the two layers of its projector (mm.1, mm.2)");
            const long middle = static_cast<long>(l1->dims[1]);
            out_dim_ = static_cast<long>(l2->dims[1]);
            if (middle < 1 || middle > (1 << 17) || out_dim_ < 1 || out_dim_ > (1 << 17))
                throw std::runtime_error("the projector of the image encoder has an invalid width");
            proj1_ = readable(f.matrix("mm.1.weight", middle, D));
            proj2_ = readable(f.matrix("mm.2.weight", out_dim_, middle));
            f.optional_floats("mm.1.bias", static_cast<size_t>(middle), proj1_b_);
            f.optional_floats("mm.2.bias", static_cast<size_t>(out_dim_), proj2_b_);
            count(proj1_);
            count(proj2_);
            if (merge_ > 1)
            {
                /*
                    Le fusionneur lit les patchs d'un carré, merge x merge d'entre eux, que
                    l'encodage garde l'un après l'autre ; la référence les déplie canal par
                    canal (colonne c * merge^2 + k pour le canal c du patch k du carré), si bien
                    que ses colonnes sont mises dans l'ordre des patchs.
                */
                proj_norm_w_ = f.floats("mm.input_norm.weight", static_cast<size_t>(D));
                const long m2 = merge_ * merge_;
                const weight_matrix pm = f.matrix("mm.patch_merger.weight", D, M4);
                owned_.emplace_back(static_cast<size_t>(D * M4));
                uint16_t* half = owned_.back().data();
                // Sur le thread de chargement : les threads du thread pool peuvent être en train de calculer les
                // passes d'autres modèles.
                std::vector<float> row(static_cast<size_t>(M4)), order(static_cast<size_t>(M4));
                for (long o = 0; o < D; ++o)
                {
                    qk_dequantize_row(pm.type, pm.data + static_cast<size_t>(o) * pm.row_bytes, row.data(), M4);
                    for (long c = 0; c < D; ++c)
                        for (long k = 0; k < m2; ++k)
                            order[static_cast<size_t>(k * D + c)] = row[static_cast<size_t>(c * m2 + k)];
                    qk_float_to_half_row(order.data(), half + o * M4, M4);
                }
                patch_merger_.type = dlib::ggml_type::F16;
                patch_merger_.data = reinterpret_cast<const uint8_t*>(half);
                patch_merger_.rows = D;
                patch_merger_.cols = M4;
                patch_merger_.row_bytes = static_cast<size_t>(M4) * sizeof(uint16_t);
                count(patch_merger_);
            }
            // Le décodeur lit un [IMG_BREAK] après chaque ligne de tokens sauf la dernière.
            if (!f.has("v.token_embd.img_break"))
                throw std::runtime_error("the image encoder lacks the [IMG_BREAK] embedding (v.token_embd.img_break)");
            img_break_ = f.floats("v.token_embd.img_break", static_cast<size_t>(out_dim_));
        }
        else
        {
            final_.norm_w = f.floats("v.post_ln.weight", static_cast<size_t>(D));
            final_.norm_b = f.floats("v.post_ln.bias", static_cast<size_t>(D));
            final_.fc1 = readable(f.matrix("mm.0.weight", M4, M4));
            final_.fc1_b = f.floats("mm.0.bias", static_cast<size_t>(M4));
            final_.fc2 = readable(f.matrix("mm.2.weight", out_dim_, M4));
            final_.fc2_b = f.floats("mm.2.bias", static_cast<size_t>(out_dim_));
            count(final_.fc1);
            count(final_.fc2);
        }
        uint64_t most = 0;
        for (const auto& kv : by_type)
        {
            weight_bytes_ += static_cast<size_t>(kv.second);
            if (kv.second > most) { most = kv.second; weight_type_ = kv.first; }
        }
        weight_bytes_ += (patch_w_.size() + pos_.size()) * sizeof(float);
        if (gemma && pos_.empty()) weight_bytes_ += pos_table_.row_bytes * static_cast<size_t>(pos_table_.rows);

        attention_work_.resize(static_cast<size_t>(pool_.size()));
        for (auto& w : attention_work_) w.resize(static_cast<size_t>(qk_encoder_attention_work(head_dim_)));
        scratch_ = qk_scratch();
    }

    std::string vision_encoder::description() const
    {
        std::ostringstream o;
        if (kind_ == family::llava_next)
        {
            o << "image encoder LLaVA-NeXT (" << blocks_.size() << " blocks, width " << hidden_ << ", " << heads_
              << " heads, patch " << patch_ << ", views of " << image_size_ << " pixels, " << pinpoints_.size()
              << " pinpoints, output " << out_dim_ << (newline_.empty() ? "" : ", newline") << ", " << weight_type_
              << ")";
            return o.str();
        }
        if (kind_ == family::gemma4)
        {
            o << "image encoder Gemma 4 (" << blocks_.size() << " blocks, width " << hidden_ << ", " << heads_
              << " heads, patch " << patch_ << ", pooling " << merge_ << "x" << merge_ << ", positions up to "
              << pos_cells_ << ", clamped products, output " << out_dim_ << ", " << weight_type_ << ")";
            return o.str();
        }
        if (kind_ == family::pixtral)
        {
            o << "image encoder Pixtral (" << blocks_.size() << " blocks, width " << hidden_ << ", " << heads_
              << " heads, patch " << patch_ << ", merge " << merge_ << "x" << merge_ << ", longest edge "
              << image_size_ << ", output " << out_dim_ << ", [IMG_BREAK], " << weight_type_ << ")";
            return o.str();
        }
        o << "image encoder (" << blocks_.size() << " blocks, width " << hidden_ << ", " << heads_ << " heads, patch "
          << patch_ << ", merge " << merge_ << "x" << merge_ << ", positions " << side_ << "x" << side_;
        if (!deepstack_.empty())
        {
            o << ", deepstack after blocks";
            for (size_t i = 0; i < deepstack_.size(); ++i) o << (i ? ", " : " ") << deepstack_[i].after;
        }
        o << ", output " << out_dim_ << " x " << 1 + deepstack_.size() << ", " << weight_type_ << ")";
        return o.str();
    }

    void vision_encoder::product(const weight_matrix& w, const float* x, long n, long x_stride, float* y, long y_stride,
        bool accumulate)
    {
        if (!offload_)
        {
            qk_matmul(w, x, n, x_stride, y, y_stride, pool_, scratch_, accumulate, qk_path::automatic);
            return;
        }
        if (!accumulate)
        {
            offload_->matmul(w, x, n, x_stride, y, y_stride,
                [&]() { qk_matmul(w, x, n, x_stride, y, y_stride, pool_, scratch_, false, qk_path::automatic); });
            return;
        }
        // L'offload écrit ses produits : ceux ajoutés aux lignes de y passent par sum_.
        const long rows = w.rows;
        sum_.resize(static_cast<size_t>(n) * static_cast<size_t>(rows));
        float* s = sum_.data();
        offload_->matmul(w, x, n, x_stride, s, rows,
            [&]() { qk_matmul(w, x, n, x_stride, s, rows, pool_, scratch_, false, qk_path::automatic); });
        pool_.parallel_for(n, [&](long t, int) { qk_add(y + t * y_stride, s + t * rows, rows); });
    }

    weight_matrix vision_encoder::readable(const weight_matrix& m)
    {
        if (m.type != dlib::ggml_type::BF16 || !m.valid()) return m;
        owned_.emplace_back(static_cast<size_t>(m.rows) * static_cast<size_t>(m.cols));
        uint16_t* half = owned_.back().data();
        // Sur le thread de chargement : les threads du thread pool peuvent être en train de calculer les
        // passes d'autres modèles.
        std::vector<float> row(static_cast<size_t>(m.cols));
        for (long r = 0; r < m.rows; ++r)
        {
            qk_dequantize_row(m.type, m.data + static_cast<size_t>(r) * m.row_bytes, row.data(), m.cols);
            qk_float_to_half_row(row.data(), half + static_cast<size_t>(r) * static_cast<size_t>(m.cols), m.cols);
        }
        weight_matrix c = m;
        c.type = dlib::ggml_type::F16;
        c.data = reinterpret_cast<const uint8_t*>(half);
        c.row_bytes = static_cast<size_t>(m.cols) * sizeof(uint16_t);
        return c;
    }

    void vision_encoder::layer_norm_rows(const float* x, const std::vector<float>& w, const std::vector<float>& b,
        float* y, long rows, long width)
    {
        if (b.empty())
        {
            // La norme par moyenne quadratique de Pixtral ; x et y peuvent être les mêmes lignes.
            pool_.parallel_for(rows, [&](long r, int) {
                const float* xr = x + r * width;
                float* yr = y + r * width;
                const float s = 1.0f / std::sqrt(qk_dot_f32(xr, xr, width) / static_cast<float>(width) + eps_);
                for (long i = 0; i < width; ++i) yr[i] = xr[i] * s * w[static_cast<size_t>(i)];
            });
            return;
        }
        pool_.parallel_for(rows, [&](long r, int) {
            qk_layer_norm(x + r * width, w.data(), b.data(), y + r * width, width, eps_);
        });
    }

    void vision_encoder::add_bias_rows(float* y, long stride, const std::vector<float>& bias, long rows)
    {
        const long n = static_cast<long>(bias.size());
        if (n == 0) return;
        pool_.parallel_for(rows, [&](long r, int) { qk_add(y + r * stride, bias.data(), n); });
    }

    namespace
    {
        // Le round(x, 7) de Python : les produits de la référence y sont arrondis avant d'être tronqués.
        double round7(double x)
        {
            return std::nearbyint(x * 1e7) / 1e7;
        }

        // Les tokens qu'un détail permet au plus à une image de Gemma 4 ("min" : le moins qu'elle en prenne).
        long gemma_tokens(const std::string& detail)
        {
            if (detail == "min") return 70;
            if (detail == "low") return 140;
            if (detail == "high") return 1120;
            if (detail.empty() || detail == "auto") return 280;
            throw std::invalid_argument("the detail of an image is \"low\", \"auto\" or \"high\"");
        }

        /*
            calc_size_preserved_ratio() de llama.cpp (mtmd-image.cpp), en flottants comme lui : chaque côté
            arrondi au multiple de align le plus proche, puis, quand l'image dépasse max_pixels ou n'atteint
            pas min_pixels, les deux côtés du même rapport, arrondis vers le bas ou vers le haut.
        */
        void preserved_ratio(long width, long height, long align, long min_pixels, long max_pixels, long& out_w,
            long& out_h)
        {
            const float a = static_cast<float>(align);
            auto round_by = [&](float x) { return static_cast<long>(std::round(x / a)) * align; };
            auto ceil_by = [&](float x) { return static_cast<long>(std::ceil(x / a)) * align; };
            auto floor_by = [&](float x) { return static_cast<long>(std::floor(x / a)) * align; };
            const float w = static_cast<float>(width), h = static_cast<float>(height);
            long wb = std::max(align, round_by(w)), hb = std::max(align, round_by(h));
            if (wb * hb > max_pixels)
            {
                const float beta = std::sqrt(h * w / static_cast<float>(max_pixels));
                hb = std::max(align, floor_by(h / beta));
                wb = std::max(align, floor_by(w / beta));
            }
            else if (wb * hb < min_pixels)
            {
                const float beta = std::sqrt(static_cast<float>(min_pixels) / (h * w));
                hb = ceil_by(h * beta);
                wb = ceil_by(w * beta);
            }
            out_w = wb;
            out_h = hb;
        }

        // Le nombre maximal de tuiles qu'un détail permet à une image LLaVA-NeXT.
        long max_tiles(const std::string& detail)
        {
            if (detail == "low") return 1;
            if (detail == "high") return 4;
            if (detail.empty() || detail == "auto") return 2;
            throw std::invalid_argument("the detail of an image is \"low\", \"auto\" or \"high\"");
        }
    }

    vision_encoder::tile_layout vision_encoder::layout(long width, long height, long res_w, long res_h) const
    {
        /*
            La grille des caractéristiques des tuiles perd les lignes ou les colonnes des bandes
            noires de l'image redimensionnée, telles que unpad_image() de la référence les calcule
            d'après la taille de l'image ; chaque ligne gardée prend l'embedding de retour à la
            ligne après elle.
        */
        tile_layout t;
        t.res_w = res_w;
        t.res_h = res_h;
        t.rows = res_h / image_size_;
        t.cols = res_w / image_size_;
        const long ch = t.rows * side_, cw = t.cols * side_;
        t.y1 = ch;
        t.x1 = cw;
        const double original = static_cast<double>(width) / static_cast<double>(height);
        const double current = static_cast<double>(cw) / static_cast<double>(ch);
        if (original > current)
        {
            const double scale = static_cast<double>(cw) / static_cast<double>(width);
            const long kept = static_cast<long>(round7(static_cast<double>(height) * scale));
            const long pad = std::max<long>(0, (ch - kept) / 2);
            t.y0 = pad;
            t.y1 = ch - pad;
        }
        else
        {
            const double scale = static_cast<double>(ch) / static_cast<double>(height);
            const long kept = static_cast<long>(round7(static_cast<double>(width) * scale));
            const long pad = std::max<long>(0, (cw - kept) / 2);
            t.x0 = pad;
            t.x1 = cw - pad;
        }
        t.tokens = side_ * side_ + (t.y1 - t.y0) * (t.x1 - t.x0 + (newline_.empty() ? 0 : 1));
        return t;
    }

    vision_encoder::tile_layout vision_encoder::best_layout(long width, long height, long tiles) const
    {
        // select_best_resolution() de la référence : le pinpoint qui garde le plus de pixels de
        // l'image, puis celui qui en gaspille le moins.
        long best_w = 0, best_h = 0;
        long long most = 0, waste = std::numeric_limits<long long>::max();
        const double W = static_cast<double>(width), H = static_cast<double>(height);
        for (const auto& pin : pinpoints_)
        {
            const long w = pin.first, h = pin.second;
            if ((w / image_size_) * (h / image_size_) > tiles) continue;
            const double scale = std::min(static_cast<double>(w) / W, static_cast<double>(h) / H);
            const long long dw = static_cast<long long>(W * scale), dh = static_cast<long long>(H * scale);
            const long long effective = std::min(dw * dh, static_cast<long long>(width) * height);
            const long long wasted = static_cast<long long>(w) * h - effective;
            if (effective > most || (effective == most && wasted < waste))
            {
                most = effective;
                waste = wasted;
                best_w = w;
                best_h = h;
            }
        }
        if (best_w == 0) throw std::invalid_argument("the image encoder has no pinpoint for this detail");
        return layout(width, height, best_w, best_h);
    }

    vision_encoder::image_plan vision_encoder::plan(long width, long height, const std::string& detail) const
    {
        if (width <= 0 || height <= 0) throw std::invalid_argument("the image has no pixels");
        image_plan p;
        if (kind_ == family::qwen3vl)
        {
            long most = 576;
            if (detail == "low") most = 256;
            else if (detail == "high") most = 1024;
            else if (!detail.empty() && detail != "auto")
                throw std::invalid_argument("the detail of an image is \"low\", \"auto\" or \"high\"");
            const long f = factor();
            fit_image_size(width, height, f, 64 * f * f, most * f * f, p.width, p.height);
            p.grid_w = p.width / f;
            p.grid_h = p.height / f;
            p.tokens = p.grid_w * p.grid_h;
            return p;
        }
        if (kind_ == family::gemma4)
        {
            const long most = gemma_tokens(detail);
            if (static_cast<double>(std::max(width, height)) / static_cast<double>(std::min(width, height)) > 200.0)
                throw std::invalid_argument("the sides of the image differ by a ratio over 200");
            const long f = factor();
            preserved_ratio(width, height, f, 70 * f * f, most * f * f, p.width, p.height);
            p.grid_w = p.width / f;
            p.grid_h = p.height / f;
            p.tokens = p.grid_w * p.grid_h;
            if (p.width / patch_ > pos_cells_ || p.height / patch_ > pos_cells_)
                throw std::invalid_argument("the image is too long for the positions of the image encoder");
            return p;
        }
        if (kind_ == family::pixtral)
        {
            long most = 576;
            if (detail == "low") most = 256;
            else if (detail == "high") most = 1024;
            else if (!detail.empty() && detail != "auto")
                throw std::invalid_argument("the detail of an image is \"low\", \"auto\" or \"high\"");
            /*
                La référence ramène le plus long côté dans la limite de image_size() pixels, les
                côtés arrondis vers le bas, puis arrondit chaque côté au-dessus à un multiple de
                factor() pixels ; une grille de plus de tokens fusionnés que le détail n'en permet
                rétrécit du même rapport sur les deux côtés, arrondie vers le bas.
            */
            const long f = factor();
            double w = static_cast<double>(width), h = static_cast<double>(height);
            const double ratio = std::max(w, h) / static_cast<double>(image_size_);
            if (ratio > 1.0)
            {
                w = std::max(1.0, std::floor(w / ratio));
                h = std::max(1.0, std::floor(h / ratio));
            }
            long gw = (static_cast<long>(w) - 1) / f + 1, gh = (static_cast<long>(h) - 1) / f + 1;
            if (gw * gh > most)
            {
                const double s = std::sqrt(static_cast<double>(most) / static_cast<double>(gw * gh));
                gw = std::max<long>(1, static_cast<long>(std::floor(static_cast<double>(gw) * s)));
                gh = std::max<long>(1, static_cast<long>(std::floor(static_cast<double>(gh) * s)));
            }
            p.width = gw * f;
            p.height = gh * f;
            p.grid_w = gw;
            p.grid_h = gh;
            p.tokens = gw * gh + gh - 1;
            return p;
        }
        const long tiles = max_tiles(detail);
        if (static_cast<double>(std::max(width, height)) / static_cast<double>(std::min(width, height)) > 200.0)
            throw std::invalid_argument("the sides of the image differ by a ratio over 200");
        // L'image est décodée à sa taille, dans la limite de 4096 pixels par côté.
        const long side = 4096;
        p.width = width;
        p.height = height;
        if (std::max(width, height) > side)
        {
            const double s = static_cast<double>(side) / static_cast<double>(std::max(width, height));
            p.width = std::max<long>(1, static_cast<long>(std::lround(static_cast<double>(width) * s)));
            p.height = std::max<long>(1, static_cast<long>(std::lround(static_cast<double>(height) * s)));
        }
        const tile_layout t = best_layout(p.width, p.height, tiles);
        p.tokens = t.tokens;
        p.grid_w = t.tokens;
        p.grid_h = 1;
        p.tiles_w = t.res_w;
        p.tiles_h = t.res_h;
        return p;
    }

    long vision_encoder::most_tokens(const std::string& detail) const
    {
        if (kind_ == family::gemma4) return gemma_tokens(detail);
        if (kind_ == family::qwen3vl)
            return detail == "min" ? 64 : detail == "low" ? 256 : detail == "high" ? 1024 : 576;
        if (kind_ == family::pixtral)
        {
            if (detail == "min") return 1;
            const long most = detail == "low" ? 256 : detail == "high" ? 1024 : 576;
            // Les tokens fusionnés, et un [IMG_BREAK] après chaque ligne sauf la dernière.
            const long rows = std::min(most, image_size_ / factor());
            return most + rows - 1;
        }
        if (detail == "min") return side_ * side_;
        const long tiles = max_tiles(detail);
        long most = 0;
        for (const auto& pin : pinpoints_)
        {
            const long rows = pin.second / image_size_, cols = pin.first / image_size_;
            if (rows * cols > tiles) continue;
            most = std::max(most, side_ * side_ + rows * side_ * (cols * side_ + (newline_.empty() ? 0 : 1)));
        }
        return most;
    }

    void vision_encoder::embed_patches(job& j, const rgb_image& image)
    {
        j.pw_ = image.width / patch_;
        j.ph_ = image.height / patch_;
        j.n_ = j.pw_ * j.ph_;
        const long n = j.n_, D = hidden_, K = 3 * patch_ * patch_, half = head_dim_ / 2, quarter = head_dim_ / 4;
        const long m2 = merge_ * merge_;
        const long grid_w = j.pw_ / merge_;
        const bool rotary = kind_ != family::llava_next;

        if (kind_ == family::gemma4)
        {
            /*
                Les patchs ligne par ligne, leurs pixels ramenés dans [-1, 1] (2 x la valeur normalisée - 1,
                comme gemma4v.cpp de llama.cpp) ; chacun reçoit la ligne de sa colonne dans la première table
                de positions et celle de sa ligne dans la seconde, et les angles de ses paires rotatives, la
                colonne pour la première moitié de chaque tête, la ligne pour la seconde.
            */
            if (j.pw_ > pos_cells_ || j.ph_ > pos_cells_)
                throw std::invalid_argument("vision_encoder: the image is too long for the position tables");
            xn_.resize(static_cast<size_t>(n * K));
            float gs[3], gb[3];
            for (int c = 0; c < 3; ++c)
            {
                gs[c] = 2.0f / (255.0f * std_[c]);
                gb[c] = 2.0f * mean_[c] / std_[c] + 1.0f;
            }
            pool_.parallel_for(n, [&](long i, int) {
                const long row = i / j.pw_, col = i % j.pw_;
                float* dst = xn_.data() + i * K;
                for (long c = 0; c < 3; ++c)
                    for (long y = 0; y < patch_; ++y)
                    {
                        const uint8_t* src = image.pixels.data()
                            + (static_cast<size_t>(row * patch_ + y) * image.width + col * patch_) * 3 + c;
                        float* d = dst + (c * patch_ + y) * patch_;
                        for (long x = 0; x < patch_; ++x) d[x] = static_cast<float>(src[3 * x]) * gs[c] - gb[c];
                    }
            });
            j.x_.resize(static_cast<size_t>(n * D));
            product(patch_matrix_, xn_.data(), n, K, j.x_.data(), D);
            j.cos_.resize(static_cast<size_t>(n * half));
            j.sin_.resize(static_cast<size_t>(n * half));
            pool_.parallel_for(n, [&](long i, int) {
                const long row = i / j.pw_, col = i % j.pw_;
                float* x = j.x_.data() + i * D;
                if (!patch_b_.empty()) qk_add(x, patch_b_.data(), D);
                const float* tx = reinterpret_cast<const float*>(pos_table_.data + static_cast<size_t>(col)
                    * pos_table_.row_bytes);
                const float* ty = reinterpret_cast<const float*>(pos_table_.data + static_cast<size_t>(pos_cells_ + row)
                    * pos_table_.row_bytes);
                qk_add(x, tx, D);
                qk_add(x, ty, D);
                float* c = j.cos_.data() + i * half;
                float* s = j.sin_.data() + i * half;
                for (long k = 0; k < quarter; ++k)
                {
                    const float ax = static_cast<float>(col) * inv_freq_[static_cast<size_t>(k)];
                    const float ay = static_cast<float>(row) * inv_freq_[static_cast<size_t>(k)];
                    c[k] = std::cos(ax);
                    s[k] = std::sin(ax);
                    c[quarter + k] = std::cos(ay);
                    s[quarter + k] = std::sin(ay);
                }
            });
            return;
        }

        // La ligne et la colonne du patch i, les patchs pris carré par carré de merge x merge.
        auto place = [&](long i, long& row, long& col) {
            const long square = i / m2, within = i % m2;
            row = square / grid_w * merge_ + within / merge_;
            col = square % grid_w * merge_ + within % merge_;
        };

        // Les pixels de chaque patch, normalisés, dans l'ordre du noyau.
        xn_.resize(static_cast<size_t>(n * K));
        float scale[3], shift[3];
        for (int c = 0; c < 3; ++c)
        {
            scale[c] = 1.0f / (255.0f * std_[c]);
            shift[c] = mean_[c] / std_[c];
        }
        pool_.parallel_for(n, [&](long i, int) {
            long row, col;
            place(i, row, col);
            float* dst = xn_.data() + i * K;
            for (long c = 0; c < 3; ++c)
                for (long y = 0; y < patch_; ++y)
                {
                    const uint8_t* src = image.pixels.data()
                        + (static_cast<size_t>(row * patch_ + y) * image.width + col * patch_) * 3 + c;
                    float* d = dst + (c * patch_ + y) * patch_;
                    for (long x = 0; x < patch_; ++x) d[x] = static_cast<float>(src[3 * x]) * scale[c] - shift[c];
                }
        });
        j.x_.resize(static_cast<size_t>(n * D));
        product(patch_matrix_, xn_.data(), n, K, j.x_.data(), D);

        // Le biais, les positions interpolées et les angles des paires rotatives.
        if (rotary)
        {
            j.cos_.resize(static_cast<size_t>(n * half));
            j.sin_.resize(static_cast<size_t>(n * half));
        }
        const float ry = j.ph_ > 1 ? static_cast<float>(side_ - 1) / static_cast<float>(j.ph_ - 1) : 0.0f;
        const float rx = j.pw_ > 1 ? static_cast<float>(side_ - 1) / static_cast<float>(j.pw_ - 1) : 0.0f;
        pool_.parallel_for(n, [&](long i, int) {
            long row, col;
            place(i, row, col);
            float* x = j.x_.data() + i * D;
            if (!patch_b_.empty()) qk_add(x, patch_b_.data(), D);
            if (!rotary)
            {
                // Une vue couvre exactement la table des positions.
                qk_add(x, pos_.data() + (row * side_ + col) * D, D);
                return;
            }
            if (kind_ == family::pixtral)
            {
                // Les paires de la première moitié tournent avec la ligne, celles de la seconde moitié avec la
                // colonne.
                float* c = j.cos_.data() + i * half;
                float* s = j.sin_.data() + i * half;
                for (long k = 0; k < half; ++k)
                {
                    const double a = static_cast<double>(k < quarter ? row : col) * inv_freq_[static_cast<size_t>(k)];
                    c[k] = static_cast<float>(std::cos(a));
                    s[k] = static_cast<float>(std::sin(a));
                }
                return;
            }
            const float fy = static_cast<float>(row) * ry, fx = static_cast<float>(col) * rx;
            const long y0 = static_cast<long>(std::floor(fy)), x0 = static_cast<long>(std::floor(fx));
            const long y1 = std::min(y0 + 1, side_ - 1), x1 = std::min(x0 + 1, side_ - 1);
            const float wy = fy - static_cast<float>(y0), wx = fx - static_cast<float>(x0);
            const float w00 = (1.0f - wy) * (1.0f - wx), w01 = (1.0f - wy) * wx, w10 = wy * (1.0f - wx), w11 = wy * wx;
            const float* p00 = pos_.data() + (y0 * side_ + x0) * D;
            const float* p01 = pos_.data() + (y0 * side_ + x1) * D;
            const float* p10 = pos_.data() + (y1 * side_ + x0) * D;
            const float* p11 = pos_.data() + (y1 * side_ + x1) * D;
            for (long d = 0; d < D; ++d) x[d] += w00 * p00[d] + w01 * p01[d] + w10 * p10[d] + w11 * p11[d];
            float* c = j.cos_.data() + i * half;
            float* s = j.sin_.data() + i * half;
            for (long k = 0; k < quarter; ++k)
            {
                const float ay = static_cast<float>(row) * inv_freq_[static_cast<size_t>(k)];
                const float ax = static_cast<float>(col) * inv_freq_[static_cast<size_t>(k)];
                c[k] = static_cast<float>(std::cos(static_cast<double>(ay)));
                s[k] = static_cast<float>(std::sin(static_cast<double>(ay)));
                c[quarter + k] = static_cast<float>(std::cos(static_cast<double>(ax)));
                s[quarter + k] = static_cast<float>(std::sin(static_cast<double>(ax)));
            }
        });
        // Pixtral normalise les patchs avant son premier bloc.
        if (kind_ == family::pixtral && !pre_ln_w_.empty())
            layer_norm_rows(j.x_.data(), pre_ln_w_, std::vector<float>(), j.x_.data(), n, D);
    }

    std::unique_ptr<vision_encoder::job> vision_encoder::start(const rgb_image& image, const image_plan& plan)
    {
        const auto t0 = std::chrono::steady_clock::now();
        // Les copies des poids restent sur le périphérique jusqu'à release_work().
        if (offload_) offload_->set_busy(true);
        std::unique_ptr<job> j(new job());
        if (image.width <= 0 || image.height <= 0
            || image.pixels.size() != static_cast<size_t>(image.width * image.height * 3))
            throw std::invalid_argument("vision_encoder: the image has no pixels");
        if (kind_ != family::llava_next)
        {
            const long fct = factor();
            if (image.width % fct != 0 || image.height % fct != 0)
                throw std::invalid_argument("vision_encoder: the sides of the image are not multiples of "
                    + std::to_string(fct) + " pixels");
            j->grid_w_ = image.width / patch_ / merge_;
            j->grid_h_ = image.height / patch_ / merge_;
            j->tokens_ = j->grid_w_ * j->grid_h_;
            if (kind_ == family::pixtral) j->tokens_ += j->grid_h_ - 1;
            embed_patches(*j, image);
            j->out_.assign(static_cast<size_t>(j->tokens_ * output_width()), 0.0f);
            j->seconds_ += seconds_since(t0);
            return j;
        }

        if (image.width != plan.width || image.height != plan.height || plan.tiles_w < image_size_
            || plan.tiles_h < image_size_)
            throw std::invalid_argument("vision_encoder: the image does not match its plan");
        const tile_layout t = layout(image.width, image.height, plan.tiles_w, plan.tiles_h);
        if (t.tokens != plan.tokens) throw std::invalid_argument("vision_encoder: the tiles of the image changed");
        const long S = image_size_;
        // L'image entière, redimensionnée en une vue.
        j->views_.push_back(resize_bilinear(image, S, S));
        /*
            L'image redimensionnée dans le pinpoint, son rapport d'aspect gardé (get_patch_output_size()
            de la référence arrondit l'autre côté au-dessus), centrée sur du noir, puis découpée en vues.
        */
        const double sw = static_cast<double>(t.res_w) / image.width, sh = static_cast<double>(t.res_h) / image.height;
        long fw = t.res_w, fh = t.res_h;
        if (sw < sh) fh = std::min<long>(static_cast<long>(std::ceil(image.height * sw)), t.res_h);
        else fw = std::min<long>(static_cast<long>(std::ceil(image.width * sh)), t.res_w);
        fw = std::max<long>(1, fw);
        fh = std::max<long>(1, fh);
        const rgb_image fitted = resize_bilinear(image, fw, fh);
        const long left = (t.res_w - fw) / 2, top = (t.res_h - fh) / 2;
        for (long r = 0; r < t.rows; ++r)
            for (long c = 0; c < t.cols; ++c)
            {
                rgb_image v;
                v.width = S;
                v.height = S;
                v.pixels.assign(static_cast<size_t>(S * S * 3), 0);
                for (long y = 0; y < S; ++y)
                {
                    const long fy = r * S + y - top;
                    if (fy < 0 || fy >= fh) continue;
                    const long x0 = std::max<long>(0, left - c * S), x1 = std::min<long>(S, left + fw - c * S);
                    if (x0 >= x1) continue;
                    const uint8_t* src = fitted.pixels.data()
                        + (static_cast<size_t>(fy) * fw + (c * S + x0 - left)) * 3;
                    std::memcpy(v.pixels.data() + (static_cast<size_t>(y) * S + x0) * 3, src,
                        static_cast<size_t>(x1 - x0) * 3);
                }
                j->views_.push_back(std::move(v));
            }
        j->tile_rows_ = t.rows;
        j->tile_cols_ = t.cols;
        j->keep_y0_ = t.y0;
        j->keep_y1_ = t.y1;
        j->keep_x0_ = t.x0;
        j->keep_x1_ = t.x1;
        j->tokens_ = t.tokens;
        j->grid_w_ = t.tokens;
        j->grid_h_ = 1;
        embed_patches(*j, j->views_[0]);
        j->features_.assign(j->views_.size() * static_cast<size_t>(side_ * side_ * out_dim_), 0.0f);
        j->out_.assign(static_cast<size_t>(j->tokens_ * out_dim_), 0.0f);
        j->seconds_ += seconds_since(t0);
        return j;
    }

    void vision_encoder::run_block(job& j, const block& b)
    {
        const long n = j.n_, D = hidden_, H = heads_, hd = head_dim_, half = hd / 2;
        const long C = std::min(chunk_rows, n);
        const float scale = 1.0f / std::sqrt(static_cast<float>(hd));
        const bool rotary = kind_ != family::llava_next;
        // Pixtral fait tourner des paires de dimensions consécutives, Qwen3-VL des dimensions distantes d'une
        // demi-tête.
        const bool pairs = kind_ == family::pixtral;
        xn_.resize(static_cast<size_t>(C * D));
        qkv_.resize(static_cast<size_t>(C * 3 * D));
        q_.resize(static_cast<size_t>(n * D));
        kt_.resize(static_cast<size_t>(n * D));
        v_.resize(static_cast<size_t>(n * D));
        ctx_.resize(static_cast<size_t>(n * D));

        /*
            Les requêtes, mises à l'échelle, et les valeurs de la tête h se trouvent dans des lignes de hd
            floats à partir de h * n * hd ; ses clés se trouvent transposées, hd lignes de n floats à partir
            de h * hd * n, pour que les scores d'une requête contre des clés consécutives soient des
            produits vectoriels.
        */
        for (long c0 = 0; c0 < n; c0 += C)
        {
            const long rows = std::min(C, n - c0);
            layer_norm_rows(j.x_.data() + c0 * D, b.ln1_w, b.ln1_b, xn_.data(), rows, D);
            if (b.fused)
            {
                product(b.qkv, xn_.data(), rows, D, qkv_.data(), 3 * D);
            }
            else
            {
                product(b.q, xn_.data(), rows, D, qkv_.data(), 3 * D);
                product(b.k, xn_.data(), rows, D, qkv_.data() + D, 3 * D);
                product(b.v, xn_.data(), rows, D, qkv_.data() + 2 * D, 3 * D);
            }
            auto one = [&](long r) {
                float* t = qkv_.data() + r * 3 * D;
                if (!b.qkv_b.empty()) qk_add(t, b.qkv_b.data(), 3 * D);
                const long p = c0 + r;
                for (long h = 0; h < H; ++h)
                {
                    float* q = t + h * hd;
                    float* k = t + D + h * hd;
                    if (rotary && pairs)
                    {
                        const float* c = j.cos_.data() + p * half;
                        const float* s = j.sin_.data() + p * half;
                        for (long i = 0; i < half; ++i)
                        {
                            const float q0 = q[2 * i], q1 = q[2 * i + 1];
                            q[2 * i] = q0 * c[i] - q1 * s[i];
                            q[2 * i + 1] = q1 * c[i] + q0 * s[i];
                            const float k0 = k[2 * i], k1 = k[2 * i + 1];
                            k[2 * i] = k0 * c[i] - k1 * s[i];
                            k[2 * i + 1] = k1 * c[i] + k0 * s[i];
                        }
                    }
                    else if (rotary)
                    {
                        const float* c = j.cos_.data() + p * half;
                        const float* s = j.sin_.data() + p * half;
                        for (long i = 0; i < half; ++i)
                        {
                            const float q0 = q[i], q1 = q[i + half];
                            q[i] = q0 * c[i] - q1 * s[i];
                            q[i + half] = q1 * c[i] + q0 * s[i];
                            const float k0 = k[i], k1 = k[i + half];
                            k[i] = k0 * c[i] - k1 * s[i];
                            k[i + half] = k1 * c[i] + k0 * s[i];
                        }
                    }
                    float* qd = q_.data() + (h * n + p) * hd;
                    for (long e = 0; e < hd; ++e) qd[e] = q[e] * scale;
                    float* kd = kt_.data() + h * hd * n + p;
                    for (long e = 0; e < hd; ++e) kd[e * n] = k[e];
                    std::memcpy(v_.data() + (h * n + p) * hd, t + 2 * D + h * hd,
                        sizeof(float) * static_cast<size_t>(hd));
                }
            };
            /*
                Seize patchs consécutifs par tâche : les clés d'une tâche remplissent ensemble les
                lignes de 64 octets des lignes transposées, au lieu que chaque ligne reçoive les
                écritures des tâches de plusieurs threads.
            */
            const long group = 16;
            pool_.parallel_for((rows + group - 1) / group, [&](long g, int) {
                const long r1 = std::min(rows, (g + 1) * group);
                for (long r = g * group; r < r1; ++r) one(r);
            });
        }

        // Chaque patch fait son attention sur chaque patch, par tuiles de requêtes d'une tête.
        const long tile = qk_encoder_tile_rows;
        const long tiles = (n + tile - 1) / tile;
        pool_.parallel_for(H * tiles, [&](long task, int thread) {
            const long h = task / tiles;
            const long t0 = task % tiles * tile;
            const long rows = std::min(tile, n - t0);
            qk_encoder_attention(q_.data() + (h * n + t0) * hd, rows, kt_.data() + h * hd * n, n,
                v_.data() + h * n * hd, hd, n, hd, ctx_.data() + t0 * D + h * hd, D,
                attention_work_[static_cast<size_t>(thread)].data());
        });
        product(b.out, ctx_.data(), n, D, j.x_.data(), D, true);
        add_bias_rows(j.x_.data(), D, b.out_b, n);

        // La couche feed-forward, un morceau de patchs à la fois.
        h_.resize(static_cast<size_t>(C * ffn_));
        if (b.gated) u_.resize(static_cast<size_t>(C * ffn_));
        for (long c0 = 0; c0 < n; c0 += C)
        {
            const long rows = std::min(C, n - c0);
            float* x = j.x_.data() + c0 * D;
            layer_norm_rows(x, b.ln2_w, b.ln2_b, xn_.data(), rows, D);
            if (b.gated)
            {
                product(b.gate, xn_.data(), rows, D, h_.data(), ffn_);
                product(b.up, xn_.data(), rows, D, u_.data(), ffn_);
                pool_.parallel_for(rows, [&](long r, int) {
                    float* u = u_.data() + r * ffn_;
                    if (!b.up_b.empty()) qk_add(u, b.up_b.data(), ffn_);
                    qk_silu_mul(h_.data() + r * ffn_, u, ffn_);
                });
            }
            else
            {
                product(b.up, xn_.data(), rows, D, h_.data(), ffn_);
                pool_.parallel_for(rows, [&](long r, int) {
                    float* t = h_.data() + r * ffn_;
                    qk_add(t, b.up_b.data(), ffn_);
                    qk_gelu_tanh(t, ffn_);
                });
            }
            product(b.down, h_.data(), rows, ffn_, x, D, true);
            add_bias_rows(x, D, b.down_b, rows);
        }
    }

    void vision_encoder::run_merger(job& j, const merger& m, bool merged_norm, long column)
    {
        // Un token fusionné est fait de merge x merge patchs consécutifs, dont les lignes se suivent.
        const long T = j.tokens_, D = hidden_, M4 = hidden_ * merge_ * merge_, W = output_width();
        m_.resize(static_cast<size_t>(T * M4));
        if (merged_norm) layer_norm_rows(j.x_.data(), m.norm_w, m.norm_b, m_.data(), T, M4);
        else layer_norm_rows(j.x_.data(), m.norm_w, m.norm_b, m_.data(), j.n_, D);
        h_.resize(static_cast<size_t>(T * M4));
        product(m.fc1, m_.data(), T, M4, h_.data(), M4);
        pool_.parallel_for(T, [&](long t, int) {
            float* r = h_.data() + t * M4;
            qk_add(r, m.fc1_b.data(), M4);
            qk_gelu_erf(r, M4);
        });
        float* out = j.out_.data() + column;
        product(m.fc2, h_.data(), T, M4, out, W);
        add_bias_rows(out, W, m.fc2_b, T);
    }

    void vision_encoder::project_view(job& j)
    {
        // Le projecteur de LLaVA-NeXT sur chaque patch de la vue : une couche linéaire, une GELU exacte,
        // une couche linéaire.
        const long n = j.n_, D = hidden_, X = final_.fc1.rows, O = out_dim_;
        h_.resize(static_cast<size_t>(n * X));
        product(final_.fc1, j.x_.data(), n, D, h_.data(), X);
        pool_.parallel_for(n, [&](long t, int) {
            float* r = h_.data() + t * X;
            qk_add(r, final_.fc1_b.data(), X);
            qk_gelu_erf(r, X);
        });
        float* out = j.features_.data() + j.view_ * static_cast<size_t>(n * O);
        product(final_.fc2, h_.data(), n, X, out, O);
        add_bias_rows(out, O, final_.fc2_b, n);
    }

    void vision_encoder::pack_views(job& j)
    {
        // L'image entière, puis la grille des tuiles gardées, ligne par ligne, chaque ligne suivie du retour
        // à la ligne.
        const long S = side_, n = S * S, O = out_dim_;
        const size_t row_bytes = sizeof(float) * static_cast<size_t>(O);
        float* out = j.out_.data();
        std::memcpy(out, j.features_.data(), row_bytes * static_cast<size_t>(n));
        out += n * O;
        for (long gy = j.keep_y0_; gy < j.keep_y1_; ++gy)
        {
            for (long gx = j.keep_x0_; gx < j.keep_x1_; ++gx)
            {
                const long tile = 1 + (gy / S) * j.tile_cols_ + gx / S;
                std::memcpy(out, j.features_.data() + (tile * n + (gy % S) * S + gx % S) * O, row_bytes);
                out += O;
            }
            if (!newline_.empty())
            {
                std::memcpy(out, newline_.data(), row_bytes);
                out += O;
            }
        }
    }

    void vision_encoder::project_pixtral(job& j)
    {
        /*
            Chaque patch normalisé, les carrés fusionnés (leurs patchs se suivent), puis une couche
            linéaire, une GELU exacte et une couche linéaire ; les lignes de la grille fusionnée vont
            à la sortie, chacune suivie, sauf la dernière, de l'embedding [IMG_BREAK].
        */
        const long n = j.n_, D = hidden_, m2 = merge_ * merge_, T = n / m2, X = proj1_.rows, O = out_dim_;
        const float* merged = j.x_.data();
        if (patch_merger_.valid())
        {
            m_.resize(static_cast<size_t>(n * D));
            layer_norm_rows(j.x_.data(), proj_norm_w_, std::vector<float>(), m_.data(), n, D);
            xn_.resize(static_cast<size_t>(T * D));
            product(patch_merger_, m_.data(), T, m2 * D, xn_.data(), D);
            merged = xn_.data();
        }
        h_.resize(static_cast<size_t>(T * X));
        product(proj1_, merged, T, D, h_.data(), X);
        pool_.parallel_for(T, [&](long t, int) {
            float* r = h_.data() + t * X;
            if (!proj1_b_.empty()) qk_add(r, proj1_b_.data(), X);
            qk_gelu_erf(r, X);
        });
        ctx_.resize(static_cast<size_t>(T * O));
        product(proj2_, h_.data(), T, X, ctx_.data(), O);
        add_bias_rows(ctx_.data(), O, proj2_b_, T);
        const long gw = j.grid_w_, gh = j.grid_h_;
        float* out = j.out_.data();
        for (long y = 0; y < gh; ++y)
        {
            std::memcpy(out, ctx_.data() + y * gw * O, sizeof(float) * static_cast<size_t>(gw * O));
            out += gw * O;
            if (y + 1 < gh)
            {
                std::memcpy(out, img_break_.data(), sizeof(float) * static_cast<size_t>(O));
                out += O;
            }
        }
    }

    vision_encoder::clamp vision_encoder::read_clamp(const model_file& f, const std::string& prefix) const
    {
        // Les bornes absentes du fichier n'écrêtent pas (FLT_MAX de clip.cpp).
        clamp c;
        std::vector<float> v;
        if (f.optional_floats(prefix + ".input_min", 1, v)) c.in_lo = v[0];
        if (f.optional_floats(prefix + ".input_max", 1, v)) c.in_hi = v[0];
        if (f.optional_floats(prefix + ".output_min", 1, v)) c.out_lo = v[0];
        if (f.optional_floats(prefix + ".output_max", 1, v)) c.out_hi = v[0];
        if (!(c.in_lo <= c.in_hi) || !(c.out_lo <= c.out_hi))
            throw std::runtime_error("the bounds of " + prefix + " in the image encoder are inverted");
        return c;
    }

    void vision_encoder::clamped_product(const weight_matrix& w, const clamp& c, const float* x, long n, long x_stride,
        float* y, long y_stride)
    {
        const long cols = w.cols, rows = w.rows;
        if (std::isfinite(c.in_lo) || std::isfinite(c.in_hi))
        {
            cl_.resize(static_cast<size_t>(n * cols));
            pool_.parallel_for(n, [&](long r, int) {
                const float* src = x + r * x_stride;
                float* dst = cl_.data() + r * cols;
                for (long i = 0; i < cols; ++i) dst[i] = std::min(std::max(src[i], c.in_lo), c.in_hi);
            });
            x = cl_.data();
            x_stride = cols;
        }
        product(w, x, n, x_stride, y, y_stride);
        if (std::isfinite(c.out_lo) || std::isfinite(c.out_hi))
            pool_.parallel_for(n, [&](long r, int) {
                float* t = y + r * y_stride;
                for (long i = 0; i < rows; ++i) t[i] = std::min(std::max(t[i], c.out_lo), c.out_hi);
            });
    }

    void vision_encoder::run_block_gemma(job& j, const block& b)
    {
        /*
            Le bloc de Gemma 4, comme build_vit() de llama.cpp pour gemma4v : norme, requêtes, clés et valeurs
            par des produits écrêtés, normes par tête (les valeurs sans poids), rotation, attention sans mise
            à l'échelle, produit de sortie écrêté et sa norme, ajoutés au patch ; puis norme, porte GELU,
            produit écrêté et sa norme, ajoutés au patch.
        */
        const long n = j.n_, D = hidden_, H = heads_, hd = head_dim_, half = hd / 2, quarter = hd / 4;
        const long C = std::min(chunk_rows, n);
        const float eps = eps_;
        auto rms = [eps](float* x, const float* w, long width) {
            const float s = 1.0f / std::sqrt(qk_dot_f32(x, x, width) / static_cast<float>(width) + eps);
            if (w) for (long i = 0; i < width; ++i) x[i] = x[i] * s * w[i];
            else for (long i = 0; i < width; ++i) x[i] *= s;
        };
        // Les dimensions o + i et o + i + quarter de chaque moitié tournent ensemble (néox).
        auto rotate = [&](float* v, const float* c, const float* s) {
            for (long o = 0; o < hd; o += half)
            {
                const float* cc = c + (o / half) * quarter;
                const float* ss = s + (o / half) * quarter;
                for (long i = 0; i < quarter; ++i)
                {
                    const float v0 = v[o + i], v1 = v[o + i + quarter];
                    v[o + i] = v0 * cc[i] - v1 * ss[i];
                    v[o + i + quarter] = v1 * cc[i] + v0 * ss[i];
                }
            }
        };
        const std::vector<float> no_bias;
        xn_.resize(static_cast<size_t>(C * D));
        qkv_.resize(static_cast<size_t>(C * 3 * D));
        q_.resize(static_cast<size_t>(n * D));
        kt_.resize(static_cast<size_t>(n * D));
        v_.resize(static_cast<size_t>(n * D));
        ctx_.resize(static_cast<size_t>(n * D));
        for (long c0 = 0; c0 < n; c0 += C)
        {
            const long rows = std::min(C, n - c0);
            layer_norm_rows(j.x_.data() + c0 * D, b.ln1_w, no_bias, xn_.data(), rows, D);
            clamped_product(b.q, b.cq, xn_.data(), rows, D, qkv_.data(), 3 * D);
            clamped_product(b.k, b.ck, xn_.data(), rows, D, qkv_.data() + D, 3 * D);
            clamped_product(b.v, b.cv, xn_.data(), rows, D, qkv_.data() + 2 * D, 3 * D);
            auto one = [&](long r) {
                float* t = qkv_.data() + r * 3 * D;
                const long p = c0 + r;
                const float* c = j.cos_.data() + p * half;
                const float* s = j.sin_.data() + p * half;
                for (long h = 0; h < H; ++h)
                {
                    float* q = t + h * hd;
                    float* k = t + D + h * hd;
                    float* v = t + 2 * D + h * hd;
                    rms(q, b.q_norm.data(), hd);
                    rms(k, b.k_norm.data(), hd);
                    rms(v, nullptr, hd);
                    rotate(q, c, s);
                    rotate(k, c, s);
                    std::memcpy(q_.data() + (h * n + p) * hd, q, sizeof(float) * static_cast<size_t>(hd));
                    float* kd = kt_.data() + h * hd * n + p;
                    for (long e = 0; e < hd; ++e) kd[e * n] = k[e];
                    std::memcpy(v_.data() + (h * n + p) * hd, v, sizeof(float) * static_cast<size_t>(hd));
                }
            };
            const long group = 16;
            pool_.parallel_for((rows + group - 1) / group, [&](long g, int) {
                const long r1 = std::min(rows, (g + 1) * group);
                for (long r = g * group; r < r1; ++r) one(r);
            });
        }

        const long tile = qk_encoder_tile_rows;
        const long tiles = (n + tile - 1) / tile;
        pool_.parallel_for(H * tiles, [&](long task, int thread) {
            const long h = task / tiles;
            const long t0 = task % tiles * tile;
            const long rows = std::min(tile, n - t0);
            qk_encoder_attention(q_.data() + (h * n + t0) * hd, rows, kt_.data() + h * hd * n, n,
                v_.data() + h * n * hd, hd, n, hd, ctx_.data() + t0 * D + h * hd, D,
                attention_work_[static_cast<size_t>(thread)].data());
        });
        o_.resize(static_cast<size_t>(n * D));
        clamped_product(b.out, b.cout, ctx_.data(), n, D, o_.data(), D);
        pool_.parallel_for(n, [&](long r, int) {
            float* o = o_.data() + r * D;
            rms(o, b.attn_post.data(), D);
            qk_add(j.x_.data() + r * D, o, D);
        });

        h_.resize(static_cast<size_t>(C * ffn_));
        u_.resize(static_cast<size_t>(C * ffn_));
        for (long c0 = 0; c0 < n; c0 += C)
        {
            const long rows = std::min(C, n - c0);
            float* x = j.x_.data() + c0 * D;
            layer_norm_rows(x, b.ln2_w, no_bias, xn_.data(), rows, D);
            clamped_product(b.gate, b.cgate, xn_.data(), rows, D, h_.data(), ffn_);
            clamped_product(b.up, b.cup, xn_.data(), rows, D, u_.data(), ffn_);
            pool_.parallel_for(rows, [&](long r, int) {
                float* g = h_.data() + r * ffn_;
                const float* u = u_.data() + r * ffn_;
                qk_gelu_tanh(g, ffn_);
                for (long i = 0; i < ffn_; ++i) g[i] *= u[i];
            });
            clamped_product(b.down, b.cdown, h_.data(), rows, ffn_, o_.data(), D);
            pool_.parallel_for(rows, [&](long r, int) {
                float* o = o_.data() + r * D;
                rms(o, b.ffn_post.data(), D);
                qk_add(x + r * D, o, D);
            });
        }
    }

    void vision_encoder::project_gemma(job& j)
    {
        /*
            Le regroupement de Gemma 4 : la moyenne de chaque carré de merge x merge patchs, les carrés ligne
            par ligne, multipliée par la racine de la largeur, normalisée sans poids, puis projetée.
        */
        const long k = merge_, gw = j.pw_ / k, gh = j.ph_ / k, T = gw * gh, D = hidden_, O = out_dim_;
        const float scale = std::sqrt(static_cast<float>(D)) / static_cast<float>(k * k);
        const float eps = eps_;
        m_.resize(static_cast<size_t>(T * D));
        pool_.parallel_for(T, [&](long t, int) {
            const long oy = t / gw, ox = t % gw;
            float* dst = m_.data() + t * D;
            std::fill(dst, dst + D, 0.0f);
            for (long dy = 0; dy < k; ++dy)
                for (long dx = 0; dx < k; ++dx)
                    qk_add(dst, j.x_.data() + ((oy * k + dy) * j.pw_ + ox * k + dx) * D, D);
            for (long i = 0; i < D; ++i) dst[i] *= scale;
            const float s = 1.0f / std::sqrt(qk_dot_f32(dst, dst, D) / static_cast<float>(D) + eps);
            for (long i = 0; i < D; ++i) dst[i] *= s;
        });
        product(input_proj_, m_.data(), T, D, j.out_.data(), O);
    }

    bool vision_encoder::advance(job& j, double seconds)
    {
        const auto t0 = std::chrono::steady_clock::now();
        const long layers = static_cast<long>(blocks_.size());
        while (!j.done_)
        {
            if (j.next_ < layers && kind_ == family::gemma4)
            {
                run_block_gemma(j, blocks_[static_cast<size_t>(j.next_)]);
                ++j.next_;
            }
            else if (j.next_ < layers)
            {
                run_block(j, blocks_[static_cast<size_t>(j.next_)]);
                for (size_t k = 0; k < deepstack_.size(); ++k)
                    if (deepstack_[k].after == j.next_)
                        run_merger(j, deepstack_[k], true, out_dim_ * static_cast<long>(1 + k));
                ++j.next_;
            }
            else if (kind_ == family::llava_next)
            {
                project_view(j);
                ++j.view_;
                if (j.view_ < j.views_.size())
                {
                    embed_patches(j, j.views_[j.view_]);
                    j.next_ = 0;
                }
                else
                {
                    pack_views(j);
                    j.done_ = true;
                    aligned_floats().swap(j.x_);
                    std::vector<float>().swap(j.features_);
                    std::vector<rgb_image>().swap(j.views_);
                }
            }
            else if (kind_ == family::gemma4)
            {
                project_gemma(j);
                j.done_ = true;
                aligned_floats().swap(j.x_);
                std::vector<float>().swap(j.cos_);
                std::vector<float>().swap(j.sin_);
            }
            else if (kind_ == family::pixtral)
            {
                project_pixtral(j);
                j.done_ = true;
                aligned_floats().swap(j.x_);
                std::vector<float>().swap(j.cos_);
                std::vector<float>().swap(j.sin_);
            }
            else
            {
                run_merger(j, final_, false, 0);
                j.done_ = true;
                aligned_floats().swap(j.x_);
                std::vector<float>().swap(j.cos_);
                std::vector<float>().swap(j.sin_);
            }
            if (seconds_since(t0) >= seconds) break;
        }
        j.seconds_ += seconds_since(t0);
        return j.done_;
    }

    void vision_encoder::encode(const rgb_image& image, const image_plan& plan, std::vector<float>& out)
    {
        std::unique_ptr<job> j = start(image, plan);
        while (!advance(*j, std::numeric_limits<double>::infinity()))
        {
        }
        out.swap(j->output());
        release_work();
    }

    void vision_encoder::release_work()
    {
        // Les copies des poids sur le périphérique peuvent le quitter au profit du modèle jusqu'à l'image
        // suivante.
        if (offload_) offload_->set_busy(false);
        aligned_floats().swap(sum_);
        aligned_floats().swap(xn_);
        aligned_floats().swap(qkv_);
        aligned_floats().swap(q_);
        aligned_floats().swap(kt_);
        aligned_floats().swap(v_);
        aligned_floats().swap(ctx_);
        aligned_floats().swap(h_);
        aligned_floats().swap(u_);
        aligned_floats().swap(m_);
        aligned_floats().swap(cl_);
        aligned_floats().swap(o_);
    }

}
