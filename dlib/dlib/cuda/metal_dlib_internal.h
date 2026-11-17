// Copyright (C) 2026  TortueTech
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_METAL_DLIB_INTERNAL_H_
#define DLIB_METAL_DLIB_INTERNAL_H_

// Shared by the Objective-C++ files of the Metal backend only; not part of the interface.

#ifndef __OBJC__
#error "metal_dlib_internal.h is for the Objective-C++ files of the Metal backend"
#endif

#import <Metal/Metal.h>

#include "metal_dlib.h"
#include <string>

namespace dlib
{
    namespace metal
    {
        struct buffer_view
        {
            id<MTLBuffer> buffer = nil;
            size_t offset = 0;        // in bytes, from the start of the buffer
        };

        buffer_view find_buffer (
            const float* p,
            size_t n
        );
        /*!
            ensures
                - returns the Metal buffer holding the n floats that start at p, and the
                  offset of p in it.  The buffer stays alive as long as the tensor that
                  owns p does.
            throws
                - metal_error when the range is not inside a buffer made by allocate().
        !*/

        id<MTLDevice> ready_device (
        );
        id<MTLCommandQueue> ready_queue (
        );
        /*!
            ensures
                - return the device and the queue of the process.
            throws
                - metal_error when no Metal device is available.
        !*/

        std::string command_problem (
            id<MTLCommandBuffer> cmd
        );
        /*!
            ensures
                - returns the error of a command buffer that did not complete, in words.
        !*/
    }
}

#endif // DLIB_METAL_DLIB_INTERNAL_H_
