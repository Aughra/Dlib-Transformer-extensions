// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// Le GPU du Mac via Metal (portage macOS).  Compilé avec le comptage automatique des références.

#include "metal_device.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <unistd.h>
#include <vector>

namespace gulliver
{

    namespace
    {
        // Le source des noyaux, embarqué par CMakeLists.txt (metal_kernels.inc.in).
        const char* const metal_source =
#include "metal_kernels.inc"
            ;

        // Les noyaux des accélérateurs neuronaux du M5, compilés à part en Metal 4 (metal_kernels_nax.metal).
        const char* const metal_nax_source =
#include "metal_kernels_nax.inc"
            ;
        // attention_nax, dans l'ordre (rep 1, 2, 4, 8) x (têtes de 64, 128 valeurs).
        const char* const nax_attention_names[] = { "attention_nax_1_64", "attention_nax_1_128", "attention_nax_2_64",
            "attention_nax_2_128", "attention_nax_4_64", "attention_nax_4_128", "attention_nax_8_64",
            "attention_nax_8_128", "attention_nax_1_256", "attention_nax_2_256", "attention_nax_4_256",
            "attention_nax_8_256", "mm_id_nax64_q4_K", "mm_id_nax64_q5_K", "mm_id_nax64_q6_K", "mm_id_nax64_q8_0",
            "mm_id_nax64_q5_0", "mm_id_nax128_q4_K", "mm_id_nax128_q5_K", "mm_id_nax128_q6_K", "mm_id_nax128_q8_0",
            "mm_id_nax128_q5_0", "mv_nax_q4_K", "mv_nax_q6_K", "mv_nax_q5_K", "mv_nax_q8_0", "mv_nax_q5_0" };
        // mm_id_nax, après les douze attention_nax : tuiles de 64 cases, puis de 128 ; puis mv_nax, dans l'ordre des
        // formats des noyaux mv_*_n.
        const int nax_mm_id = 12, nax_mm_id_128 = 17, nax_mv = 22;
        // Les simdgroups d'une tuile d'attention_nax, 16 lignes de requête chacun (GULLIVER_NAX_SG pour les essais).
        unsigned nax_simdgroups()
        {
            static const unsigned n = [] {
                const char* v = std::getenv("GULLIVER_NAX_SG");
                const int k = v ? std::atoi(v) : 8;
                return static_cast<unsigned>(k == 2 || k == 4 ? k : 8);
            }();
            return n;
        }
        int nax_attention_index(unsigned rep, unsigned hd)
        {
            const int r = rep == 1 ? 0 : rep == 2 ? 1 : rep == 4 ? 2 : rep == 8 ? 3 : -1;
            if (r >= 0 && hd == 256 && !std::getenv("GULLIVER_NO_NAX256")) return 8 + r;
            const int h = hd == 64 ? 0 : hd == 128 ? 1 : -1;
            return r < 0 || h < 0 ? -1 : 2 * r + h;
        }

        // Les noyaux, dans l'ordre de l'énumération ci-dessous ; chacun doit se compiler en pipeline.
        // L'attention existe en une variante pour chaque regroupement des têtes de requête (1, 2, 4, 8) et
        // chaque largeur de tête (64, 128, 256 valeurs : 2, 4, 8 par voie ; 512 valeurs, 16 par voie, pour les
        // étapes de Gemma 4), dont le compilateur déroule les boucles.
        const char* const kernel_names[] = { "mv_q4_K_1", "mv_q6_K_1", "mv_q4_K_n", "mv_q6_K_n", "rms_norm", "add_bias", "silu_mul", "rope",
            "kv_store", "prepare_queries", "merge_attention", "dequant_q4_K", "dequant_q6_K",
            "mv_q5_K_1", "mv_q5_K_n", "mv_q8_0_1", "mv_q8_0_n", "gdn_conv", "gdn_l2norm", "gdn_gates", "gdn_delta",
            "gdn_norm", "split_gate", "sigmoid_mul",
            "mv_q4_K_id", "mv_q5_K_id", "mv_q6_K_id", "mv_q8_0_id", "mv_f32", "moe_route", "shared_gate", "moe_combine",
            "mv_q5_0_1", "mv_q5_0_n", "mv_q5_0_id",
            "dequant_q5_K", "dequant_q8_0", "dequant_q5_0",
            "moe_group", "moe_tiles", "mm_id_q4_K", "mm_id_q5_K", "mm_id_q6_K", "mm_id_q8_0", "mm_id_q5_0",
            "attention_part_1_2", "attention_part_1_4", "attention_part_1_8",
            "attention_part_2_2", "attention_part_2_4", "attention_part_2_8",
            "attention_part_4_2", "attention_part_4_4", "attention_part_4_8",
            "attention_part_8_2", "attention_part_8_4", "attention_part_8_8",
            "attention_mtile_1_64", "attention_mtile_1_128", "attention_mtile_1_256",
            "attention_mtile_2_64", "attention_mtile_2_128", "attention_mtile_2_256",
            "attention_mtile_4_64", "attention_mtile_4_128", "attention_mtile_4_256",
            "attention_mtile_8_64", "attention_mtile_8_128", "attention_mtile_8_256", "restore_outputs", "short_conv",
            "gelu_mul", "add_rows",
            "attention_part_1_16", "attention_part_2_16", "attention_part_4_16", "attention_part_8_16",
            "attention_mtile_1_512", "attention_mtile_2_512", "attention_mtile_4_512", "attention_mtile_8_512",
            "dequant_h_q4_K", "dequant_h_q5_K", "dequant_h_q6_K", "dequant_h_q8_0", "dequant_h_q5_0", "to_half",
            "attention_dec_1_64", "attention_dec_1_128", "attention_dec_2_64", "attention_dec_2_128",
            "attention_dec_4_64", "attention_dec_4_128", "attention_dec_8_64", "attention_dec_8_128",
            "attention_dec_1_256", "attention_dec_2_256", "attention_dec_4_256", "qk_norm_rope", "mv_q4_K_1l", "mv_q6_K_1l", "mv_q4_K_idl", "mv_q6_K_idl" };
        enum kernel_index
        {
            k_mv_q4_K, k_mv_q6_K, k_mv_q4_K_n, k_mv_q6_K_n, k_rms_norm, k_add_bias, k_silu_mul, k_rope, k_kv_store, k_prepare_queries,
            k_merge_attention, k_dequant_q4_K, k_dequant_q6_K,
            k_mv_q5_K, k_mv_q5_K_n, k_mv_q8_0, k_mv_q8_0_n, k_gdn_conv, k_gdn_l2norm, k_gdn_gates, k_gdn_delta,
            k_gdn_norm, k_split_gate, k_sigmoid_mul,
            k_mv_q4_K_id, k_mv_q5_K_id, k_mv_q6_K_id, k_mv_q8_0_id, k_mv_f32, k_moe_route, k_shared_gate, k_moe_combine,
            k_mv_q5_0, k_mv_q5_0_n, k_mv_q5_0_id,
            k_dequant_q5_K, k_dequant_q8_0, k_dequant_q5_0,
            k_moe_group, k_moe_tiles, k_mm_id_q4_K, k_mm_id_q5_K, k_mm_id_q6_K, k_mm_id_q8_0, k_mm_id_q5_0,
            k_attention_part, k_attention_mtile = k_attention_part + 12, k_restore_outputs = k_attention_mtile + 12,
            k_short_conv, k_gelu_mul, k_add_rows, k_attention_part_512, k_attention_mtile_512 = k_attention_part_512 + 4,
            k_dequant_h_q4_K = k_attention_mtile_512 + 4, k_dequant_h_q5_K, k_dequant_h_q6_K, k_dequant_h_q8_0,
            k_dequant_h_q5_0, k_to_half, k_attention_dec,
            k_qk_norm_rope = k_attention_dec + 11, k_mv_q4_K_1l, k_mv_q6_K_1l, k_mv_q4_K_idl, k_mv_q6_K_idl,
            k_count
        };

