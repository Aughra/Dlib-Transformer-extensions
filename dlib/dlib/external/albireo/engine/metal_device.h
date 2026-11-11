// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_METAL_DEVICE_H_
#define ALBIREO_ENGINE_METAL_DEVICE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "../common/json.h"

namespace albireo
{

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
