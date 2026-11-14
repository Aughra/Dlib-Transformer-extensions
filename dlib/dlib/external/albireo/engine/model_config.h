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
                fichiers n'est pas appliquée, comme dans llama.cpp).  Le fork pour macOS
                ajoute qwen35 et qwen35moe (couches linéaires, voir full_attention_interval),
                gemma4 dense (voir gemma4), lfm2 et lfm2moe (convolutions courtes, voir lfm2).

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
        // L'expert partagé de qwen35moe (fork pour macOS) : un bloc feed-forward de d_ffn_shared valeurs que
        // chaque token traverse, sa sortie multipliée par sigmoid(x . ffn_gate_inp_shexp) puis ajoutée à celle
        // des experts routés ; 0 sans lui.
        long d_ffn_shared = 0;

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

        /*
            Gemma 4 (fork pour macOS).  Les couches de swa_layers voient une fenêtre glissante :
            le token en position p lit les positions p - sliding_window + 1 à p, avec des têtes de
            head_dim_swa valeurs tournées sur rope_dims_swa dimensions de base rope_base_swa, sans
            diviseurs ; les autres sont globales, avec head_dim, rope_dims, rope_base et
            rope_freq_factors.  Les n_kv_shared dernières couches n'ont pas de clés et valeurs à
            elles : elles lisent celles de la dernière couche du même type qui en a (kv_source()).
            Chaque couche a sa largeur de bloc feed-forward (ffn_widths, d_ffn étant la plus grande),
            un GELU (approximation tanh) au lieu de SiLU, des normes RMS après l'attention et après le
            bloc feed-forward, une norme RMS sans poids des valeurs, une échelle d'attention de 1, et
            une échelle de sa sortie (layer_output_scale).  Avec d_per_layer > 0, chaque couche reçoit
            en plus une entrée de d_per_layer valeurs par token (les plongements par couche).  Les
            logits passent par softcap * tanh(logits / softcap).
        */
        bool gemma4 = false;

        /*
            LFM2 et LFM2-MoE de Liquid AI (fork pour macOS).  Les couches de conv_layers (celles dont
            head_count_kv vaut 0) remplacent l'attention par une convolution courte : in_proj donne b, c et x
            (d_model valeurs chacun), y = c * conv(b * x), une convolution causale par canal sur conv_kernel
            positions, puis out_proj.  L'état d'une séquence pour une telle couche est la fenêtre de
            conv_kernel - 1 positions de b * x, et ces couches passent par la machinerie des couches linéaires
            (linear_layer(), recurrent(), ssm_state_floats()).  Les autres couches sont des attentions GQA avec
            normes des requêtes et des clés.  LFM2-MoE : les n_dense_lead premières couches ont un bloc
            feed-forward dense, les autres des experts routés par sigmoïde, choisis sur sigmoid(logit) plus le
            biais exp_probs_b, et pesés par sigmoid(logit) renormalisé (expert_sigmoid).
        */
        bool lfm2 = false;
        std::vector<char> conv_layers;
        long conv_kernel = 0;
        long n_dense_lead = 0;
        bool expert_sigmoid = false;

        std::vector<char> swa_layers;
        long sliding_window = 0;
        long head_dim_swa = 0;
        long rope_dims_swa = 0;
        double rope_base_swa = 10000.0;
        long n_kv_shared = 0;
        std::vector<long> ffn_widths;
        long d_per_layer = 0;
        double final_softcap = 0.0;

        bool is_moe() const { return n_experts > 0; }
        bool swa_layer(long li) const { return !swa_layers.empty() && swa_layers[static_cast<size_t>(li)] != 0; }
        long layer_head_dim(long li) const { return swa_layer(li) ? head_dim_swa : head_dim; }
        long layer_q_dim(long li) const { return n_heads * layer_head_dim(li); }
        long layer_kv_dim(long li) const { return n_kv_heads * layer_head_dim(li); }
        long layer_ffn(long li) const { return ffn_widths.empty() ? d_ffn : ffn_widths[static_cast<size_t>(li)]; }
        // Les couches qui ont leurs clés et valeurs, et celle dont une couche lit les clés et valeurs.
        bool owns_kv(long li) const { return li < n_layers - n_kv_shared; }
        long kv_source(long li) const
        {
            if (owns_kv(li)) return li;
            const long first_shared = n_layers - n_kv_shared;
            return first_shared - (swa_layer(li) ? 2 : 1);
        }
        long n_kv_layers() const { return n_attention_layers() - n_kv_shared; }
        bool multimodal_rope() const { return !rope_sections.empty(); }
        long input_width() const { return d_model * (1 + n_deepstack); }
        bool is_embedding() const { return pooling != 0; }
        long kv_dim() const { return n_kv_heads * head_dim; }
        long q_dim() const { return n_heads * head_dim; }
        long v_dim() const { return n_kv_heads * head_dim_v; }
        bool recurrent() const { return full_attention_interval > 0 || !conv_layers.empty(); }
        bool linear_layer(long li) const
        {
            if (!conv_layers.empty()) return conv_layers[static_cast<size_t>(li)] != 0;
            return full_attention_interval > 0 && (li + 1) % full_attention_interval != 0;
        }
        // Les couches à experts (les premières de LFM2-MoE restent denses).
        bool moe_layer(long li) const { return is_moe() && li >= n_dense_lead; }
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
        // une matrice par tête de valeurs (aucune pour la convolution courte de LFM2).
        long ssm_conv_state() const
        {
            return lfm2 ? d_model * (conv_kernel - 1) : ssm_conv_dim() * (ssm_conv_kernel - 1);
        }
        long ssm_state_floats() const
        {
            return lfm2 ? ssm_conv_state() : ssm_conv_state() + ssm_v_heads * ssm_head_dim * ssm_head_dim;
        }
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
