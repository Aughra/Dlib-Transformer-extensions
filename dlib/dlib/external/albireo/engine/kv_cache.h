// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_KV_CACHE_H_
#define ALBIREO_ENGINE_KV_CACHE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace albireo
{

    /*!
        The storage of the keys and values: half precision, or 8-bit integers by groups of
        kv_q8_group values that share a scale in half precision (q8), 34 bytes for 32 values
        against 64 in half precision, for the memory of a context and for the bytes the
        attention reads.
    !*/
    enum class kv_format { f16, q8 };

    const long kv_q8_group = 32;

    // The bytes of a row of head_dim keys or values in a format, its scales included.
    inline size_t kv_row_bytes(kv_format format, long head_dim)
    {
        return format == kv_format::q8
            ? static_cast<size_t>(head_dim) + static_cast<size_t>(head_dim / kv_q8_group) * sizeof(uint16_t)
            : static_cast<size_t>(head_dim) * sizeof(uint16_t);
    }

    class kv_pool
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                The memory that holds the keys and values of every sequence of one model,
                as blocks of a fixed number of positions.  A sequence owns the blocks its
                positions fall in and nothing more, so a short exchange takes a few blocks
                and a long one takes many, and the memory the engine reserves follows the
                text actually held rather than the longest context it could hold.

                Keys and values are stored in the format of the pool (kv_format).  Inside a
                block, the rows of one layer and one key/value head are contiguous, which is
                the order in which attention reads them; in q8, the scales of these rows
                follow them, kv_q8_group values to a scale.

                Blocks are allocated in groups as sequences grow, up to a budget in bytes;
                a block given back is reused before any new memory is taken, and trim()
                returns the memory of the blocks no sequence holds to the system.  All the
                blocks lie in one region of addresses reserved for the budget at once, aligned
                on the pages of the system, whose pages the system gives on first use only: a
                graphics processor that shares the memory of the processor can take the whole
                region as one buffer (region(), block_offset()).  A guard
                (set_guard) may refuse a new block, from the memory the system has left:
                the pool then acts as when its budget is exhausted.

                Several sequences may hold the same block: a conversation that starts with
                the text of a cached one holds the full blocks of that text along with it.
                A block counts the sequences that hold it and returns to the pool when the
                last one gives it back; a sequence copies a shared block before writing in
                it (kv_sequence::reserve).
        !*/
    public:
        kv_pool(long n_layers, long n_kv_heads, long head_dim, long block_positions, size_t budget_bytes,
            kv_format format = kv_format::f16)
            : layers_(n_layers), heads_(n_kv_heads), head_dim_(head_dim), positions_(block_positions), format_(format)
        {
            if (n_layers <= 0 || n_kv_heads <= 0 || head_dim <= 0 || block_positions <= 0)
                throw std::invalid_argument("kv_pool: invalid geometry");
            if (format == kv_format::q8 && head_dim % kv_q8_group != 0)
                throw std::invalid_argument("kv_pool: the rows of 8-bit keys and values take whole groups");
            // A block of 2^41 bytes at most, whose size stays far from overflowing.
            const double bytes = 2.0 * n_layers * n_kv_heads * block_positions * static_cast<double>(head_dim);
            if (bytes > 2199023255552.0) throw std::invalid_argument("kv_pool: blocks too large");
            // Keys then values, each layers x heads slabs of positions rows (and their scales in q8).
            slab_bytes_ = static_cast<size_t>(positions_) * kv_row_bytes(format_, head_dim_);
            bytes_per_kind_ = static_cast<size_t>(layers_) * heads_ * slab_bytes_;
            block_bytes_ = 2 * bytes_per_kind_;
            max_blocks_ = std::max<size_t>(1, budget_bytes / block_bytes_);
            // The table of blocks never moves, so that the threads reading a block during
            // a forward pass never see it relocated by an allocation, and the lists never
            // grow, so that giving a block back cannot fail.
            reserve_region();
            blocks_.reserve(max_blocks_);
            refs_.reserve(max_blocks_);
            free_.reserve(max_blocks_);
            vacant_.reserve(max_blocks_);
        }

        ~kv_pool() { release_region(); }

        kv_pool(const kv_pool&) = delete;
        kv_pool& operator=(const kv_pool&) = delete;

        // The region of all the blocks, and its length, a multiple of the page.
        uint8_t* region() const { return region_; }
        size_t region_bytes() const { return region_bytes_; }

        // Where block b starts in the region.
        size_t block_offset(int b) const { return static_cast<size_t>(b) * stride_; }

        // Where the keys (values false) or the values (values true) of a layer and a head start
        // in a block, and where their scales start (q8).
        size_t slab_offset(bool values, long layer, long head) const
        {
            return (values ? bytes_per_kind_ : 0) + offset(layer, head);
        }
        size_t scales_offset(bool values, long layer, long head) const
        {
            return slab_offset(values, layer, head) + static_cast<size_t>(positions_) * head_dim_;
        }

        /*
            The check of a new block of memory: guard(bytes, allocated) returns false when the
            system cannot give the bytes of the block, allocated being the bytes the blocks of
            the pool already hold.  It runs with the pool locked.
        */
        void set_guard(std::function<bool(size_t, size_t)> guard)
        {
            std::lock_guard<std::mutex> l(m_);
            guard_ = std::move(guard);
        }

        long block_positions() const { return positions_; }
        size_t block_bytes() const { return block_bytes_; }
        size_t max_blocks() const { return max_blocks_; }
        kv_format format() const { return format_; }

        size_t allocated_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return blocks_.size() - vacant_.size();
        }
        size_t used_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return blocks_.size() - free_.size() - vacant_.size();
        }
        size_t free_capacity_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return free_.size() + vacant_.size() + (max_blocks_ - blocks_.size());
        }

        // Returns a block index, held once, or -1 when the budget is exhausted; throws
        // std::bad_alloc when the system has no memory left.
        int allocate()
        {
            std::lock_guard<std::mutex> l(m_);
            if (!free_.empty())
            {
                const int b = free_.back();
                free_.pop_back();
                refs_[static_cast<size_t>(b)] = 1;
                return b;
            }
            if (guard_ && !guard_(block_bytes_, (blocks_.size() - vacant_.size()) * block_bytes_)) return -1;
            if (!vacant_.empty())
            {
                const int b = vacant_.back();
                commit(b);
                vacant_.pop_back();
                refs_[static_cast<size_t>(b)] = 1;
                return b;
            }
            if (blocks_.size() >= max_blocks_) return -1;
            const int b = static_cast<int>(blocks_.size());
            commit(b);
            blocks_.push_back(region_ + block_offset(b));
            refs_.push_back(1);
            return b;
        }

        // Gives a hold on a block back; the block returns to the pool with the last one.
        void release(int b)
        {
            if (b < 0) return;
            std::lock_guard<std::mutex> l(m_);
            int& holders = refs_[static_cast<size_t>(b)];
            if (holders > 0 && --holders == 0) free_.push_back(b);
        }

        // One more sequence holds block b.
        void share(int b)
        {
            std::lock_guard<std::mutex> l(m_);
            ++refs_[static_cast<size_t>(b)];
        }

        // Whether more than one sequence holds block b.
        bool shared(int b) const
        {
            std::lock_guard<std::mutex> l(m_);
            return refs_[static_cast<size_t>(b)] > 1;
        }

        // Frees the memory of the blocks no sequence holds; their slots in the table stay,
        // so that the indices of the other blocks never change.
        void trim()
        {
            std::lock_guard<std::mutex> l(m_);
            for (int b : free_)
            {
                decommit(b);
                vacant_.push_back(b);
            }
            free_.clear();
        }

        // Copies the keys and values of block src into block dst.
        void copy_block(int src, int dst)
        {
            if (src == dst) return;
            const uint8_t* from = blocks_[static_cast<size_t>(src)];
            std::copy(from, from + block_bytes_, blocks_[static_cast<size_t>(dst)]);
        }

        /*
            The rows of positions x head_dim keys or values of one layer and one key/value
            head: halves in f16, 8-bit integers in q8, whose scales, kv_q8_group values to a
            scale, follow the rows (key_scales, value_scales).
        */
        uint8_t* keys(int block, long layer, long head)
        {
            return blocks_[static_cast<size_t>(block)] + offset(layer, head);
        }
        uint8_t* values(int block, long layer, long head)
        {
            return blocks_[static_cast<size_t>(block)] + bytes_per_kind_ + offset(layer, head);
        }
        uint16_t* key_scales(int block, long layer, long head)
        {
            return reinterpret_cast<uint16_t*>(keys(block, layer, head)
                + static_cast<size_t>(positions_) * head_dim_);
        }
        uint16_t* value_scales(int block, long layer, long head)
        {
            return reinterpret_cast<uint16_t*>(values(block, layer, head)
                + static_cast<size_t>(positions_) * head_dim_);
        }

    private:
        size_t offset(long layer, long head) const
        {
            return (static_cast<size_t>(layer) * heads_ + head) * slab_bytes_;
        }

        static size_t page_bytes() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

        // The addresses of max_blocks_ blocks, reserved without memory behind them.
        void reserve_region()
        {
            // Each block starts on a page, so that its memory goes back to the system whole.
            const size_t page = page_bytes();
            stride_ = (block_bytes_ + page - 1) / page * page;
            region_bytes_ = max_blocks_ * stride_;
            void* p = mmap(nullptr, region_bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (p == MAP_FAILED) throw std::bad_alloc();
            region_ = static_cast<uint8_t*>(p);
        }

        void release_region()
        {
            if (!region_) return;
            munmap(region_, region_bytes_);
            region_ = nullptr;
        }

        // Takes the pages of block b back after decommit(); the system gives them on first use.
        void commit(int b) { madvise(region_ + block_offset(b), stride_, MADV_FREE_REUSE); }

        // Gives the memory of block b back to the system, the addresses staying reserved.
        void decommit(int b)
        {
            madvise(region_ + block_offset(b), stride_, MADV_FREE_REUSABLE);
        }

        long layers_, heads_, head_dim_, positions_;
        kv_format format_;
        size_t slab_bytes_ = 0;
        size_t bytes_per_kind_ = 0;
        size_t block_bytes_ = 0;
        size_t max_blocks_ = 0;
        mutable std::mutex m_;
        uint8_t* region_ = nullptr;
        size_t region_bytes_ = 0;
        size_t stride_ = 0;         // the distance between two blocks, whole pages
        std::vector<uint8_t*> blocks_;
        std::vector<int> refs_;     // the number of sequences holding each block
        std::vector<int> free_;     // blocks with memory, held by no sequence
        std::vector<int> vacant_;   // slots whose memory trim() freed
        std::function<bool(size_t, size_t)> guard_;
    };

    struct kv_sequence
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                The positions of one sequence held in a kv_pool: the blocks it owns, in
                position order, and how many positions they hold.
        !*/
        std::vector<int> blocks;
        long length = 0;

        /*
            Makes room for positions [0, n), taking blocks from the pool, and copies the
            block where the next position goes when other sequences hold it too, so that
            the positions this sequence writes belong to it alone.  Returns false, leaving
            the sequence unchanged, when the pool or the system cannot provide the blocks.
        */
        bool reserve(kv_pool& pool, long n)
        {
            const long per = pool.block_positions();
            const size_t needed = static_cast<size_t>((n + per - 1) / per);
            const size_t next = static_cast<size_t>(length / per);
            const bool copy_next = next < blocks.size() && next < needed && pool.shared(blocks[next]);
            const size_t extra = (needed > blocks.size() ? needed - blocks.size() : 0) + (copy_next ? 1 : 0);
            if (extra == 0) return true;
            std::vector<int> taken;
            try
            {
                blocks.reserve(blocks.size() + extra);
                taken.reserve(extra);
                while (taken.size() < extra)
                {
                    const int b = pool.allocate();
                    if (b < 0) break;
                    taken.push_back(b);
                }
            }
            catch (const std::bad_alloc&)
            {
                // Out of memory: as a full pool, the caller frees cached sequences or waits.
            }
            if (taken.size() < extra)
            {
                for (int t : taken) pool.release(t);
                return false;
            }
            if (copy_next)
            {
                const int own = taken.back();
                taken.pop_back();
                pool.copy_block(blocks[next], own);
                pool.release(blocks[next]);
                blocks[next] = own;
            }
            blocks.insert(blocks.end(), taken.begin(), taken.end());
            return true;
        }

        /*
            Holds positions [0, n) of another sequence: the full blocks of these positions
            are shared with it, and the last block, partly filled, is copied.  Returns
            false, leaving this sequence empty, when the pool or the system cannot provide
            the copy.
        */
        bool share_prefix(kv_pool& pool, const kv_sequence& from, long n)
        {
            clear(pool);
            n = std::max<long>(0, std::min(n, from.length));
            const long per = pool.block_positions();
            const size_t full = static_cast<size_t>(n / per);
            int copy = -1;
            try
            {
                blocks.reserve(full + 1);
                if (n % per != 0)
                {
                    copy = pool.allocate();
                    if (copy < 0) return false;
                }
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            if (copy >= 0) pool.copy_block(from.blocks[full], copy);
            for (size_t b = 0; b < full; ++b)
            {
                pool.share(from.blocks[b]);
                blocks.push_back(from.blocks[b]);
            }
            if (copy >= 0) blocks.push_back(copy);
            length = n;
            return true;
        }

        // Keeps positions [0, n) and gives the blocks beyond them back.
        void truncate(kv_pool& pool, long n)
        {
            if (n < 0) n = 0;
            if (n < length) length = n;
            const long per = pool.block_positions();
            const size_t keep = static_cast<size_t>((n + per - 1) / per);
            while (blocks.size() > keep)
            {
                pool.release(blocks.back());
                blocks.pop_back();
            }
        }

        void clear(kv_pool& pool) { truncate(pool, 0); }
    };

}

#endif // ALBIREO_ENGINE_KV_CACHE_H_
