// Copyright (C) 2015  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_TeNSOR_TOOLS_CPP_
#define DLIB_TeNSOR_TOOLS_CPP_

#include "tensor_tools.h"
#include "gemm_planes.h"
#include "../string.h"
#include <atomic>
#include <ostream>

/*
    DLIB_NPU_TRY(f(args)) offers the operation to the NPU module and returns when it computed
    it; otherwise the host path that follows runs, timed when the routing asked for a host
    measurement. Without DLIB_USE_NPU it expands to nothing.
*/
#ifdef DLIB_USE_NPU
#define DLIB_NPU_TRY(call) if (::dlib::npu::call) return; ::dlib::npu::cpu_measure dlib_npu_measure_
#else
#define DLIB_NPU_TRY(call)
#endif

/*
    DLIB_METAL_UNPORTED("op", tensors...) stops an operation that has no Metal version yet
    when one of its tensors is on Metal, and refuses a call that mixes CPU and Metal tensors
    (see metal_dispatch.h).  CPU tensors go on to the host path, unchanged.  Without
    DLIB_USE_METAL no tensor can be on Metal, and it expands to nothing.
*/
/*
    DLIB_METAL_TRY(call, "op", tensors...) runs metal::call and returns when the tensors are
    on Metal, refuses a mix of devices, and lets CPU tensors go on to the host path.
*/
#ifdef DLIB_USE_METAL
#include "metal_dispatch.h"
#define DLIB_METAL_UNPORTED(...) ::dlib::metal::refuse(__VA_ARGS__)
#define DLIB_METAL_TRY(call, ...) if (::dlib::metal::route(__VA_ARGS__)) { ::dlib::metal::call; return; }
#else
#define DLIB_METAL_UNPORTED(...)
#define DLIB_METAL_TRY(call, ...)
#endif

namespace dlib
{
    namespace
    {
        std::atomic<bool>& dnn_prefer_fastest_algo (
        )
        {
            static std::atomic<bool> var(true);
            return var;
        }
    }

    bool dnn_prefer_fastest_algorithms (
    )
    {
        return dnn_prefer_fastest_algo();
    }

    void set_dnn_prefer_fastest_algorithms(
    )
    {
        dnn_prefer_fastest_algo() = true;
    }

    void set_dnn_prefer_smallest_algorithms(
    )
    {
        dnn_prefer_fastest_algo() = false;
    }

#ifndef DLIB_USE_METAL
    // The Metal backend of metal_dlib.h in a build without DLIB_USE_METAL: no device, so
    // that programs can ask unconditionally.
    namespace metal
    {
        bool is_compiled_in () { return false; }
        bool is_available () { return false; }
        std::string unavailable_reason () { return "dlib was built without DLIB_USE_METAL"; }
        std::string device_name () { return std::string(); }
        std::shared_ptr<float> allocate (size_t)
        {
            throw metal_error("A Metal buffer was asked for, but dlib was built without DLIB_USE_METAL.");
        }
        void synchronize ()
        {
            throw metal_error("Metal synchronize was called, but dlib was built without DLIB_USE_METAL.");
        }
        void blit_copy (float*, const float*, size_t)
        {
            throw metal_error("Metal blit_copy was called, but dlib was built without DLIB_USE_METAL.");
        }
        size_t live_buffers () { return 0; }
    }
#endif

#ifndef DLIB_USE_NPU
    // The run-time control of npu_dlib.h in a build without DLIB_USE_NPU: no device, nothing
    // routed, so that programs can call it unconditionally.
    namespace npu
    {
        bool is_compiled_in () { return false; }
        bool is_available () { return false; }
        std::string unavailable_reason () { return "dlib was built without DLIB_USE_NPU"; }
        std::string device_name () { return std::string(); }
        std::vector<std::string> device_names () { return std::vector<std::string>(); }
        bool uses_half_precision () { return false; }
        bool has_single_precision_device () { return false; }
        void restrict_to_device (int) {}
        std::string runtime_version () { return std::string(); }
        void set_routing (routing) {}
        routing get_routing () { return routing::never; }
        void set_synchronous_compilation (bool) {}
        exact_scope::exact_scope () {}
        exact_scope::~exact_scope () {}
        std::vector<op_statistics> get_statistics () { return std::vector<op_statistics>(); }
        void reset_statistics () {}
        void print_status (std::ostream& out)
        {
#ifdef DLIB_USE_CUDA
            out << "dlib NPU routing\n  build    : without DLIB_USE_NPU, primary target CUDA\n";
#else
            out << "dlib NPU routing\n  build    : without DLIB_USE_NPU, primary target host\n";
#endif
        }
    }
#endif
}

namespace dlib { namespace tt
{

// ----------------------------------------------------------------------------------------

    void inverse_norms (
        resizable_tensor& invnorms,
        const tensor& data,
        const double eps
    )
    {
        DLIB_METAL_UNPORTED("inverse_norms", invnorms, data);
#ifdef DLIB_USE_CUDA
        cuda::inverse_norms(invnorms, data, eps);
#else
        DLIB_NPU_TRY(inverse_norms(invnorms, data, eps));
        invnorms = reciprocal(sqrt(sum_cols(squared(mat(data))) + eps));
#endif
    }

    void inverse_sqrt(
        tensor& dest,
        const tensor& src,
        const double eps
    )
    {
        DLIB_METAL_UNPORTED("inverse_sqrt", dest, src);
        DLIB_CASSERT(dest.size() == src.size());

#ifdef DLIB_USE_CUDA
        cuda::inverse_sqrt(dest, src, eps);
#else
        DLIB_NPU_TRY(inverse_sqrt(dest, src, eps));
        dest = reciprocal(sqrt(mat(src) + eps));
#endif
    }

    void divide_by_sqrt(
        tensor& dest,
        const tensor& num,
        const tensor& sqnorm,
        const double eps
    )
    {
        DLIB_METAL_UNPORTED("divide_by_sqrt", dest, num, sqnorm);
        DLIB_CASSERT(dest.size() == num.size() && dest.size() == sqnorm.size());

#ifdef DLIB_USE_CUDA
        cuda::divide_by_sqrt(dest, num, sqnorm, eps);
#else
        DLIB_NPU_TRY(divide_by_sqrt(dest, num, sqnorm, eps));
        cpu::divide_by_sqrt(dest, num, sqnorm, eps);
#endif
    }

    void dot_prods (
        resizable_tensor& out,
        const tensor& lhs,
        const tensor& rhs
    )
    {
        DLIB_METAL_UNPORTED("dot_prods", out, lhs, rhs);
#ifdef DLIB_USE_CUDA
        cuda::dot_prods(out, lhs, rhs);
#else
        DLIB_NPU_TRY(dot_prods(out, lhs, rhs));
        out = sum_cols(pointwise_multiply(mat(lhs), mat(rhs))); 
#endif
    }

    void dot_prods (
        bool add_to,
        tensor& out,
        const tensor& lhs,
        const tensor& rhs
    )
    {
        DLIB_METAL_UNPORTED("dot_prods", out, lhs, rhs);
#ifdef DLIB_USE_CUDA
        cuda::dot_prods(add_to, out, lhs, rhs);
#else
        DLIB_NPU_TRY(dot_prods(add_to, out, lhs, rhs));
        if (add_to)
            out += sum_cols(pointwise_multiply(mat(lhs), mat(rhs))); 
        else
            out = sum_cols(pointwise_multiply(mat(lhs), mat(rhs))); 
#endif
    }

    void scale_columns (
        tensor& out,
        const tensor& m,
        const tensor& v
    )
    {
        DLIB_METAL_UNPORTED("scale_columns", out, m, v);
        DLIB_CASSERT(have_same_dimensions(out,m));
        DLIB_CASSERT(is_vector(v));
        if (m.size() == 0 && v.size() == 0)
            return;
        DLIB_CASSERT(m.size() != 0);
        DLIB_CASSERT(m.size()/m.num_samples() == v.size());

#ifdef DLIB_USE_CUDA
        cuda::scale_columns(out, m, v);
#else
        DLIB_NPU_TRY(scale_columns(out, m, v));
        out = scale_columns(mat(m), mat(v));
#endif
    }

