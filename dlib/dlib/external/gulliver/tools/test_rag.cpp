// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    La justesse de la recherche dans les documents (RAG) de Gulliver (Gulliver 7.22.0, à la place de tools/test_rag.py ;
    fork pour macOS, 2026-11-22).

        build/macos-gulliver/bin/test_rag [--json sortie.json] [--generate] [--models DOSSIER] [--tirage]

    Le corpus : les épisodes du blog du portage (blog/NN-*.md).  Les questions (tools/rag_questions.json) ont été
    écrites une fois par Ministral 3 8B à partir de passages tirés au hasard (--generate), puis figées : le test est le
    même chaque semaine.  Chaque question garde son épisode et un extrait du passage qui y répond.

    Par le vrai service, mais un service d'essai isolé (port 18495, ses propres données dans build/rag-test, hors
    ligne, effacées à la fin), jamais celui de l'utilisateur : il reçoit par sa bibliothèque un clone (cp -c :
    instantané, sans place prise) du modèle de recherche installé (qwen3-embedding-0.6b.gguf, pris dans --models, par
    défaut build/gulliver-home/models), et de Ministral 3 8B pour --generate.  Dans le profil de son administrateur
    (mot de passe choisi par le lien du mail de son premier compte, gardé par un faux facteur, tools/faux_smtp.h), les
    épisodes sont indexés comme la page le fait (POST /gulliver/v1/profile/documents/<doc>/index : découpage et
    vecteurs par le service), puis chaque question est cherchée (POST /gulliver/v1/profile/search, son vecteur par
    /v1/embeddings avec la consigne de Qwen3-Embedding, comme web/js/rag.js) avec chaque moteur (hybrid, dense,
    lexical), avec et sans le tri fin, et 6, 12 ou 20 passages (le budget de la page : 1 500 caractères par passage).
    Une question est trouvée quand un des extraits rendus contient son extrait.  Mesures : le rappel (la part des
    questions trouvées) et le rang moyen réciproque (1 si le bon passage est premier, 1/2 s'il est deuxième…).  Le
    service d'essai est appelé en HTTPS, son certificat vérifié avec sa propre autorité.
*/

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "client_service.h"
#include "faux_smtp.h"
#include "../common/json.h"
#include "../common/platform.h"

extern char** environ;

using gulliver::json;

namespace
{
    const int port = 18495;
    const char* const embedder_file = "qwen3-embedding-0.6b.gguf";
    const char* const writer_file = "ministral-3-8b.gguf";
    std::string root, work, home, service_program, questions_path;
    pid_t service_pid = -1;
    std::string token;

    double now_s()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string local_time(const char* format)
    {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[64];
        std::strftime(buf, sizeof(buf), format, &tm);
        return buf;
    }

    double round_to(double x, double scale) { return std::nearbyint(x * scale) / scale; }

    // ---- le texte en points de code, pour mesurer et couper comme les chaînes de Python ----

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

    // Les blancs de Python (\s des expressions et str.strip()).
    bool space(char32_t c)
    {
        return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
               (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
    }

    std::u32string strip(const std::u32string& s, const std::u32string& chars = U"")
    {
        auto drop = [&](char32_t c) { return chars.empty() ? space(c) : chars.find(c) != std::u32string::npos; };
        size_t a = 0, b = s.size();
        while (a < b && drop(s[a])) ++a;
        while (b > a && drop(s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    // ---- le hasard de Python : random.Random(7).shuffle, pour que --generate tire les mêmes passages ----

    class python_random
    {
    public:
        explicit python_random(uint32_t seed)
        {
            // random.seed(int) : init_by_array avec les mots de 32 bits de l'entier (ici un seul).
            init_genrand(19650218u);
            const uint32_t key[1] = {seed};
            size_t i = 1, j = 0;
            for (size_t k = 624; k; --k)
            {
                mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525u)) + key[j] + static_cast<uint32_t>(j);
                ++i;
                ++j;
                if (i >= 624)
                {
                    mt_[0] = mt_[623];
                    i = 1;
                }
                if (j >= 1) j = 0;
            }
            for (size_t k = 623; k; --k)
            {
                mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941u)) - static_cast<uint32_t>(i);
                ++i;
                if (i >= 624)
                {
                    mt_[0] = mt_[623];
                    i = 1;
                }
            }
            mt_[0] = 0x80000000u;
        }

        template <typename T>
        void shuffle(std::vector<T>& x)
        {
            for (size_t i = x.size(); i-- > 1;)
                std::swap(x[i], x[below(static_cast<uint32_t>(i + 1))]);
        }

    private:
        uint32_t mt_[624];
        size_t index_ = 624;

        void init_genrand(uint32_t s)
        {
            mt_[0] = s;
            for (size_t i = 1; i < 624; ++i) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + static_cast<uint32_t>(i);
            index_ = 624;
        }

        uint32_t next()
        {
            if (index_ >= 624)
            {
                for (size_t k = 0; k < 624; ++k)
                {
                    const uint32_t y = (mt_[k] & 0x80000000u) | (mt_[(k + 1) % 624] & 0x7fffffffu);
                    mt_[k] = mt_[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
                }
                index_ = 0;
            }
            uint32_t y = mt_[index_++];
            y ^= y >> 11;
            y ^= (y << 7) & 0x9d2c5680u;
            y ^= (y << 15) & 0xefc60000u;
            y ^= y >> 18;
            return y;
        }

        // _randbelow de Python : getrandbits(n.bit_length()) jusqu'à tomber sous n.
        uint32_t below(uint32_t n)
        {
            int k = 0;
            while ((n >> k) != 0) ++k;
            for (;;)
            {
                const uint32_t r = next() >> (32 - k);
                if (r < n) return r;
            }
        }
    };

    // ---- le service d'essai ----

    gulliver::essai::service_target target()
    {
        gulliver::essai::service_target t;
        t.home = home;
        t.port = port;
        return t;
    }

    json call(const std::string& method, const std::string& path, const json* body = nullptr, int timeout = 300,
        std::string* raw = nullptr)
    {
        gulliver::essai::service_target t = target();
        t.timeout_seconds = timeout;
        const gulliver::essai::service_reply r = gulliver::essai::service_call(t, method, path, body, token);
        if (r.status == 0) throw std::runtime_error(method + " " + path + ": no answer");
        if (r.status < 200 || r.status >= 300)
            throw std::runtime_error(method + " " + path + ": HTTP Error " + std::to_string(r.status) + " " + r.text.substr(0, 300));
        if (raw) *raw = r.text;
        return r.body;
    }

    json call(const std::string& method, const std::string& path, const json& body, int timeout = 300, std::string* raw = nullptr)
    {
        return call(method, path, &body, timeout, raw);
    }

    void stop_service()
    {
        if (service_pid > 0)
        {
            kill(service_pid, SIGTERM);
            bool gone = false;
            for (int i = 0; i < 300 && !gone; ++i)
            {
                gone = waitpid(service_pid, nullptr, WNOHANG) == service_pid;
                if (!gone) gulliver::essai::pause_for(0.1);
            }
            if (!gone)
            {
                kill(service_pid, SIGKILL);
                waitpid(service_pid, nullptr, 0);
            }
            service_pid = -1;
        }
        if (std::system(("rm -rf '" + work + "'").c_str()) != 0) std::fprintf(stderr, "cannot empty %s\n", work.c_str());
    }

    std::string base_name(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    // Lance le service d'essai, installe les modèles de la bibliothèque, les valide et les active.
    std::map<std::string, std::string> start_service(const std::string& models_dir, const std::vector<std::string>& files)
    {
        if (std::system(("rm -rf '" + work + "'").c_str()) != 0) throw std::runtime_error("cannot empty " + work);
        const std::string library = gulliver::join_path(home, "library");
        gulliver::make_dirs(library);
        chmod(library.c_str(), 0700);
        chmod(home.c_str(), 0700);
        for (const std::string& f : files)
            if (std::system(("cp -c '" + gulliver::join_path(models_dir, f) + "' '" + library + "/'").c_str()) != 0)
                throw std::runtime_error("cannot clone " + f);
        json settings = json::object();
        settings["port"] = port;
        settings["offline"] = true;
        settings["default_model"] = false;
        settings["lan"] = false;
        settings["library_dir"] = library;
        if (!gulliver::write_file_atomic(gulliver::join_path(home, "settings.json"), settings.dump()))
            throw std::runtime_error("cannot write the settings");

        const std::string log = gulliver::join_path(work, "service.log");
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        std::vector<std::string> env;
        for (char** e = environ; *e; ++e)
            if (std::strncmp(*e, "GULLIVER_HOME=", 14) != 0) env.push_back(*e);
        env.push_back("GULLIVER_HOME=" + home);
        std::vector<char*> envp;
        for (std::string& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        std::string console = "--console";
        char* argv[] = {service_program.data(), console.data(), nullptr};
        const int spawned = posix_spawn(&service_pid, service_program.c_str(), &fa, nullptr, argv, envp.data());
        posix_spawn_file_actions_destroy(&fa);
        if (spawned != 0) throw std::runtime_error("the test service does not start");

        bool up = false;
        for (int i = 0; i < 120 && !up; ++i)
        {
            try
            {
                call("GET", "/gulliver/v1/status", nullptr, 3);
                up = true;
            }
            catch (const std::exception&)
            {
                gulliver::essai::pause_for(1);
            }
        }
        if (!up) throw std::runtime_error("the test service does not answer");

        std::map<std::string, std::string> ids;
        for (const std::string& f : files)
        {
            json body = json::object();
            body["file"] = f;
            const json r = call("POST", "/gulliver/v1/library/load", body);
            std::vector<std::string> wanted;
            for (const char* k : {"imported", "already"})
                for (const json& x : r[k].elements()) wanted.push_back(x.str());
            auto is_wanted = [&](const std::string& id) { return std::find(wanted.begin(), wanted.end(), id) != wanted.end(); };
            for (int i = 0; i < 120 && !ids.count(f); ++i)
            {
                // Les modèles de recherche sont rangés à part (embedding_models), sans le champ « installed ».
                const json listing = call("GET", "/gulliver/v1/models");
                for (const json& m : listing["models"].elements())
                    if (m["installed"].as_bool() && (is_wanted(m["id"].str()) || base_name(m["file"].str()) == f))
                    {
                        ids[f] = m["id"].str();
                        break;
                    }
                if (!ids.count(f))
                    for (const json& m : listing["embedding_models"].elements())
                        if (is_wanted(m["id"].str()))
                        {
                            ids[f] = m["id"].str();
                            break;
                        }
                if (!ids.count(f)) gulliver::essai::pause_for(1);
            }
            if (!ids.count(f)) throw std::runtime_error("the library did not load " + f);
            // Hors de tout catalogue signé : validé comme le ferait l'administrateur (Gulliver 7.17.0).
            call("POST", "/gulliver/v1/models/" + ids[f] + "/approve", json::object(), 900);
        }
        json role = json::object();
        if (ids.count(writer_file))
        {
            role["role"] = "general";
            call("POST", "/gulliver/v1/models/" + ids[writer_file] + "/activate", role);
        }
        if (call("GET", "/gulliver/v1/models")["active"]["embedding"].str() != ids[embedder_file])
        {
            role["role"] = "embedding";
            call("POST", "/gulliver/v1/models/" + ids[embedder_file] + "/activate", role);
        }
        return ids;
    }

    // ---- le corpus ----

    struct episode
    {
        std::string doc, name, text;
    };

    std::vector<episode> corpus()
    {
        const std::string dir = gulliver::join_path(root, "blog");
        std::vector<std::string> names;
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                const std::string n = e->d_name;
                if (n.size() > 6 && std::isdigit(static_cast<unsigned char>(n[0])) && std::isdigit(static_cast<unsigned char>(n[1])) &&
                    n[2] == '-' && n.compare(n.size() - 3, 3, ".md") == 0)
                    names.push_back(n);
            }
            closedir(d);
        }
        std::sort(names.begin(), names.end());
        std::vector<episode> out;
        for (const std::string& n : names)
        {
            episode e;
            e.name = n;
            // re.sub(r"[^a-z0-9-]", "-", name[:-3].lower()) : un tiret par caractère écarté.
            for (const char32_t c : decode(n.substr(0, n.size() - 3)))
            {
                const char32_t l = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
                e.doc += ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '-') ? static_cast<char>(l) : '-';
            }
            if (!gulliver::read_file(gulliver::join_path(dir, n), e.text, 16u << 20)) throw std::runtime_error("cannot read " + n);
            out.push_back(e);
        }
        return out;
    }

    // Les paragraphes de prose d'un épisode (ni titres, ni tableaux, ni code), de 400 à 1 200 caractères.
    std::vector<std::u32string> paragraphs(const std::string& text_utf8)
    {
        std::u32string text = decode(text_utf8);
        // re.sub(r"```.*?```", "", flags=re.S)
        for (size_t a; (a = text.find(U"```")) != std::u32string::npos;)
        {
            const size_t b = text.find(U"```", a + 3);
            if (b == std::u32string::npos) break;
            text.erase(a, b + 3 - a);
        }
        // re.split(r"\n\s*\n") : un saut de ligne, des blancs, et le dernier saut de ligne de ces blancs.
        std::vector<std::u32string> parts;
        size_t start = 0, i = 0;
        while (i < text.size())
        {
            if (text[i] == U'\n')
            {
                size_t run = i + 1, last = std::u32string::npos;
                while (run < text.size() && space(text[run]))
                {
                    if (text[run] == U'\n') last = run;
                    ++run;
                }
                if (last != std::u32string::npos)
                {
                    parts.push_back(text.substr(start, i - start));
                    start = i = last + 1;
                    continue;
                }
            }
            ++i;
        }
        parts.push_back(text.substr(start));
        std::vector<std::u32string> keep;
        for (const std::u32string& raw : parts)
        {
            const std::u32string p = strip(raw);
            if (p.size() >= 400 && p.size() <= 1200 && p[0] != U'#' && p[0] != U'|' && p[0] != U'>' && p[0] != U'-' && p[0] != U'*')
                keep.push_back(p);
        }
        return keep;
    }

    // Un extrait du milieu du paragraphe (60 caractères), qui sert à reconnaître le bon passage.
    std::string snippet(const std::u32string& p)
    {
        const size_t mid = p.size() / 2;
        const size_t a = mid >= 30 ? mid - 30 : 0;
        return encode(strip(p.substr(a, mid + 30 - a)));
    }

    struct item
    {
        std::string doc, name;
        std::u32string p;
    };

    // Les paragraphes de tous les épisodes, mélangés comme random.Random(seed).shuffle de Python.
    std::vector<item> drawn_pool(uint32_t seed)
    {
        std::vector<item> pool;
        for (const episode& e : corpus())
            for (const std::u32string& p : paragraphs(e.text)) pool.push_back({e.doc, e.name, p});
        python_random rng(seed);
        rng.shuffle(pool);
        return pool;
    }

    // Les questions : n paragraphes tirés au hasard, une question écrite par le modèle pour chacun.
    json generate(int n = 40, uint32_t seed = 7)
    {
        const std::vector<item> pool = drawn_pool(seed);
        json out = json::array();
        for (const item& it : pool)
        {
            if (static_cast<int>(out.size()) >= n) break;
            const std::string prompt = "Voici un passage d'un blog :\n\n" + encode(it.p) + "\n\nÉcris une seule question en "
                "français, précise, à laquelle ce passage répond, comme la poserait un lecteur qui ne l'a pas sous les "
                "yeux. N'emploie pas les mêmes tournures que le passage. Réponds par la question seule, sans guillemets.";
            json message = json::object();
            message["role"] = "user";
            message["content"] = prompt;
            json body = json::object();
            body["messages"] = json::array();
            body["messages"].elements().push_back(message);
            body["max_tokens"] = 80;
            body["temperature"] = 0;
            const json r = call("POST", "/v1/chat/completions", body, 600);
            // content.strip().splitlines()[-1].strip(" «»\"")
            const std::u32string content = strip(decode(r["choices"][0]["message"]["content"].str()));
            // Le texte est sans blancs de bord : sa dernière ligne est ce qui suit le dernier séparateur de lignes.
            const size_t last = content.find_last_of(U"\n\r\v\f\x1c\x1d\x1e\x85  ");
            std::u32string q = last == std::u32string::npos ? content : content.substr(last + 1);
            q = strip(q, U" «»\"");
            if (!q.empty() && q.back() == U'?' && q.size() >= 15 && q.size() <= 300)
            {
                json e = json::object();
                e["question"] = encode(q);
                e["doc"] = it.doc;
                e["name"] = it.name;
                e["snippet"] = snippet(it.p);
                out.elements().push_back(e);
                std::printf("%2zu. %s  [%s]\n", out.size(), encode(q).c_str(), it.name.c_str());
                std::fflush(stdout);
            }
        }
        json file = json::object();
        file["generated"] = local_time("%Y-%m-%d %H:%M");
        file["seed"] = static_cast<long long>(seed);
        file["questions"] = out;
        if (!gulliver::write_file_atomic(questions_path, file.dump_formatted(1)))
            throw std::runtime_error("cannot write " + questions_path);
        return out;
    }

    json evaluate(const json& questions)
    {
        const std::string embedder = call("GET", "/gulliver/v1/models")["active"]["embedding"].str();
        // Le service d'essai est neuf : son premier profil, l'administrateur, choisit un mot de passe tiré au hasard par
        // le lien du mail de son premier compte, gardé par le faux facteur ; sa session sert pour tout le test.
        {
            gulliver::essai::fake_smtp smtp;
            json s = json::object();
            s["mail_from"] = "gulliver@essai.invalid";
            s["mail_port"] = smtp.port;
            call("PUT", "/gulliver/v1/settings", s);
            json setup = json::object();
            setup["name"] = "essai-rag";
            setup["email"] = "rag@essai.invalid";
            call("POST", "/gulliver/v1/setup", setup);
            json reset = json::object();
            reset["token"] = smtp.reset_for("rag@essai.invalid");
            reset["password"] = "Rag-1-" + gulliver::random_hex(16);
            token = call("POST", "/gulliver/v1/password/reset", reset)["token"].str();
        }
        const std::vector<episode> docs = corpus();
        const double t = now_s();
        for (const episode& e : docs)
        {
            json body = json::object();
            body["owner"] = "essai";
            body["name"] = e.name;
            body["text"] = e.text;
            std::string raw;
            call("POST", "/gulliver/v1/profile/documents/" + e.doc + "/index", body, 900, &raw);
            // La dernière ligne « data: {…} » du flux dit le résultat de l'indexation.
            json info = json::object();
            size_t from = 0;
            while (from < raw.size())
            {
                size_t end = raw.find('\n', from);
                if (end == std::string::npos) end = raw.size();
                const std::string line = raw.substr(from, end - from);
                if (line.rfind("data: {", 0) == 0) info = json::parse_or_null(line.substr(6));
                from = end + 1;
            }
            if (!info.is_object() || info["error"].as_bool() || !info["error"].str().empty() || info["vectors"].as_int(0) == 0)
                throw std::runtime_error("indexing " + e.name + ": " + raw.substr(raw.size() > 200 ? raw.size() - 200 : 0));
        }
        const double index_s = now_s() - t;
        std::printf("%zu épisodes indexés en %.0f s\n", docs.size(), std::nearbyint(index_s));
        std::fflush(stdout);

        std::map<std::string, json> vectors;
        std::string lower_embedder = embedder;
        for (char& c : lower_embedder) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const json& q : questions.elements())
        {
            std::string text = q["question"].str();
            if (lower_embedder.find("qwen3-embedding") != std::string::npos)
                text = "Instruct: Given a question, retrieve the passages of the documents that answer it\nQuery:" + text;
            json body = json::object();
            body["model"] = embedder;
            body["input"] = json::array();
            body["input"].elements().push_back(json(text));
            body["encoding_format"] = "base64";
            const json r = call("POST", "/v1/embeddings", body);
            vectors[q["question"].str()] = r["data"][0]["embedding"];
        }
        json results = json::object();
        for (const char* engine : {"hybrid", "dense", "lexical"})
            for (const bool rerank : {false, true})
            {
                std::vector<int> ranks;
                for (const json& q : questions.elements())
                {
                    json body = json::object();
                    body["question"] = q["question"];
                    body["owners"] = json::array();
                    body["owners"].elements().push_back(json("essai"));
                    body["engine"] = engine;
                    body["rerank"] = rerank;
                    body["model"] = embedder;
                    body["question_vector"] = std::string(engine) != "lexical" ? vectors[q["question"].str()] : json("");
                    body["limit"] = 20;
                    body["budget"] = 1500 * 20;
                    const json r = call("POST", "/gulliver/v1/profile/search", body);
                    int rank = 0;
                    int i = 0;
                    for (const json& x : r["excerpts"].elements())
                    {
                        ++i;
                        if (x["text"].str().find(q["snippet"].str()) != std::string::npos)
                        {
                            rank = i;
                            break;
                        }
                    }
                    ranks.push_back(rank);
                }
                const std::string key = std::string(engine) + (rerank ? " + tri fin" : "");
                json res = json::object();
                const double n = static_cast<double>(ranks.size());
                for (const int k : {6, 12, 20})
                {
                    const auto found = std::count_if(ranks.begin(), ranks.end(), [k](int x) { return x && x <= k; });
                    res["rappel_" + std::to_string(k)] = round_to(static_cast<double>(found) / n, 1000);
                }
                double rr = 0;
                for (const int x : ranks)
                    if (x) rr += 1.0 / x;
                res["rang_reciproque"] = round_to(rr / n, 1000);
                results[key] = res;
                std::string padded = key;
                while (padded.size() < 20) padded += ' ';
                std::printf("%s rappel@6 %.2f  @12 %.2f  @20 %.2f  rang réciproque %.2f\n", padded.c_str(),
                    res["rappel_6"].as_double(), res["rappel_12"].as_double(), res["rappel_20"].as_double(),
                    res["rang_reciproque"].as_double());
                std::fflush(stdout);
            }
        json out = json::object();
        out["date"] = local_time("%Y-%m-%d %H:%M");
        out["questions"] = static_cast<long long>(questions.size());
        out["documents"] = static_cast<long long>(docs.size());
        out["embedder"] = embedder;
        out["index_s"] = round_to(index_s, 10);
        out["results"] = results;
        return out;
    }
}

