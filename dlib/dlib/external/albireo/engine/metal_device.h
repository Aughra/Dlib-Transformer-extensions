// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_METAL_DEVICE_H_
#define ALBIREO_ENGINE_METAL_DEVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include <dlib/data_io/gguf_reader.h>

#include "../common/json.h"

namespace albireo
{

    /*!
        Un tampon du GPU dans la mémoire qu'il partage avec le processeur : tous deux lisent et
        écrivent ses octets (contents()).  Il possède sa mémoire, ou enveloppe une mémoire qu'un
        autre objet possède (le mappage d'un fichier de modèle, la région d'un kv_pool), qui doit
        lui survivre.  Les copies partagent le même tampon.
    !*/
    class metal_buffer
    {
    public:
        metal_buffer() {}
        bool valid() const { return impl_ != nullptr; }
        void* contents() const { return contents_; }
        size_t bytes() const { return bytes_; }
        struct impl;
        std::shared_ptr<impl> impl_;
        void* contents_ = nullptr;
        size_t bytes_ = 0;
    };

    // L'endroit d'un tampon où commence un opérande d'un noyau.
    struct metal_view
    {
        const metal_buffer* buffer = nullptr;
        size_t offset = 0;
        metal_view() {}
        metal_view(const metal_buffer& b, size_t at = 0) : buffer(&b), offset(at) {}
    };

    class metal_device
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le GPU du Mac, via Metal (portage macOS) : sa file de commandes et les noyaux
                de metal_kernels.metal, que macOS compile à l'ouverture du périphérique.
                Le GPU partage la mémoire du processeur : il lit les poids dans le mappage du
                fichier de modèle et les clés et valeurs dans la région de leur pool, là où
                ils se trouvent, sans copie.  L'interface est en C++ simple, pour que seul
                metal_device.mm voie de l'Objective-C.
        !*/
    public:
        /*!
            ensures
                - Ouvre le GPU et compile ses noyaux, ou renvoie null avec why renseigné : pas de
                  GPU, ou des noyaux qui ne compilent pas.
        !*/
        static std::unique_ptr<metal_device> open(std::string& why);

        ~metal_device();
        metal_device(const metal_device&) = delete;
        metal_device& operator=(const metal_device&) = delete;

        // Le nom du GPU, "Apple M2" par exemple.
        const std::string& name() const;
        // Le plus long tampon que le GPU accepte, et la mémoire que Metal lui conseille d'utiliser au plus.
        uint64_t max_buffer_bytes() const;
        uint64_t working_set_bytes() const;
        // Les noyaux compilés, par nom.
        int kernel_count() const;

        // Un tampon d'octets en mémoire partagée, mis à zéro ; invalide quand le GPU le refuse.
        metal_buffer allocate(size_t bytes);

        /*!
            ensures
                - Renvoie un tampon sur les pages entières qui contiennent [data, data + bytes),
                  sans copie, et met dans offset l'endroit où data s'y trouve ; invalide quand le
                  GPU le refuse (plus long que max_buffer_bytes(), par exemple).  La mémoire doit
                  rester mappée tant que le tampon existe.
        !*/
        metal_buffer wrap(const void* data, size_t bytes, size_t& offset);

        // Le nombre maximal de tokens d'une passe (MAX_TOKENS de metal_kernels.metal).
        static const unsigned max_tokens = 8;

        // Les positions d'une partie de l'attention (attention()).
        static const unsigned attention_part = 256;

        /*!
            WHAT THIS OBJECT REPRESENTS
                Les commandes d'une passe : les noyaux encodés dans l'ordre, chacun voyant ce
                que les précédents ont écrit, puis exécutés par finish(), qui attend le GPU.
                Un seul thread utilise un objet à la fois.
        !*/
        class commands
        {
        public:
            ~commands();
            commands(const commands&) = delete;
            commands& operator=(const commands&) = delete;

            /*!
                y[t * y_stride + r] = alpha * sum_i W[r][i] x[t * x_stride + i] + beta * y[...],
                r < rows, t < tokens (au plus max_tokens), W dans la disposition GGUF de type, des
                lignes de cols valeurs.  Renvoie false, sans rien encoder, pour un type sans noyau
                (Q4_K, Q5_K, Q6_K et Q8_0 en ont un).
            !*/
            bool matvec(dlib::ggml_type type, metal_view W, long rows, long cols, metal_view x, unsigned tokens,
                unsigned x_stride, metal_view y, unsigned y_stride, float alpha = 1.0f, float beta = 0.0f);

            // Deux produits de matrices de même type et de mêmes colonnes sur les mêmes activations, W dans y
            // et W2 dans y2, en un seul dispatch (alpha 1, beta 0) ; rows un multiple de 16.
            bool matvec2(dlib::ggml_type type, metal_view W, long rows, metal_view W2, long rows2, long cols,
                metal_view x, unsigned tokens, unsigned x_stride, metal_view y, unsigned y_stride, metal_view y2,
                unsigned y2_stride);

