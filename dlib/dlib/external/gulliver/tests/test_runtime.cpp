// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Vérifie le runtime du processeur sur un vrai modèle.

        test_runtime model.gguf reference.bin [exact] id id id ...

    reference.bin contient les logits de chaque position de la suite de tokens calculés par
    tests/reference_forward.py.  Le programme les compare aux logits du runtime, puis vérifie
    que le runtime donne les mêmes résultats quelle que soit la découpe du travail : toute la
    suite en une passe, un prompt suivi d'un token à la fois, deux séquences partageant un lot,
    et une séquence tronquée puis prolongée à nouveau ; les clés et valeurs sur 8 bits
    concordent ensuite avec celles en demi-précision.

    Avec "exact", les produits tournent en flottants (qk_set_exact_products) et la comparaison
    avec la référence est serrée ; sinon, les activations sont quantifiées comme en
    fonctionnement normal et la comparaison porte sur les distributions.

        test_runtime synthetic [folder]

    vérifie l'attention sur le tout petit modèle de tiny_gguf.h, écrit dans folder : la
    répartition des positions entre les threads d'une seule étape comparée à l'attention d'une
    tâche par tête de clés et valeurs, avec les clés et valeurs en demi-précision et sur 8 bits.
*/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../engine/cpu_runtime.h"
#include "tiny_gguf.h"

using namespace albireo;

namespace
{
    int failures = 0;

    struct stats
    {
        double max_abs = 0;     // plus grand écart de logit
        double scale = 0;       // plus grande valeur absolue de logit de référence
        double mean_kl = 0;     // KL(reference || runtime) moyenne sur les positions
        double top1 = 0;        // part des positions qui ont le même argmax
    };

    stats compare(const float* ref, const float* got, long rows, long vocab)
    {
        stats s;
        long same = 0;
        for (long r = 0; r < rows; ++r)
        {
            const float* a = ref + r * vocab;
            const float* b = got + r * vocab;
            double ma = -1e30, mb = -1e30;
            long ia = 0, ib = 0;
            for (long v = 0; v < vocab; ++v)
            {
                s.max_abs = std::max(s.max_abs, static_cast<double>(std::fabs(a[v] - b[v])));
                s.scale = std::max(s.scale, static_cast<double>(std::fabs(a[v])));
                if (a[v] > ma) { ma = a[v]; ia = v; }
                if (b[v] > mb) { mb = b[v]; ib = v; }
            }
            if (ia == ib) ++same;
            double za = 0, zb = 0;
            for (long v = 0; v < vocab; ++v)
            {
                za += std::exp(a[v] - ma);
                zb += std::exp(b[v] - mb);
            }
            double kl = 0;
            for (long v = 0; v < vocab; ++v)
            {
                const double pa = std::exp(a[v] - ma) / za;
                const double lpa = a[v] - ma - std::log(za);
                const double lpb = b[v] - mb - std::log(zb);
                kl += pa * (lpa - lpb);
            }
            s.mean_kl += kl;
        }
        s.mean_kl /= rows;
        s.top1 = static_cast<double>(same) / rows;
        return s;
    }

    void report(const char* what, const stats& s)
    {
        std::printf("  %-34s max |diff| %.3g (logits up to %.3g), mean KL %.3g, top-1 %.1f%%\n",
            what, s.max_abs, s.scale, s.mean_kl, 100.0 * s.top1);
    }

    void expect(bool ok, const char* what)
    {
        if (!ok)
        {
            ++failures;
            std::printf("FAILED: %s\n", what);
        }
    }

    std::vector<float> run_whole(cpu_runtime& rt, const std::vector<int>& tokens)
    {
        const int seq = rt.create_sequence();
        expect(rt.reserve_sequence(seq, static_cast<long>(tokens.size())), "reserve");
        forward_batch b;
        b.tokens = tokens;
        b.segments.push_back({ seq, 0, static_cast<long>(tokens.size()) });
        for (long i = 0; i < static_cast<long>(tokens.size()); ++i) b.logit_rows.push_back(i);
        std::vector<float> logits;
        rt.forward(b, logits);
        expect(rt.sequence_length(seq) == static_cast<long>(tokens.size()), "sequence length after a pass");
        rt.release_sequence(seq);
        return logits;
    }
}

