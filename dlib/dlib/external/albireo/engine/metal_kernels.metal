// Noyaux Metal du portage macOS : le GPU du Mac lit les poids GGUF là où se trouve le fichier
// (la mémoire est partagée avec le processeur) et calcule les passes de la génération.
// macOS compile ce source quand le moteur ouvre le GPU (metal_device.mm) : aucun compilateur
// Metal n'est nécessaire pour construire le moteur.  CMakeLists.txt l'embarque dans le moteur
// sous forme de chaîne.
//
// Chaque noyau calcule ce que calcule le runtime du processeur (cpu_runtime.cpp, qkernels.cpp),
// en floats : les sommes viennent dans un autre ordre, si bien que les résultats concordent aux
// arrondis des floats près, pas bit pour bit.

#include <metal_stdlib>
using namespace metal;

// Le nombre maximal de tokens d'une passe (les étapes de génération de huit séquences au plus).
#define MAX_TOKENS 8

// ---------------------------------------------------------------------------------------------
// Produits d'une matrice de poids (blocs GGUF, lus en place) avec les activations de MAX_TOKENS
// tokens au plus : y[t][r] = alpha * sum_i W[r][i] x[t][i] + beta * y[t][r].  Un simdgroup
// calcule ROWS lignes, un threadgroup contient SG_PER_TG simdgroups.  Un dispatch peut calculer
// deux matrices de même format et de mêmes colonnes, W (rows lignes, dans y) puis W2 (rows2
// lignes, dans y2) : les produits des requêtes et des clés, ou de la porte et de la branche up,
// lisent les mêmes activations, et un dispatch coûte plus que ses quelques lignes sur ce GPU.

#define ROWS 4
#define SG_PER_TG 4

struct mv_args
{
    long rows;
    long cols;
    uint tokens;
    uint x_stride;
    uint y_stride;
    float alpha;
    float beta;
    uint y2_stride;
    long rows2;
};

// La matrice des lignes d'un simdgroup : W, ou W2 au-delà des lignes de W (un multiple de ROWS).
struct mv_target
{
    device const uchar* W;
    device float* y;
    long rows;
    uint y_stride;
    long r0;
};

static inline mv_target mv_select(device const uchar* W, device float* y, device const uchar* W2, device float* y2,
    constant mv_args& a, uint tg, uint sg)
{
    mv_target m = { W, y, a.rows, a.y_stride, (long(tg) * SG_PER_TG + sg) * ROWS };
    if (m.r0 >= a.rows)
    {
        m.W = W2;
        m.y = y2;
        m.r0 -= a.rows;
        m.rows = a.rows2;
        m.y_stride = a.y2_stride;
    }
    return m;
}

template <uint NT>
static inline void store_rows(thread const mv_target& m, constant mv_args& a, uint lane,
    thread float (&acc)[ROWS][NT])
{
    for (int r = 0; r < ROWS; ++r)
        for (uint t = 0; t < NT; ++t)
        {
            if (NT > 1 && t >= a.tokens) break;
            const float v = simd_sum(acc[r][t]);
            const long row = m.r0 + r;
            if (lane == 0 && row < m.rows)
            {
                device float* out = m.y + t * m.y_stride + row;
                *out = a.beta != 0.0f ? a.alpha * v + a.beta * *out : a.alpha * v;
            }
        }
}

// Q4_K : 8 threads par bloc de 256 valeurs, 16 octets de quants chacun (un chargement aligné),
// 4 blocs par itération du simdgroup ; les échelles et les minimums décodés sans branchement. Les
// tokens passent à l'extérieur des lignes, pour qu'un token lise ses activations une fois par bloc.
template <uint NT>
kernel void mv_q4_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const mv_target m = mv_select(W0, y0, W2, y2, a, tg, sg);
    if (m.r0 >= m.rows) return;
    device const uchar* W = m.W;
    const long r0 = m.r0;
    const long nb = a.cols / 256;
    const long row_bytes = nb * 144;
    const int k = lane % 8;              // les 16 octets k du bloc
    const int j = k / 2;                 // le morceau de 32 octets : sous-blocs 2j (bits bas) et 2j + 1 (bits hauts)
    const int p = (k % 2) * 16;          // la position dans le sous-bloc
    const int s1 = 2 * j, s2 = 2 * j + 1;
    float acc[ROWS][NT];
    for (int r = 0; r < ROWS; ++r)
        for (uint t = 0; t < NT; ++t) acc[r][t] = 0.0f;
    for (long b = lane / 8; b < nb; b += 4)
        for (uint t = 0; t < NT; ++t)
        {
            if (NT > 1 && t >= a.tokens) break;
            device const float4* xl = (device const float4*)(x + t * a.x_stride + b * 256 + 64 * j + p);
            device const float4* xh = xl + 8;
            float4 vl[4], vh[4];
            float slo = 0, shi = 0;
            for (int i = 0; i < 4; ++i)
            {
                vl[i] = xl[i];
                vh[i] = xh[i];
                slo += vl[i].x + vl[i].y + vl[i].z + vl[i].w;
                shi += vh[i].x + vh[i].y + vh[i].z + vh[i].w;
            }
            for (int r = 0; r < ROWS; ++r)
            {
                const long row = r0 + r;
                if (row >= m.rows) break;
                device const uchar* blk = W + row * row_bytes + b * 144;
                device const uint* u = (device const uint*)(blk + 4);
                const uint u0 = u[0], u1 = u[1], u2 = u[2];
                // Les échelles et les minimums des sous-blocs 2j et 2j + 1, tels que get_scale_min_k4 les décode.
                const uint sc_lo = u0 & 0x3F3F3F3F, sc_hi = (u2 & 0x0F0F0F0F) | (((u0 >> 6) & 0x03030303) << 4);
                const uint mn_lo = u1 & 0x3F3F3F3F, mn_hi = ((u2 >> 4) & 0x0F0F0F0F) | (((u1 >> 6) & 0x03030303) << 4);
                const float sc1 = float(((s1 < 4 ? sc_lo : sc_hi) >> (8 * (s1 & 3))) & 0xFF);
                const float sc2 = float(((s2 < 4 ? sc_lo : sc_hi) >> (8 * (s2 & 3))) & 0xFF);
                const float m1 = float(((s1 < 4 ? mn_lo : mn_hi) >> (8 * (s1 & 3))) & 0xFF);
                const float m2 = float(((s2 < 4 ? mn_lo : mn_hi) >> (8 * (s2 & 3))) & 0xFF);
                const half2 dd = *(device const half2*)blk;
                device const uchar4* qv = (device const uchar4*)(blk + 16 + 16 * k);
                float plo = 0, phi = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const uchar4 q = qv[i];
                    plo += dot(float4(q & uchar(0xF)), vl[i]);
                    phi += dot(float4(q >> uchar(4)), vh[i]);
                }
                acc[r][t] += float(dd.x) * (sc1 * plo + sc2 * phi) - float(dd.y) * (m1 * slo + m2 * shi);
            }
        }
    store_rows(m, a, lane, acc);
}

