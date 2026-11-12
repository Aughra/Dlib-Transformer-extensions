// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "models.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <set>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zlib.h>

#include <dlib/data_io/gguf_reader.h>

#include "../common/builtin_models.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "crypto.h"
#include "http_fetch.h"

namespace albireo
{

    namespace
    {
        /*
            Un fichier écrit à des positions données par plusieurs threads : le fichier partiel d'un
            téléchargement, créé ou rouvert à son propre nom, jamais par un lien à cet endroit, avec un
            nom unique et situé à path atteint sans passer par aucun lien, pour qu'aucune écriture n'aille
            ailleurs.
        */
        class random_access_file
        {
        public:
            bool open(const std::string& path, uint64_t size)
            {
                fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
                if (fd_ < 0) return false;
                struct stat st;
                if (fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 || !handle_at(fd_, path))
                {
                    ::close(fd_);
                    fd_ = -1;
                    return false;
                }
                return ftruncate(fd_, static_cast<off_t>(size)) == 0;
            }

            bool write_at(uint64_t offset, const char* data, size_t n)
            {
                while (n > 0)
                {
                    const ssize_t w = pwrite(fd_, data, n, static_cast<off_t>(offset));
                    if (w <= 0) return false;
                    offset += static_cast<uint64_t>(w);
                    data += w;
                    n -= static_cast<size_t>(w);
                }
                return true;
            }

            void close()
            {
                if (fd_ >= 0)
                {
                    fsync(fd_);
                    ::close(fd_);
                }
                fd_ = -1;
            }

            ~random_access_file() { close(); }

        private:
            int fd_ = -1;
        };

        std::string localized(const json& text, const std::string& language)
        {
            if (text.is_string()) return text.as_string();
            if (text[language].is_string()) return text[language].as_string();
            if (text["en"].is_string()) return text["en"].as_string();
            for (const auto& kv : text.items())
                if (kv.second.is_string()) return kv.second.as_string();
            return std::string();
        }

        bool safe_id(const std::string& id)
        {
            if (id.empty() || id.size() > 128) return false;
            for (char c : id)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
            return id[0] != '.';
        }

        // Un nom de fichier dans un dossier : ni séparateur, ni flux, ni joker, ni nom "." et "..".
        bool plain_name(const std::string& name)
        {
            return !name.empty() && name.size() <= 255 && name != "." && name != ".."
                && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
        }

        // Pourquoi un fichier n'a pas pu être ouvert sans passer par aucun lien (link_free_file::open()), en mots.
        std::string unreadable_reason(const std::string& why)
        {
            if (why == "link") return "reached through a link, which imports never follow";
            if (why == "missing") return "not found";
            return "not a regular file";
        }

        bool ends_with(const std::string& s, const std::string& tail)
        {
            return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
        }

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // Le nom d'un projecteur du dossier des modèles : <identifier>.mmproj.gguf.
        const char* const projector_suffix = ".mmproj.gguf";

        // Indique si une partie d'un nom de fichier nomme une quantification : Q4_K_M, IQ4_NL, Q8_0, F16, BF16...
        bool quantization_name(const std::string& s)
        {
            if (s == "f16" || s == "f32" || s == "bf16" || s == "f64") return true;
            const size_t at = s.compare(0, 2, "iq") == 0 ? 2 : s.compare(0, 1, "q") == 0 ? 1 : 0;
            if (at == 0 || at >= s.size() || !std::isdigit(static_cast<unsigned char>(s[at]))) return false;
            for (size_t i = at; i < s.size(); ++i)
                if (!std::isalnum(static_cast<unsigned char>(s[i])) && s[i] != '_') return false;
            return true;
        }

        /*
            Le nom que partagent un projecteur et son décodeur : le nom de fichier en minuscules sans son
            extension, le préfixe ou le suffixe "mmproj" et la quantification, ses lettres et ses chiffres
            seuls.  "mmproj-Qwen3VL-2B-Instruct-Q8_0.gguf" et "Qwen3VL-2B-Instruct-Q4_K_M.gguf" donnent
            tous deux "qwen3vl2binstruct".
        */
        std::string name_stem(const std::string& file)
        {
            std::string s = lower(file_name(file));
            if (ends_with(s, ".gguf")) s.resize(s.size() - 5);
            for (const char* tail : { ".mmproj", "-mmproj", "_mmproj" })
                if (ends_with(s, tail)) s.resize(s.size() - std::strlen(tail));
            if (s.compare(0, 6, "mmproj") == 0 && s.size() > 6 && (s[6] == '-' || s[6] == '_' || s[6] == '.'))
                s = s.substr(7);
            for (size_t i = s.size(); i-- > 0;)
                if ((s[i] == '-' || s[i] == '_' || s[i] == '.') && quantization_name(s.substr(i + 1)))
                {
                    s.resize(i);
                    break;
                }
            std::string out;
            for (char c : s)
                if (std::isalnum(static_cast<unsigned char>(c))) out += c;
            return out;
        }

        // Les lettres et les chiffres d'un nom en minuscules, par lesquels general.name est comparé.
        std::string plain_name_of(const std::string& name)
        {
            std::string out;
            for (char c : lower(name))
                if (std::isalnum(static_cast<unsigned char>(c))) out += c;
            return out;
        }

        /*
            La proximité des noms d'un projecteur et d'un décodeur : le même general.name dans les deux
            en-têtes, puis la même racine de leurs noms de fichier, puis la longueur du début commun de
            ces racines.
        */
        long name_closeness(const pairing_file& projector, const pairing_file& decoder)
        {
            long score = 0;
            const std::string a = plain_name_of(projector.description["gguf_name"].as_string());
            const std::string b = plain_name_of(decoder.description["gguf_name"].as_string());
            if (!a.empty() && a == b) score += 1 << 20;
            const std::string x = name_stem(projector.name), y = name_stem(decoder.name);
            if (!x.empty() && x == y) return score + (1 << 16);
            size_t common = 0;
            while (common < x.size() && common < y.size() && x[common] == y[common]) ++common;
            return score + static_cast<long>(std::min<size_t>(common, 0xFFFF));
        }

        json text_inputs()
        {
            json inputs = json::array();
            inputs.push_back("text");
            return inputs;
        }

        /*
            Les entrées nommées par list, "text" d'abord, avec "image" quand image est vrai et sans sinon ;
            les autres entrées (audio) telles que listées.
        */
        json inputs_with(const json& list, bool image)
        {
            json out = text_inputs();
            if (image) out.push_back("image");
            for (const auto& s : list.elements())
            {
                const std::string v = s.as_string();
                if (v.empty() || v == "text" || v == "image") continue;
                bool seen = false;
                for (const auto& o : out.elements()) seen = seen || o.as_string() == v;
                if (!seen) out.push_back(v);
            }
            return out;
        }

        /*
            Le modèle livré avec Albireo dont le fichier est celui de l'entrée du catalogue, fait à partir
            d'elle par le build d'Albireo sous un autre identifiant : son manifeste donne l'empreinte du
            fichier dont il a été fait (source_sha256).  Nul quand il n'y en a pas.
        */
        const installed_model* builtin_carrier(const std::vector<installed_model>& installed, const json& entry)
        {
            const std::string sha = entry["sha256"].as_string();
            if (sha.empty()) return nullptr;
            for (const auto& m : installed)
                if (m.source == "builtin" && m.descriptor["source_sha256"].as_string() == sha) return &m;
            return nullptr;
        }
    }

    bool reads_images(const std::string& arch)
    {
        return arch == "qwen3vl" || arch == "qwen3vlmoe";
    }

    json model_inputs(const installed_model& m)
    {
        return inputs_with(m.descriptor["inputs"], !m.projector.empty());
    }

    json entry_inputs(const json& entry)
    {
        bool image = false;
        for (const auto& s : entry["inputs"].elements()) image = image || s.as_string() == "image";
        return inputs_with(entry["inputs"], image && entry["projector"].is_object());
    }

    std::vector<int> pair_projectors(const std::vector<pairing_file>& decoders,
        const std::vector<pairing_file>& projectors)
    {
        std::vector<int> pair(projectors.size(), -1);
        std::vector<bool> taken(decoders.size(), false);
        // Les empreintes du catalogue d'abord.
        for (size_t p = 0; p < projectors.size(); ++p)
            for (size_t d = 0; d < decoders.size() && pair[p] < 0; ++d)
                if (!taken[d] && !projectors[p].sha256.empty()
                    && decoders[d].expected_projector == projectors[p].sha256)
                {
                    pair[p] = static_cast<int>(d);
                    taken[d] = true;
                }
        // Puis les largeurs des en-têtes, et les noms parmi plusieurs décodeurs.
        for (size_t p = 0; p < projectors.size(); ++p)
        {
            if (pair[p] >= 0) continue;
            const long long width = projectors[p].description["projection_width"].as_int(0);
            if (width <= 0) continue;
            long best = -1;
            int chosen = -1;
            bool tie = false;
            for (size_t d = 0; d < decoders.size(); ++d)
            {
                const json& dd = decoders[d].description;
                const bool images = dd["reads_images"].as_bool(reads_images(dd["arch"].as_string()));
                if (taken[d] || !images || dd["projection_width"].as_int(0) != width) continue;
                const long score = name_closeness(projectors[p], decoders[d]);
                if (score > best)
                {
                    best = score;
                    chosen = static_cast<int>(d);
                    tie = false;
                }
                else if (score == best)
                {
                    tie = true;
                }
            }
            if (chosen >= 0 && !tie)
            {
                pair[p] = chosen;
                taken[static_cast<size_t>(chosen)] = true;
            }
        }
        return pair;
    }

