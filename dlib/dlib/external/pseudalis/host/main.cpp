// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    pseudalis-service: the Pseudalis service.

        pseudalis-service                 as a Windows service, started by the service manager;
                                          in the console elsewhere, or when started by hand
        pseudalis-service --console       in the console, stopped by Ctrl+C
        pseudalis-service --version

    A restart happens within the process: the service stops its server and its drive, then
    starts again with the settings of the data folder.
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

    // Returned by serve() when the restart needs a new process.
    const int restart_process = 75;

    /*
        The end of the program, once the service closed what it opened and the log holds its
        last line.  On Windows the process ends at once: the engine library stays loaded
        (engine_loader.h), and its static destructors, run at the end of the process, would
        join the threads of the pool of dlib that Windows ended first, and wait forever.
    */
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

    // Runs the service, again after each restart it asks for; returns the exit code.
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
                pseudalis::log_error("service: " + error);
                if (console) std::cerr << "pseudalis-service: " << error << std::endl;
                // A start that fails is tried again a few times, for a port released late
                // by the previous run or a disk that mounts after the service.
                if (++failures >= 3) return 1;
                for (int i = 0; i < 100 && !stop_requested; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            failures = 0;
            if (svc.update_started() || !svc.restart_requested()) break;
            pseudalis::log_info("service: restart");
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
        pseudalis::log_file::instance().set_console(true);
        const int code = serve(true);
        if (code != restart_process) return code;
        // The same command line in a new process.
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
