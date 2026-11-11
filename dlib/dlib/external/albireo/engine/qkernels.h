// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_QKERNELS_H_
#define ALBIREO_ENGINE_QKERNELS_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include <dlib/data_io/gguf_reader.h>

#include "../common/system_info.h"

namespace albireo
{
    class thread_pool;

    /*!
        Produits de matrices de poids quantifiées avec des activations, sur le processeur.

        Une matrice de poids est utilisée dans la disposition où un fichier GGUF la stocke : une
        ligne par caractéristique de sortie, chaque ligne étant une suite de blocs couvrant les
        caractéristiques d'entrée.  Ses octets sont lus là où ils se trouvent, dans le mappage
        mémoire du fichier, et jamais développés en floats.  Les activations sont quantifiées une
        fois par produit en blocs 8 bits correspondant à la structure en blocs des poids (32
        valeurs pour les formats historiques, 256 valeurs avec des sommes partielles pour les
        k-quants), et chaque produit scalaire est alors calculé en entiers, bloc par bloc, avec
        une seule multiplication flottante par bloc.  C'est ce schéma qui fait tourner un modèle
        4 bits à la vitesse de la mémoire : un token lit chaque poids une fois, sous sa forme
        compacte.

        Formats avec un noyau dédié : F32, F16, BF16, Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, IQ4_NL,
        Q2_K, Q3_K, Q4_K, Q5_K et Q6_K.  Le build AVX2 contient des versions vectorielles des
        formats que les modèles publiés utilisent le plus (Q4_0, Q8_0, Q4_K, Q5_K, Q6_K, F16 et
        F32) ; les autres exécutent leur version portable, que le compilateur vectorise tout de
        même en partie.

        Les produits sont répartis sur les threads d'un thread_pool par groupes de lignes de
        sortie, si bien que chaque thread lit une partie disjointe de la matrice.
    !*/

    enum class act_format { f32, q8_0, q8_k };

    struct weight_matrix
    {
        dlib::ggml_type type = dlib::ggml_type::F32;
        const uint8_t* data = nullptr;   // rows * row_bytes octets
        long rows = 0;                   // caractéristiques de sortie
        long cols = 0;                   // caractéristiques d'entrée
        size_t row_bytes = 0;

        bool valid() const { return data != nullptr && rows > 0 && cols > 0; }
    };

    // Indique si les noyaux du processeur prennent en charge un type de tenseur, et le format
    // d'activation qu'il leur faut pour lui.
    bool qk_supported(dlib::ggml_type type);
    act_format qk_activation_format(dlib::ggml_type type);
    size_t qk_row_bytes(dlib::ggml_type type, long cols);

    // Octets d'une ligne d'activations de n valeurs quantifiée au format f.
    size_t qk_activation_row_bytes(act_format f, long n);

    // Quantifie une ligne de n floats (n multiple de la taille de bloc de f) dans dst.
    void qk_quantize_row(act_format f, const float* src, void* dst, long n);

    // Produit scalaire de la ligne de poids w (type t, n valeurs) avec une ligne d'activations
    // préparée dans le format correspondant par qk_quantize_row(), ou donnée en floats pour F32,
    // F16 et BF16.
    float qk_dot(dlib::ggml_type t, const uint8_t* w, const void* act, long n);

    // La version portable de qk_dot(), quel que soit le build : la référence par rapport à
    // laquelle les noyaux vectoriels sont vérifiés.
    float qk_dot_portable(dlib::ggml_type t, const uint8_t* w, const void* act, long n);

    // Développe une ligne de poids en floats (chemin de référence et recherche d'embedding).
    void qk_dequantize_row(dlib::ggml_type t, const uint8_t* w, float* dst, long n);

    /*!
        Un allocateur dont les blocs commencent sur 64 octets, la taille d'une ligne de cache et
        d'un vecteur AVX-512.  Les tampons des activations et de l'attention l'utilisent : une
        ligne dont la longueur est un multiple de 16 floats commence alors sur une ligne de
        cache, et aucun chargement vectoriel des noyaux ne lit deux lignes à la fois, ce qui
        coûte jusqu'à deux fois le temps d'un chargement à l'intérieur d'une seule.
    !*/
    template <typename T>
    struct aligned_allocator
    {
        typedef T value_type;

        aligned_allocator() noexcept {}
        template <typename U> aligned_allocator(const aligned_allocator<U>&) noexcept {}

        T* allocate(size_t n)
        {
            if (n > (static_cast<size_t>(-1) - 128) / sizeof(T)) throw std::bad_alloc();
            // L'adresse renvoyée par operator new est gardée juste avant le bloc aligné.
            char* raw = static_cast<char*>(::operator new(n * sizeof(T) + 64 + sizeof(void*)));
            const uintptr_t first = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
            char* p = raw + sizeof(void*) + (64 - first % 64) % 64;
            std::memcpy(p - sizeof(void*), &raw, sizeof(void*));
            return reinterpret_cast<T*>(p);
        }

        void deallocate(T* p, size_t) noexcept
        {
            void* raw;
            std::memcpy(&raw, reinterpret_cast<char*>(p) - sizeof(void*), sizeof(void*));
            ::operator delete(raw);
        }
    };

