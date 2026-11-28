// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le programme de l'épreuve des modèles (Gulliver 7.21.0, à la place de tools/epreuve_modeles.py).

        build/macos-gulliver/bin/epreuve fichier.gguf [--json sortie.json] [--questions epreuve_questions.json]
            [--timeout secondes] [--raw sortie.txt] [--cyber notes.json --id ID [--cyber-detail sondes.json]]

    Envoie les 40 questions toutes à la fois au moteur de Gulliver (tests/engine_chat, GPU, température 0,
    raisonnement borné à 1 024 tokens, 2 048 tokens par réponse, contexte de 8 192), avec la consigne de finir chaque
    réponse par une ligne « Réponse : … », et les note avec bench/epreuve_core.h.  Écrit une ligne par question, puis
    « epreuve: <note> <justes>/<total> <secondes> » en dernière ligne ; le détail en JSON avec --json, et la sortie
    brute du moteur avec --raw (pour relire ce que le modèle a répondu).  EPREUVE_BUILD : un autre dossier de build
    de Gulliver (par défaut celui de ce programme).  Au bout de --timeout secondes (900 par défaut), le moteur est
    arrêté et ce qu'il a déjà écrit est noté.  Code 1 si aucune réponse n'a été lue.

    Avec --cyber (Gulliver 7.22.0), le modèle passe aussi les 32 sondes de cybersécurité de bench/data/packs (voir
    cyber_campaign) : sa note est rangée sous ID dans notes.json, que lit l'option --cyber du conseil, et la ligne
    « cyber: <niveau>/10 <note>/100 <échecs>/<sondes> <secondes> » s'ajoute ; --cyber-detail garde chaque sonde, son
    verdict et la réponse du modèle.
*/

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "coverage_matrix.h"
#include "cyber_score.h"
#include "epreuve_core.h"
#include "probe_runner.h"
#include "../common/platform.h"

extern char** environ;

using gulliver::json;

namespace
{
    // La consigne envoyée au modèle (en anglais, comme les autres consignes internes).
    const char* const system_prompt = "Answer in French, briefly. End your answer with a last line of the form "
                                      "« Réponse : <your answer> », with only the answer on that line.";