// Q6_K : l'unité u d'une ligne est le bloc u / 4, la moitié n = (u / 2) % 2, les valeurs l0 à l0 + 15
// de chaque quart, l0 = (u % 2) * 16 ; les blocs de 210 octets sont lus octet par octet, leur
// alignement étant de 2.  Les tokens passent à l'extérieur des lignes, comme pour Q4_K.
template <uint NT>
kernel void mv_q6_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const mv_target m = mv_select(W0, y0, W2, y2, a, tg, sg);
    if (m.r0 >= m.rows) return;
    device const uchar* W = m.W;
    const long r0 = m.r0;
    const long nb = a.cols / 256;
    const long row_bytes = nb * 210;
    float acc[ROWS][NT];
    for (int r = 0; r < ROWS; ++r)
        for (uint t = 0; t < NT; ++t) acc[r][t] = 0.0f;
    for (long u = lane; u < nb * 4; u += 32)
    {
        const long b = u / 4;
        const int n = int((u / 2) % 2);
        const int l0 = int(u % 2) * 16;
        const int is = l0 / 16;
        for (uint t = 0; t < NT; ++t)
        {
            if (NT > 1 && t >= a.tokens) break;
            device const float* xb = x + t * a.x_stride + b * 256 + n * 128 + l0;
            float x1[16], x2[16], x3[16], x4[16];
            for (int l = 0; l < 16; ++l)
            {
                x1[l] = xb[l];
                x2[l] = xb[l + 32];
                x3[l] = xb[l + 64];
                x4[l] = xb[l + 96];
            }
            for (int r = 0; r < ROWS; ++r)
            {
                const long row = r0 + r;
                if (row >= m.rows) break;
                device const uchar* blk = W + row * row_bytes + b * 210;
                device const uchar* ql = blk + n * 64 + l0;
                device const uchar* qh = blk + 128 + n * 32 + l0;
                device const char* sc = (device const char*)(blk + 192) + n * 8;
                const float d = float(*(device const half*)(blk + 208));
                float c1 = 0, c2 = 0, c3 = 0, c4 = 0;
                for (int l = 0; l < 16; ++l)
                {
                    const int qa = ql[l], qc = ql[l + 32], h = qh[l];
                    c1 += float(((qa & 0xF) | ((h & 3) << 4)) - 32) * x1[l];
                    c2 += float(((qc & 0xF) | (((h >> 2) & 3) << 4)) - 32) * x2[l];
                    c3 += float(((qa >> 4) | (((h >> 4) & 3) << 4)) - 32) * x3[l];
                    c4 += float(((qc >> 4) | (((h >> 6) & 3) << 4)) - 32) * x4[l];
                }
                acc[r][t] += d * (float(sc[is]) * c1 + float(sc[is + 2]) * c2 + float(sc[is + 4]) * c3
                    + float(sc[is + 6]) * c4);
            }
        }
    }
    store_rows(m, a, lane, acc);
}

