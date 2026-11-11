#include <metal_stdlib>
using namespace metal;

// Produits matrice-vecteur des poids GGUF Q4_K et Q6_K lus en place, activations en floats.
// Un groupe de 32 threads (simdgroup) calcule ROWS lignes ; chaque thread prend des
// morceaux de 32 (Q4_K) ou 4 x 16 (Q6_K) valeurs, lit leurs activations une fois pour les ROWS
// lignes, puis simd_sum réduit.




struct block_q4_K { half d; half dmin; uchar scales[12]; uchar qs[128]; };
struct block_q6_K { uchar ql[128]; uchar qh[64]; char scales[16]; half d; };

static inline void scale_min_k4(int j, device const uchar* q, thread float& d, thread float& m)
{
    if (j < 4) { d = q[j] & 63; m = q[j + 4] & 63; }
    else { d = (q[j + 4] & 0xF) | ((q[j - 4] >> 6) << 4); m = (q[j + 4] >> 4) | ((q[j] >> 6) << 4); }
}

kernel void mv_q4_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant long& rows [[buffer(3)]], constant long& cols [[buffer(4)]],
    uint tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    const long r0 = (long(tg) * SG_PER_TG + sg) * ROWS;
    if (r0 >= rows) return;
    const long nb = cols / 256;
    const long row_bytes = nb * 144;
    float acc[ROWS] = { 0 };
    for (long u = lane; u < nb * 8; u += 32)
    {
        const long b = u / 8;
        const int s = int(u % 8);
        device const float4* xv = (device const float4*)(x + b * 256 + s * 32);
        float4 xs[8];
        float sumx = 0;
        for (int k = 0; k < 8; ++k) { xs[k] = xv[k]; sumx += xs[k].x + xs[k].y + xs[k].z + xs[k].w; }
        const int shift = (s & 1) * 4;
        for (int r = 0; r < ROWS; ++r)
        {
            const long row = r0 + r;
            if (row >= rows) break;
            device const block_q4_K* blk = (device const block_q4_K*)(W + row * row_bytes) + b;
            float sc, m;
            scale_min_k4(s, blk->scales, sc, m);
            device const uchar4* q = (device const uchar4*)(blk->qs + (s / 2) * 32);
            float p = 0;
            for (int k = 0; k < 8; ++k) p += dot(float4((q[k] >> uchar(shift)) & uchar(0xF)), xs[k]);
            acc[r] += float(blk->d) * sc * p - float(blk->dmin) * m * sumx;
        }
    }
    for (int r = 0; r < ROWS; ++r)
    {
        const float v = simd_sum(acc[r]);
        if (lane == 0 && r0 + r < rows) y[r0 + r] = v;
    }
}

kernel void mv_q6_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant long& rows [[buffer(3)]], constant long& cols [[buffer(4)]],
    uint tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    const long r0 = (long(tg) * SG_PER_TG + sg) * ROWS;
    if (r0 >= rows) return;
    const long nb = cols / 256;
    const long row_bytes = nb * 210;
    float acc[ROWS] = { 0 };
    // Unité u : bloc u / 4, moitié n = (u / 2) % 2, valeurs l0 .. l0 + 15 de chaque quart, l0 = (u % 2) * 16.
    for (long u = lane; u < nb * 4; u += 32)
    {
        const long b = u / 4;
        const int n = int((u / 2) % 2);
        const int l0 = int(u % 2) * 16;
        device const float* xb = x + b * 256 + n * 128 + l0;
        float x1[16], x2[16], x3[16], x4[16];
        for (int l = 0; l < 16; ++l) { x1[l] = xb[l]; x2[l] = xb[l + 32]; x3[l] = xb[l + 64]; x4[l] = xb[l + 96]; }
        const int is = l0 / 16;
        for (int r = 0; r < ROWS; ++r)
        {
            const long row = r0 + r;
            if (row >= rows) break;
            device const uchar* blk = W + row * row_bytes + b * 210;
            device const uchar* ql = blk + n * 64 + l0;
            device const uchar* qh = blk + 128 + n * 32 + l0;
            device const char* sc = (device const char*)(blk + 192) + n * 8;
            const float d = float(*(device const half*)(blk + 208));
            float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
            for (int l = 0; l < 16; ++l)
            {
                const int a = ql[l], c = ql[l + 32], h = qh[l];
                s1 += float(((a & 0xF) | ((h & 3) << 4)) - 32) * x1[l];
                s2 += float(((c & 0xF) | (((h >> 2) & 3) << 4)) - 32) * x2[l];
                s3 += float(((a >> 4) | (((h >> 4) & 3) << 4)) - 32) * x3[l];
                s4 += float(((c >> 4) | (((h >> 6) & 3) << 4)) - 32) * x4[l];
            }
            acc[r] += d * (float(sc[is]) * s1 + float(sc[is + 2]) * s2 + float(sc[is + 4]) * s3 + float(sc[is + 6]) * s4);
        }
    }
    for (int r = 0; r < ROWS; ++r)
    {
        const float v = simd_sum(acc[r]);
        if (lane == 0 && r0 + r < rows) y[r0 + r] = v;
    }
}

