// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Vérifie les fichiers que le service écrit dans son dossier de modèles et lit dans ses imports,
    dans un dossier à lui, avec un serveur local pour les téléchargements.

        test_host <scratch folder> [port]

    Les téléchargements créent, ouvrent et renomment leurs fichiers dans le dossier de modèles
    atteint sans aucun lien : un fichier partiel ou un nom final qui est un lien, un fichier partiel
    qui a un second nom, et un dossier de modèles remplacé par un lien sont refusés, et les fichiers
    vers lesquels ils mènent restent tels quels.  Les imports lisent sans aucun lien et par un seul
    nom, un paquet hors ligne ne donne que les fichiers que liste son catalogue, et un fichier dont
    le contenu est celui d'un modèle installé, y compris un modèle fourni avec Albireo, n'est ni
    copié à nouveau ni enregistré par-dessus.  Les liens demandent un système qui laisse chaque
    compte en créer : ils sont sautés sous Windows.

    Un fichier ne prend jamais le nom d'un autre modèle installé ; une version plus récente d'un
    modèle installé prend ses noms par la fonction qui les soustrait au moteur, et un remplacement
    qu'elle refuse laisse le modèle tel quel, le suivant se complétant à partir des fichiers reçus.
    Les plages des téléchargements apportent les octets demandés et pas plus, une base de données
    abîmée échoue à l'ouverture comme malformée, et les fichiers écrits de façon atomique ne
    laissent aucun fichier temporaire.  Les téléchargements viennent des adresses que donne le catalogue,
    qui se trouvent sur le serveur local, catalogue officiel compris (official_catalog_for_tests()) :
    trois tentatives par segment, les empreintes vérifient chaque fichier (une partie corrompue est
    récupérée de nouveau), une page web, un blocage ou une réinitialisation de la connexion font échouer
    le téléchargement avec leur message sans rien écrire dans le fichier partiel, et une annulation met
    fin à un téléchargement en cours.  Le catalogue, sans signature, vient de son adresse ou d'un
    dossier, et un catalogue plus ancien ne remplace jamais un plus récent.  Les réglages d'une version
    antérieure (public_link, les fournisseurs et la clé de la recherche) sont lus sans erreur et laissent
    settings.json au départ.  La réserve restaure un modèle et son projecteur hors ligne sans requête,
    refuse les fichiers manquants ou altérés, préserve la progression réseau et laisse le repli en ligne
    fonctionner.  Le test travaille dans un dossier de données à lui (set_data_dir()), jamais dans celui
    d'un service installé.

    L'accès web ne lit que des adresses publiques : les adresses des deux côtés de chaque plage qui
    n'est pas publique, les adresses refusées avant toute connexion (informations d'utilisateur,
    schémas, noms d'une seule étiquette, les noms et les formes numériques du loopback), puis, le
    loopback étant permis pour les tests, les pages du serveur local : une page, un texte coupé à
    2 Mo, un type refusé, les fichiers PDF de plus de 20 Mo refusés, une redirection vers une adresse
    privée refusée.  La recherche de Wikipédia répond depuis le même serveur avec ses documents.  Les
    deux routes refusent de travailler hors ligne ou avec l'accès web désactivé, et une recherche
    sans fournisseur est refusée.
*/

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <zlib.h>

#include <dlib/external/httplib/httplib.h>

#include "../common/builtin_models.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "../host/crypto.h"
#include "../host/http_fetch.h"
#include "../host/models.h"
#include "../host/settings.h"
#include "../host/store.h"
#include "../host/web_access.h"
#include "tiny_gguf.h"

