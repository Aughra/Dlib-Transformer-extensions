// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_TOOLS_CLIENT_SERVICE_H_
#define GULLIVER_TOOLS_CLIENT_SERVICE_H_

/*
    Le client des outils et des tests qui parlent à un service Gulliver de ce Mac (Gulliver 7.20.0, à la place des
    urllib.request des outils Python) : HTTPS par le TLS d'Apple (host/tls_client.h), sur 127.0.0.1, en vérifiant le
    certificat du service avec sa propre autorité (certificates/gulliver-authority.crt de son dossier de données),
    là où les outils Python acceptaient tout certificat.
*/

#include <map>
#include <string>

#include <httplib.h>

#include "../common/json.h"
#include "../common/platform.h"
#include "../host/tls_client.h"

namespace gulliver::essai
{
    struct service_target
    {
        std::string home;   // le dossier de données du service (GULLIVER_HOME), qui donne l'autorité
        int port = 18443;
        int timeout_seconds = 60;
    };

    struct service_reply
    {
        int status = 0;     // 0 : pas de réponse
        json body;          // toujours un objet (vide quand la réponse n'est pas du JSON)
        std::string text;   // le corps tel quel
    };

    // Le port des réglages du service (settings.json de home), ou fallback.
    inline int service_port(const std::string& home, int fallback = 18443)
    {
        std::string text;
        if (!read_file(join_path(home, "settings.json"), text, 1 << 20)) return fallback;
        const json j = json::parse_or_null(text);
        return j["port"].is_number() ? static_cast<int>(j["port"].as_int(fallback)) : fallback;
    }

    inline service_reply service_call(const service_target& t, const std::string& method, const std::string& path,
        const json* body = nullptr, const std::string& token = "", const std::map<std::string, std::string>& headers = {})
    {
        service_reply out;
        tls_route route;
        route.host = "localhost";
        route.port = t.port;
        route.address = "127.0.0.1";
        route.ca_path = join_path(t.home, "certificates/gulliver-authority.crt");
        std::string error;
        auto cl = https_client(route, error);
        if (!cl) return out;
        cl->set_keep_alive(false);
        cl->set_read_timeout(t.timeout_seconds, 0);
        httplib::Headers h;
        if (!token.empty()) h.emplace("X-Gulliver-Session", token);
        for (const auto& kv : headers) h.emplace(kv.first, kv.second);
        const std::string data = body ? body->dump() : std::string();
        const char* type = "application/json";
        httplib::Result r;
        if (method == "GET") r = cl->Get(path, h);
        else if (method == "DELETE") r = data.empty() ? cl->Delete(path, h) : cl->Delete(path, h, data, type);
        else if (method == "PUT") r = cl->Put(path, h, data, type);
        else r = cl->Post(path, h, data, type);
        if (!r) return out;
        out.status = r->status;
        out.text = r->body;
        out.body = json::parse_or_null(r->body);
        if (!out.body.is_object()) out.body = json::object();
        return out;
    }

    inline service_reply service_call(const service_target& t, const std::string& method, const std::string& path,
        const json& body, const std::string& token = "", const std::map<std::string, std::string>& headers = {})
    {
        return service_call(t, method, path, &body, token, headers);
    }

    inline json object_of(std::initializer_list<std::pair<std::string, json>> kv)
    {
        json o = json::object();
        for (const auto& p : kv) o[p.first] = p.second;
        return o;
    }
}

#endif // GULLIVER_TOOLS_CLIENT_SERVICE_H_
