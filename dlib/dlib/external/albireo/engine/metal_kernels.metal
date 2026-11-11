// Metal kernels of the macOS port: the GPU of the Mac reads the GGUF weights where the file
// lies (the memory is shared with the processor) and computes the passes of the generation.
// macOS compiles this source when the engine opens the GPU (metal_device.mm): no Metal
// compiler is needed to build the engine.  CMakeLists.txt embeds it in the engine as a string.
//
// Each kernel computes what the processor runtime computes (cpu_runtime.cpp, qkernels.cpp), in
// floats: the sums come in another order, so that the results agree within the roundings of
// floats, not bit for bit.

#include <metal_stdlib>
using namespace metal;

// The tokens of one pass, at most (the generation steps of up to eight sequences).
#define MAX_TOKENS 8

// ---------------------------------------------------------------------------------------------
// Products of a weight matrix (GGUF blocks, read in place) with the activations of up to
// MAX_TOKENS tokens: y[t][r] = alpha * sum_i W[r][i] x[t][i] + beta * y[t][r].  A simdgroup
// computes ROWS rows, a threadgroup holds SG_PER_TG simdgroups.  One dispatch may compute two
// matrices of the same format and the same columns, W (rows rows, into y) then W2 (rows2 rows,
// into y2): the products of the queries and of the keys, or of the gate and of the up branch,
// read the same activations, and a dispatch costs more than its few rows on this GPU.

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

// The matrix of the rows of a simdgroup: W, or W2 beyond the rows of W (a multiple of ROWS).
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

// Q4_K: 8 threads per block of 256 values, 16 bytes of quants each (one aligned load), 4 blocks
// per iteration of the simdgroup; the scales and minimums decoded without branch.  The tokens
// go outside the rows, so that one token reads its activations once per block.
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
    const int k = lane % 8;              // the 16 bytes k of the block
    const int j = k / 2;                 // the chunk of 32 bytes: sub-blocks 2j (low bits) and 2j + 1 (high bits)
    const int p = (k % 2) * 16;          // the position in the sub-block
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
                // The scales and minimums of sub-blocks 2j and 2j + 1, as get_scale_min_k4 decodes them.
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

// Q6_K: the unit u of a row is block u / 4, half n = (u / 2) % 2, values l0 to l0 + 15 of each
// quarter, l0 = (u % 2) * 16; the blocks of 210 bytes are read byte by byte, their alignment
// being 2.  The tokens go outside the rows, as for Q4_K.
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

// One token (a generation step), or up to MAX_TOKENS, the accumulators of each variant held in registers.
template [[host_name("mv_q4_K_1")]] kernel void mv_q4_K<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q4_K_n")]] kernel void mv_q4_K<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q6_K_1")]] kernel void mv_q6_K<1>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);
template [[host_name("mv_q6_K_n")]] kernel void mv_q6_K<8>(device const uchar*, device const float*, device float*,
    constant mv_args&, device const uchar*, device float*, uint, uint, uint);

// ---------------------------------------------------------------------------------------------
// Rows normalized by their root mean square (cpu_runtime::rms_norm_rows): one threadgroup per
// row; x and y may be the same rows.

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

// A bias added to rows of n values: x[t * stride + i] += b[i].
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

// The gate of the feed-forward block: gate[i] = silu(gate[i]) * up[i] (qk_silu_mul).
kernel void silu_mul(device float* gate [[buffer(0)]], device const float* up [[buffer(1)]],
    constant uint& n [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    if (i >= n) return;
    const float g = gate[i];
    gate[i] = g / (1.0f + exp(-g)) * up[i];
}

// ---------------------------------------------------------------------------------------------
// The rotary positions (cpu_runtime::apply_rope): token t turns the first rope_dims values of
// each of its heads by the cosines and sines of its position, cs[t * cs_stride + i] and
// cs[t * cs_stride + half + i], in pairs (i, i + half) (neox) or (2i, 2i + 1).

struct rope_args
{
    uint heads;
    uint hd;
    uint half_dims;
    uint neox;
    uint stride;       // the floats of a token
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
// The rotation of the 8-bit keys and of the queries that read them (qk_rotate_row): the signs
// of a hash, then a Walsh-Hadamard transform over the largest power of two that divides hd,
// normalized.  One thread per value, the row in threadgroup memory; hd at most 256.

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

// The 8-bit form of a group of 32 values held by one simdgroup (qk_float_to_q8_row): the scale
// in half precision, raised to the next half when its rounding would put the largest value
// beyond 127, the values rounded to the nearest, ties to even, within [-127, 127].
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

// Where the keys and values of a layer lie in the region of the pool (kv_cache.h): block b
// starts at b * block_stride, the keys of head h at k_slab + h * head_stride, the values at
// v_slab + h * head_stride, their scales positions * hd bytes further (q8).
struct kv_args
{
    uint hd;
    uint q8;
    uint positions;    // positions of a block
    uint kv_stride;    // the floats of the keys (or values) of a token
    ulong block_stride;
    ulong k_slab;
    ulong v_slab;
    ulong head_stride;
};

// The keys and values of the tokens of a pass into their rows of the pool: token t at row
// rows[t] of block blocks[t].  Grid: one threadgroup of hd threads per (head, token).
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

// The queries scaled for the attention, and rotated as the 8-bit keys: one threadgroup of hd
// threads per (head, token).
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
// The attention of the tokens of a pass over the positions they see, in parts of part_len
// positions (the split of FlashDecoding): the grid holds one threadgroup of ATT_SG simdgroups
// per (part, key/value head, token); each simdgroup reads its keys and values once for the rep
// query heads that share them, with a softmax kept online, and the threadgroup writes the sums
// of its part, unnormalized, with their maximums and their sums.  merge_attention() joins the
// parts.  The keys of token t are those of its sequence, blocks[t * table_stride + p / positions],
// of which it sees the first lengths[t].

#define ATT_SG 4
#define MAX_REP 8
#define MAX_PER_LANE 8      // hd / 32 at most: hd up to 256

struct attention_args
{
    uint hd;
    uint rep;
    uint kv_heads;
    uint q8;
    uint positions;    // positions of a block
    uint part_len;
    uint parts;        // parts of the grid
    uint q_stride;     // the floats of the queries of a token
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
    // The simdgroups of the threadgroup joined: each in turn, with the largest maximum.
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
    // The outputs, a half of the row at a time so that the threadgroup memory stays small.
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
        // A single part writes the outputs, normalized, merge_attention() having nothing to join.
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
    if (sg == 0 && lane == 0)
        for (uint i = 0; i < REP; ++i)
        {
            out[REP * a.hd + i] = jm[i];
            out[REP * a.hd + REP + i] = jl[i];
        }
}


// The sizes the models take: query heads per key/value head (REP) and values of a head per lane (PER = hd / 32).
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

// The parts of the attention joined: for token t and query head h, the parts its length
// reaches, each weighted by exp(its maximum - the largest), divided by the total sum, into
// ctx[t * q_stride + h * hd].  One threadgroup of hd threads per (head, token).
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
