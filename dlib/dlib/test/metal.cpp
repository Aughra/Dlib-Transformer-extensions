// Copyright (C) 2026  TortueTech
// License: Boost Software License   See LICENSE.txt for the full license.

#include <dlib/dnn.h>
#include <dlib/data_io/gguf_reader.h>
#include <mach/mach.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tester.h"

/*
    Tests of the Metal backend of dlib tensors (dlib/cuda/metal_dlib.h).

    Every build checks the device tag: a tensor is on the CPU by default, and a build
    without DLIB_USE_METAL refuses a Metal tensor with metal_error.  A build with
    DLIB_USE_METAL also checks, on the Metal device of the Mac, that a Metal tensor keeps
    the values written to it, that the GPU reads and writes the same memory, that copies,
    moves, swaps and views keep the device, that buffers are released, and that the
    tensor_tools functions refuse Metal tensors they cannot compute on Metal and calls that
    mix the two devices.
*/

namespace
{
    using namespace test;
    using namespace dlib;
    using namespace std;

    logger dlog("test.metal");

    template <typename F>
    std::string error_of (F&& f)
    {
        try
        {
            f();
        }
        catch (metal_error& e)
        {
            return e.what();
        }
        return std::string();
    }

    bool contains (const std::string& s, const std::string& part)
    {
        return s.find(part) != std::string::npos;
    }

    void fill_pattern (tensor& t, float base)
    {
        float* p = t.host();
        for (size_t i = 0; i < t.size(); ++i)
            p[i] = base + 0.25f*i - 3.0f*(i%7);
    }

    bool has_pattern (const tensor& t, float base)
    {
        const float* p = t.host();
        for (size_t i = 0; i < t.size(); ++i)
        {
            if (p[i] != base + 0.25f*i - 3.0f*(i%7))
                return false;
        }
        return true;
    }

// ----------------------------------------------------------------------------------------

    void test_default_device ()
    {
        resizable_tensor a(2,3,4,5);
        DLIB_TEST(a.get_tensor_device() == tensor_device::cpu);
        resizable_tensor empty;
        DLIB_TEST(empty.get_tensor_device() == tensor_device::cpu);
        alias_tensor view(1,3,4,5);
        auto v = view(a, 60);
        DLIB_TEST(v.get_tensor_device() == tensor_device::cpu);
        DLIB_TEST(std::string(to_string(tensor_device::cpu)) == "cpu");
        DLIB_TEST(std::string(to_string(tensor_device::metal)) == "metal");

        // Asking for the CPU, where the tensor already is, changes nothing.
        fill_pattern(a, 1);
        a.set_tensor_device(tensor_device::cpu);
        DLIB_TEST(has_pattern(a, 1));
    }

#ifndef DLIB_USE_METAL
    void test_without_metal ()
    {
        DLIB_TEST(!metal::is_compiled_in());
        DLIB_TEST(!metal::is_available());
        DLIB_TEST(contains(metal::unavailable_reason(), "without DLIB_USE_METAL"));
        DLIB_TEST(metal::live_buffers() == 0);

        resizable_tensor a(3,4);
        fill_pattern(a, 2);
        const std::string e = error_of([&]{ a.set_tensor_device(tensor_device::metal); });
        DLIB_TEST_MSG(contains(e, "without DLIB_USE_METAL"), e);
        DLIB_TEST(a.get_tensor_device() == tensor_device::cpu);
        DLIB_TEST(has_pattern(a, 2));
        DLIB_TEST(!error_of([]{ metal::allocate(4); }).empty());
        DLIB_TEST(!error_of([]{ metal::synchronize(); }).empty());
    }
#else

    resizable_tensor metal_tensor (long long n, long long k = 1, long long nr = 1, long long nc = 1)
    {
        resizable_tensor t;
        t.set_tensor_device(tensor_device::metal);
        t.set_size(n, k, nr, nc);
        return t;
    }

    void test_device ()
    {
        DLIB_TEST(metal::is_compiled_in());
        DLIB_TEST_MSG(metal::is_available(), metal::unavailable_reason());
        DLIB_TEST(metal::unavailable_reason().empty());
        DLIB_TEST(!metal::device_name().empty());
        dlog << LINFO << "Metal device: " << metal::device_name();
        metal::synchronize();
    }

