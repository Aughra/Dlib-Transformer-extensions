// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Les routes du serveur HTTPS local :

        /                         l'application web (fichiers du dossier web)
        /docs/                    la documentation interactive de l'interface
        /openapi.json             sa description OpenAPI
        /v1/models                interface compatible OpenAI
        /v1/chat/completions
        /v1/completions
        /v1/embeddings
        /gulliver/v1/...           l'interface de l'application web

    Une version personnelle : le service n'écoute que sur 127.0.0.1, et toute requête qu'il
    reçoit peut tout faire, l'application web changeant les réglages, les modèles et
    l'installation.  Il ne demande plus
    de jeton d'administration, et ne vérifie plus les en-têtes Host et Origin (un choix du
    propriétaire de ce fork, qui sait qu'un site web ouvert dans le navigateur pourrait alors
    piloter le service).
*/


#include <dlib/external/httplib/httplib.h>
#include <mbedtls/build_info.h>
#include <sqlite3.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <memory>
#include <ctime>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <thread>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <mutex>

#include "../common/engine_api.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "carnet.h"
#include "crypto.h"
#include "http_fetch.h"
#include "hub.h"
#include "rag_engine.h"
#include "relay.h"
#include "service.h"
#include "web_access.h"

namespace gulliver
{

    namespace
    {
        // Corps des routes JSON ; un modèle est envoyé à sa propre route, sans cette limite.
        const uint64_t max_json_body = 64u << 20;
        // Les valeurs d'un corps, chacune occupant une centaine d'octets une fois lue : plus que les
        // 300 000 tokens qu'une requête d'embeddings peut lister.
        const size_t max_body_values = size_t(1) << 19;
        /*
            Les corps au-delà de large_body sont lus pour max_large_bodies requêtes à la fois : un
            corps de 64 Mo, gardé et analysé par chacun des 64 threads du serveur, prendrait plus de
            mémoire que l'ordinateur n'en a.  Le compte est pris avant la lecture du corps, par le
            thread qui le lit (holding_large_body), et rendu une fois la réponse écrite ou
            abandonnée, réponse en flux comprise : la requête garde son texte en mémoire jusque-là
            (give_back_after_answer()).
        */
        const uint64_t large_body = 8u << 20;
        const int max_large_bodies = 4;
        std::atomic<int> large_bodies{0};
        thread_local bool holding_large_body = false;
        /*
            Les valeurs des documents construits et gardés à la fois, chacune occupant une centaine
            d'octets quelle que soit sa longueur dans le texte : un corps d'un mégaoctet de petites
            valeurs en construit quelque 60 Mo, que les threads du serveur ensemble multiplieraient.
            Un corps réserve les valeurs que son texte peut contenir avant d'être analysé
            (parse_body()), et les rend une fois son document libéré, par le même thread
            (holding_values).
        */
        const size_t max_held_values = 4 * (max_body_values + 1);
        std::atomic<size_t> held_values{0};
        thread_local size_t holding_values = 0;
        // Les événements d'une réponse en flux qui attendent un client lisant bien plus lentement que le moteur
        // n'écrit.
        const size_t max_backlog = 16384;

        const char* const upload_route = "/gulliver/v1/models/upload";
        // Les documents du Mac (documents_dir()), envoyés dans leurs propres octets à cette route.
        const char* const documents_route = "/gulliver/v1/documents";
        const uint64_t max_document_bytes = 512ull << 20;

        void send_json(httplib::Response& res, int status, const json& j)
        {
            res.status = status;
            res.set_content(j.dump(), "application/json; charset=utf-8");
        }

        // Le corps d'erreur de l'interface OpenAI, param nommant le membre de la requête qu'elle concerne.
        void send_error(httplib::Response& res, int status, const std::string& type, const std::string& message,
            const std::string& code = std::string(), const std::string& param = std::string())
        {
            json e = json::object();
            json inner = json::object();
            inner["message"] = message;
            inner["type"] = type;
            inner["param"] = param.empty() ? json() : json(param);
            inner["code"] = code.empty() ? json() : json(code);
            e["error"] = inner;
            send_json(res, status, e);
        }

        // Un profil rendu à la page : son id, son nom et ses dates.
        json profile_json(const profile& p)
        {
            json o = json::object();
            o["id"] = p.id;
            o["name"] = p.name;
            o["created"] = static_cast<long long>(p.created);
            o["last_used"] = static_cast<long long>(p.last_used);
            return o;
        }

        // Un index nommé rendu à la page.
        json index_json(const index_record& r)
        {
            json o = json::object();
            o["id"] = r.id;
            o["name"] = r.name;
            o["embed_model"] = r.embed_model;
            o["passage_chars"] = static_cast<long long>(r.passage_chars);
            o["overlap_chars"] = static_cast<long long>(r.overlap_chars);
            o["created"] = static_cast<long long>(r.created);
            o["active"] = r.active;
            return o;
        }

        // Les métadonnées d'une image rendues à la page (sans les octets, qui se lisent par /blob et /thumb).
        json image_json(const image_record& r)
        {
            json o = json::object();
            o["id"] = r.id;
            o["owner"] = r.owner;
            o["name"] = r.name;
            o["type"] = r.type;
            o["size"] = static_cast<long long>(r.size);
            o["width"] = static_cast<long long>(r.width);
            o["height"] = static_cast<long long>(r.height);
            o["added"] = static_cast<long long>(r.added);
            o["bytes"] = static_cast<long long>(r.blob_bytes + r.thumb_bytes);
            return o;
        }

        // Un passage (chunk) rendu à la page : ses champs, et son vecteur en base64 (vide si absent).
        json chunk_json(const chunk_record& c)
        {
            json o = json::object();
            o["id"] = c.id;
            o["owner"] = c.owner;
            o["doc"] = c.doc;
            o["ordinal"] = static_cast<long long>(c.ordinal);
            o["name"] = c.name;
            o["where"] = c.where;
            o["start"] = static_cast<long long>(c.start);
            o["end"] = static_cast<long long>(c.end);
            o["text"] = c.text;
            o["model"] = c.model;
            o["vector"] = c.vector.empty() ? std::string()
                : base64_encode(std::string(c.vector.begin(), c.vector.end()));
            return o;
        }

        // Un passage lu d'un objet JSON du corps : le vecteur arrive en base64.
        chunk_record chunk_of_json(const json& j)
        {
            chunk_record c;
            c.id = j.contains("id") ? j["id"].as_string() : std::string();
            c.owner = j.contains("owner") ? j["owner"].as_string() : std::string();
            c.doc = j.contains("doc") ? j["doc"].as_string() : std::string();
            c.ordinal = j.contains("ordinal") ? j["ordinal"].as_int(0) : 0;
            c.name = j.contains("name") ? j["name"].as_string() : std::string();
            c.where = j.contains("where") ? j["where"].as_string() : std::string();
            c.start = j.contains("start") ? j["start"].as_int(0) : 0;
            c.end = j.contains("end") ? j["end"].as_int(0) : 0;
            c.text = j.contains("text") ? j["text"].as_string() : std::string();
            c.model = j.contains("model") ? j["model"].as_string() : std::string();
            if (j.contains("vector") && !j["vector"].as_string().empty())
            {
                const std::string bytes = base64_decode(j["vector"].as_string());
                c.vector.assign(bytes.begin(), bytes.end());
            }
            return c;
        }

        // Le nom d'un profil pris dans un corps de requête : coupé de ses espaces de bord, de 1 à 80
        // caractères.  Renvoie une chaîne vide quand il manque ou qu'il est hors bornes.
        std::string profile_name_of(const json& body)
        {
            if (!body.contains("name")) return std::string();
            std::string name = body["name"].as_string();
            const auto not_space = [](unsigned char c) { return !std::isspace(c); };
            name.erase(name.begin(), std::find_if(name.begin(), name.end(), not_space));
            name.erase(std::find_if(name.rbegin(), name.rend(), not_space).base(), name.end());
            if (name.empty() || name.size() > 80) return std::string();
            return name;
        }

        /*
            Le profil d'une requête de données, lu dans l'en-tête X-Gulliver-Profile.  Renvoie true
            et remplit `out` quand l'en-tête nomme un profil connu (dont il note l'usage), sinon
            envoie l'erreur et renvoie false : 400 profile_required si l'en-tête manque, 404
            profile_unknown s'il nomme un profil qui n'existe pas (ou plus).  Les routes de données
            d'un profil commencent toutes par cet appel.
        */

        bool resolve_profile(service& svc, const httplib::Request& req, httplib::Response& res, profile& out)
        {
            const std::string id = req.get_header_value("X-Gulliver-Profile");
            if (id.empty())
            {
                send_error(res, 400, "invalid_request_error",
                    "this request needs a profile: send its id in the X-Gulliver-Profile header", "profile_required");
                return false;
            }
            if (!svc.db().find_profile(id, out))
            {
                send_error(res, 404, "invalid_request_error",
                    "no profile has this id; create one or choose another", "profile_unknown");
                return false;
            }
            svc.db().touch_profile(id, now_unix());
            return true;
        }

        /*
            La réponse à une requête dont le modèle n'est pas encore dans le moteur, parce que
            le service démarre ou que le modèle se charge : 503 avec un code et le délai après
            lequel réessayer.  Renvoie false quand le modèle manque définitivement.
        */
        bool send_not_ready(service& svc, httplib::Response& res, const std::string& requested)
        {
            const std::string phase = svc.starting_phase();
            if (!phase.empty())
            {
                send_error(res, 503, "server_error", "the service is starting (" + phase + ")", "starting");
            }
            else if (svc.model_loading(requested))
            {
                send_error(res, 503, "server_error", "the model is loading", "model_loading");
            }
            else
            {
                return false;
            }
            res.set_header("Retry-After", "5");
            return true;
        }

        /*
            Les réponses de Hugging Face gardées dans la base (fork pour macOS), pour les montrer hors ligne ou
            quand Hugging Face ne répond pas : les 30 dernières recherches ("search", par texte cherché) et
            listes de fichiers ("files", par dépôt), chacune avec sa date ("at", en secondes Unix).
        */
        std::mutex hub_cache_m;

        void hub_cache_put(store& db, const char* kind, const std::string& key, const json& value)
        {
            std::lock_guard<std::mutex> lock(hub_cache_m);
            json all;
            if (!json::parse(db.value("hub_cache", "{}"), all) || !all.is_object()) all = json::object();
            if (!all[kind].is_object()) all[kind] = json::object();
            json entry = json::object();
            entry["at"] = static_cast<long long>(std::time(nullptr));
            entry["value"] = value;
            all[kind][key] = entry;
            while (all[kind].size() > 30)
            {
                std::string oldest;
                long long at = 0;
                for (const auto& kv : all[kind].items())
                    if (oldest.empty() || kv.second["at"].as_int(0) < at)
                    {
                        oldest = kv.first;
                        at = kv.second["at"].as_int(0);
                    }
                all[kind].erase(oldest);
            }
            db.set_value("hub_cache", all.dump());
        }

        bool hub_cache_get(store& db, const char* kind, const std::string& key, json& value)
        {
            std::lock_guard<std::mutex> lock(hub_cache_m);
            json all;
            if (!json::parse(db.value("hub_cache", "{}"), all) || !all[kind][key].is_object()) return false;
            value = all[kind][key]["value"];
            value["cached_at"] = all[kind][key]["at"].as_int(0);
            return true;
        }

        // Les recherches gardées, la plus récente d'abord : {"query", "at"}.
        json hub_cache_recent(store& db)
        {
            std::lock_guard<std::mutex> lock(hub_cache_m);
            json all, out = json::array();
            if (!json::parse(db.value("hub_cache", "{}"), all)) return out;
            std::vector<std::pair<long long, std::string>> list;
            for (const auto& kv : all["search"].items()) list.emplace_back(kv.second["at"].as_int(0), kv.first);
            std::sort(list.rbegin(), list.rend());
            for (const auto& e : list)
            {
                json o = json::object();
                o["query"] = e.second;
                o["at"] = e.first;
                out.push_back(o);
            }
            return out;
        }

        json ok_result(bool ok, const std::string& error = std::string())
        {
            json o = json::object();
            o["ok"] = ok;
            if (!error.empty()) o["error"] = error;
            return o;
        }

        // Le résultat d'une opération des fenêtres, avec le code de son erreur.
        json ok_result(bool ok, const user_error& error)
        {
            json o = json::object();
            o["ok"] = ok;
            if (!ok) error.add_to(o);
            return o;
        }

        // Le maximum de valeurs que contient un texte JSON : une, plus une par virgule, crochet et accolade hors
        // de ses chaînes.
        size_t value_bound(const std::string& text)
        {
            size_t n = 1;
            bool in_string = false, escaped = false;
            for (const char c : text)
            {
                if (in_string)
                {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') in_string = false;
                }
                else if (c == '"')
                {
                    in_string = true;
                }
                else if (c == ',' || c == '[' || c == '{')
                {
                    ++n;
                }
            }
            return n;
        }

        /*
            Rend le compte du gros corps de req une fois qu'httplib a écrit sa réponse, ce qui suit
            le gestionnaire post-routage, ou l'a abandonnée : la libération du contenu de la
            réponse, quand res disparaît, le rend.  Les octets d'une réponse qui n'est pas un
            contenu en deviennent un ; une réponse en flux le rend après sa propre libération.
            Une réponse à une requête de plages d'octets le rend aussitôt.
        */
        void give_back_after_answer(const httplib::Request& req, httplib::Response& res)
        {
            if (!req.ranges.empty())
            {
                --large_bodies;
                return;
            }
            if (res.content_provider_)
            {
                auto release = std::move(res.content_provider_resource_releaser_);
                res.content_provider_resource_releaser_ = [release](bool success) {
                    if (release) release(success);
                    --large_bodies;
                };
                return;
            }
            auto text = std::make_shared<std::string>(std::move(res.body));
            const std::string type = res.get_header_value("Content-Type");
            res.set_content_provider(text->size(), type,
                [text](size_t offset, size_t length, httplib::DataSink& sink) {
                    return sink.write(text->data() + offset, length);
                },
                [](bool) { --large_bodies; });
            if (type.empty()) res.headers.erase("Content-Type");
        }

        // Rend les valeurs que le corps de la requête de ce thread a réservées.
        void release_values()
        {
            if (holding_values == 0) return;
            held_values -= holding_values;
            holding_values = 0;
        }

