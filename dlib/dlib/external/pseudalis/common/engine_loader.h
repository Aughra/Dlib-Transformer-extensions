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
                The engine of the service, compiled into the program (PSEUDALIS_ENGINE_STATIC):
                the functions of engine_api.h, which load() takes, as the service calls them.
        !*/
    public:
        engine_library() {}
        ~engine_library();
        engine_library(const engine_library&) = delete;
        engine_library& operator=(const engine_library&) = delete;

        // Takes the functions of the engine compiled into the program.
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
                An engine of a loaded library, with its key: the calls of the service.
                process() may be called from several threads at once.
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
                - Runs request (engine_api.h) on data; result receives the result document
                  and output the bytes written.  pictures, when given, receives each
                  picture and returns its description, or an empty string; lookup, when
                  given, receives each candidate substitute of a restoration and returns
                  the tags recorded for it.
                - Returns false with the reason in error.
        !*/
        bool process(const json& request, const std::string& data, json& result, std::string& output,
            const std::function<std::string(const picture_request&)>& pictures, std::string& error,
            const std::function<std::vector<std::string>(const std::string&)>& lookup =
                std::function<std::vector<std::string>(const std::string&)>()) const;
        std::string key_fingerprint() const;
        // Replaces the imposed terms of the engine ([{"term", "type", "case_sensitive"}]).
        bool set_terms(const json& terms, std::string& error) const;
        std::string variant() const;
        // The version and the build of the engine (psd_build()).
        std::string build() const;
        json models() const;

    private:
        std::shared_ptr<engine_library> lib_;
        psd_engine* e_ = nullptr;
    };
}

#endif // PSEUDALIS_COMMON_ENGINE_LOADER_H_
