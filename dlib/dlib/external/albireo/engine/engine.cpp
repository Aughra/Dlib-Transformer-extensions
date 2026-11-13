// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "engine.h"
#include "metal_device.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <list>
#include <new>
#include <stdexcept>
#include <unordered_map>

#include "../common/engine_api.h"
#include "../common/system_info.h"
#include "../common/version.h"
#include "chat_format.h"
#include "cpu_runtime.h"
#include "image_input.h"
#include "sampler.h"
#include "thread_pool.h"
#include "vision_encoder.h"

#ifndef ALBIREO_ENGINE_VARIANT
#define ALBIREO_ENGINE_VARIANT "cpu"
#endif

namespace albireo
{
    typedef std::chrono::steady_clock clock_type;

    namespace
    {
        double seconds_since(clock_type::time_point a, clock_type::time_point b)
        {
            return std::chrono::duration<double>(b - a).count();
        }

        double now_seconds()
        {
            return std::chrono::duration<double>(clock_type::now().time_since_epoch()).count();
        }

        // Un entier d'une requête compris dans [lo, hi], lu avant toute réduction vers un type plus petit.
        long long bounded(const json& v, long long def, long long lo, long long hi)
        {
            const long long x = v.as_int(def);
            return x < lo ? lo : x > hi ? hi : x;
        }

        // Octets de texte qui se terminent sur un caractère UTF-8 complet ; une séquence incomplète
        // à la fin attend le token qui la complète.
        size_t complete_utf8(const std::string& s)
        {
            const size_t n = s.size();
            size_t i = n;
            // Revenir sur au plus trois octets de continuation jusqu'à l'octet de tête.
            size_t back = 0;
            while (i > 0 && back < 4 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
            {
                --i;
                ++back;
            }
            if (i == 0) return back == n ? n : 0;
            const unsigned char lead = static_cast<unsigned char>(s[i - 1]);
            size_t need = 1;
            if ((lead & 0xE0) == 0xC0) need = 2;
            else if ((lead & 0xF0) == 0xE0) need = 3;
            else if ((lead & 0xF8) == 0xF0) need = 4;
            else return n;   // ASCII ou un octet invalide : rien à attendre
            return back + 1 >= need ? n : i - 1;
        }

        // La forme base64 des octets d'un tableau de flottants, telle que voyagent les embeddings.
        std::string base64_floats(const std::vector<float>& v)
        {
            static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const size_t n = v.size() * sizeof(float);
            std::vector<unsigned char> bytes(n);
            if (n) std::memcpy(bytes.data(), v.data(), n);
            std::string out;
            out.reserve((n + 2) / 3 * 4);
            for (size_t i = 0; i < n; i += 3)
            {
                const unsigned b0 = bytes[i];
                const unsigned b1 = i + 1 < n ? bytes[i + 1] : 0;
                const unsigned b2 = i + 2 < n ? bytes[i + 2] : 0;
                out += table[b0 >> 2];
                out += table[((b0 & 3) << 4) | (b1 >> 4)];
                out += i + 1 < n ? table[((b1 & 15) << 2) | (b2 >> 6)] : '=';
                out += i + 2 < n ? table[b2 & 63] : '=';
            }
            return out;
        }

        long common_prefix(const std::vector<int>& a, const std::vector<int>& b)
        {
            const size_t n = std::min(a.size(), b.size());
            size_t i = 0;
            while (i < n && a[i] == b[i]) ++i;
            return static_cast<long>(i);
        }

        std::string type_name(dlib::ggml_type t)
        {
            switch (t)
            {
            case dlib::ggml_type::F32: return "F32";
            case dlib::ggml_type::F16: return "F16";
            case dlib::ggml_type::BF16: return "BF16";
            case dlib::ggml_type::Q4_0: return "Q4_0";
            case dlib::ggml_type::Q4_1: return "Q4_1";
            case dlib::ggml_type::Q5_0: return "Q5_0";
            case dlib::ggml_type::Q5_1: return "Q5_1";
            case dlib::ggml_type::Q8_0: return "Q8_0";
            case dlib::ggml_type::Q8_1: return "Q8_1";
            case dlib::ggml_type::Q2_K: return "Q2_K";
            case dlib::ggml_type::Q3_K: return "Q3_K";
            case dlib::ggml_type::Q4_K: return "Q4_K";
            case dlib::ggml_type::Q5_K: return "Q5_K";
            case dlib::ggml_type::Q6_K: return "Q6_K";
            case dlib::ggml_type::Q8_K: return "Q8_K";
            case dlib::ggml_type::IQ4_NL: return "IQ4_NL";
            }
            return "unknown";
        }

        /*
            Les images d'une requête, dans l'ordre de leurs parties : le contenu d'un message
            porte, à la place de chaque image, une marque faite de deux caractères à usage privé
            entourant l'indice de l'image, que le constructeur de prompt transforme en tokens de
            l'image.  Chaque chaîne de la requête qu'un format de chat peut rendre (contenus,
            raisonnement, noms, identifiants et arguments des appels d'outils, définitions
            d'outils) perd ces deux caractères au préalable, de sorte qu'aucun texte ne peut se
            faire passer pour une image.
        */
        struct image_part
        {
            std::string url;
            std::string detail;
        };

        const char* const mark_open = "\xEE\x80\x80";     // U+E000
        const char* const mark_close = "\xEE\x80\x81";    // U+E001

        std::string without_marks(const std::string& text)
        {
            if (text.find("\xEE\x80") == std::string::npos) return text;
            std::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text.compare(i, 3, mark_open) == 0 || text.compare(i, 3, mark_close) == 0)
                {
                    out += "\xEF\xBF\xBD";
                    i += 2;
                    continue;
                }
                out += text[i];
            }
            return out;
        }

        // Les chaînes d'une valeur JSON, clés de ses objets comprises, sans les marques d'images.
        void strip_marks(json& v)
        {
            if (v.is_string())
            {
                if (v.as_string().find("\xEE\x80") != std::string::npos) v = json(without_marks(v.as_string()));
            }
            else if (v.is_array())
            {
                for (auto& e : v.elements()) strip_marks(e);
            }
            else if (v.is_object())
            {
                for (auto& kv : v.items())
                {
                    kv.first = without_marks(kv.first);
                    strip_marks(kv.second);
                }
            }
        }

        /*
            Le texte du contenu d'un message : une chaîne, ou les parties d'une liste, les
            parties texte telles quelles et les parties image sous forme de marques quand
            images les reçoit ; un modèle qui ne lit que du texte (images nul) refuse les
            parties image et audio.
        */
        std::string content_text(const json& c, std::vector<image_part>* images)
        {
            if (c.is_string()) return images ? without_marks(c.as_string()) : c.as_string();
            std::string out;
            for (const auto& part : c.elements())
            {
                const std::string type = part["type"].as_string();
                if (part.is_string()) out += images ? without_marks(part.as_string()) : part.as_string();
                else if (type == "text")
                    out += images ? without_marks(part["text"].as_string()) : part["text"].as_string();
                else if (type == "image_url" && images)
                {
                    const json& u = part["image_url"];
                    image_part ip;
                    ip.url = u.is_string() ? u.as_string() : u["url"].as_string();
                    ip.detail = u.is_string() ? std::string() : u["detail"].as_string();
                    if (ip.url.empty()) throw std::invalid_argument("an image part has no url");
                    out += mark_open + std::to_string(images->size()) + mark_close;
                    images->push_back(ip);
                }
                else if (type == "image_url" || type == "input_audio")
                {
                    throw std::invalid_argument(type == "image_url"
                        ? "this model reads text only; images are not supported"
                        : "this model reads text and images only; audio is not supported");
                }
            }
            return out;
        }

