// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "settings.h"

#include <algorithm>

#include "../common/log.h"
#include "../common/platform.h"

namespace albireo
{

    const char* const model_roles[3] = { "general", "code", "security" };
    const char* const embedding_role = "embedding";
    const char* const proxy_password_mask = "********";

    namespace
    {
        // L'endroit où se trouve le mot de passe d'un réglage de proxy "http://user:password@host:port",
        // tel que http_fetch.cpp lit le réglage ; false quand il n'en a pas.
        bool password_span(const std::string& proxy, size_t& from, size_t& length)
        {
            const size_t scheme = proxy.find("://");
            const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
            const size_t at = proxy.rfind('@');
            if (at == std::string::npos || at < start) return false;
            const size_t colon = proxy.find(':', start);
            if (colon == std::string::npos || colon > at) return false;
            from = colon + 1;
            length = at - from;
            return length > 0;
        }

        // Le réglage sans son mot de passe, ":password" retiré.
        std::string without_password(const std::string& proxy)
        {
            size_t from = 0, length = 0;
            if (!password_span(proxy, from, length)) return proxy;
            return proxy.substr(0, from - 1) + proxy.substr(from + length);
        }

        // Le fichier du proxy avec son mot de passe, à côté de settings.json.
        std::string secret_path(const std::string& settings_path)
        {
            return join_path(parent_path(settings_path), "proxy.secret");
        }

        // Le fichier de la clé d'un fournisseur de recherche que les versions antérieures gardaient à côté
        // de settings.json.
        std::string obsolete_key_path(const std::string& settings_path)
        {
            return join_path(parent_path(settings_path), "web_search.secret");
        }

        // Indique si un document des réglages contient un réglage que cette version n'a pas, ou un autre
        // fournisseur de recherche.
        bool obsolete_settings(const json& j)
        {
            for (const char* key : { "public_link", "web_search_url", "web_search_key", "web_search_key_set" })
                if (j.contains(key)) return true;
            const std::string search = j["web_search"].as_string();
            return j.contains("web_search") && search != "wikipedia" && search != "none";
        }

        // Le texte d'un fichier secret sans les fins de ligne ni les espaces qui le suivent.
        std::string trimmed(std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
                text.pop_back();
            return text;
        }

        // La copie des derniers réglages écrits, lue quand settings.json n'est plus valide.
        std::string backup_path(const std::string& settings_path)
        {
            return settings_path + ".bak";
        }

        bool read_settings(const std::string& path, std::string& text, json& j, std::string& error)
        {
            if (!read_file(path, text))
            {
                error = "unreadable";
                return false;
            }
            return json::parse(text, j, &error) && j.is_object();
        }
    }

    std::string masked_proxy(const std::string& proxy)
    {
        size_t from = 0, length = 0;
        if (!password_span(proxy, from, length)) return proxy;
        return proxy.substr(0, from) + proxy_password_mask + proxy.substr(from + length);
    }

    bool proxy_password_masked(const std::string& proxy)
    {
        size_t from = 0, length = 0;
        return password_span(proxy, from, length) && proxy.compare(from, length, proxy_password_mask) == 0;
    }

    json host_settings::to_json() const
    {
        json j = json::object();
        j["port"] = port;
        j["models_dir"] = models_dir;
        j["language"] = language;
        j["engine"] = engine;
        j["accelerator"] = accelerator;
        j["device_mb"] = device_mb;
        j["threads"] = threads;
        j["context"] = context;
        j["max_sequences"] = max_sequences;
        j["kv_mb"] = kv_mb;
        j["kv_type"] = kv_type;
        j["warm_history"] = warm_history;
        j["spin_us"] = spin_us;
        j["gpu_prompt_tokens"] = gpu_prompt_tokens;
        j["gpu_prompt_context"] = gpu_prompt_context;
        json a = json::object();
        for (const auto& kv : active) a[kv.first] = kv.second;
        j["active"] = a;
        j["default_model"] = default_model;
        j["offline"] = offline;
        j["proxy"] = masked_proxy(proxy);
        j["web_access"] = web_access;
        j["web_search"] = web_search;
        j["engine_benchmarks"] = engine_benchmarks;
        if (trust_authority >= 0) j["trust_authority"] = trust_authority > 0;
        j["lan"] = lan;
        j["lan_port"] = lan_port;
        j["lan_password_hash"] = lan_password_hash;
        return j;
    }

