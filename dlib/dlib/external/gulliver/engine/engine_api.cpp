// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'interface C de common/engine_api.h au-dessus d'gulliver::engine.  Aucune exception ne la
    traverse : chacune devient un code de retour et un message.
*/

#ifndef GULLIVER_ENGINE_BUILD
#define GULLIVER_ENGINE_BUILD
#endif

#include "../common/engine_api.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>


#include "engine.h"

struct gulliver_engine
{
    std::unique_ptr<gulliver::engine> impl;
};

namespace
{
    void copy_message(const std::string& message, char* buffer, size_t capacity)
    {
        if (!buffer || capacity == 0) return;
        // Tronqué sur une frontière de caractère, pour que le message reste de l'UTF-8 valide.
        size_t n = std::min(message.size(), capacity - 1);
        while (n > 0 && n < message.size() && (static_cast<unsigned char>(message[n]) & 0xC0) == 0x80) --n;
        std::memcpy(buffer, message.data(), n);
        buffer[n] = '\0';
    }

    int write_json(const std::string& text, char* json, size_t capacity, size_t* needed)
    {
        if (needed) *needed = text.size() + 1;
        if (!json || capacity < text.size() + 1) return GULLIVER_E_BUFFER_TOO_SMALL;
        std::memcpy(json, text.data(), text.size());
        json[text.size()] = '\0';
        return GULLIVER_OK;
    }

    int code_of(const std::exception& e)
    {
        if (dynamic_cast<const gulliver::engine_busy*>(&e)) return GULLIVER_E_BUSY;
        if (dynamic_cast<const std::invalid_argument*>(&e)) return GULLIVER_E_INVALID_ARGUMENT;
        if (dynamic_cast<const std::bad_alloc*>(&e)) return GULLIVER_E_OUT_OF_MEMORY;
        return GULLIVER_E_INTERNAL;
    }


    gulliver::json parse_or_throw(const char* text)
    {
        if (!text || !*text) return gulliver::json::object();
        gulliver::json j;
        std::string error;
        if (!gulliver::json::parse(text, j, &error)) throw std::invalid_argument("invalid JSON: " + error);
        return j;
    }
}

extern "C"
{

GULLIVER_ENGINE_EXPORT uint32_t GULLIVER_ENGINE_CALL gulliver_engine_abi_version(void)
{
    return GULLIVER_ENGINE_ABI_VERSION;
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_describe(char* json, size_t capacity, size_t* needed)
{
    try
    {
        return write_json(gulliver::engine::describe().dump(), json, capacity, needed);
    }
    catch (...)
    {
        return GULLIVER_E_INTERNAL;
    }
}

GULLIVER_ENGINE_EXPORT gulliver_engine* GULLIVER_ENGINE_CALL gulliver_engine_create(const char* config_json,
    gulliver_log_callback log, void* log_user, char* error, size_t error_capacity)
{
    try
    {
        std::unique_ptr<gulliver_engine> e(new gulliver_engine());
        gulliver::log_sink sink;
        if (log)
            sink = [log, log_user](int level, const std::string& message) { log(log_user, level, message.c_str()); };
        e->impl.reset(new gulliver::engine(parse_or_throw(config_json), sink));
        return e.release();
    }
    catch (const std::exception& ex)
    {
        copy_message(ex.what(), error, error_capacity);
    }
    catch (...)
    {
        copy_message("unknown error", error, error_capacity);
    }
    return nullptr;
}

GULLIVER_ENGINE_EXPORT void GULLIVER_ENGINE_CALL gulliver_engine_destroy(gulliver_engine* engine)
{
    try
    {
        delete engine;
    }
    catch (...)
    {
    }
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_load_model(gulliver_engine* engine,
    const char* model_id, const char* path, const char* options_json, char* error, size_t error_capacity)
{
    if (!engine || !model_id || !path) return GULLIVER_E_INVALID_ARGUMENT;
    try
    {
        engine->impl->load_model(model_id, path, parse_or_throw(options_json));
        return GULLIVER_OK;
    }
    catch (const std::exception& ex)
    {
        copy_message(ex.what(), error, error_capacity);
        return code_of(ex);
    }
    catch (...)
    {
        copy_message("unknown error", error, error_capacity);
        return GULLIVER_E_INTERNAL;
    }
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_unload_model(gulliver_engine* engine, const char* model_id)
{
    if (!engine || !model_id) return GULLIVER_E_INVALID_ARGUMENT;
    try
    {
        engine->impl->unload_model(model_id);
        return GULLIVER_OK;
    }
    catch (...)
    {
        return GULLIVER_E_INTERNAL;
    }
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_query(gulliver_engine* engine,
    const char* request_json, char* json, size_t capacity, size_t* needed)
{
    if (!engine) return GULLIVER_E_INVALID_ARGUMENT;
    try
    {
        std::string text;
        int code = GULLIVER_OK;
        try
        {
            text = engine->impl->query(parse_or_throw(request_json)).dump();
        }
        catch (const std::exception& ex)
        {
            code = code_of(ex);
            gulliver::json e = gulliver::json::object();
            e["error"] = ex.what();
            text = e.dump();
        }
        const int w = write_json(text, json, capacity, needed);
        return w != GULLIVER_OK ? w : code;
    }
    catch (...)
    {
        // Mémoire épuisée pendant l'écriture de la réponse ou de l'erreur.
        return GULLIVER_E_INTERNAL;
    }
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_submit(gulliver_engine* engine,
    const char* request_json, gulliver_event_callback callback, void* user, uint64_t* request_id,
    char* error, size_t error_capacity)
{
    if (!engine || !callback) return GULLIVER_E_INVALID_ARGUMENT;
    try
    {
        const uint64_t id = engine->impl->submit(parse_or_throw(request_json),
            [callback, user](int kind, const std::string& data) { callback(user, kind, data.data(), data.size()); });
        if (request_id) *request_id = id;
        return GULLIVER_OK;
    }
    catch (const std::exception& ex)
    {
        copy_message(ex.what(), error, error_capacity);
        const std::string what = ex.what();
        if (what.find("beyond the context") != std::string::npos) return GULLIVER_E_CONTEXT_OVERFLOW;
        return code_of(ex);
    }
    catch (...)
    {
        copy_message("unknown error", error, error_capacity);
        return GULLIVER_E_INTERNAL;
    }
}

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_cancel(gulliver_engine* engine, uint64_t request_id)
{
    if (!engine) return GULLIVER_E_INVALID_ARGUMENT;
    try
    {
        engine->impl->cancel(request_id);
        return GULLIVER_OK;
    }
    catch (...)
    {
        return GULLIVER_E_INTERNAL;
    }
}

}