    void scale_rows (
        tensor& out,
        const tensor& m,
        const tensor& v
    )
    {
        DLIB_METAL_UNPORTED("scale_rows", out, m, v);
        DLIB_CASSERT(have_same_dimensions(out,m));
        DLIB_CASSERT(is_vector(v));
        if (m.size() == 0 && v.size() == 0)
            return;
        DLIB_CASSERT(m.size() != 0);
        DLIB_CASSERT(m.num_samples() == static_cast<long long>(v.size()));

#ifdef DLIB_USE_CUDA
        cuda::scale_rows(out, m, v);
#else
        DLIB_NPU_TRY(scale_rows(out, m, v));
        out = scale_rows(mat(m), mat(v));
#endif
    }

    void scale_rows2 (
        float beta, 
        tensor& out,
        const tensor& m1,
        const tensor& m2,
        const tensor& v1,
        const tensor& v2
    )
    {
        DLIB_METAL_UNPORTED("scale_rows2", out, m1, m2, v1, v2);
        DLIB_CASSERT(have_same_dimensions(out,m1));
        DLIB_CASSERT(have_same_dimensions(out,m2));
        DLIB_CASSERT(have_same_dimensions(v1,v2));
        DLIB_CASSERT(is_vector(mat(v1))); 
        DLIB_CASSERT(static_cast<long long>(v1.size()) == m1.num_samples());

#ifdef DLIB_USE_CUDA
        cuda::scale_rows2(beta, out, m1, m2, v1, v2);
#else
        DLIB_NPU_TRY(scale_rows2(beta, out, m1, m2, v1, v2));
        if (beta == 0)
            out = scale_rows(mat(m1) - scale_rows(mat(m2),mat(v1)), mat(v2));
        else
            out = beta*mat(out) + scale_rows(mat(m1) - scale_rows(mat(m2),mat(v1)), mat(v2));
#endif
    }

// ----------------------------------------------------------------------------------------

    void exp (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("exp", dest, src);
        DLIB_CASSERT(dest.size() == src.size());

#ifdef DLIB_USE_CUDA
        cuda::exp(dest,src);
#else
        DLIB_NPU_TRY(exp(dest, src));
        dest = exp(mat(src));
#endif
    }

// ----------------------------------------------------------------------------------------

    void log (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("log", dest, src);
        DLIB_CASSERT(dest.size() == src.size());

#ifdef DLIB_USE_CUDA
        cuda::log(dest,src);
#else
        DLIB_NPU_TRY(log(dest, src));
        dest = log(mat(src));
#endif
    }

// ----------------------------------------------------------------------------------------

    void log10 (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("log10", dest, src);
        DLIB_CASSERT(dest.size() == src.size());

#ifdef DLIB_USE_CUDA
        cuda::log10(dest,src);
#else
        DLIB_NPU_TRY(log10(dest, src));
        dest = log10(mat(src));
#endif
    }

// ----------------------------------------------------------------------------------------

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
        DLIB_METAL_TRY(gemm(beta, dest, alpha, lhs, trans_lhs, rhs, trans_rhs, mode), "gemm", dest, lhs, rhs);
#ifdef DLIB_USE_CUDA
        cuda::gemm(beta, dest, alpha, lhs, trans_lhs, rhs, trans_rhs, mode);
#else
        DLIB_NPU_TRY(gemm(beta, dest, alpha, lhs, trans_lhs, rhs, trans_rhs, mode));
        if (mode == operation_mode::CHANNEL_WISE)
        {
            if (beta != 0)
            {
                if (trans_lhs && trans_rhs)
                    dest = alpha * trans(mat(lhs)) * trans(mat(rhs)) + beta * mat(dest);
                else if (!trans_lhs && trans_rhs)
                    dest = alpha * mat(lhs) * trans(mat(rhs)) + beta * mat(dest);
                else if (trans_lhs && !trans_rhs)
                    dest = alpha * trans(mat(lhs)) * mat(rhs) + beta * mat(dest);
                else
                    dest = alpha * mat(lhs) * mat(rhs) + beta * mat(dest);
            }
            else
            {
                if (trans_lhs && trans_rhs)
                    dest = alpha * trans(mat(lhs)) * trans(mat(rhs));
                else if (!trans_lhs && trans_rhs)
                    dest = alpha * mat(lhs) * trans(mat(rhs));
                else if (trans_lhs && !trans_rhs)
                    dest = alpha * trans(mat(lhs)) * mat(rhs);
                else
                    dest = alpha * mat(lhs) * mat(rhs);
            }
        }
        else if (mode == operation_mode::PLANE_WISE)
        {
            // The cut of the operands into matrices is shared with the Metal path.
            const gemm_planes p = plan_gemm_planes(dest, lhs, rhs);

            for (long b = 0; b < p.num_samples; ++b)
            {
                for (long c = 0; c < p.num_channels; ++c)
                {
                    auto lhs_slice = alias_tensor(p.lhs_rows, p.lhs_cols)(lhs, p.lhs_offset(b, c));
                    auto rhs_slice = alias_tensor(p.rhs_rows, p.rhs_cols)(rhs, p.rhs_offset(b, c));
                    auto dest_slice = alias_tensor(p.dest_rows, p.dest_cols)(dest, p.dest_offset(b, c));

                    if (beta != 0)
                    {
                        if (trans_lhs && trans_rhs)
                            dest_slice = alpha * trans(mat(lhs_slice)) * trans(mat(rhs_slice)) + beta * mat(dest_slice);
                        else if (!trans_lhs && trans_rhs)
                            dest_slice = alpha * mat(lhs_slice) * trans(mat(rhs_slice)) + beta * mat(dest_slice);
                        else if (trans_lhs && !trans_rhs)
                            dest_slice = alpha * trans(mat(lhs_slice)) * mat(rhs_slice) + beta * mat(dest_slice);
                        else
                            dest_slice = alpha * mat(lhs_slice) * mat(rhs_slice) + beta * mat(dest_slice);
                    }
                    else
                    {
                        if (trans_lhs && trans_rhs)
                            dest_slice = alpha * trans(mat(lhs_slice)) * trans(mat(rhs_slice));
                        else if (!trans_lhs && trans_rhs)
                            dest_slice = alpha * mat(lhs_slice) * trans(mat(rhs_slice));
                        else if (trans_lhs && !trans_rhs)
                            dest_slice = alpha * trans(mat(lhs_slice)) * mat(rhs_slice);
                        else
                            dest_slice = alpha * mat(lhs_slice) * mat(rhs_slice);
                    }
                }
            }
        }
#endif
    }

// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------

    tensor_rand::
    tensor_rand(
        unsigned long long seed
    ) 
#ifdef DLIB_USE_CUDA
    :rnd(seed){}
#else
    {rnd.set_seed(cast_to_string(seed)); }
#endif

    void tensor_rand::
    fill_gaussian (
        tensor& data,
        float mean,
        float stddev
    )
    {
        DLIB_METAL_UNPORTED("tensor_rand::fill_gaussian", data);
        DLIB_CASSERT(data.size()%2 == 0);
#ifdef DLIB_USE_CUDA
        rnd.fill_gaussian(data, mean, stddev);
#else
        for (auto& x : data) 
            x = rnd.get_random_gaussian()*stddev + mean;
#endif
    }

    void tensor_rand::
    fill_uniform (
        tensor& data
    )
    {
        DLIB_METAL_UNPORTED("tensor_rand::fill_uniform", data);
#ifdef DLIB_USE_CUDA
        rnd.fill_uniform(data);
#else
        for (auto& x : data) 
            x = rnd.get_random_float();
#endif
    }

// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------

