// Fork pour macOS de Dlib-Transformer-extensions (Albireo pour Apple Silicon).
// License: Boost Software License   See LICENSE.txt for the full license.
#include "hub.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

#include "http_fetch.h"

namespace albireo
{
    namespace
    {
        const char* const hub = "https://huggingface.co";

        // Les architectures que le moteur lit (model_config.cpp).
        const std::set<std::string> engine_architectures = { "llama", "mistral", "mistral3", "granite", "qwen2",
            "qwen3", "qwen3vl", "mixtral", "granitemoe", "qwen3moe", "qwen3vlmoe", "phi3", "qwen35", "qwen35moe", "gemma4",
            "lfm2", "lfm2moe" };
        const std::set<std::string> moe_architectures = { "mixtral", "granitemoe", "qwen3moe", "qwen3vlmoe", "qwen35moe",
            "lfm2moe" };
        // Les architectures dont le moteur lit les images avec un projecteur : qwen3vl_merger pour Qwen3-VL, et
        // qwen35 depuis Albireo 4.13.0 du fork (qwen35moe, dont aucun projecteur n'a encore été essayé, attend) ;
        // gemma4v pour Gemma 4 depuis Albireo 4.16.0 du fork.
        const std::set<std::string> vision_architectures = { "qwen3vl", "qwen3vlmoe", "qwen35", "gemma4" };
        // Les architectures dont les passes restent sur le processeur : Gemma 4 depuis Albireo 4.14.0 du fork, LFM2 et
        // LFM2-MoE depuis Albireo 4.17.0.
        const std::set<std::string> processor_architectures = { "gemma4", "lfm2", "lfm2moe" };

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string url_encode(const std::string& s)
        {
            static const char digits[] = "0123456789ABCDEF";
            std::string out;
            for (const unsigned char c : s)
            {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
                else
                {
                    out += '%';
                    out += digits[c >> 4];
                    out += digits[c & 15];
                }
            }
            return out;
        }

        // Un dépôt « propriétaire/nom » de caractères simples.
        bool valid_repo(const std::string& repo)
        {
            static const std::regex rx("^[A-Za-z0-9][A-Za-z0-9._-]{0,95}/[A-Za-z0-9][A-Za-z0-9._-]{0,95}$");
            return std::regex_match(repo, rx);
        }

        bool valid_file(const std::string& file)
        {
            static const std::regex rx("^[A-Za-z0-9][A-Za-z0-9._/ -]{0,200}\\.gguf$");
            return std::regex_match(file, rx) && file.find("..") == std::string::npos;
        }

        bool get_json(const std::string& url, json& out, std::string& error)
        {
            std::string body;
            if (!http_get(url, body, error, 32u << 20, 20, 60)) return false;
            if (!json::parse(body, out, &error))
            {
                error = "Hugging Face answered with something else than JSON: " + error;
                return false;
            }
            return true;
        }

        // Le format des poids d'un fichier, d'après son nom : "Q4_K_M", "IQ4_XS", "BF16"…
        std::string quant_of(const std::string& file)
        {
            static const std::regex rx("(IQ[1-4]_(XXS|XS|S|M|NL)|TQ[12]_0|MXFP4|Q[2-8]_K(_[SML]|_XL|_L)?|Q[4-8]_[01]|"
                "BF16|F16|F32)", std::regex::icase);
            std::smatch m;
            std::string upper = file;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return std::regex_search(upper, m, rx) ? m.str(1) : std::string();
        }

        // Ce que les noyaux du moteur lisent (qkernels.cpp) ; les autres formats sont refusés au chargement.
        bool quant_supported(const std::string& q)
        {
            static const std::regex rx("^(Q2_K.*|Q3_K.*|Q4_K.*|Q5_K.*|Q6_K|Q4_0|Q4_1|Q5_0|Q5_1|Q8_0|F16|BF16|F32|IQ4_NL)$");
            return std::regex_match(q, rx);
        }

        bool quant_gpu(const std::string& q)
        {
            return q == "Q4_K_M" || q == "Q4_K_S" || q == "Q4_K" || q == "Q6_K";
        }

        // Un dépôt de Gemma 4 à mélange d'experts (26B-A4B), que le moteur refuse : son nom le dit, l'API ne donnant
        // pas le nombre d'experts.
        bool gemma4_experts(const std::string& arch, const std::string& repo)
        {
            return arch == "gemma4" && std::regex_search(repo, std::regex("[-_]A[0-9]+B", std::regex::icase));
        }

        json architecture_check(const std::string& arch, const std::string& repo)
        {
            json c = json::object();
            c["compatible"] = arch.empty() ? false : engine_architectures.count(arch) > 0;
            if (arch.empty()) c["reason"] = "architecture inconnue";
            else if (!engine_architectures.count(arch)) c["reason"] = "architecture " + arch + " non prise en charge par le moteur";
            else if (gemma4_experts(arch, repo))
            {
                c["compatible"] = false;
                c["reason"] = "mélange d'experts de Gemma 4, pas encore pris en charge par le moteur";
            }
            c["moe"] = moe_architectures.count(arch) > 0;
            return c;
        }

