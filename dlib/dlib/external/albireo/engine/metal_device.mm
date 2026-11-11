// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// The GPU of the Mac through Metal (macOS port).  Compiled with automatic reference counting.

#include "metal_device.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <vector>

namespace albireo
{

    namespace
    {
        // The source of the kernels, embedded by CMakeLists.txt (metal_kernels.inc.in).
        const char* const metal_source =
#include "metal_kernels.inc"
            ;

        // The kernels the engine calls; each must compile into a pipeline.
        const char* const kernel_names[] = { "mv_q4_K", "mv_q6_K" };
    }

    struct metal_device::impl
    {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLLibrary> library = nil;
        std::vector<id<MTLComputePipelineState>> pipelines;
        std::string name;
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
