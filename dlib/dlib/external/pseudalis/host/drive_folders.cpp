// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Le lecteur de Pseudalis sans pilote (drive.h) : le dossier drive du dossier de données, dont
    un parcours trouve les changements, chaque seconde tant que des fichiers sont au repos et à
    chaque changement que Windows signale, et qu'une lettre montre pour chaque session de
    l'ordinateur.

    Un fichier est traité une fois au repos, inchangé depuis deux secondes, et une fois que le
    service l'ouvre seul : un programme qui le tient encore le garde (violation de partage), et
    le service le tient, sans rien partager, de sa lecture à son remplacement.  Chaque fichier
    est ouvert sans passer par aucun lien, vérifié là où se trouve son handle, et supprimé ou
    déplacé par ce handle, pour qu'une jonction placée dans le lecteur par un utilisateur ne
    mène jamais le service ailleurs.  Le nouveau fichier est écrit à côté sous un nom
    temporaire caché, puis prend son nom.

    Les fichiers traités sont enregistrés avec leur taille et l'heure de leur dernière écriture,
    en mémoire et dans la base de données : un parcours les passe tant qu'ils gardent les deux.
    Un fichier dont le contenu est un fichier protégé du coffre (une copie, un déplacement, un
    renommage) est enregistré sans être traité de nouveau, et son enregistrement quitte la liste
    des retirés ; l'enregistrement est marqué retiré une fois que part le dernier fichier du
    lecteur qui le montre.  Un fichier protégé dont l'original a été oublié reste tel quel, sans
    enregistrement : sa restauration remet les valeurs de ses tags.  Un fichier extrait d'une
    archive protégée que la base de données enregistre reste tel quel lui aussi, enregistré avec
    l'original de son entrée (service::adopt()).

    Un dossier qui arrive prend son nom une fois que rien n'a changé dessous pendant six
    secondes et que rien dessous n'attend : une copie en cours garde ses dossiers.  Son nom est
    protégé comme le nom d'un fichier, puis enregistré pour ne pas être lu de nouveau.  Un
    fichier d'un autre type part dans le dossier des fichiers non traités de son propriétaire, à
    la même place de l'arborescence, pour qu'un dossier copié en entier y garde sa forme.
*/

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
// macOS nomme st_mtimespec l'heure de modification que Linux nomme st_mtim.
#define st_mtim st_mtimespec

#include "drive.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "../common/file_kinds.h"
#include "../common/log.h"
#include "../common/platform.h"
#include "crypto.h"
#include "keys.h"
#include "service.h"

namespace pseudalis
{
    namespace
    {
        const int64_t file_rest_ms = 2000;      // un fichier inchangé pendant cette durée est traité
        const int64_t folder_rest_ms = 6000;    // un nouveau dossier dont l'arborescence repose cette durée est nommé
        const int64_t busy_retry_ms = 3000;     // un fichier que tient un programme est retenté après ce délai
        const int64_t failure_retry_ms = 60000; // un fichier que le service n'a pas encore pu traiter (moteur, coffre)
        const size_t max_entries = 500000;      // les entrées qu'un parcours lit au plus