    void multiply (
        bool add_to,
        tensor& dest,
        const tensor& src1,
        const tensor& src2
    )
    {
        DLIB_CASSERT(dest.k() == src1.k() && src1.k() == src2.k() &&
            dest.nr() == src1.nr() && src1.nr() == src2.nr() &&
            dest.nc() == src1.nc() && src1.nc() == src2.nc() );
        const long MD = std::max(std::max(dest.num_samples(),src1.num_samples()),src2.num_samples());
        DLIB_CASSERT((dest.num_samples()==1 || dest.num_samples()==MD) &&
                    (src1.num_samples()==1 || src1.num_samples()==MD) &&
                    (src2.num_samples()==1 || src2.num_samples()==MD) );
        DLIB_METAL_TRY(multiply(add_to, dest, src1, src2), "multiply", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::multiply(add_to, dest, src1, src2);
#else
        DLIB_NPU_TRY(multiply(add_to, dest, src1, src2));
        cpu::multiply(add_to, dest, src1, src2);
#endif

    }

    void scale_channels (
        bool add_to,
        tensor& dest,
        const tensor& src,
        const tensor& scales
    )
    {
        DLIB_METAL_UNPORTED("scale_channels", dest, src, scales);
#ifdef DLIB_USE_CUDA
        cuda::scale_channels(add_to, dest, src, scales);
#else
        DLIB_NPU_TRY(scale_channels(add_to, dest, src, scales));
        cpu::scale_channels(add_to, dest, src, scales);
#endif
    }

    void multiply_conv (
        bool add_to,
        tensor& dest,
        const tensor& src1,
        const tensor& src2
    )
    {
        DLIB_METAL_UNPORTED("multiply_conv", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::multiply_conv(add_to, dest, src1, src2);
#else
        DLIB_NPU_TRY(multiply_conv(add_to, dest, src1, src2));
        cpu::multiply_conv(add_to, dest, src1, src2);
#endif
    }

    void multiply_zero_padded (
        bool add_to,
        tensor& dest,
        const tensor& src1,
        const tensor& src2
    )
    {
        DLIB_METAL_UNPORTED("multiply_zero_padded", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::multiply_zero_padded(add_to, dest, src1, src2);
#else
        DLIB_NPU_TRY(multiply_zero_padded(add_to, dest, src1, src2));
        cpu::multiply_zero_padded(add_to, dest, src1, src2);
#endif
    }

// ----------------------------------------------------------------------------------------

    void affine_transform(
        tensor& dest,
        const tensor& src,
        const float A,
        const float B
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src,A,B);
#else
        DLIB_NPU_TRY(affine_transform(dest,src,A,B));
        cpu::affine_transform(dest,src,A,B);
#endif
    }

    void affine_transform(
        tensor& dest,
        const tensor& src,
        const float A
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src,A);
#else
        DLIB_NPU_TRY(affine_transform(dest,src,A,0));
        cpu::affine_transform(dest,src,A,0);
#endif
    }

    void affine_transform(
        tensor& dest,
        const tensor& src1,
        const tensor& src2,
        const float A,
        const float B,
        const float C
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src1,src2,A,B,C);
#else
        DLIB_NPU_TRY(affine_transform(dest,src1,src2,A,B,C));
        cpu::affine_transform(dest,src1,src2,A,B,C);
#endif
    }

    void affine_transform(
        tensor& dest,
        const tensor& src1,
        const tensor& src2,
        const float A,
        const float B
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src1,src2,A,B);
#else
        DLIB_NPU_TRY(affine_transform(dest,src1,src2,A,B,0));
        cpu::affine_transform(dest,src1,src2,A,B,0);
#endif
    }

    void affine_transform(
        tensor& dest,
        const tensor& src1,
        const tensor& src2,
        const tensor& src3,
        const float A,
        const float B,
        const float C,
        const float D
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src1, src2, src3);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src1,src2,src3,A,B,C,D);
#else
        DLIB_NPU_TRY(affine_transform(dest,src1,src2,src3,A,B,C,D));
        cpu::affine_transform(dest,src1,src2,src3,A,B,C,D);
#endif
    }

    void affine_transform_range(
        size_t begin,
        size_t end,
        tensor& dest,
        const tensor& src1,
        const tensor& src2,
        const tensor& src3,
        const float A,
        const float B,
        const float C
    )
    {
        DLIB_METAL_UNPORTED("affine_transform_range", dest, src1, src2, src3);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform_range(begin, end, dest,src1,src2,src3,A,B,C);
#else
        DLIB_NPU_TRY(affine_transform_range(begin, end, dest,src1,src2,src3,A,B,C));
        cpu::affine_transform_range(begin, end, dest,src1,src2,src3,A,B,C);
#endif
    }

    void affine_transform(
        const rectangle& rect,
        tensor& dest, 
        const tensor& src1, 
        const tensor& src2, 
        const tensor& src3, 
        float A, 
        float B,
        float C
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src1, src2, src3);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(rect, dest,src1,src2,src3,A,B,C);
#else
        DLIB_NPU_TRY(affine_transform(rect, dest,src1,src2,src3,A,B,C));
        cpu::affine_transform(rect, dest,src1,src2,src3,A,B,C);
#endif
    }

    void affine_transform(
        tensor& dest,
        const tensor& src1,
        const tensor& src2,
        const tensor& src3,
        const float A,
        const float B,
        const float C
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src1, src2, src3);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform_range(0,dest.size(),dest,src1,src2,src3,A,B,C);
#else
        DLIB_NPU_TRY(affine_transform_range(0,dest.size(),dest,src1,src2,src3,A,B,C));
        cpu::affine_transform_range(0,dest.size(),dest,src1,src2,src3,A,B,C);
#endif
    }

// ----------------------------------------------------------------------------------------

    void affine_transform(
        tensor& dest,
        const tensor& src,
        const tensor& A,
        const tensor& B
    )
    {
        DLIB_METAL_UNPORTED("affine_transform", dest, src, A, B);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform(dest,src,A,B);
#else
        DLIB_NPU_TRY(affine_transform(dest,src,A,B));
        cpu::affine_transform(dest,src,A,B);
#endif
    }

// ----------------------------------------------------------------------------------------

    void affine_transform_conv(
        tensor& dest,
        const tensor& src,
        const tensor& A,
        const tensor& B
    )
    {
        DLIB_METAL_UNPORTED("affine_transform_conv", dest, src, A, B);
#ifdef DLIB_USE_CUDA
        cuda::affine_transform_conv(dest,src,A,B);
#else
        DLIB_NPU_TRY(affine_transform_conv(dest,src,A,B));
        cpu::affine_transform_conv(dest,src,A,B);
#endif
    }

// ----------------------------------------------------------------------------------------

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
        DLIB_METAL_UNPORTED("compute_adam_update", s, m, v, params, params_grad);
#ifdef DLIB_USE_CUDA
        cuda::compute_adam_update(begin, end, s, m, v, t, learning_rate, weight_decay, momentum1,
            momentum2, params, params_grad);
#else
        DLIB_NPU_TRY(compute_adam_update(begin, end, s, m, v, t, learning_rate, weight_decay, momentum1,
            momentum2, params, params_grad));
        cpu::compute_adam_update(begin, end, s, m, v, t, learning_rate, weight_decay, momentum1,
            momentum2, params, params_grad);
#endif
    }

// ----------------------------------------------------------------------------------------

    void compute_adam_atan2_update (
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
        DLIB_METAL_UNPORTED("compute_adam_atan2_update", s, m, v, params, params_grad);
#ifdef DLIB_USE_CUDA
        cuda::compute_adam_atan2_update(begin, end, s, m, v, t, learning_rate, weight_decay,
            momentum1, momentum2, params, params_grad);
#else
        DLIB_NPU_TRY(compute_adam_atan2_update(begin, end, s, m, v, t, learning_rate, weight_decay,
            momentum1, momentum2, params, params_grad));
        cpu::compute_adam_atan2_update(begin, end, s, m, v, t, learning_rate, weight_decay,
            momentum1, momentum2, params, params_grad);
#endif
    }