        // Réserve n valeurs pour le corps de la requête de ce thread ; false quand le budget n'a pas de place pour
        // elles.
        bool reserve_values(size_t n)
        {
            release_values();
            size_t current = held_values.load();
            do
            {
                if (current + n > max_held_values) return false;
            } while (!held_values.compare_exchange_weak(current, current + n));
            holding_values = n;
            return true;
        }

        bool parse_body(const httplib::Request& req, httplib::Response& res, json& body)
        {
            std::string error;
            if (req.body.empty())
            {
                body = json::object();
                return true;
            }
            // L'analyseur s'arrête après max_body_values valeurs : aucun document n'en contient plus.
            if (!reserve_values(std::min(value_bound(req.body), max_body_values + 1)))
            {
                res.set_header("Retry-After", "2");
                send_error(res, 503, "server_error", "too many large documents are being read at once: try again",
                    "busy");
                return false;
            }
            if (!json::parse(req.body, body, &error, 64, max_body_values) || !body.is_object())
            {
                send_error(res, 400, "invalid_request_error", "the body is not a JSON object: " + error);
                return false;
            }
            return true;
        }

        std::string lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // Les réglages que contient un instantané de /gulliver/v1/settings/profiles : ceux que change l'application
        // web.
        const char* const profile_keys[] = { "accelerator", "threads", "max_sequences", "context", "kv_mb", "kv_type",
            "warm_history", "spin_us", "gpu_prompt_tokens", "gpu_prompt_context", "lan", "lan_port", "port",
            "models_dir", "offline", "web_access", "web_search", "proxy" };
        std::mutex profiles_m;

        json read_profiles(service& svc)
        {
            std::string text;
            json list;
            if (!read_file(join_path(svc.data_dir(), "settings-profiles.json"), text, 4u << 20)
                || !json::parse(text, list) || !list.is_array())
                return json::array();
            return list;
        }

        // Un document du dossier des documents : un simple nom de fichier, ni caché ni dossier.
        bool document_name(const std::string& name)
        {
            if (name.empty() || name.size() > 255 || name[0] == '.' || name == "..") return false;
            for (const unsigned char c : name)
                if (c < 32 || c == '/' || c == '\\' || c == 127) return false;
            return true;
        }

        // Le type d'un document, d'après son extension, pour le navigateur qui le lit.
        std::string document_type(const std::string& name)
        {
            const size_t dot = name.rfind('.');
            const std::string ext = dot == std::string::npos ? std::string() : lower(name.substr(dot + 1));
            static const std::pair<const char*, const char*> types[] = {
                { "pdf", "application/pdf" }, { "txt", "text/plain; charset=utf-8" }, { "md", "text/markdown; charset=utf-8" },
                { "csv", "text/csv; charset=utf-8" }, { "json", "application/json" }, { "html", "text/html; charset=utf-8" },
                { "htm", "text/html; charset=utf-8" }, { "xml", "application/xml" },
                { "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
                { "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
                { "pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
                { "odt", "application/vnd.oasis.opendocument.text" }, { "rtf", "application/rtf" },
                { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "gif", "image/gif" },
                { "webp", "image/webp" }, { "heic", "image/heic" }, { "wav", "audio/wav" }, { "mp3", "audio/mpeg" },
                { "m4a", "audio/mp4" } };
            for (const auto& t : types)
                if (ext == t.first) return t.second;
            return "application/octet-stream";
        }

        // Les adresses IPv4 du Mac sur ses réseaux, hormis celles de bouclage et de lien local.
        json lan_addresses()
        {
            json out = json::array();
            ifaddrs* list = nullptr;
            if (getifaddrs(&list) != 0) return out;
            for (ifaddrs* a = list; a; a = a->ifa_next)
            {
                if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK)
                    || !(a->ifa_flags & IFF_UP))
                    continue;
                char text[INET_ADDRSTRLEN] = {};
                const in_addr ip = reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr;
                if (!inet_ntop(AF_INET, &ip, text, sizeof(text))) continue;
                const std::string t = text;
                if (t.compare(0, 8, "169.254.") == 0) continue;
                bool seen = false;
                for (const auto& e : out.elements()) seen = seen || e.as_string() == t;
                if (!seen) out.push_back(t);
            }
            freeifaddrs(list);
            return out;
        }

        // Les réglages tels que les lit l'application web, avec les adresses de l'accès réseau.
        json public_settings(service& svc)
        {
            json j = svc.settings().get().to_json();
            j["lan_addresses"] = lan_addresses();
            return j;
        }


        struct event_channel
        {
            std::mutex m;
            std::condition_variable cv;
            std::deque<std::pair<int, std::string>> q;
            bool finished = false;

            void push(int kind, const std::string& data)
            {
                std::lock_guard<std::mutex> lock(m);
                q.emplace_back(kind, data);
                if (kind == GULLIVER_EVENT_DONE || kind == GULLIVER_EVENT_ERROR) finished = true;
                cv.notify_all();
            }

            bool pop(std::pair<int, std::string>& e, int ms)
            {
                std::unique_lock<std::mutex> lock(m);
                if (!cv.wait_for(lock, std::chrono::milliseconds(ms), [&]() { return !q.empty(); })) return false;
                e = std::move(q.front());
                q.pop_front();
                return true;
            }

            size_t pending()
            {
                std::lock_guard<std::mutex> lock(m);
                return q.size();
            }
        };

        /*
            Indique si le client d'une requête est parti : sa connexion fermée, ou terminée par un
            close_notify TLS, dont les octets attendent sur la socket là où la vérification
            d'httplib voit un client vivant.  La sonde TLS est celle qu'httplib fait pour les
            réponses qu'il envoie en flux, et rend à la lecture suivante l'octet qu'elle prend
            d'une requête qui suit.
        */
        bool client_left(const httplib::Request& req)
        {
            if (req.is_connection_closed()) return true;
#if defined(CPPHTTPLIB_MBEDTLS_SUPPORT)
            auto session = static_cast<httplib::tls::impl::MbedTlsSession*>(const_cast<void*>(req.ssl));
            return session && session->sock != INVALID_SOCKET
                && httplib::tls::is_peer_closed(session, session->sock);
#else
            return false;
#endif
        }

        /*
            Termine la requête d'une réponse envoyée en entier, par cancel, quand le service
            s'arrête, avec 503, ou quand son client est parti.  Les examine au plus une fois par
            seconde, que les événements du moteur arrivent ou non ; renvoie true quand elle a
            terminé la requête.
        */
        bool end_abandoned(service& svc, const httplib::Request& req, httplib::Response& res,
            const std::function<void()>& cancel, std::chrono::steady_clock::time_point& next_look)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < next_look) return false;
            next_look = now + std::chrono::seconds(1);
            if (svc.stopping())
            {
                cancel();
                send_error(res, 503, "server_error", "the service is stopping");
                return true;
            }
            // Un client parti termine sa requête, dont les autres prennent la place dans le moteur.
            if (client_left(req))
            {
                cancel();
                return true;
            }
            return false;
        }

        /*
            La requête moteur d'une complétion de l'interface OpenAI.  Les membres qu'elle prend
            sont déplacés hors de source, le corps de la requête, pour que les images et les
            conversations d'une grosse requête ne restent qu'une fois en mémoire ; source garde les
            membres qu'elle ne prend pas.
        */
        bool engine_request(service& svc, json& source, json& out, std::string& error, std::string& model_name)
        {
            // Lu par une référence const, qui n'ajoute aucun membre au corps.
            const json& body = source;
            const std::string requested = body["model"].as_string();
            const std::string model = svc.resolve_model(requested);
            if (model.empty())
            {
                error = requested.empty() || requested == "auto" ? std::string("no model is active")
                    : "the model '" + requested + "' is not active";
                return false;
            }
            model_name = requested.empty() ? model : requested;
            if (body["n"].as_int(1) != 1)
            {
                error = "only one choice per request (n = 1) is supported";
                return false;
            }
            // La plage de top_k dans le moteur, qui borne la valeur qu'il reçoit : une valeur hors de cette
            // plage est refusée ici, pour que le client l'apprenne.
            const json& top_k = body["top_k"];
            if (!top_k.is_null() && !(top_k.is_number() && top_k.as_double() >= 0 && top_k.as_double() <= 1024
                && top_k.as_double() == std::floor(top_k.as_double())))
            {
                error = "top_k must be an integer from 0 to 1024, 0 for no limit";
                return false;
            }
            out = json::object();
            out["model"] = model;
            static const char* const taken[] = { "messages", "prompt", "tools", "tool_choice", "max_tokens",
                "max_completion_tokens", "temperature", "top_p", "top_k", "min_p", "stop", "seed", "presence_penalty",
                "frequency_penalty", "repeat_penalty", "repetition_penalty", "repeat_last_n", "logit_bias",
                "reasoning_effort", "reasoning", "reasoning_budget", "chat_template_kwargs" };
            for (const char* k : taken)
                for (auto& member : source.items())
                    if (member.first == k)
                    {
                        out[k] = std::move(member.second);
                        break;
                    }
            if (body["think"].is_bool()) out["reasoning"] = body["think"];
            // Le travail de fond de l'application web, dont les réponses laissent aux prompts leur passe entière.
            if (requested == "gulliver-background") out["priority"] = "background";
            return true;
        }

        // Le maximum d'images d'une requête, que le moteur lit dans le prompt.
        const size_t max_images = 16;

        // Une requête refusée avant d'atteindre le moteur, sous la forme de l'interface OpenAI.
        struct refusal
        {
            std::string message;
            std::string code;
            std::string param;
        };

        /*
            Les parties des messages au-delà du texte : "image_url", une image fournie comme URL
            data d'un fichier PNG ou JPEG, puisque le service ne télécharge rien (un ordinateur sans
            réseau lit les mêmes requêtes), max_images au plus, et "input_audio".  Les compte ;
            renvoie false avec le refus de la première partie qui ne peut pas être lue.
        */
        bool check_media(const json& messages, size_t& images, size_t& sounds, refusal& r)
        {
            images = 0;
            sounds = 0;
            for (size_t i = 0; i < messages.size(); ++i)
            {
                const json& content = messages[i]["content"];
                for (size_t k = 0; k < content.size() && content.is_array(); ++k)
                {
                    const json& part = content[k];
                    const std::string kind = part["type"].as_string();
                    const std::string at = "messages[" + std::to_string(i) + "].content[" + std::to_string(k) + "]";
                    if (kind == "input_audio") ++sounds;
                    if (kind != "image_url") continue;
                    ++images;
                    const json& image = part["image_url"];
                    const std::string url = image.is_string() ? image.as_string() : image["url"].as_string();
                    if (lower(url.substr(0, 5)) != "data:")
                    {
                        r.message = "the service fetches no image: an image is given in the request as a data URL, "
                            "data:image/png;base64,... or data:image/jpeg;base64,...";
                        r.code = "invalid_image_url";
                        r.param = at + ".image_url.url";
                        return false;
                    }
                    // data:<media type>;base64,<data>, d'un fichier PNG ou JPEG d'après ses premiers octets.
                    const size_t comma = url.find(',');
                    const std::string header = lower(url.substr(5, comma == std::string::npos ? 0 : comma - 5));
                    const std::string data = comma == std::string::npos ? std::string() : url.substr(comma + 1, 16);
                    const std::string bytes = base64_decode(data.substr(0, data.size() / 4 * 4));
                    const bool png = bytes.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0;
                    const bool jpeg = bytes.compare(0, 3, std::string("\xFF\xD8\xFF", 3)) == 0;
                    if ((header != "image/png;base64" || !png) && (header != "image/jpeg;base64" || !jpeg))
                    {
                        r.message = "an image is a PNG or JPEG file in base64: data:image/png;base64,... or "
                            "data:image/jpeg;base64,...";
                        r.code = "invalid_image_format";
                        r.param = at + ".image_url.url";
                        return false;
                    }
                    const std::string detail = image["detail"].as_string();
                    if (!detail.empty() && detail != "auto" && detail != "low" && detail != "high")
                    {
                        r.message = "the detail of an image is auto, low or high";
                        r.code = "invalid_value";
                        r.param = at + ".image_url.detail";
                        return false;
                    }
                }
            }
            if (images > max_images)
            {
                r.message = "a request carries " + std::to_string(max_images) + " images at most, this one "
                    + std::to_string(images);
                r.code = "too_many_images";
                r.param = "messages";
                return false;
            }
            return true;
        }

        json usage_of(const json& done)
        {
            const json& u = done["usage"];
            json usage = json::object();
            usage["prompt_tokens"] = u["prompt_tokens"];
            usage["completion_tokens"] = u["completion_tokens"];
            usage["total_tokens"] = u["total_tokens"];
            json details = json::object();
            details["cached_tokens"] = u["cached_tokens"];
            usage["prompt_tokens_details"] = details;
            return usage;
        }

        json tool_call_of(const json& c, int index)
        {
            json call = json::object();
            if (index >= 0) call["index"] = index;
            call["id"] = c["id"];
            call["type"] = "function";
            json fn = json::object();
            fn["name"] = c["name"];
            fn["arguments"] = c["arguments"];
            call["function"] = fn;
            return call;
        }

