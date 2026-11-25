// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_PDF_RENDER_H_
#define GULLIVER_HOST_PDF_RENDER_H_

/*
    Le dessin des pages d'un PDF en images, côté service (fork pour macOS, 2026-11-25).  Il
    remplace pdf.js dans le navigateur : les pages d'un PDF numérisé deviennent des images JPEG
    pour un modèle qui lit les images.  Le dessin passe par le moteur PDF de macOS (CoreGraphics,
    celui de PDFKit et d'Aperçu) ; le texte, lui, est lu à part (doc_extract_pdf).
*/

#include <string>
#include <vector>

namespace gulliver
{
    // Une page dessinée : l'image JPEG et sa taille en pixels.
    struct rendered_page
    {
        std::string jpeg;
        int width = 0;
        int height = 0;
    };

    /*
        Dessine les premières pages du PDF bytes, limit au plus, sur fond blanc, chacune à l'échelle
        où son plus grand côté fait side pixels.  total reçoit le nombre de pages du fichier.  Lève
        doc_error (doc_extract.h) : "pdf-invalid" pour un fichier illisible, "pdf-password" pour
        un PDF protégé par un mot de passe, "pdf-render" si une page ne se dessine pas.
    */
    std::vector<rendered_page> render_pdf_pages(const std::string& bytes, int side, int limit, int& total);
}

#endif // GULLIVER_HOST_PDF_RENDER_H_
