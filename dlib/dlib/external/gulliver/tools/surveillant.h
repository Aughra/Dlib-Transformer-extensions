// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_TOOLS_SURVEILLANT_H_
#define GULLIVER_TOOLS_SURVEILLANT_H_

/*
    Le surveillant du Mac (Gulliver 7.24.0, à la place de tools/surveillant.py ; fork pour macOS, 2026-11-23, demande
    de l'humain : « un script qui tourne en tâche de fond, qui surveille l'utilisation du processeur et la
    température, m'envoie un mail si problème pendant 30 min, et tue le ou les processus qui posent souci »).  Lancé et
    relancé par launchd (macos/surveillance/), programme bin/surveillant (tools/surveillant_main.cpp).

    Toutes les 10 secondes : la température de la puce (la sonde la plus chaude, par build/temperature, l'outil de
    macos/temperature), la charge du processeur, et le temps de calcul de chaque programme (ps), regroupé par
    exécutable, et ce que chaque exécutable a pris depuis le relevé d'avant (même un programme mort entre-temps reste
    compté).  Le Mac est « en souci » quand le processeur est occupé à cpu_threshold % ou plus, ou que la puce dépasse
    temp_threshold °C.  Quand il l'a été pendant au moins 90 % des relevés des 30 dernières minutes :
      - les coupables sont les exécutables de l'utilisateur qui ont pris en moyenne au moins `cores` cœurs sur la
        demi-heure ;
      - ceux qui ne sont pas protégés reçoivent SIGTERM, puis SIGKILL 15 secondes plus tard s'ils vivent encore ;
      - un mail à l'administrateur de Gulliver (tools/mail_admin.h) donne les chiffres, ce qui a été arrêté, et ce qui
        était protégé ;
      - puis le surveillant repart d'une demi-heure vide.
    Jamais arrêtés : les programmes du système (d'un autre utilisateur), le service de Gulliver, le tunnel de
    Cloudflare, Proton Mail Bridge, VS Code, Claude, les programmes de Gulliver (build/macos-gulliver/bin, dont le
    surveillant lui-même), llama.cpp, MLX ; pendant la mesure du lundi (agent fr.fork.gulliver.bench-hebdo en marche),
    rien n'est arrêté et aucun souci n'est compté.  Journal : build/surveillance/surveillant.log.
*/

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "mail_admin.h"
#include "../common/platform.h"

extern char** environ;

namespace gulliver::essai
{
    struct surveillant_config
    {
        double cpu_threshold = 80;     // % de tout le processeur
        double temp_threshold = 95.0;  // °C, la sonde la plus chaude de la puce
        double cores = 2.0;            // cœurs pris en moyenne sur la demi-heure pour être un coupable
        double window = 30 * 60;       // secondes
        double part = 0.9;             // part des relevés de la fenêtre en souci
        double step = 10;              // secondes entre deux relevés (assez court pour voir un programme qui ne vit
                                       // qu'une demi-minute)
        std::string root;              // la racine du dépôt (build/temperature, build/surveillance)
        std::string bench_agent = "fr.fork.gulliver.bench-hebdo";
        // Pour les tests : les seuls pid que le surveillant peut voir et arrêter (vide : tous).
        std::function<bool(pid_t)> only;
    };

    // Des morceaux du chemin des exécutables protégés.  Pas « /claude » seul : les brouillons des sessions de Claude
    // (/private/tmp/claude-501/…) en contiennent aussi, et les programmes qui y sont construits doivent rester surveillés.
    inline const std::vector<std::string>& protected_parts()
    {
        static const std::vector<std::string> p = {"gulliver-service", "cloudflared", "Proton Mail Bridge",
            "Visual Studio Code", "Code Helper", "/Claude.app/", "anthropic.claude-code", "/claude-code/",
            "/build/macos-gulliver/bin/", "llama-", "mlx", "/build/temperature", "Finder", "Dock", "loginwindow",
            "Terminal", "iTerm"};
        return p;
    }

    inline std::string clock_text(const char* format)
    {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[64];
        std::strftime(buf, sizeof(buf), format, &tm);
        return buf;
    }

    inline void say(const std::string& text)
    {
        std::printf("%s %s\n", clock_text("%Y-%m-%d %H:%M:%S").c_str(), text.c_str());
        std::fflush(stdout);
    }

