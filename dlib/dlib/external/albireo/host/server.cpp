// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    The routes of the local HTTPS server:

        /                         the web application (files of the web folder)
        /docs/                    the interactive documentation of the interface
        /openapi.json             its OpenAPI description
        /v1/models                OpenAI compatible interface
        /v1/chat/completions
        /v1/completions
        /v1/embeddings
        /albireo/v1/...           the interface of the web application and of the tray

    A personal version: the service listens on 127.0.0.1 only, and any request it receives
    may do anything, the web application changing the settings, the models and the
    installation as the tray of the original did.  It no longer asks for an administration
    token, nor checks the Host and Origin headers (a choice of the owner of this fork, who
    knows that a web site open in the browser could then drive the service).
*/


#include <dlib/external/httplib/httplib.h>
#include <mbedtls/build_info.h>
#include <sqlite3.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
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
#include <fstream>
#include <mutex>

#include "../common/engine_api.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "crypto.h"
#include "http_fetch.h"
#include "service.h"
#include "speech.h"
#include "web_access.h"

namespace albireo
{

    namespace
    {
        // Bodies of the JSON routes; a model is sent to its own route without this limit.
        const uint64_t max_json_body = 64u << 20;
        // The values of a body, each taking about a hundred bytes once read: more than the
        // 300,000 tokens a request of embeddings may list.
        const size_t max_body_values = size_t(1) << 19;
        /*
            The bodies above large_body are read for max_large_bodies requests at once: a body
            of 64 MB, held and parsed by each of the 64 threads of the server, would take more
            memory than the computer has.  The count is taken before the body is read, by the
            thread that reads it (holding_large_body), and given back once the answer is written
            or given up, a streamed answer included: the request keeps its text in memory until
            then (give_back_after_answer()).
        */
        const uint64_t large_body = 8u << 20;
        const int max_large_bodies = 4;
        std::atomic<int> large_bodies{0};
        thread_local bool holding_large_body = false;
        /*
            The values of the documents being built and held at once, each taking about a hundred
            bytes whatever its length in the text: a body of one megabyte of small values builds
            some 60 MB, which the threads of the server together would multiply.  A body reserves
            the values its text may hold before it is parsed (parse_body()), and gives them back
            once its document is released, by the same thread (holding_values).
        */
        const size_t max_held_values = 4 * (max_body_values + 1);
        std::atomic<size_t> held_values{0};
        thread_local size_t holding_values = 0;
        // The events of a streamed answer waiting for a client that reads far slower than the engine writes.
        const size_t max_backlog = 16384;

        const char* const upload_route = "/albireo/v1/models/upload";

        void send_json(httplib::Response& res, int status, const json& j)
        {
            res.status = status;
            // The last guard: the errors a route returns name the files of the publication space, never its addresses.
            if (errors_name_publication_space(j))
            {
                json hidden = j;
                hide_publication_space_in_errors(hidden);
                res.set_content(hidden.dump(), "application/json; charset=utf-8");
                return;
            }
            res.set_content(j.dump(), "application/json; charset=utf-8");
        }

        // The error body of the OpenAI interface, param naming the member of the request it concerns.
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

        /*
            The answer to a request whose model is not in the engine yet, because the
            service starts or the model loads: 503 with a code and the delay after which to
            try again.  Returns false when the model is missing for good.
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

        json ok_result(bool ok, const std::string& error = std::string())
        {
            json o = json::object();
            o["ok"] = ok;
            if (!error.empty()) o["error"] = error;
            return o;
        }

        // The result of an operation of the windows, with the code of its error.
        json ok_result(bool ok, const user_error& error)
        {
            json o = json::object();
            o["ok"] = ok;
            if (!ok) error.add_to(o);
            return o;
        }

        // At most the values a JSON text holds: one, and one per comma, bracket and brace outside its strings.
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
            Gives back the count of the large body of req once httplib has written its answer, which
            follows the post-routing handler, or has given it up: the release of the content of
            the answer, when res goes, gives it back.  The bytes of an answer that is not a content
            become one; a streamed answer gives it back after its own release.  An answer to a
            request of ranges of bytes gives it back at once.
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

        // Gives back the values the body of the request of this thread reserved.
        void release_values()
        {
            if (holding_values == 0) return;
            held_values -= holding_values;
            holding_values = 0;
        }

        // Reserves n values for the body of the request of this thread; false when the budget holds no room for them.
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
            // The parser stops after max_body_values values: no document holds more.
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

        // The IPv4 addresses of the Mac on its networks, those of the loopback and of the links apart.
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

        // The settings as the web application reads them: the hash of the password of the network
        // access stays in the service, which tells whether there is one, and its addresses.
        json public_settings(service& svc)
        {
            const host_settings st = svc.settings().get();
            json j = st.to_json();
            j.erase("lan_password_hash");
            j["lan_password_set"] = !st.lan_password_hash.empty();
            j["lan_addresses"] = lan_addresses();
            return j;
        }

        /*
            The session of a device of the network: the cookie albireo_lan, a random nonce and its
            HMAC under the hash of the password, so that a new password ends every session, and a
            restart none.
        */
        const char* const lan_cookie = "albireo_lan";

