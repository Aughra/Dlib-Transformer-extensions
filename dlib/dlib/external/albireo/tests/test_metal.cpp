// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    test_metal : les noyaux Metal du portage macOS comparés au processeur, sur des données aléatoires.

        test_metal

    Chaque noyau calcule en flottants, dans un autre ordre que le processeur : ses résultats sont
    comparés à une somme en double précision, relativement à la somme des valeurs absolues des
    termes, à 1e-5 près (produits, normes, positions rotatives) ou à 1e-4 près (attention).  Les
    clés et valeurs qu'écrit le GPU sont comparées octet par octet à celles qu'écrit
    qk_float_to_q8_row(), une valeur entière pouvant différer de un là où son arrondi tombe sur une
    égalité.  Code de sortie : le nombre de vérifications échouées ; 0 avec "skipped" quand le Mac
    n'a pas de GPU que Metal atteint.
*/

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../engine/kv_cache.h"
#include "../engine/metal_device.h"
#include "../engine/qkernels.h"

using namespace albireo;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    }

    std::mt19937 rng(20261111);

    float uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

    std::string sci(double v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2e", v);
        return buf;
    }

    // Une matrice de rows x cols en Q4_K ou Q6_K, ses octets aléatoires et ses échelles dans une plage
    // raisonnable.
    std::vector<uint8_t> random_matrix(dlib::ggml_type type, long rows, long cols)
    {
        const size_t rb = qk_row_bytes(type, cols);
        std::vector<uint8_t> w(rb * rows);
        for (auto& b : w) b = static_cast<uint8_t>(rng());
        const long nb = cols / 256;
        for (long r = 0; r < rows; ++r)
            for (long b = 0; b < nb; ++b)
            {
                uint8_t* blk = w.data() + r * rb + b * (type == dlib::ggml_type::Q4_K ? 144 : 210);
                if (type == dlib::ggml_type::Q4_K)
                {
                    const uint16_t d = qk_float_to_half(uniform(0.001f, 0.02f));
                    const uint16_t m = qk_float_to_half(uniform(0.0f, 0.02f));
                    std::memcpy(blk, &d, 2);
                    std::memcpy(blk + 2, &m, 2);
                }
                else
                {
                    const uint16_t d = qk_float_to_half(uniform(0.0005f, 0.01f));
                    std::memcpy(blk + 208, &d, 2);
                }
            }
        return w;
    }

    void test_matvec(metal_device& gpu, dlib::ggml_type type, const char* name)
    {
        const long rows = 1000, cols = 2560;
        const std::vector<uint8_t> w = random_matrix(type, rows, cols);
        const size_t rb = qk_row_bytes(type, cols);
        for (unsigned tokens : { 1u, 3u, 8u })
        {
            const unsigned x_stride = cols + 64, y_stride = rows + 8;
            metal_buffer W = gpu.allocate(w.size());
            std::memcpy(W.contents(), w.data(), w.size());
            metal_buffer X = gpu.allocate(sizeof(float) * x_stride * tokens);
            metal_buffer Y = gpu.allocate(sizeof(float) * y_stride * tokens);
            float* x = static_cast<float*>(X.contents());
            float* y = static_cast<float*>(Y.contents());
            for (size_t i = 0; i < x_stride * tokens; ++i) x[i] = uniform(-1.0f, 1.0f);
            for (size_t i = 0; i < y_stride * tokens; ++i) y[i] = uniform(-1.0f, 1.0f);
            const std::vector<float> before(y, y + y_stride * tokens);
            const float alpha = 0.75f, beta = tokens == 1 ? 0.0f : 1.0f;
            auto c = gpu.begin();
            const bool encoded = c->matvec(type, W, rows, cols, X, tokens, x_stride, Y, y_stride, alpha, beta);
            c->finish();
            double worst = 0;
            std::vector<float> row(cols);
            for (long r = 0; r < rows; ++r)
            {
                qk_dequantize_row(type, w.data() + r * rb, row.data(), cols);
                for (unsigned t = 0; t < tokens; ++t)
                {
                    double ref = 0, mag = 0;
                    for (long i = 0; i < cols; ++i)
                    {
                        ref += static_cast<double>(row[i]) * x[t * x_stride + i];
                        mag += std::fabs(static_cast<double>(row[i]) * x[t * x_stride + i]);
                    }
                    const double expected = alpha * ref + beta * before[t * y_stride + r];
                    worst = std::max(worst, std::fabs(y[t * y_stride + r] - expected) / (alpha * mag + 1e-30));
                }
            }
            bool untouched = true;
            for (unsigned t = 0; t < tokens; ++t)
                for (unsigned r = rows; r < y_stride; ++r)
                    untouched = untouched && y[t * y_stride + r] == before[t * y_stride + r];
            check(encoded && worst < 1e-5 && untouched, std::string(name) + ", " + std::to_string(tokens)
                + " tokens: largest error " + sci(worst) + ", the rows beyond the matrix untouched");
        }
    }

    // Les poids développés en flottants, puis un produit d'un prompt par Metal Performance Shaders.
    void test_dequantize_gemm(metal_device& gpu, dlib::ggml_type type, const char* name)
    {
        const long rows = 300, cols = 1024;
        const unsigned tokens = 37, stride = cols + 32, y_stride = rows + 4;
        const std::vector<uint8_t> w = random_matrix(type, rows, cols);
        const size_t rb = qk_row_bytes(type, cols);
        metal_buffer W = gpu.allocate(w.size()), F = gpu.allocate(sizeof(float) * rows * stride);
        std::memcpy(W.contents(), w.data(), w.size());
        metal_buffer X = gpu.allocate(sizeof(float) * tokens * stride), Y = gpu.allocate(sizeof(float) * tokens * y_stride);
        float* x = static_cast<float*>(X.contents());
        float* y = static_cast<float*>(Y.contents());
        for (size_t i = 0; i < tokens * stride; ++i) x[i] = uniform(-1.0f, 1.0f);
        for (size_t i = 0; i < tokens * y_stride; ++i) y[i] = uniform(-1.0f, 1.0f);
        const std::vector<float> before(y, y + tokens * y_stride);
        auto c = gpu.begin();
        const bool encoded = c->dequantize(type, W, rows, cols, F, stride);
        c->gemm(X, tokens, cols, stride, F, rows, stride, Y, y_stride, 0.5f, 1.0f);
        c->finish();
        const float* f = static_cast<const float*>(F.contents());
        long apart = 0;
        double worst = 0;
        std::vector<float> row(cols);
        for (long r = 0; r < rows; ++r)
        {
            qk_dequantize_row(type, w.data() + r * rb, row.data(), cols);
            for (long i = 0; i < cols; ++i) apart += std::fabs(f[r * stride + i] - row[i]) > 1e-6f * (std::fabs(row[i]) + 1e-3f);
            for (unsigned t = 0; t < tokens; ++t)
            {
                double ref = 0, mag = 0;
                for (long i = 0; i < cols; ++i)
                {
                    ref += static_cast<double>(row[i]) * x[t * stride + i];
                    mag += std::fabs(static_cast<double>(row[i]) * x[t * stride + i]);
                }
                const double e = 0.5 * ref + before[t * y_stride + r];
                worst = std::max(worst, std::fabs(y[t * y_stride + r] - e) / (0.5 * mag + 1e-30));
            }
        }
        check(encoded && apart == 0, std::string("dequant_") + name + ": " + std::to_string(apart) + " of "
            + std::to_string(rows * cols) + " values apart from qk_dequantize_row");
        check(worst < 1e-5, std::string("gemm after dequant_") + name + ", " + std::to_string(tokens)
            + " tokens: largest error " + sci(worst));
    }

    void test_norm_rope_gate(metal_device& gpu)
    {
        // Des lignes de 2560 valeurs, puis des têtes de 128 normalisées sur place.
        for (unsigned width : { 2560u, 128u })
        {
            const unsigned rows = 5, stride = width + 16;
            metal_buffer X = gpu.allocate(sizeof(float) * stride * rows), G = gpu.allocate(sizeof(float) * width);
            float* x = static_cast<float*>(X.contents());
            float* g = static_cast<float*>(G.contents());
            for (size_t i = 0; i < stride * rows; ++i) x[i] = uniform(-3.0f, 3.0f);
            for (unsigned i = 0; i < width; ++i) g[i] = uniform(0.5f, 1.5f);
            const std::vector<float> in(x, x + stride * rows);
            auto c = gpu.begin();
            c->rms_norm(X, G, X, rows, width, stride, stride, 1e-6f);
            c->finish();
            double worst = 0;
            for (unsigned r = 0; r < rows; ++r)
            {
                double ss = 0;
                for (unsigned i = 0; i < width; ++i) ss += static_cast<double>(in[r * stride + i]) * in[r * stride + i];
                const double scale = 1.0 / std::sqrt(ss / width + 1e-6);
                for (unsigned i = 0; i < width; ++i)
                {
                    const double e = in[r * stride + i] * scale * g[i];
                    worst = std::max(worst, std::fabs(x[r * stride + i] - e) / (std::fabs(e) + 1e-3));
                }
            }
            check(worst < 1e-5, "rms_norm of rows of " + std::to_string(width) + ", in place: largest error "
                + sci(worst));
        }
        // Les positions rotatives, neox et par paires, sur 3 tokens de 4 têtes de 128 valeurs, 48 paires
        // tournées.
        for (bool neox : { true, false })
        {
            const unsigned tokens = 3, heads = 4, hd = 128, half = 48, stride = heads * hd, cs_stride = 2 * half + 4;
            metal_buffer X = gpu.allocate(sizeof(float) * stride * tokens), CS = gpu.allocate(sizeof(float) * cs_stride * tokens);
            float* x = static_cast<float*>(X.contents());
            float* cs = static_cast<float*>(CS.contents());
            for (size_t i = 0; i < stride * tokens; ++i) x[i] = uniform(-1.0f, 1.0f);
            for (unsigned t = 0; t < tokens; ++t)
                for (unsigned i = 0; i < half; ++i)
                {
                    const float a = uniform(0.0f, 6.28f);
                    cs[t * cs_stride + i] = std::cos(a);
                    cs[t * cs_stride + half + i] = std::sin(a);
                }
            std::vector<float> e(x, x + stride * tokens);
            for (unsigned t = 0; t < tokens; ++t)
                for (unsigned h = 0; h < heads; ++h)
                    for (unsigned i = 0; i < half; ++i)
                    {
                        float* r = e.data() + t * stride + h * hd;
                        const unsigned i0 = neox ? i : 2 * i, i1 = neox ? i + half : 2 * i + 1;
                        const float c0 = cs[t * cs_stride + i], s0 = cs[t * cs_stride + half + i];
                        const float x0 = r[i0], x1 = r[i1];
                        r[i0] = x0 * c0 - x1 * s0;
                        r[i1] = x0 * s0 + x1 * c0;
                    }
            auto c = gpu.begin();
            c->rope(X, CS, tokens, heads, hd, half, neox, stride, cs_stride);
            c->finish();
            double worst = 0;
            for (size_t i = 0; i < e.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(x[i] - e[i])));
            check(worst < 1e-6, std::string("rope ") + (neox ? "neox" : "in pairs") + ": largest error "
                + sci(worst));
        }
        // La porte du bloc feed-forward, et un biais.
        {
            const unsigned n = 9728;
            metal_buffer A = gpu.allocate(sizeof(float) * n), B = gpu.allocate(sizeof(float) * n);
            float* a = static_cast<float*>(A.contents());
            float* b = static_cast<float*>(B.contents());
            for (unsigned i = 0; i < n; ++i)
            {
                a[i] = uniform(-8.0f, 8.0f);
                b[i] = uniform(-2.0f, 2.0f);
            }
            std::vector<float> e(a, a + n);
            const std::vector<float> b_before(b, b + n);
            qk_silu_mul(e.data(), b, n);
            auto c = gpu.begin();
            c->silu_mul(A, B, n);
            // Puis les portes ajoutées aux 1000 premières valeurs de up, comme un biais de 1000 valeurs
            // sur 1 ligne.
            c->add_bias(B, A, 1000, 1000, 1);
            c->finish();
            double worst = 0;
            for (unsigned i = 0; i < n; ++i) worst = std::max(worst, std::fabs(a[i] - e[i]) / (std::fabs(e[i]) + 1e-3));
            bool bias = true;
            for (unsigned i = 0; i < n; ++i) bias = bias && b[i] == (i < 1000 ? b_before[i] + a[i] : b_before[i]);
            check(worst < 1e-5, "silu_mul: largest error " + sci(worst));
            check(bias, "add_bias: the bias added to its values, the others untouched");
        }
    }

    /*
        Les clés et valeurs d'une séquence de L positions dans un pool de blocs de 16 positions : le
        processeur écrit les L - T premières, le GPU les T dernières ; puis l'attention des T tokens,
        chacun sur les positions jusqu'à la sienne, comparée à une somme en double précision sur les
        clés et valeurs telles que le pool les contient.
    */
    void test_attention(metal_device& gpu, kv_format format)
    {
        const bool q8 = format == kv_format::q8;
        const long layers = 2, heads = 2, hd = 128, B = 16, L = 300, layer = 1;
        const unsigned T = 3, rep = 4, H = heads * rep;
        kv_pool pool(layers, heads, hd, B, 64 * 1024 * 1024, format);
        std::vector<int> blocks;
        for (long p = 0; p < L; p += B) blocks.push_back(pool.allocate());
        size_t region_offset = 0;
        metal_buffer region = gpu.wrap(pool.region(), pool.region_bytes(), region_offset);
        check(region.valid() && region_offset == 0, "the region of the pool taken by the GPU without copy");
        if (!region.valid()) return;

        metal_device::commands::kv_layout layout;
        layout.hd = hd;
        layout.heads = heads;
        layout.positions = B;
        layout.q8 = q8;
        layout.block_stride = pool.block_offset(1);
        layout.k_slab = pool.slab_offset(false, layer, 0);
        layout.v_slab = pool.slab_offset(true, layer, 0);
        layout.head_stride = pool.slab_offset(false, layer, 1) - pool.slab_offset(false, layer, 0);

        std::vector<float> keys(L * heads * hd), values(L * heads * hd);
        for (auto& v : keys) v = uniform(-2.0f, 2.0f);
        for (auto& v : values) v = uniform(-1.0f, 1.0f);
        // Le processeur écrit les premières positions comme le fait cpu_runtime::store_keys_values().
        auto cpu_store = [&](long p, long h, int8_t* kq, uint16_t* ks, int8_t* vq, uint16_t* vs, uint16_t* kh,
                             uint16_t* vh) {
            const float* k = keys.data() + (p * heads + h) * hd;
            const float* v = values.data() + (p * heads + h) * hd;
            if (q8)
            {
                std::vector<float> rotated(k, k + hd);
                qk_rotate_row(rotated.data(), hd);
                qk_float_to_q8_row(rotated.data(), kq, ks, hd);
                qk_float_to_q8_row(v, vq, vs, hd);
            }
            else
            {
                qk_float_to_half_row(k, kh, hd);
                qk_float_to_half_row(v, vh, hd);
            }
        };
        for (long p = 0; p < L - T; ++p)
            for (long h = 0; h < heads; ++h)
            {
                const int b = blocks[p / B];
                const long r = p % B;
                cpu_store(p, h, reinterpret_cast<int8_t*>(pool.keys(b, layer, h)) + r * hd,
                    pool.key_scales(b, layer, h) + r * (hd / 32),
                    reinterpret_cast<int8_t*>(pool.values(b, layer, h)) + r * hd,
                    pool.value_scales(b, layer, h) + r * (hd / 32),
                    reinterpret_cast<uint16_t*>(pool.keys(b, layer, h)) + r * hd,
                    reinterpret_cast<uint16_t*>(pool.values(b, layer, h)) + r * hd);
            }
        // Le GPU écrit les T dernières, et le processeur ce qu'il aurait écrit, pour la comparaison.
        metal_buffer K = gpu.allocate(sizeof(float) * T * heads * hd), V = gpu.allocate(sizeof(float) * T * heads * hd);
        metal_buffer BL = gpu.allocate(sizeof(int) * T), RW = gpu.allocate(sizeof(int) * T);
        for (unsigned t = 0; t < T; ++t)
        {
            const long p = L - T + t;
            std::memcpy(static_cast<float*>(K.contents()) + t * heads * hd, keys.data() + p * heads * hd, sizeof(float) * heads * hd);
            std::memcpy(static_cast<float*>(V.contents()) + t * heads * hd, values.data() + p * heads * hd, sizeof(float) * heads * hd);
            static_cast<int*>(BL.contents())[t] = blocks[p / B];
            static_cast<int*>(RW.contents())[t] = static_cast<int>(p % B);
        }
        {
            auto c = gpu.begin();
            c->kv_store(K, V, heads * hd, region, BL, RW, T, layout);
            c->finish();
        }
        long differ = 0, scales_differ = 0, compared = 0;
        for (unsigned t = 0; t < T; ++t)
            for (long h = 0; h < heads; ++h)
            {
                const long p = L - T + t;
                const int b = blocks[p / B];
                const long r = p % B;
                std::vector<int8_t> kq(hd), vq(hd);
                std::vector<uint16_t> ks(hd / 32), vs(hd / 32), kh(hd), vh(hd);
                cpu_store(p, h, kq.data(), ks.data(), vq.data(), vs.data(), kh.data(), vh.data());
                if (q8)
                {
                    const int8_t* gk = reinterpret_cast<const int8_t*>(pool.keys(b, layer, h)) + r * hd;
                    const int8_t* gv = reinterpret_cast<const int8_t*>(pool.values(b, layer, h)) + r * hd;
                    for (long i = 0; i < hd; ++i)
                    {
                        differ += std::abs(gk[i] - kq[i]) > 1 || std::abs(gv[i] - vq[i]) > 1;
                        compared += 2;
                    }
                    for (long g = 0; g < hd / 32; ++g)
                        scales_differ += pool.key_scales(b, layer, h)[r * (hd / 32) + g] != ks[g]
                            || pool.value_scales(b, layer, h)[r * (hd / 32) + g] != vs[g];
                }
                else
                {
                    const uint16_t* gk = reinterpret_cast<const uint16_t*>(pool.keys(b, layer, h)) + r * hd;
                    const uint16_t* gv = reinterpret_cast<const uint16_t*>(pool.values(b, layer, h)) + r * hd;
                    for (long i = 0; i < hd; ++i)
                    {
                        differ += gk[i] != kh[i] || gv[i] != vh[i];
                        compared += 2;
                    }
                }
            }
        check(differ == 0 && scales_differ == 0, std::string("kv_store ") + (q8 ? "q8" : "f16") + ": "
            + std::to_string(differ) + " of " + std::to_string(compared) + " values and " + std::to_string(scales_differ)
            + " scales apart from the processor");

        // L'attention des T tokens, les requêtes préparées par le GPU.
        const unsigned q_stride = H * hd + 32;
        const float scale = 1.0f / std::sqrt(static_cast<float>(hd));
        metal_buffer Q = gpu.allocate(sizeof(float) * q_stride * T), C = gpu.allocate(sizeof(float) * q_stride * T);
        metal_buffer LEN = gpu.allocate(sizeof(int) * T), TABLE = gpu.allocate(sizeof(int) * blocks.size() * T);
        float* q = static_cast<float*>(Q.contents());
        for (size_t i = 0; i < q_stride * T; ++i) q[i] = uniform(-1.0f, 1.0f);
        const std::vector<float> queries(q, q + q_stride * T);
        const unsigned table_stride = static_cast<unsigned>(blocks.size());
        for (unsigned t = 0; t < T; ++t)
        {
            static_cast<int*>(LEN.contents())[t] = static_cast<int>(L - T + t + 1);
            for (size_t j = 0; j < blocks.size(); ++j) static_cast<int*>(TABLE.contents())[t * table_stride + j] = blocks[j];
        }
        const unsigned parts = static_cast<unsigned>((L + metal_device::attention_part - 1) / metal_device::attention_part);
        metal_buffer PART = gpu.allocate(sizeof(float) * T * heads * parts * rep * (hd + 2));
        {
            auto c = gpu.begin();
            c->prepare_queries(Q, T, H, hd, q_stride, scale, q8);
            c->attention(Q, region, TABLE, LEN, table_stride, T, rep, parts, q_stride, PART, C, layout);
            c->finish();
        }
        // La référence : les clés et valeurs telles que le pool les contient, développées, en double
        // précision.
        double worst = 0;
        const float* ctx = static_cast<const float*>(C.contents());
        for (unsigned t = 0; t < T; ++t)
            for (unsigned hq = 0; hq < H; ++hq)
            {
                const long h = hq / rep;
                std::vector<float> qr(queries.begin() + t * q_stride + hq * hd, queries.begin() + t * q_stride + hq * hd + hd);
                for (auto& v : qr) v *= scale;
                if (q8) qk_rotate_row(qr.data(), hd);
                const long seen = L - T + t + 1;
                std::vector<double> s(seen);
                double mx = -1e300;
                std::vector<float> kf(hd), vf(hd);
                std::vector<std::vector<float>> vals(seen, std::vector<float>(hd));
                for (long p = 0; p < seen; ++p)
                {
                    const int b = blocks[p / B];
                    const long r = p % B;
                    if (q8)
                    {
                        qk_q8_to_float_row(reinterpret_cast<const int8_t*>(pool.keys(b, layer, h)) + r * hd,
                            pool.key_scales(b, layer, h) + r * (hd / 32), kf.data(), hd);
                        qk_q8_to_float_row(reinterpret_cast<const int8_t*>(pool.values(b, layer, h)) + r * hd,
                            pool.value_scales(b, layer, h) + r * (hd / 32), vals[p].data(), hd);
                    }
                    else
                    {
                        qk_half_to_float_row(reinterpret_cast<const uint16_t*>(pool.keys(b, layer, h)) + r * hd, kf.data(), hd);
                        qk_half_to_float_row(reinterpret_cast<const uint16_t*>(pool.values(b, layer, h)) + r * hd,
                            vals[p].data(), hd);
                    }
                    double d = 0;
                    for (long i = 0; i < hd; ++i) d += static_cast<double>(qr[i]) * kf[i];
                    s[p] = d;
                    mx = std::max(mx, d);
                }
                double sum = 0;
                for (long p = 0; p < seen; ++p) sum += std::exp(s[p] - mx);
                for (long i = 0; i < hd; ++i)
                {
                    double o = 0, mag = 0;
                    for (long p = 0; p < seen; ++p)
                    {
                        const double w = std::exp(s[p] - mx) / sum;
                        o += w * vals[p][i];
                        mag += std::fabs(w * vals[p][i]);
                    }
                    worst = std::max(worst, std::fabs(ctx[t * q_stride + hq * hd + i] - o) / (mag + 1e-6));
                }
            }
        check(worst < 1e-4, std::string("attention ") + (q8 ? "q8" : "f16") + " over " + std::to_string(L)
            + " positions in blocks of " + std::to_string(B) + ", " + std::to_string(T) + " tokens: largest error "
            + sci(worst));
    }
}

int main()
{
    std::string why;
    std::unique_ptr<metal_device> gpu = metal_device::open(why);
    if (!gpu)
    {
        std::printf("test_metal: skipped, %s\n", why.c_str());
        return 0;
    }
    std::printf("test_metal: %s, %d kernels\n", gpu->name().c_str(), gpu->kernel_count());
    test_matvec(*gpu, dlib::ggml_type::Q4_K, "mv_q4_K");
    test_matvec(*gpu, dlib::ggml_type::Q6_K, "mv_q6_K");
    test_dequantize_gemm(*gpu, dlib::ggml_type::Q4_K, "q4_K");
    test_dequantize_gemm(*gpu, dlib::ggml_type::Q6_K, "q6_K");
    test_norm_rope_gate(*gpu);
    test_attention(*gpu, kv_format::q8);
    test_attention(*gpu, kv_format::f16);
    std::printf(failures ? "test_metal: %d checks failed\n" : "test_metal: all checks passed\n", failures);
    return failures;
}