        /*
            Écrit la réponse dont ch reçoit les événements, en entier ou en flux d'événements, sous
            la forme de l'interface OpenAI : la réponse du moteur, ou celle du relais (relay.h).
            cancel termine sa requête quand le client part ou que le service s'arrête ; count_usage
            ajoute ses tokens à l'usage du moteur.
        */
        void write_answer(service& svc, const httplib::Request& req, httplib::Response& res,
            const std::shared_ptr<event_channel>& ch, const std::string& model_name, bool chat, bool stream,
            bool include_usage, const std::function<void()>& cancel, bool count_usage)
        {
            const std::string cid = std::string(chat ? "chatcmpl-" : "cmpl-") + random_hex(24);
            const long long created = static_cast<long long>(now_unix());

            if (!stream)
            {
                std::string content, reasoning;
                json calls = json::array();
                json done;
                bool failed = false;
                auto next_look = std::chrono::steady_clock::now();
                for (;;)
                {
                    std::pair<int, std::string> e;
                    if (ch->pop(e, 1000))
                    {
                        if (e.first == GULLIVER_EVENT_CONTENT) content += e.second;
                        else if (e.first == GULLIVER_EVENT_REASONING) reasoning += e.second;
                        else if (e.first == GULLIVER_EVENT_TOOL_CALL)
                            calls.push_back(tool_call_of(json::parse_or_null(e.second), -1));
                        else if (e.first == GULLIVER_EVENT_DONE || e.first == GULLIVER_EVENT_ERROR)
                        {
                            done = json::parse_or_null(e.second);
                            failed = e.first == GULLIVER_EVENT_ERROR;
                            break;
                        }
                    }
                    if (end_abandoned(svc, req, res, cancel, next_look)) return;
                }
                if (failed)
                {
                    /*
                        Les échecs du moteur extérieur du relais sont ceux d'une passerelle ; un
                        Pseudalis absent ou un moteur hors de portée du mode hors ligne rendent le
                        relais indisponible, et une image qu'aucun modèle du Mac ne décrit est
                        refusée comme une image du moteur.
                    */
                    const std::string code = done["code"].as_string();
                    const int status = code.compare(0, 6, "relay_") == 0 ? 502
                        : code == "pseudonymization_failed" || code == "offline" ? 503
                        : code == "image_input_not_supported" ? 400 : 500;
                    send_error(res, status, status == 400 ? "invalid_request_error" : "server_error",
                        done["message"].as_string(), code);
                    return;
                }
                if (count_usage)
                    svc.record_usage(static_cast<long>(done["usage"]["prompt_tokens"].as_int(0)),
                        static_cast<long>(done["usage"]["completion_tokens"].as_int(0)));
                json out = json::object();
                out["id"] = cid;
                out["object"] = chat ? "chat.completion" : "text_completion";
                out["created"] = created;
                out["model"] = model_name;
                json choice = json::object();
                choice["index"] = 0;
                if (chat)
                {
                    json msg = json::object();
                    msg["role"] = "assistant";
                    msg["content"] = content.empty() && !calls.empty() ? json() : json(content);
                    if (!reasoning.empty()) msg["reasoning_content"] = reasoning;
                    if (!calls.empty()) msg["tool_calls"] = calls;
                    choice["message"] = msg;
                }
                else
                {
                    choice["text"] = content;
                }
                choice["logprobs"] = nullptr;
                choice["finish_reason"] = done["finish_reason"];
                json choices = json::array();
                choices.push_back(choice);
                out["choices"] = choices;
                out["usage"] = usage_of(done);
                out["timings"] = done["timings"];
                // Une réponse que le moteur a arrêtée parce qu'elle se répétait.
                if (done["repetition"].as_bool(false)) out["repetition"] = true;
                out["system_fingerprint"] = std::string("gulliver-") + GULLIVER_VERSION;
                send_json(res, 200, out);
                return;
            }

            // Le gestionnaire post-routage marque les réponses de l'interface "no-store".
            res.set_header("X-Accel-Buffering", "no");
            auto first = std::make_shared<bool>(true);
            auto tool_index = std::make_shared<int>(0);
            service* s = &svc;
            res.set_chunked_content_provider("text/event-stream",
                [ch, cid, created, model_name, chat, include_usage, first, tool_index, s, cancel, count_usage](
                    size_t, httplib::DataSink& sink) {
                    auto chunk = [&](const json& delta, const json& finish, bool repetition) {
                        json c = json::object();
                        c["id"] = cid;
                        c["object"] = chat ? "chat.completion.chunk" : "text_completion";
                        c["created"] = created;
                        c["model"] = model_name;
                        // Le dernier morceau d'une réponse que le moteur a arrêtée parce qu'elle se répétait.
                        if (repetition) c["repetition"] = true;
                        json choice = json::object();
                        choice["index"] = 0;
                        if (chat) choice["delta"] = delta;
                        else choice["text"] = delta["content"].is_string() ? delta["content"] : json("");
                        choice["logprobs"] = nullptr;
                        choice["finish_reason"] = finish;
                        json choices = json::array();
                        choices.push_back(choice);
                        c["choices"] = choices;
                        const std::string line = "data: " + c.dump() + "\n\n";
                        return sink.write(line.data(), line.size());
                    };
                    /*
                        Un service qui s'arrête termine les réponses en cours, dont les tokens
                        peuvent couler sans pause, et un client qui lit bien plus lentement que le
                        moteur n'écrit termine la sienne : les événements qui l'attendent restent
                        bornés, et sa place dans le moteur va aux autres requêtes.
                    */
                    if (s->stopping() || ch->pending() > max_backlog)
                    {
                        cancel();
                        return false;
                    }
                    std::pair<int, std::string> e;
                    if (!ch->pop(e, 2000))
                    {
                        if (!sink.is_writable() || s->stopping())
                        {
                            cancel();
                            return false;
                        }
                        static const char ping[] = ": keep-alive\n\n";
                        return sink.write(ping, sizeof(ping) - 1);
                    }
                    if (*first && chat)
                    {
                        *first = false;
                        json delta = json::object();
                        delta["role"] = "assistant";
                        delta["content"] = "";
                        if (!chunk(delta, json(), false)) return false;
                    }
                    json delta = json::object();
                    switch (e.first)
                    {
                    case GULLIVER_EVENT_CONTENT:
                        delta["content"] = e.second;
                        return chunk(delta, json(), false);
                    case GULLIVER_EVENT_REASONING:
                        delta["reasoning_content"] = e.second;
                        return chunk(delta, json(), false);
                    case GULLIVER_EVENT_TOOL_CALL:
                    {
                        json list = json::array();
                        list.push_back(tool_call_of(json::parse_or_null(e.second), (*tool_index)++));
                        delta["tool_calls"] = list;
                        return chunk(delta, json(), false);
                    }
                    case GULLIVER_EVENT_PROGRESS:
                    {
                        // La progression d'un long prompt, sous forme d'une ligne de commentaire que
                        // les clients du standard ignorent et que l'application web affiche.
                        const std::string line = ": progress " + e.second + "\n\n";
                        return sink.write(line.data(), line.size());
                    }
                    case GULLIVER_EVENT_DONE:
                    {
                        const json done = json::parse_or_null(e.second);
                        if (count_usage)
                            s->record_usage(static_cast<long>(done["usage"]["prompt_tokens"].as_int(0)),
                                static_cast<long>(done["usage"]["completion_tokens"].as_int(0)));
                        const bool repetition = done["repetition"].as_bool(false);
                        if (!chunk(json::object(), done["finish_reason"], repetition)) return false;
                        if (include_usage)
                        {
                            json c = json::object();
                            c["id"] = cid;
                            c["object"] = chat ? "chat.completion.chunk" : "text_completion";
                            c["created"] = created;
                            c["model"] = model_name;
                            c["choices"] = json::array();
                            c["usage"] = usage_of(done);
                            c["timings"] = done["timings"];
                            const std::string line = "data: " + c.dump() + "\n\n";
                            if (!sink.write(line.data(), line.size())) return false;
                        }
                        static const char end[] = "data: [DONE]\n\n";
                        sink.write(end, sizeof(end) - 1);
                        sink.done();
                        return true;
                    }
                    default:
                    {
                        const json err = json::parse_or_null(e.second);
                        json body = json::object();
                        json inner = json::object();
                        inner["message"] = err["message"];
                        inner["type"] = "server_error";
                        inner["code"] = err["code"];
                        body["error"] = inner;
                        const std::string line = "data: " + body.dump() + "\n\n";
                        sink.write(line.data(), line.size());
                        sink.done();
                        return true;
                    }
                    }
                },
                [ch, cancel](bool) {
                    // Annulée hors du verrou des événements, que la réponse prend pour donner les siens.
                    bool finished = false;
                    {
                        std::lock_guard<std::mutex> lock(ch->m);
                        finished = ch->finished;
                    }
                    if (!finished) cancel();
                });
        }

        /*
            Une complétion de chat du modèle gulliver-relay, que le relais envoie à son moteur
            extérieur (relay.h) : ses images vérifiées comme celles du moteur, sa réponse écrite
            comme les réponses du moteur, ses tokens laissés hors de l'usage du moteur.
        */
        void handle_relay(service& svc, const httplib::Request& req, httplib::Response& res, json& body, bool chat)
        {
            if (!chat)
            {
                send_error(res, 400, "invalid_request_error", "the relay serves the chat completions "
                    "(/v1/chat/completions) only", "relay_chat_only");
                return;
            }
            if (!body["messages"].is_array())
            {
                send_error(res, 400, "invalid_request_error", "messages must be an array");
                return;
            }
            size_t images = 0, sounds = 0;
            refusal refused;
            if (!check_media(body["messages"], images, sounds, refused))
            {
                send_error(res, 400, "invalid_request_error", refused.message, refused.code, refused.param);
                return;
            }
            // Un relais coupé ou incomplet, puis des images que ni le modèle extérieur ni un modèle du Mac ne prennent.
            const json relay = svc.relay_status();
            if (!relay["ready"].as_bool(false))
            {
                send_error(res, 404, "invalid_request_error", relay["reason"].as_string(), "relay_unavailable");
                return;
            }
            if (images > 0 && relay["image_mode"].as_string() == "none")
            {
                send_error(res, 400, "invalid_request_error", "the model of the relay reads no image, and no model of "
                    "this computer that reads images can describe them", "image_input_not_supported", "messages");
                return;
            }
            const bool stream = body["stream"].as_bool(false);
            const bool include_usage = body["stream_options"]["include_usage"].as_bool(false);
            auto ch = std::make_shared<event_channel>();
            uint64_t id = 0;
            std::string error, code;
            int status = 0;
            if (!svc.relay().submit(body, [ch](int kind, const std::string& data) { ch->push(kind, data); }, id, error,
                    status, code))
            {
                send_error(res, status, status >= 500 ? "server_error" : "invalid_request_error", error, code);
                if (status == 503) res.set_header("Retry-After", "5");
                return;
            }
            body = json();
            release_values();
            write_answer(svc, req, res, ch, relay_model_id, chat, stream, include_usage,
                [&svc, id]() { svc.relay().cancel(id); }, false);
        }

        void handle_completion(service& svc, const httplib::Request& req, httplib::Response& res, bool chat)
        {
            json body;
            if (!parse_body(req, res, body)) return;
            if (body["model"].as_string() == relay_model_id)
            {
                handle_relay(svc, req, res, body, chat);
                return;
            }
            json request;
            std::string error, model_name;
            if (!engine_request(svc, body, request, error, model_name))
            {
                // Un modèle résolu et une option invalide, ou aucun modèle dans le moteur.
                if (!model_name.empty()) send_error(res, 400, "invalid_request_error", error);
                else if (!send_not_ready(svc, res, body["model"].as_string()))
                    send_error(res, 404, "invalid_request_error", error, "model_not_found");
                return;
            }
            if (chat && !request["messages"].is_array())
            {
                send_error(res, 400, "invalid_request_error", "messages must be an array");
                return;
            }
            if (chat)
            {
                // Les images et les sons vont à un modèle qui les lit, tel que le moteur l'a chargé.
                const json& r = request;
                size_t images = 0, sounds = 0;
                refusal refused;
                if (!check_media(r["messages"], images, sounds, refused))
                {
                    send_error(res, 400, "invalid_request_error", refused.message, refused.code, refused.param);
                    return;
                }
                if (images > 0 || sounds > 0)
                {
                    bool image = false, audio = false;
                    const std::map<std::string, json> traits = svc.loaded_traits();
                    const auto t = traits.find(r["model"].as_string());
                    if (t != traits.end())
                        for (const auto& s : t->second["inputs"].elements())
                        {
                            image = image || s.as_string() == "image";
                            audio = audio || s.as_string() == "audio";
                        }
                    if (images > 0 && !image)
                    {
                        send_error(res, 400, "invalid_request_error", "the model " + model_name + " reads text only: "
                            "the images go to a model that reads them", "image_input_not_supported", "messages");
                        return;
                    }
                    if (sounds > 0 && !audio)
                    {
                        send_error(res, 400, "invalid_request_error", "the model " + model_name + " reads no sound",
                            "audio_input_not_supported", "messages");
                        return;
                    }
                }
            }
            if (!chat && !request["prompt"].is_string())
            {
                send_error(res, 400, "invalid_request_error", "prompt must be a string");
                return;
            }
            const bool stream = body["stream"].as_bool(false);
            const bool include_usage = body["stream_options"]["include_usage"].as_bool(false);
            auto ch = std::make_shared<event_channel>();
            uint64_t id = 0;
            auto sink = [ch](int kind, const std::string& data) { ch->push(kind, data); };
            int code = GULLIVER_OK;
            if (!svc.engine().submit(request, sink, id, error, &code))
            {
                if (code == GULLIVER_E_BUSY)
                {
                    // Refusée pour un temps, les images en file d'attente pour le modèle occupant la mémoire qui
                    // leur est réservée.
                    send_error(res, 503, "server_error", error, "engine_busy");
                    res.set_header("Retry-After", "5");
                    return;
                }
                const bool overflow = error.find("context") != std::string::npos;
                send_error(res, 400, "invalid_request_error", error, overflow ? "context_length_exceeded" : "");
                return;
            }
            // Le moteur garde sa propre copie de la requête : les documents construits ici disparaissent, avec leurs valeurs.
            body = json();
            request = json();
            release_values();
            write_answer(svc, req, res, ch, model_name, chat, stream, include_usage,
                [&svc, id]() { svc.engine().cancel(id); }, true);
        }

        // Indique si un texte des réglages du relais est court et ne contient aucun caractère de contrôle.
        bool plain_text(const json& v, size_t max_bytes)
        {
            if (!v.is_string() || v.as_string().size() > max_bytes) return false;
            for (const char c : v.as_string())
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
            return true;
        }

        /*
            Vérifie les membres du relais (relay_settings) que donne un changement : le secret et
            l'utilisateur vont dans les en-têtes des requêtes, où aucun caractère de contrôle ne
            peut se trouver.
        */
        bool validate_relay(const json& r, std::string& error)
        {
            static const char* const keys[] = { "enabled", "provider", "url", "model", "auth", "user", "secret",
                "context", "max_output", "images", "reasoning", "pseudonymize", "keep_substitutes" };
            if (!r.is_object())
            {
                error = "relay must be an object";
                return false;
            }
            for (const auto& kv : r.items())
                if (std::find_if(std::begin(keys), std::end(keys), [&](const char* k) { return kv.first == k; })
                    == std::end(keys))
                {
                    error = "the setting 'relay." + kv.first + "' does not exist";
                    return false;
                }
            auto fail = [&](const char* key) {
                error = std::string("invalid value for relay.") + key;
                return false;
            };
            for (const char* key : { "enabled", "images", "reasoning", "pseudonymize", "keep_substitutes" })
                if (r.contains(key) && !r[key].is_bool()) return fail(key);
            const std::string provider = r["provider"].as_string();
            if (r.contains("provider") && provider != "openai" && provider != "ollama" && provider != "claude")
                return fail("provider");
            const std::string auth = r["auth"].as_string();
            if (r.contains("auth") && auth != "key" && auth != "basic" && auth != "none") return fail("auth");
            if (r.contains("url") && !(plain_text(r["url"], 2048)
                    && (r["url"].as_string().empty() || valid_relay_url(r["url"].as_string()))))
                return fail("url");
            if (r.contains("model") && !plain_text(r["model"], 256)) return fail("model");
            if (r.contains("user")
                && !(plain_text(r["user"], 256) && r["user"].as_string().find(':') == std::string::npos))
                return fail("user");
            if (r.contains("secret") && !plain_text(r["secret"], 4096)) return fail("secret");
            const json& context = r["context"];
            if (r.contains("context")
                && !(context.is_integer() && context.as_int() >= 1024 && context.as_int() <= (1 << 24)))
                return fail("context");
            const json& output = r["max_output"];
            if (r.contains("max_output")
                && !(output.is_integer() && output.as_int() >= 16 && output.as_int() <= (1 << 20)))
                return fail("max_output");
            return true;
        }