int main(int argc, char** argv)
{
    const std::string bin = gulliver::parent_path(gulliver::executable_path());    // build/macos-gulliver/bin
    root = gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(bin)));
    std::string models_dir = gulliver::join_path(root, "build/gulliver-home/models"), json_path;
    bool generating = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--models" && i + 1 < argc) models_dir = argv[++i];
        else if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else if (a == "--generate") generating = true;
        else if (a == "--tirage")
        {
            // Les passages que tirerait --generate, dans l'ordre, sans service (pour vérifier le tirage).
            for (const auto& it : drawn_pool(7)) std::printf("%s|%s\n", it.name.c_str(), snippet(it.p).c_str());
            return 0;
        }
        else
        {
            std::fprintf(stderr, "usage: test_rag [--json OUT] [--generate] [--models DIR] [--tirage]\n");
            return 2;
        }
    }
    work = gulliver::join_path(root, "build/rag-test");
    home = gulliver::join_path(work, "home");
    service_program = gulliver::join_path(bin, "gulliver-service");
    questions_path = gulliver::join_path(root, "dlib/dlib/external/gulliver/tools/rag_questions.json");
    generating = generating || !gulliver::file_exists(questions_path);
    json out;
    try
    {
        start_service(models_dir, generating ? std::vector<std::string>{embedder_file, writer_file}
                                             : std::vector<std::string>{embedder_file});
        json questions;
        if (generating) questions = generate();
        else
        {
            std::string text;
            json file;
            if (!gulliver::read_file(questions_path, text, 16u << 20) || !json::parse(text, file))
                throw std::runtime_error("cannot read " + questions_path);
            questions = file["questions"];
        }
        out = evaluate(questions);
    }
    catch (const std::exception& e)
    {
        stop_service();
        std::fprintf(stderr, "test_rag: %s\n", e.what());
        return 1;
    }
    stop_service();
    if (!json_path.empty() && !gulliver::write_file_atomic(json_path, out.dump_formatted(1)))
    {
        std::fprintf(stderr, "test_rag: cannot write %s\n", json_path.c_str());
        return 1;
    }
    return 0;
}