// Q5_K (fork pour macOS, qwen35) : comme Q4_K, avec le cinquième bit de chaque valeur dans qh (32 octets) :
// la valeur l du morceau j prend le bit 2j (quartet bas) ou 2j + 1 (quartet haut) de qh[l].  Blocs de 176
// octets : d, dmin, 12 octets d'échelles, qh, puis 128 octets de quartets.
template <uint NT>
kernel void mv_q5_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const mv_target m = mv_select(W0, y0, W2, y2, a, tg, sg);
    if (m.r0 >= m.rows) return;
    device const uchar* W = m.W;
    const long r0 = m.r0;
    const long nb = a.cols / 256;
    const long row_bytes = nb * 176;
    const int k = lane % 8;
    const int j = k / 2;
    const int p = (k % 2) * 16;
    const int s1 = 2 * j, s2 = 2 * j + 1;
    const uchar b_lo = uchar(2 * j), b_hi = uchar(2 * j + 1);
    float acc[ROWS][NT];
    for (int r = 0; r < ROWS; ++r)
        for (uint t = 0; t < NT; ++t) acc[r][t] = 0.0f;
    for (long b = lane / 8; b < nb; b += 4)
        for (uint t = 0; t < NT; ++t)
        {
            if (NT > 1 && t >= a.tokens) break;
            device const float4* xl = (device const float4*)(x + t * a.x_stride + b * 256 + 64 * j + p);
            device const float4* xh = xl + 8;
            float4 vl[4], vh[4];
            float slo = 0, shi = 0;
            for (int i = 0; i < 4; ++i)
            {
                vl[i] = xl[i];
                vh[i] = xh[i];
                slo += vl[i].x + vl[i].y + vl[i].z + vl[i].w;
                shi += vh[i].x + vh[i].y + vh[i].z + vh[i].w;
            }
            for (int r = 0; r < ROWS; ++r)
            {
                const long row = r0 + r;
                if (row >= m.rows) break;
                device const uchar* blk = W + row * row_bytes + b * 176;
                device const uint* u = (device const uint*)(blk + 4);
                const uint u0 = u[0], u1 = u[1], u2 = u[2];
                const uint sc_lo = u0 & 0x3F3F3F3F, sc_hi = (u2 & 0x0F0F0F0F) | (((u0 >> 6) & 0x03030303) << 4);
                const uint mn_lo = u1 & 0x3F3F3F3F, mn_hi = ((u2 >> 4) & 0x0F0F0F0F) | (((u1 >> 6) & 0x03030303) << 4);
                const float sc1 = float(((s1 < 4 ? sc_lo : sc_hi) >> (8 * (s1 & 3))) & 0xFF);
                const float sc2 = float(((s2 < 4 ? sc_lo : sc_hi) >> (8 * (s2 & 3))) & 0xFF);
                const float m1 = float(((s1 < 4 ? mn_lo : mn_hi) >> (8 * (s1 & 3))) & 0xFF);
                const float m2 = float(((s2 < 4 ? mn_lo : mn_hi) >> (8 * (s2 & 3))) & 0xFF);
                const half2 dd = *(device const half2*)blk;
                device const uchar4* qv = (device const uchar4*)(blk + 48 + 16 * k);
                device const uchar4* hv = (device const uchar4*)(blk + 16 + p);
                float plo = 0, phi = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const uchar4 q = qv[i], h = hv[i];
                    plo += dot(float4(q & uchar(0xF)) + 16.0f * float4((h >> b_lo) & uchar(1)), vl[i]);
                    phi += dot(float4(q >> uchar(4)) + 16.0f * float4((h >> b_hi) & uchar(1)), vh[i]);
                }
                acc[r][t] += float(dd.x) * (sc1 * plo + sc2 * phi) - float(dd.y) * (m1 * slo + m2 * shi);
            }
        }
    store_rows(m, a, lane, acc);
}

// Q8_0 (fork pour macOS, qwen35) : blocs de 34 octets, d en demi-flottant puis 32 entiers signés, lus octet
// par octet (alignement de 2) ; une voie du simdgroup par bloc.
template <uint NT>
kernel void mv_q8_0(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const mv_target m = mv_select(W0, y0, W2, y2, a, tg, sg);
    if (m.r0 >= m.rows) return;
    device const uchar* W = m.W;
    const long r0 = m.r0;
    const long nb = a.cols / 32;
    const long row_bytes = nb * 34;
    float acc[ROWS][NT];
    for (int r = 0; r < ROWS; ++r)
        for (uint t = 0; t < NT; ++t) acc[r][t] = 0.0f;
    for (long b = lane; b < nb; b += 32)
        for (uint t = 0; t < NT; ++t)
        {
            if (NT > 1 && t >= a.tokens) break;
            device const float* xb = x + t * a.x_stride + b * 32;
            for (int r = 0; r < ROWS; ++r)
            {
                const long row = r0 + r;
                if (row >= m.rows) break;
                device const uchar* blk = W + row * row_bytes + b * 34;
                device const char* q = (device const char*)(blk + 2);
                float s = 0;
                for (int l = 0; l < 32; ++l) s += float(q[l]) * xb[l];
                acc[r][t] += float(*(device const half*)blk) * s;
            }
        }
    store_rows(m, a, lane, acc);
}

// Un token (une étape de génération), ou jusqu'à MAX_TOKENS, les accumulateurs de chaque variante gardés
// dans des registres.
template [[host_name("mv_q4_K_1")]] kernel void mv_q4_K<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q4_K_n")]] kernel void mv_q4_K<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q6_K_1")]] kernel void mv_q6_K<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q6_K_n")]] kernel void mv_q6_K<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q5_K_1")]] kernel void mv_q5_K<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q5_K_n")]] kernel void mv_q5_K<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q8_0_1")]] kernel void mv_q8_0<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q8_0_n")]] kernel void mv_q8_0<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);

// ---------------------------------------------------------------------------------------------
// Les poids développés en floats, pour les produits des prompts (metal_device::gemm()) : la ligne r
// de la matrice dans out[r * out_stride], comme qk_dequantize_row() la développe.  Chaque thread
// développe 8 valeurs consécutives d'un bloc de 256 : grille (32 * blocs d'une ligne, lignes).

