// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_CPU_RUNTIME_H_
#define ALBIREO_ENGINE_CPU_RUNTIME_H_

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "kv_cache.h"
#include "model_file.h"
#include "offload.h"

namespace albireo
{
    class metal_device;
}
#include "qkernels.h"
#include "runtime.h"
#include "thread_pool.h"

namespace albireo
{

    class cpu_runtime : public model_runtime
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La passe avant d'un modèle décodeur seul sur le processeur.

                Les poids restent dans le mappage mémoire du fichier GGUF, sous leur forme
                quantifiée, et chaque produit passe par les noyaux de qkernels.h.  Les
                activations sont des floats simple précision ; les clés et valeurs sont
                stockées dans un kv_pool, en demi-précision ou en entiers 8 bits par groupes de
                kv_q8_group valeurs (kv_format), une séquence paginée par emplacement de requête.

                Le chemin numérique suit les implémentations de référence des familles prises
                en charge : blocs résiduels à pré-normalisation avec des normes RMS, attention
                à requêtes groupées avec positions rotatives (paires consécutives ou moitiés,
                avec des ajustements de fréquence linéaires, YaRN, llama 3 ou LongRoPE), biais
                QKV optionnels et normalisation QK par tête, blocs feed-forward SwiGLU ou
                mélanges d'experts SwiGLU, et les facteurs d'échelle globaux des familles qui
                les déclarent.  Les matrices fusionnées de phi3 (attn_qkv, et ffn_up avec la
                porte) sont utilisées comme plages de lignes, un produit par plage.

                L'attention s'exécute directement sur les clés paginées de chaque séquence, par
                tuiles : une tâche prend les têtes qui partagent une tête de clés et valeurs
                pour jusqu'à tile_rows lignes de tokens consécutifs d'une séquence, et lit le
                cache bloc après bloc avec un maximum et une somme courants (le softmax de
                FlashAttention), si bien que chaque clé et valeur lue en mémoire sert toutes
                les lignes de la tuile.  Quand les tuiles sont moins nombreuses que les
                threads, comme dans la génération d'une seule réponse, les positions de chaque
                tuile sont découpées en plages que des tâches lisent pour toutes les têtes du
                groupe, et les sommes partielles des plages sont fusionnées (le découpage de
                FlashDecoding).

                Avec un matmul_offload, les produits avec les matrices de poids passent par
                lui, et il décide, produit par produit, si un périphérique les calcule.  Quand
                l'offload calcule des couches entières (layer_pass), le runtime d'un modèle
                dense lui confie les couches de ses passes, une fois que les logits de passes
                du périphérique sur probe_tokens tokens, et sur les tokens à partir desquels il
                développe les poids par tuiles, ont concordé avec ceux du processeur seul : les
                états cachés restent alors sur le périphérique, et le processeur garde
                l'attention.  Les lots avec des tranches DeepStack gardent les produits, de
                même que chaque passe après un échec du périphérique.

                Un lot qui donne les positions rotatives de ses tokens (forward_batch::
                positions) fait tourner chaque paire d'une tête selon la composante de position
                que nomme sa section (model_config::rope_sections), et les lignes qu'il donne
                comme embeddings les prennent à la place de la table d'embeddings, leurs
                tranches DeepStack étant ajoutées à leurs états cachés après les premières
                couches.  Un lot sans elles exécute le chemin des modèles de texte sans
                changement.
        !*/
    public:
        cpu_runtime(std::shared_ptr<model_file> file, const model_config& config, thread_pool& pool,
            size_t kv_budget_bytes, long kv_block_positions = 256, kv_format kv_type = kv_format::f16);
        ~cpu_runtime() override;

        const model_config& config() const override { return cfg_; }

        int create_sequence() override;
        void release_sequence(int seq) override;
        long sequence_length(int seq) const override;
        void truncate_sequence(int seq, long length) override;
        bool reserve_sequence(int seq, long length) override;
        bool copy_sequence(int src, int dst, long length) override;

        void forward(const forward_batch& batch, std::vector<float>& logits) override;

        size_t kv_bytes_used() const override { return kv_.used_blocks() * kv_.block_bytes(); }
        size_t kv_bytes_budget() const override { return kv_.max_blocks() * kv_.block_bytes(); }
        size_t kv_bytes_per_token() const override
        {
            return 2 * static_cast<size_t>(cfg_.n_layers) * cfg_.n_kv_heads * kv_row_bytes(kv_.format(), cfg_.head_dim);
        }
        kv_format kv_type() const { return kv_.format(); }
        // Le contrôle de la mémoire du système avant chaque nouveau bloc de clés et valeurs (kv_pool::set_guard).
        void set_kv_guard(std::function<bool(size_t, size_t)> guard) { kv_.set_guard(std::move(guard)); }
        void trim_memory() override { kv_.trim(); }
        void set_busy(bool busy) override
        {
            if (offload_) offload_->set_busy(busy);
        }
        size_t weight_bytes() const override { return static_cast<size_t>(file_->size()); }

