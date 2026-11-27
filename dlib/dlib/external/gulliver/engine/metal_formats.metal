// Les formats des poids GGUF pour les noyaux Metal (fork pour macOS) : huit valeurs d'un bloc développées en floats.
// Placé par CMakeLists.txt en tête de metal_kernels.metal et de metal_kernels_nax.metal, qui les partagent (une seule
// copie à tenir).
#include <metal_stdlib>
using namespace metal;

// Les valeurs v0 à v0 + 7 du bloc blk développées dans y (fork pour macOS) : les noyaux dequant_* et les
// produits groupés des experts (mm_id) les partagent.  Blocs de 256 valeurs pour Q4_K, Q5_K et Q6_K, de 32 pour
// Q8_0 et Q5_0 ; v0 est un multiple de 8.
static inline void dq8_q4_K(device const uchar* blk, uint v0, thread float* y)
{
    const uint j = v0 / 64, high = (v0 % 64) / 32, l0 = v0 % 32, sub = v0 / 32;
    device const uchar* sc = blk + 4;
    float scale, minimum;
    if (sub < 4)
    {
        scale = sc[sub] & 63;
        minimum = sc[sub + 4] & 63;
    }
    else
    {
        scale = (sc[sub + 4] & 0xF) | ((sc[sub - 4] >> 6) << 4);
        minimum = (sc[sub + 4] >> 4) | ((sc[sub] >> 6) << 4);
    }
    const half2 dd = *(device const half2*)blk;
    const float d = float(dd.x) * scale, m = float(dd.y) * minimum;
    device const uchar* qs = blk + 16 + j * 32 + l0;
    for (uint i = 0; i < 8; ++i)
    {
        const uint q = high ? qs[i] >> 4 : qs[i] & 0xF;
        y[i] = d * float(q) - m;
    }
}

static inline void dq8_q6_K(device const uchar* blk, uint v0, thread float* y)
{
    const uint n = v0 / 128, w = v0 % 128, quarter = w / 32, l0 = w % 32;
    device const uchar* ql = blk + n * 64 + (quarter & 1) * 32 + l0;
    device const uchar* qh = blk + 128 + n * 32 + l0;
    device const char* sc = (device const char*)(blk + 192) + n * 8;
    const float d = float(*(device const half*)(blk + 208));
    for (uint i = 0; i < 8; ++i)
    {
        const uint l = l0 + i;
        const uint low = quarter < 2 ? ql[i] & 0xF : ql[i] >> 4;
        const uint up = (qh[i] >> (2 * quarter)) & 3;
        y[i] = d * float(sc[l / 16 + 2 * quarter]) * float(int(low | (up << 4)) - 32);
    }
}

// Q5_K : comme Q4_K, avec le cinquième bit de qh ; blocs de 176 octets.
static inline void dq8_q5_K(device const uchar* blk, uint v0, thread float* y)
{
    const uint j = v0 / 64, high = (v0 % 64) / 32, l0 = v0 % 32, sub = v0 / 32;
    device const uchar* sc = blk + 4;
    float scale, minimum;
    if (sub < 4)
    {
        scale = sc[sub] & 63;
        minimum = sc[sub + 4] & 63;
    }
    else
    {
        scale = (sc[sub + 4] & 0xF) | ((sc[sub - 4] >> 6) << 4);
        minimum = (sc[sub + 4] >> 4) | ((sc[sub] >> 6) << 4);
    }
    const half2 dd = *(device const half2*)blk;
    const float d = float(dd.x) * scale, m = float(dd.y) * minimum;
    device const uchar* qh = blk + 16 + l0;
    device const uchar* qs = blk + 48 + j * 32 + l0;
    const uint bit = 2 * j + high;
    for (uint i = 0; i < 8; ++i)
    {
        const uint q = (high ? qs[i] >> 4 : qs[i] & 0xF) | (((qh[i] >> bit) & 1) << 4);
        y[i] = d * float(q) - m;
    }
}

static inline void dq8_q8_0(device const uchar* blk, uint v0, thread float* y)
{
    const float d = float(*(device const half*)blk);
    device const char* q = (device const char*)(blk + 2) + v0;
    for (uint i = 0; i < 8; ++i) y[i] = d * float(q[i]);
}

static inline void dq8_q5_0(device const uchar* blk, uint v0, thread float* y)
{
    const float d = float(*(device const half*)blk);
    const uint qh = uint(blk[2]) | (uint(blk[3]) << 8) | (uint(blk[4]) << 16) | (uint(blk[5]) << 24);
    device const uchar* qs = blk + 6;
    for (uint i = 0; i < 8; ++i)
    {
        const uint j = v0 + i;
        const uint nib = j < 16 ? qs[j] & 0xF : qs[j - 16] >> 4;
        y[i] = d * float(int(nib | (((qh >> j) & 1) << 4)) - 16);
    }
}

// Q5_1 (fork pour macOS, experts de Gemma 4 26B-A4B) : blocs de 24 octets, d puis m en demi-flottants, qh (32 bits),
// 16 octets de quartets, rangés comme Q5_0 ; valeur d * q + m, q de 0 à 31.
static inline void dq8_q5_1(device const uchar* blk, uint v0, thread float* y)
{
    const float d = float(*(device const half*)blk), m = float(*(device const half*)(blk + 2));
    const uint qh = uint(blk[4]) | (uint(blk[5]) << 8) | (uint(blk[6]) << 16) | (uint(blk[7]) << 24);
    device const uchar* qs = blk + 8;
    for (uint i = 0; i < 8; ++i)
    {
        const uint j = v0 + i;
        const uint nib = j < 16 ? qs[j] & 0xF : qs[j - 16] >> 4;
        y[i] = d * float(nib | (((qh >> j) & 1) << 4)) + m;
    }
}

// Les formats, pour les noyaux écrits une fois pour tous (values : valeurs d'un bloc, bytes : sa taille).
struct fmt_q4_K { enum { values = 256, bytes = 144 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q4_K(b, v0, y); } };
struct fmt_q5_K { enum { values = 256, bytes = 176 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q5_K(b, v0, y); } };
struct fmt_q6_K { enum { values = 256, bytes = 210 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q6_K(b, v0, y); } };
struct fmt_q8_0 { enum { values = 32, bytes = 34 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q8_0(b, v0, y); } };
struct fmt_q5_0 { enum { values = 32, bytes = 22 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q5_0(b, v0, y); } };
struct fmt_q5_1 { enum { values = 32, bytes = 24 }; static void dq8(device const uchar* b, uint v0, thread float* y) { dq8_q5_1(b, v0, y); } };
