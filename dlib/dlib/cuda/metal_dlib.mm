// Copyright (C) 2026  TortueTech
// License: Boost Software License   See LICENSE.txt for the full license.

// The Objective-C++ side of metal_dlib.h, compiled only when dlib is built with
// DLIB_USE_METAL.  It needs automatic reference counting (-fobjc-arc), which the CMake
// file of dlib sets for this file.

#ifndef DLIB_USE_METAL
#error "metal_dlib.mm is compiled only in a build with DLIB_USE_METAL"
#endif
#if !__has_feature(objc_arc)
#error "metal_dlib.mm needs automatic reference counting (-fobjc-arc)"
#endif

#import <Foundation/Foundation.h>

#include "metal_dlib_internal.h"

#include <limits>
#include <map>
#include <mutex>

namespace dlib
{
    namespace metal
    {
        namespace
        {
            struct buffer_entry
            {
                id<MTLBuffer> buffer;
                size_t bytes;
            };

            /*
                The one Metal device of the process, its command queue, and the buffers made
                by allocate(), indexed by the address of their contents so that a host
                pointer leads back to its buffer.  Created on first use and never destroyed:
                tensors released during the static destruction of the program still find it.
            */
            struct context
            {
                id<MTLDevice> device = nil;
                id<MTLCommandQueue> queue = nil;
                std::string problem;

                std::mutex lock;
                std::map<const char*, buffer_entry> buffers;
            };

            context& get_context (
            )
            {
                static context* const ctx = []
                {
                    context* c = new context;
                    @autoreleasepool
                    {
                        c->device = MTLCreateSystemDefaultDevice();
                        if (c->device == nil)
                        {
                            c->problem = "no Metal device was found on this system";
                        }
                        else
                        {
                            c->queue = [c->device newCommandQueue];
                            if (c->queue == nil)
                                c->problem = "the Metal device did not create a command queue";
                        }
                    }
                    return c;
                }();
                return *ctx;
            }

            context& ready_context (
            )
            {
                context& c = get_context();
                if (!c.problem.empty())
                    throw metal_error("Metal backend of dlib: " + c.problem + ".");
                return c;
            }

            void release_buffer (
                float* p
            )
            {
                context& c = get_context();
                std::lock_guard<std::mutex> guard(c.lock);
                // Erasing the entry drops the last strong reference to the MTLBuffer.
                c.buffers.erase(reinterpret_cast<const char*>(p));
            }

        }

        std::string command_problem (
            id<MTLCommandBuffer> cmd
        )
        {
            NSError* e = cmd.error;
            if (e == nil)
                return "status " + std::to_string((long)cmd.status);
            return std::string([[e localizedDescription] UTF8String]);
        }

        id<MTLDevice> ready_device (
        ) { return ready_context().device; }

        id<MTLCommandQueue> ready_queue (
        ) { return ready_context().queue; }

    // ------------------------------------------------------------------------------------

        bool is_compiled_in (
        ) { return true; }

        bool is_available (
        ) { return get_context().problem.empty(); }

        std::string unavailable_reason (
        ) { return get_context().problem; }

        std::string device_name (
        )
        {
            context& c = get_context();
            if (c.device == nil)
                return std::string();
            @autoreleasepool
            {
                return std::string([[c.device name] UTF8String]);
            }
        }

        std::shared_ptr<float> allocate (
            size_t n
        )
        {
            context& c = ready_context();
            if (n == 0)
                throw metal_error("Metal backend of dlib: a buffer of 0 floats was asked for.");
            if (n > std::numeric_limits<size_t>::max()/sizeof(float))
                throw metal_error("Metal backend of dlib: a buffer of " + std::to_string(n) +
                    " floats does not fit in memory addresses.");
            const size_t bytes = n*sizeof(float);
            if (bytes > (size_t)c.device.maxBufferLength)
                throw metal_error("Metal backend of dlib: a buffer of " + std::to_string(bytes) +
                    " bytes exceeds the largest buffer of the device (" +
                    std::to_string((size_t)c.device.maxBufferLength) + " bytes).");

            id<MTLBuffer> buffer = nil;
            @autoreleasepool
            {
                buffer = [c.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            }
            if (buffer == nil)
                throw metal_error("Metal backend of dlib: the device refused a shared buffer of " +
                    std::to_string(bytes) + " bytes.");

            float* const p = static_cast<float*>([buffer contents]);
            {
                std::lock_guard<std::mutex> guard(c.lock);
                c.buffers[reinterpret_cast<const char*>(p)] = buffer_entry{buffer, bytes};
            }
            return std::shared_ptr<float>(p, release_buffer);
        }

        void synchronize (
        )
        {
            context& c = ready_context();
            @autoreleasepool
            {
                id<MTLCommandBuffer> cmd = [c.queue commandBuffer];
                if (cmd == nil)
                    throw metal_error("Metal backend of dlib: the queue did not create a command buffer.");
                [cmd commit];
                [cmd waitUntilCompleted];
                if (cmd.status != MTLCommandBufferStatusCompleted)
                    throw metal_error("Metal backend of dlib: synchronize failed: " + command_problem(cmd));
            }
        }

        buffer_view find_buffer (
            const float* p,
            size_t n
        )
        {
            context& c = ready_context();
            const char* const begin = reinterpret_cast<const char*>(p);
            std::lock_guard<std::mutex> guard(c.lock);
            auto i = c.buffers.upper_bound(begin);
            if (i != c.buffers.begin())
            {
                --i;
                const size_t offset = begin - i->first;
                if (begin >= i->first && offset <= i->second.bytes &&
                    n <= (i->second.bytes - offset)/sizeof(float))
                {
                    buffer_view v;
                    v.buffer = i->second.buffer;
                    v.offset = offset;
                    return v;
                }
            }
            throw metal_error("Metal backend of dlib: " + std::to_string(n) +
                " floats at the given address are not inside a Metal buffer.");
        }

        void blit_copy (
            float* dest,
            const float* src,
            size_t n
        )
        {
            if (n == 0)
                return;
            context& c = ready_context();
            const buffer_view d = find_buffer(dest, n);
            const buffer_view s = find_buffer(src, n);
            const char* const db = reinterpret_cast<const char*>(dest);
            const char* const sb = reinterpret_cast<const char*>(src);
            const size_t bytes = n*sizeof(float);
            if (db < sb + bytes && sb < db + bytes)
                throw metal_error("Metal backend of dlib: blit_copy was given overlapping ranges.");

            @autoreleasepool
            {
                id<MTLCommandBuffer> cmd = [c.queue commandBuffer];
                if (cmd == nil)
                    throw metal_error("Metal backend of dlib: the queue did not create a command buffer.");
                id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                if (blit == nil)
                    throw metal_error("Metal backend of dlib: the command buffer did not create a blit encoder.");
                [blit copyFromBuffer:s.buffer
                        sourceOffset:s.offset
                            toBuffer:d.buffer
                   destinationOffset:d.offset
                                size:bytes];
                [blit endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                if (cmd.status != MTLCommandBufferStatusCompleted)
                    throw metal_error("Metal backend of dlib: blit_copy failed: " + command_problem(cmd));
            }
        }

        size_t live_buffers (
        )
        {
            context& c = get_context();
            std::lock_guard<std::mutex> guard(c.lock);
            return c.buffers.size();
        }
    }
}