struct dequant_args
{
    long cols;
    uint out_stride;
};

kernel void dequant_q4_K(device const uchar* W [[buffer(0)]], device float* out [[buffer(1)]],
    constant dequant_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    const long nb = a.cols / 256;
    const long b = g.x / 32;
    if (b >= nb) return;
    const uint v0 = (g.x % 32) * 8;               // la première des 8 valeurs du thread
    device const uchar* blk = W + long(g.y) * nb * 144 + b * 144;
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
    device float* y = out + long(g.y) * a.out_stride + b * 256 + v0;
    for (uint i = 0; i < 8; ++i)
    {
        const uint q = high ? qs[i] >> 4 : qs[i] & 0xF;
        y[i] = d * float(q) - m;
    }
}

kernel void dequant_q6_K(device const uchar* W [[buffer(0)]], device float* out [[buffer(1)]],
    constant dequant_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    const long nb = a.cols / 256;
    const long b = g.x / 32;
    if (b >= nb) return;
    const uint v0 = (g.x % 32) * 8;
    device const uchar* blk = W + long(g.y) * nb * 210 + b * 210;
    const uint n = v0 / 128, w = v0 % 128, quarter = w / 32, l0 = w % 32;
    device const uchar* ql = blk + n * 64 + (quarter & 1) * 32 + l0;
    device const uchar* qh = blk + 128 + n * 32 + l0;
    device const char* sc = (device const char*)(blk + 192) + n * 8;
    const float d = float(*(device const half*)(blk + 208));
    device float* y = out + long(g.y) * a.out_stride + b * 256 + v0;
    for (uint i = 0; i < 8; ++i)
    {
        const uint l = l0 + i;
        const uint low = quarter < 2 ? ql[i] & 0xF : ql[i] >> 4;
        const uint up = (qh[i] >> (2 * quarter)) & 3;
        y[i] = d * float(sc[l / 16 + 2 * quarter]) * float(int(low | (up << 4)) - 32);
    }
}

// ---------------------------------------------------------------------------------------------
// Lignes normalisées par leur moyenne quadratique (cpu_runtime::rms_norm_rows) : un threadgroup par
// ligne ; x et y peuvent être les mêmes lignes.

struct norm_args
{
    uint width;
    uint x_stride;
    uint y_stride;
    float eps;
};