        // La variante de l'attention pour rep têtes de requête par tête de clés et valeurs et des têtes de hd
        // valeurs.
        int attention_kernel(unsigned rep, unsigned hd)
        {
            const int r = rep == 1 ? 0 : rep == 2 ? 1 : rep == 4 ? 2 : rep == 8 ? 3 : -1;
            if (r >= 0 && hd == 512) return k_attention_part_512 + r;
            const int p = hd == 64 ? 0 : hd == 128 ? 1 : hd == 256 ? 2 : -1;
            return r < 0 || p < 0 ? -1 : k_attention_part + 3 * r + p;
        }

        // Les arguments des noyaux, disposés comme leurs structures dans metal_kernels.metal.
        struct mv_args
        {
            int64_t rows;
            int64_t cols;
            uint32_t tokens;
            uint32_t x_stride;
            uint32_t y_stride;
            float alpha;
            float beta;
            uint32_t y2_stride;
            int64_t rows2;
        };
        struct norm_args
        {
            uint32_t width;
            uint32_t x_stride;
            uint32_t y_stride;
            float eps;
        };
        struct dequant_args
        {
            int64_t cols;
            uint32_t out_stride;
        };
        struct bias_args
        {
            uint32_t n;
            uint32_t stride;
        };
        struct rope_args
        {
            uint32_t heads;
            uint32_t hd;
            uint32_t half_dims;
            uint32_t neox;
            uint32_t stride;
            uint32_t cs_stride;
        };
        struct qk_rope_args
        {
            uint32_t q_heads;
            uint32_t k_heads;
            uint32_t hd;
            uint32_t half_dims;
            uint32_t neox;
            uint32_t q_stride;
            uint32_t k_stride;
            uint32_t cs_stride;
            float eps;
        };
        struct kv_args
        {
            uint32_t hd;
            uint32_t q8;
            uint32_t positions;
            uint32_t kv_stride;
            uint64_t block_stride;
            uint64_t k_slab;
            uint64_t v_slab;
            uint64_t head_stride;
            uint64_t scales;
        };
        struct query_args
        {
            uint32_t hd;
            uint32_t q8;
            uint32_t q_stride;
            float scale;
            uint32_t per_token;
        };
        struct attention_args
        {
            uint32_t hd;
            uint32_t rep;
            uint32_t kv_heads;
            uint32_t q8;
            uint32_t positions;
            uint32_t part_len;
            uint32_t parts;
            uint32_t q_stride;
            uint32_t table_stride;
            uint32_t window;       // fenêtre glissante (0 : aucune), qui aligne aussi les champs 64 bits qui suivent
            uint64_t block_stride;
            uint64_t k_slab;
            uint64_t v_slab;
            uint64_t head_stride;
            uint64_t scales;
            uint32_t kv_div;
        };
        /*
            Un regroupement des têtes que les noyaux de l'attention ne prennent pas (fork pour macOS, 2026-11-22 : Qwen2.5,
            7 têtes de requête par tête de clés ; ils prennent 1, 2, 4 et 8) : une tête de requête par « tête » de la grille
            (REP = 1), rep fois plus de têtes, chacune lisant les clés de la tête kvh / kv_div.  Chaque tête de clés est
            alors lue par chacune de ses têtes de requête au lieu d'une fois pour toutes.
        */
        struct grouping
        {
            unsigned rep;
            unsigned kv_div;
            metal_device::commands::kv_layout l;
        };
        bool grouped_by_kernels(unsigned rep) { return rep == 1 || rep == 2 || rep == 4 || rep == 8; }
        grouping regroup(unsigned rep, const metal_device::commands::kv_layout& l)
        {
            if (grouped_by_kernels(rep)) return { rep, 1, l };
            grouping g{ 1, rep, l };
            g.l.heads = l.heads * rep;
            return g;
        }
        // Les octets entre les clés d'une tête et leurs échelles (kv_layout::scales).
        uint64_t scales_at(const metal_device::commands::kv_layout& l)
        {
            return l.scales ? l.scales : static_cast<uint64_t>(l.positions) * l.hd;
        }

        // Les lignes que calcule un threadgroup des produits : ROWS * SG_PER_TG de metal_kernels.metal.
        // ROWS x SG_PER_TG de metal_kernels.metal : une ligne par simdgroup pour un token, quatre pour plusieurs.
        long mv_rows_per_group(unsigned tokens) { return tokens > 1 ? 16 : 4; }
        const unsigned mv_threads = 128, mv_nax_threads = 256;
        // MVL_NR x MVL_NSG lignes et MVL_NSG simdgroups par threadgroup des produits repris de llama.cpp (metal_kernels.metal) :
        // une ligne et quatre simdgroups, mesurés le meilleur (2026-11-22, 4B : 89,1-89,4 tokens/s contre 88,1-88,2 pour les
        // deux lignes et deux simdgroups de llama.cpp, 84,4-84,6 pour quatre lignes).
        const unsigned mvl_rows = 1 * 4, mvl_threads = 32 * 4;
        const unsigned attention_threads = 128;   // ATT_SG simdgroups
    }

    struct metal_buffer::impl
    {
        id<MTLBuffer> buffer = nil;
    };

    struct metal_device::impl
    {
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        id<MTLLibrary> library = nil;
        std::vector<id<MTLComputePipelineState>> pipelines;
        std::string name;
        /* Les noyaux des accélérateurs neuronaux (attention_nax), vides quand le GPU n'en a pas : M5 (génération 17 de
           l'architecture, 18 pour les puces « p », la règle de MLX) et macOS 26.2 ou plus ; GULLIVER_NO_NAX les coupe
           pour les mesures. */
        std::vector<id<MTLComputePipelineState>> nax;
        std::string nax_note;
    };

    struct metal_device::commands::impl
    {
        metal_device::impl* device = nullptr;
        id<MTLCommandBuffer> buffer = nil;
        id<MTLComputeCommandEncoder> encoder = nil;
        // Les tampons déjà envoyés par flush(), dans l'ordre.
        std::vector<id<MTLCommandBuffer>> sent;
        // Les produits groupés des experts sur les accélérateurs (moe_group() le décide pour matmul_id()).
        bool mm_nax = false;
        unsigned mm_tt = 64;

