// Copyright (C) 2026  TortueTech
// License: Boost Software License   See LICENSE.txt for the full license.

#include <dlib/dnn.h>
#include <algorithm>
#include <cmath>
#include <functional>
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
            {"tt::add",              [&]{ tt::add(dest, src, other); }},
            {"tt::multiply",         [&]{ tt::multiply(false, dest, src, other); }},
            {"tt::softmax",          [&]{ tt::softmax(dest, src); }},
            {"tt::gelu",             [&]{ tt::gelu(dest, src); }},
            {"tt::silu",             [&]{ tt::silu(dest, src); }},
            {"tt::affine_transform", [&]{ tt::affine_transform(dest, src, 2, 1); }},
            {"tt::gemm",             [&]{ tt::gemm(0, dest, 1, src, false, other, true); }},
            {"tt::tensor_rand::fill_uniform", [&]{ tt::tensor_rand(1).fill_uniform(dest); }},
        };
        for (auto& op : ops)
        {
            const std::string e = error_of(op.second);
            DLIB_TEST_MSG(contains(e, op.first + " has no Metal version yet"), op.first << ": " << e);
        }
        DLIB_TEST(has_pattern(dest, 2));

        // An output still to be sized counts for neither device.
        resizable_tensor means, invstds, out;
        resizable_tensor gamma = metal_tensor(1,3,4,5), beta = metal_tensor(1,3,4,5);
        const std::string e2 = error_of([&]{ tt::layer_normalize(1e-5, out, means, invstds, src, gamma, beta); });
        DLIB_TEST_MSG(contains(e2, "tt::layer_normalize has no Metal version yet"), e2);

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

        // CPU tensors take the host path, unchanged.
        resizable_tensor a(2,3,4,5), b(2,3,4,5);
        tt::tensor_rand(rnd.get_random_32bit_number()).fill_gaussian(a);
        tt::relu(b, a);
        for (size_t i = 0; i < a.size(); ++i)
            DLIB_TEST(b.host()[i] == std::max(0.0f, a.host()[i]));
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
            test_gpu_sees_the_same_memory();
            test_moving_between_devices();
            test_copies_moves_and_views();
            test_routing();
            DLIB_TEST(metal::live_buffers() == 0);
#endif
        }
    } a;

}
