// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le test du cœur du retrait du dernier du classement (bench/retrait_core.h, Gulliver 7.20.0), sur des listes
    inventées dont on connaît la bonne décision : rien sans « remove », un seul retrait par semaine, un modèle absent
    ou actif laissé, et la ligne de retires.json d'un modèle du catalogue et d'un modèle de Hugging Face.
*/

#include <cstdio>
#include <ctime>
#include <string>

#include "../bench/retrait_core.h"

using gulliver::json;
using action = gulliver::retrait_plan::action;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s%s\n", ok ? "ok   " : "FAIL ", what.c_str());
        if (!ok) ++failures;
    }

    json parse(const std::string& text)
    {
        json j;
        if (!json::parse(text, j)) std::printf("bad json in the test: %s\n", text.c_str());
        return j;
    }

    std::time_t at(const char* date)
    {
        std::tm t{};
        strptime(date, "%Y-%m-%d %H:%M", &t);
        t.tm_isdst = -1;
        return std::mktime(&t);
    }
}

int main()
{
    const std::time_t now = at("2026-11-30 09:40");
    const json models = parse(R"j({"active": {"general": "granite", "embedding": "qwen3-embedding"},
      "models": [
        {"id": "granite", "name": "Granite", "installed": true, "origin": "catalog"},
        {"id": "vieux", "name": "Vieux 3B", "installed": true, "origin": "catalog"},
        {"id": "parti", "name": "Parti", "installed": false},
        {"id": "hf-essai", "name": "Essai HF", "installed": true, "origin": "huggingface",
         "source": "https://huggingface.co/essai/Essai-GGUF",
         "files": [{"url": "https://huggingface.co/essai/Essai-GGUF/resolve/main/sous/Essai-Q4_K_M.gguf"}],
         "file": "/ailleurs/hf-essai.gguf", "projector": {"name": "mmproj-Essai.gguf"}}]})j");

    auto p = gulliver::plan_removal(json::object(), json::array(), models, now);
    check(p.what == action::nothing && p.message == "rien à retirer cette semaine", "rien sans « remove »");

    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "vieux"}})j"),
        parse(R"j([{"id": "autre", "name": "Autre", "date": "2026-11-26 08:30"}])j"), models, now);
    check(p.what == action::this_week && p.message == "déjà un retrait cette semaine (Autre, le 2026-11-26 08:30) : rien de plus",
        "un seul retrait par semaine");
    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "vieux"}})j"),
        parse(R"j([{"id": "autre", "name": "Autre", "date": "2026-11-23 08:30"}, {"id": "x", "name": "X"}])j"), models, now);
    check(p.what == action::remove, "un retrait de la semaine d'avant, ou sans date, ne bloque pas");

    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "parti"}})j"), json::array(), models, now);
    check(p.what == action::absent && p.message == "parti : déjà absent de Gulliver", "un modèle désinstallé est absent");
    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "inconnu"}})j"), json::array(), models, now);
    check(p.what == action::absent, "un modèle inconnu est absent");
    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "granite"}})j"), json::array(), models, now);
    check(p.what == action::active && p.message == "granite : actif, laissé", "un modèle actif est laissé");

    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "vieux", "quiz": 40.5, "write_512": 52.5}})j"), json::array(), models, now);
    check(p.what == action::remove && p.id == "vieux", "le dernier du classement est retiré");
    check(p.entry.dump() == R"j({"date":"2026-11-30 09:40","id":"vieux","name":"Vieux 3B","origin":"catalog","quiz":40.5,"write_512":52.5,"why":"le dernier du classement"})j",
        "sa ligne de retires.json : " + p.entry.dump());
    check(p.note == "épreuve 40/100", "la note arrondie au pair, comme Python");
    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "vieux", "quiz": 41.5}})j"), json::array(), models, now);
    check(p.note == "épreuve 42/100", "41,5 donne 42");

    p = gulliver::plan_removal(parse(R"j({"remove": {"id": "hf-essai", "name": "Essai (HF)", "unread": true}})j"), json::array(), models, now);
    check(p.what == action::remove && p.entry["name"].str() == "Essai (HF)", "le nom du conseil passe avant celui du service");
    check(p.entry["why"].str() == "le moteur ne le lit pas" && p.note == "le moteur ne le lit pas", "un modèle illisible");
    check(p.entry["repo"].str() == "essai/Essai-GGUF" && p.entry["file"].str() == "sous/Essai-Q4_K_M.gguf" &&
              p.entry["projector"].str() == "mmproj-Essai.gguf", "de quoi remettre un modèle de Hugging Face");
    check(p.entry["quiz"].is_null() && p.entry["write_512"].is_null(), "sans note ni vitesse : null");

    std::printf("test_bench_retrait: %s\n", failures == 0 ? "all tests passed" : (std::to_string(failures) + " tests failed").c_str());
    return failures == 0 ? 0 : 1;
}