kernel void rms_norm(device const float* x [[buffer(0)]], device const float* w [[buffer(1)]],
    device float* y [[buffer(2)]], constant norm_args& a [[buffer(3)]],
    uint row [[threadgroup_position_in_grid]], uint tid [[thread_index_in_threadgroup]],
    uint threads [[threads_per_threadgroup]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    threadgroup float part[32];
    device const float* xr = x + row * a.x_stride;
    device float* yr = y + row * a.y_stride;
    float s = 0;
    for (uint i = tid; i < a.width; i += threads) s += xr[i] * xr[i];
    s = simd_sum(s);
    if (lane == 0) part[sg] = s;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (sg == 0)
    {
        float v = lane < (threads + 31) / 32 ? part[lane] : 0.0f;
        v = simd_sum(v);
        if (lane == 0) part[0] = v;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const float scale = 1.0f / sqrt(part[0] / float(a.width) + a.eps);
    for (uint i = tid; i < a.width; i += threads) yr[i] = xr[i] * scale * w[i];
}

// Un biais ajouté à des lignes de n valeurs : x[t * stride + i] += b[i].
struct bias_args
{
    uint n;
    uint stride;
};

kernel void add_bias(device float* x [[buffer(0)]], device const float* b [[buffer(1)]],
    constant bias_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    if (g.x < a.n) x[g.y * a.stride + g.x] += b[g.x];
}

// La porte du bloc feed-forward : gate[i] = silu(gate[i]) * up[i] (qk_silu_mul).
kernel void silu_mul(device float* gate [[buffer(0)]], device const float* up [[buffer(1)]],
    constant uint& n [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i >= n) return;
    const float g = gate[i];
    gate[i] = g / (1.0f + exp(-g)) * up[i];
}

// ---------------------------------------------------------------------------------------------
// Les positions rotatives (cpu_runtime::apply_rope) : le token t fait tourner les rope_dims
// premières valeurs de chacune de ses têtes des cosinus et sinus de sa position,
// cs[t * cs_stride + i] et cs[t * cs_stride + half + i], par paires (i, i + half) (neox) ou
// (2i, 2i + 1).

struct rope_args
{
    uint heads;
    uint hd;
    uint half_dims;
    uint neox;
    uint stride;       // les floats d'un token
    uint cs_stride;
};

kernel void rope(device float* x [[buffer(0)]], device const float* cs [[buffer(1)]],
    constant rope_args& a [[buffer(2)]], uint3 g [[thread_position_in_grid]])
{
    const uint i = g.x, h = g.y, t = g.z;
    if (i >= a.half_dims || h >= a.heads) return;
    const float c = cs[t * a.cs_stride + i], s = cs[t * a.cs_stride + a.half_dims + i];
    device float* r = x + t * a.stride + h * a.hd;
    const uint i0 = a.neox ? i : 2 * i, i1 = a.neox ? i + a.half_dims : 2 * i + 1;
    const float x0 = r[i0], x1 = r[i1];
    r[i0] = x0 * c - x1 * s;
    r[i1] = x0 * s + x1 * c;
}

// ---------------------------------------------------------------------------------------------
// La rotation des clés 8 bits et des requêtes qui les lisent (qk_rotate_row) : les signes d'un
// hachage, puis une transformée de Walsh-Hadamard sur la plus grande puissance de deux qui divise
// hd, normalisée.  Un thread par valeur, la ligne en mémoire de threadgroup ; hd au plus 256.

static inline void rotate_row(threadgroup float* v, uint i, uint hd)
{
    if ((i * 2654435761u) >> 31) v[i] = -v[i];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint block = hd & (0u - hd);
    for (uint len = 1; len < block; len <<= 1)
    {
        const uint in_block = i % block;
        float a = 0, c = 0;
        const bool low = (in_block & len) == 0;
        if (low)
        {
            a = v[i];
            c = v[i + len];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (low)
        {
            v[i] = a + c;
            v[i + len] = a - c;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    v[i] *= 1.0f / sqrt(float(block));
    threadgroup_barrier(mem_flags::mem_threadgroup);
}

// La forme 8 bits d'un groupe de 32 valeurs détenu par un simdgroup (qk_float_to_q8_row) : l'échelle
// en demi-précision, montée au demi-flottant suivant quand son arrondi mettrait la plus grande valeur
// au-delà de 127, les valeurs arrondies au plus proche, à égalité vers le pair, dans [-127, 127].
static inline void q8_group(float x, device char* q, device ushort* scale, uint lane)
{
    const float amax = simd_max(fabs(x));
    ushort stored = as_type<ushort>(half(amax / 127.0f));
    if (float(as_type<half>(stored)) * 127.5f < amax) ++stored;
    const float d = float(as_type<half>(stored));
    const float inv = d > 0.0f ? 1.0f / d : 0.0f;
    *q = char(clamp(rint(x * inv), -127.0f, 127.0f));
    if (lane == 0) *scale = stored;
}

// L'endroit où se trouvent les clés et valeurs d'une couche dans la région du pool (kv_cache.h) : le
// bloc b commence à b * block_stride, les clés de la tête h à k_slab + h * head_stride, les valeurs à
// v_slab + h * head_stride, leurs échelles positions * hd octets plus loin (q8).
struct kv_args
{
    uint hd;
    uint q8;
    uint positions;    // positions d'un bloc
    uint kv_stride;    // les floats des clés (ou des valeurs) d'un token
    ulong block_stride;
    ulong k_slab;
    ulong v_slab;
    ulong head_stride;
};

// Les clés et valeurs des tokens d'une passe dans leurs lignes du pool : le token t à la ligne
// rows[t] du bloc blocks[t].  Grille : un threadgroup de hd threads par (tête, token).
kernel void kv_store(device const float* k [[buffer(0)]], device const float* v [[buffer(1)]],
    device uchar* region [[buffer(2)]], device const int* blocks [[buffer(3)]],
    device const int* rows [[buffer(4)]], constant kv_args& a [[buffer(5)]],
    uint2 tg [[threadgroup_position_in_grid]], uint i [[thread_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]], uint sg [[simdgroup_index_in_threadgroup]])
{
    threadgroup float row_k[256];
    const uint h = tg.x, t = tg.y;
    const ulong base = ulong(blocks[t]) * a.block_stride + ulong(h) * a.head_stride;
    const uint pos = uint(rows[t]);
    const float kv_k = k[t * a.kv_stride + h * a.hd + i];
    const float kv_v = v[t * a.kv_stride + h * a.hd + i];
    if (a.q8)
    {
        row_k[i] = kv_k;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        rotate_row(row_k, i, a.hd);
        const ulong scales = ulong(a.positions) * a.hd + ulong(pos) * (a.hd / 32) * 2 + sg * 2;
        device uchar* kb = region + base + a.k_slab;
        device uchar* vb = region + base + a.v_slab;
        q8_group(row_k[i], (device char*)(kb + ulong(pos) * a.hd + i), (device ushort*)(kb + scales), lane);
        q8_group(kv_v, (device char*)(vb + ulong(pos) * a.hd + i), (device ushort*)(vb + scales), lane);
    }
    else
    {
        device half* kb = (device half*)(region + base + a.k_slab);
        device half* vb = (device half*)(region + base + a.v_slab);
        kb[ulong(pos) * a.hd + i] = half(kv_k);
        vb[ulong(pos) * a.hd + i] = half(kv_v);
    }
}

// Les requêtes mises à l'échelle pour l'attention, et tournées comme les clés 8 bits : un
// threadgroup de hd threads par (tête, token).
struct query_args
{
    uint hd;
    uint q8;
    uint q_stride;
    float scale;
};

kernel void prepare_queries(device float* q [[buffer(0)]], constant query_args& a [[buffer(1)]],
    uint2 tg [[threadgroup_position_in_grid]], uint i [[thread_index_in_threadgroup]])
{
    threadgroup float row[256];
    device float* r = q + tg.y * a.q_stride + tg.x * a.hd;
    if (!a.q8)
    {
        r[i] *= a.scale;
        return;
    }
    row[i] = r[i] * a.scale;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    rotate_row(row, i, a.hd);
    r[i] = row[i];
}

// ---------------------------------------------------------------------------------------------
// L'attention des tokens d'une passe sur les positions qu'ils voient, par parties de part_len
// positions (le découpage de FlashDecoding) : la grille contient un threadgroup de ATT_SG
// simdgroups par (partie, tête de clés et valeurs, token) ; chaque simdgroup lit ses clés et
// valeurs une fois pour les rep têtes de requête qui les partagent, avec un softmax tenu en
// ligne, et le threadgroup écrit les sommes de sa partie, non normalisées, avec leurs maximums et
// leurs sommes.  merge_attention() réunit les parties.  Les clés du token t sont celles de sa
// séquence, blocks[t * table_stride + p / positions], dont il voit les lengths[t] premières.

#define ATT_SG 4
#define MAX_REP 8
#define MAX_PER_LANE 8      // hd / 32 au plus : hd jusqu'à 256

struct attention_args
{
    uint hd;
    uint rep;
    uint kv_heads;
    uint q8;
    uint positions;    // positions d'un bloc
    uint part_len;
    uint parts;        // parties de la grille
    uint q_stride;     // les floats des requêtes d'un token
    uint table_stride;
    ulong block_stride;
    ulong k_slab;
    ulong v_slab;
    ulong head_stride;
};

template <uint PER>
static inline void load_row(device const uchar* slab, ulong pos, uint e0, constant attention_args& a,
    thread float (&out)[PER])
{
    if (a.q8)
    {
        device const char* q = (device const char*)(slab + pos * a.hd + e0);
        const float d = float(as_type<half>(*(device const ushort*)(slab + ulong(a.positions) * a.hd
            + pos * (a.hd / 32) * 2 + (e0 / 32) * 2)));
        for (uint e = 0; e < PER; ++e) out[e] = d * float(q[e]);
    }
    else
    {
        device const half* h = (device const half*)(slab) + pos * a.hd + e0;
        for (uint e = 0; e < PER; ++e) out[e] = float(h[e]);
    }
}

template <uint REP, uint PER>
kernel void attention_part(device const float* q [[buffer(0)]], device const uchar* region [[buffer(1)]],
    device const int* blocks [[buffer(2)]], device const int* lengths [[buffer(3)]],
    device float* partial [[buffer(4)]], constant attention_args& a [[buffer(5)]], device float* ctx [[buffer(6)]],
    uint3 tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    threadgroup float tm[ATT_SG][MAX_REP], tl[ATT_SG][MAX_REP];
    threadgroup float to[ATT_SG][REP][16 * PER];
    const uint part = tg.x, kvh = tg.y, t = tg.z;
    const uint per = PER, e0 = lane * PER;
    const long length = lengths[t];
    const long p0 = long(part) * a.part_len, p1 = min(length, p0 + long(a.part_len));
    device float* out = partial + ((ulong(t) * a.kv_heads + kvh) * a.parts + part) * (a.rep * (a.hd + 2));

    float qv[REP][PER];
    for (uint i = 0; i < REP; ++i)
        for (uint e = 0; e < PER; ++e) qv[i][e] = q[t * a.q_stride + (kvh * REP + i) * a.hd + e0 + e];
    float m[REP], l[REP], o[REP][PER];
    for (uint i = 0; i < REP; ++i)
    {
        m[i] = -INFINITY;
        l[i] = 0.0f;
        for (uint e = 0; e < PER; ++e) o[i][e] = 0.0f;
    }
    for (long p = p0 + sg; p < p1; p += ATT_SG)
    {
        const ulong block = ulong(blocks[t * a.table_stride + p / a.positions]);
        const ulong row = ulong(p % a.positions);
        const ulong head = block * a.block_stride + ulong(kvh) * a.head_stride;
        float kf[PER], vf[PER];
        load_row<PER>(region + head + a.k_slab, row, e0, a, kf);
        load_row<PER>(region + head + a.v_slab, row, e0, a, vf);
        for (uint i = 0; i < REP; ++i)
        {
            float s = 0;
            for (uint e = 0; e < PER; ++e) s += qv[i][e] * kf[e];
            s = simd_sum(s);
            const float mx = max(m[i], s);
            const float shrink = exp(m[i] - mx), f = exp(s - mx);
            l[i] = l[i] * shrink + f;
            for (uint e = 0; e < PER; ++e) o[i][e] = o[i][e] * shrink + f * vf[e];
            m[i] = mx;
        }
    }
    // Les simdgroups du threadgroup réunis : chacun à son tour, avec le plus grand maximum.
    float jm[REP], jl[REP];
    for (uint i = 0; i < REP; ++i)
    {
        if (lane == 0)
        {
            tm[sg][i] = m[i];
            tl[sg][i] = l[i];
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = 0; i < REP; ++i)
    {
        float mx = -INFINITY;
        for (uint s = 0; s < ATT_SG; ++s) mx = max(mx, tm[s][i]);
        jm[i] = mx;
        float sum = 0;
        for (uint s = 0; s < ATT_SG; ++s)
            if (tl[s][i] > 0.0f) sum += tl[s][i] * exp(tm[s][i] - mx);
        jl[i] = sum;
    }
    // Les sorties, une moitié de la ligne à la fois pour que la mémoire de threadgroup reste petite.
    const uint half_hd = a.hd / 2;
    for (uint h0 = 0; h0 < a.hd; h0 += half_hd)
    {
        for (uint i = 0; i < REP; ++i)
        {
            const float f = l[i] > 0.0f ? exp(m[i] - jm[i]) : 0.0f;
            for (uint e = 0; e < PER; ++e)
            {
                const uint idx = e0 + e;
                if (idx >= h0 && idx < h0 + half_hd) to[sg][i][idx - h0] = o[i][e] * f;
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        // Une seule partie écrit les sorties, normalisées, merge_attention() n'ayant rien à réunir.
        if (sg == 0)
            for (uint i = 0; i < REP; ++i)
                for (uint idx = lane; idx < half_hd; idx += 32)
                {
                    float sum = 0;
                    for (uint s = 0; s < ATT_SG; ++s) sum += to[s][i][idx];
                    if (a.parts == 1) ctx[t * a.q_stride + (kvh * REP + i) * a.hd + h0 + idx] = sum / jl[i];
                    else out[i * a.hd + h0 + idx] = sum;
                }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Les maximums et les sommes de la partie, pour merge_attention() ; une seule partie n'en a aucun à
    // donner, et partial ne contient alors rien pour elle.
    if (a.parts > 1 && sg == 0 && lane == 0)
        for (uint i = 0; i < REP; ++i)
        {
            out[REP * a.hd + i] = jm[i];
            out[REP * a.hd + REP + i] = jl[i];
        }
}


// Les tailles que prennent les modèles : têtes de requête par tête de clés et valeurs (REP) et valeurs
// d'une tête par voie (PER = hd / 32).
template [[host_name("attention_part_1_2")]] kernel void attention_part<1, 2>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_1_4")]] kernel void attention_part<1, 4>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_1_8")]] kernel void attention_part<1, 8>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_2_2")]] kernel void attention_part<2, 2>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_2_4")]] kernel void attention_part<2, 4>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_2_8")]] kernel void attention_part<2, 8>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_4_2")]] kernel void attention_part<4, 2>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_4_4")]] kernel void attention_part<4, 4>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_4_8")]] kernel void attention_part<4, 8>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_8_2")]] kernel void attention_part<8, 2>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_8_4")]] kernel void attention_part<8, 4>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_8_8")]] kernel void attention_part<8, 8>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);

