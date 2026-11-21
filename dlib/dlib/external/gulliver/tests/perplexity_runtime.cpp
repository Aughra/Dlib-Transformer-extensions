// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    La perplexité d'un modèle sur un texte, calculée par le moteur de Gulliver comme llama.cpp la
    calcule (llama-perplexity), pour comparer la justesse des deux moteurs sur le même fichier
    (fork pour macOS, 2026-11-21) :

        perplexity_runtime model.gguf texte.txt [contexte=512] [morceaux=0] [f16|q8] [metal|cpu] [steps|stepsN]

    Le texte est tokenisé en entier (avec le BOS si le modèle le demande), puis coupé en
    morceaux de « contexte » tokens ; le premier token de chaque morceau devient le BOS quand le
    modèle en met un, et seules les prédictions de la seconde moitié du morceau comptent (la
    première sert de contexte), comme dans llama-perplexity.  morceaux = 0 : tous les morceaux
    entiers du texte.  Affiche la perplexité, son incertitude, et la part des positions où le
    token suivant est le plus probable du modèle.  Avec « steps », la première moitié de chaque
    morceau est lue d'un coup et la seconde donnée token par token, comme pendant l'écriture d'une
    réponse : la même perplexité, calculée par le chemin des étapes.  Avec « stepsN » (steps4, steps8),
    la seconde moitié est donnée N tokens à la fois, comme la vérification des brouillons : le chemin
    des étapes de plusieurs tokens.
*/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../common/gguf_reader.h"
#include "../engine/cpu_runtime.h"
#include "../engine/hf_tokenizer.h"
#include "../engine/metal_device.h"
#include "../engine/model_config.h"
#include "../engine/model_file.h"
#include "../engine/thread_pool.h"

using namespace gulliver;

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: perplexity_runtime model.gguf text.txt [context=512] [chunks=0] [f16|q8] [metal]\n");
        return 2;
    }
    const long ctx = argc > 3 ? std::atol(argv[3]) : 512;
    long chunks = argc > 4 ? std::atol(argv[4]) : 0;
    const kv_format format = argc > 5 && std::string(argv[5]) == "q8" ? kv_format::q8 : kv_format::f16;
    const bool metal = argc > 6 && std::string(argv[6]) == "metal";
    const std::string mode = argc > 7 ? argv[7] : "";
    const bool steps = mode.compare(0, 5, "steps") == 0;
    const long group = steps && mode.size() > 5 ? std::max(1L, std::atol(mode.c_str() + 5)) : 1;
    if (ctx < 16) { std::printf("perplexity_runtime: context must be at least 16\n"); return 2; }

    std::string text;
    {
        std::ifstream in(argv[2], std::ios::binary);
        if (!in) { std::printf("cannot read %s\n", argv[2]); return 2; }
        std::ostringstream s;
        s << in.rdbuf();
        text = s.str();
    }

    hf_tokenizer tok;
    {
        gguf_reader g(argv[1]);
        tok.load_from_gguf(g);
    }
    const bool bos = tok.add_bos_default() && tok.bos_id() >= 0;
    std::vector<int> tokens = tok.encode(text, bos, false, false);
    const long available = static_cast<long>(tokens.size()) / ctx;
    if (chunks <= 0 || chunks > available) chunks = available;
    if (chunks < 1) { std::printf("the text holds %zu tokens, fewer than one chunk of %ld\n", tokens.size(), ctx); return 2; }

    auto file = std::make_shared<model_file>(argv[1]);
    const model_config cfg = read_model_config(*file);
    thread_pool pool(static_cast<int>(std::thread::hardware_concurrency()));
    const size_t per_token = 2 * static_cast<size_t>(cfg.n_layers) * static_cast<size_t>(cfg.n_kv_heads)
        * kv_row_bytes(format, cfg.head_dim);
    cpu_runtime rt(file, cfg, pool, std::max<size_t>(size_t(1) << 30, per_token * static_cast<size_t>(ctx + 1024)), 256, format);
    std::printf("%s\n", rt.description().c_str());
    std::unique_ptr<metal_device> gpu;
    if (metal)
    {
        std::string why;
        gpu = metal_device::open(why);
        if (!gpu) { std::printf("no GPU: %s\n", why.c_str()); return 1; }
        rt.set_gpu_prompts(24, ctx + 64);
        rt.set_gpu(*gpu, [](int, const std::string& m) { std::printf("%s\n", m.c_str()); });
        rt.check_gpu();
    }
    file->warm_up(false);

    const long V = cfg.vocab;
    const long first = ctx / 2;
    double nll = 0, nll2 = 0;
    long count = 0, top1 = 0;
    std::vector<float> logits;
    for (long c = 0; c < chunks; ++c)
    {
        std::vector<int> chunk(tokens.begin() + c * ctx, tokens.begin() + (c + 1) * ctx);
        if (bos) chunk[0] = tok.bos_id();
        const int seq = rt.create_sequence();
        rt.reserve_sequence(seq, ctx);
        forward_batch b;
        if (!steps)
        {
            b.tokens = chunk;
            b.segments.push_back({ seq, 0, ctx });
            for (long i = first; i < ctx - 1; ++i) b.logit_rows.push_back(i);
            rt.forward(b, logits);
        }
        else
        {
            // Le début lu d'un coup jusqu'à la position first, puis un token à la fois.
            std::vector<float> all, one;
            b.tokens.assign(chunk.begin(), chunk.begin() + first + 1);
            b.segments.push_back({ seq, 0, first + 1 });
            b.logit_rows.push_back(first);
            rt.forward(b, one);
            all.insert(all.end(), one.begin(), one.end());
            for (long i = first + 1; i < ctx - 1; i += group)
            {
                const long n = std::min(group, ctx - 1 - i);
                b.clear();
                for (long j = 0; j < n; ++j)
                {
                    b.tokens.push_back(chunk[static_cast<size_t>(i + j)]);
                    b.logit_rows.push_back(j);
                }
                b.segments.push_back({ seq, 0, n });
                rt.forward(b, one);
                all.insert(all.end(), one.begin(), one.end());
            }
            logits.swap(all);
        }
        rt.release_sequence(seq);
        for (long r = 0; r < ctx - 1 - first; ++r)
        {
            const float* row = logits.data() + r * V;
            const int next = chunk[static_cast<size_t>(first + r + 1)];
            const double m = *std::max_element(row, row + V);
            double sum = 0;
            for (long i = 0; i < V; ++i) sum += std::exp(static_cast<double>(row[i]) - m);
            const double lp = static_cast<double>(row[next]) - m - std::log(sum);
            nll -= lp;
            nll2 += lp * lp;
            ++count;
            if (std::max_element(row, row + V) - row == next) ++top1;
        }
        std::printf("[%ld] %.4f\n", c + 1, std::exp(nll / count));
        std::fflush(stdout);
    }
    const double mean = nll / count;
    const double var = nll2 / count - mean * mean;
    const double err = std::exp(mean) * std::sqrt(std::max(0.0, var) / std::max<long>(1, count - 1));
    std::printf("tokens: %zu, chunks: %ld of %ld, scored positions: %ld\n", tokens.size(), chunks, ctx, count);
    std::printf("perplexity: %.4f +/- %.4f\n", std::exp(mean), err);
    std::printf("top-1: %.2f %%\n", 100.0 * top1 / count);
    return 0;
}