        int64_t now_ms()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::string lower_ascii(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // Les fichiers que les lecteurs laissent tranquilles : leurs fichiers temporaires, les fichiers de verrou et
        // temporaires des éditeurs ("~$name.docx", "~WRL0001.tmp", ".~lock.name#"), les fichiers de l'Explorateur.
        bool ignored_name(const std::string& name)
        {
            const std::string l = lower_ascii(name);
            const bool temporary = l.size() > 4 && l.compare(l.size() - 4, 4, ".tmp") == 0;
            return l.compare(0, 11, ".pseudalis-") == 0 || l.compare(0, 1, "~") == 0 || temporary ||
                   l.compare(0, 7, ".~lock.") == 0 || l == "desktop.ini" || l == "thumbs.db" ||
                   l == "ehthumbs.db" || l == ".ds_store";
        }

        std::string base_of(const std::string& path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        std::string dir_of(const std::string& path)
        {
            const size_t slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

        std::string child_of(const std::string& dir, const std::string& name)
        {
            return dir.empty() ? name : dir + "/" + name;
        }

        // Un chemin dans un dossier, '/' entre ses parties, avec les séparateurs du système.
        std::string os_path(std::string path)
        {
            return path;
        }

        // Le chemin d'un événement : "\folder\name", tel que l'icône le lit.
        std::string event_path(const std::string& path)
        {
            std::string p = "\\" + path;
            std::replace(p.begin(), p.end(), '/', '\\');
            return p;
        }

        // "name (2).ext" : le fichier texte d'une image garde l'extension de l'image avant la sienne.
        std::string numbered(const std::string& name, int k)
        {
            std::string ext = name_extension(name);
            if (ext == ".txt" && kind_of_name(name.substr(0, name.size() - 4)) == name_kind::picture)
                ext = name_extension(name.substr(0, name.size() - 4)) + ".txt";
            const std::string stem = name.substr(0, name.size() - ext.size());
            return stem + " (" + std::to_string(k) + ")" + name.substr(stem.size());
        }

        struct entry
        {
            std::string path;       // dans le dossier, '/' entre ses parties
            bool dir = false;
            long long size = 0;
            int64_t written = 0;
        };

        std::string native_path(const std::string& folder, const std::string& path)
        {
            return path.empty() ? folder : join_path(folder, path);
        }

        std::string key_of(const std::string& path)
        {
            return path;
        }

        void scan_tree(const std::string& root, std::vector<entry>& out)
        {
            std::vector<std::string> pending(1, std::string());
            while (!pending.empty() && out.size() < max_entries)
            {
                const std::string rel = pending.back();
                pending.pop_back();
                DIR* d = opendir(native_path(root, rel).c_str());
                if (!d) continue;
                while (dirent* de = readdir(d))
                {
                    const std::string name = de->d_name;
                    if (name == "." || name == "..") continue;
                    struct stat st;
                    const std::string full = native_path(root, child_of(rel, name));
                    if (lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
                    if (!S_ISDIR(st.st_mode) && (!S_ISREG(st.st_mode) || ignored_name(name))) continue;
                    entry e;
                    e.path = child_of(rel, name);
                    e.dir = S_ISDIR(st.st_mode);
                    e.size = static_cast<long long>(st.st_size);
                    e.written = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
                    out.push_back(e);
                    if (e.dir) pending.push_back(e.path);
                    if (out.size() >= max_entries) break;
                }
                closedir(d);
            }
        }

        typedef int file_handle;
        const file_handle no_handle = -1;

        void close_file(file_handle& h)
        {
            if (h >= 0) ::close(h);
            h = no_handle;
        }

        // Hors de Windows, un fichier n'est pas tenu seul : le temps qu'il reste au repos en tient lieu.
        int open_alone(const std::string& path, file_handle& h)
        {
            h = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            if (h < 0) return 2;
            struct stat st;
            if (fstat(h, &st) != 0 || !S_ISREG(st.st_mode) || !handle_at(h, path))
            {
                close_file(h);
                return 2;
            }
            return 0;
        }

        bool stat_file(file_handle h, long long& size, int64_t& written)
        {
            struct stat st;
            if (fstat(h, &st) != 0) return false;
            size = static_cast<long long>(st.st_size);
            written = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
            return true;
        }

        // Lit size octets de plus depuis h après les from octets que contient data.
        bool read_all(file_handle h, std::string& data, uint64_t size, size_t from = 0)
        {
            data.resize(from + static_cast<size_t>(size));
            size_t done = from;
            while (done < data.size())
            {
                const ssize_t got = ::read(h, &data[done], data.size() - done);
                if (got <= 0) return false;
                done += static_cast<size_t>(got);
            }
            return true;
        }

        std::string owner_of(file_handle)
        {
            return "files";
        }

        bool rename_folder(const std::string& from, const std::string& to)
        {
            return !file_exists(to) && !dir_exists(to) && ::rename(from.c_str(), to.c_str()) == 0;
        }

        bool stat_entry(const std::string& path, long long& size, int64_t& written)
        {
            struct stat st;
            if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
            size = static_cast<long long>(st.st_size);
            written = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec;
            return true;
        }

        // Les fichiers du lecteur pendant qu'ils reposent, et ce qu'ils attendent.
        struct pending_file
        {
            long long size = 0;
            int64_t written = 0;
            int64_t since = 0;          // la dernière fois qu'on l'a vu changer
            int64_t retry = 0;          // pas avant cette heure
        };

        struct folder_state
        {
            std::string kind;           // "drive", le nom du dossier dans la base de données
            std::string path;           // le dossier
            std::string letter;
            bool mapped = false;
            bool active = false;
            std::string error;
            std::map<std::string, folder_file> known;          // clé -> tel que laissé
            std::map<long long, int> shown;                     // enregistrement -> les fichiers protégés de known
            std::map<std::string, pending_file> pending;       // clé -> en changement ou en attente
            std::set<std::string> busy;                         // clés dans la file ou traitées
            std::map<std::string, int64_t> seen_folders;       // nouveaux dossiers : clé -> première vue
            int64_t last_change = 0;                            // de n'importe quel fichier, pour les dossiers
            std::map<std::string, int64_t> changed_under;      // clé de dossier -> dernier changement dessous
        };

        struct job
        {
            folder_state* folder = nullptr;
            std::string path;
            bool dir = false;
        };
    }

// ----------------------------------------------------------------------------------------

    struct drive_host::impl
    {
        explicit impl(service& s) : svc(s) {}

        service& svc;
        folder_state drive;
        std::string refused_root;
        mutable std::mutex m;
        std::condition_variable wake_cv;
        bool woken = false;
        std::atomic<bool> stopping{false};
        std::thread scanner;
        std::vector<std::thread> workers;
        std::deque<job> queue;
        std::condition_variable queue_cv;

        // ---------------------------------------------------------------------------
        // Enregistrements.

        void event(const std::string& kind, const std::string& path, const std::string& message)
        {
            try
            {
                svc.db().add_event(kind, event_path(path), message);
            }
            catch (const std::exception& e)
            {
                log_error("drive: " + std::string(e.what()));
            }
        }

        // m tenu.  Compte les fichiers protégés du lecteur qui montrent chaque enregistrement.
        static void count(folder_state& f, const folder_file& r, int delta)
        {
            if (r.record <= 0 || r.state != "protected") return;
            int& n = f.shown[r.record];
            n += delta;
            if (n <= 0) f.shown.erase(r.record);
        }

        // m tenu.
        void remember(folder_state& f, const folder_file& r)
        {
            const auto before = f.known.find(key_of(r.path));
            if (before != f.known.end()) count(f, before->second, -1);
            f.known[key_of(r.path)] = r;
            count(f, r, 1);
            try
            {
                svc.db().set_folder_file(r);
            }
            catch (const std::exception& e)
            {
                log_error("drive: " + std::string(e.what()));
            }
        }

        // m tenu.
        void forget_path(folder_state& f, const std::string& key)
        {
            const auto it = f.known.find(key);
            if (it == f.known.end()) return;
            try
            {
                svc.db().remove_folder_file(f.kind, it->second.path);
            }
            catch (const std::exception& e)
            {
                log_error("drive: " + std::string(e.what()));
            }
            count(f, it->second, -1);
            f.known.erase(it);
        }

        // m non tenu.  Un enregistrement qu'aucun fichier du lecteur ne montre plus : marqué retiré sauf si une copie
        // le montre.
        void release(folder_state& f, long long record)
        {
            if (record <= 0) return;
            {
                std::lock_guard<std::mutex> lock(m);
                if (f.shown.count(record)) return;
            }
            svc.mark_removed(record);
        }

        void wake()
        {
            std::lock_guard<std::mutex> lock(m);
            woken = true;
            wake_cv.notify_all();
        }

        // ---------------------------------------------------------------------------
        // Les dossiers.

        bool prepare(folder_state& f, std::string& error)
        {
            if (!make_dirs(f.path))
            {
                error = "the folder " + f.path + " cannot be created";
                return false;
            }
            return true;
        }

        // Le dossier des fichiers non traités d'un utilisateur, créé pour ce seul utilisateur.
        std::string refused_folder(const std::string& owner)
        {
            const std::string dir = join_path(refused_root, owner.empty() ? std::string("files") : owner);
            if (!make_dirs(dir)) return std::string();
            return dir;
        }

        // Un nom libre dans le dossier dir de f, le chemin du fichier lui-même étant permis.
        std::string free_path(const folder_state& f, const std::string& dir, const std::string& name,
                              const std::string& self)
        {
            for (int k = 1; k < 10000; ++k)
            {
                const std::string candidate = child_of(dir, k == 1 ? name : numbered(name, k));
                if (key_of(candidate) == key_of(self)) return candidate;
                const std::string full = join_path(f.path, candidate);
                if (!file_exists(full) && !dir_exists(full)) return candidate;
            }
            return std::string();
        }

        // ---------------------------------------------------------------------------
        // Les parcours.

        // m tenu : marque les dossiers de path comme changés maintenant.
        void touched(folder_state& f, const std::string& path, int64_t now)
        {
            f.last_change = now;
            for (std::string d = dir_of(path); !d.empty(); d = dir_of(d)) f.changed_under[key_of(d)] = now;
        }

        void scan(folder_state& f, std::vector<job>& ready)
        {
            if (!f.active) return;
            std::vector<entry> entries;
            scan_tree(f.path, entries);
            const int64_t now = now_ms();
            std::lock_guard<std::mutex> lock(m);
            std::set<std::string> present;
            std::vector<const entry*> new_folders;
            for (const entry& e : entries)
            {
                const std::string k = key_of(e.path);
                present.insert(k);
                if (e.dir)
                {
                    if (f.known.count(k) || f.busy.count(k)) continue;
                    if (!f.seen_folders.count(k)) f.seen_folders[k] = now;
                    new_folders.push_back(&e);
                    continue;
                }
                if (f.busy.count(k)) continue;
                const auto kn = f.known.find(k);
                if (kn != f.known.end() && kn->second.size == e.size && kn->second.written == e.written)
                {
                    f.pending.erase(k);
                    continue;
                }
                pending_file& p = f.pending[k];
                if (p.since == 0 || p.size != e.size || p.written != e.written)
                {
                    p.size = e.size;
                    p.written = e.written;
                    p.since = now;
                    touched(f, e.path, now);
                    continue;
                }
                if (now - p.since < file_rest_ms || now < p.retry) continue;
                f.busy.insert(k);
                job j;
                j.folder = &f;
                j.path = e.path;
                ready.push_back(j);
            }
            // Ce qui est parti : un fichier du lecteur supprimé, déplacé dehors ou renommé, un dossier disparu.
            for (auto it = f.known.begin(); it != f.known.end();)
            {
                if (present.count(it->first) || f.busy.count(it->first))
                {
                    ++it;
                    continue;
                }
                const folder_file gone = it->second;
                ++it;
                forget_path(f, key_of(gone.path));
                // L'enregistrement part avec le dernier fichier du lecteur qui le montre : une copie le garde.
                if (gone.state == "protected" && gone.record > 0 && !f.shown.count(gone.record))
                    svc.mark_removed(gone.record);
            }
            for (auto it = f.pending.begin(); it != f.pending.end();)
                it = present.count(it->first) ? std::next(it) : f.pending.erase(it);
            for (auto it = f.seen_folders.begin(); it != f.seen_folders.end();)
                it = present.count(it->first) ? std::next(it) : f.seen_folders.erase(it);
            // Les nouveaux dossiers, les plus profonds d'abord, une fois que rien dessous n'attend ni n'a changé
            // récemment.
            std::sort(new_folders.begin(), new_folders.end(), [](const entry* a, const entry* b) {
                return std::count(a->path.begin(), a->path.end(), '/') >
                       std::count(b->path.begin(), b->path.end(), '/');
            });
            for (const entry* e : new_folders)
            {
                const std::string k = key_of(e->path);
                const std::string prefix = k + "/";
                bool waiting = false;
                for (const auto& p : f.pending)
                    if (p.first.compare(0, prefix.size(), prefix) == 0) waiting = true;
                for (const std::string& b : f.busy)
                    if (b.compare(0, prefix.size(), prefix) == 0) waiting = true;
                const auto c = f.changed_under.find(k);
                const int64_t last = std::max(f.seen_folders[k], c == f.changed_under.end() ? 0 : c->second);
                if (waiting || now - last < folder_rest_ms) continue;
                // Un dossier plus bas attend d'abord son nom.
                bool inner = false;
                for (const job& j : ready)
                    if (j.dir && key_of(j.path).compare(0, prefix.size(), prefix) == 0) inner = true;
                if (inner) continue;
                f.busy.insert(k);
                job j;
                j.folder = &f;
                j.path = e->path;
                j.dir = true;
                ready.push_back(j);
            }
        }

        // ---------------------------------------------------------------------------
        // Le traitement d'un fichier.

        // m non tenu.  Termine la tâche de key : libre pour les parcours suivants, retentée après delay s'il n'est
        // pas 0.
        void finish(folder_state& f, const std::string& path, int64_t retry_after_ms)
        {
            std::lock_guard<std::mutex> lock(m);
            const std::string k = key_of(path);
            f.busy.erase(k);
            touched(f, path, now_ms());
            if (retry_after_ms > 0)
            {
                pending_file& p = f.pending[k];
                p.since = 0;
                p.retry = now_ms() + retry_after_ms;
            }
        }

        // La taille et l'heure de dernière écriture du fichier à path maintenant, pour son enregistrement.
        bool current(const folder_state& f, const std::string& path, long long& size, int64_t& written)
        {
            return stat_entry(join_path(f.path, path), size, written);
        }

        /*
            Écrit data à côté du fichier tenu par source, sous le nom name, et supprime le fichier
            tenu.  Un nouveau nom est pris avant que parte le fichier tenu, qui reste quand cela
            échoue ; le nom du fichier tenu est pris une fois qu'il est parti.  Renvoie le chemin
            du nouveau fichier dans le dossier, ou une chaîne vide.
        */
        std::string replace(folder_state& f, const std::string& path, file_handle& source, const std::string& data,
                            const std::string& name)
        {
            const std::string dir = dir_of(path);
            const std::string temp = join_path(join_path(f.path, dir), ".pseudalis-" + random_hex(8) + ".part");
            const bool same_name = key_of(child_of(dir, name)) == key_of(path);
            {
                new_file out;
                if (!out.create(temp) || !out.write(data.data(), data.size()) || !out.close())
                {
                    remove_file(temp);
                    return std::string();
                }
            }
            close_file(source);
            if (same_name) remove_file(join_path(f.path, path));
            const std::string candidate = free_path(f, dir, name, std::string());
            if (candidate.empty() || !rename_file(temp, join_path(f.path, candidate)))
            {
                remove_file(temp);
                return std::string();
            }
            if (!same_name) remove_file(join_path(f.path, path));
            return candidate;
        }

        /*
            Déplace le fichier tenu hors du lecteur, vers le dossier des fichiers non traités de son
            propriétaire, à la place qu'il avait dans l'arborescence du lecteur, où il n'admet que
            ce propriétaire, SYSTEM et les administrateurs : il garderait sinon les permissions du
            lecteur, qu'un déplacement au sein d'un volume emporte.
        */
        void refuse(folder_state& f, const std::string& path, file_handle& h, const std::string& why)
        {
            const std::string owner = owner_of(h);
            std::string folder = refused_folder(owner);
            if (!folder.empty() && !dir_of(path).empty())
            {
                const std::string under = join_path(folder, os_path(dir_of(path)));
                folder = make_dirs(under) ? under : folder;
            }
            std::string moved;
            if (!folder.empty())
            {
                for (int k = 1; k < 10000 && moved.empty(); ++k)
                {
                    const std::string candidate =
                        join_path(folder, k == 1 ? base_of(path) : numbered(base_of(path), k));
                    if (file_exists(candidate)) continue;
                    if (rename_file(join_path(f.path, path), candidate)) moved = candidate;
                    else break;
                }
            }
            close_file(h);
            if (moved.empty())
            {
                // Laissé en place : les parcours le passent jusqu'à ce qu'il change.
                long long size = 0;
                int64_t written = 0;
                if (current(f, path, size, written))
                {
                    folder_file r;
                    r.folder = f.kind;
                    r.path = path;
                    r.size = size;
                    r.written = written;
                    r.state = "kept";
                    std::lock_guard<std::mutex> lock(m);
                    remember(f, r);
                }
                event("refused", path, why + "; it could not leave the drive");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m);
                forget_path(f, key_of(path));
            }
            event("refused", path, why + "; moved to " + parent_path(moved));
        }

        // Enregistre le fichier à path tel que laissé : son état, et le nom qu'il prend quand il contient des
        // données personnelles.
        void keep(folder_state& f, const std::string& path, const std::string& sha, long long record,
                  const std::string& state, bool protect_name_of_file)
        {
            std::string at = path;
            if (protect_name_of_file)
            {
                std::string named;
                user_error e;
                if (svc.protect_name(base_of(path), named, e) && !named.empty() && named != base_of(path))
                {
                    const std::string target = free_path(f, dir_of(path), named, path);
                    if (!target.empty() && rename_file(join_path(f.path, path), join_path(f.path, target)))
                    {
                        std::lock_guard<std::mutex> lock(m);
                        forget_path(f, key_of(path));
                        at = target;
                    }
                }
            }
            long long size = 0;
            int64_t written = 0;
            if (!current(f, at, size, written)) return;
            folder_file r;
            r.folder = f.kind;
            r.path = at;
            r.size = size;
            r.written = written;
            r.sha256 = sha;
            r.record = record;
            r.state = state;
            std::lock_guard<std::mutex> lock(m);
            if (at != path) forget_path(f, key_of(path));
            remember(f, r);
        }

        // Des erreurs passagères : le fichier est retenté plus tard plutôt que refusé.
        static bool transient(const user_error& e)
        {
            return e.code == "engine" || e.code == "stopping" || e.code == "vault" || e.code == "database";
        }

        // Renvoie le délai avant un autre essai, 0 quand on en a fini avec le fichier.
        int64_t process_file(folder_state& f, const std::string& path)
        {
            const std::string full = join_path(f.path, path);
            file_handle h = no_handle;
            const int opened = open_alone(full, h);
            if (opened == 1) return busy_retry_ms;
            if (opened != 0) return 0;
            struct closer
            {
                file_handle& h;
                ~closer() { close_file(h); }
            } closing{ h };

            long long size = 0;
            int64_t written = 0;
            if (!stat_file(h, size, written)) return busy_retry_ms;
            const host_settings st = svc.settings().get();
            const uint64_t max_bytes = static_cast<uint64_t>(st.max_file_mb) << 20;
            folder_file before;
            {
                std::lock_guard<std::mutex> lock(m);
                const auto it = f.known.find(key_of(path));
                if (it != f.known.end()) before = it->second;
            }
            if (static_cast<uint64_t>(size) > max_bytes)
            {
                refuse(f, path, h, "the file is larger than " + std::to_string(st.max_file_mb) + " MB");
                release(f, before.record);
                return 0;
            }
            /*
                Un nom dont l'extension ne donne aucun type est traité quand son contenu est du texte
                de bout en bout, ce que décide le moteur : un début qui n'est pas du texte quitte le
                lecteur avant que le reste du fichier soit lu, sauf si le fichier peut être un fichier
                protégé renommé, d'une taille que connaît la base de données.  Un nom sans extension
                va au moteur, qui prend aussi un document, une image ou une archive par sa signature.
            */
            const std::string base = base_of(path);
            const bool by_content = !supported_name(base) && !name_extension(base).empty();
            std::string data;
            // Réservé en entier, pour que le reste lu après le début ne déplace aucun de ses octets en mémoire.
            data.reserve(static_cast<size_t>(size));
            const uint64_t head = by_content ? std::min<uint64_t>(size, 65536) : static_cast<uint64_t>(size);
            bool read = read_all(h, data, head);
            if (read && head < static_cast<uint64_t>(size))
            {
                if (!text_head(data.data(), data.size()) && !svc.db().output_size_known(size))
                {
                    wipe(data);
                    refuse(f, path, h, "this kind of file is not processed");
                    release(f, before.record);
                    return 0;
                }
                read = read_all(h, data, static_cast<uint64_t>(size) - head, data.size());
            }
            if (!read)
            {
                wipe(data);
                close_file(h);
                keep(f, path, std::string(), 0, "kept", false);
                release(f, before.record);
                return 0;
            }
            const std::string sha = sha256_hex(data);

            // Un fichier protégé revenu, copié, déplacé ou renommé : gardé tel quel, sous un nom protégé.
            file_record known_record;
            if (svc.db().find_by_output(sha, known_record, base_of(path)))
            {
                wipe(data);
                close_file(h);
                if (known_record.removed) svc.db().set_file_removed(known_record.id, 0);
                keep(f, path, sha, known_record.id, "protected", true);
                if (before.record != known_record.id) release(f, before.record);
                // Un fichier disparu entre-temps ne montre plus son enregistrement.
                release(f, known_record.id);
                return 0;
            }
            if (data.empty())
            {
                close_file(h);
                keep(f, path, sha, 0, "kept", true);
                release(f, before.record);
                return 0;
            }
            /*
                Un fichier protégé dont l'original a été oublié, ou a été gardé son temps après que son
                fichier a quitté le lecteur : gardé tel quel et sans enregistrement, pour qu'aucun nouvel
                original ne le représente ; sa restauration remet les valeurs de ses tags.
            */
            int64_t forgotten_at = 0;
            std::string forgotten_why;
            if (svc.db().find_forgotten(sha, std::string(), forgotten_at, forgotten_why))
            {
                wipe(data);
                close_file(h);
                keep(f, path, sha, 0, "kept", true);
                release(f, before.record);
                event("kept", path, "a protected file whose original is " + forgotten_why + ", kept as it is");
                return 0;
            }
            // Un fichier extrait d'une archive protégée : gardé tel quel, enregistré avec l'original de son entrée.
            long long adopted = 0;
            const int entry = svc.adopt(sha, base_of(path), size, adopted);
            if (entry < 0)
            {
                wipe(data);
                return failure_retry_ms;
            }
            if (entry > 0)
            {
                wipe(data);
                close_file(h);
                keep(f, path, sha, adopted, "protected", true);
                if (before.record != adopted) release(f, before.record);
                release(f, adopted);
                event("protected", path, "a file extracted from a protected archive, recorded with its original");
                return 0;
            }
            // Un nom sans type dont le contenu ne commence pas comme un texte, et qui n'est pas un fichier protégé.
            if (by_content && !text_head(data.data(), std::min<size_t>(data.size(), 65536)))
            {
                wipe(data);
                refuse(f, path, h, "this kind of file is not processed");
                release(f, before.record);
                return 0;
            }
            protect_result r;
            user_error e;
            bool ok = false;
            try
            {
                // Les fichiers du lecteur prennent leurs tags, ou leurs seuls substituts, comme le disent les
                // réglages.  Une version qui en remplace une autre garde les substituts de son enregistrement, sans
                // tags.
                ok = svc.protect(data, base_of(path), "drive", true, svc.settings().get().tags, r, e, before.record);
            }
            catch (const std::exception& x)
            {
                e.set(x.what());
            }
            wipe(data);
            if (!ok)
            {
                if (transient(e)) return failure_retry_ms;
                refuse(f, path, h, e.text.empty() ? std::string("the file cannot be protected") : e.text);
                release(f, before.record);
                return 0;
            }
            const std::string target = replace(f, path, h, r.bytes, r.name);
            if (target.empty())
            {
                if (r.record) svc.forget(r.record);
                return failure_retry_ms;
            }
            {
                std::lock_guard<std::mutex> lock(m);
                if (target != path) forget_path(f, key_of(path));
            }
            keep(f, target, r.sha256, r.record, "protected", false);
            // L'enregistrement du fichier avant qu'il change, un fichier protégé modifié par exemple.
            if (before.record != r.record) release(f, before.record);
            release(f, r.record);
            event("protected", target, std::to_string(r.report["total"].as_int(0)) + " values, " +
                                       std::to_string(r.report["dropped"].size()) + " entries left out");
            return 0;
        }

        // Un nouveau dossier : son nom protégé, puis enregistré.
        int64_t process_folder(folder_state& f, const std::string& path)
        {
            const std::string base = base_of(path);
            std::string named = base;
            user_error e;
            if (!svc.protect_name(base, named, e))
                return transient(e) ? failure_retry_ms : 0;
            std::string at = path;
            if (!named.empty() && named != base)
            {
                const std::string target = free_path(f, dir_of(path), named, path);
                if (target.empty()) return failure_retry_ms;
                // Un dossier en usage, un fichier ouvert dessous, garde son nom jusqu'à ce qu'il soit libre.
                if (!rename_folder(join_path(f.path, path), join_path(f.path, target))) return busy_retry_ms;
                at = target;
                std::lock_guard<std::mutex> lock(m);
                // Les fichiers dessous suivent.
                const std::string prefix = key_of(path) + "/";
                std::vector<folder_file> moved;
                for (auto it = f.known.begin(); it != f.known.end();)
                {
                    if (it->first.compare(0, prefix.size(), prefix) == 0)
                    {
                        moved.push_back(it->second);
                        try
                        {
                            svc.db().remove_folder_file(f.kind, it->second.path);
                        }
                        catch (const std::exception& x)
                        {
                            log_error("drive: " + std::string(x.what()));
                        }
                        count(f, it->second, -1);
                        it = f.known.erase(it);
                    }
                    else ++it;
                }
                for (folder_file& r : moved)
                {
                    r.path = target + r.path.substr(path.size());
                    remember(f, r);
                }
                f.seen_folders.erase(key_of(path));
            }
            folder_file r;
            r.folder = f.kind;
            r.path = at;
            r.state = "folder";
            std::lock_guard<std::mutex> lock(m);
            remember(f, r);
            return 0;
        }

        void run_job(const job& j)
        {
            int64_t retry = 0;
            try
            {
                retry = j.dir ? process_folder(*j.folder, j.path) : process_file(*j.folder, j.path);
            }
            catch (const std::exception& x)
            {
                log_error("drive: " + j.path + ": " + x.what());
                retry = failure_retry_ms;
            }
            if (j.dir)
            {
                std::lock_guard<std::mutex> lock(m);
                j.folder->busy.erase(key_of(j.path));
                if (retry > 0) j.folder->seen_folders[key_of(j.path)] = now_ms() + retry - folder_rest_ms;
                return;
            }
            finish(*j.folder, j.path, retry);
        }

        // ---------------------------------------------------------------------------
        // Les threads.

        void worker_loop()
        {
            while (true)
            {
                job j;
                {
                    std::unique_lock<std::mutex> lock(m);
                    queue_cv.wait(lock, [&]() { return stopping || !queue.empty(); });
                    if (queue.empty()) return;
                    j = queue.front();
                    queue.pop_front();
                }
                if (stopping)
                {
                    std::lock_guard<std::mutex> lock(m);
                    j.folder->busy.erase(key_of(j.path));
                    continue;
                }
                run_job(j);
                wake();
            }
        }

        void scanner_loop()
        {
            while (!stopping)
            {
                std::vector<job> ready;
                try
                {
                    scan(drive, ready);
                }
                catch (const std::exception& x)
                {
                    log_error("drive: " + std::string(x.what()));
                }
                bool waiting = false;
                {
                    std::lock_guard<std::mutex> lock(m);
                    for (const job& j : ready) queue.push_back(j);
                    waiting = !drive.pending.empty() || !drive.seen_folders.empty() || !queue.empty();
                }
                if (!ready.empty()) queue_cv.notify_all();
                std::unique_lock<std::mutex> lock(m);
                const auto pause = std::chrono::milliseconds(1000);
                wake_cv.wait_for(lock, pause, [&]() { return stopping.load() || woken; });
                woken = false;
                if (waiting && !stopping)
                {
                    // Les changements arrivent par rafales : une courte pause les regroupe.
                    lock.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
        }


        // ---------------------------------------------------------------------------

        void load(folder_state& f)
        {
            try
            {
                f.known.clear();
                f.shown.clear();
                for (const folder_file& r : svc.db().folder_files(f.kind))
                {
                    f.known[key_of(r.path)] = r;
                    count(f, r, 1);
                }
            }
            catch (const std::exception& e)
            {
                log_error("drive: " + std::string(e.what()));
            }
        }

        bool start_folder(folder_state& f, const std::string& letter, std::string& error)
        {
            f.letter = letter;
            f.error.clear();
            if (!prepare(f, f.error))
            {
                error = f.error;
                return false;
            }
            load(f);
            f.active = true;
            return error.empty();
        }
    };

// ----------------------------------------------------------------------------------------

    drive_host::drive_host(service& svc) : p_(new impl(svc)) {}

    drive_host::~drive_host()
    {
        stop();
    }

    bool drive_host::start(const std::string& letter, std::string& error)
    {
        impl& p = *p_;
        p.drive.kind = "drive";
        p.drive.path = join_path(p.svc.data_dir(), "drive");
        p.refused_root = join_path(p.svc.data_dir(), "refused");
        p.stopping = false;
        // Un lecteur sans lettre n'est pas lancé.
        error.clear();
        if (letter.empty()) return true;
        p.start_folder(p.drive, letter, error);
        p.scanner = std::thread([&p]() { p.scanner_loop(); });
        const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < std::min(4u, cores / 2); ++i)
            p.workers.push_back(std::thread([&p]() { p.worker_loop(); }));
        return error.empty();
    }

    void drive_host::stop()
    {
        impl& p = *p_;
        p.stopping = true;
        {
            std::lock_guard<std::mutex> lock(p.m);
            p.woken = true;
        }
        p.wake_cv.notify_all();
        p.queue_cv.notify_all();
        if (p.scanner.joinable()) p.scanner.join();
        for (std::thread& t : p.workers)
            if (t.joinable()) t.join();
        p.workers.clear();
        p.drive.active = false;
    }

    json drive_host::status() const
    {
        const impl& p = *p_;
        std::lock_guard<std::mutex> lock(p.m);
        const folder_state& f = p.drive;
        long long files = 0, bytes = 0;
        for (const auto& kv : f.known)
            if (kv.second.state != "folder")
            {
                ++files;
                bytes += kv.second.size;
            }
        json j = json::object();
        j["letter"] = f.letter;
        j["mounted"] = f.active;
        j["folder"] = f.path;
        j["files"] = files;
        j["bytes"] = bytes;
        j["error"] = f.error;
        j["refused_folder"] = p.refused_root;
        return j;
    }
}
