// Sonde du fork (hors moteur) : le GPU du Mac lit-il les poids Q4_K / Q6_K plus vite que le processeur ?
// usage : [Q4=mv3_q4_K] [Q6=mv_q6_K] [ROWS=4] [SGS=4] [QUICK=1] metal-sonde kernels.metal modele.gguf [threads]
// Voir README.md.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <dlib/external/albireo/engine/model_file.h>
#include <dlib/external/albireo/engine/qkernels.h>
#include <dlib/external/albireo/engine/thread_pool.h>
using namespace albireo;
typedef std::chrono::steady_clock clk;
static double ms(clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); }

struct mat { std::string name; dlib::ggml_type type; long rows, cols; const uint8_t* data; size_t bytes; id<MTLBuffer> buf; size_t off; long layer; };

int main(int argc, char** argv) { @autoreleasepool {
    std::ifstream src(argv[1]); std::stringstream ss;
    const long ROWS = getenv("ROWS") ? atol(getenv("ROWS")) : 4, SGS = getenv("SGS") ? atol(getenv("SGS")) : 2;
    ss << "#define ROWS " << ROWS << "\n#define SG_PER_TG " << SGS << "\n" << src.rdbuf();
    model_file mf(argv[2]);
    const int threads = argc > 3 ? atoi(argv[3]) : 8;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    NSError* err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:ss.str().c_str()] options:nil error:&err];
    if (!lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 1; }
    id<MTLComputePipelineState> p4 = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:[NSString stringWithUTF8String:(getenv("Q4") ? getenv("Q4") : "mv_q4_K")]] error:&err];
    id<MTLComputePipelineState> p6 = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:[NSString stringWithUTF8String:(getenv("Q6") ? getenv("Q6") : "mv_q6_K")]] error:&err];
    id<MTLComputePipelineState> pt = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"tiny"] error:&err];
    printf("%s, mémoire unifiée %d, max %llu Mo\n", dev.name.UTF8String, (int)dev.hasUnifiedMemory, dev.recommendedMaxWorkingSetSize >> 20);

    // Les matrices Q4_K et Q6_K du modèle, enveloppées en place (pages du fichier projeté).
    const size_t page = (size_t)getpagesize();
    std::vector<mat> ms_;
    size_t total = 0;
    for (const auto& info : mf.reader().tensors())
    {
        const std::string& name = info.name; const tensor_view* t = mf.find(name);
        if (!t || t->dims.size() != 2 || (t->type != dlib::ggml_type::Q4_K && t->type != dlib::ggml_type::Q6_K)) continue;
        mat m; m.name = name; m.type = t->type; m.cols = (long)t->dims[0]; m.rows = (long)t->dims[1];
        m.data = t->data; m.bytes = t->nbytes;
        const uintptr_t p = (uintptr_t)t->data, base = p & ~(uintptr_t)(page - 1);
        m.off = p - base;
        const size_t len = (m.off + m.bytes + page - 1) / page * page;
        m.buf = [dev newBufferWithBytesNoCopy:(void*)base length:len options:MTLResourceStorageModeShared deallocator:nil];
        if (!m.buf) { printf("NoCopy refusé pour %s\n", name.c_str()); return 1; }
        m.layer = name.rfind("blk.", 0) == 0 ? atol(name.c_str() + 4) : 1000;
        total += m.bytes;
        ms_.push_back(m);
    }
    std::sort(ms_.begin(), ms_.end(), [](const mat& a, const mat& b) { return a.layer < b.layer; });
    printf("%zu matrices, %.0f Mo\n", ms_.size(), total / 1048576.0);

    std::mt19937 rng(1); std::normal_distribution<float> nd(0, 1);
    long maxc = 0, maxr = 0; for (auto& m : ms_) { maxc = std::max(maxc, m.cols); maxr = std::max(maxr, m.rows); }
    id<MTLBuffer> xb = [dev newBufferWithLength:maxc * 4 options:MTLResourceStorageModeShared];
    float* x = (float*)xb.contents; for (long i = 0; i < maxc; ++i) x[i] = nd(rng);
    std::vector<id<MTLBuffer>> yb; for (auto& m : ms_) yb.push_back([dev newBufferWithLength:m.rows * 4 options:MTLResourceStorageModeShared]);

    auto encode = [&](id<MTLComputeCommandEncoder> enc, size_t i) {
        const mat& m = ms_[i];
        [enc setComputePipelineState:(m.type == dlib::ggml_type::Q4_K ? p4 : p6)];
        [enc setBuffer:m.buf offset:m.off atIndex:0];
        [enc setBuffer:xb offset:0 atIndex:1];
        [enc setBuffer:yb[i] offset:0 atIndex:2];
        [enc setBytes:&m.rows length:8 atIndex:3];
        [enc setBytes:&m.cols length:8 atIndex:4];
        const long per = ROWS * SGS, tgs = (m.rows + per - 1) / per;
        [enc dispatchThreadgroups:MTLSizeMake(tgs, 1, 1) threadsPerThreadgroup:MTLSizeMake(32 * SGS, 1, 1)];
    };
    id<MTLCommandQueue> q = [dev newCommandQueue];
    auto full_pass = [&](double& gpu_ms, int reps = 1) {
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        for (int rp = 0; rp < reps; ++rp) for (size_t i = 0; i < ms_.size(); ++i) encode(enc, i);
        [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
        gpu_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
    };

    // Exactitude : contre un produit en double des lignes décompressées, sur 64 lignes par matrice.
    double g; full_pass(g);
    double worst = 0, worst_cpu = 0;
    std::vector<float> row(maxc);
    std::vector<uint8_t> act(qk_activation_row_bytes(act_format::q8_k, maxc));
    for (size_t i = 0; i < ms_.size(); ++i)
    {
        const mat& m = ms_[i];
        const float* y = (const float*)yb[i].contents;
        qk_quantize_row(qk_activation_format(m.type), x, act.data(), m.cols);
        for (int k = 0; k < 64; ++k)
        {
            const long r = (long)(rng() % m.rows);
            const size_t rb = qk_row_bytes(m.type, m.cols);
            qk_dequantize_row(m.type, m.data + rb * r, row.data(), m.cols);
            double ref = 0, mag = 0;
            for (long c = 0; c < m.cols; ++c) { ref += (double)row[c] * x[c]; mag += std::fabs((double)row[c] * x[c]); }
            worst = std::max(worst, std::fabs(y[r] - ref) / mag);
            const float cpu = qk_dot(m.type, m.data + rb * r, act.data(), m.cols);
            worst_cpu = std::max(worst_cpu, std::fabs(cpu - ref) / mag);
        }
    }
    printf("écart max rapporté à sum|w x| : GPU %.2e, processeur (activations en 8 bits) %.2e\n", worst, worst_cpu);

    // (a) Tous les poids en une commande.
    std::vector<double> gp, wall;
    for (int it = 0; it < 12; ++it) { auto t = clk::now(); full_pass(g); wall.push_back(ms(t)); gp.push_back(g); }
    std::sort(gp.begin(), gp.end()); std::sort(wall.begin(), wall.end());
    { double g5; full_pass(g5, 10); full_pass(g5, 10); printf("(a') soutenu, 10 passes en une commande : %.2f ms par passe -> %.1f Go/s\n", g5 / 10, total / (g5 / 10) / 1e6); }
    {
        // Plafond : lecture pure de toutes les matrices, en une commande, 10 fois.
        id<MTLComputePipelineState> pr = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"read_only"] error:&err];
        for (int pass = 0; pass < 3; ++pass)
        {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            for (int rp = 0; rp < 10; ++rp)
                for (size_t i = 0; i < ms_.size(); ++i)
                {
                    const mat& m = ms_[i];
                    const long n16 = (long)(m.bytes / 16);
                    [enc setComputePipelineState:pr];
                    [enc setBuffer:m.buf offset:(m.off & ~15ul) atIndex:0];
                    [enc setBuffer:yb[i] offset:0 atIndex:2];
                    [enc setBytes:&n16 length:8 atIndex:3];
                    [enc dispatchThreads:MTLSizeMake(std::min<long>(n16, 10 * 1024 * 32), 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                }
            [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
            const double g = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0 / 10;
            if (pass == 2) printf("plafond, lecture pure : %.2f ms par passe -> %.1f Go/s\n", g, total / g / 1e6);
        }
        // Q4_K seuls, Q6_K seuls, soutenus.
        for (int kind = 0; kind < 2; ++kind)
        {
            size_t bytes = 0;
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            for (int rp = 0; rp < 10; ++rp)
                for (size_t i = 0; i < ms_.size(); ++i)
                    if ((ms_[i].type == dlib::ggml_type::Q4_K) == (kind == 0)) { encode(enc, i); if (rp == 0) bytes += ms_[i].bytes; }
            [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
            const double g = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0 / 10;
            printf("%s seuls : %.0f Mo, %.2f ms -> %.1f Go/s\n", kind == 0 ? "Q4_K" : "Q6_K", bytes / 1048576.0, g, bytes / g / 1e6);
        }
    }
    if (getenv("QUICK")) return 0;
    printf("(a) une commande, tous les poids : GPU %.2f ms (médiane %.2f), mur %.2f ms -> %.1f Go/s\n", gp[0], gp[6], wall[6], total / gp[6] / 1e6);

    // (b) Coût d'un aller-retour (une commande minuscule, attendue).
    {
        id<MTLBuffer> tb = [dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
        std::vector<double> v;
        for (int it = 0; it < 300; ++it)
        {
            auto t = clk::now();
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:pt]; [enc setBuffer:tb offset:0 atIndex:0];
            [enc dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
            v.push_back(ms(t) * 1000.0);
        }
        std::sort(v.begin(), v.end());
        printf("(b) aller-retour : médiane %.0f µs, 10e centile %.0f µs, 90e %.0f µs\n", v[150], v[30], v[270]);
    }

    // (c) Une commande par couche, attendue (le schéma de layer_pass sans attention sur le GPU).
    {
        std::vector<double> v;
        for (int it = 0; it < 8; ++it)
        {
            auto t = clk::now();
            size_t i = 0;
            while (i < ms_.size())
            {
                const long L = ms_[i].layer;
                id<MTLCommandBuffer> cb = [q commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                for (; i < ms_.size() && ms_[i].layer == L; ++i) encode(enc, i);
                [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
            }
            v.push_back(ms(t));
        }
        std::sort(v.begin(), v.end());
        printf("(c) une commande attendue par couche : %.2f ms (médiane)\n", v[4]);
    }

    // (d) Une commande par produit (le schéma de matmul_offload pour la génération).
    {
        std::vector<double> v;
        for (int it = 0; it < 4; ++it)
        {
            auto t = clk::now();
            for (size_t i = 0; i < ms_.size(); ++i)
            {
                id<MTLCommandBuffer> cb = [q commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                encode(enc, i); [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
            }
            v.push_back(ms(t));
        }
        std::sort(v.begin(), v.end());
        printf("(d) une commande attendue par produit : %.2f ms (médiane)\n", v[2]);
    }

    // Le processeur, mêmes matrices, un token, noyaux du moteur.
    {
        thread_pool pool(threads);
        qk_scratch scratch;
        std::vector<float> y(maxr);
        std::vector<double> v;
        for (int it = 0; it < 8; ++it)
        {
            auto t = clk::now();
            for (auto& m : ms_)
            {
                weight_matrix W; W.type = m.type; W.data = m.data; W.rows = m.rows; W.cols = m.cols; W.row_bytes = qk_row_bytes(m.type, m.cols);
                qk_matmul(W, x, 1, m.cols, y.data(), m.rows, pool, scratch, false, qk_path::automatic);
            }
            v.push_back(ms(t));
        }
        std::sort(v.begin(), v.end());
        printf("processeur, %d threads, mêmes produits : %.2f ms (médiane) -> %.1f Go/s\n", threads, v[4], total / v[4] / 1e6);
    }
} return 0; }
