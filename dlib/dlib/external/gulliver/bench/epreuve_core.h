// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_BENCH_EPREUVE_CORE_H_
#define GULLIVER_BENCH_EPREUVE_CORE_H_

/*
    L'épreuve des modèles de Gulliver : une note sur 100, comparable entre toutes les familles (Gulliver 7.21.0, à la
    place de tools/epreuve_modeles.py ; épreuve demandée par l'humain le 2026-11-24).

    La perplexité ne compare que des modèles qui découpent le texte de la même façon, et bien deviner un texte n'est
    pas bien répondre.  Le modèle reçoit les 40 questions de tools/epreuve_questions.json (calcul, logique,
    connaissances, français, consignes, compréhension, code), toutes à la fois, par le moteur de Gulliver, avec une
    consigne : finir chaque réponse par une ligne « Réponse : … ».  Ce cœur lit les réponses dans la sortie de
    tests/engine_chat et les note, selon le type de la question :

        nombre   le premier nombre de la réponse vaut « attendu » (à 1e-6 près ; 1 234,5 et 1234.5 se lisent)
        exact    la réponse est l'une des chaînes de « attendu »
        motif    la réponse commence par le motif « attendu » (re.match de Python)
        mot      l'un des mots de « attendu » y est, aucun de « interdit », sans accents ni majuscules

    Les expressions sont celles de l'outil Python, passées par NSRegularExpression (ICU) : mêmes classes Unicode
    (\s, \d), même repli des majuscules accentuées (« RÉPONSE »).
*/

#include <string>
#include <vector>

#include "../common/json.h"

namespace gulliver
{
    struct epreuve_question
    {
        std::string id, theme, type, question;
        json expected;      // « attendu » : un nombre, une liste de chaînes, ou un motif
        json forbidden;     // « interdit » : une liste de mots (type « mot »), ou null
    };

    // Les questions de epreuve_questions.json ({"questions": [...]}) ; vide si le fichier ne se lit pas.
    std::vector<epreuve_question> load_epreuve_questions(const std::string& path);

    // Le texte de chaque réponse dans la sortie d'engine_chat (« === question », la réponse, « --- {…} »), dans
    // l'ordre des questions ; "" pour une question sans réponse.
    std::vector<std::string> epreuve_blocks(const std::string& output, const std::vector<epreuve_question>& questions);

    // La réponse finale : ce qui suit la dernière « Réponse : », sinon la dernière ligne non vide, formules lues
    // (\frac{1}{6} → 1/6) et délimiteurs effacés.
    std::string epreuve_final_answer(const std::string& block);

    // Sans accents (marques non espaçantes après décomposition) ni majuscules, apostrophes droites.
    std::string epreuve_plain(const std::string& text);

    bool epreuve_grade(const epreuve_question& q, const std::string& answer);

    // Au plus n caractères (points de code) de text, comme text[:n] en Python.
    std::string epreuve_prefix(const std::string& text, size_t n);

    struct epreuve_result
    {
        int score = 0, right = 0, total = 0;
        json details;       // [{id, theme, ok, answer}] dans l'ordre des questions
        bool any_answer = false;
    };

    // La note de la sortie d'engine_chat : 100 × justes / total, arrondi au pair (round de Python).
    epreuve_result epreuve_score(const std::string& output, const std::vector<epreuve_question>& questions);
}

#endif // GULLIVER_BENCH_EPREUVE_CORE_H_