namespace
{
    // Les logits des étapes d'un token qui suivent un prompt formé des premiers tokens du prompt, lus
    // en une passe.
    std::vector<float> run_steps(cpu_runtime& rt, const std::vector<int>& tokens, long prompt)
    {
        const long T = static_cast<long>(tokens.size());
        const long V = rt.config().vocab;
        const int seq = rt.create_sequence();
        std::vector<float> got, logits;
        forward_batch b;
        expect(rt.reserve_sequence(seq, prompt), "reserve the prompt");
        b.tokens.assign(tokens.begin(), tokens.begin() + prompt);
        b.segments.push_back({ seq, 0, prompt });
        b.logit_rows.push_back(prompt - 1);
        rt.forward(b, logits);
        for (long i = prompt; i < T; ++i)
        {
            expect(rt.reserve_sequence(seq, i + 1), "reserve a step");
            b.clear();
            b.tokens.push_back(tokens[static_cast<size_t>(i)]);
            b.segments.push_back({ seq, 0, 1 });
            b.logit_rows.push_back(0);
            rt.forward(b, logits);
            got.insert(got.end(), logits.begin(), logits.begin() + V);
        }
        rt.release_sequence(seq);
        return got;
    }

    int synthetic(const std::string& folder)
    {
        const std::string path = folder + "test_runtime_synthetic.gguf";
        const albireo_test::tiny_gguf model = albireo_test::tiny_gguf_writer().head_dim(32).build();
        if (!model.save(path, model.bytes))
        {
            std::printf("cannot write %s\n", path.c_str());
            return 2;
        }
        {
            auto file = std::make_shared<model_file>(path);
            const model_config cfg = read_model_config(*file);
            std::printf("%s\n", describe(cfg).c_str());
            std::vector<int> tokens(1100);
            uint32_t state = 7;
            for (auto& t : tokens)
            {
                state = state * 1664525u + 1013904223u;
                t = static_cast<int>((state >> 8) % 256);
            }
            const long prompt = 1024;
            const long steps = static_cast<long>(tokens.size()) - prompt;
            const long V = cfg.vocab;

            /*
                Deux threads prennent une tâche par tête de clés et valeurs d'une étape ; douze se
                partagent les positions de l'étape (cpu_runtime::attention).  Des blocs de 16
                positions, pour que les plages commencent à l'intérieur des blocs.
            */
            thread_pool narrow(2), wide(12);
            std::vector<float> results[4];
            const kv_format formats[2] = { kv_format::f16, kv_format::q8 };
            for (int f = 0; f < 2; ++f)
            {
                cpu_runtime one(file, cfg, narrow, size_t(64) << 20, 16, formats[f]);
                cpu_runtime split(file, cfg, wide, size_t(64) << 20, 16, formats[f]);
                const size_t row = formats[f] == kv_format::q8 ? 32 + 2 : 64;
                expect(one.kv_bytes_per_token() == 2 * 2 * 2 * row, "bytes of the keys and values of a token");
                results[2 * f] = run_steps(one, tokens, prompt);
                results[2 * f + 1] = run_steps(split, tokens, prompt);
                expect(one.kv_bytes_used() == 0 && split.kv_bytes_used() == 0, "every block returned");
                const stats d = compare(results[2 * f].data(), results[2 * f + 1].data(), steps, V);
                report(f ? "split attention, 8 bits" : "split attention, half precision", d);
                expect(d.max_abs <= 1e-4 * std::max(1.0, d.scale), "split attention against one task per head");
            }
            const stats q = compare(results[0].data(), results[2].data(), steps, V);
            report("8 bits against half precision", q);
            expect(q.mean_kl < 1e-3, "8-bit keys and values against half precision");
        }
        std::remove(path.c_str());
        if (failures)
        {
            std::printf("%d checks FAILED\n", failures);
            return 1;
        }
        std::printf("all checks passed\n");
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "synthetic")
        return synthetic(argc > 2 ? std::string(argv[2]) + "/" : std::string());
    if (argc < 4)
    {
        std::printf("usage: test_runtime model.gguf reference.bin [exact] id...\n");
        return 2;
    }
    int arg = 3;
    bool exact = false;
    if (std::string(argv[arg]) == "exact")
    {
        exact = true;
        ++arg;
    }
    std::vector<int> tokens;
    for (; arg < argc; ++arg) tokens.push_back(std::atoi(argv[arg]));
    const long T = static_cast<long>(tokens.size());

    auto file = std::make_shared<model_file>(argv[1]);
    const model_config cfg = read_model_config(*file);
    std::printf("%s\n", describe(cfg).c_str());
    const long V = cfg.vocab;

    std::vector<float> ref(static_cast<size_t>(T * V));
    {
        std::ifstream in(argv[2], std::ios::binary);
        in.read(reinterpret_cast<char*>(ref.data()), static_cast<std::streamsize>(ref.size() * sizeof(float)));
        if (!in)
        {
            std::printf("cannot read %ld x %ld logits from %s\n", T, V, argv[2]);
            return 2;
        }
    }

    thread_pool pool(2);
    // De petits blocs, pour que chaque séquence en couvre plusieurs.
    cpu_runtime rt(file, cfg, pool, size_t(512) << 20, 16);
    std::printf("%s\n", rt.description().c_str());
    qk_set_exact_products(exact);

