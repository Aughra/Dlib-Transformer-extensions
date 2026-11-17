// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    albireo-builtin : prépare les modèles fournis avec Albireo, pour le build.

        albireo-builtin --manifest <builtin_models.json> --cache <folder> [--models-out <folder>]
            [--setup <albireo-setup-online.exe> --setup-out <albireo-setup.exe>] [--threads <n>]

    Le manifeste (resources/builtin_models.json) nomme chaque modèle, le fichier dont il vient,
    avec son adresse, sa taille et son SHA-256, et la quantification qu'Albireo livre ; un modèle
    qui lit des images nomme aussi son projecteur ("projector" : le fichier mmproj qui contient
    son encodeur de vision, livré tel qu'il est publié).  Le build télécharge les fichiers dans le
    cache, nommés d'après leur empreinte (cmake/builtin_models.cmake) ; ce programme les traite
    ensuite comme l'exemple d'import de dlib le fait sur un fichier GGUF
    (examples/slm_gguf_import_ex.cpp), avec le code que fait tourner le moteur :

      - le fichier est lu (gguf_reader) et sa géométrie résolue (model_config), ce qui rejette
        une architecture ou une configuration que le moteur ne fait pas tourner ;
      - son tokenizer doit redonner un texte qu'il a encodé, et un modèle qui converse doit
        venir avec un template de chat que le moteur reconnaît ;
      - le projecteur, vérifié avec son empreinte, doit être un projecteur de vision
        (general.architecture "clip") dont la sortie a la largeur que prend le décodeur :
        clip.vision.projection_dim * (1 + ses couches deepstack) égal à
        embedding_length * (1 + n_deepstack_layers) du décodeur ;
      - le modèle est converti : ses poids sont quantifiés à nouveau quand le manifeste demande
        une autre quantification que celle publiée, et ses métadonnées reçoivent son identité
        dans Albireo (albireo.id, albireo.name, albireo.roles, albireo.inputs,
        albireo.builtin, albireo.source_sha256), par laquelle chaque copie du fichier se
        décrit elle-même ;
      - le fichier converti est relu et revérifié.

    Les fichiers convertis restent dans le cache, nommés d'après la conversion, pour qu'un
    nouveau build les réutilise.  --models-out les remet à un dossier avec leur manifeste
    builtin.json (le dossier "models" des programmes d'un arbre de build), <id>.gguf et le
    projecteur <id>.mmproj.gguf, par des liens physiques quand le dossier est sur le volume du
    cache ; --setup les ajoute à une copie de l'installateur sans modèles, sous forme de la
    surcouche de common/builtin_models.h, ce qui donne l'installateur complet.  Quand
    l'installateur et ses modèles atteindraient les 4 Gio que Windows exécute (--installer-limit
    donne une taille plus petite, pour les tests), l'installateur complet est l'installateur sans
    modèles et ses modèles se trouvent à côté de lui, dans un fichier du même nom avec
    l'extension .models.  La surcouche écrite est relue avec le lecteur de l'installateur avant
    de prendre son nom.

    QUANTIZATION
        Le mélange Q4_K_M du format GGUF : les matrices en Q4_K (blocs de 256 poids en 8
        groupes de 32, chaque groupe avec une échelle sur 6 bits et un minimum sur 6 bits,
        ajustés par une recherche aux moindres carrés pondérés), avec Q6_K pour la matrice de
        sortie (les embeddings quand le modèle les partage avec sa sortie), pour les valeurs de
        l'attention et les projections descendantes du premier et du dernier huitième des
        couches et d'une couche sur trois entre les deux.  La recherche et le mélange sont ceux
        du quantificateur de référence du format, pour que le résultat corresponde aux fichiers
        publiés en Q4_K_M ; une matrice dont les lignes ne sont pas un multiple de 256 prend
        Q5_0 (Q8_0 au lieu de Q6_K).  Les vecteurs (normes, biais) gardent leur type.
*/

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <dlib/data_io/gguf_dequantize.h>
#include <dlib/data_io/gguf_reader.h>
#include <dlib/tokenizer/hf_tokenizer.h>

#include "../common/builtin_models.h"
#include "../common/json.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "../engine/chat_format.h"
#include "../engine/model_config.h"
#include "../engine/model_file.h"
#include "../host/crypto.h"

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

using dlib::ggml_type;
using dlib::gguf_type;
using dlib::gguf_tensor_info;
using dlib::gguf_value;

namespace albireo
{
namespace
{

    // La version de la conversion, qui fait partie du nom d'un fichier converti dans le cache.
    const char* const conversion_version = "albireo-builtin 1";

    std::FILE* open_file(const std::string& path, const char* mode)
    {
        return std::fopen(path.c_str(), mode);
    }

    bool ends_with(const std::string& s, const std::string& tail)
    {
        return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
    }

    // ---- Demi-précision

    // La valeur en demi-précision la plus proche, égalités au pair, telle que sont stockées les
    // échelles des blocs.
    uint16_t to_half(float value)
    {
        uint32_t x;
        std::memcpy(&x, &value, 4);
        const uint32_t sign = (x >> 16) & 0x8000u;
        const uint32_t a = x & 0x7fffffffu;
        if (a >= 0x7f800000u) return static_cast<uint16_t>(sign | (a > 0x7f800000u ? 0x7e00u : 0x7c00u));
        if (a >= 0x477ff000u) return static_cast<uint16_t>(sign | 0x7c00u);
        const int e = static_cast<int>(a >> 23);
        if (e < 113)
        {
            // Sous le plus petit demi-flottant normal : un multiple de 2^-24.
            const int shift = 126 - e;
            if (shift > 24) return static_cast<uint16_t>(sign);
            const uint32_t m = (a & 0x7fffffu) | 0x800000u;
            uint32_t h = m >> shift;
            const uint32_t rest = m & ((1u << shift) - 1);
            const uint32_t half = 1u << (shift - 1);
            if (rest > half || (rest == half && (h & 1))) ++h;
            return static_cast<uint16_t>(sign | h);
        }
        uint32_t h = (static_cast<uint32_t>(e - 112) << 10) | ((a & 0x7fffffu) >> 13);
        const uint32_t rest = a & 0x1fffu;
        if (rest > 0x1000u || (rest == 0x1000u && (h & 1))) ++h;
        return static_cast<uint16_t>(sign | h);
    }

    float from_half(uint16_t h)
    {
        return dlib::gguf_half_to_float(h);
    }

    void put_half(uint8_t* p, uint16_t h)
    {
        p[0] = static_cast<uint8_t>(h & 0xff);
        p[1] = static_cast<uint8_t>(h >> 8);
    }

    // Arrondit à l'entier le plus proche, égalités au pair, par l'addition de 1.5 * 2^23.
    int nearest_int(float v)
    {
        const float t = v + 12582912.f;
        int32_t i;
        std::memcpy(&i, &t, 4);
        return (i & 0x007fffff) - 0x00400000;
    }

    // ---- Quantification

    /*
        L'échelle et le minimum d'un groupe de n valeurs sur nmax + 1 niveaux qui minimisent
        l'erreur pondérée par weights : les niveaux des extrêmes d'abord, puis nstep + 1
        échelles autour, chacune affinée par un ajustement aux moindres carrés de l'échelle et
        du minimum.  L reçoit les niveaux ; renvoie l'échelle et met dans the_min l'opposé du
        minimum.
    */
    float fit_scale_min(int n, int nmax, const float* x, const float* weights, uint8_t* L, float& the_min,
        uint8_t* Laux, float rmin, float rdelta, int nstep)
    {
        float min = x[0];
        float max = x[0];
        float sum_w = weights[0];
        float sum_x = sum_w * x[0];
        for (int i = 1; i < n; ++i)
        {
            if (x[i] < min) min = x[i];
            if (x[i] > max) max = x[i];
            const float w = weights[i];
            sum_w += w;
            sum_x += w * x[i];
        }
        if (min > 0) min = 0;
        if (max == min)
        {
            for (int i = 0; i < n; ++i) L[i] = 0;
            the_min = -min;
            return 0.f;
        }
        float iscale = nmax / (max - min);
        float scale = 1 / iscale;
        float best_error = 0;
        for (int i = 0; i < n; ++i)
        {
            const int l = nearest_int(iscale * (x[i] - min));
            L[i] = static_cast<uint8_t>(std::max(0, std::min(nmax, l)));
            float diff = scale * L[i] + min - x[i];
            diff = diff * diff;
            const float w = weights[i];
            best_error += w * diff;
        }
        for (int is = 0; is <= nstep; ++is)
        {
            iscale = (rmin + rdelta * is + nmax) / (max - min);
            float sum_l = 0, sum_l2 = 0, sum_xl = 0;
            for (int i = 0; i < n; ++i)
            {
                int l = nearest_int(iscale * (x[i] - min));
                l = std::max(0, std::min(nmax, l));
                Laux[i] = static_cast<uint8_t>(l);
                const float w = weights[i];
                sum_l += w * l;
                sum_l2 += w * l * l;
                sum_xl += w * l * x[i];
            }
            const float D = sum_w * sum_l2 - sum_l * sum_l;
            if (D > 0)
            {
                float this_scale = (sum_w * sum_xl - sum_x * sum_l) / D;
                float this_min = (sum_l2 * sum_x - sum_l * sum_xl) / D;
                if (this_min > 0)
                {
                    this_min = 0;
                    this_scale = sum_xl / sum_l2;
                }
                float cur_error = 0;
                for (int i = 0; i < n; ++i)
                {
                    float diff = this_scale * Laux[i] + this_min - x[i];
                    diff = diff * diff;
                    const float w = weights[i];
                    cur_error += w * diff;
                }
                if (cur_error < best_error)
                {
                    for (int i = 0; i < n; ++i) L[i] = Laux[i];
                    best_error = cur_error;
                    scale = this_scale;
                    min = this_min;
                }
            }
        }
        the_min = -min;
        return scale;
    }

    /*
        L'échelle d'un groupe de n valeurs sur les niveaux symétriques -nmax .. nmax - 1,
        l'erreur pondérée par le carré de chaque valeur : l'échelle de la plus grande valeur
        absolue, puis 18 échelles autour.  L reçoit les niveaux décalés de nmax.
    */
    float fit_symmetric(int n, int nmax, const float* x, int8_t* L)
    {
        float max = 0;
        float amax = 0;
        for (int i = 0; i < n; ++i)
        {
            const float ax = std::fabs(x[i]);
            if (ax > amax)
            {
                amax = ax;
                max = x[i];
            }
        }
        if (amax < 1e-15f)
        {
            for (int i = 0; i < n; ++i) L[i] = 0;
            return 0.f;
        }
        float iscale = -nmax / max;
        float sumlx = 0;
        float suml2 = 0;
        for (int i = 0; i < n; ++i)
        {
            int l = nearest_int(iscale * x[i]);
            l = std::max(-nmax, std::min(nmax - 1, l));
            L[i] = static_cast<int8_t>(l + nmax);
            const float w = x[i] * x[i];
            sumlx += w * x[i] * l;
            suml2 += w * l * l;
        }
        float scale = suml2 ? sumlx / suml2 : 0.0f;
        float best = scale * sumlx;
        for (int is = -9; is <= 9; ++is)
        {
            if (is == 0) continue;
            iscale = -(nmax + 0.1f * is) / max;
            sumlx = suml2 = 0;
            for (int i = 0; i < n; ++i)
            {
                int l = nearest_int(iscale * x[i]);
                l = std::max(-nmax, std::min(nmax - 1, l));
                const float w = x[i] * x[i];
                sumlx += w * x[i] * l;
                suml2 += w * l * l;
            }
            if (suml2 > 0 && sumlx * sumlx > best * suml2)
            {
                for (int i = 0; i < n; ++i)
                {
                    const int l = nearest_int(iscale * x[i]);
                    L[i] = static_cast<int8_t>(nmax + std::max(-nmax, std::min(nmax - 1, l)));
                }
                scale = sumlx / suml2;
                best = scale * sumlx;
            }
        }
        return scale;
    }

    // Q4_K : d et dmin (demi), 12 octets d'échelles et de minimums sur 6 bits tassés, 128 octets de
    // niveaux.
    void quantize_q4_k(const float* x, uint64_t n, uint8_t* y)
    {
        for (uint64_t b = 0; b < n / 256; ++b, x += 256, y += 144)
        {
            uint8_t L[256];
            uint8_t Laux[32];
            float weights[32];
            float mins[8];
            float scales[8];
            float max_scale = 0;
            float max_min = 0;
            for (int j = 0; j < 8; ++j)
            {
                float sum_x2 = 0;
                for (int l = 0; l < 32; ++l) sum_x2 += x[32 * j + l] * x[32 * j + l];
                const float av_x = std::sqrt(sum_x2 / 32);
                for (int l = 0; l < 32; ++l) weights[l] = av_x + std::fabs(x[32 * j + l]);
                scales[j] = fit_scale_min(32, 15, x + 32 * j, weights, L + 32 * j, mins[j], Laux, -1.f, 0.1f, 20);
                if (scales[j] > max_scale) max_scale = scales[j];
                if (mins[j] > max_min) max_min = mins[j];
            }
            const float inv_scale = max_scale > 0 ? 63.f / max_scale : 0.f;
            const float inv_min = max_min > 0 ? 63.f / max_min : 0.f;
            uint8_t* sc = y + 4;
            for (int j = 0; j < 8; ++j)
            {
                uint8_t ls = static_cast<uint8_t>(nearest_int(inv_scale * scales[j]));
                uint8_t lm = static_cast<uint8_t>(nearest_int(inv_min * mins[j]));
                ls = std::min<uint8_t>(63, ls);
                lm = std::min<uint8_t>(63, lm);
                if (j < 4)
                {
                    sc[j] = ls;
                    sc[j + 4] = lm;
                }
                else
                {
                    sc[j + 4] = static_cast<uint8_t>((ls & 0xF) | ((lm & 0xF) << 4));
                    sc[j - 4] |= static_cast<uint8_t>((ls >> 4) << 6);
                    sc[j] |= static_cast<uint8_t>((lm >> 4) << 6);
                }
            }
            const uint16_t d = to_half(max_scale / 63.f);
            const uint16_t dmin = to_half(max_min / 63.f);
            put_half(y, d);
            put_half(y + 2, dmin);
            for (int j = 0; j < 8; ++j)
            {
                uint8_t s, m;
                dlib::gguf_get_scale_min_k4(j, sc, s, m);
                const float dd = from_half(d) * s;
                if (!dd) continue;
                const float dm = from_half(dmin) * m;
                for (int i = 0; i < 32; ++i)
                {
                    const int l = nearest_int((x[32 * j + i] + dm) / dd);
                    L[32 * j + i] = static_cast<uint8_t>(std::max(0, std::min(15, l)));
                }
            }
            uint8_t* q = y + 16;
            for (int j = 0; j < 256; j += 64, q += 32)
                for (int l = 0; l < 32; ++l) q[l] = static_cast<uint8_t>(L[j + l] | (L[j + l + 32] << 4));
        }
    }

    // Q6_K : 128 octets de bits bas, 64 de bits hauts, 16 échelles signées sur 8 bits, d (demi).
    void quantize_q6_k(const float* x, uint64_t n, uint8_t* y)
    {
        for (uint64_t b = 0; b < n / 256; ++b, x += 256, y += 210)
        {
            int8_t L[256];
            float scales[16];
            float max_scale = 0;
            float max_abs_scale = 0;
            for (int ib = 0; ib < 16; ++ib)
            {
                const float scale = fit_symmetric(16, 32, x + 16 * ib, L + 16 * ib);
                scales[ib] = scale;
                const float abs_scale = std::fabs(scale);
                if (abs_scale > max_abs_scale)
                {
                    max_abs_scale = abs_scale;
                    max_scale = scale;
                }
            }
            if (max_abs_scale < 1e-15f)
            {
                std::memset(y, 0, 210);
                continue;
            }
            const float iscale = -128.f / max_scale;
            const uint16_t d = to_half(1 / iscale);
            put_half(y + 208, d);
            int8_t* sc = reinterpret_cast<int8_t*>(y + 192);
            for (int ib = 0; ib < 16; ++ib)
                sc[ib] = static_cast<int8_t>(std::min(127, nearest_int(iscale * scales[ib])));
            for (int j = 0; j < 16; ++j)
            {
                const float dd = from_half(d) * sc[j];
                if (!dd) continue;
                for (int ii = 0; ii < 16; ++ii)
                {
                    int l = nearest_int(x[16 * j + ii] / dd);
                    l = std::max(-32, std::min(31, l));
                    L[16 * j + ii] = static_cast<int8_t>(l + 32);
                }
            }
            uint8_t* ql = y;
            uint8_t* qh = y + 128;
            for (int j = 0; j < 256; j += 128, ql += 64, qh += 32)
            {
                for (int l = 0; l < 32; ++l)
                {
                    const uint8_t q1 = L[j + l] & 0xF;
                    const uint8_t q2 = L[j + l + 32] & 0xF;
                    const uint8_t q3 = L[j + l + 64] & 0xF;
                    const uint8_t q4 = L[j + l + 96] & 0xF;
                    ql[l] = static_cast<uint8_t>(q1 | (q3 << 4));
                    ql[l + 32] = static_cast<uint8_t>(q2 | (q4 << 4));
                    qh[l] = static_cast<uint8_t>((L[j + l] >> 4) | ((L[j + l + 32] >> 4) << 2)
                        | ((L[j + l + 64] >> 4) << 4) | ((L[j + l + 96] >> 4) << 6));
                }
            }
        }
    }

    // Q8_0 : d (demi) et 32 octets signés.
    void quantize_q8_0(const float* x, uint64_t n, uint8_t* y)
    {
        for (uint64_t b = 0; b < n / 32; ++b, x += 32, y += 34)
        {
            float amax = 0.0f;
            for (int j = 0; j < 32; ++j) amax = std::max(amax, std::fabs(x[j]));
            const float d = amax / 127;
            const float id = d ? 1.0f / d : 0.0f;
            put_half(y, to_half(d));
            for (int j = 0; j < 32; ++j) y[2 + j] = static_cast<uint8_t>(static_cast<int8_t>(std::round(x[j] * id)));
        }
    }

    // Q5_0 : d (demi), les cinquièmes bits des 32 niveaux, leurs 4 bits bas.
    void quantize_q5_0(const float* x, uint64_t n, uint8_t* y)
    {
        for (uint64_t b = 0; b < n / 32; ++b, x += 32, y += 22)
        {
            float amax = 0.0f;
            float max = 0.0f;
            for (int j = 0; j < 32; ++j)
            {
                if (amax < std::fabs(x[j]))
                {
                    amax = std::fabs(x[j]);
                    max = x[j];
                }
            }
            const float d = max / -16;
            const float id = d ? 1.0f / d : 0.0f;
            put_half(y, to_half(d));
            uint32_t qh = 0;
            for (int j = 0; j < 16; ++j)
            {
                const float x0 = x[j] * id;
                const float x1 = x[16 + j] * id;
                const int l0 = static_cast<int8_t>(x0 + 16.5f);
                const int l1 = static_cast<int8_t>(x1 + 16.5f);
                const uint8_t xi0 = static_cast<uint8_t>(std::min(31, l0));
                const uint8_t xi1 = static_cast<uint8_t>(std::min(31, l1));
                y[6 + j] = static_cast<uint8_t>((xi0 & 0x0F) | ((xi1 & 0x0F) << 4));
                qh |= ((xi0 & 0x10u) >> 4) << j;
                qh |= ((xi1 & 0x10u) >> 4) << (j + 16);
            }
            std::memcpy(y + 2, &qh, 4);
        }
    }

    uint64_t row_bytes(ggml_type t, uint64_t n)
    {
        const dlib::ggml_type_traits tr = dlib::get_ggml_type_traits(t);
        return n / tr.block_size * tr.type_size;
    }

    void quantize_row(ggml_type t, const float* x, uint64_t n, uint8_t* y)
    {
        switch (t)
        {
        case ggml_type::Q4_K: quantize_q4_k(x, n, y); break;
        case ggml_type::Q6_K: quantize_q6_k(x, n, y); break;
        case ggml_type::Q8_0: quantize_q8_0(x, n, y); break;
        case ggml_type::Q5_0: quantize_q5_0(x, n, y); break;
        case ggml_type::F16:
            for (uint64_t i = 0; i < n; ++i) put_half(y + 2 * i, to_half(x[i]));
            break;
        case ggml_type::F32: std::memcpy(y, x, n * sizeof(float)); break;
        default: throw std::runtime_error("no quantization to this type");
        }
    }

    const char* type_name(ggml_type t)
    {
        switch (t)
        {
        case ggml_type::F32: return "F32";
        case ggml_type::F16: return "F16";
        case ggml_type::BF16: return "BF16";
        case ggml_type::Q4_K: return "Q4_K";
        case ggml_type::Q5_0: return "Q5_0";
        case ggml_type::Q6_K: return "Q6_K";
        case ggml_type::Q8_0: return "Q8_0";
        default: return "other";
        }
    }

    // ---- Le mélange Q4_K_M

    bool quantizable(const gguf_tensor_info& t)
    {
        return t.dims.size() >= 2 && ends_with(t.name, "weight") && t.name.find("_norm.weight") == std::string::npos
            && t.name.find("ffn_gate_inp.weight") == std::string::npos;
    }

    // Le premier et le dernier huitième des couches, et une couche sur trois entre les deux.
    bool more_bits(int i, int n)
    {
        return i < n / 8 || i >= 7 * n / 8 || (i - n / 8) % 3 == 2;
    }

    // L'ordre des tenseurs : ceux hors des couches, puis les couches dans leur ordre,
    // par nom dans chacune.
    int layer_of(const std::string& name)
    {
        int layer = -1;
        if (name.compare(0, 4, "blk.") == 0) layer = std::atoi(name.c_str() + 4);
        return layer;
    }

    bool tensor_before(const gguf_tensor_info* a, const gguf_tensor_info* b)
    {
        const int la = layer_of(a->name), lb = layer_of(b->name);
        if (la != lb) return la < lb;
        return a->name < b->name;
    }

    struct tensor_plan
    {
        const gguf_tensor_info* src = nullptr;
        ggml_type type = ggml_type::F32;
        uint64_t nbytes = 0;
        uint64_t offset = 0;
    };

    std::vector<tensor_plan> plan_tensors(const dlib::gguf_reader& r, const std::string& quantization,
        const std::string& published, int n_layers)
    {
        std::vector<const gguf_tensor_info*> order;
        for (const auto& t : r.tensors()) order.push_back(&t);
        std::sort(order.begin(), order.end(), tensor_before);
        // Un fichier publié dans la quantification qu'Albireo livre garde ses tenseurs tels quels.
        const bool requantize = quantization == "Q4_K_M" && published != quantization;
        bool tied = true;
        int n_attn_v = 0;
        for (const auto* t : order)
        {
            if (t->name == "output.weight") tied = false;
            if (t->name.find("attn_v.weight") != std::string::npos) ++n_attn_v;
        }
        int i_attn_v = 0;
        int i_ffn_down = 0;
        std::vector<tensor_plan> plan;
        uint64_t offset = 0;
        for (const auto* t : order)
        {
            tensor_plan p;
            p.src = t;
            p.type = t->type;
            if (requantize && quantizable(*t))
            {
                // Un tenseur est converti ligne par ligne : il lui faut des lignes, d'au moins une
                // valeur chacune.
                for (uint64_t d : t->dims)
                    if (d == 0) throw std::runtime_error(t->name + ": a dimension of the tensor is zero");
                const uint64_t ne0 = t->dims[0];
                ggml_type type = ggml_type::Q4_K;
                if (t->name == "output.weight" || (tied && t->name == "token_embd.weight"))
                {
                    type = ne0 % 256 == 0 ? ggml_type::Q6_K : ggml_type::Q8_0;
                }
                else if (t->name.find("attn_v.weight") != std::string::npos)
                {
                    if (more_bits(i_attn_v, n_attn_v)) type = ggml_type::Q6_K;
                    ++i_attn_v;
                }
                else if (t->name.find("ffn_down") != std::string::npos)
                {
                    if (more_bits(i_ffn_down, n_layers)) type = ggml_type::Q6_K;
                    ++i_ffn_down;
                }
                if (ne0 % 256 != 0)
                {
                    if (type == ggml_type::Q4_K) type = ggml_type::Q5_0;
                    else if (type == ggml_type::Q6_K) type = ggml_type::Q8_0;
                    if (ne0 % 32 != 0) type = ggml_type::F16;
                }
                p.type = type;
            }
            p.nbytes = row_bytes(p.type, t->n_elements());
            p.offset = offset;
            offset += (p.nbytes + r.alignment() - 1) / r.alignment() * r.alignment();
            plan.push_back(p);
        }
        return plan;
    }

    // ---- Écriture d'un fichier GGUF

    class gguf_out
    {
    public:
        gguf_out(std::FILE* f, sha256_stream& sha) : f_(f), sha_(sha) {}

        void bytes(const void* p, size_t n)
        {
            if (n && std::fwrite(p, 1, n, f_) != n) throw std::runtime_error("the converted file cannot be written");
            sha_.update(p, n);
            written_ += n;
        }
        template <typename T> void raw(T v) { bytes(&v, sizeof(v)); }
        void str(const std::string& s)
        {
            raw<uint64_t>(s.size());
            bytes(s.data(), s.size());
        }
        void zeros(uint64_t n)
        {
            static const char z[64] = { 0 };
            while (n > 0)
            {
                const size_t k = static_cast<size_t>(std::min<uint64_t>(n, sizeof(z)));
                bytes(z, k);
                n -= k;
            }
        }
        uint64_t written() const { return written_; }

    private:
        std::FILE* f_;
        sha256_stream& sha_;
        uint64_t written_ = 0;
    };

    void put_scalar(gguf_out& o, gguf_type t, int64_t i, double d, const std::string& s)
    {
        switch (t)
        {
        case gguf_type::UINT8: o.raw<uint8_t>(static_cast<uint8_t>(i)); break;
        case gguf_type::INT8: o.raw<int8_t>(static_cast<int8_t>(i)); break;
        case gguf_type::UINT16: o.raw<uint16_t>(static_cast<uint16_t>(i)); break;
        case gguf_type::INT16: o.raw<int16_t>(static_cast<int16_t>(i)); break;
        case gguf_type::UINT32: o.raw<uint32_t>(static_cast<uint32_t>(i)); break;
        case gguf_type::INT32: o.raw<int32_t>(static_cast<int32_t>(i)); break;
        case gguf_type::UINT64: o.raw<uint64_t>(static_cast<uint64_t>(i)); break;
        case gguf_type::INT64: o.raw<int64_t>(i); break;
        case gguf_type::BOOL: o.raw<uint8_t>(i ? 1 : 0); break;
        case gguf_type::FLOAT32: o.raw<float>(static_cast<float>(d)); break;
        case gguf_type::FLOAT64: o.raw<double>(d); break;
        case gguf_type::STRING: o.str(s); break;
        default: throw std::runtime_error("unknown type of metadata value");
        }
    }

    void put_value(gguf_out& o, const gguf_value& v)
    {
        o.raw<uint32_t>(static_cast<uint32_t>(v.type));
        if (v.type != gguf_type::ARRAY)
        {
            put_scalar(o, v.type, v.i, v.d, v.s);
            return;
        }
        o.raw<uint32_t>(static_cast<uint32_t>(v.array_type));
        if (v.array_type == gguf_type::STRING)
        {
            o.raw<uint64_t>(v.arr_str.size());
            for (const auto& s : v.arr_str) o.str(s);
        }
        else if (v.array_type == gguf_type::FLOAT32 || v.array_type == gguf_type::FLOAT64)
        {
            o.raw<uint64_t>(v.arr_float.size());
            for (double d : v.arr_float) put_scalar(o, v.array_type, 0, d, std::string());
        }
        else
        {
            o.raw<uint64_t>(v.arr_int.size());
            for (int64_t i : v.arr_int) put_scalar(o, v.array_type, i, 0, std::string());
        }
    }

    gguf_value string_value(const std::string& s)
    {
        gguf_value v;
        v.type = gguf_type::STRING;
        v.array_type = gguf_type::UINT8;
        v.i = 0;
        v.d = 0;
        v.s = s;
        return v;
    }

    gguf_value strings_value(const json& list)
    {
        gguf_value v = string_value(std::string());
        v.type = gguf_type::ARRAY;
        v.array_type = gguf_type::STRING;
        for (const auto& e : list.elements()) v.arr_str.push_back(e.as_string());
        return v;
    }

    gguf_value integer_value(gguf_type t, int64_t i)
    {
        gguf_value v = string_value(std::string());
        v.type = t;
        v.i = i;
        return v;
    }

    // ---- Conversion

    struct model_entry
    {
        json desc;                 // l'entrée du manifeste
        std::string id;
        std::string source;        // le fichier téléchargé
        std::string source_sha;
        std::string quantization;  // celle qui est livrée
        std::string key;           // nomme la conversion
        std::string converted;     // le fichier converti dans le cache
        std::string projector;     // le projecteur dans le cache, livré tel que publié ; vide sans projecteur
    };

    // Les lignes d'un tenseur en flottants, à partir de ses octets.
    void rows_as_floats(const gguf_tensor_info& t, const uint8_t* raw, uint64_t row0, uint64_t nrows,
        std::vector<float>& out)
    {
        const uint64_t ne0 = t.dims[0];
        const uint64_t rb = row_bytes(t.type, ne0);
        gguf_tensor_info part = t;
        part.dims = { ne0, nrows };
        part.offset = 0;
        part.nbytes = rb * nrows;
        const std::vector<uint8_t> bytes(raw + row0 * rb, raw + (row0 + nrows) * rb);
        dlib::gguf_dequantize(part, bytes, out);
    }

    // L'erreur relative d'une ligne quantifiée par rapport à sa source.
    double row_error(const gguf_tensor_info& src, const std::vector<uint8_t>& raw, ggml_type type,
        const std::vector<uint8_t>& out, uint64_t row)
    {
        const uint64_t ne0 = src.dims[0];
        std::vector<float> a, b;
        rows_as_floats(src, raw.data(), row, 1, a);
        gguf_tensor_info q = src;
        q.type = type;
        rows_as_floats(q, out.data(), row, 1, b);
        double num = 0, den = 0;
        for (uint64_t i = 0; i < ne0; ++i)
        {
            num += (a[i] - b[i]) * static_cast<double>(a[i] - b[i]);
            den += a[i] * static_cast<double>(a[i]);
        }
        return den > 0 ? std::sqrt(num / den) : std::sqrt(num);
    }

    double error_bound(ggml_type t)
    {
        switch (t)
        {
        case ggml_type::Q4_K: return 0.2;
        case ggml_type::Q5_0: return 0.1;
        case ggml_type::Q6_K: return 0.06;
        case ggml_type::Q8_0: return 0.02;
        default: return 0.002;
        }
    }

    // L'identifiant de ce processus, qui nomme ses fichiers partiels dans le cache.
    unsigned long process_id()
    {
        return static_cast<unsigned long>(::getpid());
    }

    // Si un processus de cet identifiant tourne ; un processus que le système cache à celui-ci compte
    // comme tournant.
    bool process_running(unsigned long pid)
    {
        return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
    }

    /*
        Supprime les fichiers partiels d'un fichier converti, <file>.<process>.partial, dont le
        processus s'est terminé avant de les renommer, comme le fait un build interrompu.  Le
        fichier partiel d'une conversion encore en cours reste.
    */
    void remove_stale_partials(const std::string& converted)
    {
        const std::string dir = parent_path(converted);
        const std::string prefix = file_name(converted) + ".";
        const std::string suffix = ".partial";
        for (const auto& n : list_dir(dir))
        {
            if (n.size() <= prefix.size() + suffix.size() || n.compare(0, prefix.size(), prefix) != 0
                || !ends_with(n, suffix))
                continue;
            const std::string pid = n.substr(prefix.size(), n.size() - prefix.size() - suffix.size());
            if (pid.find_first_not_of("0123456789") != std::string::npos) continue;
            if (!process_running(std::strtoul(pid.c_str(), nullptr, 10))) remove_file(join_path(dir, n));
        }
    }

    // Supprime le fichier partiel à la fin de sa portée ; un fichier renommé avant garde son nouveau
    // nom.
    class partial_file
    {
    public:
        explicit partial_file(const std::string& path) : path_(path) {}
        ~partial_file() { remove_file(path_); }
        partial_file(const partial_file&) = delete;
        partial_file& operator=(const partial_file&) = delete;

    private:
        std::string path_;
    };

    void convert(model_entry& m, unsigned threads, std::string& sha, uint64_t& size)
    {
        dlib::gguf_reader r(m.source);
        // Les tenseurs des deux fichiers commencent à des multiples de l'alignement, une puissance de
        // deux dans le format.
        const uint64_t alignment = r.alignment();
        if (alignment == 0 || alignment > 65536 || (alignment & (alignment - 1)) != 0)
            throw std::runtime_error("the alignment of " + m.source + ", " + std::to_string(alignment)
                + ", is not a power of two between 1 and 65536");
        const std::string arch = r.get_str("general.architecture");
        const int n_layers = static_cast<int>(r.get_int(arch + ".block_count"));
        const std::vector<tensor_plan> plan = plan_tensors(r, m.quantization,
            m.desc["from"]["quantization"].as_string(), n_layers);
        bool changed = false;
        for (const auto& p : plan) changed = changed || p.type != p.src->type;

        std::map<std::string, gguf_value> meta = r.metadata();
        meta.erase("general.alignment");
        if (changed)
        {
            // LLAMA_FTYPE_MOSTLY_Q4_K_M
            meta["general.file_type"] = integer_value(gguf_type::UINT32, 15);
            meta["general.quantization_version"] = integer_value(gguf_type::UINT32, 2);
        }
        meta["albireo.id"] = string_value(m.id);
        meta["albireo.name"] = string_value(m.desc["name"].as_string());
        meta["albireo.roles"] = strings_value(m.desc["roles"]);
        meta["albireo.inputs"] = strings_value(m.desc["inputs"]);
        gguf_value builtin = integer_value(gguf_type::BOOL, 1);
        meta["albireo.builtin"] = builtin;
        meta["albireo.source_sha256"] = string_value(m.source_sha);

        // Chaque processus écrit un fichier partiel à lui, pour que deux builds qui partagent le cache ne
        // mélangent jamais leur sortie.
        const std::string tmp = m.converted + "." + std::to_string(process_id()) + ".partial";
        remove_stale_partials(m.converted);
        /*
            Détruits dans l'ordre inverse de leurs déclarations : le flux est fermé tant que son
            tampon existe, puis la garde supprime le fichier partiel d'une conversion qui a échoué.
        */
        std::vector<char> buffer(4u << 20);
        const partial_file partial(tmp);
        std::FILE* f = open_file(tmp, "wb");
        if (!f) throw std::runtime_error("cannot create " + tmp);
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> closer(f, std::fclose);
        std::setvbuf(f, buffer.data(), _IOFBF, buffer.size());
        sha256_stream hash;
        gguf_out o(f, hash);
        o.raw<uint32_t>(0x46554747u);   // "GGUF"
        o.raw<uint32_t>(3);
        o.raw<uint64_t>(plan.size());
        o.raw<uint64_t>(meta.size() + 1);
        // L'architecture d'abord, comme l'écrivent les convertisseurs du format.
        o.str("general.architecture");
        put_value(o, meta.at("general.architecture"));
        for (const auto& kv : meta)
        {
            if (kv.first == "general.architecture") continue;
            o.str(kv.first);
            put_value(o, kv.second);
        }
        o.str("general.alignment");
        put_value(o, integer_value(gguf_type::UINT32, static_cast<int64_t>(alignment)));
        for (const auto& p : plan)
        {
            o.str(p.src->name);
            o.raw<uint32_t>(static_cast<uint32_t>(p.src->dims.size()));
            for (uint64_t d : p.src->dims) o.raw<uint64_t>(d);
            o.raw<uint32_t>(static_cast<uint32_t>(p.type));
            o.raw<uint64_t>(p.offset);
        }
        o.zeros((alignment - o.written() % alignment) % alignment);
        const uint64_t data_start = o.written();

        std::vector<uint8_t> raw;
        std::vector<uint8_t> out;
        for (size_t k = 0; k < plan.size(); ++k)
        {
            const tensor_plan& p = plan[k];
            o.zeros(data_start + p.offset - o.written());
            r.read_tensor_raw(*p.src, raw);
            if (p.type == p.src->type)
            {
                o.bytes(raw.data(), raw.size());
                continue;
            }
            const uint64_t ne0 = p.src->dims[0];
            const uint64_t nrows = p.src->n_elements() / ne0;
            const uint64_t out_row = row_bytes(p.type, ne0);
            out.assign(static_cast<size_t>(p.nbytes), 0);
            // Les lignes par morceaux, sur les threads.
            std::atomic<uint64_t> next(0);
            std::atomic<bool> failed(false);
            std::string failure;
            auto work = [&]() {
                std::vector<float> rows;
                const uint64_t chunk = 16;
                try
                {
                    for (;;)
                    {
                        const uint64_t r0 = next.fetch_add(chunk);
                        if (r0 >= nrows || failed) break;
                        const uint64_t n = std::min(chunk, nrows - r0);
                        rows_as_floats(*p.src, raw.data(), r0, n, rows);
                        for (uint64_t i = 0; i < n; ++i)
                            quantize_row(p.type, rows.data() + i * ne0, ne0, out.data() + (r0 + i) * out_row);
                    }
                }
                catch (const std::exception& e)
                {
                    if (!failed.exchange(true)) failure = e.what();
                }
            };
            std::vector<std::thread> pool;
            for (unsigned t = 1; t < threads; ++t) pool.emplace_back(work);
            work();
            for (auto& t : pool) t.join();
            if (failed) throw std::runtime_error(p.src->name + ": " + failure);
            // La première et la dernière ligne comparées à leur source.
            for (uint64_t row : { uint64_t(0), nrows - 1 })
            {
                const double e = row_error(*p.src, raw, p.type, out, row);
                if (!(e <= error_bound(p.type)))
                    throw std::runtime_error(p.src->name + ": the quantized row " + std::to_string(row)
                        + " differs from its source by " + std::to_string(e));
            }
            o.bytes(out.data(), out.size());
            std::cout << "  " << p.src->name << ": " << type_name(p.src->type) << " -> " << type_name(p.type) << "\n";
        }
        if (std::fflush(f) != 0) throw std::runtime_error("the converted file cannot be written");
        closer.reset();
        sha = hash.hex();
        size = o.written();
        remove_file(m.converted);
        if (!rename_file(tmp, m.converted)) throw std::runtime_error("cannot rename " + tmp);
    }

    // ---- Vérifications d'un fichier, tel que le moteur le lit

    // Vérifie un fichier tel que le moteur le lit ; un fichier converti porte aussi son identité.
    json check_model(const std::string& path, const json& desc, bool converted)
    {
        model_file file(path);
        const model_config cfg = read_model_config(file);
        dlib::hf_tokenizer tok;
        tok.load_from_gguf(file.reader());
        if (static_cast<long>(tok.size()) != cfg.vocab)
            throw std::runtime_error("the vocabulary and the output layer of the model disagree");
        const std::string sample = "Albireo, 2026: Bonjour ! Quelle est la capitale de l'Italie ? \xC3\x89t\xC3\xA9, "
            "\xE6\xBC\xA2\xE5\xAD\x97, int main() { return 0; }";
        const std::vector<int> ids = tok.encode(sample, false, false, false);
        if (ids.empty() || tok.decode(ids, false) != sample)
            throw std::runtime_error("the tokenizer does not give back the text it encoded");
        const bool embedding = desc["embedding"].as_bool(false);
        if (cfg.is_embedding() != embedding)
            throw std::runtime_error(embedding ? "the file is not an embedding model"
                : "the file is an embedding model, the manifest names a language model");
        std::string format = "embedding";
        if (!embedding)
        {
            format = chat_format::detect(tok, cfg.chat_template)->name();
            if (format == "plain") throw std::runtime_error("the engine does not recognize the chat template");
        }
        const dlib::gguf_reader& r = file.reader();
        uint64_t parameters = 0;
        for (const auto& t : r.tensors()) parameters += t.n_elements();
        json info = json::object();
        info["arch"] = r.get_str("general.architecture");
        info["parameters"] = static_cast<unsigned long long>(parameters);
        info["context"] = static_cast<long long>(cfg.ctx_train);
        info["format"] = format;
        if (converted && r.get_str("albireo.id") != desc["id"].as_string())
            throw std::runtime_error("the identity of the converted file is wrong");
        return info;
    }

    /*
        Vérifie un projecteur par rapport au décodeur auquel il est associé : un projecteur de
        vision (general.architecture "clip" avec un encodeur de vision) dont la sortie, large de
        clip.vision.projection_dim avec autant de largeurs de plus qu'il a de couches deepstack,
        a la largeur des tokens de vision que prend le décodeur,
        embedding_length * (1 + n_deepstack_layers).  Renvoie son type et cette largeur.
    */
    json check_projector(const std::string& projector, const std::string& decoder)
    {
        const dlib::gguf_reader p(projector);
        if (p.get_str("general.architecture") != "clip")
            throw std::runtime_error(projector + " is not a projector (general.architecture is not clip)");
        if (p.get_int("clip.has_vision_encoder", 1) == 0)
            throw std::runtime_error(projector + " holds no vision encoder");
        int64_t deepstack = 0;
        if (p.has("clip.vision.is_deepstack_layers"))
            for (int64_t v : p.at("clip.vision.is_deepstack_layers").arr_int) deepstack += v != 0 ? 1 : 0;
        int64_t width = p.get_int("clip.vision.projection_dim", 0) * (1 + deepstack);
        // Un projecteur LLaVA ne nomme aucune largeur de projection : sa dernière couche la donne.
        const dlib::gguf_tensor_info* last = p.find_tensor("mm.2.weight");
        if (width <= 0 && last && last->dims.size() >= 2) width = static_cast<int64_t>(last->dims[1]);
        const dlib::gguf_reader d(decoder);
        const std::string arch = d.get_str("general.architecture");
        const int64_t wanted = d.get_int(arch + ".embedding_length", 0)
            * (1 + d.get_int(arch + ".n_deepstack_layers", 0));
        if (width <= 0 || width != wanted)
            throw std::runtime_error("the projector gives vision tokens of width " + std::to_string(width) + ", the "
                + arch + " decoder takes " + std::to_string(wanted));
        json info = json::object();
        info["projector_type"] = p.get_str("clip.projector_type");
        info["projection_width"] = static_cast<long long>(width);
        info["image_size"] = static_cast<long long>(p.get_int("clip.vision.image_size", 0));
        return info;
    }

    // ---- Le manifeste et les fichiers

    std::string conversion_key(const json& desc, const std::string& source_sha, const std::string& quantization)
    {
        std::string s = std::string(conversion_version) + "|" + source_sha + "|" + quantization + "|"
            + desc["id"].as_string() + "|" + desc["name"].as_string() + "|" + desc["roles"].dump() + "|"
            + desc["inputs"].dump();
        return sha256_hex(s).substr(0, 12);
    }

    bool link_or_copy(const std::string& from, const std::string& to)
    {
        remove_file(to);
        if (::link(from.c_str(), to.c_str()) == 0) return true;
        return copy_file(from, to);
    }

    bool copy_range(std::FILE* in, uint64_t size, std::FILE* out)
    {
        std::vector<char> b(1u << 20);
        while (size > 0)
        {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(size, b.size()));
            if (std::fread(b.data(), 1, n, in) != n || std::fwrite(b.data(), 1, n, out) != n) return false;
            size -= n;
        }
        return true;
    }

    // Le fichier des modèles à côté de l'installateur complet produit : son nom avec l'extension
    // .models.
    std::string beside_installer(const std::string& output)
    {
        const std::string name = file_name(output);
        const size_t dot = name.rfind('.');
        const std::string extension = dot == std::string::npos ? std::string() : name.substr(dot);
        const bool exe = dot != std::string::npos && dot > 0 && (extension == ".exe" || extension == ".EXE");
        return (exe ? output.substr(0, output.size() - extension.size()) : output) + builtin_overlay_extension;
    }

    /*
        Relit la surcouche qui termine file et vérifie qu'elle nomme les fichiers de modèles,
        avec leurs tailles, là où ils ont été écrits : le lecteur de l'installateur
        (find_builtin_overlay()) les trouve de la même manière.
    */
    void check_overlay(const std::string& file, uint64_t lower, const std::vector<model_entry>& models)
    {
        std::FILE* f = open_file(file, "rb");
        if (!f) throw std::runtime_error("cannot read " + file);
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> closer(f, std::fclose);
        auto read = [&](uint64_t pos, void* data, size_t n) {
            if (fseeko(f, static_cast<off_t>(pos), SEEK_SET) != 0) return false;
            return std::fread(data, 1, n, f) == n;
        };
        uint64_t start = 0;
        builtin_trailer trailer;
        std::string text;
        const json manifest = find_builtin_overlay(read, lower, file_size(file), start, trailer, text)
            ? json::parse_or_null(text) : json();
        bool valid = manifest["models"].size() == models.size() && manifest["version"].as_string() == ALBIREO_VERSION;
        for (size_t i = 0; valid && i < models.size(); ++i)
        {
            const json& e = manifest["models"].elements()[i];
            valid = e["offset"].as_int(-1) >= 0 && static_cast<uint64_t>(e["offset"].as_int(0))
                + file_size(models[i].converted) <= trailer.manifest_offset;
            if (valid && !models[i].projector.empty())
                valid = e["projector"]["offset"].as_int(-1) >= 0 && static_cast<uint64_t>(e["projector"]["offset"]
                    .as_int(0)) + file_size(models[i].projector) <= trailer.manifest_offset;
        }
        if (!valid) throw std::runtime_error("the models written in " + file + " cannot be read back");
    }

    /*
        L'installateur complet : l'installateur sans modèles setup, suivi de la surcouche des
        modèles (common/builtin_models.h).  Quand les deux dépasseraient limit, la taille que
        Windows exécute, output est l'installateur sans modèles et la surcouche se trouve dans un
        fichier à part à côté de lui (beside_installer()), dès son premier octet ; un fichier de
        modèles laissé à côté de output par un build précédent disparaît dans le cas contraire.
    */
    void pack_setup(const std::string& setup, const std::string& output, const std::vector<model_entry>& models,
        json manifest, uint64_t limit)
    {
        // Les positions des fichiers dans la surcouche, puis le manifeste qui les donne.
        uint64_t at = 0;
        for (size_t i = 0; i < models.size(); ++i)
        {
            json& entry = manifest["models"].elements()[i];
            entry["offset"] = static_cast<unsigned long long>(at);
            at += file_size(models[i].converted);
            if (!models[i].projector.empty())
            {
                entry["projector"]["offset"] = static_cast<unsigned long long>(at);
                at += file_size(models[i].projector);
            }
        }
        const std::string text = manifest.dump();
        const uint64_t program = file_size(setup);
        const uint64_t whole = program + at + text.size() + sizeof(builtin_trailer) + 7;
        const bool beside = whole > limit;
        const std::string overlay_file = beside ? beside_installer(output) : output;

        const std::string tmp = overlay_file + ".partial";
        std::FILE* out = open_file(tmp, "wb");
        if (!out) throw std::runtime_error("cannot create " + tmp);
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> closer(out, std::fclose);
        // Copie length octets de file dans out.
        auto append = [&](const std::string& file, uint64_t length) {
            std::FILE* m = open_file(file, "rb");
            if (!m) throw std::runtime_error("cannot read " + file);
            const bool ok = copy_range(m, length, out);
            std::fclose(m);
            if (!ok) throw std::runtime_error("cannot append " + file);
        };
        if (!beside) append(setup, program);
        for (const auto& m : models)
        {
            append(m.converted, file_size(m.converted));
            if (!m.projector.empty()) append(m.projector, file_size(m.projector));
        }
        builtin_trailer trailer;
        trailer.manifest_offset = at;
        trailer.manifest_size = text.size();
        const uint64_t end = (beside ? 0 : program) + at + text.size() + sizeof(trailer);
        const uint64_t pad = (8 - end % 8) % 8;
        trailer.overlay_size = at + text.size() + pad + sizeof(trailer);
        std::vector<char> zeros(static_cast<size_t>(pad), 0);
        if (std::fwrite(text.data(), 1, text.size(), out) != text.size()
            || (pad && std::fwrite(zeros.data(), 1, zeros.size(), out) != zeros.size())
            || std::fwrite(&trailer, 1, sizeof(trailer), out) != sizeof(trailer) || std::fflush(out) != 0)
            throw std::runtime_error("cannot write " + tmp);
        closer.reset();
        check_overlay(tmp, beside ? 0 : program, models);
        remove_file(overlay_file);
        if (!rename_file(tmp, overlay_file)) throw std::runtime_error("cannot rename " + tmp);

        if (beside)
        {
            // L'installateur est celui sans modèles, sous le nom de l'installateur complet.
            const std::string copy = output + ".partial";
            std::FILE* o = open_file(copy, "wb");
            if (!o) throw std::runtime_error("cannot create " + copy);
            std::unique_ptr<std::FILE, int (*)(std::FILE*)> copy_closer(o, std::fclose);
            std::FILE* in = open_file(setup, "rb");
            if (!in) throw std::runtime_error("cannot read " + setup);
            const bool copied = copy_range(in, program, o) && std::fflush(o) == 0;
            std::fclose(in);
            if (!copied) throw std::runtime_error("cannot copy " + setup);
            copy_closer.reset();
            remove_file(output);
            if (!rename_file(copy, output)) throw std::runtime_error("cannot rename " + copy);
            std::cout << "the program and its models would exceed " << limit << " bytes: the models lie in "
                      << overlay_file << ", which travels with " << file_name(output) << "\n";
        }
        else
            remove_file(beside_installer(output));
        // Les droits du programme qu'il prolonge.
        struct stat st;
        if (::stat(setup.c_str(), &st) == 0) ::chmod(output.c_str(), st.st_mode & 07777);
    }

    int usage()
    {
        std::cerr << "usage: albireo-builtin --manifest <builtin_models.json> --cache <folder>\n"
                     "           [--models-out <folder>] [--setup <setup.exe> --setup-out <setup.exe>]\n"
                     "           [--installer-limit <bytes>] [--threads <n>]\n";
        return 2;
    }

    int run(int argc, char** argv)
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i + 1 < argc; i += 2)
        {
            const std::string k = argv[i];
            if (k.compare(0, 2, "--") != 0) return usage();
            args[k.substr(2)] = argv[i + 1];
        }
        if (argc % 2 == 0 || !args.count("manifest") || !args.count("cache")) return usage();
        if (args.count("setup") != args.count("setup-out")) return usage();
        unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        if (args.count("threads")) threads = std::max(1, std::atoi(args["threads"].c_str()));

        std::string text;
        if (!read_file(args["manifest"], text, 16u << 20)) throw std::runtime_error("cannot read " + args["manifest"]);
        const json manifest = json::parse_or_null(text);
        if (!manifest["models"].is_array()) throw std::runtime_error("the manifest lists no models");
        const std::string cache = args["cache"];
        make_dirs(cache);

        std::vector<model_entry> models;
        json out = json::object();
        out["format"] = 1;
        out["version"] = ALBIREO_VERSION;
        out["models"] = json::array();
        for (const auto& d : manifest["models"].elements())
        {
            model_entry m;
            m.desc = d;
            m.id = d["id"].as_string();
            m.source_sha = d["from"]["sha256"].as_string();
            m.quantization = d["quantization"].str(d["from"]["quantization"].as_string());
            m.source = join_path(cache, m.source_sha + ".gguf");
            m.key = conversion_key(d, m.source_sha, m.quantization);
            m.converted = join_path(cache, m.id + "-" + m.key + ".gguf");
            std::cout << m.id << "\n";

            /*
                Un fichier converti de la même conversion est réutilisé quand sa taille et son
                SHA-256 sont ceux de sa description, écrite une fois qu'il a été vérifié ; il est
                converti à nouveau sinon.
            */
            const std::string side = m.converted + ".json";
            std::string side_text;
            json info = file_exists(m.converted) && read_file(side, side_text, 1u << 20)
                ? json::parse_or_null(side_text) : json();
            bool reuse = info.is_object() && !info["sha256"].as_string().empty()
                && info["size"].as_int(-1) == static_cast<long long>(file_size(m.converted));
            if (reuse)
            {
                std::cout << "  checking " << m.converted << "\n";
                reuse = sha256_file(m.converted) == info["sha256"].as_string();
                if (!reuse) std::cout << "  the SHA-256 of the converted file differs from its description\n";
            }
            if (!reuse)
            {
                if (!file_exists(m.source))
                    throw std::runtime_error(m.id + ": " + m.source + " is missing (cmake/builtin_models.cmake "
                        "downloads it)");
                std::cout << "  checking " << m.source << "\n";
                if (sha256_file(m.source) != m.source_sha)
                    throw std::runtime_error(m.id + ": the SHA-256 of " + m.source + " differs from the manifest");
                check_model(m.source, d, false);
                std::string sha;
                uint64_t size = 0;
                std::cout << "  converting to " << m.quantization << "\n";
                convert(m, threads, sha, size);
                info = check_model(m.converted, d, true);
                info["sha256"] = sha;
                info["size"] = static_cast<unsigned long long>(size);
                if (!write_file_atomic(side, info.dump_formatted(2) + "\n"))
                    throw std::runtime_error("cannot write " + side);
            }
            std::cout << "  " << file_name(m.converted) << ", " << info["size"].as_int(0) << " bytes, "
                      << info["format"].as_string() << "\n";

            // Le projecteur d'un modèle qui lit des images, livré tel qu'il est publié.
            json projector_entry;
            const json& pd = d["projector"];
            if (pd.is_object())
            {
                const std::string psha = pd["sha256"].as_string();
                m.projector = join_path(cache, psha + ".gguf");
                if (!file_exists(m.projector))
                    throw std::runtime_error(m.id + ": " + m.projector + " is missing (cmake/builtin_models.cmake "
                        "downloads it)");
                std::cout << "  checking " << m.projector << "\n";
                if (psha.empty() || sha256_file(m.projector) != psha)
                    throw std::runtime_error(m.id + ": the SHA-256 of " + m.projector + " differs from the manifest");
                const json pinfo = check_projector(m.projector, m.converted);
                std::cout << "  projector " << pinfo["projector_type"].as_string() << ", vision tokens of width "
                          << pinfo["projection_width"].as_int(0) << "\n";
                projector_entry = json::object();
                projector_entry["file"] = m.id + ".mmproj.gguf";
                projector_entry["size"] = static_cast<unsigned long long>(file_size(m.projector));
                projector_entry["sha256"] = psha;
                projector_entry["quantization"] = pd["quantization"];
                projector_entry["source_url"] = pd["url"];
            }

            json e = d;
            e.erase("from");
            e["quantization"] = m.quantization;
            e["arch"] = info["arch"];
            e["parameters"] = info["parameters"];
            e["context"] = info["context"];
            e["file"] = m.id + ".gguf";
            e["size"] = info["size"];
            e["sha256"] = info["sha256"];
            e["source_sha256"] = m.source_sha;
            e["source_url"] = d["from"]["url"];
            e.erase("projector");
            if (projector_entry.is_object()) e["projector"] = projector_entry;
            out["models"].push_back(e);
            models.push_back(m);
        }

        if (args.count("models-out"))
        {
            const std::string dir = args["models-out"];
            make_dirs(dir);
            for (const auto& m : models)
            {
                if (!link_or_copy(m.converted, join_path(dir, m.id + ".gguf"))
                    || (!m.projector.empty() && !link_or_copy(m.projector, join_path(dir, m.id + ".mmproj.gguf"))))
                    throw std::runtime_error("cannot place " + m.id + " in " + dir);
            }
            if (!write_file_atomic(join_path(dir, builtin_manifest_name), out.dump_formatted(2) + "\n"))
                throw std::runtime_error("cannot write the manifest in " + dir);
            std::cout << "models in " << dir << "\n";
        }
        if (args.count("setup"))
        {
            // La taille du plus gros installateur complet, inférieure aux 4 Gio que Windows exécute, ou
            // une autre pour les tests.
            const uint64_t limit = args.count("installer-limit")
                ? std::strtoull(args["installer-limit"].c_str(), nullptr, 10) : max_installer_size;
            pack_setup(args["setup"], args["setup-out"], models, out, std::min(limit, max_installer_size));
            std::cout << args["setup-out"] << ", " << file_size(args["setup-out"]) << " bytes\n";
        }
        return 0;
    }

}
}

int main(int argc, char** argv)
{
    try
    {
        return albireo::run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << "albireo-builtin: " << e.what() << "\n";
        return 1;
    }
}
