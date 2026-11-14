// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_VISION_ENCODER_H_
#define ALBIREO_ENGINE_VISION_ENCODER_H_

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "image_input.h"
#include "model_file.h"
#include "offload.h"
#include "qkernels.h"
#include "thread_pool.h"

namespace albireo
{

    class vision_encoder
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'encodeur d'images d'un modèle qui lit des images, lu depuis un fichier GGUF
                de projecteur (architecture "clip"), ou depuis les tenseurs de vision et les
                métadonnées clip du fichier du modèle lui-même.  Quatre familles sont lues.

                Qwen3-VL (clip.projector_type "qwen3vl_merger") : un vision transformer dont
                les patchs sont fusionnés par carrés de merge_size() x merge_size() en tokens
                d'entrée du modèle de langage.  L'image, redimensionnée à des multiples de
                factor() pixels, est découpée en patchs de patch_size() pixels, pris carré par
                carré.  L'embedding des patchs est la convolution de la référence avec une image
                fixe, qui répète l'image dans le temps : la somme de ses deux noyaux temporels.
                La table de positions apprise est interpolée bilinéairement, coins alignés, sur
                la grille des patchs, et chaque tête de l'attention fait tourner la première
                moitié de ses paires rotatives selon la ligne du patch et la seconde moitié
                selon sa colonne.  Après les blocs que nomme le fichier, un fusionneur DeepStack
                normalise chaque carré de patchs, puis applique deux couches linéaires autour
                d'une GELU exacte ; le fusionneur final normalise chaque patch, fusionne les
                carrés et applique les mêmes couches.  La sortie d'un token fusionné est la
                sortie du fusionneur final suivie de celles des fusionneurs DeepStack,
                output_width() floats, qu'un décodeur Qwen3-VL lit comme son embedding d'entrée
                et les tranches qu'il ajoute après ses premières couches.

                LLaVA-NeXT (clip.projector_type "mlp" avec clip.vision.image_grid_pinpoints,
                EuroVLM) : un vision transformer SigLIP lit des vues carrées de image_size()
                pixels, chacune donnant un token par patch.  Les vues sont l'image entière
                redimensionnée en carré, puis les tuiles de l'image redimensionnée, son rapport
                d'aspect gardé, dans la résolution des pinpoints qui en perd le moins, des bandes
                noires remplissant le reste.  Chaque vue passe par les blocs jusqu'à la couche
                de caractéristiques du fichier, puis par le projecteur, deux couches linéaires
                autour d'une GELU exacte.  Les tokens de l'image sont ceux de l'image entière,
                puis la grille des tuiles, ligne par ligne, sans les lignes ni les colonnes des
                bandes noires, chaque ligne suivie de l'embedding de retour à la ligne du
                fichier, tels que l'implémentation de référence les range.

                Pixtral (clip.projector_type "pixtral", Ministral 3, Devstral Small 2 et les
                autres modèles Mistral qui lisent des images) : l'image, redimensionnée dans la
                limite du plus long côté du fichier (image_size()), son rapport d'aspect gardé,
                à des multiples de factor() pixels, est découpée en patchs de patch_size()
                pixels.  Chaque tête de l'attention fait tourner ses paires de dimensions
                consécutives, la première moitié selon la ligne du patch et la seconde moitié
                selon sa colonne, aux fréquences paires et impaires de la tête.  Les blocs
                normalisent par leur moyenne quadratique et leur couche feed-forward est une
                porte SiLU.  Le projecteur normalise chaque patch, fusionne les carrés de
                merge_size() x merge_size() patchs par une couche linéaire, puis applique deux
                couches linéaires autour d'une GELU exacte.  Les tokens de l'image sont ses
                tokens fusionnés ligne par ligne, chaque ligne sauf la dernière suivie de
                l'embedding [IMG_BREAK] du fichier, qu'un encodeur Pixtral doit contenir ; le
                décodeur lit le token [IMG_END] après eux.

