// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le test du surveillant (tools/surveillant.h, Gulliver 7.24.0, à la place de tools/test_surveillant.py).

        build/macos-gulliver/tests/test_surveillant

    Une demi-heure ramenée à 40 secondes, un relevé toutes les 2 secondes, et un seuil de processeur à 0 % : le Mac est
    toujours « en souci ».  Le surveillant ne voit et ne peut arrêter que les processus du test (config.only) : trois
    `yes` relancés toutes les 6 secondes, comme une série d'essais dont chaque programme vit peu, et un `sleep` qui ne
    calcule rien.  Le mail part vers un faux serveur d'envoi (tools/faux_smtp.h), jamais vers le Bridge.  Contrôles : les
    `yes` sont comptés et arrêtés, leur lanceur est nommé, le `sleep` est épargné, le mail le dit.  Environ une minute,
    trois cœurs.
*/

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../tools/faux_smtp.h"
#include "../tools/surveillant.h"

namespace
{
    int failures = 0;

    void check(const std::string& name, bool ok, const std::string& detail = "")
    {
        std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.empty() ? "" : ("  " + detail).c_str());
        std::fflush(stdout);
        if (!ok) ++failures;
    }

    pid_t spawn(std::vector<std::string> argv)
    {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        std::vector<char*> args;
        for (std::string& a : argv) args.push_back(a.data());
        args.push_back(nullptr);
        pid_t pid = -1;
        if (posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ) != 0) pid = -1;
        posix_spawn_file_actions_destroy(&fa);
        return pid;
    }

    bool exited(pid_t pid)
    {
        int st = 0;
        return waitpid(pid, &st, WNOHANG) == pid;
    }
}

int main()
{
    const std::string here = gulliver::parent_path(gulliver::executable_path());   // build/macos-gulliver/tests
    const std::string root = gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(here)));
    const std::string home = gulliver::join_path(root, "build/surveillant-test");
    if (std::system(("rm -rf '" + home + "'").c_str()) != 0) std::printf("cannot empty %s\n", home.c_str());
    gulliver::make_dirs(home);

    gulliver::essai::fake_smtp smtp;
    const std::string port = std::to_string(smtp.port);
    gulliver::write_file_atomic(gulliver::join_path(home, "settings.json"),
        "{\"mail_from\": \"gulliver@essai.invalid\", \"mail_port\": " + port + "}");
    gulliver::write_file_atomic(gulliver::join_path(home, "mail.secret"),
        "{\"scope\": \"gulliver@essai.invalid|" + port + "\", \"secret\": \"s\"}");
    sqlite3* db = nullptr;
    sqlite3_open(gulliver::join_path(home, "gulliver.db").c_str(), &db);
    sqlite3_exec(db, "CREATE TABLE profiles (admin INTEGER, email TEXT, created INTEGER);"
                     "INSERT INTO profiles VALUES (1, 'admin@essai.invalid', 1);", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    std::mutex m;
    std::set<pid_t> mine;
    std::atomic<bool> done{false};
    gulliver::essai::surveillant_config c;
    c.window = 40;
    c.step = 2;
    c.cpu_threshold = 0;
    c.cores = 1.5;
    c.root = root;
    c.only = [&](pid_t pid) {
        std::lock_guard<std::mutex> lock(m);
        return mine.count(pid) > 0;
    };

    // Trois « yes » relancés toutes les 6 secondes (ou dès que l'un d'eux s'arrête).
    std::thread relaunch([&]() {
        while (!done)
        {
            std::vector<pid_t> ps;
            for (int i = 0; i < 3; ++i)
            {
                const pid_t p = spawn({"yes"});
                if (p > 0)
                {
                    ps.push_back(p);
                    std::lock_guard<std::mutex> lock(m);
                    mine.insert(p);
                }
            }
            std::vector<bool> gone(ps.size(), false);
            for (int t = 0; t < 60 && !done; ++t)
            {
                bool any = false;
                for (size_t i = 0; i < ps.size(); ++i)
                    if (!gone[i] && exited(ps[i])) gone[i] = any = true;
                if (any) break;
                gulliver::essai::pause_for(0.1);
            }
            for (size_t i = 0; i < ps.size(); ++i)
                if (!gone[i])
                {
                    kill(ps[i], SIGKILL);
                    waitpid(ps[i], nullptr, 0);
                }
        }
    });
    const pid_t idle = spawn({"sleep", "300"});
    {
        std::lock_guard<std::mutex> lock(m);
        mine.insert(idle);
    }

    gulliver::essai::surveillant w(home, c);
    for (int i = 0; i < 45; ++i)
    {
        w.tick();
        if (!smtp.all().empty()) break;
        gulliver::essai::pause_for(c.step);
    }
    done = true;
    relaunch.join();

    check("mail sent", smtp.count("admin@essai.invalid") == 1);
    gulliver::essai::mail mail;
    const std::string body = smtp.last("admin@essai.invalid", mail, 0, 1) ? mail.body : "";
    check("the yes processes are stopped", body.find("Arrêtés :") != std::string::npos && body.find("- yes (") != std::string::npos,
        body.substr(0, 400));
    check("their launcher is named", body.find("lancés par") != std::string::npos);
    check("the idle process is spared", !exited(idle));
    check("the bridge password reached the server",
        smtp.logins.size() == 1 && smtp.logins[0].first == "gulliver@essai.invalid" && smtp.logins[0].second == "s");
    kill(idle, SIGKILL);
    waitpid(idle, nullptr, 0);
    if (std::system(("rm -rf '" + home + "'").c_str()) != 0) std::printf("cannot empty %s\n", home.c_str());
    std::printf("\nfailed checks: %d\n", failures);
    return failures ? 1 : 0;
}
