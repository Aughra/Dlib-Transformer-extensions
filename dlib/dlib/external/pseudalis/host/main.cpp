// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    pseudalis-service : le service Pseudalis.

        pseudalis-service                 lancé au premier plan dans la console, arrêté par Ctrl+C
        pseudalis-service --version

    Un redémarrage a lieu dans le processus : le service arrête son serveur et son lecteur,
    puis redémarre avec les réglages du dossier de données.
*/


#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "../common/log.h"
#include "../common/platform.h"
#include "../common/version.h"
#include "service.h"

namespace
{
    std::mutex current_m;
    pseudalis::service* current = nullptr;
    std::atomic<bool> stop_requested{false};

    // Renvoyé par serve() quand le redémarrage demande un nouveau processus.
    const int restart_process = 75;

    int finish(int code)
    {
        return code;
    }

    void request_stop()
    {
        stop_requested = true;
        std::lock_guard<std::mutex> lock(current_m);
        if (current) current->stop();
    }

    // Exécute le service, de nouveau après chaque redémarrage qu'il demande ; renvoie le code de sortie.
    int serve(bool console)
    {
        int failures = 0;
        while (!stop_requested)
        {
            pseudalis::service svc;
            {
                std::lock_guard<std::mutex> lock(current_m);
                current = &svc;
            }
            std::string error;
            bool ok = false;
            try
            {
                ok = !stop_requested && svc.run(error);
            }
            catch (const std::exception& e)
            {
                // Une erreur que run() n'a pas signalée, une base de données qui lâche par exemple : un démarrage
                // raté, retenté.
                error = std::string("unexpected error: ") + e.what();
            }
            catch (...)
            {
                error = "unexpected error";
            }
            {
                std::lock_guard<std::mutex> lock(current_m);
                current = nullptr;
            }
            if (stop_requested) break;
            if (!ok)
            {
                pseudalis::log_error("service: " + error);
                if (console) std::cerr << "pseudalis-service: " << error << std::endl;
                // Un démarrage qui échoue est retenté quelques fois, pour un port libéré tard par
                // l'exécution précédente ou un disque monté après le service.
                if (++failures >= 3) return 1;
                for (int i = 0; i < 100 && !stop_requested; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            failures = 0;
            if (!svc.restart_requested()) break;
            pseudalis::log_info("service: restart");
        }
        return 0;
    }

    int run_console(const std::vector<std::string>& args)
    {
        // Les signaux sont traités par un thread à part, hors de tout gestionnaire de signal.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGHUP);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
        signal(SIGPIPE, SIG_IGN);
        std::thread([set]() {
            int sig = 0;
            if (sigwait(&set, &sig) == 0) request_stop();
        }).detach();
        pseudalis::log_file::instance().set_console(true);
        const int code = serve(true);
        if (code != restart_process) return code;
        // La même ligne de commande dans un nouveau processus.
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        char self[PATH_MAX];
        const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n > 0)
        {
            self[n] = '\0';
            execv(self, argv.data());
        }
        execvp(argv[0], argv.data());
        return 1;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);

    if (args.size() >= 2 && args[1] == "--version")
    {
        std::cout << PSEUDALIS_FULL_NAME << " " << PSEUDALIS_VERSION << std::endl;
        return 0;
    }
    const bool console = args.size() >= 2 && args[1] == "--console";
    (void)console;
    return finish(run_console(args));
}