    inline std::string fmt(const char* f, double x)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), f, x);
        return buf;
    }

    // Un nombre écrit comme str(float) de Python : le plus court qui se relit à l'identique, avec « .0 » s'il est entier.
    inline std::string python_float(double x)
    {
        char buf[64];
        for (int p = 1; p <= 17; ++p)
        {
            std::snprintf(buf, sizeof(buf), "%.*g", p, x);
            if (std::strtod(buf, nullptr) == x) break;
        }
        std::string s = buf;
        if (s.find_first_of(".en") == std::string::npos) s += ".0";
        return s;
    }

    // La sortie d'un programme (argv[0] cherché dans le PATH), sans sa sortie d'erreur ; "" s'il ne se lance pas.
    inline std::string output_of(std::vector<std::string> argv, int timeout_s = 60)
    {
        int fds[2];
        if (pipe(fds) != 0) return std::string();
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addclose(&fa, fds[0]);
        posix_spawn_file_actions_addclose(&fa, fds[1]);
        std::vector<char*> args;
        for (std::string& a : argv) args.push_back(a.data());
        args.push_back(nullptr);
        pid_t pid = -1;
        const int r = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        close(fds[1]);
        std::string out;
        if (r == 0)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
            fcntl(fds[0], F_SETFL, O_NONBLOCK);
            char buf[65536];
            for (;;)
            {
                const ssize_t n = read(fds[0], buf, sizeof(buf));
                if (n > 0)
                {
                    out.append(buf, static_cast<size_t>(n));
                    continue;
                }
                if (n == 0) break;
                if (std::chrono::steady_clock::now() > deadline)
                {
                    kill(pid, SIGKILL);
                    out.clear();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            waitpid(pid, nullptr, 0);
        }
        close(fds[0]);
        return out;
    }

    inline std::vector<std::string> lines_of(const std::string& text)
    {
        std::vector<std::string> out;
        size_t from = 0;
        while (from < text.size())
        {
            size_t end = text.find('\n', from);
            if (end == std::string::npos) end = text.size();
            out.push_back(text.substr(from, end - from));
            from = end + 1;
        }
        return out;
    }

    // Les champs d'une ligne séparés par des blancs, le dernier gardant le reste de la ligne quand il y en a max
    // (str.split(None, max - 1) de Python).
    inline std::vector<std::string> fields(const std::string& line, size_t max)
    {
        std::vector<std::string> out;
        size_t i = 0;
        auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; };
        while (i < line.size())
        {
            while (i < line.size() && blank(line[i])) ++i;
            if (i >= line.size()) break;
            if (out.size() + 1 == max)
            {
                size_t end = line.size();
                while (end > i && blank(line[end - 1])) --end;
                out.push_back(line.substr(i, end - i));
                break;
            }
            size_t j = i;
            while (j < line.size() && !blank(line[j])) ++j;
            out.push_back(line.substr(i, j - i));
            i = j;
        }
        return out;
    }

    inline bool all_digits(const std::string& s)
    {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    // « [jj-]hh:mm:ss.cc » ou « mm:ss.cc » en secondes ; false si le texte ne se lit pas.
    inline bool cpu_seconds(std::string t, double& out)
    {
        double days = 0;
        const size_t dash = t.find('-');
        if (dash != std::string::npos)
        {
            if (!all_digits(t.substr(0, dash))) return false;
            days = std::stod(t.substr(0, dash));
            t = t.substr(dash + 1);
        }
        double total = 0;
        size_t from = 0;
        while (from <= t.size())
        {
            size_t end = t.find(':', from);
            if (end == std::string::npos) end = t.size();
            const std::string part = t.substr(from, end - from);
            char* stop = nullptr;
            const double v = std::strtod(part.c_str(), &stop);
            if (part.empty() || *stop != '\0') return false;
            total = total * 60 + v;
            from = end + 1;
        }
        out = days * 86400 + total;
        return true;
    }

    struct proc_info
    {
        uid_t uid = 0;
        double secs = 0;
        std::string path;
        bool safe = false;
        pid_t ppid = 0;
    };

    // {pid: (uid, secondes de calcul, exécutable, protégé, parent)} ; la protection se lit sur la ligne de commande
    // entière (tous les scripts d'un interprète ont le même exécutable).
    inline std::map<pid_t, proc_info> processes()
    {
        std::map<pid_t, std::string> args;
        for (const std::string& line : lines_of(output_of({"ps", "-Ao", "pid=,args="})))
        {
            const auto f = fields(line, 2);
            if (f.size() == 2 && all_digits(f[0])) args[static_cast<pid_t>(std::stol(f[0]))] = f[1];
        }
        const std::set<pid_t> me = {getpid(), getppid()};
        std::map<pid_t, proc_info> procs;
        for (const std::string& line : lines_of(output_of({"ps", "-Ao", "pid=,ppid=,uid=,time=,comm="})))
        {
            const auto f = fields(line, 5);
            if (f.size() != 5 || !all_digits(f[0]) || !all_digits(f[1]) || !all_digits(f[2])) continue;
            proc_info p;
            if (!cpu_seconds(f[3], p.secs)) continue;
            const pid_t pid = static_cast<pid_t>(std::stol(f[0]));
            p.ppid = static_cast<pid_t>(std::stol(f[1]));
            p.uid = static_cast<uid_t>(std::stoul(f[2]));
            p.path = f[4];
            const std::string& cmd = args.count(pid) ? args[pid] : std::string();
            bool safe = me.count(pid) || p.uid != getuid();
            for (const std::string& part : protected_parts())
                safe = safe || p.path.find(part) != std::string::npos || cmd.find(part) != std::string::npos;
            p.safe = safe;
            procs[pid] = p;
        }
        return procs;
    }

    inline std::string base_name(const std::string& p)
    {
        const size_t s = p.rfind('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }

    // SIGTERM, puis SIGKILL aux survivants ; rend les pid arrêtés.
    inline std::vector<pid_t> stop_processes(const std::vector<pid_t>& pids)
    {
        for (const pid_t pid : pids) kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::seconds(15));
        for (const pid_t pid : pids)
            if (kill(pid, 0) == 0) kill(pid, SIGKILL);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        // Encore là : vivant et pas un fantôme (Z, arrêté mais pas encore relevé par son parent).
        std::set<pid_t> alive;
        if (!pids.empty())
        {
            std::string list;
            for (const pid_t p : pids) list += (list.empty() ? "" : ",") + std::to_string(p);
            for (const std::string& line : lines_of(output_of({"ps", "-o", "pid=,stat=", "-p", list})))
            {
                const auto f = fields(line, 0);
                if (f.size() == 2 && all_digits(f[0]) && f[1][0] != 'Z') alive.insert(static_cast<pid_t>(std::stol(f[0])));
            }
        }
        std::vector<pid_t> out;
        for (const pid_t p : pids)
            if (!alive.count(p)) out.push_back(p);
        return out;
    }

    class surveillant
    {
    public:
        surveillant(const std::string& home, surveillant_config c) : home_(home), c_(std::move(c)) {}

        // (température de la puce en °C, charge du processeur en %), chacune absente si elle ne se lit pas.
        std::pair<std::optional<double>, std::optional<double>> machine()
        {
            const std::string tool = join_path(c_.root, "build/temperature");
            if (access(tool.c_str(), X_OK) != 0)
                output_of({"clang", "-fobjc-arc", "-framework", "Foundation", "-framework", "IOKit", "-o", tool,
                    join_path(c_.root, "macos/temperature/temperature.m")}, 120);
            std::optional<double> temp, cpu;
            for (const std::string& line : lines_of(output_of({tool}, 30)))
            {
                const auto f = fields(line, 0);
                char* stop = nullptr;
                if (line.rfind("puce :", 0) == 0 && f.size() > 2)
                {
                    const double v = std::strtod(f[2].c_str(), &stop);
                    if (*stop == '\0') temp = v;
                }
                else if (line.rfind("charge :", 0) == 0 && f.size() > 3)
                {
                    const double v = std::strtod(f[3].c_str(), &stop);
                    if (*stop == '\0') cpu = v;
                }
            }
            return {temp, cpu};
        }

        bool bench_running()
        {
            const std::string out = output_of({"launchctl", "print", "gui/" + std::to_string(getuid()) + "/" + c_.bench_agent});
            return out.find("state = running") != std::string::npos;
        }

        // Ce que chaque exécutable a pris depuis le relevé d'avant ; un processus né entre-temps compte en entier.
        void deltas(const std::map<pid_t, proc_info>& procs, std::map<std::pair<std::string, bool>, double>& groups,
            std::map<pid_t, double>& used)
        {
            groups.clear();
            used.clear();
            for (const auto& [pid, p] : procs)
            {
                if (c_.only && !c_.only(pid)) continue;
                double d;
                if (prev_ && prev_->count(pid) && prev_->at(pid).path == p.path) d = p.secs - prev_->at(pid).secs;
                else d = prev_ ? p.secs : 0.0;
                used[pid] = std::max(0.0, d);
                groups[{p.path, p.safe}] += used[pid];
            }
        }

        void tick(bool act = true)
        {
            const double now = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0;
            const auto procs = processes();
            if (bench_running())
            {
                // La mesure du lundi chauffe le Mac exprès : rien n'est compté pendant qu'elle tourne.
                samples_.clear();
                prev_ = procs;
                return;
            }
            sample s;
            deltas(procs, s.groups, last_);
            s.interval = samples_.empty() ? c_.step : now - samples_.back().time;
            prev_ = procs;
            std::tie(s.temp, s.cpu) = machine();
            s.time = now;
            s.trouble = (s.cpu && *s.cpu >= c_.cpu_threshold) || (s.temp && *s.temp >= c_.temp_threshold);
            samples_.push_back(s);
            while (!samples_.empty() && samples_.front().time < now - c_.window) samples_.erase(samples_.begin());
            const double span = now - samples_.front().time + samples_.front().interval;
            const auto bad = std::count_if(samples_.begin(), samples_.end(), [](const sample& x) { return x.trouble; });
            if (span >= c_.window - 1.5 * c_.step && static_cast<double>(bad) >= c_.part * static_cast<double>(samples_.size()))
                act_on(span, procs, act);
        }

        struct culprit
        {
            std::string path;
            bool safe = false;
            double secs = 0;
            std::vector<pid_t> pids;
            std::vector<pid_t> parents;
        };

        // Les exécutables qui ont pris au moins `cores` cœurs en moyenne sur la fenêtre, et leurs processus qui
        // calculent encore (un quart de cœur au moins depuis le relevé d'avant : un script au repos ne paie pas pour
        // un autre qui chauffe).
        std::vector<culprit> culprits(double span, const std::map<pid_t, proc_info>& procs)
        {
            std::map<std::pair<std::string, bool>, double> total;
            std::vector<std::pair<std::string, bool>> order;   // l'ordre d'arrivée des clés, comme un dict de Python
            for (const sample& x : samples_)
                for (const auto& [key, secs] : x.groups)
                {
                    if (!total.count(key)) order.push_back(key);
                    total[key] += secs;
                }
            const double interval = samples_.empty() ? c_.step : samples_.back().interval;
            std::vector<culprit> heavy;
            for (const auto& key : order)
            {
                const double secs = total[key];
                if (secs / span < c_.cores) continue;
                culprit g;
                g.path = key.first;
                g.safe = key.second;
                g.secs = secs;
                std::set<pid_t> parents;
                for (const auto& [pid, p] : procs)
                    if (p.path == g.path && p.safe == g.safe && last_.count(pid) && last_[pid] >= 0.25 * interval)
                    {
                        g.pids.push_back(pid);
                        parents.insert(p.ppid);
                    }
                g.parents.assign(parents.begin(), parents.end());
                heavy.push_back(g);
            }
            std::stable_sort(heavy.begin(), heavy.end(), [](const culprit& a, const culprit& b) { return a.secs > b.secs; });
            return heavy;
        }

        // Le relevé de --essai : la machine, puis ce qu'ont pris les programmes en 10 secondes.
        void trial()
        {
            const auto [temp, cpu] = machine();
            say(std::string("puce ") + (temp ? python_float(*temp) : "None") + " °C, processeur " + (cpu ? python_float(*cpu) : "None") +
                " % ; mesure du lundi en marche : " + (bench_running() ? "True" : "False"));
            prev_ = processes();
            std::this_thread::sleep_for(std::chrono::seconds(10));
            const auto now_procs = processes();
            sample s;
            deltas(now_procs, s.groups, last_);
            s.time = 0;
            s.trouble = true;
            s.temp = temp;
            s.cpu = cpu;
            s.interval = 10.0;
            samples_ = {s};
            for (const culprit& g : culprits(10, now_procs))
                say(fmt("%.1f", g.secs / 10) + " cœurs : " + g.path + " (" + std::to_string(g.pids.size()) + " processus)" +
                    (g.safe ? " [protégé]" : ""));
        }

        const surveillant_config& config() const { return c_; }

    private:
        struct sample
        {
            double time = 0;
            bool trouble = false;
            std::optional<double> temp, cpu;
            std::map<std::pair<std::string, bool>, double> groups;
            double interval = 0;
        };

        std::string home_;
        surveillant_config c_;
        std::vector<sample> samples_;
        std::optional<std::map<pid_t, proc_info>> prev_;   // les processus du relevé d'avant
        std::map<pid_t, double> last_;                     // secondes prises depuis le relevé d'avant

        void act_on(double span, const std::map<pid_t, proc_info>& procs, bool act)
        {
            std::vector<double> temps, cpus;
            for (const sample& s : samples_)
            {
                if (s.temp) temps.push_back(*s.temp);
                if (s.cpu) cpus.push_back(*s.cpu);
            }
            std::vector<std::string> stopped, kept;
            for (const culprit& g : culprits(span, procs))
            {
                const double cores = g.secs / span;
                const std::string name = base_name(g.path).empty() ? g.path : base_name(g.path);
                if (g.safe)
                {
                    kept.push_back(name + " (" + std::to_string(g.pids.size()) + " processus, " + fmt("%.1f", cores) +
                                   " cœurs en moyenne) : protégé, laissé");
                    continue;
                }
                const std::vector<pid_t> done = act ? stop_processes(g.pids) : std::vector<pid_t>();
                const std::string left = act && done.size() < g.pids.size()
                    ? ", " + std::to_string(g.pids.size() - done.size()) + " encore là" : "";
                std::string launcher;
                if (g.parents.size() == 1 && procs.count(g.parents[0]))
                    launcher = ", lancés par " + base_name(procs.at(g.parents[0]).path) + " (pid " + std::to_string(g.parents[0]) + ")";
                stopped.push_back(name + " (" + std::to_string(g.pids.size()) + " processus" + left + ", " + fmt("%.1f", cores) +
                                  " cœurs en moyenne" + launcher + ", " + g.path + ")");
            }
            auto mean = [](const std::vector<double>& v) {
                double s = 0;
                for (const double x : v) s += x;
                return s / static_cast<double>(v.size());
            };
            const std::string summary = (cpus.empty() ? std::string("processeur ?") : "processeur " + fmt("%.0f", mean(cpus)) + " % en moyenne") +
                ", " + (temps.empty() ? std::string("puce ?") : "puce " + fmt("%.1f", mean(temps)) + " °C en moyenne, " +
                fmt("%.1f", *std::max_element(temps.begin(), temps.end())) + " °C au plus");
            say("souci depuis " + fmt("%.0f", span / 60) + " min : " + summary);
            for (const std::string& s : stopped) say((act ? "arrêté : " : "arrêterait : ") + s);
            for (const std::string& k : kept) say(k);
            if (!act) return;
            std::string body = "Bonjour,\n\nDepuis " + fmt("%.0f", span / 60) + " minutes, le Mac est en souci : " + summary +
                " (seuils : processeur " + fmt("%g", c_.cpu_threshold) + " %, puce " + fmt("%.0f", c_.temp_threshold) + " °C).\n\n";
            if (!stopped.empty())
            {
                body += "Arrêtés :";
                for (const std::string& s : stopped) body += "\n  - " + s;
            }
            else body += "Rien n'a été arrêté.";
            if (!kept.empty())
            {
                body += "\n\nGros consommateurs protégés, laissés en marche :";
                for (const std::string& k : kept) body += "\n  - " + k;
            }
            body += "\n\nLe journal : build/surveillance/surveillant.log.\n\nGulliver\n";
            const std::string plural = stopped.size() > 1 ? "s" : "";
            const std::string subject = stopped.empty() ? "Gulliver : le Mac était en surchauffe"
                : "Gulliver : " + std::to_string(stopped.size()) + " programme" + plural + " arrêté" + plural + " sur le Mac";
            std::string error;
            if (send_admin(home_, subject, body, error)) say("mail envoyé à l'administrateur");
            else say("mail pas envoyé : " + error);
            samples_.clear();
        }
    };
}

#endif // GULLIVER_TOOLS_SURVEILLANT_H_