    json describe_gguf(const std::string& path)
    {
        json d = json::object();
        try
        {
            dlib::gguf_reader g(path);
            const std::string arch = g.get_str("general.architecture", "");
            d["name"] = g.get_str("general.name", file_name(path));
            d["gguf_name"] = g.get_str("general.name", "");
            d["arch"] = arch;
            // L'entreprise qui a fait le modèle, quand son en-tête la nomme.
            const std::string publisher = !g.get_str("general.organization", "").empty()
                ? g.get_str("general.organization") : g.get_str("general.author", "");
            if (!publisher.empty()) d["publisher"] = publisher;
            uint64_t params = 0;
            std::map<std::string, uint64_t> by_type;
            for (const auto& t : g.tensors())
            {
                params += t.n_elements();
                by_type[std::to_string(static_cast<unsigned>(t.type))] += t.nbytes;
            }
            d["parameters"] = static_cast<unsigned long long>(params);
            static const std::map<std::string, std::string> names = { { "0", "F32" }, { "1", "F16" }, { "2", "Q4_0" },
                { "3", "Q4_1" }, { "6", "Q5_0" }, { "7", "Q5_1" }, { "8", "Q8_0" }, { "10", "Q2_K" }, { "11", "Q3_K" },
                { "12", "Q4_K" }, { "13", "Q5_K" }, { "14", "Q6_K" }, { "20", "IQ4_NL" }, { "30", "BF16" } };
            uint64_t most = 0;
            std::string q;
            for (const auto& kv : by_type)
                if (kv.second > most) { most = kv.second; q = kv.first; }
            d["quantization"] = names.count(q) ? names.at(q) : q;
            // Les largeurs des tokens de vision, bornées pour qu'un en-tête abîmé ne donne aucune largeur.
            auto width_of = [](int64_t width, int64_t layers) -> long long {
                if (width <= 0 || width > (int64_t(1) << 20) || layers < 0 || layers > 256) return 0;
                return static_cast<long long>(width * (1 + layers));
            };
            if (arch == "clip")
            {
                // Un projecteur : l'encodeur de vision d'un modèle qui lit les images, et la projection
                // de sa sortie dans les embeddings du décodeur.
                d["projector"] = true;
                d["projector_type"] = g.get_str("clip.projector_type", "");
                d["vision"] = g.get_int("clip.has_vision_encoder", 1) != 0;
                int64_t deepstack = 0;
                if (g.has("clip.vision.is_deepstack_layers"))
                    for (int64_t v : g.at("clip.vision.is_deepstack_layers").arr_int) deepstack += v != 0 ? 1 : 0;
                // Un projecteur LLaVA ne nomme aucune largeur de projection, et celle d'un projecteur
                // Pixtral en est une autre : leur dernière couche la donne.
                const std::string type = d["projector_type"].as_string();
                long long width = type == "pixtral" ? 0
                    : width_of(g.get_int("clip.vision.projection_dim", 0), deepstack);
                for (const char* last : { "mm.2.weight", "mm.0.weight" })
                {
                    const dlib::gguf_tensor_info* t = width > 0 ? nullptr : g.find_tensor(last);
                    if (t && t->dims.size() >= 2) width = width_of(static_cast<int64_t>(t->dims[1]), 0);
                }
                d["projection_width"] = width;
                d["image_size"] = static_cast<long long>(g.get_int("clip.vision.image_size", 0));
                return d;
            }
            d["context"] = static_cast<long long>(g.get_int(arch + ".context_length", 0));
            d["experts"] = static_cast<long long>(g.get_int(arch + ".expert_count", 0));
            /*
                Un décodeur qui lit les images : Qwen3-VL, un décodeur LLaVA, dont le vocabulaire contient
                <image>, ou un décodeur Mistral, dont le vocabulaire contient [IMG] et [IMG_END].
            */
            bool images = reads_images(arch);
            if (!images && g.has("tokenizer.ggml.tokens"))
            {
                const std::vector<std::string>& tokens = g.at("tokenizer.ggml.tokens").arr_str;
                auto holds = [&](const char* t) { return std::find(tokens.begin(), tokens.end(), t) != tokens.end(); };
                images = holds("<image>") || (holds("[IMG]") && holds("[IMG_END]"));
            }
            d["reads_images"] = images;
            if (images)
                d["projection_width"] = width_of(g.get_int(arch + ".embedding_length", 0),
                    g.get_int(arch + ".n_deepstack_layers", 0));
            const std::string tmpl = g.get_str("tokenizer.chat_template", "");
            // Un modèle d'embedding déclare comment il regroupe ses états ; il sert la recherche
            // dans les documents et rien d'autre.
            const bool embedding = g.get_int(arch + ".pooling_type", 0) != 0;
            d["embedding"] = embedding;
            if (embedding)
            {
                json roles = json::array();
                roles.push_back("embedding");
                d["roles"] = roles;
            }
            /*
                Les capacités que les formats du moteur donnent à un modèle de ce gabarit, lues dans le
                gabarit seul (chat_format::detect() lit aussi le vocabulaire) :
                    - outils : les formats de Mistral, Llama 3, Granite 3 et Granite 4.2, et les gabarits
                      ChatML qui décrivent des outils ; Phi-4-mini, Foundation-Sec et les autres formats
                      n'en appellent aucun ;
                    - raisonnement : les gabarits ChatML qui ouvrent le raisonnement ou le commutent,
                      Granite 4.2, Foundation-Sec-8B-Reasoning et les gabarits avec [THINK] ;
                    - raisonnement toujours : les variantes thinking de Qwen3 et Qwen3-VL, dont le gabarit
                      ouvre le raisonnement sans commutateur, et Foundation-Sec-8B-Reasoning, dont les
                      fichiers portent un gabarit sans <|end|> qui retire le raisonnement des réponses
                      précédentes.
            */
            const auto holds = [&](const char* s) { return tmpl.find(s) != std::string::npos; };
            const bool chatml = holds("<|im_start|>");
            const bool granite4 = chatml && holds("<function=") && holds("<think></think>");
            const bool opens = chatml && holds("<|im_start|>assistant\\n<think>\\n'");
            const bool switchable = chatml && holds("enable_thinking");
            const bool foundation_sec = !chatml && holds("<|assistant|>") && holds("'</think>' in content")
                && !holds("<|end|>");
            d["reasoning"] = !embedding && (granite4 || opens || switchable || foundation_sec || holds("[THINK]"));
            d["reasoning_always"] = !embedding && ((opens && !switchable) || foundation_sec);
            d["tools"] = !embedding && (holds("[INST]") || holds("<|start_header_id|>") || holds("<|start_of_role|>")
                || granite4 || (chatml && (holds("<tool_call>") || holds("tools"))));
            // Les entrées que le moteur lit dans le modèle, et l'identité que porte un fichier
            // préparé par le build d'Albireo (tools/albireo_builtin.cpp).
            json inputs = json::array();
            if (g.has("albireo.inputs"))
                for (const auto& s : g.at("albireo.inputs").arr_str) inputs.push_back(s);
            if (inputs.size() == 0) inputs.push_back("text");
            d["inputs"] = inputs;
            if (g.has("albireo.id"))
            {
                d["albireo_id"] = g.get_str("albireo.id");
                if (g.has("albireo.name")) d["name"] = g.get_str("albireo.name");
                if (g.has("albireo.roles"))
                {
                    json roles = json::array();
                    for (const auto& s : g.at("albireo.roles").arr_str) roles.push_back(s);
                    d["roles"] = roles;
                }
            }
        }
        catch (const std::exception& e)
        {
            d["error"] = e.what();
        }
        return d;
    }

    struct model_manager::job
    {
        struct chunk
        {
            std::string url;          // l'adresse de la ressource, donnée par le catalogue
            uint64_t resource = 0;    // octets de la ressource
            uint64_t from = 0;        // plage d'octets dans la ressource
            uint64_t length = 0;      // octets de la ressource (taille compressée pour gzip)
            uint64_t offset = 0;      // où vont les octets dans le fichier
            std::atomic<uint64_t> done{0};   // octets reçus de la ressource
            bool gzip = false;
            uint64_t output = 0;      // octets que produit la partie (gzip : taille décompressée)
            size_t part = 0;          // la partie de son fichier
            size_t file = 0;          // 0 : le décodeur, 1 : le projecteur
        };

        // Un fichier du modèle : le décodeur, et le projecteur d'un modèle qui lit les images.
        struct output_file
        {
            std::string target, partial;
            uint64_t size = 0;
            std::string sha256;
            json parts;               // les "files" du catalogue : url, size, sha256
        };

        std::string id;
        json entry;
        std::vector<output_file> files;
        uint64_t total = 0;
        std::vector<std::unique_ptr<chunk>> chunks;
        std::atomic<uint64_t> received{0};
        std::atomic<bool> cancel{false};
        // Annulé par l'utilisateur, fixé sous m_ : la progression et le fichier partiel sont abandonnés,
        // là où un arrêt du service les garde pour le prochain démarrage.
        std::atomic<bool> abandoned{false};
        std::string state = "queued";
        user_error error;
        double speed = 0;
        int64_t started = 0;
        std::thread worker;
    };

    model_manager::model_manager(store& db, const std::string& models_dir)
        : db_(db), dir_(models_dir)
    {
        /*
            Le service écrit, renomme et supprime les fichiers des modèles avec ses droits : seuls
            SYSTEM et les administrateurs peuvent modifier le dossier (secure_folder()).  Un dossier
            qui ne peut pas être rendu tel laisse les modèles au dossier des données.
        */
        std::string error;
        if (!secure_folder(dir_, error))
        {
            const std::string fallback = join_path(data_dir(), "models");
            log_error("models: " + error + "; the models go to " + fallback);
            dir_ = fallback;
            if (!secure_folder(dir_, error))
            {
                log_error("models: " + error);
                make_dirs(dir_);
            }
        }
        // Le dossier par son propre chemin : un lien placé plus tard sur son trajet se voit (direct_path()).
        const std::string resolved = final_path(dir_);
        if (!resolved.empty()) dir_ = resolved;
        catalog_ = json::parse_or_null(db_.value("catalog", "{}"));
        if (!catalog_.is_object()) catalog_ = json::object();
        custom_ = json::parse_or_null(db_.value("custom_catalog", "[]"));
        if (!custom_.is_array()) custom_ = json::array();
        merge_custom();
    }

    // Mêle la liste personnelle au catalogue en mémoire (sous m_, ou avant que les threads ne démarrent).
    void model_manager::merge_custom()
    {
        json models = json::array();
        for (const auto& e : catalog_["models"].elements())
        {
            bool mine = false;
            for (const auto& c : custom_.elements()) mine = mine || c["id"].as_string() == e["id"].as_string();
            if (!mine) models.push_back(e);
        }
        for (const auto& c : custom_.elements()) models.push_back(c);
        catalog_["models"] = models;
    }

    bool model_manager::add_custom(const json& entry, std::string& error)
    {
        const std::string id = entry["id"].as_string();
        bool valid = !id.empty() && id.size() <= 120;
        for (const char c : id) valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-');
        if (!valid)
        {
            error = "invalid model identifier";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            json kept = json::array();
            for (const auto& c : custom_.elements())
                if (c["id"].as_string() != id) kept.push_back(c);
            kept.push_back(entry);
            db_.set_value("custom_catalog", kept.dump());
            custom_ = kept;
            merge_custom();
        }
        ++generation_;
        log_info("models: " + id + " added to the personal list from " + entry["source"].as_string());
        return true;
    }

