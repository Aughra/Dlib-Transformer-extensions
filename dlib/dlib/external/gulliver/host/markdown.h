// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_MARKDOWN_H_
#define GULLIVER_HOST_MARKDOWN_H_

/*
    Le Markdown des réponses en HTML, côté service (fork pour macOS, 2026-11-27).  Il remplace marked
    et DOMPurify dans le navigateur : un analyseur écrit pour Gulliver, du Markdown à la GitHub
    (titres, paragraphes, listes et cases à cocher, citations, blocs de code, tableaux, liens, images,
    gras, italique, barré, surligné entre == et ==, liens nus).

    Le HTML rendu est sûr par construction : tout texte est échappé, le HTML écrit par un modèle
    s'affiche comme du texte, un lien ne prend que http, https, mailto ou une adresse de la page,
    s'ouvre dans un autre onglet sans transmettre la page, et une image qui ne vient pas de la page
    (adresse de la page, data:image, blob:) devient un lien vers elle.

    Les blocs de code sont colorés au passage (host/highlight.cpp).  Pour une réponse en cours
    d'écriture (live), un bloc dont la clôture manque et un bloc sans langage restent en texte simple,
    comme un bloc marqué text ou output ; une fois la réponse écrite, un bloc sans langage de moins de
    20 000 caractères voit son langage deviné.
*/

#include <string>

namespace gulliver
{
    struct markdown_options
    {
        bool live = false;     // une réponse en cours d'écriture
    };

    std::string markdown_to_html(const std::string& text, const markdown_options& options = markdown_options());
}

#endif // GULLIVER_HOST_MARKDOWN_H_
