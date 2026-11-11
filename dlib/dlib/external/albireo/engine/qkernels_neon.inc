// Noyaux NEON du portage macOS (Apple Silicon), inclus par qkernels.cpp dans l'espace de noms
// albireo quand ALBIREO_QK_NEON est défini (ARM64 avec les instructions de produit scalaire).
//
// Chaque noyau calcule, bloc par bloc, les mêmes sommes entières que sa référence (dot_*_ref),
// exactes quel que soit l'ordre des additions, puis termine chaque bloc par la même expression
// en virgule flottante dans le même ordre : son résultat est celui de la référence, bit pour
// bit, si bien qu'une réponse ne dépend pas du noyau qui l'a calculée.

namespace
{
    float dot_q4_K_neon(const uint8_t* w, const block_q8_K* y, long n)
    {
        const uint8x16_t m4 = vdupq_n_u8(0x0F);
        float sum = 0.0f;
        for (long b = 0; b < n / 256; ++b, w += 144)
        {
            const float d = h2f(w) * y[b].d;
            const float dmin = h2f(w + 2) * y[b].d;
            /*
                Les huit échelles et les huit minimums du bloc décodés d'un coup, dans l'ordre
                de get_scale_min_k4(0 à 7) : u[0..1] contiennent les échelles, u[2..3] les minimums.
            */
            uint32_t u[4];
            std::memcpy(u, w + 4, 12);
            u[3] = ((u[2] >> 4) & 0x0F0F0F0F) | (((u[1] >> 6) & 0x03030303) << 4);
            const uint32_t low_mins = u[1] & 0x3F3F3F3F;
            u[1] = (u[2] & 0x0F0F0F0F) | (((u[0] >> 6) & 0x03030303) << 4);
            u[2] = low_mins;
            u[0] &= 0x3F3F3F3F;
            const uint8_t* sc = reinterpret_cast<const uint8_t*>(u);
            // Les minimums contre les sommes des activations, 32 par 32 : la paire k de bsums est le groupe k.
            const int16x8_t pairs = vpaddq_s16(vld1q_s16(y[b].bsums), vld1q_s16(y[b].bsums + 8));
            const int16x8_t mins = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(sc + 8)));
            const int summ = vaddvq_s32(vmlal_high_s16(vmull_s16(vget_low_s16(pairs), vget_low_s16(mins)), pairs, mins));
            // Les produits restent dans des vecteurs, chaque groupe de 32 y est mis à l'échelle, et sont
            // additionnés une fois par bloc.
            const uint8_t* qs = w + 16;
            const int8_t* q8 = y[b].qs;
            int32x4_t acc1 = vdupq_n_s32(0), acc2 = vdupq_n_s32(0);
            for (int j = 0; j < 4; ++j)
            {
                const uint8x16_t q0 = vld1q_u8(qs);
                const uint8x16_t q1 = vld1q_u8(qs + 16);
                // Les quatre bits de poids faible contre les 32 premières activations, les quatre de poids
                // fort contre les 32 suivantes.
                int32x4_t a1 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vandq_u8(q0, m4)), vld1q_s8(q8));
                a1 = vdotq_s32(a1, vreinterpretq_s8_u8(vandq_u8(q1, m4)), vld1q_s8(q8 + 16));
                int32x4_t a2 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vshrq_n_u8(q0, 4)), vld1q_s8(q8 + 32));
                a2 = vdotq_s32(a2, vreinterpretq_s8_u8(vshrq_n_u8(q1, 4)), vld1q_s8(q8 + 48));
                acc1 = vmlaq_n_s32(acc1, a1, sc[2 * j]);
                acc2 = vmlaq_n_s32(acc2, a2, sc[2 * j + 1]);
                qs += 32;
                q8 += 64;
            }
            const int isum = vaddvq_s32(vaddq_s32(acc1, acc2));
            sum += d * static_cast<float>(isum) - dmin * static_cast<float>(summ);
        }
        return sum;
    }

    float dot_q6_K_neon(const uint8_t* w, const block_q8_K* y, long n)
    {
        const uint8x16_t m4 = vdupq_n_u8(0x0F);
        const uint8x16_t m2 = vdupq_n_u8(0x03);
        const int8x16_t offset = vdupq_n_s8(32);
        float sum = 0.0f;
        for (long b = 0; b < n / 256; ++b, w += 210)
        {
            const uint8_t* ql = w;
            const uint8_t* qh = w + 128;
            const int8_t* sc = reinterpret_cast<const int8_t*>(w + 192);
            const float d = h2f(w + 208) * y[b].d;
            const int8_t* q8 = y[b].qs;
            // Les sommes s[j] de la référence restent dans des vecteurs, chacune y est multipliée par sc[j],
            // et sont additionnées une fois par bloc.
            const int32x4_t zero = vdupq_n_s32(0);
            int32x4_t acc1 = zero, acc2 = zero;
            for (int k = 0; k < 2; ++k)
            {
                // Valeurs 16 h à 16 h + 15 de chaque quart : les sommes s[2 i + h] de la référence.
                for (int h = 0; h < 2; ++h)
                {
                    const uint8x16_t la = vld1q_u8(ql + 16 * h);
                    const uint8x16_t lb = vld1q_u8(ql + 32 + 16 * h);
                    const uint8x16_t hb = vld1q_u8(qh + 16 * h);
                    const uint8x16_t u1 = vorrq_u8(vandq_u8(la, m4), vshlq_n_u8(vandq_u8(hb, m2), 4));
                    const uint8x16_t u2 = vorrq_u8(vandq_u8(lb, m4), vshlq_n_u8(vandq_u8(vshrq_n_u8(hb, 2), m2), 4));
                    const uint8x16_t u3 = vorrq_u8(vshrq_n_u8(la, 4), vshlq_n_u8(vandq_u8(vshrq_n_u8(hb, 4), m2), 4));
                    const uint8x16_t u4 = vorrq_u8(vshrq_n_u8(lb, 4), vshlq_n_u8(vshrq_n_u8(hb, 6), 4));
                    acc1 = vmlaq_n_s32(acc1, vdotq_s32(zero, vsubq_s8(vreinterpretq_s8_u8(u1), offset), vld1q_s8(q8 + 16 * h)), sc[0 + h]);
                    acc2 = vmlaq_n_s32(acc2, vdotq_s32(zero, vsubq_s8(vreinterpretq_s8_u8(u2), offset), vld1q_s8(q8 + 32 + 16 * h)), sc[2 + h]);
                    acc1 = vmlaq_n_s32(acc1, vdotq_s32(zero, vsubq_s8(vreinterpretq_s8_u8(u3), offset), vld1q_s8(q8 + 64 + 16 * h)), sc[4 + h]);
                    acc2 = vmlaq_n_s32(acc2, vdotq_s32(zero, vsubq_s8(vreinterpretq_s8_u8(u4), offset), vld1q_s8(q8 + 96 + 16 * h)), sc[6 + h]);
                }
                ql += 64;
                qh += 32;
                sc += 8;
                q8 += 128;
            }
            const int isum = vaddvq_s32(vaddq_s32(acc1, acc2));
            sum += d * static_cast<float>(isum);
        }
        return sum;
    }

    /*
        Les produits de NT tokens de X (pas xs) avec 4 lignes de W (K floats chacune, l'une après
        l'autre), écrits dans Y (pas ys) ou ajoutés à Y : chaque poids lu sert NT tokens.
        Chaque somme garde l'ordre de dot_f32_ref, quatre voies sur les colonnes, les colonnes
        restantes ajoutées à la première voie, puis (s0 + s1) + (s2 + s3), et ses arrondis : chaque
        produit arrondi, puis ajouté, comme Apple clang compile la référence (une
        multiplication-addition fusionnée change le dernier bit de la plupart des sommes).  Les
        résultats sont ceux de qk_dot_f32() dans ce build, bit pour bit.
    */
    template <int NT>
    void gemm_neon(const float* X, long xs, const float* W, long K, float* Y, long ys, bool accumulate)
    {
        float32x4_t acc[NT][4];
        for (int i = 0; i < NT; ++i)
            for (int j = 0; j < 4; ++j) acc[i][j] = vdupq_n_f32(0.0f);
        long k = 0;
        for (; k + 4 <= K; k += 4)
        {
            const float32x4_t w0 = vld1q_f32(W + k);
            const float32x4_t w1 = vld1q_f32(W + K + k);
            const float32x4_t w2 = vld1q_f32(W + 2 * K + k);
            const float32x4_t w3 = vld1q_f32(W + 3 * K + k);
            for (int i = 0; i < NT; ++i)
            {
                const float32x4_t x = vld1q_f32(X + i * xs + k);
                acc[i][0] = vaddq_f32(acc[i][0], vmulq_f32(w0, x));
                acc[i][1] = vaddq_f32(acc[i][1], vmulq_f32(w1, x));
                acc[i][2] = vaddq_f32(acc[i][2], vmulq_f32(w2, x));
                acc[i][3] = vaddq_f32(acc[i][3], vmulq_f32(w3, x));
            }
        }
        for (int i = 0; i < NT; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                float s0 = vgetq_lane_f32(acc[i][j], 0);
                const float s1 = vgetq_lane_f32(acc[i][j], 1);
                const float s2 = vgetq_lane_f32(acc[i][j], 2);
                const float s3 = vgetq_lane_f32(acc[i][j], 3);
                for (long c = k; c < K; ++c) s0 += W[j * K + c] * X[i * xs + c];
                const float v = (s0 + s1) + (s2 + s3);
                float* out = Y + i * ys + j;
                *out = accumulate ? *out + v : v;
            }
        }
    }

    /*
        L'attention par tuiles (qk_attention_scores et qk_attention_values), avec les sommes du
        code portable dans le même ordre : chaque score est une somme sur les hd éléments, du
        premier au dernier, chaque élément de sortie une somme sur les clés d'un groupe, de la
        première à la dernière.  Les quatre voies d'un registre contiennent quatre clés (scores) ou
        quatre éléments (valeurs), jamais deux parties d'une même somme.  Les arrondis sont ceux des
        boucles portables telles qu'Apple clang les compile.  Un score de hd termes : sur les
        hd / 4 * 4 premiers, chaque produit arrondi, puis ajouté (la boucle est vectorisée) ; sur
        les hd % 4 derniers, une multiplication-addition fusionnée.  Une valeur : une
        multiplication-addition fusionnée pour chaque clé (la boucle lit les valeurs avec un pas de
        hd et reste scalaire ; seul hd == 1, qu'aucun modèle n'a, est vectorisé).  Les résultats
        sont ceux du code portable de ce build, bit pour bit.  Les demi-flottants sont convertis
        par l'instruction du processeur, exacte comme la table de h2f().
    */

    // n demi-flottants de src en floats.
    inline void halves_to_floats(const uint16_t* src, float* dst, long n)
    {
        long i = 0;
        for (; i + 4 <= n; i += 4) vst1q_f32(dst + i, vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(src + i))));
        for (; i < n; ++i) dst[i] = h2f(src[i]);
    }

    // Groupes de quatre clés de hd demi-flottants en floats, transposés : kt[(c4 * hd + i) * 4 + c]
    // contient l'élément i de la clé 4 c4 + c, pour qu'un registre contienne l'élément i de quatre clés.
    inline void keys_to_floats_x4(const uint16_t* K, long groups, long hd, float* kt)
    {
        for (long c4 = 0; c4 < groups; ++c4)
        {
            const uint16_t* k = K + c4 * 4 * hd;
            float* out = kt + c4 * hd * 4;
            long i = 0;
            for (; i + 4 <= hd; i += 4)
            {
                float32x4x4_t t;
                t.val[0] = vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(k + i)));
                t.val[1] = vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(k + hd + i)));
                t.val[2] = vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(k + 2 * hd + i)));
                t.val[3] = vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(k + 3 * hd + i)));
                vst4q_f32(out + i * 4, t);
            }
            for (; i < hd; ++i)
                for (int c = 0; c < 4; ++c) out[i * 4 + c] = h2f(k[c * hd + i]);
        }
    }

    // Les scores de R lignes de requêtes avec NB groupes de quatre clés transposées.
    template <int R, int NB>
    void scores_tile(const float* const* q, const float* kt, long hd, float* const* s)
    {
        float32x4_t acc[R][NB];
        for (int r = 0; r < R; ++r)
            for (int b = 0; b < NB; ++b) acc[r][b] = vdupq_n_f32(0.0f);
        const long hd4 = hd / 4 * 4;
        for (long i = 0; i < hd; ++i)
        {
            float32x4_t k[NB];
            for (int b = 0; b < NB; ++b) k[b] = vld1q_f32(kt + (b * hd + i) * 4);
            for (int r = 0; r < R; ++r)
            {
                const float32x4_t x = vdupq_n_f32(q[r][i]);
                if (i < hd4)
                    for (int b = 0; b < NB; ++b) acc[r][b] = vaddq_f32(acc[r][b], vmulq_f32(x, k[b]));
                else
                    for (int b = 0; b < NB; ++b) acc[r][b] = vfmaq_f32(acc[r][b], x, k[b]);
            }
        }
        for (int r = 0; r < R; ++r)
            for (int b = 0; b < NB; ++b) vst1q_f32(s[r] + 4 * b, acc[r][b]);
    }

    template <int R>
    void scores_rows(const float* Q, long r, long hd, const float* kt, long g4, float* S, long lds, long j0)
    {
        const float* q[R];
        for (int e = 0; e < R; ++e) q[e] = Q + (r + e) * hd;
        long j = 0;
        for (; j + 16 <= g4; j += 16)
        {
            float* s[R];
            for (int e = 0; e < R; ++e) s[e] = S + (r + e) * lds + j0 + j;
            scores_tile<R, 4>(q, kt + j * hd, hd, s);
        }
        for (; j < g4; j += 4)
        {
            float* s[R];
            for (int e = 0; e < R; ++e) s[e] = S + (r + e) * lds + j0 + j;
            scores_tile<R, 1>(q, kt + j * hd, hd, s);
        }
    }

    /*
        Les clés ou les valeurs de l'attention en floats, depuis leurs deux stockages : demi-flottants,
        ou entiers 8 bits par groupes de 32 partageant une échelle en demi-précision (le format q8 de
        kv_cache.h).  Une valeur q8 est son entier multiplié par son échelle : le produit d'un entier
        8 bits par un demi-flottant est exact dans un float, si bien que les floats sont ceux de
        qk_q8_to_float_row(), quel que soit l'ordre.  transposed() remplit des groupes de quatre clés
        comme keys_to_floats_x4(), rows() compte les lignes dans l'ordre.
    */
    struct half_rows
    {
        const uint16_t* x;
        long hd;
        void transposed(long j0, long groups, float* kt) const { keys_to_floats_x4(x + j0 * hd, groups, hd, kt); }
        void rows(long j0, long count, float* dst) const { halves_to_floats(x + j0 * hd, dst, count * hd); }
    };

    struct q8_rows
    {
        const int8_t* x;
        const uint16_t* scales;   // hd / 32 par ligne
        long hd;

        // 8 entiers de q multipliés par d, vers deux registres de quatre floats.
        static void eight(const int8_t* q, float d, float32x4_t& lo, float32x4_t& hi)
        {
            const int16x8_t w = vmovl_s8(vld1_s8(q));
            lo = vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(w))), d);
            hi = vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(w)), d);
        }

        void transposed(long j0, long groups, float* kt) const
        {
            const long spr = hd / 32;
            for (long c4 = 0; c4 < groups; ++c4)
            {
                const long j = j0 + 4 * c4;
                float* out = kt + c4 * hd * 4;
                for (long i = 0; i < hd; i += 8)
                {
                    float32x4x4_t lo, hi;
                    for (int c = 0; c < 4; ++c)
                        eight(x + (j + c) * hd + i, h2f(scales[(j + c) * spr + i / 32]), lo.val[c], hi.val[c]);
                    vst4q_f32(out + i * 4, lo);
                    vst4q_f32(out + (i + 4) * 4, hi);
                }
            }
        }

        void rows(long j0, long count, float* dst) const
        {
            const long n = count * hd;
            const int8_t* q = x + j0 * hd;
            const uint16_t* sc = scales + j0 * (hd / 32);
            for (long b = 0; b * 32 < n; ++b)
            {
                const float d = h2f(sc[b]);
                for (int k = 0; k < 4; ++k)
                {
                    float32x4_t lo, hi;
                    eight(q + b * 32 + 8 * k, d, lo, hi);
                    vst1q_f32(dst + b * 32 + 8 * k, lo);
                    vst1q_f32(dst + b * 32 + 8 * k + 4, hi);
                }
            }
        }
    };

    template <typename Keys>
    void attention_scores_neon(const float* Q, long rows, const Keys& keys, long m, long hd, float* S, long lds,
        float* scratch, long G)
    {
        for (long j0 = 0; j0 < m; j0 += G)
        {
            const long g = std::min(G, m - j0);
            const long g4 = g / 4 * 4;
            keys.transposed(j0, g4 / 4, scratch);
            // Les clés restantes, comme le code portable : qk_dot_f32 sur les clés converties dans l'ordre.
            float* rest = scratch + g4 * hd;
            keys.rows(j0 + g4, g - g4, rest);
            long r = 0;
            for (; r + 4 <= rows; r += 4) scores_rows<4>(Q, r, hd, scratch, g4, S, lds, j0);
            for (; r < rows; ++r) scores_rows<1>(Q, r, hd, scratch, g4, S, lds, j0);
            for (long j = g4; j < g; ++j)
                for (r = 0; r < rows; ++r) S[r * lds + j0 + j] = qk_dot_f32(Q + r * hd, rest + (j - g4) * hd, hd);
        }
    }

    void attention_scores_neon(const float* Q, long rows, const uint16_t* K, long m, long hd, float* S, long lds,
        float* scratch, long G)
    {
        attention_scores_neon(Q, rows, half_rows{ K, hd }, m, hd, S, lds, scratch, G);
    }

    void attention_scores_q8_neon(const float* Q, long rows, const int8_t* K, const uint16_t* K_scales, long m,
        long hd, float* S, long lds, float* scratch, long G)
    {
        attention_scores_neon(Q, rows, q8_rows{ K, K_scales, hd }, m, hd, S, lds, scratch, G);
    }

    // o[r][i .. i + 4 NB) += somme sur j < g de p[r][j] * v[j * hd + i ..], clé après clé.
    template <int R, int NB>
    void values_tile(const float* const* p, const float* v, long g, long hd, float* const* o, long i)
    {
        float32x4_t acc[R][NB];
        for (int r = 0; r < R; ++r)
            for (int b = 0; b < NB; ++b) acc[r][b] = vld1q_f32(o[r] + i + 4 * b);
        for (long j = 0; j < g; ++j)
        {
            float32x4_t x[NB];
            for (int b = 0; b < NB; ++b) x[b] = vld1q_f32(v + j * hd + i + 4 * b);
            for (int r = 0; r < R; ++r)
            {
                const float32x4_t w = vdupq_n_f32(p[r][j]);
                for (int b = 0; b < NB; ++b) acc[r][b] = vfmaq_f32(acc[r][b], w, x[b]);
            }
        }
        for (int r = 0; r < R; ++r)
            for (int b = 0; b < NB; ++b) vst1q_f32(o[r] + i + 4 * b, acc[r][b]);
    }

    template <int R>
    void values_rows(const float* P, long lds, long r, long j0, const float* v, long g, long hd, float* O)
    {
        const float* p[R];
        float* o[R];
        for (int e = 0; e < R; ++e)
        {
            p[e] = P + (r + e) * lds + j0;
            o[e] = O + (r + e) * hd;
        }
        long i = 0;
        for (; i + 16 <= hd; i += 16) values_tile<R, 4>(p, v, g, hd, o, i);
        for (; i + 4 <= hd; i += 4) values_tile<R, 1>(p, v, g, hd, o, i);
        for (; i < hd; ++i)
            for (int e = 0; e < R; ++e)
            {
                float t = o[e][i];
                for (long j = 0; j < g; ++j) t = std::fma(p[e][j], v[j * hd + i], t);
                o[e][i] = t;
            }
    }

    template <typename Values>
    void attention_values_neon(const float* P, long lds, long rows, const Values& values, long m, long hd, float* O,
        float* scratch, long G)
    {
        for (long j0 = 0; j0 < m; j0 += G)
        {
            const long g = std::min(G, m - j0);
            values.rows(j0, g, scratch);
            long r = 0;
            for (; r + 4 <= rows; r += 4) values_rows<4>(P, lds, r, j0, scratch, g, hd, O);
            for (; r < rows; ++r) values_rows<1>(P, lds, r, j0, scratch, g, hd, O);
        }
    }

    void attention_values_neon(const float* P, long lds, long rows, const uint16_t* V, long m, long hd, float* O,
        float* scratch, long G)
    {
        attention_values_neon(P, lds, rows, half_rows{ V, hd }, m, hd, O, scratch, G);
    }

    void attention_values_q8_neon(const float* P, long lds, long rows, const int8_t* V, const uint16_t* V_scales,
        long m, long hd, float* O, float* scratch, long G)
    {
        attention_values_neon(P, lds, rows, q8_rows{ V, V_scales, hd }, m, hd, O, scratch, G);
    }
}
