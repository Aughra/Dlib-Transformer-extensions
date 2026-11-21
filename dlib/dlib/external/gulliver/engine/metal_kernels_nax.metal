// Les noyaux de Gulliver sur les accélérateurs neuronaux du M5 (fork pour macOS, 2026-11-21, chantier « Vitesse »,
// V1/V5) : compilés à part, en Metal 4 (MetalPerformancePrimitives), seulement quand le GPU en a (metal_device::open()).
// Sinon, le moteur garde ses noyaux de metal_kernels.metal.
//
// attention_nax remplace attention_mtile pour les têtes de 64 et 128 valeurs : mêmes entrées, même grille de tuiles.
// Son organisation reprend celle de l'attention de MLX (steel_attention_nax.h, licence MIT) : un threadgroup de
// 4 simdgroups par (tuile, tête de clés), 16 lignes de requête par simdgroup (ligne = token x REP + tête), par pas de
// 32 positions ; les clés et valeurs du pas chargées une fois par le threadgroup en demi-précision (mémoire de
// threadgroup), puis les scores S = Q K^T et la somme O += P V par matmul2d (fragments 16 x 16) ; les scores restent
// dans les registres, le softmax en ligne se fait par échanges entre les 4 voies d'une même ligne.  Les clés et valeurs sont lues dans la mémoire de conversation par blocs (table de blocs), en 8 bits
// (une échelle par groupe de 32) ou en demi-précision.
#include <metal_stdlib>
#include <MetalPerformancePrimitives/MetalPerformancePrimitives.h>
using namespace metal;

// Les positions d'un pas.
#ifndef NAX_BK
#define NAX_BK 32
#endif

struct attention_args
{
    uint hd;
    uint rep;
    uint kv_heads;
    uint q8;
    uint positions;
    uint part_len;
    uint parts;
    uint q_stride;
    uint table_stride;
    uint window;
    ulong block_stride;
    ulong k_slab;
    ulong v_slab;
    ulong head_stride;
    ulong scales;
};

// Un fragment 16 x 16 : la voie lane tient 8 valeurs, aux lignes fm et fm + 8, colonnes fn à fn + 3 (disposition des
// fragments de matmul2d, reprise de MLX).  Les 4 voies d'une même ligne ne diffèrent que par les bits 0 et 3.
inline short2 nax_coord(ushort lane)
{
    const short qid = lane >> 2;
    const short fm = (qid & 4) | ((lane >> 1) & 3);
    const short fn = ((qid & 2) | (lane & 1)) * 4;
    return short2(fn, fm);
}

inline float row_max(float v)
{
    v = max(v, simd_shuffle_xor(v, ushort(1)));
    return max(v, simd_shuffle_xor(v, ushort(8)));
}

inline float row_sum(float v)
{
    v += simd_shuffle_xor(v, ushort(1));
    return v + simd_shuffle_xor(v, ushort(8));
}

// Quatre valeurs consécutives (dimensions e à e + 3) de la position dont row est la ligne dans son bloc, s l'adresse de
// la tranche des clés ou des valeurs de la tête dans ce bloc ; zéro pour une position au-delà de la dernière.
template <uint HD>
inline half4 load4(device const uchar* s, ulong row, uint e, bool valid, constant attention_args& a)
{
    if (!valid) return half4(0.0h);
    if (a.q8)
    {
        const float d = float(as_type<half>(*(device const ushort*)(s + a.scales + row * (HD / 32) * 2 + (e / 32) * 2)));
        return half4(d * float4(*(device const char4*)(s + row * HD + e)));
    }
    return *(device const half4*)((device const half*)s + row * HD + e);
}