    json model_manager::custom() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return custom_;
    }

    model_manager::~model_manager()
    {
        stop();
        if (refresher_.joinable()) refresher_.join();
        std::map<std::string, std::shared_ptr<job>> jobs;
        {
            std::lock_guard<std::mutex> lock(m_);
            jobs = jobs_;
        }
        for (auto& kv : jobs)
            if (kv.second->worker.joinable()) kv.second->worker.join();
    }

    void model_manager::stop()
    {
        stopped_ = true;
        std::lock_guard<std::mutex> lock(m_);
        for (auto& kv : jobs_) kv.second->cancel = true;
    }

    bool model_manager::install_catalog(const std::string& body, bool keep_newer, json& result, std::string& error)
    {
        json c;
        if (!json::parse(body, c, &error) || !c["models"].is_array())
        {
            error = "the catalog is not valid: " + error;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            catalog_state_ = "ok";
            catalog_error_.clear();
            // Les dates de publication, "YYYY-MM-DDTHH:MM:SSZ" (le jour seul dans les premiers
            // catalogues), se comparent comme des chaînes : un catalogue plus ancien, rejoué ou apporté
            // sur un vieux support, ne remplace jamais un plus récent.
            const json& current = catalog_;
            if (keep_newer && current["updated"].as_string() > c["updated"].as_string())
            {
                result["state"] = "kept";
                return true;
            }
            /*
                Écrit sous le verrou, pour que de deux catalogues installés en même temps, depuis le
                réseau et depuis un support, la base garde celui gardé en mémoire, et écrit d'abord :
                une base qui le refuse, sur un disque plein par exemple, laisse le catalogue en usage
                tel qu'il était.
            */
            db_.set_value("catalog", body);
            db_.set_value("catalog_time", std::to_string(now_unix()));
            catalog_ = c;
            merge_custom();
        }
        ++generation_;
        result["models"] = static_cast<long long>(c["models"].size());
        log_info("models: catalog of " + std::to_string(c["models"].size()) + " models, published "
            + c["updated"].as_string());
        return true;
    }

    namespace
    {
        // L'adresse du catalogue : le dépôt de l'original, qui le garde à jour.  Il n'est plus signé.
        const char* const official_catalog = "https://raw.githubusercontent.com/Cydral/Dlib-Transformer-extensions/"
            "main/dist/albireo/catalog.json";

        // L'adresse que official_catalog_for_tests() donne à la place de l'officielle, vide sinon.
        std::mutex catalog_address_m;
        std::string catalog_address_of_tests;

        // L'adresse d'où le catalogue est lu.
        std::string catalog_address()
        {
            std::lock_guard<std::mutex> lock(catalog_address_m);
            return catalog_address_of_tests.empty() ? std::string(official_catalog) : catalog_address_of_tests;
        }
    }

    void official_catalog_for_tests(const std::string& url)
    {
        std::lock_guard<std::mutex> lock(catalog_address_m);
        catalog_address_of_tests = url;
    }

    bool model_manager::refresh_catalog(std::string& error)
    {
        // Le catalogue vient de son adresse, sans signature à vérifier.
        const std::string url = catalog_address();
        std::string body, e;
        json result = json::object();
        if (!http_get(url, body, e, 16u << 20))
        {
            error = "the catalog cannot be downloaded from " + url + ": " + e;
            return false;
        }
        if (!install_catalog(body, true, result, e))
        {
            error = "the catalog of " + url + " is refused: " + e;
            return false;
        }
        return true;
    }

    void model_manager::refresh_catalog_async()
    {
        std::lock_guard<std::mutex> start(refresher_m_);
        if (stopped_ || refreshing_.exchange(true)) return;
        if (refresher_.joinable()) refresher_.join();
        std::string before;
        {
            std::lock_guard<std::mutex> lock(m_);
            before = catalog_state_;
            catalog_state_ = "checking";
        }
        refresher_ = std::thread([this, before]() {
            std::string error;
            bool ok = false;
            try
            {
                ok = refresh_catalog(error);
            }
            catch (const std::exception& e)
            {
                // Une base qui ne peut pas être écrite, par exemple un disque plein.
                error = e.what();
            }
            catch (...)
            {
                error = "the catalog cannot be refreshed";
            }
            // Une mise à jour interrompue par le service, qui s'arrête ou passe hors ligne, n'est pas une erreur.
            const bool ended = stopped_ || http_cancelled();
            if (!ok)
            {
                std::lock_guard<std::mutex> lock(m_);
                catalog_state_ = ended ? before : std::string("error");
                if (!ended) catalog_error_ = error;
            }
            if (!ok && !ended) log_warning("models: catalog not refreshed: " + error);
            refreshing_ = false;
        });
    }

    json model_manager::import_catalog(const std::string& folder, const std::string& base)
    {
        json result = json::object();
        std::string body, error;
        if (!read_file_link_free(join_path(folder, "catalog.json"), base, body, 16u << 20))
        {
            result["error"] = "no catalog (catalog.json) in " + folder;
            return result;
        }
        if (!install_catalog(body, true, result, error)) result["error"] = error;
        return result;
    }

    json model_manager::catalog_status() const
    {
        json s = json::object();
        {
            std::lock_guard<std::mutex> lock(m_);
            s["state"] = catalog_state_;
            if (!catalog_error_.empty()) s["error"] = catalog_error_;
            s["updated"] = catalog_["updated"].as_string();
            s["models"] = static_cast<long long>(catalog_["models"].size());
        }
        s["time"] = static_cast<long long>(std::atoll(db_.value("catalog_time", "0").c_str()));
        return s;
    }

    json model_manager::catalog() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return catalog_;
    }

    bool model_manager::catalog_entry(const std::string& id, json& entry) const
    {
        std::lock_guard<std::mutex> lock(m_);
        for (const auto& e : catalog_["models"].elements())
            if (e["id"].as_string() == id)
            {
                entry = e;
                return true;
            }
        return false;
    }

    json model_manager::overview(const std::string& language) const
    {
        const std::vector<installed_model> installed = db_.models();
        json out = json::array();
        std::map<std::string, bool> seen;
        json cat = catalog();
        auto embedding = [](const json& d) {
            if (d["embedding"].as_bool(false)) return true;
            for (const auto& r : d["roles"].elements())
                if (r.as_string() == "embedding") return true;
            return false;
        };
        /*
            Ce que l'installation d'un modèle ajoute à son entrée : son fichier, son projecteur, ses
            empreintes, les entrées qu'il lit (du texte, et des images avec son projecteur) et les
            octets de ses fichiers.
        */
        auto installed_fields = [](json& m, const installed_model& im) {
            const json& d = im.descriptor;
            m["installed"] = true;
            m["file"] = im.file;
            m["local_sha256"] = im.sha256;
            m["verified_at"] = static_cast<long long>(im.verified_at);
            m["inputs"] = model_inputs(im);
            if (im.projector.empty())
            {
                m.erase("projector");
            }
            else
            {
                json p = d["projector"].is_object() ? d["projector"] : json::object();
                p.erase("files");
                p["file"] = im.projector;
                p["size"] = static_cast<unsigned long long>(im.projector_size);
                p["sha256"] = im.projector_sha256;
                m["projector"] = p;
            }
            m["total_size"] = static_cast<unsigned long long>(im.size + im.projector_size);
        };
        // Les modèles livrés avec Albireo d'abord, dans l'ordre de leur manifeste.
        for (const auto& im : installed)
        {
            if (im.source != "builtin" || embedding(im.descriptor)) continue;
            json m = im.descriptor;
            m["id"] = im.id;
            m["name"] = im.name;
            m["description"] = localized(m["description"], language);
            m["size"] = static_cast<unsigned long long>(im.size);
            installed_fields(m, im);
            seen[im.id] = true;
            out.push_back(m);
        }
        for (const auto& e : cat["models"].elements())
        {
            if (embedding(e)) continue;
            // L'entrée que remplace un modèle livré avec Albireo, sauf si elle est installée sous son propre
            // identifiant.
            const std::string eid = e["id"].as_string();
            bool own = false;
            for (const auto& im : installed) own = own || im.id == eid;
            if (!own && builtin_carrier(installed, e)) continue;
            json m = e;
            m["description"] = localized(e["description"], language);
            m["installed"] = false;
            m["inputs"] = entry_inputs(e);
            m["total_size"] = static_cast<unsigned long long>(e["size"].as_int(0) + e["projector"]["size"].as_int(0));
            const std::string id = e["id"].as_string();
            for (const auto& im : installed)
                if (im.id == id)
                {
                    installed_fields(m, im);
                    // À jour : les fichiers de l'entrée, le projecteur qu'elle nomme compris.
                    const std::string wanted = e["projector"]["sha256"].as_string();
                    m["up_to_date"] = im.sha256 == e["sha256"].as_string()
                        && (wanted.empty() || im.projector_sha256 == wanted);
                }
            seen[id] = true;
            out.push_back(m);
        }
        for (const auto& im : installed)
        {
            if (seen.count(im.id) || embedding(im.descriptor)) continue;
            json m = im.descriptor.is_object() ? im.descriptor : json::object();
            m["id"] = im.id;
            if (!m.contains("name")) m["name"] = im.name;
            m["description"] = localized(m["description"], language);
            m["size"] = static_cast<unsigned long long>(im.size);
            m["source"] = im.source;
            installed_fields(m, im);
            out.push_back(m);
        }
        // L'entreprise qui a fait chaque modèle, vide quand ni son entrée ni son en-tête ne la nomme.
        for (auto& m : out.elements())
            if (!m["publisher"].is_string()) m["publisher"] = "";
        json embedders = json::array();
        for (const auto& im : installed)
        {
            if (!embedding(im.descriptor)) continue;
            json e = json::object();
            e["id"] = im.id;
            e["name"] = im.name;
            e["source"] = im.source;
            e["size"] = static_cast<unsigned long long>(im.size);
            e["inputs"] = text_inputs();
            // La licence et l'éditeur de son entrée dans le catalogue ou dans le manifeste, comme pour
            // les modèles de langage.
            e["publisher"] = "";
            for (const char* k : {"license", "publisher"})
                if (im.descriptor.contains(k)) e[k] = im.descriptor[k];
            embedders.push_back(e);
        }
        // Les projecteurs qui attendent leurs décodeurs.
        json pending = json::array();
        for (const auto& p : db_.projectors())
        {
            json j = json::object();
            j["name"] = p.name;
            j["size"] = static_cast<unsigned long long>(p.size);
            j["sha256"] = p.sha256;
            j["projector_type"] = p.descriptor["projector_type"];
            j["projection_width"] = p.descriptor["projection_width"];
            pending.push_back(j);
        }
        json result = json::object();
        result["models"] = out;
        result["embedding_models"] = embedders;
        result["pending_projectors"] = pending;
        result["downloads"] = downloads();
        const json cs = catalog_status();
        result["catalog_time"] = cs["time"];
        result["catalog"] = cs;
        return result;
    }

    json model_manager::downloads() const
    {
        std::lock_guard<std::mutex> lock(m_);
        json out = json::array();
        for (const auto& kv : jobs_)
        {
            const job& j = *kv.second;
            json d = json::object();
            d["id"] = j.id;
            d["state"] = j.state;
            d["received"] = static_cast<unsigned long long>(j.received.load());
            d["total"] = static_cast<unsigned long long>(j.total);
            d["speed"] = j.speed;
            if (!j.error.text.empty()) j.error.add_to(d);
            out.push_back(d);
        }
        return out;
    }

    bool model_manager::start_download(const std::string& id, user_error& error)
    {
        if (stopped_)
        {
            error.set("the service stops", "stopping");
            return false;
        }
        json entry;
        if (!catalog_entry(id, entry))
        {
            error.set("the model '" + id + "' is not in the catalog", "not_in_catalog").arg("m", id);
            return false;
        }
        const std::string name = entry["name"].str(id);
        if (!safe_id(id))
        {
            error.set("invalid model identifier", "catalog_entry").arg("m", name);
            return false;
        }
        // Un modèle livré avec Albireo garde son identifiant : un téléchargement de celui-ci remplacerait son entrée.
        installed_model holder;
        if (db_.find_model(id, holder) && holder.source == "builtin")
        {
            db_.remove_download_state(id);
            error.set("the model '" + id + "' comes with Albireo and is installed already", "builtin_installed")
                .arg("m", name);
            return false;
        }
        // Le fichier de l'entrée est celui qu'un modèle livré avec Albireo porte sous un autre nom.
        const std::vector<installed_model> installed = db_.models();
        const installed_model* carrier = builtin_carrier(installed, entry);
        if (carrier)
        {
            db_.remove_download_state(id);
            error.set("the model '" + id + "' comes with Albireo as " + carrier->name, "builtin_installed")
                .arg("m", carrier->name);
            return false;
        }
        std::shared_ptr<job> j(new job());
        j->id = id;
        j->entry = entry;
        // Le décodeur, et le projecteur d'un modèle qui lit les images, chacun dans son propre fichier partiel.
        const json& e = j->entry;
        job::output_file decoder;
        decoder.target = join_path(dir_, id + ".gguf");
        decoder.partial = decoder.target + ".partial";
        decoder.sha256 = e["sha256"].as_string();
        decoder.parts = e["files"];
        j->files.push_back(decoder);
        if (e["projector"].is_object())
        {
            job::output_file projector;
            projector.target = join_path(dir_, id + projector_suffix);
            projector.partial = projector.target + ".partial";
            projector.sha256 = e["projector"]["sha256"].as_string();
            projector.parts = e["projector"]["files"];
            if (projector.sha256.size() != 64 || projector.parts.size() == 0)
            {
                error.set("the catalog entry of '" + id + "' lists an invalid projector", "catalog_entry")
                    .arg("m", name);
                return false;
            }
            j->files.push_back(projector);
        }

        // Les parties, puis les segments des parties non compressées.
        const uint64_t segment = 64ull << 20;
        for (size_t fi = 0; fi < j->files.size(); ++fi)
        {
            job::output_file& out = j->files[fi];
            uint64_t offset = 0;
            size_t part = 0;
            for (const auto& f : static_cast<const json&>(out.parts).elements())
            {
                const std::string url = f["url"].as_string();
                // Une taille de partie au-delà de 1 Tio, ou négative, ferait des milliards de segments : l'entrée
                // est invalide.
                const long long given = f["size"].as_int(0);
                const uint64_t size = given > 0 && given <= (1ll << 40) ? static_cast<uint64_t>(given) : 0;
                const bool gz = f["compression"].as_string() == "gzip";
                if (url.empty() || size == 0)
                {
                    error.set("the catalog entry of '" + id + "' lists an invalid file", "catalog_entry")
                        .arg("m", name);
                    return false;
                }
                if (gz)
                {
                    std::unique_ptr<job::chunk> c(new job::chunk());
                    c->url = url;
                    c->from = 0;
                    c->length = static_cast<uint64_t>(std::max<long long>(0, f["compressed_size"].as_int(0)));
                    c->resource = c->length;
                    c->offset = offset;
                    c->gzip = true;
                    c->output = size;
                    c->part = part;
                    c->file = fi;
                    if (c->length == 0)
                    {
                        error.set("the catalog entry of '" + id + "' gives no compressed size", "catalog_entry")
                            .arg("m", name);
                        return false;
                    }
                    j->chunks.push_back(std::move(c));
                }
                else
                {
                    for (uint64_t s = 0; s < size; s += segment)
                    {
                        std::unique_ptr<job::chunk> c(new job::chunk());
                        c->url = url;
                        c->resource = size;
                        c->from = s;
                        c->length = std::min(segment, size - s);
                        c->offset = offset + s;
                        c->output = c->length;
                        c->part = part;
                        c->file = fi;
                        j->chunks.push_back(std::move(c));
                    }
                }
                offset += size;
                ++part;
            }
            // La taille du fichier, quand l'entrée la donne, est celle de ses parties.
            const long long declared = fi == 0 ? e["size"].as_int(-1) : e["projector"]["size"].as_int(-1);
            if ((declared >= 0 && static_cast<uint64_t>(declared) != offset) || offset == 0)
            {
                error.set("the sizes of the parts of '" + id + "' do not add up to the size of the model",
                    "catalog_entry").arg("m", name);
                return false;
            }
            out.size = offset;
            j->total += offset;
        }
        // Les fichiers du modèle, et la place gardée libre pour le système et les autres programmes.
        const uint64_t total = j->total;
        const uint64_t room = free_disk_space(dir_);
        const uint64_t reserve = total / 20 + (256ull << 20);
        if (room < total + reserve && !file_exists(j->files[0].partial))
        {
            error.set("not enough disk space for " + id + ": " + std::to_string((total + reserve) >> 20)
                + " MB needed (" + std::to_string(total >> 20) + " MB for the model, " + std::to_string(reserve >> 20)
                + " MB kept free), " + std::to_string(room >> 20) + " MB available", "disk_space")
                .arg("m", name).size("needed", total + reserve).size("model", total).size("reserve", reserve)
                .size("available", room);
            return false;
        }

        // Reprendre ce qu'une exécution précédente a reçu, quand les fichiers du catalogue sont les mêmes.
        const json saved = db_.download_state(id);
        bool partials = true;
        for (const auto& f : j->files) partials = partials && file_exists(f.partial);
        const std::string saved_projector = j->files.size() > 1 ? j->files[1].sha256 : std::string();
        if (saved.is_object() && saved["sha256"].as_string() == e["sha256"].as_string()
            && saved["projector_sha256"].as_string() == saved_projector && partials
            && saved["done"].size() == j->chunks.size())
        {
            for (size_t i = 0; i < j->chunks.size(); ++i)
            {
                job::chunk& c = *j->chunks[i];
                uint64_t d = static_cast<uint64_t>(saved["done"][i].as_int(0));
                if (c.gzip && d < c.length) d = 0;   // un flux compressé recommence depuis son début
                c.done = std::min(d, c.length);
                j->received += c.gzip ? (c.done == c.length ? c.output : 0) : c.done.load();
            }
        }

        std::lock_guard<std::mutex> lock(m_);
        // Le worker d'un téléchargement antérieur du modèle, qui peut encore prendre m_ dans ses
        // dernières étapes, est rejoint par le nouveau worker avant qu'il démarre.
        std::thread previous;
        auto it = jobs_.find(id);
        if (it != jobs_.end())
        {
            if (it->second->state == "downloading" || it->second->state == "verifying" || it->second->state == "queued")
                return true;
            previous = std::move(it->second->worker);
        }
        jobs_[id] = j;
        j->started = now_unix();
        auto failed = [this, j](const std::string& why) {
            {
                std::lock_guard<std::mutex> lock(m_);
                j->state = "error";
                j->error.set(why, "download_failed").arg("e", why);
            }
            log_error("models: download of " + j->id + ": " + why);
        };
        // Le worker précédent, gardé par le travail quand aucun thread ne peut le prendre, pour qu'il soit
        // tout de même rejoint plus tard.
        const auto before = std::make_shared<std::thread>(std::move(previous));
        try
        {
            j->worker = std::thread([this, j, failed, before]() {
                if (before->joinable()) before->join();
                // Aucune exception ne sort du thread : une base ou un disque qui lâche pendant le téléchargement le
                // termine en erreur.
                try
                {
                    run_job(j);
                }
                catch (const std::exception& e)
                {
                    failed(e.what());
                }
                catch (...)
                {
                    failed("the download fails");
                }
            });
        }
        catch (const std::exception& e)
        {
            // Un thread que le système refuse laisse le téléchargement en erreur, jamais en file d'attente.
            j->worker = std::move(*before);
            const std::string why = std::string("the download cannot start: ") + e.what();
            j->state = "error";
            j->error.set(why, "download_failed").arg("e", why);
        }
        return true;
    }

    void model_manager::note_failure(const std::string& id, const user_error& error)
    {
        std::shared_ptr<job> j(new job());
        j->id = id;
        j->state = "error";
        j->error = error;
        std::thread previous;
        {
            std::lock_guard<std::mutex> lock(m_);
            auto it = jobs_.find(id);
            if (it != jobs_.end())
            {
                const std::string& s = it->second->state;
                if (s == "downloading" || s == "verifying" || s == "queued") return;
                previous = std::move(it->second->worker);
            }
            jobs_[id] = j;
        }
        // Le worker du téléchargement qui s'est terminé, rejoint hors de m_, que ses dernières étapes peuvent
        // prendre.
        if (previous.joinable()) previous.join();
    }

    void model_manager::cancel_download(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = jobs_.find(id);
        const std::string state = it == jobs_.end() ? std::string() : it->second->state;
        if (it != jobs_.end())
        {
            it->second->abandoned = true;
            it->second->cancel = true;
        }
        db_.remove_download_state(id);
        if (state == "queued" || state == "downloading" || state == "verifying") return;
        /*
            Un téléchargement terminé par une erreur, ou laissé par un redémarrage, n'a pas de worker
            pour abandonner son fichier partiel : le fichier part ici, et l'entrée du téléchargement
            signale alors l'annulation.
        */
        if (state == "error")
        {
            it->second->state = "cancelled";
            it->second->error = user_error();
        }
        // Les fichiers partiels du décodeur et du projecteur.
        if (safe_id(id) && direct_path(dir_))
        {
            remove_file(join_path(dir_, id + ".gguf.partial"));
            remove_file(join_path(dir_, id + projector_suffix + std::string(".partial")));
        }
    }

    void model_manager::resume_pending()
    {
        for (const auto& id : db_.pending_downloads())
        {
            user_error error;
            if (!start_download(id, error)) log_warning("models: cannot resume " + id + ": " + error.text);
        }
    }

    namespace
    {
        // SHA-256 des octets [offset, offset + length) d'un fichier.
        std::string sha256_range(link_free_file& in, uint64_t offset, uint64_t length)
        {
            if (offset + length > in.size() || !in.seek(offset)) return std::string();
            sha256_stream s;
            std::vector<char> buf(1 << 20);
            while (length > 0)
            {
                const size_t n = static_cast<size_t>(std::min<uint64_t>(length, buf.size()));
                if (in.read(buf.data(), n) != static_cast<long long>(n)) return std::string();
                s.update(buf.data(), n);
                length -= n;
            }
            return s.hex();
        }

        // Comment se termine le transfert d'un segment d'un téléchargement (run_job()).
        enum class transfer_end
        {
            complete,
            failed,       // la requête échoue
            corrupt,      // une partie compressée qui ne se décompresse pas à sa taille
            other_size,   // le serveur annonce la ressource à une autre taille que le catalogue
            local         // le fichier partiel refuse les octets, ou la décompression ne peut pas démarrer
        };
    }

    bool model_manager::place(const std::string& from, const std::string& target)
    {
        const std::string before = file_identity(from);
        if (before.empty() || parent_path(target) != dir_ || !direct_path(dir_) || !rename_file(from, target))
            return false;
        // Le dossier remplacé par un lien entre la vérification et le déplacement se voit ici : target nomme
        // un autre fichier.
        return file_identity(target) == before;
    }

    void model_manager::run_job(std::shared_ptr<job> j)
    {
        auto set_state = [&](const std::string& s, const user_error& e = user_error()) {
            std::lock_guard<std::mutex> lock(m_);
            j->state = s;
            j->error = e;
        };
        // Les fichiers partiels ne partent que du dossier des modèles atteint sans passer par aucun lien.
        auto drop_partials = [&]() {
            if (!direct_path(dir_)) return;
            for (const auto& f : j->files) remove_file(f.partial);
        };
        // La progression est écrite sous m_, où cancel_download() la retire, pour qu'un téléchargement
        // annulé par l'utilisateur ne revienne jamais au prochain démarrage.
        auto save = [&]() {
            const json& e = j->entry;
            json st = json::object();
            st["sha256"] = e["sha256"];
            st["projector_sha256"] = j->files.size() > 1 ? j->files[1].sha256 : std::string();
            json done = json::array();
            for (const auto& c : j->chunks) done.push_back(static_cast<unsigned long long>(c->done.load()));
            st["done"] = done;
            std::lock_guard<std::mutex> lock(m_);
            if (!j->abandoned) db_.set_download_state(j->id, st);
        };
        const bool with_projector = j->files.size() > 1;
        log_info("models: downloading " + j->id + " (" + std::to_string(j->total >> 20) + " MB, "
            + std::to_string(j->chunks.size()) + " segments" + (with_projector ? ", with its projector)" : ")"));
        for (int round = 0; round < 2; ++round)
        {
            set_state("downloading");
            // Un fichier partiel par fichier du modèle, le dossier des modèles vérifié de nouveau juste
            // avant l'ouverture de chacun, qui vérifie ensuite son propre chemin.
            std::vector<std::unique_ptr<random_access_file>> outputs;
            for (const auto& f : j->files)
            {
                outputs.emplace_back(new random_access_file());
                if (!direct_path(dir_) || !outputs.back()->open(f.partial, f.size))
                {
                    set_state("error", user_error().set("cannot create " + f.partial, "download_file")
                        .arg("file", f.partial));
                    log_error("models: download of " + j->id + ": " + f.partial + " cannot be created in the models "
                        "folder reached through no link");
                    return;
                }
            }
            save();

            std::mutex chunk_m;
            size_t next = 0;
            std::atomic<bool> failed{false};
            std::string failure;
            // Le premier échec du téléchargement ; chunk_m est tenu.
            auto fail_with = [&](const std::string& why) {
                if (!failed) failure = why;
                failed = true;
            };
            // Les transferts s'arrêtent : le téléchargement est annulé, ou un segment l'a fait échouer.
            auto ended = [&]() { return j->cancel || failed; };
            // La pause avant la tentative numéro attempt, 2 s pour chaque tentative faite, qu'une fin des
            // transferts ou des téléchargements du service interrompt.
            auto pause = [&](int attempt) {
                for (int t = 0; t < 20 * attempt && !ended() && !http_cancelled(); ++t)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            };

            /*
                Un transfert du segment c depuis address dans son fichier partiel : sa plage depuis l'endroit où il
                s'est arrêté, ou toute la ressource d'une partie compressée, décompressée à mesure qu'elle arrive.
                Avec sized, une ressource que le serveur annonce à une autre taille que le catalogue s'arrête avant
                qu'aucun de ses octets ne soit écrit.  wrote reçoit les octets écrits ; une partie compressée qui
                échoue les retire de la progression, et recommence depuis son début.  reason dit pourquoi un
                transfert échoue, ou l'échec local (un fichier partiel qui refuse les octets).
            */
            auto fetch = [&](job::chunk& c, const std::string& address, bool sized, std::string& reason,
                uint64_t& wrote) {
                random_access_file& file = *outputs[c.file];
                reason.clear();
                wrote = 0;
                uint64_t total = 0;
                bool ok = false, unwritten = false, corrupt = false, other_size = false;
                // La taille de la ressource, que le serveur annonce avant ses premiers octets.
                auto resized = [&]() {
                    other_size = other_size || (sized && total != 0 && total != c.resource);
                    return other_size;
                };
                if (c.gzip)
                {
                    // Décompressée à mesure qu'elle arrive, écrite aux positions de la partie.
                    z_stream zs;
                    std::memset(&zs, 0, sizeof(zs));
                    if (inflateInit2(&zs, 15 + 32) != Z_OK)
                    {
                        reason = "the compressed part " + std::to_string(c.part) + " cannot be inflated";
                        return transfer_end::local;
                    }
                    uint64_t produced = 0;
                    std::vector<char> out(1 << 20);
                    ok = http_get_range(address, 0, -1, [&](const char* data, size_t n) {
                        if (j->cancel || resized()) return false;
                        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
                        zs.avail_in = static_cast<uInt>(n);
                        while (zs.avail_in > 0)
                        {
                            zs.next_out = reinterpret_cast<Bytef*>(out.data());
                            zs.avail_out = static_cast<uInt>(out.size());
                            const int r = inflate(&zs, Z_NO_FLUSH);
                            const size_t got = out.size() - zs.avail_out;
                            if ((r != Z_OK && r != Z_STREAM_END) || produced + got > c.output)
                            {
                                corrupt = true;
                                return false;
                            }
                            if (!file.write_at(c.offset + produced, out.data(), got))
                            {
                                unwritten = true;
                                return false;
                            }
                            produced += got;
                            j->received += got;
                            if (r == Z_STREAM_END) break;
                        }
                        return true;
                    }, reason, &total);
                    inflateEnd(&zs);
                    corrupt = corrupt || (ok && produced != c.output);
                    ok = ok && !corrupt;
                    if (ok) c.done = c.length;
                    else j->received -= produced;
                    wrote = produced;
                }
                else
                {
                    const uint64_t before = c.done;
                    ok = http_get_range(address, c.from + c.done, static_cast<int64_t>(c.from + c.length - 1),
                        [&](const char* data, size_t n) {
                            if (j->cancel || resized()) return false;
                            const uint64_t done = c.done;
                            const size_t take = static_cast<size_t>(std::min<uint64_t>(n, c.length - done));
                            if (!file.write_at(c.offset + done, data, take))
                            {
                                unwritten = true;
                                return false;
                            }
                            c.done = done + take;
                            j->received += take;
                            return true;
                        }, reason, &total);
                    ok = ok && c.done == c.length;
                    wrote = c.done - before;
                }
                if (ok) return transfer_end::complete;
                if (unwritten)
                {
                    reason = "cannot write " + j->files[c.file].partial;
                    return transfer_end::local;
                }
                if (corrupt) return transfer_end::corrupt;
                if (other_size) return transfer_end::other_size;
                if (reason.empty()) reason = "the answer ends too soon";
                return transfer_end::failed;
            };

            auto transfer = [&]() {
                for (;;)
                {
                    job::chunk* cp = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(chunk_m);
                        while (next < j->chunks.size() && j->chunks[next]->done >= j->chunks[next]->length) ++next;
                        if (next >= j->chunks.size() || failed || j->cancel) return;
                        cp = j->chunks[next++].get();
                    }
                    job::chunk& c = *cp;
                    std::string reason, error;
                    uint64_t wrote = 0;
                    transfer_end end = transfer_end::failed;
                    // Trois tentatives depuis l'adresse du catalogue, les mêmes plages ; les téléchargements
                    // arrêtés pour tout le service (il s'arrête, ou passe hors ligne) ne sont pas retentés.
                    bool ok = false;
                    for (int attempt = 0; attempt < 3 && !ok && !ended(); ++attempt)
                    {
                        pause(attempt);
                        if (ended()) break;
                        end = fetch(c, c.url, true, reason, wrote);
                        ok = end == transfer_end::complete;
                        if (end == transfer_end::local || (!ok && http_cancelled())) break;
                    }
                    if (!ok && !ended() && !http_cancelled())
                    {
                        error = end == transfer_end::local ? reason
                            : end == transfer_end::corrupt ? "the compressed file of " + without_queries(c.url) + " is corrupt"
                            : end == transfer_end::other_size ? "the file of " + without_queries(c.url)
                                + " does not have the size the catalog gives"
                            : without_queries(c.url) + " cannot be downloaded: " + reason;
                    }
                    if (!ok && !j->cancel)
                    {
                        std::lock_guard<std::mutex> lock(chunk_m);
                        fail_with(error.empty() ? "download failed" : error);
                        return;
                    }
                }
            };
            // Une exception d'un transfert, mémoire épuisée par exemple, fait échouer le téléchargement et ne sort
            // jamais de son thread.
            auto worker = [&]() {
                try
                {
                    transfer();
                }
                catch (const std::exception& e)
                {
                    std::lock_guard<std::mutex> lock(chunk_m);
                    fail_with(std::string("the download fails: ") + e.what());
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(chunk_m);
                    fail_with("the download fails");
                }
            };

            std::vector<std::thread> threads;
            try
            {
                for (int t = 0; t < 4; ++t) threads.emplace_back(worker);
            }
            catch (const std::exception& e)
            {
                // Un thread que le système refuse : ceux qui ont démarré finissent avec leur chunk, rejoints plus bas.
                std::lock_guard<std::mutex> lock(chunk_m);
                fail_with(std::string("the download cannot start its transfers: ") + e.what());
            }
            auto last_save = std::chrono::steady_clock::now();
            auto speed_t = std::chrono::steady_clock::now();
            uint64_t speed_bytes = j->received;
            // Progression, vitesse et enregistrement périodique pendant que les workers tournent.
            for (;;)
            {
                bool running = false;
                for (const auto& c : j->chunks) running = running || c->done < c->length;
                if (!running || failed || j->cancel) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double>(now - speed_t).count();
                if (dt >= 2.0)
                {
                    std::lock_guard<std::mutex> lock(m_);
                    const uint64_t r = j->received;
                    j->speed = r > speed_bytes ? (r - speed_bytes) / dt : 0.0;
                    speed_bytes = r;
                    speed_t = now;
                }
                if (std::chrono::duration<double>(now - last_save).count() >= 5.0)
                {
                    save();
                    last_save = now;
                }
            }
            for (auto& t : threads) t.join();
            for (auto& o : outputs) o->close();

            if (j->cancel)
            {
                set_state("cancelled");
                if (j->abandoned) drop_partials();
                log_info("models: download of " + j->id + " cancelled");
                return;
            }
            save();
            if (failed)
            {
                set_state("error", user_error().set(failure, "download_failed").arg("e", failure));
                log_error("models: download of " + j->id + " failed: " + failure);
                // Une annulation arrivée pendant l'échec du téléchargement abandonne tout de même ses fichiers
                // partiels.
                if (j->abandoned) drop_partials();
                return;
            }

            set_state("verifying");
            /*
                Chaque fichier écrit, lu sans passer par aucun lien depuis le dossier des modèles, est vérifié
                avec sa propre empreinte ; il est fermé avant d'être renommé ou supprimé, ce que son handle
                refuserait sous Windows.  Les parties dont l'empreinte propre échoue sont récupérées de
                nouveau, une fois.
            */
            std::vector<std::string> shas(j->files.size());
            bool all_match = true;
            bool any = false;
            for (size_t fi = 0; fi < j->files.size(); ++fi)
            {
                const job::output_file& f = j->files[fi];
                link_free_file written;
                std::string why;
                const bool readable = direct_path(dir_) && written.open(f.partial, dir_, why) && written.links() == 1;
                shas[fi] = readable ? sha256_open_file(written, &j->cancel) : std::string();
                if (j->cancel)
                {
                    // Arrêté pendant la vérification : les fichiers sont gardés et vérifiés de nouveau au
                    // prochain démarrage, sauf si le téléchargement lui-même a été annulé.
                    written.close();
                    set_state("cancelled");
                    if (j->abandoned) drop_partials();
                    log_info("models: download of " + j->id + " cancelled during its check");
                    return;
                }
                if (!readable)
                {
                    set_state("error", user_error().set("cannot read " + f.partial, "download_file")
                        .arg("file", f.partial));
                    log_error("models: download of " + j->id + ": " + f.partial + " is no longer the file written");
                    return;
                }
                if (shas[fi] == f.sha256) continue;
                all_match = false;
                uint64_t offset = 0;
                size_t part = 0;
                for (const auto& p : static_cast<const json&>(f.parts).elements())
                {
                    const uint64_t size = static_cast<uint64_t>(p["size"].as_int(0));
                    const std::string expected = p["sha256"].as_string();
                    if (round == 0 && !expected.empty() && sha256_range(written, offset, size) != expected)
                    {
                        for (auto& c : j->chunks)
                            if (c->file == fi && c->part == part)
                            {
                                j->received -= c->gzip ? c->output : c->done.load();
                                c->done = 0;
                            }
                        any = true;
                        log_warning("models: part " + std::to_string(part) + " of " + (fi == 0 ? std::string()
                            : std::string("the projector of ")) + j->id + " is corrupt, fetched again");
                    }
                    offset += size;
                    ++part;
                }
            }
            if (all_match)
            {
                /*
                    Les fichiers prennent leurs noms ensemble, le décodeur d'abord.  Quand l'un ne le peut pas,
                    ou quand la base ne peut pas enregistrer le modèle, ceux déjà placés reprennent leurs noms
                    partiels : le prochain démarrage trouve la progression complète, vérifie les fichiers et
                    les installe.
                */
                std::string error;
                size_t placed = 0;
                // Une version plus récente d'un modèle installé prend ses noms pendant que le moteur ne tient aucun
                // de ses fichiers.
                replacing(j->id, [&]() {
                    placed = 0;
                    while (placed < j->files.size() && place(j->files[placed].partial, j->files[placed].target))
                        ++placed;
                    return placed == j->files.size();
                });
                if (placed < j->files.size() || !install_file(j->id, j->files[0].target, j->entry, shas[0], "catalog",
                        with_projector ? j->files[1].target : std::string(), with_projector ? shas[1] : std::string(),
                        error))
                {
                    for (size_t i = 0; i < placed; ++i) place(j->files[i].target, j->files[i].partial);
                    if (error.empty())
                        error = "cannot install " + j->files[std::min(placed, j->files.size() - 1)].target;
                    set_state("error", user_error().set(error, "download_install").arg("e", error));
                    log_error("models: download of " + j->id + ": " + error);
                    return;
                }
                db_.remove_download_state(j->id);
                if (with_projector) drop_pending_copy(shas[1]);
                set_state("done");
                log_info("models: " + j->id + " installed" + (with_projector ? ", with its projector" : ""));
                return;
            }
            if (!any) break;
        }
        set_state("error", user_error().set("the downloaded file does not match the hash of the catalog",
            "download_hash"));
        log_error("models: " + j->id + " does not match its hash");
        db_.remove_download_state(j->id);
        drop_partials();
    }

    bool model_manager::install_file(const std::string& id, const std::string& file, const json& descriptor,
        const std::string& sha, const std::string& source, const std::string& projector,
        const std::string& projector_sha, std::string& error)
    {
        installed_model m;
        m.id = id;
        m.name = descriptor["name"].str(id);
        m.file = file;
        m.size = file_size(file);
        m.sha256 = sha;
        m.source = source;
        m.installed_at = now_unix();
        m.verified_at = now_unix();
        m.descriptor = descriptor;
        m.projector = projector;
        m.projector_size = projector.empty() ? 0 : file_size(projector);
        m.projector_sha256 = projector.empty() ? std::string() : projector_sha;
        if (m.size == 0)
        {
            error = file + " is empty";
            return false;
        }
        if (!projector.empty() && m.projector_size == 0)
        {
            error = projector + " is empty";
            return false;
        }
        try
        {
            // L'entrée d'un modèle livré avec Albireo n'est jamais remplacée par un autre fichier.
            installed_model existing;
            if (source != "builtin" && db_.find_model(id, existing) && existing.source == "builtin")
            {
                error = "the model " + id + " comes with Albireo";
                return false;
            }
            db_.put_model(m);
        }
        catch (const std::exception& e)
        {
            // Une base qui ne peut pas être écrite, sur un disque plein par exemple.
            error = "the model cannot be recorded: " + std::string(e.what());
            return false;
        }
        // Le modèle est installé quelles que soient les fonctions qu'il reçoit.
        try
        {
            if (installed_cb_) installed_cb_(m);
        }
        catch (const std::exception& e)
        {
            log_error("models: " + id + " is installed, its functions are not given: " + e.what());
        }
        return true;
    }

    bool model_manager::remove(const std::string& id, std::string& error)
    {
        installed_model m;
        if (!db_.find_model(id, m))
        {
            error = "the model '" + id + "' is not installed";
            return false;
        }
        // Un fichier du dossier des modèles n'est supprimé que par le dossier atteint sans lien.
        for (const std::string& f : { m.file, m.projector })
            if (!f.empty() && parent_path(f) == dir_ && !direct_path(dir_))
            {
                error = "the models folder " + dir_ + " now leads elsewhere through a link";
                return false;
            }
        if (file_exists(m.file) && !remove_file(m.file))
        {
            error = "cannot delete " + m.file + " (in use?)";
            return false;
        }
        // Le projecteur va avec son décodeur.
        if (!m.projector.empty() && file_exists(m.projector) && !remove_file(m.projector))
            log_warning("models: the projector " + m.projector + " of " + id + " cannot be deleted");
        db_.remove_model(id);
        log_info("models: " + id + " removed");
        return true;
    }

    json model_manager::verify(const std::string& id)
    {
        json r = json::object();
        r["id"] = id;
        installed_model m;
        if (!db_.find_model(id, m))
        {
            r["error"] = "not installed";
            return r;
        }
        // La vérification s'arrête aussitôt quand le service s'arrête.
        const std::string sha = sha256_file(m.file, nullptr, &stopped_);
        const std::string projector_sha = m.projector.empty() ? std::string()
            : sha256_file(m.projector, nullptr, &stopped_);
        if (stopped_)
        {
            r["error"] = "the service stops";
            return r;
        }
        const bool decoder_match = !sha.empty() && sha == m.sha256;
        const bool projector_match = m.projector.empty()
            || (!projector_sha.empty() && projector_sha == m.projector_sha256);
        r["sha256"] = sha;
        if (!m.projector.empty())
        {
            r["projector_sha256"] = projector_sha;
            r["projector_match"] = projector_match;
        }
        r["local_match"] = decoder_match && projector_match;
        json found;
        if (catalog_entry(id, found))
        {
            const json& entry = found;
            r["catalog_sha256"] = entry["sha256"];
            r["catalog_match"] = sha == entry["sha256"].as_string();
            if (entry["projector"].is_object())
            {
                r["catalog_projector_sha256"] = entry["projector"]["sha256"];
                r["catalog_projector_match"] = projector_sha == entry["projector"]["sha256"].as_string();
            }
        }
        if (decoder_match && projector_match) db_.set_verified(id, now_unix());
        return r;
    }

    bool model_manager::installed_with_hash(const std::string& sha, installed_model& m)
    {
        bool found = false;
        for (const auto& x : db_.models())
        {
            if (x.sha256 != sha || !file_exists(x.file)) continue;
            if (!found || x.source == "builtin") m = x;
            found = true;
        }
        return found;
    }

    bool model_manager::projector_with_hash(const std::string& sha, std::string& holder, std::string& path)
    {
        holder.clear();
        path.clear();
        for (const auto& x : db_.models())
            if (!x.projector.empty() && x.projector_sha256 == sha && file_exists(x.projector))
            {
                holder = x.id;
                path = x.projector;
                return true;
            }
        for (const auto& p : db_.projectors())
            if (p.sha256 == sha && file_exists(p.file))
            {
                path = p.file;
                return true;
            }
        return false;
    }

    std::string model_manager::catalog_projector(const std::string& decoder_sha) const
    {
        std::lock_guard<std::mutex> lock(m_);
        const json& catalog = catalog_;
        for (const auto& e : catalog["models"].elements())
            if (e["sha256"].as_string() == decoder_sha) return e["projector"]["sha256"].as_string();
        return std::string();
    }

    void model_manager::drop_pending_copy(const std::string& sha)
    {
        try
        {
            for (const auto& p : db_.projectors())
            {
                if (p.sha256 != sha) continue;
                if (parent_path(p.file) == dir_ && direct_path(dir_)) remove_file(p.file);
                db_.remove_projector(sha);
                log_info("models: the pending projector " + p.name + " is installed with its model");
            }
        }
        catch (const std::exception& e)
        {
            log_warning(std::string("models: the pending projectors cannot be read: ") + e.what());
        }
    }

    bool model_manager::take_pending(const pending_projector& pending, const std::string& target)
    {
        if (parent_path(pending.file) != dir_ || file_size(pending.file) != pending.size
            || !place(pending.file, target))
        {
            log_warning("models: the pending projector " + pending.file + " cannot be taken");
            return false;
        }
        try
        {
            db_.remove_projector(pending.sha256);
        }
        catch (const std::exception& e)
        {
            place(target, pending.file);
            log_warning("models: the pending projector " + pending.file + " cannot be taken: " + e.what());
            return false;
        }
        return true;
    }

    void model_manager::return_pending(const pending_projector& pending, const std::string& target)
    {
        if (!place(target, pending.file)) return;
        try
        {
            db_.put_projector(pending);
        }
        catch (const std::exception& e)
        {
            log_warning("models: the pending projector " + pending.file + " cannot be recorded again: " + e.what());
        }
    }

    std::string model_manager::free_identifier(const std::string& id, bool update)
    {
        installed_model holder;
        if (!db_.find_model(id, holder) || (update && holder.source == "catalog")) return id;
        for (int n = 1; n < 100; ++n)
        {
            const std::string candidate = id + "-import" + (n > 1 ? "-" + std::to_string(n) : std::string());
            if (!db_.find_model(candidate, holder)) return candidate;
        }
        return id + "-import-" + random_hex(8);
    }

    struct model_manager::import_item
    {
        std::string path;           // où se trouve le fichier
        std::string base;           // le dossier de confiance d'où il est lu, vide pour la racine de son disque
        std::string name;           // le nom sous lequel il est arrivé
        std::string sha256;
        json description;           // describe_gguf()
        bool projector = false;
        std::unique_ptr<link_free_file> file;   // ouvert depuis inspect() jusqu'à ce que le fichier soit placé
        std::string identity;
        // Le résultat : "imported", "already", "pending" ou "error" ; le modèle concerné ; l'erreur.
        std::string result;
        std::string id;
        std::string error;
    };

    namespace
    {
        bool lacks_projector(const installed_model& m)
        {
            return m.projector.empty() || !file_exists(m.projector);
        }

        bool embedding_entry(const json& d)
        {
            if (d["embedding"].as_bool(false)) return true;
            for (const auto& r : d["roles"].elements())
                if (r.as_string() == "embedding") return true;
            return false;
        }
    }

    bool model_manager::inspect(import_item& item, const std::string& known_sha)
    {
        /*
            Le fichier lui-même, atteint sans passer par aucun lien depuis base et avec un nom unique :
            un lien ou un second nom mènerait le service, qui lit avec les droits du système, au
            fichier d'un autre compte.
        */
        item.file.reset(new link_free_file());
        std::string why;
        if (!item.file->open(item.path, item.base, why))
        {
            item.error = unreadable_reason(why);
            return false;
        }
        if (item.file->links() != 1)
        {
            item.file->close();
            item.error = "the file has several names (hard links), which imports never read";
            return false;
        }
        item.identity = item.file->identity();
        item.sha256 = known_sha.empty() ? sha256_open_file(*item.file, &stopped_) : known_sha;
        if (item.sha256.empty())
        {
            item.file->close();
            item.error = stopped_ ? "the service stops" : "unreadable";
            return false;
        }
        item.description = describe_gguf(item.path);
        // Un projecteur par son en-tête, ou par l'empreinte qu'une entrée du catalogue donne à son projecteur.
        bool listed = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            const json& catalog = catalog_;
            for (const auto& e : catalog["models"].elements())
                listed = listed || e["projector"]["sha256"].as_string() == item.sha256;
        }
        const json& description = item.description;
        item.projector = description["projector"].as_bool(false) || listed;
        return true;
    }

    namespace
    {
        pairing_file pairing_of_pending(const pending_projector& p)
        {
            pairing_file f;
            f.name = p.name;
            f.sha256 = p.sha256;
            f.description = p.descriptor;
            return f;
        }
    }

    void model_manager::import_items(std::vector<import_item>& items, bool move)
    {
        auto fail = [](import_item& it, const std::string& error) {
            it.result = "error";
            it.error = error;
            if (it.file) it.file->close();
        };
        auto pairing_of = [this](const import_item& it) {
            pairing_file f;
            f.name = file_name(it.name);
            f.sha256 = it.sha256;
            f.description = it.description;
            if (!it.projector) f.expected_projector = catalog_projector(it.sha256);
            return f;
        };
        // Les fichiers qui n'ont pas pu être lus, les décodeurs et les projecteurs.
        std::vector<size_t> decoders, projectors;
        for (size_t i = 0; i < items.size(); ++i)
        {
            import_item& it = items[i];
            if (!it.error.empty())
                fail(it, it.error);
            else if (it.projector && !static_cast<const json&>(it.description)["vision"].as_bool(true))
                fail(it, "the projector holds no vision encoder, and Albireo reads images only");
            else if (it.projector)
                projectors.push_back(i);
            else
                decoders.push_back(i);
        }
        // Un projecteur déjà installé, ou en attente, n'est pas recopié ; avec move, son fichier quitte le
        // dossier.
        std::vector<size_t> free_projectors;
        for (size_t i : projectors)
        {
            import_item& p = items[i];
            std::string holder, path;
            if (!projector_with_hash(p.sha256, holder, path))
            {
                free_projectors.push_back(i);
                continue;
            }
            p.result = "already";
            p.id = holder;
            p.file->close();
            if (move && p.identity != file_identity(path)) remove_file(p.path);
            log_info("models: " + p.name + " holds the projector " + (holder.empty() ? std::string("waiting for its "
                "decoder") : "of " + holder) + ", installed already: not copied again");
        }
        // Les projecteurs des fichiers vont avec leurs décodeurs.
        std::vector<pairing_file> dp, pp;
        for (size_t i : decoders) dp.push_back(pairing_of(items[i]));
        for (size_t i : free_projectors) pp.push_back(pairing_of(items[i]));
        const std::vector<int> pairs = pair_projectors(dp, pp);
        std::vector<int> projector_of(items.size(), -1);
        for (size_t k = 0; k < pairs.size(); ++k)
            if (pairs[k] >= 0)
                projector_of[decoders[static_cast<size_t>(pairs[k])]] = static_cast<int>(free_projectors[k]);

        for (size_t i : decoders)
        {
            if (stopped_) break;
            import_item& d = items[i];
            import_item* p = projector_of[i] >= 0 ? &items[static_cast<size_t>(projector_of[i])] : nullptr;
            std::string error;
            // Un fichier qui répète un modèle installé, y compris un modèle livré avec Albireo, n'est pas recopié.
            installed_model same;
            if (installed_with_hash(d.sha256, same))
            {
                d.result = "already";
                d.id = same.id;
                d.file->close();
                if (move && d.identity != file_identity(same.file)) remove_file(d.path);
                log_info("models: " + d.name + " holds " + same.id + ", installed already: not copied again"
                    + (move ? ", removed from its folder" : ""));
                // Son projecteur vient du dossier quand le modèle installé n'en a pas.
                if (p && lacks_projector(same) && same.source != "builtin" && parent_path(same.file) == dir_)
                {
                    if (attach_projector(*p, same, move, error))
                    {
                        p->result = "imported";
                        p->id = same.id;
                    }
                    else
                    {
                        fail(*p, error);
                    }
                }
                continue;
            }
            // Sans projecteur parmi les fichiers, un projecteur en attente qui va avec lui.
            pending_projector pending;
            bool with_pending = false;
            if (!p) with_pending = pending_for(d, pending);
            if (import_decoder(d, p, with_pending ? &pending : nullptr, move, error))
            {
                d.result = "imported";
                if (p)
                {
                    p->result = "imported";
                    p->id = d.id;
                }
                log_info("models: imported " + d.id + " from " + d.name + (p ? ", with the projector " + p->name
                    : with_pending ? ", with the projector " + pending.name : std::string()));
            }
            else
            {
                fail(d, error);
                if (p) fail(*p, "its decoder " + file_name(d.name) + " cannot be installed: " + error);
            }
        }
        // Les autres projecteurs vont à un décodeur installé qui n'en a pas, ou attendent le leur.
        for (size_t i : free_projectors)
        {
            if (stopped_) break;
            import_item& p = items[i];
            if (!p.result.empty()) continue;
            std::string error;
            installed_model target;
            if (installed_for(p, target))
            {
                if (attach_projector(p, target, move, error))
                {
                    p.result = "imported";
                    p.id = target.id;
                }
                else
                {
                    fail(p, error);
                }
            }
            else if (keep_pending(p, move, error))
            {
                p.result = "pending";
            }
            else
            {
                fail(p, error);
            }
        }
        for (auto& it : items)
            if (it.file) it.file->close();
    }

    json model_manager::import_result(const std::vector<import_item>& items)
    {
        json imported = json::array();
        json already = json::array();
        json pending = json::array();
        json errors = json::array();
        json files = json::array();
        auto add = [](json& list, const std::string& v) {
            for (const auto& x : static_cast<const json&>(list).elements())
                if (x.as_string() == v) return;
            list.push_back(v);
        };
        for (const auto& it : items)
        {
            json f = json::object();
            f["file"] = it.name;
            f["result"] = it.result;
            if (!it.id.empty()) f["id"] = it.id;
            files.push_back(f);
            if (it.result == "imported") add(imported, it.id);
            else if (it.result == "already" && !it.id.empty()) add(already, it.id);
            // Un projecteur qui attend, reçu de nouveau.
            else if (it.result == "already" || it.result == "pending") add(pending, file_name(it.name));
            else if (it.result == "error") errors.push_back(it.name + ": " + it.error);
        }
        json result = json::object();
        result["imported"] = imported;
        result["already"] = already;
        result["pending"] = pending;
        result["errors"] = errors;
        result["files"] = files;
        return result;
    }

    bool model_manager::replacing(const std::string& id, const std::function<bool()>& work)
    {
        installed_model m;
        if (id.empty() || !replace_cb_ || !db_.find_model(id, m)) return work();
        return replace_cb_(id, work);
    }

    bool model_manager::put_file(link_free_file& source, const std::string& path, const std::string& base,
        const std::string& sha, bool move, const std::string& target, placement& how, std::string& error,
        const std::string& id)
    {
        how = placement();
        // Un fichier du dossier des modèles à son propre nom, enregistré de nouveau là où il est (une base
        // recommencée).
        how.in_place = !source.identity().empty() && file_identity(target) == source.identity();
        // Le renommage qui donne au fichier son nom, par-dessus le fichier d'un modèle installé via replacing().
        auto rename_to_target = [&](const std::string& from) {
            return replacing(id, [&]() { return place(from, target); });
        };
        /*
            Déplacé quand c'est demandé et que le dossier des modèles est sur le même volume, comme
            le fichier vérifié plus haut ; sinon copié depuis le fichier ouvert plus haut, par un
            nouveau nom temporaire, et vérifié avec l'empreinte pendant l'écriture.
        */
        if (move && !how.in_place)
        {
            const std::string identity = source.identity();
            source.close();
            how.moved = file_identity(path) == identity && rename_to_target(path);
            std::string why;
            if (!how.moved && !source.open(path, base, why))
            {
                error = unreadable_reason(why);
                return false;
            }
        }
        how.copied = !how.moved && !how.in_place;
        if (how.copied)
        {
            const std::string tmp = join_path(dir_, ".import-" + random_hex(16) + ".partial");
            if (copy_open_file(source, tmp, sha, &stopped_).empty() || !rename_to_target(tmp))
            {
                remove_file(tmp);
                source.close();
                how.copied = false;
                error = stopped_ ? "the service stops" : "copy failed";
                return false;
            }
        }
        source.close();
        return true;
    }

    void model_manager::undo_put(const std::string& path, const std::string& target, const placement& how)
    {
        if (how.moved) rename_file(target, path);
        else if (how.copied) remove_file(target);
    }

    bool model_manager::pending_for(import_item& d, pending_projector& found)
    {
        std::vector<pending_projector> list;
        for (const auto& p : db_.projectors())
            if (parent_path(p.file) == dir_ && file_size(p.file) == p.size) list.push_back(p);
        if (list.empty()) return false;
        // L'en-tête d'un fichier d'un paquet, lu ici seulement, une fois qu'il existe des projecteurs en attente.
        if (!d.description.is_object()) d.description = describe_gguf(d.path);
        pairing_file f;
        f.name = file_name(d.name);
        f.sha256 = d.sha256;
        f.description = d.description;
        f.expected_projector = catalog_projector(d.sha256);
        std::vector<pairing_file> pp;
        for (const auto& p : list) pp.push_back(pairing_of_pending(p));
        const std::vector<int> pairs = pair_projectors(std::vector<pairing_file>(1, f), pp);
        for (size_t k = 0; k < pairs.size(); ++k)
            if (pairs[k] == 0)
            {
                found = list[k];
                return true;
            }
        return false;
    }

    bool model_manager::installed_for(const import_item& p, installed_model& found)
    {
        // Les décodeurs installés du dossier des modèles qui n'ont pas de projecteur et peuvent lire les images.
        std::vector<installed_model> candidates;
        std::vector<pairing_file> dp;
        for (const auto& m : db_.models())
        {
            if (m.source == "builtin" || parent_path(m.file) != dir_ || !lacks_projector(m) || !file_exists(m.file)
                || embedding_entry(m.descriptor))
                continue;
            pairing_file f;
            f.name = file_name(m.file);
            f.sha256 = m.sha256;
            const json& d = m.descriptor;
            f.expected_projector = d["projector"]["sha256"].as_string();
            if (f.expected_projector.empty()) f.expected_projector = catalog_projector(m.sha256);
            // Une entrée qui ne nomme ni projecteur ni architecture qui lit les images est écartée sans être lue.
            if (f.expected_projector.empty() && d["arch"].is_string()
                && !d["reads_images"].as_bool(reads_images(d["arch"].as_string())))
                continue;
            f.description = describe_gguf(m.file);
            candidates.push_back(m);
            dp.push_back(f);
        }
        if (candidates.empty()) return false;
        pairing_file f;
        f.name = file_name(p.name);
        f.sha256 = p.sha256;
        f.description = p.description;
        const std::vector<int> pairs = pair_projectors(dp, std::vector<pairing_file>(1, f));
        if (pairs[0] < 0) return false;
        found = candidates[static_cast<size_t>(pairs[0])];
        return true;
    }

    bool model_manager::import_decoder(import_item& d, import_item* p, const pending_projector* pending, bool move,
        std::string& error)
    {
        // Une entrée du catalogue avec cette empreinte donne l'identifiant et la description.
        json entry;
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            const json& catalog = catalog_;
            for (const auto& e : catalog["models"].elements())
                if (e["sha256"].as_string() == d.sha256)
                {
                    entry = e;
                    known = true;
                }
        }
        std::string id;
        if (known)
        {
            id = static_cast<const json&>(entry)["id"].as_string();
        }
        else
        {
            if (d.description.contains("error"))
            {
                error = d.description["error"].as_string();
                return false;
            }
            entry = d.description;
            std::string name = file_name(d.name);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".gguf") name = name.substr(0, name.size() - 5);
            for (char& c : name)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) c = '-';
            // Un modèle d'Albireo garde son identifiant.
            const std::string own = static_cast<const json&>(entry)["albireo_id"].as_string();
            id = own.empty() ? name : own;
            entry.erase("albireo_id");
            // Les champs de l'en-tête que seul l'appariement lit.
            entry.erase("gguf_name");
            entry.erase("projection_width");
            /*
                Une description annexe écrite à côté du fichier, lue seulement dans le dossier d'import,
                où seuls les administrateurs placent des fichiers, et sans passer par aucun lien.
            */
            std::string side;
            const std::string& f = d.path;
            if (f.size() > 5 && f.substr(f.size() - 5) == ".gguf" && d.base == join_path(data_dir(), "import")
                && read_file_link_free(f.substr(0, f.size() - 5) + ".json", d.base, side, 1u << 20))
            {
                const json s = json::parse_or_null(side);
                for (const auto& kv : s.items())
                    if (kv.first != "id" && kv.first != "files" && kv.first != "sha256" && kv.first != "projector")
                        entry[kv.first] = kv.second;
            }
        }
        /*
            Un fichier ne prend jamais l'identifiant d'un autre modèle installé, y compris un modèle livré avec
            Albireo ; une version plus récente, venue du catalogue, d'un modèle du catalogue le remplace.
        */
        id = free_identifier(id, known);
        entry["id"] = id;
        if (!safe_id(id))
        {
            error = "invalid identifier";
            return false;
        }
        // Le dossier des modèles doit encore être atteint sans lien avant que le service y écrive.
        if (!direct_path(dir_))
        {
            error = "the models folder " + dir_ + " now leads elsewhere through a link";
            return false;
        }
        const std::string target = join_path(dir_, id + ".gguf");
        placement dh;
        if (!put_file(*d.file, d.path, d.base, d.sha256, move, target, dh, error, id)) return false;
        // Le projecteur prend le nom du modèle : depuis les fichiers, ou depuis les projecteurs en attente.
        std::string projector, projector_sha;
        placement ph;
        if (p)
        {
            projector = join_path(dir_, id + projector_suffix);
            projector_sha = p->sha256;
            if (!put_file(*p->file, p->path, p->base, p->sha256, move, projector, ph, error, id))
            {
                undo_put(d.path, target, dh);
                return false;
            }
        }
        else if (pending)
        {
            projector = join_path(dir_, id + projector_suffix);
            projector_sha = pending->sha256;
            // Un projecteur en attente qui ne peut pas être pris reste en attente : le modèle ne lit que du texte.
            if (!take_pending(*pending, projector))
            {
                projector.clear();
                projector_sha.clear();
                pending = nullptr;
            }
        }
        if (!install_file(id, target, entry, d.sha256, known ? "catalog" : "import", projector, projector_sha, error))
        {
            // Un modèle que la base ne peut pas enregistrer quitte le dossier des modèles : les fichiers
            // déplacés retournent d'où ils venaient, les copies sont supprimées, un projecteur en attente
            // attend de nouveau.
            undo_put(d.path, target, dh);
            if (p) undo_put(p->path, projector, ph);
            if (pending) return_pending(*pending, projector);
            return false;
        }
        d.id = id;
        // Les sources des copies partent une fois le modèle enregistré.
        if (move && dh.copied) remove_file(d.path);
        if (p && move && ph.copied) remove_file(p->path);
        return true;
    }

    bool model_manager::attach_projector(import_item& p, const installed_model& m, bool move, std::string& error)
    {
        if (!direct_path(dir_))
        {
            error = "the models folder " + dir_ + " now leads elsewhere through a link";
            return false;
        }
        const std::string target = join_path(dir_, m.id + projector_suffix);
        placement how;
        // Le modèle n'a pas de projecteur : aucun programme ne tient de fichier à ce nom, et le modèle reste
        // dans le moteur.
        if (!put_file(*p.file, p.path, p.base, p.sha256, move, target, how, error, std::string())) return false;
        installed_model updated = m;
        updated.projector = target;
        updated.projector_size = file_size(target);
        updated.projector_sha256 = p.sha256;
        try
        {
            db_.put_model(updated);
        }
        catch (const std::exception& e)
        {
            undo_put(p.path, target, how);
            error = "the projector cannot be recorded: " + std::string(e.what());
            return false;
        }
        if (move && how.copied) remove_file(p.path);
        log_info("models: " + m.id + " receives the projector " + file_name(p.name) + " and reads images");
        // Le modèle, déjà chargé, se recharge avec son projecteur.
        try
        {
            if (installed_cb_) installed_cb_(updated);
        }
        catch (const std::exception& e)
        {
            log_error("models: " + m.id + " has its projector, the service is not told: " + e.what());
        }
        return true;
    }

    bool model_manager::keep_pending(import_item& p, bool move, std::string& error)
    {
        if (p.description.contains("error"))
        {
            error = p.description["error"].as_string();
            return false;
        }
        if (!direct_path(dir_))
        {
            error = "the models folder " + dir_ + " now leads elsewhere through a link";
            return false;
        }
        const std::string target = join_path(dir_, ".pending-" + p.sha256.substr(0, 16) + projector_suffix);
        placement how;
        if (!put_file(*p.file, p.path, p.base, p.sha256, move, target, how, error, std::string())) return false;
        pending_projector pp;
        pp.sha256 = p.sha256;
        pp.file = target;
        pp.size = file_size(target);
        pp.name = file_name(p.name);
        pp.descriptor = p.description;
        pp.received_at = now_unix();
        try
        {
            db_.put_projector(pp);
        }
        catch (const std::exception& e)
        {
            undo_put(p.path, target, how);
            error = "the projector cannot be recorded: " + std::string(e.what());
            return false;
        }
        if (move && how.copied) remove_file(p.path);
        log_info("models: the projector " + pp.name + " waits for its decoder, in " + target);
        return true;
    }

    std::vector<installed_model> model_manager::register_builtin(const std::string& dir,
        std::vector<installed_model>& removed)
    {
        std::vector<installed_model> added;
        std::string text;
        const json manifest = read_file(join_path(dir, builtin_manifest_name), text, 16u << 20)
            ? json::parse_or_null(text) : json();
        auto plain = [](const std::string& name) {
            return !name.empty() && name.find_first_of("/\\:") == std::string::npos;
        };
        std::set<std::string> listed;
        for (const auto& e : manifest["models"].elements())
        {
            const std::string id = e["id"].as_string();
            const std::string name = e["file"].as_string();
            const std::string file = join_path(dir, name);
            if (!safe_id(id) || !plain(name) || !file_exists(file))
            {
                log_warning("models: the model " + id + " of " + builtin_manifest_name + " is missing from " + dir);
                continue;
            }
            listed.insert(id);
            json d = e;
            d.erase("file");
            d.erase("offset");
            d["builtin"] = true;
            // Le projecteur d'un modèle qui lit les images se trouve à côté de lui ; sans lui, le modèle ne lit
            // que du texte.
            std::string projector, projector_sha;
            const json& p = e["projector"];
            if (p.is_object())
            {
                json pd = p;
                pd.erase("file");
                pd.erase("offset");
                d["projector"] = pd;
                const std::string pname = p["file"].as_string();
                if (plain(pname) && file_exists(join_path(dir, pname)))
                {
                    projector = join_path(dir, pname);
                    projector_sha = p["sha256"].as_string();
                }
                else
                {
                    log_warning("models: the projector of " + id + " is missing from " + dir + ": it reads text only");
                }
            }
            installed_model existing;
            const bool known = db_.find_model(id, existing);
            if (known && existing.source == "builtin" && existing.file == file
                && existing.sha256 == e["sha256"].as_string() && existing.descriptor == d
                && existing.projector == projector && existing.projector_sha256 == projector_sha)
                continue;
            installed_model m;
            m.id = id;
            m.name = e["name"].str(id);
            m.file = file;
            m.size = file_size(file);
            m.sha256 = e["sha256"].as_string();
            m.source = "builtin";
            m.installed_at = known ? existing.installed_at : now_unix();
            m.projector = projector;
            m.projector_size = projector.empty() ? 0 : file_size(projector);
            m.projector_sha256 = projector_sha;
            // L'installateur a vérifié les empreintes des fichiers qu'il a écrits.
            m.verified_at = known && existing.sha256 == m.sha256 && existing.projector_sha256 == m.projector_sha256
                ? existing.verified_at : now_unix();
            m.descriptor = d;
            db_.put_model(m);
            if (!known) added.push_back(m);
            log_info("models: " + id + " comes with Albireo, in " + file + (projector.empty() ? std::string()
                : ", with its projector " + projector));
        }
        for (const auto& m : db_.models())
        {
            if (m.source != "builtin" || listed.count(m.id)) continue;
            db_.remove_model(m.id);
            removed.push_back(m);
            log_info("models: " + m.id + " no longer comes with Albireo");
        }
        return added;
    }

    json model_manager::import_path(const std::string& path, const std::string& base, bool move)
    {
        std::vector<std::string> files;
        // Le dossier d'import lui-même, ou un dossier sous lui atteint sans passer par aucun lien.
        if (path == base ? dir_exists(path) : link_free_folder(path, base))
        {
            // Le dossier et son sous-dossier models, la disposition d'un paquet hors ligne ; inspect()
            // refuse les liens parmi eux.
            for (const std::string& dir : { path, join_path(path, "models") })
            {
                if (dir != path && !link_free_folder(dir, base)) continue;
                for (const auto& n : list_dir(dir))
                    if (n.size() > 5 && n.substr(n.size() - 5) == ".gguf") files.push_back(join_path(dir, n));
            }
        }
        else
        {
            files.push_back(path);
        }
        std::vector<import_item> items;
        for (const auto& f : files)
        {
            if (stopped_) break;
            import_item it;
            it.path = f;
            it.base = base;
            it.name = f;
            inspect(it, std::string());
            items.push_back(std::move(it));
        }
        import_items(items, move);
        return import_result(items);
    }

    bool model_manager::bundle_file(const std::string& folder, const json& parts, const std::string& fallback,
        const std::string& sha, uint64_t size, import_item& out)
    {
        // Les noms sous lesquels tools/make_offline_bundle.py place un fichier : aucun autre fichier n'est lu.
        std::vector<std::string> names;
        if (parts.size() == 1) names.push_back(file_name(parts[0]["url"].as_string()));
        names.push_back(fallback);
        out.file.reset(new link_free_file());
        std::string why, refused;
        for (const std::string& dir : { join_path(folder, "models"), folder })
            for (const auto& name : names)
            {
                if (out.file->is_open() || !plain_name(name)) continue;
                const std::string p = join_path(dir, name);
                if (out.file->open(p, std::string(), why)) out.path = p;
                else if (why == "link" && refused.empty()) refused = p;
            }
        if (!out.file->is_open())
        {
            if (!refused.empty())
            {
                out.name = refused;
                out.error = unreadable_reason("link");
            }
            return false;
        }
        out.name = out.path;
        out.base = std::string();
        out.identity = out.file->identity();
        // Sa taille d'abord, puis son empreinte, avant toute copie.
        if (out.file->size() != size || out.file->links() != 1 || sha256_open_file(*out.file, &stopped_) != sha)
        {
            out.file->close();
            out.error = stopped_ ? std::string("the service stops") : "does not match the catalog of the bundle";
            return false;
        }
        out.sha256 = sha;
        return true;
    }

    json model_manager::import_bundle(const std::string& folder, const json& catalog)
    {
        std::vector<import_item> items;
        for (const auto& e : catalog["models"].elements())
        {
            if (stopped_) break;
            const std::string listed = e["id"].as_string();
            const std::string sha = e["sha256"].as_string();
            const uint64_t size = static_cast<uint64_t>(e["size"].as_int(0));
            if (!safe_id(listed) || sha.size() != 64 || size == 0) continue;
            import_item d;
            if (!bundle_file(folder, e["files"], listed + ".gguf", sha, size, d))
            {
                if (!d.error.empty())
                {
                    d.result = "error";
                    if (d.error != unreadable_reason("link")) d.error += " (the model " + listed + ")";
                    items.push_back(std::move(d));
                }
                continue;
            }
            // Le projecteur que nomme l'entrée, quand le paquet le contient.
            import_item p;
            bool with_projector = false;
            const json& pe = e["projector"];
            const std::string projector_sha = pe["sha256"].as_string();
            const uint64_t projector_size = static_cast<uint64_t>(pe["size"].as_int(0));
            if (pe.is_object() && projector_sha.size() == 64 && projector_size > 0)
            {
                with_projector = bundle_file(folder, pe["files"], listed + projector_suffix, projector_sha,
                    projector_size, p);
                std::string holder, path;
                if (with_projector && projector_with_hash(projector_sha, holder, path))
                {
                    // Déjà installé, ou en attente.
                    p.file->close();
                    p.result = "already";
                    p.id = holder;
                    with_projector = false;
                }
                else if (!with_projector && !p.error.empty())
                {
                    p.result = "error";
                }
            }
            std::string error;
            installed_model same;
            if (installed_with_hash(sha, same))
            {
                d.file->close();
                d.result = "already";
                d.id = same.id;
                log_info("models: " + d.path + " holds " + same.id + ", installed already: not copied again");
                // Le projecteur du paquet va au décodeur installé quand il n'en a pas.
                if (with_projector && lacks_projector(same) && same.source != "builtin"
                    && parent_path(same.file) == dir_)
                {
                    if (attach_projector(p, same, false, error))
                    {
                        p.result = "imported";
                        p.id = same.id;
                    }
                    else
                    {
                        p.result = "error";
                        p.error = error;
                    }
                }
            }
            else
            {
                // L'entrée du catalogue en usage quand elle décrit ce fichier, sinon celle du paquet.
                json found;
                const bool current = catalog_entry(listed, found) && static_cast<const json&>(found)["sha256"]
                    .as_string() == sha;
                json entry = current ? found : e;
                // Une version plus récente d'un modèle du catalogue prend sa place ; un autre modèle garde la sienne.
                const std::string id = free_identifier(listed, true);
                entry["id"] = id;
                const std::string target = join_path(dir_, id + ".gguf");
                const std::string projector = join_path(dir_, id + projector_suffix);
                placement dh, ph;
                pending_projector pending;
                const bool with_pending = !with_projector && pending_for(d, pending);
                bool ok = direct_path(dir_)
                    && put_file(*d.file, d.path, std::string(), sha, false, target, dh, error, id);
                if (ok && with_projector
                    && !put_file(*p.file, p.path, std::string(), projector_sha, false, projector, ph, error, id))
                {
                    undo_put(d.path, target, dh);
                    ok = false;
                }
                const bool taken = ok && with_pending && take_pending(pending, projector);
                const std::string kept = with_projector || taken ? projector : std::string();
                const std::string kept_sha = with_projector ? projector_sha : taken ? pending.sha256 : std::string();
                if (ok && !install_file(id, target, entry, sha, "catalog", kept, kept_sha, error))
                {
                    undo_put(d.path, target, dh);
                    if (with_projector) undo_put(p.path, projector, ph);
                    if (taken) return_pending(pending, projector);
                    ok = false;
                }
                if (ok)
                {
                    d.result = "imported";
                    d.id = id;
                    if (with_projector)
                    {
                        p.result = "imported";
                        p.id = id;
                    }
                    log_info("models: imported " + id + " from the offline bundle " + d.path
                        + (with_projector ? ", with its projector " + p.path : std::string()));
                }
                else
                {
                    d.result = "error";
                    d.error = stopped_ ? std::string("the service stops") : error.empty() ? std::string("copy failed")
                        : error;
                    if (with_projector)
                    {
                        p.result = "error";
                        p.error = d.error;
                    }
                }
            }
            if (d.file) d.file->close();
            if (p.file) p.file->close();
            items.push_back(std::move(d));
            if (!p.result.empty()) items.push_back(std::move(p));
        }
        json result = import_result(items);
        result.erase("files");
        return result;
    }

    json model_manager::import_received(const std::string& file, const std::string& original_name,
        const std::string& sha)
    {
        std::vector<import_item> items(1);
        import_item& it = items[0];
        it.path = file;
        it.base = dir_;
        it.name = original_name;
        inspect(it, sha);
        import_items(items, true);
        if (it.result.empty())
        {
            it.result = "error";
            it.error = "the service stops";
        }
        // Un fichier qui ne peut pas être installé quitte le dossier des modèles.
        if (it.result == "error") remove_file(file);
        else if (it.result == "imported") log_info("models: the transfer of " + original_name + " installed " + it.id);
        json result = import_result(items);
        result.erase("files");
        return result;
    }

}