        std::string description() const override;

        // Confie les produits, ou les couches, des passes suivantes à un périphérique.
        void set_offload(std::unique_ptr<matmul_offload> offload);

        /*!
            requires
                - device survit à cet objet.
            ensures
                - Confie au GPU du Mac, entières, les passes d'au plus metal_device::max_tokens
                  tokens d'un modèle dense (les étapes de génération) : une commande par passe
                  (cpu_runtime_metal.cpp).  La première de ces passes vérifie le GPU par rapport
                  au processeur sur des étapes de sonde ; log reçoit le résultat, et un GPU qui
                  n'est pas d'accord, ou qui échoue plus tard, laisse les passes au processeur.
                  Sans effet pour un modèle que les noyaux du GPU ne couvrent pas.
        !*/
        void set_gpu(metal_device& device, std::function<void(int, const std::string&)> log);

        /*
            Les prompts que lit le GPU : les passes de min_tokens tokens ou plus (9 au moins, les étapes
            d'au plus metal_device::max_tokens tokens lui allant de toute façon) dont le contexte finit en
            deçà de max_positions positions ; max_positions à 0 laisse chaque prompt au processeur.
            Avant set_gpu().
        */
        void set_gpu_prompts(long min_tokens, long max_positions)
        {
            gpu_prompt_min_ = std::max<long>(9, min_tokens);
            gpu_prompt_context_ = std::max<long>(0, max_positions);
        }

        // Fait le contrôle du GPU maintenant, au chargement du modèle, plutôt qu'à sa première passe.
        void check_gpu() { if (gpu_) gpu_ready(); }

        // Ce que font les passes du modèle avec le GPU, pour l'état du service.
        struct gpu_usage
        {
            std::string state = "off";  // "on", "checking" (avant son contrôle) ou "off"
            std::string reason;         // pourquoi "off", quand un modèle ou un contrôle l'a refusé
            size_t bytes = 0;           // ses tampons ; les poids et les clés et valeurs sont partagés, non comptés
        };
        // Sûr pendant que les passes s'exécutent sur un autre thread.
        gpu_usage gpu_use() const;

        /*!
            requires
                - aucune passe avant n'a été exécutée, ou les diviseurs rotatifs restent les mêmes.
            ensures
                - Choisit les diviseurs rotatifs d'un modèle avec LongRoPE pour des séquences
                  d'au plus positions tokens (select_rope_factors()).
        !*/
        void set_context(long positions);

        const std::vector<float>& hidden() const override { return hidden_out_; }

    private:
        struct layer
        {
            std::vector<float> attn_norm, ffn_norm, q_norm, k_norm, bq, bk, bv;
            weight_matrix wq, wk, wv, wo;
            weight_matrix w_gate, w_up, w_down;
            weight_matrix router;
            std::vector<weight_matrix> e_gate, e_up, e_down;
        };

        // Le nombre maximal de lignes de requêtes d'une tuile d'attention : les scores d'une tuile sur un
        // bloc du cache restent dans quelques dizaines de kilo-octets.
        static const long tile_rows = 64;

        // Le nombre minimal de positions d'une plage du découpage de l'attention (attention()).
        static const long part_positions = 256;

        // Les tokens des passes qui comparent les couches du périphérique au processeur.
        static const long probe_tokens = 8;

        // Les échecs consécutifs par manque de mémoire du périphérique après lesquels les couches restent sur
        // le processeur.
        static const int max_layer_pass_memory_failures = 4;

        // Les passes qui suivent un contrôle de la layer pass reporté par manque de place, avant le suivant.
        static const int layer_pass_check_interval = 64;

        // Tokens consécutifs d'un segment du lot, qui font leur attention ensemble.
        struct attention_tile
        {
            int sequence;
            long start;     // premier token dans le lot
            long count;
        };

        // Les tampons d'un thread de l'attention.
        struct attention_work
        {
            aligned_floats q, s, o, max, sum, scratch;
        };