        std::string lan_session(const std::string& hash)
        {
            const std::string nonce = random_hex(16);
            return nonce + "." + hmac_sha256_hex(hash, nonce);
        }

        bool lan_authorized(const httplib::Request& req, const std::string& hash)
        {
            if (hash.empty()) return false;
            const std::string cookies = req.get_header_value("Cookie");
            const std::string key = std::string(lan_cookie) + "=";
            size_t at = 0;
            while ((at = cookies.find(key, at)) != std::string::npos)
            {
                if (at == 0 || cookies[at - 1] == ' ' || cookies[at - 1] == ';') break;
                at += key.size();
            }
            if (at == std::string::npos) return false;
            const size_t from = at + key.size(), end = cookies.find(';', from);
            const std::string value = cookies.substr(from, end == std::string::npos ? std::string::npos : end - from);
            const size_t dot = value.find('.');
            if (dot == std::string::npos) return false;
            const std::string mac = hmac_sha256_hex(hash, value.substr(0, dot));
            return !mac.empty() && same_secret(mac, value.substr(dot + 1));
        }

        // The page that asks a device of the network for the password.
        const char* const lan_login_page = R"ALBIREO(<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Albireo</title><style>
:root { color-scheme: light dark; --bg: #f6f4f0; --card: #fff; --text: #222; --accent: #e56b3f; }
@media (prefers-color-scheme: dark) { :root { --bg: #0f1424; --card: #18203a; --text: #eee; } }
body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: var(--bg); color: var(--text);
  font: 16px -apple-system, system-ui, sans-serif; }
form { background: var(--card); padding: 28px 24px; border-radius: 16px; width: min(340px, calc(100vw - 32px));
  box-shadow: 0 8px 30px rgb(0 0 0 / 15%); display: grid; gap: 14px; }
h1 { margin: 0; font-size: 22px; } p { margin: 0; opacity: .8; font-size: 14px; }
input, button { font: inherit; padding: 12px; border-radius: 10px; border: 1px solid rgb(128 128 128 / 40%); }
button { background: var(--accent); color: #fff; border: 0; font-weight: 600; }
.error { color: #d33; min-height: 1.2em; }
</style></head><body><form id="f"><h1>Albireo</h1><p>Accès depuis le réseau : le mot de passe choisi dans
l'onglet « Service » des paramètres, sur le Mac.</p>
<input id="p" type="password" autocomplete="current-password" placeholder="Mot de passe" required autofocus>
<button>Entrer</button><p class="error" id="e"></p></form><script>
document.getElementById('f').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const e = document.getElementById('e');
  e.textContent = '';
  const r = await fetch('/albireo/v1/lan/login', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ password: document.getElementById('p').value }) });
  if (r.ok) location.replace('/');
  else e.textContent = r.status === 401 ? 'Mot de passe incorrect.' : 'Erreur ' + r.status + '.';
});
</script></body></html>
)ALBIREO";

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
                if (kind == ALBIREO_EVENT_DONE || kind == ALBIREO_EVENT_ERROR) finished = true;
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
            Whether the client of a request left: its connection closed, or ended by a TLS
            close_notify, whose bytes wait on the socket where the check of httplib sees a live
            client.  The TLS probe is the one httplib makes for the answers it streams, and
            gives back to the next read the byte it takes from a request that follows.
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
            Ends the engine request id of an answer sent whole when the service stops, with
            503, or when its client left.  Looks at them once a second at most, whether the
            events of the engine flow or not; returns true when it ended the request.
        */
        bool end_abandoned(service& svc, const httplib::Request& req, httplib::Response& res, uint64_t id,
            std::chrono::steady_clock::time_point& next_look)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < next_look) return false;
            next_look = now + std::chrono::seconds(1);
            if (svc.stopping())
            {
                svc.engine().cancel(id);
                send_error(res, 503, "server_error", "the service is stopping");
                return true;
            }
            // A client that left ends its request, whose place in the engine the others take.
            if (client_left(req))
            {
                svc.engine().cancel(id);
                return true;
            }
            return false;
        }

        /*
            The engine request of a completion of the OpenAI interface.  The members it takes are
            moved out of source, the body of the request, so that the images and the conversations
            of a large request stay in memory once; source keeps the members it does not take.
        */
        bool engine_request(service& svc, json& source, json& out, std::string& error, std::string& model_name)
        {
            // Read through a const reference, which adds no member to the body.
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
            // The range of top_k in the engine, which bounds the value it receives: a value outside it
            // is refused here, so that the client learns it.
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
            // The work in the background of the web application, whose answers leave the prompts their whole pass.
            if (requested == "albireo-background") out["priority"] = "background";
            return true;
        }

        // The images of a request at most, which the engine reads into the prompt.
        const size_t max_images = 16;

        // A request refused before it reaches the engine, in the form of the OpenAI interface.
        struct refusal
        {
            std::string message;
            std::string code;
            std::string param;
        };

        /*
            The parts of the messages beyond text: "image_url", an image given as a data URL of a
            PNG or JPEG file, since the service fetches nothing (a computer without network reads
            the same requests), max_images at most, and "input_audio".  Counts them; returns false
            with the refusal of the first part that cannot be read.
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
                    // data:<media type>;base64,<data>, of a PNG or JPEG file by its first bytes.
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

        void handle_completion(service& svc, const httplib::Request& req, httplib::Response& res, bool chat)
        {
            json body;
            if (!parse_body(req, res, body)) return;
            json request;
            std::string error, model_name;
            if (!engine_request(svc, body, request, error, model_name))
            {
                // A model resolved and an invalid option, or no model in the engine.
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
                // The images and the sounds go to a model that reads them, as the engine loaded it.
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
            int code = ALBIREO_OK;
            if (!svc.engine().submit(request, sink, id, error, &code))
            {
                if (code == ALBIREO_E_BUSY)
                {
                    // Refused for a while, the images queued for the model holding the memory kept for them.
                    send_error(res, 503, "server_error", error, "engine_busy");
                    res.set_header("Retry-After", "5");
                    return;
                }
                const bool overflow = error.find("context") != std::string::npos;
                send_error(res, 400, "invalid_request_error", error, overflow ? "context_length_exceeded" : "");
                return;
            }
            // The engine holds its own copy of the request: the documents built here go, with their values.
            body = json();
            request = json();
            release_values();
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
                        if (e.first == ALBIREO_EVENT_CONTENT) content += e.second;
                        else if (e.first == ALBIREO_EVENT_REASONING) reasoning += e.second;
                        else if (e.first == ALBIREO_EVENT_TOOL_CALL)
                            calls.push_back(tool_call_of(json::parse_or_null(e.second), -1));
                        else if (e.first == ALBIREO_EVENT_DONE || e.first == ALBIREO_EVENT_ERROR)
                        {
                            done = json::parse_or_null(e.second);
                            failed = e.first == ALBIREO_EVENT_ERROR;
                            break;
                        }
                    }
                    if (end_abandoned(svc, req, res, id, next_look)) return;
                }
                if (failed)
                {
                    send_error(res, 500, "server_error", done["message"].as_string(), done["code"].as_string());
                    return;
                }
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
                // An answer the engine stopped because it repeated itself.
                if (done["repetition"].as_bool(false)) out["repetition"] = true;
                out["system_fingerprint"] = std::string("albireo-") + ALBIREO_VERSION;
                send_json(res, 200, out);
                return;
            }

            // The post-routing handler marks the answers of the interface "no-store".
            res.set_header("X-Accel-Buffering", "no");
            auto first = std::make_shared<bool>(true);
            auto tool_index = std::make_shared<int>(0);
            service* s = &svc;
            res.set_chunked_content_provider("text/event-stream",
                [ch, cid, created, model_name, chat, include_usage, first, tool_index, s, id](
                    size_t, httplib::DataSink& sink) {
                    auto chunk = [&](const json& delta, const json& finish, bool repetition) {
                        json c = json::object();
                        c["id"] = cid;
                        c["object"] = chat ? "chat.completion.chunk" : "text_completion";
                        c["created"] = created;
                        c["model"] = model_name;
                        // The last chunk of an answer the engine stopped because it repeated itself.
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
                        A service that stops ends the answers in progress, whose tokens may flow
                        without pause, and a client that reads far slower than the engine writes
                        ends its own: the events waiting for it stay bounded, and its place in
                        the engine goes to the other requests.
                    */
                    if (s->stopping() || ch->pending() > max_backlog)
                    {
                        s->engine().cancel(id);
                        return false;
                    }
                    std::pair<int, std::string> e;
                    if (!ch->pop(e, 2000))
                    {
                        if (!sink.is_writable() || s->stopping())
                        {
                            s->engine().cancel(id);
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
                    case ALBIREO_EVENT_CONTENT:
                        delta["content"] = e.second;
                        return chunk(delta, json(), false);
                    case ALBIREO_EVENT_REASONING:
                        delta["reasoning_content"] = e.second;
                        return chunk(delta, json(), false);
                    case ALBIREO_EVENT_TOOL_CALL:
                    {
                        json list = json::array();
                        list.push_back(tool_call_of(json::parse_or_null(e.second), (*tool_index)++));
                        delta["tool_calls"] = list;
                        return chunk(delta, json(), false);
                    }
                    case ALBIREO_EVENT_PROGRESS:
                    {
                        // The progress of a long prompt, as a comment line the clients of
                        // the standard ignore and the web application shows.
                        const std::string line = ": progress " + e.second + "\n\n";
                        return sink.write(line.data(), line.size());
                    }
                    case ALBIREO_EVENT_DONE:
                    {
                        const json done = json::parse_or_null(e.second);
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
                [ch, s, id](bool) {
                    std::lock_guard<std::mutex> lock(ch->m);
                    if (!ch->finished) s->engine().cancel(id);
                });
        }

        // Checks a change of the settings sent by the web application; the keys outside this
        // list are set by hand in settings.json only.
        bool validate_settings(const json& body, std::string& error)
        {
            static const char* const keys[] = { "port", "language", "engine", "accelerator",
                "device_mb", "threads", "context", "max_sequences", "kv_mb", "kv_type", "offline",
                "proxy", "web_access", "web_search", "lan", "lan_port", "lan_password" };
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
            const json& password = body["lan_password"];
            if (body.contains("lan_password")
                && !(password.is_string() && password.as_string().size() >= 4 && password.as_string().size() <= 128))
            {
                error = "invalid value for lan_password: 4 to 128 characters";
                return false;
            }
            return one_of("web_search", { "wikipedia", "none" }) && boolean("web_access") && boolean("lan")
                && in_range("lan_port", 1024, 65535)
                && one_of("language", { "auto", "en", "fr", "de", "it", "es" })
                && one_of("kv_type", { "auto", "q8", "f16" })
                && one_of("engine", { "auto", "best", "cpu", "cpu-avx2", "cpu-avxvnni", "openvino", "cuda" })
                && one_of("accelerator", { "auto", "none" })
                && in_range("port", 1024, 65535) && in_range("threads", 0, 1024) && in_range("context", 0, 1 << 20)
                && in_range("max_sequences", 1, 64) && in_range("kv_mb", 0, 1 << 24)
                && in_range("device_mb", 0, 1 << 24)
                && boolean("offline");
        }

        // The floats of the base64 form of a vector.
        std::vector<float> floats_of(const std::string& text)
        {
            const std::string bytes = base64_decode(text);
            std::vector<float> v(bytes.size() / sizeof(float));
            if (!v.empty()) std::memcpy(v.data(), bytes.data(), v.size() * sizeof(float));
            return v;
        }

        /*
            The text read aloud by the speech synthesis that comes with Albireo, as the speech
            route of the OpenAI interface: input (at most 8192 bytes of UTF-8), voice (a voice
            of Albireo or its language, else the language of the request or English), speed
            (0.25 to 4 accepted, read from 0.5 to 2) and response_format (wav, or pcm for the
            samples alone).  model is not checked: one synthesizer serves every model name.
        */
        void handle_speech(service& svc, const httplib::Request& req, httplib::Response& res)
        {
            json body;
            if (!parse_body(req, res, body)) return;
            if (!body["input"].is_string() || body["input"].as_string().empty())
            {
                send_error(res, 400, "invalid_request_error", "input must be a non-empty string", "", "input");
                return;
            }
            const std::string input = body["input"].as_string();
            if (input.size() > speech_max_bytes)
            {
                send_error(res, 400, "invalid_request_error", "input holds more than 8192 bytes",
                    "string_above_max_length", "input");
                return;
            }
            const std::string format = body["response_format"].str("wav");
            if (format != "wav" && format != "pcm")
            {
                send_error(res, 400, "invalid_request_error", "response_format must be wav or pcm", "",
                    "response_format");
                return;
            }
            double speed = 1.0;
            if (body.contains("speed"))
            {
                speed = body["speed"].as_double(-1.0);
                if (!body["speed"].is_number() || !(speed >= 0.25 && speed <= 4.0))
                {
                    send_error(res, 400, "invalid_request_error", "speed must be a number from 0.25 to 4", "", "speed");
                    return;
                }
            }
            // A voice of Albireo by its name or its language; the voices of other services read
            // with the voice of the language the request gives.
            const speech_voice* voice = find_voice(body["voice"].as_string());
            if (!voice) voice = find_voice(body["language"].str("en"));
            if (!voice)
            {
                send_error(res, 400, "invalid_request_error", "language must be en, fr, de, it or es", "", "language");
                return;
            }
            const std::string dir = svc.speech_dir();
            if (!file_exists(svc.speech_program()) || installed_voices(dir).empty())
            {
                send_error(res, 503, "server_error", "the speech synthesis or its voices are not installed",
                    "speech_unavailable");
                return;
            }
            std::string wav, error;
            if (!speak(svc.speech_program(), dir, *voice, speed, input, wav, error))
            {
                log_warning("speech: " + error);
                send_error(res, 500, "server_error", error, "speech_failed");
                return;
            }
            res.status = 200;
            if (format == "pcm") res.set_content(wav.substr(44), "audio/pcm");
            else res.set_content(wav, "audio/wav");
            res.set_header("Cache-Control", "no-store");
        }

        /*
            The vectors of texts, as the OpenAI interface computes them: "input" a text or a
            list of texts (or of token lists), "encoding_format" float or base64, and
            "dimensions" for the models that keep their meaning when shortened.  The
            requests of every client share the passes of the embedding model.
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
                    || requested == "albireo-embedding";
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
            // Moved, not copied: the texts of a large request stay in memory once.
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
            // The engine holds its own copy of the request: the documents built here go, with their values.
            body = json();
            request = json();
            release_values();
            std::pair<int, std::string> e;
            auto next_look = std::chrono::steady_clock::now();
            for (;;)
            {
                if (ch->pop(e, 1000) && (e.first == ALBIREO_EVENT_DONE || e.first == ALBIREO_EVENT_ERROR)) break;
                if (end_abandoned(svc, req, res, id, next_look)) return;
            }
            const json done = json::parse_or_null(e.second);
            if (e.first == ALBIREO_EVENT_ERROR)
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

        // Refuses an operation that needs the network on a computer set offline.
        bool refuse_offline(service& svc, httplib::Response& res)
        {
            if (!svc.settings().get().offline) return false;
            send_json(res, 409, ok_result(false, user_error().set("offline mode: the catalog, the updates and the "
                "models come from files (import)", "offline")));
            return true;
        }

        // The answer of a route of the web access (web_access.h): its body, or its error with its code.
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
            The license texts of the installation lie in the licenses folder of the programs: those
            of the components the programs hold (cmake/payload.cmake) and of the runtimes of the
            engines.  A text is named by its file, an entry of that folder read through no link.
        */
        const uint64_t max_license_bytes = 1u << 20;

        struct license_component
        {
            const char* file;
            const char* name;
            const char* version;    // the version the service was built with, empty when it has none
        };

        const license_component license_components[] = {
            {"albireo.txt", "Albireo", ALBIREO_VERSION},
            {"dlib.txt", "dlib", ""},
            {"mbedtls.txt", "Mbed TLS", MBEDTLS_VERSION_STRING},
            {"cpp-httplib.txt", "cpp-httplib", CPPHTTPLIB_VERSION},
            {"zlib.txt", "zlib", ZLIB_VERSION},
            {"sqlite.txt", "SQLite", SQLITE_VERSION},
            {"libpng.txt", "libpng", ""},
            {"libjpeg.txt", "libjpeg (Independent JPEG Group)", ""},
            {"openvino-license.txt", "OpenVINO", ""},
            {"cuda-eula.txt", "CUDA", ""},
            {"cudnn-license.txt", "cuDNN", ""}
        };

        // Whether name can be an entry of the licenses folder: no separator, colon, control
        // character or leading dot, which leaves out "." and "..".
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

        // Opens the entry name of the licenses folder through no link: a file of one name only (no
        // hard link) and of max_license_bytes at most.
        bool open_license(const std::string& folder, const std::string& name, link_free_file& f)
        {
            std::string why;
            return license_file_name(name) && f.open(join_path(folder, name), folder, why) && f.links() == 1
                && f.size() <= max_license_bytes;
        }

        // The license texts of folder: [{"file", "name", "size", "version"}], "version" when known.
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

        // Reads the whole text of a license opened by open_license().
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

    void install_routes(httplib::Server& srv, service& svc, bool lan)
    {
        service* s = &svc;

        // The password of the network access, then the bodies the routes accept.
        srv.set_pre_routing_handler([s, lan](const httplib::Request& req, httplib::Response& res) {
            auto refuse = [&](int status, const char* message) {
                res.status = status;
                res.set_header("Connection", "close");
                res.set_content(message, "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            };
            if (lan && !(req.method == "POST" && req.path == "/albireo/v1/lan/login")
                && !lan_authorized(req, s->settings().get().lan_password_hash))
            {
                // A page asks for the password; any other request is refused.
                const bool page = req.method == "GET" && req.path.compare(0, 4, "/v1/") != 0
                    && req.path.compare(0, 9, "/albireo/") != 0;
                res.status = page ? 200 : 401;
                res.set_header("Cache-Control", "no-store");
                if (page) res.set_content(lan_login_page, "text/html; charset=utf-8");
                else send_error(res, 401, "authentication_error", "the network access asks for its password first",
                    "lan_login");
                return httplib::Server::HandlerResponse::Handled;
            }
            // The methods that send no data take no body, which httplib would read whole before
            // their handlers run.  Any other body is JSON of a known length, of 64 MB at most,
            // apart from the models sent to their own route.
            const bool no_body = req.method == "GET" || req.method == "HEAD" || req.method == "DELETE"
                || req.method == "OPTIONS";
            if (no_body && (req.has_header("Transfer-Encoding") || content_length(req) > 0))
                return refuse(400, "this method takes no body");
            if (!(req.method == "POST" && req.path == upload_route))
            {
                if (req.has_header("Transfer-Encoding")) return refuse(411, "the body must have a length");
                if (content_length(req) > max_json_body) return refuse(413, "the body is too large");
                if (content_length(req) > 0 && req.get_header_value("Content-Type").find("application/json") != 0)
                    return refuse(415, "the body must be application/json");
                // A count this thread kept from a request whose answer was never written is given back.
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
            // The body of the request is handled: the values of its document go back, its count after the answer.
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
                    The page in which the web application runs the code of the agent: framed
                    by the application only, in an iframe whose sandbox gives it an origin of
                    its own, so that the code reaches neither the data of the application nor
                    the network.  Its script and the workers it starts may evaluate code.
                */
                res.set_header("X-Frame-Options", "SAMEORIGIN");
                res.set_header("Cross-Origin-Resource-Policy", "same-origin");
                res.set_header("Content-Security-Policy", "default-src 'none'; script-src 'self' blob: 'unsafe-eval'; "
                    "worker-src blob:; style-src 'unsafe-inline'; frame-ancestors 'self'; base-uri 'none'; "
                    "form-action 'none'");
                return;
            }
            res.set_header("X-Frame-Options", "DENY");
            // The script of the sandbox is loaded by a page of another (opaque) origin.
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
            if (req.path.compare(0, 4, "/v1/") == 0 || req.path.compare(0, 11, "/albireo/v1") == 0)
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

        // OpenAI compatible interface.
        srv.Get("/v1/models", [s](const httplib::Request&, httplib::Response& res) {
            json list = json::array();
            const host_settings st = s->settings().get();
            const long long now = static_cast<long long>(now_unix());
            // What each model reads and the company that made it; an alias gives those of its model.
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
                m["owned_by"] = "albireo";
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
                m["id"] = std::string("albireo-") + role;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "albireo";
                m["alias_of"] = target;
                add_traits(m, target);
                list.push_back(m);
            }
            const std::string background = s->resolve_model("background");
            if (!background.empty())
            {
                json m = json::object();
                m["id"] = "albireo-background";
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "albireo";
                m["alias_of"] = background;
                add_traits(m, background);
                list.push_back(m);
            }
            const std::string embedder = s->resolve_embedding_model(std::string());
            if (!embedder.empty())
            {
                json m = json::object();
                m["id"] = std::string("albireo-") + embedding_role;
                m["object"] = "model";
                m["created"] = now;
                m["owned_by"] = "albireo";
                m["alias_of"] = embedder;
                add_traits(m, embedder);
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
        srv.Post("/v1/audio/speech", [s](const httplib::Request& req, httplib::Response& res) {
            handle_speech(*s, req, res);
        });
        // The voices of the speech synthesis whose files are installed, none without its program.
        srv.Get("/albireo/v1/speech/voices", [s](const httplib::Request&, httplib::Response& res) {
            json list = json::array();
            const std::vector<speech_voice> voices = file_exists(s->speech_program())
                ? installed_voices(s->speech_dir()) : std::vector<speech_voice>();
            for (const auto& v : voices)
            {
                json o = json::object();
                o["id"] = v.id;
                o["language"] = v.language;
                list.push_back(o);
            }
            json out = json::object();
            out["voices"] = list;
            out["format"] = "wav";
            out["sample_rate"] = 16000;
            send_json(res, 200, out);
        });

        // Interface of the web application and of the tray.  The status tells the web application
        // whether the web access is available and its provider of search (web_capability()).
        srv.Get("/albireo/v1/status", [s](const httplib::Request&, httplib::Response& res) {
            json status = s->status();
            status["web"] = web_capability(s->settings().get());
            send_json(res, 200, status);
        });
        srv.Get("/albireo/v1/models", [s](const httplib::Request& req, httplib::Response& res) {
            std::string lang = req.get_param_value("lang");
            if (lang.empty() || lang == "auto") lang = "en";
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
        srv.Get("/albireo/v1/usage", [s](const httplib::Request&, httplib::Response& res) {
            json o = json::object();
            o["days"] = s->db().usage(30);
            send_json(res, 200, o);
        });
        srv.Get("/albireo/v1/engines", [s](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, s->engines());
        });
        // The license texts of the installation, read only: their list, and each text by its file.
        const std::string licenses = join_path(svc.program_dir(), "licenses");
        srv.Get("/albireo/v1/licenses", [licenses](const httplib::Request&, httplib::Response& res) {
            send_json(res, 200, license_list(licenses));
        });
        srv.Get(R"(/albireo/v1/licenses/(.+))", [licenses](const httplib::Request& req, httplib::Response& res) {
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
        srv.Post("/albireo/v1/count", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            json q = json::object();
            q["op"] = body["messages"].is_array() ? "count" : "tokenize";
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
            The web access of the agent mode (web_access.h): local routes of the web application,
            of the origin of the rest of the interface, refused while the service works offline
            or once the administrator turned the web access off.
        */
        srv.Post("/albireo/v1/web/search", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            send_web_reply(res, web_search(body, s->settings().get()));
        });
        srv.Post("/albireo/v1/web/fetch", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            send_web_reply(res, web_fetch(body, s->settings().get()));
        });

        // The catalog is fetched in the background; its state is in /albireo/v1/models.
        srv.Post("/albireo/v1/models/refresh", [s](const httplib::Request& req, httplib::Response& res) {
            if (refuse_offline(*s, res)) return;
            s->models().refresh_catalog_async();
            json o = ok_result(true);
            o["catalog"] = s->models().catalog_status();
            send_json(res, 202, o);
        });
        srv.Post(R"(/albireo/v1/models/([A-Za-z0-9._-]+)/download)", [s](const httplib::Request& req,
                                                                               httplib::Response& res) {
            if (refuse_offline(*s, res)) return;
            user_error error;
            const bool ok = s->models().start_download(req.matches[1], error);
            send_json(res, ok ? 202 : 400, ok_result(ok, error));
        });
        srv.Delete(R"(/albireo/v1/models/([A-Za-z0-9._-]+)/download)", [s](const httplib::Request& req,
                                                                                 httplib::Response& res) {
            s->models().cancel_download(req.matches[1]);
            send_json(res, 200, ok_result(true));
        });
        srv.Post(R"(/albireo/v1/models/([A-Za-z0-9._-]+)/activate)", [s](const httplib::Request& req,
                                                                               httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            user_error error;
            const bool ok = s->activate(req.matches[1], body["role"].str("general"), error);
            send_json(res, ok ? 200 : 400, ok_result(ok, error));
        });
        srv.Post(R"(/albireo/v1/models/([A-Za-z0-9._-]+)/deactivate)", [s](const httplib::Request& req,
                                                                                 httplib::Response& res) {
            user_error error;
            const bool ok = s->deactivate(req.matches[1], error);
            send_json(res, ok ? 200 : 400, ok_result(ok, error));
        });
        srv.Post(R"(/albireo/v1/models/([A-Za-z0-9._-]+)/verify)", [s](const httplib::Request& req,
                                                                             httplib::Response& res) {
            send_json(res, 200, s->models().verify(req.matches[1]));
        });
        srv.Delete(R"(/albireo/v1/models/([A-Za-z0-9._-]+))", [s](const httplib::Request& req,
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
            The signed offline bundles and the content of the import folder (service::import_folder()),
            which the service reads with its own rights: a refusal comes with its code and the path.
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
        srv.Post("/albireo/v1/import", import);
        srv.Post("/albireo/v1/models/import", import);
        // A model file sent by the tray, read with the rights of its user, for the files the service never reads.
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
            // A new file of the models folder reached through no link, as every file written there.
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
            // A model installed already under another file is a success too ("already"), as a
            // projector kept until its decoder comes ("pending").
            const json r = s->models().import_received(tmp, name, hash.hex());
            send_json(res, r["imported"].empty() && r["already"].empty() && r["pending"].empty() ? 400 : 200, r);
        });

        // The password of the proxy is given as proxy_password_mask (host_settings::to_json()).
        /*
            The login of a device of the network: the password of the network access gives a cookie
            for 30 days.  A wrong one waits a second, so that guessing it takes time.
        */
        srv.Post("/albireo/v1/lan/login", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            const std::string hash = s->settings().get().lan_password_hash;
            if (hash.empty() || !password_matches(body["password"].as_string(), hash))
            {
                log_warning("server: a wrong password of the network access, from " + req.remote_addr);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                send_error(res, 401, "authentication_error", "wrong password", "lan_password_wrong");
                return;
            }
            log_info("server: the network access opened to " + req.remote_addr);
            res.set_header("Set-Cookie", std::string(lan_cookie) + "=" + lan_session(hash)
                + "; Path=/; Max-Age=2592000; HttpOnly; SameSite=Strict");
            send_json(res, 200, ok_result(true));
        });
        srv.Post("/albireo/v1/lan/logout", [](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Set-Cookie", std::string(lan_cookie) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
            send_json(res, 200, ok_result(true));
        });
        srv.Get("/albireo/v1/settings", [s](const httplib::Request& req, httplib::Response& res) {
            send_json(res, 200, public_settings(*s));
        });
        srv.Put("/albireo/v1/settings", [s](const httplib::Request& req, httplib::Response& res) {
            json body;
            if (!parse_body(req, res, body)) return;
            std::string error;
            if (!validate_settings(body, error))
            {
                send_error(res, 400, "invalid_request_error", error);
                return;
            }
            // Read through a const reference, which adds no member to the body.
            const json& changes = body;
            // A new port is opened once before it is saved, so that a port taken by another
            // program or reserved by the system never leaves the service without an address.
            const long long port = changes["port"].as_int(s->port());
            if (port != s->port() && !port_available(static_cast<int>(port)))
            {
                send_error(res, 409, "invalid_request_error", "the port " + std::to_string(port)
                    + " is in use or cannot be opened", "port_unavailable");
                return;
            }
            // The access from the network: its own port, and a password before it opens.
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
            if (changes["lan"].as_bool(now.lan) && now.lan_password_hash.empty() && !changes.contains("lan_password"))
            {
                send_error(res, 400, "invalid_request_error", "the network access needs a password first",
                    "lan_password_missing");
                return;
            }
            // The password is kept as its hash only, computed outside the lock of the settings.
            std::string lan_hash;
            if (changes.contains("lan_password"))
            {
                lan_hash = password_hash(changes["lan_password"].as_string());
                if (lan_hash.empty())
                {
                    send_error(res, 500, "server_error", "the password cannot be hashed");
                    return;
                }
            }
            host_settings before;
            bool masked_elsewhere = false;
            const host_settings after = s->settings().update([&](host_settings& current) {
                before = current;
                // The mask of the password stands for the stored password, with the address and
                // the user it was given for only: it never sends the password to another server.
                const std::string proxy = changes["proxy"].as_string();
                if (proxy_password_masked(proxy) && proxy != masked_proxy(current.proxy))
                {
                    masked_elsewhere = true;
                    return;
                }
                json merged = current.to_json();
                for (const auto& kv : body.items())
                    if (kv.first != "lan_password") merged[kv.first] = kv.second;
                if (!lan_hash.empty()) merged["lan_password_hash"] = lan_hash;
                current.from_json(merged);
                if (current.proxy == masked_proxy(before.proxy)) current.proxy = before.proxy;
            });
            if (masked_elsewhere)
            {
                send_error(res, 400, "invalid_request_error", "the masked password of the proxy applies to its "
                    "stored address and user only: type the password again", "proxy_password");
                return;
            }
            s->apply_network_settings(after);
            // What the running engine or server cannot take without a restart.
            const bool restart = after.port != before.port || after.engine != before.engine
                || after.accelerator != before.accelerator || after.device_mb != before.device_mb
                || after.threads != before.threads
                || after.context != before.context || after.max_sequences != before.max_sequences
                || after.kv_mb != before.kv_mb || after.kv_type != before.kv_type
                || after.lan != before.lan || after.lan_port != before.lan_port;
            if (!lan_hash.empty()) log_info("server: the password of the network access changed");
            json o = ok_result(true);
            o["restart"] = restart;
            send_json(res, 200, o);
            if (restart) s->request_restart();
        });
        srv.Post("/albireo/v1/restart", [s](const httplib::Request& req, httplib::Response& res) {
            send_json(res, 202, ok_result(true));
            s->request_restart();
        });
        srv.Post("/albireo/v1/engines/benchmark", [s](const httplib::Request& req, httplib::Response& res) {
            // A measure already running refuses the second one with 409.
            const json r = s->benchmark_engines();
            send_json(res, r["code"].as_string() == "benchmark_running" ? 409 : 200, r);
        });
        srv.Get("/albireo/v1/logs", [](const httplib::Request& req, httplib::Response& res) {
            json lines = json::array();
            for (const auto& l : log_file::instance().recent()) lines.push_back(l);
            json o = json::object();
            o["lines"] = lines;
            send_json(res, 200, o);
        });

        // The web application, the documentation viewer and their assets.
        const std::string web = join_path(svc.program_dir(), "web");
        if (!srv.set_mount_point("/", web)) log_warning("http: no web folder at " + web);
        srv.set_file_extension_and_mimetype_mapping("mjs", "text/javascript");
        srv.set_file_extension_and_mimetype_mapping("woff2", "font/woff2");
        srv.set_file_extension_and_mimetype_mapping("webmanifest", "application/manifest+json");
        srv.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
            /*
                Every file of the application is checked at each load, its pages as well as its
                scripts and styles, so that an update shows at once: without this header, a
                browser may keep the scripts of the previous version for hours.  The check costs
                a request answered by 304 Not Modified while the file is unchanged.
            */
            res.set_header("Cache-Control", "no-cache");
        });
        srv.Get("/docs", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/docs/"); });
    }

}