        // Les noyaux vont à un encodeur de calcul, que ferme un produit de Metal Performance Shaders.
        /* GULLIVER_GPU_PROFILE : pour les mesures du chantier Vitesse, chaque noyau dans un encodeur à lui, horodaté par
           le GPU au début et à la fin (compteurs aux limites de passe) ; finish() additionne le temps par noyau et en
           donne le tableau toutes les N passes (la valeur de la variable, 64 si elle n'en est pas une), dans la sortie
           d'erreur.  Les encodeurs séparés ralentissent un peu le
           tout : la répartition compte, pas le total. */
        static bool profiling()
        {
            static const bool p = std::getenv("GULLIVER_GPU_PROFILE") != nullptr;
            return p;
        }
        id<MTLCounterSampleBuffer> samples = nil;
        std::vector<const char*> sampled;
        void profiled_encoder(const char* name)
        {
            close();
            if (!samples)
            {
                for (id<MTLCounterSet> set in device->device.counterSets)
                    if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
                    {
                        MTLCounterSampleBufferDescriptor* d = [MTLCounterSampleBufferDescriptor new];
                        d.counterSet = set;
                        d.storageMode = MTLStorageModeShared;
                        d.sampleCount = 2048;
                        NSError* e = nil;
                        samples = [device->device newCounterSampleBufferWithDescriptor:d error:&e];
                        if (!samples)
                            std::fprintf(stderr, "gpu profile: %s\n", e ? e.localizedDescription.UTF8String : "no sample buffer");
                    }
            }
            if (!samples || sampled.size() >= 1024)
            {
                encoder = [buffer computeCommandEncoder];
                return;
            }
            MTLComputePassDescriptor* d = [MTLComputePassDescriptor computePassDescriptor];
            d.sampleBufferAttachments[0].sampleBuffer = samples;
            d.sampleBufferAttachments[0].startOfEncoderSampleIndex = 2 * sampled.size();
            d.sampleBufferAttachments[0].endOfEncoderSampleIndex = 2 * sampled.size() + 1;
            encoder = [buffer computeCommandEncoderWithDescriptor:d];
            sampled.push_back(name);
        }
        void use(int k)
        {
            if (profiling()) profiled_encoder(kernel_names[k]);
            else if (!encoder) encoder = [buffer computeCommandEncoder];
            [encoder setComputePipelineState:device->pipelines[static_cast<size_t>(k)]];
        }
        /* Le noyau mv_nax des petits lots (metal_kernels_nax.metal) pour un produit de plusieurs tokens, ou -1 : sur
           les accélérateurs, quand les lignes de chaque matrice sont un multiple de 32 et les colonnes de 64.
           GULLIVER_NO_NAX_MV garde les noyaux mv_*_n pour les mesures. */
        int mv_nax_index(gulliver::ggml_type type, unsigned tokens, long rows, long rows2, long cols) const
        {
            static const bool off = std::getenv("GULLIVER_NO_NAX_MV") != nullptr;
            if (off || tokens < 2 || device->nax.empty() || rows % 32 != 0 || rows2 % 32 != 0 || cols % 64 != 0) return -1;
            switch (type)
            {
            case gulliver::ggml_type::Q4_K: return nax_mv;
            case gulliver::ggml_type::Q6_K: return nax_mv + 1;
            case gulliver::ggml_type::Q5_K: return nax_mv + 2;
            case gulliver::ggml_type::Q8_0: return nax_mv + 3;
            case gulliver::ggml_type::Q5_0: return nax_mv + 4;
            default: return -1;
            }
        }
        void use_nax(int k)
        {
            if (profiling()) profiled_encoder(nax_attention_names[k]);
            else if (!encoder) encoder = [buffer computeCommandEncoder];
            [encoder setComputePipelineState:device->nax[static_cast<size_t>(k)]];
        }
        void report_profile()
        {
            if (!samples || sampled.empty())
            {
                static bool told = false;
                if (!told) std::fprintf(stderr, "gpu profile: no samples (%s)\n", samples ? "nothing sampled" : "no sample buffer");
                told = true;
                return;
            }
            NSData* data = [samples resolveCounterRange:NSMakeRange(0, 2 * sampled.size())];
            if (!data) { std::fprintf(stderr, "gpu profile: the samples do not resolve\n"); return; }
            const MTLCounterResultTimestamp* t = (const MTLCounterResultTimestamp*)data.bytes;
            static std::map<std::string, std::pair<double, long>> total;
            static int passes = 0;
            for (size_t i = 0; i < sampled.size(); ++i)
            {
                const uint64_t a = t[2 * i].timestamp, b = t[2 * i + 1].timestamp;
                if (a == MTLCounterErrorValue || b == MTLCounterErrorValue || b < a) continue;
                auto& e = total[sampled[i]];
                e.first += double(b - a);
                e.second += 1;
            }
            sampled.clear();
            static const int period = [] {
                const int v = std::atoi(std::getenv("GULLIVER_GPU_PROFILE"));
                return v > 0 ? v : 64;
            }();
            if (++passes < period) return;
            std::vector<std::pair<double, std::string>> rows;
            double sum = 0;
            for (const auto& e : total)
            {
                rows.push_back({ e.second.first, e.first + " x" + std::to_string(e.second.second / passes) });
                sum += e.second.first;
            }
            std::sort(rows.rbegin(), rows.rend());
            std::fprintf(stderr, "gpu profile over %d passes (timestamp units per pass, share):\n", passes);
            for (const auto& e : rows)
                std::fprintf(stderr, "  %-34s %10.0f  %5.1f %%\n", e.second.c_str(), e.first / passes, 100.0 * e.first / sum);
            total.clear();
            passes = 0;
        }
        void close()
        {
            if (encoder) [encoder endEncoding];
            encoder = nil;
        }
        void bind(metal_view v, unsigned index)
        {
            [encoder setBuffer:v.buffer->impl_->buffer offset:v.offset atIndex:index];
        }
        template <typename T>
        void args(const T& a, unsigned index) { [encoder setBytes:&a length:sizeof(T) atIndex:index]; }
        void groups(unsigned x, unsigned y, unsigned z, unsigned threads)
        {
            [encoder dispatchThreadgroups:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
        }
        // Un lancement dont les nombres de threadgroups sont dans indirect, à l'octet offset, écrits par un noyau
        // précédent de la même commande.
        void groups_indirect(metal_view indirect, size_t offset, unsigned threads)
        {
            [encoder dispatchThreadgroupsWithIndirectBuffer:indirect.buffer->impl_->buffer
                indirectBufferOffset:indirect.offset + offset threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
        }
        void grid(unsigned x, unsigned y, unsigned z)
        {
            const NSUInteger w = std::min<NSUInteger>(x, 256);
            [encoder dispatchThreads:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(w, 1, 1)];
        }
    };

    metal_device::metal_device() : impl_(new impl) {}

    metal_device::~metal_device() = default;

    std::unique_ptr<metal_device> metal_device::open(std::string& why)
    {
        @autoreleasepool
        {
            std::unique_ptr<metal_device> d(new metal_device);
            impl& m = *d->impl_;
            m.device = MTLCreateSystemDefaultDevice();
            if (!m.device)
            {
                why = "no GPU that Metal reaches";
                return nullptr;
            }
            m.name = m.device.name.UTF8String;
            m.queue = [m.device newCommandQueue];
            NSError* error = nil;
            MTLCompileOptions* options = [MTLCompileOptions new];
            m.library = [m.device newLibraryWithSource:[NSString stringWithUTF8String:metal_source]
                options:options error:&error];
            if (!m.library)
            {
                why = std::string("the Metal kernels do not compile: ")
                    + (error ? error.localizedDescription.UTF8String : "unknown error");
                return nullptr;
            }
            static_assert(sizeof(kernel_names) / sizeof(kernel_names[0]) == k_count, "a kernel without its name");
            for (const char* k : kernel_names)
            {
                id<MTLFunction> f = [m.library newFunctionWithName:[NSString stringWithUTF8String:k]];
                id<MTLComputePipelineState> p = f ? [m.device newComputePipelineStateWithFunction:f error:&error] : nil;
                if (!p)
                {
                    why = std::string("the Metal kernel ") + k + " cannot be prepared"
                        + (error ? std::string(": ") + error.localizedDescription.UTF8String : std::string());
                    return nullptr;
                }
                m.pipelines.push_back(p);
            }
            auto open_nax = [](metal_device::impl& m)
            {
                if (std::getenv("GULLIVER_NO_NAX")) { m.nax_note = "neural accelerators off (GULLIVER_NO_NAX)"; return; }
                if (@available(macOS 26.2, *))
                {
                    const std::string arch = m.device.architecture.name.UTF8String;   // « applegpu_g17s »
                    const size_t g = arch.find("_g");
                    const int gen = g == std::string::npos ? 0 : std::atoi(arch.c_str() + g + 2);
                    const bool p_chip = !arch.empty() && arch.back() == 'p';
                    if (gen < (p_chip ? 18 : 17)) { m.nax_note = "no neural accelerators (" + arch + ")"; return; }
                    NSError* error = nil;
                    MTLCompileOptions* options = [MTLCompileOptions new];
                    options.languageVersion = MTLLanguageVersion4_0;
                    id<MTLLibrary> lib = [m.device newLibraryWithSource:[NSString stringWithUTF8String:metal_nax_source]
                        options:options error:&error];
                    if (!lib)
                    {
                        m.nax_note = std::string("neural accelerator kernels do not compile: ")
                            + (error ? error.localizedDescription.UTF8String : "unknown error");
                        return;
                    }
                    std::vector<id<MTLComputePipelineState>> p;
                    for (const char* k : nax_attention_names)
                    {
                        id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:k]];
                        id<MTLComputePipelineState> s = f ? [m.device newComputePipelineStateWithFunction:f error:&error] : nil;
                        if (!s) { m.nax_note = std::string("the kernel ") + k + " cannot be prepared"; return; }
                        p.push_back(s);
                    }
                    m.nax = std::move(p);
                    m.nax_note = "neural accelerators (" + arch + ")";
                }
                else
                    m.nax_note = "no neural accelerators (macOS before 26.2)";
            };
            open_nax(m);
            return d;
        }
    }

    const std::string& metal_device::name() const { return impl_->name; }

    uint64_t metal_device::max_buffer_bytes() const { return impl_->device.maxBufferLength; }

    uint64_t metal_device::working_set_bytes() const { return impl_->device.recommendedMaxWorkingSetSize; }

    int metal_device::kernel_count() const { return static_cast<int>(impl_->pipelines.size()); }