namespace
{
    using albireo::json;

    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++failures;
    }

    void write_text(const std::string& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    std::string read_text(const std::string& path)
    {
        std::string data;
        albireo::read_file(path, data);
        return data;
    }

    // Un contenu de size octets qui varie avec seed.
    std::string blob(size_t size, char seed)
    {
        std::string b(size, '\0');
        for (size_t i = 0; i < size; ++i) b[i] = static_cast<char>(seed + i * 31 + (i >> 9));
        return b;
    }

    json catalog_entry(const std::string& id, const std::string& content, const std::string& url)
    {
        json f = json::object();
        f["url"] = url;
        f["size"] = static_cast<unsigned long long>(content.size());
        f["sha256"] = albireo::sha256_hex(content);
        json files = json::array();
        files.push_back(f);
        json e = json::object();
        e["id"] = id;
        e["name"] = id;
        e["size"] = static_cast<unsigned long long>(content.size());
        e["sha256"] = albireo::sha256_hex(content);
        e["files"] = files;
        json roles = json::array();
        roles.push_back("general");
        e["roles"] = roles;
        return e;
    }

    // Les octets de content compressés par zlib, tels que les donne l'adresse d'une partie compressée.
    std::string deflated(const std::string& content)
    {
        uLongf size = compressBound(static_cast<uLong>(content.size()));
        std::string out(size, '\0');
        if (compress2(reinterpret_cast<Bytef*>(&out[0]), &size, reinterpret_cast<const Bytef*>(content.data()),
                static_cast<uLong>(content.size()), 6) != Z_OK)
            return std::string();
        out.resize(size);
        return out;
    }

    /*
        Une entrée de catalogue de plusieurs parties, chacune à son adresse dans urls : contents donne les
        octets de chaque partie, et packed, quand il n'est pas vide pour une partie, les octets compressés
        que donne son adresse ("compression": "gzip").
    */
    json parts_entry(const std::string& id, const std::vector<std::string>& contents,
        const std::vector<std::string>& urls, const std::vector<std::string>& packed)
    {
        json files = json::array();
        std::string whole;
        for (size_t i = 0; i < contents.size(); ++i)
        {
            json f = json::object();
            f["url"] = urls[i];
            f["size"] = static_cast<unsigned long long>(contents[i].size());
            f["sha256"] = albireo::sha256_hex(contents[i]);
            if (!packed[i].empty())
            {
                f["compression"] = "gzip";
                f["compressed_size"] = static_cast<unsigned long long>(packed[i].size());
            }
            files.push_back(f);
            whole += contents[i];
        }
        json e = catalog_entry(id, whole, urls[0]);
        e["files"] = files;
        return e;
    }

    // Le nombre de lignes du journal récent qui contiennent du texte.
    int logged(const std::string& text)
    {
        int n = 0;
        for (const auto& line : albireo::log_file::instance().recent())
            if (line.find(text) != std::string::npos) ++n;
        return n;
    }

    // Attend que le téléchargement de id quitte ses états actifs ; renvoie son dernier état.
    json wait_download(albireo::model_manager& m, const std::string& id)
    {
        for (int i = 0; i < 1200; ++i)
        {
            // La liste tenue par une variable, que la boucle lit une fois l'appel terminé.
            const json downloads = m.downloads();
            for (const auto& d : downloads.elements())
                if (d["id"].as_string() == id)
                {
                    const std::string s = d["state"].as_string();
                    if (s != "queued" && s != "downloading" && s != "verifying") return d;
                }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return json();
    }

    bool make_symlink(const std::string& target, const std::string& link)
    {
        return symlink(target.c_str(), link.c_str()) == 0;
    }

    bool make_hard_link(const std::string& target, const std::string& link)
    {
        return ::link(target.c_str(), link.c_str()) == 0;
    }

    bool installed(albireo::store& db, const std::string& id, albireo::installed_model& m)
    {
        return db.find_model(id, m);
    }

    /*
        Un serveur qui accepte chaque connexion, lit sa requête et la réinitialise (SO_LINGER à
        0), comme un filtre du réseau qui coupe les connexions vers un site.
    */
    class reset_server
    {
    public:
        ~reset_server() { stop(); }

        bool start(int port)
        {
            listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listener_ == INVALID_SOCKET) return false;
            int yes = 1;
            setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_port = htons(static_cast<unsigned short>(port));
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(listener_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || ::listen(listener_, 8) != 0)
            {
                httplib::detail::close_socket(listener_);
                listener_ = INVALID_SOCKET;
                return false;
            }
            thread_ = std::thread([this]() {
                while (!stopped_)
                {
                    if (!readable(listener_)) continue;
                    const socket_t c = ::accept(listener_, nullptr, nullptr);
                    if (c == INVALID_SOCKET) continue;
                    char request[4096];
                    if (readable(c)) ::recv(c, request, sizeof(request), 0);
                    linger reset;
                    reset.l_onoff = 1;
                    reset.l_linger = 0;
                    setsockopt(c, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&reset), sizeof(reset));
                    httplib::detail::close_socket(c);
                }
            });
            return true;
        }

        void stop()
        {
            stopped_ = true;
            if (thread_.joinable()) thread_.join();
            if (listener_ != INVALID_SOCKET) httplib::detail::close_socket(listener_);
            listener_ = INVALID_SOCKET;
        }

    private:
        // Si une socket a des données ou une connexion à donner en un dixième de seconde.
        static bool readable(socket_t sock)
        {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(sock, &set);
            timeval wait = { 0, 100000 };
            return select(static_cast<int>(sock + 1), &set, nullptr, nullptr, &wait) > 0;
        }

        socket_t listener_ = INVALID_SOCKET;
        std::atomic<bool> stopped_{false};
        std::thread thread_;
    };

    // Si text contient word, ses lettres ASCII comparées sans tenir compte de la casse (word en minuscules).
    bool holds_folded(const std::string& text, const std::string& word)
    {
        std::string folded = text;
        for (auto& c : folded) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return folded.find(word) != std::string::npos;
    }

}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: test_host <scratch folder> [port]\n");
        return 2;
    }
    const int port = argc >= 3 ? std::atoi(argv[2]) : 18487;
    const std::string root = albireo::final_path(argv[1]).empty() ? argv[1] : albireo::final_path(argv[1]);
    const std::string work = albireo::join_path(root, "test_host-" + albireo::random_hex(8));
    const std::string models_dir = albireo::join_path(work, "models");
    const std::string outside = albireo::join_path(work, "outside");
    albireo::make_dirs(models_dir);
    albireo::make_dirs(outside);
    // Un dossier de données à lui, sur tous les systèmes : le dossier d'import d'un service installé n'est
    // jamais touché.
    const std::string data = albireo::join_path(work, "data");
    albireo::set_data_dir(data);
    check(albireo::data_dir() == data, "the test works in a data folder of its own");
    const std::string inbox = albireo::join_path(albireo::data_dir(), "import");
    albireo::make_dirs(inbox);

    const std::string secured = albireo::join_path(data, "secure-test");
    std::string security_error;
    check(albireo::secure_folder(secured, security_error), "secure_folder creates a private folder");
    struct stat secured_stat;
    check(lstat(secured.c_str(), &secured_stat) == 0 && (secured_stat.st_mode & 0777) == 0700,
          "secure_folder sets mode 0700");
    check(albireo::owned_by_system(secured) && albireo::folder_admin_only(secured),
          "owned folders pass the macOS ownership checks");
    const std::string writable = albireo::join_path(secured, "writable");
    {
        std::ofstream out(writable);
        out << "test";
    }
    chmod(writable.c_str(), 0666);
    check(!albireo::folder_admin_only(secured), "a group-writable or world-writable entry is rejected");
    check(!albireo::secure_folder(secured, security_error, true) && !security_error.empty(),
          "secure_folder refuses unsafe content when asked to inspect it");
    chmod(writable.c_str(), 0600);
    check(albireo::folder_admin_only(secured), "safe contents pass recursive permission checks");
    // Les ACL étendues de macOS : une entrée « allow » qui laisse un autre compte écrire est refusée,
    // une entrée « deny » est acceptée, et secure_folder vide l'ACL dangereuse de son dossier.
    const auto acl = [](const std::string& rule, const std::string& path)
    { return std::system(("/bin/chmod " + rule + " '" + path + "'").c_str()) == 0; };
    check(acl("+a 'everyone allow write'", writable), "the security test adds an ACL to a file");
    check(!albireo::folder_admin_only(secured), "an ACL entry that lets everyone write a file is rejected");
    check(!albireo::secure_folder(secured, security_error, true) && !security_error.empty(),
          "secure_folder refuses content with a writable ACL when asked to inspect it");
    check(acl("-N", writable) && albireo::folder_admin_only(secured), "the file passes again without its ACL");
    check(acl("+a 'everyone deny delete'", secured) && albireo::folder_admin_only(secured)
              && albireo::owned_by_system(secured),
          "a deny ACL entry is accepted");
    check(acl("+a 'everyone allow add_file'", secured) && !albireo::folder_admin_only(secured)
              && !albireo::owned_by_system(secured),
          "an ACL entry that lets everyone add files to the folder is rejected");
    check(albireo::secure_folder(secured, security_error) && albireo::folder_admin_only(secured),
          "secure_folder removes a writable ACL from its folder");
    const std::string linked = albireo::join_path(data, "linked-secure-test");
    check(make_symlink(secured, linked), "the security test creates a symlink");
    check(!albireo::owned_by_system(linked) && !albireo::folder_admin_only(linked),
          "security checks refuse a symlink");
    check(!albireo::secure_folder(linked, security_error) && !albireo::secure_folder(linked + "/", security_error),
          "secure_folder refuses a symlink, including one with a trailing slash");
    check(!albireo::secure_folder("/", security_error), "secure_folder refuses the filesystem root");

    // Les fichiers que donne le serveur local, avec des plages, et un fichier qu'il donne en entier quelle
    // que soit la plage demandée.
    const std::string served = blob(300000, 'a');
    const std::string other = blob(200000, 'k');
    const std::string projector = blob(90000, 'p');
    const std::string newer_tiny = blob(300000, 'b');
    httplib::Server server;
    server.Get("/model.bin", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(served, "application/octet-stream");
    });
    server.Get("/whole.bin", [&](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider("application/octet-stream", [&](size_t offset, httplib::DataSink& sink) {
            if (offset >= served.size())
            {
                sink.done();
                return true;
            }
            return sink.write(served.data() + offset, std::min<size_t>(65536, served.size() - offset));
        });
    });
    /*
        Les pannes que le serveur local donne à la place d'une réponse, comme le ferait un proxy, un filtre
        du réseau ou un serveur abîmé.
    */
    std::atomic<int> official_requests{0};
    enum fault { no_fault, html_200, html_403, proxy_407, stall, trickle };
    // La réponse d'une panne ; false quand il n'y a pas de panne.
    auto fault_answer = [](int f, httplib::Response& res) {
        const std::string blocked = "<html><body><h1>Access denied</h1><p>The filter of this network blocks the "
            "site.</p></body></html>";
        switch (f)
        {
        case html_200:
            res.set_content(blocked, "text/html; charset=utf-8");
            return true;
        case html_403:
            res.status = 403;
            res.set_content(blocked, "text/html");
            return true;
        case proxy_407:
            res.status = 407;
            res.set_header("Proxy-Authenticate", "Basic realm=\"filter\"");
            res.set_content(blocked, "text/html");
            return true;
        case stall:
            std::this_thread::sleep_for(std::chrono::seconds(3));
            res.set_content("{}", "application/json");
            return true;
        case trickle:
            // Un octet toutes les 0,3 s, sous la borne de chaque attente et au-delà de la borne de toute
            // la requête.
            res.set_chunked_content_provider("application/json", [](size_t offset, httplib::DataSink& sink) {
                if (offset >= 20)
                {
                    sink.done();
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                return sink.write(" ", 1);
            });
            return true;
        default:
            return false;
        }
    };
    server.Get("/alive", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("alive", "text/plain");
    });
    // Un fichier dont les premiers octets sont altérés : le premier transfert donne stale_copy, les suivants
    // fresh, comme un serveur qui répare sa copie.
    const std::string stale_copy = blob(150000, 's'), fresh = blob(150000, 'r');
    std::atomic<int> flaky_requests{0};
    server.Get("/flaky.bin", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(flaky_requests++ == 0 ? stale_copy : fresh, "application/octet-stream");
    });
    const std::string first_part = blob(120000, 'f'), second_part = blob(80000, 'g');
    /*
        Les adresses officielles des catalogues, sous /official/ : les fichiers de official_files, donnés
        avec leurs plages, ceux dont le nom commence par "slow" à raison d'un kilo-octet toutes les 20 ms,
        les autres répondus par 404.  official_fault les fait répondre comme le ferait un proxy ou un
        filtre du réseau, et official_asked compte les requêtes de chaque nom.
    */
    std::mutex official_m;
    std::map<std::string, std::string> official_files;
    std::map<std::string, int> official_asked;
    std::atomic<int> official_fault{no_fault};
    auto official_serve = [&](const std::string& name, const std::string& content) {
        std::lock_guard<std::mutex> lock(official_m);
        official_files[name] = content;
    };
    auto official_count = [&](const std::string& name) {
        std::lock_guard<std::mutex> lock(official_m);
        const auto it = official_asked.find(name);
        return it == official_asked.end() ? 0 : it->second;
    };
    server.Get(R"(/official/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        ++official_requests;
        const std::string name = req.matches[1].str();
        auto content = std::make_shared<std::string>();
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(official_m);
            ++official_asked[name];
            const auto it = official_files.find(name);
            found = it != official_files.end();
            if (found) *content = it->second;
        }
        if (fault_answer(official_fault, res)) return;
        if (!found)
        {
            res.status = 404;
            return;
        }
        if (name.compare(0, 4, "slow") != 0)
        {
            res.set_content(*content, "application/octet-stream");
            return;
        }
        res.set_content_provider(content->size(), "application/octet-stream",
            [content](size_t offset, size_t length, httplib::DataSink& sink) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return sink.write(content->data() + offset, std::min<size_t>(length, 1024));
            });
    });
    /*
        Les pages de l'accès web : une page, un texte de 3 Mo, une image, un petit fichier PDF, deux
        fichiers PDF de 21 Mo (l'un annoncé par sa longueur, l'autre envoyé par morceaux), une page
        qui répond 404, des redirections vers une adresse privée, vers une adresse relative et vers
        elles-mêmes, et une page qui renvoie les en-têtes qu'elle a reçus.  La recherche répond comme
        l'interface de Wikipédia, et garde ce qu'elle a reçu, que les vérifications lisent sous
        seen_m.
    */
    const std::string page = "<html><body><p>Albireo &amp; the web</p></body></html>";
    const std::string large_text = blob(3u << 20, 't');
    const size_t large_pdf = size_t(21) << 20;
    server.Get("/web/page.html", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(page, "text/html; charset=UTF-8");
    });
    server.Get("/web/large.txt", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(large_text, "text/plain");
    });
    server.Get("/web/image.png", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string("\x89PNG\r\n\x1a\n", 8), "image/png");
    });
    server.Get("/web/small.pdf", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("%PDF-1.4\n%%EOF\n", "application/pdf");
    });
    server.Get("/web/large.pdf", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content_provider(large_pdf, "application/pdf", [](size_t, size_t length, httplib::DataSink& sink) {
            const std::string chunk(65536, '%');
            return sink.write(chunk.data(), std::min(length, chunk.size()));
        });
    });
    server.Get("/web/stream.pdf", [&](const httplib::Request&, httplib::Response& res) {
        res.set_chunked_content_provider("application/pdf", [&](size_t offset, httplib::DataSink& sink) {
            if (offset >= large_pdf)
            {
                sink.done();
                return true;
            }
            const std::string chunk(65536, '%');
            return sink.write(chunk.data(), chunk.size());
        });
    });
    server.Get("/web/missing", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 404;
        res.set_content("<p>no such page</p>", "text/html");
    });
    server.Get("/web/to-private", [&](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("http://192.168.1.10/admin");
    });
    server.Get("/web/relative", [&](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("./sub/../page.html?from=relative");
    });
    server.Get("/web/loop", [&](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/web/loop");
    });
    server.Get("/web/headers", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content(req.get_header_value("User-Agent") + "\n" + req.get_header_value("Accept-Language") + "\n"
            + req.get_header_value("Cookie"), "text/plain; charset=\"utf-8\"");
    });
    std::mutex seen_m;
    std::string seen_agent, seen_query, seen_count;
    server.Get("/w/rest.php/v1/search/page", [&](const httplib::Request& req, httplib::Response& res) {
        {
            std::lock_guard<std::mutex> lock(seen_m);
            seen_agent = req.get_header_value("User-Agent");
            seen_query = req.get_param_value("q");
            seen_count = req.get_param_value("limit");
        }
        if (req.get_param_value("q") == "too many")
        {
            res.status = 429;
            res.set_header("Retry-After", "30");
            res.set_content("You are making too many requests to the API.", "text/plain");
            return;
        }
        res.set_content("{\"pages\": [{\"id\": 736, \"key\": \"Albert_Einstein\", \"title\": \"Albert Einstein\", "
            "\"excerpt\": \"<span class=\\\"searchmatch\\\">Albert</span> Einstein was a &quot;physicist&quot;\", "
            "\"matched_title\": null, \"description\": \"German-born physicist\"}, {\"id\": 9411, \"key\": "
            "\"Einstein_(crater)\", \"title\": \"Einstein (crater)\", \"excerpt\": \"\", \"description\": "
            "\"Lunar crater\"}]}", "application/json");
    });
    if (!server.bind_to_port("127.0.0.1", port))
    {
        std::printf("the port %d cannot be opened\n", port);
        return 2;
    }
    std::thread listener([&]() { server.listen_after_bind(); });
    const std::string base_url = "http://127.0.0.1:" + std::to_string(port);
    /*
        Les téléchargements des tests viennent des adresses officielles de leurs catalogues sur le serveur
        local, catalogue officiel compris.
    */
    const std::string official = base_url + "/official/";
    albireo::official_catalog_for_tests(official + "catalog.json");
    official_serve("model.bin", served);
    official_serve("other.bin", other);
    official_serve("projector.bin", projector);
    official_serve("model-2.bin", newer_tiny);

    {
        albireo::store db(albireo::join_path(work, "albireo.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-10-30T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", served, base_url + "/official/model.bin"));
        list.push_back(catalog_entry("small", other, base_url + "/official/other.bin"));
        json vision = catalog_entry("vision", other, base_url + "/official/other.bin");
        json projector_file = json::object();
        projector_file["url"] = base_url + "/official/projector.bin";
        projector_file["size"] = static_cast<unsigned long long>(projector.size());
        projector_file["sha256"] = albireo::sha256_hex(projector);
        json projector_files = json::array();
        projector_files.push_back(projector_file);
        json projector_entry = json::object();
        projector_entry["size"] = static_cast<unsigned long long>(projector.size());
        projector_entry["sha256"] = albireo::sha256_hex(projector);
        projector_entry["files"] = projector_files;
        vision["projector"] = projector_entry;
        list.push_back(vision);
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        const std::string reserve_dir = albireo::join_path(work, "reserve");
        albireo::model_manager m(db, models_dir, reserve_dir);
        const std::string dir = m.models_dir();
        const std::string partial = albireo::join_path(dir, "tiny.gguf.partial");
        const std::string target = albireo::join_path(dir, "tiny.gguf");
        albireo::user_error error;

        // Le fichier partiel, un lien vers un fichier extérieur : refusé, le fichier extérieur inchangé.
        const std::string victim = albireo::join_path(outside, "victim.txt");
        write_text(victim, "kept");
        make_symlink(victim, partial);
        check(m.start_download("tiny", error), "a download starts");
        json d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_file",
            "a partial file that is a link is refused (" + d["state"].as_string() + ")");
        check(read_text(victim) == "kept", "the file the link leads to is unchanged");
        albireo::remove_file(partial);

        // Le fichier partiel, second nom d'un fichier extérieur : refusé, le fichier inchangé.
        make_hard_link(victim, partial);
        check(m.start_download("tiny", error), "a download starts again");
        d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_file",
            "a partial file with a second name is refused");
        check(read_text(victim) == "kept", "the file of the second name is unchanged");
        albireo::remove_file(partial);

        // Le nom final, un lien : le téléchargement remplace le lien, n'écrit jamais à travers lui.
        make_symlink(victim, target);
        check(m.start_download("tiny", error), "a download starts over a final name that is a link");
        d = wait_download(m, "tiny");
        albireo::installed_model im;
        check(d["state"].as_string() == "done" && !albireo::is_link(target) && read_text(target) == served,
            "the model replaces the link at its final name (" + d["state"].as_string() + ")");
        check(read_text(victim) == "kept", "the file the final link led to is unchanged");
        check(installed(db, "tiny", im) && im.file == target, "the model is recorded at its name");
        check(!albireo::file_exists(partial), "no partial file is left");

        // Le dossier de modèles remplacé par un lien : le téléchargement n'écrit rien là où il mène.
        const std::string moved = dir + ".moved";
        const std::string elsewhere = albireo::join_path(outside, "elsewhere");
        albireo::make_dirs(elsewhere);
        albireo::rename_file(dir, moved);
        make_symlink(elsewhere, dir);
        check(m.start_download("small", error), "a download starts into a folder replaced by a link");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "error", "the download into a folder replaced by a link fails");
        check(albireo::list_dir(elsewhere).empty(), "nothing is written where the link leads");
        albireo::remove_file(dir);
        albireo::rename_file(moved, dir);

        // Un téléchargement annulé ne laisse aucun fichier partiel et revient ensuite en entier.
        albireo::installed_model small;
        check(m.start_download("small", error), "a download of small starts");
        m.cancel_download("small");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "cancelled" || d["state"].as_string() == "done",
            "the cancelled download ends (" + d["state"].as_string() + ")");
        if (d["state"].as_string() == "cancelled")
            check(!albireo::file_exists(albireo::join_path(dir, "small.gguf.partial")), "its partial file is removed");
        check(m.start_download("small", error), "the download starts again");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && installed(db, "small", small), "the new download installs it");

        // Un fichier inconnu nommé d'après un modèle installé : installé sous un nom à lui, le modèle
        // inchangé.
        const albireo_test::tiny_gguf tiny_model = albireo_test::tiny_gguf_writer().build();
        const std::string unknown = albireo::join_path(inbox, "tiny.gguf");
        tiny_model.save(unknown, tiny_model.bytes);
        const json u = m.import_path(unknown, inbox, false);
        albireo::installed_model kept;
        check(u["imported"].size() == 1 && u["imported"][0].as_string() == "tiny-import",
            "an unknown file named after an installed model takes the suffix -import (" + u.dump() + ")");
        check(read_text(target) == served && installed(db, "tiny", kept) && kept.sha256 == albireo::sha256_hex(served)
            && kept.source == "catalog", "the installed model keeps its file and its record");
        // Un autre fichier inconnu de ce nom prend le nom libre suivant.
        std::vector<unsigned char> changed = tiny_model.bytes;
        changed.back() = static_cast<unsigned char>(changed.back() ^ 0x5a);
        tiny_model.save(unknown, changed);
        const json u2 = m.import_path(unknown, inbox, false);
        check(u2["imported"].size() == 1 && u2["imported"][0].as_string() == "tiny-import-2",
            "a second unknown file of that name takes the next free name (" + u2.dump() + ")");
        albireo::remove_file(unknown);

        const int network_before_restore = official_count("other.bin");
        std::string remove_error;
        const std::string reserved = albireo::join_path(reserve_dir, albireo::sha256_hex(other) + ".gguf");
        struct stat reserve_stat = {};
        check(m.remove("small", remove_error) && !albireo::file_exists(small.file)
            && read_text(reserved) == other && ::stat(reserved.c_str(), &reserve_stat) == 0
            && (reserve_stat.st_mode & 0777) == 0600 && !installed(db, "small", small),
            "removing a model keeps its verified file in the local reserve");
        check(m.start_download("small", error), "a reserved model can be reinstalled");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && read_text(small.file) == other
            && official_count("other.bin") == network_before_restore,
            "a reserved model is restored without a network request");
        check(m.remove("small", remove_error), "a restored model can be reserved again");
        check(!albireo::http_cancelled() && m.start_download("small", error, true),
            "a local-only restoration of a model without projector starts while the network is available");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && read_text(small.file) == other
            && official_count("other.bin") == network_before_restore,
            "a model without projector is restored locally without a network request");
        check(m.remove("small", remove_error), "the offline-restored model can be reserved again");
        std::string corrupt_reserve = other;
        corrupt_reserve[0] ^= 1;
        write_text(reserved, corrupt_reserve);
        const int network_before_corrupt_reserve = official_count("other.bin");
        check(!albireo::http_cancelled() && m.start_download("small", error, true),
            "a local-only restoration starts while network access is otherwise available");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "reserve_unavailable"
            && !installed(db, "small", small) && official_count("other.bin") == network_before_corrupt_reserve,
            "a corrupt reserve is refused locally without a network request");
        check(m.start_download("small", error), "a corrupt reserved model falls back to its catalog address");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && read_text(small.file) == other
            && official_count("other.bin") > network_before_corrupt_reserve,
            "a corrupt reserve file is not installed and the network copy is checked");

        check(m.start_download("vision", error), "a model with an image projector downloads");
        d = wait_download(m, "vision");
        albireo::installed_model vision_model;
        check(d["state"].as_string() == "done" && installed(db, "vision", vision_model)
            && read_text(vision_model.file) == other && read_text(vision_model.projector) == projector,
            "the model and projector are installed together");
        check(m.remove("vision", remove_error), "a model and projector can be moved to reserve");
        const std::string vision_reserved = albireo::join_path(reserve_dir,
            albireo::sha256_hex(other) + ".gguf");
        const std::string projector_reserved = albireo::join_path(reserve_dir,
            albireo::sha256_hex(projector) + ".gguf");
        check(read_text(vision_reserved) == other && read_text(projector_reserved) == projector,
            "both model and projector are kept in the local reserve");
        const int network_before_vision_restore = official_count("other.bin") + official_count("projector.bin");
        albireo::http_cancel_all("offline mode");
        check(m.start_download("vision", error, true), "a model with its projector can be restored offline");
        d = wait_download(m, "vision");
        albireo::http_restore();
        check(d["state"].as_string() == "done" && read_text(vision_model.file) == other
            && read_text(vision_model.projector) == projector
            && official_count("other.bin") + official_count("projector.bin") == network_before_vision_restore,
            "a reserved model and projector are restored while network transfers are disabled");

        check(m.remove("vision", remove_error), "the restored model and projector can be reserved again");
        const std::string vision_partial = vision_model.file + ".partial";
        const std::string projector_partial = vision_model.projector + ".partial";
        const std::string decoder_prefix = other.substr(0, 1000), projector_prefix = projector.substr(0, 500);
        write_text(vision_partial, decoder_prefix);
        write_text(projector_partial, projector_prefix);
        json progress = json::object(), done = json::array();
        progress["sha256"] = albireo::sha256_hex(other);
        progress["projector_sha256"] = albireo::sha256_hex(projector);
        done.push_back(static_cast<unsigned long long>(decoder_prefix.size()));
        done.push_back(static_cast<unsigned long long>(projector_prefix.size()));
        progress["done"] = done;
        db.set_download_state("vision", progress);
        check(albireo::remove_file(projector_reserved), "the projector can be removed from the reserve for the test");
        const int network_before_missing = official_count("other.bin") + official_count("projector.bin");
        check(m.start_download("vision", error, true), "a local-only restoration with a missing projector starts");
        d = wait_download(m, "vision");
        const std::vector<std::string> after_missing = albireo::list_dir(dir);
        check(d["state"].as_string() == "error" && d["code"].as_string() == "reserve_unavailable"
            && !installed(db, "vision", vision_model)
            && official_count("other.bin") + official_count("projector.bin") == network_before_missing
            && read_text(vision_partial) == decoder_prefix && read_text(projector_partial) == projector_prefix
            && db.download_state("vision").dump() == progress.dump()
            && std::none_of(after_missing.begin(), after_missing.end(), [](const std::string& name) {
                return name.compare(0, 9, ".reserve-") == 0;
            }),
            "a missing projector leaves previous progress intact and makes no network request");

        std::string corrupt_projector = projector;
        corrupt_projector[0] ^= 1;
        write_text(projector_reserved, corrupt_projector);
        check(m.start_download("vision", error, true), "a local-only restoration with a corrupt projector starts");
        d = wait_download(m, "vision");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "reserve_unavailable"
            && official_count("other.bin") + official_count("projector.bin") == network_before_missing
            && read_text(vision_partial) == decoder_prefix && read_text(projector_partial) == projector_prefix
            && db.download_state("vision").dump() == progress.dump(),
            "a corrupt projector is refused without network access or loss of previous progress");

        const int other_before_fallback = official_count("other.bin");
        const int projector_before_fallback = official_count("projector.bin");
        check(m.start_download("vision", error), "the same model can fall back to the network when online");
        d = wait_download(m, "vision");
        check(d["state"].as_string() == "done" && installed(db, "vision", vision_model)
            && read_text(vision_model.file) == other && read_text(vision_model.projector) == projector
            && official_count("other.bin") + official_count("projector.bin") > network_before_missing
            && !db.download_state("vision").is_object(),
            "the online fallback resumes and verifies both files");
        // Fork pour macOS : le fichier sain de la réserve est repris, seul le projecteur abîmé se télécharge, et
        // la fin du téléchargement remet une copie saine dans la réserve.
        check(official_count("other.bin") == other_before_fallback
            && official_count("projector.bin") > projector_before_fallback,
            "online, the sound file of the reserve is reused and only the damaged projector is downloaded");
        struct stat kept_stat = {};
        check(read_text(projector_reserved) == projector && ::stat(projector_reserved.c_str(), &kept_stat) == 0
            && kept_stat.st_mtime >= std::time(nullptr) - 60,
            "the end of a download keeps a sound copy of each file in the reserve, dated when it is kept");

        check(m.remove("vision", remove_error), "the model can be reserved before a service stop");
        write_text(vision_partial, decoder_prefix);
        write_text(projector_partial, projector_prefix);
        db.set_download_state("vision", progress);
        const int network_before_stop = official_count("other.bin") + official_count("projector.bin");
        check(m.start_download("vision", error, true), "a local restoration starts before the service stops");
        m.stop();
        d = wait_download(m, "vision");
        const bool kept_progress = d["state"].as_string() == "cancelled"
            && read_text(vision_partial) == decoder_prefix && read_text(projector_partial) == projector_prefix
            && db.download_state("vision").dump() == progress.dump();
        const bool completed = d["state"].as_string() == "done" && installed(db, "vision", vision_model)
            && read_text(vision_model.file) == other && read_text(vision_model.projector) == projector;
        check((kept_progress || completed)
            && official_count("other.bin") + official_count("projector.bin") == network_before_stop,
            "a service stop preserves previous progress unless restoration has already completed");

        // Fork pour macOS : le plafond de la réserve, son état et son vidage.
        json reserve = m.reserve_status();
        check(reserve["files"].size() >= 1 && reserve["limit"].as_int() == (200ll << 30),
            "the reserve lists its copies under the default limit of 200 GB (" + reserve.dump() + ")");
        m.reserve_limit([]() { return static_cast<uint64_t>(0); });
        m.trim_reserve();
        reserve = m.reserve_status();
        check(reserve["used"].as_int() == 0 && albireo::file_exists(vision_reserved) && reserve["shared"].as_int() > 0,
            "a limit of 0 removes the copies of removed models and keeps those of installed ones");
        const std::string old_copy = albireo::join_path(reserve_dir, albireo::sha256_hex("old reserve copy") + ".gguf");
        const std::string new_copy = albireo::join_path(reserve_dir, albireo::sha256_hex("new reserve copy") + ".gguf");
        write_text(old_copy, "old reserve copy");
        write_text(new_copy, "new reserve copy");
        struct timeval long_ago[2] = { { 1000000, 0 }, { 1000000, 0 } };
        utimes(old_copy.c_str(), long_ago);
        m.reserve_limit([]() { return static_cast<uint64_t>(16); });
        m.trim_reserve();
        check(!albireo::file_exists(old_copy) && albireo::file_exists(new_copy),
            "beyond its limit, the reserve removes its oldest copy first");
        uint64_t freed = 0;
        const size_t emptied = m.clear_reserve(freed);
        check(emptied >= 2 && freed >= 16 && m.reserve_status()["files"].size() == 0,
            "emptying the reserve removes every copy");
    }

    {
        /*
            Une version plus récente du modèle installé tiny : son fichier prend son nom par la
            fonction de on_replace(), qui soustrait les fichiers au moteur.  Un remplacement qu'elle
            refuse laisse le modèle tel quel ; le téléchargement suivant se complète à partir des
            fichiers reçus, sans nouveau transfert.
        */
        albireo::store db(albireo::join_path(work, "albireo.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-10-31T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", newer_tiny, base_url + "/official/model-2.bin"));
        list.push_back(catalog_entry("small", other, base_url + "/official/other.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        albireo::model_manager m(db, models_dir);
        const std::string target = albireo::join_path(m.models_dir(), "tiny.gguf");
        std::atomic<bool> allow{false};
        std::atomic<int> asked{0};
        m.on_replace([&](const std::string& id, const std::function<bool()>& place) {
            if (id == "tiny") ++asked;
            return allow && place();
        });
        albireo::user_error error;
        check(m.start_download("tiny", error), "the download of a newer version of an installed model starts");
        json d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_install" && asked == 1,
            "a replacement refused ends the download in error (" + d.dump() + ")");
        albireo::installed_model im;
        check(read_text(target) == served && installed(db, "tiny", im) && im.sha256 == albireo::sha256_hex(served),
            "the model keeps its file and its record when its replacement is refused");
        allow = true;
        check(m.start_download("tiny", error), "the download starts again");
        d = wait_download(m, "tiny");
        check(d["state"].as_string() == "done" && asked == 2, "the next replacement completes it (" + d.dump() + ")");
        check(read_text(target) == newer_tiny && installed(db, "tiny", im)
            && im.sha256 == albireo::sha256_hex(newer_tiny), "the newer version takes the name and the record of tiny");
        check(!albireo::file_exists(target + ".partial"), "no partial file is left");
        m.stop();
    }

    {
        // Les modèles fournis avec Albireo, et les imports du même contenu.
        albireo::store db(albireo::join_path(work, "albireo-2.db"));
        const std::string builtin = albireo::join_path(work, "builtin");
        albireo::make_dirs(builtin);
        const std::string own = blob(150000, 'q');
        write_text(albireo::join_path(builtin, "base.gguf"), own);
        json entry = json::object();
        entry["id"] = "base-albireo";
        entry["name"] = "Base";
        entry["file"] = "base.gguf";
        entry["sha256"] = albireo::sha256_hex(own);
        // Le fichier du catalogue à partir duquel le build l'a fait.
        const std::string source = blob(140000, 'o');
        entry["source_sha256"] = albireo::sha256_hex(source);
        json listed = json::array();
        listed.push_back(entry);
        json manifest = json::object();
        manifest["models"] = listed;
        write_text(albireo::join_path(builtin, albireo::builtin_manifest_name), manifest.dump());
        // Le catalogue en usage connaît un autre contenu sous l'identifiant du modèle fourni avec Albireo.
        const std::string newer = blob(160000, 'z');
        json catalog = json::object();
        catalog["updated"] = "2026-10-30T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("base-albireo", newer, base_url + "/none.bin"));
        list.push_back(catalog_entry("base", source, base_url + "/source.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        const std::string models2 = albireo::join_path(work, "models-2");
        albireo::model_manager m(db, models2);
        std::vector<albireo::installed_model> removed;
        m.register_builtin(builtin, removed);

        // L'entrée du fichier à partir duquel le modèle d'Albireo a été fait : le modèle d'Albireo en
        // tient lieu.
        const json view = m.overview("en");
        bool listed_source = false, listed_builtin = false;
        for (const auto& x : view["models"].elements())
        {
            listed_source = listed_source || x["id"].as_string() == "base";
            listed_builtin = listed_builtin || x["id"].as_string() == "base-albireo";
        }
        check(!listed_source && listed_builtin, "the catalog entry a model of Albireo stands for is not listed");
        albireo::user_error carried;
        check(!m.start_download("base", carried) && carried.code == "builtin_installed",
            "the download of the file a model of Albireo carries is refused");

        // Le même contenu dans le dossier d'import : pas copié, pas enregistré à nouveau, retiré du dossier.
        const std::string copy = albireo::join_path(inbox, "base-albireo.gguf");
        write_text(copy, own);
        const json r = m.import_path(copy, inbox, true);
        albireo::installed_model im;
        check(r["imported"].size() == 0 && r["already"].size() == 1 && r["already"][0].as_string() == "base-albireo",
            "a copy of a model that comes with Albireo is installed already");
        check(albireo::list_dir(m.models_dir()).empty(), "no second copy enters the models folder");
        check(installed(db, "base-albireo", im) && im.source == "builtin", "the model stays the one of Albireo");
        check(!albireo::file_exists(copy), "the copy leaves the import folder");

        // Un autre contenu sous son identifiant : installé sous son propre nom, jamais par-dessus.
        const std::string variant = albireo::join_path(inbox, "variant.gguf");
        write_text(variant, newer);
        const json v = m.import_path(variant, inbox, false);
        albireo::installed_model renamed;
        check(v["imported"].size() == 1 && v["imported"][0].as_string() == "base-albireo-import",
            "a file with the identifier of a model of Albireo takes the suffix -import");
        check(installed(db, "base-albireo", im) && im.source == "builtin", "the model of Albireo keeps its entry");
        check(installed(db, "base-albireo-import", renamed) && renamed.source == "catalog",
            "the other file has an entry of its own");
        check(albireo::file_exists(variant), "a file imported without move stays in place");

        // Un téléchargement de son identifiant est refusé.
        albireo::user_error error;
        check(!m.start_download("base-albireo", error) && error.code == "builtin_installed",
            "a download of a model that comes with Albireo is refused");

        // Le dossier d'import se lit sans aucun lien et par un seul nom.
        const std::string secret = albireo::join_path(outside, "secret.gguf");
        write_text(secret, blob(1000, 's'));
        const std::string linked = albireo::join_path(inbox, "linked.gguf");
        make_symlink(secret, linked);
        const json l = m.import_path(linked, inbox, true);
        check(l["imported"].size() == 0 && l["errors"].size() == 1, "a link in the import folder is refused");
        const std::string hard = albireo::join_path(inbox, "hard.gguf");
        make_hard_link(secret, hard);
        const json h = m.import_path(hard, inbox, true);
        check(h["imported"].size() == 0 && h["errors"].size() == 1, "a second name in the import folder is refused");
        check(albireo::file_exists(secret), "the file they lead to stays");
        albireo::remove_file(linked);
        albireo::remove_file(hard);
        // Un dossier du dossier d'import remplacé par un lien : rien n'est lu à travers lui.
        const std::string sub = albireo::join_path(inbox, "models");
        make_symlink(outside, sub);
        const json s = m.import_path(inbox, inbox, false);
        check(s["imported"].size() == 0, "the models subfolder that is a link is not read");
        albireo::remove_file(sub);
        std::string why;
        albireo::link_free_file f;
        check(!f.open(albireo::join_path(albireo::join_path(work, "via"), "secret.gguf"), work, why)
            && (why == "missing" || why == "link"), "a missing entry is reported");
        make_symlink(outside, albireo::join_path(work, "via"));
        check(!f.open(albireo::join_path(albireo::join_path(work, "via"), "secret.gguf"), work, why) && why == "link",
            "a folder that is a link stops a link-free opening");

        // Un paquet hors ligne : seulement les fichiers de son catalogue, avec leurs tailles et empreintes.
        const std::string bundle = albireo::join_path(work, "bundle");
        albireo::make_dirs(albireo::join_path(bundle, "models"));
        const std::string listed_content = blob(120000, 'm');
        json bundle_catalog = json::object();
        json models = json::array();
        models.push_back(catalog_entry("bundled", listed_content, "https://example.org/files/bundled-q4.gguf"));
        models.push_back(catalog_entry("damaged", blob(50000, 'd'), "https://example.org/files/damaged.gguf"));
        bundle_catalog["models"] = models;
        write_text(albireo::join_path(albireo::join_path(bundle, "models"), "bundled-q4.gguf"), listed_content);
        write_text(albireo::join_path(albireo::join_path(bundle, "models"), "damaged.gguf"), blob(50000, 'x'));
        write_text(albireo::join_path(albireo::join_path(bundle, "models"), "unlisted.gguf"), blob(1000, 'u'));
        const json b = m.import_bundle(bundle, bundle_catalog);
        check(b["imported"].size() == 1 && b["imported"][0].as_string() == "bundled", "the listed model is imported");
        check(b["errors"].size() == 1, "the file that does not match the catalog is refused");
        bool leftover = false;
        for (const auto& n : albireo::list_dir(m.models_dir()))
            leftover = leftover || n.find(".partial") != std::string::npos;
        check(!leftover, "no temporary file is left in the models folder");
        const json again = m.import_bundle(bundle, bundle_catalog);
        check(again["imported"].size() == 0 && again["already"].size() == 1, "an imported model is not copied twice");
        m.stop();
    }

    {
        // Les plages des téléchargements apportent les octets demandés et pas plus.
        std::string got, error;
        auto keep = [&](const char* data, size_t n) {
            got.append(data, n);
            return true;
        };
        check(albireo::http_get_range(base_url + "/model.bin", 1000, 1999, keep, error)
            && got == served.substr(1000, 1000), "a part of a file arrives as asked (" + error + ")");
        got.clear();
        check(albireo::http_get_range(base_url + "/whole.bin", 0, 999, keep, error) && got == served.substr(0, 1000),
            "a server that ignores the range of a request from 0 gives the bytes asked for alone (" + error + ")");
        got.clear();
        check(!albireo::http_get_range(base_url + "/whole.bin", 1000, 1999, keep, error) && got.empty(),
            "a server that ignores the range of a request from elsewhere is refused");
    }

    {
        // Les adresses des messages ne portent pas leur requête (une clé de recherche peut s'y trouver).
        check(albireo::without_queries("https://example.org/search?q=X&key=Y: status 500")
            == "https://example.org/search: status 500", "the queries of the addresses stay out of the messages");
        check(albireo::without_queries("'https://example.org/a?x=1' \"https://example.org/b?y=2\"\r\n"
                "https://example.org/c?z=3\tend")
            == "'https://example.org/a' \"https://example.org/b\"\r\nhttps://example.org/c\tend",
            "queries are removed from quoted addresses and lines without losing their delimiters");
        // Le journal écrit ses lignes telles qu'elles sont données.
        const std::string log_path = albireo::join_path(work, "logs/service.log");
        albireo::make_dirs(albireo::join_path(work, "logs"));
        albireo::log_file::instance().open(log_path);
        albireo::log_warning("models: a line of the log");
        check(read_text(log_path).find("[warning] models: a line of the log") != std::string::npos,
            "the log writes its lines as they are given");
    }

    {
        /*
            Les téléchargements depuis les adresses du catalogue : un modèle, un modèle de deux parties, un
            fichier corrompu qui est récupéré de nouveau, une empreinte qui échoue encore, une adresse qui
            n'a pas le fichier, puis une adresse qui le donne à une autre taille.
        */
        albireo::store db(albireo::join_path(work, "albireo-3.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-11-01T00:00:00Z";
        json list = json::array();
        const std::string absent = blob(5000, 'n');
        const std::string linked = blob(250000, 'l');
        list.push_back(catalog_entry("linked", linked, official + "gone.bin"));
        list.push_back(catalog_entry("absent", absent, official + "absent.bin"));
        list.push_back(parts_entry("mixed", { first_part, second_part }, { official + "first.bin",
            official + "second.bin" }, { std::string(), std::string() }));
        list.push_back(catalog_entry("stale", fresh, base_url + "/flaky.bin"));
        list.push_back(catalog_entry("wrong", fresh, official + "wrong.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        official_serve("gone.bin", linked);
        official_serve("first.bin", first_part);
        official_serve("second.bin", second_part);
        official_serve("wrong.bin", stale_copy);
        albireo::model_manager m(db, albireo::join_path(work, "models-3"));
        albireo::user_error started;
        albireo::installed_model im;
        auto complete = [&](const json& d, const std::string& id, const std::string& content) {
            return d["state"].as_string() == "done" && installed(db, id, im) && read_text(im.file) == content
                && im.sha256 == albireo::sha256_hex(content) && d["received"].as_int(-1) == d["total"].as_int(-2);
        };
        check(m.start_download("linked", started), "the download of a model starts");
        json d = wait_download(m, "linked");
        check(complete(d, "linked", linked) && official_count("gone.bin") == 1
            && logged("models: linked installed") == 1, "a model comes from the address of the catalog ("
            + d.dump() + ")");
        check(m.start_download("mixed", started), "the download of a model of two parts starts");
        d = wait_download(m, "mixed");
        check(complete(d, "mixed", first_part + second_part) && official_count("first.bin") == 1
            && official_count("second.bin") == 1, "a model of two parts comes from its two addresses (" + d.dump()
            + ")");
        check(m.start_download("stale", started), "the download of a model whose first copy has other bytes starts");
        d = wait_download(m, "stale");
        check(complete(d, "stale", fresh) && flaky_requests == 2
            && logged("models: part 0 of stale is corrupt, fetched again") == 1,
            "a part whose bytes fail the hash is fetched again (" + d.dump() + ")");
        check(m.start_download("wrong", started), "the download of a model whose bytes are always wrong starts");
        d = wait_download(m, "wrong");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_hash"
            && official_count("wrong.bin") == 2 && !installed(db, "wrong", im),
            "a file that fails its hash twice is refused, not installed (" + d.dump() + ")");

        // Une adresse qui n'a pas le fichier : trois tentatives, l'erreur nomme l'adresse et la réponse.
        check(m.start_download("absent", started), "the download of a model the address lacks starts");
        d = wait_download(m, "absent");
        const std::string missing = official + "absent.bin cannot be downloaded: the request is answered with the "
            "status 404";
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_failed"
            && d["error"].as_string() == missing && d["args"]["e"].as_string() == missing
            && official_count("absent.bin") == 3 && !installed(db, "absent", im),
            "a model whose address lacks the file fails with the address and the answer (" + d.dump() + ")");
        // Une adresse qui donne le fichier à une autre taille que le catalogue échoue, la bonne se complète.
        official_serve("absent.bin", blob(4000, 'n'));
        check(m.start_download("absent", started), "a download starts from an address of another size");
        d = wait_download(m, "absent");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_failed"
            && d["error"].as_string() == "the file of " + official + "absent.bin does not have the size the catalog "
            "gives" && !albireo::file_exists(albireo::join_path(m.models_dir(), "absent.gguf")),
            "an address of another size fails (" + d.dump() + ")");
        official_serve("absent.bin", absent);
        check(m.start_download("absent", started), "a download starts again from the right address");
        d = wait_download(m, "absent");
        check(complete(d, "absent", absent), "a model comes from its address once it serves the file ("
            + d.dump() + ")");
        m.stop();
    }

    {
        /*
            Les échecs du réseau, avec les bornes des attentes raccourcies à 1 s et celle d'une requête entière
            à 2 s : une page web, un blocage ou une réinitialisation font échouer le transfert avant qu'aucun
            octet n'atteigne le fichier, l'opération échoue avec son message, aucune exception n'en sort, et le
            serveur local répond encore.
        */
        reset_server resetting;
        check(resetting.start(port + 1), "a server that resets its connections listens on the port "
            + std::to_string(port + 1));
        albireo::http_bounds_for_tests(1, 2);
        auto answers = [&]() {
            std::string alive, e;
            return albireo::http_get(base_url + "/alive", alive, e, 64) && alive == "alive";
        };
        const std::string page_text = "a web page comes in place of the answer, as a proxy or a filter of the network "
            "sends one";
        std::string written, error;
        auto keep = [&](const char* data, size_t n) {
            written.append(data, n);
            return true;
        };
        bool escaped = false;
        official_fault = html_200;
        const bool from_page = albireo::http_get_range(official + "model.bin", 0, 999, keep, error);
        const std::string page_error = error;
        official_fault = stall;
        const bool from_stall = albireo::http_get_range(official + "model.bin", 0, 999, keep, error);
        const std::string stall_error = error;
        official_fault = no_fault;
        const bool from_reset = albireo::http_get_range("http://127.0.0.1:" + std::to_string(port + 1)
            + "/official/model.bin", 1000, 1999, keep, error);
        // Un récepteur qui lève une exception : l'exception devient l'erreur du téléchargement.
        try
        {
            albireo::http_get_range(official + "model.bin", 0, 999, [](const char*, size_t) -> bool {
                throw std::runtime_error("the disk is full");
            }, error);
        }
        catch (...)
        {
            escaped = true;
        }
        check(!from_page && !from_stall && !from_reset && written.empty() && page_error == page_text
            && stall_error == "no data arrives within 1 s" && !escaped
            && error == "the transfer fails: the disk is full", "a range answered by a web page, a stall or a reset "
            "writes nothing, and an exception of its receiver becomes its error (" + page_error + "; " + stall_error
            + "; " + error + ")");

        // Un modèle dont l'adresse répond une page web, puis donne le fichier ; un catalogue que son adresse
        // n'a pas.
        albireo::store db(albireo::join_path(work, "albireo-4.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-11-01T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", served, official + "model.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        {
            albireo::model_manager m(db, albireo::join_path(work, "models-4"));
            albireo::user_error started;
            official_fault = html_200;
            check(m.start_download("tiny", started), "the download of a model whose address is a web page starts");
            json d = wait_download(m, "tiny");
            official_fault = no_fault;
            const std::string partial = albireo::join_path(m.models_dir(), "tiny.gguf.partial");
            const std::string blocked = official + "model.bin cannot be downloaded: " + page_text;
            check(d["state"].as_string() == "error" && d["code"].as_string() == "download_failed"
                && d["error"].as_string() == blocked && d["args"]["e"].as_string() == blocked
                && read_text(partial).find("Access denied") == std::string::npos && answers(),
                "a model whose address is a web page fails with its message, its partial file free of the page ("
                + d.dump() + ")");
            check(m.start_download("tiny", started), "the download of the model starts again");
            d = wait_download(m, "tiny");
            albireo::installed_model im;
            check(d["state"].as_string() == "done" && installed(db, "tiny", im) && read_text(im.file) == served
                && im.sha256 == albireo::sha256_hex(served) && answers(),
                "a model comes from its address once the page is gone (" + d.dump() + ")");
            std::string refreshed;
            const bool refreshed_ok = m.refresh_catalog(refreshed);
            check(!refreshed_ok && refreshed == "the catalog cannot be downloaded from " + official + "catalog.json: "
                "the request is answered with the status 404" && answers(),
                "a catalog its address does not have fails with the address and the answer (" + refreshed + ")");
            m.stop();
        }
        albireo::http_bounds_for_tests(0, 0);
        resetting.stop();
    }

    {
        /*
            Un modèle de huit parties, dont une compressée, dont la dernière n'est pas encore servie : le
            téléchargement échoue avec son adresse, puis reprend avec cette seule partie ; le catalogue sans
            signature, de son adresse ou d'un dossier ; et un téléchargement annulé pendant qu'il vient de
            son adresse.
        */
        albireo::store db(albireo::join_path(work, "albireo-5.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-11-01T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", served, official + "model.bin"));
        std::vector<std::string> contents, urls, packed;
        for (int i = 0; i < 8; ++i)
        {
            const std::string name = "part-" + std::to_string(i) + ".bin";
            contents.push_back(blob(20000 + 1000 * i, static_cast<char>('A' + i)));
            urls.push_back(official + name);
            packed.push_back(i == 3 ? deflated(contents.back()) : std::string());
            // La dernière partie est servie plus tard.
            if (i < 7) official_serve(name, packed.back().empty() ? contents.back() : packed.back());
        }
        list.push_back(parts_entry("parts", contents, urls, packed));
        const std::string slow = blob(300000, 'v');
        list.push_back(catalog_entry("slow", slow, official + "slow.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        official_serve("slow.bin", slow);
        albireo::model_manager m(db, albireo::join_path(work, "models-5"));
        albireo::user_error started;
        albireo::installed_model im;

        check(m.start_download("parts", started), "the download of a model of eight parts starts");
        json d = wait_download(m, "parts");
        const std::string failure = official + "part-7.bin cannot be downloaded: the request is answered with the "
            "status 404";
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_failed"
            && d["error"].as_string() == failure, "a part its address lacks fails the download with the address ("
            + d.dump() + ")");
        official_serve("part-7.bin", contents[7]);
        check(m.start_download("parts", started), "the download of the model of eight parts starts again");
        d = wait_download(m, "parts");
        std::string whole;
        bool each_once = true;
        for (int i = 0; i < 8; ++i)
        {
            whole += contents[static_cast<size_t>(i)];
            each_once = each_once && official_count("part-" + std::to_string(i) + ".bin") == (i < 7 ? 1 : 4);
        }
        check(d["state"].as_string() == "done" && installed(db, "parts", im) && read_text(im.file) == whole
            && im.sha256 == albireo::sha256_hex(whole) && d["received"].as_int(-1) == d["total"].as_int(-2)
            && each_once, "a model of eight parts, one compressed, resumes with its last part (" + d.dump() + ")");

        // Le catalogue, sans signature : son adresse le sert, et un catalogue plus ancien ne remplace pas le
        // plus récent.
        json served_catalog = json::object();
        served_catalog["updated"] = "2026-11-02T00:00:00Z";
        json served_models = json::array();
        served_models.push_back(catalog_entry("tiny", served, official + "model.bin"));
        served_models.push_back(catalog_entry("slow", slow, official + "slow.bin"));
        served_catalog["models"] = served_models;
        official_serve("catalog.json", served_catalog.dump());
        albireo::store cdb(albireo::join_path(work, "albireo-6.db"));
        albireo::model_manager cm(cdb, albireo::join_path(work, "models-6"));
        std::string refresh_error;
        const bool refreshed = cm.refresh_catalog(refresh_error);
        json status = cm.catalog_status();
        check(refreshed && status["models"].as_int() == 2 && status["state"].as_string() == "ok"
            && status["updated"].as_string() == "2026-11-02T00:00:00Z",
            "the catalog comes from its address, without signature (" + refresh_error + ")");
        json older = served_catalog;
        older["updated"] = "2026-10-01T00:00:00Z";
        older["models"] = json::array();
        official_serve("catalog.json", older.dump());
        const bool kept = cm.refresh_catalog(refresh_error);
        status = cm.catalog_status();
        check(kept && status["models"].as_int() == 2 && status["updated"].as_string() == "2026-11-02T00:00:00Z",
            "an older catalog never replaces a newer one (" + refresh_error + ")");
        // Un dossier d'un disque : son catalog.json seul suffit.
        const std::string drive = albireo::join_path(work, "drive");
        albireo::make_dirs(drive);
        json drive_catalog = served_catalog;
        drive_catalog["updated"] = "2026-11-03T00:00:00Z";
        json drive_models = json::array();
        drive_models.push_back(catalog_entry("tiny", served, official + "model.bin"));
        drive_catalog["models"] = drive_models;
        write_text(albireo::join_path(drive, "catalog.json"), drive_catalog.dump());
        const json imported = cm.import_catalog(drive, std::string());
        status = cm.catalog_status();
        check(!imported.contains("error") && imported["models"].as_int() == 1
            && status["updated"].as_string() == "2026-11-03T00:00:00Z",
            "the catalog.json of a folder is installed without signature (" + imported.dump() + ")");
        const json empty_drive = cm.import_catalog(albireo::join_path(work, "no-drive"), std::string());
        check(empty_drive.contains("error"), "a folder without catalog.json is refused");
        cm.stop();

        // Un téléchargement annulé pendant qu'il vient de son adresse se termine aussitôt, son fichier partiel
        // supprimé.
        check(m.start_download("slow", started), "the download of a slow model starts");
        long long received = 0;
        for (int i = 0; i < 300 && received == 0; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const json downloads = m.downloads();
            for (const auto& x : downloads.elements())
                if (x["id"].as_string() == "slow") received = x["received"].as_int(0);
        }
        albireo::user_error local_busy;
        check(!m.start_download("slow", local_busy, true) && local_busy.code == "download_busy",
            "a local-only request does not claim that a running network download is a local restoration");
        const auto cancelled_at = std::chrono::steady_clock::now();
        m.cancel_download("slow");
        d = wait_download(m, "slow");
        const double cancel_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()
            - cancelled_at).count();
        check(received > 0 && received < static_cast<long long>(slow.size()) && d["state"].as_string() == "cancelled"
            && cancel_seconds < 5 && official_count("slow.bin") == 1 && !installed(db, "slow", im)
            && !albireo::file_exists(albireo::join_path(m.models_dir(), "slow.gguf.partial"))
            && !db.download_state("slow").is_object(), "a download cancelled while it comes from its address "
            "ends in " + std::to_string(static_cast<int>(cancel_seconds)) + " s, its partial file and progress "
            "removed (" + d.dump() + ")");
        m.stop();
    }

    {
        // Les réglages d'une version antérieure : lus sans erreur, réécrits sans ce qui manque à cette
        // version.
        const std::string folder = albireo::join_path(work, "settings");
        albireo::make_dirs(folder);
        const std::string settings_file = albireo::join_path(folder, "settings.json");
        const std::string key_file = albireo::join_path(folder, "web_search.secret");
        write_text(settings_file, "{\"port\": 9123, \"web_search\": \"brave\", \"web_search_url\": "
            "\"https://searx.example.org\", \"web_search_key_set\": true, \"web_search_key\": \"BSA-by-hand-42\", "
            "\"public_link\": \"https://u.pcloud.link/publink/show?code=kZeNhSJZpcMygkcCvGXmq1MTvkCfBuSsx2GV\"}");
        write_text(key_file, "BSA-secret-0123456789");
        {
            albireo::settings_store store(settings_file);
            const std::string text = read_text(settings_file), copy = read_text(settings_file + ".bak");
            check(store.get().port == 9123 && store.get().web_search == "wikipedia" && !holds_folded(text, "pcloud")
                && !holds_folded(copy, "pcloud") && text.find("web_search_url") == std::string::npos
                && text.find("web_search_key") == std::string::npos && text.find("public_link") == std::string::npos
                && text.find("BSA-") == std::string::npos && !albireo::file_exists(key_file),
                "the settings of an earlier version are read, and written again without what this version lacks");
        }
        albireo::host_settings s;
        const char* const providers[] = { "auto", "brave", "searxng", "wikipedia" };
        bool wikipedia = s.web_search == "wikipedia";
        for (const char* p : providers)
        {
            json j = json::object();
            j["web_search"] = p;
            s.from_json(j);
            wikipedia = wikipedia && s.web_search == "wikipedia";
        }
        json none = json::object();
        none["web_search"] = "none";
        s.from_json(none);
        const bool off = s.web_search == "none";
        json legacy_engine = json::object();
        legacy_engine["engine"] = "openvino";
        legacy_engine["accelerator"] = "gpu";
        s.from_json(legacy_engine);
        const bool legacy_engine_reset = s.engine == "auto" && s.accelerator == "auto";
        json cpu_only = json::object();
        cpu_only["accelerator"] = "none";
        s.from_json(cpu_only);
        const bool cpu_only_kept = s.engine == "auto" && s.accelerator == "none";
        json number = json::object();
        number["web_search"] = 3;
        s.from_json(number);
        const json document = s.to_json();
        check(wikipedia && off && legacy_engine_reset && cpu_only_kept && s.web_search == "wikipedia"
            && !document.contains("public_link")
            && !document.contains("web_search_url") && !document.contains("web_search_key_set"),
            "legacy engine choices return to auto, and settings retain only supported search and accelerator choices");
    }

    {
        // Une base de données abîmée au-delà de son en-tête échoue à l'ouverture comme malformée.
        const std::string path = albireo::join_path(work, "damaged.db");
        {
            albireo::store db(path);
            for (int i = 0; i < 200; ++i) db.set_value("key-" + std::to_string(i), blob(2000, static_cast<char>(i)));
        }
        for (const char* part : { "-wal", "-shm" }) albireo::remove_file(path + part);
        std::string bytes = read_text(path);
        check(bytes.size() > 8 * 4096, "the database spans several pages");
        // Sa première page, l'en-tête et le schéma, reste ; les pages de ses tables sont effacées.
        for (size_t i = 4096; i < bytes.size(); ++i) bytes[i] = 0;
        write_text(path, bytes);
        bool malformed = false;
        try
        {
            albireo::store damaged(path);
        }
        catch (const std::exception& e)
        {
            malformed = std::string(e.what()).find("malformed") != std::string::npos;
        }
        check(malformed, "a damaged database fails its opening as malformed");
    }

    {
        // Un fichier écrit de façon atomique remplace celui en place et ne laisse aucun fichier temporaire.
        const std::string file = albireo::join_path(work, "atomic.txt");
        check(albireo::write_file_atomic(file, "first") && albireo::write_file_atomic(file, "second")
            && read_text(file) == "second", "a file written atomically replaces its content");
        bool temporary = false;
        for (const auto& n : albireo::list_dir(work)) temporary = temporary || n.find("atomic.txt.tmp-") == 0;
        check(!temporary, "no temporary file is left");
    }

    {
        // Les adresses publiques : celles qui suivent et précèdent chaque plage qui n'est pas publique, et
        // ses bornes.
        const char* const public_addresses[] = {
            "1.0.0.0", "9.255.255.255", "11.0.0.0", "100.63.255.255", "100.128.0.0", "126.255.255.255",
            "128.0.0.0", "169.253.255.255", "169.255.0.0", "172.15.255.255", "172.32.0.0", "191.255.255.255",
            "192.0.1.0", "192.0.1.255", "192.0.3.0", "192.88.98.255", "192.88.100.0", "192.167.255.255",
            "192.169.0.0", "198.17.255.255", "198.20.0.0", "198.51.99.255", "198.51.101.0", "203.0.112.255",
            "203.0.114.0", "223.255.255.255", "8.8.8.8", "::1:0:0", "ff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "100:0:0:1::", "2001:db7:ffff:ffff:ffff:ffff:ffff:ffff", "2001:db9::", "2000:ffff:ffff:ffff::",
            "2001:1::", "64:ff9b:0:ffff:ffff:ffff:ffff:ffff", "64:ff9b:2::",
            "fbff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fe00::", "fe7f:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "::fffe:ffff:ffff", "::1:0:0:0", "::ffff:8.8.8.8", "64:ff9a:ffff:ffff:ffff:ffff:ffff:ffff",
            "64:ff9b::1:0:0", "64:ff9b::808:808", "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "2003::",
            "2002:808:808::", "2606:4700:4700::1111"
        };
        const char* const private_addresses[] = {
            "0.0.0.0", "0.255.255.255", "10.0.0.0", "10.255.255.255", "100.64.0.0", "100.127.255.255",
            "127.0.0.0", "127.255.255.255", "169.254.0.0", "169.254.255.255", "172.16.0.0", "172.31.255.255",
            "192.0.0.0", "192.0.0.255", "192.0.2.0", "192.0.2.255", "192.88.99.0", "192.88.99.255",
            "192.168.0.0", "192.168.255.255", "198.18.0.0", "198.19.255.255", "198.51.100.0", "198.51.100.255",
            "203.0.113.0", "203.0.113.255", "224.0.0.0", "239.255.255.255", "240.0.0.0", "255.255.255.255",
            "::", "::1", "100::", "100::ffff:ffff:ffff:ffff", "2001:db8::", "2001:db8:ffff:ffff:ffff:ffff:ffff:ffff",
            "fc00::", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fe80::", "febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "fec0::", "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "ff00::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "::ffff:0.0.0.0", "::ffff:127.0.0.1", "::ffff:10.1.2.3", "::ffff:192.168.1.1", "64:ff9b::7f00:1",
            "64:ff9b::a00:1", "2002:7f00:1::", "2002:c0a8:101::1", "2002:a9fe:1::", "::2", "::7f00:1",
            "::ffff:ffff", "64:ff9b:1::", "64:ff9b:1::a00:1", "64:ff9b:1:ffff:ffff:ffff:ffff:ffff", "2001::",
            "2001::1", "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"
        };
        std::string wrong;
        for (const char* a : public_addresses)
            if (!albireo::public_address(a)) wrong += std::string(" ") + a;
        check(wrong.empty(), "the addresses beside the ranges that are not public are public" + wrong);
        wrong.clear();
        for (const char* a : private_addresses)
            if (albireo::public_address(a)) wrong += std::string(" ") + a;
        check(wrong.empty(), "the addresses of the ranges that are not public are not" + wrong);
        check(!albireo::public_address("example.org") && !albireo::public_address("127.1")
            && !albireo::public_address(""), "a text that is no address is not public");

        // Les adresses refusées avant toute connexion : 400 pour une adresse invalide, 403 pour une
        // adresse locale ou privée.
        albireo::host_settings web;
        auto fetch = [&](const std::string& url, const albireo::host_settings& s, const std::string& language) {
            json request = json::object();
            request["url"] = url;
            if (!language.empty()) request["language"] = language;
            return albireo::web_fetch(request, s);
        };
        struct refusal
        {
            std::string url;
            int status;
        };
        const refusal refusals[] = {
            {"ftp://example.org/file", 400}, {"file:///etc/passwd", 400}, {"javascript:alert(1)", 400},
            {"http://user:secret@example.org/", 400}, {"http://user@example.org/", 400}, {"http:///no-host", 400},
            {"http://exa mple.org/", 400}, {"http://999.1.1.1/", 400}, {"http://example.org:99999/", 400},
            {"https://intranet/", 403}, {"http://localhost/", 403}, {"http://LOCALHOST./", 403},
            {"http://app.localhost:8080/", 403}, {"http://printer.local/", 403}, {"http://db.internal/", 403},
            {"http://nas.home.arpa/", 403}, {"http://2130706433/", 403}, {"http://0x7f.1/", 403},
            {"http://127.1/", 403}, {"http://017700000001/", 403}, {"http://[::1]/", 403},
            {"http://[::ffff:127.0.0.1]/", 403}, {"http://[::ffff:7f00:1]/", 403}, {"http://10.0.0.1/", 403},
            {"http://[fe80::1]/", 403}, {"http://169.254.169.254/latest/meta-data/", 403},
            {base_url + "/web/page.html", 403}
        };
        for (const auto& r : refusals)
        {
            const albireo::web_reply reply = fetch(r.url, web, "");
            check(reply.status == r.status, "the address " + r.url + " is refused with " + std::to_string(r.status)
                + " (" + std::to_string(reply.status) + " " + reply.code + ": " + reply.message + ")");
        }
        const albireo::web_reply local = fetch("http://[::ffff:127.0.0.1]/", web, "");
        check(local.code == "address_refused" && local.message == "the address of [::ffff:127.0.0.1] is local or "
            "private, which the web access of Albireo does not read",
            "a refusal names the host (" + local.message + ")");

        // Le service hors ligne et l'accès web désactivé refusent les deux routes ; la capacité suit.
        albireo::host_settings offline = web, off = web, none = web;
        offline.offline = true;
        off.web_access = false;
        none.web_search = "none";
        json search_request = json::object();
        search_request["query"] = "Einstein relativit\xc3\xa9";
        albireo::web_reply reply = fetch("https://example.org/", offline, "");
        check(reply.status == 403 && reply.code == "offline", "a page is refused offline (" + reply.message + ")");
        reply = albireo::web_search(search_request, offline);
        check(reply.status == 403 && reply.code == "offline", "a search is refused offline");
        reply = fetch("https://example.org/", off, "");
        check(reply.status == 403 && reply.code == "web_access_off",
            "a page is refused with the web access turned off");
        reply = albireo::web_search(search_request, off);
        check(reply.status == 403 && reply.code == "web_access_off", "a search is refused with the web access off");
        reply = albireo::web_search(search_request, none);
        check(reply.status == 403 && reply.code == "no_search_provider" && reply.message == "no search provider is set",
            "a search without provider is refused");
        check(albireo::web_capability(web).dump() == "{\"available\":true,\"search\":\"wikipedia\"}"
            && albireo::web_capability(offline).dump() == "{\"available\":false,\"search\":\"none\"}"
            && albireo::web_capability(off).dump() == "{\"available\":false,\"search\":\"none\"}"
            && albireo::web_capability(none).dump() == "{\"available\":true,\"search\":\"none\"}"
            && albireo::web_search_provider(web) == "wikipedia" && albireo::web_search_provider(none) == "none",
            "the capability gives the web access and its provider of search, wikipedia or none");

        // Les pages du serveur local, le loopback permis pour les tests.
        albireo::web_allow_loopback_for_tests(true);
        reply = fetch(base_url + "/web/page.html", web, "");
        const json& b = reply.body;
        check(reply.status == 200 && b["status"].as_int() == 200 && b["content_type"].as_string() == "text/html"
            && b["charset"].as_string() == "utf-8" && !b["truncated"].as_bool()
            && b["size"].as_int() == static_cast<long long>(page.size())
            && albireo::base64_decode(b["data"].as_string()) == page && b["url"].as_string() == base_url
            + "/web/page.html" && b["final_url"].as_string() == base_url + "/web/page.html",
            "a page is read with its status, its type and its bytes (" + reply.message + ")");
        reply = fetch(base_url + "/web/large.txt", web, "");
        check(reply.status == 200 && reply.body["truncated"].as_bool() && reply.body["size"].as_int() == (2 << 20)
            && albireo::base64_decode(reply.body["data"].as_string()) == large_text.substr(0, 2u << 20),
            "a text is cut at 2 MB (" + reply.message + ")");
        reply = fetch(base_url + "/web/image.png", web, "");
        check(reply.status == 415 && reply.code == "unsupported_type" && reply.message.find("image/png")
            != std::string::npos, "a type the web access does not read is refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/small.pdf", web, "");
        check(reply.status == 200 && reply.body["content_type"].as_string() == "application/pdf"
            && albireo::base64_decode(reply.body["data"].as_string()) == "%PDF-1.4\n%%EOF\n", "a PDF file is read");
        reply = fetch(base_url + "/web/large.pdf", web, "");
        check(reply.status == 413 && reply.code == "too_large", "a PDF file announced above 20 MB is refused ("
            + reply.message + ")");
        reply = fetch(base_url + "/web/stream.pdf", web, "");
        check(reply.status == 413 && reply.code == "too_large", "a PDF file that passes 20 MB while it is read is "
            "refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/missing", web, "");
        check(reply.status == 200 && reply.body["status"].as_int() == 404
            && albireo::base64_decode(reply.body["data"].as_string()) == "<p>no such page</p>",
            "a page that answers 404 is given with its status and its body");
        reply = fetch(base_url + "/web/to-private", web, "");
        check(reply.status == 403 && reply.code == "address_refused" && reply.message.find("192.168.1.10")
            != std::string::npos, "a redirection to a private address is refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/relative", web, "");
        check(reply.status == 200 && reply.body["final_url"].as_string() == base_url + "/web/page.html?from=relative"
            && albireo::base64_decode(reply.body["data"].as_string()) == page,
            "a relative redirection is followed to its address (" + reply.message + ")");
        reply = fetch(base_url + "/web/loop", web, "");
        check(reply.status == 502 && reply.code == "too_many_redirects",
            "redirections are followed five times at most");
        reply = fetch(base_url + "/web/headers", web, "fr-FR");
        check(reply.status == 200 && albireo::base64_decode(reply.body["data"].as_string()) == std::string("Albireo/")
            + ALBIREO_VERSION + " (+https://github.com/Cydral/Dlib-Transformer-extensions)\nfr-FR\n",
            "a page receives the User-Agent of Albireo, the language asked and no cookie");
        albireo::http_cancel_all("the service stops");
        reply = fetch(base_url + "/web/page.html", web, "");
        check(reply.status == 503 && reply.code == "cancelled", "a service that stops ends the reading of pages");
        albireo::http_restore();

        // La recherche de Wikipédia, servie par le serveur local.
        albireo::web_provider_origin_for_tests(base_url);
        search_request["count"] = 5;
        search_request["language"] = "fr";
        reply = albireo::web_search(search_request, web);
        const json results = reply.body["results"];
        check(reply.status == 200 && reply.body["provider"].as_string() == "wikipedia" && results.size() == 2
            && results[0]["title"].as_string() == "Albert Einstein"
            && results[0]["url"].as_string() == "https://fr.wikipedia.org/wiki/Albert_Einstein"
            && results[0]["snippet"].as_string() == "Albert Einstein was a \"physicist\""
            && results[1]["url"].as_string() == "https://fr.wikipedia.org/wiki/Einstein_(crater)"
            && results[1]["snippet"].as_string() == "Lunar crater",
            "Wikipedia gives its pages (" + reply.message + ")");
        {
            std::lock_guard<std::mutex> lock(seen_m);
            check(seen_query == "Einstein relativit\xc3\xa9" && seen_count == "5"
                && seen_agent == std::string("Albireo/") + ALBIREO_VERSION
                + " (https://github.com/Cydral/Dlib-Transformer-extensions; cydraltechnology@gmail.com)",
                "Wikipedia receives the query, the count and the User-Agent with a contact (" + seen_agent + ")");
        }
        search_request["query"] = "too many";
        reply = albireo::web_search(search_request, web);
        check(reply.status == 429 && reply.code == "rate_limited" && reply.retry_after == 30
            && reply.message.find("Wikipedia limits the rate") != std::string::npos,
            "a limit of the rate of Wikipedia is reported with its delay (" + reply.message + ")");
        json invalid = json::object();
        invalid["query"] = "   ";
        const bool empty_refused = albireo::web_search(invalid, web).code == "invalid_query";
        invalid["query"] = std::string(401, 'q');
        const bool long_refused = albireo::web_search(invalid, web).code == "invalid_query";
        std::string four_hundred;
        for (int i = 0; i < 400; ++i) four_hundred += "\xc3\xa9";
        invalid["query"] = four_hundred;
        const bool characters_counted = albireo::web_search(invalid, web).status == 200;
        invalid["query"] = "ok";
        invalid["count"] = 11;
        const bool count_refused = albireo::web_search(invalid, web).code == "invalid_count";
        invalid["count"] = 2;
        invalid["language"] = "french";
        const bool language_refused = albireo::web_search(invalid, web).code == "invalid_language";
        check(empty_refused && long_refused && characters_counted && count_refused && language_refused,
            "a query of 1 to 400 characters, a count of 1 to 10 and a code of language are required");

        albireo::web_provider_origin_for_tests("");
        albireo::web_allow_loopback_for_tests(false);

        // Le journal nomme les hôtes lus et le fournisseur, jamais un chemin ni une requête.
        bool leaked = false, read_line = false, search_line = false;
        for (const auto& line : albireo::log_file::instance().recent())
        {
            leaked = leaked || line.find("relativit") != std::string::npos || line.find("/web/") != std::string::npos
                || line.find("q=") != std::string::npos;
            read_line = read_line || line.find("web: 127.0.0.1 read (200, ") != std::string::npos;
            search_line = search_line || line.find("web: search through wikipedia (2 results)") != std::string::npos;
        }
        check(!leaked && read_line && search_line, "the log names the hosts and the provider, never a path or a "
            "query");
    }

    server.stop();
    listener.join();
    if (failures == 0) albireo::remove_tree(work);
    std::printf(failures ? "%d checks FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