        void rms_norm_rows(const float* x, const float* gamma, float* y, long rows, long width, float eps);
        void ensure_rope(long positions);
        void apply_rope(float* rows, long n_rows, long n_heads, const std::vector<long>& positions);
        // La rotation des tokens d'un lot qui donne leurs positions : les cosinus et sinus de
        // chaque paire de chaque token, rassemblés une fois par passe (prepare_given_positions),
        // puis appliqués aux requêtes et aux clés de chaque couche.
        void prepare_given_positions(const forward_batch& batch, long T);
        void apply_given_rope(float* rows, long n_rows, long n_heads);
        // Les lignes du lot qui viennent d'un encodeur : leurs embeddings, et la tranche d'une
        // couche DeepStack ajoutée après celle-ci.
        void check_given_embeddings(const forward_batch& batch, long T) const;
        void add_given_embeddings(const forward_batch& batch, long slice);
        // Les clés et valeurs des tokens d'un lot, écrites dans les blocs de leurs séquences.
        void store_keys_values(long layer, const forward_batch& batch);
        void attention(long layer, const forward_batch& batch, const std::vector<long>& positions);
        void moe_ffn(const layer& L, long n_tokens);
        void embed(const std::vector<int>& tokens);
        /*
            La passe avant, avec les couches sur le périphérique via layer_pass_ quand on_device vaut
            true ; false quand le périphérique n'a pas pris la passe, qui n'a alors rien fait.
        */
        bool run(const forward_batch& batch, std::vector<float>& logits, bool on_device);
        bool takes_layer_pass(const forward_batch& batch) const;
        int probe_layer_pass(long n, std::string& detail);
        void check_layer_pass();
        void drop_layer_pass(const std::string& why);
        // Y = X W^T via l'offload quand il y en a un, avec les noyaux de qkernels.h
        // sinon.
        void product(const weight_matrix& W, const float* X, long n, long x_stride, float* Y, long y_stride,
            qk_path path = qk_path::automatic);

        // Les passes sur le GPU du Mac (cpu_runtime_metal.cpp).
        struct gpu_state;
        // Partagé, pas unique : le constructeur et le destructeur se trouvent là où gpu_state est incomplet.
        std::shared_ptr<gpu_state> gpu_;
        bool takes_gpu(const forward_batch& batch) const;
        void grow_gpu(size_t tokens);
        bool gpu_ready();
        void run_gpu(const forward_batch& batch, std::vector<float>& logits);
        void drop_gpu(const std::string& why);
        // gpu_use() lit gpu_report_, que le thread des passes met à jour après chaque changement de gpu_
        // (note_gpu()).
        void note_gpu(const std::string& reason = std::string());
        mutable std::mutex gpu_report_m_;
        gpu_usage gpu_report_;
        // Les produits en floats simple précision, les activations telles que données (qk_path::floats) : la
        // référence du contrôle du GPU, par rapport à laquelle les activations 8 bits différeraient.
        bool float_products_ = false;
        // Les prompts sur le GPU (set_gpu_prompts()) : mesuré sur le M2, le GPU lit un prompt plus vite que le
        // processeur à partir de 24 tokens, et perd au-delà de 2 048 positions.
        long gpu_prompt_min_ = qk_float_product_tokens;
        long gpu_prompt_context_ = 2048;

        std::shared_ptr<model_file> file_;
        model_config cfg_;
        thread_pool& pool_;
        std::unique_ptr<matmul_offload> offload_;
        // Détruit avant offload_, qu'il utilise.
        std::unique_ptr<layer_pass> layer_pass_;
        bool layer_pass_checked_ = false;
        int layer_pass_memory_failures_ = 0;
        int layer_pass_wait_ = 0;
        // Les produits des passes de sonde du processeur ne vont pas à l'offload.
        bool host_only_ = false;
        kv_pool kv_;
        std::vector<std::unique_ptr<kv_sequence>> sequences_;
        std::vector<int> free_sequences_;

        std::vector<layer> layers_;
        weight_matrix token_embd_, output_;
        std::vector<float> output_norm_;

        // Tables rotatives : cos et sin pour chaque position calculée jusqu'ici.
        std::vector<float> rope_cos_, rope_sin_;
        long rope_positions_ = 0;
        // La composante de position de chaque paire rotative (0 temps, 1 ligne, 2 colonne), et les
        // cosinus et sinus des tokens d'un lot qui donne leurs positions.
        std::vector<int> pair_component_;
        std::vector<float> given_cos_, given_sin_;

        // Tampons de travail, réutilisés d'une passe à l'autre.
        aligned_floats x_, xn_, q_, k_, v_, ctx_, tmp_, gate_, up_;
        aligned_floats router_logits_, expert_x_, expert_gate_, expert_up_, expert_y_;
        std::vector<float> hidden_out_, router_probs_;
        std::vector<long> router_order_;
        std::vector<std::vector<std::pair<long, float>>> routed_;
        std::vector<qk_scratch> scratch_;
        std::vector<attention_work> attention_work_;
        std::vector<attention_tile> tiles_;
        // Les sommes partielles des plages d'une attention découpée, et la séquence de chaque token d'un lot.
        aligned_floats partial_;
        std::vector<int> token_sequences_;
        std::vector<long> positions_;
    };

}

#endif // ALBIREO_ENGINE_CPU_RUNTIME_H_