            // La ligne r de W (rows lignes de cols valeurs, type GGUF Q4_K ou Q6_K) développée en floats
            // dans out + r * out_stride, comme le fait qk_dequantize_row() ; false pour un autre type.
            bool dequantize(dlib::ggml_type type, metal_view W, long rows, long cols, metal_view out,
                unsigned out_stride);

            /*!
                Y[t * y_stride + n] = alpha * sum_k X[t * x_stride + k] Wf[n * w_stride + k] + beta * Y[...],
                t < rows, n < cols, k < inner, en floats simple précision, via Metal Performance
                Shaders : les produits des prompts, Wf étant les poids que dequantize() a développés.
            !*/
            void gemm(metal_view X, unsigned rows, unsigned inner, unsigned x_stride, metal_view Wf, unsigned cols,
                unsigned w_stride, metal_view Y, unsigned y_stride, float alpha = 1.0f, float beta = 0.0f);

            // y[r] = x[r] * gamma / sqrt(moyenne de x[r]^2 + eps), des lignes de width valeurs (x peut être y).
            void rms_norm(metal_view x, metal_view gamma, metal_view y, unsigned rows, unsigned width,
                unsigned x_stride, unsigned y_stride, float eps);

            // x[t * stride + i] += b[i], i < n, t < rows.
            void add_bias(metal_view x, metal_view b, unsigned n, unsigned stride, unsigned rows);

            // gate[i] = silu(gate[i]) * up[i], i < n.
            void silu_mul(metal_view gate, metal_view up, unsigned n);

            /*!
                Les positions rotatives de tokens lignes de heads têtes de hd valeurs (stride floats
                par token) : les 2 * half premières valeurs de chaque tête tournent de
                cs[t * cs_stride + i] (cosinus) et cs[t * cs_stride + half + i] (sinus), par paires
                (i, i + half) quand neox, (2i, 2i + 1) sinon.
            !*/
            void rope(metal_view x, metal_view cs, unsigned tokens, unsigned heads, unsigned hd, unsigned half,
                bool neox, unsigned stride, unsigned cs_stride);

            /*!
                La disposition des clés et valeurs d'une couche dans la région d'un kv_pool :
                le bloc b à b * block_stride, les clés de la tête h à k_slab + h * head_stride, les
                valeurs à v_slab + h * head_stride, en entiers 8 bits avec leurs échelles (q8) ou
                en demi-flottants.
            !*/
            struct kv_layout
            {
                unsigned hd = 0;
                unsigned heads = 0;
                unsigned positions = 0;
                bool q8 = false;
                uint64_t block_stride = 0;
                uint64_t k_slab = 0;
                uint64_t v_slab = 0;
                uint64_t head_stride = 0;
            };

            // Les clés et valeurs de tokens tokens (kv_stride floats par token) dans la ligne rows[t] du
            // bloc blocks[t] (int32), les clés tournées en q8 (qk_rotate_row).
            void kv_store(metal_view k, metal_view v, unsigned kv_stride, metal_view region, metal_view blocks,
                metal_view rows, unsigned tokens, const kv_layout& layout);

            // Les requêtes de tokens tokens, heads têtes de hd valeurs, mises à l'échelle, et tournées en q8.
            void prepare_queries(metal_view q, unsigned tokens, unsigned heads, unsigned hd, unsigned q_stride,
                float scale, bool q8);

            /*!
                L'attention de tokens tokens : le token t, dont les requêtes (préparées) se trouvent à
                q + t * q_stride, voit les lengths[t] premières positions de sa séquence, dont les
                blocs sont blocks[t * table_stride + j] ; rep têtes de requête partagent chaque tête
                de clés et valeurs.  Écrit les sorties à ctx + t * q_stride.  partial contient
                tokens * heads * parts * rep * (hd + 2) floats, parts couvrant la plus grande
                longueur par tranches de part_len positions ; avec une seule partie, rien ne va
                dans partial.
            !*/
            void attention(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
                unsigned table_stride, unsigned tokens, unsigned rep, unsigned parts, unsigned q_stride,
                metal_view partial, metal_view ctx, const kv_layout& layout, unsigned part_len = attention_part);