    template <typename T, typename U>
    bool operator==(const aligned_allocator<T>&, const aligned_allocator<U>&) { return true; }
    template <typename T, typename U>
    bool operator!=(const aligned_allocator<T>&, const aligned_allocator<U>&) { return false; }

    typedef std::vector<float, aligned_allocator<float>> aligned_floats;

    /*!
        Mémoire de travail qu'un thread réutilise d'un produit à l'autre, pour qu'un token
        n'alloue rien.
    !*/
    struct qk_scratch
    {
        std::vector<uint8_t> act;
        std::vector<float> row;
    };

    /*!
        À partir de ce nombre de tokens, qk_matmul() développe les poids en floats, une tuile
        de lignes à la fois, et les multiplie avec les activations telles quelles : le
        développement est alors partagé par assez de tokens pour coûter peu, et les produits
        tournent au plein débit des unités de calcul flottant.  En dessous, les activations
        sont quantifiées et les noyaux entiers calculent chaque produit scalaire à partir des
        poids compacts, ce dont a besoin une étape de génération, limitée par la lecture des
        poids.
    !*/
#if defined(__APPLE__) && defined(__ARM_FEATURE_DOTPROD)
    // Apple Silicon : les noyaux entiers restent plus rapides que les produits d'Accelerate
    // jusqu'à environ 24 tokens (qkernels_accelerate.inc).
    const long qk_float_product_tokens = 24;
#else
    const long qk_float_product_tokens = 8;
#endif

    /*!
        ensures
            - Calcule Y[t][o] = sum_i W[o][i] * X[t][i] pour t < n_tokens et o < W.rows.
            - X contient n_tokens lignes de W.cols floats, espacées de x_stride floats ; Y
              reçoit n_tokens lignes de W.rows floats, espacées de y_stride floats.
            - Quand accumulate vaut true, les produits sont ajoutés à Y au lieu de le remplacer.
            - path choisit les noyaux.  Les experts d'un mélange reçoivent une part variable
              des tokens d'une passe ; ils suivent la taille de la passe entière, pour qu'un
              token soit calculé de la même façon quel que soit l'expert qu'il atteint.
    !*/
    enum class qk_path
    {
        automatic,  // floats à partir de qk_float_product_tokens tokens, entiers en dessous
        integer,    // activations quantifiées et noyaux entiers
        floats      // poids développés en floats
    };

    void qk_matmul(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
        float* Y, long y_stride, thread_pool& pool, qk_scratch& scratch, bool accumulate = false,
        qk_path path = qk_path::automatic);

    // Nom du jeu d'instructions pour lequel les noyaux ont été compilés.
    const char* qk_isa();


    /*!
        ensures
            - Quand on vaut true, qk_matmul() développe chaque ligne de poids en floats et
              calcule des produits scalaires en simple précision avec les activations telles
              que données, au lieu de quantifier les activations.  C'est lent et uniquement
              destiné à la validation : les résultats peuvent alors être comparés à une
              référence en virgule flottante au niveau des erreurs d'arrondi.
    !*/
    void qk_set_exact_products(bool on);

    // Conversions demi-précision et bfloat16 utilisées dans tout le moteur.
    float qk_half_to_float(uint16_t h);
    uint16_t qk_float_to_half(float f);
    void qk_half_to_float_row(const uint16_t* src, float* dst, long n);
    void qk_float_to_half_row(const float* src, uint16_t* dst, long n);

    /*!
        Les clés et valeurs 8 bits du cache : des lignes de n valeurs, n multiple de 32, chaque
        groupe de 32 valeurs stocké en entiers 8 bits avec une échelle en demi-précision, la plus
        grande magnitude du groupe divisée par 127.

        qk_float_to_q8_row() ensures:
            - dst[i] == round(src[i] / d), dans [-127, 127], pour i < n, d étant l'échelle du
              groupe de i telle que stockée dans scales[i / 32] : la plus grande magnitude du
              groupe divisée par 127 en demi-précision, montée au demi-flottant suivant quand
              l'arrondi d'un demi-flottant sous-normal porterait la plus grande valeur au-delà de
              127,5 pas.
        qk_q8_to_float_row() ensures:
            - dst[i] == half_to_float(scales[i / 32]) * src[i] pour i < n.
    !*/
    void qk_float_to_q8_row(const float* src, int8_t* dst, uint16_t* scales, long n);
    void qk_q8_to_float_row(const int8_t* src, const uint16_t* scales, float* dst, long n);

    /*!
        requires
            - n > 0
        ensures
            - Fait tourner la ligne x de n valeurs : les signes d'un motif fixe, puis la
              transformée de Walsh-Hadamard normalisée sur chaque bloc de la plus grande
              puissance de deux qui divise n.  L'application est orthogonale, si bien qu'une
              clé et une requête tournées de la même façon gardent leur produit scalaire.  Les
              clés du cache 8 bits sont stockées tournées, les requêtes étant tournées de la
              même façon avant les scores : quelques canaux des clés de certains modèles
              contiennent des valeurs dix fois plus grandes que les autres, que la rotation
              répartit sur la ligne, pour que les groupes du format 8 bits les arrondissent avec
              un pas plus fin.
    !*/
    void qk_rotate_row(float* x, long n);

