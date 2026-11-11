// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_METAL_DEVICE_H_
#define ALBIREO_ENGINE_METAL_DEVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include <dlib/data_io/gguf_reader.h>

#include "../common/json.h"

namespace albireo
{

    /*!
        A buffer of the GPU in the memory it shares with the processor: both read and write its
        bytes (contents()).  It owns its memory, or wraps memory that another object owns (the
        mapping of a model file, the region of a kv_pool), which must outlive it.  Copies share
        the same buffer.
    !*/
    class metal_buffer
    {
    public:
        metal_buffer() {}
        bool valid() const { return impl_ != nullptr; }
        void* contents() const { return contents_; }
        size_t bytes() const { return bytes_; }
        struct impl;
        std::shared_ptr<impl> impl_;
        void* contents_ = nullptr;
        size_t bytes_ = 0;
    };

    // Where in a buffer an operand of a kernel starts.
    struct metal_view
    {
        const metal_buffer* buffer = nullptr;
        size_t offset = 0;
        metal_view() {}
        metal_view(const metal_buffer& b, size_t at = 0) : buffer(&b), offset(at) {}
    };

    class metal_device
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                The GPU of the Mac, through Metal (macOS port): its queue of commands and the
                kernels of metal_kernels.metal, which macOS compiles when the device opens.
                The GPU shares the memory of the processor: it reads the weights in the
                mapping of the model file and the keys and values in the region of their pool,
                where they lie, without copy.  The interface is plain C++, so that only
                metal_device.mm sees Objective-C.
        !*/
    public:
        /*!
            ensures
                - Opens the GPU and compiles its kernels, or returns null with why set: no GPU,
                  or kernels that do not compile.
        !*/
        static std::unique_ptr<metal_device> open(std::string& why);

        ~metal_device();
        metal_device(const metal_device&) = delete;
        metal_device& operator=(const metal_device&) = delete;

        // The name of the GPU, "Apple M2" for instance.
        const std::string& name() const;
        // The longest buffer the GPU takes, and the memory Metal advises it to use at most.
        uint64_t max_buffer_bytes() const;
        uint64_t working_set_bytes() const;
        // The kernels compiled, by name.
        int kernel_count() const;

        // A buffer of bytes in shared memory, zeroed; invalid when the GPU refuses it.
        metal_buffer allocate(size_t bytes);

        /*!
            ensures
                - Returns a buffer over the whole pages that hold [data, data + bytes), without
                  copy, and sets offset to where data lies in it; invalid when the GPU refuses
                  it (longer than max_buffer_bytes(), for instance).  The memory must stay
                  mapped while the buffer lives.
        !*/
        metal_buffer wrap(const void* data, size_t bytes, size_t& offset);

        // The tokens of a pass at most (MAX_TOKENS of metal_kernels.metal).
        static const unsigned max_tokens = 8;

        // The positions of a part of the attention (attention()).
        static const unsigned attention_part = 256;

        /*!
            WHAT THIS OBJECT REPRESENTS
                The commands of one pass: the kernels encoded in order, each one seeing what the
                previous ones wrote, then run by finish(), which waits for the GPU.  One thread
                uses an object at a time.
        !*/
        class commands
        {
        public:
            ~commands();
            commands(const commands&) = delete;
            commands& operator=(const commands&) = delete;

            /*!
                y[t * y_stride + r] = alpha * sum_i W[r][i] x[t * x_stride + i] + beta * y[...],
                r < rows, t < tokens (at most max_tokens), W in the GGUF layout of type, rows of
                cols values.  Returns false, encoding nothing, for a type without a kernel
                (Q4_K and Q6_K have one).
            !*/
            bool matvec(dlib::ggml_type type, metal_view W, long rows, long cols, metal_view x, unsigned tokens,
                unsigned x_stride, metal_view y, unsigned y_stride, float alpha = 1.0f, float beta = 0.0f);

            // y[r] = x[r] * gamma / sqrt(mean of x[r]^2 + eps), rows of width values (x may be y).
            void rms_norm(metal_view x, metal_view gamma, metal_view y, unsigned rows, unsigned width,
                unsigned x_stride, unsigned y_stride, float eps);

            // x[t * stride + i] += b[i], i < n, t < rows.
            void add_bias(metal_view x, metal_view b, unsigned n, unsigned stride, unsigned rows);

            // gate[i] = silu(gate[i]) * up[i], i < n.
            void silu_mul(metal_view gate, metal_view up, unsigned n);

            /*!
                The rotary positions of tokens rows of heads heads of hd values (stride floats a
                token): the first 2 * half values of each head turn by cs[t * cs_stride + i]
                (cosines) and cs[t * cs_stride + half + i] (sines), in pairs (i, i + half) when
                neox, (2i, 2i + 1) otherwise.
            !*/
            void rope(metal_view x, metal_view cs, unsigned tokens, unsigned heads, unsigned hd, unsigned half,
                bool neox, unsigned stride, unsigned cs_stride);

            /*!
                The layout of the keys and values of one layer in the region of a kv_pool:
                block b at b * block_stride, the keys of head h at k_slab + h * head_stride, the
                values at v_slab + h * head_stride, in 8-bit integers with their scales (q8) or
                in halves.
            !*/
            struct kv_layout
            {
                unsigned hd = 0;
                unsigned heads = 0;
                unsigned positions = 0;
                bool q8 = false;
                uint64_t block_stride = 0;
                uint64_t k_slab = 0;
                uint64_t v_slab = 0;
                uint64_t head_stride = 0;
            };

            // The keys and values of tokens tokens (kv_stride floats a token) into row rows[t] of
            // block blocks[t] (int32), the keys rotated in q8 (qk_rotate_row).
            void kv_store(metal_view k, metal_view v, unsigned kv_stride, metal_view region, metal_view blocks,
                metal_view rows, unsigned tokens, const kv_layout& layout);

            // The queries of tokens tokens, heads heads of hd values, scaled, and rotated in q8.
            void prepare_queries(metal_view q, unsigned tokens, unsigned heads, unsigned hd, unsigned q_stride,
                float scale, bool q8);

            /*!
                The attention of tokens tokens: token t, whose queries (prepared) lie at
                q + t * q_stride, sees the first lengths[t] positions of its sequence, whose
                blocks are blocks[t * table_stride + j]; rep query heads share each key/value
                head.  Writes the outputs at ctx + t * q_stride.  partial holds
                tokens * heads * parts * rep * (hd + 2) floats, parts covering the longest
                length by attention_part positions.
            !*/
            void attention(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
                unsigned table_stride, unsigned tokens, unsigned rep, unsigned parts, unsigned q_stride,
                metal_view partial, metal_view ctx, const kv_layout& layout);

            /*!
                ensures
                    - Runs the commands and waits for them; throws std::runtime_error when the
                      GPU fails them.
            !*/
            void finish();

        private:
            friend class metal_device;
            commands(metal_device& device);
            struct impl;
            std::unique_ptr<impl> impl_;
        };

        // A new pass of commands.
        std::unique_ptr<commands> begin();

    private:
        metal_device();
        struct impl;
        std::unique_ptr<impl> impl_;
    };

    // The GPU as describe() lists it, {"type": "gpu", "name", "memory", "max_buffer"}, without
    // compiling the kernels; null when the Mac has no GPU that Metal reaches.
    json metal_describe();

}

#endif // ALBIREO_ENGINE_METAL_DEVICE_H_