// Les parties de l'attention réunies : pour le token t et la tête de requête h, les parties qu'atteint
// sa longueur, chacune pondérée par exp(son maximum - le plus grand), divisées par la somme totale,
// dans ctx[t * q_stride + h * hd].  Un threadgroup de hd threads par (tête, token).
kernel void merge_attention(device const float* partial [[buffer(0)]], device const int* lengths [[buffer(1)]],
    device float* ctx [[buffer(2)]], constant attention_args& a [[buffer(3)]],
    uint2 tg [[threadgroup_position_in_grid]], uint e [[thread_index_in_threadgroup]])
{
    const uint h = tg.x, t = tg.y;
    const uint kvh = h / a.rep, i = h % a.rep;
    const uint used = uint((lengths[t] + a.part_len - 1) / a.part_len);
    const uint stride = a.rep * (a.hd + 2);
    device const float* first = partial + (ulong(t) * a.kv_heads + kvh) * a.parts * stride;
    float mx = -INFINITY;
    for (uint p = 0; p < used; ++p)
        if (first[p * stride + a.rep * a.hd + a.rep + i] > 0.0f) mx = max(mx, first[p * stride + a.rep * a.hd + i]);
    float sum = 0, acc = 0;
    for (uint p = 0; p < used; ++p)
    {
        device const float* r = first + p * stride;
        const float s = r[a.rep * a.hd + a.rep + i];
        if (s <= 0.0f) continue;
        const float f = exp(r[a.rep * a.hd + i] - mx);
        sum += s * f;
        acc += f * r[i * a.hd + e];
    }
    ctx[t * a.q_stride + h * a.hd + e] = acc / sum;
}