template <uint REP, uint HD>
kernel void attention_nax(device const float* q [[buffer(0)]], device const uchar* region [[buffer(1)]],
    device const int* blocks [[buffer(2)]], device const int* lengths [[buffer(3)]], device const int* tiles [[buffer(4)]],
    constant attention_args& a [[buffer(5)]], device float* ctx [[buffer(6)]],
    uint3 tg [[threadgroup_position_in_grid]], ushort sg [[simdgroup_index_in_threadgroup]],
    ushort lane [[thread_index_in_simdgroup]], ushort nsg [[simdgroups_per_threadgroup]])
{
    constexpr uint TD = HD / 16;
    const uint kvh = tg.y;
    const int t0 = tiles[2 * tg.x], count = tiles[2 * tg.x + 1];
    const short2 c = nax_coord(lane);

    // Les deux lignes de requête de la voie : leur token, leur tête et la longueur qu'il voit.
    int tt[2];
    uint hh[2];
    long len[2];
    for (uint k = 0; k < 2; ++k)
    {
        const uint row = sg * 16 + c.y + k * 8;
        tt[k] = int(row / REP);
        hh[k] = row % REP;
        len[k] = tt[k] < count ? long(lengths[t0 + tt[k]]) : 0;
    }
    // Les requêtes, dans les registres pour toute la tuile.
    half qf[TD][8];
    for (uint d = 0; d < TD; ++d)
        for (uint e = 0; e < 8; ++e)
        {
            const uint k = e / 4;
            qf[d][e] = tt[k] < count
                ? half(q[ulong(t0 + tt[k]) * a.q_stride + (kvh * REP + hh[k]) * HD + d * 16 + c.x + e % 4]) : 0.0h;
        }

    float o[TD][8];
    for (uint d = 0; d < TD; ++d)
        for (uint e = 0; e < 8; ++e) o[d][e] = 0.0f;
    float m[2] = { -INFINITY, -INFINITY }, l[2] = { 0.0f, 0.0f };

    const long last = long(lengths[t0 + count - 1]);
    const long first = a.window > 0 ? max(0L, long(lengths[t0]) - long(a.window)) : 0;
    device const int* table = blocks + ulong(t0) * a.table_stride;

    constexpr auto dqk = mpp::tensor_ops::matmul2d_descriptor(16, 32, 16, false, true, true,
        mpp::tensor_ops::matmul2d_descriptor::mode::multiply_accumulate);
    constexpr auto dpv = mpp::tensor_ops::matmul2d_descriptor(16, 32, 16, false, false, true,
        mpp::tensor_ops::matmul2d_descriptor::mode::multiply_accumulate);
    mpp::tensor_ops::matmul2d<dqk, execution_simdgroup> op_qk;
    mpp::tensor_ops::matmul2d<dpv, execution_simdgroup> op_pv;

    // Les clés et valeurs d'un pas, chargées une fois par le threadgroup, en demi-précision (développées des 8 bits
    // une seule fois), puis lues en fragments par les 4 simdgroups.
    constexpr uint BK = NAX_BK, NF = BK / 16;
    threadgroup half ks[BK * HD];
    threadgroup half vs[BK * HD];
    const uint tid = sg * 32 + lane;

    for (long p0 = first; p0 < last; p0 += BK)
    {
        // Huit valeurs consécutives par fil et par passage : 32 positions x HD valeurs, 128 fils.
        for (uint idx = tid * 8; idx < BK * HD; idx += uint(nsg) * 32 * 8)
        {
            const uint jj = idx / HD, e = idx % HD;
            const long p = p0 + jj;
            threadgroup half4* kd = (threadgroup half4*)(ks + idx);
            threadgroup half4* vd = (threadgroup half4*)(vs + idx);
            if (p >= last)
            {
                kd[0] = kd[1] = vd[0] = vd[1] = half4(0.0h);
                continue;
            }
            const ulong head = ulong(table[p / a.positions]) * a.block_stride + ulong(kvh) * a.head_stride;
            const ulong row = ulong(p % a.positions);
            kd[0] = load4<HD>(region + head + a.k_slab, row, e, true, a);
            kd[1] = load4<HD>(region + head + a.k_slab, row, e + 4, true, a);
            vd[0] = load4<HD>(region + head + a.v_slab, row, e, true, a);
            vd[1] = load4<HD>(region + head + a.v_slab, row, e + 4, true, a);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        // S = Q K^T : 16 lignes x 32 positions (deux fragments).
        float s[NF * 8];
        for (uint e = 0; e < NF * 8; ++e) s[e] = 0.0f;
        for (uint d = 0; d < TD; ++d)
        for (uint f2 = 0; f2 < NF; f2 += 2)
        {
            auto ct_a = op_qk.template get_left_input_cooperative_tensor<half, half, float>();
            auto ct_b = op_qk.template get_right_input_cooperative_tensor<half, half, float>();
            auto ct_c = op_qk.template get_destination_cooperative_tensor<
                metal::remove_addrspace_t<decltype(ct_a)>, metal::remove_addrspace_t<decltype(ct_b)>, float>();
            for (uint e = 0; e < 8; ++e) ct_a[e] = qf[d][e];
            for (uint f = 0; f < 2; ++f)
                for (uint k = 0; k < 2; ++k)
                {
                    const half4 v = *(threadgroup const half4*)(ks + ((f2 + f) * 16 + c.y + k * 8) * HD + d * 16 + c.x);
                    for (uint j = 0; j < 4; ++j) ct_b[f * 8 + k * 4 + j] = v[j];
                }
            for (uint e = 0; e < 16; ++e) ct_c[e] = s[f2 * 8 + e];
            op_qk.run(ct_a, ct_b, ct_c);
            for (uint e = 0; e < 16; ++e) s[f2 * 8 + e] = ct_c[e];
        }

        // Le softmax en ligne, ligne par ligne (k), les scores restant dans les registres.
        for (uint k = 0; k < 2; ++k)
        {
            float mx = -INFINITY;
            for (uint f = 0; f < NF; ++f)
                for (uint j = 0; j < 4; ++j)
                {
                    const uint idx = f * 8 + k * 4 + j;
                    const long p = p0 + f * 16 + c.x + j;
                    const bool visible = p < len[k] && (a.window == 0 || p >= len[k] - long(a.window));
                    if (!visible) s[idx] = -INFINITY;
                    mx = max(mx, s[idx]);
                }
            mx = row_max(mx);
            const float mn = max(m[k], mx);
            const float shrink = mn == -INFINITY ? 1.0f : exp(m[k] - mn);
            float sum = 0.0f;
            for (uint f = 0; f < NF; ++f)
                for (uint j = 0; j < 4; ++j)
                {
                    const uint idx = f * 8 + k * 4 + j;
                    const float pk = mn == -INFINITY ? 0.0f : exp(s[idx] - mn);
                    s[idx] = pk;
                    sum += pk;
                }
            l[k] = l[k] * shrink + row_sum(sum);
            m[k] = mn;
            for (uint d = 0; d < TD; ++d)
                for (uint j = 0; j < 4; ++j) o[d][k * 4 + j] *= shrink;
        }

        // O += P V : deux fragments de dimensions à la fois, les deux fragments de positions l'un après l'autre.
        for (uint dp = 0; dp < TD; dp += 2)
            for (uint f = 0; f < NF; ++f)
            {
                auto ct_a = op_pv.template get_left_input_cooperative_tensor<half, half, float>();
                auto ct_b = op_pv.template get_right_input_cooperative_tensor<half, half, float>();
                auto ct_c = op_pv.template get_destination_cooperative_tensor<
                    metal::remove_addrspace_t<decltype(ct_a)>, metal::remove_addrspace_t<decltype(ct_b)>, float>();
                for (uint e = 0; e < 8; ++e) ct_a[e] = half(s[f * 8 + e]);
                for (uint h = 0; h < 2; ++h)
                    for (uint k = 0; k < 2; ++k)
                    {
                        const half4 v = *(threadgroup const half4*)(vs + (f * 16 + c.y + k * 8) * HD + (dp + h) * 16 + c.x);
                        for (uint j = 0; j < 4; ++j) ct_b[h * 8 + k * 4 + j] = v[j];
                    }
                for (uint e = 0; e < 8; ++e)
                {
                    ct_c[e] = o[dp][e];
                    ct_c[8 + e] = o[dp + 1][e];
                }
                op_pv.run(ct_a, ct_b, ct_c);
                for (uint e = 0; e < 8; ++e)
                {
                    o[dp][e] = ct_c[e];
                    o[dp + 1][e] = ct_c[8 + e];
                }
            }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (uint k = 0; k < 2; ++k)
    {
        if (tt[k] >= count) continue;
        const float inv = l[k] > 0.0f ? 1.0f / l[k] : 0.0f;
        device float* out = ctx + ulong(t0 + tt[k]) * a.q_stride + (kvh * REP + hh[k]) * HD;
        for (uint d = 0; d < TD; ++d)
            for (uint j = 0; j < 4; ++j) out[d * 16 + c.x + j] = o[d][k * 4 + j] * inv;
    }
}

#define ATT_NAX(R, H) template [[host_name("attention_nax_" #R "_" #H)]] kernel void attention_nax<R, H>( \
    device const float*, device const uchar*, device const int*, device const int*, device const int*, \
    constant attention_args&, device float*, uint3, ushort, ushort, ushort);
ATT_NAX(1, 64) ATT_NAX(2, 64) ATT_NAX(4, 64) ATT_NAX(8, 64)
ATT_NAX(1, 128) ATT_NAX(2, 128) ATT_NAX(4, 128) ATT_NAX(8, 128)
