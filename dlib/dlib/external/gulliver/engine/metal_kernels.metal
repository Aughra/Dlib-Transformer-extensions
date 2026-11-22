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

// Une ligne par simdgroup pour un seul token, quatre pour plusieurs (chantier Vitesse, 2026-11-21) : mesuré sur le Mac
// mini, un token du 4B relit tous ses poids en 9,72 ms au lieu de 10,70 avec 4 lignes (sonde Metal ; écriture +5 %),
// mais quatre séquences ensemble perdent 6 à 10 % avec une ligne : les activations de plusieurs tokens s'amortissent
// sur plusieurs lignes.  ROWS dépend donc de NT, le nombre de tokens de la variante du noyau.
#define ROWS (NT == 1 ? 1 : 4)
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

// Les produits par expert (fork pour macOS) : le pas d'un expert au suivant dans un tenseur d'experts, et le
// nombre de cases du lot qui lisent la même ligne d'activations (les experts choisis par token).
struct moe_args
{
    ulong expert_bytes;
    uint x_div;
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

template <uint NT>
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
static inline void mv_q4_K_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, uint sg, uint lane)
{
    const mv_target m = mv_select<NT>(W0, y0, W2, y2, a, tg, sg);
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
    if (NT == 1)
    {
        for (long b = lane / 8; b < nb; b += 4)
            for (uint t = 0; t < NT; ++t)
            {
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
    }
    else
    {
        const uint nt = NT > 1 ? min(uint(NT), uint(a.tokens)) : 1u;
        /*
            Pour plusieurs tokens, les poids d'un bloc sont développés une fois puis appliqués à chaque token (chantier
            Vitesse, 2026-11-21) : la boucle des tokens était autour de celle des lignes, si bien que chaque bloc était relu
            et développé une fois par token.  Les additions se font dans le même ordre que pour un seul token : chaque token
            d'un lot reçoit exactement le résultat qu'il aurait seul.  Un seul token garde la boucle d'origine, plus rapide
            pour lui (80 contre 74 tokens/s mesurés en écriture).
        */
        for (long b = lane / 8; b < nb; b += 4)
        {
            float slo[NT], shi[NT];
            for (uint t = 0; t < nt; ++t)
            {
                device const float4* xl = (device const float4*)(x + t * a.x_stride + b * 256 + 64 * j + p);
                device const float4* xh = xl + 8;
                float sl = 0, sh = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const float4 vl = xl[i], vh = xh[i];
                    sl += vl.x + vl.y + vl.z + vl.w;
                    sh += vh.x + vh.y + vh.z + vh.w;
                }
                slo[t] = sl;
                shi[t] = sh;
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
                float4 ql[4], qh[4];
                for (int i = 0; i < 4; ++i)
                {
                    const uchar4 q = qv[i];
                    ql[i] = float4(q & uchar(0xF));
                    qh[i] = float4(q >> uchar(4));
                }
                for (uint t = 0; t < nt; ++t)
                {
                    device const float4* xl = (device const float4*)(x + t * a.x_stride + b * 256 + 64 * j + p);
                    device const float4* xh = xl + 8;
                    float plo = 0, phi = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        plo += dot(ql[i], xl[i]);
                        phi += dot(qh[i], xh[i]);
                    }
                    acc[r][t] += float(dd.x) * (sc1 * plo + sc2 * phi) - float(dd.y) * (m1 * slo[t] + m2 * shi[t]);
                }
            }
        }
    }
    store_rows(m, a, lane, acc);
}

// Q6_K : l'unité u d'une ligne est le bloc u / 4, la moitié n = (u / 2) % 2, les valeurs l0 à l0 + 15
// de chaque quart, l0 = (u % 2) * 16 ; les blocs de 210 octets sont lus octet par octet, leur
// alignement étant de 2.  Les tokens passent à l'extérieur des lignes, comme pour Q4_K.
template <uint NT>
static inline void mv_q6_K_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, uint sg, uint lane)
{
    const mv_target m = mv_select<NT>(W0, y0, W2, y2, a, tg, sg);
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
static inline void mv_q5_K_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, uint sg, uint lane)
{
    const mv_target m = mv_select<NT>(W0, y0, W2, y2, a, tg, sg);
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

// Q5_0 (fork pour macOS, experts de qwen35moe) : blocs de 22 octets, d en demi-flottant, qh (32 bits, le
// cinquième bit de chaque valeur), puis 16 octets de quartets ; la valeur j < 16 prend le quartet bas de qs[j]
// et le bit j de qh, la valeur j + 16 le quartet haut et le bit j + 16 ; valeur - 16, fois d.  Lus octet par
// octet (alignement de 2) ; une voie du simdgroup par bloc.
template <uint NT>
static inline void mv_q5_0_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, uint sg, uint lane)
{
    const mv_target m = mv_select<NT>(W0, y0, W2, y2, a, tg, sg);
    if (m.r0 >= m.rows) return;
    device const uchar* W = m.W;
    const long r0 = m.r0;
    const long nb = a.cols / 32;
    const long row_bytes = nb * 22;
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
                device const uchar* blk = W + row * row_bytes + b * 22;
                const uint qh = uint(blk[2]) | (uint(blk[3]) << 8) | (uint(blk[4]) << 16) | (uint(blk[5]) << 24);
                device const uchar* qs = blk + 6;
                float s = 0;
                for (int j = 0; j < 16; ++j)
                {
                    const int x0 = int((qs[j] & 0x0F) | (((qh >> j) << 4) & 0x10)) - 16;
                    const int x1 = int((qs[j] >> 4) | ((qh >> (j + 12)) & 0x10)) - 16;
                    s += float(x0) * xb[j] + float(x1) * xb[j + 16];
                }
                acc[r][t] += float(*(device const half*)blk) * s;
            }
        }
    store_rows(m, a, lane, acc);
}

template <uint NT>
kernel void mv_q5_0(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    mv_q5_0_body<NT>(W0, x, y0, a, W2, y2, tg, sg, lane);
}

