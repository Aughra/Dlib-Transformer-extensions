// Metal kernels of the macOS port: the GPU of the Mac reads the GGUF weights where the file
// lies (the memory is shared with the processor) and computes the passes of the generation.
// macOS compiles this source when the engine opens the GPU (metal_device.mm): no Metal
// compiler is needed to build the engine.  CMakeLists.txt embeds it in the engine as a string.

#include <metal_stdlib>
using namespace metal;

// Products of a weight matrix with one vector of floats: a simdgroup computes ROWS rows, and a
// threadgroup holds SG_PER_TG simdgroups.
#define ROWS 4
#define SG_PER_TG 4

struct block_q4_K { half d; half dmin; uchar scales[12]; uchar qs[128]; };

// Q4_K: 8 threads per block of 256 values, 16 bytes of quants each (one aligned load), 4 blocks
// per iteration of the simdgroup; the scales and minimums decoded without branch.
kernel void mv_q4_K(device const uchar* W [[buffer(0)]], device const float* x [[buffer(1)]],
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
            // The scales and minimums of sub-blocks 2j and 2j + 1, as get_scale_min_k4 decodes them.
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

// Q6_K: the unit u of a row is block u / 4, half (u / 2) % 2, values l0 to l0 + 15 of each quarter,
// l0 = (u % 2) * 16; the blocks of 210 bytes are read byte by byte, their alignment being 2.
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
    // Unit u: block u / 4, half n = (u / 2) % 2, values l0 to l0 + 15 of each quarter, l0 = (u % 2) * 16.
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