        std::string slug(const std::string& s)
        {
            std::string out;
            for (const unsigned char c : lower(s))
                out += std::isalnum(c) || c == '.' || c == '_' || c == '-' ? static_cast<char>(c) : '-';
            while (out.find("--") != std::string::npos) out.replace(out.find("--"), 2, "-");
            return out.substr(0, 100);
        }

        std::string license_of(const json& info)
        {
            const std::string l = info["cardData"]["license"].as_string();
            if (!l.empty()) return l;
            for (const auto& t : info["tags"].elements())
                if (t.as_string().compare(0, 8, "license:") == 0) return t.as_string().substr(8);
            return std::string();
        }
    }

    bool hub_search(const std::string& query, json& out, std::string& error)
    {
        json list;
        const std::string url = std::string(hub) + "/api/models?filter=gguf&sort=downloads&limit=40" +
            "&expand%5B%5D=gguf&expand%5B%5D=downloads&expand%5B%5D=likes&expand%5B%5D=lastModified" +
            (query.empty() ? std::string() : "&search=" + url_encode(query.substr(0, 100)));
        if (!get_json(url, list, error)) return false;
        json models = json::array();
        for (const auto& m : list.elements())
        {
            const std::string arch = m["gguf"]["architecture"].as_string();
            json o = architecture_check(arch, m["id"].as_string());
            o["repo"] = m["id"];
            o["architecture"] = arch;
            o["parameters"] = m["gguf"]["total"];
            o["context"] = m["gguf"]["context_length"];
            o["downloads"] = m["downloads"];
            o["likes"] = m["likes"];
            o["updated"] = m["lastModified"];
            models.push_back(o);
        }
        out = json::object();
        out["models"] = models;
        return true;
    }