                Gemma 4 (clip.projector_type "gemma4v", fork pour macOS) : l'image, redimensionnée
                à des multiples de factor() pixels (patch_size() x merge_size(), le noyau du
                regroupement) entre 70 et 140, 280 ou 1120 tokens, est découpée en patchs de patch_size()
                pixels, pris ligne par ligne, ses pixels ramenés dans [-1, 1].  Chaque patch reçoit
                la ligne de sa colonne dans une première table de positions et celle de sa ligne
                dans une seconde.  Les blocs normalisent par la moyenne quadratique avant et après
                l'attention et la couche feed-forward ; requêtes et clés sont normalisées par tête,
                les valeurs sans poids ; chaque tête fait tourner sa première moitié selon la
                colonne du patch et sa seconde moitié selon sa ligne, chaque moitié par paires
                distantes d'un quart de tête, à la base 100 ; l'attention n'est pas mise à
                l'échelle ; la couche feed-forward est une porte GELU (forme tanh).  Chaque produit
                d'un bloc écrête son entrée et sa sortie aux bornes que donne le fichier.  Les
                patchs sont ensuite regroupés par moyenne en carrés de merge_size() x merge_size(),
                ligne par ligne, multipliés par la racine de la largeur, normalisés sans poids et
                projetés à la largeur du modèle de langage.

                Les blocs de Qwen3-VL et de LLaVA-NeXT normalisent avant l'attention et avant
                la couche feed-forward, avec une moyenne et un biais ; l'attention lit chaque
                patch de la vue ; la couche feed-forward active avec la forme tanh de GELU.  Les
                matrices stockées en bfloat16 sont converties en demi-précision au chargement de
                l'encodeur, ce qui garde leurs valeurs : le format demi contient plus de bits de
                mantisse.

                Les produits passent par les noyaux quantifiés de qkernels.h sur les threads du
                moteur (poids Q8_0, F16 et F32, et les autres types que lisent les noyaux), et
                l'attention s'exécute par tuiles de requêtes contre des blocs de clés
                (qk_encoder_attention).  Avec un matmul_offload (set_offload()), les produits
                passent par lui, qui calcule sur son périphérique les formes que le périphérique
                exécute plus vite ; l'offload compte comme occupé de start() à release_work()
                seulement, pour que ses copies des poids puissent quitter le périphérique au
                profit du modèle entre deux images.

                Un encodage s'exécute une étape à la fois (advance()) : l'ordonnanceur du moteur
                entrelace les blocs d'une image avec les passes des autres requêtes.

            THREAD SAFETY
                Un seul thread pilote un encodeur : ses tampons de travail servent la tâche
                qu'il fait avancer.  Le constructeur s'exécute sur le seul thread de chargement
                et n'ouvre aucune section du thread pool, dont les threads peuvent être en train
                de calculer les passes d'autres modèles.  plan(), most_tokens() et les autres
                fonctions const peuvent être appelées depuis n'importe quel thread.
        !*/
    public:
        enum class family { qwen3vl, llava_next, pixtral, gemma4 };

        vision_encoder(std::shared_ptr<model_file> file, thread_pool& pool);

        // Indique si un fichier contient les tenseurs d'un encodeur d'images.
        static bool present(const model_file& f);

        family kind() const { return kind_; }
        long patch_size() const { return patch_; }
        long merge_size() const { return merge_; }
        // Pixels par côté d'un token fusionné, le facteur dont les côtés d'une image sont des multiples
        // (Qwen3-VL, Pixtral).
        long factor() const { return patch_ * merge_; }
        // Pixels par côté d'une vue (LLaVA-NeXT), le plus long côté d'une image (Pixtral).
        long image_size() const { return image_size_; }
        long output_dim() const { return out_dim_; }
        long deepstack_count() const { return static_cast<long>(deepstack_.size()); }
        long output_width() const { return out_dim_ * (1 + deepstack_count()); }
        size_t weight_bytes() const { return weight_bytes_; }
        std::string description() const;

        struct image_plan
        {
            /*!
                WHAT THIS OBJECT REPRESENTS
                    La façon dont l'encodeur lit une image, d'après sa seule taille : la taille à
                    laquelle ses pixels sont décodés, les tokens qu'elle prend dans le prompt et,
                    pour Qwen3-VL, la grille de ces tokens, qui donne leurs positions rotatives ;
                    pour LLaVA-NeXT, la résolution de ses tuiles.
            !*/
            long width = 0, height = 0;
            long tokens = 0;
            long grid_w = 0, grid_h = 0;
            long tiles_w = 0, tiles_h = 0;      // pixels que couvrent les tuiles (LLaVA-NeXT)
        };

        /*!
            ensures
                - Renvoie la façon dont une image de width x height pixels est lue avec le détail
                  "low", "auto" (aussi "") ou "high" : pour Qwen3-VL, la taille redimensionnée à
                  des multiples de factor() dans les limites de 64 et 256, 576 ou 1024 tokens
                  fusionnés ; pour LLaVA-NeXT, les pinpoints de 1, 2 ou 4 tuiles au plus ; pour
                  Pixtral, la taille dans la limite du plus long côté, arrondie au-dessus à des
                  multiples de factor(), puis réduite, quand elle en contient plus, à 256, 576 ou
                  1024 tokens fusionnés ; pour Gemma 4, la taille arrondie à des multiples de
                  factor(), son rapport d'aspect gardé, entre 70 et 140, 280 ou 1120 tokens (la
                  règle calc_size_preserved_ratio de llama.cpp).
                - Lève std::invalid_argument pour un autre détail ou une taille inutilisable.
        !*/
        image_plan plan(long width, long height, const std::string& detail) const;