    // Fonctions vectorielles d'aide du code d'attention et de normalisation.
    float qk_dot_f32(const float* a, const float* b, long n);
    float qk_dot_f32_f16(const float* a, const uint16_t* b, long n);
    void qk_axpy_f16(float alpha, const uint16_t* x, float* y, long n);   // y += alpha * x
    void qk_scale(float* x, float s, long n);
    float qk_max(const float* x, long n);
    // x[i] = exp(x[i] - m), renvoie la somme des résultats.
    float qk_exp_shift_sum(float* x, float m, long n);
    // g[i] = silu(g[i]) * u[i], la porte d'un bloc SwiGLU, silu(x) = x / (1 + exp(-x)).
    void qk_silu_mul(float* g, const float* u, long n);

    /*!
        L'attention par tuiles du runtime du processeur.  Une tuile contient des lignes de
        requêtes, les têtes qui partagent une tête de clés et valeurs pour des tokens consécutifs
        d'une séquence, et lit le cache bloc après bloc : chaque clé et chaque valeur prise en
        mémoire sert toutes les lignes de la tuile.  K et V contiennent m lignes de hd
        demi-flottants, Q et O des lignes de hd floats, et scratch qk_attention_scratch(hd)
        floats propres à l'appelant.

        qk_attention_scores() ensures:
            - S[r * lds + j] == dot(Q + r * hd, K + j * hd), pour r < rows et j < m.
        qk_attention_values() ensures:
            - O[r * hd + i] += somme sur j < m de P[r * lds + j] * V[j * hd + i], pour
              r < rows et i < hd.
    !*/
    long qk_attention_scratch(long hd);
    void qk_attention_scores(const float* Q, long rows, const uint16_t* K, long m, long hd, float* S, long lds,
        float* scratch);
    void qk_attention_values(const float* P, long lds, long rows, const uint16_t* V, long m, long hd, float* O,
        float* scratch);
    // La même chose sur des clés et valeurs 8 bits (qk_float_to_q8_row), hd multiple de 32.
    void qk_attention_scores_q8(const float* Q, long rows, const int8_t* K, const uint16_t* K_scales, long m, long hd,
        float* S, long lds, float* scratch);
    void qk_attention_values_q8(const float* P, long lds, long rows, const int8_t* V, const uint16_t* V_scales, long m,
        long hd, float* O, float* scratch);

    /*!
        Les fonctions vectorielles d'aide des encodeurs d'images, dont les blocs normalisent
        avec une moyenne et un biais et activent avec GELU.

        qk_layer_norm() ensures:
            - y[i] == (x[i] - mean) / sqrt(var + eps) * gamma[i] + beta[i] pour i < n, mean et
              var étant la moyenne de x et la moyenne de ses écarts au carré.  x et y peuvent
              être la même ligne.
        qk_gelu_tanh() ensures:
            - x[i] == x[i] / (1 + exp(-2 u)) avec u == sqrt(2 / pi) * (x[i] + 0.044715 x[i]^3),
              ce qui vaut 0.5 x[i] (1 + tanh(u)), l'approximation tanh de GELU.
        qk_gelu_erf() ensures:
            - x[i] == 0.5 x[i] (1 + erf(x[i] / sqrt(2))), la GELU exacte, avec erf à 1.5e-7
              près (Abramowitz et Stegun 7.1.26).
        qk_add() ensures:
            - y[i] += x[i] pour i < n.
    !*/
    void qk_layer_norm(const float* x, const float* gamma, const float* beta, float* y, long n, float eps);
    void qk_gelu_tanh(float* x, long n);
    void qk_gelu_erf(float* x, long n);
    void qk_add(float* y, const float* x, long n);

    /*!
        L'attention d'un encodeur, dont tous les tokens se voient les uns les autres, pour une
        tuile de lignes de requêtes d'une tête : O = softmax(Q K^T) V.

        Q contient rows lignes de hd floats, déjà mises à l'échelle ; Kt contient les clés
        transposées, hd lignes de m floats, espacées de ldk floats, pour que les scores d'une
        ligne de requêtes contre une suite de clés soient des produits vectoriels ; V contient
        m lignes de hd floats, espacées de ldv floats ; O reçoit rows lignes de hd floats,
        espacées de ldo floats.  Les clés sont lues par blocs avec un maximum et une somme
        courants, pour que la mémoire de travail ne grandisse pas avec m.

        requires
            - 0 < rows <= qk_encoder_tile_rows
            - work contient qk_encoder_attention_work(hd) floats propres à l'appelant
    !*/
    const long qk_encoder_tile_rows = 64;
    long qk_encoder_attention_work(long hd);
    void qk_encoder_attention(const float* Q, long rows, const float* Kt, long ldk, const float* V, long ldv, long m,
        long hd, float* O, long ldo, float* work);

}

#endif // ALBIREO_ENGINE_QKERNELS_H_
