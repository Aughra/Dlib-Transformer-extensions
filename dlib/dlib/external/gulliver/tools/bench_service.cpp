// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les mesures « comme un utilisateur » (Gulliver 7.22.0, à la place de tools/bench_service.py ; fork pour macOS,
    2026-11-22) : par le vrai service, sur un service d'essai isolé, jamais sur le service de l'utilisateur.

        build/macos-gulliver/bin/bench_service fichier.gguf [--name "nom affiché"] [--json sortie.json] [--llama] [--quick]

    Un service Gulliver d'essai (port 18496, ses propres données dans build/bench-service, hors ligne) reçoit un clone
    du modèle par sa bibliothèque (cp -c : instantané, sans place prise), le valide comme le ferait l'administrateur
    (modèle hors catalogue, Gulliver 7.17.0), puis :
      5. le temps de chargement : du lancement du service, le modèle déjà installé, à la fin d'une toute petite
         réponse (llama-server : de son lancement à la même réponse) ;
      1. une conversation longue : un document d'environ 32 768 tokens, une réponse de 512 tokens au plus, avec les
         brouillons (le réglage par défaut), puis le service relancé sans brouillons (draft_tokens 0) ;
      2. 4 requêtes à la fois (questions courtes, 256 tokens de réponse chacune) : le débit total ;
      3. 3 documents d'environ 4 000 tokens à la fois (256 tokens de réponse) : le temps total et l'attente moyenne ;
      4. le plus grand contexte qui tient : 65 536 puis 131 072 tokens (dans la limite du modèle, 95 % de la taille
         pour laisser la place du modèle de conversation), réponse de 16 tokens, jusqu'au premier échec ; 262 144
         n'est pas essayé (de 30 à 60 minutes de lecture chaque semaine).  --quick s'en passe (pour un essai).
    Avec --llama, les tests 5, 1 (sans brouillons), 2 et 3 sur llama-server (llama.cpp, 4 requêtes en parallèle, même
    fichier), comme une ligne de comparaison.  Les temps viennent des réponses elles-mêmes (« timings », « usage ») ;
    chaque requête a un temps limite.  Écrit le résultat en JSON (--json, mêmes clés que l'outil Python, que lit
    l'option --service du conseil) et sur la sortie.  Le service d'essai est appelé en HTTPS, son certificat vérifié
    avec sa propre autorité ; llama-server en HTTP sur 127.0.0.1.
*/

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <httplib.h>

#include "client_service.h"
#include "../common/json.h"
#include "../common/platform.h"

extern char** environ;

using gulliver::json;

namespace
{
    const int service_port = 18496, llama_port = 18497;
    std::string root, work, service_program, text_path;

    double now_s()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void pause_for(double seconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
    }

    void say(const std::string& text)
    {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char stamp[16];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S ", &tm);
        std::printf("%s%s\n", stamp, text.c_str());
        std::fflush(stdout);
    }

    double round1(double x) { return std::nearbyint(x * 10) / 10; }

    // L'échec d'une requête, avec le corps de la réponse quand il y en a un (le refus d'un contexte trop grand).
    struct call_error : std::runtime_error
    {
        std::string body;
        call_error(const std::string& what, const std::string& b) : std::runtime_error(what), body(b) {}
    };

    // Un point d'appel : le service d'essai (HTTPS) ou llama-server (HTTP).
    struct endpoint
    {
        bool https = true;
        int port = service_port;
    };

    json call(const endpoint& at, const std::string& path, const json* body = nullptr, int timeout = 60)
    {
        const std::string data = body ? body->dump() : std::string();
        httplib::Result r;
        httplib::Headers h;
        std::shared_ptr<httplib::Client> cl;
        if (at.https)
        {
            gulliver::tls_route route;
            route.host = "localhost";
            route.port = at.port;
            route.address = "127.0.0.1";
            route.ca_path = gulliver::join_path(work, "home/certificates/gulliver-authority.crt");
            std::string error;
            cl = gulliver::https_client(route, error);
            if (!cl) throw call_error("<urlopen error " + error + ">", "");
        }
        else cl = std::make_shared<httplib::Client>("127.0.0.1", at.port);
        cl->set_keep_alive(false);
        cl->set_connection_timeout(timeout, 0);
        cl->set_read_timeout(timeout, 0);
        cl->set_write_timeout(timeout, 0);
        r = body ? cl->Post(path, h, data, "application/json") : cl->Get(path, h);
        if (!r) throw call_error(r.error() == httplib::Error::Read ? "timed out" : "<urlopen error " + httplib::to_string(r.error()) + ">", "");
        if (r->status < 200 || r->status >= 300)
            throw call_error("HTTP Error " + std::to_string(r->status) + ": " + httplib::status_message(r->status), r->body);
        json j;
        if (!json::parse(r->body, j)) throw call_error("the answer is not JSON", r->body);
        return j;
    }

    json call(const endpoint& at, const std::string& path, const json& body, int timeout = 60)
    {
        return call(at, path, &body, timeout);
    }

    struct chat_result
    {
        double seconds = 0;
        json usage, timings;
    };

    chat_result chat(const endpoint& at, const std::string& text, int max_tokens, int timeout = 900)
    {
        json message = json::object();
        message["role"] = "user";
        message["content"] = text;
        json messages = json::array();
        messages.elements().push_back(message);
        json body = json::object();
        body["messages"] = messages;
        body["max_tokens"] = max_tokens;
        body["temperature"] = 0;
        const double t = now_s();
        const json r = call(at, "/v1/chat/completions", body, timeout);
        chat_result out;
        out.seconds = now_s() - t;
        out.usage = r["usage"].is_object() ? r["usage"] : json::object();
        out.timings = r["timings"].is_object() ? r["timings"] : json::object();
        return out;
    }

    // (lecture, écriture) en tokens/s, de Gulliver (prompt_per_second…) ou de llama-server (predicted_per_second…).
    json read_speed(const json& timings) { return timings["prompt_per_second"]; }
    json write_speed(const json& timings)
    {
        return timings.contains("generation_per_second") ? timings["generation_per_second"] : timings["predicted_per_second"];
    }

    // Un programme lancé en arrière-plan, sa sortie ajoutée à log.
    pid_t spawn(std::vector<std::string> argv, const std::string& log, const std::vector<std::string>& extra_env)
    {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        std::vector<std::string> env;
        for (char** e = environ; *e; ++e)
        {
            bool replaced = false;
            for (const std::string& x : extra_env)
                replaced = replaced || std::strncmp(*e, x.c_str(), x.find('=') + 1) == 0;
            if (!replaced) env.push_back(*e);
        }
        for (const std::string& x : extra_env) env.push_back(x);
        std::vector<char*> envp, args;
        for (std::string& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        for (std::string& a : argv) args.push_back(a.data());
        args.push_back(nullptr);
        pid_t pid = -1;
        const int r = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), envp.data());
        posix_spawn_file_actions_destroy(&fa);
        if (r != 0) throw std::runtime_error("cannot start " + argv[0]);
        return pid;
    }

    void stop_process(pid_t& pid)
    {
        if (pid <= 0) return;
        kill(pid, SIGTERM);
        for (int i = 0; i < 300; ++i)
        {
            if (waitpid(pid, nullptr, WNOHANG) == pid)
            {
                pid = -1;
                return;
            }
            pause_for(0.1);
        }
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        pid = -1;
    }

    std::string base_name(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    struct gulliver_service
    {
        std::string model, home;
        bool drafts = true;
        pid_t pid = -1;
        endpoint at;

        void start(bool fresh)
        {
            const std::string library = gulliver::join_path(home, "library");
            if (fresh)
            {
                if (std::system(("rm -rf '" + work + "'").c_str()) != 0) throw std::runtime_error("cannot empty " + work);
                gulliver::make_dirs(library);
                chmod(library.c_str(), 0700);
                chmod(home.c_str(), 0700);
                if (std::system(("cp -c '" + model + "' '" + library + "/'").c_str()) != 0)
                    throw std::runtime_error("cannot clone " + model);
            }
            // Au redémarrage, les réglages que le service a écrits (le modèle actif) sont gardés : seules nos clés changent.
            const std::string path = gulliver::join_path(home, "settings.json");
            json settings = json::object();
            if (!fresh)
            {
                std::string text;
                json j;
                if (gulliver::read_file(path, text, 16u << 20) && json::parse(text, j) && j.is_object()) settings = j;
            }
            settings["port"] = service_port;
            settings["offline"] = true;
            settings["default_model"] = false;
            settings["lan"] = false;
            settings["library_dir"] = library;
            settings["draft_tokens"] = drafts ? 7 : 0;
            if (!gulliver::write_file_atomic(path, settings.dump())) throw std::runtime_error("cannot write " + path);
            pid = spawn({service_program, "--console"}, gulliver::join_path(work, "service.log"), {"GULLIVER_HOME=" + home});
            for (int i = 0; i < 120; ++i)
            {
                try
                {
                    call(at, "/gulliver/v1/status", nullptr, 3);
                    return;
                }
                catch (const std::exception&)
                {
                    pause_for(1);
                }
            }
            throw std::runtime_error("the test service does not answer");
        }

        void stop() { stop_process(pid); }

        bool loaded(const std::string& id)
        {
            for (const json& x : call(at, "/gulliver/v1/models")["loaded"].elements())
                if (x.str() == id) return true;
            return false;
        }

        // Le modèle de la bibliothèque dans le service, validé, actif ; rend le temps de chargement.
        double load()
        {
            const std::string name = base_name(model);
            // La bibliothèque lit tout le fichier avant de répondre : une minute ne suffit pas à un modèle de 18 Go froid.
            json body = json::object();
            body["file"] = name;
            const json r = call(at, "/gulliver/v1/library/load", body, 900);
            std::vector<std::string> ids;
            for (const char* k : {"imported", "already"})
                for (const json& x : r[k].elements()) ids.push_back(x.str());
            std::string mid;
            for (int i = 0; i < 120 && mid.empty(); ++i)
            {
                for (const json& m : call(at, "/gulliver/v1/models")["models"].elements())
                {
                    if (!m["installed"].as_bool()) continue;
                    bool known = base_name(m["file"].str()) == name;
                    for (const std::string& id : ids) known = known || m["id"].str() == id;
                    if (known)
                    {
                        mid = m["id"].str();
                        break;
                    }
                }
                if (mid.empty()) pause_for(1);
            }
            if (mid.empty()) throw std::runtime_error("the library did not load " + name);
            // Hors de tout catalogue signé : validé comme le ferait l'administrateur (Gulliver 7.17.0) ; relit le fichier.
            call(at, "/gulliver/v1/models/" + mid + "/approve", json::object(), 900);
            // Le chargement : de l'activation à la fin d'une toute petite réponse ; le service ne sert un modèle qu'une
            // fois chargé (GET /gulliver/v1/models : « loaded »).
            const double t = now_s();
            json role = json::object();
            role["role"] = "general";
            call(at, "/gulliver/v1/models/" + mid + "/activate", role);
            for (int i = 0; i < 1200 && !loaded(mid); ++i) pause_for(0.5);
            chat(at, "Bonjour.", 4, 600);
            return now_s() - t;
        }
    };

    struct llama_server
    {
        std::string model;
        pid_t pid = -1;
        endpoint at{false, llama_port};

        // llama-server : ctx tokens de contexte en tout, partagés entre slots places ; rend le temps jusqu'à la fin
        // d'une toute petite réponse.
        double start_and_load(int ctx, int slots)
        {
            const double t = now_s();
            pid = spawn({"llama-server", "-m", model, "-ngl", "99", "-c", std::to_string(ctx), "-np", std::to_string(slots),
                            "--port", std::to_string(llama_port), "--host", "127.0.0.1"},
                gulliver::join_path(work, "llama-server.log"), {});
            for (int i = 0; i < 600; ++i)
            {
                try
                {
                    chat(at, "Bonjour.", 4, 5);
                    return now_s() - t;
                }
                catch (const std::exception&)
                {
                    pause_for(0.5);
                }
            }
            throw std::runtime_error("llama-server does not answer");
        }

        void stop() { stop_process(pid); }
    };

    // Le texte en points de code, pour couper et tourner comme les chaînes de Python.
    std::u32string decode(const std::string& s)
    {
        std::u32string out;
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
            char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
            for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
            out += cp;
            i += n;
        }
        return out;
    }

    std::string encode(const std::u32string& s)
    {
        std::string out;
        for (const char32_t c : s)
        {
            if (c < 0x80) out += static_cast<char>(c);
            else if (c < 0x800)
            {
                out += static_cast<char>(0xc0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
            else if (c < 0x10000)
            {
                out += static_cast<char>(0xe0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
            else
            {
                out += static_cast<char>(0xf0 | (c >> 18));
                out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        return out;
    }

    // Un texte d'environ tokens tokens (le texte de justesse, répété), mesuré par /gulliver/v1/count quand il existe.
    std::string document(const endpoint& count_at, long long tokens, long long offset = 0)
    {
        std::string raw;
        if (!gulliver::read_file(text_path, raw, 64u << 20)) throw std::runtime_error("cannot read " + text_path);
        std::u32string src = decode(raw);
        if (src.empty()) throw std::runtime_error("empty " + text_path);
        const size_t shift = static_cast<size_t>(offset) % src.size();
        src = src.substr(shift) + src.substr(0, shift);
        double ratio = 3.2;
        try
        {
            json body = json::object();
            body["text"] = encode(src.substr(0, 20000));
            const long long n = call(count_at, "/gulliver/v1/count", body)["tokens"].as_int(0);
            if (n > 0) ratio = 20000.0 / static_cast<double>(n);
        }
        catch (const std::exception&)
        {
        }
        const size_t chars = static_cast<size_t>(static_cast<double>(tokens) * ratio);
        std::u32string out;
        while (out.size() < chars) out += src;
        return encode(out.substr(0, chars));
    }

    json long_conversation(const endpoint& at, const endpoint& count_at)
    {
        const std::string doc = document(count_at, 32768 - 700);
        const chat_result r = chat(at, "Voici un document :\n\n" + doc + "\n\nRésume ce document en détail, en reprenant "
                                       "ses chiffres.", 512);
        json o = json::object();
        o["prompt_tokens"] = r.usage["prompt_tokens"];
        o["answer_tokens"] = r.usage["completion_tokens"];
        o["read"] = read_speed(r.timings);
        o["write"] = write_speed(r.timings);
        o["total_s"] = round1(r.seconds);
        return o;
    }

    json together(const endpoint& at, const endpoint& count_at, int n, int doc_tokens, int answer)
    {
        std::vector<std::string> texts;
        for (int i = 0; i < n; ++i)
            texts.push_back(doc_tokens ? "Voici un document :\n\n" + document(count_at, doc_tokens, 7919LL * i) +
                                             "\n\nQue dit ce document ? Réponds en détail."
                                       : "Explique en détail le sujet n° " + std::to_string(i + 1) +
                                             " : la mémoire d'un ordinateur, ses niveaux et leur vitesse.");
        const double t = now_s();
        std::vector<std::future<chat_result>> pending;
        for (const std::string& x : texts)
            pending.push_back(std::async(std::launch::async, [&at, x, answer]() { return chat(at, x, answer); }));
        std::vector<chat_result> results;
        std::exception_ptr failure;
        for (auto& f : pending)
        {
            try
            {
                results.push_back(f.get());
            }
            catch (...)
            {
                if (!failure) failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);
        const double wall = now_s() - t;
        long long tokens = 0;
        double waits = 0;
        bool any_wait = false;
        for (const chat_result& r : results)
        {
            tokens += r.usage["completion_tokens"].as_int(0);
            const double w = (r.timings["queued_ms"].as_double(0) + r.timings["prompt_ms"].as_double(0)) / 1000;
            waits += w;
            any_wait = any_wait || w != 0;
        }
        json o = json::object();
        o["requests"] = n;
        o["answered"] = static_cast<long long>(results.size());
        o["answer_tokens"] = tokens;
        o["total_s"] = round1(wall);
        o["total_write"] = wall > 0 ? json(round1(static_cast<double>(tokens) / wall)) : json();
        o["mean_first_token_s"] = !results.empty() && any_wait ? json(round1(waits / static_cast<double>(results.size()))) : json();
        return o;
    }

    json largest(const endpoint& at, const endpoint& count_at)
    {
        long long limit = 262144;
        try
        {
            json body = json::object();
            body["text"] = "x";
            const long long c = call(count_at, "/gulliver/v1/count", body)["context"].as_int(0);
            if (c > 0) limit = c;
        }
        catch (const std::exception&)
        {
        }
        json best;
        for (const long long n : {65536LL, 131072LL})
        {
            if (n > limit) break;
            try
            {
                const chat_result r = chat(at, "Voici un document :\n\n" + document(count_at, static_cast<long long>(n * 0.95)) +
                                                   "\n\nEn une phrase, de quoi parle-t-il ?", 16, 1200);
                best = json::object();
                best["context"] = n;
                best["prompt_tokens"] = r.usage["prompt_tokens"];
                best["read"] = read_speed(r.timings);
                say("  contexte de " + std::to_string(n) + " : tient");
            }
            catch (const std::exception& e)
            {
                std::string detail = e.what();
                if (const auto* ce = dynamic_cast<const call_error*>(&e); ce && !ce->body.empty())
                    detail += " " + ce->body.substr(0, 300);
                say("  contexte de " + std::to_string(n) + " : échec (" + detail.substr(0, 200) + ")");
                if (!best.is_object()) best = json::object();
                best["refused"] = n;
                best["reason"] = detail.substr(0, 300);
                break;
            }
        }
        return best;
    }

    bool on_path(const std::string& program)
    {
        const char* path = std::getenv("PATH");
        if (!path) return false;
        std::string p = path;
        size_t from = 0;
        while (from <= p.size())
        {
            const size_t end = std::min(p.find(':', from), p.size());
            const std::string candidate = gulliver::join_path(p.substr(from, end - from), program);
            if (access(candidate.c_str(), X_OK) == 0) return true;
            from = end + 1;
        }
        return false;
    }

    std::string short_text(const std::exception& e, size_t n) { return std::string(e.what()).substr(0, n); }
}

int main(int argc, char** argv)
{
    std::string model, name, json_path;
    bool with_llama = false, quick = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--name" && i + 1 < argc) name = argv[++i];
        else if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else if (a == "--llama") with_llama = true;
        else if (a == "--quick") quick = true;
        else if (a.rfind("--", 0) != 0 && model.empty()) model = a;
        else
        {
            std::fprintf(stderr, "usage: bench_service MODEL.gguf [--name NAME] [--json OUT] [--llama] [--quick]\n");
            return 2;
        }
    }
    if (model.empty())
    {
        std::fprintf(stderr, "usage: bench_service MODEL.gguf [--name NAME] [--json OUT] [--llama] [--quick]\n");
        return 2;
    }
    const std::string bin = gulliver::parent_path(gulliver::executable_path());      // build/macos-gulliver/bin
    root = gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(bin)));
    work = gulliver::join_path(root, "build/bench-service");
    service_program = gulliver::join_path(bin, "gulliver-service");
    text_path = gulliver::join_path(root, "build/bench-moteurs/texte.txt");

    json out = json::object();
    out["model"] = name.empty() ? base_name(model) : name;
    json g_out = json::object(), l_out = json::object();
    gulliver_service g;
    g.model = model;
    g.home = gulliver::join_path(work, "home");
    try
    {
        g.start(true);
        say("Gulliver : installation du modèle");
        g.load();
        say("Gulliver : conversation de 32k, avec brouillons");
        g_out["long_drafts"] = long_conversation(g.at, g.at);
        say("Gulliver : 4 requêtes à la fois");
        g_out["four"] = together(g.at, g.at, 4, 0, 256);
        say("Gulliver : 3 documents de 4 000 tokens à la fois");
        g_out["three_docs"] = together(g.at, g.at, 3, 4000, 256);
        if (!quick)
        {
            say("Gulliver : le plus grand contexte");
            g_out["largest"] = largest(g.at, g.at);
        }
        g.stop();
        // Le chargement (test 5) : du lancement du service, le modèle installé et actif, à une première réponse.
        g.drafts = false;
        const double t = now_s();
        g.start(false);
        const std::string mid = call(g.at, "/gulliver/v1/models")["active"]["general"].str();
        if (mid.empty()) throw std::runtime_error("no active model after the restart of the test service");
        for (int i = 0; i < 1200 && !g.loaded(mid); ++i) pause_for(0.5);
        chat(g.at, "Bonjour.", 4, 600);
        g_out["load_s"] = round1(now_s() - t);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", g_out["load_s"].as_double());
        say(std::string("Gulliver : chargé en ") + buf + " s ; conversation de 32k, sans brouillons");
        g_out["long_plain"] = long_conversation(g.at, g.at);
    }
    catch (const std::exception& e)
    {
        g_out["error"] = short_text(e, 300);
        say("Gulliver : " + short_text(e, 200));
    }
    if (with_llama && on_path("llama-server"))
    {
        llama_server l;
        l.model = model;
        try
        {
            // Une seule place de 40 960 tokens pour la conversation longue (llama-server partage son contexte entre
            // ses places), puis 4 places de 8 192 pour les tests à plusieurs.
            say("llama.cpp : chargement");
            l_out["load_s"] = round1(l.start_and_load(40960, 1));
            say("llama.cpp : conversation de 32k");
            l_out["long_plain"] = long_conversation(l.at, g.at);
            l.stop();
            l.start_and_load(32768, 4);
            say("llama.cpp : 4 requêtes à la fois");
            l_out["four"] = together(l.at, g.at, 4, 0, 256);
            say("llama.cpp : 3 documents de 4 000 tokens à la fois");
            l_out["three_docs"] = together(l.at, g.at, 3, 4000, 256);
        }
        catch (const std::exception& e)
        {
            l_out["error"] = short_text(e, 300);
            say("llama.cpp : " + short_text(e, 200));
        }
        l.stop();
    }
    g.stop();
    out["gulliver"] = g_out;
    out["llama"] = l_out;
    const std::string text = out.dump_formatted(1);
    if (!json_path.empty() && !gulliver::write_file_atomic(json_path, text))
        std::fprintf(stderr, "bench_service: cannot write %s\n", json_path.c_str());
    std::printf("%s\n", text.c_str());
    return 0;
}