kernel void mv_q5_0_id(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q5_0_body<1>(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

// Q8_0 (fork pour macOS, qwen35) : blocs de 34 octets, d en demi-flottant puis 32 entiers signés, lus octet
// par octet (alignement de 2) ; une voie du simdgroup par bloc.
template <uint NT>
static inline void mv_q8_0_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, uint sg, uint lane)
{
    const mv_target m = mv_select<NT>(W0, y0, W2, y2, a, tg, sg);
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


template <uint NT>
kernel void mv_q4_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    mv_q4_K_body<NT>(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// Le produit Q4_K d'un token repris de llama.cpp (ggml-metal, kernels/mul_mv.metal, kernel_mul_mv_q4_K_f32_impl,
// licence MIT ; chantier Vitesse, 2026-11-22) : les quarts d'octet pris par masques de 16 bits sans décalage, le facteur
// 1/16 ou 1/256 appliqué une fois à la somme ; 32 activations par fil et par bloc, partagées entre les MVL_NR lignes du
// simdgroup.  llama.cpp prend deux lignes et deux simdgroups ; une ligne et quatre simdgroups vont plus vite ici.
// Écriture du 4B du service : 83,0 tokens/s avec mv_q4_K_1, 85,9 avec celui-ci, 88,2 avec aussi mv_q6_K_1l, 89,3 en 1 x 4.
#define MVL_NR 1
#define MVL_NSG 4
static inline void mv_q4_K_1l_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, ushort sg, ushort lane)
{
    constexpr ushort kmask1 = 0x3f3f, kmask2 = 0x0f0f, kmask3 = 0xc0c0;
    device const uchar* W = W0;
    device float* y = y0;
    long rows = a.rows, first_row = (long(tg) * MVL_NSG + sg) * MVL_NR;
    if (first_row >= a.rows)
    {
        W = W2;
        y = y2;
        first_row -= a.rows;
        rows = a.rows2;
    }
    if (first_row >= rows) return;
    const short ix = lane / 8, it = lane % 8, iq = it / 4, ir = it % 4;
    const int nb = int(a.cols / 256);
    const long row_bytes = long(nb) * 144;
    device const uchar* blocks = W + first_row * row_bytes;
    float yl[16], yh[16];
    float sumf[MVL_NR] = { 0.0f };
    device const float* y4 = x + ix * 256 + 64 * iq + 8 * ir;
    ushort sc16[4];
    thread const uchar* sc8 = (thread const uchar*)sc16;
    for (int ib = ix; ib < nb; ib += 4)
    {
        float4 sumy = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (short i = 0; i < 8; ++i)
        {
            yl[i + 0] = y4[i + 0];   sumy[0] += yl[i + 0];
            yl[i + 8] = y4[i + 32];  sumy[1] += yl[i + 8];
            yh[i + 0] = y4[i + 128]; sumy[2] += yh[i + 0];
            yh[i + 8] = y4[i + 160]; sumy[3] += yh[i + 8];
        }
        device const uchar* blk = blocks + long(ib) * 144;
        device const ushort* sc = (device const ushort*)(blk + 4) + iq;
        device const ushort* q1 = (device const ushort*)(blk + 16) + 16 * iq + 4 * ir;
        device const half* dh = (device const half*)blk;
        for (short row = 0; row < MVL_NR; ++row)
        {
            sc16[0] = sc[0] & kmask1;
            sc16[1] = sc[2] & kmask1;
            sc16[2] = ((sc[4] >> 0) & kmask2) | ((sc[0] & kmask3) >> 2);
            sc16[3] = ((sc[4] >> 4) & kmask2) | ((sc[2] & kmask3) >> 2);
            device const ushort* q2 = q1 + 32;
            float4 acc1 = { 0.0f, 0.0f, 0.0f, 0.0f }, acc2 = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (short i = 0; i < 4; ++i)
            {
                acc1[0] += yl[2 * i + 0] * (q1[i] & 0x000F);
                acc1[1] += yl[2 * i + 1] * (q1[i] & 0x0F00);
                acc1[2] += yl[2 * i + 8] * (q1[i] & 0x00F0);
                acc1[3] += yl[2 * i + 9] * (q1[i] & 0xF000);
                acc2[0] += yh[2 * i + 0] * (q2[i] & 0x000F);
                acc2[1] += yh[2 * i + 1] * (q2[i] & 0x0F00);
                acc2[2] += yh[2 * i + 8] * (q2[i] & 0x00F0);
                acc2[3] += yh[2 * i + 9] * (q2[i] & 0xF000);
            }
            sumf[row] += float(dh[0]) * ((acc1[0] + 1.f / 256.f * acc1[1]) * sc8[0]
                + (acc1[2] + 1.f / 256.f * acc1[3]) * sc8[1] * 1.f / 16.f
                + (acc2[0] + 1.f / 256.f * acc2[1]) * sc8[4]
                + (acc2[2] + 1.f / 256.f * acc2[3]) * sc8[5] * 1.f / 16.f)
                - float(dh[1]) * (sumy[0] * sc8[2] + sumy[1] * sc8[3] + sumy[2] * sc8[6] + sumy[3] * sc8[7]);
            q1 += row_bytes / 2;
            sc += row_bytes / 2;
            dh += row_bytes / 2;
        }
        y4 += 4 * 256;
    }
    for (short row = 0; row < MVL_NR; ++row)
    {
        const float v = simd_sum(sumf[row]);
        const long r = first_row + row;
        if (lane == 0 && r < rows)
        {
            device float* out = y + r;
            *out = a.beta != 0.0f ? a.alpha * v + a.beta * *out : a.alpha * v;
        }
    }
}

// Le produit Q6_K d'un token repris de llama.cpp de la même façon (kernel_mul_mv_q6_K_f32_impl, licence MIT) :
// MVL_NR lignes par simdgroup, MVL_NSG simdgroups par threadgroup, seize activations par fil partagées entre les lignes.
static inline void mv_q6_K_1l_body(device const uchar* W0, device const float* x, device float* y0, constant mv_args& a,
    device const uchar* W2, device float* y2, uint tg, ushort sg, ushort lane)
{
    constexpr uchar kmask1 = 0x03, kmask2 = 0x0C, kmask3 = 0x30, kmask4 = 0xC0;
    device const uchar* W = W0;
    device float* y = y0;
    long rows = a.rows, first_row = (long(tg) * MVL_NSG + sg) * MVL_NR;
    if (first_row >= a.rows)
    {
        W = W2;
        y = y2;
        first_row -= a.rows;
        rows = a.rows2;
    }
    if (first_row >= rows) return;
    const int nb = int(a.cols / 256);
    const long row_bytes = long(nb) * 210;
    device const uchar* blocks = W + first_row * row_bytes;
    float sumf[MVL_NR] = { 0.0f };
    float yl[16];
    const short tid = lane / 2, ix = lane % 2, ip = tid / 8, il = tid % 8, l0 = 4 * il, is = 8 * ip + l0 / 16;
    const short y_offset = 128 * ip + l0, q_offset_l = 64 * ip + l0, q_offset_h = 32 * ip + l0;
    for (int i = ix; i < nb; i += 2)
    {
        device const uchar* blk = blocks + long(i) * 210;
        device const uchar* q1 = blk + q_offset_l;
        device const uchar* q2 = q1 + 32;
        device const uchar* qh = blk + 128 + q_offset_h;
        device const char* sc = (device const char*)(blk + 192) + is;
        device const half* dh = (device const half*)(blk + 208);
        device const float* yy = x + long(i) * 256 + y_offset;
        for (short l = 0; l < 4; ++l)
        {
            yl[4 * l + 0] = yy[l + 0];
            yl[4 * l + 1] = yy[l + 32];
            yl[4 * l + 2] = yy[l + 64];
            yl[4 * l + 3] = yy[l + 96];
        }
        for (short row = 0; row < MVL_NR; ++row)
        {
            float4 sums = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (short l = 0; l < 4; ++l)
            {
                sums[0] += yl[4 * l + 0] * float(int(char((q1[l] & 0xF) | ((qh[l] & kmask1) << 4))) - 32);
                sums[1] += yl[4 * l + 1] * float(int(char((q2[l] & 0xF) | ((qh[l] & kmask2) << 2))) - 32);
                sums[2] += yl[4 * l + 2] * float(int(char((q1[l] >> 4) | ((qh[l] & kmask3) << 0))) - 32);
                sums[3] += yl[4 * l + 3] * float(int(char((q2[l] >> 4) | ((qh[l] & kmask4) >> 2))) - 32);
            }
            sumf[row] += float(dh[0]) * (sums[0] * sc[0] + sums[1] * sc[2] + sums[2] * sc[4] + sums[3] * sc[6]);
            q1 += row_bytes;
            q2 += row_bytes;
            qh += row_bytes;
            sc += row_bytes;
            dh += row_bytes / 2;
        }
    }
    for (short row = 0; row < MVL_NR; ++row)
    {
        const float v = simd_sum(sumf[row]);
        const long r = first_row + row;
        if (lane == 0 && r < rows)
        {
            device float* out = y + r;
            *out = a.beta != 0.0f ? a.alpha * v + a.beta * *out : a.alpha * v;
        }
    }
}

kernel void mv_q4_K_1l(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    ushort sg [[simdgroup_index_in_threadgroup]], ushort lane [[thread_index_in_simdgroup]])
{
    mv_q4_K_1l_body(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// Les produits par expert avec le même corps (chantier Vitesse, 2026-11-22), comme mv_q4_K_id.
kernel void mv_q4_K_idl(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    ushort sg [[simdgroup_index_in_threadgroup]], ushort lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q4_K_1l_body(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

kernel void mv_q6_K_1l(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    ushort sg [[simdgroup_index_in_threadgroup]], ushort lane [[thread_index_in_simdgroup]])
{
    mv_q6_K_1l_body(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// Les produits par expert avec le même corps (chantier Vitesse, 2026-11-22), comme mv_q6_K_id.
kernel void mv_q6_K_idl(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    ushort sg [[simdgroup_index_in_threadgroup]], ushort lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q6_K_1l_body(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

// La variante par expert (mélange d'experts) : la case tg.y du lot lit les poids de l'expert ids[tg.y], ses
// activations à la ligne tg.y / x_div de x, et écrit sa ligne de y.
kernel void mv_q4_K_id(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q4_K_body<1>(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

template <uint NT>
kernel void mv_q6_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    mv_q6_K_body<NT>(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// La variante par expert (mélange d'experts) : la case tg.y du lot lit les poids de l'expert ids[tg.y], ses
// activations à la ligne tg.y / x_div de x, et écrit sa ligne de y.
kernel void mv_q6_K_id(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q6_K_body<1>(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

template <uint NT>
kernel void mv_q5_K(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    mv_q5_K_body<NT>(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// La variante par expert (mélange d'experts) : la case tg.y du lot lit les poids de l'expert ids[tg.y], ses
// activations à la ligne tg.y / x_div de x, et écrit sa ligne de y.
kernel void mv_q5_K_id(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q5_K_body<1>(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
}

template <uint NT>
kernel void mv_q8_0(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const uchar* W2 [[buffer(4)]],
    device float* y2 [[buffer(5)]], uint tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    mv_q8_0_body<NT>(W0, x, y0, a, W2, y2, tg, sg, lane);
}

// La variante par expert (mélange d'experts) : la case tg.y du lot lit les poids de l'expert ids[tg.y], ses
// activations à la ligne tg.y / x_div de x, et écrit sa ligne de y.
kernel void mv_q8_0_id(device const uchar* W0 [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y0 [[buffer(2)]], constant mv_args& a [[buffer(3)]], device const int* ids [[buffer(4)]],
    constant moe_args& m [[buffer(5)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint slot = tg.y;
    device const uchar* W = W0 + ulong(ids[slot]) * m.expert_bytes;
    device float* y = y0 + ulong(slot) * a.y_stride;
    mv_q8_0_body<1>(W, x + ulong(slot / m.x_div) * a.x_stride, y, a, W, y, tg.x, sg, lane);
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
template [[host_name("mv_q5_0_1")]] kernel void mv_q5_0<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q5_0_n")]] kernel void mv_q5_0<8>(device const uchar*, device const float*, device float*,
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

// Les fonctions dq8_* et les formats fmt_* (une ligne de 8 valeurs développée) sont dans metal_formats.metal, que
// CMakeLists.txt place en tête de ce source et de celui des accélérateurs (metal_kernels_nax.metal).

// Chaque thread développe 8 valeurs consécutives d'une ligne : grille (colonnes / 8, lignes).
template <typename F>
kernel void dequant(device const uchar* W [[buffer(0)]], device float* out [[buffer(1)]],
    constant dequant_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    const long nb = a.cols / F::values;
    const long b = g.x / (F::values / 8);
    if (b >= nb) return;
    const uint v0 = (g.x % (F::values / 8)) * 8;
    float v[8];
    F::dq8(W + long(g.y) * nb * F::bytes + b * F::bytes, v0, v);
    device float* y = out + long(g.y) * a.out_stride + b * F::values + v0;
    for (uint i = 0; i < 8; ++i) y[i] = v[i];
}

// La même chose en demi-précision, pour les produits des prompts en demi-précision (Gulliver 6.7.0).
template <typename F>
kernel void dequant_h(device const uchar* W [[buffer(0)]], device half* out [[buffer(1)]],
    constant dequant_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    const long nb = a.cols / F::values;
    const long b = g.x / (F::values / 8);
    if (b >= nb) return;
    const uint v0 = (g.x % (F::values / 8)) * 8;
    float v[8];
    F::dq8(W + long(g.y) * nb * F::bytes + b * F::bytes, v0, v);
    device half* y = out + long(g.y) * a.out_stride + b * F::values + v0;
    for (uint i = 0; i < 8; ++i) y[i] = half(v[i]);
}
template [[host_name("dequant_h_q4_K")]] kernel void dequant_h<fmt_q4_K>(device const uchar*, device half*, constant dequant_args&, uint2);
template [[host_name("dequant_h_q6_K")]] kernel void dequant_h<fmt_q6_K>(device const uchar*, device half*, constant dequant_args&, uint2);
template [[host_name("dequant_h_q5_K")]] kernel void dequant_h<fmt_q5_K>(device const uchar*, device half*, constant dequant_args&, uint2);
template [[host_name("dequant_h_q8_0")]] kernel void dequant_h<fmt_q8_0>(device const uchar*, device half*, constant dequant_args&, uint2);
template [[host_name("dequant_h_q5_0")]] kernel void dequant_h<fmt_q5_0>(device const uchar*, device half*, constant dequant_args&, uint2);

// Des lignes de floats copiées en demi-précision : grille (colonnes / 4, lignes).
struct to_half_args
{
    uint cols;
    uint x_stride;
    uint y_stride;
};
kernel void to_half(device const float* x [[buffer(0)]], device half* y [[buffer(1)]],
    constant to_half_args& a [[buffer(2)]], uint2 g [[thread_position_in_grid]])
{
    const uint c = g.x * 4;
    if (c >= a.cols) return;
    const float4 v = *(device const float4*)(x + ulong(g.y) * a.x_stride + c);
    *(device half4*)(y + ulong(g.y) * a.y_stride + c) = half4(v);
}

template [[host_name("dequant_q4_K")]] kernel void dequant<fmt_q4_K>(device const uchar*, device float*,
    constant dequant_args&, uint2);
template [[host_name("dequant_q6_K")]] kernel void dequant<fmt_q6_K>(device const uchar*, device float*,
    constant dequant_args&, uint2);
template [[host_name("dequant_q5_K")]] kernel void dequant<fmt_q5_K>(device const uchar*, device float*,
    constant dequant_args&, uint2);
template [[host_name("dequant_q8_0")]] kernel void dequant<fmt_q8_0>(device const uchar*, device float*,
    constant dequant_args&, uint2);
template [[host_name("dequant_q5_0")]] kernel void dequant<fmt_q5_0>(device const uchar*, device float*,
    constant dequant_args&, uint2);

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

// Gemma 3 (fork pour macOS) : gate[i] = gelu(gate[i]) * up[i], l'approximation tanh (qk_gelu_tanh).
kernel void gelu_mul(device float* gate [[buffer(0)]], device const float* up [[buffer(1)]],
    constant uint& n [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i >= n) return;
    const float g = gate[i];
    gate[i] = 0.5f * g * (1.0f + precise::tanh(0.7978845608028654f * (g + 0.044715f * g * g * g))) * up[i];
}

// Gemma 3 (fork pour macOS) : x[i] += s * y[i], la somme résiduelle d'une sortie normalisée.
struct add_args
{
    uint n;
    float s;
};
kernel void add_rows(device float* x [[buffer(0)]], device const float* y [[buffer(1)]],
    constant add_args& a [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i >= a.n) return;
    x[i] += a.s * y[i];
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

// La norme par tête des requêtes et des clés (q_norm, k_norm), puis leurs positions rotatives, en un seul lancement
// (chantier Vitesse, 2026-11-22) : rms_norm puis rope, deux fois, faisaient quatre lancements par couche, chacun suivi
// d'une barrière.  Un threadgroup par tête et par token : les têtes de requête (q_heads), puis celles des clés
// (k_heads, 0 pour une couche qui lit les clés d'une autre).  Même nombre de fils et même réduction que rms_norm
// (min(256, hd) fils), mêmes formules que rope : les résultats sont ceux des quatre noyaux, au bit près.
struct qk_rope_args
{
    uint q_heads;
    uint k_heads;
    uint hd;
    uint half_dims;
    uint neox;
    uint q_stride;
    uint k_stride;
    uint cs_stride;
    float eps;
};

kernel void qk_norm_rope(device float* q [[buffer(0)]], device float* k [[buffer(1)]],
    device const float* wq [[buffer(2)]], device const float* wk [[buffer(3)]], device const float* cs [[buffer(4)]],
    constant qk_rope_args& a [[buffer(5)]], uint2 g [[threadgroup_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]], uint2 tpg [[threads_per_threadgroup]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    threadgroup float v[512];
    threadgroup float part[32];
    const uint h = g.x, t = g.y, threads = tpg.x;
    const bool is_q = h < a.q_heads;
    device float* r = is_q ? q + t * a.q_stride + h * a.hd : k + t * a.k_stride + (h - a.q_heads) * a.hd;
    device const float* w = is_q ? wq : wk;
    float s = 0;
    for (uint i = tid; i < a.hd; i += threads)
    {
        const float x = r[i];
        v[i] = x;
        s += x * x;
    }
    s = simd_sum(s);
    if (lane == 0) part[sg] = s;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (sg == 0)
    {
        float u = lane < (threads + 31) / 32 ? part[lane] : 0.0f;
        u = simd_sum(u);
        if (lane == 0) part[0] = u;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const float scale = 1.0f / sqrt(part[0] / float(a.hd) + a.eps);
    for (uint i = tid; i < a.hd; i += threads) v[i] = v[i] * scale * w[i];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = tid; i < a.hd; i += threads)
    {
        // La paire de i, et sa place dans la paire.
        uint p, i0, i1;
        bool first;
        if (a.neox)
        {
            first = i < a.half_dims;
            p = first ? i : i - a.half_dims;
            i0 = p;
            i1 = p + a.half_dims;
        }
        else
        {
            first = (i & 1) == 0;
            p = i / 2;
            i0 = 2 * p;
            i1 = 2 * p + 1;
        }
        if (p >= a.half_dims || (a.neox && i >= 2 * a.half_dims))
        {
            r[i] = v[i];
            continue;
        }
        const float c = cs[t * a.cs_stride + p], sn = cs[t * a.cs_stride + a.half_dims + p];
        const float x0 = v[i0], x1 = v[i1];
        r[i] = first ? x0 * c - x1 * sn : x0 * sn + x1 * c;
    }
}

// ---------------------------------------------------------------------------------------------
// La rotation des clés et valeurs 8 bits et des requêtes qui lisent les clés (qk_rotate_row) : les
// signes d'un hachage, puis une transformée de Walsh-Hadamard sur la plus grande puissance de deux
// qui divise hd, normalisée ; unrotate_row la défait (qk_unrotate_row).  Un thread par valeur, la
// ligne en mémoire de threadgroup ; hd au plus 512 (Gemma 4).

static inline void walsh_hadamard_row(threadgroup float* v, uint i, uint hd)
{
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

static inline void rotate_row(threadgroup float* v, uint i, uint hd)
{
    if ((i * 2654435761u) >> 31) v[i] = -v[i];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    walsh_hadamard_row(v, i, hd);
}

static inline void unrotate_row(threadgroup float* v, uint i, uint hd)
{
    walsh_hadamard_row(v, i, hd);
    if ((i * 2654435761u) >> 31) v[i] = -v[i];
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
// v_slab + h * head_stride, leurs échelles scales octets plus loin (q8 ; positions * hd, ou positions * 512 pour
// les couches de 256 valeurs de Gemma 4).
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
    ulong scales;
};

// Les clés et valeurs des tokens d'une passe dans leurs lignes du pool : le token t à la ligne
// rows[t] du bloc blocks[t].  Grille : un threadgroup de hd threads par (tête, token).
kernel void kv_store(device const float* k [[buffer(0)]], device const float* v [[buffer(1)]],
    device uchar* region [[buffer(2)]], device const int* blocks [[buffer(3)]],
    device const int* rows [[buffer(4)]], constant kv_args& a [[buffer(5)]],
    uint2 tg [[threadgroup_position_in_grid]], uint i [[thread_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]], uint sg [[simdgroup_index_in_threadgroup]])
{
    threadgroup float row_k[512];
    threadgroup float row_v[512];
    const uint h = tg.x, t = tg.y;
    const ulong base = ulong(blocks[t]) * a.block_stride + ulong(h) * a.head_stride;
    const uint pos = uint(rows[t]);
    const float kv_k = k[t * a.kv_stride + h * a.hd + i];
    const float kv_v = v[t * a.kv_stride + h * a.hd + i];
    if (a.q8)
    {
        row_k[i] = kv_k;
        row_v[i] = kv_v;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        rotate_row(row_k, i, a.hd);
        rotate_row(row_v, i, a.hd);
        const ulong scales = a.scales + ulong(pos) * (a.hd / 32) * 2 + sg * 2;
        device uchar* kb = region + base + a.k_slab;
        device uchar* vb = region + base + a.v_slab;
        q8_group(row_k[i], (device char*)(kb + ulong(pos) * a.hd + i), (device ushort*)(kb + scales), lane);
        q8_group(row_v[i], (device char*)(vb + ulong(pos) * a.hd + i), (device ushort*)(vb + scales), lane);
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
    threadgroup float row[512];
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

// Les sorties de l'attention sur des valeurs 8 bits, stockées tournées : chaque tête remise en place
// (qk_unrotate_row).  Un threadgroup de hd threads par (tête, token) ; les arguments de prepare_queries.
kernel void restore_outputs(device float* o [[buffer(0)]], constant query_args& a [[buffer(1)]],
    uint2 tg [[threadgroup_position_in_grid]], uint i [[thread_index_in_threadgroup]])
{
    threadgroup float row[512];
    device float* r = o + tg.y * a.q_stride + tg.x * a.hd;
    row[i] = r[i];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    unrotate_row(row, i, a.hd);
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
#define MAX_PER_LANE 16     // hd / 32 au plus : hd jusqu'à 512 (Gemma 4)

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
    uint window;       // fenêtre glissante (Gemma 3) : le token de longueur L voit les positions L - window à L - 1 ; 0 sans
    ulong block_stride;
    ulong k_slab;
    ulong v_slab;
    ulong head_stride;
    ulong scales;      // les octets entre les clés d'une tête et leurs échelles (kv_args)
};

template <uint PER>
static inline void load_row(device const uchar* slab, ulong pos, uint e0, constant attention_args& a,
    thread float (&out)[PER])
{
    if (a.q8)
    {
        device const char* q = (device const char*)(slab + pos * a.hd + e0);
        const float d = float(as_type<half>(*(device const ushort*)(slab + a.scales
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
    // Les sorties par tranches de CH valeurs : la moitié de la tête, 128 au plus (16 Ko pour 8 têtes de requête),
    // pour que la mémoire de threadgroup reste petite avec les têtes de 512 de Gemma 4.
    constexpr uint CH = 16 * PER < 128 ? 16 * PER : 128;
    threadgroup float to[ATT_SG][REP][CH];
    const uint part = tg.x, kvh = tg.y, t = tg.z;
    const uint per = PER, e0 = lane * PER;
    const long length = lengths[t];
    long p0 = long(part) * a.part_len;
    const long p1 = min(length, p0 + long(a.part_len));
    if (a.window > 0) p0 = max(p0, length - long(a.window));
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
    // Les sorties, une tranche de CH valeurs de la ligne à la fois pour que la mémoire de threadgroup reste petite.
    const uint half_hd = CH;
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


// L'attention d'une étape d'écriture, à la manière du « flash-decoding » (chantier Vitesse, 2026-11-21) : mêmes entrées
// et mêmes sorties qu'attention_part, mais chaque voie d'un simdgroup prend une position, 32 positions à la fois, au lieu
// d'une position par simdgroup.  Les scores des REP têtes de la voie se calculent sur toute la tête (requêtes dans la
// mémoire de threadgroup), le softmax en ligne avance par paquets de 32 positions (une réduction par paquet et par tête
// au lieu d'une par position), puis la somme des valeurs se fait ligne par ligne, chaque voie gardant PER valeurs de la
// sortie et recevant de ses voisines le poids de chaque position.
template <uint REP, uint HD>
kernel void attention_dec(device const float* q [[buffer(0)]], device const uchar* region [[buffer(1)]],
    device const int* blocks [[buffer(2)]], device const int* lengths [[buffer(3)]],
    device float* partial [[buffer(4)]], constant attention_args& a [[buffer(5)]], device float* ctx [[buffer(6)]],
    uint3 tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    constexpr uint PER = HD / 32;
    threadgroup float qs[REP * HD];
    threadgroup float tm[ATT_SG][MAX_REP], tl[ATT_SG][MAX_REP];
    threadgroup float to[ATT_SG][REP][HD];
    const uint part = tg.x, kvh = tg.y, t = tg.z;
    const uint e0 = lane * PER;
    const long length = lengths[t];
    long p0 = long(part) * a.part_len;
    const long p1 = min(length, p0 + long(a.part_len));
    if (a.window > 0) p0 = max(p0, length - long(a.window));
    device float* out = partial + ((ulong(t) * a.kv_heads + kvh) * a.parts + part) * (a.rep * (a.hd + 2));
    for (uint idx = sg * 32 + lane; idx < REP * HD; idx += ATT_SG * 32)
        qs[idx] = q[t * a.q_stride + (kvh * REP + idx / HD) * HD + idx % HD];
    threadgroup_barrier(mem_flags::mem_threadgroup);

    float m[REP], l[REP], o[REP][PER];
    for (uint i = 0; i < REP; ++i)
    {
        m[i] = -INFINITY;
        l[i] = 0.0f;
        for (uint e = 0; e < PER; ++e) o[i][e] = 0.0f;
    }
    device const int* table = blocks + ulong(t) * a.table_stride;
    for (long pb = p0 + long(sg) * 32; pb < p1; pb += ATT_SG * 32)
    {
        // Les scores de la position de la voie, pour ses REP têtes.
        const long p = pb + lane;
        const bool ok = p < p1;
        float sc[REP];
        for (uint i = 0; i < REP; ++i) sc[i] = 0.0f;
        if (ok)
        {
            const ulong head = ulong(table[p / a.positions]) * a.block_stride + ulong(kvh) * a.head_stride;
            const ulong row = ulong(p % a.positions);
            device const uchar* ks = region + head + a.k_slab;
            for (uint e = 0; e < HD; e += 4)
            {
                float4 kv;
                if (a.q8)
                {
                    const float d = float(as_type<half>(*(device const ushort*)(ks + a.scales + row * (HD / 32) * 2 + (e / 32) * 2)));
                    kv = d * float4(*(device const char4*)(ks + row * HD + e));
                }
                else
                    kv = float4(*(device const half4*)((device const half*)ks + row * HD + e));
                for (uint i = 0; i < REP; ++i) sc[i] += dot(kv, *(threadgroup const float4*)(qs + i * HD + e));
            }
        }
        // Le softmax en ligne, un paquet de 32 positions à la fois.
        float pk[REP];
        for (uint i = 0; i < REP; ++i)
        {
            const float s = ok ? sc[i] : -INFINITY;
            const float mn = max(m[i], simd_max(s));
            const float shrink = mn == -INFINITY ? 1.0f : exp(m[i] - mn);
            pk[i] = ok ? exp(s - mn) : 0.0f;
            l[i] = l[i] * shrink + simd_sum(pk[i]);
            m[i] = mn;
            for (uint e = 0; e < PER; ++e) o[i][e] *= shrink;
        }
        // La somme des valeurs : position par position, chaque voie ses PER valeurs de la ligne.
        // Quatre positions à la fois, leurs adresses calculées d'avance, pour que leurs lectures partent ensemble ;
        // l'adresse de la ligne de la voie (sa position) sert aux autres voies par simd_shuffle.
        const uint n = uint(min(32L, p1 - pb));
        const int vblk = ok ? table[p / a.positions] : 0;
        const uint vrw = ok ? uint(p % a.positions) : 0u;
        device const uchar* vhead = region + ulong(kvh) * a.head_stride + a.v_slab;
        for (uint j0 = 0; j0 < n; j0 += 4)
        {
            float vf[4][PER];
            for (uint u = 0; u < 4; ++u)
            {
                const ushort j = ushort(min(j0 + u, n - 1));
                const int blk = simd_shuffle(vblk, j);
                const uint row = simd_shuffle(vrw, j);
                device const uchar* base = vhead + ulong(blk) * a.block_stride;
                load_row<PER>(base, row, e0, a, vf[u]);
            }
            for (uint u = 0; u < 4 && j0 + u < n; ++u)
                for (uint i = 0; i < REP; ++i)
                {
                    const float w = simd_shuffle(pk[i], ushort(j0 + u));
                    for (uint e = 0; e < PER; ++e) o[i][e] += w * vf[u][e];
                }
        }
    }
    // Les simdgroups réunis, comme dans attention_part.
    for (uint i = 0; i < REP; ++i)
        if (lane == 0)
        {
            tm[sg][i] = m[i];
            tl[sg][i] = l[i];
        }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    float jm[REP], jl[REP];
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
    for (uint i = 0; i < REP; ++i)
    {
        const float f = l[i] > 0.0f ? exp(m[i] - jm[i]) : 0.0f;
        for (uint e = 0; e < PER; ++e) to[sg][i][e0 + e] = o[i][e] * f;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (sg == 0)
        for (uint i = 0; i < REP; ++i)
            for (uint idx = lane; idx < HD; idx += 32)
            {
                float sum = 0;
                for (uint s = 0; s < ATT_SG; ++s) sum += to[s][i][idx];
                if (a.parts == 1) ctx[t * a.q_stride + (kvh * REP + i) * HD + idx] = jl[i] > 0.0f ? sum / jl[i] : 0.0f;
                else out[i * HD + idx] = sum;
            }
    if (a.parts > 1 && sg == 0 && lane == 0)
        for (uint i = 0; i < REP; ++i)
        {
            out[REP * HD + i] = jm[i];
            out[REP * HD + REP + i] = jl[i];
        }
}

#define ATT_DEC(R, H) template [[host_name("attention_dec_" #R "_" #H)]] kernel void attention_dec<R, H>( \
    device const float*, device const uchar*, device const int*, device const int*, device float*, \
    constant attention_args&, device float*, uint3, uint, uint);
ATT_DEC(1, 64) ATT_DEC(1, 128) ATT_DEC(2, 64) ATT_DEC(2, 128) ATT_DEC(4, 64) ATT_DEC(4, 128) ATT_DEC(8, 64) ATT_DEC(8, 128)
// Têtes de 256 valeurs (Gemma 3) : 1, 2 ou 4 têtes de requête par tête de clés (la sortie du threadgroup, 4 x REP x 256
// floats, tient en 16 Ko).
ATT_DEC(1, 256) ATT_DEC(2, 256) ATT_DEC(4, 256)

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
// Les têtes de 512 valeurs (Gemma 4).
template [[host_name("attention_part_1_16")]] kernel void attention_part<1, 16>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_2_16")]] kernel void attention_part<2, 16>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_4_16")]] kernel void attention_part<4, 16>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);
template [[host_name("attention_part_8_16")]] kernel void attention_part<8, 16>(device const float*, device const uchar*,
    device const int*, device const int*, device float*, constant attention_args&, device float*, uint3, uint, uint);

// ---------------------------------------------------------------------------------------------
// L'attention des prompts en tuiles, par instructions matricielles (simdgroup_matrix, 8 x 8) : un threadgroup de NSG
// simdgroups par (tuile, tête de clés) ; les M = 8 x NSG lignes de requête de la tuile sont ses TOK = M / REP tokens
// et leurs REP têtes (ligne = token x REP + tête), chaque simdgroup en prenant 8.  Par tranche de 32 positions : les
// scores S (8 x 32) = Q K^T, le softmax en ligne (une position par voie), la remise à l'échelle des sorties par une
// matrice diagonale, puis O += P V.  Entrées en demi-précision, sommes en floats.
//
// SPLIT coupe la tête en SPLIT tranches de HC = HD / SPLIT valeurs, pour les têtes de 512 (Gemma 4) : la tuile des
// clés et valeurs ne garde qu'une tranche à la fois (les scores s'additionnent tranche par tranche), et chaque
// threadgroup ne calcule que la tranche tg.z de la sortie.  Les scores sont alors calculés SPLIT fois, mais la mémoire
// de threadgroup et les fragments de sortie restent ceux d'une tête de HC valeurs.
template <uint REP, uint HD, uint NSG, uint SPLIT>
kernel void attention_mtile(device const float* q [[buffer(0)]], device const uchar* region [[buffer(1)]],
    device const int* blocks [[buffer(2)]], device const int* lengths [[buffer(3)]], device const int* tiles [[buffer(4)]],
    constant attention_args& a [[buffer(5)]], device float* ctx [[buffer(6)]],
    uint3 tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]], uint tid [[thread_index_in_threadgroup]])
{
    constexpr uint M = NSG * 8, HC = HD / SPLIT, F = HC / 8;
    const uint e_out = tg.z * HC;
    threadgroup half qs[M * HD];
    threadgroup half kv[32 * HC];
    threadgroup float ss[NSG][8 * 32];
    threadgroup half ps[NSG][8 * 32];
    threadgroup float dd[NSG][64];
    const uint kvh = tg.y;
    const int t0 = tiles[2 * tg.x], count = tiles[2 * tg.x + 1];
    for (uint idx = tid; idx < M * HD; idx += NSG * 32)
    {
        const uint row = idx / HD, e = idx % HD, tt = row / REP, i = row % REP;
        qs[idx] = half(int(tt) < count ? q[ulong(t0 + int(tt)) * a.q_stride + (kvh * REP + i) * HD + e] : 0.0f);
    }
    float m[8], l[8];
    long len[8];
    for (uint r = 0; r < 8; ++r)
    {
        const uint tt = (sg * 8 + r) / REP;
        len[r] = int(tt) < count ? long(lengths[t0 + int(tt)]) : 0;
        m[r] = -INFINITY;
        l[r] = 0.0f;
    }
    simdgroup_float8x8 o[F];
    for (uint j = 0; j < F; ++j) o[j] = make_filled_simdgroup_matrix<float, 8, 8>(0.0f);
    const long last = long(lengths[t0 + count - 1]);
    // La fenêtre glissante : rien avant celle du premier token de la tuile.
    const long first = a.window > 0 ? max(0L, long(lengths[t0]) - long(a.window)) : 0;
    device const int* table = blocks + ulong(t0) * a.table_stride;
    // Huit valeurs consécutives par thread (un groupe de 32 n'a qu'une échelle) : une recherche dans la table des blocs,
    // une échelle et une lecture de 8 octets (ou 16 en demi-précision) par huit valeurs ; la tranche de HC valeurs qui
    // commence à e0.
    auto load = [&](ulong slab, long p0, uint nk, uint e0) {
        for (uint idx = tid * 8; idx < 32 * HC; idx += NSG * 32 * 8)
        {
            const uint jj = idx / HC, e = e0 + idx % HC;
            threadgroup half4* dst = (threadgroup half4*)(kv + idx);
            if (jj >= nk)
            {
                dst[0] = half4(0.0h);
                dst[1] = half4(0.0h);
                continue;
            }
            const long p = p0 + jj;
            const ulong head = ulong(table[p / a.positions]) * a.block_stride + ulong(kvh) * a.head_stride;
            device const uchar* s = region + head + slab;
            const ulong row = ulong(p % a.positions);
            if (a.q8)
            {
                const float d = float(as_type<half>(*(device const ushort*)(s + a.scales
                    + row * (HD / 32) * 2 + (e / 32) * 2)));
                device const char4* qv = (device const char4*)(s + row * HD + e);
                dst[0] = half4(d * float4(qv[0]));
                dst[1] = half4(d * float4(qv[1]));
            }
            else
            {
                device const half4* hv = (device const half4*)((device const half*)s + row * HD + e);
                dst[0] = hv[0];
                dst[1] = hv[1];
            }
        }
    };
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (long p0 = first; p0 < last; p0 += 32)
    {
        const uint nk = uint(min(32L, last - p0));
        simdgroup_float8x8 s[4];
        for (uint c = 0; c < 4; ++c) s[c] = make_filled_simdgroup_matrix<float, 8, 8>(0.0f);
        for (uint part = 0; part < SPLIT; ++part)
        {
            if (part > 0) threadgroup_barrier(mem_flags::mem_threadgroup);
            load(a.k_slab, p0, nk, part * HC);
            threadgroup_barrier(mem_flags::mem_threadgroup);
            for (uint k = 0; k < F; ++k)
            {
                simdgroup_half8x8 qa;
                simdgroup_load(qa, qs + (sg * 8) * HD + part * HC + k * 8, HD);
                for (uint c = 0; c < 4; ++c)
                {
                    simdgroup_half8x8 kb;
                    simdgroup_load(kb, kv + (c * 8) * HC + k * 8, HC, ulong2(0, 0), true);
                    simdgroup_multiply_accumulate(s[c], qa, kb, s[c]);
                }
            }
        }
        for (uint c = 0; c < 4; ++c) simdgroup_store(s[c], ss[sg] + c * 8, 32);
        simdgroup_barrier(mem_flags::mem_threadgroup);
        // Le softmax en ligne : la voie lane prend la position p0 + lane de chaque ligne.
        float shrink[8];
        for (uint r = 0; r < 8; ++r)
        {
            const bool visible = lane < nk && p0 + long(lane) < len[r]
                && (a.window == 0 || p0 + long(lane) >= len[r] - long(a.window));
            const float sv = visible ? ss[sg][r * 32 + lane] : -INFINITY;
            const float mn = max(m[r], simd_max(sv));
            if (mn == -INFINITY)
            {
                shrink[r] = 1.0f;
                ps[sg][r * 32 + lane] = half(0.0f);
                continue;
            }
            shrink[r] = exp(m[r] - mn);
            const float pk = visible ? exp(sv - mn) : 0.0f;
            l[r] = l[r] * shrink[r] + simd_sum(pk);
            m[r] = mn;
            ps[sg][r * 32 + lane] = half(pk);
        }
        for (uint idx = lane; idx < 64; idx += 32) dd[sg][idx] = idx / 8 == idx % 8 ? shrink[idx / 8] : 0.0f;
        simdgroup_barrier(mem_flags::mem_threadgroup);
        simdgroup_float8x8 dm;
        simdgroup_load(dm, dd[sg], 8);
        for (uint j = 0; j < F; ++j) simdgroup_multiply(o[j], dm, o[j]);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        load(a.v_slab, p0, nk, e_out);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        simdgroup_half8x8 pa[4];
        for (uint c = 0; c < 4; ++c) simdgroup_load(pa[c], ps[sg] + c * 8, 32);
        for (uint j = 0; j < F; ++j)
            for (uint c = 0; c < 4; ++c)
            {
                simdgroup_half8x8 vb;
                simdgroup_load(vb, kv + (c * 8) * HC + j * 8, HC);
                simdgroup_multiply_accumulate(o[j], pa[c], vb, o[j]);
            }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Les sorties, normalisées, un fragment de 8 x 8 à la fois.
    for (uint j = 0; j < F; ++j)
    {
        simdgroup_store(o[j], dd[sg], 8);
        simdgroup_barrier(mem_flags::mem_threadgroup);
        for (uint idx = lane; idx < 64; idx += 32)
        {
            const uint r = idx / 8, row = sg * 8 + r, tt = row / REP, i = row % REP;
            if (int(tt) < count)
                ctx[ulong(t0 + int(tt)) * a.q_stride + (kvh * REP + i) * HD + e_out + j * 8 + idx % 8] = dd[sg][idx] / l[r];
        }
        simdgroup_barrier(mem_flags::mem_threadgroup);
    }
}

// NSG : 4 simdgroups (32 lignes de requête) jusqu'à des têtes de 128 valeurs, 2 (16 lignes) pour 256, pour tenir
// dans 32 Ko ; pour 512 (Gemma 4), 1 simdgroup (8 lignes) et deux tranches de 256 (SPLIT) : 8 Ko de requêtes, 16 Ko
// de clés ou de valeurs, moins de 2 Ko pour les scores.  Les tokens d'une tuile : 8 x NSG / REP.
#define ATT_MTILE(R, H, N, S) template [[host_name("attention_mtile_" #R "_" #H)]] kernel void attention_mtile<R, H, N, S>( \
    device const float*, device const uchar*, device const int*, device const int*, device const int*, \
    constant attention_args&, device float*, uint3, uint, uint, uint);
ATT_MTILE(1, 64, 4, 1) ATT_MTILE(1, 128, 4, 1) ATT_MTILE(1, 256, 2, 1)
ATT_MTILE(2, 64, 4, 1) ATT_MTILE(2, 128, 4, 1) ATT_MTILE(2, 256, 2, 1)
ATT_MTILE(4, 64, 4, 1) ATT_MTILE(4, 128, 4, 1) ATT_MTILE(4, 256, 2, 1)
ATT_MTILE(8, 64, 4, 1) ATT_MTILE(8, 128, 4, 1) ATT_MTILE(8, 256, 2, 1)
ATT_MTILE(1, 512, 1, 2) ATT_MTILE(2, 512, 1, 2) ATT_MTILE(4, 512, 1, 2) ATT_MTILE(8, 512, 1, 2)

// Les parties de l'attention réunies : pour le token t et la tête de requête h, les parties qu'atteint
// sa longueur, chacune pondérée par exp(son maximum - le plus grand), divisées par la somme totale,
// dans ctx[t * q_stride + h * hd].  Un threadgroup de hd threads par (tête, token).
//
// Les poids des parties calculés une fois par threadgroup et partagés (chantier Vitesse, 2026-11-22) : chaque fil
// recalculait le maximum et l'exponentielle de toutes les parties (64 à 16 384 positions, parties de 256), et le noyau
// prenait 5,7 % d'une étape de Gemma 3 4B après 16 384 tokens.  Les fils se partagent les parties pour le maximum
// (réduit par simd_max puis en mémoire de threadgroup) et les poids ; la somme pondérée de chaque valeur garde l'ordre
// des parties.  Au-delà de MERGE_PARTS parties, l'ancien calcul.
#define MERGE_PARTS 2048
kernel void merge_attention(device const float* partial [[buffer(0)]], device const int* lengths [[buffer(1)]],
    device float* ctx [[buffer(2)]], constant attention_args& a [[buffer(3)]],
    uint2 tg [[threadgroup_position_in_grid]], uint e [[thread_index_in_threadgroup]],
    uint2 tpg [[threads_per_threadgroup]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    threadgroup float wt[MERGE_PARTS];
    threadgroup float red[32];
    const uint h = tg.x, t = tg.y, threads = tpg.x, nsg = (threads + 31) / 32;
    const uint kvh = h / a.rep, i = h % a.rep;
    const uint used = uint((lengths[t] + a.part_len - 1) / a.part_len);
    const uint stride = a.rep * (a.hd + 2);
    device const float* first = partial + (ulong(t) * a.kv_heads + kvh) * a.parts * stride;
    if (used > MERGE_PARTS)
    {
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
        return;
    }
    // Le maximum des parties qui comptent.
    float mx = -INFINITY;
    for (uint p = e; p < used; p += threads)
        if (first[p * stride + a.rep * a.hd + a.rep + i] > 0.0f) mx = max(mx, first[p * stride + a.rep * a.hd + i]);
    mx = simd_max(mx);
    if (lane == 0) red[sg] = mx;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (sg == 0)
    {
        float v = lane < nsg ? red[lane] : -INFINITY;
        v = simd_max(v);
        if (lane == 0) red[0] = v;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    mx = red[0];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    // Le poids de chaque partie, et sa contribution au dénominateur.
    float sum = 0;
    for (uint p = e; p < used; p += threads)
    {
        device const float* r = first + p * stride;
        const float s = r[a.rep * a.hd + a.rep + i];
        const float f = s > 0.0f ? exp(r[a.rep * a.hd + i] - mx) : 0.0f;
        wt[p] = f;
        sum += s * f;
    }
    sum = simd_sum(sum);
    if (lane == 0) red[sg] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (sg == 0)
    {
        float v = lane < nsg ? red[lane] : 0.0f;
        v = simd_sum(v);
        if (lane == 0) red[0] = v;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    float acc = 0;
    for (uint p = 0; p < used; ++p)
    {
        const float f = wt[p];
        if (f != 0.0f) acc += f * first[p * stride + i * a.hd + e];
    }
    ctx[t * a.q_stride + h * a.hd + e] = acc / red[0];
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

// La convolution courte de LFM2 (fork pour macOS), comme cpu_runtime::short_convolution() : mix contient, par token,
// b, c et x (C valeurs chacun) ; chaque canal de b * x passe dans la convolution causale de ses K poids, sur la
// fenêtre de sa séquence (K - 1 valeurs à layer_offset), et out reçoit c fois le résultat.  Grille (canaux, segments).
kernel void short_conv(device const float* mix [[buffer(0)]], device const float* w [[buffer(1)]],
    device float* states [[buffer(2)]], device const int* seg [[buffer(3)]], device float* out [[buffer(4)]],
    constant gdn_args& a [[buffer(5)]], uint2 gid [[thread_position_in_grid]])
{
    const uint c = gid.x, s = gid.y;
    if (c >= a.C || s >= a.segments) return;
    const int sq = seg[3 * s], start = seg[3 * s + 1], count = seg[3 * s + 2];
    const uint n = a.K - 1;
    device float* win = states + ulong(sq) * a.slot_floats + a.layer_offset + ulong(c) * n;
    device const float* wc = w + ulong(c) * a.K;
    for (int j = 0; j < count; ++j)
    {
        device const float* row = mix + ulong(start + j) * 3 * a.C;
        const float in = row[c] * row[2 * a.C + c];
        float sum = 0.0f;
        for (uint i = 0; i < n; ++i) sum += win[i] * wc[i];
        sum += in * wc[n];
        for (uint i = 0; i + 1 < n; ++i) win[i] = win[i + 1];
        if (n > 0) win[n - 1] = in;
        out[ulong(start + j) * a.C + c] = row[a.C + c] * sum;
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

// ---------------------------------------------------------------------------------------------
// Le mélange d'experts sur le GPU (fork pour macOS), comme cpu_runtime::moe_ffn() le calcule.

// y[t][r] = sum_i W[r][i] x[t][i], W en floats (le routeur) : grille (lignes, tokens).
struct f32_args
{
    uint rows;
    uint cols;
    uint x_stride;
    uint y_stride;
};
// Un simdgroup par ligne, ses 32 voies lisant des floats voisins : threadgroups de 4 simdgroups, grille
// (lignes / 4, tokens).
kernel void mv_f32(device const float* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant f32_args& a [[buffer(3)]], uint2 tg [[threadgroup_position_in_grid]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    const uint row = tg.x * 4 + sg;
    if (row >= a.rows) return;
    device const float* w = W + ulong(row) * a.cols;
    device const float* xt = x + ulong(tg.y) * a.x_stride;
    float s = 0.0f;
    for (uint i = lane; i < a.cols; i += 32) s += w[i] * xt[i];
    s = simd_sum(s);
    if (lane == 0) y[ulong(tg.y) * a.y_stride + row] = s;
}

// Le routage d'un token : softmax des logits des experts, les top_k plus probables, leurs poids renormalisés
// quand norm ; un thread par token.  Avec sigmoid (LFM2-MoE, fork pour macOS) : le choix sur sigmoid(logit) + bias,
// le poids sigmoid(logit), renormalisé, la somme bornée au plus petit demi-flottant normal, comme cpu_runtime::moe_ffn().
struct route_args
{
    uint experts;
    uint top_k;
    uint norm;
    uint sigmoid;
};
kernel void moe_route(device const float* logits [[buffer(0)]], device int* ids [[buffer(1)]],
    device float* weights [[buffer(2)]], constant route_args& a [[buffer(3)]], device const float* bias [[buffer(4)]],
    uint t [[threadgroup_position_in_grid]], uint lane [[thread_index_in_simdgroup]])
{
    // Un simdgroup par token : la voie lane garde les experts lane, lane + 32, ...  Le plus grand logit pas encore
    // choisi l'emporte, le plus petit numéro à égalité, comme std::partial_sort le garde.
    device const float* l = logits + ulong(t) * a.experts;
    float mx = -INFINITY;
    for (uint e = lane; e < a.experts; e += 32) mx = max(mx, l[e]);
    mx = simd_max(mx);
    float sum = 0.0f;
    for (uint e = lane; e < a.experts; e += 32) sum += exp(l[e] - mx);
    sum = simd_sum(sum);
    uint taken = 0;   // les experts de la voie déjà choisis, un bit par rang (32 au plus : 1 024 experts)
    float selected = 0.0f;
    for (uint k = 0; k < a.top_k; ++k)
    {
        float best = -INFINITY;
        uint best_e = 0xFFFFFFFFu;
        for (uint e = lane, j = 0; e < a.experts; e += 32, ++j)
        {
            const float score = a.sigmoid != 0 ? 1.0f / (1.0f + exp(-l[e])) + bias[e] : l[e];
            if (!(taken & (1u << j)) && (score > best || (score == best && e < best_e)))
            {
                best = score;
                best_e = e;
            }
        }
        const float top = simd_max(best);
        const uint e = simd_min(best == top ? best_e : 0xFFFFFFFFu);
        if (e % 32 == lane) taken |= 1u << (e / 32);
        const float p = a.sigmoid != 0 ? 1.0f / (1.0f + exp(-l[e])) : exp(top - mx) / sum;
        if (lane == 0)
        {
            ids[t * a.top_k + k] = int(e);
            weights[t * a.top_k + k] = p;
        }
        selected += p;
    }
    if (a.sigmoid != 0) selected = max(selected, 6.103515625e-5f);
    if (lane == 0 && a.norm != 0 && selected > 0.0f)
        for (uint k = 0; k < a.top_k; ++k) weights[t * a.top_k + k] /= selected;
}

// La porte de l'expert partagé de qwen35moe : g[t] = sigmoid(x[t] . w) ; un thread par token.
// Un simdgroup par token.
kernel void shared_gate(device const float* x [[buffer(0)]], device const float* w [[buffer(1)]],
    device float* g [[buffer(2)]], constant uint& d [[buffer(3)]], uint t [[threadgroup_position_in_grid]],
    uint lane [[thread_index_in_simdgroup]])
{
    device const float* xt = x + ulong(t) * d;
    float s = 0.0f;
    for (uint i = lane; i < d; i += 32) s += xt[i] * w[i];
    s = simd_sum(s);
    if (lane == 0) g[t] = 1.0f / (1.0f + exp(-s));
}

// x[t][j] += scale * (sum_k weights[t][k] * y[t * top_k + k][j] + g[t] * shared[t][j]) : grille (d, tokens) ;
// sans expert partagé, has_shared vaut 0.
struct combine_args
{
    uint d;
    uint top_k;
    uint has_shared;
    float scale;
};
kernel void moe_combine(device float* x [[buffer(0)]], device const float* y [[buffer(1)]],
    device const float* weights [[buffer(2)]], device const float* shared [[buffer(3)]],
    device const float* g [[buffer(4)]], constant combine_args& a [[buffer(5)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= a.d) return;
    const uint t = gid.y;
    float s = 0.0f;
    for (uint k = 0; k < a.top_k; ++k) s += weights[t * a.top_k + k] * y[ulong(t * a.top_k + k) * a.d + gid.x];
    if (a.has_shared != 0) s += g[t] * shared[ulong(t) * a.d + gid.x];
    x[ulong(t) * a.d + gid.x] += a.scale * s;
}

// ---------------------------------------------------------------------------------------------
// Les experts des prompts (fork pour macOS) : au lieu d'un produit par case (matvec_id), chaque expert
// multiplie d'un coup les cases (token * top_k + rang) qui l'ont choisi.  moe_group range ces cases par
// expert, moe_tiles en fait des tuiles de MM_TT cases et prépare deux lancements indirects, mm_id calcule
// une tuile de MM_TT cases sur MM_NR lignes de poids, en produits de matrices 8 x 8.

#define MM_TT 16    // les cases d'une tuile
#define MM_NR 32    // les lignes de poids d'une tuile
#define MM_KC 64    // les colonnes lues à la fois

// Les cases de l'expert e, dans l'ordre, dans list[e * capacity...], leur nombre dans counts[e] : un
// threadgroup de 256 threads par expert, chacun parcourant une part contiguë des cases.
struct group_args
{
    uint slots;
    uint capacity;
};
kernel void moe_group(device const int* ids [[buffer(0)]], device int* list [[buffer(1)]],
    device int* counts [[buffer(2)]], constant group_args& a [[buffer(3)]],
    uint e [[threadgroup_position_in_grid]], uint tid [[thread_index_in_threadgroup]],
    uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]])
{
    threadgroup uint sums[8];
    const uint per = (a.slots + 255) / 256, s0 = min(a.slots, tid * per), s1 = min(a.slots, s0 + per);
    uint c = 0;
    for (uint s = s0; s < s1; ++s) c += ids[s] == int(e) ? 1 : 0;
    const uint inclusive = simd_prefix_inclusive_sum(c);
    if (lane == 31) sums[sg] = inclusive;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint w = inclusive - c;
    for (uint j = 0; j < sg; ++j) w += sums[j];
    device int* out = list + ulong(e) * a.capacity;
    for (uint s = s0; s < s1; ++s)
        if (ids[s] == int(e)) out[w++] = int(s);
    if (tid == 255) counts[e] = int(w);
}

// Les tuiles de travail (expert, premier rang dans sa liste), dans l'ordre des experts, et deux jeux
// d'arguments de lancement indirect (MTLDispatchThreadgroupsIndirectArguments) : (tiles_a, tuiles, 1)
// puis (tiles_b, tuiles, 1).  Un seul threadgroup de 256 threads.
struct tiles_args
{
    uint experts;
    uint tiles_a;
    uint tiles_b;
    uint per_tile;     // les cases d'une tuile : MM_TT pour mm_id, 64 pour mm_id_nax (accélérateurs du M5)
};
kernel void moe_tiles(device const int* counts [[buffer(0)]], device int2* work [[buffer(1)]],
    device uint* dispatch [[buffer(2)]], constant tiles_args& a [[buffer(3)]],
    uint tid [[thread_index_in_threadgroup]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    threadgroup uint sums[8];
    const uint per = (a.experts + 255) / 256, e0 = min(a.experts, tid * per), e1 = min(a.experts, e0 + per);
    uint c = 0;
    for (uint e = e0; e < e1; ++e) c += (uint(counts[e]) + a.per_tile - 1) / a.per_tile;
    const uint inclusive = simd_prefix_inclusive_sum(c);
    if (lane == 31) sums[sg] = inclusive;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint w = inclusive - c;
    for (uint j = 0; j < sg; ++j) w += sums[j];
    for (uint e = e0; e < e1; ++e)
        for (uint first = 0; first < uint(counts[e]); first += a.per_tile) work[w++] = int2(int(e), int(first));
    if (tid == 255)
    {
        dispatch[0] = a.tiles_a;
        dispatch[1] = w;
        dispatch[2] = 1;
        dispatch[3] = a.tiles_b;
        dispatch[4] = w;
        dispatch[5] = 1;
    }
}

// y[s][r] = sum_i W_e[r][i] x[s / x_div][i] pour les cases s de la tuile, e leur expert : grille
// (lignes / MM_NR, tuiles), 4 simdgroups de 8 lignes chacun.  Les poids sont développés en demi-précision dans
// la mémoire du threadgroup, MM_KC colonnes à la fois, les activations et les sommes restant en floats (Gulliver
// 6.8.0 : Qwen3-30B-A3B lit ses prompts 3 à 49 % plus vite qu'avec des poids en floats).
struct mm_args
{
    ulong expert_bytes;
    uint row_bytes;
    uint cols;
    uint x_stride;
    uint x_div;
    uint y_stride;
    uint capacity;
};
template <typename F>
kernel void mm_id(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], device const int* list [[buffer(3)]], device const int* counts [[buffer(4)]],
    device const int2* work [[buffer(5)]], constant mm_args& a [[buffer(6)]],
    uint2 tg [[threadgroup_position_in_grid]], uint tid [[thread_index_in_threadgroup]],
    uint sg [[simdgroup_index_in_threadgroup]])
{
    threadgroup float xs[MM_TT * MM_KC];
    threadgroup half ws[MM_NR * MM_KC];
    threadgroup float out[MM_TT * MM_NR];
    threadgroup int src[MM_TT];
    const int2 job = work[tg.y];
    const uint e = uint(job.x), first = uint(job.y);
    const uint n = min(uint(MM_TT), uint(counts[e]) - first);
    if (tid < MM_TT) src[tid] = tid < n ? list[ulong(e) * a.capacity + first + tid] : -1;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint row0 = tg.x * MM_NR;
    device const uchar* We = W + ulong(e) * a.expert_bytes;
    simdgroup_float8x8 acc0 = make_filled_simdgroup_matrix<float, 8, 8>(0.0f);
    simdgroup_float8x8 acc1 = make_filled_simdgroup_matrix<float, 8, 8>(0.0f);
    // Les 16 cases sur 64 colonnes : 8 valeurs par thread ; les 32 lignes de poids : deux fois 8 valeurs.
    const uint xt = tid / 8, xc = (tid % 8) * 8;
    const int xs_row = src[xt];
    device const float* xrow = xs_row >= 0 ? x + ulong(uint(xs_row) / a.x_div) * a.x_stride : x;
    for (uint k0 = 0; k0 < a.cols; k0 += MM_KC)
    {
        threadgroup float* dx = xs + xt * MM_KC + xc;
        if (xs_row >= 0)
        {
            const float4 u = *(device const float4*)(xrow + k0 + xc), v = *(device const float4*)(xrow + k0 + xc + 4);
            dx[0] = u.x; dx[1] = u.y; dx[2] = u.z; dx[3] = u.w;
            dx[4] = v.x; dx[5] = v.y; dx[6] = v.z; dx[7] = v.w;
        }
        else
            for (uint i = 0; i < 8; ++i) dx[i] = 0.0f;
        for (uint h = 0; h < 2; ++h)
        {
            const uint g = tid + 128 * h, r = g / 8, c = k0 + (g % 8) * 8;
            float v[8];
            F::dq8(We + ulong(row0 + r) * a.row_bytes + (c / F::values) * F::bytes, c % F::values, v);
            threadgroup half* dw = ws + r * MM_KC + (g % 8) * 8;
            for (uint i = 0; i < 8; ++i) dw[i] = half(v[i]);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint kk = 0; kk < MM_KC; kk += 8)
        {
            simdgroup_float8x8 a0, a1;
            simdgroup_half8x8 b;
            simdgroup_load(a0, xs + kk, MM_KC);
            simdgroup_load(a1, xs + 8 * MM_KC + kk, MM_KC);
            simdgroup_load(b, ws + (sg * 8) * MM_KC + kk, MM_KC, ulong2(0, 0), true);
            simdgroup_multiply_accumulate(acc0, a0, b, acc0);
            simdgroup_multiply_accumulate(acc1, a1, b, acc1);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    simdgroup_store(acc0, out + sg * 8, MM_NR);
    simdgroup_store(acc1, out + 8 * MM_NR + sg * 8, MM_NR);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint ot = tid / 8, orow = (tid % 8) * 4;
    const int s = src[ot];
    if (s >= 0)
    {
        device float* yo = y + ulong(uint(s)) * a.y_stride + row0 + orow;
        for (uint i = 0; i < 4; ++i) yo[i] = out[ot * MM_NR + orow + i];
    }
}

#define MM_ID(N, F) template [[host_name("mm_id_" #N)]] kernel void mm_id<F>(device const uchar*, \
    device const float*, device float*, device const int*, device const int*, device const int2*, constant mm_args&, \
    uint2, uint, uint)
MM_ID(q4_K, fmt_q4_K);
MM_ID(q5_K, fmt_q5_K);
MM_ID(q6_K, fmt_q6_K);
MM_ID(q8_0, fmt_q8_0);
MM_ID(q5_0, fmt_q5_0);