        // Le plus grand nombre de tokens que prend une image avec un détail, "min" donnant le plus petit
        // nombre qu'une image peut prendre.
        long most_tokens(const std::string& detail) const;

        class job
        {
            /*!
                WHAT THIS OBJECT REPRESENTS
                    L'encodage d'une image en cours : ses patchs entre les blocs, puis sa
                    sortie.
            !*/
        public:
            long tokens() const { return tokens_; }
            long grid_width() const { return grid_w_; }     // tokens fusionnés par ligne
            long grid_height() const { return grid_h_; }
            bool done() const { return done_; }
            // tokens() lignes de output_width() floats, une fois terminé.
            const std::vector<float>& output() const { return out_; }
            std::vector<float>& output() { return out_; }
            double seconds() const { return seconds_; }

        private:
            friend class vision_encoder;
            long pw_ = 0, ph_ = 0, n_ = 0;      // patchs par ligne et par colonne, patchs
            long grid_w_ = 0, grid_h_ = 0, tokens_ = 0;
            long next_ = 0;                     // le bloc suivant ; le fusionneur final après le dernier
            bool done_ = false;
            aligned_floats x_;                  // n_ lignes de la largeur de l'encodeur
            std::vector<float> cos_, sin_;      // n_ lignes de head_dim / 2 valeurs
            std::vector<float> out_;
            double seconds_ = 0;
            // LLaVA-NeXT : les vues, celle en cours d'encodage, les caractéristiques des vues et la
            // partie de la grille des tuiles gardée (lignes et colonnes).
            std::vector<rgb_image> views_;
            size_t view_ = 0;
            long tile_rows_ = 0, tile_cols_ = 0;
            long keep_y0_ = 0, keep_y1_ = 0, keep_x0_ = 0, keep_x1_ = 0;
            std::vector<float> features_;
        };

        /*!
            requires
                - Pour Qwen3-VL et Pixtral, image.width et image.height sont des multiples
                  positifs de factor() ; pour LLaVA-NeXT, image est l'image dont plan() a reçu
                  la taille, décodée en plan.width x plan.height.
            ensures
                - Renvoie un encodage de l'image avec son premier embedding de patchs calculé.
        !*/
        std::unique_ptr<job> start(const rgb_image& image, const image_plan& plan);

        /*!
            ensures
                - Exécute les étapes suivantes de j, un bloc chacune (avec son fusionneur
                  DeepStack) et le fusionneur final en dernier, jusqu'à ce que seconds soient
                  écoulées ou que l'encodage soit complet ; au moins une étape s'exécute.
                - Renvoie j.done().
        !*/
        bool advance(job& j, double seconds);

        // Encode une image d'un coup : out reçoit ses tokens, output_width() floats chacun.
        void encode(const rgb_image& image, const image_plan& plan, std::vector<float>& out);

        // Rend au système la mémoire des tampons de travail.
        void release_work();

        // Confie les produits des encodages suivants à un périphérique.
        void set_offload(std::unique_ptr<matmul_offload> offload) { offload_ = std::move(offload); }

    private:
        // Les bornes auxquelles un produit de Gemma 4 écrête son entrée et sa sortie (Gemma4ClippableLinear).
        struct clamp
        {
            float in_lo = -std::numeric_limits<float>::infinity(), in_hi = std::numeric_limits<float>::infinity();
            float out_lo = -std::numeric_limits<float>::infinity(), out_hi = std::numeric_limits<float>::infinity();
        };

        struct block
        {
            // Les biais sont vides quand le fichier n'en a pas (Pixtral).
            std::vector<float> ln1_w, ln1_b, ln2_w, ln2_b, qkv_b, out_b, up_b, down_b;
            weight_matrix qkv, q, k, v, out, gate, up, down;
            bool fused = true;          // requêtes, clés et valeurs dans une seule matrice
            bool gated = false;         // une porte SiLU dans la couche feed-forward (Pixtral)
            // Gemma 4 : les normes des requêtes et des clés par tête, celles après l'attention et après la couche
            // feed-forward, et les bornes de chaque produit.
            std::vector<float> q_norm, k_norm, attn_post, ffn_post;
            clamp cq, ck, cv, cout, cgate, cup, cdown;
        };