    // 1. Toute la suite en une passe, comparée à la référence.
    auto t0 = std::chrono::steady_clock::now();
    const std::vector<float> whole = run_whole(rt, tokens);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const stats s = compare(ref.data(), whole.data(), T, V);
    report(exact ? "reference (exact products)" : "reference (quantized activations)", s);
    std::printf("  one pass of %ld tokens: %.1f ms\n", T, ms);
    if (exact)
    {
        // Les clés et valeurs sont stockées en demi-précision, le seul arrondi qui reste.
        expect(s.mean_kl < 1e-4, "exact distribution");
        expect(s.max_abs <= 0.1 * std::max(1.0, s.scale / 10), "exact logits");
        expect(s.top1 >= 0.99, "exact argmax");
    }
    else
    {
        // Activations sur 8 bits, comme les utilise chaque noyau entier de l'écosystème GGUF.
        expect(s.mean_kl < 0.05, "distribution against the reference");
        expect(s.top1 >= 0.85, "argmax against the reference");
    }

    // 2. Un prompt, puis un token à la fois.
    {
        const long k = std::max<long>(1, T / 3);
        const int seq = rt.create_sequence();
        std::vector<float> got(static_cast<size_t>(T * V));
        std::vector<float> logits;
        forward_batch b;
        expect(rt.reserve_sequence(seq, k), "reserve prompt");
        b.tokens.assign(tokens.begin(), tokens.begin() + k);
        b.segments.push_back({ seq, 0, k });
        for (long i = 0; i < k; ++i) b.logit_rows.push_back(i);
        rt.forward(b, logits);
        std::copy(logits.begin(), logits.end(), got.begin());
        for (long i = k; i < T; ++i)
        {
            expect(rt.reserve_sequence(seq, i + 1), "reserve step");
            b.clear();
            b.tokens.push_back(tokens[static_cast<size_t>(i)]);
            b.segments.push_back({ seq, 0, 1 });
            b.logit_rows.push_back(0);
            rt.forward(b, logits);
            std::copy(logits.begin(), logits.end(), got.begin() + i * V);
        }
        // Le prompt passe par les produits en flottants et les étapes simples par les noyaux
        // entiers, si bien que les deux passes concordent en distribution, pas bit pour bit.
        const stats d = compare(whole.data(), got.data(), T, V);
        report("prompt then single steps", d);
        if (exact)
            expect(d.max_abs <= 1e-3 * std::max(1.0, d.scale), "single steps against one pass");
        else
            expect(d.mean_kl < 0.05 && d.top1 >= 0.85, "single steps against one pass");

        // 4. Troncature : retirer le dernier tiers et le recalculer.
        const long m = std::max<long>(1, 2 * T / 3);
        rt.truncate_sequence(seq, m);
        expect(rt.sequence_length(seq) == m, "length after truncation");
        // Les blocs au-delà des positions gardées sont retournés au pool.
        expect(rt.reserve_sequence(seq, T), "reserve after truncation");
        b.clear();
        b.tokens.assign(tokens.begin() + m, tokens.end());
        b.segments.push_back({ seq, 0, T - m });
        for (long i = 0; i < T - m; ++i) b.logit_rows.push_back(i);
        rt.forward(b, logits);
        const stats r = compare(whole.data() + m * V, logits.data(), T - m, V);
        report("truncated and extended", r);
        if (exact)
            expect(r.max_abs <= 1e-3 * std::max(1.0, r.scale), "truncated sequence");
        else
            expect(r.mean_kl < 0.05 && r.top1 >= 0.85, "truncated sequence");
        rt.release_sequence(seq);
    }

    // 3. Deux séquences dans un lot : toute la suite, et sa seconde moitié comme une
    //    séquence à part qui commence à la position 0.
    {
        const long h = T / 2;
        std::vector<int> second(tokens.begin() + h, tokens.end());
        const std::vector<float> alone = run_whole(rt, second);

        const int a = rt.create_sequence();
        const int c = rt.create_sequence();
        expect(rt.reserve_sequence(a, T) && rt.reserve_sequence(c, T - h), "reserve batch");
        forward_batch b;
        b.tokens = second;
        b.tokens.insert(b.tokens.end(), tokens.begin(), tokens.end());
        b.segments.push_back({ c, 0, T - h });
        b.segments.push_back({ a, T - h, T });
        for (long i = 0; i < static_cast<long>(b.tokens.size()); ++i) b.logit_rows.push_back(i);
        std::vector<float> logits;
        rt.forward(b, logits);
        const stats d1 = compare(alone.data(), logits.data(), T - h, V);
        const stats d2 = compare(whole.data(), logits.data() + (T - h) * V, T, V);
        report("batched, short sequence", d1);
        report("batched, long sequence", d2);
        expect(d1.max_abs <= 1e-3 * std::max(1.0, d1.scale), "batched short sequence");
        expect(d2.max_abs <= 1e-3 * std::max(1.0, d2.scale), "batched long sequence");
        rt.release_sequence(a);
        rt.release_sequence(c);
        expect(rt.kv_bytes_used() == 0, "every block returned");
    }

