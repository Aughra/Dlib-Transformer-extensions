// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Les mises à jour des modèles de la semaine (Gulliver 7.23.0, à la place de tools/modeles_semaine.py ; fork pour
    macOS, 2026-11-22) : tenir à jour les modèles de Gulliver avant la mesure de la semaine (lundi 1 h, puis la mesure
    à 8 h : macos/modeles-semaine, macos/bench-hebdo).

        build/macos-gulliver/bin/modeles_semaine [GULLIVER_HOME] [--essai]

    Par le service qui tourne sur ce Mac (https://127.0.0.1:<port de ses réglages>, certificat vérifié avec l'autorité
    du service) :
      1. le catalogue est rafraîchi (POST /gulliver/v1/models/refresh) ;
      2. un modèle du catalogue installé dont le catalogue donne une autre empreinte que celle du fichier est
         retéléchargé (sa nouvelle version) ;
      3. un modèle ajouté depuis Hugging Face dont le fichier a changé sur Hugging Face (empreinte de l'en-tête
         X-Linked-Etag, sans télécharger le fichier) est réajouté, ce qui télécharge sa nouvelle version ;
      4. les modèles du catalogue qui tiennent dans ce Mac et qu'il ne possède pas encore sont téléchargés : une
         architecture que le moteur lit, un fichier (avec son projecteur) d'au plus les trois quarts de la mémoire, un
         modèle qui répond aux questions (pas un modèle de recherche dans les documents).
    Puis il attend la fin des téléchargements (six heures au plus ; le service vérifie l'empreinte de chaque fichier)
    et écrit les modèles installés dans build/bench/hebdo/nouveaux.json : la mesure de la semaine
    (tools/bench_modeles.sh) les mesure en premier et les signale.  Avec --essai, il ne fait que dire ce qu'il ferait.
      5. Hugging Face (décisions de l'humain du 2026-11-23 et du 2026-11-24 : « prends-en 5 parmi tous les tris ») :
         pour chacun des cinq ordres de Hugging Face (les plus téléchargés, tendance, aimés, récents, mis à jour), les
         hub_top premiers modèles GGUF d'éditeurs connus (hub_editors), sans les versions « abliterated » ; puis les
         hub_top premiers « abliterated » (sans réflexe de refus, pour la cybersécurité, usage autorisé seulement) des
         recherches de l'onglet Modèles (hub_cyber), d'éditeurs connus ou de ceux qui les publient
         (hub_cyber_editors).  Un modèle compte s'il tourne sur ce moteur (architecture et format de poids, d'après le
         service), en Q4_K_M de préférence, avec son projecteur d'images, et tient dans ce Mac.  Un modèle déjà
         installé compte sans être retéléchargé ; un modèle déjà retiré (build/bench/hebdo/retires.json, programme
         retrait_modeles) est sauté sans compter, ce qui laisse sa place au suivant.  Rien n'est ajouté quand le disque
         garderait moins de disk_floor octets libres.
    Ce qu'il a trouvé part ensuite dans un mail à l'administrateur (tools/mail_admin.h ; décision de l'humain du
    2026-11-23 : il décide, d'après la mesure du matin, des modèles qu'on garde).
*/

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <sys/statvfs.h>
#include <sys/sysctl.h>

#include "client_service.h"
#include "mail_admin.h"
#include "../common/json.h"
#include "../common/platform.h"

using gulliver::json;

namespace
{
    // Les architectures que le moteur lit (engine/model_config.cpp, read_model_config).
    const std::set<std::string> archs = {"llama", "mistral", "mistral3", "granite", "qwen2", "qwen3", "qwen3vl", "mixtral",
        "granitemoe", "qwen3moe", "qwen3vlmoe", "qwen35moe", "phi3", "qwen35", "gemma4", "gemma3", "lfm2", "lfm2moe"};
    const int hub_top = 5;
    const std::vector<std::pair<std::string, std::string>> hub_sorts = {{"downloads", "les plus téléchargés"},
        {"trendingScore", "les plus tendance"}, {"likes", "les plus aimés"}, {"createdAt", "les plus récents"},
        {"lastModified", "les derniers mis à jour"}};
    const std::vector<std::string> hub_cyber = {"abliterated GGUF", "Qwen3 abliterated", "Llama 3 abliterated", "dolphin"};   // comme web/js/models.js
    const std::set<std::string> hub_cyber_editors = {"huihui-ai", "mlabonne", "mradermacher", "cognitivecomputations", "dphn",
        "QuantFactory"};
    const unsigned long long disk_floor = 200ULL * 1000 * 1000 * 1000;
    const std::set<std::string> hub_editors = {"Qwen", "mistralai", "google", "ibm-granite", "microsoft", "meta-llama",
        "LiquidAI", "ggml-org", "unsloth", "bartowski", "lmstudio-community", "HuggingFaceTB", "allenai", "nvidia",
        "deepseek-ai", "openai", "utter-project", "CohereLabs", "tiiuae"};
    const std::vector<std::string> hub_refused = {"abliterated", "uncensored", "heretic"};   // hors de la liste « cybersécurité »
    // Les fichiers d'aide d'un dépôt (petits modèles de brouillon), qui ne sont pas le modèle lui-même.
    const std::vector<std::string> hub_helpers = {"dflash", "draft", "eagle", "imatrix"};
    const std::vector<std::string> quants = {"Q4_K_M", "Q4_K_S", "Q6_K", "Q8_0"};   // dans l'ordre de préférence
    const std::set<std::string> running = {"queued", "downloading", "verifying"};

    std::vector<std::string> report;   // les lignes du mail de la nuit

    std::string local_time(const char* format)
    {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[64];
        std::strftime(buf, sizeof(buf), format, &tm);
        return buf;
    }

    void say(const std::string& text, bool in_report = false)
    {
        std::printf("%s %s\n", local_time("%Y-%m-%d %H:%M").c_str(), text.c_str());
        std::fflush(stdout);
        if (in_report) report.push_back(text);
    }

    std::string format(const char* f, double x)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), f, x);
        return buf;
    }

    std::string lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

    // urllib.parse.quote : tout sauf les lettres, les chiffres, « _.-~ » et « / » en %XX.
    std::string quote(const std::string& s)
    {
        std::string out;
        char buf[4];
        for (const unsigned char c : s)
        {
            if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~' || c == '/') out += static_cast<char>(c);
            else
            {
                std::snprintf(buf, sizeof(buf), "%%%02X", c);
                out += buf;
            }
        }
        return out;
    }

    // ---- le service qui tourne sur ce Mac ----

    gulliver::essai::service_target service;

    // Comme l'outil Python : un POST sans corps envoie « {} » ; une réponse d'erreur rend son JSON, ou
    // {"ok": false, "code": "<statut>"} ; un service qui ne répond pas arrête le programme.
    json call(const std::string& method, const std::string& path, const json* body = nullptr)
    {
        const json empty = json::object();
        const gulliver::essai::service_reply r =
            gulliver::essai::service_call(service, method, path, body ? body : (method == "POST" ? &empty : nullptr));
        if (r.status == 0) throw std::runtime_error(method + " " + path + ": the service does not answer");
        if (r.status >= 200 && r.status < 300) return r.body;
        json j;
        if (json::parse(r.text, j) && j.is_object()) return j;
        json o = json::object();
        o["ok"] = false;
        o["code"] = std::to_string(r.status);
        return o;
    }

    json call(const std::string& method, const std::string& path, const json& body) { return call(method, path, &body); }

    // Le code d'un refus : r.get("code") or r.get("error").
    std::string refusal(const json& r)
    {
        if (!r["code"].str().empty()) return r["code"].str();
        return r["error"].is_string() ? r["error"].str() : r["error"].is_null() ? "None" : r["error"].dump();
    }

    // Le texte s dont on retire, aux deux bouts, les caractères de chars (str.strip(chars) de Python).
    std::string strip_chars(const std::string& s, const std::string& chars)
    {
        size_t a = 0, b = s.size();
        while (a < b && chars.find(s[a]) != std::string::npos) ++a;
        while (b > a && chars.find(s[b - 1]) != std::string::npos) --b;
        return s.substr(a, b - a);
    }

    // L'empreinte SHA-256 actuelle d'un fichier de Hugging Face (en-tête X-Linked-Etag), sans le télécharger ni suivre
    // la redirection vers le stockage.
    std::string hub_sha(const std::string& url)
    {
        const std::string head = "https://";
        if (url.compare(0, head.size(), head) != 0) return "";
        const size_t slash = url.find('/', head.size());
        gulliver::tls_route route;
        route.host = url.substr(head.size(), slash == std::string::npos ? std::string::npos : slash - head.size());
        route.port = 443;
        std::string error;
        auto cl = gulliver::https_client(route, error);
        if (!cl) return "";
        cl->set_follow_location(false);
        cl->set_read_timeout(30, 0);
        cl->set_connection_timeout(30, 0);
        const httplib::Headers h = {{"User-Agent", "gulliver-models-update"}};
        const auto r = cl->Head(slash == std::string::npos ? "/" : url.substr(slash), h);
        if (!r) return "";
        std::string etag = r->has_header("X-Linked-Etag") ? r->get_header_value("X-Linked-Etag")
                         : r->has_header("ETag") ? r->get_header_value("ETag") : std::string();
        etag = strip_chars(strip_chars(strip_chars(etag, "\""), "W/"), "\"");
        return etag.size() == 64 ? etag : "";
    }

    // Le nom d'un modèle réduit à ses lettres et chiffres, sans « gguf » ni format de poids, pour reconnaître un doublon.
    std::string core(const std::string& name)
    {
        std::string n = lower(name);
        const size_t slash = n.rfind('/');
        if (slash != std::string::npos) n = n.substr(slash + 1);
        // re.sub(r"(-|_|\.)(gguf|q\d.*|iq\d.*|bf16|f16|ud)$", "", n) : coupé au premier séparateur qui ouvre une fin
        // reconnue.
        for (size_t i = 0; i < n.size(); ++i)
        {
            if (n[i] != '-' && n[i] != '_' && n[i] != '.') continue;
            const std::string rest = n.substr(i + 1);
            const bool digit1 = rest.size() > 1 && std::isdigit(static_cast<unsigned char>(rest[1]));
            const bool digit2 = rest.size() > 2 && std::isdigit(static_cast<unsigned char>(rest[2]));
            if (rest == "gguf" || (rest[0] == 'q' && digit1) || (rest.compare(0, 2, "iq") == 0 && digit2) || rest == "bf16" ||
                rest == "f16" || rest == "ud")
            {
                n.erase(i);
                break;
            }
        }
        for (const std::string cut : {"-gguf", "-mtp"})   // la variante à plusieurs tokens par pas est le même modèle
            for (size_t at; (at = n.find(cut)) != std::string::npos;) n.erase(at, cut.size());
        std::string out;
        for (const char c : n)
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        return out;
    }

    struct pick_t
    {
        std::string file, projector;
        unsigned long long size = 0;
    };

    // Le fichier à prendre dans un dépôt, avec son projecteur et leur taille ; false s'il n'y en a pas qui tienne.
    bool hub_pick(const std::string& repo, unsigned long long budget, pick_t& out)
    {
        const json files = call("GET", "/gulliver/v1/hub/files?repo=" + quote(repo));
        const json* pick = nullptr;
        for (const std::string& quant : quants)
        {
            for (const json& f : files["files"].elements())
            {
                if (!f["compatible"].as_bool() || f["quantization"].str() != quant) continue;
                bool helper = false;
                for (const std::string& w : hub_helpers) helper = helper || contains(lower(f["file"].str()), w);
                if (!helper)
                {
                    pick = &f;
                    break;
                }
            }
            if (pick) break;
        }
        if (!pick) return false;
        const json& projs = files["projectors"];
        const json* proj = nullptr;
        for (const json& x : projs.elements())
            if (x["quantization"].str() == "F16")
            {
                proj = &x;
                break;
            }
        if (!proj && projs.size() > 0) proj = &projs.elements()[0];
        const unsigned long long size = static_cast<unsigned long long>((*pick)["size"].as_int(0)) +
            (proj ? static_cast<unsigned long long>((*proj)["size"].as_int(0)) : 0ULL);
        if (size > budget) return false;
        out = {(*pick)["file"].str(), proj ? (*proj)["file"].str() : std::string(), size};
        return true;
    }

    std::string sort_key(const std::string& sort)
    {
        if (sort == "downloads") return "downloads";
        if (sort == "trendingScore") return "trending";
        if (sort == "likes") return "likes";
        if (sort == "createdAt") return "created";
        return "updated";
    }

    // Les modèles compatibles des recherches queries, dans l'ordre sort, des éditeurs editors.
    std::vector<json> hub_list(const std::vector<std::string>& queries, const std::string& sort,
        const std::set<std::string>& editors, bool cyber)
    {
        std::set<std::string> seen;
        std::vector<json> out;
        for (const std::string& q : queries)
        {
            // sort toujours donné : ces recherches restent hors des « recherches gardées » de la page.
            for (const json& m : call("GET", "/gulliver/v1/hub/search?q=" + quote(q) + "&sort=" + sort)["models"].elements())
            {
                const std::string repo = m["repo"].str();
                if (seen.count(repo)) continue;
                seen.insert(repo);
                const std::string l = lower(repo);
                bool refused = false;
                for (const std::string& w : hub_refused) refused = refused || contains(l, w);
                const std::string editor = repo.substr(0, repo.find('/'));
                if (m["compatible"].as_bool() && editors.count(editor) && (refused || contains(l, "dolphin")) == cyber)
                    out.push_back(m);
            }
        }
        // Les dates sont des textes ISO, qui se trient comme tels ; les autres clés sont des nombres.  Tri stable et
        // décroissant, comme sort(reverse=True) de Python.
        const std::string key = sort_key(sort);
        const bool text = key == "created" || key == "updated";
        std::stable_sort(out.begin(), out.end(), [&](const json& a, const json& b) {
            if (text) return a[key].str() > b[key].str();
            return a[key].as_double(0) > b[key].as_double(0);
        });
        return out;
    }

    struct addition
    {
        std::string repo, file, projector, why;
        unsigned long long size = 0;
    };

    // Les dépôts à ajouter, hub_top par liste.
    std::vector<addition> hub_popular(const std::set<std::string>& installed, const std::set<std::string>& retired,
        unsigned long long budget)
    {
        struct list_t
        {
            std::string sort, label;
            std::vector<std::string> queries;
            std::set<std::string> editors;
            bool cyber;
        };
        std::vector<list_t> lists;
        std::vector<std::string> editor_queries = {""};
        for (const std::string& e : hub_editors) editor_queries.push_back(e);   // std::set : trié comme sorted() de Python
        for (const auto& [sort, label] : hub_sorts) lists.push_back({sort, label, editor_queries, hub_editors, false});
        std::set<std::string> both = hub_editors;
        both.insert(hub_cyber_editors.begin(), hub_cyber_editors.end());
        lists.push_back({"downloads", "les plus téléchargés des « abliterated » (cybersécurité)", hub_cyber, both, true});
        std::vector<addition> out;
        std::set<std::string> taken;
        for (const list_t& l : lists)
        {
            int count = 0;
            for (const json& m : hub_list(l.queries, l.sort, l.editors, l.cyber))
            {
                if (count >= hub_top) break;
                const std::string repo = m["repo"].str();
                const std::string c = core(repo);
                if (retired.count(c)) continue;              // déjà essayé et retiré : sa place va au suivant
                if (installed.count(c) || taken.count(c))
                {
                    ++count;                                 // déjà là : il compte, sans téléchargement
                    continue;
                }
                pick_t pick;
                if (!hub_pick(repo, budget, pick)) continue;
                taken.insert(c);
                ++count;
                out.push_back({repo, pick.file, pick.projector, l.label, pick.size});
            }
        }
        return out;
    }

    std::string base_name(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    // Le mail de la nuit à l'administrateur, quand il y a quelque chose à dire.
    void mail_report(const std::string& home, bool trial)
    {
        if (trial || report.empty()) return;
        std::string body = "Bonjour,\n\nLa veille des modèles de Gulliver a trouvé ceci cette nuit :\n\n";
        for (const std::string& line : report) body += "  - " + line + "\n";
        body += "\nLes modèles téléchargés sont mesurés ce lundi à 8 h contre le modèle en place ; le résultat et le "
                "conseil arrivent dans un second mail, et dans l'onglet Modèles. À vous de décider ensuite lesquels garder.\n\nGulliver\n";
        const std::string subject = "Gulliver : " + std::to_string(report.size()) + " nouveauté" + (report.size() > 1 ? "s" : "") +
            " dans les modèles";
        std::string error;
        if (gulliver::essai::send_admin(home, subject, body, error)) say("mail envoyé à l'administrateur");
        else say("mail pas envoyé : " + error);
    }

    void write_news(const std::string& path, const json& ids, bool pretty)
    {
        json j = json::object();
        j["date"] = local_time("%Y-%m-%d %H:%M");
        j["ids"] = ids;
        if (!gulliver::write_file_atomic(path, pretty ? j.dump_formatted(1) : j.dump()))
            say("cannot write " + path);
    }
}