        /*
            Le relais de current avec les membres que donne changes.  Le masque du secret tient lieu
            du secret enregistré seulement avec le fournisseur, l'adresse et l'utilisateur pour
            lesquels il a été donné, pour qu'il n'aille jamais à un autre serveur : false quand ils
            ont changé.
        */
        bool merge_relay(const relay_settings& current, const json& changes, relay_settings& out)
        {
            json merged = current.to_json();
            for (const auto& kv : changes.items()) merged[kv.first] = kv.second;
            out.from_json(merged);
            if (out.provider == "openai" && out.auth == "none")
            {
                out.secret.clear();
                return true;
            }
            if (out.secret != relay_secret_mask) return true;
            if (current.secret.empty() || out.secret_scope() != current.secret_scope()) return false;
            out.secret = current.secret;
            return true;
        }

        // Vérifie un changement des réglages envoyé par l'application web ; les clés hors de cette
        // liste ne se règlent qu'à la main dans settings.json.
        bool validate_settings(const json& body, std::string& error)
        {
            static const char* const keys[] = { "port", "language", "engine", "accelerator",
                "device_mb", "threads", "context", "max_sequences", "kv_mb", "kv_type", "offline",
                "proxy", "web_access", "web_search", "lan", "lan_port", "warm_history", "spin_us",
                "gpu_prompt_tokens", "gpu_prompt_context", "models_dir", "reserve_gb", "library_dir", "relay",
                "rag_engine", "rag_top_k" };
            for (const auto& kv : body.items())
            {
                if (std::find_if(std::begin(keys), std::end(keys), [&](const char* k) { return kv.first == k; })
                    == std::end(keys))
                {
                    error = "the setting '" + kv.first + "' cannot be changed through this interface";
                    return false;
                }
            }
            auto one_of = [&](const char* key, std::initializer_list<const char*> values) {
                if (!body.contains(key)) return true;
                for (const char* v : values)
                    if (body[key].as_string() == v) return true;
                error = std::string("invalid value for ") + key;
                return false;
            };
            auto in_range = [&](const char* key, long long lo, long long hi) {
                if (!body.contains(key)) return true;
                if (body[key].is_integer() && body[key].as_int() >= lo && body[key].as_int() <= hi) return true;
                error = std::string("invalid value for ") + key;
                return false;
            };
            auto boolean = [&](const char* key) {
                if (!body.contains(key) || body[key].is_bool()) return true;
                error = std::string("invalid value for ") + key;
                return false;
            };
            const json& proxy = body["proxy"];
            if (body.contains("proxy") && !(proxy.is_string() && valid_proxy_setting(proxy.as_string())))
            {
                error = "invalid value for proxy: http://host:port, \"none\" or empty";
                return false;
            }
            const json& models_dir = body["models_dir"];
            if (body.contains("models_dir") && !(models_dir.is_string()
                && (models_dir.as_string().empty() || dir_exists(models_dir.as_string()))))
            {
                error = "invalid value for models_dir: an existing folder, or empty for the folder of the data";
                return false;
            }
            // La bibliothèque du Mac : un dossier existant, nommé par son chemin complet, ou vide.
            const json& library_dir = body["library_dir"];
            if (body.contains("library_dir") && !(library_dir.is_string() && (library_dir.as_string().empty()
                || (library_dir.as_string()[0] == '/' && dir_exists(library_dir.as_string())))))
            {
                error = "invalid value for library_dir: an existing folder named by its full path, or empty for the "
                    "folder of the data";
                return false;
            }
            if (body.contains("relay") && !validate_relay(body["relay"], error)) return false;
            return one_of("web_search", { "wikipedia", "none" }) && boolean("web_access") && boolean("lan")
                && in_range("lan_port", 1024, 65535) && boolean("warm_history") && in_range("spin_us", 0, 100000)
                && in_range("gpu_prompt_tokens", 9, 1 << 20) && in_range("gpu_prompt_context", 0, 1 << 20)
                && one_of("language", { "auto", "fr" })
                && one_of("kv_type", { "auto", "q8", "f16" })
                && one_of("engine", { "auto" })
                && one_of("accelerator", { "auto", "none" })
                && in_range("port", 1024, 65535) && in_range("threads", 0, 1024) && in_range("context", 0, 1 << 20)
                && in_range("max_sequences", 1, 64) && in_range("kv_mb", 0, 1 << 24)
                && in_range("device_mb", 0, 1 << 24)
                && in_range("reserve_gb", 0, 100000)
                && boolean("offline");
        }

        /*
            Pseudalis sur ce Mac (fork pour macOS) : son dossier de données (PSEUDALIS_HOME, sinon
            ~/.local/share/pseudalis, comme platform.cpp de Pseudalis), le port de son settings.json (8444 par
            défaut), et s'il répond sur 127.0.0.1 à ce port.  Rien de plus n'est lu ni renvoyé : ni son jeton
            d'administration, ni ses fichiers.
        */
        json pseudalis_peer()
        {
            json o = json::object();
            std::string home;
            if (const char* h = std::getenv("PSEUDALIS_HOME")) home = h;
            else if (const char* u = std::getenv("HOME")) home = join_path(join_path(join_path(u, ".local"), "share"), "pseudalis");
            std::string text;
            const bool found = !home.empty() && read_file(join_path(home, "settings.json"), text, 1u << 20);
            const json settings = found ? json::parse_or_null(text) : json();
            const long long port = found ? settings["port"].as_int(8444) : 0;
            o["found"] = found && port > 0 && port < 65536;
            o["port"] = port;
            // Son accès depuis le réseau de la maison, pour le sélecteur vu depuis un autre appareil.
            const long long lan_port = found ? settings["lan_port"].as_int(8445) : 0;
            o["lan"] = found && settings["lan"].as_bool(false) && lan_port >= 1024 && lan_port <= 65535;
            o["lan_port"] = lan_port;
            bool running = false;
            if (o["found"].as_bool())
            {
                const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                if (fd >= 0)
                {
                    sockaddr_in a{};
                    a.sin_family = AF_INET;
                    a.sin_port = htons(static_cast<uint16_t>(port));
                    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    running = ::connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0;
                    ::close(fd);
                }
            }
            o["running"] = running;
            return o;
        }

        // Les flottants de la forme base64 d'un vecteur.
        std::vector<float> floats_of(const std::string& text)
        {
            const std::string bytes = base64_decode(text);
            std::vector<float> v(bytes.size() / sizeof(float));
            if (!v.empty()) std::memcpy(v.data(), bytes.data(), v.size() * sizeof(float));
            return v;
        }

        /*
            Les vecteurs de textes, tels que les calcule l'interface OpenAI : "input" un texte ou
            une liste de textes (ou de listes de tokens), "encoding_format" float ou base64, et
            "dimensions" pour les modèles qui gardent leur sens une fois raccourcis.  Les
            requêtes de tous les clients partagent les passes du modèle d'embeddings.
        */
        void handle_embeddings(service& svc, const httplib::Request& req, httplib::Response& res)
        {
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string requested = body["model"].as_string();
            const std::string model = svc.resolve_embedding_model(requested);
            if (model.empty())
            {
                const bool alias = requested.empty() || requested == "auto" || requested == "embedding"
                    || requested == "gulliver-embedding";
                if (send_not_ready(svc, res, alias ? std::string("embedding") : requested)) return;
                send_error(res, 404, "invalid_request_error", requested.empty() || requested == "auto"
                    ? std::string("no embedding model is active")
                    : "the embedding model '" + requested + "' is not active", "model_not_found");
                return;
            }
            const std::string format = body["encoding_format"].str("float");
            if (format != "float" && format != "base64")
            {
                send_error(res, 400, "invalid_request_error", "encoding_format must be float or base64");
                return;
            }
            json request = json::object();
            request["model"] = model;
            // Déplacés, non copiés : les textes d'une grosse requête ne restent qu'une fois en mémoire.
            request["input"] = std::move(body["input"]);
            if (body.contains("dimensions")) request["dimensions"] = body["dimensions"];
            auto ch = std::make_shared<event_channel>();
            uint64_t id = 0;
            std::string error;
            if (!svc.engine().submit(request, [ch](int kind, const std::string& data) { ch->push(kind, data); }, id,
                    error))
            {
                send_error(res, 400, "invalid_request_error", error);
                return;
            }
            // Le moteur garde sa propre copie de la requête : les documents construits ici disparaissent, avec leurs valeurs.
            body = json();
            request = json();
            release_values();
            std::pair<int, std::string> e;
            auto next_look = std::chrono::steady_clock::now();
            const std::function<void()> cancel = [&svc, id]() { svc.engine().cancel(id); };
            for (;;)
            {
                if (ch->pop(e, 1000) && (e.first == GULLIVER_EVENT_DONE || e.first == GULLIVER_EVENT_ERROR)) break;
                if (end_abandoned(svc, req, res, cancel, next_look)) return;
            }
            const json done = json::parse_or_null(e.second);
            if (e.first == GULLIVER_EVENT_ERROR)
            {
                send_error(res, 500, "server_error", done["message"].as_string(), done["code"].as_string());
                return;
            }
            json data = json::array();
            for (const auto& d : done["data"].elements())
            {
                json item = json::object();
                item["object"] = "embedding";
                item["index"] = d["index"];
                if (format == "base64")
                {
                    item["embedding"] = d["embedding"];
                }
                else
                {
                    json values = json::array();
                    for (float x : floats_of(d["embedding"].as_string())) values.push_back(static_cast<double>(x));
                    item["embedding"] = values;
                }
                data.push_back(item);
            }
            svc.record_usage(static_cast<long>(done["usage"]["prompt_tokens"].as_int(0)), 0);
            json out = json::object();
            out["object"] = "list";
            out["data"] = data;
            out["model"] = requested.empty() ? model : requested;
            json usage = json::object();
            usage["prompt_tokens"] = done["usage"]["prompt_tokens"];
            usage["total_tokens"] = done["usage"]["total_tokens"];
            out["usage"] = usage;
            send_json(res, 200, out);
        }

        uint64_t content_length(const httplib::Request& req)
        {
            const std::string v = req.get_header_value("Content-Length");
            return v.empty() ? 0 : std::strtoull(v.c_str(), nullptr, 10);
        }

        // Refuse une opération qui a besoin du réseau sur un ordinateur réglé hors ligne.
        bool refuse_offline(service& svc, httplib::Response& res)
        {
            if (!svc.settings().get().offline) return false;
            send_json(res, 409, ok_result(false, user_error().set("offline mode: the catalog, the updates and the "
                "models come from files (import)", "offline")));
            return true;
        }

        // La réponse d'une route de l'accès au web (web_access.h) : son corps, ou son erreur avec son code.
        void send_web_reply(httplib::Response& res, const web_reply& r)
        {
            if (r.status == 200)
            {
                send_json(res, 200, r.body);
                return;
            }
            send_error(res, r.status, r.status >= 500 ? "server_error" : "invalid_request_error", r.message, r.code,
                r.param);
            if (r.retry_after > 0) res.set_header("Retry-After", std::to_string(r.retry_after));
        }

        /*
            Les textes de licence de l'installation se trouvent dans le dossier licenses des
            programmes : ceux des composants que contiennent les programmes (cmake/payload.cmake) et
            des runtimes des moteurs.  Un texte est désigné par son fichier, une entrée de ce
            dossier lue sans passer par aucun lien.
        */
        const uint64_t max_license_bytes = 1u << 20;

        struct license_component
        {
            const char* file;
            const char* name;
            const char* version;    // la version avec laquelle le service a été construit, vide s'il n'en a pas
        };

        const license_component license_components[] = {
            {"gulliver.txt", "Gulliver", GULLIVER_VERSION},
            {"dlib.txt", "dlib", ""},
            {"mbedtls.txt", "Mbed TLS", MBEDTLS_VERSION_STRING},
            {"cpp-httplib.txt", "cpp-httplib", CPPHTTPLIB_VERSION},
            {"zlib.txt", "zlib", ZLIB_VERSION},
            {"sqlite.txt", "SQLite", SQLITE_VERSION},
            {"libpng.txt", "libpng", ""},
            {"libjpeg.txt", "libjpeg (Independent JPEG Group)", ""}
        };

