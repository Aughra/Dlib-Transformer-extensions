// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_OFFLOAD_H_
#define ALBIREO_ENGINE_OFFLOAD_H_

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "qkernels.h"

namespace albireo
{

    /*!
        Les poids d'un décodeur dense dont un layer_pass calcule les couches : des blocs
        normalisés avant l'attention et avant le bloc feed-forward par des normes RMS, avec les
        quatre projections de l'attention et un bloc feed-forward SwiGLU.  Les normes et les
        matrices appartiennent au runtime, qui les garde tant que le layer_pass existe.
    !*/
    struct dense_layer
    {
        const float* attn_norm = nullptr;   // d_model valeurs
        const float* ffn_norm = nullptr;    // d_model valeurs
        weight_matrix wq, wk, wv, wo, w_gate, w_up, w_down;
    };

    struct dense_model
    {
        long d_model = 0;
        long q_dim = 0;
        long kv_dim = 0;
        long d_ffn = 0;
        float rms_eps = 0;
        // Le facteur des sorties des blocs ajoutées aux états cachés.
        float residual_scale = 1;
        std::vector<dense_layer> layers;
    };

    /*!
        L'échec d'un périphérique pendant un layer_pass.  memory indique si le périphérique a
        manqué de mémoire, que la fin d'un autre programme peut rendre, plutôt qu'échoué.
    !*/
    class device_failure : public std::runtime_error
    {
    public:
        device_failure(const std::string& what, bool lack_of_memory)
            : std::runtime_error(what), memory(lack_of_memory) {}

        bool memory;
    };

    class layer_pass
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les couches d'un décodeur dense calculées sur un périphérique, autour de
                l'attention que garde le runtime du processeur.  Les états cachés des tokens
                d'une passe restent sur le périphérique de la première couche à la dernière :
                chaque couche envoie au processeur les requêtes, clés et valeurs des tokens et
                reçoit les sorties de leur attention, un échange par couche au lieu d'un par
                produit.  Le périphérique calcule les normalisations des blocs, les produits,
                les portes SwiGLU et les sommes résiduelles ; le processeur les biais, les
                normalisations des requêtes et des clés, les positions rotatives, les clés et
                valeurs des séquences et l'attention.

                Une passe appelle begin(), puis attention_inputs() et attention_output() pour
                chaque couche à tour de rôle, puis end().  Les fonctions qui calculent lèvent
                device_failure quand le périphérique échoue, la passe étant alors perdue : le
                runtime la recalcule avec ses produits.

            THREAD SAFETY
                Un seul thread utilise un objet à la fois : le thread qui exécute les passes
                avant de son modèle.
        !*/
    public:
        virtual ~layer_pass() {}

        /*!
            requires
                - x contient n lignes de d_model floats, les états cachés des tokens de la passe
                  avant la première couche.
            ensures
                - Démarre une passe et renvoie true, ou renvoie false quand le périphérique ne
                  prend pas la passe maintenant : les copies des poids manquent de place, ou la
                  passe est trop petite pour le périphérique.  Le runtime calcule alors la passe
                  avec ses produits, dont le périphérique peut encore prendre certains.
        !*/
        virtual bool begin(const float* x, long n) = 0;

        /*!
            requires
                - begin() a renvoyé true, et chaque couche avant layer a eu son
                  attention_output().
            ensures
                - Écrit les requêtes, clés et valeurs des n tokens de la passe pour layer :
                  n lignes de q_dim floats dans q, n lignes de kv_dim floats dans k et dans v.
        !*/
        virtual void attention_inputs(long layer, float* q, float* k, float* v) = 0;

        /*!
            requires
                - attention_inputs() a été appelé pour layer, et ctx contient les n lignes de
                  q_dim floats qu'a données l'attention de la couche.
            ensures
                - Ajoute aux états cachés la projection de ctx, puis la sortie du bloc
                  feed-forward de layer.
        !*/
        virtual void attention_output(long layer, const float* ctx) = 0;