    void host_settings::from_json(const json& j)
    {
        const host_settings d;
        port = static_cast<int>(j["port"].as_int(d.port));
        if (port < 1 || port > 65535) port = d.port;
        models_dir = j["models_dir"].str(d.models_dir);
        language = j["language"].str(d.language);
        engine = j["engine"].str(d.engine);
        if (engine != "auto") engine = d.engine;
        accelerator = j["accelerator"].str(d.accelerator);
        if (accelerator != "auto" && accelerator != "none") accelerator = d.accelerator;
        device_mb = std::max<long long>(0, j["device_mb"].as_int(d.device_mb));
        threads = static_cast<int>(j["threads"].as_int(d.threads));
        context = static_cast<long>(j["context"].as_int(d.context));
        max_sequences = static_cast<int>(j["max_sequences"].as_int(d.max_sequences));
        if (max_sequences < 1 || max_sequences > 64) max_sequences = d.max_sequences;
        kv_mb = j["kv_mb"].as_int(d.kv_mb);
        kv_type = j["kv_type"].str(d.kv_type);
        if (kv_type != "auto" && kv_type != "q8" && kv_type != "f16") kv_type = d.kv_type;
        warm_history = j["warm_history"].as_bool(d.warm_history);
        spin_us = static_cast<int>(std::max<long long>(0, std::min<long long>(100000, j["spin_us"].as_int(d.spin_us))));
        gpu_prompt_tokens = static_cast<int>(std::max<long long>(9, std::min<long long>(1 << 20,
            j["gpu_prompt_tokens"].as_int(d.gpu_prompt_tokens))));
        gpu_prompt_context = static_cast<int>(std::max<long long>(0, std::min<long long>(1 << 20,
            j["gpu_prompt_context"].as_int(d.gpu_prompt_context))));
        active.clear();
        for (const auto& kv : j["active"].items())
        {
            if (!kv.second.is_string() || kv.second.as_string().empty()) continue;
            for (const char* r : model_roles)
                if (kv.first == r) active[kv.first] = kv.second.as_string();
            if (kv.first == embedding_role) active[kv.first] = kv.second.as_string();
        }
        default_model = j["default_model"].as_bool(d.default_model);
        offline = j["offline"].as_bool(d.offline);
        proxy = j["proxy"].str(d.proxy);
        web_access = j["web_access"].as_bool(d.web_access);
        // Les fournisseurs des versions antérieures, "auto", "brave" et "searxng", cèdent la place à
        // Wikipédia.
        web_search = j["web_search"].as_string() == "none" ? "none" : d.web_search;
        engine_benchmarks = j["engine_benchmarks"].is_object() ? j["engine_benchmarks"] : json::object();
        trust_authority = j["trust_authority"].is_bool() ? (j["trust_authority"].as_bool() ? 1 : 0) : d.trust_authority;
        lan = j["lan"].as_bool(d.lan);
        lan_port = static_cast<int>(j["lan_port"].as_int(d.lan_port));
        if (lan_port < 1024 || lan_port > 65535) lan_port = d.lan_port;
        lan_password_hash = j["lan_password_hash"].str(d.lan_password_hash);
    }

    std::string host_settings::effective_models_dir() const
    {
        return models_dir.empty() ? join_path(data_dir(), "models") : models_dir;
    }

    settings_store::settings_store(const std::string& path) : path_(path)
    {
        bool restored = false;
        // Un document d'une version antérieure, réécrit sans ce que cette version n'a pas.
        bool obsolete = false;
        if (file_exists(path_))
        {
            json j;
            std::string text, error;
            if (read_settings(path_, text, j, error))
            {
                s_.from_json(j);
                obsolete = obsolete_settings(j);
                // La copie suit un fichier valide, y compris celui que l'installateur ou une main a écrit.
                const std::string backup = backup_path(path_);
                std::string copy;
                if ((!read_file(backup, copy) || copy != text) && !write_file_atomic(backup, text))
                    log_warning("settings: the copy of " + path_ + " cannot be written");
            }
            else
            {
                /*
                    Un fichier abîmé sur son disque, ou par une main : la copie des derniers réglages
                    valides sert, pour que le mode hors ligne, le port, les fonctions des modèles et
                    les dossiers restent comme ils étaient, et settings.json est réécrit à partir
                    d'elle.
                */
                json saved;
                std::string saved_text, unused;
                restored = read_settings(backup_path(path_), saved_text, saved, unused);
                if (restored) s_.from_json(saved);
                log_warning("settings: " + path_ + " is not valid JSON (" + error + "), "
                    + (restored ? "its last copy is used" : "defaults used"));
            }
        }
        std::lock_guard<std::mutex> lock(m_);
        if (proxy_password_masked(s_.proxy))
        {
            // Le mot de passe enregistré pour cette adresse et cet utilisateur.
            std::string stored;
            if (read_file(secret_path(path_), stored, 4096)) stored = trimmed(stored);
            if (!stored.empty() && masked_proxy(stored) == s_.proxy)
            {
                s_.proxy = stored;
            }
            else
            {
                log_warning("settings: the password of the proxy is not stored, the proxy is used without it");
                s_.proxy = without_password(s_.proxy);
            }
        }
        else if (masked_proxy(s_.proxy) != s_.proxy && !write())
        {
            log_warning("settings: the password of the proxy cannot move from " + path_ + " to proxy.secret");
        }
        // Réécrit une fois le mot de passe du proxy revenu dans les réglages : à partir de sa copie, ou sans les
        // réglages d'une version antérieure ni la clé de son fournisseur de recherche.
        if ((restored || obsolete || file_exists(obsolete_key_path(path_))) && !write())
            log_warning("settings: " + path_ + " cannot be written again");
    }

    bool settings_store::write()
    {
        make_dirs(parent_path(path_));
        // Le mot de passe d'abord, pour que settings.json ne masque aucun mot de passe non enregistré.
        const std::string secret = secret_path(path_);
        const bool password = masked_proxy(s_.proxy) != s_.proxy;
        if (password && !write_file_restricted(secret, s_.proxy)) return false;
        const std::string text = s_.to_json().dump_formatted(2) + "\n";
        if (!write_file_atomic(path_, text)) return false;
        // La copie lue quand settings.json est abîmé, écrite une fois settings.json en place.
        if (!write_file_atomic(backup_path(path_), text)) log_warning("settings: its copy cannot be written");
        if (!password && file_exists(secret)) remove_file(secret);
        if (file_exists(obsolete_key_path(path_))) remove_file(obsolete_key_path(path_));
        return true;
    }

    host_settings settings_store::get() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return s_;
    }

    bool settings_store::set(const host_settings& s)
    {
        std::lock_guard<std::mutex> lock(m_);
        s_ = s;
        return write();
    }

    host_settings settings_store::update(const std::function<void(host_settings&)>& change)
    {
        std::lock_guard<std::mutex> lock(m_);
        change(s_);
        if (!write()) log_warning("settings: " + path_ + " cannot be written");
        return s_;
    }

}
