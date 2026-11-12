// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    albireo-service : le service Albireo.

        albireo-service                  lancé au premier plan dans la console, arrêté par Ctrl+C
        albireo-service --version

    Outils du moteur, exécutés dans un processus qui se termine après leur réponse :

        --probe-engine <library>         affiche la description d'une bibliothèque de moteur
        --bench-engine <library> <model> <configuration>
                                         mesure un moteur et affiche le résultat

    Quand le moteur chargé impose un redémarrage, le service relance son propre programme
    après l'arrêt de l'instance courante.  Les outils du moteur terminent aussi leur processus
    sans décharger le moteur (end_process()).
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
#include "engine_host.h"
#include "service.h"

namespace
{
    std::mutex current_m;
    albireo::service* current = nullptr;
    std::atomic<bool> stop_requested{false};

    // Renvoyé par serve() quand le redémarrage demande un nouveau processus.
    const int restart_process = 75;

    // Le code de sortie du programme, qui se termine sans décharger le moteur d'un périphérique
    // une fois que l'un d'eux a tourné (engine_host::pinned_engine_started()).
    int finish(int code)
    {
        if (albireo::engine_host::pinned_engine_started()) albireo::end_process(code);
        return code;
    }

    void request_stop()
    {
        stop_requested = true;
        std::lock_guard<std::mutex> lock(current_m);
        if (current) current->stop();
    }

    // Exécute le service, à nouveau après chaque redémarrage qu'il demande ; renvoie le code de sortie.
    int serve(bool console)
    {
        int failures = 0;
        while (!stop_requested)
        {
            albireo::service svc;
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
                // Une erreur que run() n'a pas signalée, une base de données qui lâche sous lui par exemple :
                // un démarrage manqué, retenté.
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
                albireo::log_error("service: " + error);
                if (console) std::cerr << "albireo-service: " << error << std::endl;
                // Un démarrage qui échoue est retenté quelques fois, pour un port libéré tard
                // par l'exécution précédente ou un disque monté après le service.
                if (++failures >= 3) return 1;
                for (int i = 0; i < 100 && !stop_requested; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            failures = 0;
            if (!svc.restart_requested()) break;
            if (albireo::engine_host::pinned_engine_started())
            {
                albireo::log_info("service: restart in a new process");
                return restart_process;
            }
            albireo::log_info("service: restart");
        }
        return 0;
    }

    int run_console(const std::vector<std::string>& args)
    {
        // Les signaux sont traités par un thread dédié, hors de tout gestionnaire de signal.
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
        albireo::log_file::instance().set_console(true);
        const int code = serve(true);
        if (code != restart_process) return code;
        // La même ligne de commande dans un nouveau processus.
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        // Le chemin du programme (_NSGetExecutablePath sur macOS, où /proc n'existe pas).
        const std::string self = albireo::executable_path();
        if (!self.empty()) execv(self.c_str(), argv.data());
        execvp(argv[0], argv.data());
        return 1;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);

    // Les sondes se terminent dès que leur résultat est écrit, le moteur qu'elles ont chargé encore en mémoire.
    if (args.size() >= 3 && args[1] == "--probe-engine") albireo::end_process(albireo::service::probe_engine(args[2]));
    if (args.size() >= 5 && args[1] == "--bench-engine")
        albireo::end_process(albireo::service::benchmark_engine(args[2], args[3], args[4]));
    if (args.size() >= 2 && args[1] == "--version")
    {
        std::cout << ALBIREO_FULL_NAME << " " << ALBIREO_VERSION << std::endl;
        return 0;
    }
    const bool console = args.size() >= 2 && args[1] == "--console";
    (void)console;
    return finish(run_console(args));
}
