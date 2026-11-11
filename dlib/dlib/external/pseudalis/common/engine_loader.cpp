// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#include "engine_loader.h"

#include <cstring>

#include "build_stamp.h"
#include "platform.h"
#include "version.h"

namespace pseudalis
{
    namespace
    {

        struct callback_context
        {
            const std::function<std::string(const picture_request&)>* pictures;
            const std::function<std::vector<std::string>(const std::string&)>* lookup;
            const engine_library* lib;
        };

        int PSD_CALL on_lookup(void* context, const char* candidate, size_t size, psd_buffer* tags)
        {
            const callback_context* c = static_cast<const callback_context*>(context);
            tags->data = nullptr;
            tags->size = 0;
            try
            {
                std::string all;
                for (const std::string& t : (*c->lookup)(std::string(candidate, size)))
                    if (!t.empty() && t.find('\n') == std::string::npos) all += (all.empty() ? "" : "\n") + t;
                if (all.empty()) return 0;
                tags->data = c->lib->alloc(all.size());
                if (!tags->data) return 0;
                std::memcpy(tags->data, all.data(), all.size());
                tags->size = all.size();
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        int PSD_CALL on_picture(void* context, const psd_picture* p, psd_buffer* description)
        {
            const callback_context* c = static_cast<const callback_context*>(context);
            description->data = nullptr;
            description->size = 0;
            try
            {
                picture_request r;
                r.bytes.assign(p->bytes, p->size);
                r.id = p->id;
                r.sha256 = p->sha256;
                r.media_type = p->media_type;
                r.location = p->location;
                const std::string text = (*c->pictures)(r);
                if (text.empty()) return 0;
                description->data = c->lib->alloc(text.size());
                if (!description->data) return 0;
                std::memcpy(description->data, text.data(), text.size());
                description->size = text.size();
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }
    }

    engine_library::~engine_library() {}

    bool engine_library::load()
    {
        api_version = psd_api_version;
        variant = psd_variant;
        build = psd_build;
        models = psd_models;
        create = psd_create;
        destroy = psd_destroy;
        process = psd_process;
        key_fingerprint = psd_key_fingerprint;
        set_terms = psd_set_terms;
        alloc = psd_alloc;
        free_buffer = psd_free;
        path_ = "(built in)";
        return true;
    }

    engine_client::engine_client(const std::shared_ptr<engine_library>& lib, const std::string& key,
                                 const json& options, std::string& error)
        : lib_(lib)
    {
        if (!lib_ || !lib_->loaded())
        {
            error = "no engine library";
            return;
        }
        psd_buffer err = { nullptr, 0 };
        e_ = lib_->create(key.data(), key.size(), options.dump().c_str(), &err);
        if (!e_) error = err.data ? std::string(err.data, err.size) : "the engine cannot start";
        lib_->free_buffer(&err);
    }

    engine_client::~engine_client()
    {
        if (e_) lib_->destroy(e_);
    }

    bool engine_client::process(const json& request, const std::string& data, json& result, std::string& output,
                                const std::function<std::string(const picture_request&)>& pictures,
                                std::string& error,
                                const std::function<std::vector<std::string>(const std::string&)>& lookup) const
    {
        if (!e_)
        {
            error = "no engine";
            return false;
        }
        callback_context c = { &pictures, &lookup, lib_.get() };
        psd_buffer r = { nullptr, 0 }, o = { nullptr, 0 }, err = { nullptr, 0 };
        const int ok = lib_->process(e_, request.dump().c_str(), data.data(), data.size(),
                                     pictures ? on_picture : nullptr, lookup ? on_lookup : nullptr, &c, &r, &o, &err);
        if (ok == 1)
        {
            result = json::parse_or_null(std::string(r.data ? r.data : "", r.size));
            output.assign(o.data ? o.data : "", o.size);
        }
        else
        {
            error = err.data ? std::string(err.data, err.size) : "the engine failed";
        }
        lib_->free_buffer(&r);
        lib_->free_buffer(&o);
        lib_->free_buffer(&err);
        return ok == 1;
    }

    std::string engine_client::key_fingerprint() const
    {
        if (!e_) return std::string();
        psd_buffer b = { nullptr, 0 };
        std::string out;
        if (lib_->key_fingerprint(e_, &b) == 1 && b.data) out.assign(b.data, b.size);
        lib_->free_buffer(&b);
        return out;
    }

    bool engine_client::set_terms(const json& terms, std::string& error) const
    {
        if (!e_ || !lib_->set_terms)
        {
            error = "no engine";
            return false;
        }
        const std::string text = terms.dump();
        psd_buffer err = { nullptr, 0 };
        const int ok = lib_->set_terms(e_, text.data(), text.size(), &err);
        if (ok != 1) error = err.data ? std::string(err.data, err.size) : std::string("the terms were refused");
        lib_->free_buffer(&err);
        return ok == 1;
    }

    std::string engine_client::variant() const
    {
        return lib_ && lib_->variant ? lib_->variant() : std::string();
    }

    std::string engine_client::build() const
    {
        const char* b = lib_ && lib_->build ? lib_->build() : nullptr;
        return b ? b : std::string();
    }

    json engine_client::models() const
    {
        psd_buffer b = { nullptr, 0 };
        json j;
        if (lib_ && lib_->models && lib_->models(&b) == 1 && b.data)
            j = json::parse_or_null(std::string(b.data, b.size));
        if (lib_ && b.data) lib_->free_buffer(&b);
        return j;
    }
}
