// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_METAL_DLIB_H_
#define DLIB_METAL_DLIB_H_

/*
    The Metal backend of dlib, for Apple Silicon.

    A tensor carries a device tag, tensor_device::cpu (the default) or tensor_device::metal.
    The values of a Metal tensor live in an MTLBuffer created with MTLStorageModeShared: the
    CPU and the GPU see the same memory, so host() points straight into the buffer and no
    copy between host and device ever happens.  Every command sent to the GPU is waited for
    before the call that sent it returns, so host() always reads finished values.

    The tensor_tools functions route a call by the device of its tensors: CPU tensors take
    the host path, unchanged; Metal tensors take a Metal kernel, or raise metal_error when
    the operation has no Metal version yet.  A call mixing CPU and Metal tensors raises
    metal_error as well.  Nothing falls back to the CPU silently.

    This header is plain C++: the Objective-C++ side lives in metal_dlib.mm, compiled only
    when dlib is built with DLIB_USE_METAL.  Without it, is_compiled_in() is false and
    asking for a Metal tensor raises metal_error.
*/

#if defined(DLIB_USE_METAL) && defined(DLIB_USE_CUDA)
#error "DLIB_USE_METAL and DLIB_USE_CUDA exclude each other"
#endif

#include "../error.h"
#include <cstddef>
#include <memory>
#include <string>

namespace dlib
{

// ----------------------------------------------------------------------------------------

    enum class tensor_device
    {
        cpu,
        metal
    };

    inline const char* to_string (tensor_device d)
    {
        return d == tensor_device::metal ? "metal" : "cpu";
    }

    struct metal_error : public error
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                The exception thrown when the Metal backend cannot do what it was asked:
                no Metal device, a failed command, an operation without a Metal version,
                tensors of different devices in one call, or a Metal tensor asked of a
                build without DLIB_USE_METAL.
        !*/

        metal_error(const std::string& message): error(message) {}
    };

// ----------------------------------------------------------------------------------------

    namespace metal
    {
        bool is_compiled_in (
        );
        /*!
            ensures
                - returns true when dlib was built with DLIB_USE_METAL.
        !*/

        bool is_available (
        );
        /*!
            ensures
                - returns true when dlib was built with DLIB_USE_METAL and the system has a
                  Metal device.  The device and its command queue are created on the first
                  call, once for the whole process.
        !*/

        std::string unavailable_reason (
        );
        /*!
            ensures
                - returns why is_available() is false, or an empty string when it is true.
        !*/

        std::string device_name (
        );
        /*!
            ensures
                - returns the name of the Metal device, or an empty string without one.
        !*/

        std::shared_ptr<float> allocate (
            size_t n
        );
        /*!
            requires
                - n > 0
            ensures
                - returns a pointer to n floats inside a new shared MTLBuffer.  The buffer is
                  released with the last copy of the returned pointer.
            throws
                - metal_error when no Metal device is available or the allocation fails.
        !*/

        void synchronize (
        );
        /*!
            ensures
                - sends an empty command buffer through the command queue and waits for it:
                  everything sent before it has finished.
            throws
                - metal_error when no Metal device is available or the command fails.
        !*/

        void blit_copy (
            float* dest,
            const float* src,
            size_t n
        );
        /*!
            requires
                - dest and src each point at n floats inside buffers returned by allocate(),
                  and the two ranges do not overlap.
            ensures
                - the GPU copies the n floats of src to dest with a blit command, and the
                  call returns once the copy has finished.  A test of the skeleton: it shows
                  that the GPU reads and writes the memory the CPU sees.
            throws
                - metal_error when a range is not inside a Metal buffer or the command fails.
        !*/

        size_t live_buffers (
        );
        /*!
            ensures
                - returns the number of buffers allocate() made that are still held.
        !*/

        size_t live_bytes (
        );
        /*!
            ensures
                - returns the sum of the lengths of the buffers allocate() made that are
                  still held.
        !*/

        size_t allocated_bytes (
        );
        /*!
            ensures
                - returns the memory the Metal device reports allocated by this process
                  (MTLDevice.currentAllocatedSize), or 0 without a device.
        !*/
    }

// ----------------------------------------------------------------------------------------

}

#endif // DLIB_METAL_DLIB_H_