    // 5. Une séquence qui commence par les positions d'une autre : les blocs pleins sont partagés,
    //    le dernier copié, et une séquence qui écrit dans un bloc partagé le copie d'abord.
    {
        const long B = 16;
        const long h = std::min(T - 1, B + 1 + T / 4);
        // Les passes de moins de tokens passent par les noyaux entiers : elles concordent avec la
        // passe entière en distribution.
        auto agree = [&](const stats& d, long batch_tokens) {
            if (exact || batch_tokens >= qk_float_product_tokens) return d.max_abs <= 1e-3 * std::max(1.0, d.scale);
            return d.mean_kl < 0.05 && d.top1 >= 0.85;
        };
        std::vector<float> logits;
        const int a = rt.create_sequence();
        expect(rt.reserve_sequence(a, h), "reserve before sharing");
        forward_batch b;
        b.tokens.assign(tokens.begin(), tokens.begin() + h);
        b.segments.push_back({ a, 0, h });
        rt.forward(b, logits);
        const size_t before = rt.kv_bytes_used();
        const int c = rt.create_sequence();
        expect(rt.copy_sequence(a, c, h), "share a prefix");
        expect(rt.sequence_length(c) == h, "length of the shared prefix");
        const size_t copied = static_cast<size_t>(h % B != 0 ? B : 0) * rt.kv_bytes_per_token();
        expect(rt.kv_bytes_used() - before == copied, "full blocks held once");

        // Les deux séquences continuent à partir du préfixe partagé, en un lot.
        expect(rt.reserve_sequence(a, T) && rt.reserve_sequence(c, T), "reserve after sharing");
        b.clear();
        b.tokens.assign(tokens.begin() + h, tokens.end());
        b.tokens.insert(b.tokens.end(), tokens.begin() + h, tokens.end());
        b.segments.push_back({ a, 0, T - h });
        b.segments.push_back({ c, T - h, T - h });
        for (long i = 0; i < 2 * (T - h); ++i) b.logit_rows.push_back(i);
        rt.forward(b, logits);
        const stats d1 = compare(whole.data() + h * V, logits.data(), T - h, V);
        const stats d2 = compare(whole.data() + h * V, logits.data() + (T - h) * V, T - h, V);
        report("shared prefix, first sequence", d1);
        report("shared prefix, second sequence", d2);
        expect(agree(d1, 2 * (T - h)), "first sequence after sharing");
        expect(agree(d2, 2 * (T - h)), "second sequence after sharing");

        // La première séquence réécrit un bloc partagé avec d'autres tokens ; la seconde garde
        // ses positions.
        const long cut = B / 2;
        rt.truncate_sequence(a, cut);
        expect(rt.reserve_sequence(a, T), "reserve a shared block for writing");
        b.clear();
        b.tokens.assign(static_cast<size_t>(T - cut), tokens[0]);
        b.segments.push_back({ a, 0, T - cut });
        rt.forward(b, logits);
        rt.truncate_sequence(c, T - 1);
        expect(rt.reserve_sequence(c, T), "reserve the last position again");
        b.clear();
        b.tokens.push_back(tokens[static_cast<size_t>(T - 1)]);
        b.segments.push_back({ c, 0, 1 });
        b.logit_rows.push_back(0);
        rt.forward(b, logits);
        const stats d3 = compare(whole.data() + (T - 1) * V, logits.data(), 1, V);
        report("shared prefix after a write of the other sequence", d3);
        expect(agree(d3, 1), "positions of a shared block kept");
        rt.release_sequence(a);
        rt.release_sequence(c);
        expect(rt.kv_bytes_used() == 0, "every shared block returned");
    }

    /*
        6. Les clés et valeurs sur 8 bits comparées à celles en demi-précision, sur toute la
           suite : leurs écarts restent sous ceux des produits entiers des étapes simples (2),
           quelques millièmes de KL sur les modèles du catalogue.
    */
    if (cfg.head_dim % kv_q8_group == 0)
    {
        cpu_runtime rt8(file, cfg, pool, size_t(512) << 20, 16, kv_format::q8);
        const std::vector<float> q8 = run_whole(rt8, tokens);
        const stats d = compare(whole.data(), q8.data(), T, V);
        report("8-bit keys and values", d);
        expect(d.mean_kl < 0.005, "8-bit keys and values as a distribution");
        expect(d.top1 >= 0.9, "8-bit keys and values, argmax");
        expect(rt8.kv_bytes_used() == 0, "every 8-bit block returned");
    }

    if (failures)
    {
        std::printf("%d checks FAILED\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