// ---------------------------------------------------------------------------------------------
// Les couches Gated DeltaNet de qwen35 (fork pour macOS), comme cpu_runtime::linear_attention() les
// calcule.  L'état d'une séquence est dans states, à seq * slot_floats ; celui d'une couche commence à
// layer_offset : la fenêtre de la convolution (K - 1 valeurs par canal), puis, par tête de valeurs, la
// matrice S rangée par clés (S[i][r] à i * S + r).  Les tokens d'une séquence passent dans l'ordre ; une
// séquence est un segment (séquence, premier token, nombre de tokens) du lot.

struct gdn_args
{
    uint C;
    uint K;
    uint Hk;
    uint Hv;
    uint S;
    uint VD;
    uint slot_floats;
    uint layer_offset;
    uint conv_floats;
    uint segments;
    float eps;
    float qscale;
};

// Convolution causale de chaque canal sur la fenêtre de sa séquence, puis SiLU : grille (canaux, segments).
kernel void gdn_conv(device float* mix [[buffer(0)]], device const float* w [[buffer(1)]],
    device float* states [[buffer(2)]], device const int* seg [[buffer(3)]], constant gdn_args& a [[buffer(4)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint c = gid.x, s = gid.y;
    if (c >= a.C || s >= a.segments) return;
    const int sq = seg[3 * s], start = seg[3 * s + 1], count = seg[3 * s + 2];
    const uint n = a.K - 1;
    device float* win = states + ulong(sq) * a.slot_floats + a.layer_offset + ulong(c) * n;
    device const float* wc = w + ulong(c) * a.K;
    for (int j = 0; j < count; ++j)
    {
        device float* xp = mix + ulong(start + j) * a.C + c;
        const float in = *xp;
        float sum = 0.0f;
        for (uint i = 0; i < n; ++i) sum += win[i] * wc[i];
        sum += in * wc[n];
        for (uint i = 0; i + 1 < n; ++i) win[i] = win[i + 1];
        if (n > 0) win[n - 1] = in;
        *xp = sum / (1.0f + exp(-sum));
    }
}

// Normes L2 des têtes de requêtes et de clés de chaque token : grille (2 * Hk, tokens).
kernel void gdn_l2norm(device float* mix [[buffer(0)]], constant gdn_args& a [[buffer(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= 2 * a.Hk) return;
    device float* xp = mix + ulong(gid.y) * a.C + gid.x * a.S;
    float ss = 0.0f;
    for (uint i = 0; i < a.S; ++i) ss += xp[i] * xp[i];
    const float sc = 1.0f / sqrt(ss + a.eps);
    for (uint i = 0; i < a.S; ++i) xp[i] *= sc;
}

// L'oubli g = softplus(alpha + dt_bias) * ssm_a et la force sigmoid(beta) : grille (Hv, tokens).
kernel void gdn_gates(device float* alpha [[buffer(0)]], device float* beta [[buffer(1)]],
    device const float* dt_bias [[buffer(2)]], device const float* ssm_a [[buffer(3)]],
    constant gdn_args& a [[buffer(4)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= a.Hv) return;
    const uint i = gid.y * a.Hv + gid.x;
    const float b = alpha[i] + dt_bias[gid.x];
    alpha[i] = (b > 20.0f ? b : log(1.0f + exp(b))) * ssm_a[gid.x];
    beta[i] = 1.0f / (1.0f + exp(-beta[i]));
}

// La règle delta : un threadgroup par (tête de valeurs, segment), un thread par valeur r de la tête.
kernel void gdn_delta(device const float* mix [[buffer(0)]], device const float* alpha [[buffer(1)]],
    device const float* beta [[buffer(2)]], device float* states [[buffer(3)]], device const int* seg [[buffer(4)]],
    device float* out [[buffer(5)]], constant gdn_args& a [[buffer(6)]],
    uint2 tg [[threadgroup_position_in_grid]], uint r [[thread_index_in_threadgroup]])
{
    threadgroup float kq[512];
    const uint h = tg.x, s = tg.y, S = a.S, kh = h % a.Hk;
    const int sq = seg[3 * s], start = seg[3 * s + 1], count = seg[3 * s + 2];
    device float* st = states + ulong(sq) * a.slot_floats + a.layer_offset + a.conv_floats + ulong(h) * S * S;
    for (int j = 0; j < count; ++j)
    {
        const uint t = uint(start + j);
        device const float* row = mix + ulong(t) * a.C;
        if (r < S)
        {
            kq[r] = row[kh * S + r];
            kq[S + r] = row[(a.Hk + kh) * S + r];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (r < S)
        {
            const float decay = exp(alpha[t * a.Hv + h]), b = beta[t * a.Hv + h];
            float kv = 0.0f;
            for (uint i = 0; i < S; ++i)
            {
                const float v = st[i * S + r] * decay;
                st[i * S + r] = v;
                kv += kq[S + i] * v;
            }
            const float delta = (row[2 * a.Hk * S + h * S + r] - kv) * b;
            float o = 0.0f;
            for (uint i = 0; i < S; ++i)
            {
                const float v = st[i * S + r] + kq[S + i] * delta;
                st[i * S + r] = v;
                o += kq[i] * v;
            }
            out[ulong(t) * a.VD + h * S + r] = o * a.qscale;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}

// La norme RMS de chaque tête, multipliée par SiLU(z) : grille (Hv, tokens).
kernel void gdn_norm(device float* out [[buffer(0)]], device const float* z [[buffer(1)]],
    device const float* w [[buffer(2)]], constant gdn_args& a [[buffer(3)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= a.Hv) return;
    device float* o = out + ulong(gid.y) * a.VD + gid.x * a.S;
    device const float* zz = z + ulong(gid.y) * a.VD + gid.x * a.S;
    float ss = 0.0f;
    for (uint i = 0; i < a.S; ++i) ss += o[i] * o[i];
    const float sc = 1.0f / sqrt(ss / float(a.S) + a.eps);
    for (uint i = 0; i < a.S; ++i) o[i] = o[i] * sc * w[i] * (zz[i] / (1.0f + exp(-zz[i])));
}

// La requête et la porte de l'attention de qwen35, rangées tête par tête dans qg : grille (hd, têtes, tokens).
struct split_args
{
    uint hd;
    uint qd;
};
kernel void split_gate(device const float* qg [[buffer(0)]], device float* q [[buffer(1)]],
    device float* gate [[buffer(2)]], constant split_args& a [[buffer(3)]], uint3 gid [[thread_position_in_grid]])
{
    if (gid.x >= a.hd) return;
    const ulong src = ulong(gid.z) * 2 * a.qd + gid.y * 2 * a.hd + gid.x;
    const ulong dst = ulong(gid.z) * a.qd + gid.y * a.hd + gid.x;
    q[dst] = qg[src];
    gate[dst] = qg[src + a.hd];
}

// x[i] *= sigmoid(gate[i]).
kernel void sigmoid_mul(device float* x [[buffer(0)]], device const float* gate [[buffer(1)]],
    constant uint& n [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i < n) x[i] *= 1.0f / (1.0f + exp(-gate[i]));
}
