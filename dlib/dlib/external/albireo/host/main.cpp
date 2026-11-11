// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    albireo-service: the Albireo service.

        albireo-service                  as a Windows service, started by the service manager;
                                         in the console elsewhere, or when started by hand
        albireo-service --console        in the console, stopped by Ctrl+C
        albireo-service --version

    The service starts itself again with the arguments below, for the
    work that must never take the service down with it.

        --probe-engine <library>         prints the description of an engine library
        --bench-engine <library> <model> <configuration>
                                         measures an engine and prints the result

    A restart normally happens within the process.  Once the engine of a device has run,
    whose library stays loaded, the service restarts in a new process instead: as a
    Windows service, through a copy of the program started with --restart-service, which
    waits for the service to stop and starts it again.  A process that loaded the engine of
    a device, the probes included, ends without unloading it (end_process()).
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

    // Returned by serve() when the restart needs a new process.
    const int restart_process = 75;

    // The exit code of the program, which ends without unloading the engine of a device
    // once one has run (engine_host::pinned_engine_started()).
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

    // Runs the service, again after each restart it asks for; returns the exit code.
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
                // An error run() did not report, a database failing under it say: a failed start, tried again.
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
                // A start that fails is tried again a few times, for a port released late
                // by the previous run or a disk that mounts after the service.
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
        // The signals are handled by a thread of their own, outside any signal handler.
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
        // The same command line in a new process.
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        // The path of the program (_NSGetExecutablePath on macOS, where /proc does not exist).
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

    // The probes end as soon as their result is written, the engine they loaded still in memory.
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
