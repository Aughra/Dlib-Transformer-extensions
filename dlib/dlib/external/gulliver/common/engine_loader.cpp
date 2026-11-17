// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "engine_loader.h"

#include <vector>

#include <dlfcn.h>

namespace albireo
{

    namespace
    {

        void* symbol(void* handle, const char* name)
        {
            return dlsym(handle, name);
        }

        template <typename F>
        bool resolve(void* handle, const char* name, F& f)
        {
            f = reinterpret_cast<F>(symbol(handle, name));
            return f != nullptr;
        }
    }

    engine_library::~engine_library()
    {
        unload();
    }

    bool engine_library::load(const std::string& path, std::string& error)
    {
        unload();
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_)
        {
            const char* e = dlerror();
            error = "cannot load " + path + ": " + (e ? e : "unknown error");
            return false;
        }
        path_ = path;
        const bool ok = resolve(handle_, "albireo_engine_abi_version", abi_version)
            && resolve(handle_, "albireo_engine_describe", describe)
            && resolve(handle_, "albireo_engine_create", create)
            && resolve(handle_, "albireo_engine_destroy", destroy)
            && resolve(handle_, "albireo_engine_load_model", load_model)
            && resolve(handle_, "albireo_engine_unload_model", unload_model)
            && resolve(handle_, "albireo_engine_query", query)
            && resolve(handle_, "albireo_engine_submit", submit)
            && resolve(handle_, "albireo_engine_cancel", cancel);
        if (!ok)
        {
            error = path + " is not an Albireo engine (missing functions)";
            unload();
            return false;
        }
        if (abi_version() != ALBIREO_ENGINE_ABI_VERSION)
        {
            error = path + " implements the engine interface " + std::to_string(abi_version()) + ", this program needs "
                + std::to_string(ALBIREO_ENGINE_ABI_VERSION);
            unload();
            return false;
        }
        return true;
    }

    void engine_library::unload()
    {
        if (!handle_) return;
        if (!keep_)
        {
            dlclose(handle_);
        }
        preloaded_.clear();
        handle_ = nullptr;
        keep_ = false;
        path_.clear();
        abi_version = nullptr;
        describe = nullptr;
        create = nullptr;
        destroy = nullptr;
        load_model = nullptr;
        unload_model = nullptr;
        query = nullptr;
        submit = nullptr;
        cancel = nullptr;
    }

    json engine_library::description() const
    {
        if (!describe) return json();
        size_t needed = 0;
        std::vector<char> buf(4096);
        int rc = describe(buf.data(), buf.size(), &needed);
        if (rc == ALBIREO_E_BUFFER_TOO_SMALL)
        {
            buf.resize(needed);
            rc = describe(buf.data(), buf.size(), &needed);
        }
        if (rc != ALBIREO_OK) return json();
        return json::parse_or_null(buf.data());
    }

    int engine_library::run_query(albireo_engine* e, const json& request, json& answer) const
    {
        const std::string text = request.dump();
        std::vector<char> buf(16384);
        size_t needed = 0;
        int rc = query(e, text.c_str(), buf.data(), buf.size(), &needed);
        if (rc == ALBIREO_E_BUFFER_TOO_SMALL)
        {
            buf.resize(needed);
            rc = query(e, text.c_str(), buf.data(), buf.size(), &needed);
        }
        answer = json::parse_or_null(buf.data());
        return rc;
    }

    std::string engine_file_name(const std::string& variant)
    {
        return "libalbireo-engine-" + variant + ".dylib";
    }

}
