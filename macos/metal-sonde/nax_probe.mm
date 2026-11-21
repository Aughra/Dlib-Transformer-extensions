// Sonde des accélérateurs neuronaux du M5 (fork pour macOS, 2026-11-21).
//
//   clang++ -std=c++17 -O2 -fobjc-arc -o build/nax-sonde macos/metal-sonde/nax_probe.mm \
//     -framework Metal -framework MetalPerformanceShaders -framework Foundation
//   build/nax-sonde macos/metal-sonde/nax_kernels.metal [M=512] [N=9728] [K=2560] [essais=20]
//
// Le même produit C (M x N, floats) = A (M x K) . B^T (B : N x K), en demi-précision, par trois
// chemins : MPSMatrixMultiplication (ce que Gulliver utilise pour les produits des prompts), un noyau
// simdgroup_matrix et un noyau sur les accélérateurs du M5 (MetalPerformancePrimitives).  Pour
// chacun : l'écart à un produit en double précision sur 256 sorties tirées au hasard, puis le temps
// moyen de « essais » produits enchaînés dans une commande, en mesures alternées (MPS, sg, NAX, puis
// de nouveau), et le débit en milliers de milliards d'opérations par seconde.
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <Foundation/Foundation.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

struct dims { uint32_t M, N, K; };