    void test_values_read_back ()
    {
        const size_t before = metal::live_buffers();
        {
            resizable_tensor t = metal_tensor(2,3,4,5);
            DLIB_TEST(t.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(t.size() == 120);
            DLIB_TEST(metal::live_buffers() == before + 1);

            fill_pattern(t, 0.5f);
            DLIB_TEST(has_pattern(t, 0.5f));

            // Writes through the other accessors land in the same memory.
            float* w = t.host_write_only();
            for (size_t i = 0; i < t.size(); ++i)
                w[i] = -1.0f*i;
            const tensor& ct = t;
            for (size_t i = 0; i < t.size(); ++i)
                DLIB_TEST(ct.host()[i] == -1.0f*i);

            // Values set through a matrix and through operator= (float).
            t = 7.5f;
            for (auto x : t)
                DLIB_TEST(x == 7.5f);
            matrix<float> m = matrix_cast<float>(randm(2, 60));
            t = m;
            DLIB_TEST(t.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(max(abs(mat(t) - m)) == 0);

            // A tensor that grows gets a new Metal block; one that shrinks keeps its own.
            t.set_size(4,3,4,5);
            DLIB_TEST(t.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(metal::live_buffers() == before + 1);
            fill_pattern(t, 3);
            DLIB_TEST(has_pattern(t, 3));
            t.set_size(1,2);
            DLIB_TEST(metal::live_buffers() == before + 1);

            t.clear();
            DLIB_TEST(t.size() == 0);
            DLIB_TEST(t.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(metal::live_buffers() == before);
        }
        DLIB_TEST(metal::live_buffers() == before);
    }

    // A released tensor gives its memory back to the system: the device counts it no more.
    void test_memory_is_given_back ()
    {
        resizable_tensor warm = metal_tensor(1, 1, 1, 1000);
        const size_t before = metal::allocated_bytes();
        for (int i = 0; i < 10; ++i)
        {
            resizable_tensor t = metal_tensor(1, 1, 1, 12500000);   // 50 MB
            t = 1;
        }
        const size_t after = metal::allocated_bytes();
        DLIB_TEST_MSG(after <= before + 1000000, "the device holds " << (after - before) << " more bytes");
    }

    void test_gpu_sees_the_same_memory ()
    {
        const size_t before = metal::live_buffers();
        {
            resizable_tensor src = metal_tensor(3,5,7,11);
            resizable_tensor dest = metal_tensor(3,5,7,11);
            fill_pattern(src, 4);
            dest = 0;
            metal::blit_copy(dest.host(), src.host(), src.size());
            DLIB_TEST(has_pattern(dest, 4));

            // A range starting inside a buffer, at an offset.
            dest = 0;
            metal::blit_copy(dest.host() + 100, src.host() + 50, 200);
            for (size_t i = 0; i < dest.size(); ++i)
            {
                const float expected = (i >= 100 && i < 300) ? src.host()[i - 50] : 0.0f;
                DLIB_TEST(dest.host()[i] == expected);
            }

            // Memory outside the Metal buffers, a range running past the end of a buffer,
            // and overlapping ranges are refused.
            resizable_tensor cpu(3,5,7,11);
            DLIB_TEST(contains(error_of([&]{ metal::blit_copy(dest.host(), cpu.host(), 10); }),
                "not inside a Metal buffer"));
            DLIB_TEST(contains(error_of([&]{ metal::blit_copy(dest.host() + 1, src.host(), dest.size()); }),
                "not inside a Metal buffer"));
            DLIB_TEST(contains(error_of([&]{ metal::blit_copy(dest.host() + 1, dest.host(), 10); }),
                "overlapping"));
        }
        DLIB_TEST(metal::live_buffers() == before);
    }

    void test_moving_between_devices ()
    {
        const size_t before = metal::live_buffers();
        resizable_tensor t(4,6);
        fill_pattern(t, 9);
        t.set_tensor_device(tensor_device::metal);
        DLIB_TEST(t.get_tensor_device() == tensor_device::metal);
        DLIB_TEST(t.num_samples() == 4 && t.k() == 6);
        DLIB_TEST(has_pattern(t, 9));
        DLIB_TEST(metal::live_buffers() == before + 1);
        // The GPU reads the values the move brought.
        resizable_tensor copy = metal_tensor(4,6);
        metal::blit_copy(copy.host(), t.host(), t.size());
        DLIB_TEST(has_pattern(copy, 9));

        t.set_tensor_device(tensor_device::cpu);
        DLIB_TEST(t.get_tensor_device() == tensor_device::cpu);
        DLIB_TEST(has_pattern(t, 9));
        DLIB_TEST(metal::live_buffers() == before + 1);   // copy is still held

        resizable_tensor empty;
        empty.set_tensor_device(tensor_device::metal);
        DLIB_TEST(empty.get_tensor_device() == tensor_device::metal);
        DLIB_TEST(metal::live_buffers() == before + 1);   // an empty tensor holds no buffer
    }

    void test_copies_moves_and_views ()
    {
        const size_t before = metal::live_buffers();
        {
            resizable_tensor a = metal_tensor(2,3,4);
            fill_pattern(a, 5);

            resizable_tensor b(a);
            DLIB_TEST(b.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(has_pattern(b, 5));
            DLIB_TEST(b.host() != a.host());

            const tensor& as_tensor = a;
            resizable_tensor c(as_tensor);
            DLIB_TEST(c.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(has_pattern(c, 5));

            resizable_tensor d(2,3,4);
            d = a;                                         // assignment takes the device
            DLIB_TEST(d.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(has_pattern(d, 5));
            DLIB_TEST(metal::live_buffers() == before + 4);

            resizable_tensor e(std::move(d));
            DLIB_TEST(e.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(has_pattern(e, 5));
            DLIB_TEST(d.get_tensor_device() == tensor_device::cpu);
            DLIB_TEST(metal::live_buffers() == before + 4);

            resizable_tensor f(2,2);
            fill_pattern(f, 6);
            f.swap(e);
            DLIB_TEST(f.get_tensor_device() == tensor_device::metal && has_pattern(f, 5));
            DLIB_TEST(e.get_tensor_device() == tensor_device::cpu && has_pattern(e, 6));

            // A copy of a CPU tensor into a Metal one takes the CPU device as well.
            resizable_tensor g = metal_tensor(2,2);
            g = e;
            DLIB_TEST(g.get_tensor_device() == tensor_device::cpu && has_pattern(g, 6));

            // A view shares the device and the memory of the tensor it looks into.
            alias_tensor view(1,3,4);
            auto v = view(a, 12);
            DLIB_TEST(v.get_tensor_device() == tensor_device::metal);
            DLIB_TEST(v.host() == a.host() + 12);
            v.host()[0] = 1234;
            DLIB_TEST(a.host()[12] == 1234);

            // memcpy between devices is an explicit copy, and works both ways.
            resizable_tensor h(2,3,4);
            memcpy(h, a);
            DLIB_TEST(max(abs(mat(h) - mat(a))) == 0);
            fill_pattern(h, 8);
            memcpy(a, h);
            DLIB_TEST(has_pattern(a, 8));

            // Serialization reads the values; a deserialized tensor is on the CPU.
            std::ostringstream sout;
            serialize(a, sout);
            std::istringstream sin(sout.str());
            resizable_tensor back;
            deserialize(back, sin);
            DLIB_TEST(back.get_tensor_device() == tensor_device::cpu);
            DLIB_TEST(has_pattern(back, 8));
        }
        DLIB_TEST(metal::live_buffers() == before);
    }

    void test_routing ()
    {
        dlib::rand rnd(1);
        resizable_tensor src = metal_tensor(2,3,4,5);
        resizable_tensor dest = metal_tensor(2,3,4,5);
        resizable_tensor other = metal_tensor(2,3,4,5);
        fill_pattern(src, 1);
        fill_pattern(dest, 2);
        fill_pattern(other, 3);

        // Operations without a Metal version stop, naming themselves, and write nothing.
        const std::string e1 = error_of([&]{ tt::relu(dest, src); });
        DLIB_TEST_MSG(contains(e1, "tt::relu has no Metal version yet"), e1);
        DLIB_TEST(has_pattern(dest, 2));
        const std::vector<std::pair<std::string, std::function<void()>>> ops = {
            {"tt::sigmoid",          [&]{ tt::sigmoid(dest, src); }},
            {"tt::tanh",             [&]{ tt::tanh(dest, src); }},
            {"tt::exp",              [&]{ tt::exp(dest, src); }},
            {"tt::softmax_all",      [&]{ tt::softmax_all(dest, src); }},
            {"tt::gelu_gradient",    [&]{ tt::gelu_gradient(dest, src, other); }},
            {"tt::affine_transform", [&]{ tt::affine_transform(dest, src, 2, 1); }},
            {"tt::log",              [&]{ tt::log(dest, src); }},
            {"tt::tensor_rand::fill_uniform", [&]{ tt::tensor_rand(1).fill_uniform(dest); }},
        };
        for (auto& op : ops)
        {
            const std::string e = error_of(op.second);
            DLIB_TEST_MSG(contains(e, op.first + " has no Metal version yet"), op.first << ": " << e);
        }
        DLIB_TEST(has_pattern(dest, 2));

        // An output still to be sized counts for neither device.
        resizable_tensor scale, out;
        resizable_tensor gamma = metal_tensor(1,3,4,5);
        const std::string e2 = error_of([&]{ tt::rms_normalize(1e-5, out, scale, src, gamma, false); });
        DLIB_TEST_MSG(contains(e2, "tt::rms_normalize has no Metal version yet"), e2);

        // Methods of the classes in tensor_tools.h stop as well.
        tt::tensor_conv conv;
        resizable_tensor filters = metal_tensor(2,3,2,2);
        conv.setup(src, filters, 1, 1, 0, 0);
        resizable_tensor conv_out = metal_tensor(2,2,3,4);
        const std::string e3 = error_of([&]{ conv(false, conv_out, src, filters); });
        DLIB_TEST_MSG(contains(e3, "tt::tensor_conv has no Metal version yet"), e3);

        // A call mixing the two devices is refused, whatever the operation.
        resizable_tensor cpu_dest(2,3,4,5);
        fill_pattern(cpu_dest, 4);
        const std::string e4 = error_of([&]{ tt::relu(cpu_dest, src); });
        DLIB_TEST_MSG(contains(e4, "tt::relu was given tensors on the CPU and tensors on Metal"), e4);
        DLIB_TEST(has_pattern(cpu_dest, 4));
        const std::string e5 = error_of([&]{ tt::add(cpu_dest, src, other); });
        DLIB_TEST_MSG(contains(e5, "tt::add was given tensors on the CPU and tensors on Metal"), e5);
        DLIB_TEST(has_pattern(cpu_dest, 4));

        // CPU tensors take the host path, unchanged.
        resizable_tensor a(2,3,4,5), b(2,3,4,5);
        tt::tensor_rand(rnd.get_random_32bit_number()).fill_gaussian(a);
        tt::relu(b, a);
        for (size_t i = 0; i < a.size(); ++i)
            DLIB_TEST(b.host()[i] == std::max(0.0f, a.host()[i]));
    }

// ----------------------------------------------------------------------------------------
// Parity of the operations of phase 2: the same random operands, a CPU result of
// reference, a Metal result, compared with |metal - cpu| <= atol + rtol*|cpu|.

    struct parity
    {
        double max_abs = 0;
        double max_rel = 0;
        size_t compared = 0;
        size_t failed = 0;
        double max_bound_ratio = 0;   // for gemm: the largest gap over its bound
        double max_cpu_error = -1;    // with an exact reference: the largest gaps of each side
        double max_gpu_error = -1;
        double sum_gap2 = 0;          // against float32 weights: the relative gap of all values
        double sum_norm2 = 0;
    };

    // Tolerances of float32 (see the report in the journal of the fork): 1e-5 absolute and
    // 1e-5 relative, about 84 units in the last place of a value near 1.
    const double default_atol = 1e-5;
    const double default_rtol = 1e-5;

    std::map<std::string, parity>& parity_table ()
    {
        static std::map<std::string, parity> table;
        return table;
    }

    void check_parity (
        const std::string& op,
        const tensor& cpu,
        const tensor& gpu,
        double atol = default_atol,
        double rtol = default_rtol
    )
    {
        DLIB_TEST(cpu.get_tensor_device() == tensor_device::cpu);
        DLIB_TEST(gpu.get_tensor_device() == tensor_device::metal);
        DLIB_TEST(have_same_dimensions(cpu, gpu));
        parity& p = parity_table()[op];
        const float* c = cpu.host();
        const float* g = gpu.host();
        for (size_t i = 0; i < cpu.size(); ++i)
        {
            const double a = std::abs((double)g[i] - (double)c[i]);
            const bool same_special = (std::isnan(c[i]) && std::isnan(g[i])) || (c[i] == g[i]);
            ++p.compared;
            if (same_special)
                continue;
            p.max_abs = std::max(p.max_abs, a);
            // The relative gap is read where it means something: away from 0.
            if (std::abs((double)c[i]) >= 1e-3)
                p.max_rel = std::max(p.max_rel, a/std::abs((double)c[i]));
            if (!(a <= atol + rtol*std::abs((double)c[i])))
            {
                if (p.failed < 5)
                    dlog << LERROR << op << ": value " << i << " cpu " << c[i] << " metal " << g[i];
                ++p.failed;
            }
        }
        DLIB_TEST_MSG(p.failed == 0, op << ": " << p.failed << " values beyond the tolerance");
    }

    resizable_tensor random_cpu (dlib::rand& rnd, long long n, long long k, long long nr, long long nc, float scale = 1)
    {
        resizable_tensor t(n, k, nr, nc);
        for (auto& x : t)
            x = scale*rnd.get_random_gaussian();
        return t;
    }

    resizable_tensor on_metal (const tensor& t)
    {
        resizable_tensor m(t);
        m.set_tensor_device(tensor_device::metal);
        return m;
    }

    struct shape { long long n, k, nr, nc; };

    void test_parity_add (dlib::rand& rnd)
    {
        // add(dest, src1, src2): same shapes, then sources smaller or larger than dest in
        // each dimension (values outside a source count as 0).
        const std::vector<std::vector<shape>> cases = {
            {{2,3,4,5}, {2,3,4,5}, {2,3,4,5}},
            {{1,1,1,1}, {1,1,1,1}, {1,1,1,1}},
            {{3,1,1,1000}, {3,1,1,1000}, {3,1,1,1000}},
            {{2,3,4,5}, {1,3,4,5}, {2,2,3,5}},
            {{2,3,4,5}, {3,4,5,6}, {1,1,1,1}},
            {{4,2,7,3}, {4,1,7,3}, {2,2,2,2}},
        };
        for (auto& c : cases)
        {
            resizable_tensor d(c[0].n, c[0].k, c[0].nr, c[0].nc);
            d = 0;
            resizable_tensor a = random_cpu(rnd, c[1].n, c[1].k, c[1].nr, c[1].nc);
            resizable_tensor b = random_cpu(rnd, c[2].n, c[2].k, c[2].nr, c[2].nc);
            resizable_tensor md = on_metal(d), ma = on_metal(a), mb = on_metal(b);
            tt::add(d, a, b);
            tt::add(md, ma, mb);
            check_parity("add(dest,src1,src2)", d, md);
        }

        // add(beta, dest, alpha, src) for each broadcast the CPU accepts.
        const shape dshape = {3,4,5,6};
        const std::vector<shape> srcs = {{3,4,5,6}, {1,4,1,1}, {1,4,5,6}, {1,1,5,6}, {3,1,1,1}};
        const std::vector<std::pair<float,float>> coefs = {{1,1}, {0,1}, {0.5f,-2}, {1,0}, {0,0}};
        for (auto& s : srcs)
        {
            for (auto& ab : coefs)
            {
                resizable_tensor d = random_cpu(rnd, dshape.n, dshape.k, dshape.nr, dshape.nc);
                resizable_tensor src = random_cpu(rnd, s.n, s.k, s.nr, s.nc);
                resizable_tensor md = on_metal(d), ms = on_metal(src);
                tt::add(ab.first, d, ab.second, src);
                tt::add(ab.first, md, ab.second, ms);
                check_parity("add(beta,dest,alpha,src)", d, md);
            }
        }
    }

    void test_parity_multiply (dlib::rand& rnd)
    {
        // The three cases of cpu::multiply: same sizes, a dest of one sample (sum over the
        // samples), and sources of one sample repeated over the samples of dest.
        const std::vector<std::vector<shape>> cases = {
            {{2,3,4,5}, {2,3,4,5}, {2,3,4,5}},
            {{1,3,4,5}, {6,3,4,5}, {6,3,4,5}},
            {{1,3,4,5}, {1,3,4,5}, {6,3,4,5}},
            {{6,3,4,5}, {1,3,4,5}, {6,3,4,5}},
            {{6,3,4,5}, {6,3,4,5}, {1,3,4,5}},
            {{5,1,1,700}, {1,1,1,700}, {1,1,1,700}},
        };
        for (auto& c : cases)
        {
            for (bool add_to : {false, true})
            {
                resizable_tensor d = random_cpu(rnd, c[0].n, c[0].k, c[0].nr, c[0].nc);
                resizable_tensor a = random_cpu(rnd, c[1].n, c[1].k, c[1].nr, c[1].nc);
                resizable_tensor b = random_cpu(rnd, c[2].n, c[2].k, c[2].nr, c[2].nc);
                resizable_tensor md = on_metal(d), ma = on_metal(a), mb = on_metal(b);
                tt::multiply(add_to, d, a, b);
                tt::multiply(add_to, md, ma, mb);
                check_parity("multiply", d, md);
            }
        }
    }

    void test_parity_softmax (dlib::rand& rnd)
    {
        const std::vector<shape> shapes = {
            {1,1,1,1}, {2,3,4,5}, {3,1000,1,1}, {2,7,1,1}, {1,1,9,513}, {4,5,3,3}, {1,1,1,32000}};
        for (auto& s : shapes)
        {
            for (auto mode : {operation_mode::CHANNEL_WISE, operation_mode::PLANE_WISE})
            {
                for (float scale : {1.0f, 20.0f})
                {
                    resizable_tensor src = random_cpu(rnd, s.n, s.k, s.nr, s.nc, scale);
                    resizable_tensor d(s.n, s.k, s.nr, s.nc);
                    resizable_tensor ms = on_metal(src), md = on_metal(d);
                    tt::softmax(d, src, mode);
                    tt::softmax(md, ms, mode);
                    check_parity(mode == operation_mode::CHANNEL_WISE ? "softmax CHANNEL_WISE" : "softmax PLANE_WISE", d, md);
                    // In place, as the layers of dlib do.
                    tt::softmax(src, src, mode);
                    tt::softmax(ms, ms, mode);
                    check_parity(mode == operation_mode::CHANNEL_WISE ? "softmax CHANNEL_WISE" : "softmax PLANE_WISE", src, ms);
                }
            }
        }
        // A row of -infinity gives zeros along the rows.
        resizable_tensor src(1,2,2,3);
        src = -std::numeric_limits<float>::infinity();
        src.host()[0] = 1;
        resizable_tensor d(1,2,2,3), ms = on_metal(src), md = on_metal(d);
        tt::softmax(d, src, operation_mode::PLANE_WISE);
        tt::softmax(md, ms, operation_mode::PLANE_WISE);
        check_parity("softmax PLANE_WISE", d, md);
    }

    void test_parity_activations (dlib::rand& rnd)
    {
        for (auto s : std::vector<shape>{{1,1,1,1}, {2,3,4,5}, {1,1,1,100003}})
        {
            for (float scale : {1.0f, 4.0f, 30.0f})
            {
                resizable_tensor src = random_cpu(rnd, s.n, s.k, s.nr, s.nc, scale);
                src.host()[0] = 0;
                resizable_tensor d(s.n, s.k, s.nr, s.nc);
                resizable_tensor ms = on_metal(src), md = on_metal(d);
                tt::gelu(d, src);
                tt::gelu(md, ms);
                check_parity("gelu", d, md);
                tt::silu(d, src);
                tt::silu(md, ms);
                check_parity("silu", d, md);
            }
        }
    }

    void test_parity_views (dlib::rand& rnd)
    {
        // Views at an offset inside Metal tensors: the kernels bind the buffer at that offset.
        resizable_tensor big = random_cpu(rnd, 4,3,4,5);
        resizable_tensor mbig = on_metal(big);
        alias_tensor part(1,3,4,5);
        auto a = part(big, 60), b = part(big, 120), out = part(big, 180);
        auto ma = part(mbig, 60), mb = part(mbig, 120), mout = part(mbig, 180);
        tt::add(out, a, b);
        tt::add(mout, ma, mb);
        check_parity("add(dest,src1,src2)", big, mbig);
        tt::gelu(out, a);
        tt::gelu(mout, ma);
        check_parity("gelu", big, mbig);
        tt::softmax(out, b, operation_mode::CHANNEL_WISE);
        tt::softmax(mout, mb, operation_mode::CHANNEL_WISE);
        check_parity("softmax CHANNEL_WISE", big, mbig);
    }

// ----------------------------------------------------------------------------------------
// Parity of tt::gemm.  Both sides sum products in their own order, so the bound is that of
// the error analysis of a dot product of length K in float32 (u = 2^-24): each side is
// within (K+2)*u*S of the exact value, S being what the same computation gives on absolute
// values; the two sides are thus within 2*(K+2)*u*S of each other, times the number of
// products a value accumulates.

    void check_gemm_parity (
        const std::string& op,
        const tensor& cpu,
        const tensor& gpu,
        const tensor& scale,
        double terms
    )
    {
        DLIB_TEST(have_same_dimensions(cpu, gpu) && have_same_dimensions(cpu, scale));
        const double u = std::ldexp(1.0, -24);
        parity& p = parity_table()[op];
        const float* c = cpu.host();
        const float* g = gpu.host();
        const float* s = scale.host();
        for (size_t i = 0; i < cpu.size(); ++i)
        {
            const double a = std::abs((double)g[i] - (double)c[i]);
            const double bound = 2*(terms + 2)*u*s[i] + 1e-30;
            ++p.compared;
            p.max_abs = std::max(p.max_abs, a);
            if (s[i] > 0)
                p.max_rel = std::max(p.max_rel, a/s[i]);
            p.max_bound_ratio = std::max(p.max_bound_ratio, a/bound);
            if (!(a <= bound))
            {
                if (p.failed < 5)
                    dlog << LERROR << op << ": value " << i << " cpu " << c[i] << " metal " << g[i] << " bound " << bound;
                ++p.failed;
            }
        }
        DLIB_TEST_MSG(p.failed == 0, op << ": " << p.failed << " values beyond the bound");
    }

    resizable_tensor absolute (const tensor& t)
    {
        resizable_tensor a(t);
        for (auto& x : a)
            x = std::abs(x);
        return a;
    }

    void run_gemm_case (
        dlib::rand& rnd,
        const std::string& op,
        shape ds, shape ls, shape rs,
        bool tl, bool tr,
        float alpha, float beta,
        operation_mode mode,
        double terms
    )
    {
        resizable_tensor dest = random_cpu(rnd, ds.n, ds.k, ds.nr, ds.nc);
        resizable_tensor lhs = random_cpu(rnd, ls.n, ls.k, ls.nr, ls.nc);
        resizable_tensor rhs = random_cpu(rnd, rs.n, rs.k, rs.nr, rs.nc);
        resizable_tensor scale = absolute(dest);
        resizable_tensor md = on_metal(dest), ml = on_metal(lhs), mr = on_metal(rhs);
        tt::gemm(std::abs(beta), scale, std::abs(alpha), absolute(lhs), tl, absolute(rhs), tr, mode);
        tt::gemm(beta, dest, alpha, lhs, tl, rhs, tr, mode);
        tt::gemm(beta, md, alpha, ml, tl, mr, tr, mode);
        check_gemm_parity(op, dest, md, scale, terms);
    }

    void test_parity_gemm (dlib::rand& rnd)
    {
        // CHANNEL_WISE: mat(t) has a row per sample.  Square, rectangular, small and large,
        // with each transposition, and alpha and beta.
        struct mnk { long m, n, k; };
        const std::vector<mnk> sizes = {{1,1,1}, {2,3,4}, {64,64,64}, {3,5,700}, {1000,300,17},
                                        {33,65,129}, {512,512,512}, {7,1,1024}, {1,4096,64}};
        const std::vector<std::pair<float,float>> coefs = {{1,0}, {1,1}, {-2,0.5f}};
        for (auto& s : sizes)
        {
            for (int t = 0; t < 4; ++t)
            {
                const bool tl = t & 1, tr = t & 2;
                for (auto& ab : coefs)
                {
                    const shape ls = tl ? shape{s.k, s.m, 1, 1} : shape{s.m, s.k, 1, 1};
                    const shape rs = tr ? shape{s.n, s.k, 1, 1} : shape{s.k, s.n, 1, 1};
                    run_gemm_case(rnd, "gemm CHANNEL_WISE", {s.m, s.n, 1, 1}, ls, rs, tl, tr,
                        ab.first, ab.second, operation_mode::CHANNEL_WISE, s.k);
                }
            }
        }
        // mat() of a 4-dimensional tensor: k*nr*nc columns.
        run_gemm_case(rnd, "gemm CHANNEL_WISE", {6,2,3,4}, {6,5,2,2}, {20,2,3,4}, false, false, 1, 0,
            operation_mode::CHANNEL_WISE, 20);

        // PLANE_WISE: a product per (sample, channel), operands of one plane shared by the
        // batch, and a dest shared by the batch, which accumulates with beta = 1.
        for (int t = 0; t < 4; ++t)
        {
            const bool tl = t & 1, tr = t & 2;
            const long m = 9, n = 13, k = 17;
            const shape a = tl ? shape{2,3,k,m} : shape{2,3,m,k};
            const shape b = tr ? shape{2,3,n,k} : shape{2,3,k,n};
            run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,m,n}, a, b, tl, tr, 1, 0, operation_mode::PLANE_WISE, k);
            run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,m,n}, a, b, tl, tr, 0.5f, 2, operation_mode::PLANE_WISE, k);
            const shape bm = tr ? shape{1,1,n,k} : shape{1,1,k,n};
            run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,m,n}, a, bm, tl, tr, 1, 0, operation_mode::PLANE_WISE, k);
            const shape am = tl ? shape{1,1,k,m} : shape{1,1,m,k};
            run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,m,n}, am, b, tl, tr, 1, 1, operation_mode::PLANE_WISE, k);
            run_gemm_case(rnd, "gemm PLANE_WISE", {1,1,m,n}, a, b, tl, tr, 1, 1, operation_mode::PLANE_WISE, 6*k);
        }
        // Attention-like shapes: (batch, heads, sequence, head size).
        run_gemm_case(rnd, "gemm PLANE_WISE", {2,4,64,64}, {2,4,64,32}, {2,4,64,32}, false, true, 0.125f, 0,
            operation_mode::PLANE_WISE, 32);
        run_gemm_case(rnd, "gemm PLANE_WISE", {2,4,64,32}, {2,4,64,64}, {2,4,64,32}, false, false, 1, 0,
            operation_mode::PLANE_WISE, 64);
        // A matrix of planes of one value: (K, N, 1, 1) is the K x N matrix of a linear layer.
        run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,5,7}, {2,3,5,11}, {11,7,1,1}, false, false, 1, 0,
            operation_mode::PLANE_WISE, 11);
        run_gemm_case(rnd, "gemm PLANE_WISE", {2,3,5,7}, {5,11,1,1}, {2,3,11,7}, false, false, 1, 0.5f,
            operation_mode::PLANE_WISE, 11);

        // Views at an offset, in a larger buffer.
        resizable_tensor big = random_cpu(rnd, 4, 1, 1, 64*64);
        resizable_tensor mbig = on_metal(big);
        alias_tensor part(64, 64);
        auto a = part(big, 1), b = part(big, 64*64 + 3), d = part(big, 2*64*64 + 101);
        auto ma = part(mbig, 1), mb = part(mbig, 64*64 + 3), md = part(mbig, 2*64*64 + 101);
        resizable_tensor scale(64, 64);
        scale = 0;
        tt::gemm(0, scale, 1, absolute(a), false, absolute(b), false);
        tt::gemm(0, d, 1, a, false, b, false);
        tt::gemm(0, md, 1, ma, false, mb, false);
        resizable_tensor dc(d), dm(md);
        check_gemm_parity("gemm CHANNEL_WISE", dc, dm, scale, 64);

        // A NaN left in dest does not come through when beta == 0, as on the CPU.
        resizable_tensor l = random_cpu(rnd, 8, 8, 1, 1), r = random_cpu(rnd, 8, 8, 1, 1);
        resizable_tensor nan_dest(8, 8);
        nan_dest = std::numeric_limits<float>::quiet_NaN();
        resizable_tensor ml = on_metal(l), mr = on_metal(r), mn = on_metal(nan_dest);
        tt::gemm(0, nan_dest, 1, l, false, r, false);
        tt::gemm(0, mn, 1, ml, false, mr, false);
        for (auto x : mn)
            DLIB_TEST(!std::isnan(x));

        // Refused: matrices that do not fit, and a dest that overlaps an operand.
        resizable_tensor x = on_metal(random_cpu(rnd, 4, 5, 1, 1)), y = on_metal(random_cpu(rnd, 6, 3, 1, 1));
        resizable_tensor z = on_metal(random_cpu(rnd, 4, 3, 1, 1));
        DLIB_TEST(contains(error_of([&]{ tt::gemm(0, z, 1, x, false, y, false); }), "tt::gemm was given matrices"));
        DLIB_TEST(contains(error_of([&]{ tt::gemm(0, x, 1, x, false, x, true); }), "overlaps"));
    }

    // Times of tt::gemm on the CPU (dlib with its BLAS) and on Metal, for square matrices.
    // Only with DLIB_METAL_BENCH set: 3 runs to warm up, then the median of 15.
    void bench_gemm (dlib::rand& rnd)
    {
        if (!std::getenv("DLIB_METAL_BENCH"))
            return;
        for (long n : {256L, 1024L, 2048L})
        {
            resizable_tensor a = random_cpu(rnd, n, n, 1, 1), b = random_cpu(rnd, n, n, 1, 1), d(n, n);
            resizable_tensor ma = on_metal(a), mb = on_metal(b), md = on_metal(d);
            auto time_of = [&](tensor& dd, const tensor& aa, const tensor& bb)
            {
                std::vector<double> t;
                for (int i = 0; i < 3 + 15; ++i)
                {
                    const auto start = std::chrono::steady_clock::now();
                    tt::gemm(0, dd, 1, aa, false, bb, false);
                    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    if (i >= 3)
                        t.push_back(s);
                }
                std::sort(t.begin(), t.end());
                return t[t.size()/2];
            };
            const double tc = time_of(d, a, b);
            const double tm = time_of(md, ma, mb);
            const double flops = 2.0*n*n*n;
            std::cout << "gemm " << n << "x" << n << ": cpu " << tc*1e3 << " ms (" << flops/tc*1e-9
                      << " GFLOP/s), metal " << tm*1e3 << " ms (" << flops/tm*1e-9 << " GFLOP/s)" << std::endl;
        }
    }

    /*
        Parity of layer_normalize against an exact reference, computed in double with two
        passes.  Both sides sum D values in float, in different orders, and compute the
        variance as E[x^2] - mean^2, a subtraction that loses digits when the mean is large
        before the spread.  The rule: the two sides agree within 1e-5 + 1e-5*|cpu|, plus the
        gap of the CPU itself to the exact value; a gap beyond it would be Metal's own error.
    */
    void check_against_exact (
        const std::string& op,
        const tensor& cpu,
        const tensor& gpu,
        const std::vector<double>& exact
    )
    {
        DLIB_TEST(have_same_dimensions(cpu, gpu) && cpu.size() == exact.size());
        parity& p = parity_table()[op];
        const float* c = cpu.host();
        const float* g = gpu.host();
        for (size_t i = 0; i < cpu.size(); ++i)
        {
            const double a = std::abs((double)g[i] - (double)c[i]);
            const double ec = std::abs((double)c[i] - exact[i]);
            const double eg = std::abs((double)g[i] - exact[i]);
            ++p.compared;
            p.max_abs = std::max(p.max_abs, a);
            if (std::abs(c[i]) >= 1e-3)
                p.max_rel = std::max(p.max_rel, a/std::abs((double)c[i]));
            p.max_cpu_error = std::max(p.max_cpu_error, ec);
            p.max_gpu_error = std::max(p.max_gpu_error, eg);
            if (!(a <= 1e-5 + 1e-5*std::abs((double)c[i]) + ec))
            {
                if (p.failed < 5)
                    dlog << LERROR << op << ": value " << i << " cpu " << c[i] << " metal " << g[i] << " exact " << exact[i];
                ++p.failed;
            }
        }
        DLIB_TEST_MSG(p.failed == 0, op << ": " << p.failed << " values beyond the tolerance");
    }

    void exact_layer_norm (
        const tensor& src,
        const tensor& gamma,
        const tensor& beta,
        double eps,
        std::vector<double>& dest,
        std::vector<double>& means,
        std::vector<double>& invstds
    )
    {
        const long n = src.num_samples();
        const long d = src.size()/n;
        const long plane = src.nr()*src.nc();
        const float* x = src.host();
        dest.assign(src.size(), 0);
        means.assign(n, 0);
        invstds.assign(n, 0);
        for (long s = 0; s < n; ++s)
        {
            double m = 0;
            for (long i = 0; i < d; ++i)
                m += x[s*d + i];
            m /= d;
            double v = 0;
            for (long i = 0; i < d; ++i)
                v += (x[s*d + i] - m)*(x[s*d + i] - m);
            v /= d;
            const double r = 1/std::sqrt(v + eps);
            means[s] = m;
            invstds[s] = r;
            for (long i = 0; i < d; ++i)
                dest[s*d + i] = (x[s*d + i] - m)*r*gamma.host()[i/plane] + beta.host()[i/plane];
        }
    }

    std::set<long>& layer_norm_sizes ()
    {
        static std::set<long> sizes;
        return sizes;
    }

    void test_parity_layer_norm (dlib::rand& rnd)
    {
        // Values per sample around the width of a simdgroup (32) and of a threadgroup (256),
        // and far from them; one channel or several of nr*nc values; centred values, and
        // values with an offset, whose variance the formula E[x^2] - mean^2 computes by
        // a subtraction.
        const std::vector<long> sizes = {1, 7, 31, 32, 33, 100, 255, 256, 257, 1000, 1023, 1024, 4097, 12345};
        for (long d : sizes)
        {
            for (long samples : {1L, 3L, 17L})
            {
                for (float offset : {0.0f, 3.0f})
                {
                    const shape s = (d % 3 == 0 && d > 3) ? shape{samples, 3, 1, d/3} : shape{samples, d, 1, 1};
                    resizable_tensor src = random_cpu(rnd, s.n, s.k, s.nr, s.nc);
                    for (auto& x : src)
                        x += offset;
                    resizable_tensor gamma = random_cpu(rnd, 1, s.k, 1, 1), beta = random_cpu(rnd, 1, s.k, 1, 1);
                    resizable_tensor dest, means, invstds;
                    resizable_tensor ms = on_metal(src), mg = on_metal(gamma), mb = on_metal(beta);
                    resizable_tensor mdest, mmeans, minvstds;
                    tt::layer_normalize(1e-5, dest, means, invstds, src, gamma, beta);
                    tt::layer_normalize(1e-5, mdest, mmeans, minvstds, ms, mg, mb);
                    DLIB_TEST(mdest.get_tensor_device() == tensor_device::metal);
                    const std::string tag = offset == 0 ? "" : " offset 3";
                    std::vector<double> edest, emeans, einvstds;
                    exact_layer_norm(src, gamma, beta, 1e-5, edest, emeans, einvstds);
                    check_against_exact("layer_normalize dest" + tag, dest, mdest, edest);
                    check_against_exact("layer_normalize means" + tag, means, mmeans, emeans);
                    check_against_exact("layer_normalize invstds" + tag, invstds, minvstds, einvstds);
                    layer_norm_sizes().insert(d);
                }
            }
        }
        // Refused: a gamma of several samples.
        resizable_tensor src = on_metal(random_cpu(rnd, 2, 4, 1, 1)), g = on_metal(random_cpu(rnd, 2, 4, 1, 1));
        resizable_tensor d, m, v;
        DLIB_TEST(contains(error_of([&]{ tt::layer_normalize(1e-5, d, m, v, src, g, g); }), "gamma of one sample"));
    }

    void exact_attention (
        const tensor& q,
        const tensor& k,
        const tensor& v,
        double scale,
        bool causal,
        std::vector<double>& out
    )
    {
        const long B = q.num_samples(), H = q.k(), tq = q.nr(), tk = k.nr(), d = q.nc(), dv = v.nc();
        out.assign(B*H*tq*dv, 0);
        std::vector<double> s(tk);
        for (long p = 0; p < B*H; ++p)
        {
            const float* Q = q.host() + p*tq*d;
            const float* K = k.host() + p*tk*d;
            const float* V = v.host() + p*tk*dv;
            for (long i = 0; i < tq; ++i)
            {
                double mx = -std::numeric_limits<double>::infinity();
                for (long j = 0; j < tk; ++j)
                {
                    if (causal && j > i + tk - tq)
                    {
                        s[j] = -std::numeric_limits<double>::infinity();
                        continue;
                    }
                    double dot = 0;
                    for (long c = 0; c < d; ++c)
                        dot += (double)Q[i*d + c]*K[j*d + c];
                    s[j] = scale*dot;
                    mx = std::max(mx, s[j]);
                }
                double sum = 0;
                for (long j = 0; j < tk; ++j)
                {
                    s[j] = std::isinf(s[j]) ? 0 : std::exp(s[j] - mx);
                    sum += s[j];
                }
                for (long c = 0; c < dv; ++c)
                {
                    double acc = 0;
                    for (long j = 0; j < tk; ++j)
                        acc += s[j]/sum*V[j*dv + c];
                    out[(p*tq + i)*dv + c] = acc;
                }
            }
        }
    }

    void test_parity_attention (dlib::rand& rnd)
    {
        struct form { long batch, heads, tq, tk, d, dv; };
        const std::vector<form> forms = {
            {1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 16, 16}, {1, 1, 7, 7, 16, 16}, {2, 4, 64, 64, 64, 64},
            {1, 8, 129, 129, 64, 64}, {2, 3, 33, 33, 80, 80}, {1, 2, 300, 300, 32, 48},
            {2, 4, 1, 40, 64, 64}, {1, 4, 5, 77, 32, 32}, {3, 1, 17, 9, 8, 8}};
        for (auto& f : forms)
        {
            for (bool causal : {false, true})
            {
                if (causal && f.tq > f.tk)
                    continue;
                resizable_tensor q = random_cpu(rnd, f.batch, f.heads, f.tq, f.d);
                resizable_tensor k = random_cpu(rnd, f.batch, f.heads, f.tk, f.d);
                resizable_tensor v = random_cpu(rnd, f.batch, f.heads, f.tk, f.dv);
                const float scale = 1/std::sqrt((float)f.d);
                resizable_tensor out, mout;
                tt::attention(out, q, k, v, scale, causal);
                resizable_tensor mq = on_metal(q), mk = on_metal(k), mv = on_metal(v);
                tt::attention(mout, mq, mk, mv, scale, causal);
                DLIB_TEST(mout.get_tensor_device() == tensor_device::metal);
                std::vector<double> exact;
                exact_attention(q, k, v, scale, causal, exact);
                check_against_exact(causal ? "attention causal" : "attention", out, mout, exact);
                dlog << LINFO << "attention " << f.batch << "x" << f.heads << " heads, " << f.tq << " queries, "
                     << f.tk << " keys, head " << f.d << "/" << f.dv << (causal ? ", causal" : "");
            }
        }

        // Mixed devices are refused.
        resizable_tensor q = on_metal(random_cpu(rnd, 1, 2, 4, 8)), k = random_cpu(rnd, 1, 2, 4, 8), out;
        DLIB_TEST(contains(error_of([&]{ try { tt::attention(out, q, k, k, 1, false); }
                                          catch (dlib::fatal_error& e) { throw metal_error(e.what()); } }),
                           "one device"));
    }

