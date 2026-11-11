// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// The GPU of the Mac through Metal (macOS port).  Compiled with automatic reference counting.

#include "metal_device.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace albireo
{

    namespace
    {
        // The source of the kernels, embedded by CMakeLists.txt (metal_kernels.inc.in).
        const char* const metal_source =
#include "metal_kernels.inc"
            ;

        // The kernels, in the order of the enumeration below; each must compile into a pipeline.
        // The attention comes in one variant for each grouping of the query heads (1, 2, 4, 8) and each
        // width of a head (64, 128, 256 values: 2, 4, 8 a lane), whose loops the compiler unrolls.
        const char* const kernel_names[] = { "mv_q4_K_1", "mv_q6_K_1", "mv_q4_K_n", "mv_q6_K_n", "rms_norm", "add_bias", "silu_mul", "rope",
            "kv_store", "prepare_queries", "merge_attention", "dequant_q4_K", "dequant_q6_K",
            "attention_part_1_2", "attention_part_1_4", "attention_part_1_8",
            "attention_part_2_2", "attention_part_2_4", "attention_part_2_8",
            "attention_part_4_2", "attention_part_4_4", "attention_part_4_8",
            "attention_part_8_2", "attention_part_8_4", "attention_part_8_8" };
        enum kernel_index
        {
            k_mv_q4_K, k_mv_q6_K, k_mv_q4_K_n, k_mv_q6_K_n, k_rms_norm, k_add_bias, k_silu_mul, k_rope, k_kv_store, k_prepare_queries,
            k_merge_attention, k_dequant_q4_K, k_dequant_q6_K, k_attention_part, k_count = k_attention_part + 12
        };

        // The variant of the attention for rep query heads per key/value head and heads of hd values.
        int attention_kernel(unsigned rep, unsigned hd)
        {
            const int r = rep == 1 ? 0 : rep == 2 ? 1 : rep == 4 ? 2 : rep == 8 ? 3 : -1;
            const int p = hd == 64 ? 0 : hd == 128 ? 1 : hd == 256 ? 2 : -1;
            return r < 0 || p < 0 ? -1 : k_attention_part + 3 * r + p;
        }

        // The arguments of the kernels, laid out as their structures in metal_kernels.metal.
        struct mv_args
        {
            int64_t rows;
            int64_t cols;
            uint32_t tokens;
            uint32_t x_stride;
            uint32_t y_stride;
            float alpha;
            float beta;
            uint32_t y2_stride;
            int64_t rows2;
        };
        struct norm_args
        {
            uint32_t width;
            uint32_t x_stride;
            uint32_t y_stride;
            float eps;
        };
        struct dequant_args
        {
            int64_t cols;
            uint32_t out_stride;
        };
        struct bias_args
        {
            uint32_t n;
            uint32_t stride;
        };
        struct rope_args
        {
            uint32_t heads;
            uint32_t hd;
            uint32_t half_dims;
            uint32_t neox;
            uint32_t stride;
            uint32_t cs_stride;
        };
        struct kv_args
        {
            uint32_t hd;
            uint32_t q8;
            uint32_t positions;
            uint32_t kv_stride;
            uint64_t block_stride;
            uint64_t k_slab;
            uint64_t v_slab;
            uint64_t head_stride;
        };
        struct query_args
        {
            uint32_t hd;
            uint32_t q8;
            uint32_t q_stride;
            float scale;
        };
        struct attention_args
        {
            uint32_t hd;
            uint32_t rep;
            uint32_t kv_heads;
            uint32_t q8;
            uint32_t positions;
            uint32_t part_len;
            uint32_t parts;
            uint32_t q_stride;
            uint32_t table_stride;
            uint32_t pad;          // the alignment of the 64-bit fields that follow, as in Metal
            uint64_t block_stride;
            uint64_t k_slab;
            uint64_t v_slab;
            uint64_t head_stride;
        };

        // The rows a threadgroup of the products computes: ROWS * SG_PER_TG of metal_kernels.metal.
        const long mv_rows_per_group = 16;
        const unsigned mv_threads = 128;
        const unsigned attention_threads = 128;   // ATT_SG simdgroups
    }

    struct metal_buffer::impl
    {
        id<MTLBuffer> buffer = nil;
    };

    struct metal_device::impl
    {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLLibrary> library = nil;
        std::vector<id<MTLComputePipelineState>> pipelines;
        std::string name;
    };

    struct metal_device::commands::impl
    {
        metal_device::impl* device = nullptr;
        id<MTLCommandBuffer> buffer = nil;
        id<MTLComputeCommandEncoder> encoder = nil;

        // The kernels go to a compute encoder, which a product of Metal Performance Shaders closes.
        void use(int k)
        {
            if (!encoder) encoder = [buffer computeCommandEncoder];
            [encoder setComputePipelineState:device->pipelines[static_cast<size_t>(k)]];
        }
        void close()
        {
            if (encoder) [encoder endEncoding];
            encoder = nil;
        }
        void bind(metal_view v, unsigned index)
        {
            [encoder setBuffer:v.buffer->impl_->buffer offset:v.offset atIndex:index];
        }
        template <typename T>
        void args(const T& a, unsigned index) { [encoder setBytes:&a length:sizeof(T) atIndex:index]; }
        void groups(unsigned x, unsigned y, unsigned z, unsigned threads)
        {
            [encoder dispatchThreadgroups:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
        }
        void grid(unsigned x, unsigned y, unsigned z)
        {
            const NSUInteger w = std::min<NSUInteger>(x, 256);
            [encoder dispatchThreads:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(w, 1, 1)];
        }
    };

    metal_device::metal_device() : impl_(new impl) {}

    metal_device::~metal_device() = default;

    std::unique_ptr<metal_device> metal_device::open(std::string& why)
    {
        @autoreleasepool
        {
            std::unique_ptr<metal_device> d(new metal_device);
            impl& m = *d->impl_;
            m.device = MTLCreateSystemDefaultDevice();
            if (!m.device)
            {
                why = "no GPU that Metal reaches";
                return nullptr;
            }
            m.name = m.device.name.UTF8String;
            m.queue = [m.device newCommandQueue];
            NSError* error = nil;
            MTLCompileOptions* options = [MTLCompileOptions new];
            m.library = [m.device newLibraryWithSource:[NSString stringWithUTF8String:metal_source]
                options:options error:&error];
            if (!m.library)
            {
                why = std::string("the Metal kernels do not compile: ")
                    + (error ? error.localizedDescription.UTF8String : "unknown error");
                return nullptr;
            }
            static_assert(sizeof(kernel_names) / sizeof(kernel_names[0]) == k_count, "a kernel without its name");
            for (const char* k : kernel_names)
            {
                id<MTLFunction> f = [m.library newFunctionWithName:[NSString stringWithUTF8String:k]];
                id<MTLComputePipelineState> p = f ? [m.device newComputePipelineStateWithFunction:f error:&error] : nil;
                if (!p)
                {
                    why = std::string("the Metal kernel ") + k + " cannot be prepared"
                        + (error ? std::string(": ") + error.localizedDescription.UTF8String : std::string());
                    return nullptr;
                }
                m.pipelines.push_back(p);
            }
            return d;
        }
    }

    const std::string& metal_device::name() const { return impl_->name; }

    uint64_t metal_device::max_buffer_bytes() const { return impl_->device.maxBufferLength; }

    uint64_t metal_device::working_set_bytes() const { return impl_->device.recommendedMaxWorkingSetSize; }

    int metal_device::kernel_count() const { return static_cast<int>(impl_->pipelines.size()); }

    metal_buffer metal_device::allocate(size_t bytes)
    {
        metal_buffer b;
        id<MTLBuffer> buffer = [impl_->device newBufferWithLength:std::max<size_t>(bytes, 16)
            options:MTLResourceStorageModeShared];
        if (!buffer) return b;
        std::memset(buffer.contents, 0, buffer.length);
        b.impl_ = std::make_shared<metal_buffer::impl>();
        b.impl_->buffer = buffer;
        b.contents_ = buffer.contents;
        b.bytes_ = bytes;
        return b;
    }

    metal_buffer metal_device::wrap(const void* data, size_t bytes, size_t& offset)
    {
        metal_buffer b;
        const uintptr_t page = static_cast<uintptr_t>(getpagesize());
        const uintptr_t p = reinterpret_cast<uintptr_t>(data);
        const uintptr_t first = p / page * page;
        const uintptr_t end = (p + bytes + page - 1) / page * page;
        if (end - first > impl_->device.maxBufferLength) return b;
        id<MTLBuffer> buffer = [impl_->device newBufferWithBytesNoCopy:reinterpret_cast<void*>(first)
            length:end - first options:MTLResourceStorageModeShared deallocator:nil];
        if (!buffer) return b;
        offset = p - first;
        b.impl_ = std::make_shared<metal_buffer::impl>();
        b.impl_->buffer = buffer;
        b.contents_ = reinterpret_cast<void*>(first);
        b.bytes_ = end - first;
        return b;
    }

    std::unique_ptr<metal_device::commands> metal_device::begin()
    {
        return std::unique_ptr<commands>(new commands(*this));
    }

    metal_device::commands::commands(metal_device& device) : impl_(new impl)
    {
        impl_->device = device.impl_.get();
        impl_->buffer = [impl_->device->queue commandBuffer];
        impl_->encoder = [impl_->buffer computeCommandEncoder];
    }

    metal_device::commands::~commands()
    {
        // Commands never finished are given up: their encoder must end before it goes.
        impl_->close();
    }

    bool metal_device::commands::dequantize(dlib::ggml_type type, metal_view W, long rows, long cols, metal_view out,
        unsigned out_stride)
    {
        int k;
        if (type == dlib::ggml_type::Q4_K) k = k_dequant_q4_K;
        else if (type == dlib::ggml_type::Q6_K) k = k_dequant_q6_K;
        else return false;
        if (rows == 0) return true;
        if (cols % 256 != 0) throw std::invalid_argument("metal_device: a matrix of other columns than its kernel takes");
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(out, 1);
        const dequant_args a = { cols, out_stride };
        impl_->args(a, 2);
        impl_->grid(static_cast<unsigned>(cols / 256 * 32), static_cast<unsigned>(rows), 1);
        return true;
    }

    void metal_device::commands::gemm(metal_view X, unsigned rows, unsigned inner, unsigned x_stride, metal_view Wf,
        unsigned cols, unsigned w_stride, metal_view Y, unsigned y_stride, float alpha, float beta)
    {
        if (rows == 0 || cols == 0) return;
        impl_->close();
        id<MTLDevice> device = impl_->device->device;
        auto matrix = [](metal_view v, unsigned r, unsigned c, unsigned stride) {
            MPSMatrixDescriptor* d = [MPSMatrixDescriptor matrixDescriptorWithRows:r columns:c
                rowBytes:stride * sizeof(float) dataType:MPSDataTypeFloat32];
            return [[MPSMatrix alloc] initWithBuffer:v.buffer->impl_->buffer offset:v.offset descriptor:d];
        };
        MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc] initWithDevice:device transposeLeft:NO
            transposeRight:YES resultRows:rows resultColumns:cols interiorColumns:inner alpha:alpha beta:beta];
        [mm encodeToCommandBuffer:impl_->buffer leftMatrix:matrix(X, rows, inner, x_stride)
            rightMatrix:matrix(Wf, cols, inner, w_stride) resultMatrix:matrix(Y, rows, cols, y_stride)];
    }

    bool metal_device::commands::matvec(dlib::ggml_type type, metal_view W, long rows, long cols, metal_view x,
        unsigned tokens, unsigned x_stride, metal_view y, unsigned y_stride, float alpha, float beta)
    {
        kernel_index k;
        if (type == dlib::ggml_type::Q4_K) k = tokens > 1 ? k_mv_q4_K_n : k_mv_q4_K;
        else if (type == dlib::ggml_type::Q6_K) k = tokens > 1 ? k_mv_q6_K_n : k_mv_q6_K;
        else return false;
        if (tokens == 0) return true;
        if (tokens > max_tokens || cols % 256 != 0)
            throw std::invalid_argument("metal_device: a product of more tokens or other columns than its kernel takes");
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        impl_->bind(W, 4);
        impl_->bind(y, 5);
        const mv_args a = { rows, cols, tokens, x_stride, y_stride, alpha, beta, y_stride, 0 };
        impl_->args(a, 3);
        impl_->groups(static_cast<unsigned>((rows + mv_rows_per_group - 1) / mv_rows_per_group), 1, 1, mv_threads);
        return true;
    }

    bool metal_device::commands::matvec2(dlib::ggml_type type, metal_view W, long rows, metal_view W2, long rows2,
        long cols, metal_view x, unsigned tokens, unsigned x_stride, metal_view y, unsigned y_stride, metal_view y2,
        unsigned y2_stride)
    {
        kernel_index k;
        if (type == dlib::ggml_type::Q4_K) k = tokens > 1 ? k_mv_q4_K_n : k_mv_q4_K;
        else if (type == dlib::ggml_type::Q6_K) k = tokens > 1 ? k_mv_q6_K_n : k_mv_q6_K;
        else return false;
        if (tokens == 0) return true;
        // The rows of a simdgroup (ROWS) never straddle the two matrices.
        if (tokens > max_tokens || cols % 256 != 0 || rows % 16 != 0)
            throw std::invalid_argument("metal_device: a double product of more tokens or other shapes than its kernel takes");
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        impl_->bind(W2, 4);
        impl_->bind(y2, 5);
        const mv_args a = { rows, cols, tokens, x_stride, y_stride, 1.0f, 0.0f, y2_stride, rows2 };
        impl_->args(a, 3);
        impl_->groups(static_cast<unsigned>((rows + rows2 + mv_rows_per_group - 1) / mv_rows_per_group), 1, 1, mv_threads);
        return true;
    }

    void metal_device::commands::rms_norm(metal_view x, metal_view gamma, metal_view y, unsigned rows, unsigned width,
        unsigned x_stride, unsigned y_stride, float eps)
    {
        if (rows == 0) return;
        impl_->use(k_rms_norm);
        impl_->bind(x, 0);
        impl_->bind(gamma, 1);
        impl_->bind(y, 2);
        const norm_args a = { width, x_stride, y_stride, eps };
        impl_->args(a, 3);
        const unsigned threads = std::min(256u, (width + 31) / 32 * 32);
        impl_->groups(rows, 1, 1, threads);
    }

    void metal_device::commands::add_bias(metal_view x, metal_view b, unsigned n, unsigned stride, unsigned rows)
    {
        if (rows == 0 || n == 0) return;
        impl_->use(k_add_bias);
        impl_->bind(x, 0);
        impl_->bind(b, 1);
        const bias_args a = { n, stride };
        impl_->args(a, 2);
        impl_->grid(n, rows, 1);
    }

    void metal_device::commands::silu_mul(metal_view gate, metal_view up, unsigned n)
    {
        if (n == 0) return;
        impl_->use(k_silu_mul);
        impl_->bind(gate, 0);
        impl_->bind(up, 1);
        impl_->args(n, 2);
        impl_->grid(n, 1, 1);
    }

    void metal_device::commands::rope(metal_view x, metal_view cs, unsigned tokens, unsigned heads, unsigned hd,
        unsigned half, bool neox, unsigned stride, unsigned cs_stride)
    {
        if (tokens == 0 || half == 0) return;
        impl_->use(k_rope);
        impl_->bind(x, 0);
        impl_->bind(cs, 1);
        const rope_args a = { heads, hd, half, neox ? 1u : 0u, stride, cs_stride };
        impl_->args(a, 2);
        impl_->grid(half, heads, tokens);
    }

    void metal_device::commands::kv_store(metal_view k, metal_view v, unsigned kv_stride, metal_view region,
        metal_view blocks, metal_view rows, unsigned tokens, const kv_layout& l)
    {
        if (tokens == 0) return;
        if (l.hd > 256 || l.hd % 32 != 0) throw std::invalid_argument("metal_device: heads of other widths than its kernels take");
        impl_->use(k_kv_store);
        impl_->bind(k, 0);
        impl_->bind(v, 1);
        impl_->bind(region, 2);
        impl_->bind(blocks, 3);
        impl_->bind(rows, 4);
        const kv_args a = { l.hd, l.q8 ? 1u : 0u, l.positions, kv_stride, l.block_stride, l.k_slab, l.v_slab, l.head_stride };
        impl_->args(a, 5);
        impl_->groups(l.heads, tokens, 1, l.hd);
    }

    void metal_device::commands::prepare_queries(metal_view q, unsigned tokens, unsigned heads, unsigned hd,
        unsigned q_stride, float scale, bool q8)
    {
        if (tokens == 0) return;
        impl_->use(k_prepare_queries);
        impl_->bind(q, 0);
        const query_args a = { hd, q8 ? 1u : 0u, q_stride, scale };
        impl_->args(a, 1);
        impl_->groups(heads, tokens, 1, hd);
    }

    void metal_device::commands::attention(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
        unsigned table_stride, unsigned tokens, unsigned rep, unsigned parts, unsigned q_stride, metal_view partial,
        metal_view ctx, const kv_layout& l, unsigned part_len)
    {
        if (tokens == 0) return;
        const int kernel = attention_kernel(rep, l.hd);
        if (kernel < 0) throw std::invalid_argument("metal_device: an attention of other heads than its kernels take");
        const attention_args a = { l.hd, rep, l.heads, l.q8 ? 1u : 0u, l.positions, part_len, parts, q_stride,
            table_stride, 0, l.block_stride, l.k_slab, l.v_slab, l.head_stride };
        impl_->use(kernel);
        impl_->bind(q, 0);
        impl_->bind(region, 1);
        impl_->bind(blocks, 2);
        impl_->bind(lengths, 3);
        impl_->bind(partial, 4);
        impl_->args(a, 5);
        impl_->bind(ctx, 6);
        impl_->groups(parts, l.heads, tokens, attention_threads);
        // A single part wrote the outputs itself.
        if (parts == 1) return;
        impl_->use(k_merge_attention);
        impl_->bind(partial, 0);
        impl_->bind(lengths, 1);
        impl_->bind(ctx, 2);
        impl_->args(a, 3);
        impl_->groups(l.heads * rep, tokens, 1, l.hd);
    }

    void metal_device::commands::finish()
    {
        impl_->close();
        [impl_->buffer commit];
        [impl_->buffer waitUntilCompleted];
        if (impl_->buffer.status == MTLCommandBufferStatusError)
            throw std::runtime_error(std::string("the GPU failed the commands: ")
                + (impl_->buffer.error ? impl_->buffer.error.localizedDescription.UTF8String : "unknown error"));
    }

    double metal_device::commands::gpu_milliseconds() const
    {
        return (impl_->buffer.GPUEndTime - impl_->buffer.GPUStartTime) * 1000.0;
    }

    json metal_describe()
    {
        @autoreleasepool
        {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) return json();
            json d = json::object();
            d["type"] = "gpu";
            d["name"] = device.name.UTF8String;
            d["api"] = "Metal";
            d["memory"] = static_cast<unsigned long long>(device.recommendedMaxWorkingSetSize);
            d["max_buffer"] = static_cast<unsigned long long>(device.maxBufferLength);
            d["unified_memory"] = static_cast<bool>(device.hasUnifiedMemory);
            return d;
        }
    }

}