        struct merger
        {
            std::vector<float> norm_w, norm_b, fc1_b, fc2_b;
            weight_matrix fc1, fc2;
            long after = -1;            // le bloc dont il lit la sortie (DeepStack)
        };

        // La disposition des tokens d'une image lue par LLaVA-NeXT.
        struct tile_layout
        {
            long res_w = 0, res_h = 0;  // le pinpoint, en pixels
            long rows = 0, cols = 0;    // tuiles
            long y0 = 0, y1 = 0, x0 = 0, x1 = 0;
            long tokens = 0;
        };
        tile_layout layout(long width, long height, long res_w, long res_h) const;
        tile_layout best_layout(long width, long height, long max_tiles) const;

        void embed_patches(job& j, const rgb_image& image);
        // Une layer norm avec la moyenne et le biais de b, ou une norme par moyenne quadratique quand b
        // est vide.
        void layer_norm_rows(const float* x, const std::vector<float>& w, const std::vector<float>& b, float* y,
            long rows, long width);
        void add_bias_rows(float* y, long stride, const std::vector<float>& bias, long rows);
        void run_block(job& j, const block& b);
        void run_merger(job& j, const merger& m, bool merged_norm, long column);
        void project_view(job& j);
        void pack_views(job& j);
        void project_pixtral(job& j);
        void run_block_gemma(job& j, const block& b);
        void project_gemma(job& j);
        // Un produit de Gemma 4 : son entrée écrêtée (copiée dans cl_ quand ses bornes sont finies), sa sortie écrêtée.
        void clamped_product(const weight_matrix& w, const clamp& c, const float* x, long n, long x_stride, float* y,
            long y_stride);
        clamp read_clamp(const model_file& f, const std::string& prefix) const;
        void product(const weight_matrix& w, const float* x, long n, long x_stride, float* y, long y_stride,
            bool accumulate = false);
        // Une matrice du fichier, convertie en demi-précision quand elle est stockée en bfloat16.
        weight_matrix readable(const weight_matrix& m);

        std::shared_ptr<model_file> file_;
        thread_pool& pool_;
        family kind_ = family::qwen3vl;
        long hidden_ = 0, heads_ = 0, head_dim_ = 0, ffn_ = 0, patch_ = 0, merge_ = 0, out_dim_ = 0, side_ = 0;
        long image_size_ = 0;           // LLaVA-NeXT : pixels par côté d'une vue
        long feature_blocks_ = 0;       // les blocs par lesquels passe une vue
        float eps_ = 1e-6f;
        float mean_[3] = { 0.5f, 0.5f, 0.5f };
        float std_[3] = { 0.5f, 0.5f, 0.5f };
        std::string weight_type_;
        size_t weight_bytes_ = 0;

        std::vector<float> patch_w_, patch_b_, pos_;
        weight_matrix patch_matrix_;
        std::vector<float> inv_freq_;
        std::vector<block> blocks_;
        std::vector<merger> deepstack_;
        merger final_;
        // LLaVA-NeXT : les pinpoints (largeur, hauteur) et l'embedding de retour à la ligne.
        std::vector<std::pair<long, long>> pinpoints_;
        std::vector<float> newline_;
        // Pixtral : la norme après l'embedding des patchs et avant le projecteur, le fusionneur des
        // carrés de patchs (ses colonnes dans l'ordre des patchs d'un carré), les deux couches du
        // projecteur et l'embedding [IMG_BREAK].
        std::vector<float> pre_ln_w_, proj_norm_w_, proj1_b_, proj2_b_, img_break_;
        weight_matrix patch_merger_, proj1_, proj2_;
        // Gemma 4 : les deux tables de positions (colonnes, puis lignes ; pos_cells_ lignes chacune), lues dans le
        // fichier, et la projection vers le modèle de langage.
        weight_matrix pos_table_, input_proj_;
        long pos_cells_ = 0;
        // Les matrices converties au chargement de l'encodeur.
        std::vector<std::vector<uint16_t>> owned_;

        // Tampons de travail ; sum_ reçoit les produits du périphérique que l'encodeur ajoute à ses lignes.
        aligned_floats xn_, qkv_, q_, kt_, v_, ctx_, h_, u_, m_, sum_, cl_, o_;
        std::vector<aligned_floats> attention_work_;
        qk_scratch scratch_;
        std::unique_ptr<matmul_offload> offload_;
    };

}

#endif // ALBIREO_ENGINE_VISION_ENCODER_H_