// ----------------------------------------------------------------------------------------
// GGUF weights in Q4_0 and Q8_0: a quantizer written from the definition of the formats
// (one half precision scale per block of 32 values, then 4 or 8 bit values), the product of
// the CPU (tt::gguf_matvec on the host) as reference, and the product of Metal.

    uint16_t half_bits (float x)
    {
        const __fp16 h = (__fp16)x;
        uint16_t b;
        std::memcpy(&b, &h, sizeof(b));
        return b;
    }

    float from_half_bits (uint16_t b)
    {
        __fp16 h;
        std::memcpy(&h, &b, sizeof(b));
        return (float)h;
    }

    // Quantizes a rows x cols matrix (cols a multiple of 32) into the bytes of a GGUF
    // matrix, stored in the floats of a tensor.
    resizable_tensor quantize (const matrix<float>& m, int type)
    {
        const long rows = m.nr(), cols = m.nc();
        const size_t row_bytes = gguf_blocks::row_bytes(type, cols);
        std::vector<uint8_t> bytes((size_t)rows*row_bytes, 0);
        for (long r = 0; r < rows; ++r)
        {
            for (long g = 0; g < cols/32; ++g)
            {
                uint8_t* b = bytes.data() + (size_t)r*row_bytes + g*(type == gguf_blocks::q4_0 ? 18 : 34);
                if (type == gguf_blocks::q8_0)
                {
                    float amax = 0;
                    for (int j = 0; j < 32; ++j)
                        amax = std::max(amax, std::abs(m(r, g*32 + j)));
                    const uint16_t hd = half_bits(amax/127);
                    const float d = from_half_bits(hd);
                    std::memcpy(b, &hd, 2);
                    for (int j = 0; j < 32; ++j)
                    {
                        const int q = d == 0 ? 0 : (int)std::lround(m(r, g*32 + j)/d);
                        b[2 + j] = (uint8_t)(int8_t)std::max(-127, std::min(127, q));
                    }
                }
                else
                {
                    // The value of largest magnitude maps to -8, so that the 16 levels
                    // -8..7 cover the block.
                    float extreme = 0;
                    for (int j = 0; j < 32; ++j)
                        if (std::abs(m(r, g*32 + j)) > std::abs(extreme))
                            extreme = m(r, g*32 + j);
                    const uint16_t hd = half_bits(extreme/-8);
                    const float d = from_half_bits(hd);
                    std::memcpy(b, &hd, 2);
                    for (int j = 0; j < 16; ++j)
                    {
                        auto level = [&](float x) { return d == 0 ? 8 : std::max(0, std::min(15, (int)std::lround(x/d) + 8)); };
                        b[2 + j] = (uint8_t)(level(m(r, g*32 + j)) | (level(m(r, g*32 + j + 16)) << 4));
                    }
                }
            }
        }
        resizable_tensor w(1, 1, 1, (long)((bytes.size() + 3)/4));
        w = 0;
        std::memcpy(w.host(), bytes.data(), bytes.size());
        return w;
    }

    void test_parity_gguf (dlib::rand& rnd)
    {
        struct form { long rows, cols, n; };
        const std::vector<form> forms = {{1, 32, 1}, {7, 64, 3}, {64, 4096, 1}, {64, 4096, 8}, {513, 4128, 9},
                                         {33, 256, 17}, {1000, 1024, 2}};
        for (int type : {(int)gguf_blocks::q4_0, (int)gguf_blocks::q8_0})
        {
            const std::string name = type == gguf_blocks::q4_0 ? "gguf_matvec Q4_0" : "gguf_matvec Q8_0";
            for (auto& f : forms)
            {
                const matrix<float> wf = matrix_cast<float>(gaussian_randm(f.rows, f.cols, rnd.get_random_32bit_number()));
                resizable_tensor w = quantize(wf, type);
                const size_t row_bytes = gguf_blocks::row_bytes(type, f.cols);
                resizable_tensor x = random_cpu(rnd, f.n, 1, 1, f.cols), y(f.n, 1, 1, f.rows);
                tt::gguf_matvec(y, x, w, type, f.rows, f.cols, row_bytes);

                resizable_tensor mw = on_metal(w), mx = on_metal(x), my = on_metal(y);
                my = 0;
                tt::gguf_matvec(my, mx, mw, type, f.rows, f.cols, row_bytes);

                // The bound of a dot product of cols terms, as for gemm, on the decoded weights.
                resizable_tensor wd(f.rows, f.cols);
                tt::gguf_dequantize(wd, w, type, 0, f.rows, f.cols, row_bytes);
                resizable_tensor scale(f.n, 1, 1, f.rows);
                tt::gemm(0, scale, 1, absolute(x), false, absolute(wd), true);
                check_gemm_parity(name, y, my, scale, f.cols);

                // What quantization costs, against the product by the weights in float32:
                // the largest gap, and the relative gap of all the values together.
                const matrix<float> exact = mat(x)*trans(wf);
                parity& q = parity_table()[name + " vs float32 weights"];
                for (long t = 0; t < f.n; ++t)
                {
                    for (long r = 0; r < f.rows; ++r)
                    {
                        const double e = (double)my.host()[t*f.rows + r] - exact(t, r);
                        q.max_abs = std::max(q.max_abs, std::abs(e));
                        q.sum_gap2 += e*e;
                        q.sum_norm2 += (double)exact(t, r)*exact(t, r);
                        ++q.compared;
                    }
                }
                q.max_rel = std::sqrt(q.sum_gap2/q.sum_norm2);
            }
        }

        // Unhandled cases stop with a clear error.
        resizable_tensor w = on_metal(quantize(matrix_cast<float>(gaussian_randm(4, 64, 1)), gguf_blocks::q4_0));
        resizable_tensor x = on_metal(random_cpu(rnd, 1, 1, 1, 64)), y = on_metal(random_cpu(rnd, 1, 1, 1, 4));
        DLIB_TEST(contains(error_of([&]{ tt::gguf_matvec(y, x, w, gguf_blocks::q4_K, 4, 256, gguf_blocks::row_bytes(gguf_blocks::q4_K, 256)); }),
            "no Metal version for the GGUF format 12"));
        DLIB_TEST(contains(error_of([&]{ tt::gguf_matvec(y, x, w, gguf_blocks::q4_0, 4, 64, 37); }), "rows of 37 bytes"));
        resizable_tensor y8 = on_metal(random_cpu(rnd, 1, 1, 1, 8));
        DLIB_TEST(contains(error_of([&]{ tt::gguf_matvec(y8, x, w, gguf_blocks::q4_0, 8, 64, 36); }), "bytes of weights"));
    }

    /*
        Memory of the weights of a GGUF model on Metal, kept in their Q4_0 or Q8_0 bytes,
        against the same weights decoded to float32.  Only with DLIB_METAL_GGUF set to the
        path of a model.  Method: every Q4_0 and Q8_0 tensor of the file is copied into a
        Metal tensor as its bytes; the buffers the backend holds (metal::live_bytes(), the
        sum of the lengths of its MTLBuffers) and the memory the device reports allocated
        (metal::allocated_bytes(), MTLDevice.currentAllocatedSize) are read before and after.
        Then the same tensors are decoded by the CPU (tt::gguf_dequantize) and moved to Metal
        in float32, and both counts are read again.  The other tensors of the file (norms,
        embeddings in other formats) are counted apart, not loaded.  A product of the first
        matrix by a random vector checks the Metal product on real weights.
    */
    // The physical memory of the process (phys_footprint of macOS), which counts the pages
    // of the shared Metal buffers once written.
    size_t footprint ()
    {
        task_vm_info_data_t info;
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
            return 0;
        return (size_t)info.phys_footprint;
    }

    void measure_gguf_memory (dlib::rand& rnd)
    {
        const char* path = std::getenv("DLIB_METAL_GGUF");
        if (!path)
            return;
        gguf_reader reader(path);
        struct loaded { const gguf_tensor_info* info; resizable_tensor bytes; };
        std::vector<loaded> packed;
        uint64_t values = 0, other_bytes = 0, file_bytes = 0;
        std::map<int, size_t> counts;
        const size_t live0 = metal::live_bytes(), dev0 = metal::allocated_bytes(), foot0 = footprint();
        std::vector<uint8_t> raw;
        for (auto& t : reader.tensors())
        {
            const int type = (int)t.type;
            if (type != gguf_blocks::q4_0 && type != gguf_blocks::q8_0)
            {
                other_bytes += t.nbytes;
                continue;
            }
            reader.read_tensor_raw(t, raw);
            loaded l;
            l.info = &t;
            l.bytes.set_tensor_device(tensor_device::metal);
            l.bytes.set_size(1, 1, 1, (long)((raw.size() + 3)/4));
            std::memcpy(l.bytes.host(), raw.data(), raw.size());
            packed.push_back(std::move(l));
            values += t.n_elements();
            file_bytes += t.nbytes;
            ++counts[type];
        }
        raw.clear();
        raw.shrink_to_fit();
        const size_t live_q = metal::live_bytes() - live0, dev_q = metal::allocated_bytes() - dev0;
        const size_t foot_q = footprint() - foot0;

        // A product by the first matrix, on real weights.
        if (!packed.empty())
        {
            const gguf_tensor_info& t = *packed[0].info;
            const long cols = (long)t.dims[0], rows = (long)(t.n_elements()/t.dims[0]);
            const int type = (int)t.type;
            const size_t row_bytes = gguf_blocks::row_bytes(type, cols);
            resizable_tensor w(packed[0].bytes);
            w.set_tensor_device(tensor_device::cpu);
            resizable_tensor x = random_cpu(rnd, 1, 1, 1, cols), y(1, 1, 1, rows);
            tt::gguf_matvec(y, x, w, type, rows, cols, row_bytes);
            resizable_tensor mx = on_metal(x), my = on_metal(y);
            tt::gguf_matvec(my, mx, packed[0].bytes, type, rows, cols, row_bytes);
            resizable_tensor wd(rows, cols), scale(1, 1, 1, rows);
            tt::gguf_dequantize(wd, w, type, 0, rows, cols, row_bytes);
            tt::gemm(0, scale, 1, absolute(x), false, absolute(wd), true);
            check_gemm_parity("gguf_matvec on the weights of the model", y, my, scale, cols);
            std::cout << "real matrix " << t.name << " (" << rows << "x" << cols << "): max gap/bound "
                      << parity_table()["gguf_matvec on the weights of the model"].max_bound_ratio << std::endl;
        }

        std::vector<resizable_tensor> decoded;
        for (auto& l : packed)
        {
            const gguf_tensor_info& t = *l.info;
            const long cols = (long)t.dims[0], rows = (long)(t.n_elements()/t.dims[0]);
            resizable_tensor w(l.bytes);
            w.set_tensor_device(tensor_device::cpu);
            resizable_tensor f(rows, cols);
            tt::gguf_dequantize(f, w, (int)t.type, 0, rows, cols, gguf_blocks::row_bytes((int)t.type, cols));
            f.set_tensor_device(tensor_device::metal);
            decoded.push_back(std::move(f));
        }
        const size_t live_f = metal::live_bytes() - live0 - live_q, dev_f = metal::allocated_bytes() - dev0 - dev_q;
        const size_t foot_f = footprint() - foot0 - foot_q;

        std::cout << "model " << path << "\n"
                  << "  tensors in Q4_0: " << counts[gguf_blocks::q4_0] << ", in Q8_0: " << counts[gguf_blocks::q8_0]
                  << ", values: " << values << ", their bytes in the file: " << file_bytes
                  << ", other tensors (not loaded): " << other_bytes << " bytes\n"
                  << "  Metal, kept quantized: buffers " << live_q << " bytes, device " << dev_q
                  << " bytes, process footprint +" << foot_q << " bytes\n"
                  << "  Metal, decoded to float32: buffers " << live_f << " bytes, device " << dev_f
                  << " bytes, process footprint +" << foot_f << " bytes\n"
                  << "  ratio float32 / quantized (buffers): " << (double)live_f/live_q << std::endl;
    }

    void report_parity ()
    {
        for (auto& e : parity_table())
            dlog << LINFO << "parity " << e.first << ": " << e.second.compared << " values, max |diff| "
                 << e.second.max_abs << ", max rel (|cpu| >= 1e-3) " << e.second.max_rel;
        std::ostringstream sout;
        for (auto& e : parity_table())
            sout << e.first << ": " << e.second.compared << " values, max |diff| " << e.second.max_abs
                 << (e.second.max_bound_ratio > 0 ? ", max gap/scale " :
                     e.second.sum_norm2 > 0 ? ", relative gap of all values " : ", max rel (|cpu| >= 1e-3) ") << e.second.max_rel
                 << (e.second.max_bound_ratio > 0 ? ", max gap/bound " + std::to_string(e.second.max_bound_ratio) : std::string())
                 << (e.second.max_cpu_error >= 0 ? ", max |cpu-exact| " + std::to_string(e.second.max_cpu_error) +
                     ", max |metal-exact| " + std::to_string(e.second.max_gpu_error) : std::string())
                 << "\n";
        if (std::getenv("DLIB_METAL_REPORT"))
            std::cout << sout.str();
    }

#endif

// ----------------------------------------------------------------------------------------

    class metal_tester : public tester
    {
    public:
        metal_tester (
        ) :
            tester ("test_metal",
                    "Runs tests on the Metal backend of dlib tensors.")
        {}

        void perform_test (
        )
        {
            test_default_device();
#ifndef DLIB_USE_METAL
            test_without_metal();
#else
            test_device();
            test_values_read_back();
            test_memory_is_given_back();
            test_gpu_sees_the_same_memory();
            test_moving_between_devices();
            test_copies_moves_and_views();
            test_routing();
            dlib::rand rnd(20261117);
            test_parity_add(rnd);
            test_parity_multiply(rnd);
            test_parity_softmax(rnd);
            test_parity_activations(rnd);
            test_parity_views(rnd);
            test_parity_gemm(rnd);
            test_parity_layer_norm(rnd);
            test_parity_attention(rnd);
            test_parity_gguf(rnd);
            report_parity();
            bench_gemm(rnd);
            measure_gguf_memory(rnd);
            DLIB_TEST(metal::live_buffers() == 0);
#endif
        }
    } a;

}
