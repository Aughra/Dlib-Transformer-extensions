// La perplexité d'un modèle sur un texte, calculée par le moteur de l'Albireo d'origine (fork pour
// macOS, 2026-11-21) : la même découpe que perplexity_runtime de Gulliver et que llama-perplexity,
// pour comparer la justesse des moteurs.  Compilée contre la copie de l'original et son dlib par
// macos/albireo-original/build.sh, sans rien changer à leur code ; sur le processeur seulement
// (l'original n'a pas de GPU sur Mac).
//
//     perplexity_albireo model.gguf texte.txt [contexte=512] [morceaux=0] [f16|q8]

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

#include <dlib/data_io/gguf_reader.h>
#include <dlib/tokenizer/hf_tokenizer.h>
#include "dlib/external/albireo/engine/cpu_runtime.h"
#include "dlib/external/albireo/engine/model_config.h"
#include "dlib/external/albireo/engine/model_file.h"
#include "dlib/external/albireo/engine/thread_pool.h"

using namespace albireo;
using dlib::gguf_reader;
using dlib::hf_tokenizer;

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: perplexity_albireo model.gguf text.txt [context=512] [chunks=0] [f16|q8]\n");
        return 2;
    }
    const long ctx = argc > 3 ? std::atol(argv[3]) : 512;
    long chunks = argc > 4 ? std::atol(argv[4]) : 0;
    const kv_format format = argc > 5 && std::string(argv[5]) == "q8" ? kv_format::q8 : kv_format::f16;
    if (ctx < 16) { std::printf("perplexity_albireo: context must be at least 16\n"); return 2; }

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
        b.tokens = chunk;
        b.segments.push_back({ seq, 0, ctx });
        for (long i = first; i < ctx - 1; ++i) b.logit_rows.push_back(i);
        rt.forward(b, logits);
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
