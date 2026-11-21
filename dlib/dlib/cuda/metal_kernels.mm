// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.

// The Metal versions of the tensor_tools operations (see metal_dispatch.h).  The kernels
// are written in the Metal Shading Language below and compiled from source the first time
// one is needed, with the precise math functions.  Each operation waits for its command
// buffer: when it returns, the values are in the tensor.

#ifndef DLIB_USE_METAL
#error "metal_kernels.mm is compiled only in a build with DLIB_USE_METAL"
#endif
#if !__has_feature(objc_arc)
#error "metal_kernels.mm needs automatic reference counting (-fobjc-arc)"
#endif

#import <Foundation/Foundation.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include "metal_dlib_internal.h"
#include "metal_dispatch.h"
#include "gemm_planes.h"
#include "gguf_blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <mutex>
#include <vector>

namespace dlib
{
    namespace metal
    {
        namespace
        {
            const char* const kernel_source = R"METAL(
#include <metal_stdlib>
using namespace metal;

// ----------------------------------------------------------------------------------------
// Element-wise addition and multiplication

struct fill_params
{
    float value;
    uint count;
};

kernel void fill_value (
    device float* d                    [[buffer(0)]],
    constant fill_params& p            [[buffer(1)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i < p.count)
        d[i] = p.value;
}

kernel void add_same (
    device float* d          [[buffer(0)]],
    device const float* a    [[buffer(1)]],
    device const float* b    [[buffer(2)]],
    constant uint& count     [[buffer(3)]],
    uint i                   [[thread_position_in_grid]])
{
    if (i < count)
        d[i] = a[i] + b[i];
}

struct shape4 { uint n, k, nr, nc; };

struct add_padded_params
{
    shape4 d, a, b;
    uint count;
};

// Each value of d is the sum of the values of a and b at the same (n,k,r,c), a value
// outside a source counting as 0.
kernel void add_padded (
    device float* d                    [[buffer(0)]],
    device const float* a              [[buffer(1)]],
    device const float* b              [[buffer(2)]],
    constant add_padded_params& p      [[buffer(3)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.count)
        return;
    const uint c = i % p.d.nc;
    uint t = i / p.d.nc;
    const uint r = t % p.d.nr;
    t /= p.d.nr;
    const uint k = t % p.d.k;
    const uint n = t / p.d.k;

    float v1 = 0;
    float v2 = 0;
    if (n < p.a.n && k < p.a.k && r < p.a.nr && c < p.a.nc)
        v1 = a[((n*p.a.k + k)*p.a.nr + r)*p.a.nc + c];
    if (n < p.b.n && k < p.b.k && r < p.b.nr && c < p.b.nc)
        v2 = b[((n*p.b.k + k)*p.b.nr + r)*p.b.nc + c];
    d[i] = v1 + v2;
}

struct add_scaled_params
{
    shape4 d, s;
    float beta, alpha;
    uint count;
};

// d = beta*d + alpha*s, s broadcast along each of its dimensions of size 1.
kernel void add_scaled (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    constant add_scaled_params& p      [[buffer(2)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.count)
        return;
    const uint c = i % p.d.nc;
    uint t = i / p.d.nc;
    const uint r = t % p.d.nr;
    t /= p.d.nr;
    const uint k = t % p.d.k;
    const uint n = t / p.d.k;

    const uint sn = p.s.n == 1 ? 0 : n;
    const uint sk = p.s.k == 1 ? 0 : k;
    const uint sr = p.s.nr == 1 ? 0 : r;
    const uint sc = p.s.nc == 1 ? 0 : c;
    d[i] = p.beta*d[i] + p.alpha*s[((sn*p.s.k + sk)*p.s.nr + sr)*p.s.nc + sc];
}

struct multiply_params
{
    uint add_to;
    uint dsize, asize, bsize, max_size;
};

// The three cases of cpu::multiply.  Same sizes: one product per value.
kernel void multiply_same (
    device float* d                    [[buffer(0)]],
    device const float* a              [[buffer(1)]],
    device const float* b              [[buffer(2)]],
    constant multiply_params& p        [[buffer(3)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.dsize)
        return;
    if (p.add_to)
        d[i] += a[i]*b[i];
    else
        d[i] = a[i]*b[i];
}

// A destination of one sample sums the products over the samples of the sources, in the
// order of the CPU loop: value j receives the products j, j+dsize, j+2*dsize...
kernel void multiply_reduce (
    device float* d                    [[buffer(0)]],
    device const float* a              [[buffer(1)]],
    device const float* b              [[buffer(2)]],
    constant multiply_params& p        [[buffer(3)]],
    uint j                             [[thread_position_in_grid]])
{
    if (j >= p.dsize)
        return;
    float acc = p.add_to ? d[j] : 0.0f;
    for (uint i = j; i < p.max_size; i += p.dsize)
        acc += a[i % p.asize]*b[i % p.bsize];
    d[j] = acc;
}

// Otherwise a source of one sample is repeated over the samples of the destination.
kernel void multiply_broadcast (
    device float* d                    [[buffer(0)]],
    device const float* a              [[buffer(1)]],
    device const float* b              [[buffer(2)]],
    constant multiply_params& p        [[buffer(3)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.max_size)
        return;
    if (p.add_to)
        d[i] += a[i % p.asize]*b[i % p.bsize];
    else
        d[i] = a[i % p.asize]*b[i % p.bsize];
}

// ----------------------------------------------------------------------------------------
// Softmax: one thread per vector, in the order of the CPU loops

struct softmax_params
{
    uint samples, channels, locations, nr, nc;
};

// Across the channels, at each location of each sample.
kernel void softmax_channels (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    constant softmax_params& p         [[buffer(2)]],
    uint t                             [[thread_position_in_grid]])
{
    if (t >= p.samples*p.locations)
        return;
    const uint n = t / p.locations;
    const uint base = n*p.locations*p.channels + t % p.locations;
    const uint step = p.locations;

    float max_val = -INFINITY;
    for (uint k = 0; k < p.channels; ++k)
        max_val = max(max_val, s[base + k*step]);
    float sum = 0.0f;
    for (uint k = 0; k < p.channels; ++k)
    {
        const float e = exp(s[base + k*step] - max_val);
        d[base + k*step] = e;
        sum += e;
    }
    for (uint k = 0; k < p.channels; ++k)
        d[base + k*step] /= sum;
}

// Along each row of each channel; a row of -infinity only gives zeros.
kernel void softmax_rows (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    constant softmax_params& p         [[buffer(2)]],
    uint t                             [[thread_position_in_grid]])
{
    if (t >= p.samples*p.channels*p.nr)
        return;
    const uint base = t*p.nc;

    float max_val = -INFINITY;
    for (uint c = 0; c < p.nc; ++c)
        max_val = max(max_val, s[base + c]);
    if (max_val == -INFINITY)
    {
        for (uint c = 0; c < p.nc; ++c)
            d[base + c] = 0.0f;
        return;
    }
    float sum = 0.0f;
    for (uint c = 0; c < p.nc; ++c)
    {
        const float e = exp(s[base + c] - max_val);
        d[base + c] = e;
        sum += e;
    }
    for (uint c = 0; c < p.nc; ++c)
        d[base + c] /= sum;
}

// ----------------------------------------------------------------------------------------
// Activations

// The Metal library has no erf.  This is formula 7.1.26 of Abramowitz and Stegun, Handbook
// of Mathematical Functions (1964), whose error on erf is at most 1.5e-7 for every x.
float erf_as (float x)
{
    const float sign = x < 0 ? -1.0f : 1.0f;
    x = fabs(x);
    const float t = 1.0f/(1.0f + 0.3275911f*x);
    const float poly = ((((1.061405429f*t - 1.453152027f)*t + 1.421413741f)*t
                        - 0.284496736f)*t + 0.254829592f)*t;
    return sign*(1.0f - poly*exp(-x*x));
}

kernel void gelu (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    constant uint& count               [[buffer(2)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= count)
        return;
    const float x = s[i];
    d[i] = 0.5f*x*(1.0f + erf_as(x*M_SQRT1_2_F));
}

// The formula of the CPU: x * (1/(1 + exp(-x))).
kernel void silu (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    constant uint& count               [[buffer(2)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= count)
        return;
    const float x = s[i];
    d[i] = x*(1.0f/(1.0f + exp(-x)));
}

// ----------------------------------------------------------------------------------------
// Layer normalization: a threadgroup per sample sums its values and their squares, each
// thread over a stride of the group width, then the simdgroups and the group combine their
// partial sums.  Any number of values per sample works, multiple of the width or not.

struct layer_norm_params
{
    uint samples;
    uint per_sample;    // k*nr*nc
    uint plane;         // nr*nc: the values of one channel, which share gamma and beta
    float eps;
};

kernel void layer_norm_stats (
    device const float* s              [[buffer(0)]],
    device float* means                [[buffer(1)]],
    device float* invstds              [[buffer(2)]],
    constant layer_norm_params& p      [[buffer(3)]],
    uint n                             [[threadgroup_position_in_grid]],
    uint tid                           [[thread_index_in_threadgroup]],
    uint width                         [[threads_per_threadgroup]],
    uint lane                          [[thread_index_in_simdgroup]],
    uint simd                          [[simdgroup_index_in_threadgroup]],
    uint simds                         [[simdgroups_per_threadgroup]])
{
    threadgroup float part_sum[32];
    threadgroup float part_sq[32];

    device const float* x = s + (size_t)n*p.per_sample;
    float a = 0;
    float b = 0;
    for (uint i = tid; i < p.per_sample; i += width)
    {
        const float v = x[i];
        a += v;
        b += v*v;
    }
    a = simd_sum(a);
    b = simd_sum(b);
    if (lane == 0)
    {
        part_sum[simd] = a;
        part_sq[simd] = b;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0)
    {
        float sa = 0;
        float sb = 0;
        for (uint g = 0; g < simds; ++g)
        {
            sa += part_sum[g];
            sb += part_sq[g];
        }
        // The formulas of the CPU: the mean of the values and of their squares.
        const float m = sa/p.per_sample;
        const float sq = sb/p.per_sample;
        means[n] = m;
        invstds[n] = 1.0f/sqrt(sq - m*m + p.eps);
    }
}

kernel void layer_norm_apply (
    device float* d                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    device const float* means          [[buffer(2)]],
    device const float* invstds        [[buffer(3)]],
    device const float* gamma          [[buffer(4)]],
    device const float* beta           [[buffer(5)]],
    constant layer_norm_params& p      [[buffer(6)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.samples*p.per_sample)
        return;
    const uint n = i / p.per_sample;
    const uint k = (i % p.per_sample) / p.plane;
    const float y = (s[i] - means[n])*invstds[n];
    d[i] = y*gamma[k] + beta[k];
}
// ----------------------------------------------------------------------------------------
// Products by GGUF weight matrices read in place (Q4_0 and Q8_0), decoded on the fly.  A
// block holds 32 consecutive values of a row and starts with its scale, a half precision
// float: Q4_0 then packs 32 nibbles in 16 bytes (value j in the low nibble of byte j, value
// j+16 in its high nibble, each minus 8), Q8_0 32 signed bytes.  A simdgroup handles a row:
// its lanes take the blocks in turn, each block's 32 products are summed in the order of the
// CPU, and the lanes' sums are added by simd_sum.  The sources go by 8 rows, so that the
// weights are read once for every 8 of them, as on the CPU.

struct matvec_params
{
    uint rows, cols, n, row_bytes;
};

inline float half_at (device const uchar* p)
{
    const ushort h = ushort(p[0]) | ushort(ushort(p[1]) << 8);
    return float(as_type<half>(h));
}

inline void decode_q4_0 (device const uchar* b, thread float* v)
{
    const float d = half_at(b);
    for (int j = 0; j < 16; ++j)
    {
        v[j] = float(int(b[2 + j] & 0x0F) - 8)*d;
        v[j + 16] = float(int(b[2 + j] >> 4) - 8)*d;
    }
}

inline void decode_q8_0 (device const uchar* b, thread float* v)
{
    const float d = half_at(b);
    for (int j = 0; j < 32; ++j)
        v[j] = float(as_type<char>(b[2 + j]))*d;
}

template <int block_bytes>
inline void matvec_rows (
    device float* y,
    device const float* x,
    device const uchar* w,
    constant matvec_params& p,
    uint r,
    uint lane)
{
    device const uchar* row = w + (size_t)r*p.row_bytes;
    const uint groups = p.cols/32;
    for (uint t0 = 0; t0 < p.n; t0 += 8)
    {
        const uint tn = min(8u, p.n - t0);
        float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (uint g = lane; g < groups; g += 32)
        {
            float v[32];
            if (block_bytes == 18)
                decode_q4_0(row + g*18, v);
            else
                decode_q8_0(row + g*34, v);
            for (uint t = 0; t < tn; ++t)
            {
                device const float* xt = x + (size_t)(t0 + t)*p.cols + g*32;
                float s = 0;
                for (int i = 0; i < 32; ++i)
                    s += v[i]*xt[i];
                acc[t] += s;
            }
        }
        for (uint t = 0; t < tn; ++t)
        {
            const float s = simd_sum(acc[t]);
            if (lane == 0)
                y[(size_t)(t0 + t)*p.rows + r] = s;
        }
    }
}

kernel void gguf_matvec_q4_0 (
    device float* y                    [[buffer(0)]],
    device const float* x              [[buffer(1)]],
    device const uchar* w              [[buffer(2)]],
    constant matvec_params& p          [[buffer(3)]],
    uint r                             [[threadgroup_position_in_grid]],
    uint lane                          [[thread_index_in_threadgroup]])
{
    matvec_rows<18>(y, x, w, p, r, lane);
}

kernel void gguf_matvec_q8_0 (
    device float* y                    [[buffer(0)]],
    device const float* x              [[buffer(1)]],
    device const uchar* w              [[buffer(2)]],
    constant matvec_params& p          [[buffer(3)]],
    uint r                             [[threadgroup_position_in_grid]],
    uint lane                          [[thread_index_in_threadgroup]])
{
    matvec_rows<34>(y, x, w, p, r, lane);
}
// ----------------------------------------------------------------------------------------
// Backward passes.  As on the CPU, a gradient is written over when grad is the same tensor
// as gradient_input, and added to grad otherwise (p.assign).

struct softmax_grad_params
{
    uint samples, channels, locations, nr, nc;
    uint assign;
};

kernel void softmax_gradient_channels (
    device float* g                    [[buffer(0)]],
    device const float* d              [[buffer(1)]],
    device const float* in             [[buffer(2)]],
    constant softmax_grad_params& p    [[buffer(3)]],
    uint t                             [[thread_position_in_grid]])
{
    if (t >= p.samples*p.locations)
        return;
    const uint base = (t / p.locations)*p.locations*p.channels + t % p.locations;
    const uint step = p.locations;
    float sum = 0.0f;
    for (uint k = 0; k < p.channels; ++k)
        sum += -d[base + k*step]*in[base + k*step];
    for (uint k = 0; k < p.channels; ++k)
    {
        const uint i = base + k*step;
        const float v = d[i]*(sum + in[i]);
        if (p.assign)
            g[i] = v;
        else
            g[i] += v;
    }
}

kernel void softmax_gradient_rows (
    device float* g                    [[buffer(0)]],
    device const float* d              [[buffer(1)]],
    device const float* in             [[buffer(2)]],
    constant softmax_grad_params& p    [[buffer(3)]],
    uint t                             [[thread_position_in_grid]])
{
    if (t >= p.samples*p.channels*p.nr)
        return;
    const uint base = t*p.nc;
    float sum = 0.0f;
    for (uint c = 0; c < p.nc; ++c)
        sum += -d[base + c]*in[base + c];
    for (uint c = 0; c < p.nc; ++c)
    {
        const float v = d[base + c]*(sum + in[base + c]);
        if (p.assign)
            g[base + c] = v;
        else
            g[base + c] += v;
    }
}

struct act_grad_params
{
    uint count;
    uint assign;
};

// d/dx of x*Phi(x): Phi(x) + x*phi(x), the formula of the CPU.
kernel void gelu_gradient (
    device float* g                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    device const float* in             [[buffer(2)]],
    constant act_grad_params& p        [[buffer(3)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.count)
        return;
    const float x = s[i];
    const float cdf = 0.5f*(1.0f + erf_as(x*M_SQRT1_2_F));
    const float pdf = 0.3989422804014327f*exp(-0.5f*x*x);   // 1/sqrt(2*pi)
    const float v = in[i]*(cdf + x*pdf);
    if (p.assign)
        g[i] = v;
    else
        g[i] += v;
}

kernel void silu_gradient (
    device float* g                    [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    device const float* in             [[buffer(2)]],
    constant act_grad_params& p        [[buffer(3)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.count)
        return;
    const float x = s[i];
    const float sig = 1.0f/(1.0f + exp(-x));
    const float v = in[i]*(sig*(1.0f + x*(1.0f - sig)));
    if (p.assign)
        g[i] = v;
    else
        g[i] += v;
}

struct bias_grad_params
{
    uint size;       // the values of one sample
    uint samples;
};

// grad[i] = the sum over the samples of gradient_input, in the order of the CPU.
kernel void bias_gradient (
    device float* g                    [[buffer(0)]],
    device const float* in             [[buffer(1)]],
    constant bias_grad_params& p       [[buffer(2)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.size)
        return;
    float acc = in[i];
    for (uint n = 1; n < p.samples; ++n)
        acc += in[(size_t)n*p.size + i];
    g[i] = acc;
}

struct channel_sum_params
{
    uint samples, channels, plane;
};

// grad[k] = the sum of gradient_input over the samples and the plane of channel k: a
// threadgroup per channel.
kernel void conv_bias_gradient (
    device float* g                    [[buffer(0)]],
    device const float* in             [[buffer(1)]],
    constant channel_sum_params& p     [[buffer(2)]],
    uint k                             [[threadgroup_position_in_grid]],
    uint tid                           [[thread_index_in_threadgroup]],
    uint width                         [[threads_per_threadgroup]],
    uint lane                          [[thread_index_in_simdgroup]],
    uint simd                          [[simdgroup_index_in_threadgroup]],
    uint simds                         [[simdgroups_per_threadgroup]])
{
    threadgroup float part[32];
    float a = 0;
    const uint per_channel = p.samples*p.plane;
    for (uint j = tid; j < per_channel; j += width)
    {
        const uint n = j / p.plane, i = j % p.plane;
        a += in[((size_t)n*p.channels + k)*p.plane + i];
    }
    a = simd_sum(a);
    if (lane == 0)
        part[simd] = a;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0)
    {
        float s = 0;
        for (uint q = 0; q < simds; ++q)
            s += part[q];
        g[k] = s;
    }
}

// Layer normalization, backward: per sample, dvars and dmeans; per channel, gamma_grad and
// beta_grad; per value, src_grad += ..., with the formulas of the CPU.
kernel void layer_norm_grad_stats (
    device const float* gi             [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    device const float* means          [[buffer(2)]],
    device const float* invstds        [[buffer(3)]],
    device const float* gamma          [[buffer(4)]],
    device float* dmeans               [[buffer(5)]],
    device float* dvars                [[buffer(6)]],
    constant layer_norm_params& p      [[buffer(7)]],
    uint n                             [[threadgroup_position_in_grid]],
    uint tid                           [[thread_index_in_threadgroup]],
    uint width                         [[threads_per_threadgroup]],
    uint lane                          [[thread_index_in_simdgroup]],
    uint simd                          [[simdgroup_index_in_threadgroup]],
    uint simds                         [[simdgroups_per_threadgroup]])
{
    threadgroup float part_dx[32];
    threadgroup float part_dxc[32];
    threadgroup float part_c[32];
    const float m = means[n];
    const size_t base = (size_t)n*p.per_sample;
    float a = 0, b = 0, c = 0;
    for (uint i = tid; i < p.per_sample; i += width)
    {
        const float dx = gi[base + i]*gamma[i / p.plane];
        const float xc = s[base + i] - m;
        a += dx;
        b += dx*xc;
        c += xc;
    }
    a = simd_sum(a);
    b = simd_sum(b);
    c = simd_sum(c);
    if (lane == 0)
    {
        part_dx[simd] = a;
        part_dxc[simd] = b;
        part_c[simd] = c;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0)
    {
        float sa = 0, sb = 0, sc = 0;
        for (uint q = 0; q < simds; ++q)
        {
            sa += part_dx[q];
            sb += part_dxc[q];
            sc += part_c[q];
        }
        const float r = invstds[n];
        const float invstd_pow = -0.5f*r*r*r;
        const float invnum = 1.0f/p.per_sample;
        const float dv = sb*invstd_pow;
        dvars[n] = dv;
        dmeans[n] = -r*sa + dv*-2.0f*sc*invnum;
    }
}

kernel void layer_norm_grad_params (
    device const float* gi             [[buffer(0)]],
    device const float* s              [[buffer(1)]],
    device const float* means          [[buffer(2)]],
    device const float* invstds        [[buffer(3)]],
    device float* gamma_grad           [[buffer(4)]],
    device float* beta_grad            [[buffer(5)]],
    constant layer_norm_params& p      [[buffer(6)]],
    uint k                             [[threadgroup_position_in_grid]],
    uint tid                           [[thread_index_in_threadgroup]],
    uint width                         [[threads_per_threadgroup]],
    uint lane                          [[thread_index_in_simdgroup]],
    uint simd                          [[simdgroup_index_in_threadgroup]],
    uint simds                         [[simdgroups_per_threadgroup]])
{
    threadgroup float part_g[32];
    threadgroup float part_b[32];
    const uint per_channel = p.samples*p.plane;
    float a = 0, b = 0;
    for (uint j = tid; j < per_channel; j += width)
    {
        const uint n = j / p.plane, i = j % p.plane;
        const size_t idx = (size_t)n*p.per_sample + (size_t)k*p.plane + i;
        const float x_hat = (s[idx] - means[n])*invstds[n];
        a += gi[idx]*x_hat;
        b += gi[idx];
    }
    a = simd_sum(a);
    b = simd_sum(b);
    if (lane == 0)
    {
        part_g[simd] = a;
        part_b[simd] = b;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0)
    {
        float sg = 0, sb = 0;
        for (uint q = 0; q < simds; ++q)
        {
            sg += part_g[q];
            sb += part_b[q];
        }
        gamma_grad[k] = sg;
        beta_grad[k] = sb;
    }
}

kernel void layer_norm_grad_src (
    device float* src_grad             [[buffer(0)]],
    device const float* gi             [[buffer(1)]],
    device const float* s              [[buffer(2)]],
    device const float* means          [[buffer(3)]],
    device const float* invstds        [[buffer(4)]],
    device const float* gamma          [[buffer(5)]],
    device const float* dmeans         [[buffer(6)]],
    device const float* dvars          [[buffer(7)]],
    constant layer_norm_params& p      [[buffer(8)]],
    uint i                             [[thread_position_in_grid]])
{
    if (i >= p.samples*p.per_sample)
        return;
    const uint n = i / p.per_sample;
    const uint k = (i % p.per_sample) / p.plane;
    const float invnum = 1.0f/p.per_sample;
    const float dx = gi[i]*gamma[k];
    src_grad[i] += dx*invstds[n] + dvars[n]*2*(s[i] - means[n])*invnum + dmeans[n]*invnum;
}
// ----------------------------------------------------------------------------------------
// Optimizers.  affine_values covers the forms of tt::affine_transform with scalars:
// d = c0*s1 [+ c1*s2 [+ c2*s3]] [+ k], over [begin, end), each in the order of the CPU.

struct affine_params
{
    uint begin, end;
    uint sources;       // 1, 2 or 3
    uint with_constant;
    float c0, c1, c2, k;
};

kernel void affine_values (
    device float* d                    [[buffer(0)]],
    device const float* s1             [[buffer(1)]],
    device const float* s2             [[buffer(2)]],
    device const float* s3             [[buffer(3)]],
    constant affine_params& p          [[buffer(4)]],
    uint t                             [[thread_position_in_grid]])
{
    const uint i = p.begin + t;
    if (i >= p.end)
        return;
    float v;
    if (p.sources == 1)
        v = p.with_constant ? p.c0*s1[i] + p.k : p.c0*s1[i];
    else if (p.sources == 2)
        v = p.with_constant ? p.c0*s1[i] + p.c1*s2[i] + p.k : p.c0*s1[i] + p.c1*s2[i];
    else
        v = p.with_constant ? p.c0*s1[i] + p.c1*s2[i] + p.c2*s3[i] + p.k : p.c0*s1[i] + p.c1*s2[i] + p.c2*s3[i];
    d[i] = v;
}

struct adam_params
{
    uint begin, end;
    float alpha, weight_decay, momentum1, momentum2;
};

// The update of compute_adam_update on the CPU, value by value.
kernel void adam_update (
    device float* s                    [[buffer(0)]],
    device float* m                    [[buffer(1)]],
    device float* v                    [[buffer(2)]],
    device const float* params         [[buffer(3)]],
    device const float* params_grad    [[buffer(4)]],
    constant adam_params& p            [[buffer(5)]],
    uint t                             [[thread_position_in_grid]])
{
    const uint i = p.begin + t;
    if (i >= p.end)
        return;
    const float eps = 1e-8f;
    const float g = p.weight_decay*params[i] + params_grad[i];
    m[i] = p.momentum1*m[i] + (1 - p.momentum1)*g;
    v[i] = p.momentum2*v[i] + (1 - p.momentum2)*g*g;
    s[i] = -p.alpha*m[i]/(sqrt(v[i]) + eps);
}
)METAL";

            /*
                The compiled library and one pipeline per kernel, made on first use.
                Never destroyed, like the context of metal_dlib.mm.
            */
            struct kernel_cache
            {
                std::mutex lock;
                id<MTLLibrary> library = nil;
                std::map<std::string, id<MTLComputePipelineState>> pipelines;
            };

            kernel_cache& get_cache (
            )
            {
                static kernel_cache* const cache = new kernel_cache;
                return *cache;
            }

            id<MTLComputePipelineState> pipeline (
                const char* name
            )
            {
                id<MTLDevice> device = ready_device();
                kernel_cache& c = get_cache();
                std::lock_guard<std::mutex> guard(c.lock);
                auto i = c.pipelines.find(name);
                if (i != c.pipelines.end())
                    return i->second;

                @autoreleasepool
                {
                    NSError* error = nil;
                    if (c.library == nil)
                    {
                        MTLCompileOptions* options = [MTLCompileOptions new];
                        // The precise versions of exp and of the other math functions,
                        // without the reassociations of fast math.
                        options.mathMode = MTLMathModeSafe;
                        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
                        c.library = [device newLibraryWithSource:[NSString stringWithUTF8String:kernel_source]
                                                         options:options error:&error];
                        if (c.library == nil)
                            throw metal_error(std::string("Metal backend of dlib: the kernels do not compile: ") +
                                (error ? [[error localizedDescription] UTF8String] : "unknown error"));
                    }
                    id<MTLFunction> f = [c.library newFunctionWithName:[NSString stringWithUTF8String:name]];
                    if (f == nil)
                        throw metal_error(std::string("Metal backend of dlib: no kernel is named ") + name + ".");
                    id<MTLComputePipelineState> p = [device newComputePipelineStateWithFunction:f error:&error];
                    if (p == nil)
                        throw metal_error(std::string("Metal backend of dlib: the kernel ") + name +
                            " cannot be prepared: " + (error ? [[error localizedDescription] UTF8String] : "unknown error"));
                    c.pipelines[name] = p;
                    return p;
                }
            }

            // A float range a kernel reads or writes.
            struct arg
            {
                const float* p;
                size_t n;
            };

            arg in (const tensor& t) { return arg{t.host(), t.size()}; }
            arg out (tensor& t) { return arg{t.host(), t.size()}; }

            uint32_t as_index (
                size_t n,
                const char* op
            )
            {
                if (n > UINT32_MAX)
                    throw metal_error(std::string("Metal backend of dlib: tt::") + op +
                        " takes tensors of at most 4294967295 values on Metal, and was given " +
                        std::to_string(n) + ".");
                return static_cast<uint32_t>(n);
            }

            /*
                Runs one kernel over threads threads, its float ranges bound to the buffers
                0, 1, ... in order and its parameters to the next one, and waits for it.
                An empty range is bound to the first nonempty one: the kernel reads none of
                its values.  With group_width > 0, threads counts threadgroups of
                group_width threads instead.
            */
            void run (
                const char* name,
                size_t threads,
                std::initializer_list<arg> ranges,
                const void* params,
                size_t params_size,
                size_t group_width = 0
            )
            {
                if (threads == 0)
                    return;
                id<MTLComputePipelineState> p = pipeline(name);
                id<MTLCommandQueue> queue = ready_queue();

                buffer_view fallback;
                bool have_fallback = false;
                std::vector<buffer_view> views;
                for (const arg& a : ranges)
                {
                    if (a.n == 0)
                    {
                        views.push_back(buffer_view());
                        continue;
                    }
                    views.push_back(find_buffer(a.p, a.n));
                    if (!have_fallback)
                    {
                        fallback = views.back();
                        have_fallback = true;
                    }
                }

                @autoreleasepool
                {
                    id<MTLCommandBuffer> cmd = [queue commandBuffer];
                    if (cmd == nil)
                        throw metal_error("Metal backend of dlib: the queue did not create a command buffer.");
                    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                    if (enc == nil)
                        throw metal_error("Metal backend of dlib: the command buffer did not create a compute encoder.");
                    [enc setComputePipelineState:p];
                    NSUInteger index = 0;
                    for (const buffer_view& v : views)
                    {
                        const buffer_view& b = v.buffer != nil ? v : fallback;
                        [enc setBuffer:b.buffer offset:b.offset atIndex:index++];
                    }
                    [enc setBytes:params length:params_size atIndex:index];
                    if (group_width > 0)
                    {
                        if (group_width > p.maxTotalThreadsPerThreadgroup)
                            throw metal_error(std::string("Metal backend of dlib: the kernel ") + name +
                                " takes at most " + std::to_string((size_t)p.maxTotalThreadsPerThreadgroup) +
                                " threads per group.");
                        [enc dispatchThreadgroups:MTLSizeMake(threads, 1, 1)
                            threadsPerThreadgroup:MTLSizeMake(group_width, 1, 1)];
                    }
                    else
                    {
                        const NSUInteger width = std::min<NSUInteger>(p.maxTotalThreadsPerThreadgroup, 256);
                        [enc dispatchThreads:MTLSizeMake(threads, 1, 1)
                            threadsPerThreadgroup:MTLSizeMake(std::min<NSUInteger>(width, threads), 1, 1)];
                    }
                    [enc endEncoding];
                    [cmd commit];
                    [cmd waitUntilCompleted];
                    if (cmd.status != MTLCommandBufferStatusCompleted)
                        throw metal_error(std::string("Metal backend of dlib: the kernel ") + name +
                            " failed: " + command_problem(cmd));
                }
            }

            struct shape4
            {
                uint32_t n, k, nr, nc;
            };

            shape4 shape_of (const tensor& t, const char* op)
            {
                as_index(t.size(), op);
                return shape4{(uint32_t)t.num_samples(), (uint32_t)t.k(), (uint32_t)t.nr(), (uint32_t)t.nc()};
            }

            bool overlap (const tensor& a, const tensor& b)
            {
                if (a.size() == 0 || b.size() == 0)
                    return false;
                const float* pa = a.host();
                const float* pb = b.host();
                return pa < pb + b.size() && pb < pa + a.size();
            }
        }

    // ------------------------------------------------------------------------------------

        void add (
            tensor& dest,
            const tensor& src1,
            const tensor& src2
        )
        {
            const uint32_t count = as_index(dest.size(), "add");
            if (have_same_dimensions(dest, src1) && have_same_dimensions(dest, src2))
            {
                run("add_same", count, {out(dest), in(src1), in(src2)}, &count, sizeof(count));
                return;
            }
            struct
            {
                shape4 d, a, b;
                uint32_t count;
            } p = {shape_of(dest, "add"), shape_of(src1, "add"), shape_of(src2, "add"), count};
            run("add_padded", count, {out(dest), in(src1), in(src2)}, &p, sizeof(p));
        }

        void add (
            float beta,
            tensor& dest,
            float alpha,
            const tensor& src
        )
        {
            DLIB_CASSERT(
                  (have_same_dimensions(src, dest) ||
                  (src.num_samples()==1 && src.k()==dest.k() && src.nr()==1 && src.nc()==1) ||
                  (src.num_samples()==1 && src.k()==dest.k() && src.nr()==dest.nr() && src.nc()==dest.nc()) ||
                  (src.num_samples()==1 && src.k()==1 && src.nr()==dest.nr() && src.nc()==dest.nc()) ||
                  (src.num_samples()==dest.num_samples() && src.k()==1 && src.nr()==1 && src.nc()==1)) &&
                  is_same_object(src,dest) == false);
            const uint32_t count = as_index(dest.size(), "add");
            // As on the CPU, beta == alpha == 0 sets dest to 0 without reading it.
            if (beta == 0 && alpha == 0)
            {
                fill(dest, 0);
                return;
            }
            if (overlap(dest, src))
                throw metal_error("Metal backend of dlib: tt::add(beta,dest,alpha,src) was given a src "
                    "that overlaps dest, which the Metal version does not support.");
            struct
            {
                shape4 d, s;
                float beta, alpha;
                uint32_t count;
            } p = {shape_of(dest, "add"), shape_of(src, "add"), beta, alpha, count};
            run("add_scaled", count, {out(dest), in(src)}, &p, sizeof(p));
        }

        void fill (
            tensor& dest,
            float value
        )
        {
            const uint32_t count = as_index(dest.size(), "fill");
            struct { float value; uint32_t count; } p = {value, count};
            run("fill_value", count, {out(dest)}, &p, sizeof(p));
        }

        void multiply (
            bool add_to,
            tensor& dest,
            const tensor& src1,
            const tensor& src2
        )
        {
            if (dest.size() == 0)
                return;
            const size_t max_size = std::max(std::max(dest.size(), src1.size()), src2.size());
            struct
            {
                uint32_t add_to;
                uint32_t dsize, asize, bsize, max_size;
            } p = {add_to ? 1u : 0u, as_index(dest.size(), "multiply"), as_index(src1.size(), "multiply"),
                   as_index(src2.size(), "multiply"), as_index(max_size, "multiply")};

            if (dest.size() == src1.size() && src1.size() == src2.size())
            {
                run("multiply_same", dest.size(), {out(dest), in(src1), in(src2)}, &p, sizeof(p));
            }
            else if (dest.num_samples() == 1)
            {
                if (overlap(dest, src1) || overlap(dest, src2))
                    throw metal_error("Metal backend of dlib: tt::multiply was asked to sum over the "
                        "samples into a dest that overlaps a source, which the Metal version does not support.");
                run("multiply_reduce", dest.size(), {out(dest), in(src1), in(src2)}, &p, sizeof(p));
            }
            else
            {
                run("multiply_broadcast", max_size, {out(dest), in(src1), in(src2)}, &p, sizeof(p));
            }
        }

        void softmax (
            tensor& dest,
            const tensor& src,
            operation_mode mode
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest, src));
            if (mode != operation_mode::CHANNEL_WISE && mode != operation_mode::PLANE_WISE)
                throw metal_error("Metal backend of dlib: tt::softmax takes CHANNEL_WISE or PLANE_WISE.");
            as_index(src.size(), "softmax");
            if (src.size() == 0)
                return;
            struct
            {
                uint32_t samples, channels, locations, nr, nc;
            } p = {(uint32_t)src.num_samples(), (uint32_t)src.k(), (uint32_t)(src.nr()*src.nc()),
                   (uint32_t)src.nr(), (uint32_t)src.nc()};
            if (mode == operation_mode::CHANNEL_WISE)
                run("softmax_channels", (size_t)p.samples*p.locations, {out(dest), in(src)}, &p, sizeof(p));
            else
                run("softmax_rows", (size_t)p.samples*p.channels*p.nr, {out(dest), in(src)}, &p, sizeof(p));
        }

        void gelu (
            tensor& dest,
            const tensor& src
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest, src));
            const uint32_t count = as_index(src.size(), "gelu");
            run("gelu", count, {out(dest), in(src)}, &count, sizeof(count));
        }

        void silu (
            tensor& dest,
            const tensor& src
        )
        {
            DLIB_CASSERT(have_same_dimensions(dest, src));
            const uint32_t count = as_index(src.size(), "silu");
            run("silu", count, {out(dest), in(src)}, &count, sizeof(count));
        }

    // ------------------------------------------------------------------------------------
    // Matrix products, by MPSMatrixMultiplication

        namespace
        {
            struct product
            {
                size_t lhs_offset, rhs_offset, dest_offset;    // in values
            };

            MPSMatrix* matrix_view (
                const tensor& t,
                size_t offset,
                long rows,
                long cols
            )
            {
                const buffer_view v = find_buffer(t.host() + offset, (size_t)rows*cols);
                MPSMatrixDescriptor* d = [MPSMatrixDescriptor matrixDescriptorWithRows:rows
                                                                               columns:cols
                                                                              rowBytes:cols*sizeof(float)
                                                                              dataType:MPSDataTypeFloat32];
                return [[MPSMatrix alloc] initWithBuffer:v.buffer offset:v.offset descriptor:d];
            }

            void check_product_shapes (
                long lhs_rows, long lhs_cols, bool trans_lhs,
                long rhs_rows, long rhs_cols, bool trans_rhs,
                long dest_rows, long dest_cols
            )
            {
                const long m = trans_lhs ? lhs_cols : lhs_rows;
                const long k = trans_lhs ? lhs_rows : lhs_cols;
                const long k2 = trans_rhs ? rhs_cols : rhs_rows;
                const long n = trans_rhs ? rhs_rows : rhs_cols;
                if (k != k2 || dest_rows != m || dest_cols != n)
                    throw metal_error("Metal backend of dlib: tt::gemm was given matrices of " +
                        std::to_string(lhs_rows) + "x" + std::to_string(lhs_cols) + (trans_lhs ? " (transposed)" : "") +
                        " and " + std::to_string(rhs_rows) + "x" + std::to_string(rhs_cols) + (trans_rhs ? " (transposed)" : "") +
                        " for a result of " + std::to_string(dest_rows) + "x" + std::to_string(dest_cols) + ".");
                if (k == 0 && m*n != 0)
                    throw metal_error("Metal backend of dlib: tt::gemm with an inner dimension of 0 has no Metal version.");
            }
        }

        void gemm (
            float beta,
            tensor& dest,
            float alpha,
            const tensor& lhs,
            bool trans_lhs,
            const tensor& rhs,
            bool trans_rhs,
            operation_mode mode
        )
        {
            if (mode != operation_mode::CHANNEL_WISE && mode != operation_mode::PLANE_WISE)
                throw metal_error("Metal backend of dlib: tt::gemm takes CHANNEL_WISE or PLANE_WISE.");
            if (overlap(dest, lhs) || overlap(dest, rhs))
                throw metal_error("Metal backend of dlib: tt::gemm was given a dest that overlaps lhs or rhs, "
                    "which the Metal version does not support.");

            long lhs_rows, lhs_cols, rhs_rows, rhs_cols, dest_rows, dest_cols;
            std::vector<product> products;
            if (mode == operation_mode::CHANNEL_WISE)
            {
                // mat(t): one row per sample, k*nr*nc columns.
                auto cols_of = [](const tensor& t) { return t.num_samples() == 0 ? 0L : (long)(t.size()/t.num_samples()); };
                lhs_rows = lhs.num_samples();  lhs_cols = cols_of(lhs);
                rhs_rows = rhs.num_samples();  rhs_cols = cols_of(rhs);
                dest_rows = dest.num_samples(); dest_cols = cols_of(dest);
                products.push_back(product{0, 0, 0});
            }
            else
            {
                const tt::gemm_planes p = tt::plan_gemm_planes(dest, lhs, rhs);
                lhs_rows = p.lhs_rows;   lhs_cols = p.lhs_cols;
                rhs_rows = p.rhs_rows;   rhs_cols = p.rhs_cols;
                dest_rows = p.dest_rows; dest_cols = p.dest_cols;
                for (long b = 0; b < p.num_samples; ++b)
                    for (long c = 0; c < p.num_channels; ++c)
                        products.push_back(product{p.lhs_offset(b, c), p.rhs_offset(b, c), p.dest_offset(b, c)});
            }
            check_product_shapes(lhs_rows, lhs_cols, trans_lhs, rhs_rows, rhs_cols, trans_rhs, dest_rows, dest_cols);
            for (const tensor* t : {&lhs, &rhs, (const tensor*)&dest})
                as_index(t->size(), "gemm");
            if (products.empty() || dest_rows*dest_cols == 0)
                return;
            const long inner = trans_lhs ? lhs_rows : lhs_cols;

            id<MTLDevice> device = ready_device();
            id<MTLCommandQueue> queue = ready_queue();
            @autoreleasepool
            {
                id<MTLCommandBuffer> cmd = [queue commandBuffer];
                if (cmd == nil)
                    throw metal_error("Metal backend of dlib: the queue did not create a command buffer.");
                MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc] initWithDevice:device
                                                                                 transposeLeft:trans_lhs
                                                                                transposeRight:trans_rhs
                                                                                    resultRows:dest_rows
                                                                                 resultColumns:dest_cols
                                                                               interiorColumns:inner
                                                                                         alpha:alpha
                                                                                          beta:beta];
                if (mm == nil)
                    throw metal_error("Metal backend of dlib: MPSMatrixMultiplication could not be created.");
                for (const product& pr : products)
                {
                    [mm encodeToCommandBuffer:cmd
                                   leftMatrix:matrix_view(lhs, pr.lhs_offset, lhs_rows, lhs_cols)
                                  rightMatrix:matrix_view(rhs, pr.rhs_offset, rhs_rows, rhs_cols)
                                 resultMatrix:matrix_view(dest, pr.dest_offset, dest_rows, dest_cols)];
                }
                [cmd commit];
                [cmd waitUntilCompleted];
                if (cmd.status != MTLCommandBufferStatusCompleted)
                    throw metal_error("Metal backend of dlib: tt::gemm failed: " + command_problem(cmd));
            }
        }

    // ------------------------------------------------------------------------------------

        namespace
        {
            // An output a Metal operation sizes itself: the route let it through because it
            // was empty, so it moves to Metal without a copy.
            void adopt (resizable_tensor& t)
            {
                if (t.get_tensor_device() == tensor_device::metal)
                    return;
                if (t.size() != 0)
                    throw metal_error("Metal backend of dlib: an output on the CPU was given to a Metal operation.");
                t.set_tensor_device(tensor_device::metal);
            }
        }

        void layer_normalize (
            const double eps,
            resizable_tensor& dest,
            resizable_tensor& means,
            resizable_tensor& invstds,
            const tensor& src,
            const tensor& gamma,
            const tensor& beta
        )
        {
            DLIB_CASSERT(
                have_same_dimensions(gamma, beta) &&
                gamma.k() == src.k() &&
                gamma.nr() == 1 &&
                gamma.nc() == 1 &&
                eps > 0);
            if (gamma.num_samples() != 1)
                throw metal_error("Metal backend of dlib: tt::layer_normalize takes a gamma of one sample.");
            if (&dest == &src)
                throw metal_error("Metal backend of dlib: tt::layer_normalize cannot write over its src.");
            adopt(dest);
            adopt(means);
            adopt(invstds);
            dest.copy_size(src);
            means.set_size(src.num_samples());
            invstds.set_size(src.num_samples());
            if (src.size() == 0)
                return;

            struct
            {
                uint32_t samples, per_sample, plane;
                float eps;
            } p = {(uint32_t)src.num_samples(), as_index(src.size()/src.num_samples(), "layer_normalize"),
                   (uint32_t)(src.nr()*src.nc()), (float)eps};
            as_index(src.size(), "layer_normalize");
            const size_t width = std::min<size_t>(256, (p.per_sample + 31)/32*32);
            run("layer_norm_stats", p.samples, {in(src), out(means), out(invstds)}, &p, sizeof(p), width);
            run("layer_norm_apply", src.size(), {out(dest), in(src), in(means), in(invstds), in(gamma), in(beta)},
                &p, sizeof(p));
        }

        void gguf_matvec (
            tensor& dest,
            const tensor& src,
            const tensor& weights,
            int type,
            long rows,
            long cols,
            size_t row_bytes
        )
        {
            if (type != gguf_blocks::q4_0 && type != gguf_blocks::q8_0)
                throw metal_error("Metal backend of dlib: tt::gguf_matvec has no Metal version for the GGUF "
                    "format " + std::to_string(type) + "; Q4_0 (2) and Q8_0 (8) have one.");
            if (rows <= 0 || cols <= 0 || cols % 32 != 0)
                throw metal_error("Metal backend of dlib: tt::gguf_matvec takes rows > 0 and a number of "
                    "columns that fills whole blocks of 32, and was given " + std::to_string(rows) + "x" +
                    std::to_string(cols) + ".");
            if (row_bytes != gguf_blocks::row_bytes(type, cols))
                throw metal_error("Metal backend of dlib: tt::gguf_matvec was given rows of " +
                    std::to_string(row_bytes) + " bytes, where the format needs " +
                    std::to_string(gguf_blocks::row_bytes(type, cols)) + ".");
            if (&dest == &src || &dest == &weights || overlap(dest, src) || overlap(dest, weights))
                throw metal_error("Metal backend of dlib: tt::gguf_matvec was given a dest that overlaps "
                    "src or weights.");
            if (src.size() % (size_t)cols != 0)
                throw metal_error("Metal backend of dlib: tt::gguf_matvec takes a src of whole rows of " +
                    std::to_string(cols) + " values.");
            const size_t n = src.size()/(size_t)cols;
            if (dest.size() != n*(size_t)rows)
                throw metal_error("Metal backend of dlib: tt::gguf_matvec takes a dest of " +
                    std::to_string(n*(size_t)rows) + " values, and was given " + std::to_string(dest.size()) + ".");
            if (weights.size()*sizeof(float) < (size_t)rows*row_bytes)
                throw metal_error("Metal backend of dlib: tt::gguf_matvec was given " +
                    std::to_string(weights.size()*sizeof(float)) + " bytes of weights for a matrix of " +
                    std::to_string((size_t)rows*row_bytes) + ".");
            if (n == 0)
                return;
            struct
            {
                uint32_t rows, cols, n, row_bytes;
            } p = {as_index(rows, "gguf_matvec"), as_index(cols, "gguf_matvec"), as_index(n, "gguf_matvec"),
                   as_index(row_bytes, "gguf_matvec")};
            as_index(dest.size(), "gguf_matvec");
            as_index(src.size(), "gguf_matvec");
            run(type == gguf_blocks::q4_0 ? "gguf_matvec_q4_0" : "gguf_matvec_q8_0", rows,
                {out(dest), in(src), in(weights)}, &p, sizeof(p), 32);
        }

    // ------------------------------------------------------------------------------------
    // Backward passes

        void softmax_gradient (
            tensor& grad,
            const tensor& dest,
            const tensor& gradient_input,
            operation_mode mode
        )
        {
            DLIB_CASSERT(have_same_dimensions(grad, dest) && have_same_dimensions(grad, gradient_input));
            if (mode != operation_mode::CHANNEL_WISE && mode != operation_mode::PLANE_WISE)
                throw metal_error("Metal backend of dlib: tt::softmax_gradient takes CHANNEL_WISE or PLANE_WISE.");
            as_index(grad.size(), "softmax_gradient");
            if (grad.size() == 0)
                return;
            struct
            {
                uint32_t samples, channels, locations, nr, nc;
                uint32_t assign;
            } p = {(uint32_t)grad.num_samples(), (uint32_t)grad.k(), (uint32_t)(grad.nr()*grad.nc()),
                   (uint32_t)grad.nr(), (uint32_t)grad.nc(), is_same_object(grad, gradient_input) ? 1u : 0u};
            if (mode == operation_mode::CHANNEL_WISE)
                run("softmax_gradient_channels", (size_t)p.samples*p.locations, {out(grad), in(dest), in(gradient_input)}, &p, sizeof(p));
            else
                run("softmax_gradient_rows", (size_t)p.samples*p.channels*p.nr, {out(grad), in(dest), in(gradient_input)}, &p, sizeof(p));
        }

        namespace
        {
            void activation_gradient (
                const char* kernel,
                const char* op,
                tensor& grad,
                const tensor& src,
                const tensor& gradient_input
            )
            {
                DLIB_CASSERT(have_same_dimensions(src, gradient_input) && have_same_dimensions(src, grad));
                struct
                {
                    uint32_t count, assign;
                } p = {as_index(src.size(), op), is_same_object(grad, gradient_input) ? 1u : 0u};
                run(kernel, p.count, {out(grad), in(src), in(gradient_input)}, &p, sizeof(p));
            }
        }

        void gelu_gradient (
            tensor& grad,
            const tensor& src,
            const tensor& gradient_input
        )
        {
            activation_gradient("gelu_gradient", "gelu_gradient", grad, src, gradient_input);
        }

        void silu_gradient (
            tensor& grad,
            const tensor& src,
            const tensor& gradient_input
        )
        {
            activation_gradient("silu_gradient", "silu_gradient", grad, src, gradient_input);
        }

        void assign_bias_gradient (
            tensor& grad,
            const tensor& gradient_input
        )
        {
            DLIB_CASSERT(
                  grad.num_samples() == 1 &&
                  gradient_input.k() == grad.k() &&
                  gradient_input.nr() == grad.nr() &&
                  gradient_input.nc() == grad.nc() &&
                  gradient_input.size() > 0);
            if (overlap(grad, gradient_input))
                throw metal_error("Metal backend of dlib: tt::assign_bias_gradient was given a grad that "
                    "overlaps gradient_input.");
            struct
            {
                uint32_t size, samples;
            } p = {as_index(grad.size(), "assign_bias_gradient"), (uint32_t)gradient_input.num_samples()};
            as_index(gradient_input.size(), "assign_bias_gradient");
            run("bias_gradient", p.size, {out(grad), in(gradient_input)}, &p, sizeof(p));
        }

        void assign_conv_bias_gradient (
            tensor& grad,
            const tensor& gradient_input
        )
        {
            DLIB_CASSERT(
                  grad.num_samples() == 1 &&
                  grad.k()  >= 1 &&
                  grad.nr() == 1 &&
                  grad.nc() == 1 &&
                  gradient_input.k() == grad.k() &&
                  gradient_input.size() > 0 &&
                  is_same_object(grad,gradient_input) == false);
            if (overlap(grad, gradient_input))
                throw metal_error("Metal backend of dlib: tt::assign_conv_bias_gradient was given a grad "
                    "that overlaps gradient_input.");
            struct
            {
                uint32_t samples, channels, plane;
            } p = {(uint32_t)gradient_input.num_samples(), (uint32_t)gradient_input.k(),
                   (uint32_t)(gradient_input.nr()*gradient_input.nc())};
            as_index(gradient_input.size(), "assign_conv_bias_gradient");
            const size_t per_channel = (size_t)p.samples*p.plane;
            const size_t width = std::min<size_t>(256, (per_channel + 31)/32*32);
            run("conv_bias_gradient", p.channels, {out(grad), in(gradient_input)}, &p, sizeof(p), width);
        }

        void layer_normalize_gradient (
            const double eps,
            const tensor& gradient_input,
            const tensor& means,
            const tensor& invstds,
            const tensor& src,
            const tensor& gamma,
            tensor& src_grad,
            tensor& gamma_grad,
            tensor& beta_grad,
            resizable_tensor& dmeans,
            resizable_tensor& dvars
        )
        {
            DLIB_CASSERT(src.num_samples() == (long long)means.size());
            DLIB_CASSERT(src.num_samples() == (long long)invstds.size());
            DLIB_CASSERT(have_same_dimensions(gamma, gamma_grad));
            DLIB_CASSERT(have_same_dimensions(gamma_grad, beta_grad));
            DLIB_CASSERT(gamma.k() == src.k());
            DLIB_CASSERT(gamma.nr() == 1);
            DLIB_CASSERT(gamma.nc() == 1);
            DLIB_CASSERT(have_same_dimensions(gradient_input, src));
            DLIB_CASSERT(have_same_dimensions(gradient_input, src_grad));
            DLIB_CASSERT(eps > 0);
            if (gamma.num_samples() != 1)
                throw metal_error("Metal backend of dlib: tt::layer_normalize_gradient takes a gamma of one sample.");
            adopt(dmeans);
            adopt(dvars);
            dvars.copy_size(invstds);
            dmeans.copy_size(means);
            if (src.size() == 0)
                return;
            struct
            {
                uint32_t samples, per_sample, plane;
                float eps;
            } p = {(uint32_t)src.num_samples(), as_index(src.size()/src.num_samples(), "layer_normalize_gradient"),
                   (uint32_t)(src.nr()*src.nc()), (float)eps};
            as_index(src.size(), "layer_normalize_gradient");
            const size_t width = std::min<size_t>(256, (p.per_sample + 31)/32*32);
            run("layer_norm_grad_stats", p.samples,
                {in(gradient_input), in(src), in(means), in(invstds), in(gamma), out(dmeans), out(dvars)},
                &p, sizeof(p), width);
            const size_t per_channel = (size_t)p.samples*p.plane;
            run("layer_norm_grad_params", src.k(),
                {in(gradient_input), in(src), in(means), in(invstds), out(gamma_grad), out(beta_grad)},
                &p, sizeof(p), std::min<size_t>(256, (per_channel + 31)/32*32));
            run("layer_norm_grad_src", src.size(),
                {out(src_grad), in(gradient_input), in(src), in(means), in(invstds), in(gamma), in(dmeans), in(dvars)},
                &p, sizeof(p));
        }

    // ------------------------------------------------------------------------------------
    // Optimizers

        void affine_values (
            size_t begin,
            size_t end,
            tensor& dest,
            std::initializer_list<const tensor*> sources,
            const float* coefficients,
            bool with_constant,
            float constant
        )
        {
            for (const tensor* s : sources)
                if (s->size() != dest.size())
                    throw metal_error("Metal backend of dlib: tt::affine_transform takes tensors of one size.");
            if (begin > end || end > dest.size())
                throw metal_error("Metal backend of dlib: tt::affine_transform_range was given a range outside dest.");
            as_index(dest.size(), "affine_transform");
            if (begin == end)
                return;
            struct
            {
                uint32_t begin, end, sources, with_constant;
                float c0, c1, c2, k;
            } p = {(uint32_t)begin, (uint32_t)end, (uint32_t)sources.size(), with_constant ? 1u : 0u,
                   coefficients[0], sources.size() > 1 ? coefficients[1] : 0.0f,
                   sources.size() > 2 ? coefficients[2] : 0.0f, constant};
            const tensor* s[3];
            size_t n = 0;
            for (const tensor* t : sources)
                s[n++] = t;
            for (; n < 3; ++n)
                s[n] = s[0];
            run("affine_values", end - begin, {out(dest), in(*s[0]), in(*s[1]), in(*s[2])}, &p, sizeof(p));
        }

        void compute_adam_update (
            size_t begin,
            size_t end,
            tensor& s,
            tensor& m,
            tensor& v,
            const float t,
            const float learning_rate,
            const float weight_decay,
            const float momentum1,
            const float momentum2,
            const tensor& params,
            const tensor& params_grad
        )
        {
            DLIB_CASSERT(s.size() == m.size() &&
                         s.size() == v.size() &&
                         s.size() == params.size() &&
                         s.size() == params_grad.size());
            DLIB_CASSERT(begin <= end && end <= params.size());
            as_index(params.size(), "compute_adam_update");
            if (begin == end)
                return;
            // The step size of the CPU, computed the same way.
            const float alpha = learning_rate*std::sqrt(1-std::pow(momentum2,t))/(1-std::pow(momentum1, t));
            struct
            {
                uint32_t begin, end;
                float alpha, weight_decay, momentum1, momentum2;
            } p = {(uint32_t)begin, (uint32_t)end, alpha, weight_decay, momentum1, momentum2};
            run("adam_update", end - begin, {out(s), out(m), out(v), in(params), in(params_grad)}, &p, sizeof(p));
        }

        void affine_transform (tensor& dest, const tensor& src, const float A, const float B)
        {
            const float c[] = {A};
            affine_values(0, dest.size(), dest, {&src}, c, true, B);
        }

        void affine_transform (tensor& dest, const tensor& src1, const tensor& src2, const float A,
                               const float B, const float C)
        {
            const float c[] = {A, B};
            affine_values(0, dest.size(), dest, {&src1, &src2}, c, true, C);
        }

        void affine_transform (tensor& dest, const tensor& src1, const tensor& src2, const tensor& src3,
                               const float A, const float B, const float C, const float D)
        {
            const float c[] = {A, B, C};
            affine_values(0, dest.size(), dest, {&src1, &src2, &src3}, c, true, D);
        }

        void affine_transform_range (size_t begin, size_t end, tensor& dest, const tensor& src1,
                                     const tensor& src2, const tensor& src3, const float A,
                                     const float B, const float C)
        {
            const float c[] = {A, B, C};
            affine_values(begin, end, dest, {&src1, &src2, &src3}, c, false, 0);
        }
    }
}