// ----------------------------------------------------------------------------------------

    void batch_normalize_inference (
        const double eps,
        resizable_tensor& dest,
        const tensor& src,
        const tensor& gamma, 
        const tensor& beta,
        const tensor& running_means,
        const tensor& running_variances
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize_inference", dest, src, gamma, beta, running_means, running_variances);
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize_inference(eps,dest,src,gamma,beta,running_means,running_variances);
#else
        DLIB_NPU_TRY(batch_normalize_inference(eps,dest,src,gamma,beta,running_means,running_variances));
        cpu::batch_normalize_inference(eps,dest,src,gamma,beta,running_means,running_variances);
#endif
    }

    void batch_normalize (
        const double eps,
        resizable_tensor& dest,
        resizable_tensor& means,
        resizable_tensor& vars,
        const double averaging_factor,
        resizable_tensor& running_means,
        resizable_tensor& running_variances,
        const tensor& src,
        const tensor& gamma, 
        const tensor& beta 
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize", dest, means, vars, running_means, running_variances, src, gamma, beta);
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize(eps,dest,means,vars,averaging_factor,running_means,running_variances,src,gamma,beta);
#else
        DLIB_NPU_TRY(batch_normalize(eps,dest,means,vars,averaging_factor,running_means,running_variances,
            src,gamma,beta));
        cpu::batch_normalize(eps,dest,means,vars,averaging_factor,running_means,running_variances,src,gamma,beta);
#endif
    }

    void batch_normalize_gradient (
        const double eps,
            const tensor& gradient_input,
            const tensor& means,
            const tensor& invstds,
            const tensor& src,
            const tensor& gamma,
            tensor& src_grad,
            tensor& gamma_grad, 
            tensor& beta_grad 
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize_gradient", gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
             
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
#else
        DLIB_NPU_TRY(batch_normalize_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad, gamma_grad,
            beta_grad));
        cpu::batch_normalize_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
#endif
    }

// ----------------------------------------------------------------------------------------

    void batch_normalize_conv_inference (
        const double eps,
        resizable_tensor& dest,
        const tensor& src,
        const tensor& gamma, 
        const tensor& beta,
        const tensor& running_means,
        const tensor& running_variances
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize_conv_inference", dest, src, gamma, beta, running_means, running_variances);
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize_conv_inference(eps,dest,src,gamma,beta,running_means,running_variances);
#else
        DLIB_NPU_TRY(batch_normalize_conv_inference(eps,dest,src,gamma,beta,running_means,running_variances));
        cpu::batch_normalize_conv_inference(eps,dest,src,gamma,beta,running_means,running_variances);
#endif
    }

    void batch_normalize_conv (
        const double eps,
        resizable_tensor& dest,
        resizable_tensor& means,
        resizable_tensor& vars,
        const double averaging_factor,
        resizable_tensor& running_means,
        resizable_tensor& running_variances,
        const tensor& src,
        const tensor& gamma, 
        const tensor& beta 
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize_conv", dest, means, vars, running_means, running_variances, src, gamma, beta);
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize_conv(eps,dest,means,vars,averaging_factor,running_means,running_variances,src,gamma,beta);
#else
        DLIB_NPU_TRY(batch_normalize_conv(eps,dest,means,vars,averaging_factor,running_means,running_variances,
            src,gamma,beta));
        cpu::batch_normalize_conv(eps,dest,means,vars,averaging_factor,running_means,running_variances,src,gamma,beta);
#endif
    }

    void batch_normalize_conv_gradient (
        const double eps,
        const tensor& gradient_input,
        const tensor& means,
        const tensor& invstds,
        const tensor& src,
        const tensor& gamma,
        tensor& src_grad,
        tensor& gamma_grad, 
        tensor& beta_grad 
    )
    {
        DLIB_METAL_UNPORTED("batch_normalize_conv_gradient", gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
             
#ifdef DLIB_USE_CUDA
        cuda::batch_normalize_conv_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
#else
        DLIB_NPU_TRY(batch_normalize_conv_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad,
            gamma_grad, beta_grad));
        cpu::batch_normalize_conv_gradient(eps,gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad);
#endif
    }

// ----------------------------------------------------------------------------------------

    void layer_normalize (
        const double eps,
        resizable_tensor& dest,
        resizable_tensor& means,
        resizable_tensor& vars,
        const tensor& src,
        const tensor& gamma,
        const tensor& beta
    )
    {
        DLIB_METAL_UNPORTED("layer_normalize", dest, means, vars, src, gamma, beta);
#ifdef DLIB_USE_CUDA
        cuda::layer_normalize(eps, dest, means, vars, src, gamma, beta);
#else
        DLIB_NPU_TRY(layer_normalize(eps, dest, means, vars, src, gamma, beta));
        cpu::layer_normalize(eps, dest, means, vars, src, gamma, beta);
#endif
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
        DLIB_METAL_UNPORTED("layer_normalize_gradient", gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad, dmeans, dvars);
#ifdef DLIB_USE_CUDA
        cuda::layer_normalize_gradient(eps, gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad, dmeans, dvars);
#else
        DLIB_NPU_TRY(layer_normalize_gradient(eps, gradient_input, means, invstds, src, gamma, src_grad, gamma_grad,
            beta_grad, dmeans, dvars));
        cpu::layer_normalize_gradient(eps, gradient_input, means, invstds, src, gamma, src_grad, gamma_grad, beta_grad, dmeans, dvars);
#endif
    }

// ----------------------------------------------------------------------------------------

    void rms_normalize(
        const double eps,
        resizable_tensor& dest,
        resizable_tensor& scale,
        const tensor& src,
        const tensor& gamma,
        bool axis_is_nc
    )
    {            
        DLIB_METAL_UNPORTED("rms_normalize", dest, scale, src, gamma);
#ifdef DLIB_USE_CUDA
        cuda::rms_normalize(eps, dest, scale, src, gamma, axis_is_nc);
#else
        DLIB_NPU_TRY(rms_normalize(eps, dest, scale, src, gamma, axis_is_nc));
        cpu::rms_normalize(eps, dest, scale, src, gamma, axis_is_nc);
#endif
    }

    void rms_normalize_gradient(
        const tensor& gradient_input,
        const tensor& scale,
        const tensor& src,
        const tensor& gamma,
        tensor& src_grad,
        tensor& gamma_grad,
        resizable_tensor& dscale,
        bool axis_is_nc
    )
    {            
        DLIB_METAL_UNPORTED("rms_normalize_gradient", gradient_input, scale, src, gamma, src_grad, gamma_grad, dscale);
#ifdef DLIB_USE_CUDA
        cuda::rms_normalize_gradient(gradient_input, scale, src, gamma, src_grad, gamma_grad, dscale, axis_is_nc);
#else
        DLIB_NPU_TRY(rms_normalize_gradient(gradient_input, scale, src, gamma, src_grad, gamma_grad, dscale,
            axis_is_nc));
        cpu::rms_normalize_gradient(gradient_input, scale, src, gamma, src_grad, gamma_grad, dscale, axis_is_nc);
#endif
    }

// ----------------------------------------------------------------------------------------

    void threshold (
        tensor& data,
        float thresh
    )
    {
        DLIB_METAL_UNPORTED("threshold", data);
#ifdef DLIB_USE_CUDA
        cuda::threshold(data,thresh);
#else
        DLIB_NPU_TRY(threshold(data,thresh));
        cpu::threshold(data,thresh);
#endif
    }

    void dot (
        const tensor& a,
        const tensor& b,
        tensor& result,
        size_t idx
    )
    {
        DLIB_METAL_UNPORTED("dot", a, b, result);
#ifdef DLIB_USE_CUDA
        cuda::dot(a,b,result,idx);
#else
        DLIB_NPU_TRY(dot(a,b,result,idx));
        cpu::dot(a,b,result,idx);
#endif
    }

