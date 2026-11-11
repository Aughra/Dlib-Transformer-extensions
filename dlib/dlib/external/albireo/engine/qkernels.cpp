// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "qkernels.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <dlib/data_io/gguf_dequantize.h>

#include "thread_pool.h"


// ARM64 with the dot product instructions (Apple Silicon): the NEON kernels of qkernels_neon.inc.
#if defined(__ARM_NEON) && defined(__ARM_FEATURE_DOTPROD)
#define ALBIREO_QK_NEON 1
#include <arm_neon.h>
#endif

// Apple: the products of the long passes through Accelerate (qkernels_accelerate.inc).
#if ALBIREO_QK_NEON && defined(__APPLE__)
#define ALBIREO_QK_ACCELERATE 1
#define ACCELERATE_NEW_LAPACK
#include <vecLib/cblas_new.h>
#endif

/*
    The instructions beyond AVX2, the kernels of the processor engine cpu-avxvnni:
        ALBIREO_QK_VNNI     the dot products of bytes and of 16-bit integers in one
                            instruction, on 256-bit vectors: AVX-VNNI (cpu-avxvnni), or its
                            AVX-512 form (ALBIREO_QK_VNNI_EVEX, which tests these kernels on
                            the processors that have AVX-512 VNNI and lack AVX-VNNI).
    MSVC defines no macro for VNNI: the build names it (ALBIREO_AVX_VNNI).
*/

namespace albireo
{
    using dlib::ggml_type;

    // Half precision

    namespace
    {
        // Conversion table of the portable build, filled before main() runs.
        struct half_table
        {
            float v[65536];
            half_table()
            {
                for (uint32_t h = 0; h < 65536; ++h)
                    v[h] = dlib::gguf_half_to_float(static_cast<uint16_t>(h));
            }
        };
        const half_table half_values;

        inline float h2f(uint16_t h)
        {
            return half_values.v[h];
        }

        inline float h2f(const uint8_t* p)
        {
            uint16_t h;
            std::memcpy(&h, p, 2);
            return h2f(h);
        }

        inline float bf2f(uint16_t h)
        {
            const uint32_t bits = static_cast<uint32_t>(h) << 16;
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return f;
        }
    }

    float qk_half_to_float(uint16_t h) { return h2f(h); }

