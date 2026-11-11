// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_ENGINE_LOADER_H_
#define PSEUDALIS_COMMON_ENGINE_LOADER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine_api.h"
#include "json.h"

namespace pseudalis
{
    class engine_library
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le moteur du service, compilé dans le programme (PSEUDALIS_ENGINE_STATIC) : les
                fonctions de engine_api.h, que load() récupère, telles que le service les appelle.
        !*/
    public:
        engine_library() {}
        ~engine_library();
        engine_library(const engine_library&) = delete;
        engine_library& operator=(const engine_library&) = delete;

        // Récupère les fonctions du moteur compilé dans le programme.
        bool load();
        bool loaded() const { return create != nullptr; }
        const std::string& path() const { return path_; }

        int (PSD_CALL* api_version)(void) = nullptr;
        const char* (PSD_CALL* variant)(void) = nullptr;
        const char* (PSD_CALL* build)(void) = nullptr;
        int (PSD_CALL* models)(psd_buffer*) = nullptr;
        psd_engine* (PSD_CALL* create)(const char*, size_t, const char*, psd_buffer*) = nullptr;
        void (PSD_CALL* destroy)(psd_engine*) = nullptr;
        int (PSD_CALL* process)(psd_engine*, const char*, const char*, size_t, psd_picture_fn, psd_lookup_fn, void*,
            psd_buffer*, psd_buffer*, psd_buffer*) = nullptr;
        int (PSD_CALL* key_fingerprint)(psd_engine*, psd_buffer*) = nullptr;
        int (PSD_CALL* set_terms)(psd_engine*, const char*, size_t, psd_buffer*) = nullptr;
        char* (PSD_CALL* alloc)(size_t) = nullptr;
        void (PSD_CALL* free_buffer)(psd_buffer*) = nullptr;

    private:
        std::string path_;
    };

    struct picture_request
    {
        std::string bytes, id, sha256, media_type, location;
    };

    class engine_client
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un moteur d'une bibliothèque chargée, avec sa clé : les appels du service.
                process() peut être appelée depuis plusieurs threads à la fois.
        !*/
    public:
        engine_client(const std::shared_ptr<engine_library>& lib, const std::string& key, const json& options,
            std::string& error);
        ~engine_client();
        engine_client(const engine_client&) = delete;
        engine_client& operator=(const engine_client&) = delete;

        bool ok() const { return e_ != nullptr; }
        /*!
            ensures
                - Exécute request (engine_api.h) sur data ; result reçoit le document de
                  résultat et output les octets écrits.  pictures, quand il est fourni, reçoit
                  chaque image et renvoie sa description, ou une chaîne vide ; lookup, quand il
                  est fourni, reçoit chaque substitut candidat d'une restauration et renvoie
                  les étiquettes enregistrées pour lui.
                - Renvoie false avec la raison dans error.
        !*/
        bool process(const json& request, const std::string& data, json& result, std::string& output,
            const std::function<std::string(const picture_request&)>& pictures, std::string& error,
            const std::function<std::vector<std::string>(const std::string&)>& lookup =
                std::function<std::vector<std::string>(const std::string&)>()) const;
        std::string key_fingerprint() const;
        // Remplace les termes imposés du moteur ([{"term", "type", "case_sensitive"}]).
        bool set_terms(const json& terms, std::string& error) const;
        std::string variant() const;
        // La version et le build du moteur (psd_build()).
        std::string build() const;
        json models() const;

    private:
        std::shared_ptr<engine_library> lib_;
        psd_engine* e_ = nullptr;
    };
}

#endif // PSEUDALIS_COMMON_ENGINE_LOADER_H_