// ----------------------------------------------------------------------------------------

    void add(
        float beta,
        tensor& dest,
        float alpha,
        const tensor& src
    )
    {
        DLIB_METAL_TRY(add(beta,dest,alpha,src), "add", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::add(beta,dest,alpha,src);
#else
        DLIB_NPU_TRY(add(beta,dest,alpha,src));
        cpu::add(beta,dest,alpha,src);
#endif
    }

// ----------------------------------------------------------------------------------------

    void add (
        tensor& dest,
        const tensor& src1,
        const tensor& src2
    )
    {
        DLIB_METAL_TRY(add(dest, src1, src2), "add", dest, src1, src2);
#ifdef DLIB_USE_CUDA
        cuda::add(dest, src1, src2);
#else
        DLIB_NPU_TRY(add(dest, src1, src2));
        cpu::add(dest, src1, src2);
#endif
    }

// ----------------------------------------------------------------------------------------

    void assign_conv_bias_gradient (
        tensor& grad,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("assign_conv_bias_gradient", grad, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::assign_conv_bias_gradient(grad,gradient_input);
#else
        DLIB_NPU_TRY(assign_conv_bias_gradient(grad,gradient_input));
        cpu::assign_conv_bias_gradient(grad,gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void assign_bias_gradient (
        tensor& grad,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("assign_bias_gradient", grad, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::assign_bias_gradient(grad,gradient_input);
#else
        DLIB_NPU_TRY(assign_bias_gradient(grad,gradient_input));
        cpu::assign_bias_gradient(grad,gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void softmax(
        tensor& dest,
        const tensor& src,
        operation_mode mode
    )
    {
        DLIB_METAL_TRY(softmax(dest, src, mode), "softmax", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::softmax(dest, src, mode);
#else
        DLIB_NPU_TRY(softmax(dest, src, mode));
        cpu::softmax(dest, src, mode);
#endif
    }

    void softmax_gradient(
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input,
        operation_mode mode
    )
    {
        DLIB_METAL_UNPORTED("softmax_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::softmax_gradient(grad, dest, gradient_input, mode);
#else
        DLIB_NPU_TRY(softmax_gradient(grad, dest, gradient_input, mode));
        cpu::softmax_gradient(grad, dest, gradient_input, mode);
#endif
    }

// ----------------------------------------------------------------------------------------

    void softmax_all (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("softmax_all", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::softmax_all(dest,src);
#else
        DLIB_NPU_TRY(softmax_all(dest,src));
        cpu::softmax_all(dest,src);
#endif
    }

    void softmax_all_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("softmax_all_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::softmax_all_gradient(grad, dest, gradient_input);
#else
        DLIB_NPU_TRY(softmax_all_gradient(grad, dest, gradient_input));
        cpu::softmax_all_gradient(grad, dest, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void sigmoid (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("sigmoid", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::sigmoid(dest,src);
#else
        DLIB_NPU_TRY(sigmoid(dest,src));
        cpu::sigmoid(dest,src);
#endif
    }

    void sigmoid_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("sigmoid_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::sigmoid_gradient(grad, dest, gradient_input);
#else
        DLIB_NPU_TRY(sigmoid_gradient(grad, dest, gradient_input));
        cpu::sigmoid_gradient(grad, dest, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void mish (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("mish", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::mish(dest,src);
#else
        DLIB_NPU_TRY(mish(dest,src));
        cpu::mish(dest,src);
#endif
    }

    void mish_gradient (
        tensor& grad,
        const tensor& src,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("mish_gradient", grad, src, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::mish_gradient(grad, src, gradient_input);
#else
        DLIB_NPU_TRY(mish_gradient(grad, src, gradient_input));
        cpu::mish_gradient(grad, src, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void relu (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("relu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::relu(dest,src);
#else
        DLIB_NPU_TRY(relu(dest,src));
        cpu::relu(dest,src);
#endif
    }

    void relu_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("relu_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::relu_gradient(grad, dest, gradient_input);
#else
        DLIB_NPU_TRY(relu_gradient(grad, dest, gradient_input));
        cpu::relu_gradient(grad, dest, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void prelu (
        tensor& dest,
        const tensor& src,
        const tensor& param
    )
    {
        DLIB_METAL_UNPORTED("prelu", dest, src, param);
#ifdef DLIB_USE_CUDA
        cuda::prelu(dest, src, param);
#else
        DLIB_NPU_TRY(prelu(dest, src, param));
        cpu::prelu(dest, src, param);
#endif
    }

    void prelu_gradient (
        tensor& grad,
        const tensor& src,
        const tensor& gradient_input,
        const tensor& param,
        tensor& params_grad 
    )
    {
        DLIB_METAL_UNPORTED("prelu_gradient", grad, src, gradient_input, param, params_grad);
#ifdef DLIB_USE_CUDA
        cuda::prelu_gradient(grad, src, gradient_input, param, params_grad);
#else
        DLIB_NPU_TRY(prelu_gradient(grad, src, gradient_input, param, params_grad));
        cpu::prelu_gradient(grad, src, gradient_input, param, params_grad);
#endif
    }

// ----------------------------------------------------------------------------------------

    void leaky_relu (
        tensor& dest,
        const tensor& src,
        const float alpha
    )
    {
        DLIB_METAL_UNPORTED("leaky_relu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::leaky_relu(dest, src, alpha);
#else
        DLIB_NPU_TRY(leaky_relu(dest, src, alpha));
        cpu::leaky_relu(dest, src, alpha);
#endif
    }

    void leaky_relu_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input,
        const float alpha
    )
    {
        DLIB_METAL_UNPORTED("leaky_relu_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::leaky_relu_gradient(grad, dest, gradient_input, alpha);
#else
        DLIB_NPU_TRY(leaky_relu_gradient(grad, dest, gradient_input, alpha));
        cpu::leaky_relu_gradient(grad, dest, gradient_input, alpha);
#endif
    }

// ----------------------------------------------------------------------------------------

    void tanh (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("tanh", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::tanh(dest,src);
#else
        DLIB_NPU_TRY(tanh(dest,src));
        cpu::tanh(dest,src);
#endif
    }

    void tanh_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("tanh_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::tanh_gradient(grad, dest, gradient_input);
#else
        DLIB_NPU_TRY(tanh_gradient(grad, dest, gradient_input));
        cpu::tanh_gradient(grad, dest, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void clipped_relu (
        tensor& dest,
        const tensor& src,
        const float ceiling
    )
    {
        DLIB_METAL_UNPORTED("clipped_relu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::clipped_relu(dest, src, ceiling);
#else
        DLIB_NPU_TRY(clipped_relu(dest, src, ceiling));
        cpu::clipped_relu(dest, src, ceiling);
#endif
    }

    void clipped_relu_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input,
        const float ceiling
    )
    {
        DLIB_METAL_UNPORTED("clipped_relu_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::clipped_relu_gradient(grad, dest, gradient_input, ceiling);
#else
        DLIB_NPU_TRY(clipped_relu_gradient(grad, dest, gradient_input, ceiling));
        cpu::clipped_relu_gradient(grad, dest, gradient_input, ceiling);
#endif
    }

// ----------------------------------------------------------------------------------------

    void elu (
        tensor& dest,
        const tensor& src,
        const float alpha
    )
    {
        DLIB_METAL_UNPORTED("elu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::elu(dest, src, alpha);
#else
        DLIB_NPU_TRY(elu(dest, src, alpha));
        cpu::elu(dest, src, alpha);
#endif
    }

    void elu_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input,
        const float alpha
    )
    {
        DLIB_METAL_UNPORTED("elu_gradient", grad, dest, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::elu_gradient(grad, dest, gradient_input, alpha);
#else
        DLIB_NPU_TRY(elu_gradient(grad, dest, gradient_input, alpha));
        cpu::elu_gradient(grad, dest, gradient_input, alpha);
#endif
    }

// ----------------------------------------------------------------------------------------

    void gelu (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_TRY(gelu(dest, src), "gelu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::gelu(dest,src);
#else
        DLIB_NPU_TRY(gelu(dest,src));
        cpu::gelu(dest,src);
#endif
    }

    void gelu_gradient (
        tensor& grad,
        const tensor& src,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("gelu_gradient", grad, src, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::gelu_gradient(grad, src, gradient_input);
#else
        DLIB_NPU_TRY(gelu_gradient(grad, src, gradient_input));
        cpu::gelu_gradient(grad, src, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void smelu (
        tensor& dest,
        const tensor& src,
        const float beta
    )
    {
        DLIB_METAL_UNPORTED("smelu", dest, src);
        DLIB_CASSERT(beta > 0);
#ifdef DLIB_USE_CUDA
        cuda::smelu(dest, src, beta);
#else
        DLIB_NPU_TRY(smelu(dest, src, beta));
        cpu::smelu(dest, src, beta);
#endif
    }

    void smelu_gradient (
        tensor& grad,
        const tensor& dest,
        const tensor& gradient_input,
        const float beta
    )
    {
        DLIB_METAL_UNPORTED("smelu_gradient", grad, dest, gradient_input);
        DLIB_CASSERT(beta > 0);
#ifdef DLIB_USE_CUDA
        cuda::smelu_gradient(grad, dest, gradient_input, beta);
#else
        DLIB_NPU_TRY(smelu_gradient(grad, dest, gradient_input, beta));
        cpu::smelu_gradient(grad, dest, gradient_input, beta);
#endif
    }

// ----------------------------------------------------------------------------------------

    void silu (
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_TRY(silu(dest, src), "silu", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::silu(dest,src);
#else
        DLIB_NPU_TRY(silu(dest,src));
        cpu::silu(dest,src);
#endif
    }

    void silu_gradient (
        tensor& grad,
        const tensor& src,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("silu_gradient", grad, src, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::silu_gradient(grad, src, gradient_input);
#else
        DLIB_NPU_TRY(silu_gradient(grad, src, gradient_input));
        cpu::silu_gradient(grad, src, gradient_input);
#endif
    }

// ----------------------------------------------------------------------------------------

    void resize_bilinear (
        tensor& dest,
        long dest_row_stride,
        long dest_channel_stride,
        const tensor& src,
        long src_row_stride,
        long src_channel_stride
    )
    {
        DLIB_METAL_UNPORTED("resize_bilinear", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::resize_bilinear(dest,dest_row_stride,dest_channel_stride, src,src_row_stride,src_channel_stride);
#else
        DLIB_NPU_TRY(resize_bilinear(dest,dest_row_stride,dest_channel_stride, src,src_row_stride,src_channel_stride));
        cpu::resize_bilinear(dest,dest_row_stride,dest_channel_stride, src,src_row_stride,src_channel_stride);
#endif
    }

    void resize_bilinear_gradient (
        tensor& grad,
        long grad_row_stride,
        long grad_channel_stride,
        const tensor& gradient_input,
        long gradient_input_row_stride,
        long gradient_input_channel_stride
    )
    {
        DLIB_METAL_UNPORTED("resize_bilinear_gradient", grad, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::resize_bilinear_gradient(grad,grad_row_stride,grad_channel_stride,  gradient_input,gradient_input_row_stride,gradient_input_channel_stride);
#else
        DLIB_NPU_TRY(resize_bilinear_gradient(grad,grad_row_stride,grad_channel_stride,  gradient_input,
            gradient_input_row_stride,gradient_input_channel_stride));
        cpu::resize_bilinear_gradient(grad,grad_row_stride,grad_channel_stride,  gradient_input,gradient_input_row_stride,gradient_input_channel_stride);
#endif
    }

// ------------------------------------------------------------------------------------

    void reorg (
        bool add_to,
        tensor& dest,
        const int row_stride,
        const int col_stride,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("reorg", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::reorg(add_to, dest, row_stride, col_stride, src);
#else
        DLIB_NPU_TRY(reorg(add_to, dest, row_stride, col_stride, src));
        cpu::reorg(add_to, dest, row_stride, col_stride, src);
#endif
    }

    void reorg_gradient (
        bool add_to,
        tensor& grad,
        const int row_stride,
        const int col_stride,
        const tensor& gradient_input
    )
    {
        DLIB_METAL_UNPORTED("reorg_gradient", grad, gradient_input);
#ifdef DLIB_USE_CUDA
        cuda::reorg_gradient(add_to, grad, row_stride, col_stride, gradient_input);
#else
        DLIB_NPU_TRY(reorg_gradient(add_to, grad, row_stride, col_stride, gradient_input));
        cpu::reorg_gradient(add_to, grad, row_stride, col_stride, gradient_input);
#endif
    }

// ------------------------------------------------------------------------------------

    void copy_tensor(
            bool add_to,
            tensor& dest,
            size_t dest_k_offset,
            const tensor& src,
            size_t src_k_offset,
            size_t count_k
    )
    {
        DLIB_METAL_UNPORTED("copy_tensor", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::copy_tensor(add_to, dest, dest_k_offset, src, src_k_offset, count_k);
#else
        DLIB_NPU_TRY(copy_tensor(add_to, dest, dest_k_offset, src, src_k_offset, count_k));
        cpu::copy_tensor(add_to, dest, dest_k_offset, src, src_k_offset, count_k);
#endif
    }

// ----------------------------------------------------------------------------------------

    void copy_tensor(
        bool add_to,
        tensor& dest,
        size_t dk, size_t dnr, size_t dnc,
        const tensor& src,
        size_t sk, size_t snr, size_t snc,
        size_t k, size_t nr, size_t nc
    )
    {
        DLIB_METAL_UNPORTED("copy_tensor", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::copy_tensor(add_to, dest, dk, dnr, dnc , src, sk, snr, snc, k, nr, nc);
#else
        DLIB_NPU_TRY(copy_tensor(add_to, dest, dk, dnr, dnc, src, sk, snr, snc, k, nr, nc));
        cpu::copy_tensor(add_to, dest, dk, dnr, dnc, src, sk, snr, snc, k, nr, nc);
#endif
    }

// ----------------------------------------------------------------------------------------

    void inv::
    operator() (
        const tensor& m,
        resizable_tensor& out
    )
    {
        DLIB_METAL_UNPORTED("inv", m, out);
#ifdef DLIB_USE_CUDA
        finv(m,out);
#else
        DLIB_NPU_TRY(inv(m, out));
        out = dlib::inv(mat(m));
#endif
    }

// ----------------------------------------------------------------------------------------

    void transpose(
        bool add_to,
        tensor& dest,
        const tensor& src
    )
    {
        DLIB_METAL_UNPORTED("transpose", dest, src);
#ifdef DLIB_USE_CUDA
        cuda::transpose(add_to, dest, src);
#else
        DLIB_NPU_TRY(transpose(add_to, dest, src));
        cpu::transpose(add_to, dest, src);
#endif
    }

// ----------------------------------------------------------------------------------------

    void embeddings(
        resizable_tensor& dest,
        const tensor& src,
        const tensor& embs
    )
    {
        DLIB_METAL_UNPORTED("embeddings", dest, src, embs);
#ifdef DLIB_USE_CUDA
        cuda::embeddings(dest, src, embs);
#else
        DLIB_NPU_TRY(embeddings(dest, src, embs));
        cpu::embeddings(dest, src, embs);
#endif
    }

    void embeddings_gradient(
        const tensor& prev,
        const tensor& gradient_input,
        tensor& grads,
        const tensor& freqs,
        float learning_rate,
        bool scale
    )
    {
        DLIB_METAL_UNPORTED("embeddings_gradient", prev, gradient_input, grads, freqs);
#ifdef DLIB_USE_CUDA
        cuda::embeddings_gradient(prev, gradient_input, grads, freqs, learning_rate, scale);
#else
        DLIB_NPU_TRY(embeddings_gradient(prev, gradient_input, grads, freqs, learning_rate, scale));
        cpu::embeddings_gradient(prev, gradient_input, grads, freqs, learning_rate, scale);
#endif
    }

// ----------------------------------------------------------------------------------------

    void puzzle_embeddings(
        resizable_tensor& dest,
        const tensor& tokens,
        const tensor& token_table,
        const tensor& puzzle_table,
        float scale
    )
    {
        DLIB_METAL_UNPORTED("puzzle_embeddings", dest, tokens, token_table, puzzle_table);
#ifdef DLIB_USE_CUDA
        cuda::puzzle_embeddings(dest, tokens, token_table, puzzle_table, scale);
#else
        DLIB_NPU_TRY(puzzle_embeddings(dest, tokens, token_table, puzzle_table, scale));
        cpu::puzzle_embeddings(dest, tokens, token_table, puzzle_table, scale);
#endif
    }

    void puzzle_embeddings_gradient(
        const tensor& tokens,
        const tensor& gradient_input,
        tensor& token_grad,
        tensor& puzzle_table,
        float scale,
        float puzzle_rate,
        float puzzle_decay
    )
    {
        DLIB_METAL_UNPORTED("puzzle_embeddings_gradient", tokens, gradient_input, token_grad, puzzle_table);
#ifdef DLIB_USE_CUDA
        cuda::puzzle_embeddings_gradient(tokens, gradient_input, token_grad, puzzle_table,
                                         scale, puzzle_rate, puzzle_decay);
#else
        DLIB_NPU_TRY(puzzle_embeddings_gradient(tokens, gradient_input, token_grad, puzzle_table,
                                        scale, puzzle_rate, puzzle_decay));
        cpu::puzzle_embeddings_gradient(tokens, gradient_input, token_grad, puzzle_table,
                                        scale, puzzle_rate, puzzle_decay);
#endif
    }

// ----------------------------------------------------------------------------------------

    void repeat_channels(
        tensor& dest,
        const tensor& src,
        long repeat_factor
    )
    {
        DLIB_METAL_UNPORTED("repeat_channels", dest, src);
        DLIB_CASSERT(repeat_factor >= 1);
        DLIB_CASSERT(dest.num_samples() == src.num_samples());
        DLIB_CASSERT(dest.k() == src.k() * repeat_factor);
        DLIB_CASSERT(dest.nr() == src.nr() && dest.nc() == src.nc());

        if (repeat_factor == 1) {
            tt::copy_tensor(false, dest, 0, src, 0, src.k());
            return;
        }

#ifdef DLIB_USE_CUDA
        cuda::repeat_channels(dest, src, repeat_factor);
#else
        DLIB_NPU_TRY(repeat_channels(dest, src, repeat_factor));
        cpu::repeat_channels(dest, src, repeat_factor);
#endif
    }

    void accumulate_repeated_channels(
        tensor& dest,
        const tensor& src,
        long repeat_factor
    )
    {
        DLIB_METAL_UNPORTED("accumulate_repeated_channels", dest, src);
        DLIB_CASSERT(repeat_factor >= 1);
        DLIB_CASSERT(dest.num_samples() == src.num_samples());
        DLIB_CASSERT(src.k() == dest.k() * repeat_factor);
        DLIB_CASSERT(dest.nr() == src.nr() && dest.nc() == src.nc());

        if (repeat_factor == 1) {
            tt::copy_tensor(true, dest, 0, src, 0, src.k());
            return;
        }

#ifdef DLIB_USE_CUDA
        cuda::accumulate_repeated_channels(dest, src, repeat_factor);
#else
        DLIB_NPU_TRY(accumulate_repeated_channels(dest, src, repeat_factor));
        cpu::accumulate_repeated_channels(dest, src, repeat_factor);
#endif
    }

// ----------------------------------------------------------------------------------------

    void compute_halt_logits(
        tensor& logits,
        tensor& halt_probs,
        const tensor& state,
        const tensor& halt_params,
        const tensor& active_mask,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim
    )
    {
        DLIB_METAL_UNPORTED("compute_halt_logits", logits, halt_probs, state, halt_params, active_mask);
#ifdef DLIB_USE_CUDA
        cuda::compute_halt_logits(logits, halt_probs, state, halt_params, active_mask,
            batch_size, seq_len, num_channels, feature_dim);
#else
        DLIB_NPU_TRY(compute_halt_logits(logits, halt_probs, state, halt_params, active_mask,
            batch_size, seq_len, num_channels, feature_dim));
        cpu::compute_halt_logits(logits, halt_probs, state, halt_params, active_mask,
            batch_size, seq_len, num_channels, feature_dim);
#endif
    }

    void compute_halt_gradient(
        tensor& grad_params,
        tensor& grad_state,
        const tensor& gradient_input,
        const tensor& state,
        const tensor& halt_probs,
        const tensor& halt_params,
        const tensor& active_mask,
        const tensor& n_steps,
        const tensor& final_state,
        long current_step,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim,
        float ponder_penalty,
        bool gradient_check_mode
    )
    {
        DLIB_METAL_UNPORTED("compute_halt_gradient", grad_params, grad_state, gradient_input, state, halt_probs, halt_params, active_mask, n_steps, final_state);
#ifdef DLIB_USE_CUDA
        cuda::compute_halt_gradient(grad_params, grad_state, gradient_input, state,
            halt_probs, halt_params, active_mask, n_steps, final_state,
            current_step, batch_size, seq_len, num_channels, feature_dim,
            ponder_penalty, gradient_check_mode);
#else
        DLIB_NPU_TRY(compute_halt_gradient(grad_params, grad_state, gradient_input, state,
            halt_probs, halt_params, active_mask, n_steps, final_state,
            current_step, batch_size, seq_len, num_channels, feature_dim,
            ponder_penalty, gradient_check_mode));
        cpu::compute_halt_gradient(grad_params, grad_state, gradient_input, state,
            halt_probs, halt_params, active_mask, n_steps, final_state,
            current_step, batch_size, seq_len, num_channels, feature_dim,
            ponder_penalty, gradient_check_mode);
#endif
    }

    void accumulate_weighted_state(
        tensor& output,
        const tensor& state,
        const tensor& weights,
        const tensor& active_mask,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim
    )
    {
        DLIB_METAL_UNPORTED("accumulate_weighted_state", output, state, weights, active_mask);
#ifdef DLIB_USE_CUDA
        cuda::accumulate_weighted_state(output, state, weights, active_mask,
            batch_size, seq_len, num_channels, feature_dim);
#else
        DLIB_NPU_TRY(accumulate_weighted_state(output, state, weights, active_mask,
            batch_size, seq_len, num_channels, feature_dim));
        cpu::accumulate_weighted_state(output, state, weights, active_mask,
            batch_size, seq_len, num_channels, feature_dim);
#endif
    }
    
// ----------------------------------------------------------------------------------------

    void apply_act_depth_scaling(
        tensor& gradients,
        const tensor& n_steps,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim,
        long max_steps
    )
    {
        DLIB_METAL_UNPORTED("apply_act_depth_scaling", gradients, n_steps);
#ifdef DLIB_USE_CUDA
        cuda::apply_act_depth_scaling(gradients, n_steps, batch_size, seq_len,
            num_channels, feature_dim, max_steps);
#else
        DLIB_NPU_TRY(apply_act_depth_scaling(gradients, n_steps, batch_size, seq_len,
            num_channels, feature_dim, max_steps));
        cpu::apply_act_depth_scaling(gradients, n_steps, batch_size, seq_len,
            num_channels, feature_dim, max_steps);
#endif
    }
    
// ----------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------

    void broadcast_row (
        tensor& dest,
        const tensor& row
    )
    {
        DLIB_METAL_UNPORTED("broadcast_row", dest, row);
#ifdef DLIB_USE_CUDA
        cuda::broadcast_row(dest, row);
#else
        DLIB_NPU_TRY(broadcast_row(dest, row));
        cpu::broadcast_row(dest, row);
#endif
    }

// ----------------------------------------------------------------------------------------

    void act_mark_active(
        tensor& active_mask,
        tensor& active_count,
        const tensor& cumulative_halting,
        float halt_threshold
    )
    {
        DLIB_METAL_UNPORTED("act_mark_active", active_mask, active_count, cumulative_halting);
        DLIB_CASSERT(active_mask.size() == cumulative_halting.size());
        DLIB_CASSERT(active_count.size() == 1);
#ifdef DLIB_USE_CUDA
        cuda::act_mark_active(active_mask, active_count, cumulative_halting, halt_threshold);
#else
        DLIB_NPU_TRY(act_mark_active(active_mask, active_count, cumulative_halting, halt_threshold));
        cpu::act_mark_active(active_mask, active_count, cumulative_halting, halt_threshold);
#endif
    }

// ----------------------------------------------------------------------------------------

    void act_update_halting(
        tensor& cumulative_halting,
        tensor& remainders,
        tensor& n_steps,
        tensor& step_weights,
        const tensor& halt_probs,
        const tensor& active_mask,
        long step,
        long max_steps,
        float halt_threshold
    )
    {
        DLIB_METAL_UNPORTED("act_update_halting", cumulative_halting, remainders, n_steps, step_weights, halt_probs, active_mask);
        DLIB_CASSERT(cumulative_halting.size() == remainders.size());
        DLIB_CASSERT(cumulative_halting.size() == n_steps.size());
        DLIB_CASSERT(cumulative_halting.size() == step_weights.size());
        DLIB_CASSERT(cumulative_halting.size() == halt_probs.size());
        DLIB_CASSERT(cumulative_halting.size() == active_mask.size());
        DLIB_CASSERT(step >= 0 && step < max_steps);
#ifdef DLIB_USE_CUDA
        cuda::act_update_halting(cumulative_halting, remainders, n_steps, step_weights,
            halt_probs, active_mask, step, max_steps, halt_threshold);
#else
        DLIB_NPU_TRY(act_update_halting(cumulative_halting, remainders, n_steps, step_weights,
            halt_probs, active_mask, step, max_steps, halt_threshold));
        cpu::act_update_halting(cumulative_halting, remainders, n_steps, step_weights,
            halt_probs, active_mask, step, max_steps, halt_threshold);
#endif
    }

// ----------------------------------------------------------------------------------------

    void act_gather_final_state(
        tensor& final_state,
        const tensor& step_state,
        const tensor& n_steps,
        long step,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim
    )
    {
        DLIB_METAL_UNPORTED("act_gather_final_state", final_state, step_state, n_steps);
        DLIB_CASSERT(have_same_dimensions(final_state, step_state));
        DLIB_CASSERT(n_steps.size() == (size_t)(batch_size * seq_len));
#ifdef DLIB_USE_CUDA
        cuda::act_gather_final_state(final_state, step_state, n_steps, step,
            batch_size, seq_len, num_channels, feature_dim);
#else
        DLIB_NPU_TRY(act_gather_final_state(final_state, step_state, n_steps, step,
            batch_size, seq_len, num_channels, feature_dim));
        cpu::act_gather_final_state(final_state, step_state, n_steps, step,
            batch_size, seq_len, num_channels, feature_dim);
#endif
    }

// ----------------------------------------------------------------------------------------

    void act_accumulate_output_grad(
        tensor& grad_state,
        const tensor& gradient_input,
        const tensor& step_weights,
        const tensor& active_mask,
        const tensor& n_steps,
        long step,
        long batch_size,
        long seq_len,
        long num_channels,
        long feature_dim
    )
    {
        DLIB_METAL_UNPORTED("act_accumulate_output_grad", grad_state, gradient_input, step_weights, active_mask, n_steps);
        DLIB_CASSERT(have_same_dimensions(grad_state, gradient_input));
        DLIB_CASSERT(step_weights.size() == (size_t)(batch_size * seq_len));
        DLIB_CASSERT(active_mask.size() == step_weights.size());
        DLIB_CASSERT(n_steps.size() == step_weights.size());
#ifdef DLIB_USE_CUDA
        cuda::act_accumulate_output_grad(grad_state, gradient_input, step_weights,
            active_mask, n_steps, step, batch_size, seq_len, num_channels, feature_dim);
#else
        DLIB_NPU_TRY(act_accumulate_output_grad(grad_state, gradient_input, step_weights,
            active_mask, n_steps, step, batch_size, seq_len, num_channels, feature_dim));
        cpu::act_accumulate_output_grad(grad_state, gradient_input, step_weights,
            active_mask, n_steps, step, batch_size, seq_len, num_channels, feature_dim);
#endif
    }

    void apply_rotary_positional_embedding(
        bool is_backward,
        resizable_tensor& data,
        const resizable_tensor& cos_cache,
        const resizable_tensor& sin_cache
    )
    {
        DLIB_METAL_UNPORTED("apply_rotary_positional_embedding", data, cos_cache, sin_cache);
#ifdef DLIB_USE_CUDA
        cuda::apply_rotary_positional_embedding(is_backward, data, cos_cache, sin_cache);
#else
        DLIB_NPU_TRY(apply_rotary_positional_embedding(is_backward, data, cos_cache, sin_cache));
        cpu::apply_rotary_positional_embedding(is_backward, data, cos_cache, sin_cache);
#endif
    }

// ----------------------------------------------------------------------------------------

    void split_heads(
        bool add_to,
        tensor& dst,
        const tensor& src)
    {
        DLIB_METAL_UNPORTED("split_heads", dst, src);
#ifdef DLIB_USE_CUDA
        cuda::split_heads(add_to, dst, src);
#else
        DLIB_NPU_TRY(split_heads(add_to, dst, src));
        cpu::split_heads(add_to, dst, src);
#endif
    }

    void merge_heads(
        bool add_to,
        tensor& dst,
        const tensor& src)
    {
        DLIB_METAL_UNPORTED("merge_heads", dst, src);
#ifdef DLIB_USE_CUDA
        cuda::merge_heads(add_to, dst, src);
#else
        DLIB_NPU_TRY(merge_heads(add_to, dst, src));
        cpu::merge_heads(add_to, dst, src);
#endif
    }

// ----------------------------------------------------------------------------------------

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
        DLIB_METAL_UNPORTED("gguf_matvec", dest, src, weights);
#ifdef DLIB_USE_CUDA
        cuda::gguf_matvec(dest, src, weights, type, rows, cols, row_bytes);
#else
        cpu::gguf_matvec(dest, src, weights, type, rows, cols, row_bytes);
#endif
    }

    void gguf_dequantize (
        tensor& dest,
        const tensor& weights,
        int type,
        long first,
        long count,
        long cols,
        size_t row_bytes
    )
    {
        DLIB_METAL_UNPORTED("gguf_dequantize", dest, weights);
#ifdef DLIB_USE_CUDA
        cuda::gguf_dequantize(dest, weights, type, first, count, cols, row_bytes);
#else
        cpu::gguf_dequantize(dest, weights, type, first, count, cols, row_bytes);
#endif
    }

    void rms_norm_rows (
        tensor& dest,
        const tensor& src,
        const tensor& gamma,
        float eps
    )
    {
        DLIB_METAL_UNPORTED("rms_norm_rows", dest, src, gamma);
#ifdef DLIB_USE_CUDA
        cuda::rms_norm_rows(dest, src, gamma, eps);
#else
        cpu::rms_norm_rows(dest, src, gamma, eps);
#endif
    }

    void silu_multiply (
        tensor& gate,
        const tensor& up
    )
    {
        DLIB_METAL_UNPORTED("silu_multiply", gate, up);
#ifdef DLIB_USE_CUDA
        cuda::silu_multiply(gate, up);
#else
        cpu::silu_multiply(gate, up);
#endif
    }

// ----------------------------------------------------------------------------------------

}}

#endif // DLIB_TeNSOR_TOOLS_CPP_