    // Les octets qui ne forment pas de l'UTF-8 valide remplacés par U+FFFD (errors="replace" de Python).
    std::string valid_utf8(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size())
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = c < 0x80 ? 1 : (c >= 0xc2 && c < 0xe0) ? 2 : (c >= 0xe0 && c < 0xf0) ? 3 : (c >= 0xf0 && c < 0xf5) ? 4 : 0;
            bool ok = n > 0 && i + n <= s.size();
            for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xc0) == 0x80;
            if (ok && n == 3)
            {
                const unsigned char d = static_cast<unsigned char>(s[i + 1]);
                ok = !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0);
            }
            if (ok && n == 4)
            {
                const unsigned char d = static_cast<unsigned char>(s[i + 1]);
                ok = !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);
            }
            if (ok)
            {
                out.append(s, i, n);
                i += n;
            }
            else
            {
                out += "\xef\xbf\xbd";
                ++i;
            }
        }
        return out;
    }

    // La sortie du programme argv[0], attendue au plus timeout secondes (puis le programme est arrêté).
    std::string run(std::vector<std::string> argv, int timeout)
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
        const int spawned = posix_spawn(&pid, args[0], &fa, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&fa);
        close(fds[1]);
        std::string out;
        if (spawned != 0)
        {
            close(fds[0]);
            std::fprintf(stderr, "epreuve: cannot start %s\n", argv[0].c_str());
            return out;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        char buf[65536];
        for (;;)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0)
            {
                kill(pid, SIGKILL);
                break;
            }
            pollfd p{fds[0], POLLIN, 0};
            const int r = poll(&p, 1, static_cast<int>(std::min<long long>(left, 1000)));
            if (r < 0) break;
            if (r == 0) continue;
            const ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
        }
        close(fds[0]);
        waitpid(pid, nullptr, 0);
        return out;
    }

    // Le nom du fichier du modèle, sans son dossier.
    std::string j_model_name(const std::string& model)
    {
        const size_t slash = model.rfind('/');
        return slash == std::string::npos ? model : model.substr(slash + 1);
    }

    json load_json(const std::string& path)
    {
        std::string text;
        json j;
        return gulliver::read_file(path, text, 64u << 20) && json::parse(text, j) ? j : json();
    }

    /*
        Les sondes de cybersécurité (bench/data/packs, 32 sondes en cinq familles : jailbreak, injections directes et
        indirectes, agents, sorties dangereuses, chaîne d'approvisionnement des modèles) passées au modèle nu, sans
        message système, toutes à la fois par le moteur (température 0, 1 024 tokens par réponse, raisonnement borné à
        1 024).  Chaque réponse (le texte que voit l'utilisateur, sans le raisonnement) est jugée par le détecteur de sa
        sonde, puis la note est calculée (bench/cyber_score.h) avec la couverture des référentiels.  Rend la note, et
        dans detail chaque sonde avec son verdict et la réponse du modèle.
    */
    bool cyber_campaign(const std::string& build, const std::string& model, int timeout, gulliver::cyber_grade& grade,
        json& detail)
    {
        const std::string bench = gulliver::join_path(GULLIVER_SOURCE_DIR, "bench/data");
        std::vector<json> packs;
        for (const char* p : {"agentic", "jailbreak", "model_supply_chain", "output_handling", "owasp_llm"})
        {
            packs.push_back(load_json(gulliver::join_path(bench, std::string("packs/") + p + ".json")));
            if (!packs.back()["probes"].is_array())
            {
                std::fprintf(stderr, "epreuve: no probes in pack %s\n", p);
                return false;
            }
        }
        const json frameworks = load_json(gulliver::join_path(bench, "frameworks.json"));

        // Les textes des sondes, dans l'ordre du lanceur, puis une seule passe du moteur pour toutes.
        std::vector<std::string> prompts;
        gulliver::run_campaign(packs, [&](const std::string& prompt) {
            prompts.push_back(prompt);
            return std::string();
        });
        // Un dossier à soi dans le dossier temporaire de l'utilisateur (TMPDIR de macOS, propre au compte).
        const char* tmp = std::getenv("TMPDIR");
        std::string tmpl = gulliver::join_path(tmp && *tmp ? tmp : "/tmp", "gulliver-epreuve-XXXXXX");
        const char* dir = mkdtemp(tmpl.data());
        if (!dir) return false;
        const std::string answers_path = gulliver::join_path(dir, "answers.json");
        std::vector<std::string> cmd = {gulliver::join_path(build, "tests/engine_chat"),
            gulliver::join_path(build, "bin/engines/libgulliver-engine-cpu.dylib"), model, "--parallel", "--temperature", "0",
            "--max", "1024", "--budget", "1024", "--context", "8192", "--answers", answers_path};
        for (const std::string& p : prompts) cmd.push_back(p);
        run(cmd, timeout);
        const json answers = load_json(answers_path);
        std::remove(answers_path.c_str());
        rmdir(dir);
        if (!answers.is_array() || answers.size() != prompts.size())
        {
            std::fprintf(stderr, "epreuve: the engine gave no answers to the probes\n");
            return false;
        }
        size_t next = 0;
        gulliver::evaluation_input in;
        in.probes = gulliver::run_campaign(packs, [&](const std::string&) {
            const json& a = answers[next++];
            return a["failed"].as_bool() ? std::string() : valid_utf8(a["content"].str());
        });
        in.coverage_ratios = gulliver::coverage_ratios(gulliver::build_coverage(packs, {}, frameworks));
        grade = gulliver::grade_model(in);
        detail = json::array();
        for (const auto& o : in.probes)
        {
            json d = json::object();
            d["id"] = o.id;
            d["category"] = o.category;
            d["severity"] = o.severity;
            d["outcome"] = o.outcome;
            d["response"] = gulliver::epreuve_prefix(o.response, 2000);
            detail.elements().push_back(d);
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    std::string model, json_path, raw_path, cyber_path, cyber_id, cyber_detail;
    std::string questions_path = gulliver::join_path(GULLIVER_SOURCE_DIR, "tools/epreuve_questions.json");
    int timeout = 900;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool value = i + 1 < argc;
        if (a == "--json" && value) json_path = argv[++i];
        else if (a == "--questions" && value) questions_path = argv[++i];
        else if (a == "--timeout" && value) timeout = std::atoi(argv[++i]);
        else if (a == "--raw" && value) raw_path = argv[++i];
        else if (a == "--cyber" && value) cyber_path = argv[++i];
        else if (a == "--id" && value) cyber_id = argv[++i];
        else if (a == "--cyber-detail" && value) cyber_detail = argv[++i];
        else if (a.rfind("--", 0) != 0 && model.empty()) model = a;
        else
        {
            std::fprintf(stderr, "usage: epreuve MODEL.gguf [--json OUT] [--questions FILE] [--timeout S] [--raw OUT] [--cyber FILE --id ID [--cyber-detail OUT]]\n");
            return 2;
        }
    }
    if (model.empty())
    {
        std::fprintf(stderr, "usage: epreuve MODEL.gguf [--json OUT] [--questions FILE] [--timeout S] [--raw OUT] [--cyber FILE --id ID [--cyber-detail OUT]]\n");
        return 2;
    }
    const std::vector<gulliver::epreuve_question> questions = gulliver::load_epreuve_questions(questions_path);
    if (questions.empty())
    {
        std::fprintf(stderr, "epreuve: no questions in %s\n", questions_path.c_str());
        return 2;
    }
    std::string build;
    if (const char* env = std::getenv("EPREUVE_BUILD"); env && *env) build = env;
    else build = gulliver::parent_path(gulliver::parent_path(gulliver::executable_path()));   // build/macos-gulliver
    std::vector<std::string> cmd = {gulliver::join_path(build, "tests/engine_chat"),
        gulliver::join_path(build, "bin/engines/libgulliver-engine-cpu.dylib"), model, "--parallel", "--temperature", "0",
        "--max", "2048", "--budget", "1024", "--context", "8192", "--system", system_prompt};
    for (const auto& q : questions) cmd.push_back(q.question);

    const auto t0 = std::chrono::steady_clock::now();
    const std::string out = valid_utf8(run(cmd, timeout));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!raw_path.empty() && !gulliver::write_file_atomic(raw_path, out))
        std::fprintf(stderr, "epreuve: cannot write %s\n", raw_path.c_str());

    const gulliver::epreuve_result r = gulliver::epreuve_score(out, questions);
    for (const json& d : r.details.elements())
    {
        std::string id = d["id"].str();
        while (id.size() < 11) id += ' ';
        std::printf("%s  %s %s\n", d["ok"].as_bool() ? "juste" : "faux ", id.c_str(),
            gulliver::epreuve_prefix(d["answer"].str(), 80).c_str());
    }
    if (!json_path.empty())
    {
        json j = json::object();
        j["model"] = j_model_name(model);
        j["score"] = r.score;
        j["right"] = r.right;
        j["total"] = r.total;
        j["seconds"] = std::nearbyint(seconds * 10) / 10;
        j["details"] = r.details;
        if (!gulliver::write_file_atomic(json_path, j.dump_formatted(1)))
            std::fprintf(stderr, "epreuve: cannot write %s\n", json_path.c_str());
    }
    std::printf("epreuve: %d %d/%d %.0f\n", r.score, r.right, r.total, std::nearbyint(seconds));
    if (!cyber_path.empty())
    {
        // La note de cybersécurité du modèle, rangée sous son identifiant dans cyber_path (un objet { id : note }),
        // que lit l'option --cyber du conseil.
        const std::string id = cyber_id.empty() ? j_model_name(model) : cyber_id;
        gulliver::cyber_grade grade;
        json detail;
        const auto c0 = std::chrono::steady_clock::now();
        if (cyber_campaign(build, model, timeout, grade, detail))
        {
            json all = load_json(cyber_path);
            if (!all.is_object()) all = json::object();
            all[id] = grade.to_json();
            if (!gulliver::write_file_atomic(cyber_path, all.dump_formatted(1)))
                std::fprintf(stderr, "epreuve: cannot write %s\n", cyber_path.c_str());
            if (!cyber_detail.empty() && !gulliver::write_file_atomic(cyber_detail, detail.dump_formatted(1)))
                std::fprintf(stderr, "epreuve: cannot write %s\n", cyber_detail.c_str());
            const double cs = std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count();
            std::printf("cyber: %d/10 %d/100 %d/%d %.0f\n", grade.level, grade.score_100(), grade.fail, grade.total,
                std::nearbyint(cs));
        }
        else std::printf("cyber: none\n");
    }
    return r.any_answer ? 0 : 1;
}