        /*!
            requires
                - chaque couche a eu son attention_output().
            ensures
                - Écrit dans x les états cachés après la dernière couche des lignes données de
                  la passe, la ligne r à x + r * d_model, et termine la passe.
        !*/
        virtual void end(const std::vector<long>& rows, float* x) = 0;

        // Le nombre minimal de tokens d'une passe pour que begin() la prenne.
        virtual long min_tokens() const = 0;

        /*
            Le nombre de tokens à partir duquel une passe calcule ses produits d'une autre façon,
            à partir de tuiles des poids développés plutôt que de leurs blocs en place, ce que le
            runtime vérifie à part ; 0 pour une passe qui les calcule d'une seule façon.
        */
        virtual long tiled_tokens() const { return 0; }

        /*!
            ensures
                - Écrit dans le journal du périphérique le résultat de la comparaison que le
                  runtime fait entre des passes de cet objet et des passes du processeur avant
                  d'utiliser l'objet (ok indique si elles concordaient), ou la raison pour
                  laquelle le runtime cesse d'utiliser l'objet (ok à false).
        !*/
        virtual void report(bool ok, const std::string& detail) = 0;
    };

    class matmul_offload
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un périphérique qui calcule certains des produits d'un cpu_runtime avec ses
                matrices de poids.

                Le runtime garde le reste de la passe avant sur le processeur (les
                normalisations, les positions rotatives, l'attention sur les clés et valeurs
                paginées) et confie chaque produit à cet objet avec la façon de le calculer sur
                le processeur.  L'objet décide alors où le produit s'exécute, d'après le temps
                que chaque endroit a mis sur des produits de même forme : le périphérique
                remporte les produits sur de nombreux tokens, limités par l'arithmétique, et le
                processeur garde souvent ceux d'un seul token, limités par la lecture des poids
                et que ses noyaux quantifiés servent bien.

                Un périphérique qui calcule des couches entières fournit aussi un layer_pass
                (make_layer_pass()), qui prend les couches d'un modèle dense, normalisations
                comprises, et garde les états cachés d'une passe sur le périphérique ; les
                produits des autres passes, et les produits finaux des logits, viennent
                toujours ici.

            THREAD SAFETY
                Un seul thread utilise un objet à la fois : le thread qui exécute les passes
                avant de son modèle.
        !*/
    public:
        virtual ~matmul_offload() {}

        /*!
            requires
                - host calcule le produit sur le processeur, comme le fait qk_matmul().
            ensures
                - Calcule Y[t][o] = sum_i W[o][i] * X[t][i] pour t < n_tokens et o < W.rows,
                  avec la disposition de qk_matmul(), soit sur le périphérique, soit en appelant
                  host().
        !*/
        virtual void matmul(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
            float* Y, long y_stride, const std::function<void()>& host) = 0;

        /*!
            ensures
                - Indique si le modèle a des requêtes en cours ou en attente.  Quand le
                  périphérique manque de place, les copies des modèles sans requêtes le
                  quittent, le modèle le moins récemment utilisé en premier, au profit de celles
                  d'un modèle qui en a ; les copies d'un modèle avec des requêtes restent jusqu'à
                  ce que ses requêtes soient servies.
        !*/
        virtual void set_busy(bool busy) = 0;

        // Une courte description du périphérique.
        virtual std::string description() const = 0;

        /*!
            ensures
                - Renvoie un objet qui calcule les couches de model sur le périphérique, ou null
                  quand le périphérique ne prend que des produits.  L'objet utilise celui-ci et
                  la mémoire des poids de model, et il est détruit avant eux.
        !*/
        virtual std::unique_ptr<layer_pass> make_layer_pass(const dense_model& model)
        {
            (void)model;
            return std::unique_ptr<layer_pass>();
        }
    };

}

#endif // ALBIREO_ENGINE_OFFLOAD_H_
