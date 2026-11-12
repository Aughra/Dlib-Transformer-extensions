// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_MODEL_CONFIG_H_
#define ALBIREO_ENGINE_MODEL_CONFIG_H_

#include <string>
#include <vector>

#include "model_file.h"

namespace albireo
{

    enum class rope_style
    {
        norm,   // paires consécutives (x[2i], x[2i+1]), la convention llama des fichiers GGUF
        neox    // moitiés (x[i], x[i + d/2]), la convention de qwen, gemma et phi
    };

    struct model_config
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La géométrie et les conventions numériques d'un modèle à décodeur seul,
                lues dans les métadonnées de son fichier GGUF.  Tout ce dont dépend la
                passe avant est résolu ici une fois, de sorte que les runtimes se
                branchent sur des champs plutôt que sur des noms d'architecture.

            ARCHITECTURES PRISES EN CHARGE
                llama, mistral et mistral3 (dense, GQA, SwiGLU, embeddings liés ou
                séparés, facteurs de fréquence de llama 3, YaRN et la mise à l'échelle
                des requêtes en contexte long de Ministral 3), qwen2 (avec biais QKV),
                qwen3 (avec normalisation QK), qwen3moe, mixtral et granitemoe (mélanges
                d'experts), granite (avec ses échelles d'embedding, de résidu,
                d'attention et de logits), qwen3vl et qwen3vlmoe (les décodeurs de
                Qwen3-VL : qwen3 et qwen3moe avec positions rotatives multimodales et
                entrées DeepStack, voir rope_sections et n_deepstack), phi3 (Phi-3 et
                Phi-4-mini : une matrice pour les requêtes, les clés et les valeurs, une
                pour les projections gate et up, positions rotatives sur les premières
                dimensions des têtes, LongRoPE ; la fenêtre glissante que déclarent les
                fichiers n'est pas appliquée, comme dans llama.cpp).

            MODÈLES D'EMBEDDING
                Un décodeur qui déclare <arch>.pooling_type calcule des vecteurs au lieu
                de texte : sa dernière étape renvoie l'état caché normalisé plutôt que les
                logits.  Les décodeurs causaux prennent l'état de leur dernier token,
                celui qui a lu tout le texte (type de pooling 3).
        !*/

        std::string arch;               // general.architecture
        std::string name;               // general.name, cleaned
        long n_layers = 0;
        long n_heads = 0;
        long n_kv_heads = 0;
        long d_model = 0;
        long d_ffn = 0;
        long head_dim = 0;              // clés et requêtes
        long head_dim_v = 0;            // valeurs
        long vocab = 0;
        long ctx_train = 0;             // longueur de contexte avec laquelle le modèle a été entraîné
        double rms_eps = 1e-5;

        // Embedding rotatif.
        rope_style rope = rope_style::norm;
        long rope_dims = 0;             // premières dimensions de chaque tête qui tournent
        double rope_base = 10000.0;
        double rope_freq_scale = 1.0;   // 1 / facteur de mise à l'échelle linéaire
        std::vector<float> rope_freq_factors;   // diviseurs par fréquence de llama 3
        bool yarn = false;
        double yarn_orig_ctx = 0;
        double yarn_beta_fast = 32.0;
        double yarn_beta_slow = 1.0;
        double yarn_ext_factor = 1.0;
        double yarn_attn_factor = 1.0;  // correction d'amplitude appliquée à cos et sin

        /*
            LongRoPE (phi3) : diviseurs par fréquence pour un contexte d'au plus
            rope_orig_ctx positions et pour un contexte plus long.  rope_freq_factors
            reçoit le jeu que select_rope_factors() choisit pour le contexte d'une séquence.
        */
        std::vector<float> rope_factors_short, rope_factors_long;
        long rope_orig_ctx = 0;
        double rope_attn_factor = 1.0;  // <arch>.rope.scaling.attn_factor, appliqué à cos et sin

        // Projections fusionnées (phi3) : attn_qkv contient les lignes des requêtes, puis celles
        // des clés et des valeurs ; ffn_up contient les lignes de gate, puis celles de la
        // projection up.
        bool fused_qkv = false;
        bool fused_gate_up = false;

        // Mise à l'échelle des requêtes en contexte long :
        // q *= 1 + attn_temp_scale * log(1 + floor(pos / attn_temp_length)).
        double attn_temp_scale = 0.0;
        double attn_temp_length = 8192.0;

        // Parties facultatives.
        bool qkv_bias = false;
        bool qk_norm = false;
        bool tied_embeddings = false;

        // Facteurs d'échelle globaux (granite) ; valeurs neutres sinon.
        double embedding_scale = 1.0;
        double residual_scale = 1.0;
        double attention_scale = 0.0;   // 0 : 1/sqrt(head_dim)
        double logit_scale = 1.0;       // les logits sont divisés par lui

        // Mélange d'experts.
        long n_experts = 0;
        long n_experts_used = 0;
        long d_ffn_expert = 0;
        bool expert_weights_norm = true;   // renormaliser les poids de routage sélectionnés

        // Tokenizer et format de chat, tels que déclarés.
        std::string chat_template;
        std::string tokenizer_pre;

        // Pooling d'un modèle d'embedding : 0 aucun (un modèle de langage), 1 moyenne,
        // 2 premier token, 3 dernier token, 4 rang.
        int pooling = 0;

        /*
            Positions rotatives multimodales (qwen3vl) : un token a trois composantes de
            position, temps, ligne et colonne, et la paire rotative j d'une tête prend son
            angle de la ligne quand j % 3 == 1 et j < 3 * rope_sections[1], de la colonne
            quand j % 3 == 2 et j < 3 * rope_sections[2], et du temps sinon.  Un token de
            texte a la même valeur dans ses trois composantes, ce qui donne la rotation d'un
            modèle à une position par token.  Vide pour les autres modèles.
        */
        std::vector<int> rope_sections;

        // DeepStack (qwen3vl) : un token d'image porte d_model * (1 + n_deepstack) flottants
        // d'entrée ; les d_model premiers remplacent son embedding, et la tranche k est
        // ajoutée à son état caché après la couche k.
        long n_deepstack = 0;

        /*
            Couches linéaires (qwen35, fork pour macOS).  Avec full_attention_interval > 0, la couche
            li est une couche d'attention quand (li + 1) % full_attention_interval == 0, et une couche
            « Gated DeltaNet » sinon : une attention linéaire qui garde, par séquence, un état de taille
            fixe (la fenêtre de sa convolution causale et une matrice ssm_head_dim x ssm_head_dim par
            tête de valeurs) au lieu de clés et valeurs.  Ses dimensions : une convolution de
            ssm_conv_kernel positions, ssm_k_heads têtes de requêtes et de clés et ssm_v_heads têtes
            de valeurs, toutes de ssm_head_dim.  La tête de valeurs h lit la tête de clés
            h % ssm_k_heads.  Un état ne revient pas en arrière : une séquence ne se reprend qu'à sa
            longueur, ou depuis le début (runtime::resumable()).
            Les couches d'attention de ces modèles ont une porte de sortie (attn_output_gate) :
            attn_q donne, tête par tête, la requête puis sa porte, et la sortie de l'attention est
            multipliée par sigmoid(porte) avant attn_output.  La norme d'avant le bloc feed-forward
            s'y appelle post_attention_norm.
        */
        long full_attention_interval = 0;
        long ssm_conv_kernel = 0;
        long ssm_k_heads = 0;
        long ssm_v_heads = 0;
        long ssm_head_dim = 0;
        bool attn_output_gate = false;
        // Les couches de prédiction de plusieurs tokens (MTP) rangées après les autres, non exécutées.
        long n_nextn = 0;

        bool is_moe() const { return n_experts > 0; }
        bool multimodal_rope() const { return !rope_sections.empty(); }
        long input_width() const { return d_model * (1 + n_deepstack); }
        bool is_embedding() const { return pooling != 0; }
        long kv_dim() const { return n_kv_heads * head_dim; }
        long q_dim() const { return n_heads * head_dim; }
        long v_dim() const { return n_kv_heads * head_dim_v; }
        bool recurrent() const { return full_attention_interval > 0; }
        bool linear_layer(long li) const
        {
            return full_attention_interval > 0 && (li + 1) % full_attention_interval != 0;
        }
        long n_attention_layers() const
        {
            long n = 0;
            for (long li = 0; li < n_layers; ++li) n += linear_layer(li) ? 0 : 1;
            return n;
        }
        // Les canaux de la convolution d'une couche linéaire : requêtes, clés, puis valeurs.
        long ssm_conv_dim() const { return (2 * ssm_k_heads + ssm_v_heads) * ssm_head_dim; }
        long ssm_value_dim() const { return ssm_v_heads * ssm_head_dim; }
        // Les floats de l'état d'une séquence pour une couche linéaire : la fenêtre de la convolution, puis
        // une matrice par tête de valeurs.
        long ssm_conv_state() const { return ssm_conv_dim() * (ssm_conv_kernel - 1); }
        long ssm_state_floats() const { return ssm_conv_state() + ssm_v_heads * ssm_head_dim * ssm_head_dim; }
    };

    // Lit la configuration d'un modèle, ou lève une exception donnant la raison pour
    // laquelle il ne peut pas être servi.
    model_config read_model_config(const model_file& f);

    /*!
        ensures
            - Quand c contient des diviseurs LongRoPE, fait de c.rope_freq_factors les
              diviseurs longs si une séquence peut contenir plus de c.rope_orig_ctx
              positions, et les courts sinon, comme llama.cpp les choisit d'après le
              contexte d'une séquence.  read_model_config() les choisit pour le contexte
              avec lequel le modèle a été entraîné.
    !*/
    void select_rope_factors(model_config& c, long context);

    // Une ligne décrivant une configuration, pour les logs.
    std::string describe(const model_config& c);

}

#endif // ALBIREO_ENGINE_MODEL_CONFIG_H_
