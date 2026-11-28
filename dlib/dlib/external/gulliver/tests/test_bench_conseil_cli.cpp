// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
//
// Le portage du test de tools/test_conseil.py : sur des chiffres bruts figés dont on connaît
// les bonnes réponses, l'assembleur conseil (host/conseil_core) doit désigner les mêmes
// modèles et écrire les mêmes tableaux.  On appelle les émetteurs directement (JSON, Markdown,
// document) avec une date fixée, plutôt que de lancer le binaire : même matière, test pur.
//
// Cas 1 : le choix pour tout faire / le plus juste / le plus rapide ; llama.cpp dans le tableau
// mais jamais conseillé, ni les modèles de moins de 3 milliards de paramètres ; la mémoire de
// conversation ; la charge de la machine ; les sections service et RAG ; la réécriture du
// document entre ses marqueurs.  Cas 2 : avec l'épreuve, la note décide ; les « abliterated »
// n'ont que leur ligne cybersécurité ; le dernier du classement est désigné.

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../common/json.h"
#include "../bench/conseil_core.h"

using gulliver::json;

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::fprintf(ok ? stdout : stderr, "%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool has(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

// Deux lignes identiques « id|nom|image|taille|mémoire|lecture|écriture|où », comme rows() de Python.
static void rows(std::vector<std::string>& raw, const std::string& mid, const std::string& name,
                 const std::string& image, const std::string& calc,
                 const std::vector<std::array<std::string, 4>>& values)
{
    for (const auto& v : values)   // v = { taille, mémoire, lecture, écriture }
    {
        const std::string l = mid + "|" + name + "|" + image + "|" + v[0] + "|" + v[1] + "|" + v[2] + "|" + v[3] + "|" + calc;
        raw.push_back(l); raw.push_back(l);
    }
}
static std::string join(const std::vector<std::string>& v)
{
    std::string s; for (const auto& l : v) s += l + "\n"; return s;
}
static json parse(const std::string& s)
{
    json j; std::string e;
    if (!json::parse(s, j, &e)) std::fprintf(stderr, "parse: %s\n", e.c_str());
    return j;
}

int main()
{
    // ---- les chiffres bruts communs (identiques à test_conseil.py) ----------------------
    std::vector<std::string> raw;
    rows(raw, "a", "Qwen3-VL 4B A", "1", "gpu",
         { { "512", "q8", "2600", "88" }, { "512", "f16", "2600", "89" },
           { "4096", "q8", "2200", "74" }, { "4096", "f16", "2200", "74.5" } });
    rows(raw, "b", "Qwen3-VL 30B B (MoE)", "1", "gpu",
         { { "512", "q8", "1200", "80" }, { "512", "f16", "1210", "84" },
           { "4096", "q8", "1400", "70" }, { "4096", "f16", "1410", "73.5" } });
    rows(raw, "c", "Petit C", "0", "processeur",
         { { "512", "q8", "1500", "190" }, { "512", "f16", "1500", "185" },
           { "4096", "q8", "1400", "170" }, { "4096", "f16", "1400", "168" } });
    rows(raw, "d", "Qwen3 D", "1", "gpu",
         { { "512", "q8", "300", "30" }, { "512", "f16", "300", "30" },
           { "4096", "q8", "250", "25" }, { "4096", "f16", "250", "25" } });
    rows(raw, "e", "Minuscule E 0.5B", "0", "processeur",
         { { "512", "q8", "5000", "250" }, { "4096", "q8", "4000", "230" } });
    rows(raw, "llama.cpp:a", "llama.cpp · Qwen3-VL 4B A", "0", "gpu",
         { { "512", "q8", "2500", "300" }, { "4096", "q8", "2000", "290" } });
    raw.insert(raw.end(), { "ppl|a|15.36", "ppl|b|10.93", "ppl|c|25.75", "ppl|d|9.50", "ppl|llama.cpp:a|15.38" });

    const json rag = parse(R"({"questions":40,"documents":61,"embedder":"qwen3-embedding-0.6b","index_s":136.2,
        "results":{
          "hybrid":{"rappel_6":0.875,"rappel_12":0.95,"rappel_20":0.975,"rang_reciproque":0.71},
          "dense + tri fin":{"rappel_6":0.925,"rappel_12":0.95,"rappel_20":0.95,"rang_reciproque":0.73},
          "lexical":{"rappel_6":0.825,"rappel_12":0.9,"rappel_20":0.925,"rang_reciproque":0.62}}})");
    const json service = parse(R"({"model":"Qwen3-VL 4B A",
        "gulliver":{"load_s":1.7,"long_drafts":{"read":729.0,"write":33.8},"long_plain":{"read":728.0,"write":36.1},
                    "four":{"total_write":235.4},"three_docs":{"total_s":15.4,"mean_first_token_s":6.8},
                    "largest":{"context":131072,"refused":262144}},
        "llama":{"load_s":1.1,"long_plain":{"read":768.0,"write":34.4},"four":{"total_write":229.6},
                 "three_docs":{"total_s":12.4,"mean_first_token_s":3.2}}})");

    // ---- Cas 1 ---------------------------------------------------------------------------
    {
        gulliver::conseil_options opt;
        opt.previous = "a";
        opt.fresh = { "b" };
        opt.machine = "Mac|M5 Pro, 64 Go|27.2|6.29.0|1 au début, 2 à la fin|30 au début";
        opt.temperature = "50.0|60.0|70.0|5|19|40|0|63|99|3";
        opt.service = service;
        opt.rag = rag;

        const std::string text = join(raw);
        json out = gulliver::build_conseil(text, opt, "2026-11-24 10:00");
        const std::string page = gulliver::build_conseil_markdown(text, opt, "2026-11-24 10:00");
        const std::string doc_in = "avant\n<!-- semaine : début -->\nancien\n<!-- semaine : fin -->\naprès\n";
        const std::string after = gulliver::build_conseil_doc(text, opt, "24/11/2026 à 10 h 00", doc_in);

        json adv = out["advice"];
        std::map<std::string, std::string> roles, kv;
        bool e_in = false, engine_in = false;
        for (size_t i = 0; i < adv.size(); ++i)
        {
            const std::string id = adv[i]["id"].as_string();
            roles[adv[i]["role"].as_string()] = id;
            kv[id] = adv[i]["settings"]["kv_type"].as_string();
            if (id == "e") e_in = true;
            if (has(id, ":")) engine_in = true;
        }
        auto role = [&](const std::string& r) { return roles.count(r) ? roles[r] : std::string(); };

        check(role("general") == "b", "pour tout faire : B, le plus juste des Qwen3 qui écrivent vite sur le GPU");
        check(role("accurate") == "d", "le plus juste : D, même trop lent pour tout faire");
        check(role("fastest") == "c", "le plus rapide : C (llama.cpp, plus rapide, n'est pas conseillé)");
        check(!e_in, "un modèle de moins de 3 milliards de paramètres n'est jamais conseillé");
        check(!engine_in, "llama.cpp et MLX ne sont jamais conseillés");
        check(kv["b"] == "f16" && kv["c"] == "q8", "mémoire : demi-précision pour B (+5 %), 8 bits pour C");
        check(has(page, "llama.cpp · Qwen3-VL 4B A"), "llama.cpp figure dans le tableau");
        check(has(page, "(choix précédent)") && has(page, "(nouveau)"), "le choix précédent et le nouveau sont signalés");
        check(has(page, "le conseil pour tout faire passe de Qwen3-VL 4B A à Qwen3-VL 30B B (MoE)"),
              "le changement de conseil est dit");
        check(has(page, "60,0 °C en moyenne, de 50,0 à 70,0 °C") && has(page, "63 % en moyenne, de 0 à 99 %"),
              "la température et la charge : minimum, moyenne, maximum");
        check(has(page, "| Conversation de 32k : écriture, sans brouillons | 36,1 jetons/s | 34,4 jetons/s |")
              && has(page, "| 3 documents à la fois : attente moyenne du premier mot | 6,8 s | 3,2 s |")
              && has(page, "131 072 tokens (262 144 refusés)"), "le tableau « comme un utilisateur », Gulliver et llama.cpp");
        check(has(page, "| Sens seul + tri fin (le meilleur) | 92 % | 95 % | 95 % | 0,73 |")
              && has(page, "moteur « Sens seul », tri fin allumé"),
              "le tableau du RAG et son meilleur réglage (à égalité dans les 12, le meilleur rang l'emporte)");
        check(has(after, "La recherche dans les documents (RAG)") && has(after, "| Mots seuls | 82 % | 90 % | 92 % | 0,62 |"),
              "le tableau du RAG dans le document");
        check(has(after, "| Chargement du modèle (jusqu'à une première réponse) | 1,7 s |") && !has(after, "3,2 s"),
              "dans le document, le tableau « comme un utilisateur » de Gulliver seul");
        check(after.rfind("avant\n<!-- semaine : début -->", 0) == 0
              && after.size() >= std::string("<!-- semaine : fin -->\naprès\n").size()
              && after.compare(after.size() - std::string("<!-- semaine : fin -->\naprès\n").size(),
                               std::string::npos, "<!-- semaine : fin -->\naprès\n") == 0
              && !has(after, "ancien") && has(after, "## La mesure de la semaine"),
              "la section du document est réécrite entre ses marqueurs, le reste intact");
        check(!has(after.substr(0, after.find("Conseillé")), "llama.cpp"), "la section du document ne montre que Gulliver");
    }

    // ---- Cas 2 : avec l'épreuve ----------------------------------------------------------
    {
        std::vector<std::string> raw2 = raw;
        rows(raw2, "f", "Gemma F 12B", "1", "gpu", { { "512", "q8", "2000", "70" }, { "4096", "q8", "1800", "60" } });
        rows(raw2, "g", "Qwen3 G 8B abliterated", "1", "gpu", { { "512", "q8", "2200", "75" }, { "4096", "q8", "1900", "65" } });
        rows(raw2, "h", "Faible H 7B", "0", "gpu", { { "512", "q8", "2100", "72" }, { "4096", "q8", "1900", "60" } });
        rows(raw2, "i", "Actif I 7B", "0", "gpu", { { "512", "q8", "2100", "71" }, { "4096", "q8", "1900", "60" } });
        raw2.insert(raw2.end(), { "quiz|a|80", "quiz|b|85", "quiz|c|60", "quiz|d|88", "quiz|e|50", "quiz|f|93",
                                  "quiz|g|97", "quiz|h|45", "quiz|i|30", "quiz|llama.cpp:a|99" });

        gulliver::conseil_options opt;
        opt.previous = "a";
        opt.keep = { "i" };
        opt.removed = parse(R"([{"date":"2026-11-17 12:40","id":"z","name":"Ancien Z"}])");

        const std::string text = join(raw2);
        json out = gulliver::build_conseil(text, opt, "2026-11-24 10:00");
        const std::string page = gulliver::build_conseil_markdown(text, opt, "2026-11-24 10:00");

        json adv = out["advice"];
        std::map<std::string, std::string> roles;
        int g_count = 0;
        for (size_t i = 0; i < adv.size(); ++i)
        {
            const std::string id = adv[i]["id"].as_string();
            roles[adv[i]["role"].as_string()] = id;
            if (id == "g") ++g_count;
        }
        auto role = [&](const std::string& r) { return roles.count(r) ? roles[r] : std::string(); };

        check(role("general") == "f", "épreuve : pour tout faire, F, la meilleure note des modèles assez rapides sur le GPU");
        check(!roles.count("accurate") || role("accurate") == "f", "épreuve : le plus juste se compare entre familles (F, pas D)");
        check(role("cyber") == "g" && g_count == 1, "épreuve : l'abliterated G n'est conseillé que pour la cybersécurité");
        check(out["remove"].is_object() && out["remove"]["id"].as_string() == "h",
              "le dernier du classement H est désigné, pas l'actif I");
        check(out["removed"].is_array() && out["removed"].size() == 1
              && out["removed"][static_cast<size_t>(0)]["id"].as_string() == "z",
              "la liste des modèles déjà retirés est recopiée");
        check(has(page, "| 93/100 |") && has(page, "retiré de Gulliver : **Faible H 7B**"),
              "la note est dans le tableau et le retrait est dit");
    }

    std::fprintf(stdout, "\ntest_conseil_cli : %s\n", failures ? "des vérifications ont échoué" : "all tests passed");
    return failures ? 1 : 0;
}