    metal_buffer metal_device::allocate(size_t bytes)
    {
        metal_buffer b;
        id<MTLBuffer> buffer = [impl_->device newBufferWithLength:std::max<size_t>(bytes, 16)
            options:MTLResourceStorageModeShared];
        if (!buffer) return b;
        std::memset(buffer.contents, 0, buffer.length);
        b.impl_ = std::make_shared<metal_buffer::impl>();
        b.impl_->buffer = buffer;
        b.contents_ = buffer.contents;
        b.bytes_ = bytes;
        return b;
    }

    metal_buffer metal_device::wrap(const void* data, size_t bytes, size_t& offset)
    {
        metal_buffer b;
        const uintptr_t page = static_cast<uintptr_t>(getpagesize());
        const uintptr_t p = reinterpret_cast<uintptr_t>(data);
        const uintptr_t first = p / page * page;
        const uintptr_t end = (p + bytes + page - 1) / page * page;
        if (end - first > impl_->device.maxBufferLength) return b;
        id<MTLBuffer> buffer = [impl_->device newBufferWithBytesNoCopy:reinterpret_cast<void*>(first)
            length:end - first options:MTLResourceStorageModeShared deallocator:nil];
        if (!buffer) return b;
        offset = p - first;
        b.impl_ = std::make_shared<metal_buffer::impl>();
        b.impl_->buffer = buffer;
        b.contents_ = reinterpret_cast<void*>(first);
        b.bytes_ = end - first;
        return b;
    }

    std::unique_ptr<metal_device::commands> metal_device::begin()
    {
        return std::unique_ptr<commands>(new commands(*this));
    }

    metal_device::commands::commands(metal_device& device) : impl_(new impl)
    {
        impl_->device = device.impl_.get();
        impl_->buffer = [impl_->device->queue commandBuffer];
        impl_->encoder = [impl_->buffer computeCommandEncoder];
    }

    metal_device::commands::~commands()
    {
        // Les commandes jamais terminées sont abandonnées : leur encodeur doit se terminer avant de disparaître.
        impl_->close();
    }

    bool metal_device::commands::dequantize(gulliver::ggml_type type, metal_view W, long rows, long cols, metal_view out,
        unsigned out_stride)
    {
        int k;
        if (type == gulliver::ggml_type::Q4_K) k = k_dequant_q4_K;
        else if (type == gulliver::ggml_type::Q6_K) k = k_dequant_q6_K;
        else if (type == gulliver::ggml_type::Q5_K) k = k_dequant_q5_K;
        else if (type == gulliver::ggml_type::Q8_0) k = k_dequant_q8_0;
        else if (type == gulliver::ggml_type::Q5_0) k = k_dequant_q5_0;
        else return false;
        if (rows == 0) return true;
        const bool small_blocks = type == gulliver::ggml_type::Q8_0 || type == gulliver::ggml_type::Q5_0;
        if (cols % (small_blocks ? 32 : 256) != 0)
            throw std::invalid_argument("metal_device: a matrix of other columns than its kernel takes");
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(out, 1);
        const dequant_args a = { cols, out_stride };
        impl_->args(a, 2);
        // Huit valeurs par thread.
        impl_->grid(static_cast<unsigned>(cols / 8), static_cast<unsigned>(rows), 1);
        return true;
    }

