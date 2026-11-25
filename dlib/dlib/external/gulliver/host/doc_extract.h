// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_DOC_EXTRACT_H_
#define GULLIVER_HOST_DOC_EXTRACT_H_

/*
    L'extraction du texte des documents, côté service (fork pour macOS).  Portée depuis la page
    (web/js/documents.js) pour que le navigateur n'ait plus à lire les formats lui-même : il
    envoie les octets du fichier, le service rend le texte.

    Formats :
      - PDF : par PDFKit, le cadriciel natif de macOS (depuis la 7.3.0 ; PDFium abandonnée, plus
        rien à embarquer).  Un PDF sans aucun texte lisible (un PDF scanné, fait d'images) lève
        l'erreur "pdf-images" — ses pages sont alors dessinées par host/pdf_render.cpp
        (CoreGraphics), côté service aussi, pour le modèle de vision.
      - Office (docx, pptx, xlsx) et OpenDocument (odt, odp, ods) : par lecture du ZIP et du XML.
      - RTF, HTML, texte brut : lecture directe.

    extract_text() rend le texte, ou lève doc_error avec un code (les mêmes que la page :
    "pdf-images", "pdf-password", "pdf-invalid", "too-large", "unsupported", …).  "pdf-reader"
    ne peut plus se produire : PDFKit est toujours là sur un Mac.
*/

#include <stdexcept>
#include <string>

namespace gulliver
{
    // L'échec d'une extraction, avec un code court (voir doc_extract.h) que la page connaît déjà.
    struct doc_error : std::runtime_error
    {
        std::string code;
        doc_error(const std::string& code_, const std::string& message)
            : std::runtime_error(message), code(code_) {}
    };

    // Le texte d'un document, deviné d'après son nom (extension) et ses octets.  Lève doc_error
    // en cas d'échec.  name sert à nommer l'extension et les pages/feuilles dans le texte.
    std::string extract_text(const std::string& name, const std::string& bytes);

    // Le texte d'un PDF (ses octets), page par page, séparées par une ligne de titre "\f" + nom.
    // Lève doc_error("pdf-images", …) si aucune page ne contient de texte (PDF scanné), avec le
    // nombre de pages dans le message.
    std::string extract_pdf_text(const std::string& bytes);

    // Vrai si le service sait lire les PDF.  Toujours vrai sur macOS (PDFKit, cadriciel système) ;
    // gardé pour que host/server.cpp n'ait rien à changer.
    bool pdf_available();
}

#endif // GULLIVER_HOST_DOC_EXTRACT_H_