        // Indique si name peut être une entrée du dossier licenses : aucun séparateur, deux-points,
        // caractère de contrôle ni point initial, ce qui exclut "." et "..".
        bool license_file_name(const std::string& name)
        {
            if (name.empty() || name.size() > 255 || name[0] == '.') return false;
            for (const char ch : name)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') return false;
            }
            return true;
        }

        // Ouvre l'entrée name du dossier licenses sans passer par aucun lien : un fichier d'un seul nom
        // (aucun lien physique) et de max_license_bytes au plus.
        bool open_license(const std::string& folder, const std::string& name, link_free_file& f)
        {
            std::string why;
            return license_file_name(name) && f.open(join_path(folder, name), folder, why) && f.links() == 1
                && f.size() <= max_license_bytes;
        }

        // Les textes de licence de folder : [{"file", "name", "size", "version"}], "version" quand elle est connue.
        json license_list(const std::string& folder)
        {
            std::vector<std::string> entries = list_dir(folder);
            std::sort(entries.begin(), entries.end());
            json list = json::array();
            for (const auto& file : entries)
            {
                link_free_file f;
                if (!open_license(folder, file, f)) continue;
                json e = json::object();
                e["file"] = file;
                e["name"] = file.substr(0, file.rfind('.'));
                for (const auto& c : license_components)
                    if (file == c.file)
                    {
                        e["name"] = c.name;
                        if (*c.version) e["version"] = c.version;
                    }
                e["size"] = static_cast<unsigned long long>(f.size());
                list.push_back(e);
            }
            return list;
        }

        // Lit le texte entier d'une licence ouverte par open_license().
        bool read_license(link_free_file& f, std::string& text)
        {
            text.resize(static_cast<size_t>(f.size()));
            size_t done = 0;
            while (done < text.size())
            {
                const long long n = f.read(&text[done], text.size() - done);
                if (n <= 0) return false;
                done += static_cast<size_t>(n);
            }
            return true;
        }
    }

    void install_routes(httplib::Server& srv, service& svc)
    {
        service* s = &svc;

        // Les corps que les routes acceptent ; l'accès réseau est ouvert, sans mot de passe (fork pour macOS).
        srv.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            auto refuse = [&](int status, const char* message) {
                res.status = status;
                res.set_header("Connection", "close");
                res.set_content(message, "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            };
            // Les méthodes qui n'envoient pas de données ne prennent aucun corps, qu'httplib lirait en
            // entier avant que leurs gestionnaires tournent.  Tout autre corps est du JSON de longueur
            // connue, de 64 Mo au plus, hormis les modèles envoyés à leur propre route.
            const bool no_body = req.method == "GET" || req.method == "HEAD" || req.method == "DELETE"
                || req.method == "OPTIONS";
            if (no_body && (req.has_header("Transfer-Encoding") || content_length(req) > 0))
                return refuse(400, "this method takes no body");
            if (!(req.method == "POST" && (req.path == upload_route || req.path == documents_route)))
            {
                if (req.has_header("Transfer-Encoding")) return refuse(411, "the body must have a length");
                if (content_length(req) > max_json_body) return refuse(413, "the body is too large");
                if (content_length(req) > 0 && req.get_header_value("Content-Type").find("application/json") != 0)
                    return refuse(415, "the body must be application/json");
                // Un compte que ce thread a gardé d'une requête dont la réponse n'a jamais été écrite est rendu.
                if (holding_large_body)
                {
                    holding_large_body = false;
                    --large_bodies;
                }
                release_values();
                if (content_length(req) > large_body)
                {
                    if (large_bodies.fetch_add(1) >= max_large_bodies)
                    {
                        --large_bodies;
                        res.set_header("Retry-After", "2");
                        return refuse(503, "too many large bodies are being read at once: try again");
                    }
                    holding_large_body = true;
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        srv.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            // Le corps de la requête est traité : les valeurs de son document sont rendues, son compte après la
            // réponse.
            release_values();
            if (holding_large_body)
            {
                holding_large_body = false;
                give_back_after_answer(req, res);
            }
            res.set_header("X-Content-Type-Options", "nosniff");
            res.set_header("Referrer-Policy", "no-referrer");
            res.set_header("Cross-Origin-Opener-Policy", "same-origin");
            if (req.path == "/sandbox.html")
            {
                /*
                    La page dans laquelle l'application web exécute le code de l'agent : encadrée
                    par l'application seule, dans une iframe dont le sandbox lui donne une origine
                    propre, pour que le code n'atteigne ni les données de l'application ni le
                    réseau.  Son script et les workers qu'il démarre peuvent évaluer du code.
                */
                res.set_header("X-Frame-Options", "SAMEORIGIN");
                res.set_header("Cross-Origin-Resource-Policy", "same-origin");
                res.set_header("Content-Security-Policy", "default-src 'none'; script-src 'self' blob: 'unsafe-eval'; "
                    "worker-src blob:; style-src 'unsafe-inline'; frame-ancestors 'self'; base-uri 'none'; "
                    "form-action 'none'");
                return;
            }
            res.set_header("X-Frame-Options", "DENY");
            // Le script du sandbox est chargé par une page d'une autre origine (opaque).
            const bool sandbox = req.path == "/js/sandbox.js";
            res.set_header("Cross-Origin-Resource-Policy", sandbox ? "cross-origin" : "same-origin");
            if (req.path.compare(0, 5, "/docs") == 0)
                res.set_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data:; "
                    "style-src 'self' 'unsafe-inline'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'");
            else
                res.set_header("Content-Security-Policy", "default-src 'self'; img-src 'self' data: blob:; "
                    "media-src 'self' blob:; style-src 'self' 'unsafe-inline'; script-src 'self'; font-src 'self' "
                    "data:; connect-src 'self'; worker-src 'self' blob:; frame-ancestors 'none'; base-uri 'none'; "
                    "form-action 'none'");
            if (req.path.compare(0, 4, "/v1/") == 0 || req.path.compare(0, 11, "/gulliver/v1") == 0)
                res.set_header("Cache-Control", "no-store");
        });
        srv.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
            std::string what = "internal error";
            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception& e)
            {
                what = e.what();
            }
            catch (...)
            {
            }
            log_error("http: " + req.method + " " + req.path + ": " + what);
            send_error(res, 500, "server_error", what);
        });

        // Interface compatible OpenAI.
        srv.Get("/v1/models", [s](const httplib::Request&, httplib::Response& res) {
            json list = json::array();
            const host_settings st = s->settings().get();
            const long long now = static_cast<long long>(now_unix());
            // Ce que lit chaque modèle et l'entreprise qui l'a fait ; un alias donne ceux de son modèle.
            const std::map<std::string, json> traits = s->loaded_traits();
            auto add_traits = [&traits](json& m, const std::string& id) {
                const auto t = traits.find(id);
                json inputs = json::array();
                inputs.push_back("text");
                m["inputs"] = t != traits.end() ? t->second["inputs"] : inputs;
                m["publisher"] = t != traits.end() ? t->second["publisher"] : json("");
            };
            for (const auto& id : s->engine().loaded_models())
            {
                json m = json::object();
                m["id"] = id;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "gulliver";
                json roles = json::array();
                for (const auto& kv : st.active)
                    if (kv.second == id) roles.push_back(kv.first);
                m["roles"] = roles;
                add_traits(m, id);
                list.push_back(m);
            }
            for (const char* role : model_roles)
            {
                const std::string target = s->resolve_model(role);
                if (target.empty()) continue;
                json m = json::object();
                m["id"] = std::string("gulliver-") + role;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "gulliver";
                m["alias_of"] = target;
                add_traits(m, target);
                list.push_back(m);
            }
            const std::string background = s->resolve_model("background");
            if (!background.empty())
            {
                json m = json::object();
                m["id"] = "gulliver-background";
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "gulliver";
                m["alias_of"] = background;
                add_traits(m, background);
                list.push_back(m);
            }
            const std::string embedder = s->resolve_embedding_model(std::string());
            if (!embedder.empty())
            {
                json m = json::object();
                m["id"] = std::string("gulliver-") + embedding_role;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "gulliver";
                m["alias_of"] = embedder;
                add_traits(m, embedder);
                list.push_back(m);
            }
            // Le modèle extérieur du relais, une fois le relais prêt, sans son adresse.
            const json relay = s->relay_status();
            if (relay["ready"].as_bool(false))
            {
                json m = json::object();
                m["id"] = relay_model_id;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = relay["provider"];
                m["relay_of"] = relay["model"];
                json inputs = json::array();
                inputs.push_back("text");
                if (relay["image_mode"].as_string() != "none") inputs.push_back("image");
                m["inputs"] = inputs;
                m["publisher"] = relay["provider_name"];
                m["context_length"] = relay["context"];
                list.push_back(m);
            }
            json out = json::object();
            out["object"] = "list";
            out["data"] = list;
            send_json(res, 200, out);
        });
        srv.Post("/v1/chat/completions", [s](const httplib::Request& req, httplib::Response& res) {
            handle_completion(*s, req, res, true);
        });
        srv.Post("/v1/completions", [s](const httplib::Request& req, httplib::Response& res) {
            handle_completion(*s, req, res, false);
        });
        srv.Post("/v1/embeddings", [s](const httplib::Request& req, httplib::Response& res) {
            handle_embeddings(*s, req, res);
        });

        // Interface de l'application web.  Le statut dit à l'application web
        // si l'accès au web est disponible et quel est son fournisseur de recherche (web_capability()).
        srv.Get("/gulliver/v1/status", [s](const httplib::Request&, httplib::Response& res) {
            json status = s->status();
            status["web"] = web_capability(s->settings().get());
            send_json(res, 200, status);
        });
        srv.Get("/gulliver/v1/models", [s](const httplib::Request& req, httplib::Response& res) {
            std::string lang = req.get_param_value("lang");
            if (lang.empty() || lang == "auto") lang = "fr";
            json o = s->models().overview(lang);
            const host_settings st = s->settings().get();
            o["free"] = static_cast<unsigned long long>(free_disk_space(s->models().models_dir()));
            o["offline"] = st.offline;
            json active = json::object(), selected = json::object();
            for (const auto& kv : s->effective_models(st)) active[kv.first] = kv.second;
            for (const auto& kv : st.active) selected[kv.first] = kv.second;
            o["active"] = active;
            o["selected"] = selected;
            json loaded = json::array();
            for (const auto& id : s->engine().loaded_models()) loaded.push_back(id);
            o["loaded"] = loaded;
            o["leaving"] = s->leaving_models();
            o["background"] = s->background_model();
            send_json(res, 200, o);
        });
        // Le débit du moteur, lu en direct par l'application (fork pour macOS).
        srv.Get("/gulliver/v1/speed", [s](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, s->speed());
        });
        // Pseudalis sur ce Mac (fork pour macOS), pour passer d'une application à l'autre.
        srv.Get("/gulliver/v1/pseudalis", [](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, pseudalis_peer());
        });
        // Le carnet du portage (fork pour macOS, host/carnet.h) : son sommaire, puis un fichier de sa liste.
        srv.Get("/gulliver/v1/carnet", [](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, carnet_index());
        });
        srv.Get("/gulliver/v1/carnet/file", [](const httplib::Request& req, httplib::Response& res) {
            std::string data, type;
            if (!carnet_file(req.get_param_value("path"), data, type))
            {
                send_error(res, 404, "invalid_request_error", "no such document in the notebook", "not_found");
                return;
            }
            res.set_content(data, type);
        });
        // La réserve des modèles (fork pour macOS) : ce qu'elle garde, et son vidage.
        srv.Get("/gulliver/v1/reserve", [s](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, s->models().reserve_status());
        });
        srv.Delete("/gulliver/v1/reserve", [s](const httplib::Request&, httplib::Response& res) {
            uint64_t freed = 0;
            const size_t removed = s->models().clear_reserve(freed);
            json o = ok_result(true);
            o["removed"] = static_cast<unsigned long long>(removed);
            o["freed"] = static_cast<unsigned long long>(freed);
            send_json(res, 200, o);
        });
        srv.Get("/gulliver/v1/usage", [s](const httplib::Request&, httplib::Response& res) {
            json o = json::object();
            o["days"] = s->db().usage(30);
            send_json(res, 200, o);
        });
        srv.Get("/gulliver/v1/engines", [s](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, s->engines());
        });
        // Les textes de licence de l'installation, en lecture seule : leur liste, et chaque texte par son fichier.
        const std::string licenses = join_path(svc.program_dir(), "licenses");
        srv.Get("/gulliver/v1/licenses", [licenses](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, license_list(licenses));
        });
        srv.Get(R"(/gulliver/v1/licenses/(.+))", [licenses](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            if (!license_file_name(name))
            {
                send_error(res, 400, "invalid_request_error", "a license text is named by a file of the licenses "
                    "folder", "invalid_license");
                return;
            }
            const std::vector<std::string> entries = list_dir(licenses);
            link_free_file f;
            std::string text;
            if (std::find(entries.begin(), entries.end(), name) == entries.end() || !open_license(licenses, name, f)
                || !read_license(f, text))
            {
                send_error(res, 404, "invalid_request_error", "no license text " + name, "license_not_found");
                return;
            }
            res.status = 200;
            res.set_content(text, "text/plain; charset=utf-8");
        });
        srv.Post("/gulliver/v1/count", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            json q = json::object();
            q["op"] = body["messages"].is_array() ? "count" : "tokenize";
            if (body["model"].as_string() == relay_model_id)
            {
                /*
                    Les tokens d'une discussion du relais tels que le modèle général du Mac les compte,
                    ou environ quatre octets par token sans lui, dans le contexte du modèle extérieur.
                */
                const json r = s->relay_status();
                if (!r["ready"].as_bool(false))
                {
                    send_error(res, 404, "invalid_request_error", r["reason"].as_string(), "relay_unavailable");
                    return;
                }
                q["model"] = s->resolve_model("general");
                if (body["messages"].is_array()) q["messages"] = body["messages"];
                if (body["text"].is_string()) q["text"] = body["text"];
                json counted = q["model"].as_string().empty() ? json() : s->engine().query(q);
                json out = json::object();
                long long tokens = counted["tokens"].is_array() ? static_cast<long long>(counted["tokens"].size())
                    : counted["tokens"].as_int(-1);
                if (tokens < 0 || counted.contains("error"))
                {
                    const size_t bytes = body["messages"].is_array() ? body["messages"].dump().size()
                        : body["text"].as_string().size();
                    tokens = static_cast<long long>((bytes + 3) / 4);
                }
                out["tokens"] = tokens;
                out["context"] = r["context"];
                out["estimated"] = true;
                send_json(res, 200, out);
                return;
            }
            q["model"] = s->resolve_model(body["model"].as_string());
            if (body["messages"].is_array()) q["messages"] = body["messages"];
            if (body["tools"].is_array()) q["tools"] = body["tools"];
            if (body["text"].is_string()) q["text"] = body["text"];
            json r = s->engine().query(q);
            if (q["op"].as_string() == "tokenize" && r["tokens"].is_array())
                r["tokens"] = static_cast<long long>(r["tokens"].size());
            send_json(res, r.contains("error") ? 400 : 200, r);
        });
        /*
            L'accès au web du mode agent (web_access.h) : routes locales de l'application web, de
            l'origine du reste de l'interface, refusées tant que le service travaille hors ligne
            ou une fois que l'administrateur a désactivé l'accès au web.
        */
        srv.Post("/gulliver/v1/web/search", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            send_web_reply(res, web_search(body, s->settings().get()));
        });
        srv.Post("/gulliver/v1/web/fetch", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            send_web_reply(res, web_fetch(body, s->settings().get()));
        });
        /*
            Le relais (relay.h) : les fournisseurs et les modèles qu'il connaît, et son état, sans son
            adresse ni son secret, pour l'application web ; l'essai d'un fournisseur avec les réglages
            que l'application web s'apprête à enregistrer.
        */
        srv.Get("/gulliver/v1/relay", [s](const httplib::Request&, httplib::Response& res) {
            json o = relay_catalog();
            o["relay"] = s->relay_status();
            // Si Pseudalis répond sur ce Mac, pour l'application web, qui propose la pseudonymisation.
            o["pseudalis"] = s->relay().pseudonymization_status();
            send_json(res, 200, o);
        });
        srv.Post("/gulliver/v1/relay/test", [s](const httplib::Request& req, httplib::Response& res) {
            // L'essai joint le fournisseur avec la clé enregistrée.  (Le relais se règle et s'essaie
            // depuis n'importe quel appareil du Wi-Fi, téléphone compris : choix de l'humain, 2026-11-17.)
            json body;
            if (!parse_body(req, res, body)) return;
            std::string error;
            if (!validate_relay(body, error))
            {
                send_error(res, 400, "invalid_request_error", error);
                return;
            }
            relay_settings r;
            if (!merge_relay(s->settings().get().relay, body, r))
            {
                send_error(res, 400, "invalid_request_error", "the masked key of the relay applies to its stored "
                    "provider, address and user only: type the key or the password again", "relay_secret");
                return;
            }
            send_json(res, 200, s->relay().test(r));
        });

        // Le catalogue est téléchargé en arrière-plan ; son état est dans /gulliver/v1/models.
        srv.Post("/gulliver/v1/models/refresh", [s](const httplib::Request& req, httplib::Response& res) {
            if (refuse_offline(*s, res)) return;
            s->models().refresh_catalog_async();
            json o = ok_result(true);
            o["catalog"] = s->models().catalog_status();
            send_json(res, 202, o);
        });
        /*
            Hugging Face (fork pour macOS) : la recherche des modèles GGUF, les fichiers d'un dépôt, et l'ajout d'un
            fichier à la liste personnelle, dont le téléchargement démarre aussitôt.
        */
        /*
            Chaque réponse est gardée sur le Mac.  Hors ligne, avec local=1, ou quand Hugging Face ne répond pas,
            la dernière réponse gardée est rendue, avec sa date dans "cached_at" ; sans elle : 404 hub_not_cached
            (hors ligne ou local=1), 502 hub_unreachable sinon.
        */
        const auto hub_route = [s](const char* kind, const std::string& key, const httplib::Request& req,
            httplib::Response& res, const std::function<bool(json&, std::string&)>& ask) {
            const bool local = req.get_param_value("local") == "1" || s->settings().get().offline;
            json out;
            std::string error;
            if (!local && ask(out, error))
            {
                hub_cache_put(s->db(), kind, key, out);
                send_json(res, 200, out);
                return;
            }
            if (hub_cache_get(s->db(), kind, key, out))
            {
                send_json(res, 200, out);
                return;
            }
            if (local)
                send_error(res, 404, "invalid_request_error", "this answer of Hugging Face was never kept on this Mac",
                    "hub_not_cached");
            else
                send_error(res, 502, "server_error", "Hugging Face: " + error, "hub_unreachable");
        };
        srv.Get("/gulliver/v1/hub/search", [hub_route](const httplib::Request& req, httplib::Response& res) {
            const std::string q = req.get_param_value("q");
            hub_route("search", q, req, res, [&](json& out, std::string& error) { return hub_search(q, out, error); });
        });
        srv.Get("/gulliver/v1/hub/files", [hub_route](const httplib::Request& req, httplib::Response& res) {
            const std::string repo = req.get_param_value("repo");
            hub_route("files", repo, req, res, [&](json& out, std::string& error) { return hub_files(repo, out, error); });
        });
        srv.Get("/gulliver/v1/hub/recent", [s](const httplib::Request&, httplib::Response& res) {
            json o = json::object();
            o["searches"] = hub_cache_recent(s->db());
            send_json(res, 200, o);
        });
        srv.Post("/gulliver/v1/hub/add", [s](const httplib::Request& req, httplib::Response& res) {
            if (refuse_offline(*s, res)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            json entry;
            std::string error;
            // Lu en constant : sur un json modifiable, [] ajoute une clé absente (projector, facultatif), et cet ajout
            // peut déplacer les valeurs dont les autres arguments tiennent déjà une référence.
            const json& b = body;
            if (!hub_entry(b["repo"].as_string(), b["file"].as_string(), b["projector"].as_string(), entry, error)
                || !s->models().add_custom(entry, error))
            {
                send_error(res, 400, "invalid_request_error", error, "hub_entry");
                return;
            }
            user_error started;
            if (!s->models().start_download(entry["id"].as_string(), started))
            {
                send_json(res, 400, ok_result(false, started));
                return;
            }
            json o = ok_result(true);
            o["id"] = entry["id"];
            o["name"] = entry["name"];
            send_json(res, 200, o);
        });
        srv.Post(R"(/gulliver/v1/models/([A-Za-z0-9._-]+)/download)", [s](const httplib::Request& req,
                                                                               httplib::Response& res) {
            user_error error;
            const bool ok = s->models().start_download(req.matches[1], error, s->settings().get().offline);
            send_json(res, ok ? 202 : 400, ok_result(ok, error));
        });
        srv.Delete(R"(/gulliver/v1/models/([A-Za-z0-9._-]+)/download)", [s](const httplib::Request& req,
                                                                                 httplib::Response& res) {
            s->models().cancel_download(req.matches[1]);
            send_json(res, 200, ok_result(true));
        });
        srv.Post(R"(/gulliver/v1/models/([A-Za-z0-9._-]+)/activate)", [s](const httplib::Request& req,
                                                                               httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            user_error error;
            const bool ok = s->activate(req.matches[1], body["role"].str("general"), error);
            send_json(res, ok ? 200 : 400, ok_result(ok, error));
        });
        srv.Post(R"(/gulliver/v1/models/([A-Za-z0-9._-]+)/deactivate)", [s](const httplib::Request& req,
                                                                                 httplib::Response& res) {
            user_error error;
            const bool ok = s->deactivate(req.matches[1], error);
            send_json(res, ok ? 200 : 400, ok_result(ok, error));
        });
        srv.Post(R"(/gulliver/v1/models/([A-Za-z0-9._-]+)/verify)", [s](const httplib::Request& req,
                                                                             httplib::Response& res) {
            send_json(res, 200, s->models().verify(req.matches[1]));
        });
        srv.Delete(R"(/gulliver/v1/models/([A-Za-z0-9._-]+))", [s](const httplib::Request& req,
                                                                        httplib::Response& res) {
            const std::string id = req.matches[1];
            user_error error;
            bool pending = false;
            const bool ok = s->remove_model(id, error, pending);
            json o = ok_result(ok, error);
            if (ok) o["pending"] = pending;
            send_json(res, ok ? 200 : 400, o);
        });
        /*
            Les paquets hors ligne et le contenu du dossier d'import (service::import_folder()),
            que le service lit avec ses propres droits : un refus vient avec son code et le chemin.
        */
        auto import = [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            user_error refusal;
            json r;
            if (body["path"].is_string()) r = s->import_folder(body["path"].as_string(), refusal);
            else refusal.set("the body names no path", "import_path_invalid").arg("path", "");
            if (!refusal.code.empty())
            {
                send_json(res, 400, ok_result(false, refusal));
                return;
            }
            send_json(res, 200, r);
        };
        srv.Post("/gulliver/v1/import", import);
        srv.Post("/gulliver/v1/models/import", import);
        // La bibliothèque du Mac (fork pour macOS) : ses fichiers, et le chargement de l'un d'eux, sans réseau.
        srv.Get("/gulliver/v1/library", [s](const httplib::Request&, httplib::Response& res) {
            user_error refusal;
            const json r = s->library(refusal);
            if (!refusal.code.empty()) send_json(res, 400, ok_result(false, refusal));
            else send_json(res, 200, r);
        });
        srv.Post("/gulliver/v1/library/load", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            user_error refusal;
            json r;
            if (body["file"].is_string()) r = s->load_from_library(body["file"].as_string(), refusal);
            else refusal.set("the body names no file", "library_not_found").arg("path", "");
            if (!refusal.code.empty())
            {
                send_json(res, 400, ok_result(false, refusal));
                return;
            }
            r["ok"] = true;
            send_json(res, 200, r);
        });
        // Un fichier de modèle envoyé par la page, lu avec les droits de son utilisateur, pour les fichiers
        // que le service ne lit jamais.
        srv.Post(upload_route, [s](const httplib::Request& req, httplib::Response& res,
                                    const httplib::ContentReader& reader) {
            const std::string name = file_name(req.get_param_value("name"));
            const uint64_t length = content_length(req);
            if (name.empty() || length == 0 || req.has_header("Transfer-Encoding"))
            {
                res.set_header("Connection", "close");
                send_error(res, 400, "invalid_request_error", "a file name and a length are required");
                return;
            }
            const std::string dir = s->models().models_dir();
            if (free_disk_space(dir) < length + (256u << 20))
            {
                res.set_header("Connection", "close");
                send_error(res, 507, "server_error", "not enough free space in " + dir);
                return;
            }
            // Un nouveau fichier du dossier des modèles atteint sans passer par aucun lien, comme tout fichier
            // qui y est écrit.
            const std::string tmp = join_path(dir, ".transfer-" + random_hex(16) + ".partial");
            new_file out;
            if (!direct_path(dir) || !out.create(tmp))
            {
                res.set_header("Connection", "close");
                send_error(res, 500, "server_error", "cannot create " + tmp);
                return;
            }
            sha256_stream hash;
            uint64_t received = 0;
            bool ok = reader([&](const char* data, size_t n) {
                received += n;
                if (received > length || s->stopping() || !out.write(data, n)) return false;
                hash.update(data, n);
                return true;
            });
            ok = out.close() && ok;
            if (!ok || received != length)
            {
                remove_file(tmp);
                res.set_header("Connection", "close");
                send_error(res, 400, "invalid_request_error", "the transfer did not complete");
                return;
            }
            // Un modèle déjà installé sous un autre fichier est aussi un succès ("already"), comme un
            // projecteur gardé jusqu'à l'arrivée de son décodeur ("pending").
            const json r = s->models().import_received(tmp, name, hash.hex());
            send_json(res, r["imported"].empty() && r["already"].empty() && r["pending"].empty() ? 400 : 200, r);
        });

        /*
            Les documents du Mac : les fichiers de documents_dir(), hormis ses dossiers, que l'application
            web liste, lit pour les joindre, envoie (chaque fichier joint dans la page y est copié) et
            supprime.  Un nom est un simple nom de fichier : aucun dossier, aucun fichier caché.
        */
        srv.Get(documents_route, [s](const httplib::Request& req, httplib::Response& res) {
            const std::string dir = s->documents_dir();
            json list = json::array();
            for (const auto& name : list_dir(dir))
            {
                if (!document_name(name)) continue;
                const std::string path = join_path(dir, name);
                struct stat st;
                if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                json d = json::object();
                d["name"] = name;
                d["size"] = static_cast<unsigned long long>(st.st_size);
                d["modified"] = static_cast<long long>(st.st_mtime);
                list.push_back(d);
            }
            json o = json::object();
            o["folder"] = dir;
            o["documents"] = list;
            send_json(res, 200, o);
        });
        srv.Get(R"(/gulliver/v1/documents/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            std::string data;
            if (!document_name(name) || !read_file(join_path(s->documents_dir(), name), data, max_document_bytes))
            {
                send_error(res, 404, "invalid_request_error", "no such document", "document_not_found");
                return;
            }
            res.set_header("Cache-Control", "no-store");
            res.set_content(data, document_type(name));
        });
        srv.Delete(R"(/gulliver/v1/documents/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            const std::string name = req.matches[1];
            const std::string path = join_path(s->documents_dir(), name);
            if (!document_name(name) || !file_exists(path) || !remove_file(path))
            {
                send_error(res, 404, "invalid_request_error", "no such document", "document_not_found");
                return;
            }
            log_info("documents: " + name + " deleted");
            send_json(res, 200, ok_result(true));
        });
        srv.Post(documents_route, [s](const httplib::Request& req, httplib::Response& res,
                                      const httplib::ContentReader& reader) {
            const std::string name = req.get_param_value("name");
            const uint64_t length = content_length(req);
            if (!document_name(name) || req.has_header("Transfer-Encoding") || length > max_document_bytes)
            {
                res.set_header("Connection", "close");
                send_error(res, 400, "invalid_request_error", "a plain file name and a length of 512 MB at most "
                    "are required", "document_name");
                return;
            }
            const std::string dir = s->documents_dir();
            const std::string tmp = join_path(dir, ".transfer-" + random_hex(16) + ".partial");
            new_file out;
            if (!out.create(tmp))
            {
                res.set_header("Connection", "close");
                send_error(res, 500, "server_error", "cannot create " + tmp);
                return;
            }
            sha256_stream hash;
            uint64_t received = 0;
            bool ok = reader([&](const char* data, size_t n) {
                received += n;
                if (received > length || s->stopping() || !out.write(data, n)) return false;
                hash.update(data, n);
                return true;
            });
            ok = out.close() && ok;
            if (!ok || received != length)
            {
                remove_file(tmp);
                res.set_header("Connection", "close");
                send_error(res, 400, "invalid_request_error", "the transfer did not complete");
                return;
            }
            /*
                Les mêmes octets sous le même nom, ou sous un nom qu'il leur a donné auparavant, ne sont
                gardés qu'une fois ("already") ; un autre fichier de ce nom le garde, et le nouveau prend
                "name (2).ext".
            */
            const std::string digest = hash.hex();
            const size_t dot = name.rfind('.');
            const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
            const std::string ext = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
            std::string target = name;
            for (int k = 2; file_exists(join_path(dir, target)); ++k)
            {
                if (file_size(join_path(dir, target)) == length && sha256_file(join_path(dir, target)) == digest)
                {
                    remove_file(tmp);
                    json o = ok_result(true);
                    o["name"] = target;
                    o["already"] = true;
                    send_json(res, 200, o);
                    return;
                }
                target = stem + " (" + std::to_string(k) + ")" + ext;
            }
            if (!rename_file(tmp, join_path(dir, target)))
            {
                remove_file(tmp);
                send_error(res, 500, "server_error", "cannot write " + target);
                return;
            }
            log_info("documents: " + target + " received, " + std::to_string(length) + " bytes");
            json o = ok_result(true);
            o["name"] = target;
            o["already"] = false;
            send_json(res, 200, o);
        });

        /*
            Les réglages enregistrés : des instantanés des réglages que change l'application web, sous une
            étiquette que donne l'utilisateur, dans settings-profiles.json du dossier de données.  Un
            instantané ne contient pas le mot de passe du proxy (seulement son masque).  En restaurer un est un changement des réglages, que l'application web envoie à
            /gulliver/v1/settings.
        */
        srv.Get("/gulliver/v1/settings/profiles", [s](const httplib::Request& req, httplib::Response& res) {
            json o = json::object();
            o["profiles"] = read_profiles(*s);
            send_json(res, 200, o);
        });
        srv.Post("/gulliver/v1/settings/profiles", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string label = body["label"].as_string();
            if (label.empty() || label.size() > 120)
            {
                send_error(res, 400, "invalid_request_error", "a label of 1 to 120 characters is required", "label");
                return;
            }
            const json all = public_settings(*s);
            json snapshot = json::object();
            for (const char* k : profile_keys)
                if (all.contains(k)) snapshot[k] = all[k];
            std::lock_guard<std::mutex> lock(profiles_m);
            // Une liste nommée : elements() d'un temporaire lui survivrait dans la boucle.
            const json profiles = read_profiles(*s);
            json kept = json::array();
            for (const auto& p : profiles.elements())
                if (p["label"].as_string() != label) kept.push_back(p);
            json p = json::object();
            p["label"] = label;
            p["saved"] = static_cast<long long>(now_unix());
            p["settings"] = snapshot;
            kept.push_back(p);
            if (!write_file_atomic(join_path(s->data_dir(), "settings-profiles.json"), kept.dump_formatted(1)))
            {
                send_error(res, 500, "server_error", "the saved settings cannot be written");
                return;
            }
            log_info("settings: saved as " + label);
            send_json(res, 200, p);
        });
        srv.Delete("/gulliver/v1/settings/profiles", [s](const httplib::Request& req, httplib::Response& res) {
            const std::string label = req.get_param_value("label");
            std::lock_guard<std::mutex> lock(profiles_m);
            const json profiles = read_profiles(*s);
            json kept = json::array();
            bool found = false;
            for (const auto& p : profiles.elements())
            {
                if (p["label"].as_string() == label) found = true;
                else kept.push_back(p);
            }
            if (!found)
            {
                send_error(res, 404, "invalid_request_error", "no saved settings of that label", "label");
                return;
            }
            write_file_atomic(join_path(s->data_dir(), "settings-profiles.json"), kept.dump_formatted(1));
            send_json(res, 200, ok_result(true));
        });

        // Les profils d'utilisateur (l'espace de chacun : discussions, projets, documents, images).
        // À ne pas confondre avec /settings/profiles ci-dessus, qui sont des instantanés de réglages.
        // Lister ; créer avec un nom ; renommer par son id ; supprimer par son id (ses données
        // partent avec lui par la cascade des tables de profil).
        srv.Get("/gulliver/v1/profiles", [s](const httplib::Request&, httplib::Response& res) {
            json list = json::array();
            for (const auto& p : s->db().profiles())
                list.push_back(profile_json(p));
            json o = json::object();
            o["profiles"] = list;
            send_json(res, 200, o);
        });
        srv.Post("/gulliver/v1/profiles", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string name = profile_name_of(body);
            if (name.empty())
            {
                send_error(res, 400, "invalid_request_error",
                    "a name of 1 to 80 characters is required", "name");
                return;
            }
            profile exists;
            if (s->db().find_profile_by_name(name, exists))
            {
                send_error(res, 409, "invalid_request_error", "a profile already has this name", "name_taken");
                return;
            }
            profile p;
            p.id = random_hex(12);
            p.name = name;
            p.created = now_unix();
            p.last_used = p.created;
            try { s->db().put_profile(p); }
            catch (const std::exception&)
            {
                // Une collision de nom au même instant, par exemple : la base l'a refusée.
                send_error(res, 409, "invalid_request_error", "a profile already has this name", "name_taken");
                return;
            }
            log_info("profiles: created " + p.id);
            send_json(res, 200, profile_json(p));
        });
        srv.Put(R"(/gulliver/v1/profiles/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string id = req.matches[1];
            profile p;
            if (!s->db().find_profile(id, p))
            {
                send_error(res, 404, "invalid_request_error", "no profile has this id", "profile_unknown");
                return;
            }
            const std::string name = profile_name_of(body);
            if (name.empty())
            {
                send_error(res, 400, "invalid_request_error", "a name of 1 to 80 characters is required", "name");
                return;
            }
            profile other;
            if (s->db().find_profile_by_name(name, other) && other.id != id)
            {
                send_error(res, 409, "invalid_request_error", "another profile already has this name", "name_taken");
                return;
            }
            p.name = name;
            try { s->db().put_profile(p); }
            catch (const std::exception&)
            {
                send_error(res, 409, "invalid_request_error", "another profile already has this name", "name_taken");
                return;
            }
            send_json(res, 200, profile_json(p));
        });
        srv.Delete(R"(/gulliver/v1/profiles/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            const std::string id = req.matches[1];
            profile p;
            if (!s->db().find_profile(id, p))
            {
                send_error(res, 404, "invalid_request_error", "no profile has this id", "profile_unknown");
                return;
            }
            s->db().remove_profile(id);
            log_info("profiles: removed " + id);
            send_json(res, 200, ok_result(true));
        });

        // Les données d'un profil : discussions et projets.  Chaque route commence par resolve_profile
        // (en-tête X-Gulliver-Profile).  Le corps et les réponses portent le JSON entier que la page
        // gère (champ "doc"), déjà parsé une fois ici pour vérifier qu'il tient dans les bornes.
        srv.Get("/gulliver/v1/profile/conversations", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json list = json::array();
            for (const std::string& doc : s->db().conversation_docs(p.id))
            {
                json d;
                if (json::parse(doc, d)) list.push_back(d);
            }
            json o = json::object();
            o["conversations"] = list;
            send_json(res, 200, o);
        });
        srv.Get(R"(/gulliver/v1/profile/conversations/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            std::string doc;
            if (!s->db().get_conversation(p.id, req.matches[1], doc))
            {
                send_error(res, 404, "invalid_request_error", "no conversation of this id in the profile", "not_found");
                return;
            }
            json d;
            json::parse(doc, d);
            send_json(res, 200, d);
        });
        srv.Put(R"(/gulliver/v1/profile/conversations/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            if (!body.contains("doc") || !body["doc"].is_object())
            {
                send_error(res, 400, "invalid_request_error", "a doc object is required", "doc");
                return;
            }
            const std::string id = req.matches[1];
            const std::string project = body.contains("project") ? body["project"].as_string() : std::string();
            const int64_t updated = body.contains("updated") ? body["updated"].as_int(0) : 0;
            // La révision optimiste : si le corps dit quelle valeur d'updated il croyait avoir, l'écriture
            // n'a lieu que si elle n'a pas changé entre-temps (409 sinon).  Sans ce champ, écriture forcée.
            const int64_t expected = body.contains("expected_updated") ? body["expected_updated"].as_int(-1) : -1;
            const bool ok = s->db().put_conversation(p.id, id, project, updated, body["doc"].dump(), expected);
            if (!ok)
            {
                send_error(res, 409, "invalid_request_error",
                    "the conversation changed since it was read; read it again and retry", "revision_conflict");
                return;
            }
            send_json(res, 200, ok_result(true));
        });
        srv.Delete(R"(/gulliver/v1/profile/conversations/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            s->db().delete_conversation(p.id, req.matches[1]);
            send_json(res, 200, ok_result(true));
        });

        srv.Get("/gulliver/v1/profile/projects", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json list = json::array();
            for (const std::string& doc : s->db().project_docs(p.id))
            {
                json d;
                if (json::parse(doc, d)) list.push_back(d);
            }
            json o = json::object();
            o["projects"] = list;
            send_json(res, 200, o);
        });
        srv.Get(R"(/gulliver/v1/profile/projects/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            std::string doc;
            if (!s->db().get_project(p.id, req.matches[1], doc))
            {
                send_error(res, 404, "invalid_request_error", "no project of this id in the profile", "not_found");
                return;
            }
            json d;
            json::parse(doc, d);
            send_json(res, 200, d);
        });
        srv.Put(R"(/gulliver/v1/profile/projects/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            if (!body.contains("doc") || !body["doc"].is_object())
            {
                send_error(res, 400, "invalid_request_error", "a doc object is required", "doc");
                return;
            }
            const std::string name = body["doc"].contains("name") ? body["doc"]["name"].as_string() : std::string();
            s->db().put_project(p.id, req.matches[1], name, body["doc"].dump());
            send_json(res, 200, ok_result(true));
        });
        srv.Delete(R"(/gulliver/v1/profile/projects/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            s->db().delete_project(p.id, req.matches[1]);
            send_json(res, 200, ok_result(true));
        });

        // Les réglages de la page, par profil (thème, nom, prompt système, mémoire…).  Chaque valeur
        // est un JSON quelconque, gardé sérialisé.  GET rend l'objet entier {clé: valeur} ; PUT écrit
        // les clés de l'objet du corps (une valeur null efface la clé, comme un oubli de réglage).
        srv.Get("/gulliver/v1/profile/page-settings", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const json stored = s->db().page_settings(p.id);
            json out = json::object();
            for (const auto& kv : stored.items())
            {
                json v;
                if (json::parse(kv.second.as_string(), v)) out[kv.first] = v;
            }
            send_json(res, 200, out);
        });
        srv.Put("/gulliver/v1/profile/page-settings", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            for (const auto& kv : body.items())
                s->db().set_page_setting(p.id, kv.first, kv.second.dump());
            send_json(res, 200, ok_result(true));
        });

        // Les index nommés d'un profil (chaque combinaison modèle + découpage).  Lister, créer/mettre à
        // jour, rendre actif, supprimer.  L'index actif est celui où les passages sont rangés et cherchés.
        srv.Get("/gulliver/v1/profile/indexes", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json list = json::array();
            for (const index_record& r : s->db().indexes(p.id)) list.push_back(index_json(r));
            json o = json::object();
            o["indexes"] = list;
            o["active"] = s->db().active_index(p.id);
            send_json(res, 200, o);
        });
        srv.Post("/gulliver/v1/profile/indexes", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string name = profile_name_of(body);  // mêmes bornes qu'un nom de profil
            if (name.empty())
            {
                send_error(res, 400, "invalid_request_error", "a name of 1 to 80 characters is required", "name");
                return;
            }
            index_record r;
            r.id = random_hex(12);
            r.name = name;
            r.embed_model = body.contains("embed_model") ? body["embed_model"].as_string() : std::string();
            r.passage_chars = body.contains("passage_chars") ? body["passage_chars"].as_int(1200) : 1200;
            r.overlap_chars = body.contains("overlap_chars") ? body["overlap_chars"].as_int(240) : 240;
            r.created = now_unix();
            r.active = s->db().indexes(p.id).empty();  // le premier index est actif d'office
            s->db().put_index(p.id, r);
            send_json(res, 200, index_json(r));
        });
        srv.Put(R"(/gulliver/v1/profile/indexes/([^/]+)/active)", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            index_record r;
            if (!s->db().find_index(p.id, req.matches[1], r))
            {
                send_error(res, 404, "invalid_request_error", "no index of this id in the profile", "not_found");
                return;
            }
            s->db().set_active_index(p.id, req.matches[1]);
            send_json(res, 200, ok_result(true));
        });
        srv.Delete(R"(/gulliver/v1/profile/indexes/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            s->db().remove_index(p.id, req.matches[1]);
            send_json(res, 200, ok_result(true));
        });

        // L'index RAG d'un profil : les passages des documents, avec leurs vecteurs en base64.
        // GET chunks?owner=…  : les passages d'un propriétaire ; GET chunks?doc=… : ceux d'un document.
        // GET chunks/status?owner=…&model=… : les comptes par document, sans transférer les vecteurs.
        // GET chunks/documents : les couples (owner, doc) distincts (pour l'élagage au démarrage).
        // PUT chunks/<doc> {records:[…]} : remplace en bloc les passages d'un document.
        // DELETE chunks?owner=…  ou  ?doc=… : efface par propriétaire ou par document.
        srv.Get("/gulliver/v1/profile/chunks", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const std::string owner = req.get_param_value("owner");
            const std::string doc = req.get_param_value("doc");
            if (owner.empty() == doc.empty())
            {
                send_error(res, 400, "invalid_request_error", "exactly one of owner or doc is required", "owner_or_doc");
                return;
            }
            const std::vector<chunk_record> records = owner.empty()
                ? s->db().chunks_of_document(p.id, doc, s->db().active_index(p.id)) : s->db().chunks_of(p.id, owner, s->db().active_index(p.id));
            json list = json::array();
            for (const chunk_record& c : records) list.push_back(chunk_json(c));
            json o = json::object();
            o["chunks"] = list;
            send_json(res, 200, o);
        });
        srv.Get("/gulliver/v1/profile/chunks/status", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const std::string owner = req.get_param_value("owner");
            const std::string model = req.get_param_value("model");
            json list = json::array();
            for (const chunk_status_row& r : s->db().chunk_status(p.id, owner, model, s->db().active_index(p.id)))
            {
                json o = json::object();
                o["doc"] = r.doc;
                o["passages"] = static_cast<long long>(r.passages);
                o["vectors"] = static_cast<long long>(r.vectors);
                o["model"] = r.model;
                list.push_back(o);
            }
            json o = json::object();
            o["status"] = list;
            send_json(res, 200, o);
        });
        srv.Get("/gulliver/v1/profile/chunks/documents", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json list = json::array();
            for (const auto& od : s->db().chunk_documents(p.id, s->db().active_index(p.id)))
            {
                json o = json::object();
                o["owner"] = od.first;
                o["doc"] = od.second;
                list.push_back(o);
            }
            json o = json::object();
            o["documents"] = list;
            send_json(res, 200, o);
        });
        srv.Put(R"(/gulliver/v1/profile/chunks/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            if (!body.contains("records") || !body["records"].is_array())
            {
                send_error(res, 400, "invalid_request_error", "a records array is required", "records");
                return;
            }
            std::vector<chunk_record> records;
            for (const auto& r : body["records"].elements()) records.push_back(chunk_of_json(r));
            s->db().replace_chunks(p.id, req.matches[1], records, s->db().active_index(p.id));
            send_json(res, 200, ok_result(true));
        });
        srv.Delete("/gulliver/v1/profile/chunks", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const std::string owner = req.get_param_value("owner");
            const std::string doc = req.get_param_value("doc");
            if (owner.empty() == doc.empty())
            {
                send_error(res, 400, "invalid_request_error", "exactly one of owner or doc is required", "owner_or_doc");
                return;
            }
            if (owner.empty()) s->db().delete_chunks_of_document(p.id, doc, s->db().active_index(p.id));
            else s->db().delete_chunks_of_owner(p.id, owner, s->db().active_index(p.id));
            send_json(res, 200, ok_result(true));
        });

        // La recherche documentaire côté service : corps {question, owners:[...], engine?}.  Charge les
        // passages des propriétaires, cherche avec le moteur choisi (lexical pour l'instant ; dense et
        // hybrid viennent ensuite) et renvoie les extraits.  Le moteur lexical n'a pas besoin de vecteur.
        srv.Post("/gulliver/v1/profile/search", [s](const httplib::Request& req, httplib::Response& res) {
            profile prof;
            if (!resolve_profile(*s, req, res, prof)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string question = body.contains("question") ? body["question"].as_string() : std::string();
            std::vector<rag_passage> passages;
            if (body.contains("owners") && body["owners"].is_array())
            {
                for (const auto& o : body["owners"].elements())
                {
                    for (const chunk_record& c : s->db().chunks_of(prof.id, o.as_string(), s->db().active_index(prof.id)))
                    {
                        rag_passage rp;
                        rp.id = c.id; rp.doc = c.doc; rp.name = c.name; rp.where = c.where;
                        rp.ordinal = c.ordinal; rp.text = c.text; rp.model = c.model;
                        // Le vecteur (octets float) repassé en floats pour les moteurs dense et hybrid.
                        if (!c.vector.empty())
                        {
                            rp.vector.resize(c.vector.size() / sizeof(float));
                            std::memcpy(rp.vector.data(), c.vector.data(), rp.vector.size() * sizeof(float));
                        }
                        passages.push_back(std::move(rp));
                    }
                }
            }
            const host_settings cfg = s->settings().get();
            rag_search_options opt;
            opt.limit = cfg.rag_top_k;
            if (body.contains("limit")) opt.limit = static_cast<int>(body["limit"].as_int(opt.limit));
            if (body.contains("budget")) opt.budget = body["budget"].as_int(opt.budget);
            if (body.contains("model")) opt.model = body["model"].as_string();
            // Le vecteur de la question (pour les moteurs dense et hybrid), fourni en base64 par la page
            // qui l'a calculé via /v1/embeddings.
            if (body.contains("question_vector") && !body["question_vector"].as_string().empty())
            {
                const std::string bytes = base64_decode(body["question_vector"].as_string());
                opt.question_vector.resize(bytes.size() / sizeof(float));
                std::memcpy(opt.question_vector.data(), bytes.data(), opt.question_vector.size() * sizeof(float));
            }
            const std::string engine_name = body.contains("engine") ? body["engine"].as_string() : cfg.rag_engine;
            std::unique_ptr<rag_engine> engine = engine_for(engine_name);
            const std::vector<rag_excerpt> hits = engine->search(passages, question, opt);
            json list = json::array();
            for (const rag_excerpt& e : hits)
            {
                json o = json::object();
                o["doc"] = e.doc; o["name"] = e.name; o["where"] = e.where; o["text"] = e.text;
                o["score"] = e.score;
                json ids = json::array();
                for (const std::string& id : e.passages) ids.push_back(id);
                o["passages"] = ids;
                list.push_back(o);
            }
            json out = json::object();
            out["excerpts"] = list;
            out["engine"] = engine->name();
            send_json(res, 200, out);
        });

        // Les images d'un profil.  Les métadonnées sont en base ; les octets (image pleine et vignette)
        // sont des fichiers sous <data>/profiles/<id>/images/<imageId>{,.thumb}.
        // GET images?owner=…  : les métadonnées d'un propriétaire.  GET images/usage : nombre et octets.
        // GET images/<id>/blob  et  /thumb : les octets, avec leur type.
        // PUT images/<id> {owner, name, type, size, width, height, blob(base64), thumb(base64)}.
        // DELETE images/<id>  ou  images?owner=… : efface la (les) ligne(s) et le(s) fichier(s).
        srv.Get("/gulliver/v1/profile/images", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json list = json::array();
            for (const image_record& r : s->db().images_of(p.id, req.get_param_value("owner")))
                list.push_back(image_json(r));
            json o = json::object();
            o["images"] = list;
            send_json(res, 200, o);
        });
        srv.Get("/gulliver/v1/profile/images/usage", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            int64_t count = 0, bytes = 0;
            s->db().image_usage(p.id, count, bytes);
            json o = json::object();
            o["count"] = static_cast<long long>(count);
            o["bytes"] = static_cast<long long>(bytes);
            send_json(res, 200, o);
        });
        const auto serve_image_file = [s](const httplib::Request& req, httplib::Response& res, bool thumb) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            image_record r;
            if (!s->db().find_image(p.id, req.matches[1], r))
            {
                send_error(res, 404, "invalid_request_error", "no image of this id in the profile", "not_found");
                return;
            }
            const std::string path = join_path(s->profile_images_dir(p.id), r.id + (thumb ? ".thumb" : ""));
            std::string data;
            if (!read_file(path, data, max_document_bytes))
            {
                send_error(res, 404, "invalid_request_error", "the image file is missing", "not_found");
                return;
            }
            res.set_content(data, r.type.empty() ? "application/octet-stream" : r.type.c_str());
        };
        srv.Get(R"(/gulliver/v1/profile/images/([^/]+)/blob)",
            [serve_image_file](const httplib::Request& req, httplib::Response& res) { serve_image_file(req, res, false); });
        srv.Get(R"(/gulliver/v1/profile/images/([^/]+)/thumb)",
            [serve_image_file](const httplib::Request& req, httplib::Response& res) { serve_image_file(req, res, true); });
        srv.Put(R"(/gulliver/v1/profile/images/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string id = req.matches[1];
            const std::string dir = s->profile_images_dir(p.id);
            if (!make_dirs(dir))
            {
                send_error(res, 500, "server_error", "the image folder cannot be created");
                return;
            }
            const std::string blob = body.contains("blob") ? base64_decode(body["blob"].as_string()) : std::string();
            const std::string thumb = body.contains("thumb") ? base64_decode(body["thumb"].as_string()) : std::string();
            if (!write_file_atomic(join_path(dir, id), blob)
                || !write_file_atomic(join_path(dir, id + ".thumb"), thumb))
            {
                send_error(res, 500, "server_error", "the image cannot be written");
                return;
            }
            image_record r;
            r.id = id;
            r.owner = body.contains("owner") ? body["owner"].as_string() : std::string();
            r.name = body.contains("name") ? body["name"].as_string() : std::string();
            r.type = body.contains("type") ? body["type"].as_string() : std::string();
            r.size = body.contains("size") ? body["size"].as_int(0) : 0;
            r.width = body.contains("width") ? body["width"].as_int(0) : 0;
            r.height = body.contains("height") ? body["height"].as_int(0) : 0;
            r.added = body.contains("added") ? body["added"].as_int(now_unix()) : now_unix();
            r.blob_bytes = static_cast<int64_t>(blob.size());
            r.thumb_bytes = static_cast<int64_t>(thumb.size());
            s->db().put_image(p.id, r);
            send_json(res, 200, ok_result(true));
        });
        srv.Delete(R"(/gulliver/v1/profile/images/([^/]+))", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const std::string id = req.matches[1];
            const std::string dir = s->profile_images_dir(p.id);
            remove_file(join_path(dir, id));
            remove_file(join_path(dir, id + ".thumb"));
            s->db().delete_image(p.id, id);
            send_json(res, 200, ok_result(true));
        });
        srv.Delete("/gulliver/v1/profile/images", [s](const httplib::Request& req, httplib::Response& res) {
            profile p;
            if (!resolve_profile(*s, req, res, p)) return;
            const std::string owner = req.get_param_value("owner");
            if (owner.empty())
            {
                send_error(res, 400, "invalid_request_error", "owner is required", "owner");
                return;
            }
            const std::string dir = s->profile_images_dir(p.id);
            for (const std::string& id : s->db().delete_images_of_owner(p.id, owner))
            {
                remove_file(join_path(dir, id));
                remove_file(join_path(dir, id + ".thumb"));
            }
            send_json(res, 200, ok_result(true));
        });

        // Le mot de passe du proxy est donné sous la forme proxy_password_mask (host_settings::to_json()).
        srv.Get("/gulliver/v1/settings", [s](const httplib::Request& req, httplib::Response& res) {
            send_json(res, 200, public_settings(*s));
        });
        srv.Put("/gulliver/v1/settings", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            std::string error;
            if (!validate_settings(body, error))
            {
                send_error(res, 400, "invalid_request_error", error);
                return;
            }
            // Lu par une référence const, qui n'ajoute aucun membre au corps.
            const json& changes = body;
            // Un nouveau port est ouvert une fois avant d'être enregistré, pour qu'un port pris par un autre
            // programme ou réservé par le système ne laisse jamais le service sans adresse.
            const long long port = changes["port"].as_int(s->port());
            if (port != s->port() && !port_available(static_cast<int>(port)))
            {
                send_error(res, 409, "invalid_request_error", "the port " + std::to_string(port)
                    + " is in use or cannot be opened", "port_unavailable");
                return;
            }
            // Les réglages (relais compris) se changent depuis n'importe quel appareil du Wi-Fi, le
            // téléphone compris (choix de l'humain, 2026-11-17 : « tout est changeable par tél »).
            const host_settings now = s->settings().get();
            const long long lan_port = changes["lan_port"].as_int(now.lan_port);
            if (lan_port == port)
            {
                send_error(res, 400, "invalid_request_error", "the network port must differ from the port of the "
                    "service", "lan_port_same");
                return;
            }
            if (lan_port != now.lan_port && !port_available(static_cast<int>(lan_port), true))
            {
                send_error(res, 409, "invalid_request_error", "the port " + std::to_string(lan_port)
                    + " is in use or cannot be opened", "port_unavailable");
                return;
            }
            host_settings before;
            bool masked_elsewhere = false, relay_masked_elsewhere = false;
            const host_settings after = s->settings().update([&](host_settings& current) {
                before = current;
                // Le masque du mot de passe tient lieu du mot de passe enregistré, seulement avec l'adresse et
                // l'utilisateur pour lesquels il a été donné : il n'envoie jamais le mot de passe à un autre serveur.
                const std::string proxy = changes["proxy"].as_string();
                if (proxy_password_masked(proxy) && proxy != masked_proxy(current.proxy))
                {
                    masked_elsewhere = true;
                    return;
                }
                relay_settings relay = current.relay;
                if (changes.contains("relay") && !merge_relay(current.relay, changes["relay"], relay))
                {
                    relay_masked_elsewhere = true;
                    return;
                }
                json merged = current.to_json();
                for (const auto& kv : body.items())
                    if (kv.first != "relay") merged[kv.first] = kv.second;
                current.from_json(merged);
                if (current.proxy == masked_proxy(before.proxy)) current.proxy = before.proxy;
                current.relay = relay;
            });
            if (masked_elsewhere)
            {
                send_error(res, 400, "invalid_request_error", "the masked password of the proxy applies to its "
                    "stored address and user only: type the password again", "proxy_password");
                return;
            }
            if (relay_masked_elsewhere)
            {
                send_error(res, 400, "invalid_request_error", "the masked key of the relay applies to its stored "
                    "provider, address and user only: type the key or the password again", "relay_secret");
                return;
            }
            s->apply_network_settings(after);
            // Les valeurs gardées pour la pseudonymisation s'en vont dès que le relais ou sa pseudonymisation s'arrête.
            if (!(after.relay.enabled && after.relay.pseudonymize)) s->relay().forget_pseudonyms();
            // Ce que le moteur ou le serveur en marche ne peut pas prendre sans redémarrage.
            const bool restart = after.port != before.port || after.engine != before.engine
                || after.accelerator != before.accelerator || after.device_mb != before.device_mb
                || after.threads != before.threads
                || after.context != before.context || after.max_sequences != before.max_sequences
                || after.kv_mb != before.kv_mb || after.kv_type != before.kv_type
                || after.lan != before.lan || after.lan_port != before.lan_port
                || after.warm_history != before.warm_history || after.spin_us != before.spin_us
                || after.gpu_prompt_tokens != before.gpu_prompt_tokens
                || after.gpu_prompt_context != before.gpu_prompt_context || after.models_dir != before.models_dir;
            json o = ok_result(true);
            o["restart"] = restart;
            send_json(res, 200, o);
            // Un plafond plus bas s'applique tout de suite.
            if (after.reserve_gb < before.reserve_gb) s->models().trim_reserve();
            if (restart) s->request_restart();
        });
        srv.Post("/gulliver/v1/restart", [s](const httplib::Request& req, httplib::Response& res) {
            send_json(res, 202, ok_result(true));
            s->request_restart();
        });
        srv.Post("/gulliver/v1/engines/benchmark", [s](const httplib::Request& req, httplib::Response& res) {
            // Une mesure déjà en cours refuse la seconde avec 409.
            const json r = s->benchmark_engines();
            send_json(res, r["code"].as_string() == "benchmark_running" ? 409 : 200, r);
        });
        srv.Get("/gulliver/v1/logs", [](const httplib::Request& req, httplib::Response& res) {
            json lines = json::array();
            for (const auto& l : log_file::instance().recent()) lines.push_back(l);
            json o = json::object();
            o["lines"] = lines;
            send_json(res, 200, o);
        });

        // L'application web, la visionneuse de documentation et leurs ressources.
        const std::string web = join_path(svc.program_dir(), "web");
        if (!srv.set_mount_point("/", web)) log_warning("http: no web folder at " + web);
        srv.set_file_extension_and_mimetype_mapping("mjs", "text/javascript");
        srv.set_file_extension_and_mimetype_mapping("woff2", "font/woff2");
        srv.set_file_extension_and_mimetype_mapping("webmanifest", "application/manifest+json");
        srv.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
            /*
                Chaque fichier de l'application est vérifié à chaque chargement, ses pages comme ses
                scripts et ses styles, pour qu'une mise à jour apparaisse aussitôt : sans cet en-tête,
                un navigateur peut garder les scripts de la version précédente pendant des heures.
                La vérification coûte une requête à laquelle répond 304 Not Modified tant que le
                fichier n'a pas changé.
            */
            res.set_header("Cache-Control", "no-cache");
        });
        srv.Get("/docs", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/docs/"); });
    }

}
