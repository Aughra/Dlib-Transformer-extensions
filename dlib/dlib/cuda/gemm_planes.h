// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_GEMM_PLANES_H_
#define DLIB_GEMM_PLANES_H_

/*
    How tt::gemm in operation_mode::PLANE_WISE cuts its three tensors into matrices.  The
    host path of tensor_tools.cpp and the Metal path read the same plan, so that they agree
    on every shape.  The rules are those of tt::gemm, moved here unchanged.
*/

#include "tensor.h"
#include <algorithm>
#include <cstddef>

namespace dlib
{
    namespace tt
    {
        struct gemm_planes
        {
            // The products run for each (sample, channel) of this batch.
            long num_samples = 0;
            long num_channels = 0;

            // A matrix operand is the same for every product of the batch; the others hold
            // one plane per (sample, channel), of rows x cols values.
            bool lhs_is_matrix = false, rhs_is_matrix = false, dest_is_matrix = false;
            long lhs_rows = 0, lhs_cols = 0;
            long rhs_rows = 0, rhs_cols = 0;
            long dest_rows = 0, dest_cols = 0;
            size_t lhs_plane_size = 0, rhs_plane_size = 0, dest_plane_size = 0;

            // Offsets, in values, of the operands of the product (b, c).
            size_t lhs_offset (long b, long c) const { return lhs_is_matrix ? 0 : (b*num_channels + c)*lhs_plane_size; }
            size_t rhs_offset (long b, long c) const { return rhs_is_matrix ? 0 : (b*num_channels + c)*rhs_plane_size; }
            size_t dest_offset (long b, long c) const { return dest_is_matrix ? 0 : (b*num_channels + c)*dest_plane_size; }
        };

        inline gemm_planes plan_gemm_planes (
            const tensor& dest,
            const tensor& lhs,
            const tensor& rhs
        )
        {
            /* Disambiguate the two matrix layouts against the batch geometry of the
               operands: when one of them is unambiguously plane-batched and carries
               the same (num_samples, k), the tensor belongs to that batch and its
               1x1 planes are planes, not the cells of a matrix. */
            auto plane_batched = [](const tensor& t) {
                return t.num_samples() * t.k() > 1 && t.nr() * t.nc() > 1;
            };
            auto shares_batch = [&](const tensor& t) {
                auto same = [&](const tensor& o) {
                    return plane_batched(o)
                        && o.num_samples() == t.num_samples() && o.k() == t.k();
                    };
                return same(lhs) || same(rhs) || same(dest);
            };
            auto is_matrix = [&](const tensor& t) {
                if (t.num_samples() * t.k() == 1) return t.nr() * t.nc() > 1;
                return t.nr() * t.nc() == 1 && !shares_batch(t);
            };

            gemm_planes p;
            p.num_samples = std::min({ lhs.num_samples(), rhs.num_samples(), dest.num_samples() });
            p.num_channels = std::min({ lhs.k(), rhs.k(), dest.k() });
            p.lhs_is_matrix = is_matrix(lhs);
            p.rhs_is_matrix = is_matrix(rhs);
            p.dest_is_matrix = is_matrix(dest);

            if (p.lhs_is_matrix && p.rhs_is_matrix && p.dest_is_matrix) {
                p.num_samples = p.num_channels = 1;
            }

            p.lhs_rows = (p.lhs_is_matrix && lhs.num_samples() > 1) ? lhs.num_samples() : lhs.nr();
            p.lhs_cols = (p.lhs_is_matrix && lhs.k() > 1) ? lhs.k() : lhs.nc();
            p.rhs_rows = (p.rhs_is_matrix && rhs.num_samples() > 1) ? rhs.num_samples() : rhs.nr();
            p.rhs_cols = (p.rhs_is_matrix && rhs.k() > 1) ? rhs.k() : rhs.nc();
            p.dest_rows = (p.dest_is_matrix && dest.num_samples() > 1) ? dest.num_samples() : dest.nr();
            p.dest_cols = (p.dest_is_matrix && dest.k() > 1) ? dest.k() : dest.nc();

            p.lhs_plane_size = p.lhs_rows * p.lhs_cols;
            p.rhs_plane_size = p.rhs_rows * p.rhs_cols;
            p.dest_plane_size = p.dest_rows * p.dest_cols;
            return p;
        }
    }
}

#endif // DLIB_GEMM_PLANES_H_
