// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_CONTENT_GUARD_H_
#define GULLIVER_HOST_CONTENT_GUARD_H_

/*
    Le contrôle des documents à leur entrée (fork pour macOS, 2026-11-27), selon le guide de l'ANSSI
    « Recommandations de sécurité pour un système d'IA générative à enrichissement contextuel (RAG) »
    (ANSSI-PA-119, 13/10/2026), recommandation R25 : normaliser le texte et repérer les motifs connus
    d'injection indirecte avant de l'indexer ou de le donner au modèle.

    guard_clean() retire du texte les caractères qu'un lecteur ne voit pas : caractères de contrôle,
    espaces de largeur nulle, marques et forçages de sens d'écriture, caractères « étiquettes »
    (U+E0000 à U+E007F, qui écrivent un texte ASCII invisible) et sélecteurs de variante étendus
    (U+E0100 à U+E01EF).  Les jointures U+200C et U+200D (persan, émojis composés) restent.

    guard_inspect() repère, sans rien changer :
      - "hidden" : des caractères invisibles qui servent à cacher un texte (étiquettes, sélecteurs
        étendus, forçages de sens d'écriture), comptés par guard_clean() ;
      - "instructions" : une phrase qui demande d'ignorer ou d'oublier les consignes (« ignore les
        consignes précédentes », "disregard all previous instructions"), en français et en anglais ;
      - "markup" : le balisage d'un modèle de discussion (<|im_start|>, [INST], <start_of_turn>…) ou
        celui des extraits de Gulliver (</excerpt>, </document>), qui imite une frontière du contexte.

    Ce contrôle réduit le risque sans le supprimer (le guide le dit de toute mesure de ce genre) : il
    sert à mettre en quarantaine et à prévenir, pas de frontière de sécurité.
*/

#include <cstddef>
#include <string>
#include <vector>

namespace gulliver
{
    struct guard_report
    {
        size_t removed = 0;               // caractères invisibles retirés
        size_t hidden = 0;                // parmi eux, ceux qui servent à cacher un texte
        std::vector<std::string> signs;   // "hidden", "instructions", "markup", sans doublon
    };

    // Le texte sans ses caractères invisibles ; report compte ce qui a été retiré.
    std::string guard_clean(const std::string& text, guard_report& report);

    // Ajoute à report.signs les signes d'injection trouvés dans text (déjà nettoyé de préférence).
    void guard_inspect(const std::string& text, guard_report& report);

    // Nettoie puis inspecte : le texte nettoyé, et report rempli.
    std::string guard_document(const std::string& text, guard_report& report);

    // Les signes séparés par des virgules ("" sans signe).
    std::string guard_codes(const guard_report& report);
}

#endif // GULLIVER_HOST_CONTENT_GUARD_H_