int main(int argc, char** argv)
{
    @autoreleasepool {
        if (argc < 2) { std::printf("usage: nax-sonde nax_kernels.metal [M] [N] [K] [essais]\n"); return 2; }
        const uint32_t M = argc > 2 ? std::atoi(argv[2]) : 512, N = argc > 3 ? std::atoi(argv[3]) : 9728,
            K = argc > 4 ? std::atoi(argv[4]) : 2560;
        const int rounds = argc > 5 ? std::atoi(argv[5]) : 20;
        if (M % 64 || N % 128 || K % 16) { std::printf("M multiple de 64, N de 128, K de 16\n"); return 2; }
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        std::printf("GPU : %s\n", [[dev name] UTF8String]);

        NSError* err = nil;
        NSString* src = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]
            encoding:NSUTF8StringEncoding error:&err];
        if (!src) { std::printf("lecture des noyaux : %s\n", [[err description] UTF8String]); return 1; }
        MTLCompileOptions* opt = [MTLCompileOptions new];
        opt.languageVersion = MTLLanguageVersion4_0;
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:opt error:&err];
        if (!lib) { std::printf("compilation : %s\n", [[err description] UTF8String]); return 1; }
        auto pipe = [&](const char* name) {
            id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:name]];
            NSError* e = nil;
            id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:f error:&e];
            if (!p) std::printf("%s : %s\n", name, [[e description] UTF8String]);
            return p;
        };
        id<MTLComputePipelineState> p_sg = pipe("gemm_sg"), p_nax = pipe("gemm_nax");
        if (!p_sg || !p_nax) return 1;

        // Données au hasard, de l'ordre de grandeur des activations et des poids d'un modèle.
        std::mt19937 rng(7);
        std::normal_distribution<float> g(0.0f, 1.0f);
        id<MTLBuffer> bA = [dev newBufferWithLength:size_t(M) * K * 2 options:MTLResourceStorageModeShared];
        id<MTLBuffer> bB = [dev newBufferWithLength:size_t(N) * K * 2 options:MTLResourceStorageModeShared];
        __fp16* A = (__fp16*)[bA contents];
        __fp16* B = (__fp16*)[bB contents];
        for (size_t i = 0; i < size_t(M) * K; ++i) A[i] = (__fp16)g(rng);
        for (size_t i = 0; i < size_t(N) * K; ++i) B[i] = (__fp16)(0.03f * g(rng));
        id<MTLBuffer> bC[3];
        for (auto& b : bC) b = [dev newBufferWithLength:size_t(M) * N * 4 options:MTLResourceStorageModeShared];
        dims d = { M, N, K };
        id<MTLCommandQueue> q = [dev newCommandQueue];

        MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc] initWithDevice:dev transposeLeft:NO
            transposeRight:YES resultRows:M resultColumns:N interiorColumns:K alpha:1.0 beta:0.0];
        auto mat = [&](id<MTLBuffer> b, uint32_t r, uint32_t c, MPSDataType t, size_t bytes) {
            return [[MPSMatrix alloc] initWithBuffer:b descriptor:[MPSMatrixDescriptor
                matrixDescriptorWithRows:r columns:c rowBytes:c * bytes dataType:t]];
        };
        MPSMatrix* mA = mat(bA, M, K, MPSDataTypeFloat16, 2);
        MPSMatrix* mB = mat(bB, N, K, MPSDataTypeFloat16, 2);
        MPSMatrix* mC = mat(bC[0], M, N, MPSDataTypeFloat32, 4);

        // Un essai : « count » produits du chemin « path » dans une commande ; renvoie le temps moyen (ms).
        auto run = [&](int path, int count) {
            id<MTLCommandBuffer> cb = [q commandBuffer];
            for (int r = 0; r < count; ++r)
            {
                if (path == 0)
                {
                    [mm encodeToCommandBuffer:cb leftMatrix:mA rightMatrix:mB resultMatrix:mC];
                    continue;
                }
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                [enc setComputePipelineState:path == 1 ? p_sg : p_nax];
                [enc setBuffer:bA offset:0 atIndex:0];
                [enc setBuffer:bB offset:0 atIndex:1];
                [enc setBuffer:bC[path] offset:0 atIndex:2];
                [enc setBytes:&d length:sizeof d atIndex:3];
                [enc dispatchThreadgroups:MTLSizeMake(N / 128, M / 64, 1) threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];
                [enc endEncoding];
            }
            auto t = std::chrono::steady_clock::now();
            [cb commit];
            [cb waitUntilCompleted];
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
            if ([cb error]) std::printf("erreur du GPU : %s\n", [[[cb error] description] UTF8String]);
            return ms / count;
        };

        const char* names[3] = { "MPS (Gulliver aujourd'hui)", "simdgroup_matrix 8 x 8", "accélérateurs (MPP)" };
        // Exactitude.
        for (int p = 0; p < 3; ++p) run(p, 1);
        std::uniform_int_distribution<uint32_t> um(0, M - 1), un(0, N - 1);
        for (int p = 0; p < 3; ++p)
        {
            const float* C = (const float*)[bC[p] contents];
            double worst = 0, scale = 0;
            std::mt19937 pick(11);
            for (int s = 0; s < 256; ++s)
            {
                const uint32_t i = um(pick), j = un(pick);
                double ref = 0;
                for (uint32_t k = 0; k < K; ++k) ref += double(A[size_t(i) * K + k]) * double(B[size_t(j) * K + k]);
                worst = std::max(worst, std::fabs(ref - C[size_t(i) * N + j]));
                scale = std::max(scale, std::fabs(ref));
            }
            std::printf("%-28s écart max %.3g (valeurs jusqu'à %.3g, relatif %.2g)\n", names[p], worst, scale, worst / scale);
        }
        // Vitesse, en alternance.
        for (int p = 0; p < 3; ++p) run(p, 3);
        double best[3] = { 1e30, 1e30, 1e30 };
        std::vector<double> all[3];
        for (int round = 0; round < 3; ++round)
            for (int p = 0; p < 3; ++p)
            {
                const double ms = run(p, rounds);
                all[p].push_back(ms);
                best[p] = std::min(best[p], ms);
            }
        const double flops = 2.0 * M * N * K;
        for (int p = 0; p < 3; ++p)
        {
            std::printf("%-28s", names[p]);
            for (double ms : all[p]) std::printf(" %.3f", ms);
            std::printf(" ms  -> %.1f Tflop/s (meilleur essai)\n", flops / (best[p] * 1e-3) / 1e12);
        }
    }
    return 0;
}