kernel void tiny(device float* y [[buffer(0)]], uint i [[thread_position_in_grid]]) { y[i] += 1.0f; }

// Version 2 : lectures contiguës. Un simdgroup traite un bloc de 256 valeurs par itération ;
// le thread l lit le mot l des quants du bloc (Q4_K : 4 octets ; Q6_K : 2 octets x 3).

kernel void mv2_q4_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant long& rows [[buffer(3)]], constant long& cols [[buffer(4)]],
    uint tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    const long r0 = (long(tg) * SG_PER_TG + sg) * ROWS;
    if (r0 >= rows) return;
    const long nb = cols / 256;
    const long row_bytes = nb * 144;
    const int j = lane / 8;              // morceau de 32 octets : sous-blocs 2j (bas) et 2j + 1 (haut)
    const int p = (lane % 8) * 4;        // position dans le sous-bloc
    float acc[ROWS] = { 0 };
    for (long b = 0; b < nb; ++b)
    {
        device const float* xb = x + b * 256 + 64 * j + p;
        const float4 xlo = *(device const float4*)xb;
        const float4 xhi = *(device const float4*)(xb + 32);
        const float slo = xlo.x + xlo.y + xlo.z + xlo.w, shi = xhi.x + xhi.y + xhi.z + xhi.w;
        for (int r = 0; r < ROWS; ++r)
        {
            const long row = r0 + r;
            if (row >= rows) break;
            device const block_q4_K* blk = (device const block_q4_K*)(W + row * row_bytes) + b;
            float sc1, m1, sc2, m2;
            scale_min_k4(2 * j, blk->scales, sc1, m1);
            scale_min_k4(2 * j + 1, blk->scales, sc2, m2);
            const uchar4 q = ((device const uchar4*)blk->qs)[lane];
            const float plo = dot(float4(q & uchar(0xF)), xlo);
            const float phi = dot(float4(q >> uchar(4)), xhi);
            acc[r] += float(blk->d) * (sc1 * plo + sc2 * phi) - float(blk->dmin) * (m1 * slo + m2 * shi);
        }
    }
    for (int r = 0; r < ROWS; ++r)
    {
        const float v = simd_sum(acc[r]);
        if (lane == 0 && r0 + r < rows) y[r0 + r] = v;
    }
}

kernel void mv2_q6_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant long& rows [[buffer(3)]], constant long& cols [[buffer(4)]],
    uint tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    const long r0 = (long(tg) * SG_PER_TG + sg) * ROWS;
    if (r0 >= rows) return;
    const long nb = cols / 256;
    const long row_bytes = nb * 210;
    const int n = lane / 16;             // moitié de 128 valeurs
    const int l = (lane % 16) * 2;       // valeurs l et l + 1 de chaque quart
    const int is = l / 16;
    float acc[ROWS] = { 0 };
    for (long b = 0; b < nb; ++b)
    {
        device const float* xb = x + b * 256 + n * 128 + l;
        const float2 x1 = *(device const float2*)xb, x2 = *(device const float2*)(xb + 32);
        const float2 x3 = *(device const float2*)(xb + 64), x4 = *(device const float2*)(xb + 96);
        for (int r = 0; r < ROWS; ++r)
        {
            const long row = r0 + r;
            if (row >= rows) break;
            device const uchar* blk = W + row * row_bytes + b * 210;
            const ushort a = *(device const ushort*)(blk + n * 64 + l);
            const ushort c = *(device const ushort*)(blk + n * 64 + 32 + l);
            const ushort h = *(device const ushort*)(blk + 128 + n * 32 + l);
            device const char* sc = (device const char*)(blk + 192) + n * 8 + is;
            const float d = float(*(device const half*)(blk + 208));
            float s1 = 0, s2 = 0, s3 = 0, s4 = 0;
            for (int k = 0; k < 2; ++k)
            {
                const int ak = (a >> (8 * k)) & 0xFF, ck = (c >> (8 * k)) & 0xFF, hk = (h >> (8 * k)) & 0xFF;
                s1 += float(((ak & 0xF) | ((hk & 3) << 4)) - 32) * x1[k];
                s2 += float(((ck & 0xF) | (((hk >> 2) & 3) << 4)) - 32) * x2[k];
                s3 += float(((ak >> 4) | (((hk >> 4) & 3) << 4)) - 32) * x3[k];
                s4 += float(((ck >> 4) | (((hk >> 6) & 3) << 4)) - 32) * x4[k];
            }
            acc[r] += d * (float(sc[0]) * s1 + float(sc[2]) * s2 + float(sc[4]) * s3 + float(sc[6]) * s4);
        }
    }
    for (int r = 0; r < ROWS; ++r)
    {
        const float v = simd_sum(acc[r]);
        if (lane == 0 && r0 + r < rows) y[r0 + r] = v;
    }
}

