// NEON kernels of the macOS port (Apple Silicon), included by qkernels.cpp inside namespace
// albireo when ALBIREO_QK_NEON is set (ARM64 with the dot product instructions).
//
// Each kernel computes, block by block, the same integer sums as its reference (dot_*_ref),
// which are exact whatever the order of the additions, then ends each block with the same
// floating point expression in the same order: its result is the one of the reference, bit
// for bit, so that an answer does not depend on the kernel that computed it.

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
                The eight scales and the eight minimums of the block decoded at once, in the order
                of get_scale_min_k4(0 to 7): u[0..1] hold the scales, u[2..3] the minimums.
            */
            uint32_t u[4];
            std::memcpy(u, w + 4, 12);
            u[3] = ((u[2] >> 4) & 0x0F0F0F0F) | (((u[1] >> 6) & 0x03030303) << 4);
            const uint32_t low_mins = u[1] & 0x3F3F3F3F;
            u[1] = (u[2] & 0x0F0F0F0F) | (((u[0] >> 6) & 0x03030303) << 4);
            u[2] = low_mins;
            u[0] &= 0x3F3F3F3F;
            const uint8_t* sc = reinterpret_cast<const uint8_t*>(u);
            // The minimums against the sums of the activations, 32 by 32: pair k of bsums is group k.
            const int16x8_t pairs = vpaddq_s16(vld1q_s16(y[b].bsums), vld1q_s16(y[b].bsums + 8));
            const int16x8_t mins = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(sc + 8)));
            const int summ = vaddvq_s32(vmlal_high_s16(vmull_s16(vget_low_s16(pairs), vget_low_s16(mins)), pairs, mins));
            // The products stay in vectors, each group of 32 scaled there, and are summed once per block.
            const uint8_t* qs = w + 16;
            const int8_t* q8 = y[b].qs;
            int32x4_t acc1 = vdupq_n_s32(0), acc2 = vdupq_n_s32(0);
            for (int j = 0; j < 4; ++j)
            {
                const uint8x16_t q0 = vld1q_u8(qs);
                const uint8x16_t q1 = vld1q_u8(qs + 16);
                // The low four bits against the first 32 activations, the high four against the next 32.
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
            // The sums s[j] of the reference stay in vectors, each scaled by sc[j] there, and are
            // summed once per block.
            const int32x4_t zero = vdupq_n_s32(0);
            int32x4_t acc1 = zero, acc2 = zero;
            for (int k = 0; k < 2; ++k)
            {
                // Values 16 h to 16 h + 15 of each quarter: the sums s[2 i + h] of the reference.
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
        The products of NT tokens of X (stride xs) with 4 rows of W (K floats each, one after
        the other), written to Y (stride ys) or added to it: each weight read serves NT tokens.
        Each sum keeps the order of dot_f32_ref, four lanes over the columns, the columns left
        over added to the first lane, then (s0 + s1) + (s2 + s3), and its roundings: each
        product rounded, then added, as Apple clang compiles the reference (a fused multiply-add
        changes the last bit of most sums).  The results are those of qk_dot_f32() in this
        build, bit for bit.
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
        The attention by tiles (qk_attention_scores and qk_attention_values), with the sums of
        the portable code in the same order: each score is one sum over the hd elements, from
        the first to the last, each output element one sum over the keys of a group, from the
        first to the last.  The four lanes of a register hold four keys (scores) or four
        elements (values), never two parts of one sum.  The roundings are those of the portable
        loops as Apple clang compiles them.  A score of hd terms: over the first hd / 4 * 4,
        each product rounded, then added (the loop is vectorized); over the last hd % 4, a
        fused multiply-add.  A value: a fused multiply-add for every key (the loop reads the
        values with a stride of hd and stays scalar; only hd == 1, which no model has, is
        vectorized).  The results are those of the portable code of this build, bit for bit.  The halves are
        converted by the instruction of the processor, exact as the table of h2f().
    */

    // n halves of src to floats.
    inline void halves_to_floats(const uint16_t* src, float* dst, long n)
    {
        long i = 0;
        for (; i + 4 <= n; i += 4) vst1q_f32(dst + i, vcvt_f32_f16(vreinterpret_f16_u16(vld1_u16(src + i))));
        for (; i < n; ++i) dst[i] = h2f(src[i]);
    }

    // Groups of four keys of hd halves to floats, transposed: kt[(c4 * hd + i) * 4 + c] holds
    // element i of key 4 c4 + c, so that one register holds element i of four keys.
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

    // The scores of R rows of queries with NB groups of four transposed keys.
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

    void attention_scores_neon(const float* Q, long rows, const uint16_t* K, long m, long hd, float* S, long lds,
        float* scratch, long G)
    {
        for (long j0 = 0; j0 < m; j0 += G)
        {
            const long g = std::min(G, m - j0);
            const long g4 = g / 4 * 4;
            keys_to_floats_x4(K + j0 * hd, g4 / 4, hd, scratch);
            // The keys left over, as the portable code: qk_dot_f32 on keys converted in order.
            float* rest = scratch + g4 * hd;
            halves_to_floats(K + (j0 + g4) * hd, rest, (g - g4) * hd);
            long r = 0;
            for (; r + 4 <= rows; r += 4) scores_rows<4>(Q, r, hd, scratch, g4, S, lds, j0);
            for (; r < rows; ++r) scores_rows<1>(Q, r, hd, scratch, g4, S, lds, j0);
            for (long j = g4; j < g; ++j)
                for (r = 0; r < rows; ++r) S[r * lds + j0 + j] = qk_dot_f32(Q + r * hd, rest + (j - g4) * hd, hd);
        }
    }

    // o[r][i .. i + 4 NB) += sum over j < g of p[r][j] * v[j * hd + i ..], key after key.
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

    void attention_values_neon(const float* P, long lds, long rows, const uint16_t* V, long m, long hd, float* O,
        float* scratch, long G)
    {
        for (long j0 = 0; j0 < m; j0 += G)
        {
            const long g = std::min(G, m - j0);
            halves_to_floats(V + j0 * hd, scratch, g * hd);
            long r = 0;
            for (; r + 4 <= rows; r += 4) values_rows<4>(P, lds, r, j0, scratch, g, hd, O);
            for (; r < rows; ++r) values_rows<1>(P, lds, r, j0, scratch, g, hd, O);
        }
    }
}