    uint16_t qk_float_to_half(float f)
    {
        uint32_t x;
        std::memcpy(&x, &f, sizeof(x));
        const uint32_t sign = (x >> 16) & 0x8000u;
        uint32_t mant = x & 0x007FFFFFu;
        const int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF);
        if (exp == 0xFF)
            return static_cast<uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0u));
        int32_t e = exp - 127 + 15;
        if (e >= 0x1F)
            return static_cast<uint16_t>(sign | 0x7C00u);
        if (e <= 0)
        {
            if (e < -10) return static_cast<uint16_t>(sign);
            mant |= 0x00800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - e);
            uint32_t half_mant = mant >> shift;
            const uint32_t rem = mant & ((1u << shift) - 1u);
            const uint32_t halfway = 1u << (shift - 1);
            if (rem > halfway || (rem == halfway && (half_mant & 1u))) ++half_mant;
            return static_cast<uint16_t>(sign | half_mant);
        }
        uint32_t half = sign | (static_cast<uint32_t>(e) << 10) | (mant >> 13);
        const uint32_t rem = mant & 0x1FFFu;
        if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;
        return static_cast<uint16_t>(half);
    }

    void qk_half_to_float_row(const uint16_t* src, float* dst, long n)
    {
        long i = 0;
        for (; i < n; ++i) dst[i] = h2f(src[i]);
    }

    void qk_float_to_half_row(const float* src, uint16_t* dst, long n)
    {
        long i = 0;
        for (; i < n; ++i) dst[i] = qk_float_to_half(src[i]);
    }

    // 8-bit keys and values

    void qk_float_to_q8_row(const float* src, int8_t* dst, uint16_t* scales, long n)
    {
        for (long b = 0; b * 32 < n; ++b)
        {
            const float* x = src + b * 32;
            int8_t* q = dst + b * 32;
            float amax = 0.0f;
            for (long i = 0; i < 32; ++i) amax = std::max(amax, std::fabs(x[i]));
            /*
                The values are quantized with the scale as stored, in half precision, raised to the next half
                when its rounding brought it so far below amax / 127 that the largest value would round beyond
                127 (the subnormal halves of the tiny groups), the values then kept within [-127, 127].
            */
            uint16_t stored = qk_float_to_half(amax / 127.0f);
            if (h2f(stored) * 127.5f < amax) ++stored;
            const float d = h2f(stored);
            const float inv = d > 0.0f ? 1.0f / d : 0.0f;
            scales[b] = stored;
            for (long i = 0; i < 32; ++i)
            {
                const float r = std::nearbyint(x[i] * inv);
                q[i] = static_cast<int8_t>(std::max(-127.0f, std::min(127.0f, r)));
            }
        }
    }

    void qk_q8_to_float_row(const int8_t* src, const uint16_t* scales, float* dst, long n)
    {
        for (long b = 0; b * 32 < n; ++b)
        {
            const float d = h2f(scales[b]);
            const int8_t* q = src + b * 32;
            float* y = dst + b * 32;
            for (long i = 0; i < 32; ++i) y[i] = d * static_cast<float>(q[i]);
        }
    }

    void qk_rotate_row(float* x, long n)
    {
        for (long i = 0; i < n; ++i)
            if ((static_cast<uint32_t>(i) * 2654435761u) >> 31) x[i] = -x[i];
        const long block = n & -n;
        const float norm = 1.0f / std::sqrt(static_cast<float>(block));
        for (long b0 = 0; b0 < n; b0 += block)
        {
            float* y = x + b0;
            for (long len = 1; len < block; len <<= 1)
                for (long i = 0; i < block; i += 2 * len)
                    for (long j = i; j < i + len; ++j)
                    {
                        const float a = y[j], c = y[j + len];
                        y[j] = a + c;
                        y[j + len] = a - c;
                    }
            for (long i = 0; i < block; ++i) y[i] *= norm;
        }
    }

    const char* qk_isa()
    {
#if   ALBIREO_QK_NEON
        return "NEON+dotprod";
#else
        return "portable";
#endif
    }

    // Activation formats

    namespace
    {
        // 32 activations with their scale and the scaled sum of their quanta, which the
        // asymmetric weight formats (_1) need for their minimum.
        struct block_q8_0x
        {
            float d;
            float s;
            int8_t qs[32];
        };
        static_assert(sizeof(block_q8_0x) == 40, "unexpected padding");

        // 256 activations with their scale and the sums of each group of 16, which the
        // k-quant kernels use for the minimums and the offsets of their weights.
        struct block_q8_K
        {
            float d;
            int8_t qs[256];
            int16_t bsums[16];
        };
        static_assert(sizeof(block_q8_K) == 292, "unexpected padding");

        inline int nearest_int(float f)
        {
            return static_cast<int>(std::lrintf(f));
        }

        void quantize_row_q8_0x(const float* x, block_q8_0x* y, long n)
        {
            const long nb = n / 32;
            for (long b = 0; b < nb; ++b, x += 32)
            {
                float amax = 0.0f;
                for (int j = 0; j < 32; ++j) amax = std::max(amax, std::fabs(x[j]));
                const float d = amax / 127.0f;
                const float id = d != 0.0f ? 127.0f / amax : 0.0f;
                y[b].d = d;
                int sum = 0;
                for (int j = 0; j < 32; ++j)
                {
                    const int q = nearest_int(x[j] * id);
                    y[b].qs[j] = static_cast<int8_t>(q);
                    sum += q;
                }
                y[b].s = d * static_cast<float>(sum);
            }
        }

        void quantize_row_q8_K(const float* x, block_q8_K* y, long n)
        {
            const long nb = n / 256;
            for (long b = 0; b < nb; ++b, x += 256)
            {
                float max = 0.0f, amax = 0.0f;
                for (int j = 0; j < 256; ++j)
                {
                    const float ax = std::fabs(x[j]);
                    if (ax > amax) { amax = ax; max = x[j]; }
                }
                if (amax == 0.0f)
                {
                    y[b].d = 0.0f;
                    std::memset(y[b].qs, 0, sizeof(y[b].qs));
                    std::memset(y[b].bsums, 0, sizeof(y[b].bsums));
                    continue;
                }
                // The extreme value maps to -127, as in the reference implementation
                // of the format, which keeps the quanta symmetric.
                const float iscale = -127.0f / max;
                for (int j = 0; j < 256; ++j)
                {
                    const int v = nearest_int(iscale * x[j]);
                    y[b].qs[j] = static_cast<int8_t>(std::min(127, v));
                }
                for (int j = 0; j < 16; ++j)
                {
                    int sum = 0;
                    for (int k = 0; k < 16; ++k) sum += y[b].qs[j * 16 + k];
                    y[b].bsums[j] = static_cast<int16_t>(sum);
                }
                y[b].d = 1.0f / iscale;
            }
        }
    }

    act_format qk_activation_format(ggml_type type)
    {
        switch (type)
        {
        case ggml_type::Q4_0: case ggml_type::Q4_1: case ggml_type::Q5_0: case ggml_type::Q5_1:
        case ggml_type::Q8_0: case ggml_type::IQ4_NL:
            return act_format::q8_0;
        case ggml_type::Q2_K: case ggml_type::Q3_K: case ggml_type::Q4_K: case ggml_type::Q5_K:
        case ggml_type::Q6_K:
            return act_format::q8_k;
        default:
            return act_format::f32;
        }
    }

    bool qk_supported(ggml_type type)
    {
        switch (type)
        {
        case ggml_type::F32: case ggml_type::F16: case ggml_type::BF16:
        case ggml_type::Q4_0: case ggml_type::Q4_1: case ggml_type::Q5_0: case ggml_type::Q5_1:
        case ggml_type::Q8_0: case ggml_type::IQ4_NL:
        case ggml_type::Q2_K: case ggml_type::Q3_K: case ggml_type::Q4_K: case ggml_type::Q5_K:
        case ggml_type::Q6_K:
            return true;
        default:
            return false;
        }
    }

    size_t qk_row_bytes(ggml_type type, long cols)
    {
        const dlib::ggml_type_traits tr = dlib::get_ggml_type_traits(type);
        return static_cast<size_t>(cols) / tr.block_size * tr.type_size;
    }

    size_t qk_activation_row_bytes(act_format f, long n)
    {
        switch (f)
        {
        case act_format::q8_0: return static_cast<size_t>(n / 32) * sizeof(block_q8_0x);
        case act_format::q8_k: return static_cast<size_t>(n / 256) * sizeof(block_q8_K);
        default: return static_cast<size_t>(n) * sizeof(float);
        }
    }

    void qk_quantize_row(act_format f, const float* src, void* dst, long n)
    {
        switch (f)
        {
        case act_format::q8_0: quantize_row_q8_0x(src, static_cast<block_q8_0x*>(dst), n); break;
        case act_format::q8_k: quantize_row_q8_K(src, static_cast<block_q8_K*>(dst), n); break;
        default: std::memcpy(dst, src, static_cast<size_t>(n) * sizeof(float)); break;
        }
    }

    // Portable dot products

    namespace
    {
        const int8_t iq4nl_values[16] = {
            -127, -104, -83, -65, -49, -35, -22, -10, 1, 13, 25, 38, 53, 69, 89, 113
        };

        float dot_q4_0_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 18)
            {
                const uint8_t* qs = w + 2;
                int s = 0;
                for (int j = 0; j < 16; ++j)
                    s += ((qs[j] & 0x0F) - 8) * y[b].qs[j] + ((qs[j] >> 4) - 8) * y[b].qs[j + 16];
                sum += h2f(w) * y[b].d * static_cast<float>(s);
            }
            return sum;
        }

        float dot_q4_1_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 20)
            {
                const uint8_t* qs = w + 4;
                int s = 0;
                for (int j = 0; j < 16; ++j)
                    s += (qs[j] & 0x0F) * y[b].qs[j] + (qs[j] >> 4) * y[b].qs[j + 16];
                sum += h2f(w) * y[b].d * static_cast<float>(s) + h2f(w + 2) * y[b].s;
            }
            return sum;
        }

        float dot_q5_0_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 22)
            {
                uint32_t qh;
                std::memcpy(&qh, w + 2, 4);
                const uint8_t* qs = w + 6;
                int s = 0;
                for (int j = 0; j < 16; ++j)
                {
                    const int h0 = static_cast<int>(((qh >> j) << 4) & 0x10u);
                    const int h1 = static_cast<int>((qh >> (j + 12)) & 0x10u);
                    s += (((qs[j] & 0x0F) | h0) - 16) * y[b].qs[j] + (((qs[j] >> 4) | h1) - 16) * y[b].qs[j + 16];
                }
                sum += h2f(w) * y[b].d * static_cast<float>(s);
            }
            return sum;
        }

        float dot_q5_1_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 24)
            {
                uint32_t qh;
                std::memcpy(&qh, w + 4, 4);
                const uint8_t* qs = w + 8;
                int s = 0;
                for (int j = 0; j < 16; ++j)
                {
                    const int h0 = static_cast<int>(((qh >> j) << 4) & 0x10u);
                    const int h1 = static_cast<int>((qh >> (j + 12)) & 0x10u);
                    s += ((qs[j] & 0x0F) | h0) * y[b].qs[j] + ((qs[j] >> 4) | h1) * y[b].qs[j + 16];
                }
                sum += h2f(w) * y[b].d * static_cast<float>(s) + h2f(w + 2) * y[b].s;
            }
            return sum;
        }

        float dot_q8_0_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 34)
            {
                const int8_t* qs = reinterpret_cast<const int8_t*>(w + 2);
                int s = 0;
                for (int j = 0; j < 32; ++j) s += qs[j] * y[b].qs[j];
                sum += h2f(w) * y[b].d * static_cast<float>(s);
            }
            return sum;
        }

        float dot_iq4_nl_ref(const uint8_t* w, const block_q8_0x* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 32; ++b, w += 18)
            {
                const uint8_t* qs = w + 2;
                int s = 0;
                for (int j = 0; j < 16; ++j)
                    s += iq4nl_values[qs[j] & 0x0F] * y[b].qs[j] + iq4nl_values[qs[j] >> 4] * y[b].qs[j + 16];
                sum += h2f(w) * y[b].d * static_cast<float>(s);
            }
            return sum;
        }

        inline void get_scale_min_k4(int j, const uint8_t* q, int& d, int& m)
        {
            if (j < 4)
            {
                d = q[j] & 63;
                m = q[j + 4] & 63;
            }
            else
            {
                d = (q[j + 4] & 0x0F) | ((q[j - 4] >> 6) << 4);
                m = (q[j + 4] >> 4) | ((q[j] >> 6) << 4);
            }
        }

        float dot_q2_K_ref(const uint8_t* w, const block_q8_K* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 256; ++b, w += 84)
            {
                const uint8_t* scales = w;
                const uint8_t* q = w + 16;
                const float d = h2f(w + 80) * y[b].d;
                const float dmin = h2f(w + 82) * y[b].d;
                // Minimums against the group sums of the activations.
                int summs = 0;
                for (int j = 0; j < 16; ++j) summs += y[b].bsums[j] * (scales[j] >> 4);
                const int8_t* q8 = y[b].qs;
                int isum = 0;
                int is = 0;
                for (int k = 0; k < 2; ++k)
                {
                    int shift = 0;
                    for (int j = 0; j < 4; ++j)
                    {
                        int d1 = scales[is++] & 0x0F;
                        int s1 = 0;
                        for (int l = 0; l < 16; ++l) s1 += q8[l] * ((q[l] >> shift) & 3);
                        int d2 = scales[is++] & 0x0F;
                        int s2 = 0;
                        for (int l = 16; l < 32; ++l) s2 += q8[l] * ((q[l] >> shift) & 3);
                        isum += d1 * s1 + d2 * s2;
                        shift += 2;
                        q8 += 32;
                    }
                    q += 32;
                }
                sum += d * static_cast<float>(isum) - dmin * static_cast<float>(summs);
            }
            return sum;
        }

        float dot_q3_K_ref(const uint8_t* w, const block_q8_K* y, long n)
        {
            const uint32_t kmask1 = 0x03030303u, kmask2 = 0x0f0f0f0fu;
            float sum = 0.0f;
            for (long b = 0; b < n / 256; ++b, w += 110)
            {
                const uint8_t* hm = w;
                const uint8_t* q = w + 32;
                const float d = h2f(w + 108) * y[b].d;
                uint32_t aux[4];
                std::memcpy(aux, w + 96, 12);
                const uint32_t tmp = aux[2];
                aux[2] = ((aux[0] >> 4) & kmask2) | (((tmp >> 4) & kmask1) << 4);
                aux[3] = ((aux[1] >> 4) & kmask2) | (((tmp >> 6) & kmask1) << 4);
                aux[0] = (aux[0] & kmask2) | (((tmp >> 0) & kmask1) << 4);
                aux[1] = (aux[1] & kmask2) | (((tmp >> 2) & kmask1) << 4);
                int8_t scales[16];
                std::memcpy(scales, aux, 16);
                const int8_t* q8 = y[b].qs;
                int isum = 0;
                int is = 0;
                uint8_t m = 1;
                for (int k = 0; k < 2; ++k)
                {
                    int shift = 0;
                    for (int j = 0; j < 4; ++j)
                    {
                        int s1 = 0;
                        for (int l = 0; l < 16; ++l)
                            s1 += q8[l] * (((q[l] >> shift) & 3) - ((hm[l] & m) ? 0 : 4));
                        int s2 = 0;
                        for (int l = 16; l < 32; ++l)
                            s2 += q8[l] * (((q[l] >> shift) & 3) - ((hm[l] & m) ? 0 : 4));
                        isum += (scales[is] - 32) * s1 + (scales[is + 1] - 32) * s2;
                        is += 2;
                        shift += 2;
                        m <<= 1;
                        q8 += 32;
                    }
                    q += 32;
                }
                sum += d * static_cast<float>(isum);
            }
            return sum;
        }

        float dot_q4_K_ref(const uint8_t* w, const block_q8_K* y, long n)
        {
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
                    int s1 = 0, s2 = 0;
                    for (int l = 0; l < 32; ++l)
                    {
                        s1 += (qs[l] & 0x0F) * q8[l];
                        s2 += (qs[l] >> 4) * q8[l + 32];
                    }
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

        float dot_q5_K_ref(const uint8_t* w, const block_q8_K* y, long n)
        {
            float sum = 0.0f;
            for (long b = 0; b < n / 256; ++b, w += 176)
            {
                const float d = h2f(w) * y[b].d;
                const float dmin = h2f(w + 2) * y[b].d;
                const uint8_t* scales = w + 4;
                const uint8_t* qh = w + 16;
                const uint8_t* qs = w + 48;
                const int8_t* q8 = y[b].qs;
                int isum = 0, summ = 0;
                uint8_t u1 = 1, u2 = 2;
                for (int j = 0; j < 4; ++j)
                {
                    int sc1, m1, sc2, m2;
                    get_scale_min_k4(2 * j, scales, sc1, m1);
                    get_scale_min_k4(2 * j + 1, scales, sc2, m2);
                    int s1 = 0, s2 = 0;
                    for (int l = 0; l < 32; ++l)
                    {
                        s1 += ((qs[l] & 0x0F) + ((qh[l] & u1) ? 16 : 0)) * q8[l];
                        s2 += ((qs[l] >> 4) + ((qh[l] & u2) ? 16 : 0)) * q8[l + 32];
                    }
                    isum += sc1 * s1 + sc2 * s2;
                    summ += m1 * (y[b].bsums[4 * j] + y[b].bsums[4 * j + 1])
                          + m2 * (y[b].bsums[4 * j + 2] + y[b].bsums[4 * j + 3]);
                    qs += 32;
                    q8 += 64;
                    u1 <<= 2;
                    u2 <<= 2;
                }
                sum += d * static_cast<float>(isum) - dmin * static_cast<float>(summ);
            }
            return sum;
        }

        float dot_q6_K_ref(const uint8_t* w, const block_q8_K* y, long n)
        {
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
                    int s[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                    for (int l = 0; l < 32; ++l)
                    {
                        const int is = l / 16;
                        const int q1 = ((ql[l] & 0x0F) | (((qh[l] >> 0) & 3) << 4)) - 32;
                        const int q2 = ((ql[l + 32] & 0x0F) | (((qh[l] >> 2) & 3) << 4)) - 32;
                        const int q3 = ((ql[l] >> 4) | (((qh[l] >> 4) & 3) << 4)) - 32;
                        const int q4 = ((ql[l + 32] >> 4) | (((qh[l] >> 6) & 3) << 4)) - 32;
                        s[is + 0] += q1 * q8[l];
                        s[is + 2] += q2 * q8[l + 32];
                        s[is + 4] += q3 * q8[l + 64];
                        s[is + 6] += q4 * q8[l + 96];
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

        float dot_f32_ref(const float* a, const float* b, long n)
        {
            float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
            long i = 0;
            for (; i + 4 <= n; i += 4)
            {
                s0 += a[i] * b[i];
                s1 += a[i + 1] * b[i + 1];
                s2 += a[i + 2] * b[i + 2];
                s3 += a[i + 3] * b[i + 3];
            }
            for (; i < n; ++i) s0 += a[i] * b[i];
            return (s0 + s1) + (s2 + s3);
        }

        float dot_f16_ref(const uint8_t* w, const float* x, long n)
        {
            const uint16_t* h = reinterpret_cast<const uint16_t*>(w);
            float s0 = 0, s1 = 0;
            long i = 0;
            for (; i + 2 <= n; i += 2)
            {
                s0 += h2f(h[i]) * x[i];
                s1 += h2f(h[i + 1]) * x[i + 1];
            }
            for (; i < n; ++i) s0 += h2f(h[i]) * x[i];
            return s0 + s1;
        }

        float dot_bf16_ref(const uint8_t* w, const float* x, long n)
        {
            const uint16_t* h = reinterpret_cast<const uint16_t*>(w);
            float s0 = 0, s1 = 0;
            long i = 0;
            for (; i + 2 <= n; i += 2)
            {
                s0 += bf2f(h[i]) * x[i];
                s1 += bf2f(h[i + 1]) * x[i + 1];
            }
            for (; i < n; ++i) s0 += bf2f(h[i]) * x[i];
            return s0 + s1;
        }
    }

#if ALBIREO_QK_NEON
#include "qkernels_neon.inc"
#endif

// AVX2 dot products


// VNNI dot products: one instruction for the sums of four products of bytes, or of two
// products of 16-bit integers, on 256-bit vectors


    float qk_dot_portable(ggml_type t, const uint8_t* w, const void* act, long n)
    {
        switch (t)
        {
        case ggml_type::Q4_0: return dot_q4_0_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::Q4_1: return dot_q4_1_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::Q5_0: return dot_q5_0_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::Q5_1: return dot_q5_1_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::Q8_0: return dot_q8_0_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::IQ4_NL: return dot_iq4_nl_ref(w, static_cast<const block_q8_0x*>(act), n);
        case ggml_type::Q2_K: return dot_q2_K_ref(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q3_K: return dot_q3_K_ref(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q4_K: return dot_q4_K_ref(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q5_K: return dot_q5_K_ref(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q6_K: return dot_q6_K_ref(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::F32: return dot_f32_ref(reinterpret_cast<const float*>(w), static_cast<const float*>(act), n);
        case ggml_type::F16: return dot_f16_ref(w, static_cast<const float*>(act), n);
        case ggml_type::BF16: return dot_bf16_ref(w, static_cast<const float*>(act), n);
        default:
            throw std::runtime_error("qk_dot: unsupported tensor type "
                + std::to_string(static_cast<unsigned>(t)));
        }
    }

    float qk_dot(ggml_type t, const uint8_t* w, const void* act, long n)
    {
#if ALBIREO_QK_NEON
        switch (t)
        {
        case ggml_type::Q4_K: return dot_q4_K_neon(w, static_cast<const block_q8_K*>(act), n);
        case ggml_type::Q6_K: return dot_q6_K_neon(w, static_cast<const block_q8_K*>(act), n);
        default: break;
        }
#endif
        return qk_dot_portable(t, w, act, n);
    }

    namespace
    {
        /*
            The dot products of one row of weights with the nt <= 4 rows of activations act,
            written to out: the kernels of several rows for the k-quants of this build, which
            unpack the weights once, and qk_dot() row by row for the other formats.  Each
            result is the one of qk_dot(), bit for bit, so that an answer never depends on the
            requests computed with it.
        */
        void dot_rows(ggml_type t, const uint8_t* w, const uint8_t* const* act, long nt, long n, float* out)
        {
            for (long i = 0; i < nt; ++i) out[i] = qk_dot(t, w, act[i], n);
        }

    }

    void qk_dequantize_row(ggml_type t, const uint8_t* w, float* dst, long n)
    {
        typedef void (*block_fn)(const uint8_t*, float*);
        block_fn fn = nullptr;
        long qk = 0, bytes = 0;
        switch (t)
        {
        case ggml_type::F32: std::memcpy(dst, w, static_cast<size_t>(n) * 4); return;
        case ggml_type::F16: qk_half_to_float_row(reinterpret_cast<const uint16_t*>(w), dst, n); return;
        case ggml_type::BF16:
            for (long i = 0; i < n; ++i)
            {
                uint16_t h;
                std::memcpy(&h, w + 2 * i, 2);
                dst[i] = bf2f(h);
            }
            return;
        case ggml_type::Q4_0: fn = dlib::gguf_dq_q4_0; qk = 32; bytes = 18; break;
        case ggml_type::Q4_1: fn = dlib::gguf_dq_q4_1; qk = 32; bytes = 20; break;
        case ggml_type::Q5_0: fn = dlib::gguf_dq_q5_0; qk = 32; bytes = 22; break;
        case ggml_type::Q5_1: fn = dlib::gguf_dq_q5_1; qk = 32; bytes = 24; break;
        case ggml_type::Q8_0: fn = dlib::gguf_dq_q8_0; qk = 32; bytes = 34; break;
        case ggml_type::IQ4_NL: fn = dlib::gguf_dq_iq4_nl; qk = 32; bytes = 18; break;
        case ggml_type::Q2_K: fn = dlib::gguf_dq_q2_K; qk = 256; bytes = 84; break;
        case ggml_type::Q3_K: fn = dlib::gguf_dq_q3_K; qk = 256; bytes = 110; break;
        case ggml_type::Q4_K: fn = dlib::gguf_dq_q4_K; qk = 256; bytes = 144; break;
        case ggml_type::Q5_K: fn = dlib::gguf_dq_q5_K; qk = 256; bytes = 176; break;
        case ggml_type::Q6_K:
            fn = dlib::gguf_dq_q6_K;
            qk = 256;
            bytes = 210;
            break;
        default:
            throw std::runtime_error("qk_dequantize_row: unsupported tensor type "
                + std::to_string(static_cast<unsigned>(t)));
        }
        for (long b = 0; b < n / qk; ++b) fn(w + b * bytes, dst + b * qk);
    }

    // Products

    namespace
    {
        std::atomic<bool> exact_products{false};

        void matmul_exact(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
            float* Y, long y_stride, thread_pool& pool, bool accumulate)
        {
            const long rows = W.rows, n = W.cols;
            const long group = 16;
            pool.parallel_for((rows + group - 1) / group, [&](long g, int) {
                std::vector<float> w(static_cast<size_t>(n));
                const long r1 = std::min(rows, (g + 1) * group);
                for (long r = g * group; r < r1; ++r)
                {
                    qk_dequantize_row(W.type, W.data + static_cast<size_t>(r) * W.row_bytes, w.data(), n);
                    for (long t = 0; t < n_tokens; ++t)
                    {
                        const float v = qk_dot_f32(w.data(), X + t * x_stride, n);
                        float& out = Y[t * y_stride + r];
                        out = accumulate ? out + v : v;
                    }
                }
            });
        }
    }

    void qk_set_exact_products(bool on) { exact_products.store(on); }

    // Products of several tokens on floats

    namespace
    {
        inline void store_result(float* out, float v, bool accumulate)
        {
            *out = accumulate ? *out + v : v;
        }



        // The products of n_rows contiguous float rows of W with n_tokens rows of X,
        // written to the first n_rows columns of Y.
        void gemm_rows(const float* W, long n_rows, long K, const float* X, long n_tokens, long x_stride,
            float* Y, long y_stride, bool accumulate)
        {
            long t = 0;
            long r4 = 0;
#if   ALBIREO_QK_NEON
            r4 = n_rows / 4 * 4;
            for (; t + 4 <= n_tokens; t += 4)
                for (long r = 0; r < r4; r += 4)
                    gemm_neon<4>(X + t * x_stride, x_stride, W + r * K, K, Y + t * y_stride + r, y_stride, accumulate);
            for (long u = t; u < n_tokens; ++u)
                for (long r = 0; r < r4; r += 4)
                    gemm_neon<1>(X + u * x_stride, x_stride, W + r * K, K, Y + u * y_stride + r, y_stride, accumulate);
            t = 0;
#endif
            // What the blocks above leave: every token against the last rows, or all of
            // it in the portable build.
            for (; t < n_tokens; ++t)
                for (long r = r4; r < n_rows; ++r)
                    store_result(Y + t * y_stride + r, qk_dot_f32(W + r * K, X + t * x_stride, K), accumulate);
        }


#if ALBIREO_QK_ACCELERATE
#include "qkernels_accelerate.inc"
#endif

        void matmul_float(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
            float* Y, long y_stride, thread_pool& pool, bool accumulate)
        {
#if ALBIREO_QK_ACCELERATE
            if (n_tokens >= accelerate_min_tokens)
            {
                matmul_accelerate(W, X, n_tokens, x_stride, Y, y_stride, pool, accumulate);
                return;
            }
#endif
            const long rows = W.rows, K = W.cols;
            // A tile of rows is expanded once to floats and serves every token: its
            // floats stay in the second level cache while the tokens go by, in groups
            // small enough for their rows to stay in the first level.
            const long tile_rows = 16;
            const long token_group = 48;
            const long n_tiles = (rows + tile_rows - 1) / tile_rows;
            pool.parallel_for(n_tiles, [&](long tile, int) {
                static thread_local std::vector<float> buffer;
                const long r0 = tile * tile_rows;
                const long n = std::min(tile_rows, rows - r0);
                const float* w = nullptr;
                if (W.type == ggml_type::F32)
                {
                    w = reinterpret_cast<const float*>(W.data + static_cast<size_t>(r0) * W.row_bytes);
                }
                else
                {
                    buffer.resize(static_cast<size_t>(n * K));
                    for (long r = 0; r < n; ++r)
                        qk_dequantize_row(W.type, W.data + static_cast<size_t>(r0 + r) * W.row_bytes,
                            buffer.data() + r * K, K);
                    w = buffer.data();
                }
                for (long t0 = 0; t0 < n_tokens; t0 += token_group)
                    gemm_rows(w, n, K, X + t0 * x_stride, std::min(token_group, n_tokens - t0), x_stride,
                        Y + t0 * y_stride + r0, y_stride, accumulate);
            });
        }
    }

    void qk_matmul(const weight_matrix& W, const float* X, long n_tokens, long x_stride,
        float* Y, long y_stride, thread_pool& pool, qk_scratch& scratch, bool accumulate, qk_path path)
    {
        if (!W.valid() || n_tokens <= 0) return;
        if (exact_products.load(std::memory_order_relaxed))
        {
            matmul_exact(W, X, n_tokens, x_stride, Y, y_stride, pool, accumulate);
            return;
        }
        if (path == qk_path::floats || (path == qk_path::automatic && n_tokens >= qk_float_product_tokens))
        {
            matmul_float(W, X, n_tokens, x_stride, Y, y_stride, pool, accumulate);
            return;
        }
        const act_format f = qk_activation_format(W.type);
        const long n = W.cols;
        const size_t act_bytes = qk_activation_row_bytes(f, n);

        // The activations, quantized once for all the rows of the matrix.
        const uint8_t* act = nullptr;
        if (f == act_format::f32 && x_stride == n)
        {
            act = reinterpret_cast<const uint8_t*>(X);
        }
        else
        {
            scratch.act.resize(act_bytes * static_cast<size_t>(n_tokens));
            uint8_t* dst = scratch.act.data();
            if (n_tokens >= 8)
            {
                pool.parallel_for(n_tokens, [&](long t, int) {
                    qk_quantize_row(f, X + t * x_stride, dst + static_cast<size_t>(t) * act_bytes, n);
                });
            }
            else
            {
                for (long t = 0; t < n_tokens; ++t)
                    qk_quantize_row(f, X + t * x_stride, dst + static_cast<size_t>(t) * act_bytes, n);
            }
            act = dst;
        }

        // Groups of output rows, enough of them to balance the threads, few enough for
        // each group to amortize its hand-over.
        const long rows = W.rows;
        const long threads = pool.active();
        long group = std::max<long>(1, rows / (threads * 8));
        group = std::min<long>(group, 128);
        if (n_tokens > 1) group = std::max<long>(1, std::min<long>(group, 32));
        const long n_groups = (rows + group - 1) / group;
        const ggml_type type = W.type;
        const uint8_t* data = W.data;
        const size_t row_bytes = W.row_bytes;

        // With several tokens, the tokens go by tiles: the rows of a group and the
        // activations of a tile then stay in the caches while every pair is computed.
        const long tile = 16;
        pool.parallel_for(n_groups, [&](long g, int) {
            const long r0 = g * group;
            const long r1 = std::min(rows, r0 + group);
            for (long t0 = 0; t0 < n_tokens; t0 += tile)
            {
                const long t1 = std::min(n_tokens, t0 + tile);
                for (long r = r0; r < r1; ++r)
                {
                    const uint8_t* wrow = data + static_cast<size_t>(r) * row_bytes;
                    // Up to four tokens at a time, for which the weights of the row are unpacked
                    // once; a token left alone goes to qk_dot() directly.
                    long t = t0;
                    for (; t + 1 < t1; t += 4)
                    {
                        const long nt = std::min<long>(4, t1 - t);
                        const uint8_t* rows[4];
                        float v[4];
                        for (long i = 0; i < nt; ++i) rows[i] = act + static_cast<size_t>(t + i) * act_bytes;
                        dot_rows(type, wrow, rows, nt, n, v);
                        for (long i = 0; i < nt; ++i)
                        {
                            float& out = Y[(t + i) * y_stride + r];
                            out = accumulate ? out + v[i] : v[i];
                        }
                    }
                    for (; t < t1; ++t)
                    {
                        const float v = qk_dot(type, wrow, act + static_cast<size_t>(t) * act_bytes, n);
                        float& out = Y[t * y_stride + r];
                        out = accumulate ? out + v : v;
                    }
                }
            }
        });
    }

    // Vector helpers

    float qk_dot_f32(const float* a, const float* b, long n)
    {
        return dot_f32_ref(a, b, n);
    }

    float qk_dot_f32_f16(const float* a, const uint16_t* b, long n)
    {
        return dot_f16_ref(reinterpret_cast<const uint8_t*>(b), a, n);
    }

    void qk_axpy_f16(float alpha, const uint16_t* x, float* y, long n)
    {
        long i = 0;
        for (; i < n; ++i) y[i] += alpha * h2f(x[i]);
    }

    void qk_scale(float* x, float s, long n)
    {
        long i = 0;
        for (; i < n; ++i) x[i] *= s;
    }

    float qk_max(const float* x, long n)
    {
        float m = -INFINITY;
        long i = 0;
        for (; i < n; ++i) m = std::max(m, x[i]);
        return m;
    }


    float qk_exp_shift_sum(float* x, float m, long n)
    {
        float sum = 0.0f;
        long i = 0;
        for (; i < n; ++i)
        {
            x[i] = std::exp(x[i] - m);
            sum += x[i];
        }
        return sum;
    }

    void qk_silu_mul(float* g, const float* u, long n)
    {
        long i = 0;
        for (; i < n; ++i) g[i] = g[i] / (1.0f + std::exp(-g[i])) * u[i];
    }

    // Attention by tiles

    namespace
    {
        // The keys or the values converted to floats at a time: a few kilobytes, which stay
        // in the first level cache while every row of the tile reads them.
        inline long attention_group(long hd)
        {
            return std::max<long>(4, std::min<long>(64, 4096 / std::max<long>(1, hd)) / 4 * 4);
        }


        /*
            The dot products of R rows of queries (R being 1 or 2) with four consecutive keys
            of hd floats: s[r][c] = dot(q[r], k + c * hd).
        */
        template <int R>
        void dot_rows_x4(const float* const* q, const float* k, long hd, float* const* s)
        {
            long i = 0;
            for (int r = 0; r < R; ++r)
                for (int c = 0; c < 4; ++c)
                {
                    float t = 0;
                    for (i = 0; i < hd; ++i) t += q[r][i] * k[c * hd + i];
                    s[r][c] = t;
                }
        }

        /*
            The values of g keys weighted for R rows (R being 1 or 2) and added to the rows of
            the output: o[r][i] += sum over j < g of p[r][j] * v[j * hd + i].
        */
        template <int R>
        void add_weighted_rows(const float* const* p, const float* v, long g, long hd, float* const* o)
        {
            long i = 0;
            for (; i < hd; ++i)
                for (int r = 0; r < R; ++r)
                {
                    float t = o[r][i];
                    for (long j = 0; j < g; ++j) t += p[r][j] * v[j * hd + i];
                    o[r][i] = t;
                }
        }
    }

    long qk_attention_scratch(long hd)
    {
        return attention_group(hd) * hd;
    }

    namespace
    {
        // The scores of the rows of queries over m keys, which expand(j0, g, to) gives as floats g at a time.
        template <typename Expand>
        void attention_scores(const float* Q, long rows, long m, long hd, float* S, long lds, float* scratch,
            const Expand& expand)
        {
            const long G = attention_group(hd);
            for (long j0 = 0; j0 < m; j0 += G)
            {
                const long g = std::min(G, m - j0);
                expand(j0, g, scratch);
                long r = 0;
                for (; r + 2 <= rows; r += 2)
                {
                    const float* q[2] = { Q + r * hd, Q + (r + 1) * hd };
                    long j = 0;
                    for (; j + 4 <= g; j += 4)
                    {
                        float* s[2] = { S + r * lds + j0 + j, S + (r + 1) * lds + j0 + j };
                        dot_rows_x4<2>(q, scratch + j * hd, hd, s);
                    }
                    for (; j < g; ++j)
                        for (int e = 0; e < 2; ++e) S[(r + e) * lds + j0 + j] = qk_dot_f32(q[e], scratch + j * hd, hd);
                }
                for (; r < rows; ++r)
                {
                    const float* q[1] = { Q + r * hd };
                    long j = 0;
                    for (; j + 4 <= g; j += 4)
                    {
                        float* s[1] = { S + r * lds + j0 + j };
                        dot_rows_x4<1>(q, scratch + j * hd, hd, s);
                    }
                    for (; j < g; ++j) S[r * lds + j0 + j] = qk_dot_f32(q[0], scratch + j * hd, hd);
                }
            }
        }

        // The values of m keys weighted and added to the rows of the output, expand giving them as above.
        template <typename Expand>
        void attention_values(const float* P, long lds, long rows, long m, long hd, float* O, float* scratch,
            const Expand& expand)
        {
            const long G = attention_group(hd);
            for (long j0 = 0; j0 < m; j0 += G)
            {
                const long g = std::min(G, m - j0);
                expand(j0, g, scratch);
                long r = 0;
                for (; r + 2 <= rows; r += 2)
                {
                    const float* p[2] = { P + r * lds + j0, P + (r + 1) * lds + j0 };
                    float* o[2] = { O + r * hd, O + (r + 1) * hd };
                    add_weighted_rows<2>(p, scratch, g, hd, o);
                }
                for (; r < rows; ++r)
                {
                    const float* p[1] = { P + r * lds + j0 };
                    float* o[1] = { O + r * hd };
                    add_weighted_rows<1>(p, scratch, g, hd, o);
                }
            }
        }
    }

    void qk_attention_scores(const float* Q, long rows, const uint16_t* K, long m, long hd, float* S, long lds,
        float* scratch)
    {
#if ALBIREO_QK_NEON
        attention_scores_neon(Q, rows, K, m, hd, S, lds, scratch, attention_group(hd));
        return;
#endif
        attention_scores(Q, rows, m, hd, S, lds, scratch,
            [=](long j0, long g, float* to) { qk_half_to_float_row(K + j0 * hd, to, g * hd); });
    }

    void qk_attention_values(const float* P, long lds, long rows, const uint16_t* V, long m, long hd, float* O,
        float* scratch)
    {
#if ALBIREO_QK_NEON
        attention_values_neon(P, lds, rows, V, m, hd, O, scratch, attention_group(hd));
        return;
#endif
        attention_values(P, lds, rows, m, hd, O, scratch,
            [=](long j0, long g, float* to) { qk_half_to_float_row(V + j0 * hd, to, g * hd); });
    }

    void qk_attention_scores_q8(const float* Q, long rows, const int8_t* K, const uint16_t* K_scales, long m, long hd,
        float* S, long lds, float* scratch)
    {
#if ALBIREO_QK_NEON
        attention_scores_q8_neon(Q, rows, K, K_scales, m, hd, S, lds, scratch, attention_group(hd));
        return;
#endif
        attention_scores(Q, rows, m, hd, S, lds, scratch, [=](long j0, long g, float* to) {
            qk_q8_to_float_row(K + j0 * hd, K_scales + j0 * (hd / 32), to, g * hd);
        });
    }

    void qk_attention_values_q8(const float* P, long lds, long rows, const int8_t* V, const uint16_t* V_scales, long m,
        long hd, float* O, float* scratch)
    {
#if ALBIREO_QK_NEON
        attention_values_q8_neon(P, lds, rows, V, V_scales, m, hd, O, scratch, attention_group(hd));
        return;
#endif
        attention_values(P, lds, rows, m, hd, O, scratch, [=](long j0, long g, float* to) {
            qk_q8_to_float_row(V + j0 * hd, V_scales + j0 * (hd / 32), to, g * hd);
        });
    }

    // Encoders

    void qk_layer_norm(const float* x, const float* gamma, const float* beta, float* y, long n, float eps)
    {
        float sum = 0.0f;
        long i = 0;
        for (; i < n; ++i) sum += x[i];
        const float mean = sum / static_cast<float>(n);
        float squares = 0.0f;
        i = 0;
        for (; i < n; ++i) squares += (x[i] - mean) * (x[i] - mean);
        const float inv = 1.0f / std::sqrt(squares / static_cast<float>(n) + eps);
        i = 0;
        for (; i < n; ++i) y[i] = (x[i] - mean) * inv * gamma[i] + beta[i];
    }

    void qk_gelu_tanh(float* x, long n)
    {
        const float c = 0.7978845608028654f;   // sqrt(2 / pi)
        const float a = 0.044715f;
        long i = 0;
        for (; i < n; ++i)
        {
            const float v = x[i];
            x[i] = v / (1.0f + std::exp(-2.0f * c * (v + a * v * v * v)));
        }
    }

    namespace
    {
        // erf(z) for z >= 0 after Abramowitz and Stegun 7.1.26: 1 - t P(t) exp(-z^2) with
        // t = 1 / (1 + p z), within 1.5e-7.
        const float erf_p = 0.3275911f;
        const float erf_a1 = 0.254829592f, erf_a2 = -0.284496736f, erf_a3 = 1.421413741f;
        const float erf_a4 = -1.453152027f, erf_a5 = 1.061405429f;

        inline float erf_positive(float z)
        {
            const float t = 1.0f / (1.0f + erf_p * z);
            const float poly = t * (erf_a1 + t * (erf_a2 + t * (erf_a3 + t * (erf_a4 + t * erf_a5))));
            return 1.0f - poly * std::exp(-z * z);
        }
    }

    void qk_gelu_erf(float* x, long n)
    {
        const float s = 0.7071067811865476f;   // 1 / sqrt(2)
        long i = 0;
        for (; i < n; ++i)
        {
            const float v = x[i];
            const float e = erf_positive(std::fabs(v) * s);
            x[i] = 0.5f * v * (1.0f + (v < 0.0f ? -e : e));
        }
    }

    void qk_add(float* y, const float* x, long n)
    {
        long i = 0;
        for (; i < n; ++i) y[i] += x[i];
    }

    namespace
    {
        // The keys of the encoder attention read at a time: the scores of a tile of queries
        // against them take 32 kilobytes.
        const long encoder_block_keys = 128;


        /*
            S[r * lds + j] = sum over d < hd of Q[r * hd + d] * Kt[d * ldk + j], for R rows and the
            n keys of a group: exactly 16 with AVX2, any number otherwise.
            Each key of Kt is read once for the R rows, without any horizontal sum.
        */
        template <int R>
        void encoder_scores_group(const float* Q, long hd, const float* Kt, long ldk, long n, float* S, long lds)
        {
            for (int r = 0; r < R; ++r)
                for (long j = 0; j < n; ++j)
                {
                    float t = 0.0f;
                    for (long d = 0; d < hd; ++d) t += Q[r * hd + d] * Kt[d * ldk + j];
                    S[r * lds + j] = t;
                }
        }

        void encoder_scores(const float* Q, long rows, long hd, const float* Kt, long ldk, long n, float* S, long lds)
        {
#if ALBIREO_QK_ACCELERATE
            encoder_scores_accelerate(Q, rows, hd, Kt, ldk, n, S, lds);
            return;
#endif
            const long G = 16;
            for (long r = 0; r < rows; r += 6)
            {
                const long R = std::min<long>(6, rows - r);
                const float* q = Q + r * hd;
                for (long j = 0; j < n; j += G)
                {
                    const long g = std::min(G, n - j);
                    float* s = S + r * lds + j;
                    switch (R)
                    {
                    case 6: encoder_scores_group<6>(q, hd, Kt + j, ldk, g, s, lds); break;
                    case 5: encoder_scores_group<5>(q, hd, Kt + j, ldk, g, s, lds); break;
                    case 4: encoder_scores_group<4>(q, hd, Kt + j, ldk, g, s, lds); break;
                    case 3: encoder_scores_group<3>(q, hd, Kt + j, ldk, g, s, lds); break;
                    case 2: encoder_scores_group<2>(q, hd, Kt + j, ldk, g, s, lds); break;
                    default: encoder_scores_group<1>(q, hd, Kt + j, ldk, g, s, lds); break;
                    }
                }
            }
        }

        /*
            O[r * ldo + i] += sum over j < n of P[r * lds + j] * V[j * ldv + i], for R rows and
            the w dimensions of a group: exactly 32 with AVX2, any number otherwise.  Each row of
            V is read once for the R rows.
        */
        template <int R>
        void encoder_values_group(const float* P, long lds, const float* V, long ldv, long n, long w, float* O,
            long ldo)
        {
            for (int r = 0; r < R; ++r)
                for (long j = 0; j < n; ++j)
                {
                    const float p = P[r * lds + j];
                    for (long i = 0; i < w; ++i) O[r * ldo + i] += p * V[j * ldv + i];
                }
        }

        void encoder_values(const float* P, long lds, long rows, const float* V, long ldv, long n, long hd, float* O,
            long ldo)
        {
#if ALBIREO_QK_ACCELERATE
            encoder_values_accelerate(P, lds, rows, V, ldv, n, hd, O, ldo);
            return;
#endif
            const long RG = 1, W = hd;
            for (long r = 0; r < rows; r += RG)
            {
                const long R = std::min(RG, rows - r);
                for (long i = 0; i < hd; i += W)
                {
                    const long w = std::min(W, hd - i);
                    const float* p = P + r * lds;
                    float* o = O + r * ldo + i;
                    // The portable build has no vector form for several rows: one row at a time.
                    (void)R;
                    encoder_values_group<1>(p, lds, V + i, ldv, n, w, o, ldo);
                }
            }
        }
    }

    long qk_encoder_attention_work(long)
    {
        return qk_encoder_tile_rows * encoder_block_keys + 2 * qk_encoder_tile_rows;
    }

    void qk_encoder_attention(const float* Q, long rows, const float* Kt, long ldk, const float* V, long ldv, long m,
        long hd, float* O, long ldo, float* work)
    {
        const long B = encoder_block_keys;
        float* S = work;
        float* top = work + qk_encoder_tile_rows * B;
        float* sum = top + qk_encoder_tile_rows;
        for (long r = 0; r < rows; ++r)
        {
            std::fill(O + r * ldo, O + r * ldo + hd, 0.0f);
            top[r] = -INFINITY;
            sum[r] = 0.0f;
        }
        for (long j0 = 0; j0 < m; j0 += B)
        {
            const long b = std::min(B, m - j0);
            encoder_scores(Q, rows, hd, Kt + j0, ldk, b, S, B);
            for (long r = 0; r < rows; ++r)
            {
                float* s = S + r * B;
                const float mx = std::max(top[r], qk_max(s, b));
                if (mx > top[r] && sum[r] > 0.0f)
                {
                    // A larger maximum: what the row gathered so far shrinks.
                    const float shrink = std::exp(top[r] - mx);
                    sum[r] *= shrink;
                    qk_scale(O + r * ldo, shrink, hd);
                }
                top[r] = mx;
                sum[r] += qk_exp_shift_sum(s, mx, b);
            }
            encoder_values(S, B, rows, V + j0 * ldv, ldv, b, hd, O, ldo);
        }
        for (long r = 0; r < rows; ++r) qk_scale(O + r * ldo, 1.0f / sum[r], hd);
    }

}