    void metal_device::commands::gemm(metal_view X, unsigned rows, unsigned inner, unsigned x_stride, metal_view Wf,
        unsigned cols, unsigned w_stride, metal_view Y, unsigned y_stride, float alpha, float beta)
    {
        if (rows == 0 || cols == 0) return;
        impl_->close();
        id<MTLDevice> device = impl_->device->device;
        auto matrix = [](metal_view v, unsigned r, unsigned c, unsigned stride) {
            MPSMatrixDescriptor* d = [MPSMatrixDescriptor matrixDescriptorWithRows:r columns:c
                rowBytes:stride * sizeof(float) dataType:MPSDataTypeFloat32];
            return [[MPSMatrix alloc] initWithBuffer:v.buffer->impl_->buffer offset:v.offset descriptor:d];
        };
        MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc] initWithDevice:device transposeLeft:NO
            transposeRight:YES resultRows:rows resultColumns:cols interiorColumns:inner alpha:alpha beta:beta];
        [mm encodeToCommandBuffer:impl_->buffer leftMatrix:matrix(X, rows, inner, x_stride)
            rightMatrix:matrix(Wf, cols, inner, w_stride) resultMatrix:matrix(Y, rows, cols, y_stride)];
    }

    void metal_device::commands::gemm_half(metal_view X, unsigned rows, unsigned inner, unsigned x_stride, metal_view Wh,
        unsigned cols, unsigned w_stride, metal_view Y, unsigned y_stride, float alpha, float beta)
    {
        if (rows == 0 || cols == 0) return;
        impl_->close();
        id<MTLDevice> device = impl_->device->device;
        auto matrix = [](metal_view v, unsigned r, unsigned c, unsigned stride, MPSDataType t, size_t bytes) {
            MPSMatrixDescriptor* d = [MPSMatrixDescriptor matrixDescriptorWithRows:r columns:c
                rowBytes:stride * bytes dataType:t];
            return [[MPSMatrix alloc] initWithBuffer:v.buffer->impl_->buffer offset:v.offset descriptor:d];
        };
        MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc] initWithDevice:device transposeLeft:NO
            transposeRight:YES resultRows:rows resultColumns:cols interiorColumns:inner alpha:alpha beta:beta];
        [mm encodeToCommandBuffer:impl_->buffer leftMatrix:matrix(X, rows, inner, x_stride, MPSDataTypeFloat16, 2)
            rightMatrix:matrix(Wh, cols, inner, w_stride, MPSDataTypeFloat16, 2)
            resultMatrix:matrix(Y, rows, cols, y_stride, MPSDataTypeFloat32, 4)];
    }

    void metal_device::commands::to_half(metal_view x, unsigned rows, unsigned cols, unsigned x_stride, metal_view y,
        unsigned y_stride)
    {
        if (rows == 0 || cols == 0) return;
        impl_->use(k_to_half);
        impl_->bind(x, 0);
        impl_->bind(y, 1);
        struct { unsigned cols, x_stride, y_stride; } a = { cols, x_stride, y_stride };
        impl_->args(a, 2);
        impl_->grid(cols / 4, rows, 1);
    }

    bool metal_device::commands::dequantize_half(gulliver::ggml_type type, metal_view W, long rows, long cols, metal_view out,
        unsigned out_stride)
    {
        kernel_index k;
        if (type == gulliver::ggml_type::Q4_K) k = k_dequant_h_q4_K;
        else if (type == gulliver::ggml_type::Q5_K) k = k_dequant_h_q5_K;
        else if (type == gulliver::ggml_type::Q6_K) k = k_dequant_h_q6_K;
        else if (type == gulliver::ggml_type::Q8_0) k = k_dequant_h_q8_0;
        else if (type == gulliver::ggml_type::Q5_0) k = k_dequant_h_q5_0;
        else return false;
        if (rows == 0) return true;
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(out, 1);
        const dequant_args a = { cols, out_stride };
        impl_->args(a, 2);
        impl_->grid(static_cast<unsigned>(cols / 8), static_cast<unsigned>(rows), 1);
        return true;
    }

    bool metal_device::commands::matvec(gulliver::ggml_type type, metal_view W, long rows, long cols, metal_view x,
        unsigned tokens, unsigned x_stride, metal_view y, unsigned y_stride, float alpha, float beta)
    {
        kernel_index k;
        if (type == gulliver::ggml_type::Q4_K) k = tokens > 1 ? k_mv_q4_K_n : k_mv_q4_K;
        else if (type == gulliver::ggml_type::Q6_K) k = tokens > 1 ? k_mv_q6_K_n : k_mv_q6_K;
        else if (type == gulliver::ggml_type::Q5_K) k = tokens > 1 ? k_mv_q5_K_n : k_mv_q5_K;
        else if (type == gulliver::ggml_type::Q8_0) k = tokens > 1 ? k_mv_q8_0_n : k_mv_q8_0;
        else if (type == gulliver::ggml_type::Q5_0) k = tokens > 1 ? k_mv_q5_0_n : k_mv_q5_0;
        else return false;
        if (tokens == 0) return true;
        if (tokens > max_tokens || cols % (type == gulliver::ggml_type::Q8_0 || type == gulliver::ggml_type::Q5_0 ? 32 : 256) != 0)
            throw std::invalid_argument("metal_device: a product of more tokens or other columns than its kernel takes");
        const int nax = impl_->mv_nax_index(type, tokens, rows, 0, cols);
        // Un token en Q4_K ou Q6_K : les produits repris de llama.cpp (mv_q4_K_1l, mv_q6_K_1l) ; GULLIVER_OLD_MV garde
        // mv_q4_K_1 et mv_q6_K_1 pour les mesures.
        static const bool old_mv = std::getenv("GULLIVER_OLD_MV") != nullptr;
        const bool ql = nax < 0 && tokens == 1 && !old_mv
            && (type == gulliver::ggml_type::Q4_K || type == gulliver::ggml_type::Q6_K);
        if (ql) k = type == gulliver::ggml_type::Q4_K ? k_mv_q4_K_1l : k_mv_q6_K_1l;
        if (nax >= 0) impl_->use_nax(nax);
        else impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        impl_->bind(W, 4);
        impl_->bind(y, 5);
        const mv_args a = { rows, cols, tokens, x_stride, y_stride, alpha, beta, y_stride, 0 };
        impl_->args(a, 3);
        if (nax >= 0) impl_->groups(static_cast<unsigned>(rows / 32), 1, 1, mv_nax_threads);
        else if (ql) impl_->groups(static_cast<unsigned>((rows + mvl_rows - 1) / mvl_rows), 1, 1, mvl_threads);
        else impl_->groups(static_cast<unsigned>((rows + mv_rows_per_group(tokens) - 1) / mv_rows_per_group(tokens)), 1, 1, mv_threads);
        return true;
    }

    bool metal_device::commands::matvec2(gulliver::ggml_type type, metal_view W, long rows, metal_view W2, long rows2,
        long cols, metal_view x, unsigned tokens, unsigned x_stride, metal_view y, unsigned y_stride, metal_view y2,
        unsigned y2_stride)
    {
        kernel_index k;
        if (type == gulliver::ggml_type::Q4_K) k = tokens > 1 ? k_mv_q4_K_n : k_mv_q4_K;
        else if (type == gulliver::ggml_type::Q6_K) k = tokens > 1 ? k_mv_q6_K_n : k_mv_q6_K;
        else if (type == gulliver::ggml_type::Q5_K) k = tokens > 1 ? k_mv_q5_K_n : k_mv_q5_K;
        else if (type == gulliver::ggml_type::Q8_0) k = tokens > 1 ? k_mv_q8_0_n : k_mv_q8_0;
        else if (type == gulliver::ggml_type::Q5_0) k = tokens > 1 ? k_mv_q5_0_n : k_mv_q5_0;
        else return false;
        if (tokens == 0) return true;
        // Les lignes d'un simdgroup (ROWS) ne chevauchent jamais les deux matrices.
        if (tokens > max_tokens || cols % 256 != 0 || rows % 16 != 0)
            throw std::invalid_argument("metal_device: a double product of more tokens or other shapes than its kernel takes");
        const int nax = impl_->mv_nax_index(type, tokens, rows, rows2, cols);
        // Un token en Q4_K ou Q6_K : les produits repris de llama.cpp (mv_q4_K_1l, mv_q6_K_1l) ; GULLIVER_OLD_MV garde
        // mv_q4_K_1 et mv_q6_K_1 pour les mesures.
        static const bool old_mv = std::getenv("GULLIVER_OLD_MV") != nullptr;
        const bool ql = nax < 0 && tokens == 1 && !old_mv
            && (type == gulliver::ggml_type::Q4_K || type == gulliver::ggml_type::Q6_K);
        if (ql) k = type == gulliver::ggml_type::Q4_K ? k_mv_q4_K_1l : k_mv_q6_K_1l;
        if (nax >= 0) impl_->use_nax(nax);
        else impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        impl_->bind(W2, 4);
        impl_->bind(y2, 5);
        const mv_args a = { rows, cols, tokens, x_stride, y_stride, 1.0f, 0.0f, y2_stride, rows2 };
        impl_->args(a, 3);
        if (nax >= 0) impl_->groups(static_cast<unsigned>((rows + rows2) / 32), 1, 1, mv_nax_threads);
        else if (ql) impl_->groups(static_cast<unsigned>((rows + rows2 + mvl_rows - 1) / mvl_rows), 1, 1, mvl_threads);
        else impl_->groups(static_cast<unsigned>((rows + rows2 + mv_rows_per_group(tokens) - 1) / mv_rows_per_group(tokens)), 1, 1,
            mv_threads);
        return true;
    }

    void metal_device::commands::rms_norm(metal_view x, metal_view gamma, metal_view y, unsigned rows, unsigned width,
        unsigned x_stride, unsigned y_stride, float eps)
    {
        if (rows == 0) return;
        impl_->use(k_rms_norm);
        impl_->bind(x, 0);
        impl_->bind(gamma, 1);
        impl_->bind(y, 2);
        const norm_args a = { width, x_stride, y_stride, eps };
        impl_->args(a, 3);
        const unsigned threads = std::min(256u, (width + 31) / 32 * 32);
        impl_->groups(rows, 1, 1, threads);
    }

    void metal_device::commands::add_bias(metal_view x, metal_view b, unsigned n, unsigned stride, unsigned rows)
    {
        if (rows == 0 || n == 0) return;
        impl_->use(k_add_bias);
        impl_->bind(x, 0);
        impl_->bind(b, 1);
        const bias_args a = { n, stride };
        impl_->args(a, 2);
        impl_->grid(n, rows, 1);
    }

    namespace
    {
        // Le miroir de gdn_args (metal_kernels.metal).
        struct gdn_args
        {
            unsigned C, K, Hk, Hv, S, VD, slot_floats, layer_offset, conv_floats, segments;
            float eps, qscale;
        };
        gdn_args gdn(const metal_device::commands::gdn_layout& l, unsigned segments)
        {
            return { l.C, l.K, l.Hk, l.Hv, l.S, l.Hv * l.S, l.slot_floats, l.layer_offset, l.conv_floats, segments,
                l.eps, 1.0f / std::sqrt(static_cast<float>(l.S)) };
        }
        struct split_args
        {
            unsigned hd, qd;
        };
    }

    void metal_device::commands::gdn_conv(metal_view mix, metal_view weights, metal_view states, metal_view segments,
        unsigned n_segments, const gdn_layout& l)
    {
        if (n_segments == 0) return;
        impl_->use(k_gdn_conv);
        impl_->bind(mix, 0);
        impl_->bind(weights, 1);
        impl_->bind(states, 2);
        impl_->bind(segments, 3);
        impl_->args(gdn(l, n_segments), 4);
        impl_->grid(l.C, n_segments, 1);
    }

    void metal_device::commands::short_conv(metal_view mix, metal_view weights, metal_view states, metal_view segments,
        unsigned n_segments, metal_view out, const gdn_layout& l)
    {
        if (n_segments == 0) return;
        impl_->use(k_short_conv);
        impl_->bind(mix, 0);
        impl_->bind(weights, 1);
        impl_->bind(states, 2);
        impl_->bind(segments, 3);
        impl_->bind(out, 4);
        impl_->args(gdn(l, n_segments), 5);
        impl_->grid(l.C, n_segments, 1);
    }

    void metal_device::commands::gdn_l2norm(metal_view mix, unsigned tokens, const gdn_layout& l)
    {
        if (tokens == 0) return;
        impl_->use(k_gdn_l2norm);
        impl_->bind(mix, 0);
        impl_->args(gdn(l, 0), 1);
        impl_->grid(2 * l.Hk, tokens, 1);
    }

    void metal_device::commands::gdn_gates(metal_view alpha, metal_view beta, metal_view dt_bias, metal_view ssm_a,
        unsigned tokens, const gdn_layout& l)
    {
        if (tokens == 0) return;
        impl_->use(k_gdn_gates);
        impl_->bind(alpha, 0);
        impl_->bind(beta, 1);
        impl_->bind(dt_bias, 2);
        impl_->bind(ssm_a, 3);
        impl_->args(gdn(l, 0), 4);
        impl_->grid(l.Hv, tokens, 1);
    }

    void metal_device::commands::gdn_delta(metal_view mix, metal_view alpha, metal_view beta, metal_view states,
        metal_view segments, unsigned n_segments, metal_view out, const gdn_layout& l)
    {
        if (n_segments == 0) return;
        if (l.S > 256) throw std::invalid_argument("metal_device: linear attention heads wider than 256 values");
        impl_->use(k_gdn_delta);
        impl_->bind(mix, 0);
        impl_->bind(alpha, 1);
        impl_->bind(beta, 2);
        impl_->bind(states, 3);
        impl_->bind(segments, 4);
        impl_->bind(out, 5);
        impl_->args(gdn(l, n_segments), 6);
        impl_->groups(l.Hv, n_segments, 1, (l.S + 31) / 32 * 32);
    }

    void metal_device::commands::gdn_norm(metal_view out, metal_view z, metal_view weight, unsigned tokens,
        const gdn_layout& l)
    {
        if (tokens == 0) return;
        impl_->use(k_gdn_norm);
        impl_->bind(out, 0);
        impl_->bind(z, 1);
        impl_->bind(weight, 2);
        impl_->args(gdn(l, 0), 3);
        impl_->grid(l.Hv, tokens, 1);
    }

    void metal_device::commands::split_gate(metal_view qg, metal_view q, metal_view gate, unsigned tokens,
        unsigned heads, unsigned hd)
    {
        if (tokens == 0) return;
        impl_->use(k_split_gate);
        impl_->bind(qg, 0);
        impl_->bind(q, 1);
        impl_->bind(gate, 2);
        const split_args a = { hd, heads * hd };
        impl_->args(a, 3);
        impl_->grid(hd, heads, tokens);
    }

    void metal_device::commands::sigmoid_mul(metal_view x, metal_view gate, unsigned n)
    {
        if (n == 0) return;
        impl_->use(k_sigmoid_mul);
        impl_->bind(x, 0);
        impl_->bind(gate, 1);
        impl_->args(n, 2);
        impl_->grid(n, 1, 1);
    }

    namespace
    {
        struct moe_args
        {
            uint64_t expert_bytes;
            unsigned x_div;
        };
        struct f32_args
        {
            unsigned rows, cols, x_stride, y_stride;
        };
        struct route_args
        {
            unsigned experts, top_k, norm, sigmoid;
        };
        struct combine_args
        {
            unsigned d, top_k, has_shared;
            float scale;
        };
        struct group_args
        {
            unsigned slots, capacity;
        };
        struct tiles_args
        {
            unsigned experts, tiles_a, tiles_b, per_tile;
        };
        struct mm_args
        {
            uint64_t expert_bytes;
            unsigned row_bytes, cols, x_stride, x_div, y_stride, capacity;
        };
        // Les dimensions des tuiles de mm_id (MM_TT, MM_NR, MM_KC dans metal_kernels.metal).
        const unsigned mm_tile_slots = 16, mm_tile_rows = 32, mm_tile_cols = 64;
        // mm_id_nax (accélérateurs du M5) : 64 cases et 64 lignes par tuile, 32 colonnes à la fois.
        const unsigned mm_nax_rows = 64, mm_nax_cols = 64;
    }

    bool metal_device::commands::matvec_id(gulliver::ggml_type type, metal_view W, long rows, long cols,
        uint64_t expert_bytes, metal_view x, unsigned x_stride, unsigned x_div, metal_view ids, unsigned slots,
        metal_view y, unsigned y_stride)
    {
        kernel_index k;
        if (type == gulliver::ggml_type::Q4_K) k = k_mv_q4_K_id;
        else if (type == gulliver::ggml_type::Q5_K) k = k_mv_q5_K_id;
        else if (type == gulliver::ggml_type::Q6_K) k = k_mv_q6_K_id;
        else if (type == gulliver::ggml_type::Q8_0) k = k_mv_q8_0_id;
        else if (type == gulliver::ggml_type::Q5_0) k = k_mv_q5_0_id;
        else return false;
        if (slots == 0) return true;
        if (cols % (type == gulliver::ggml_type::Q8_0 || type == gulliver::ggml_type::Q5_0 ? 32 : 256) != 0 || x_div == 0)
            throw std::invalid_argument("metal_device: an expert product of other columns than its kernel takes");
        // Q4_K et Q6_K : le corps repris de llama.cpp (mv_*_idl), sauf avec GULLIVER_OLD_MV.
        static const bool old_mv = std::getenv("GULLIVER_OLD_MV") != nullptr;
        const bool idl = !old_mv && (type == gulliver::ggml_type::Q4_K || type == gulliver::ggml_type::Q6_K);
        if (idl) k = type == gulliver::ggml_type::Q4_K ? k_mv_q4_K_idl : k_mv_q6_K_idl;
        impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        const mv_args a = { rows, cols, 1, x_stride, y_stride, 1.0f, 0.0f, y_stride, 0 };
        impl_->args(a, 3);
        impl_->bind(ids, 4);
        const moe_args m = { expert_bytes, x_div };
        impl_->args(m, 5);
        if (idl) impl_->groups(static_cast<unsigned>((rows + mvl_rows - 1) / mvl_rows), slots, 1, mvl_threads);
        else impl_->groups(static_cast<unsigned>((rows + mv_rows_per_group(1) - 1) / mv_rows_per_group(1)), slots, 1, mv_threads);
        return true;
    }

    void metal_device::commands::moe_group(metal_view ids, unsigned slots, unsigned experts, unsigned capacity,
        metal_view list, metal_view counts, metal_view work, metal_view dispatch, unsigned rows_a, unsigned rows_b)
    {
        if (slots == 0) return;
        if (rows_a % mm_tile_rows != 0 || rows_b % mm_tile_rows != 0)
            throw std::invalid_argument("metal_device: grouped expert products of rows that are not multiples of 32");
        /* Les tuiles des accélérateurs quand le GPU en a et que les lignes s'y prêtent ; matmul_id() suit ce choix, gardé
           jusqu'au prochain moe_group().  GULLIVER_NO_NAX_MM garde mm_id pour les mesures. */
        static const bool no_nax_mm = std::getenv("GULLIVER_NO_NAX_MM") != nullptr;
        impl_->mm_nax = !no_nax_mm && !impl_->device->nax.empty() && rows_a % mm_nax_rows == 0 && rows_b % mm_nax_rows == 0;
        // Tuiles de 64 cases (GULLIVER_NAX_MM_TT=128 pour les mesures : mesuré sur Qwen3-30B-A3B, 128 cases perdent
        // à 512 tokens, 831-834 contre 1 143-1 149 tokens/s, comme à 4 096, 1 188-1 190 contre 1 369-1 371).
        static const unsigned forced_tt = [] {
            const char* v = std::getenv("GULLIVER_NAX_MM_TT");
            return v ? static_cast<unsigned>(std::atoi(v)) : 0u;
        }();
        impl_->mm_tt = forced_tt == 128 ? 128u : 64u;
        const unsigned tile_rows = impl_->mm_nax ? mm_nax_rows : mm_tile_rows;
        impl_->use(k_moe_group);
        impl_->bind(ids, 0);
        impl_->bind(list, 1);
        impl_->bind(counts, 2);
        const group_args a = { slots, capacity };
        impl_->args(a, 3);
        impl_->groups(experts, 1, 1, 256);
        impl_->use(k_moe_tiles);
        impl_->bind(counts, 0);
        impl_->bind(work, 1);
        impl_->bind(dispatch, 2);
        const tiles_args b = { experts, rows_a / tile_rows, rows_b / tile_rows, impl_->mm_nax ? impl_->mm_tt : mm_tile_slots };
        impl_->args(b, 3);
        impl_->groups(1, 1, 1, 256);
    }

    unsigned metal_device::commands::moe_tiles_most(unsigned slots, unsigned experts)
    {
        return (slots + mm_tile_slots - 1) / mm_tile_slots + experts;
    }

    bool metal_device::commands::matmul_id(gulliver::ggml_type type, metal_view W, long rows, long cols, uint64_t expert_bytes,
        uint64_t row_bytes, metal_view x, unsigned x_stride, unsigned x_div, metal_view list, metal_view counts,
        metal_view work, unsigned capacity, metal_view dispatch, unsigned which, metal_view y, unsigned y_stride)
    {
        kernel_index k;
        if (type == gulliver::ggml_type::Q4_K) k = k_mm_id_q4_K;
        else if (type == gulliver::ggml_type::Q5_K) k = k_mm_id_q5_K;
        else if (type == gulliver::ggml_type::Q6_K) k = k_mm_id_q6_K;
        else if (type == gulliver::ggml_type::Q8_0) k = k_mm_id_q8_0;
        else if (type == gulliver::ggml_type::Q5_0) k = k_mm_id_q5_0;
        else return false;
        if (rows % mm_tile_rows != 0 || cols % mm_tile_cols != 0 || x_div == 0)
            throw std::invalid_argument("metal_device: a grouped expert product of other sizes than its kernel takes");
        if (impl_->mm_nax && (rows % mm_nax_rows != 0 || cols % mm_nax_cols != 0))
            throw std::invalid_argument("metal_device: a grouped expert product that its tiles do not fit");
        if (impl_->mm_nax) impl_->use_nax((impl_->mm_tt == 128 ? nax_mm_id_128 : nax_mm_id) + (k - k_mm_id_q4_K));
        else impl_->use(k);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        impl_->bind(list, 3);
        impl_->bind(counts, 4);
        impl_->bind(work, 5);
        const mm_args a = { expert_bytes, static_cast<unsigned>(row_bytes), static_cast<unsigned>(cols), x_stride, x_div,
            y_stride, capacity };
        impl_->args(a, 6);
        impl_->groups_indirect(dispatch, which == 0 ? 0 : 3 * sizeof(uint32_t), impl_->mm_nax ? 2 * impl_->mm_tt : 128);
        return true;
    }

    void metal_device::commands::matvec_f32(metal_view W, unsigned rows, unsigned cols, metal_view x, unsigned tokens,
        unsigned x_stride, metal_view y, unsigned y_stride)
    {
        if (tokens == 0 || rows == 0) return;
        impl_->use(k_mv_f32);
        impl_->bind(W, 0);
        impl_->bind(x, 1);
        impl_->bind(y, 2);
        const f32_args a = { rows, cols, x_stride, y_stride };
        impl_->args(a, 3);
        impl_->groups((rows + 3) / 4, tokens, 1, 128);
    }

    void metal_device::commands::moe_route(metal_view logits, unsigned experts, unsigned top_k, bool norm,
        metal_view ids, metal_view weights, unsigned tokens, bool sigmoid, metal_view bias)
    {
        if (tokens == 0) return;
        if (top_k == 0 || top_k > 32 || top_k > experts)
            throw std::invalid_argument("metal_device: a routing of more than 32 experts per token");
        impl_->use(k_moe_route);
        impl_->bind(logits, 0);
        impl_->bind(ids, 1);
        impl_->bind(weights, 2);
        if (experts > 1024) throw std::invalid_argument("metal_device: a routing among more than 1024 experts");
        const route_args a = { experts, top_k, norm ? 1u : 0u, sigmoid ? 1u : 0u };
        impl_->args(a, 3);
        impl_->bind(bias, 4);
        impl_->groups(tokens, 1, 1, 32);
    }

    void metal_device::commands::shared_gate(metal_view x, metal_view w, metal_view g, unsigned d, unsigned tokens)
    {
        if (tokens == 0) return;
        impl_->use(k_shared_gate);
        impl_->bind(x, 0);
        impl_->bind(w, 1);
        impl_->bind(g, 2);
        impl_->args(d, 3);
        impl_->groups(tokens, 1, 1, 32);
    }

    void metal_device::commands::moe_combine(metal_view x, metal_view y, metal_view weights, const metal_view* shared,
        metal_view g, unsigned d, unsigned top_k, unsigned tokens, float scale)
    {
        if (tokens == 0) return;
        impl_->use(k_moe_combine);
        impl_->bind(x, 0);
        impl_->bind(y, 1);
        impl_->bind(weights, 2);
        // Sans expert partagé, le noyau ne lit ni shared ni g : y les remplace.
        impl_->bind(shared ? *shared : y, 3);
        impl_->bind(shared ? g : y, 4);
        const combine_args a = { d, top_k, shared ? 1u : 0u, scale };
        impl_->args(a, 5);
        impl_->grid(d, tokens, 1);
    }

    void metal_device::commands::gelu_mul(metal_view gate, metal_view up, unsigned n)
    {
        if (n == 0) return;
        impl_->use(k_gelu_mul);
        impl_->bind(gate, 0);
        impl_->bind(up, 1);
        impl_->args(n, 2);
        impl_->grid(n, 1, 1);
    }

    void metal_device::commands::add_rows(metal_view x, metal_view y, unsigned n, float s)
    {
        if (n == 0) return;
        struct add_args { uint32_t n; float s; };
        impl_->use(k_add_rows);
        impl_->bind(x, 0);
        impl_->bind(y, 1);
        const add_args a = { n, s };
        impl_->args(a, 2);
        impl_->grid(n, 1, 1);
    }

    void metal_device::commands::silu_mul(metal_view gate, metal_view up, unsigned n)
    {
        if (n == 0) return;
        impl_->use(k_silu_mul);
        impl_->bind(gate, 0);
        impl_->bind(up, 1);
        impl_->args(n, 2);
        impl_->grid(n, 1, 1);
    }

    void metal_device::commands::rope(metal_view x, metal_view cs, unsigned tokens, unsigned heads, unsigned hd,
        unsigned half, bool neox, unsigned stride, unsigned cs_stride)
    {
        if (tokens == 0 || half == 0) return;
        impl_->use(k_rope);
        impl_->bind(x, 0);
        impl_->bind(cs, 1);
        const rope_args a = { heads, hd, half, neox ? 1u : 0u, stride, cs_stride };
        impl_->args(a, 2);
        impl_->grid(half, heads, tokens);
    }

    void metal_device::commands::qk_norm_rope(metal_view q, metal_view k, metal_view wq, metal_view wk, metal_view cs,
        unsigned tokens, unsigned q_heads, unsigned k_heads, unsigned hd, unsigned half, bool neox, unsigned q_stride,
        unsigned k_stride, unsigned cs_stride, float eps)
    {
        if (tokens == 0) return;
        if (hd > 512 || hd % 32 != 0 || 2 * half > hd)
            throw std::invalid_argument("metal_device: heads of other widths than qk_norm_rope takes");
        impl_->use(k_qk_norm_rope);
        impl_->bind(q, 0);
        impl_->bind(k_heads > 0 ? k : q, 1);
        impl_->bind(wq, 2);
        impl_->bind(k_heads > 0 ? wk : wq, 3);
        impl_->bind(cs, 4);
        const qk_rope_args a = { q_heads, k_heads, hd, half, neox ? 1u : 0u, q_stride, k_stride, cs_stride, eps };
        impl_->args(a, 5);
        impl_->groups(q_heads + k_heads, tokens, 1, std::min(256u, hd));
    }

    void metal_device::commands::kv_store(metal_view k, metal_view v, unsigned kv_stride, metal_view region,
        metal_view blocks, metal_view rows, unsigned tokens, const kv_layout& l)
    {
        if (tokens == 0) return;
        if (l.hd > 512 || l.hd % 32 != 0) throw std::invalid_argument("metal_device: heads of other widths than its kernels take");
        impl_->use(k_kv_store);
        impl_->bind(k, 0);
        impl_->bind(v, 1);
        impl_->bind(region, 2);
        impl_->bind(blocks, 3);
        impl_->bind(rows, 4);
        const kv_args a = { l.hd, l.q8 ? 1u : 0u, l.positions, kv_stride, l.block_stride, l.k_slab, l.v_slab, l.head_stride,
            scales_at(l) };
        impl_->args(a, 5);
        impl_->groups(l.heads, tokens, 1, l.hd);
    }

    void metal_device::commands::prepare_queries(metal_view q, unsigned tokens, unsigned heads, unsigned hd,
        unsigned q_stride, float scale, bool q8, const metal_view* factors)
    {
        if (tokens == 0) return;
        impl_->use(k_prepare_queries);
        impl_->bind(q, 0);
        const query_args a = { hd, q8 ? 1u : 0u, q_stride, scale, factors ? 1u : 0u };
        impl_->args(a, 1);
        impl_->bind(factors ? *factors : q, 2);
        impl_->groups(heads, tokens, 1, hd);
    }

    void metal_device::commands::restore_outputs(metal_view o, unsigned tokens, unsigned heads, unsigned hd,
        unsigned stride)
    {
        if (tokens == 0) return;
        impl_->use(k_restore_outputs);
        impl_->bind(o, 0);
        const query_args a = { hd, 1u, stride, 1.0f };
        impl_->args(a, 1);
        impl_->groups(heads, tokens, 1, hd);
    }

    void metal_device::commands::attention(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
        unsigned table_stride, unsigned tokens, unsigned rep0, unsigned parts, unsigned q_stride, metal_view partial,
        metal_view ctx, const kv_layout& l0, unsigned part_len)
    {
        if (tokens == 0) return;
        const grouping gr = regroup(rep0, l0);
        const unsigned rep = gr.rep;
        const kv_layout& l = gr.l;
        int kernel = attention_kernel(rep, l.hd);
        if (kernel < 0) throw std::invalid_argument("metal_device: an attention of other heads than its kernels take");
        // attention_dec (« flash-decoding ») pour les têtes de 64 et 128 valeurs, et de 256 avec 4 têtes de requête au plus ;
        // GULLIVER_OLD_DECODE garde attention_part pour les mesures.
        static const bool old_decode = std::getenv("GULLIVER_OLD_DECODE") != nullptr;
        const int r = rep == 1 ? 0 : rep == 2 ? 1 : rep == 4 ? 2 : rep == 8 ? 3 : -1;
        if (!old_decode && r >= 0 && (l.hd == 64 || l.hd == 128)) kernel = k_attention_dec + 2 * r + (l.hd == 128 ? 1 : 0);
        else if (!old_decode && r >= 0 && r <= 2 && l.hd == 256) kernel = k_attention_dec + 8 + r;
        const attention_args a = { l.hd, rep, l.heads, l.q8 ? 1u : 0u, l.positions, part_len, parts, q_stride,
            table_stride, l.window, l.block_stride, l.k_slab, l.v_slab, l.head_stride, scales_at(l), gr.kv_div };
        impl_->use(kernel);
        impl_->bind(q, 0);
        impl_->bind(region, 1);
        impl_->bind(blocks, 2);
        impl_->bind(lengths, 3);
        impl_->bind(partial, 4);
        impl_->args(a, 5);
        impl_->bind(ctx, 6);
        impl_->groups(parts, l.heads, tokens, attention_threads);
        // Une seule partie a écrit elle-même les sorties.
        if (parts == 1) return;
        impl_->use(k_merge_attention);
        impl_->bind(partial, 0);
        impl_->bind(lengths, 1);
        impl_->bind(ctx, 2);
        impl_->args(a, 3);
        impl_->groups(l.heads * rep, tokens, 1, l.hd);
    }

    bool metal_device::neural_accelerators() const { return !impl_->nax.empty(); }

    const std::string& metal_device::accelerator_note() const { return impl_->nax_note; }

    unsigned metal_device::tile_tokens(unsigned rep, unsigned hd) const
    {
        if (!grouped_by_kernels(rep)) rep = 1;   // regroup()
        // attention_nax : 16 lignes de requête par simdgroup, 8 simdgroups (128 lignes) par défaut.
        if (!impl_->nax.empty() && nax_attention_index(rep, hd) >= 0) return 16 * nax_simdgroups() / rep;
        // Comme les instances d'attention_mtile (metal_kernels.metal) : 32 lignes de requête (16 pour des têtes de 256
        // valeurs, 8 pour 512), rep par token.
        if ((rep != 1 && rep != 2 && rep != 4 && rep != 8) || (hd != 64 && hd != 128 && hd != 256 && hd != 512)) return 0;
        return (hd == 512 ? 8 : hd == 256 ? 16 : 32) / rep;
    }

    void metal_device::commands::attention_tiles(metal_view q, metal_view region, metal_view blocks, metal_view lengths,
        unsigned table_stride, metal_view tiles, unsigned n_tiles, unsigned rep0, unsigned q_stride, metal_view ctx,
        const kv_layout& l0)
    {
        if (n_tiles == 0) return;
        const grouping gr = regroup(rep0, l0);
        const unsigned rep = gr.rep;
        const kv_layout& l = gr.l;
        const int nax = impl_->device->nax.empty() ? -1 : nax_attention_index(rep, l.hd);
        if (nax >= 0)
        {
            const attention_args a = { l.hd, rep, l.heads, l.q8 ? 1u : 0u, l.positions, 0, 1, q_stride,
                table_stride, l.window, l.block_stride, l.k_slab, l.v_slab, l.head_stride, scales_at(l), gr.kv_div };
            impl_->use_nax(nax);
            impl_->bind(q, 0);
            impl_->bind(region, 1);
            impl_->bind(blocks, 2);
            impl_->bind(lengths, 3);
            impl_->bind(tiles, 4);
            impl_->args(a, 5);
            impl_->bind(ctx, 6);
            // nax_simdgroups() simdgroups de 16 lignes.
            impl_->groups(n_tiles, l.heads, 1, 32 * nax_simdgroups());
            return;
        }
        const int base = attention_kernel(rep, l.hd);
        if (base < 0) throw std::invalid_argument("metal_device: an attention of other heads than its kernels take");
        // Les têtes de 512 valeurs ont leurs instances à part, en deux tranches de 256 (troisième dimension de la grille).
        const bool wide = l.hd == 512;
        const int kernel = wide ? base - k_attention_part_512 + k_attention_mtile_512 : base - k_attention_part + k_attention_mtile;
        const attention_args a = { l.hd, rep, l.heads, l.q8 ? 1u : 0u, l.positions, 0, 1, q_stride,
            table_stride, l.window, l.block_stride, l.k_slab, l.v_slab, l.head_stride, scales_at(l), gr.kv_div };
        impl_->use(kernel);
        impl_->bind(q, 0);
        impl_->bind(region, 1);
        impl_->bind(blocks, 2);
        impl_->bind(lengths, 3);
        impl_->bind(tiles, 4);
        impl_->args(a, 5);
        impl_->bind(ctx, 6);
        // 4 simdgroups, 2 pour des têtes de 256 valeurs, 1 par tranche pour 512.
        impl_->groups(n_tiles, l.heads, wide ? 2 : 1, wide ? 32 : l.hd == 256 ? 64 : 128);
    }

    void metal_device::commands::flush()
    {
        // Le relevé par noyau (GULLIVER_GPU_PROFILE) lit ses horodatages dans un seul tampon.
        if (impl::profiling()) return;
        impl_->close();
        [impl_->buffer commit];
        impl_->sent.push_back(impl_->buffer);
        impl_->buffer = [impl_->device->queue commandBuffer];
        impl_->encoder = [impl_->buffer computeCommandEncoder];
    }

    void metal_device::commands::finish()
    {
        impl_->close();
        [impl_->buffer commit];
        [impl_->buffer waitUntilCompleted];
        if (impl::profiling()) impl_->report_profile();
        // Les tampons envoyés plus tôt se sont exécutés avant le dernier.
        impl_->sent.push_back(impl_->buffer);
        for (id<MTLCommandBuffer> b : impl_->sent)
            if (b.status == MTLCommandBufferStatusError)
                throw std::runtime_error(std::string("the GPU failed the commands: ")
                    + (b.error ? b.error.localizedDescription.UTF8String : "unknown error"));
    }

    double metal_device::commands::gpu_milliseconds() const
    {
        const id<MTLCommandBuffer> first = impl_->sent.empty() ? impl_->buffer : impl_->sent.front();
        return (impl_->buffer.GPUEndTime - first.GPUStartTime) * 1000.0;
    }

    json metal_describe()
    {
        @autoreleasepool
        {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) return json();
            json d = json::object();
            d["type"] = "gpu";
            d["name"] = device.name.UTF8String;
            d["api"] = "Metal";
            d["memory"] = static_cast<unsigned long long>(device.recommendedMaxWorkingSetSize);
            d["max_buffer"] = static_cast<unsigned long long>(device.maxBufferLength);
            d["unified_memory"] = static_cast<bool>(device.hasUnifiedMemory);
            return d;
        }
    }

}
