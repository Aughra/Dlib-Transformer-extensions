// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <membership.h>
#include <sys/acl.h>

namespace pseudalis
{


    std::string executable_path()
    {
        // macOS n'a pas de /proc : le chemin par lequel le programme a été lancé, puis sans ses
        // liens.
        char buf[4096];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) != 0) return std::string();
        char real[PATH_MAX];
        return realpath(buf, real) ? std::string(real) : std::string(buf);
    }

    std::string executable_dir()
    {
        return parent_path(executable_path());
    }

    namespace
    {
        // Le dossier que donne set_data_dir(), vide s'il n'y en a pas.
        std::mutex& data_dir_mutex()
        {
            static std::mutex m;
            return m;
        }

        std::string& data_dir_override()
        {
            static std::string folder;
            return folder;
        }
    }

    void set_data_dir(const std::string& p)
    {
        std::lock_guard<std::mutex> lock(data_dir_mutex());
        data_dir_override() = p;
    }

    std::string data_dir()
    {
        {
            std::lock_guard<std::mutex> lock(data_dir_mutex());
            if (!data_dir_override().empty()) return data_dir_override();
        }
        const char* home = std::getenv("PSEUDALIS_HOME");
        if (home && *home) return home;
        const char* h = std::getenv("HOME");
        return join_path(join_path(join_path(h ? h : "/tmp", ".local"), "share"), "pseudalis");
    }

    std::string join_path(const std::string& a, const std::string& b)
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        const char sep = '/';
        if (a.back() == '/') return a + b;
        return a + sep + b;
    }

    std::string parent_path(const std::string& p)
    {
        const size_t pos = p.find_last_of("/\\");
        if (pos == std::string::npos) return std::string();
        if (pos == 0) return p.substr(0, 1);
        return p.substr(0, pos);
    }

    std::string file_name(const std::string& p)
    {
        const size_t pos = p.find_last_of("/\\");
        return pos == std::string::npos ? p : p.substr(pos + 1);
    }

    bool file_exists(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    bool dir_exists(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    bool make_dirs(const std::string& p)
    {
        if (p.empty() || dir_exists(p)) return true;
        const std::string parent = parent_path(p);
        if (!parent.empty() && parent != p && !make_dirs(parent)) return false;
        return mkdir(p.c_str(), 0755) == 0 || dir_exists(p);
    }

    uint64_t file_size(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
    }

    bool remove_file(const std::string& p)
    {
        return std::remove(p.c_str()) == 0;
    }

    bool rename_file(const std::string& from, const std::string& to)
    {
        return std::rename(from.c_str(), to.c_str()) == 0;
    }


    std::vector<std::string> list_dir(const std::string& p)
    {
        std::vector<std::string> out;
        DIR* d = opendir(p.c_str());
        if (!d) return out;
        while (dirent* e = readdir(d))
        {
            const std::string n = e->d_name;
            if (n != "." && n != "..") out.push_back(n);
        }
        closedir(d);
        return out;
    }

    bool remove_tree(const std::string& p)
    {
        if (is_link(p))
        {
            return unlink(p.c_str()) == 0;
        }
        if (file_exists(p)) return remove_file(p);
        if (!dir_exists(p)) return true;
        bool ok = true;
        for (const auto& n : list_dir(p)) ok = remove_tree(join_path(p, n)) && ok;
        return rmdir(p.c_str()) == 0 && ok;
    }


    bool is_link(const std::string& p)
    {
        struct stat st;
        return lstat(p.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
    }

    std::string final_path(const std::string& p)
    {
        char* r = realpath(p.c_str(), nullptr);
        if (!r) return std::string();
        const std::string out = r;
        std::free(r);
        return out;
    }

    bool path_inside(const std::string& path, const std::string& folder)
    {
        std::string p = final_path(path);
        if (p.empty())
        {
            // Un fichier à venir : son dossier, liens résolus, et son nom.
            const std::string parent = final_path(parent_path(path));
            if (parent.empty()) return false;
            p = join_path(parent, file_name(path));
        }
        const std::string f = final_path(folder);
        if (f.empty() || p.size() < f.size()) return false;
        if (p.compare(0, f.size(), f) != 0) return false;
        return p.size() == f.size() || f.back() == '/' || p[f.size()] == '/';
    }

    namespace
    {
        std::string without_trailing_slashes(const std::string& path)
        {
            std::string out = path;
            while (out.size() > 1 && out.back() == '/') out.pop_back();
            return out;
        }

        bool trusted_owner(const struct stat& st)
        {
            return st.st_uid == geteuid() || st.st_uid == 0;
        }

        /*
            Vrai si p porte une ACL étendue de macOS dont une entrée « allow » laisse un autre compte que
            ce programme ou root écrire, ajouter ou supprimer une entrée, ou changer les attributs, les
            droits ou le propriétaire.  Une entrée « deny » ne fait que retirer des droits : elle est
            acceptée (le dossier personnel en porte une, « everyone deny delete »).  Une entrée dont le
            bénéficiaire est un groupe compte comme un autre compte.  Un système de fichiers sans ACL
            compte comme sans entrée ; une ACL illisible compte comme dangereuse.
        */
        bool acl_lets_others_write(const std::string& path)
        {
            errno = 0;
            acl_t acl = acl_get_link_np(path.c_str(), ACL_TYPE_EXTENDED);
            if (!acl) return !(errno == ENOENT || errno == ENOTSUP || errno == EOPNOTSUPP);
            static const acl_perm_t writes[] = { ACL_WRITE_DATA, ACL_APPEND_DATA, ACL_ADD_SUBDIRECTORY,
                ACL_DELETE, ACL_DELETE_CHILD, ACL_WRITE_ATTRIBUTES, ACL_WRITE_EXTATTRIBUTES,
                ACL_WRITE_SECURITY, ACL_CHANGE_OWNER };
            bool unsafe = false;
            acl_entry_t entry;
            for (int which = ACL_FIRST_ENTRY; !unsafe && acl_get_entry(acl, which, &entry) == 0;
                 which = ACL_NEXT_ENTRY)
            {
                acl_tag_t tag;
                acl_permset_t perms;
                if (acl_get_tag_type(entry, &tag) != 0 || acl_get_permset(entry, &perms) != 0)
                {
                    unsafe = true;
                    break;
                }
                if (tag != ACL_EXTENDED_ALLOW) continue;
                bool grants_write = false;
                for (const acl_perm_t perm : writes)
                    if (acl_get_perm_np(perms, perm) != 0) grants_write = true;
                if (!grants_write) continue;
                guid_t* who = static_cast<guid_t*>(acl_get_qualifier(entry));
                uid_t id = 0;
                int type = 0;
                const bool known = who && mbr_uuid_to_id(who->g_guid, &id, &type) == 0;
                if (who) acl_free(who);
                if (!(known && type == ID_TYPE_UID && (id == geteuid() || id == 0))) unsafe = true;
            }
            acl_free(acl);
            return unsafe;
        }

        bool safe_directory_chain(const std::string& path)
        {
            std::string p = path;
            while (!p.empty())
            {
                struct stat st;
                if (lstat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || !trusted_owner(st)
                    || acl_lets_others_write(p)) return false;
                const bool writable = (st.st_mode & (S_IWGRP | S_IWOTH)) != 0;
                if (writable && !(st.st_uid == 0 && (st.st_mode & S_ISVTX) != 0)) return false;
                const std::string parent = parent_path(p);
                if (parent.empty() || parent == p) break;
                p = parent;
            }
            return true;
        }

        bool safe_tree_entry(const std::string& path)
        {
            struct stat st;
            if (lstat(path.c_str(), &st) != 0 || S_ISLNK(st.st_mode) || !trusted_owner(st)
                || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0 || acl_lets_others_write(path)) return false;
            if (S_ISDIR(st.st_mode))
            {
                DIR* dir = opendir(path.c_str());
                if (!dir) return false;
                bool safe = true;
                errno = 0;
                while (dirent* entry = readdir(dir))
                {
                    const std::string name = entry->d_name;
                    if (name != "." && name != ".." && !safe_tree_entry(join_path(path, name)))
                    {
                        safe = false;
                        break;
                    }
                    errno = 0;
                }
                safe = safe && errno == 0;
                closedir(dir);
                return safe;
            }
            return S_ISREG(st.st_mode);
        }
    }

    bool owned_by_system(const std::string& p)
    {
        const std::string path = without_trailing_slashes(p);
        struct stat st;
        if (path.empty() || lstat(path.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) return false;
        const std::string actual = final_path(path);
        return !actual.empty() && safe_directory_chain(parent_path(actual))
            && lstat(actual.c_str(), &st) == 0 && trusted_owner(st) && !acl_lets_others_write(actual);
    }

    bool secure_folder(const std::string& p, std::string& error, bool check_content)
    {
        const std::string path = without_trailing_slashes(p);
        error.clear();
        if (path.empty() || path == "/" || !make_dirs(path))
        {
            error = "cannot create " + path;
            return false;
        }
        struct stat st;
        if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid())
        {
            error = "the folder is not owned by this account: " + path;
            return false;
        }
        const std::string actual = final_path(path);
        if (actual.empty() || actual == "/" || !safe_directory_chain(parent_path(actual))
            || chmod(actual.c_str(), 0700) != 0)
        {
            error = "cannot restrict permissions on " + path;
            return false;
        }
        // Une ACL qui laisse un autre compte écrire est remplacée par une ACL vide, comme le fait
        // « chmod -N ».
        if (acl_lets_others_write(actual))
        {
            acl_t empty = acl_init(0);
            const bool cleared = empty && acl_set_link_np(actual.c_str(), ACL_TYPE_EXTENDED, empty) == 0;
            if (empty) acl_free(empty);
            if (!cleared || acl_lets_others_write(actual))
            {
                error = "cannot remove the access control list of " + path;
                return false;
            }
        }
        if (check_content && !safe_tree_entry(actual))
        {
            error = "the folder contains an entry with unsafe ownership or permissions: " + actual;
            return false;
        }
        return true;
    }

    bool folder_admin_only(const std::string& p)
    {
        const std::string path = without_trailing_slashes(p);
        struct stat st;
        if (path.empty() || lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return false;
        const std::string actual = final_path(path);
        return !actual.empty() && safe_directory_chain(parent_path(actual)) && safe_tree_entry(actual);
    }

    namespace
    {
        // Les entrées de path sous folder, ouvertes l'une dans l'autre sans suivre de lien.
        int open_link_free(const std::string& path, const std::string& folder, bool want_folder, std::string& why)
        {
            std::string base = folder.empty() ? std::string("/") : folder;
            while (base.size() > 1 && base.back() == '/') base.pop_back();
            std::string p = path;
            while (p.size() > 1 && p.back() == '/') p.pop_back();
            const size_t start = base == "/" ? 1 : base.size() + 1;
            if (p.size() <= start || p.compare(0, base.size(), base) != 0 || p[start - 1] != '/')
            {
                why = "invalid";
                return -1;
            }
            std::vector<std::string> names;
            for (size_t at = start; at <= p.size();)
            {
                size_t end = p.find('/', at);
                if (end == std::string::npos) end = p.size();
                const std::string name = p.substr(at, end - at);
                if (name.empty() || name == "." || name == "..")
                {
                    why = "invalid";
                    return -1;
                }
                names.push_back(name);
                at = end + 1;
            }
            int dir = ::open(base.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dir < 0)
            {
                why = "missing";
                return -1;
            }
            for (size_t i = 0; i < names.size(); ++i)
            {
                const bool file = !want_folder && i + 1 == names.size();
                // Un tube ou un périphérique s'ouvre sans attendre, puis échoue au test du fichier
                // ordinaire.
                const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (file ? O_NOCTTY | O_NONBLOCK : O_DIRECTORY);
                const int next = ::openat(dir, names[i].c_str(), flags);
                if (next < 0)
                {
                    struct stat st;
                    why = fstatat(dir, names[i].c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 ? "missing"
                        : S_ISLNK(st.st_mode) ? "link" : "invalid";
                    ::close(dir);
                    return -1;
                }
                ::close(dir);
                dir = next;
            }
            return dir;
        }
    }

    bool link_free_file::open(const std::string& path, const std::string& folder, std::string& why)
    {
        close();
        const int fd = open_link_free(path, folder, false, why);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
        {
            ::close(fd);
            why = "invalid";
            return false;
        }
        fd_ = fd;
        size_ = static_cast<uint64_t>(st.st_size);
        links_ = static_cast<uint32_t>(st.st_nlink);
        identity_ = std::to_string(static_cast<unsigned long long>(st.st_dev)) + ":"
            + std::to_string(static_cast<unsigned long long>(st.st_ino));
        return true;
    }

    bool link_free_file::is_open() const
    {
        return fd_ >= 0;
    }

    long long link_free_file::read(char* data, size_t n)
    {
        if (!is_open()) return -1;
        for (;;)
        {
            const ssize_t got = ::read(fd_, data, n);
            if (got < 0 && errno == EINTR) continue;
            return got < 0 ? -1 : static_cast<long long>(got);
        }
    }

    bool link_free_file::seek(uint64_t offset)
    {
        if (!is_open()) return false;
        return lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(offset);
    }

    void link_free_file::close()
    {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        size_ = 0;
        links_ = 0;
        identity_.clear();
    }

    bool handle_at(int fd, const std::string& path)
    {
        struct stat a, b;
        return fstat(fd, &a) == 0 && lstat(path.c_str(), &b) == 0 && S_ISREG(b.st_mode) && a.st_dev == b.st_dev
            && a.st_ino == b.st_ino;
    }

    bool new_file::create(const std::string& path)
    {
        close();
        failed_ = false;
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (fd < 0) return false;
        if (!handle_at(fd, path))
        {
            ::close(fd);
            return false;
        }
        fd_ = fd;
        return true;
    }

    bool new_file::write(const char* data, size_t n)
    {
        while (fd_ >= 0 && !failed_ && n > 0)
        {
            const ssize_t w = ::write(fd_, data, n);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0)
            {
                failed_ = true;
                break;
            }
            data += w;
            n -= static_cast<size_t>(w);
        }
        return fd_ >= 0 && !failed_;
    }

    bool new_file::close()
    {
        if (fd_ < 0) return false;
        const bool flushed = fsync(fd_) == 0;
        const bool closed = ::close(fd_) == 0;
        fd_ = -1;
        if (!closed) failed_ = true;
        const bool ok = flushed && !failed_;
        failed_ = false;
        return ok;
    }

    bool read_file(const std::string& p, std::string& data, uint64_t max_bytes)
    {
        std::ifstream in(p, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0 || static_cast<uint64_t>(size) > max_bytes) return false;
        in.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(size));
        if (size > 0) in.read(&data[0], size);
        return static_cast<bool>(in);
    }

    bool write_file_atomic(const std::string& p, const std::string& data)
    {
        const std::string tmp = p + ".tmp-" + random_hex(8);
        // Les données sont écrites sur le disque, pas seulement dans le cache du système, avant le
        // renommage.
        bool ok = true;
        size_t done = 0;
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0) return false;
        while (ok && done < data.size())
        {
            const ssize_t w = ::write(fd, data.data() + done, data.size() - done);
            if (w < 0 && errno == EINTR) continue;
            ok = w > 0;
            if (ok) done += static_cast<size_t>(w);
        }
        ok = ok && fsync(fd) == 0;
        ok = ::close(fd) == 0 && ok;
        if (!ok || !rename_file(tmp, p))
        {
            remove_file(tmp);
            return false;
        }
        // Le nouveau nom atteint le disque avec son dossier.
        const std::string folder = parent_path(p);
        const int dir = ::open(folder.empty() ? "." : folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir >= 0)
        {
            fsync(dir);
            ::close(dir);
        }
        return true;
    }

    bool write_file_restricted(const std::string& p, const std::string& data, bool interactive_readers)
    {
        const std::string tmp = p + ".tmp-" + random_hex(8);
        (void)interactive_readers;
        const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) return false;
        size_t done = 0;
        while (done < data.size())
        {
            const ssize_t w = ::write(fd, data.data() + done, data.size() - done);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            done += static_cast<size_t>(w);
        }
        const bool ok = done == data.size() && fsync(fd) == 0;
        ::close(fd);
        if (!ok || !rename_file(tmp, p))
        {
            remove_file(tmp);
            return false;
        }
        return true;
    }

    uint64_t free_disk_space(const std::string& p)
    {
        uint64_t available = 0;
        return free_disk_space(p, available) ? available : 0;
    }

    bool free_disk_space(const std::string& p, uint64_t& available)
    {
        available = 0;
        std::string dir = p;
        while (!dir.empty() && !dir_exists(dir)) dir = parent_path(dir);
        struct statvfs s;
        if (statvfs(dir.empty() ? "/" : dir.c_str(), &s) != 0) return false;
        available = static_cast<uint64_t>(s.f_bavail) * s.f_frsize;
        return true;
    }

    int64_t now_unix()
    {
        return static_cast<int64_t>(std::time(nullptr));
    }

    std::string iso_time(int64_t t)
    {
        const std::time_t tt = static_cast<std::time_t>(t);
        std::tm tm;
        gmtime_r(&tt, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return buf;
    }

    std::string random_hex(size_t n)
    {
        static const char* hex = "0123456789abcdef";
        std::string out;
        std::vector<unsigned char> bytes((n + 1) / 2);
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        if (!urandom.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            std::random_device rd;
            for (auto& b : bytes) b = static_cast<unsigned char>(rd());
        }
        for (unsigned char b : bytes)
        {
            out += hex[b >> 4];
            out += hex[b & 15];
        }
        out.resize(n);
        return out;
    }


}
