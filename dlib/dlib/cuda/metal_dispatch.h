// Copyright (C) 2026  TortueTech
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_METAL_DISPATCH_H_
#define DLIB_METAL_DISPATCH_H_

/*
    How a tensor_tools function chooses its path from the devices of its tensors.

    route(op, args...) looks at the tensors among args (other arguments are ignored):
        - all on the CPU                -> false: the host path runs, unchanged;
        - all on Metal                  -> true: the Metal path runs;
        - some on the CPU, some on Metal -> metal_error, naming the operation.
    An empty tensor (size() == 0) holds no values and counts for neither device, so that an
    output still to be sized does not decide the route.

    refuse(op, args...) is route() for an operation without a Metal version: it raises
    metal_error as soon as one of its tensors is on Metal.

    The operations below have a Metal version (metal_kernels.mm).  Each takes the arguments
    of its tt:: counterpart, all its tensors on Metal, and returns once the GPU is done.
*/

#include "metal_dlib.h"
#include "operation_mode.h"
#include "tensor.h"
#include <string>
#include <type_traits>

namespace dlib
{
    namespace metal
    {
        namespace impl
        {
            struct device_census
            {
                bool cpu = false;
                bool metal = false;
            };

            inline void count (device_census& c, const tensor& t)
            {
                if (t.size() == 0)
                    return;
                if (t.get_tensor_device() == tensor_device::metal)
                    c.metal = true;
                else
                    c.cpu = true;
            }

            template <typename T>
            inline typename std::enable_if<!std::is_base_of<tensor, T>::value>::type
            count (device_census&, const T&) {}
        }

        template <typename... T>
        bool route (
            const char* op,
            const T&... args
        )
        {
            impl::device_census c;
            const int expand[] = {0, (impl::count(c, args), 0)...};
            (void)expand;
            if (c.cpu && c.metal)
                throw metal_error(std::string("Metal backend of dlib: tt::") + op +
                    " was given tensors on the CPU and tensors on Metal.  Put them on one "
                    "device with resizable_tensor::set_tensor_device() first.");
            return c.metal;
        }

        [[noreturn]] inline void not_ported (
            const char* op
        )
        {
            throw metal_error(std::string("Metal backend of dlib: tt::") + op +
                " has no Metal version yet, and dlib does not compute a Metal tensor on the "
                "CPU behind your back.  Move the tensors with "
                "set_tensor_device(tensor_device::cpu) to use the CPU version.");
        }

        template <typename... T>
        void refuse (
            const char* op,
            const T&... args
        )
        {
            if (route(op, args...))
                not_ported(op);
        }

    // ------------------------------------------------------------------------------------

        void fill (tensor& dest, float value);
        void add (tensor& dest, const tensor& src1, const tensor& src2);
        void add (float beta, tensor& dest, float alpha, const tensor& src);
        void multiply (bool add_to, tensor& dest, const tensor& src1, const tensor& src2);
        void softmax (tensor& dest, const tensor& src, operation_mode mode);
        void gelu (tensor& dest, const tensor& src);
        void silu (tensor& dest, const tensor& src);
        void gemm (float beta, tensor& dest, float alpha, const tensor& lhs, bool trans_lhs,
                   const tensor& rhs, bool trans_rhs, operation_mode mode);
        void layer_normalize (double eps, resizable_tensor& dest, resizable_tensor& means,
                              resizable_tensor& invstds, const tensor& src, const tensor& gamma,
                              const tensor& beta);
        void gguf_matvec (tensor& dest, const tensor& src, const tensor& weights, int type, long rows,
                          long cols, size_t row_bytes);

        // Backward passes.
        void softmax_gradient (tensor& grad, const tensor& dest, const tensor& gradient_input,
                               operation_mode mode);
        void gelu_gradient (tensor& grad, const tensor& src, const tensor& gradient_input);
        void silu_gradient (tensor& grad, const tensor& src, const tensor& gradient_input);
        void assign_bias_gradient (tensor& grad, const tensor& gradient_input);
        void assign_conv_bias_gradient (tensor& grad, const tensor& gradient_input);
        void layer_normalize_gradient (double eps, const tensor& gradient_input, const tensor& means,
                                       const tensor& invstds, const tensor& src, const tensor& gamma,
                                       tensor& src_grad, tensor& gamma_grad, tensor& beta_grad,
                                       resizable_tensor& dmeans, resizable_tensor& dvars);
    }
}

#endif // DLIB_METAL_DISPATCH_H_