// Lecture pure : chaque thread somme des uint4 consécutifs (plafond du débit).
kernel void read_only(device const uint4* W [[buffer(0)]], device uint* out [[buffer(2)]], constant long& n16 [[buffer(3)]],
    uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]])
{
    uint4 s = 0;
    for (long i = gid; i < n16; i += gsize) s += W[i];
    if ((s.x ^ s.y ^ s.z ^ s.w) == 0x12345678u) out[0] = s.x;
}

// Version 3 (Q4_K) : 8 threads par bloc, 16 octets de quants chacun (lecture alignée de 16
// octets), 4 blocs par itération du simdgroup ; échelles décodées sans branchement.
kernel void mv3_q4_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
    device float* y [[buffer(2)]], constant long& rows [[buffer(3)]], constant long& cols [[buffer(4)]],
    uint tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]],
    uint lane [[thread_index_in_simdgroup]])
{
    const long r0 = (long(tg) * SG_PER_TG + sg) * ROWS;
    if (r0 >= rows) return;
    const long nb = cols / 256;
    const long row_bytes = nb * 144;
    const int k = lane % 8;              // 16 octets k du bloc
    const int j = k / 2;                 // morceau de 32 octets : sous-blocs 2j et 2j + 1
    const int p = (k % 2) * 16;          // position dans le sous-bloc
    float acc[ROWS] = { 0 };
    for (long b = lane / 8; b < nb; b += 4)
    {
        device const float4* xlo = (device const float4*)(x + b * 256 + 64 * j + p);
        device const float4* xhi = xlo + 8;
        float4 lo[4], hi[4];
        float slo = 0, shi = 0;
        for (int i = 0; i < 4; ++i)
        {
            lo[i] = xlo[i]; hi[i] = xhi[i];
            slo += lo[i].x + lo[i].y + lo[i].z + lo[i].w;
            shi += hi[i].x + hi[i].y + hi[i].z + hi[i].w;
        }
        for (int r = 0; r < ROWS; ++r)
        {
            const long row = r0 + r;
            if (row >= rows) break;
            device const uchar* blk = W + row * row_bytes + b * 144;
            device const uint* u = (device const uint*)(blk + 4);
            const uint u0 = u[0], u1 = u[1], u2 = u[2];
            // Octet j et j + 4 des échelles décodées (get_scale_min_k4 pour 2j et 2j + 1).
            const uint sc_lo = u0 & 0x3F3F3F3F, sc_hi = (u2 & 0x0F0F0F0F) | (((u0 >> 6) & 0x03030303) << 4);
            const uint mn_lo = u1 & 0x3F3F3F3F, mn_hi = ((u2 >> 4) & 0x0F0F0F0F) | (((u1 >> 6) & 0x03030303) << 4);
            const int s1i = 2 * j, s2i = 2 * j + 1;
            const float sc1 = float(((s1i < 4 ? sc_lo : sc_hi) >> (8 * (s1i & 3))) & 0xFF);
            const float sc2 = float(((s2i < 4 ? sc_lo : sc_hi) >> (8 * (s2i & 3))) & 0xFF);
            const float m1 = float(((s1i < 4 ? mn_lo : mn_hi) >> (8 * (s1i & 3))) & 0xFF);
            const float m2 = float(((s2i < 4 ? mn_lo : mn_hi) >> (8 * (s2i & 3))) & 0xFF);
            const half2 dd = *(device const half2*)blk;
            const uchar4 q4[4] = { ((device const uchar4*)(blk + 16 + 16 * k))[0], ((device const uchar4*)(blk + 16 + 16 * k))[1],
                                   ((device const uchar4*)(blk + 16 + 16 * k))[2], ((device const uchar4*)(blk + 16 + 16 * k))[3] };
            float plo = 0, phi = 0;
            for (int i = 0; i < 4; ++i)
            {
                plo += dot(float4(q4[i] & uchar(0xF)), lo[i]);
                phi += dot(float4(q4[i] >> uchar(4)), hi[i]);
            }
            acc[r] += float(dd.x) * (sc1 * plo + sc2 * phi) - float(dd.y) * (m1 * slo + m2 * shi);
        }
    }
    for (int r = 0; r < ROWS; ++r)
    {
        const float v = simd_sum(acc[r]);
        if (lane == 0 && r0 + r < rows) y[r0 + r] = v;
    }
}