    bool hub_files(const std::string& repo, json& out, std::string& error)
    {
        if (!valid_repo(repo))
        {
            error = "not a Hugging Face repository: owner/name";
            return false;
        }
        json info, tree;
        if (!get_json(std::string(hub) + "/api/models/" + repo + "?expand%5B%5D=gguf&expand%5B%5D=cardData&expand%5B%5D=tags",
                info, error)
            || !get_json(std::string(hub) + "/api/models/" + repo + "/tree/main?recursive=true", tree, error))
            return false;
        const std::string arch = info["gguf"]["architecture"].as_string();
        json files = json::array(), projectors = json::array();
        for (const auto& f : tree.elements())
        {
            const std::string path = f["path"].as_string();
            if (f["type"].as_string() != "file" || lower(path).size() < 5
                || lower(path).compare(lower(path).size() - 5, 5, ".gguf") != 0)
                continue;
            json o = json::object();
            o["file"] = path;
            o["size"] = f["lfs"]["size"].is_null() ? f["size"] : f["lfs"]["size"];
            const std::string q = quant_of(path);
            o["quantization"] = q;
            const bool split = std::regex_search(path, std::regex("-[0-9]{5}-of-[0-9]{5}\\.gguf$"));
            if (lower(path).find("mmproj") != std::string::npos)
            {
                // Un projecteur n'est proposé que pour une architecture dont le moteur lit les images.
                if (vision_architectures.count(arch)) projectors.push_back(o);
                continue;
            }
            // Les petits modèles de brouillon (prédiction de plusieurs tokens) de Gemma 4, et le tokenizer audio et la voix
            // (vocoder) de LFM2.5-Audio, ne se servent pas seuls.
            const std::string name = lower(path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1));
            if (name.compare(0, 4, "mtp-") == 0 || name.compare(0, 10, "tokenizer-") == 0
                || name.compare(0, 8, "vocoder-") == 0)
                continue;
            json c = architecture_check(arch, repo);
            if (c["compatible"].as_bool(false) && split)
            {
                c["compatible"] = false;
                c["reason"] = "fichier découpé en plusieurs parties, que le service ne réassemble pas";
            }
            else if (c["compatible"].as_bool(false) && !quant_supported(q))
            {
                c["compatible"] = false;
                c["reason"] = "format de poids " + (q.empty() ? std::string("inconnu") : q) + " non lu par le moteur";
            }
            for (const auto& kv : c.items()) o[kv.first] = kv.second;
            // Les mélanges d'experts écrivent aussi sur le GPU depuis Albireo 4.7.0 du fork.
            o["gpu"] = c["compatible"].as_bool(false) && quant_gpu(q) && !processor_architectures.count(arch);
            files.push_back(o);
        }
        out = json::object();
        out["repo"] = repo;
        out["architecture"] = arch;
        out["parameters"] = info["gguf"]["total"];
        out["context"] = info["gguf"]["context_length"];
        out["license"] = license_of(info);
        out["files"] = files;
        out["projectors"] = projectors;
        return true;
    }

    bool hub_entry(const std::string& repo, const std::string& file, const std::string& projector, json& entry,
        std::string& error)
    {
        if (!valid_repo(repo) || !valid_file(file) || (!projector.empty() && !valid_file(projector)))
        {
            error = "invalid repository or file name";
            return false;
        }
        json info, tree;
        if (!get_json(std::string(hub) + "/api/models/" + repo + "?expand%5B%5D=gguf&expand%5B%5D=cardData&expand%5B%5D=tags",
                info, error)
            || !get_json(std::string(hub) + "/api/models/" + repo + "/tree/main?recursive=true", tree, error))
            return false;
        // La taille et l'empreinte SHA-256 d'un fichier, que Git LFS donne (lfs.oid).
        auto find = [&](const std::string& path, json& part) {
            for (const auto& f : tree.elements())
            {
                if (f["path"].as_string() != path) continue;
                const std::string sha = f["lfs"]["oid"].as_string();
                const long long size = f["lfs"]["size"].as_int(f["size"].as_int(0));
                if (sha.size() != 64 || size <= 0) return false;
                part = json::object();
                part["url"] = std::string(hub) + "/" + repo + "/resolve/main/" + path;
                part["size"] = size;
                part["sha256"] = sha;
                return true;
            }
            return false;
        };
        json decoder, image;
        if (!find(file, decoder))
        {
            error = "the file " + file + " is not in " + repo + ", or has no SHA-256";
            return false;
        }
        if (!projector.empty() && !find(projector, image))
        {
            error = "the projector " + projector + " is not in " + repo + ", or has no SHA-256";
            return false;
        }
        const std::string arch = info["gguf"]["architecture"].as_string();
        if (!engine_architectures.count(arch))
        {
            error = "the architecture " + (arch.empty() ? std::string("(unknown)") : arch) + " is not supported by the engine";
            return false;
        }
        if (gemma4_experts(arch, repo))
        {
            error = "the mixtures of experts of Gemma 4 are not supported by the engine yet";
            return false;
        }
        if (!projector.empty() && !vision_architectures.count(arch))
        {
            error = "the engine does not read the images of the architecture " + arch;
            return false;
        }
        const std::string q = quant_of(file);
        const std::string name = repo.substr(repo.find('/') + 1);
        std::string shown = name;
        for (const char* tail : { "-GGUF", "-gguf", "_GGUF" })
        {
            const std::string t = tail;
            if (shown.size() > t.size() && shown.compare(shown.size() - t.size(), t.size(), t) == 0)
                shown = shown.substr(0, shown.size() - t.size());
        }
        const std::string low = lower(repo);
        entry = json::object();
        entry["id"] = slug("hf-" + repo.substr(0, repo.find('/')) + "-" + shown + "-" + q);
        entry["name"] = shown + (q.empty() ? std::string() : " " + q);
        entry["publisher"] = repo.substr(0, repo.find('/'));
        entry["quantization"] = q;
        json roles = json::array();
        roles.push_back("general");
        if (low.find("code") != std::string::npos || low.find("devstral") != std::string::npos) roles.push_back("code");
        if (low.find("sec") != std::string::npos) roles.push_back("security");
        entry["roles"] = roles;
        json inputs = json::array();
        inputs.push_back("text");
        if (!projector.empty()) inputs.push_back("image");
        entry["inputs"] = inputs;
        entry["tools"] = info["gguf"]["chat_template"].as_string().find("tools") != std::string::npos;
        entry["reasoning"] = low.find("think") != std::string::npos || low.find("reason") != std::string::npos
            || low.find("-r1") != std::string::npos;
        entry["license"] = license_of(info);
        entry["arch"] = arch;
        entry["parameters"] = info["gguf"]["total"];
        entry["context"] = info["gguf"]["context_length"];
        entry["description"] = "Modèle " + repo + " ajouté depuis Hugging Face (" + arch + ", " +
            (q.empty() ? std::string("format inconnu") : q) + "). Il ne vient pas du catalogue de l'original : sa "
            "description, ses spécialités et sa licence sont déduites de son dépôt.";
        entry["source"] = std::string(hub) + "/" + repo;
        entry["origin"] = "huggingface";
        entry["size"] = decoder["size"];
        entry["sha256"] = decoder["sha256"];
        json parts = json::array();
        parts.push_back(decoder);
        entry["files"] = parts;
        if (!projector.empty())
        {
            json p = json::object();
            p["quantization"] = quant_of(projector);
            p["size"] = image["size"];
            p["sha256"] = image["sha256"];
            json pp = json::array();
            pp.push_back(image);
            p["files"] = pp;
            entry["projector"] = p;
        }
        return true;
    }
}