        std::vector<chat_message> parse_messages(const json& list, std::vector<image_part>* images = nullptr)
        {
            if (!list.is_array() || list.empty()) throw std::invalid_argument("messages must be a non-empty array");
            // Les chaînes qu'un format rend en texte, sans les marques d'images pour un modèle qui les lit.
            auto text = [&](const std::string& s) { return images ? without_marks(s) : s; };
            std::vector<chat_message> out;
            for (const auto& m : list.elements())
            {
                chat_message c;
                c.role = m["role"].as_string();
                if (c.role == "developer") c.role = "system";
                if (c.role != "system" && c.role != "user" && c.role != "assistant" && c.role != "tool")
                    throw std::invalid_argument("unknown message role '" + c.role + "'");
                c.content = content_text(m["content"], images);
                c.reasoning = text(m.contains("reasoning_content") ? m["reasoning_content"].as_string()
                    : m["reasoning"].as_string());
                c.tool_call_id = text(m["tool_call_id"].as_string());
                c.name = text(m["name"].as_string());
                for (const auto& t : m["tool_calls"].elements())
                {
                    tool_call_data d;
                    d.id = text(t["id"].as_string());
                    const json& f = t["function"];
                    d.name = text(f["name"].as_string());
                    // Un objet est écrit comme l'écrivent les templates, avec une espace après chaque séparateur.
                    d.arguments = text(f["arguments"].is_string() ? f["arguments"].as_string()
                        : f["arguments"].is_null() ? std::string("{}") : f["arguments"].dump_formatted());
                    c.tool_calls.push_back(d);
                }
                out.push_back(c);
            }
            return out;
        }
    }

    struct cache_entry
    {
        int seq = -1;
        std::vector<int> tokens;    // les clés des tokens de la séquence (request_state::keys)
        uint64_t stamp = 0;
    };

    // L'encodage d'une image, conservé sous l'empreinte de ses pixels.
    struct image_encoding
    {
        std::array<uint8_t, 32> digest{};
        std::shared_ptr<const std::vector<float>> features;
    };

    // Une image d'une requête : sa taille en tokens fusionnés, ses pixels jusqu'à leur encodage, puis son encodage.
    struct request_image
    {
        std::array<uint8_t, 32> digest{};
        long grid_w = 0, grid_h = 0, tokens = 0;
        long start = 0;             // indice de son premier token dans le prompt
        vision_encoder::image_plan plan;
        rgb_image pixels;
        std::shared_ptr<const std::vector<float>> features;
    };

    struct loaded_model
    {
        std::string id;
        std::string path;
        std::shared_ptr<model_file> file;
        model_config cfg;
        dlib::hf_tokenizer tok;
        std::unique_ptr<chat_format> format;
        std::unique_ptr<model_runtime> runtime;
        long context = 0;
        long max_sequences = 8;
        long batch_tokens = 512;
        kv_format kv_type = kv_format::f16;
        std::vector<int> stops;
        std::vector<cache_entry> idle;
        std::vector<std::shared_ptr<request_state>> active;
        std::deque<std::shared_ptr<request_state>> waiting;
        std::atomic<bool> unloading{false};
        uint64_t stamp = 0;
        /*
            Les vitesses mesurées sur les réponses du modèle, en tokens par seconde : sa lecture
            des prompts qui ont calculé 64 tokens ou plus, et son écriture des réponses de 8
            tokens ou plus, chacune une moyenne exponentielle des dernières réponses (model_info).
        */
        std::atomic<double> prompt_rate{0.0};
        std::atomic<double> generation_rate{0.0};
        // Quand le modèle a calculé pour la dernière fois, et si la mémoire de ses blocs libres
        // est retournée au système depuis (engine::trim_idle).
        clock_type::time_point last_step;
        bool trimmed = true;
        // Les étapes exécutées jusqu'ici, qui désignent le prompt en lecture qui prend en premier le
        // budget d'une passe.
        size_t prefill_turn = 0;
        forward_batch batch;
        std::vector<float> logits;
        uint64_t parameters = 0;
        uint64_t bytes = 0;
        std::string quantization;
        // L'encodeur d'images d'un modèle qui lit des images, ses tokens et ses encodages.
        std::shared_ptr<model_file> projector;
        std::unique_ptr<vision_encoder> vision;
        int vision_start = -1, image_pad = -1, vision_end = -1;
        std::list<image_encoding> encodings;            // les plus récemment utilisés d'abord
        size_t encoding_bytes = 0;
        // Les pixels décodés que détiennent les images des requêtes actives et en attente, libérés à
        // mesure que chaque image est encodée ou que sa requête se termine ; submit refuse une
        // requête qui dépasserait le budget.
        std::atomic<size_t> queued_pixels{0};
        std::unique_ptr<vision_encoder::job> vision_job;
        uint64_t vision_owner = 0;                      // la requête dont l'image est en cours d'encodage
        size_t vision_image = 0;
        std::atomic<long> n_active{0};
        std::atomic<long> n_idle{0};
        std::atomic<uint64_t> generated{0};
        std::atomic<uint64_t> prompted{0};
        // Les tokens que lisent les préchauffages des historiques, une partie de prompted, et les
        // préchauffages actifs ou en attente, que n_active et les requêtes en attente des
        // statistiques laissent de côté.
        std::atomic<uint64_t> warmed{0};
        std::atomic<long> n_warming{0};
    };

    // Une requête d'un modèle d'embedding : un vecteur par entrée, chaque entrée calculée comme
    // une partie à part entière, un request_state qui détient la tâche.
    struct embedding_job
    {
        uint64_t id = 0;
        event_sink sink;
        std::vector<std::vector<float>> vectors;
        long remaining = 0;
        long tokens = 0;
        long dimensions = 0;    // 0 : la largeur du modèle
        std::atomic<bool> cancelled{false};
        bool ended = false;
        clock_type::time_point submitted, started;
    };

    struct request_state
    {
        enum class phase { waiting, prefill, decode, done };

        uint64_t id = 0;
        std::string model_id;
        event_sink sink;
        std::vector<int> prompt;
        /*
            keys contient le prompt avec les tokens de chaque image rendus distincts par
            l'empreinte de l'image, que compare la réutilisation des séquences en cache ;
            kv_tokens contient les clés des tokens présents dans les clés et valeurs de la
            séquence.
        */
        std::vector<int> keys;
        std::vector<int> kv_tokens;
        std::vector<request_image> images;
        // Positions rotatives du prompt, trois par token, pour les modèles à positions multimodales,
        // et la position du premier token après le prompt.
        std::vector<long> positions;
        long next_position = 0;
        int seq = -1;
        long cached = 0;
        std::unique_ptr<sampler> smp;
        std::unique_ptr<output_parser> parser;
        long max_tokens = 0;
        // Travail en arrière-plan (titres, mémoire, résumés) : ses réponses ne ralentissent jamais les
        // prompts des utilisateurs.
        bool background = false;
        std::vector<std::string> stops;
        std::string pending_bytes;
        std::string held;
        long generated = 0;
        int next_token = -1;
        int tool_calls = 0;
        std::atomic<bool> cancelled{false};
        phase ph = phase::waiting;
        clock_type::time_point submitted, started, prompt_end, finished;
        bool prompt_done = false;
        // La tâche d'embedding dont cette requête calcule une entrée, et l'indice de l'entrée.
        std::shared_ptr<embedding_job> job;
        long job_index = -1;
        /*
            Les messages et les options avec lesquels une requête de chat a été rendue, et la
            réponse et le raisonnement qu'elle a livrés : engine::warm_history() les rend à nouveau
            comme le fera la requête suivante de la conversation.  warm marque une requête qui lit
            un tel historique en avance sur cette requête, sans client : elle se termine une fois
            son prompt lu, ses clés et valeurs rejoignant les séquences en cache.
        */
        std::vector<chat_message> messages;
        render_options options;
        std::string answer, thought;
        bool warm = false;
        /*
            Le raisonnement de la réponse : les tokens qu'il peut prendre avant que le moteur n'y
            mette fin (-1 sans borne), les tokens qu'il a pris, les tokens de la fin que le moteur
            écrit à la place du modèle (closing), ceux de cette fin qui restent à écrire, un par
            étape (forced), et si le token en cours de traitement en fait partie (forcing), ce que
            le client ne reçoit pas comme raisonnement.  brief marque le raisonnement d'un modèle
            qui raisonne toujours, pour une requête qui n'en a pas demandé, auquel le moteur met
            fin aussitôt sauf si la requête donne un budget ; sa réponse reçoit alors
            answer_tokens tokens une fois le raisonnement terminé (-1 sans limite propre).
        */
        long reasoning_budget = -1;
        long reasoning_tokens = 0;
        std::vector<int> closing;
        std::deque<int> forced;
        bool forcing = false;
        bool brief = false;
        long answer_tokens = -1;
        /*
            La répétition de la partie en cours d'écriture, le raisonnement ou la réponse
            (loops()) : ses derniers tokens, le nombre de fois où chacune de leurs fenêtres a été
            écrite, si chacun des derniers tokens a terminé une fenêtre déjà écrite deux fois, et
            combien l'ont fait ; in_reasoning indique de quelle partie il s'agit.  repetition
            marque une réponse arrêtée parce qu'elle tournait en boucle.
        */
        std::deque<int> written;
        std::unordered_map<uint64_t, unsigned char> windows;
        std::deque<char> repeats;
        long repeated = 0;
        bool in_reasoning = false;
        bool repetition = false;

        bool is_cancelled() const { return cancelled || (job && job->cancelled); }
    };

    namespace
    {
        bool request_done(const std::shared_ptr<request_state>& r)
        {
            return r->ph == request_state::phase::done;
        }

        /*
            Les tokens du raisonnement court d'une requête qui demande un faible effort : une
            réponse à un utilisateur, et un travail en arrière-plan (titres, résumés, mémoire), qui
            attend moins.
        */
        const long low_effort_budget = 512;
        const long background_low_effort_budget = 256;

        /*
            L'échantillonnage que conseillent les auteurs d'une famille de modèles sur les fiches
            de leurs modèles, pour les requêtes qui n'en donnent pas, reasoning indiquant si le
            modèle raisonne avant sa réponse et images si la requête contient des images :
                - Qwen3-VL Thinking : température 1, top-p 0.95, top-k 20, et une pénalité de
                  présence de 1.5 sur toute la réponse pour une requête de texte seul ; le
                  raisonnement terminé aussitôt, le modèle répond comme un modèle sans
                  raisonnement, avec les valeurs que conseillent les auteurs de Qwen3 pour ce
                  mode, température 0.7, top-p 0.8 et top-k 20, et la pénalité de présence de 1.5
                  qu'ils donnent contre les répétitions ;
                - Qwen3-VL Instruct : température 0.7, top-p 0.8, top-k 20 et une pénalité de
                  présence de 1.5 sur toute la réponse avec images, température 1, top-p 1,
                  top-k 40 et une pénalité de présence de 2 pour du texte seul ;
                - CyberSecQwen-4B : température 0.3, celle de l'usage et des évaluations que
                  décrit sa fiche, top-p 0.8 et top-k 20 ;
                - Qwen3 : température 0.6, top-p 0.95, top-k 20 avec raisonnement, 0.7, 0.8 et
                  20 sans ;
                - Qwen2.5 : température 0.7, top-p 0.8, top-k 20 et une pénalité de répétition
                  de 1.05 ;
                - Foundation-Sec-8B-Reasoning et Foundation-Sec 1.1 8B Instruct : température
                  0.3, le réglage des évaluations de leurs auteurs, top-p 0.95 et top-k 20 ;
                - Granite 4.2 : température 1 et top-p 0.95 pour toutes les tâches, avec et sans
                  raisonnement, sans top-k ;
                - Ministral 3 : température 0.1 ; Devstral et les autres modèles Mistral sans
                  raisonnement : température 0.15 ; sans top-p ni top-k ;
                - les autres modèles : température 0.6, top-p 0.95 et top-k 20 quand ils
                  raisonnent, 0.7, 0.9 et 40 sinon.
            Aucun d'eux ne sélectionne par min-p.
        */
        sampling_params advised_sampling(const loaded_model& m, bool reasoning, bool images)
        {
            sampling_params p;
            p.min_p = 0.0f;
            const std::string& arch = m.cfg.arch;
            const std::string format = m.format->name();
            const bool chatml = format == "chatml";
            const bool qwen3vl = arch == "qwen3vl" || arch == "qwen3vlmoe";
            std::string name = m.cfg.name;
            for (char& c : name)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (chatml && qwen3vl && m.format->always_reasons() && reasoning)
            {
                p.temperature = 1.0f;
                p.top_p = 0.95f;
                p.top_k = 20;
                if (!images)
                {
                    p.presence_penalty = 1.5f;
                    p.repeat_last_n = -1;
                }
            }
            else if (chatml && qwen3vl && m.format->always_reasons())
            {
                p.temperature = 0.7f;
                p.top_p = 0.8f;
                p.top_k = 20;
                p.presence_penalty = 1.5f;
                p.repeat_last_n = -1;
            }
            else if (chatml && qwen3vl)
            {
                p.temperature = images ? 0.7f : 1.0f;
                p.top_p = images ? 0.8f : 1.0f;
                p.top_k = images ? 20 : 40;
                p.presence_penalty = images ? 1.5f : 2.0f;
                p.repeat_last_n = -1;
            }
            else if (chatml && name.find("cybersecqwen") != std::string::npos)
            {
                p.temperature = 0.3f;
                p.top_p = 0.8f;
                p.top_k = 20;
            }
            else if (chatml && (arch == "qwen3" || arch == "qwen3moe") && !reasoning)
            {
                p.temperature = 0.7f;
                p.top_p = 0.8f;
                p.top_k = 20;
            }
            else if (chatml && arch == "qwen2")
            {
                p.temperature = 0.7f;
                p.top_p = 0.8f;
                p.top_k = 20;
                p.repeat_penalty = 1.05f;
            }
            else if (format == "foundation-sec" || format == "role-tags")
            {
                p.temperature = 0.3f;
                p.top_p = 0.95f;
                p.top_k = 20;
            }
            else if (format == "granite4")
            {
                p.temperature = 1.0f;
                p.top_p = 0.95f;
                p.top_k = 0;
            }
            else if (format == "mistral" && !m.format->supports_reasoning())
            {
                p.temperature = name.find("ministral") != std::string::npos ? 0.1f : 0.15f;
                p.top_p = 1.0f;
                p.top_k = 0;
            }
            else if (reasoning)
            {
                p.temperature = 0.6f;
                p.top_p = 0.95f;
                p.top_k = 20;
            }
            else
            {
                p.temperature = 0.7f;
                p.top_p = 0.9f;
                p.top_k = 40;
            }
            return p;
        }

        // Les tokens d'une fenêtre dont la garde contre les boucles compte la répétition, et les
        // derniers tokens qu'elle lit.
        const size_t repeat_window = 16;
        const size_t repeat_span = 384;

        // Oublie les répétitions de la partie en cours d'écriture.
        void reset_loops(request_state& r)
        {
            r.written.clear();
            r.windows.clear();
            r.repeats.clear();
            r.repeated = 0;
        }

        /*
            Enregistre un token du raisonnement ou de la réponse, in_reasoning indiquant lequel,
            et indique si cette partie tourne en boucle : les neuf dixièmes de ses repeat_span
            derniers tokens terminent une fenêtre de repeat_window tokens que la partie a déjà
            écrite deux fois, ce que ne fait pas un raisonnement ou une réponse qui progresse.  Un
            passage réécrit une fois, un code donné avant et après sa correction, ne compte pas ;
            ni les fenêtres du prompt, de sorte qu'une réponse qui reprend un document ou un code
            qu'on lui a donné n'est pas concernée, ni celles du raisonnement pour la réponse, qui
            peut reprendre son brouillon.
        */
        bool loops(request_state& r, int token, bool in_reasoning)
        {
            if (in_reasoning != r.in_reasoning)
            {
                reset_loops(r);
                r.in_reasoning = in_reasoning;
            }
            r.written.push_back(token);
            if (r.written.size() > repeat_window) r.written.pop_front();
            if (r.written.size() < repeat_window) return false;
            // FNV-1a sur les identifiants des tokens de la fenêtre.
            uint64_t h = 14695981039346656037ull;
            for (int t : r.written) h = (h ^ static_cast<uint32_t>(t)) * 1099511628211ull;
            unsigned char& times = r.windows[h];
            if (times < 255) ++times;
            const char seen = times >= 3 ? 1 : 0;
            r.repeats.push_back(seen);
            r.repeated += seen;
            if (r.repeats.size() > repeat_span)
            {
                r.repeated -= r.repeats.front();
                r.repeats.pop_front();
            }
            return r.repeats.size() == repeat_span
                && static_cast<size_t>(r.repeated) * 10 >= static_cast<size_t>(repeat_span) * 9;
        }

        // Les images d'une requête, au plus.
        const size_t max_request_images = 16;
        // La mémoire que peuvent prendre les encodages des images qu'un modèle garde pour les
        // requêtes suivantes.
        const size_t encoding_budget = size_t(256) << 20;
        // Les tokens de prompts qu'une passe lit au plus tant que des réponses s'écrivent, dans
        // n'importe quel modèle.
        const long prefill_while_answering = 64;
        // Les pixels d'images décodés que les requêtes d'un modèle peuvent détenir à la fois, en
        // attente d'encodage.
        const size_t queued_pixels_budget = size_t(1) << 30;
        // Les tokens qu'un historique doit laisser à calculer pour qu'un préchauffage le lise en avance.
        const long min_warm_tokens = 16;

        // Libère les pixels décodés d'une image et les retire du budget de son modèle.
        void drop_pixels(loaded_model& m, request_image& im)
        {
            const size_t bytes = im.pixels.pixels.size();
            if (bytes == 0) return;
            m.queued_pixels.fetch_sub(bytes, std::memory_order_relaxed);
            im.pixels.clear();
        }

        // Libère les pixels que détient encore une requête qui se termine, pour que son budget
        // revienne au modèle.
        void drop_request_pixels(loaded_model& m, request_state& r)
        {
            for (request_image& im : r.images) drop_pixels(m, im);
        }

        uint64_t mix64(uint64_t z)
        {
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return z ^ (z >> 31);
        }

        /*
            La clé du token j d'une image dans le cache des prompts, une valeur négative distincte
            des identifiants du vocabulaire.  Les neuf premiers tokens d'une image portent les 256
            bits de l'empreinte de ses pixels, 31 à la fois, de sorte que deux images dont les
            clés concordent sur ces tokens contiennent les mêmes pixels ; les tokens suivants
            portent un hachage de l'empreinte entière et de leur indice.
        */
        int image_key(const std::array<uint8_t, 32>& d, long j)
        {
            uint32_t x = 0;
            if (j < 9)
            {
                const long end = std::min<long>(31 * j + 31, 256);
                for (long bit = 31 * j; bit < end; ++bit)
                    x = (x << 1) | ((d[static_cast<size_t>(bit / 8)] >> (7 - bit % 8)) & 1u);
            }
            else
            {
                uint64_t h = static_cast<uint64_t>(j) * 0x9e3779b97f4a7c15ull;
                for (size_t w = 0; w < 4; ++w)
                {
                    uint64_t v = 0;
                    for (size_t b = 0; b < 8; ++b) v = (v << 8) | d[8 * w + b];
                    h = mix64(h ^ v);
                }
                x = static_cast<uint32_t>(h >> 33);
            }
            return -1 - static_cast<int>(x & 0x7fffffffu);
        }

        // Le code et le message qui terminent une requête dont l'image n'a pas pu être encodée.
        void encoding_error(const std::exception& e, std::string& code, std::string& message)
        {
            if (dynamic_cast<const std::bad_alloc*>(&e))
            {
                code = "out_of_memory";
                message = "the system has no memory left to encode an image of this request";
            }
            else
            {
                code = "internal";
                message = std::string("the encoding of an image of this request failed: ") + e.what();
            }
        }

        /*
            Lit les images de parts pour l'encodeur de m : leurs octets, leur taille, la façon
            dont l'encodeur les lit avec le détail de la partie (vision_encoder::plan()) et, avec
            decode, leurs pixels à la taille du plan et l'empreinte de ces pixels, qui pour
            LLaVA-NeXT désigne aussi les tuiles par lesquelles l'image est lue.
        */
        void prepare_images(const loaded_model& m, const std::vector<image_part>& parts, bool decode,
            std::vector<request_image>& out)
        {
            if (parts.size() > max_request_images)
                throw std::invalid_argument("a request holds " + std::to_string(parts.size())
                    + " images; the engine reads at most " + std::to_string(max_request_images));
            const image_limits limits;
            for (const auto& part : parts)
            {
                const std::string bytes = decode_image_data_url(part.url, limits.max_bytes);
                const image_header header = read_image_header(bytes, limits);
                request_image im;
                im.plan = m.vision->plan(header.width, header.height, part.detail);
                im.grid_w = im.plan.grid_w;
                im.grid_h = im.plan.grid_h;
                im.tokens = im.plan.tokens;
                if (decode)
                {
                    im.pixels = decode_image(bytes, limits, im.plan.width, im.plan.height);
                    im.digest = image_digest(im.pixels);
                    if (m.vision->kind() == vision_encoder::family::llava_next)
                    {
                        std::array<uint8_t, 48> named{};
                        std::copy(im.digest.begin(), im.digest.end(), named.begin());
                        for (int b = 0; b < 8; ++b)
                        {
                            named[static_cast<size_t>(32 + b)] = static_cast<uint8_t>(im.plan.tiles_w >> (8 * b));
                            named[static_cast<size_t>(40 + b)] = static_cast<uint8_t>(im.plan.tiles_h >> (8 * b));
                        }
                        im.digest = sha256(named.data(), named.size());
                    }
                }
                out.push_back(std::move(im));
            }
        }

        /*
            Les messages d'un modèle LLaVA-NeXT placent leurs images avant leur texte, chacune
            suivie d'un saut de ligne, comme les écrit son template de chat.
        */
        void images_first(std::vector<chat_message>& messages)
        {
            for (auto& msg : messages)
            {
                if (msg.content.find(mark_open) == std::string::npos) continue;
                std::string marks, text;
                size_t at = 0;
                for (;;)
                {
                    const size_t open = msg.content.find(mark_open, at);
                    const size_t close = open == std::string::npos ? open : msg.content.find(mark_close, open + 3);
                    if (close == std::string::npos)
                    {
                        text += msg.content.substr(at);
                        break;
                    }
                    text += msg.content.substr(at, open - at);
                    marks += msg.content.substr(open, close + 3 - open) + "\n";
                    at = close + 3;
                }
                msg.content = marks + text;
            }
        }

        /*
            Les templates de chat des modèles Mistral trient par type les deux parties d'un
            message fait d'un texte et d'une image, ce qui place l'image en premier ; les autres
            messages gardent l'ordre de leurs parties.
        */
        void mistral_images(std::vector<chat_message>& messages)
        {
            for (auto& msg : messages)
            {
                const size_t open = msg.content.find(mark_open);
                if (open == std::string::npos || open == 0) continue;
                const size_t close = msg.content.find(mark_close, open + 3);
                if (close == std::string::npos || close + 3 != msg.content.size()) continue;
                if (msg.content.find(mark_open, close + 3) != std::string::npos) continue;
                msg.content = msg.content.substr(open) + msg.content.substr(0, open);
            }
        }

        /*
            Les tokens d'un prompt rendu.  Une marque d'image dans le texte d'un message devient
            le balisage <|vision_start|><|image_pad|><|vision_end|> (Qwen3-VL), <image>
            (LLaVA-NeXT) ou [IMG][IMG_END] (Pixtral), de sorte que le texte qui l'entoure est
            encodé à part, comme le tokenizer de référence coupe sur ces tokens ; le token
            d'image est ensuite répété pour chaque token de l'image, et images[i].start reçoit
            l'indice de son premier token.
        */
        std::vector<int> prompt_tokens(const loaded_model& m, const std::vector<prompt_piece>& pieces,
            std::vector<request_image>& images)
        {
            if (images.empty()) return chat_format::tokenize(m.tok, pieces);
            std::vector<prompt_piece> parts;
            std::vector<size_t> order;
            // Chaque image prend une seule place : une seconde marque d'une même image est refusée.
            std::vector<bool> placed(images.size(), false);
            for (const auto& p : pieces)
            {
                if (p.markup || p.text.find(mark_open) == std::string::npos)
                {
                    parts.push_back(p);
                    continue;
                }
                size_t at = 0;
                for (;;)
                {
                    const size_t open = p.text.find(mark_open, at);
                    const size_t close = open == std::string::npos ? open : p.text.find(mark_close, open + 3);
                    prompt_piece text;
                    if (close == std::string::npos)
                    {
                        text.text = p.text.substr(at);
                        if (!text.text.empty()) parts.push_back(text);
                        break;
                    }
                    text.text = p.text.substr(at, open - at);
                    if (!text.text.empty()) parts.push_back(text);
                    const std::string index = p.text.substr(open + 3, close - open - 3);
                    if (index.empty() || index.size() > 3 || index.find_first_not_of("0123456789") != std::string::npos)
                        throw std::invalid_argument("a mark of image names no image of the request");
                    const size_t i = std::stoul(index);
                    if (i >= images.size() || placed[i])
                        throw std::invalid_argument("a mark of image names no image of the request, or one already "
                            "placed");
                    placed[i] = true;
                    prompt_piece mark;
                    const vision_encoder::family kind = m.vision->kind();
                    mark.text = kind == vision_encoder::family::llava_next ? "<image>"
                        : kind == vision_encoder::family::pixtral ? "[IMG][IMG_END]"
                        : "<|vision_start|><|image_pad|><|vision_end|>";
                    mark.markup = true;
                    parts.push_back(mark);
                    order.push_back(i);
                    at = close + 3;
                }
            }
            if (order.size() != images.size())
                throw std::invalid_argument("the chat format of the model gives no place to an image of this request");
            const std::vector<int> ids = chat_format::tokenize(m.tok, parts);
            std::vector<int> out;
            out.reserve(ids.size());
            size_t k = 0;
            for (int id : ids)
            {
                if (id != m.image_pad)
                {
                    out.push_back(id);
                    continue;
                }
                if (k >= order.size()) throw std::invalid_argument("the prompt holds more image tokens than images");
                request_image& im = images[order[k++]];
                im.start = static_cast<long>(out.size());
                out.insert(out.end(), static_cast<size_t>(im.tokens), m.image_pad);
            }
            if (k != order.size()) throw std::invalid_argument("an image of the prompt lost its tokens");
            return out;
        }

        /*
            Les clés du prompt de r et, pour un modèle à positions rotatives multimodales, les
            positions de ses tokens (la règle de l'implémentation de référence de Qwen3-VL) : un
            token de texte prend la position suivante dans ses trois composantes ; le token à la
            ligne y et à la colonne x d'une image commençant à la position p prend
            (p, p + y, p + x), et le texte après l'image commence à p plus le plus grand côté de
            sa grille.
        */
        void finish_prompt(const loaded_model& m, request_state& r)
        {
            r.keys = r.prompt;
            const long n = static_cast<long>(r.prompt.size());
            for (const request_image& im : r.images)
            {
                // Les tokens de chaque image se trouvent dans le prompt, et la grille de ses positions
                // parmi eux.
                if (im.start < 0 || im.tokens <= 0 || im.tokens > n - im.start || im.grid_w < 0 || im.grid_h < 0
                    || im.grid_w * im.grid_h > im.tokens)
                    throw std::invalid_argument("an image of the request lies outside its prompt");
                for (long j = 0; j < im.tokens; ++j)
                    r.keys[static_cast<size_t>(im.start + j)] = image_key(im.digest, j);
            }
            r.positions.clear();
            if (!m.cfg.multimodal_rope()) return;
            r.positions.assign(static_cast<size_t>(3 * n), 0);
            std::vector<const request_image*> by_start;
            for (const request_image& im : r.images) by_start.push_back(&im);
            std::sort(by_start.begin(), by_start.end(),
                [](const request_image* a, const request_image* b) { return a->start < b->start; });
            long p = 0, t = 0;
            size_t next = 0;
            while (t < n)
            {
                if (next < by_start.size() && by_start[next]->start == t)
                {
                    const request_image& im = *by_start[next++];
                    for (long y = 0; y < im.grid_h; ++y)
                        for (long x = 0; x < im.grid_w; ++x)
                        {
                            long* q = &r.positions[static_cast<size_t>(3 * (t + y * im.grid_w + x))];
                            q[0] = p;
                            q[1] = p + y;
                            q[2] = p + x;
                        }
                    t += im.tokens;
                    p += std::max(im.grid_h, im.grid_w);
                    continue;
                }
                long* q = &r.positions[static_cast<size_t>(3 * t)];
                q[0] = q[1] = q[2] = p;
                ++p;
                ++t;
            }
            r.next_position = p;
        }

        // L'encodage de l'image d'empreinte d que garde m, rendu le plus récemment utilisé, ou nul.
        std::shared_ptr<const std::vector<float>> kept_encoding(loaded_model& m, const std::array<uint8_t, 32>& d)
        {
            for (auto it = m.encodings.begin(); it != m.encodings.end(); ++it)
                if (it->digest == d)
                {
                    m.encodings.splice(m.encodings.begin(), m.encodings, it);
                    return m.encodings.front().features;
                }
            return nullptr;
        }

        void keep_encoding(loaded_model& m, const std::array<uint8_t, 32>& d,
            const std::shared_ptr<const std::vector<float>>& features)
        {
            image_encoding e;
            e.digest = d;
            e.features = features;
            m.encodings.push_front(e);
            m.encoding_bytes += features->size() * sizeof(float);
            while (m.encoding_bytes > encoding_budget && m.encodings.size() > 1)
            {
                m.encoding_bytes -= m.encodings.back().features->size() * sizeof(float);
                m.encodings.pop_back();
            }
        }
    }

    engine::engine(const json& config, log_sink sink) : settings_(config), log_(std::move(sink))
    {
        const cpu_info cpu = read_cpu_info();
        threads_ = static_cast<int>(settings_["threads"].as_int(0));
        if (threads_ <= 0) threads_ = cpu.physical_cores;
        threads_ = std::max(1, std::min(threads_, cpu.logical_cores));
        pool_.reset(new thread_pool(threads_));
        // Une attente active d'un dixième de seconde au plus, car une plus longue occupe le processeur
        // entre les passes.
        pool_->set_spin_time(std::chrono::microseconds(bounded(settings_["spin_us"], 2000, 0, 100000)));
        warm_history_ = settings_["warm_history"].as_bool(true);
        log(ALBIREO_LOG_INFO, std::string("engine ") + ALBIREO_ENGINE_VARIANT + " " + ALBIREO_VERSION + ", "
            + std::to_string(threads_) + " threads, kernels " + qk_isa());
        if (settings_["metal"].as_bool(true))
        {
            std::string why;
            gpu_ = metal_device::open(why);
            if (gpu_)
                log(ALBIREO_LOG_INFO, "GPU " + gpu_->name() + " through Metal, " + std::to_string(gpu_->kernel_count())
                    + " kernels compiled, " + std::to_string(gpu_->working_set_bytes() >> 20) + " MB advised, buffers of "
                    + std::to_string(gpu_->max_buffer_bytes() >> 20) + " MB at most");
            else
            {
                log(ALBIREO_LOG_WARNING, "GPU: " + why);
                gpu_off_ = why;
            }
        }
        worker_ = std::thread([this]() { run(); });
    }

    engine::~engine()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            for (auto& r : requests_) r.second->cancelled = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        // Les requêtes que le worker n'a plus eu d'étape pour terminer.  Le destructeur ne lève
        // rien : un événement qui ne peut pas être écrit faute de mémoire est abandonné.
        try
        {
            std::vector<std::shared_ptr<request_state>> left;
            for (auto& r : requests_) left.push_back(r.second);
            for (auto& r : left)
                if (r->ph != request_state::phase::done) fail(*r, "cancelled", "the engine stopped");
            requests_.clear();
            std::vector<std::shared_ptr<embedding_job>> jobs;
            for (auto& j : jobs_) jobs.push_back(j.second);
            for (auto& j : jobs) end_job(*j, "cancelled", "the engine stopped");
        }
        catch (...)
        {
        }
        models_.clear();
    }

    void engine::log(int level, const std::string& message)
    {
        if (log_) log_(level, message);
    }

    json engine::describe()
    {
        const cpu_info cpu = read_cpu_info();
        const memory_info mem = read_memory_info();
        json d = json::object();
        d["variant"] = ALBIREO_ENGINE_VARIANT;
        d["version"] = ALBIREO_VERSION;
        d["backend"] = "processor";
        d["abi"] = static_cast<long long>(ALBIREO_ENGINE_ABI_VERSION);
#if   defined(__clang__)
        d["build"] = std::string("Clang ") + __clang_version__ + ", " + __DATE__;
#elif defined(__GNUC__)
        d["build"] = std::string("GCC ") + __VERSION__ + ", " + __DATE__;
#endif
        d["kernels"] = qk_isa();
        json dev = json::object();
        dev["type"] = "cpu";
        dev["name"] = cpu.brand;
        dev["cores"] = cpu.physical_cores;
        dev["threads"] = cpu.logical_cores;
        dev["memory"] = static_cast<unsigned long long>(mem.total);
        json features = json::array();
        for (const auto& f : cpu.features) features.push_back(f);
        dev["features"] = features;
        json devices = json::array();
        devices.push_back(dev);
        const json gpu = metal_describe();
        if (gpu.is_object()) devices.push_back(gpu);
        bool usable = true;
        std::string reason;
        // Les raisons portent un code, avec lequel l'application web les explique dans la
        // langue de l'utilisateur.
        std::string reason_code;
        json reason_args = json::object();
        d["devices"] = devices;
        d["usable"] = usable;
        if (!reason.empty()) d["reason"] = reason;
        if (!reason_code.empty())
        {
            d["reason_code"] = reason_code;
            if (reason_args.size() > 0) d["reason_args"] = reason_args;
        }
        return d;
    }

    // Modèles

    void engine::load_model(const std::string& id, const std::string& path, const json& options)
    {
        if (id.empty()) throw std::invalid_argument("a model needs an identifier");
        // Un chargement à la fois : sinon deux chargements du même identifiant remplaceraient
        // tous deux le modèle, les requêtes du premier ne se terminant jamais.
        std::lock_guard<std::mutex> serial(loading_);
        auto m = std::make_shared<loaded_model>();
        m->id = id;
        m->path = path;
        m->file = std::make_shared<model_file>(path);
        m->cfg = read_model_config(*m->file);
        m->tok.load_from_gguf(m->file->reader());
        if (static_cast<long>(m->tok.size()) != m->cfg.vocab)
            throw std::runtime_error("the vocabulary and the output layer of the model disagree");
        // Les tokens que le moteur ajoute ou sur lesquels il s'arrête doivent être des tokens du
        // vocabulaire.
        if (m->tok.bos_id() >= m->cfg.vocab || m->tok.eos_id() >= m->cfg.vocab)
            throw std::runtime_error("the tokenizer of the model names a special token outside its vocabulary");
        // Un fichier publié sans son template prend le template du dépôt de son modèle.
        if (m->cfg.chat_template.empty()) m->cfg.chat_template = published_template(m->cfg.name);
        m->format = chat_format::detect(m->tok, m->cfg.chat_template);

        // L'encodeur d'images : le fichier projecteur des options, ou les tenseurs de vision du
        // fichier du modèle.
        const std::string projector = options["projector"].as_string();
        if (!projector.empty())
        {
            m->projector = std::make_shared<model_file>(projector);
            if (!vision_encoder::present(*m->projector))
                throw std::invalid_argument("the projector file holds no image encoder");
        }
        else if (vision_encoder::present(*m->file))
        {
            m->projector = m->file;
        }
        if (m->projector)
        {
            m->vision.reset(new vision_encoder(m->projector, *pool_));
            // Qwen3-VL place les tokens d'une image par positions rotatives multimodales, un
            // décodeur LLaVA-NeXT ou Pixtral par leur ordre.
            const vision_encoder::family kind = m->vision->kind();
            const bool llava = kind == vision_encoder::family::llava_next;
            const bool pixtral = kind == vision_encoder::family::pixtral;
            if ((kind == vision_encoder::family::qwen3vl) != m->cfg.multimodal_rope())
                throw std::invalid_argument(m->cfg.multimodal_rope() || m->cfg.n_deepstack > 0
                    ? "the projector is not the image encoder of this model"
                    : "this model reads text only and takes no projector");
            if (m->vision->output_width() != m->cfg.input_width())
                throw std::invalid_argument("the projector gives " + std::to_string(m->vision->output_width())
                    + " values per image token and the model reads " + std::to_string(m->cfg.input_width())
                    + ": they do not go together");
            if (llava)
            {
                m->image_pad = m->tok.token_id("<image>");
                if (m->image_pad < 0)
                    throw std::invalid_argument("the vocabulary of the model lacks the token <image>");
            }
            else if (pixtral)
            {
                // [IMG] tient lieu de chaque token de l'image, [IMG_END] les suit.
                m->image_pad = m->tok.token_id("[IMG]");
                m->vision_end = m->tok.token_id("[IMG_END]");
                if (m->image_pad < 0 || m->vision_end < 0)
                    throw std::invalid_argument("the vocabulary of the model lacks the tokens [IMG] and [IMG_END]");
            }
            else
            {
                m->vision_start = m->tok.token_id("<|vision_start|>");
                m->image_pad = m->tok.token_id("<|image_pad|>");
                m->vision_end = m->tok.token_id("<|vision_end|>");
                if (m->vision_start < 0 || m->image_pad < 0 || m->vision_end < 0)
                    throw std::invalid_argument("the vocabulary of the model lacks the tokens of the images");
            }
        }

        // Un modèle d'embedding lit des passages et des questions de quelques centaines de
        // tokens, beaucoup à la fois, et n'en garde rien : ses valeurs par défaut lui sont
        // propres, et ses clés et valeurs viennent par petits blocs, rendus dès que chaque
        // entrée est lue.
        const bool embedding = m->cfg.is_embedding();
        if (embedding && m->cfg.pooling != 3)
            throw std::runtime_error("this embedding model pools its hidden states by a method the engine does not "
                "implement (the engine takes the state of the last token)");
        /*
            Les réglages du modèle, ses options ou celles du moteur, dans des bornes lues avant
            toute réduction (long fait 32 bits sous Windows), qui sont un contexte de 2^26
            positions, 256 séquences, des passes de 65536 tokens et 2^24 Mo de clés et valeurs.
        */
        auto setting = [&](const char* key, long long def, long long lo, long long hi) {
            const json& v = options.contains(key) ? options[key] : settings_[key];
            if (v.is_null()) return def;
            if (!v.is_number()) throw std::invalid_argument(std::string("the setting ") + key + " must be a number");
            const double x = v.as_double();
            if (!(x >= static_cast<double>(lo) && x <= static_cast<double>(hi)))
                throw std::invalid_argument(std::string("the setting ") + key + " must lie between "
                    + std::to_string(lo) + " and " + std::to_string(hi));
            return v.as_int(def);
        };
        long ctx = embedding && !options.contains("context") ? 0
            : static_cast<long>(setting("context", 0, 0, 1 << 26));
        const bool chosen_context = ctx > 0;
        /*
            Par défaut, le contexte pour lequel le modèle a été entraîné, que la mémoire du
            système peut réduire plus bas ; un modèle d'embedding lit des entrées de 8192 tokens
            au plus.
        */
        if (ctx <= 0 && m->cfg.ctx_train > 0)
            ctx = embedding ? std::min<long>(m->cfg.ctx_train, 8192) : m->cfg.ctx_train;
        if (ctx <= 0) ctx = 4096;
        const long limit = m->cfg.ctx_train > 0 ? std::max<long>(m->cfg.ctx_train, 256) : ctx;
        m->context = std::max<long>(256, std::min<long>(ctx, limit));
        m->max_sequences = embedding && !options.contains("max_sequences") ? 8
            : std::max<long>(1, static_cast<long>(setting("max_sequences", 8, 0, 256)));
        // Un modèle à état récurrent (qwen35) garde au plus 64 états de séquences (cpu_runtime::max_state_slots) :
        // 16 requêtes ensemble au plus, et au plus 32 séquences en cache (finish()).
        if (m->cfg.recurrent()) m->max_sequences = std::min<long>(m->max_sequences, 16);
        m->batch_tokens = embedding && !options.contains("batch_tokens") ? 256
            : std::max<long>(16, static_cast<long>(setting("batch_tokens", 512, 0, 65536)));

        /*
            Le stockage des clés et valeurs (kv_format) : auto prend les entiers sur 8 bits pour
            un modèle de langage dont les têtes en contiennent des groupes entiers, et la
            demi-précision pour un modèle d'embedding, dont les vecteurs gardent alors les
            valeurs de son implémentation de référence.
        */
        const json none;
        const json& kv_type_value = options.contains("kv_type") ? options["kv_type"]
            : embedding ? none : settings_["kv_type"];
        const std::string kv_type = kv_type_value.is_string() ? kv_type_value.as_string() : "auto";
        if ((!kv_type_value.is_null() && !kv_type_value.is_string())
            || (kv_type != "auto" && kv_type != "q8" && kv_type != "f16"))
            throw std::invalid_argument("the setting kv_type must be auto, q8 or f16");
        const bool whole_groups = m->cfg.head_dim % kv_q8_group == 0;
        m->kv_type = whole_groups && (kv_type == "q8" || (kv_type == "auto" && !embedding)) ? kv_format::q8
            : kv_format::f16;
        if (kv_type == "q8" && !whole_groups)
            log(ALBIREO_LOG_WARNING, "model '" + id + "': heads of " + std::to_string(m->cfg.head_dim) + " values "
                "hold no whole groups of 8-bit keys and values, which stay in half precision");

        // Seules les couches d'attention gardent des clés et valeurs (les couches linéaires de qwen35 ont un état
        // de taille fixe par séquence).
        const size_t per_token = 2 * static_cast<size_t>(m->cfg.n_kv_layers()) * static_cast<size_t>(m->cfg.n_kv_heads)
            * kv_row_bytes(m->kv_type, m->cfg.head_dim);
        const memory_info mem = read_memory_info();
        /*
            La mémoire que les clés et valeurs laissent au système et aux autres programmes, ainsi
            qu'aux poids du modèle, qu'une génération lit à chaque token et qui doivent rester en
            mémoire physique.
        */
        const uint64_t weights = static_cast<uint64_t>(m->file->size())
            + (m->projector && m->projector != m->file ? static_cast<uint64_t>(m->projector->size()) : 0);
        const uint64_t reserve = std::max<uint64_t>(uint64_t(1) << 30, mem.total / 8) + weights;
        // La mémoire que peuvent prendre les clés et valeurs, quand le système l'indique.
        const bool known = mem.commit_total > 0;
        const uint64_t room_bytes = mem.commit_total > reserve ? mem.commit_total - reserve : 0;
        // Les octets des clés et valeurs de positions, en blocs entiers.
        const long block_positions = embedding ? 64 : 256;
        auto bytes_for = [&](long positions) {
            const long blocks = (positions + block_positions - 1) / block_positions;
            return per_token * static_cast<size_t>(blocks * block_positions);
        };
        size_t kv = (embedding && !options.contains("kv_mb") ? size_t(0)
            : static_cast<size_t>(setting("kv_mb", 0, 0, 1 << 24))) << 20;
        if (kv == 0 && embedding)
        {
            // Une entrée de la taille du contexte entier.
            kv = bytes_for(m->context);
        }
        else if (kv == 0)
        {
            /*
                Deux contextes complets, dans la limite de la mémoire que le système peut
                engager, sa mémoire physique et son fichier d'échange ou swap ensemble, moins la
                réserve.  Les blocs sont alloués à mesure que les séquences grandissent, et la
                garde ci-dessous refuse un nouveau bloc quand la mémoire qui reste au système se
                réduit à la réserve : ce budget est une borne que seules atteignent des séquences
                de cette longueur, le fichier d'échange ou le swap contenant alors ce que la
                mémoire physique ne contient pas.  Le contexte du modèle se réduit à ce que cette
                mémoire peut contenir, sauf si un réglage l'a choisi.
            */
            kv = 2 * bytes_for(m->context);
            if (known) kv = std::min<size_t>(kv, static_cast<size_t>(room_bytes));
            if (chosen_context) kv = std::max<size_t>(kv, bytes_for(m->context));
            kv = std::max<size_t>(kv, bytes_for(std::min<long>(m->context, 4096)));
            if (chosen_context && known && bytes_for(m->context) > room_bytes)
                log(ALBIREO_LOG_WARNING, "model '" + id + "': the keys and values of a context of "
                    + std::to_string(m->context) + " tokens take " + std::to_string(bytes_for(m->context) >> 20)
                    + " MB, beyond the " + std::to_string(room_bytes >> 20) + " MB the system can commit for them");
        }
        std::unique_ptr<cpu_runtime> runtime(new cpu_runtime(m->file, m->cfg, *pool_, kv, block_positions,
            m->kv_type));
        /*
            Un nouveau bloc de clés et valeurs laisse la réserve au système, au-delà des blocs des
            4096 premières positions, que le modèle a toujours.
        */
        const size_t floor_bytes = bytes_for(std::min<long>(m->context, 4096));
        runtime->set_kv_guard([reserve, floor_bytes](size_t bytes, size_t allocated) {
            if (allocated + bytes <= floor_bytes) return true;
            const memory_info now = read_memory_info();
            return now.commit_available == 0 || now.commit_available >= bytes + reserve;
        });
        // Les étapes de génération sur le GPU du Mac, quand le moteur l'a ouvert.
        if (gpu_)
        {
            // Les prompts que lit le GPU (réglages gpu_prompt_tokens et gpu_prompt_context).
            runtime->set_gpu_prompts(bounded(settings_["gpu_prompt_tokens"], qk_float_product_tokens, 9, 1 << 20),
                bounded(settings_["gpu_prompt_context"], 262144, 0, 1 << 20));
            runtime->set_gpu(*gpu_, [this, id](int level, const std::string& message) {
                log(level, "model '" + id + "': " + message);
            });
            // La vérification du GPU au chargement, pour que la première réponse ne l'attende pas.
            runtime->check_gpu();
        }
        cpu_runtime* const processor = runtime.get();
        m->runtime = std::move(runtime);

        // Le contexte ne dépasse jamais ce que les clés et valeurs peuvent contenir : un prompt
        // que le moteur accepte peut alors toujours être lu jusqu'au bout, et un plus long est
        // refusé aussitôt.
        const long room = static_cast<long>(m->runtime->kv_bytes_budget() / m->runtime->kv_bytes_per_token());
        if (room < m->context)
        {
            log(ALBIREO_LOG_INFO, "model '" + id + "': context reduced from " + std::to_string(m->context) + " to "
                + std::to_string(room) + " tokens, the positions its keys and values can hold in the memory given "
                "to them");
            m->context = room;
        }
        // Les diviseurs rotatifs d'un modèle avec LongRoPE suivent le contexte de ses séquences.
        processor->set_context(m->context);
        select_rope_factors(m->cfg, m->context);

        if (m->tok.eos_id() >= 0) m->stops.push_back(m->tok.eos_id());
        for (int s : m->format->stop_tokens()) m->stops.push_back(s);
        std::sort(m->stops.begin(), m->stops.end());
        m->stops.erase(std::unique(m->stops.begin(), m->stops.end()), m->stops.end());

        // Taille et type de stockage dominant des poids.
        std::map<std::string, uint64_t> by_type;
        for (const auto& t : m->file->reader().tensors())
        {
            m->parameters += t.n_elements();
            by_type[type_name(t.type)] += t.nbytes;
        }
        m->bytes = m->file->size();
        uint64_t most = 0;
        for (const auto& kvp : by_type)
            if (kvp.second > most) { most = kvp.second; m->quantization = kvp.first; }

        if (settings_["warm_up"].as_bool(true)) m->file->warm_up(true);

        const std::string use = embedding ? "vectors of " + std::to_string(m->cfg.d_model) + " dimensions"
            : std::string("chat format ") + m->format->name();
        if (m->vision) log(ALBIREO_LOG_INFO, "model '" + id + "': images read by " + m->vision->description());
        log(ALBIREO_LOG_INFO, "model '" + id + "': " + m->runtime->description() + ", " + use
            + ", context " + std::to_string(m->context) + ", keys and values in "
            + (m->kv_type == kv_format::q8 ? "8 bits" : "half precision") + " up to "
            + std::to_string(m->runtime->kv_bytes_budget() >> 20) + " MB"
            + (m->tok.exact_pretokenizer() ? "" : ", pre-tokenizer '" + m->tok.pretokenizer() + "' approximated"));

        unload_model(id);
        std::lock_guard<std::mutex> lock(mutex_);
        models_[id] = m;
    }

    void engine::unload_model(const std::string& id)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = models_.find(id);
        if (it == models_.end()) return;
        it->second->unloading = true;
        wake_.notify_all();
        unloaded_.wait(lock, [&]() { return models_.find(id) == models_.end() || stop_; });
    }

    json engine::model_info(const loaded_model& m) const
    {
        json j = json::object();
        j["id"] = m.id;
        j["name"] = m.cfg.name;
        j["arch"] = m.cfg.arch;
        j["path"] = m.path;
        j["context"] = m.context;
        j["trained_context"] = m.cfg.ctx_train;
        j["format"] = m.format->name();
        j["tools"] = !m.cfg.is_embedding() && m.format->supports_tools();
        j["reasoning"] = !m.cfg.is_embedding() && m.format->supports_reasoning();
        // Un modèle qui raisonne avant chaque réponse, le raisonnement d'une requête qui n'en
        // demande pas étant court.
        j["reasoning_always"] = !m.cfg.is_embedding() && m.format->always_reasons();
        j["parameters"] = static_cast<unsigned long long>(m.parameters);
        j["quantization"] = m.quantization;
        j["bytes"] = static_cast<unsigned long long>(m.bytes);
        j["kv_bytes_per_token"] = static_cast<unsigned long long>(m.runtime->kv_bytes_per_token());
        j["kv_type"] = m.kv_type == kv_format::q8 ? "q8" : "f16";
        // Les vitesses mesurées, 0 tant que le modèle n'a pas répondu (loaded_model::prompt_rate).
        j["prompt_tokens_per_second"] = std::round(10.0 * m.prompt_rate.load(std::memory_order_relaxed)) / 10.0;
        j["generation_tokens_per_second"] = std::round(10.0 * m.generation_rate.load(std::memory_order_relaxed)) / 10.0;
        j["experts"] = m.cfg.n_experts;
        j["embedding"] = m.cfg.is_embedding();
        if (m.cfg.is_embedding()) j["dimensions"] = m.cfg.d_model;
        j["description"] = albireo::describe(m.cfg);
        json inputs = json::array();
        inputs.push_back("text");
        if (m.vision)
        {
            inputs.push_back("image");
            json t = json::object();
            for (const char* level : { "min", "low", "auto", "high" }) t[level] = m.vision->most_tokens(level);
            j["image_tokens"] = t;
            j["images_per_request"] = static_cast<long>(max_request_images);
            j["projector_bytes"] = static_cast<unsigned long long>(m.projector == m.file ? 0 : m.projector->size());
        }
        j["inputs"] = inputs;
        return j;
    }

    // Requêtes

    uint64_t engine::submit(const json& req, event_sink sink)
    {
        const std::string model_id = req["model"].as_string();
        std::shared_ptr<loaded_model> m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = models_.find(model_id);
            if (it == models_.end() || it->second->unloading)
                throw std::invalid_argument("the model '" + model_id + "' is not loaded");
            m = it->second;
        }
        if (req.contains("input"))
        {
            if (!m->cfg.is_embedding())
                throw std::invalid_argument("the model '" + model_id + "' writes text; vectors come from an embedding "
                    "model");
            return submit_embeddings(m, req, std::move(sink));
        }
        if (m->cfg.is_embedding())
            throw std::invalid_argument("the model '" + model_id + "' computes vectors: its requests give their texts "
                "in \"input\"");

        auto r = std::make_shared<request_state>();
        r->model_id = model_id;
        r->sink = std::move(sink);
        r->submitted = clock_type::now();

        render_options opt;
        current_dates(opt);
        const std::string effort = req["reasoning_effort"].as_string();
        bool reasoning = m->format->supports_reasoning();
        if (req.contains("reasoning")) reasoning = req["reasoning"].as_bool(reasoning);
        if (!effort.empty()) reasoning = effort != "none" && effort != "minimal";
        if (req["chat_template_kwargs"].contains("enable_thinking"))
            reasoning = req["chat_template_kwargs"]["enable_thinking"].as_bool(reasoning);
        opt.reasoning = reasoning && m->format->supports_reasoning();
        /*
            Le budget du raisonnement : les tokens qu'il peut prendre avant que le moteur n'y mette
            fin.  Le template d'un modèle qui raisonne toujours ouvre son raisonnement quoi que
            demande la requête : pour une requête qui ne demande pas de raisonnement, le moteur y
            met fin aussitôt avec la phrase que ses auteurs donnent pour un budget, de sorte que
            le modèle répond sans raisonnement et sans prompt hors de ce qu'il a appris, les
            tokens de cette fin ne prenant rien à ceux de la réponse ; une requête qui demande un
            faible effort reçoit un raisonnement court, dans la limite de son max_tokens.  Un
            budget donné à null n'est pas un budget.
        */
        const bool budget_given = req["reasoning_budget"].is_number();
        long budget = budget_given ? static_cast<long>(bounded(req["reasoning_budget"], -1, -1, 1 << 24)) : -1;
        bool brief = false;
        if (m->format->always_reasons() && !opt.reasoning)
        {
            opt.reasoning = true;
            brief = true;
        }
        if (!budget_given && brief) budget = 0;
        else if (!budget_given && effort == "low")
            budget = req["priority"].as_string() == "background" ? background_low_effort_budget : low_effort_budget;
        if (!opt.reasoning) budget = -1;
        if (req["tool_choice"].as_string() != "none" && req["tools"].is_array() && !req["tools"].empty())
        {
            if (!m->format->supports_tools()) throw std::invalid_argument("this model does not support tools");
            opt.tools = req["tools"];
            if (m->vision) strip_marks(opt.tools);
        }

        if (req.contains("messages"))
        {
            std::vector<image_part> parts;
            std::vector<chat_message> messages = parse_messages(req["messages"], m->vision ? &parts : nullptr);
            if (!parts.empty() && m->vision->kind() == vision_encoder::family::llava_next) images_first(messages);
            if (!parts.empty() && m->vision->kind() == vision_encoder::family::pixtral) mistral_images(messages);
            std::vector<prompt_piece> pieces;
            m->format->render(messages, opt, pieces);
            if (!parts.empty()) prepare_images(*m, parts, true, r->images);
            r->prompt = prompt_tokens(*m, pieces, r->images);
            r->parser = m->format->make_parser(opt);
            // La conversation d'un client, dont la fin de sa réponse lit l'historique en avance.
            if (warm_history_ && req["priority"].as_string() != "background")
            {
                r->messages.swap(messages);
                r->options = opt;
            }
        }
        else if (req["prompt"].is_string())
        {
            r->prompt = m->tok.encode(req["prompt"].as_string(), m->tok.add_bos_default(), false, true);
            // Une complétion brute est du texte brut : pas de marqueurs à interpréter.
            struct passthrough : output_parser
            {
                void feed(const std::string& t, std::vector<output_event>& e) override
                {
                    output_event ev;
                    ev.text = t;
                    e.push_back(ev);
                }
                void finish(std::vector<output_event>&) override {}
            };
            r->parser.reset(new passthrough());
        }
        else
        {
            throw std::invalid_argument("a request needs messages or a prompt");
        }
        if (r->prompt.empty()) throw std::invalid_argument("the prompt is empty");
        finish_prompt(*m, *r);
        if (static_cast<long>(r->prompt.size()) >= m->context)
            throw std::invalid_argument("the prompt holds " + std::to_string(r->prompt.size())
                + " tokens, beyond the context of " + std::to_string(m->context) + " tokens of the model");

        /*
            Échantillonnage : les valeurs de la requête, ou celles que conseillent les auteurs du
            modèle pour une réponse avec ou sans raisonnement du modèle, un raisonnement terminé
            aussitôt comptant comme aucun.
        */
        const sampling_params advised = advised_sampling(*m, opt.reasoning && budget != 0, !r->images.empty());
        sampling_params sp;
        sp.temperature = static_cast<float>(req["temperature"].as_double(advised.temperature));
        sp.top_p = static_cast<float>(req["top_p"].as_double(advised.top_p));
        // Les candidats sont sélectionnés dans un tas : plus de 1024 coûteraient du temps à chaque
        // token pour une masse que la température et top-p laissent de côté.
        sp.top_k = static_cast<int>(bounded(req["top_k"], advised.top_k, 0, 1024));
        sp.min_p = static_cast<float>(req["min_p"].as_double(advised.min_p));
        const double repeat_penalty = req["repeat_penalty"].as_double(req["repetition_penalty"].as_double(
            advised.repeat_penalty));
        sp.repeat_penalty = static_cast<float>(repeat_penalty);
        sp.repeat_last_n = static_cast<int>(bounded(req["repeat_last_n"], advised.repeat_last_n, -1, 1 << 20));
        const double presence = req["presence_penalty"].as_double(advised.presence_penalty);
        const double frequency = req["frequency_penalty"].as_double(0.0);
        sp.presence_penalty = static_cast<float>(presence);
        sp.frequency_penalty = static_cast<float>(frequency);
        sp.seed = static_cast<uint64_t>(req["seed"].as_int(0));
        for (const auto& b : req["logit_bias"].items())
        {
            char* end = nullptr;
            const long long token = std::strtoll(b.first.c_str(), &end, 10);
            if (end == b.first.c_str() || *end != '\0' || token < 0 || token >= m->cfg.vocab)
                throw std::invalid_argument("logit_bias holds an invalid token identifier");
            // La plage de l'interface OpenAI, de -100 (interdit) à 100 (forcé).
            const double bias = b.second.as_double(0.0);
            if (!b.second.is_number() || !(bias >= -100.0 && bias <= 100.0))
                throw std::invalid_argument("logit_bias values must be numbers between -100 and 100");
            sp.logit_bias[static_cast<int>(token)] = static_cast<float>(bias);
        }
        /*
            Les nombres JSON peuvent être infinis, donc chaque paramètre est vérifié par rapport
            à une plage finie, ce qui garde les logits finis.
        */
        if (!(sp.temperature >= 0 && sp.temperature <= 1e4) || !(sp.top_p >= 0 && sp.top_p <= 1)
            || !(sp.min_p >= 0 && sp.min_p <= 1) || !(repeat_penalty > 0 && repeat_penalty <= 100)
            || !(presence >= -100 && presence <= 100) || !(frequency >= -100 && frequency <= 100))
            throw std::invalid_argument("sampling parameters out of range");
        r->smp.reset(new sampler(sp));

        const long room = m->context - static_cast<long>(r->prompt.size());
        // Comparé avant toute réduction : long fait 32 bits sous Windows.
        const long long asked = req["max_completion_tokens"].as_int(req["max_tokens"].as_int(0));
        r->max_tokens = asked <= 0 || asked > room ? room : static_cast<long>(asked);
        r->background = req["priority"].as_string() == "background";
        /*
            La fin d'un raisonnement que le moteur peut écrire : au budget, et à la place d'un
            raisonnement qui tourne en rond (loops()).
        */
        r->brief = brief;
        if (opt.reasoning && req.contains("messages"))
        {
            r->closing = chat_format::tokenize(m->tok, m->format->reasoning_close());
            if (!r->closing.empty())
            {
                r->reasoning_budget = budget;
                // Le raisonnement court d'une requête qui n'en a pas demandé s'ajoute à sa réponse.
                if (brief && asked > 0 && budget >= 0)
                {
                    r->max_tokens = static_cast<long>(std::min<long long>(room,
                        static_cast<long long>(r->max_tokens) + budget + static_cast<long long>(r->closing.size())));
                    r->answer_tokens = static_cast<long>(std::min<long long>(asked, room));
                }
                // Un budget de 0 termine aussitôt le raisonnement qu'ouvre le prompt.
                if (budget == 0 && r->parser->in_reasoning())
                {
                    r->forced.assign(r->closing.begin(), r->closing.end());
                    r->closing.clear();
                }
            }
        }

        if (req["stop"].is_string() && !req["stop"].as_string().empty()) r->stops.push_back(req["stop"].as_string());
        for (const auto& s : req["stop"].elements())
            if (s.is_string() && !s.as_string().empty()) r->stops.push_back(s.as_string());
        if (r->stops.size() > 16) throw std::invalid_argument("at most 16 stop sequences");
        // Chaque token compare la fin de la réponse avec les séquences d'arrêt, dont la longueur est
        // bornée pour cela.
        for (const auto& s : r->stops)
            if (s.size() > 1024) throw std::invalid_argument("a stop sequence holds 1024 bytes at most");
        // Les marqueurs d'un format écrits en texte brut terminent aussi le tour du modèle.
        if (req.contains("messages"))
            for (const auto& s : m->format->stop_strings())
                if (std::find(r->stops.begin(), r->stops.end(), s) == r->stops.end()) r->stops.push_back(s);

        // Les pixels décodés que détient cette requête, décomptés du budget de son modèle ci-dessous.
        size_t pixels = 0;
        for (const request_image& im : r->images) pixels += im.pixels.pixels.size();

        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) throw std::runtime_error("the engine is stopping");
        // Le modèle a pu être déchargé pendant la préparation du prompt : sa file n'est plus
        // lue.
        if (m->unloading) throw std::invalid_argument("the model '" + model_id + "' is not loaded");
        /*
            Les images des requêtes en file gardent leurs pixels jusqu'à leur encodage.  Une
            requête qui porterait le modèle au-delà de son budget est refusée, de sorte qu'une
            rafale de grandes images ne peut pas épuiser la mémoire de l'hôte.
        */
        if (pixels > 0 && m->queued_pixels.load(std::memory_order_relaxed) + pixels > queued_pixels_budget)
            throw engine_busy("the images already queued for the model '" + model_id
                + "' hold the memory kept for them; submit this request again shortly");
        m->queued_pixels.fetch_add(pixels, std::memory_order_relaxed);
        r->id = next_id_++;
        requests_[r->id] = r;
        m->waiting.push_back(r);
        ++total_requests_;
        wake_.notify_all();
        return r->id;
    }

    uint64_t engine::submit_embeddings(const std::shared_ptr<loaded_model>& m, const json& req, event_sink sink)
    {
        // Les entrées : un texte, une liste de textes, une liste d'identifiants de tokens, ou une
        // liste de telles listes.  Un texte reçoit les tokens que le modèle ajoute autour de ses
        // entrées (le token final dont l'état devient le vecteur, pour les décodeurs) ; les
        // listes de tokens sont prises telles quelles.
        std::vector<std::vector<int>> inputs;
        const json& in = req["input"];
        auto text_tokens = [&](const std::string& text) {
            return m->tok.encode(text, m->tok.add_bos_default(), m->tok.add_eos_default(), false);
        };
        auto token_list = [&](const json& list) {
            std::vector<int> ids;
            for (const auto& t : list.elements())
            {
                if (!t.is_integer() || t.as_int() < 0 || t.as_int() >= m->cfg.vocab)
                    throw std::invalid_argument("input holds an invalid token identifier");
                ids.push_back(static_cast<int>(t.as_int()));
            }
            return ids;
        };
        if (in.is_string()) inputs.push_back(text_tokens(in.as_string()));
        else if (in.is_array() && !in.empty() && in[0].is_integer()) inputs.push_back(token_list(in));
        else if (in.is_array())
        {
            for (const auto& e : in.elements())
            {
                if (e.is_string()) inputs.push_back(text_tokens(e.as_string()));
                else if (e.is_array()) inputs.push_back(token_list(e));
                else throw std::invalid_argument("input must hold texts or lists of token identifiers");
            }
        }
        if (inputs.empty()) throw std::invalid_argument("input is empty");
        if (inputs.size() > 2048) throw std::invalid_argument("at most 2048 inputs per request");
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (inputs[i].empty()) throw std::invalid_argument("input " + std::to_string(i) + " is empty");
            if (static_cast<long>(inputs[i].size()) > m->context)
                throw std::invalid_argument("input " + std::to_string(i) + " holds " + std::to_string(inputs[i].size())
                    + " tokens, beyond the " + std::to_string(m->context) + " tokens the model reads at once");
        }
        const long long dimensions = req["dimensions"].as_int(0);
        if (dimensions < 0 || dimensions > m->cfg.d_model)
            throw std::invalid_argument("dimensions must lie between 1 and " + std::to_string(m->cfg.d_model));

        auto job = std::make_shared<embedding_job>();
        job->sink = std::move(sink);
        job->vectors.resize(inputs.size());
        job->remaining = static_cast<long>(inputs.size());
        job->dimensions = static_cast<long>(dimensions);
        job->submitted = clock_type::now();
        std::vector<std::shared_ptr<request_state>> parts;
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            auto r = std::make_shared<request_state>();
            r->model_id = m->id;
            r->job = job;
            r->job_index = static_cast<long>(i);
            r->submitted = job->submitted;
            job->tokens += static_cast<long>(inputs[i].size());
            r->prompt.swap(inputs[i]);
            r->keys = r->prompt;
            parts.push_back(r);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) throw std::runtime_error("the engine is stopping");
        if (m->unloading) throw std::invalid_argument("the model '" + m->id + "' is not loaded");
        job->id = next_id_++;
        jobs_[job->id] = job;
        for (auto& r : parts)
        {
            r->id = next_id_++;
            m->waiting.push_back(r);
        }
        ++total_requests_;
        wake_.notify_all();
        return job->id;
    }

    void engine::complete_part(loaded_model& m, request_state& r, const float* hidden)
    {
        // L'état du dernier token, réduit aux dimensions demandées (les premières, pour les
        // modèles entraînés à garder leur sens une fois raccourcis), de longueur unité.
        embedding_job& job = *r.job;
        const long n = job.dimensions > 0 ? job.dimensions : m.cfg.d_model;
        std::vector<float> v(hidden, hidden + n);
        double norm = 0;
        for (float x : v) norm += static_cast<double>(x) * x;
        const float scale = norm > 0 ? static_cast<float>(1.0 / std::sqrt(norm)) : 0.0f;
        for (float& x : v) x *= scale;
        job.vectors[static_cast<size_t>(r.job_index)].swap(v);
        r.ph = request_state::phase::done;
        r.finished = clock_type::now();
        if (r.seq >= 0) m.runtime->release_sequence(r.seq);
        r.seq = -1;
        if (job.started.time_since_epoch().count() == 0 || r.started < job.started) job.started = r.started;
        if (--job.remaining > 0 || job.ended) return;

        job.ended = true;
        json data = json::array();
        for (size_t i = 0; i < job.vectors.size(); ++i)
        {
            json e = json::object();
            e["index"] = static_cast<long long>(i);
            e["embedding"] = base64_floats(job.vectors[i]);
            data.push_back(e);
        }
        json done = json::object();
        done["data"] = data;
        done["dimensions"] = n;
        json usage = json::object();
        usage["prompt_tokens"] = job.tokens;
        usage["total_tokens"] = job.tokens;
        done["usage"] = usage;
        json timings = json::object();
        const clock_type::time_point end = clock_type::now();
        const double compute = seconds_since(job.started, end);
        timings["queued_ms"] = 1000.0 * seconds_since(job.submitted, job.started);
        timings["prompt_ms"] = 1000.0 * compute;
        timings["prompt_per_second"] = compute > 0 ? job.tokens / compute : 0.0;
        done["timings"] = timings;
        job.sink(ALBIREO_EVENT_DONE, done.dump());
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.erase(job.id);
    }

    void engine::end_job(embedding_job& job, const std::string& code, const std::string& message)
    {
        // Le premier échec ou la première annulation d'une entrée termine toute la requête ; ses
        // autres entrées voient l'annulation et s'arrêtent.
        job.cancelled = true;
        if (job.ended) return;
        job.ended = true;
        json e = json::object();
        e["code"] = code;
        e["message"] = message;
        if (job.sink) job.sink(ALBIREO_EVENT_ERROR, e.dump());
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.erase(job.id);
    }

    void engine::cancel_job(embedding_job& job)
    {
        // Les parties restantes de la requête voient l'annulation et s'arrêtent sans événement.
        job.cancelled = true;
        if (job.ended) return;
        job.ended = true;
        json done = json::object();
        done["finish_reason"] = "cancelled";
        json usage = json::object();
        usage["prompt_tokens"] = job.tokens;
        usage["total_tokens"] = job.tokens;
        done["usage"] = usage;
        if (job.sink) job.sink(ALBIREO_EVENT_DONE, done.dump());
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.erase(job.id);
    }

    void engine::cancel(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = requests_.find(id);
        if (it != requests_.end()) it->second->cancelled = true;
        auto j = jobs_.find(id);
        if (j != jobs_.end()) j->second->cancelled = true;
        wake_.notify_all();
    }

    // Ordonnanceur

    void engine::run()
    {
        for (;;)
        {
            std::vector<std::shared_ptr<loaded_model>> models;
            // Indique si un modèle de langage a des requêtes : les modèles d'embedding calculent
            // alors des passes courtes, les réponses étant ce qu'attendent les utilisateurs.
            bool answering = false;
            std::vector<bool> busy;
            bool woken = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // Sans requêtes, le thread se réveille tout de même de temps en temps pour rendre la
                // mémoire des modèles inactifs (trim_idle).
                woken = wake_.wait_for(lock, std::chrono::seconds(5), [&]() {
                    if (stop_) return true;
                    for (const auto& m : models_)
                        if (m.second->unloading || !m.second->waiting.empty() || !m.second->active.empty()) return true;
                    return false;
                });
                if (stop_) break;
                for (const auto& m : models_)
                {
                    models.push_back(m.second);
                    const bool has_requests = !m.second->waiting.empty() || !m.second->active.empty();
                    busy.push_back(has_requests);
                    // Les préchauffages des historiques laissent aux modèles d'embedding leurs passes entières.
                    bool clients = false;
                    for (const auto& r : m.second->waiting) clients = clients || !r->warm;
                    for (const auto& r : m.second->active) clients = clients || !r->warm;
                    if (!m.second->cfg.is_embedding() && clients) answering = true;
                }
            }
            if (!woken)
            {
                trim_idle(models);
                continue;
            }
            /*
                Indique si une réponse à un utilisateur s'écrit dans un modèle quelconque : les
                passes lisent alors peu de tokens de prompts.  Les réponses du travail en
                arrière-plan laissent aux prompts leur budget entier.
            */
            bool decoding = false;
            for (const auto& m : models)
                for (const auto& r : m->active)
                    if (r->ph == request_state::phase::decode && !r->background) decoding = true;
            // Le périphérique garde les copies des modèles qui ont des requêtes, et peut leur
            // donner la place des autres (tensor_backend.h).
            for (size_t i = 0; i < models.size(); ++i) models[i]->runtime->set_busy(busy[i]);
            for (const auto& m : models)
            {
                if (m->unloading)
                {
                    // Terminer ce que le modèle détient encore, puis le libérer.
                    std::deque<std::shared_ptr<request_state>> waiting;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        waiting.swap(m->waiting);
                    }
                    for (auto& r : waiting)
                    {
                        try
                        {
                            fail(*r, "unloaded", "the model was unloaded");
                        }
                        catch (...)
                        {
                            // Mémoire épuisée pendant l'écriture de l'événement : la requête se termine sans lui.
                        }
                    }
                    abandon(*m, "unloaded", "the model was unloaded");
                    std::lock_guard<std::mutex> lock(mutex_);
                    models_.erase(m->id);
                    unloaded_.notify_all();
                    continue;
                }
                try
                {
                    const bool short_pass = m->cfg.is_embedding() && answering;
                    step(m, short_pass ? std::min<long>(m->batch_tokens, 16) : m->batch_tokens, decoding);
                    // Un modèle d'embedding ne garde rien entre ses requêtes : une fois inactif, la
                    // mémoire des entrées qu'il a lues retourne au système.
                    if (m->cfg.is_embedding() && m->active.empty())
                    {
                        bool idle = false;
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            idle = m->waiting.empty();
                        }
                        if (idle) m->runtime->trim_memory();
                    }
                }
                catch (const std::bad_alloc&)
                {
                    /*
                        Le système a manqué de mémoire pendant la passe.  Ses requêtes se terminent
                        avec le code d'une mémoire de clés et valeurs pleine, et les blocs libres
                        retournent au système, pour que les requêtes suivantes trouvent de la place.
                    */
                    abandon(*m, "out_of_memory", "the system has no memory left for this request");
                    trim_after_failure(*m);
                }
                catch (const std::exception& e)
                {
                    try
                    {
                        log(ALBIREO_LOG_ERROR, "model '" + m->id + "': " + e.what());
                    }
                    catch (...)
                    {
                    }
                    abandon(*m, "internal", e.what());
                }
                catch (...)
                {
                    abandon(*m, "internal", "unknown error");
                }
            }
            // Les modèles sans requêtes depuis un moment rendent leurs blocs libres, quoi que calculent
            // les autres.
            trim_idle(models);
        }
    }

    void engine::trim_after_failure(loaded_model& m)
    {
        try
        {
            m.runtime->trim_memory();
        }
        catch (...)
        {
        }
    }

    void engine::abandon(loaded_model& m, const std::string& code, const std::string& message)
    {
        /*
            Termine sur une erreur chaque requête que calcule le modèle et rend leurs
            séquences.  L'écriture d'un événement peut manquer de mémoire : cette requête se
            termine alors sans son événement, et les autres se terminent quand même.
        */
        for (auto& r : m.active)
        {
            try
            {
                if (r->ph != request_state::phase::done) fail(*r, code, message);
            }
            catch (...)
            {
                r->ph = request_state::phase::done;
            }
            try
            {
                if (r->seq >= 0) m.runtime->release_sequence(r->seq);
            }
            catch (...)
            {
            }
            r->seq = -1;
        }
        m.active.clear();
    }

    void engine::trim_idle(const std::vector<std::shared_ptr<loaded_model>>& models)
    {
        // Les blocs libres d'un modèle de langage sans requêtes depuis un moment retournent au
        // système ; ses conversations en cache restent.
        const auto now = clock_type::now();
        for (const auto& m : models)
        {
            if (m->trimmed || m->unloading || !m->active.empty()) continue;
            if (seconds_since(m->last_step, now) < 30) continue;
            m->runtime->trim_memory();
            m->trimmed = true;
        }
    }

    void engine::drop_cancelled(loaded_model& m)
    {
        // Les requêtes annulées quittent la file aussitôt, qu'une place soit libre ou non.
        std::vector<std::shared_ptr<request_state>> dropped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = m.waiting.begin(); it != m.waiting.end();)
            {
                if ((*it)->is_cancelled())
                {
                    dropped.push_back(*it);
                    it = m.waiting.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        for (auto& r : dropped) finish(m, *r, "cancelled");
    }

    bool engine::preempt(loaded_model& m, const request_state* keep)
    {
        /*
            Donne la mémoire d'une requête qui lit son prompt aux réponses en cours d'écriture,
            la dernière requête admise d'abord : elle retourne en tête de file
            (requeue_preempted) et relit son prompt une fois qu'il y a de la place.  Un
            préchauffage d'historique donne sa mémoire en premier : il se termine, ses clés et
            valeurs rejoignant les séquences en cache, que reserve() rend.
        */
        for (const auto& w : m.active)
            if (w.get() != keep && w->warm && w->ph == request_state::phase::prefill)
            {
                finish(m, *w, "replaced");
                return true;
            }
        for (auto it = m.active.rbegin(); it != m.active.rend(); ++it)
        {
            request_state& r = **it;
            if (&r == keep || r.ph != request_state::phase::prefill || r.job || r.seq < 0) continue;
            if (m.runtime->sequence_length(r.seq) == 0) continue;
            m.runtime->release_sequence(r.seq);
            r.seq = -1;
            r.kv_tokens.clear();
            r.cached = 0;
            r.ph = request_state::phase::waiting;
            log(ALBIREO_LOG_DEBUG, "model '" + m.id + "': a prompt being read gave its memory to the answers being "
                "written, and will be read again");
            return true;
        }
        return false;
    }

    void engine::requeue_preempted(loaded_model& m)
    {
        std::vector<std::shared_ptr<request_state>> back;
        for (const auto& r : m.active)
            if (r->ph == request_state::phase::waiting) back.push_back(r);
        if (back.empty()) return;
        // De retour dans la file avant de quitter les requêtes actives, pour qu'un échec de
        // l'insertion les laisse là où abandon() les trouve.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            m.waiting.insert(m.waiting.begin(), back.begin(), back.end());
        }
        m.active.erase(std::remove_if(m.active.begin(), m.active.end(),
            [](const std::shared_ptr<request_state>& r) { return r->ph == request_state::phase::waiting; }),
            m.active.end());
    }

    void engine::admit(loaded_model& m)
    {
        yield_warm_ups(m);
        for (;;)
        {
            std::shared_ptr<request_state> r;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (m.waiting.empty() || static_cast<long>(m.active.size()) >= m.max_sequences) return;
                // De la place pour la requête parmi les actives avant qu'elle quitte la file, pour
                // qu'une exception à partir d'ici la termine avec les autres (abandon).
                m.active.reserve(m.active.size() + 1);
                // Les requêtes des clients dans leur ordre, les préchauffages des historiques après elles.
                auto next = m.waiting.begin();
                while (next != m.waiting.end() && (*next)->warm) ++next;
                if (next == m.waiting.end()) next = m.waiting.begin();
                r = *next;
                m.waiting.erase(next);
            }
            m.active.push_back(r);
            if (r->is_cancelled())
            {
                finish(m, *r, "cancelled");
                m.active.pop_back();
                continue;
            }
            r->started = clock_type::now();
            if (!assign_sequence(m, *r))
            {
                fail(*r, "out_of_memory", "no memory left for the keys and values of this request");
                m.active.pop_back();
                continue;
            }
            r->ph = request_state::phase::prefill;
            // Un préchauffage dont la réponse a quitté entre-temps les séquences en cache lirait tout son
            // historique : il se termine.
            if (r->warm && static_cast<long>(r->prompt.size()) - r->cached > 2 * r->max_tokens)
            {
                finish(m, *r, "replaced");
                m.active.pop_back();
                continue;
            }
            if (r->cached > 0)
            {
                json p = json::object();
                p["prompt_done"] = r->cached;
                p["prompt_total"] = static_cast<long>(r->prompt.size());
                p["cached"] = r->cached;
                r->sink(ALBIREO_EVENT_PROGRESS, p.dump());
            }
        }
    }

    bool engine::assign_sequence(loaded_model& m, request_state& r)
    {
        // La séquence en cache qui partage le plus long préfixe avec le prompt, un token de prompt
        // au moins restant à calculer : ses logits commencent la réponse.
        const long limit = static_cast<long>(r.prompt.size()) - 1;
        long best_len = 0;
        size_t best = m.idle.size();
        for (size_t i = 0; i < m.idle.size(); ++i)
        {
            // Un modèle à état récurrent ne reprend une séquence qu'à sa longueur ou à un point de reprise
            // (runtime::resume_point()).
            const long n = m.runtime->resume_point(m.idle[i].seq, std::min(limit, common_prefix(m.idle[i].tokens, r.keys)));
            if (n > best_len)
            {
                best_len = n;
                best = i;
            }
        }
        if (best < m.idle.size())
        {
            cache_entry& e = m.idle[best];
            const bool take = best_len * 2 >= static_cast<long>(e.tokens.size());
            if (take)
            {
                // Le reste de la séquence en cache ne sert plus : la reprendre.
                r.seq = e.seq;
                m.runtime->truncate_sequence(r.seq, best_len);
                m.idle.erase(m.idle.begin() + static_cast<std::ptrdiff_t>(best));
            }
            else
            {
                // Une autre conversation peut encore en avoir besoin : copier le préfixe partagé.
                const int s = m.runtime->create_sequence();
                bool copied = m.runtime->copy_sequence(e.seq, s, best_len);
                while (!copied && m.idle.size() > 1)
                {
                    // Libérer l'autre séquence la moins récemment utilisée et réessayer.
                    size_t lru = m.idle.size();
                    for (size_t i = 0; i < m.idle.size(); ++i)
                        if (i != best && (lru == m.idle.size() || m.idle[i].stamp < m.idle[lru].stamp)) lru = i;
                    m.runtime->release_sequence(m.idle[lru].seq);
                    m.idle.erase(m.idle.begin() + static_cast<std::ptrdiff_t>(lru));
                    if (lru < best) --best;
                    copied = m.runtime->copy_sequence(m.idle[best].seq, s, best_len);
                }
                if (!copied)
                {
                    m.runtime->release_sequence(s);
                    r.seq = m.idle[best].seq;
                    m.runtime->truncate_sequence(r.seq, best_len);
                    m.idle.erase(m.idle.begin() + static_cast<std::ptrdiff_t>(best));
                }
                else
                {
                    r.seq = s;
                    m.idle[best].stamp = ++m.stamp;
                }
            }
            r.cached = best_len;
        }
        else
        {
            r.seq = m.runtime->create_sequence();
            r.cached = 0;
        }
        r.kv_tokens.assign(r.keys.begin(), r.keys.begin() + r.cached);
        return true;
    }

    bool engine::reserve(loaded_model& m, int seq, long length)
    {
        while (!m.runtime->reserve_sequence(seq, length))
        {
            if (m.idle.empty()) return false;
            size_t lru = 0;
            for (size_t i = 1; i < m.idle.size(); ++i)
                if (m.idle[i].stamp < m.idle[lru].stamp) lru = i;
            m.runtime->release_sequence(m.idle[lru].seq);
            m.idle.erase(m.idle.begin() + static_cast<std::ptrdiff_t>(lru));
        }
        return true;
    }

    void engine::advance_encoding(loaded_model& m)
    {
        // La requête dont l'image est en cours d'encodage : l'encodage s'arrête quand elle est partie.
        request_state* owner = nullptr;
        bool answering = false;
        for (const auto& rp : m.active)
        {
            if (rp->id == m.vision_owner && rp->ph == request_state::phase::prefill) owner = rp.get();
            if (rp->ph == request_state::phase::decode) answering = true;
        }
        if (!owner || m.vision_image >= owner->images.size())
        {
            m.vision_job.reset();
            m.vision->release_work();
            return;
        }
        const request_image& image = owner->images[m.vision_image];
        const std::array<uint8_t, 32> digest = image.digest;
        try
        {
            // Une tranche courte tant que des réponses s'écrivent, pour que leurs tokens continuent
            // d'arriver.
            if (!m.vision->advance(*m.vision_job, answering ? 0.1 : 0.5)) return;
            const std::shared_ptr<std::vector<float>> out = std::make_shared<std::vector<float>>();
            out->swap(m.vision_job->output());
            const std::shared_ptr<const std::vector<float>> features = out;
            log(ALBIREO_LOG_DEBUG, "model '" + m.id + "': image of " + std::to_string(image.grid_w) + "x"
                + std::to_string(image.grid_h) + " tokens encoded in "
                + std::to_string(static_cast<long>(1000 * m.vision_job->seconds())) + " ms");
            keep_encoding(m, digest, features);
            for (const auto& rp : m.active)
                for (request_image& im : rp->images)
                    if (!im.features && im.digest == digest)
                    {
                        im.features = features;
                        drop_pixels(m, im);
                    }
        }
        catch (const std::exception& e)
        {
            /*
                Les requêtes qui attendent cette image se terminent avec l'erreur de son encodage,
                avant que cette passe soit construite ; les autres requêtes du modèle continuent.
            */
            std::string code, message;
            encoding_error(e, code, message);
            m.vision_job.reset();
            m.vision->release_work();
            try
            {
                log(ALBIREO_LOG_WARNING, "model '" + m.id + "': " + message);
            }
            catch (...)
            {
            }
            for (const auto& rp : m.active)
            {
                if (rp->ph == request_state::phase::done) continue;
                for (const request_image& im : rp->images)
                    if (!im.features && im.digest == digest)
                    {
                        fail(*rp, code, message);
                        break;
                    }
            }
            if (code == "out_of_memory") trim_after_failure(m);
            return;
        }
        m.vision_job.reset();
        m.vision->release_work();
    }

    bool engine::step(const std::shared_ptr<loaded_model>& mp, long budget_tokens, bool answering)
    {
        loaded_model& m = *mp;
        for (auto& r : m.active)
            if (r->is_cancelled() && r->ph != request_state::phase::done) finish(m, *r, "cancelled");
        m.active.erase(std::remove_if(m.active.begin(), m.active.end(), request_done), m.active.end());
        drop_cancelled(m);
        admit(m);
        if (m.active.empty())
        {
            m.n_active = 0;
            m.n_warming = 0;
            return false;
        }
        if (m.vision_job) advance_encoding(m);

        // Le token suivant de chaque requête en cours de génération, puis des tranches des prompts.
        struct row { request_state* r; long count; bool logits; };
        std::vector<row> rows;
        forward_batch& b = m.batch;
        b.clear();
        b.hidden_only = m.cfg.is_embedding();
        const bool multimodal = m.cfg.multimodal_rope();
        long budget = budget_tokens;
        for (auto& rp : m.active)
        {
            request_state& r = *rp;
            if (r.ph != request_state::phase::decode) continue;
            const long len = m.runtime->sequence_length(r.seq);
            if (len + 1 > m.context)
            {
                finish(m, r, "length");
                continue;
            }
            bool reserved = reserve(m, r.seq, len + 1);
            while (!reserved && preempt(m)) reserved = reserve(m, r.seq, len + 1);
            if (!reserved)
            {
                // Les réponses en cours d'écriture occupent toute la mémoire des clés et valeurs.
                fail(r, "out_of_memory", "no memory left for the keys and values of this answer");
                continue;
            }
            b.segments.push_back({ r.seq, static_cast<long>(b.tokens.size()), 1 });
            b.logit_rows.push_back(static_cast<long>(b.tokens.size()));
            b.tokens.push_back(r.next_token);
            if (multimodal)
            {
                // Un token généré suit la dernière position du prompt.
                const long p = r.next_position + len - static_cast<long>(r.prompt.size());
                b.positions.insert(b.positions.end(), { p, p, p });
            }
            rows.push_back({ &r, 1, true });
            --budget;
        }
        requeue_preempted(m);
        // Tant que des réponses aux utilisateurs s'écrivent, dans ce modèle ou un autre, une passe lit
        // peu de tokens de prompts.
        bool users = answering;
        for (const row& w : rows) users = users || !w.r->background;
        if (users) budget = std::min(budget, prefill_while_answering);

        // Les tokens du prompt de r que la passe peut lire, jusqu'à most : aucun tant qu'il attend une
        // image.
        auto slice = [&](request_state& r, long most) -> long {
            const long done = static_cast<long>(r.kv_tokens.size());
            long n = std::min<long>(static_cast<long>(r.prompt.size()) - done, most);
            // La tranche s'arrête avant la première image qu'elle lit qui n'est pas encore encodée.
            for (size_t i = 0; i < r.images.size(); ++i)
            {
                request_image& im = r.images[i];
                if (im.features || im.start + im.tokens <= done || im.start >= done + n) continue;
                im.features = kept_encoding(m, im.digest);
                if (im.features)
                {
                    drop_pixels(m, im);
                    continue;
                }
                if (!m.vision_job)
                {
                    try
                    {
                        m.vision_job = m.vision->start(im.pixels, im.plan);
                    }
                    catch (const std::exception& e)
                    {
                        // Cette requête se termine avec l'erreur de son image ; les autres continuent.
                        std::string code, message;
                        encoding_error(e, code, message);
                        m.vision->release_work();
                        fail(r, code, message);
                        if (code == "out_of_memory") trim_after_failure(m);
                        return 0L;
                    }
                    m.vision_owner = r.id;
                    m.vision_image = i;
                }
                n = std::min<long>(n, std::max<long>(0, im.start - done));
            }
            return n;
        };
        // Ajoute à la passe n tokens du prompt de r.
        auto read = [&](request_state& r, long n) {
            const long done = static_cast<long>(r.kv_tokens.size());
            // Un préchauffage n'a pas besoin de logits : il se termine avec son prompt.
            const bool last = done + n == static_cast<long>(r.prompt.size()) && !r.warm;
            const long row0 = static_cast<long>(b.tokens.size());
            b.segments.push_back({ r.seq, row0, n });
            if (last) b.logit_rows.push_back(row0 + n - 1);
            b.tokens.insert(b.tokens.end(), r.prompt.begin() + done, r.prompt.begin() + done + n);
            if (multimodal)
                b.positions.insert(b.positions.end(), r.positions.begin() + 3 * done,
                    r.positions.begin() + 3 * (done + n));
            // Les tokens des images lisent leurs encodages : une ligne de input_width() valeurs chacun.
            for (const request_image& im : r.images)
            {
                const long from = std::max(im.start, done), to = std::min(im.start + im.tokens, done + n);
                if (from >= to) continue;
                const size_t width = static_cast<size_t>(m.cfg.input_width());
                for (long t = from; t < to; ++t)
                {
                    b.embedding_rows.push_back(row0 + t - done);
                    const float* row = im.features->data() + static_cast<size_t>(t - im.start) * width;
                    b.embeddings.insert(b.embeddings.end(), row, row + width);
                }
            }
            rows.push_back({ &r, n, last });
            budget -= n;
        };

        /*
            Les prompts en cours de lecture prennent le budget à tour de rôle, le premier changeant
            à chaque étape, de sorte qu'un prompt court n'attend pas la totalité d'un long.  Un
            prompt sans place attend tant qu'une autre requête calcule dans la passe.  Quand
            aucune ne le fait, le prompt admis en premier parmi ceux sans place prend la mémoire
            des prompts admis après lui, ou se termine si cela ne suffit pas, de sorte que deux
            prompts ne s'attendent jamais l'un l'autre.  Les préchauffages des historiques lisent
            avec le budget que laissent les prompts des requêtes, et pas pendant qu'une réponse à
            un utilisateur s'écrit ; un préchauffage sans place se termine.
        */
        std::vector<request_state*> reading;    // dans l'ordre d'admission, les préchauffages en dernier
        for (auto& rp : m.active)
            if (rp->ph == request_state::phase::prefill && !rp->warm) reading.push_back(rp.get());
        const size_t requests = reading.size();
        if (!users)
            for (auto& rp : m.active)
                if (rp->ph == request_state::phase::prefill && rp->warm) reading.push_back(rp.get());
        const size_t first = requests == 0 ? 0 : m.prefill_turn++ % requests;
        request_state* blocked = nullptr;
        size_t blocked_at = 0;
        long blocked_n = 0;
        for (size_t k = 0; k < reading.size() && budget > 0; ++k)
        {
            const size_t at = k < requests ? (first + k) % requests : k;
            request_state& r = *reading[at];
            if (r.ph != request_state::phase::prefill) continue;
            const long n = slice(r, budget);
            if (n <= 0) continue;
            if (!reserve(m, r.seq, static_cast<long>(r.kv_tokens.size()) + n))
            {
                if (r.warm)
                {
                    finish(m, r, "replaced");
                    continue;
                }
                if (!blocked || at < blocked_at)
                {
                    blocked = &r;
                    blocked_at = at;
                    blocked_n = n;
                }
                continue;
            }
            read(r, n);
        }
        if (rows.empty() && blocked)
        {
            request_state& r = *blocked;
            const long end = static_cast<long>(r.kv_tokens.size()) + blocked_n;
            bool reserved = false;
            while (!reserved && preempt(m, &r)) reserved = reserve(m, r.seq, end);
            if (reserved) read(r, blocked_n);
            else fail(r, "out_of_memory", "the keys and values of this prompt do not fit in memory");
        }
        requeue_preempted(m);
        if (rows.empty())
        {
            m.active.erase(std::remove_if(m.active.begin(), m.active.end(), request_done), m.active.end());
            return false;
        }

        m.runtime->forward(b, m.logits);
        m.last_step = clock_type::now();
        m.trimmed = false;

        long prompt_tokens = 0, generated = 0;
        size_t logit_row = 0;
        const long V = m.cfg.vocab;
        // Les lignes dont on tire le token suivant : chaque requête a son propre échantillonneur,
        // donc les tirages s'exécutent en parallèle et les tokens sont ensuite traités dans l'ordre.
        struct draw { request_state* r; float* logits; int token; };
        std::vector<draw> draws;
        for (const row& w : rows)
        {
            request_state& r = *w.r;
            if (r.ph == request_state::phase::decode)
            {
                r.kv_tokens.push_back(r.next_token);
            }
            else
            {
                const long done = static_cast<long>(r.kv_tokens.size());
                r.kv_tokens.insert(r.kv_tokens.end(), r.keys.begin() + done, r.keys.begin() + done + w.count);
                prompt_tokens += w.count;
                if (r.warm)
                {
                    m.warmed += static_cast<uint64_t>(w.count);
                    // L'historique lu en entier : ses clés et valeurs rejoignent les séquences en cache.
                    if (r.kv_tokens.size() == r.prompt.size()) finish(m, r, "read");
                    continue;
                }
                if (!r.job)
                {
                    json p = json::object();
                    p["prompt_done"] = static_cast<long>(r.kv_tokens.size());
                    p["prompt_total"] = static_cast<long>(r.prompt.size());
                    p["cached"] = r.cached;
                    r.sink(ALBIREO_EVENT_PROGRESS, p.dump());
                }
            }
            if (!w.logits) continue;
            const long this_row = static_cast<long>(logit_row++);
            if (r.job)
            {
                // Une entrée d'une requête d'embedding, lue jusqu'au bout.
                complete_part(m, r, m.runtime->hidden().data() + this_row * m.cfg.d_model);
                continue;
            }
            float* lg = m.logits.data() + this_row * V;
            if (r.ph == request_state::phase::prefill)
            {
                r.prompt_end = clock_type::now();
                /*
                    Les pénalités sur les derniers tokens voient la fin du prompt comme le font les
                    serveurs de référence ; celles sur toute la réponse ne lisent que la réponse,
                    comme les serveurs dont les auteurs des modèles donnent les valeurs.
                */
                if (r.smp->params().repeat_last_n >= 0)
                {
                    const size_t tail = std::min<size_t>(r.prompt.size(), 64);
                    for (size_t i = r.prompt.size() - tail; i < r.prompt.size(); ++i) r.smp->accept(r.prompt[i]);
                }
                r.ph = request_state::phase::decode;
            }
            if (r.cancelled)
            {
                finish(m, r, "cancelled");
                continue;
            }
            draws.push_back({ &r, lg, -1 });
        }
        if (draws.size() == 1)
            draws[0].token = draws[0].r->smp->sample(draws[0].logits, V);
        else if (draws.size() > 1)
            pool_->parallel_for(static_cast<long>(draws.size()), [&](long i, int) {
                draw& d = draws[static_cast<size_t>(i)];
                d.token = d.r->smp->sample(d.logits, V);
            });
        for (const draw& d : draws)
        {
            ++generated;
            int token = d.token;
            // La fin d'un raisonnement qui a atteint son budget prend la place des tokens tirés.
            d.r->forcing = !d.r->forced.empty();
            if (d.r->forcing)
            {
                token = d.r->forced.front();
                d.r->forced.pop_front();
            }
            handle_token(m, *d.r, token);
        }

        m.prompted += static_cast<uint64_t>(prompt_tokens);
        m.generated += static_cast<uint64_t>(generated);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            const double t = now_seconds();
            if (generated) generated_window_.emplace_back(t, generated);
            if (prompt_tokens) prompt_window_.emplace_back(t, prompt_tokens);
            total_generated_ += static_cast<uint64_t>(generated);
            total_prompt_ += static_cast<uint64_t>(prompt_tokens);
            while (!generated_window_.empty() && generated_window_.front().first < t - 5) generated_window_.pop_front();
            while (!prompt_window_.empty() && prompt_window_.front().first < t - 5) prompt_window_.pop_front();
        }

        m.active.erase(std::remove_if(m.active.begin(), m.active.end(), request_done), m.active.end());
        long warming = 0;
        for (const auto& r : m.active) warming += r->warm ? 1 : 0;
        m.n_active = static_cast<long>(m.active.size()) - warming;
        m.n_warming = warming;
        m.n_idle = static_cast<long>(m.idle.size());
        return true;
    }

    void engine::handle_token(loaded_model& m, request_state& r, int token)
    {
        if (std::binary_search(m.stops.begin(), m.stops.end(), token))
        {
            finish(m, r, "stop");
            return;
        }
        ++r.generated;
        // La fin d'un raisonnement qu'écrit le moteur n'est pas un texte du modèle pour ses pénalités.
        if (!r.forcing) r.smp->accept(token);
        // Les tokens de contrôle ne sont pas du texte, sauf les marqueurs que lit l'analyseur de sortie.
        if (m.tok.token_type(token) != dlib::hf_tokenizer::TT_CONTROL
            || std::binary_search(m.format->marker_tokens().begin(), m.format->marker_tokens().end(), token))
            r.pending_bytes += m.tok.token_bytes(token);
        const size_t n = complete_utf8(r.pending_bytes);
        if (n > 0)
        {
            const std::string text = r.pending_bytes.substr(0, n);
            r.pending_bytes.erase(0, n);
            emit_text(m, r, text);
            if (r.ph == request_state::phase::done) return;
        }
        const bool reasoning = r.parser && r.parser->in_reasoning();
        /*
            La fin d'un raisonnement court : la réponse reçoit les tokens que la requête lui a
            donnés, après la séparation que le moteur écrit encore quand il termine lui-même le
            raisonnement.
        */
        if (r.in_reasoning && !reasoning && r.answer_tokens >= 0)
        {
            r.max_tokens = std::min(r.max_tokens, r.generated + static_cast<long>(r.forced.size()) + r.answer_tokens);
            r.answer_tokens = -1;
        }
        // La fin d'un raisonnement qu'écrit le moteur ne compte pas dans les répétitions du modèle.
        if (!r.forcing && loops(r, token, reasoning))
        {
            if (reasoning && !r.closing.empty())
            {
                // Un raisonnement qui tourne en rond se termine comme un raisonnement qui a atteint son
                // budget.
                r.forced.assign(r.closing.begin(), r.closing.end());
                r.closing.clear();
                reset_loops(r);
                log(ALBIREO_LOG_INFO, "engine: model '" + r.model_id + "': a reasoning that repeated itself was "
                    "ended after " + std::to_string(r.generated) + " tokens");
            }
            else
            {
                r.repetition = true;
                log(ALBIREO_LOG_WARNING, "engine: model '" + r.model_id + "': an answer that repeated itself was "
                    "stopped after " + std::to_string(r.generated) + " tokens");
                finish(m, r, "length");
                return;
            }
        }
        // Un raisonnement qui a atteint son budget : le moteur écrit sa fin, un token par étape.
        if (r.reasoning_budget >= 0 && r.forced.empty() && !r.closing.empty() && r.parser->in_reasoning()
            && ++r.reasoning_tokens >= r.reasoning_budget)
        {
            r.forced.assign(r.closing.begin(), r.closing.end());
            r.closing.clear();
        }
        if (r.generated >= r.max_tokens || m.runtime->sequence_length(r.seq) + 1 >= m.context)
        {
            finish(m, r, "length");
            return;
        }
        r.next_token = token;
    }

    namespace
    {
        void deliver(request_state& r, std::vector<output_event>& events)
        {
            // Les textes d'une réponse dont l'historique sera lu en avance (engine::warm_history()).
            const bool kept = !r.messages.empty();
            for (auto& e : events)
            {
                if (e.kind == output_kind::content)
                {
                    if (kept) r.answer += e.text;
                    r.sink(ALBIREO_EVENT_CONTENT, e.text);
                }
                else if (e.kind == output_kind::reasoning)
                {
                    if (kept) r.thought += e.text;
                    r.sink(ALBIREO_EVENT_REASONING, e.text);
                }
                else
                {
                    json c = json::object();
                    c["index"] = r.tool_calls++;
                    c["id"] = e.call.id;
                    c["name"] = e.call.name;
                    c["arguments"] = e.call.arguments;
                    r.sink(ALBIREO_EVENT_TOOL_CALL, c.dump());
                }
            }
            events.clear();
        }
    }

    void engine::emit_text(loaded_model& m, request_state& r, const std::string& text)
    {
        std::vector<output_event> events;
        /*
            Les séquences d'arrêt s'appliquent au texte que le modèle écrit pour la requête : ni à
            la fin d'un raisonnement qu'écrit le moteur, ni au raisonnement court d'une requête
            qui n'en a pas demandé.
        */
        if (r.forcing)
        {
            /*
                La fin d'un raisonnement que le moteur écrit à la place du modèle n'est pas
                montrée comme son raisonnement ; le texte du modèle retenu pour les séquences
                d'arrêt passe d'abord.
            */
            if (!r.held.empty())
            {
                r.parser->feed(r.held, events);
                r.held.clear();
                deliver(r, events);
            }
            r.parser->feed(text, events);
            events.erase(std::remove_if(events.begin(), events.end(),
                [](const output_event& e) { return e.kind == output_kind::reasoning; }), events.end());
            deliver(r, events);
            return;
        }
        if (r.stops.empty() || (r.brief && r.parser->in_reasoning()))
        {
            r.parser->feed(r.held + text, events);
            r.held.clear();
            deliver(r, events);
            return;
        }
        r.held += text;
        size_t at = std::string::npos;
        for (const auto& s : r.stops)
        {
            const size_t p = r.held.find(s);
            if (p < at) at = p;
        }
        if (at != std::string::npos)
        {
            r.parser->feed(r.held.substr(0, at), events);
            r.held.clear();
            deliver(r, events);
            finish(m, r, "stop");
            return;
        }
        // Retenir ce qui pourrait commencer une séquence d'arrêt.
        size_t keep = 0;
        for (const auto& s : r.stops)
            for (size_t len = std::min(s.size() - 1, r.held.size()); len > keep; --len)
                if (r.held.compare(r.held.size() - len, len, s, 0, len) == 0)
                {
                    keep = len;
                    break;
                }
        r.parser->feed(r.held.substr(0, r.held.size() - keep), events);
        r.held.erase(0, r.held.size() - keep);
        deliver(r, events);
    }

    void engine::finish(loaded_model& m, request_state& r, const std::string& reason, bool keep_cache)
    {
        if (r.ph == request_state::phase::done) return;
        drop_request_pixels(m, r);
        if (r.job)
        {
            r.ph = request_state::phase::done;
            if (r.seq >= 0) m.runtime->release_sequence(r.seq);
            r.seq = -1;
            if (reason == "cancelled") cancel_job(*r.job);
            else end_job(*r.job, reason, "the request was " + reason);
            return;
        }
        std::vector<output_event> events;
        if (r.parser)
        {
            if (!r.held.empty()) r.parser->feed(r.held, events);
            r.held.clear();
            r.parser->finish(events);
            deliver(r, events);
        }
        r.ph = request_state::phase::done;
        r.finished = clock_type::now();

        json done = json::object();
        done["finish_reason"] = reason == "stop" && r.tool_calls > 0 ? std::string("tool_calls") : reason;
        if (r.repetition) done["repetition"] = true;
        json usage = json::object();
        usage["prompt_tokens"] = static_cast<long>(r.prompt.size());
        usage["completion_tokens"] = r.generated;
        usage["total_tokens"] = static_cast<long>(r.prompt.size()) + r.generated;
        usage["cached_tokens"] = r.cached;
        done["usage"] = usage;
        json timings = json::object();
        const bool started = r.started.time_since_epoch().count() != 0;
        const bool prompted = r.prompt_end.time_since_epoch().count() != 0;
        timings["queued_ms"] = started ? 1000.0 * seconds_since(r.submitted, r.started) : 0.0;
        if (prompted)
        {
            const double pt = seconds_since(r.started, r.prompt_end);
            const double gt = seconds_since(r.prompt_end, r.finished);
            const long computed = static_cast<long>(r.prompt.size()) - r.cached;
            timings["prompt_ms"] = 1000.0 * pt;
            timings["generation_ms"] = 1000.0 * gt;
            timings["prompt_per_second"] = pt > 0 ? computed / pt : 0.0;
            timings["generation_per_second"] = gt > 0 && r.generated > 1 ? (r.generated - 1) / gt : 0.0;
            auto average = [](std::atomic<double>& rate, double sample) {
                const double old = rate.load(std::memory_order_relaxed);
                rate.store(old > 0 ? 0.7 * old + 0.3 * sample : sample, std::memory_order_relaxed);
            };
            if (computed >= 64 && pt > 0) average(m.prompt_rate, computed / pt);
            if (r.generated >= 8 && gt > 0) average(m.generation_rate, (r.generated - 1) / gt);
        }
        done["timings"] = timings;
        r.sink(ALBIREO_EVENT_DONE, done.dump());

        // Indique si la séquence de la requête a rejoint celles en cache, comme la dernière d'entre elles.
        bool cached = false;
        if (r.seq >= 0)
        {
            if (keep_cache && !r.kv_tokens.empty())
            {
                cache_entry e;
                e.seq = r.seq;
                e.tokens.swap(r.kv_tokens);
                e.stamp = ++m.stamp;
                m.idle.push_back(std::move(e));
                cached = true;
                // Un nombre borné de séquences en cache, les moins récemment utilisées abandonnées.
                size_t limit = static_cast<size_t>(std::max<long>(4, 2 * m.max_sequences));
                if (m.cfg.recurrent()) limit = std::min<size_t>(limit, 32);
                while (m.idle.size() > limit)
                {
                    size_t lru = 0;
                    for (size_t i = 1; i < m.idle.size(); ++i)
                        if (m.idle[i].stamp < m.idle[lru].stamp) lru = i;
                    m.runtime->release_sequence(m.idle[lru].seq);
                    m.idle.erase(m.idle.begin() + static_cast<std::ptrdiff_t>(lru));
                }
            }
            else
            {
                m.runtime->release_sequence(r.seq);
            }
            r.seq = -1;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests_.erase(r.id);
        }
        /*
            Une réponse que le client garde, entière ou coupée à sa longueur : son historique est
            lu en avance sur la question suivante.  La séquence en cache est la dernière, les
            autres moins récemment utilisées partant avant elle.
        */
        if (cached && (reason == "stop" || reason == "length") && !r.messages.empty() && r.tool_calls == 0)
            warm_history(m, r, m.idle.back().tokens);
    }

    void engine::warm_history(loaded_model& m, const request_state& r, const std::vector<int>& cached)
    {
        /*
            La requête suivante de la conversation rend cette réponse comme un tour de son
            historique, que les formats écrivent autrement que les tokens générés par le modèle :
            les templates des modèles de raisonnement retirent le raisonnement des tours
            précédents, ceux de Qwen 3 le raisonnement vide qui ouvre une réponse sans
            raisonnement, et le texte de la réponse est tokenisé à nouveau.  La séquence en cache
            de la requête ne correspond alors au prompt suivant que jusqu'au début de la réponse,
            et la requête suivante relirait toute la réponse avant son premier token.
            L'historique est rendu ici comme cette requête le rendra, avec une question d'une
            lettre, deux fois, de sorte que les tokens que partagent les deux rendus se terminent
            là où commence le texte de la question ; une requête à part entière, en arrière-plan
            et sans client, les lit maintenant, pendant que l'utilisateur lit la réponse.  Ses
            clés et valeurs rejoignent alors les séquences en cache, et la requête suivante ne
            calcule que sa question.  Une requête d'un client qui commence par cet historique
            reprend ce que le préchauffage a lu quand elle arrive (yield_warm_ups()).
        */
        if (m.unloading) return;
        auto w = std::make_shared<request_state>();
        try
        {
            std::vector<chat_message> history = r.messages;
            chat_message answer;
            answer.role = "assistant";
            answer.content = r.answer;
            answer.reasoning = r.thought;
            history.push_back(answer);
            std::vector<int> renderings[2];
            std::vector<request_image> placed;
            for (int k = 0; k < 2; ++k)
            {
                std::vector<chat_message> next = history;
                chat_message question;
                question.role = "user";
                question.content = k == 0 ? "A" : "B";
                next.push_back(question);
                std::vector<prompt_piece> pieces;
                m.format->render(next, r.options, pieces);
                std::vector<request_image> images;
                for (const request_image& im : r.images)
                {
                    // Les encodages des images, sans leurs pixels, que la requête a rendus.
                    if (!im.features) return;
                    request_image copy;
                    copy.digest = im.digest;
                    copy.grid_w = im.grid_w;
                    copy.grid_h = im.grid_h;
                    copy.tokens = im.tokens;
                    copy.plan = im.plan;
                    copy.features = im.features;
                    images.push_back(copy);
                }
                renderings[k] = prompt_tokens(m, pieces, images);
                if (k == 0) placed.swap(images);
            }
            const long n = common_prefix(renderings[0], renderings[1]);
            if (n <= 0 || n >= m.context) return;
            w->prompt.assign(renderings[0].begin(), renderings[0].begin() + n);
            for (request_image& im : placed)
            {
                if (im.start + im.tokens > n) return;
                w->images.push_back(im);
            }
            finish_prompt(m, *w);
        }
        catch (const std::exception&)
        {
            // Un historique que le format ne peut pas rendre à nouveau : la requête suivante le lit
            // tel qu'il vient.
            return;
        }
        long known = common_prefix(cached, w->keys);
        // Un modèle à état récurrent ne reprend la séquence de la requête qu'à sa fin : sinon, le préchauffage
        // relit tout l'historique, et s'y attend (admit() abandonne un préchauffage qui lit bien plus qu'attendu).
        if (m.cfg.recurrent() && known != static_cast<long>(cached.size())) known = 0;
        if (static_cast<long>(w->keys.size()) - known < min_warm_tokens) return;
        w->model_id = m.id;
        // Les tokens que le préchauffage s'attend à lire, qu'admit() compare avec ceux qu'il trouve
        // en cache.
        w->max_tokens = static_cast<long>(w->keys.size()) - known;
        w->warm = true;
        w->background = true;
        w->submitted = clock_type::now();
        w->sink = [](int, const std::string&) {};
        log(ALBIREO_LOG_DEBUG, "model '" + m.id + "': the history of an answer is read ahead of the next question ("
            + std::to_string(w->keys.size() - known) + " tokens)");
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || m.unloading) return;
        w->id = next_id_++;
        requests_[w->id] = w;
        m.waiting.push_back(w);
    }

    void engine::yield_warm_ups(loaded_model& m)
    {
        /*
            Un préchauffage cède la place à une requête d'un client qui commence par l'historique
            qu'il lit, la question suivante de sa conversation : un préchauffage en cours de
            lecture se termine là où il en est, ses clés et valeurs rejoignant les séquences en
            cache, et un préchauffage en attente quitte la file, de sorte que la requête reprend
            l'historique à son admission.  Les autres requêtes, comme le travail en arrière-plan
            du client, calculent à côté des préchauffages, qui cèdent aussi leurs places parmi
            les requêtes actives aux requêtes qui en attendent une.
        */
        std::vector<std::shared_ptr<request_state>> dropped;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool warming = false;
            long clients = 0;
            for (const auto& w : m.waiting)
            {
                warming = warming || w->warm;
                clients += w->warm ? 0 : 1;
            }
            for (const auto& a : m.active) warming = warming || a->warm;
            if (!warming || clients == 0) return;
            long places = m.max_sequences - static_cast<long>(m.active.size());
            /*
                Une requête reprend un préchauffage quand elle commence par la plus grande partie
                de son historique, ou, une fois que le préchauffage détient une séquence, par tous
                les tokens que le préchauffage a trouvés en cache, comme le fait une requête qui
                réécrit la même réponse.
            */
            auto takes_over = [&](const request_state& warm) {
                for (const auto& c : m.waiting)
                {
                    if (c->warm) continue;
                    const long shared = common_prefix(c->keys, warm.keys);
                    if (shared > 0 && (2 * shared >= static_cast<long>(warm.keys.size())
                        || (warm.seq >= 0 && shared >= warm.cached)))
                        return true;
                }
                return false;
            };
            for (auto it = m.waiting.begin(); it != m.waiting.end();)
            {
                if ((*it)->warm && takes_over(**it))
                {
                    dropped.push_back(*it);
                    it = m.waiting.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (const auto& a : m.active)
            {
                if (!a->warm || a->ph != request_state::phase::prefill) continue;
                if (takes_over(*a) || places < clients)
                {
                    dropped.push_back(a);
                    ++places;
                }
            }
        }
        for (const auto& w : dropped) finish(m, *w, "replaced");
        m.active.erase(std::remove_if(m.active.begin(), m.active.end(), request_done), m.active.end());
    }

    void engine::fail(request_state& r, const std::string& code, const std::string& message)
    {
        if (r.ph == request_state::phase::done) return;
        r.ph = request_state::phase::done;
        json e = json::object();
        e["code"] = code;
        e["message"] = message;
        if (r.job) end_job(*r.job, code, message);
        else if (r.sink) r.sink(ALBIREO_EVENT_ERROR, e.dump());
        std::shared_ptr<loaded_model> m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = models_.find(r.model_id);
            if (it != models_.end()) m = it->second;
        }
        // Les pixels décodés que la requête détient encore retournent au budget de son modèle.
        if (m) drop_request_pixels(*m, r);
        if (r.seq >= 0)
        {
            if (m) m->runtime->release_sequence(r.seq);
            r.seq = -1;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.erase(r.id);
    }

    // Interrogations

    json engine::stats()
    {
        json s = json::object();
        s["variant"] = ALBIREO_ENGINE_VARIANT;
        s["version"] = ALBIREO_VERSION;
        s["threads"] = threads_;
        // Le GPU du Mac : son nom, ou la raison pour laquelle le moteur ne l'utilise pas.
        json gpu = json::object();
        if (gpu_) gpu["name"] = gpu_->name();
        else if (!settings_["metal"].as_bool(true)) gpu["disabled"] = true;
        else gpu["reason"] = gpu_off_;
        s["gpu"] = gpu;
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            const double t = now_seconds();
            // Les tokens des cinq dernières secondes : aucun une fois que le moteur attend, plutôt
            // qu'un débit qui retombe lentement après la dernière passe.
            auto rate = [&](const std::deque<std::pair<double, long>>& w) {
                long total = 0;
                double first = t;
                for (const auto& e : w)
                {
                    if (e.first < t - 5) continue;
                    total += e.second;
                    first = std::min(first, e.first);
                }
                return total > 0 ? total / std::max(1.0, t - first) : 0.0;
            };
            s["generation_tokens_per_second"] = rate(generated_window_);
            s["prompt_tokens_per_second"] = rate(prompt_window_);
            s["generated_total"] = static_cast<unsigned long long>(total_generated_);
            s["prompt_total"] = static_cast<unsigned long long>(total_prompt_);
        }
        json models = json::array();
        long active = 0, waiting = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        s["requests_total"] = static_cast<unsigned long long>(total_requests_);
        for (const auto& kv : models_)
        {
            const loaded_model& m = *kv.second;
            // Les requêtes des clients ; les préchauffages des historiques à part.
            long queued = 0, warming = m.n_warming.load();
            for (const auto& r : m.waiting)
            {
                if (r->warm) ++warming;
                else ++queued;
            }
            json j = json::object();
            j["id"] = m.id;
            j["active"] = m.n_active.load();
            j["waiting"] = queued;
            j["warming"] = warming;
            j["cached_sequences"] = m.n_idle.load();
            j["kv_used"] = static_cast<unsigned long long>(m.runtime->kv_bytes_used());
            j["kv_budget"] = static_cast<unsigned long long>(m.runtime->kv_bytes_budget());
            // L'utilisation du GPU par ses passes.
            if (const cpu_runtime* rt = dynamic_cast<const cpu_runtime*>(m.runtime.get()))
            {
                const cpu_runtime::gpu_usage u = rt->gpu_use();
                json g = json::object();
                g["state"] = u.state;
                if (!u.reason.empty()) g["reason"] = u.reason;
                g["bytes"] = static_cast<unsigned long long>(u.bytes);
                j["gpu"] = g;
            }
            j["generated_total"] = static_cast<unsigned long long>(m.generated.load());
            j["prompt_total"] = static_cast<unsigned long long>(m.prompted.load());
            j["warmed_total"] = static_cast<unsigned long long>(m.warmed.load());
            active += m.n_active.load();
            waiting += queued;
            models.push_back(j);
        }
        s["active"] = active;
        s["waiting"] = waiting;
        s["models"] = models;
        return s;
    }

    json engine::query(const json& req)
    {
        const std::string op = req["op"].as_string();
        if (op == "stats") return stats();
        if (op == "describe") return describe();
        if (op == "models")
        {
            json list = json::array();
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& kv : models_) list.push_back(model_info(*kv.second));
            json out = json::object();
            out["models"] = list;
            return out;
        }

        std::shared_ptr<loaded_model> m;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = models_.find(req["model"].as_string());
            if (it == models_.end())
                throw std::invalid_argument("the model '" + req["model"].as_string() + "' is not loaded");
            m = it->second;
        }
        json out = json::object();
        if (op == "tokenize")
        {
            const std::vector<int> ids = m->tok.encode(req["text"].as_string(), false, false,
                req["special"].as_bool(false));
            json t = json::array();
            for (int id : ids) t.push_back(id);
            out["tokens"] = t;
            return out;
        }
        if (op == "detokenize")
        {
            std::string text;
            for (const auto& t : req["tokens"].elements())
            {
                const long long id = t.as_int(-1);
                if (!t.is_integer() || id < 0 || id >= m->cfg.vocab)
                    throw std::invalid_argument("tokens holds an invalid token identifier");
                text += m->tok.token_bytes(static_cast<int>(id));
            }
            out["text"] = text;
            return out;
        }
        if (op == "count" || op == "render")
        {
            render_options opt;
            current_dates(opt);
            // Comme le rend une requête de réponse : un modèle qui raisonne toujours ouvre son
            // raisonnement.
            opt.reasoning = req["reasoning"].as_bool(m->format->supports_reasoning()) || m->format->always_reasons();
            if (req["tools"].is_array()) opt.tools = req["tools"];
            if (m->vision) strip_marks(opt.tools);
            std::vector<image_part> parts;
            std::vector<chat_message> messages = parse_messages(req["messages"], m->vision ? &parts : nullptr);
            if (!parts.empty() && m->vision->kind() == vision_encoder::family::llava_next) images_first(messages);
            if (!parts.empty() && m->vision->kind() == vision_encoder::family::pixtral) mistral_images(messages);
            std::vector<prompt_piece> pieces;
            m->format->render(messages, opt, pieces);
            // Les images comptent selon leur taille, lue dans leurs en-têtes, sans être décodées.
            std::vector<request_image> images;
            if (!parts.empty()) prepare_images(*m, parts, false, images);
            const std::vector<int> ids = prompt_tokens(*m, pieces, images);
            out["tokens"] = static_cast<long>(ids.size());
            out["context"] = m->context;
            if (op == "render")
            {
                std::string text;
                for (const auto& p : pieces) text += p.text;
                out["prompt"] = text;
            }
            return out;
        }
        throw std::invalid_argument("unknown query '" + op + "'");
    }

}
