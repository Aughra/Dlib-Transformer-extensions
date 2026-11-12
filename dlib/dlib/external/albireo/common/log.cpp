// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "platform.h"

namespace albireo
{

    std::string without_queries(const std::string& message)
    {
        std::string out = message;
        size_t from = 0;
        for (;;)
        {
            const size_t scheme = out.find("://", from);
            if (scheme == std::string::npos) break;
            size_t end = scheme + 3;
            while (end < out.size() && out[end] != ' ' && out[end] != '\t' && out[end] != '\r'
                && out[end] != '\n' && out[end] != '"' && out[end] != '\'')
                ++end;
            // La ponctuation qui suit une adresse dans un message, "<adresse>: status 404", n'en fait pas partie.
            while (end > scheme + 3 && (out[end - 1] == ':' || out[end - 1] == ',' || out[end - 1] == ';'
                || out[end - 1] == ')'))
                --end;
            const size_t query = out.find('?', scheme + 3);
            if (query != std::string::npos && query < end)
            {
                out.erase(query, end - query);
                end = query;
            }
            from = end;
        }
        return out;
    }

    log_file& log_file::instance()
    {
        static log_file f;
        return f;
    }

    void log_file::open(const std::string& path, uint64_t max_bytes)
    {
        std::lock_guard<std::mutex> lock(m_);
        path_ = path;
        max_bytes_ = max_bytes;
        make_dirs(parent_path(path));
    }

    void log_file::write(log_level level, const std::string& message)
    {
        if (static_cast<int>(level) < static_cast<int>(level_)) return;
        static const char* names[] = { "debug", "info", "warning", "error" };
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const long ms = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tm;
        localtime_r(&t, &tm);
        char stamp[64];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
        char ms_part[8];
        std::snprintf(ms_part, sizeof(ms_part), ".%03ld", ms);
        const std::string line = std::string(stamp) + ms_part + " [" + names[static_cast<int>(level)] + "] " + message;

        std::lock_guard<std::mutex> lock(m_);
        recent_.push_back(line);
        while (recent_.size() > 500) recent_.pop_front();
        if (console_)
        {
            std::fputs(line.c_str(), stderr);
            std::fputc('\n', stderr);
        }
        if (path_.empty()) return;
        if (file_size(path_) > max_bytes_) rename_file(path_, path_ + ".1");
        std::ofstream out(path_, std::ios::app | std::ios::binary);
        if (out)
        {
            out << line;
            out << "\n";
        }
    }

    std::deque<std::string> log_file::recent() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return recent_;
    }

}
