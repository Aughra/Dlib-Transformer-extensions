// NEON kernels of the macOS port (Apple Silicon), included by qkernels.cpp inside namespace
// albireo when ALBIREO_QK_NEON is set (ARM64 with the dot product instructions).
//
// Each kernel computes, block by block, the same integer sums as its reference (dot_*_ref),
// which are exact whatever the order of the additions, then ends each block with the same
// floating point expression in the same order: its result is the one of the reference, bit
// for bit, so that an answer does not depend on the kernel that computed it.

namespace
{
    // The sum of the products of 16 signed bytes of a by 16 signed bytes of b.
    inline int dot16(int8x16_t a, int8x16_t b)
    {
        return vaddvq_s32(vdotq_s32(vdupq_n_s32(0), a, b));
    }

    float dot_q4_K_neon(const uint8_t* w, const block_q8_K* y, long n)
    {
        const uint8x16_t m4 = vdupq_n_u8(0x0F);
        float sum = 0.0f;
        for (long b = 0; b < n / 256; ++b, w += 144)
        {
            const float d = h2f(w) * y[b].d;
            const float dmin = h2f(w + 2) * y[b].d;
            const uint8_t* scales = w + 4;
            const uint8_t* qs = w + 16;
            const int8_t* q8 = y[b].qs;
            int isum = 0, summ = 0;
            for (int j = 0; j < 4; ++j)
            {
                int sc1, m1, sc2, m2;
                get_scale_min_k4(2 * j, scales, sc1, m1);
                get_scale_min_k4(2 * j + 1, scales, sc2, m2);
                const uint8x16_t q0 = vld1q_u8(qs);
                const uint8x16_t q1 = vld1q_u8(qs + 16);
                // The low four bits against the first 32 activations, the high four against the next 32.
                int32x4_t a1 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vandq_u8(q0, m4)), vld1q_s8(q8));
                a1 = vdotq_s32(a1, vreinterpretq_s8_u8(vandq_u8(q1, m4)), vld1q_s8(q8 + 16));
                int32x4_t a2 = vdotq_s32(vdupq_n_s32(0), vreinterpretq_s8_u8(vshrq_n_u8(q0, 4)), vld1q_s8(q8 + 32));
                a2 = vdotq_s32(a2, vreinterpretq_s8_u8(vshrq_n_u8(q1, 4)), vld1q_s8(q8 + 48));
                const int s1 = vaddvq_s32(a1);
                const int s2 = vaddvq_s32(a2);
                isum += sc1 * s1 + sc2 * s2;
                summ += m1 * (y[b].bsums[4 * j] + y[b].bsums[4 * j + 1])
                      + m2 * (y[b].bsums[4 * j + 2] + y[b].bsums[4 * j + 3]);
                qs += 32;
                q8 += 64;
            }
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
            int isum = 0;
            for (int k = 0; k < 2; ++k)
            {
                int s[8];
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
                    s[0 + h] = dot16(vsubq_s8(vreinterpretq_s8_u8(u1), offset), vld1q_s8(q8 + 16 * h));
                    s[2 + h] = dot16(vsubq_s8(vreinterpretq_s8_u8(u2), offset), vld1q_s8(q8 + 32 + 16 * h));
                    s[4 + h] = dot16(vsubq_s8(vreinterpretq_s8_u8(u3), offset), vld1q_s8(q8 + 64 + 16 * h));
                    s[6 + h] = dot16(vsubq_s8(vreinterpretq_s8_u8(u4), offset), vld1q_s8(q8 + 96 + 16 * h));
                }
                for (int j = 0; j < 8; ++j) isum += sc[j] * s[j];
                ql += 64;
                qh += 32;
                sc += 8;
                q8 += 128;
            }
            sum += d * static_cast<float>(isum);
        }
        return sum;
    }
}
