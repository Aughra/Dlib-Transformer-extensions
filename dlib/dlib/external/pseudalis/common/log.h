// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_LOG_H_
#define PSEUDALIS_COMMON_LOG_H_

#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>

namespace pseudalis
{

    enum class log_level { debug = 0, info = 1, warning = 2, error = 3 };

    class log_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le journal d'un programme : des lignes avec leur heure et leur gravité, écrites
                dans un fichier renommé avec le suffixe ".1" quand il atteint sa taille limite,
                de sorte que deux fichiers au plus sont conservés.  Les dernières lignes restent
                aussi en mémoire, pour les fenêtres qui les affichent.
        !*/
    public:
        static log_file& instance();

        void open(const std::string& path, uint64_t max_bytes = 4u << 20);
        void set_console(bool on) { console_ = on; }
        void set_level(log_level l) { level_ = l; }

        void write(log_level level, const std::string& message);
        std::deque<std::string> recent() const;

    private:
        log_file() {}
        mutable std::mutex m_;
        std::string path_;
        uint64_t max_bytes_ = 4u << 20;
        bool console_ = false;
        log_level level_ = log_level::info;
        std::deque<std::string> recent_;
    };

    inline void log_debug(const std::string& m) { log_file::instance().write(log_level::debug, m); }
    inline void log_info(const std::string& m) { log_file::instance().write(log_level::info, m); }
    inline void log_warning(const std::string& m) { log_file::instance().write(log_level::warning, m); }
    inline void log_error(const std::string& m) { log_file::instance().write(log_level::error, m); }

}

#endif // PSEUDALIS_COMMON_LOG_H_