int main(int argc, char** argv)
{
    std::string home;
    bool trial = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--essai") trial = true;
        else if (a.rfind("--", 0) != 0 && home.empty()) home = a;
    }
    if (home.empty())
        if (const char* env = std::getenv("GULLIVER_HOME")) home = env;
    service.home = home;
    service.port = gulliver::essai::service_port(home);
    try
    {
        unsigned long long memory = 0;
        size_t len = sizeof(memory);
        sysctlbyname("hw.memsize", &memory, &len, nullptr, 0);
        const unsigned long long budget = memory * 3 / 4;
        if (call("GET", "/gulliver/v1/models")["offline"].as_bool())
        {
            say("le service est hors ligne : rien à faire");
            return 0;
        }
        // build/macos-gulliver/bin → build/bench/hebdo
        const std::string hebdo = gulliver::join_path(
            gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(gulliver::executable_path()))), "bench/hebdo");
        const std::string news = gulliver::join_path(hebdo, "nouveaux.json");
        if (!trial)
        {
            // Les nouveaux de la semaine repartent de zéro : une semaine sans nouveauté n'en annonce aucune à la mesure.
            gulliver::make_dirs(hebdo);
            write_news(news, json::array(), false);
            call("POST", "/gulliver/v1/models/refresh");
            for (int i = 0; i < 60; ++i)
            {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (call("GET", "/gulliver/v1/models")["catalog"]["state"].str() != "checking") break;
            }
        }
        // Les modèles déjà retirés (programme retrait_modeles) : jamais retéléchargés.
        std::set<std::string> retired_ids, retired_cores;
        {
            std::string text;
            json retired;
            if (gulliver::read_file(gulliver::join_path(hebdo, "retires.json"), text, 16u << 20) && json::parse(text, retired))
                for (const json& r : retired.elements())
                {
                    retired_ids.insert(r["id"].str());
                    const std::string c = core(!r["repo"].str().empty() ? r["repo"].str() : r["name"].str());
                    if (!c.empty()) retired_cores.insert(c);
                }
        }
        const json data = call("GET", "/gulliver/v1/models");
        const std::string state = data["catalog"]["state"].str().empty() ? "?" : data["catalog"]["state"].str();
        say("catalogue : " + state + ", " + std::to_string(data["models"].size()) + " modèles ; mémoire " +
            format("%.0f", static_cast<double>(memory) / (1ULL << 30)) + " Go, au plus " +
            format("%.0f", static_cast<double>(budget) / (1ULL << 30)) + " Go par modèle");
        std::vector<std::string> started;
        for (const json& m : data["models"].elements())
        {
            const std::string mid = m["id"].str();
            const unsigned long long size = static_cast<unsigned long long>(
                m["total_size"].as_int(0) ? m["total_size"].as_int(0) : m["size"].as_int(0));
            if (m["origin"].str() == "huggingface")
            {
                if (!m["installed"].as_bool() || m["files"].size() == 0) continue;
                const std::string url = m["files"][0]["url"].str();
                const std::string now = contains(url, "/resolve/") ? hub_sha(url) : "";
                if (!now.empty() && now != m["sha256"].str())
                {
                    std::string repo = m["source"].str();
                    const std::string hub = "https://huggingface.co/";
                    for (size_t at; (at = repo.find(hub)) != std::string::npos;) repo.erase(at, hub.size());
                    std::string name;
                    const size_t resolve = url.find("/resolve/");
                    if (resolve != std::string::npos)
                    {
                        const std::string after = url.substr(resolve + 9);
                        const size_t s = after.find('/');
                        name = s == std::string::npos ? std::string() : after.substr(s + 1);
                    }
                    say(mid + " : nouvelle version sur Hugging Face (" + now.substr(0, 12) + "…), réajouté", true);
                    if (!trial)
                    {
                        json body = json::object();
                        body["repo"] = repo;
                        body["file"] = name;
                        body["projector"] = m["projector"]["name"].str();
                        const json r = call("POST", "/gulliver/v1/hub/add", body);
                        if (r["ok"].as_bool()) started.push_back(mid);
                        else say(mid + " : refusé (" + refusal(r) + ")", true);
                    }
                }
                continue;
            }
            if (m["installed"].as_bool())
            {
                if (!m["local_sha256"].str().empty() && !m["sha256"].str().empty() && m["local_sha256"].str() != m["sha256"].str())
                {
                    say(mid + " : nouvelle version dans le catalogue, retéléchargé", true);
                    if (!trial && call("POST", "/gulliver/v1/models/" + mid + "/download")["ok"].as_bool()) started.push_back(mid);
                }
                continue;
            }
            if (retired_ids.count(mid))
            {
                say(mid + " : laissé (retiré un lundi précédent)");
                continue;
            }
            bool embedding = false;
            for (const json& r : m["roles"].elements()) embedding = embedding || r.str() == "embedding";
            const bool known = archs.count(m["arch"].str()) > 0;
            if (!known || embedding || !size || size > budget)
            {
                const std::string why = !known ? "architecture que le moteur ne lit pas"
                                      : embedding ? "modèle de recherche dans les documents" : "trop grand pour ce Mac";
                say(mid + " : laissé (" + why + ")");
                continue;
            }
            say(mid + " : téléchargé (" + format("%.1f", static_cast<double>(size) / 1e9) + " Go)", true);
            if (!trial)
            {
                const json r = call("POST", "/gulliver/v1/models/" + mid + "/download");
                if (r["ok"].as_bool()) started.push_back(mid);
                else say(mid + " : refusé (" + r["code"].str() + ")", true);
            }
        }
        // 5. Les plus populaires de Hugging Face.
        std::set<std::string> installed;
        for (const json& m : data["models"].elements())
        {
            if (m["origin"].str() == "huggingface") installed.insert(core(m["source"].str()));
            if (m["installed"].as_bool())
            {
                installed.insert(core(base_name(m["file"].str())));
                installed.insert(core(m["name"].str()));
            }
        }
        installed.erase("");
        struct statvfs fs{};
        unsigned long long free_bytes = statvfs(home.empty() ? "." : home.c_str(), &fs) == 0
            ? static_cast<unsigned long long>(fs.f_bavail) * fs.f_frsize : 0;
        for (const addition& a : hub_popular(installed, retired_cores, budget))
        {
            if (free_bytes < a.size || free_bytes - a.size < disk_floor)
            {
                say(a.repo + " : laissé, le disque garderait moins de " + std::to_string(disk_floor / 1000000000ULL) + " Go libres", true);
                continue;
            }
            free_bytes -= a.size;
            say(a.repo + " : parmi " + a.why + " de Hugging Face, " + a.file + (a.projector.empty() ? "" : " avec " + a.projector) +
                    ", " + format("%.1f", static_cast<double>(a.size) / 1e9) + " Go, ajouté", true);
            if (!trial)
            {
                json body = json::object();
                body["repo"] = a.repo;
                body["file"] = a.file;
                body["projector"] = a.projector;
                const json r = call("POST", "/gulliver/v1/hub/add", body);
                if (r["ok"].as_bool() && !r["id"].str().empty()) started.push_back(r["id"].str());
                else say(a.repo + " : refusé (" + refusal(r) + ")", true);
            }
        }
        if (trial || started.empty())
        {
            say(started.empty() && !trial ? "rien à télécharger" : "essai : rien n'a été lancé");
            mail_report(home, trial);
            return 0;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::hours(6);
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            bool busy = false;
            for (const json& d : call("GET", "/gulliver/v1/models")["downloads"].elements())
                busy = busy || running.count(d["state"].str());
            if (!busy) break;
        }
        const json final_list = call("GET", "/gulliver/v1/models");
        json done = json::array();
        for (const std::string& mid : started)
        {
            bool ok = false;
            for (const json& m : final_list["models"].elements())
                if (m["id"].str() == mid) ok = m["installed"].as_bool();
            say(mid + " : " + (ok ? "installé, empreinte vérifiée" : "pas installé, voir le journal du service"), true);
            if (ok) done.elements().push_back(json(mid));
        }
        // Les nouveaux de la semaine, pour la mesure de la semaine (bench_modeles.sh les mesure en premier et les signale).
        if (done.size() > 0)
        {
            write_news(news, done, true);
            say(std::to_string(done.size()) + " nouveaux pour la mesure de la semaine : " + news);
        }
        mail_report(home, trial);
        return 0;
    }
    catch (const std::exception& e)
    {
        say(std::string("arrêt : ") + e.what());
        return 1;
    }
}
