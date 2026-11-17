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
    le contenu est celui d'un modèle installé, y compris un modèle fourni avec Gulliver, n'est ni
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
    Le relais (repris de l'original, Gulliver 1.0.23) garde son secret dans relay.secret (0600),
    jamais dans settings.json, pour le fournisseur, l'adresse et l'utilisateur pour lesquels il a
    été donné ; il rend les pseudonymes d'une réponse coupée n'importe où, aux limites des mots, sa
    mémoire de la pseudonymisation sert un texte de nouveau sans Pseudalis et oublie les valeurs
    les plus anciennes et inutilisées, et ses requêtes joignent un serveur local qui répond comme
    un moteur de l'interface d'OpenAI, comme Ollama et comme Claude, et comme Pseudalis 3.1.0 : les
    textes et les descriptions des images partent pseudonymisés, les réponses reviennent avec leurs
    valeurs, un Pseudalis qui ne donne pas les valeurs garde le texte, et un refus de la clé ne
    nomme ni la clé ni l'adresse.
*/

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
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
#include "../common/engine_api.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "../host/crypto.h"
#include "../host/http_fetch.h"
#include "../host/models.h"
#include "../host/pseudonyms.h"
#include "../host/relay.h"
#include "../host/settings.h"
#include "../host/store.h"
#include "../host/web_access.h"
#include "tiny_gguf.h"

namespace
{
    using gulliver::json;

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
        gulliver::read_file(path, data);
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
        f["sha256"] = gulliver::sha256_hex(content);
        json files = json::array();
        files.push_back(f);
        json e = json::object();
        e["id"] = id;
        e["name"] = id;
        e["size"] = static_cast<unsigned long long>(content.size());
        e["sha256"] = gulliver::sha256_hex(content);
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
            f["sha256"] = gulliver::sha256_hex(contents[i]);
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
        for (const auto& line : gulliver::log_file::instance().recent())
            if (line.find(text) != std::string::npos) ++n;
        return n;
    }

    // Attend que le téléchargement de id quitte ses états actifs ; renvoie son dernier état.
    json wait_download(gulliver::model_manager& m, const std::string& id)
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

    bool installed(gulliver::store& db, const std::string& id, gulliver::installed_model& m)
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
    const std::string root = gulliver::final_path(argv[1]).empty() ? argv[1] : gulliver::final_path(argv[1]);
    const std::string work = gulliver::join_path(root, "test_host-" + gulliver::random_hex(8));
    const std::string models_dir = gulliver::join_path(work, "models");
    const std::string outside = gulliver::join_path(work, "outside");
    gulliver::make_dirs(models_dir);
    gulliver::make_dirs(outside);
    // Un dossier de données à lui, sur tous les systèmes : le dossier d'import d'un service installé n'est
    // jamais touché.
    const std::string data = gulliver::join_path(work, "data");
    gulliver::set_data_dir(data);
    check(gulliver::data_dir() == data, "the test works in a data folder of its own");
    const std::string inbox = gulliver::join_path(gulliver::data_dir(), "import");
    gulliver::make_dirs(inbox);

    {
        // Les profils : créer, lister, retrouver par id et par nom, renommer, noter l'usage, supprimer.
        gulliver::store db(gulliver::join_path(work, "profiles.db"));
        check(db.profiles().empty(), "a new database has no profile");

        gulliver::profile a;
        a.id = gulliver::random_hex(12);
        a.name = "barbidou";
        a.created = 1000;
        a.last_used = 1000;
        db.put_profile(a);

        gulliver::profile b;
        b.id = gulliver::random_hex(12);
        b.name = "essai";
        b.created = 2000;
        b.last_used = 2000;
        db.put_profile(b);

        auto all = db.profiles();
        check(all.size() == 2, "two profiles are listed");
        // Le plus récemment utilisé d'abord : essai (2000) avant barbidou (1000).
        check(all[0].name == "essai" && all[1].name == "barbidou", "profiles are listed most recently used first");

        gulliver::profile got;
        check(db.find_profile(a.id, got) && got.name == "barbidou" && got.created == 1000,
            "a profile is found by its id");
        check(db.find_profile_by_name("essai", got) && got.id == b.id, "a profile is found by its name");
        check(!db.find_profile("absent", got) && !db.find_profile_by_name("absent", got),
            "an absent profile is not found, by id or by name");

        // Renommer : même id, nouveau nom.
        a.name = "barbidou-pro";
        db.put_profile(a);
        check(db.find_profile(a.id, got) && got.name == "barbidou-pro", "a profile keeps its id when renamed");

        // Noter l'usage change l'ordre de la liste.
        db.touch_profile(a.id, 3000);
        all = db.profiles();
        check(all[0].id == a.id && all[0].last_used == 3000, "touching a profile moves it to the front");

        // Un nom déjà pris, sous un autre id, est refusé par l'unicité de la base.
        gulliver::profile clash;
        clash.id = gulliver::random_hex(12);
        clash.name = "essai";
        bool refused = false;
        try { db.put_profile(clash); } catch (const std::exception&) { refused = true; }
        check(refused, "a duplicate profile name is refused");

        db.remove_profile(a.id);
        check(!db.find_profile(a.id, got) && db.profiles().size() == 1, "a removed profile is gone");

        // La base reste saine après ces écritures.
        gulliver::store reopened(gulliver::join_path(work, "profiles.db"));
        check(reopened.profiles().size() == 1 && reopened.profiles()[0].name == "essai",
            "profiles survive reopening the database");
    }

    {
        // À largeur égale, un projecteur ne va qu'avec un décodeur de sa famille (Gulliver 4.30.0) : le projecteur de
        // Qwen3.5-4B et celui de Gemma 3 4B sortent tous deux 2 560 valeurs.
        auto file = [](const std::string& name, const std::string& arch, const std::string& type) {
            gulliver::pairing_file f;
            f.name = name;
            f.description = json::object();
            f.description["arch"] = arch;
            f.description["projection_width"] = 2560;
            if (type.empty()) f.description["reads_images"] = true;
            else
            {
                f.description["projector"] = true;
                f.description["projector_type"] = type;
            }
            return f;
        };
        const std::vector<gulliver::pairing_file> decoders = { file("gemma-3-4b-it-Q4_K_M.gguf", "gemma3", "") };
        const std::vector<gulliver::pairing_file> projectors = { file("Qwen3.5-4B-mmproj-F16.gguf", "clip", "qwen3vl_merger"),
            file("gemma-3-4b-it-mmproj-F16.gguf", "clip", "gemma3") };
        const std::vector<int> pairs = gulliver::pair_projectors(decoders, projectors);
        check(pairs.size() == 2 && pairs[0] == -1 && pairs[1] == 0,
            "a projector goes only with a decoder of its family, whatever the width they share");
    }

    const std::string secured = gulliver::join_path(data, "secure-test");
    std::string security_error;
    check(gulliver::secure_folder(secured, security_error), "secure_folder creates a private folder");
    struct stat secured_stat;
    check(lstat(secured.c_str(), &secured_stat) == 0 && (secured_stat.st_mode & 0777) == 0700,
          "secure_folder sets mode 0700");
    check(gulliver::owned_by_system(secured) && gulliver::folder_admin_only(secured),
          "owned folders pass the macOS ownership checks");
    const std::string writable = gulliver::join_path(secured, "writable");
    {
        std::ofstream out(writable);
        out << "test";
    }
    chmod(writable.c_str(), 0666);
    check(!gulliver::folder_admin_only(secured), "a group-writable or world-writable entry is rejected");
    check(!gulliver::secure_folder(secured, security_error, true) && !security_error.empty(),
          "secure_folder refuses unsafe content when asked to inspect it");
    chmod(writable.c_str(), 0600);
    check(gulliver::folder_admin_only(secured), "safe contents pass recursive permission checks");
    // Les ACL étendues de macOS : une entrée « allow » qui laisse un autre compte écrire est refusée,
    // une entrée « deny » est acceptée, et secure_folder vide l'ACL dangereuse de son dossier.
    const auto acl = [](const std::string& rule, const std::string& path)
    { return std::system(("/bin/chmod " + rule + " '" + path + "'").c_str()) == 0; };
    check(acl("+a 'everyone allow write'", writable), "the security test adds an ACL to a file");
    check(!gulliver::folder_admin_only(secured), "an ACL entry that lets everyone write a file is rejected");
    check(!gulliver::secure_folder(secured, security_error, true) && !security_error.empty(),
          "secure_folder refuses content with a writable ACL when asked to inspect it");
    check(acl("-N", writable) && gulliver::folder_admin_only(secured), "the file passes again without its ACL");
    check(acl("+a 'everyone deny delete'", secured) && gulliver::folder_admin_only(secured)
              && gulliver::owned_by_system(secured),
          "a deny ACL entry is accepted");
    check(acl("+a 'everyone allow add_file'", secured) && !gulliver::folder_admin_only(secured)
              && !gulliver::owned_by_system(secured),
          "an ACL entry that lets everyone add files to the folder is rejected");
    check(gulliver::secure_folder(secured, security_error) && gulliver::folder_admin_only(secured),
          "secure_folder removes a writable ACL from its folder");
    const std::string linked = gulliver::join_path(data, "linked-secure-test");
    check(make_symlink(secured, linked), "the security test creates a symlink");
    check(!gulliver::owned_by_system(linked) && !gulliver::folder_admin_only(linked),
          "security checks refuse a symlink");
    check(!gulliver::secure_folder(linked, security_error) && !gulliver::secure_folder(linked + "/", security_error),
          "secure_folder refuses a symlink, including one with a trailing slash");
    check(!gulliver::secure_folder("/", security_error), "secure_folder refuses the filesystem root");

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
    const std::string page = "<html><body><p>Gulliver &amp; the web</p></body></html>";
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
    gulliver::official_catalog_for_tests(official + "catalog.json");
    official_serve("model.bin", served);
    official_serve("other.bin", other);
    official_serve("projector.bin", projector);
    official_serve("model-2.bin", newer_tiny);

    {
        gulliver::store db(gulliver::join_path(work, "gulliver.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-10-30T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", served, base_url + "/official/model.bin"));
        list.push_back(catalog_entry("small", other, base_url + "/official/other.bin"));
        json vision = catalog_entry("vision", other, base_url + "/official/other.bin");
        json projector_file = json::object();
        projector_file["url"] = base_url + "/official/projector.bin";
        projector_file["size"] = static_cast<unsigned long long>(projector.size());
        projector_file["sha256"] = gulliver::sha256_hex(projector);
        json projector_files = json::array();
        projector_files.push_back(projector_file);
        json projector_entry = json::object();
        projector_entry["size"] = static_cast<unsigned long long>(projector.size());
        projector_entry["sha256"] = gulliver::sha256_hex(projector);
        projector_entry["files"] = projector_files;
        vision["projector"] = projector_entry;
        list.push_back(vision);
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        const std::string reserve_dir = gulliver::join_path(work, "reserve");
        gulliver::model_manager m(db, models_dir, reserve_dir);
        const std::string dir = m.models_dir();
        const std::string partial = gulliver::join_path(dir, "tiny.gguf.partial");
        const std::string target = gulliver::join_path(dir, "tiny.gguf");
        gulliver::user_error error;

        // Le fichier partiel, un lien vers un fichier extérieur : refusé, le fichier extérieur inchangé.
        const std::string victim = gulliver::join_path(outside, "victim.txt");
        write_text(victim, "kept");
        make_symlink(victim, partial);
        check(m.start_download("tiny", error), "a download starts");
        json d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_file",
            "a partial file that is a link is refused (" + d["state"].as_string() + ")");
        check(read_text(victim) == "kept", "the file the link leads to is unchanged");
        gulliver::remove_file(partial);

        // Le fichier partiel, second nom d'un fichier extérieur : refusé, le fichier inchangé.
        make_hard_link(victim, partial);
        check(m.start_download("tiny", error), "a download starts again");
        d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_file",
            "a partial file with a second name is refused");
        check(read_text(victim) == "kept", "the file of the second name is unchanged");
        gulliver::remove_file(partial);

        // Le nom final, un lien : le téléchargement remplace le lien, n'écrit jamais à travers lui.
        make_symlink(victim, target);
        check(m.start_download("tiny", error), "a download starts over a final name that is a link");
        d = wait_download(m, "tiny");
        gulliver::installed_model im;
        check(d["state"].as_string() == "done" && !gulliver::is_link(target) && read_text(target) == served,
            "the model replaces the link at its final name (" + d["state"].as_string() + ")");
        check(read_text(victim) == "kept", "the file the final link led to is unchanged");
        check(installed(db, "tiny", im) && im.file == target, "the model is recorded at its name");
        check(!gulliver::file_exists(partial), "no partial file is left");

        // Le dossier de modèles remplacé par un lien : le téléchargement n'écrit rien là où il mène.
        const std::string moved = dir + ".moved";
        const std::string elsewhere = gulliver::join_path(outside, "elsewhere");
        gulliver::make_dirs(elsewhere);
        gulliver::rename_file(dir, moved);
        make_symlink(elsewhere, dir);
        check(m.start_download("small", error), "a download starts into a folder replaced by a link");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "error", "the download into a folder replaced by a link fails");
        check(gulliver::list_dir(elsewhere).empty(), "nothing is written where the link leads");
        gulliver::remove_file(dir);
        gulliver::rename_file(moved, dir);

        // Un téléchargement annulé ne laisse aucun fichier partiel et revient ensuite en entier.
        gulliver::installed_model small;
        check(m.start_download("small", error), "a download of small starts");
        m.cancel_download("small");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "cancelled" || d["state"].as_string() == "done",
            "the cancelled download ends (" + d["state"].as_string() + ")");
        if (d["state"].as_string() == "cancelled")
            check(!gulliver::file_exists(gulliver::join_path(dir, "small.gguf.partial")), "its partial file is removed");
        check(m.start_download("small", error), "the download starts again");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && installed(db, "small", small), "the new download installs it");

        // Un fichier inconnu nommé d'après un modèle installé : installé sous un nom à lui, le modèle
        // inchangé.
        const gulliver_test::tiny_gguf tiny_model = gulliver_test::tiny_gguf_writer().build();
        const std::string unknown = gulliver::join_path(inbox, "tiny.gguf");
        tiny_model.save(unknown, tiny_model.bytes);
        const json u = m.import_path(unknown, inbox, false);
        gulliver::installed_model kept;
        check(u["imported"].size() == 1 && u["imported"][0].as_string() == "tiny-import",
            "an unknown file named after an installed model takes the suffix -import (" + u.dump() + ")");
        check(read_text(target) == served && installed(db, "tiny", kept) && kept.sha256 == gulliver::sha256_hex(served)
            && kept.source == "catalog", "the installed model keeps its file and its record");
        // Un autre fichier inconnu de ce nom prend le nom libre suivant.
        std::vector<unsigned char> changed = tiny_model.bytes;
        changed.back() = static_cast<unsigned char>(changed.back() ^ 0x5a);
        tiny_model.save(unknown, changed);
        const json u2 = m.import_path(unknown, inbox, false);
        check(u2["imported"].size() == 1 && u2["imported"][0].as_string() == "tiny-import-2",
            "a second unknown file of that name takes the next free name (" + u2.dump() + ")");
        gulliver::remove_file(unknown);

        const int network_before_restore = official_count("other.bin");
        std::string remove_error;
        const std::string reserved = gulliver::join_path(reserve_dir, gulliver::sha256_hex(other) + ".gguf");
        struct stat reserve_stat = {};
        check(m.remove("small", remove_error) && !gulliver::file_exists(small.file)
            && read_text(reserved) == other && ::stat(reserved.c_str(), &reserve_stat) == 0
            && (reserve_stat.st_mode & 0777) == 0600 && !installed(db, "small", small),
            "removing a model keeps its verified file in the local reserve");
        check(m.start_download("small", error), "a reserved model can be reinstalled");
        d = wait_download(m, "small");
        check(d["state"].as_string() == "done" && read_text(small.file) == other
            && official_count("other.bin") == network_before_restore,
            "a reserved model is restored without a network request");
        check(m.remove("small", remove_error), "a restored model can be reserved again");
        check(!gulliver::http_cancelled() && m.start_download("small", error, true),
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
        check(!gulliver::http_cancelled() && m.start_download("small", error, true),
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
        gulliver::installed_model vision_model;
        check(d["state"].as_string() == "done" && installed(db, "vision", vision_model)
            && read_text(vision_model.file) == other && read_text(vision_model.projector) == projector,
            "the model and projector are installed together");
        check(m.remove("vision", remove_error), "a model and projector can be moved to reserve");
        const std::string vision_reserved = gulliver::join_path(reserve_dir,
            gulliver::sha256_hex(other) + ".gguf");
        const std::string projector_reserved = gulliver::join_path(reserve_dir,
            gulliver::sha256_hex(projector) + ".gguf");
        check(read_text(vision_reserved) == other && read_text(projector_reserved) == projector,
            "both model and projector are kept in the local reserve");
        const int network_before_vision_restore = official_count("other.bin") + official_count("projector.bin");
        gulliver::http_cancel_all("offline mode");
        check(m.start_download("vision", error, true), "a model with its projector can be restored offline");
        d = wait_download(m, "vision");
        gulliver::http_restore();
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
        progress["sha256"] = gulliver::sha256_hex(other);
        progress["projector_sha256"] = gulliver::sha256_hex(projector);
        done.push_back(static_cast<unsigned long long>(decoder_prefix.size()));
        done.push_back(static_cast<unsigned long long>(projector_prefix.size()));
        progress["done"] = done;
        db.set_download_state("vision", progress);
        check(gulliver::remove_file(projector_reserved), "the projector can be removed from the reserve for the test");
        const int network_before_missing = official_count("other.bin") + official_count("projector.bin");
        check(m.start_download("vision", error, true), "a local-only restoration with a missing projector starts");
        d = wait_download(m, "vision");
        const std::vector<std::string> after_missing = gulliver::list_dir(dir);
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
        check(reserve["used"].as_int() == 0 && gulliver::file_exists(vision_reserved) && reserve["shared"].as_int() > 0,
            "a limit of 0 removes the copies of removed models and keeps those of installed ones");
        const std::string old_copy = gulliver::join_path(reserve_dir, gulliver::sha256_hex("old reserve copy") + ".gguf");
        const std::string new_copy = gulliver::join_path(reserve_dir, gulliver::sha256_hex("new reserve copy") + ".gguf");
        write_text(old_copy, "old reserve copy");
        write_text(new_copy, "new reserve copy");
        struct timeval long_ago[2] = { { 1000000, 0 }, { 1000000, 0 } };
        utimes(old_copy.c_str(), long_ago);
        m.reserve_limit([]() { return static_cast<uint64_t>(16); });
        m.trim_reserve();
        check(!gulliver::file_exists(old_copy) && gulliver::file_exists(new_copy),
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
        gulliver::store db(gulliver::join_path(work, "gulliver.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-10-31T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", newer_tiny, base_url + "/official/model-2.bin"));
        list.push_back(catalog_entry("small", other, base_url + "/official/other.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        gulliver::model_manager m(db, models_dir);
        const std::string target = gulliver::join_path(m.models_dir(), "tiny.gguf");
        std::atomic<bool> allow{false};
        std::atomic<int> asked{0};
        m.on_replace([&](const std::string& id, const std::function<bool()>& place) {
            if (id == "tiny") ++asked;
            return allow && place();
        });
        gulliver::user_error error;
        check(m.start_download("tiny", error), "the download of a newer version of an installed model starts");
        json d = wait_download(m, "tiny");
        check(d["state"].as_string() == "error" && d["code"].as_string() == "download_install" && asked == 1,
            "a replacement refused ends the download in error (" + d.dump() + ")");
        gulliver::installed_model im;
        check(read_text(target) == served && installed(db, "tiny", im) && im.sha256 == gulliver::sha256_hex(served),
            "the model keeps its file and its record when its replacement is refused");
        allow = true;
        check(m.start_download("tiny", error), "the download starts again");
        d = wait_download(m, "tiny");
        check(d["state"].as_string() == "done" && asked == 2, "the next replacement completes it (" + d.dump() + ")");
        check(read_text(target) == newer_tiny && installed(db, "tiny", im)
            && im.sha256 == gulliver::sha256_hex(newer_tiny), "the newer version takes the name and the record of tiny");
        check(!gulliver::file_exists(target + ".partial"), "no partial file is left");
        m.stop();
    }

    {
        // Les modèles fournis avec Gulliver, et les imports du même contenu.
        gulliver::store db(gulliver::join_path(work, "gulliver-2.db"));
        const std::string builtin = gulliver::join_path(work, "builtin");
        gulliver::make_dirs(builtin);
        const std::string own = blob(150000, 'q');
        write_text(gulliver::join_path(builtin, "base.gguf"), own);
        json entry = json::object();
        entry["id"] = "base-gulliver";
        entry["name"] = "Base";
        entry["file"] = "base.gguf";
        entry["sha256"] = gulliver::sha256_hex(own);
        // Le fichier du catalogue à partir duquel le build l'a fait.
        const std::string source = blob(140000, 'o');
        entry["source_sha256"] = gulliver::sha256_hex(source);
        json listed = json::array();
        listed.push_back(entry);
        json manifest = json::object();
        manifest["models"] = listed;
        write_text(gulliver::join_path(builtin, gulliver::builtin_manifest_name), manifest.dump());
        // Le catalogue en usage connaît un autre contenu sous l'identifiant du modèle fourni avec Gulliver.
        const std::string newer = blob(160000, 'z');
        json catalog = json::object();
        catalog["updated"] = "2026-10-30T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("base-gulliver", newer, base_url + "/none.bin"));
        list.push_back(catalog_entry("base", source, base_url + "/source.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        const std::string models2 = gulliver::join_path(work, "models-2");
        gulliver::model_manager m(db, models2);
        std::vector<gulliver::installed_model> removed;
        m.register_builtin(builtin, removed);

        // L'entrée du fichier à partir duquel le modèle d'Gulliver a été fait : le modèle d'Gulliver en
        // tient lieu.
        const json view = m.overview("en");
        bool listed_source = false, listed_builtin = false;
        for (const auto& x : view["models"].elements())
        {
            listed_source = listed_source || x["id"].as_string() == "base";
            listed_builtin = listed_builtin || x["id"].as_string() == "base-gulliver";
        }
        check(!listed_source && listed_builtin, "the catalog entry a model of Gulliver stands for is not listed");
        gulliver::user_error carried;
        check(!m.start_download("base", carried) && carried.code == "builtin_installed",
            "the download of the file a model of Gulliver carries is refused");

        // Le même contenu dans le dossier d'import : pas copié, pas enregistré à nouveau, retiré du dossier.
        const std::string copy = gulliver::join_path(inbox, "base-gulliver.gguf");
        write_text(copy, own);
        const json r = m.import_path(copy, inbox, true);
        gulliver::installed_model im;
        check(r["imported"].size() == 0 && r["already"].size() == 1 && r["already"][0].as_string() == "base-gulliver",
            "a copy of a model that comes with Gulliver is installed already");
        check(gulliver::list_dir(m.models_dir()).empty(), "no second copy enters the models folder");
        check(installed(db, "base-gulliver", im) && im.source == "builtin", "the model stays the one of Gulliver");
        check(!gulliver::file_exists(copy), "the copy leaves the import folder");

        // Un autre contenu sous son identifiant : installé sous son propre nom, jamais par-dessus.
        const std::string variant = gulliver::join_path(inbox, "variant.gguf");
        write_text(variant, newer);
        const json v = m.import_path(variant, inbox, false);
        gulliver::installed_model renamed;
        check(v["imported"].size() == 1 && v["imported"][0].as_string() == "base-gulliver-import",
            "a file with the identifier of a model of Gulliver takes the suffix -import");
        check(installed(db, "base-gulliver", im) && im.source == "builtin", "the model of Gulliver keeps its entry");
        check(installed(db, "base-gulliver-import", renamed) && renamed.source == "catalog",
            "the other file has an entry of its own");
        check(gulliver::file_exists(variant), "a file imported without move stays in place");

        // Un téléchargement de son identifiant est refusé.
        gulliver::user_error error;
        check(!m.start_download("base-gulliver", error) && error.code == "builtin_installed",
            "a download of a model that comes with Gulliver is refused");

        // Le dossier d'import se lit sans aucun lien et par un seul nom.
        const std::string secret = gulliver::join_path(outside, "secret.gguf");
        write_text(secret, blob(1000, 's'));
        const std::string linked = gulliver::join_path(inbox, "linked.gguf");
        make_symlink(secret, linked);
        const json l = m.import_path(linked, inbox, true);
        check(l["imported"].size() == 0 && l["errors"].size() == 1, "a link in the import folder is refused");
        const std::string hard = gulliver::join_path(inbox, "hard.gguf");
        make_hard_link(secret, hard);
        const json h = m.import_path(hard, inbox, true);
        check(h["imported"].size() == 0 && h["errors"].size() == 1, "a second name in the import folder is refused");
        check(gulliver::file_exists(secret), "the file they lead to stays");
        gulliver::remove_file(linked);
        gulliver::remove_file(hard);
        // Un dossier du dossier d'import remplacé par un lien : rien n'est lu à travers lui.
        const std::string sub = gulliver::join_path(inbox, "models");
        make_symlink(outside, sub);
        const json s = m.import_path(inbox, inbox, false);
        check(s["imported"].size() == 0, "the models subfolder that is a link is not read");
        gulliver::remove_file(sub);
        std::string why;
        gulliver::link_free_file f;
        check(!f.open(gulliver::join_path(gulliver::join_path(work, "via"), "secret.gguf"), work, why)
            && (why == "missing" || why == "link"), "a missing entry is reported");
        make_symlink(outside, gulliver::join_path(work, "via"));
        check(!f.open(gulliver::join_path(gulliver::join_path(work, "via"), "secret.gguf"), work, why) && why == "link",
            "a folder that is a link stops a link-free opening");

        // Un paquet hors ligne : seulement les fichiers de son catalogue, avec leurs tailles et empreintes.
        const std::string bundle = gulliver::join_path(work, "bundle");
        gulliver::make_dirs(gulliver::join_path(bundle, "models"));
        const std::string listed_content = blob(120000, 'm');
        json bundle_catalog = json::object();
        json models = json::array();
        models.push_back(catalog_entry("bundled", listed_content, "https://example.org/files/bundled-q4.gguf"));
        models.push_back(catalog_entry("damaged", blob(50000, 'd'), "https://example.org/files/damaged.gguf"));
        bundle_catalog["models"] = models;
        write_text(gulliver::join_path(gulliver::join_path(bundle, "models"), "bundled-q4.gguf"), listed_content);
        write_text(gulliver::join_path(gulliver::join_path(bundle, "models"), "damaged.gguf"), blob(50000, 'x'));
        write_text(gulliver::join_path(gulliver::join_path(bundle, "models"), "unlisted.gguf"), blob(1000, 'u'));
        const json b = m.import_bundle(bundle, bundle_catalog);
        check(b["imported"].size() == 1 && b["imported"][0].as_string() == "bundled", "the listed model is imported");
        check(b["errors"].size() == 1, "the file that does not match the catalog is refused");
        bool leftover = false;
        for (const auto& n : gulliver::list_dir(m.models_dir()))
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
        check(gulliver::http_get_range(base_url + "/model.bin", 1000, 1999, keep, error)
            && got == served.substr(1000, 1000), "a part of a file arrives as asked (" + error + ")");
        got.clear();
        check(gulliver::http_get_range(base_url + "/whole.bin", 0, 999, keep, error) && got == served.substr(0, 1000),
            "a server that ignores the range of a request from 0 gives the bytes asked for alone (" + error + ")");
        got.clear();
        check(!gulliver::http_get_range(base_url + "/whole.bin", 1000, 1999, keep, error) && got.empty(),
            "a server that ignores the range of a request from elsewhere is refused");
    }

    {
        // Les adresses des messages ne portent pas leur requête (une clé de recherche peut s'y trouver).
        check(gulliver::without_queries("https://example.org/search?q=X&key=Y: status 500")
            == "https://example.org/search: status 500", "the queries of the addresses stay out of the messages");
        check(gulliver::without_queries("'https://example.org/a?x=1' \"https://example.org/b?y=2\"\r\n"
                "https://example.org/c?z=3\tend")
            == "'https://example.org/a' \"https://example.org/b\"\r\nhttps://example.org/c\tend",
            "queries are removed from quoted addresses and lines without losing their delimiters");
        // Le journal écrit ses lignes telles qu'elles sont données.
        const std::string log_path = gulliver::join_path(work, "logs/service.log");
        gulliver::make_dirs(gulliver::join_path(work, "logs"));
        gulliver::log_file::instance().open(log_path);
        gulliver::log_warning("models: a line of the log");
        check(read_text(log_path).find("[warning] models: a line of the log") != std::string::npos,
            "the log writes its lines as they are given");
    }

    {
        /*
            Les téléchargements depuis les adresses du catalogue : un modèle, un modèle de deux parties, un
            fichier corrompu qui est récupéré de nouveau, une empreinte qui échoue encore, une adresse qui
            n'a pas le fichier, puis une adresse qui le donne à une autre taille.
        */
        gulliver::store db(gulliver::join_path(work, "gulliver-3.db"));
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
        gulliver::model_manager m(db, gulliver::join_path(work, "models-3"));
        gulliver::user_error started;
        gulliver::installed_model im;
        auto complete = [&](const json& d, const std::string& id, const std::string& content) {
            return d["state"].as_string() == "done" && installed(db, id, im) && read_text(im.file) == content
                && im.sha256 == gulliver::sha256_hex(content) && d["received"].as_int(-1) == d["total"].as_int(-2);
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
            "gives" && !gulliver::file_exists(gulliver::join_path(m.models_dir(), "absent.gguf")),
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
        gulliver::http_bounds_for_tests(1, 2);
        auto answers = [&]() {
            std::string alive, e;
            return gulliver::http_get(base_url + "/alive", alive, e, 64) && alive == "alive";
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
        const bool from_page = gulliver::http_get_range(official + "model.bin", 0, 999, keep, error);
        const std::string page_error = error;
        official_fault = stall;
        const bool from_stall = gulliver::http_get_range(official + "model.bin", 0, 999, keep, error);
        const std::string stall_error = error;
        official_fault = no_fault;
        const bool from_reset = gulliver::http_get_range("http://127.0.0.1:" + std::to_string(port + 1)
            + "/official/model.bin", 1000, 1999, keep, error);
        // Un récepteur qui lève une exception : l'exception devient l'erreur du téléchargement.
        try
        {
            gulliver::http_get_range(official + "model.bin", 0, 999, [](const char*, size_t) -> bool {
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
        gulliver::store db(gulliver::join_path(work, "gulliver-4.db"));
        json catalog = json::object();
        catalog["updated"] = "2026-11-01T00:00:00Z";
        json list = json::array();
        list.push_back(catalog_entry("tiny", served, official + "model.bin"));
        catalog["models"] = list;
        db.set_value("catalog", catalog.dump());
        {
            gulliver::model_manager m(db, gulliver::join_path(work, "models-4"));
            gulliver::user_error started;
            official_fault = html_200;
            check(m.start_download("tiny", started), "the download of a model whose address is a web page starts");
            json d = wait_download(m, "tiny");
            official_fault = no_fault;
            const std::string partial = gulliver::join_path(m.models_dir(), "tiny.gguf.partial");
            const std::string blocked = official + "model.bin cannot be downloaded: " + page_text;
            check(d["state"].as_string() == "error" && d["code"].as_string() == "download_failed"
                && d["error"].as_string() == blocked && d["args"]["e"].as_string() == blocked
                && read_text(partial).find("Access denied") == std::string::npos && answers(),
                "a model whose address is a web page fails with its message, its partial file free of the page ("
                + d.dump() + ")");
            check(m.start_download("tiny", started), "the download of the model starts again");
            d = wait_download(m, "tiny");
            gulliver::installed_model im;
            check(d["state"].as_string() == "done" && installed(db, "tiny", im) && read_text(im.file) == served
                && im.sha256 == gulliver::sha256_hex(served) && answers(),
                "a model comes from its address once the page is gone (" + d.dump() + ")");
            std::string refreshed;
            const bool refreshed_ok = m.refresh_catalog(refreshed);
            check(!refreshed_ok && refreshed == "the catalog cannot be downloaded from " + official + "catalog.json: "
                "the request is answered with the status 404" && answers(),
                "a catalog its address does not have fails with the address and the answer (" + refreshed + ")");
            m.stop();
        }
        gulliver::http_bounds_for_tests(0, 0);
        resetting.stop();
    }

    {
        /*
            Un modèle de huit parties, dont une compressée, dont la dernière n'est pas encore servie : le
            téléchargement échoue avec son adresse, puis reprend avec cette seule partie ; le catalogue sans
            signature, de son adresse ou d'un dossier ; et un téléchargement annulé pendant qu'il vient de
            son adresse.
        */
        gulliver::store db(gulliver::join_path(work, "gulliver-5.db"));
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
        gulliver::model_manager m(db, gulliver::join_path(work, "models-5"));
        gulliver::user_error started;
        gulliver::installed_model im;

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
            && im.sha256 == gulliver::sha256_hex(whole) && d["received"].as_int(-1) == d["total"].as_int(-2)
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
        gulliver::store cdb(gulliver::join_path(work, "gulliver-6.db"));
        gulliver::model_manager cm(cdb, gulliver::join_path(work, "models-6"));
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
        const std::string drive = gulliver::join_path(work, "drive");
        gulliver::make_dirs(drive);
        json drive_catalog = served_catalog;
        drive_catalog["updated"] = "2026-11-03T00:00:00Z";
        json drive_models = json::array();
        drive_models.push_back(catalog_entry("tiny", served, official + "model.bin"));
        drive_catalog["models"] = drive_models;
        write_text(gulliver::join_path(drive, "catalog.json"), drive_catalog.dump());
        const json imported = cm.import_catalog(drive, std::string());
        status = cm.catalog_status();
        check(!imported.contains("error") && imported["models"].as_int() == 1
            && status["updated"].as_string() == "2026-11-03T00:00:00Z",
            "the catalog.json of a folder is installed without signature (" + imported.dump() + ")");
        const json empty_drive = cm.import_catalog(gulliver::join_path(work, "no-drive"), std::string());
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
        gulliver::user_error local_busy;
        check(!m.start_download("slow", local_busy, true) && local_busy.code == "download_busy",
            "a local-only request does not claim that a running network download is a local restoration");
        const auto cancelled_at = std::chrono::steady_clock::now();
        m.cancel_download("slow");
        d = wait_download(m, "slow");
        const double cancel_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()
            - cancelled_at).count();
        check(received > 0 && received < static_cast<long long>(slow.size()) && d["state"].as_string() == "cancelled"
            && cancel_seconds < 5 && official_count("slow.bin") == 1 && !installed(db, "slow", im)
            && !gulliver::file_exists(gulliver::join_path(m.models_dir(), "slow.gguf.partial"))
            && !db.download_state("slow").is_object(), "a download cancelled while it comes from its address "
            "ends in " + std::to_string(static_cast<int>(cancel_seconds)) + " s, its partial file and progress "
            "removed (" + d.dump() + ")");
        m.stop();
    }

    {
        // Les réglages d'une version antérieure : lus sans erreur, réécrits sans ce qui manque à cette
        // version.
        const std::string folder = gulliver::join_path(work, "settings");
        gulliver::make_dirs(folder);
        const std::string settings_file = gulliver::join_path(folder, "settings.json");
        const std::string key_file = gulliver::join_path(folder, "web_search.secret");
        write_text(settings_file, "{\"port\": 9123, \"web_search\": \"brave\", \"web_search_url\": "
            "\"https://searx.example.org\", \"web_search_key_set\": true, \"web_search_key\": \"BSA-by-hand-42\", "
            "\"public_link\": \"https://files.example.invalid/publink/show?code=0123456789\"}");
        write_text(key_file, "BSA-secret-0123456789");
        {
            gulliver::settings_store store(settings_file);
            const std::string text = read_text(settings_file), copy = read_text(settings_file + ".bak");
            check(store.get().port == 9123 && store.get().web_search == "wikipedia" && !holds_folded(text, "example.invalid")
                && !holds_folded(copy, "example.invalid") && text.find("web_search_url") == std::string::npos
                && text.find("web_search_key") == std::string::npos && text.find("public_link") == std::string::npos
                && text.find("BSA-") == std::string::npos && !gulliver::file_exists(key_file),
                "the settings of an earlier version are read, and written again without what this version lacks");
        }
        gulliver::host_settings s;
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
        const std::string path = gulliver::join_path(work, "damaged.db");
        {
            gulliver::store db(path);
            for (int i = 0; i < 200; ++i) db.set_value("key-" + std::to_string(i), blob(2000, static_cast<char>(i)));
        }
        for (const char* part : { "-wal", "-shm" }) gulliver::remove_file(path + part);
        std::string bytes = read_text(path);
        check(bytes.size() > 8 * 4096, "the database spans several pages");
        // Sa première page, l'en-tête et le schéma, reste ; les pages de ses tables sont effacées.
        for (size_t i = 4096; i < bytes.size(); ++i) bytes[i] = 0;
        write_text(path, bytes);
        bool malformed = false;
        try
        {
            gulliver::store damaged(path);
        }
        catch (const std::exception& e)
        {
            malformed = std::string(e.what()).find("malformed") != std::string::npos;
        }
        check(malformed, "a damaged database fails its opening as malformed");
    }

    {
        // Un fichier écrit de façon atomique remplace celui en place et ne laisse aucun fichier temporaire.
        const std::string file = gulliver::join_path(work, "atomic.txt");
        check(gulliver::write_file_atomic(file, "first") && gulliver::write_file_atomic(file, "second")
            && read_text(file) == "second", "a file written atomically replaces its content");
        bool temporary = false;
        for (const auto& n : gulliver::list_dir(work)) temporary = temporary || n.find("atomic.txt.tmp-") == 0;
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
            if (!gulliver::public_address(a)) wrong += std::string(" ") + a;
        check(wrong.empty(), "the addresses beside the ranges that are not public are public" + wrong);
        wrong.clear();
        for (const char* a : private_addresses)
            if (gulliver::public_address(a)) wrong += std::string(" ") + a;
        check(wrong.empty(), "the addresses of the ranges that are not public are not" + wrong);
        check(!gulliver::public_address("example.org") && !gulliver::public_address("127.1")
            && !gulliver::public_address(""), "a text that is no address is not public");

        // Les adresses refusées avant toute connexion : 400 pour une adresse invalide, 403 pour une
        // adresse locale ou privée.
        gulliver::host_settings web;
        auto fetch = [&](const std::string& url, const gulliver::host_settings& s, const std::string& language) {
            json request = json::object();
            request["url"] = url;
            if (!language.empty()) request["language"] = language;
            return gulliver::web_fetch(request, s);
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
            const gulliver::web_reply reply = fetch(r.url, web, "");
            check(reply.status == r.status, "the address " + r.url + " is refused with " + std::to_string(r.status)
                + " (" + std::to_string(reply.status) + " " + reply.code + ": " + reply.message + ")");
        }
        const gulliver::web_reply local = fetch("http://[::ffff:127.0.0.1]/", web, "");
        check(local.code == "address_refused" && local.message == "the address of [::ffff:127.0.0.1] is local or "
            "private, which the web access of Gulliver does not read",
            "a refusal names the host (" + local.message + ")");

        // Le service hors ligne et l'accès web désactivé refusent les deux routes ; la capacité suit.
        gulliver::host_settings offline = web, off = web, none = web;
        offline.offline = true;
        off.web_access = false;
        none.web_search = "none";
        json search_request = json::object();
        search_request["query"] = "Einstein relativit\xc3\xa9";
        gulliver::web_reply reply = fetch("https://example.org/", offline, "");
        check(reply.status == 403 && reply.code == "offline", "a page is refused offline (" + reply.message + ")");
        reply = gulliver::web_search(search_request, offline);
        check(reply.status == 403 && reply.code == "offline", "a search is refused offline");
        reply = fetch("https://example.org/", off, "");
        check(reply.status == 403 && reply.code == "web_access_off",
            "a page is refused with the web access turned off");
        reply = gulliver::web_search(search_request, off);
        check(reply.status == 403 && reply.code == "web_access_off", "a search is refused with the web access off");
        reply = gulliver::web_search(search_request, none);
        check(reply.status == 403 && reply.code == "no_search_provider" && reply.message == "no search provider is set",
            "a search without provider is refused");
        check(gulliver::web_capability(web).dump() == "{\"available\":true,\"search\":\"wikipedia\"}"
            && gulliver::web_capability(offline).dump() == "{\"available\":false,\"search\":\"none\"}"
            && gulliver::web_capability(off).dump() == "{\"available\":false,\"search\":\"none\"}"
            && gulliver::web_capability(none).dump() == "{\"available\":true,\"search\":\"none\"}"
            && gulliver::web_search_provider(web) == "wikipedia" && gulliver::web_search_provider(none) == "none",
            "the capability gives the web access and its provider of search, wikipedia or none");

        // Les pages du serveur local, le loopback permis pour les tests.
        gulliver::web_allow_loopback_for_tests(true);
        reply = fetch(base_url + "/web/page.html", web, "");
        const json& b = reply.body;
        check(reply.status == 200 && b["status"].as_int() == 200 && b["content_type"].as_string() == "text/html"
            && b["charset"].as_string() == "utf-8" && !b["truncated"].as_bool()
            && b["size"].as_int() == static_cast<long long>(page.size())
            && gulliver::base64_decode(b["data"].as_string()) == page && b["url"].as_string() == base_url
            + "/web/page.html" && b["final_url"].as_string() == base_url + "/web/page.html",
            "a page is read with its status, its type and its bytes (" + reply.message + ")");
        reply = fetch(base_url + "/web/large.txt", web, "");
        check(reply.status == 200 && reply.body["truncated"].as_bool() && reply.body["size"].as_int() == (2 << 20)
            && gulliver::base64_decode(reply.body["data"].as_string()) == large_text.substr(0, 2u << 20),
            "a text is cut at 2 MB (" + reply.message + ")");
        reply = fetch(base_url + "/web/image.png", web, "");
        check(reply.status == 415 && reply.code == "unsupported_type" && reply.message.find("image/png")
            != std::string::npos, "a type the web access does not read is refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/small.pdf", web, "");
        check(reply.status == 200 && reply.body["content_type"].as_string() == "application/pdf"
            && gulliver::base64_decode(reply.body["data"].as_string()) == "%PDF-1.4\n%%EOF\n", "a PDF file is read");
        reply = fetch(base_url + "/web/large.pdf", web, "");
        check(reply.status == 413 && reply.code == "too_large", "a PDF file announced above 20 MB is refused ("
            + reply.message + ")");
        reply = fetch(base_url + "/web/stream.pdf", web, "");
        check(reply.status == 413 && reply.code == "too_large", "a PDF file that passes 20 MB while it is read is "
            "refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/missing", web, "");
        check(reply.status == 200 && reply.body["status"].as_int() == 404
            && gulliver::base64_decode(reply.body["data"].as_string()) == "<p>no such page</p>",
            "a page that answers 404 is given with its status and its body");
        reply = fetch(base_url + "/web/to-private", web, "");
        check(reply.status == 403 && reply.code == "address_refused" && reply.message.find("192.168.1.10")
            != std::string::npos, "a redirection to a private address is refused (" + reply.message + ")");
        reply = fetch(base_url + "/web/relative", web, "");
        check(reply.status == 200 && reply.body["final_url"].as_string() == base_url + "/web/page.html?from=relative"
            && gulliver::base64_decode(reply.body["data"].as_string()) == page,
            "a relative redirection is followed to its address (" + reply.message + ")");
        reply = fetch(base_url + "/web/loop", web, "");
        check(reply.status == 502 && reply.code == "too_many_redirects",
            "redirections are followed five times at most");
        reply = fetch(base_url + "/web/headers", web, "fr-FR");
        check(reply.status == 200 && gulliver::base64_decode(reply.body["data"].as_string()) == std::string("Gulliver/")
            + GULLIVER_VERSION + " (+https://github.com/Cydral/Dlib-Transformer-extensions)\nfr-FR\n",
            "a page receives the User-Agent of Gulliver, the language asked and no cookie");
        gulliver::http_cancel_all("the service stops");
        reply = fetch(base_url + "/web/page.html", web, "");
        check(reply.status == 503 && reply.code == "cancelled", "a service that stops ends the reading of pages");
        gulliver::http_restore();

        // La recherche de Wikipédia, servie par le serveur local.
        gulliver::web_provider_origin_for_tests(base_url);
        search_request["count"] = 5;
        search_request["language"] = "fr";
        reply = gulliver::web_search(search_request, web);
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
                && seen_agent == std::string("Gulliver/") + GULLIVER_VERSION
                + " (https://github.com/Cydral/Dlib-Transformer-extensions; cydraltechnology@gmail.com)",
                "Wikipedia receives the query, the count and the User-Agent with a contact (" + seen_agent + ")");
        }
        search_request["query"] = "too many";
        reply = gulliver::web_search(search_request, web);
        check(reply.status == 429 && reply.code == "rate_limited" && reply.retry_after == 30
            && reply.message.find("Wikipedia limits the rate") != std::string::npos,
            "a limit of the rate of Wikipedia is reported with its delay (" + reply.message + ")");
        json invalid = json::object();
        invalid["query"] = "   ";
        const bool empty_refused = gulliver::web_search(invalid, web).code == "invalid_query";
        invalid["query"] = std::string(401, 'q');
        const bool long_refused = gulliver::web_search(invalid, web).code == "invalid_query";
        std::string four_hundred;
        for (int i = 0; i < 400; ++i) four_hundred += "\xc3\xa9";
        invalid["query"] = four_hundred;
        const bool characters_counted = gulliver::web_search(invalid, web).status == 200;
        invalid["query"] = "ok";
        invalid["count"] = 11;
        const bool count_refused = gulliver::web_search(invalid, web).code == "invalid_count";
        invalid["count"] = 2;
        invalid["language"] = "french";
        const bool language_refused = gulliver::web_search(invalid, web).code == "invalid_language";
        check(empty_refused && long_refused && characters_counted && count_refused && language_refused,
            "a query of 1 to 400 characters, a count of 1 to 10 and a code of language are required");

        gulliver::web_provider_origin_for_tests("");
        gulliver::web_allow_loopback_for_tests(false);

        // Le journal nomme les hôtes lus et le fournisseur, jamais un chemin ni une requête.
        bool leaked = false, read_line = false, search_line = false;
        for (const auto& line : gulliver::log_file::instance().recent())
        {
            leaked = leaked || line.find("relativit") != std::string::npos || line.find("/web/") != std::string::npos
                || line.find("q=") != std::string::npos;
            read_line = read_line || line.find("web: 127.0.0.1 read (200, ") != std::string::npos;
            search_line = search_line || line.find("web: search through wikipedia (2 results)") != std::string::npos;
        }
        check(!leaked && read_line && search_line, "the log names the hosts and the provider, never a path or a "
            "query");
    }

    {
        // Le relais (repris de l'original, Gulliver 1.0.23) : son secret dans les réglages, la restauration des
        // pseudonymes, la mémoire de la pseudonymisation, et les requêtes de ses trois fournisseurs à un serveur
        // local qui répond comme chacun.
        const std::string folder = gulliver::join_path(work, "relay-settings");
        gulliver::make_dirs(folder);
        const std::string file = gulliver::join_path(folder, "settings.json");
        const std::string secret_file = gulliver::join_path(folder, "relay.secret");
        const std::string key = "sk-ant-test-0123456789";
        {
            gulliver::settings_store store(file);
            store.update([&](gulliver::host_settings& s) {
                s.relay.enabled = true;
                s.relay.provider = "claude";
                s.relay.model = "claude-opus-5-5";
                s.relay.secret = key;
            });
        }
        const std::string text = read_text(file);
        bool reread = false;
        {
            gulliver::settings_store store(file);
            reread = store.get().relay.secret == key && store.get().relay.to_json()["secret"].as_string() == "********";
        }
        // Sur le Mac, relay.secret garde la clé telle quelle, lisible par le compte du service seul (0600).
        struct stat secret_stat;
        const bool sealed = ::stat(secret_file.c_str(), &secret_stat) == 0 && (secret_stat.st_mode & 0077) == 0;
        check(text.find(key) == std::string::npos && text.find("\"secret\": \"********\"") != std::string::npos
            && read_text(file + ".bak").find(key) == std::string::npos && gulliver::file_exists(secret_file) && reread
            && sealed, "the key of the relay stays out of settings.json and its copy, kept in relay.secret and read "
            "again");
        json edited = json::parse_or_null(text);
        edited["relay"]["provider"] = "ollama";
        write_text(file, edited.dump());
        bool elsewhere = false;
        {
            gulliver::settings_store store(file);
            elsewhere = store.get().relay.secret.empty();
            store.update([](gulliver::host_settings& s) { s.relay.enabled = false; });
        }
        check(elsewhere && !gulliver::file_exists(secret_file),
            "a settings.json that names another provider reads no key, and relay.secret goes with the key");

        // Les valeurs reviennent dans une réponse coupée n'importe où, aux limites des mots seulement.
        std::map<std::string, std::string> values;
        values["Xoydoeh Nuggwdfsorn"] = "Jean Dupont";
        values["Qbspm"] = "Paris";
        values["[[PERSON:Xoydoeh Nuggwdfsorn#EKR]]"] = "Jean Dupont";
        {
            gulliver::stream_restorer r(values);
            std::string out;
            for (const char* chunk : { "Bonjour Xoy", "doeh Nugg", "wdfsorn, de Qbs", "pm et Qbspmx. ",
                     "M. Nuggwdfsorn", " et [[PERSON:Xoydoeh", " Nuggwdfsorn#EKR]], d\xE2\x80\x99Xoydoeh" })
                out += r.feed(chunk);
            out += r.finish();
            check(out == "Bonjour Jean Dupont, de Paris et Qbspmx. M. Dupont et Jean Dupont, d\xE2\x80\x99Jean",
                "the substitutes of an answer cut anywhere come back as their values, their words alone too ("
                + out + ")");
            std::map<std::string, std::string> both;
            both["Mxrtin"] = "Durand";
            both["Jxan Mxrtin"] = "Paul Dupont";
            gulliver::stream_restorer w(both);
            const std::string kept = w.feed("Ask Mxrtin, then Jxan Mxrtin.") + w.finish();
            check(kept == "Ask Durand, then Paul Dupont.", "a whole substitute keeps its value when it is also a word "
                "of another one (" + kept + ")");
        }

        // Un serveur local qui répond comme Pseudalis et comme les fournisseurs.
        struct relay_mock
        {
            std::mutex m;
            int anonymized = 0;
            std::string mode = "ok";
            json body;
            httplib::Headers headers;
        } mock;
        const std::vector<std::pair<std::string, std::string>> people = { { "Jean Dupont", "Xoydoeh Nuggwdfsorn" },
            { "Paris", "Qbspm" }, { "Marie Curie", "Lsyiq Bfksz" }, { "Lyon", "Wpzq" } };
        httplib::Server relay_server;
        relay_server.Post("/v1/text/anonymize", [&](const httplib::Request& req, httplib::Response& res) {
            // Comme Pseudalis 3.1.0 (fork pour macOS) : avec session, des pseudonymes seuls et leurs valeurs.
            const json in = json::parse_or_null(req.body);
            const bool session = in["session"].as_bool(false);
            std::string text = in["text"].as_string(), out;
            json spans = json::array(), values = json::array();
            for (size_t i = 0; i < text.size();)
            {
                bool hit = false;
                for (const auto& p : people)
                    if (text.compare(i, p.first.size(), p.first) == 0)
                    {
                        json span = json::object();
                        span["begin"] = static_cast<long long>(out.size());
                        out += p.second;
                        span["end"] = static_cast<long long>(out.size());
                        span["type"] = "PERSON";
                        spans.push_back(span);
                        json v = json::object();
                        v["substitute"] = p.second;
                        v["value"] = p.first;
                        values.push_back(v);
                        i += p.first.size();
                        hit = true;
                        break;
                    }
                if (!hit) out += text[i++];
            }
            json o = json::object();
            o["text"] = out;
            o["tags"] = spans;
            std::string mode;
            {
                std::lock_guard<std::mutex> lock(mock.m);
                ++mock.anonymized;
                mode = mock.mode;
            }
            // Un Pseudalis antérieur à 3.1.0 ignore session et ne donne aucune valeur.
            if (session && mode != "old_pseudalis") o["values"] = values;
            res.set_content(o.dump(), "application/json");
        });
        auto event = [](const json& data) { return "data: " + data.dump() + "\n\n"; };
        auto record = [&](const httplib::Request& req) {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.body = json::parse_or_null(req.body);
            mock.headers = req.headers;
            return mock.mode;
        };
        relay_server.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            const std::string mode = record(req);
            if (mode == "unauthorized")
            {
                // Une passerelle qui recopie l'en-tête de l'authentification dans son refus.
                json refusal = json::object(), inner = json::object();
                inner["message"] = "invalid key: " + req.get_header_value("Authorization");
                refusal["error"] = inner;
                res.status = 401;
                res.set_content(refusal.dump(), "application/json");
                return;
            }
            if (mode == "credit")
            {
                res.status = 402;
                res.set_content("{\"error\": {\"message\": \"insufficient credits\"}}", "application/json");
                return;
            }
            if (mode == "whole")
            {
                res.set_content("{\"choices\": [{\"message\": {\"content\": \"Xoydoeh Nuggwdfsorn habite Qbspm.\"}, "
                    "\"finish_reason\": \"stop\"}], \"usage\": {\"prompt_tokens\": 5, \"completion_tokens\": 6}}",
                    "application/json");
                return;
            }
            if (mode == "page")
            {
                res.set_content("<html><body>Sign in</body></html>", "text/html");
                return;
            }
            std::string sse;
            for (const char* r : { "Xoydoeh vit \xC3\xA0 Qbs", "pm." })
            {
                json d = json::object(), c = json::object(), e = json::object();
                d["reasoning"] = r;
                c["delta"] = d;
                e["choices"] = json::array();
                e["choices"].push_back(c);
                sse += event(e);
            }
            for (const char* t : { "Xoydoeh Nugg", "wdfsorn habite Qbspm." })
            {
                json d = json::object(), c = json::object(), e = json::object();
                d["content"] = t;
                c["delta"] = d;
                e["choices"] = json::array();
                e["choices"].push_back(c);
                sse += event(e);
            }
            json c = json::object(), e = json::object(), u = json::object(), last = json::object();
            c["delta"] = json::object();
            c["finish_reason"] = "stop";
            e["choices"] = json::array();
            e["choices"].push_back(c);
            sse += event(e);
            u["prompt_tokens"] = 12;
            u["completion_tokens"] = 7;
            last["choices"] = json::array();
            last["usage"] = u;
            sse += event(last) + "data: [DONE]\n\n";
            res.set_content(sse, "text/event-stream");
        });
        relay_server.Post("/v1/messages", [&](const httplib::Request& req, httplib::Response& res) {
            record(req);
            // Une recherche sur le web : sa requête en morceaux, ses résultats, puis un texte qui en cite un.
            if (json::parse_or_null(req.body)["tools"].is_array())
            {
                const char* const searched[] = {
                    "{\"type\": \"message_start\", \"message\": {\"usage\": {\"input_tokens\": 30}}}",
                    "{\"type\": \"content_block_start\", \"index\": 0, \"content_block\": {\"type\": "
                        "\"server_tool_use\", \"id\": \"srvtoolu_1\", \"name\": \"web_search\"}}",
                    "{\"type\": \"content_block_delta\", \"index\": 0, \"delta\": {\"type\": \"input_json_delta\", "
                        "\"partial_json\": \"{\\\"query\\\": \\\"capitale\"}}",
                    "{\"type\": \"content_block_delta\", \"index\": 0, \"delta\": {\"type\": \"input_json_delta\", "
                        "\"partial_json\": \" Italie\\\"}\"}}",
                    "{\"type\": \"content_block_stop\", \"index\": 0}",
                    "{\"type\": \"content_block_start\", \"index\": 1, \"content_block\": {\"type\": "
                        "\"web_search_tool_result\", \"tool_use_id\": \"srvtoolu_1\", \"content\": [{\"type\": "
                        "\"web_search_result\", \"title\": \"Rome\", \"url\": "
                        "\"https://fr.wikipedia.org/wiki/Rome\"}]}}",
                    "{\"type\": \"content_block_stop\", \"index\": 1}",
                    "{\"type\": \"content_block_start\", \"index\": 2, \"content_block\": {\"type\": \"text\", "
                        "\"text\": \"\"}}",
                    "{\"type\": \"content_block_delta\", \"index\": 2, \"delta\": {\"type\": \"citations_delta\", "
                        "\"citation\": {\"type\": \"web_search_result_location\", \"url\": "
                        "\"https://www.fr.wikipedia.org/wiki/Rome_(ville)\", \"title\": \"Rome\"}}}",
                    "{\"type\": \"content_block_delta\", \"index\": 2, \"delta\": {\"type\": \"text_delta\", "
                        "\"text\": \"Rome est la capitale.\"}}",
                    "{\"type\": \"content_block_stop\", \"index\": 2}",
                    "{\"type\": \"message_delta\", \"delta\": {\"stop_reason\": \"end_turn\"}, \"usage\": "
                        "{\"output_tokens\": 12, \"server_tool_use\": {\"web_search_requests\": 1}}}",
                    "{\"type\": \"message_stop\"}" };
                std::string sse;
                for (const char* e : searched)
                    sse += "event: " + json::parse_or_null(e)["type"].as_string() + "\ndata: " + e + "\n\n";
                res.set_content(sse, "text/event-stream");
                return;
            }
            const char* const events[] = {
                "{\"type\": \"message_start\", \"message\": {\"usage\": {\"input_tokens\": 20, "
                    "\"cache_read_input_tokens\": 5}}}",
                "{\"type\": \"content_block_start\", \"index\": 0, \"content_block\": {\"type\": \"thinking\"}}",
                "{\"type\": \"content_block_delta\", \"index\": 0, \"delta\": {\"type\": \"thinking_delta\", "
                    "\"thinking\": \"Let me think.\"}}",
                "{\"type\": \"content_block_delta\", \"index\": 0, \"delta\": {\"type\": \"signature_delta\", "
                    "\"signature\": \"abc\"}}",
                "{\"type\": \"content_block_stop\", \"index\": 0}",
                "{\"type\": \"content_block_delta\", \"index\": 1, \"delta\": {\"type\": \"text_delta\", "
                    "\"text\": \"Bonjour\"}}",
                "{\"type\": \"content_block_delta\", \"index\": 1, \"delta\": {\"type\": \"text_delta\", "
                    "\"text\": \" !\"}}",
                "{\"type\": \"message_delta\", \"delta\": {\"stop_reason\": \"end_turn\"}, \"usage\": "
                    "{\"output_tokens\": 9}}",
                "{\"type\": \"message_stop\"}" };
            std::string sse;
            for (const char* e : events)
                sse += "event: " + json::parse_or_null(e)["type"].as_string() + "\ndata: " + e + "\n\n";
            res.set_content(sse, "text/event-stream");
        });
        relay_server.Get("/v1/models", [&](const httplib::Request& req, httplib::Response& res) {
            if (req.get_header_value("Authorization") != "Bearer sk-test-secret")
            {
                res.status = 401;
                res.set_content("{\"error\": \"unauthorized\"}", "application/json");
                return;
            }
            res.set_content("{\"data\": [{\"id\": \"m1\"}, {\"id\": \"m2\"}]}", "application/json");
        });
        const int relay_port = port + 7;
        const bool relay_listening = relay_server.bind_to_port("127.0.0.1", relay_port);
        std::thread relay_listener([&]() { if (relay_listening) relay_server.listen_after_bind(); });
        const std::string origin = "http://127.0.0.1:" + std::to_string(relay_port);
        check(relay_listening, "the server of the relay listens on the port " + std::to_string(relay_port));
        gulliver::pseudalis_client::endpoint_for_tests(origin);
        gulliver::relay_origin_for_tests("claude", origin);
        gulliver::relay_origin_for_tests("ollama", origin);

        // La mémoire de la pseudonymisation : un texte servi de nouveau sans Pseudalis, ses valeurs oubliées.
        {
            gulliver::pseudalis_client client;
            gulliver::pseudonym_memory::limits limits;
            limits.values = 3;
            limits.max_idle = 100;
            gulliver::pseudonym_memory memory(limits);
            memory.set_time_for_tests(1000);
            std::map<std::string, std::string> found;
            std::string out, error;
            const bool first = memory.protect("Jean Dupont habite Paris.", false, client, nullptr, out, found, error);
            const int calls = mock.anonymized;
            std::map<std::string, std::string> again;
            std::string out2;
            const bool second = memory.protect("Jean Dupont habite Paris.", false, client, nullptr, out2, again, error);
            check(first && out == "Xoydoeh Nuggwdfsorn habite Qbspm." && found["Xoydoeh Nuggwdfsorn"] == "Jean Dupont"
                && found["Qbspm"] == "Paris" && second && out2 == out && again == found && mock.anonymized == calls,
                "a text is protected once, its substitutes known with their values, and served again from memory ("
                + out + ", " + error + ")");
            memory.protect("Marie Curie habite Lyon.", false, client, nullptr, out, found, error);
            const bool bounded = memory.stats()["values"].as_int() == 3;
            memory.protect("Jean Dupont habite Paris.", false, client, nullptr, out, found, error);
            const bool again_asked = mock.anonymized == calls + 2;
            memory.set_time_for_tests(1000 + 200);
            const bool idle = memory.stats()["values"].as_int() == 0 && memory.stats()["texts"].as_int() == 0;
            check(bounded && again_asked && idle, "the memory keeps its bounds, protects again a text whose values "
                "went, and forgets what stays unused");
            // Un Pseudalis qui ne donne pas les valeurs (antérieur à 3.1.0) : le texte ne part pas.
            {
                std::lock_guard<std::mutex> lock(mock.m);
                mock.mode = "old_pseudalis";
            }
            std::map<std::string, std::string> none;
            std::string out3, old_error;
            const bool old = memory.protect("Marie Curie habite Lyon.", false, client, nullptr, out3, none, old_error);
            {
                std::lock_guard<std::mutex> lock(mock.m);
                mock.mode = "ok";
            }
            check(!old && out3.empty() && old_error.find("3.1.0") != std::string::npos,
                "a Pseudalis that gives no values refuses the text, which does not leave (" + old_error + ")");
        }

        // Le relais avec chaque fournisseur.
        struct answer
        {
            std::mutex m;
            std::condition_variable cv;
            std::string content, reasoning, done, error;
            bool finished = false;
        };
        std::mutex settings_m;
        gulliver::host_settings hs;
        int described = 0;
        gulliver::relay_service relay(
            [&]() {
                std::lock_guard<std::mutex> lock(settings_m);
                return hs;
            },
            [&](const std::string&, const std::atomic<bool>&, std::string& description, std::string&) {
                ++described;
                description = "Une photo de Jean Dupont devant la tour Eiffel.";
                return true;
            });
        auto ask = [&](const json& request, std::shared_ptr<answer>& a, int& status, std::string& code) {
            a = std::make_shared<answer>();
            const std::shared_ptr<answer> target = a;
            uint64_t id = 0;
            std::string error;
            if (!relay.submit(request, [target](int kind, const std::string& data) {
                    std::lock_guard<std::mutex> lock(target->m);
                    if (kind == GULLIVER_EVENT_CONTENT) target->content += data;
                    if (kind == GULLIVER_EVENT_REASONING) target->reasoning += data;
                    if (kind == GULLIVER_EVENT_DONE) target->done = data;
                    if (kind == GULLIVER_EVENT_ERROR) target->error = data;
                    if (kind == GULLIVER_EVENT_DONE || kind == GULLIVER_EVENT_ERROR)
                    {
                        target->finished = true;
                        target->cv.notify_all();
                    }
                }, id, error, status, code))
                return false;
            std::unique_lock<std::mutex> lock(a->m);
            return a->cv.wait_for(lock, std::chrono::seconds(30), [&]() { return a->finished; });
        };
        auto set_relay = [&](const std::function<void(gulliver::relay_settings&)>& change) {
            std::lock_guard<std::mutex> lock(settings_m);
            hs.relay = gulliver::relay_settings();
            hs.relay.enabled = true;
            change(hs.relay);
        };
        const json picture = json::parse_or_null("{\"type\": \"image_url\", \"image_url\": {\"url\": "
            "\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUg==\"}}");

        // Un moteur de l'interface d'OpenAI, les textes pseudonymisés, l'image décrite ici.
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "openai";
            r.url = origin + "/v1/";
            r.model = "local-test";
            r.secret = "sk-test-secret";
            r.pseudonymize = true;
        });
        json request = json::object();
        request["model"] = gulliver::relay_model_id;
        request["stream"] = true;
        request["messages"] = json::parse_or_null("[{\"role\": \"system\", \"content\": \"Tu aides Jean Dupont.\"}, "
            "{\"role\": \"user\", \"content\": [{\"type\": \"text\", \"text\": "
            "\"O\u00f9 habite Jean Dupont, Paris ?\"}]}]");
        request["messages"].at(1)["content"].push_back(picture);
        request["stop"] = "\nJean Dupont:";
        request["temperature"] = "Jean Dupont";
        request["top_p"] = 0.9;
        std::shared_ptr<answer> a;
        int status = 0;
        std::string code;
        bool answered = ask(request, a, status, code);
        std::string sent;
        std::string authorization;
        {
            std::lock_guard<std::mutex> lock(mock.m);
            sent = mock.body.dump();
            const auto h = mock.headers.find("Authorization");
            authorization = h == mock.headers.end() ? std::string() : h->second;
        }
        check(answered && a->error.empty() && a->content == "Jean Dupont habite Paris."
            && a->reasoning == "Jean vit \xC3\xA0 Paris."
            && json::parse_or_null(a->done)["usage"]["prompt_tokens"].as_int() == 12,
            "the answer of an engine of the interface of OpenAI streams back with its values (" + a->content + " | "
            + a->reasoning + " | " + a->error + ")");
        check(sent.find("Jean") == std::string::npos && sent.find("Paris") == std::string::npos
            && sent.find("\"stop\"") == std::string::npos && sent.find("\"temperature\"") == std::string::npos
            && sent.find("\"top_p\":0.9") != std::string::npos
            && sent.find("Xoydoeh Nuggwdfsorn") != std::string::npos
            && sent.find("image_url") == std::string::npos && sent.find("[Picture 1, described") != std::string::npos
            && described == 1 && authorization == "Bearer sk-test-secret" && sent.find("sk-test") == std::string::npos,
            "the texts, the description of the image and no stop sequence leave pseudonymized, the sampling as "
            "numbers alone, the key in its header alone");
        ask(request, a, status, code);
        check(described == 1 && a->content == "Jean Dupont habite Paris.", "the description of an image serves again");

        // Une clé refusée : l'erreur ne nomme ni la clé ni l'adresse.
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "unauthorized";
        }
        answered = ask(request, a, status, code);
        check(answered && json::parse_or_null(a->error)["code"].as_string() == "relay_refused"
            && a->error.find("refuses the key") != std::string::npos && a->error.find("sk-test") == std::string::npos
            && a->error.find("127.0.0.1") == std::string::npos, "a refused key ends the answer with its reason, the "
            "key that the engine echoes masked (" + a->error + ")");
        // Une réponse envoyée en entier malgré le flux, puis une page web à la place d'une réponse.
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "whole";
        }
        const bool whole = ask(request, a, status, code) && a->content == "Jean Dupont habite Paris."
            && json::parse_or_null(a->done)["usage"]["completion_tokens"].as_int() == 6;
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "page";
        }
        const bool page = ask(request, a, status, code)
            && json::parse_or_null(a->error)["code"].as_string() == "relay_failed";
        check(whole && page, "an answer sent whole comes back with its values, a web page ends the answer with an "
            "error");
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "ok";
        }
        gulliver::relay_settings tested = hs.relay;
        const json listed = relay.test(tested);
        tested.secret = "wrong";
        const json refused_test = relay.test(tested);
        check(listed["ok"].as_bool(false) && listed["models"].size() == 2 && !refused_test["ok"].as_bool(true),
            "the test of the relay lists the models of the engine, and fails with a wrong key");

        // Un compte sans crédit, puis les pseudonymes laissés dans la réponse, un contrôle de ce qui est parti.
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "credit";
        }
        const bool credit = ask(request, a, status, code)
            && json::parse_or_null(a->error)["message"].as_string().find("no credit left") != std::string::npos
            && a->error.find("insufficient credits") != std::string::npos;
        {
            std::lock_guard<std::mutex> lock(mock.m);
            mock.mode = "ok";
        }
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "openai";
            r.url = origin + "/v1/";
            r.model = "local-test";
            r.secret = "sk-test-secret";
            r.pseudonymize = true;
            r.keep_substitutes = true;
        });
        const bool kept = ask(request, a, status, code) && a->content == "Xoydoeh Nuggwdfsorn habite Qbspm."
            && a->reasoning == "Xoydoeh vit \xC3\xA0 Qbspm."
            && gulliver::relay_description(hs.relay)["keep_substitutes"].as_bool(false);
        check(credit && kept, "an account without credit ends the answer with its reason, and the check of what "
            "left keeps the substitutes in the answer (" + a->content + ")");

        // Une adresse sans /v1, que le relais complète, une autre avec le chemin d'une opération, et leurs échecs.
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "openai";
            r.url = origin;
            r.model = "local-test";
            r.secret = "sk-test-secret";
        });
        json plain = json::object();
        plain["model"] = gulliver::relay_model_id;
        plain["stream"] = true;
        plain["messages"] = json::parse_or_null("[{\"role\": \"user\", \"content\": \"Bonjour\"}]");
        const bool completed = ask(plain, a, status, code) && a->error.empty()
            && a->content == "Xoydoeh Nuggwdfsorn habite Qbspm.";
        gulliver::relay_settings bare = hs.relay;
        const json found = relay.test(bare);
        bare.url = origin + "/v1/chat/completions";
        const json pasted = relay.test(bare);
        bare.url = origin + "/nothing";
        const json nowhere = relay.test(bare);
        bare.url = origin + "/v1";
        bare.secret = "wrong";
        const json refused_key = relay.test(bare);
        check(completed && found["ok"].as_bool(false) && found["url"].as_string() == origin + "/v1"
            && pasted["ok"].as_bool(false) && pasted["url"].as_string() == origin + "/v1"
            && !nowhere["ok"].as_bool(true) && nowhere["code"].as_string() == "relay_http_address"
            && nowhere["args"]["s"].as_string() == "404" && refused_key["code"].as_string() == "relay_http_key"
            && refused_key["args"]["s"].as_string() == "401" && refused_key["detail"].as_string() == "unauthorized"
            && !refused_key.contains("url"),
            "an address without /v1 or with the path of an operation reaches the interface at /v1, and the failures "
            "of the test give their code, their status and the message of the engine (" + nowhere.dump() + ")");
        // Un utilisateur, une requête ou un fragment à côté de l'hôte est refusé ; un moteur hors de portée est
        // nommé par son code.
        bare.url = "http://evil.example?.localhost/v1";
        const json hidden = relay.test(bare);
        bare.url = "http://user@127.0.0.1:" + std::to_string(relay_port) + "/v1";
        const json with_user = relay.test(bare);
        bare.url = "http://127.0.0.1:1/v1";
        const json closed = relay.test(bare);
        check(hidden["code"].as_string() == "relay_address" && with_user["code"].as_string() == "relay_address"
            && gulliver::valid_relay_url("http://gpu_box.lan:8000/v1") && gulliver::valid_relay_url("http://[::1]:8080")
            && !gulliver::valid_relay_url("http://host:70000/v1") && !gulliver::valid_relay_url("http://host#x/v1")
            && closed["code"].as_string() == "relay_unreachable",
            "an address whose host carries a user, a query or a fragment is refused, and an engine out of reach gives "
            "its code (" + closed.dump() + ")");

        // Claude : le système à part, le raisonnement résumé avec son effort, pas de température, l'image telle quelle.
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "claude";
            r.model = "claude-opus-5-5";
            r.secret = key;
        });
        json claude = json::object();
        claude["model"] = gulliver::relay_model_id;
        claude["temperature"] = 0.7;
        claude["reasoning_effort"] = "high";
        claude["messages"] = json::parse_or_null("[{\"role\": \"system\", \"content\": \"Be brief.\"}, "
            "{\"role\": \"user\", \"content\": [{\"type\": \"text\", \"text\": \"Hello\"}]}]");
        claude["messages"].at(1)["content"].push_back(picture);
        answered = ask(claude, a, status, code);
        json body;
        std::string api_key;
        {
            std::lock_guard<std::mutex> lock(mock.m);
            body = mock.body;
            const auto h = mock.headers.find("x-api-key");
            api_key = h == mock.headers.end() ? std::string() : h->second;
        }
        const json done = json::parse_or_null(a->done);
        check(answered && a->content == "Bonjour !" && a->reasoning == "Let me think."
            && done["usage"]["prompt_tokens"].as_int() == 25 && done["usage"]["completion_tokens"].as_int() == 9
            && done["finish_reason"].as_string() == "stop", "the answer of Claude streams back with its reasoning and "
            "its usage (" + a->content + " | " + a->error + ")");
        check(body["system"].as_string() == "Be brief." && body["thinking"]["type"].as_string() == "adaptive"
            && body["thinking"]["display"].as_string() == "summarized"
            && body["output_config"]["effort"].as_string() == "high" && !body.contains("temperature")
            && static_cast<const json&>(body)["messages"][0]["content"][1]["source"]["media_type"].as_string()
                == "image/png"
            && body["max_tokens"].as_int() == 64000 && api_key == key,
            "the request of Claude has its system, its reasoning, its effort, its image and 64000 tokens of answer "
            "without a bound of the request, no temperature, the key in x-api-key");

        // La recherche sur le web de Claude : son outil, sa requête avec le raisonnement, sa source après le texte.
        json searching = claude;
        searching["web_search"] = true;
        answered = ask(searching, a, status, code);
        {
            std::lock_guard<std::mutex> lock(mock.m);
            body = mock.body;
        }
        const json searched_done = json::parse_or_null(a->done);
        const json tool = static_cast<const json&>(body)["tools"][0];
        const bool searched = answered && tool["type"].as_string() == "web_search_20250305"
            && tool["max_uses"].as_int() == 5
            && a->content
                == "Rome est la capitale. ([fr.wikipedia.org](https://www.fr.wikipedia.org/wiki/Rome_%28ville%29))"
            && a->reasoning.find("web_search: capitale Italie") != std::string::npos
            && searched_done["usage"]["web_search_requests"].as_int() == 1;
        {
            std::lock_guard<std::mutex> lock(settings_m);
            hs.web_access = false;
        }
        const bool web_off = !ask(searching, a, status, code) && status == 403 && code == "web_access_off";
        {
            std::lock_guard<std::mutex> lock(settings_m);
            hs.web_access = true;
        }
        check(searched && web_off, "Claude searches the web when the request asks for it, its query shown with the "
            "reasoning and its source as a link after the text, and the web access turned off refuses it ("
            + a->content + " | " + a->reasoning + ")");

        // Ollama : le raisonnement d'un modèle qui l'active ou le coupe, l'image telle quelle.
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "ollama";
            r.model = "gemma4:31b";
            r.secret = "sk-test-secret";
        });
        json ollama = request;
        ollama["reasoning"] = false;
        answered = ask(ollama, a, status, code);
        {
            std::lock_guard<std::mutex> lock(mock.m);
            body = mock.body;
        }
        check(answered && body["reasoning_effort"].as_string() == "none" && body["model"].as_string() == "gemma4:31b"
            && body.dump().find("image_url") != std::string::npos
            && body.dump().find("Jean Dupont") != std::string::npos,
            "Ollama receives the reasoning turned off and the image itself without pseudonymization");
        json ollama_web = ollama;
        ollama_web["web_search"] = true;
        check(!ask(ollama_web, a, status, code) && status == 400 && code == "web_search_not_supported",
            "the search of the web of the relay takes Claude alone");

        // Les refus avant toute requête.
        json tools = request;
        tools["tools"] = json::parse_or_null("[{\"type\": \"function\"}]");
        const bool no_tools = !ask(tools, a, status, code) && status == 400 && code == "tools_not_supported";
        {
            std::lock_guard<std::mutex> lock(settings_m);
            hs.offline = true;
        }
        const bool offline = !ask(request, a, status, code) && status == 503 && code == "offline";
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "claude";
            r.model = "claude-opus-5-5";
        });
        const bool no_key = !ask(request, a, status, code) && status == 404 && code == "relay_unavailable";
        check(no_tools && offline && no_key, "the relay refuses the tools, the offline mode and a missing key");

        /*
            Hors ligne, un moteur de ce Mac répond, joint directement quelle que soit l'annulation des
            téléchargements que tient le mode hors ligne, Pseudalis compris ; les fournisseurs
            d'Internet et une adresse publique sont refusés avant toute connexion.
        */
        gulliver::http_cancel_all("offline mode");
        set_relay([&](gulliver::relay_settings& r) {
            r.provider = "openai";
            r.url = origin + "/v1";
            r.model = "local-test";
            r.secret = "sk-test-secret";
            r.pseudonymize = true;
        });
        const bool local = ask(request, a, status, code) && a->error.empty()
            && a->content == "Jean Dupont habite Paris.";
        const std::string local_error = a ? a->error : std::string();
        std::string why;
        gulliver::relay_settings remote = hs.relay;
        remote.url = "http://93.184.215.14/v1";
        const bool remote_refused = !gulliver::relay_offline_ready(remote, why)
            && why.find("neither") != std::string::npos;
        const json remote_test = relay.test(remote);
        remote.url = "http://[2606:4700:4700::1111]/v1";
        const bool remote6_refused = !gulliver::relay_offline_ready(remote, why);
        gulliver::relay_settings loopback6 = hs.relay;
        loopback6.url = "http://[::1]:8080/v1";
        gulliver::relay_settings cloud = hs.relay;
        cloud.provider = "claude";
        cloud.model = "claude-opus-5-5";
        cloud.secret = key;
        const bool cloud_refused = !gulliver::relay_offline_ready(cloud, why)
            && why.find("Internet") != std::string::npos;
        const json cloud_test = relay.test(cloud);
        gulliver::relay_settings named = hs.relay;
        named.url = "http://localhost:" + std::to_string(relay_port) + "/v1";
        const json named_test = relay.test(named);
        /*
            Un nom du réseau local mène la requête à l'adresse vérifiée, un nom que le résolveur du
            système ne connaît pas ici ; un nom qui a une adresse publique parmi les siennes est refusé.
        */
        gulliver::relay_name_for_tests("engine.gulliver-test", { "127.0.0.1" });
        gulliver::relay_name_for_tests("mixed.gulliver-test", { "10.0.0.5", "93.184.215.14" });
        gulliver::relay_settings pinned = hs.relay;
        pinned.url = "http://Engine.Gulliver-Test:" + std::to_string(relay_port) + "/v1";
        const json pinned_test = relay.test(pinned);
        gulliver::relay_settings mixed = hs.relay;
        mixed.url = "http://mixed.gulliver-test:" + std::to_string(relay_port) + "/v1";
        const json mixed_test = relay.test(mixed);
        gulliver::relay_name_for_tests("engine.gulliver-test", {});
        gulliver::relay_name_for_tests("mixed.gulliver-test", {});
        // Un nom sous .localhost joint 127.0.0.1 quel que soit le résolveur ; seul le réseau local reste à portée.
        gulliver::relay_settings dotted = hs.relay;
        dotted.url = "http://engine.localhost:" + std::to_string(relay_port) + "/v1";
        const json dotted_test = relay.test(dotted);
        gulliver::relay_settings nearby = hs.relay;
        nearby.url = "http://100.64.1.1:8080/v1";
        const bool shared_refused = !gulliver::relay_offline_ready(nearby, why);
        nearby.url = "http://[64:ff9b:1::1]:8080/v1";
        const bool translated_refused = !gulliver::relay_offline_ready(nearby, why);
        nearby.url = "http://192.168.1.20:8080/v1";
        const bool lan_ready = gulliver::relay_offline_ready(nearby, why);
        nearby.url = "http://[fd00::1]:8080/v1";
        const bool unique_local_ready = gulliver::relay_offline_ready(nearby, why);
        check(pinned_test["ok"].as_bool(false) && mixed_test["code"].as_string() == "relay_offline_remote"
            && dotted_test["ok"].as_bool(false) && shared_refused && translated_refused && lan_ready
            && unique_local_ready,
            "offline, a name of the local network leads the request to the address checked, a name under .localhost "
            "to 127.0.0.1, and the addresses outside the local network are refused (" + pinned_test.dump() + " | "
            + mixed_test.dump() + " | " + dotted_test.dump() + ")");
        gulliver::http_restore();
        check(local && remote_refused && remote6_refused && remote_test["code"].as_string() == "relay_offline_remote"
            && gulliver::relay_offline_ready(loopback6, why) && cloud_refused
            && cloud_test["code"].as_string() == "relay_offline" && named_test["ok"].as_bool(false),
            "offline, the relay answers through an engine of this computer and Pseudalis whatever the cancellation of "
            "the downloads, and refuses the providers of the Internet and a public address before any connection ("
            + local_error + " | " + named_test.dump() + ")");
        bool leaked = false;
        for (const auto& line : gulliver::log_file::instance().recent())
            leaked = leaked || line.find("sk-test") != std::string::npos || line.find(key) != std::string::npos
                || line.find("Jean Dupont") != std::string::npos;
        check(!leaked, "the log names no key and no value of the discussions");
        relay.stop();
        gulliver::pseudalis_client::endpoint_for_tests("");
        // L'état de Pseudalis nomme par un code pourquoi il n'est pas joint, qu'il soit sur ce Mac ou non.
        {
            gulliver::pseudalis_client client;
            const json s = client.status();
            const std::string code = s["code"].as_string();
            const bool coherent = s["found"].as_bool(false) ? code.empty() && s["reason"].as_string().empty()
                : (code == "not_installed" || code == "no_authority" || code == "unprotected" || code == "failed")
                    && !s["reason"].as_string().empty();
            check(coherent, "the state of Pseudalis names by a code why it is not reached (" + s.dump() + ")");
        }
        gulliver::relay_origin_for_tests("claude", "");
        gulliver::relay_origin_for_tests("ollama", "");
        relay_server.stop();
        relay_listener.join();
    }

    server.stop();
    listener.join();
    if (failures == 0) gulliver::remove_tree(work);
    std::printf(failures ? "%d checks FAILED\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