            /*!
                Les couches Gated DeltaNet de qwen35 (fork pour macOS), comme
                cpu_runtime::linear_attention() les calcule (metal_kernels.metal) : mix contient,
                par token, les C canaux de la convolution (requêtes, clés, valeurs) ; states,
                l'état de chaque séquence à seq * slot_floats floats, celui de la couche à
                layer_offset (la fenêtre de la convolution, conv_floats floats, puis une matrice
                S x S par tête de valeurs, rangée par clés) ; segments, des triplets int32
                (séquence, premier token, nombre de tokens).
            !*/
            struct gdn_layout
            {
                unsigned C = 0, K = 0, Hk = 0, Hv = 0, S = 0;
                unsigned slot_floats = 0, layer_offset = 0, conv_floats = 0;
                float eps = 0.0f;
            };
            // Convolution causale et SiLU de chaque canal, la fenêtre de chaque séquence mise à jour.
            void gdn_conv(metal_view mix, metal_view weights, metal_view states, metal_view segments,
                unsigned n_segments, const gdn_layout& l);
            // Normes L2 des têtes de requêtes et de clés de tokens tokens.
            void gdn_l2norm(metal_view mix, unsigned tokens, const gdn_layout& l);
            // alpha devient l'oubli softplus(alpha + dt_bias) * ssm_a, beta la force sigmoid(beta).
            void gdn_gates(metal_view alpha, metal_view beta, metal_view dt_bias, metal_view ssm_a, unsigned tokens,
                const gdn_layout& l);
            // La règle delta de chaque tête de valeurs de chaque segment, ses sorties dans out (Hv * S par token).
            void gdn_delta(metal_view mix, metal_view alpha, metal_view beta, metal_view states, metal_view segments,
                unsigned n_segments, metal_view out, const gdn_layout& l);
            // out = norme RMS de chaque tête * weight * SiLU(z).
            void gdn_norm(metal_view out, metal_view z, metal_view weight, unsigned tokens, const gdn_layout& l);
            // La requête et la porte de l'attention de qwen35, rangées tête par tête dans qg (2 * heads * hd par token).
            void split_gate(metal_view qg, metal_view q, metal_view gate, unsigned tokens, unsigned heads, unsigned hd);
            // x[i] *= sigmoid(gate[i]), i < n.
            void sigmoid_mul(metal_view x, metal_view gate, unsigned n);

            /*!
                Le mélange d'experts (fork pour macOS), comme cpu_runtime::moe_ffn() :
                matvec_id calcule, pour chaque case s < slots, la ligne s de y avec l'expert
                ids[s] (int32) d'un tenseur d'experts dont W est le premier et expert_bytes le pas,
                sur la ligne s / x_div de x ; false pour un type sans noyau.
            !*/
            bool matvec_id(dlib::ggml_type type, metal_view W, long rows, long cols, uint64_t expert_bytes,
                metal_view x, unsigned x_stride, unsigned x_div, metal_view ids, unsigned slots, metal_view y,
                unsigned y_stride);
            // y[t * y_stride + r] = sum_i W[r][i] x[t * x_stride + i], W en floats (le routeur).
            void matvec_f32(metal_view W, unsigned rows, unsigned cols, metal_view x, unsigned tokens,
                unsigned x_stride, metal_view y, unsigned y_stride);
            // Softmax des logits (experts par token), les top_k plus probables dans ids et weights.
            void moe_route(metal_view logits, unsigned experts, unsigned top_k, bool norm, metal_view ids,
                metal_view weights, unsigned tokens);
            // g[t] = sigmoid(x[t] . w), x de d valeurs par token.
            void shared_gate(metal_view x, metal_view w, metal_view g, unsigned d, unsigned tokens);
            // x[t] += scale * (sum_k weights[t][k] y[t * top_k + k] + g[t] shared[t]), shared seulement s'il est donné.
            void moe_combine(metal_view x, metal_view y, metal_view weights, const metal_view* shared, metal_view g,
                unsigned d, unsigned top_k, unsigned tokens, float scale);

            /*!
                L'attention des prompts en tuiles (fork pour macOS) : tiles contient n_tiles paires
                int32 (premier token, nombre de tokens), des tokens consécutifs d'une même séquence,
                tile_tokens(rep, hd) au plus par tuile ; le reste comme attention(), sans parties.
            !*/
            void attention_tiles(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
                unsigned table_stride, metal_view tiles, unsigned n_tiles, unsigned rep, unsigned q_stride,
                metal_view ctx, const kv_layout& layout);

            /*!
                ensures
                    - Exécute les commandes et les attend ; lève std::runtime_error quand le
                      GPU les fait échouer.
            !*/
            void finish();

            // Le temps que le GPU a mis pour les commandes, en millisecondes, une fois finish() revenu.
            double gpu_milliseconds() const;

        private:
            friend class metal_device;
            commands(metal_device& device);
            struct impl;
            std::unique_ptr<impl> impl_;
        };

        // Une nouvelle passe de commandes.
        std::unique_ptr<commands> begin();

        // Les tokens d'une tuile d'attention_tiles() pour rep têtes de requête par tête de clés et des têtes de hd valeurs.
        static unsigned tile_tokens(unsigned rep, unsigned hd);

    private:
        metal_device();
        struct impl;
        std::unique_ptr<impl> impl_;
    };

    // Le GPU tel que describe() le liste, {"type": "gpu", "name", "memory", "max_buffer"}, sans
    // compiler les noyaux ; null quand le Mac n'a pas de GPU que Metal atteint.
    json metal_describe();

}

#endif // ALBIREO_ENGINE_METAL_DEVICE_H_
