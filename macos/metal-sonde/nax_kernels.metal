// Sonde des accélérateurs neuronaux du M5 (fork pour macOS, 2026-11-21) : un produit de matrices
// C (M x N, floats) = A (M x K, demi-précision) . B^T (B : N x K, demi-précision, les poids rangés
// comme ceux d'un modèle), écrit deux fois :
//   - gemm_sg  : instructions matricielles classiques (simdgroup_matrix, fragments 8 x 8) ;
//   - gemm_nax : accélérateurs du M5, par MetalPerformancePrimitives (matmul2d, fragments 16 x 16),
//                la disposition des fragments reprise de MLX (steel/gemm/nax.h, licence MIT).
// Les deux noyaux ont le même découpage : un threadgroup de 4 simdgroups calcule une tuile de
// 64 x 128 sorties, chaque simdgroup 32 x 64, en lisant A et B directement dans la mémoire du GPU.
// M doit être multiple de 64, N de 128, K de 16.
#include <metal_stdlib>
#include <MetalPerformancePrimitives/MetalPerformancePrimitives.h>
using namespace metal;

struct dims { uint M, N, K; };

// --- simdgroup_matrix 8 x 8 ----------------------------------------------------------------------
kernel void gemm_sg(device const half* A [[buffer(0)]], device const half* B [[buffer(1)]],
    device float* C [[buffer(2)]], constant dims& d [[buffer(3)]],
    uint2 tg [[threadgroup_position_in_grid]], ushort sg [[simdgroup_index_in_threadgroup]])
{
    const uint m0 = tg.y * 64 + (sg / 2) * 32, n0 = tg.x * 128 + (sg % 2) * 64;
    simdgroup_float8x8 acc[4][8];
    for (uint i = 0; i < 4; ++i)
        for (uint j = 0; j < 8; ++j) acc[i][j] = make_filled_simdgroup_matrix<float, 8, 8>(0.0f);
    for (uint k0 = 0; k0 < d.K; k0 += 8)
    {
        simdgroup_half8x8 a[4], b[8];
        for (uint i = 0; i < 4; ++i) simdgroup_load(a[i], A + ulong(m0 + i * 8) * d.K + k0, d.K);
        for (uint j = 0; j < 8; ++j) simdgroup_load(b[j], B + ulong(n0 + j * 8) * d.K + k0, d.K, ulong2(0, 0), true);
        for (uint i = 0; i < 4; ++i)
            for (uint j = 0; j < 8; ++j) simdgroup_multiply_accumulate(acc[i][j], a[i], b[j], acc[i][j]);
    }
    for (uint i = 0; i < 4; ++i)
        for (uint j = 0; j < 8; ++j) simdgroup_store(acc[i][j], C + ulong(m0 + i * 8) * d.N + n0 + j * 8, d.N);
}

// --- accélérateurs du M5 (MetalPerformancePrimitives) ----------------------------------------------
// Un fragment 16 x 16 : chaque voie tient 8 valeurs, 2 lignes (fm, fm + 8) de 4 colonnes (fn .. fn + 3).
inline short2 nax_coord(ushort lane)
{
    const short qid = lane >> 2;
    const short fm = (qid & 4) | ((lane >> 1) & 3);
    const short fn = ((qid & 2) | (lane & 1)) * 4;
    return short2(fn, fm);
}

kernel void gemm_nax(device const half* A [[buffer(0)]], device const half* B [[buffer(1)]],
    device float* C [[buffer(2)]], constant dims& d [[buffer(3)]],
    uint2 tg [[threadgroup_position_in_grid]], ushort sg [[simdgroup_index_in_threadgroup]],
    ushort lane [[thread_index_in_simdgroup]])
{
    const uint m0 = tg.y * 64 + (sg / 2) * 32, n0 = tg.x * 128 + (sg % 2) * 64;
    const short2 c = nax_coord(lane);
    constexpr auto desc = mpp::tensor_ops::matmul2d_descriptor(16, 32, 16, false, true, true,
        mpp::tensor_ops::matmul2d_descriptor::mode::multiply_accumulate);
    mpp::tensor_ops::matmul2d<desc, execution_simdgroup> op;
    // acc[mi][oj] : la sortie 16 x 32 (deux fragments) des lignes mi et des colonnes oj.
    float acc[2][2][16];
    for (uint i = 0; i < 2; ++i)
        for (uint j = 0; j < 2; ++j)
            for (uint e = 0; e < 16; ++e) acc[i][j][e] = 0.0f;
    for (uint k0 = 0; k0 < d.K; k0 += 16)
    {
        half a[2][8], b[4][8];
        for (uint i = 0; i < 2; ++i)
            for (uint e = 0; e < 8; ++e)
                a[i][e] = A[ulong(m0 + i * 16 + c.y + (e / 4) * 8) * d.K + k0 + c.x + e % 4];
        for (uint j = 0; j < 4; ++j)
            for (uint e = 0; e < 8; ++e)
                b[j][e] = B[ulong(n0 + j * 16 + c.y + (e / 4) * 8) * d.K + k0 + c.x + e % 4];
        for (uint i = 0; i < 2; ++i)
            for (uint j = 0; j < 2; ++j)
            {
                auto ct_a = op.template get_left_input_cooperative_tensor<half, half, float>();
                auto ct_b = op.template get_right_input_cooperative_tensor<half, half, float>();
                auto ct_c = op.template get_destination_cooperative_tensor<
                    metal::remove_addrspace_t<decltype(ct_a)>, metal::remove_addrspace_t<decltype(ct_b)>, float>();
                for (uint e = 0; e < 8; ++e)
                {
                    ct_a[e] = a[i][e];
                    ct_b[e] = b[2 * j][e];
                    ct_b[8 + e] = b[2 * j + 1][e];
                }
                for (uint e = 0; e < 16; ++e) ct_c[e] = acc[i][j][e];
                op.run(ct_a, ct_b, ct_c);
                for (uint e = 0; e < 16; ++e) acc[i][j][e] = ct_c[e];
            }
    }
    for (uint i = 0; i < 2; ++i)
        for (uint j = 0; j < 2; ++j)
            for (uint f = 0; f < 2; ++f)
                for (uint e = 0; e < 8; ++e)
                    C[ulong(m0 + i * 16 + c.y + (e / 4) * 8) * d.N + n0 + j * 32 + f * 16 + c.x + e % 4] =
                        acc[i][j][f * 8 + e];
}
