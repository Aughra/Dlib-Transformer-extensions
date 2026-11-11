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

namespace pseudalis
{


    std::string executable_path()
    {
        // macOS has no /proc: the path the program was started by, then without its links.
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
        // The folder set_data_dir() gives, empty when none.
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

    bool file_being_written(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < 30;
    }

    bool remove_file(const std::string& p)
    {
        return std::remove(p.c_str()) == 0;
    }

    bool rename_file(const std::string& from, const std::string& to)
    {
        return std::rename(from.c_str(), to.c_str()) == 0;
    }


    bool copy_file(const std::string& from, const std::string& to, const std::atomic<bool>* cancel)
    {
        bool ok = false;
        {
            std::ifstream in(from, std::ios::binary);
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            if (!in || !out) return false;
            std::vector<char> buf(1 << 20);
            ok = true;
            while (ok && in)
            {
                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const std::streamsize n = in.gcount();
                ok = !(cancel && *cancel) && (n == 0 || static_cast<bool>(out.write(buf.data(), n)));
            }
            out.close();
            ok = ok && !in.bad() && static_cast<bool>(out);
        }
        if (!ok) std::remove(to.c_str());
        return ok;
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

    bool direct_path(const std::string& p)
    {
        std::string q = p;
        while (q.size() > 1 && q.back() == '/') q.pop_back();
        return !q.empty() && final_path(q) == q;
    }

    bool path_inside(const std::string& path, const std::string& folder)
    {
        std::string p = final_path(path);
        if (p.empty())
        {
            // A file to come: its folder, links resolved, and its name.
            const std::string parent = final_path(parent_path(path));
            if (parent.empty()) return false;
            p = join_path(parent, file_name(path));
        }
        const std::string f = final_path(folder);
        if (f.empty() || p.size() < f.size()) return false;
        if (p.compare(0, f.size(), f) != 0) return false;
        return p.size() == f.size() || f.back() == '/' || p[f.size()] == '/';
    }

    bool owned_by_system(const std::string& p)
    {
        (void)p;
        return true;
    }

    bool secure_folder(const std::string& p, std::string& error, bool check_content)
    {
        (void)check_content;
        if (!make_dirs(p) || !dir_exists(p))
        {
            error = "cannot create " + p;
            return false;
        }
        return true;
    }

    bool folder_admin_only(const std::string& p)
    {
        (void)p;
        return true;
    }

    namespace
    {
        // The entries of path below folder, opened one within the other without following a link.
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
                // A pipe or a device opens without waiting, then fails the check of a regular file.
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

    bool read_file_link_free(const std::string& path, const std::string& folder, std::string& data,
        uint64_t max_bytes)
    {
        link_free_file f;
        std::string why;
        if (!f.open(path, folder, why) || f.size() > max_bytes) return false;
        data.resize(static_cast<size_t>(f.size()));
        size_t done = 0;
        while (done < data.size())
        {
            const long long n = f.read(&data[done], data.size() - done);
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        return true;
    }

    bool link_free_folder(const std::string& path, const std::string& folder)
    {
        std::string why;
        const int fd = open_link_free(path, folder, true, why);
        if (fd < 0) return false;
        ::close(fd);
        return true;
    }

    std::string file_identity(const std::string& p)
    {
        struct stat st;
        if (lstat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return std::string();
        return std::to_string(static_cast<unsigned long long>(st.st_dev)) + ":"
            + std::to_string(static_cast<unsigned long long>(st.st_ino));
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
        // The data is written to the disk, not only to the cache of the system, before the rename.
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
        // The new name reaches the disk with its folder.
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

    int compare_versions(const std::string& a, const std::string& b)
    {
        std::istringstream sa(a), sb(b);
        for (int i = 0; i < 4; ++i)
        {
            long x = 0, y = 0;
            std::string pa, pb;
            if (std::getline(sa, pa, '.')) x = std::atol(pa.c_str());
            if (std::getline(sb, pb, '.')) y = std::atol(pb.c_str());
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
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


    int run_process(const std::string& program, const std::vector<std::string>& args, std::string* output,
        unsigned timeout_ms, const std::atomic<bool>* stop, const process_options* options)
    {
        const std::string* input = options ? options->input : nullptr;
        // The waits last a tenth of a second at most, so that stop ends the program at once.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        auto remaining_ms = [&]() {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            return left > 0 && !(stop && stop->load()) ? static_cast<int>(std::min<long long>(left, 100)) : 0;
        };
        int fds[2] = { -1, -1 }, ins[2] = { -1, -1 };
        if (output && pipe(fds) != 0) return -1;
        if (input && pipe(ins) != 0)
        {
            if (output)
            {
                close(fds[0]);
                close(fds[1]);
            }
            return -1;
        }
        /*
            What the child needs is made before the fork: between the fork and the start of
            the program, the child of a process with threads may only make system calls, a
            lock of the allocator possibly held by a thread that does not exist in it.
        */
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0 || max_fd > 65536) max_fd = 65536;
        const char* folder = options && !options->directory.empty() ? options->directory.c_str() : nullptr;
        const bool restricted = options && options->restricted;
        const size_t memory_limit = options ? options->memory_limit : 0;
        const pid_t pid = fork();
        if (pid < 0)
        {
            for (int fd : { fds[0], fds[1], ins[0], ins[1] })
                if (fd >= 0) close(fd);
            return -1;
        }
        if (pid == 0)
        {
            if (output)
            {
                dup2(fds[1], 1);
                dup2(fds[1], 2);
            }
            if (input) dup2(ins[0], 0);
            if (folder && chdir(folder) != 0) _exit(127);
            // A restricted child is bounded in memory, writes no file and leaves no core file.
            if (restricted)
            {
                rlimit limit;
                if (memory_limit)
                {
                    limit.rlim_cur = limit.rlim_max = static_cast<rlim_t>(memory_limit);
                    setrlimit(RLIMIT_AS, &limit);
                }
                limit.rlim_cur = limit.rlim_max = 0;
                setrlimit(RLIMIT_FSIZE, &limit);
                setrlimit(RLIMIT_CORE, &limit);
            }
            // The child keeps its standard streams only.
            for (long fd = 3; fd < max_fd; ++fd) close(static_cast<int>(fd));
            execv(program.c_str(), argv.data());
            _exit(127);
        }
        // The input goes to the child from a thread, so that its output is read meanwhile.
        std::thread writer;
        if (input)
        {
            close(ins[0]);
            const int w = ins[1];
            writer = std::thread([w, input]() {
                size_t at = 0;
                while (at < input->size())
                {
                    const ssize_t n = write(w, input->data() + at, input->size() - at);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) break;
                    at += static_cast<size_t>(n);
                }
                close(w);
            });
        }
        bool ended = false;
        if (output)
        {
            close(fds[1]);
            char buf[4096];
            for (;;)
            {
                pollfd p = { fds[0], POLLIN, 0 };
                const int wait = remaining_ms();
                if (wait == 0)
                {
                    ended = true;
                    break;
                }
                const int r = poll(&p, 1, wait);
                if (r < 0 && errno == EINTR) continue;
                if (r < 0) break;
                if (r == 0) continue;
                const ssize_t n = read(fds[0], buf, sizeof(buf));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                output->append(buf, static_cast<size_t>(n));
            }
            close(fds[0]);
        }
        int status = 0;
        for (;;)
        {
            const pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) break;
            if (ended || remaining_ms() == 0)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                if (writer.joinable()) writer.join();
                return -1;
            }
            usleep(10000);
        }
        if (writer.joinable()) writer.join();
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    void end_process(int code)
    {
        std::fflush(nullptr);
        _exit(code);
    }

}
