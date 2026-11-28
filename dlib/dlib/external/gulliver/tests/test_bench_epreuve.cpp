// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le test du cœur de l'épreuve (bench/epreuve_core.h, Gulliver 7.21.0), sur des réponses inventées qui couvrent les
    cas délicats de l'outil Python qu'il remplace : la dernière « Réponse : » (même en majuscules accentuées), la
    dernière ligne à défaut, les formules (\frac, \boxed, \( \), $), les guillemets et la ponctuation finale, les
    nombres à la française (espaces fines, virgule), les mots sans accents ni majuscules et les mots interdits, les
    motifs ancrés au début, la réponse exacte, une sortie d'engine_chat découpée en blocs, et la note arrondie au
    pair.  Les attentes ont été vérifiées avec les fonctions de tools/epreuve_modeles.py.
*/

#include <cstdio>
#include <string>

#include "../bench/epreuve_core.h"

using gulliver::json;

namespace
{
    int failures = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s%s\n", ok ? "ok   " : "FAIL ", what.c_str());
        if (!ok) ++failures;
    }

    void answer_is(const std::string& block, const std::string& expected)
    {
        const std::string got = gulliver::epreuve_final_answer(block);
        check(got == expected, "réponse « " + expected + " » (lu : « " + got + " »)");
    }

    gulliver::epreuve_question q(const std::string& type, const std::string& expected, const std::string& forbidden = "null")
    {
        gulliver::epreuve_question e;
        e.id = "essai";
        e.type = type;
        json::parse(expected, e.expected);
        json::parse(forbidden, e.forbidden);
        return e;
    }
}

int main()
{
    answer_is("Je calcule.\nRéponse : 42", "42");
    answer_is("réponse : 3\nEn fait non.\nRÉPONSE FINALE : **7**.", "7");
    answer_is("Reponse:   Paris !", "Paris");
    answer_is("Sans marque\n\nla dernière ligne  \n\n", "la dernière ligne");
    answer_is("Réponse : \\(\\frac{1}{6}\\)", "1/6");
    answer_is("Réponse : $\\dfrac{3}{4}$", "3/4");
    answer_is("Réponse : \\boxed{12}", "12");
    answer_is("Réponse : « bonjour »", "bonjour");
    answer_is("Réponse :\n\nla suite", "la suite");
    answer_is("Réponse : ", "");
    answer_is("", "");

    check(gulliver::epreuve_plain("Élève À l’ÉCOLE") == "eleve a l'ecole", "sans accents ni majuscules, apostrophe droite");

    check(gulliver::epreuve_grade(q("nombre", "1234.5"), "1 234,5 euros"), "nombre à la française, espace");
    check(gulliver::epreuve_grade(q("nombre", "1234.5"), "1 234,5"), "nombre à la française, espace fine");
    check(gulliver::epreuve_grade(q("nombre", "-3"), "environ -3 degrés"), "nombre négatif");
    check(!gulliver::epreuve_grade(q("nombre", "7"), "sept"), "un nombre en lettres n'est pas lu");
    check(!gulliver::epreuve_grade(q("nombre", "12"), "1 et 12"), "seul le premier nombre compte");
    check(gulliver::epreuve_grade(q("mot", R"(["Hugo"])"), "c'est Victor HUGO"), "mot sans majuscules");
    check(gulliver::epreuve_grade(q("mot", R"(["été"])"), "en ETE"), "mot sans accents");
    check(!gulliver::epreuve_grade(q("mot", R"(["mer"])"), "la merveille"), "un mot entier seulement");
    check(!gulliver::epreuve_grade(q("mot", R"(["Paris"])", R"(["Lyon"])"), "Paris ou Lyon"), "un mot interdit refuse");
    check(gulliver::epreuve_grade(q("motif", R"("^1 ?/ ?6$")"), "1 / 6"), "motif");
    check(!gulliver::epreuve_grade(q("motif", R"("^1 ?/ ?6$")"), "x 1/6"), "motif ancré au début");
    check(gulliver::epreuve_grade(q("motif", R"("1/6")"), "1/6 environ"), "motif sans fin imposée");
    check(gulliver::epreuve_grade(q("exact", R"(["ruetanidro"])"), "ruetanidro"), "réponse exacte");
    check(!gulliver::epreuve_grade(q("exact", R"(["ruetanidro"])"), "Ruetanidro"), "exacte : majuscules comptées");

    std::vector<gulliver::epreuve_question> qs = {q("nombre", "2"), q("mot", R"(["oui"])"), q("nombre", "5")};
    qs[0].id = "a";
    qs[0].question = "Combien font 1+1 ?";
    qs[1].id = "b";
    qs[1].question = "Oui ou non ?";
    qs[2].id = "c";
    qs[2].question = "Sans réponse ?";
    const std::string output = "début du journal\n=== Combien font 1+1 ?\nLe calcul.\nRéponse : 2\n--- {\"tokens\": 9}\n"
                               "=== Oui ou non ?\nRéponse : Oui.\n--- {\"tokens\": 4}\n";
    const auto blocks = gulliver::epreuve_blocks(output, qs);
    check(blocks.size() == 3 && blocks[0] == "Le calcul.\nRéponse : 2" && blocks[1] == "Réponse : Oui." && blocks[2].empty(),
        "les blocs de la sortie d'engine_chat");
    const auto r = gulliver::epreuve_score(output, qs);
    check(r.right == 2 && r.total == 3 && r.score == 67 && r.any_answer, "la note : 2 justes sur 3, 67");
    check(r.details[2]["answer"].str().empty() && !r.details[2]["ok"].as_bool(), "une question sans bloc est fausse");
    qs.push_back(q("nombre", "1"));
    qs.back().question = "Encore ?";
    check(gulliver::epreuve_score(output, qs).score == 50, "2 sur 4 : 50");
    std::vector<gulliver::epreuve_question> eight(8, qs[0]);
    check(gulliver::epreuve_score(output, std::vector<gulliver::epreuve_question>(eight.begin(), eight.begin() + 8)).score == 100,
        "huit fois la même question : 100");
    std::vector<gulliver::epreuve_question> forty(40, qs[2]);
    forty[0] = qs[0];
    check(gulliver::epreuve_score(output, forty).score == 2, "1 sur 40 : 2,5 arrondi au pair, 2");
    forty[1] = qs[0];
    forty[2] = qs[0];
    check(gulliver::epreuve_score(output, forty).score == 8, "3 sur 40 : 7,5 arrondi au pair, 8");
    check(gulliver::epreuve_prefix("élève", 3) == "élè", "un préfixe compte les caractères, pas les octets");

    std::printf("test_bench_epreuve: %s\n", failures == 0 ? "all tests passed" : (std::to_string(failures) + " tests failed").c_str());
    return failures == 0 ? 0 : 1;
}
