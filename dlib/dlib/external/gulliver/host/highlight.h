// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_HIGHLIGHT_H_
#define GULLIVER_HOST_HIGHLIGHT_H_

/*
    La coloration du code des réponses, côté service (fork pour macOS, 2026-11-25).  Elle remplace
    highlight.js dans le navigateur : la page envoie le texte d'un bloc de code et le nom de son
    langage, le service rend son HTML, le texte échappé dans des <span class="hljs-…">, les mêmes
    classes que highlight.js, que les thèmes GitHub de la page (web/vendor/highlight) colorent.

    Un découpeur par langage, réglé par une fiche (mots-clés, types, littéraux, commentaires,
    chaînes), et quelques découpeurs à part (HTML et XML, CSS, JSON, YAML, diff).  Il ne vise pas
    l'exactitude d'un analyseur : il colore les mots-clés, les chaînes, les commentaires, les
    nombres, les noms de fonctions et de classes, comme highlight.js le fait.
*/

#include <string>
#include <vector>

namespace gulliver
{
    // Le nom canonique d'un langage d'après le nom d'un bloc de code ("C++", "py", "sh"…) ; "" s'il est inconnu.
    std::string highlight_language(const std::string& name);

    // Le langage le plus probable d'un code sans nom, d'après ses mots et ses signes ; "" quand rien ne ressort.
    std::string detect_language(const std::string& code);

    /*
        Le HTML d'un bloc de code : le texte échappé, ses parties colorées dans des <span class="hljs-…">.
        language est un nom de bloc ("cpp", "Python"…) ; vide, le langage est deviné (detect_language).
        Un langage inconnu ou introuvable rend le texte échappé seul.  used reçoit le langage employé,
        "" pour aucun.
    */
    std::string highlight_code(const std::string& code, const std::string& language, std::string* used = nullptr);

    // Les noms canoniques des langages colorés.
    std::vector<std::string> highlight_languages();
}

#endif // GULLIVER_HOST_HIGHLIGHT_H_
