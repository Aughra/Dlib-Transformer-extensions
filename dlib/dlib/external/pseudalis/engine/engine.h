// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_ENGINE_ENGINE_H_
#define PSEUDALIS_ENGINE_ENGINE_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../common/imposed_terms.h"
#include "../common/json.h"

namespace pseudalis
{
    /*
        Les types de fichiers que le moteur traite : les documents de dlib/privacy (texte brut,
        valeurs délimitées, balisage, Word, Excel, PowerPoint et PDF), les images (PNG, JPEG,
        GIF, BMP, TIFF, WebP), dont la place est prise par un texte qui les décrit, et les
        archives (ZIP, TAR, TAR.GZ) qui les contiennent.
    */
    enum class file_kind { unsupported, document, picture, archive };

    struct picture_found
    {
        std::string id;           // 16 chiffres hexadécimaux d'une empreinte des octets calculée avec la clé
        std::string sha256;
        std::string media_type;   // "image/png"...
        std::string location;     // le fichier, l'entrée d'une archive, la partie ou la page
        const std::string* bytes = nullptr;
    };

    // Reçoit chaque image trouvée et renvoie sa description en clair, ou une chaîne vide.
    typedef std::function<std::string(const picture_found&)> picture_handler;

    /*
        Renvoie les étiquettes enregistrées pour un substitut égal à candidate, la plus probable
        d'abord : les noms et les propriétés des documents contiennent des substituts, des valeurs
        écrites sans leurs étiquettes, que l'appelant enregistre quand elles sont créées
        (engine_result::substitutions).
    */
    typedef std::function<std::vector<std::string>(const std::string& candidate)> substitute_lookup;

    struct substitution
    {
        std::string substitute;             // tel qu'écrit dans un nom, une propriété, un texte sans étiquettes
        std::string tag;                    // l'étiquette qu'il représente
        bool text = false;                  // écrit dans le texte d'un document protégé sans étiquettes
    };

    struct engine_options
    {
        std::string date_order = "dmy";     // ou "mdy" : l'ordre des dates numériques
        std::string context = "default";    // le contexte de la pseudonymisation
    };

    /*
        Une entrée qu'anonymize() a écrite dans une archive : son chemin dans l'archive lue, son
        chemin dans l'archive écrite, le SHA-256 des octets écrits et son type.  Les entrées d'une
        archive contenue dans l'archive la suivent, leurs chemins après le sien et '!'
        ("dossier/notes.tar.gz!a.txt").
    */
    struct entry_written
    {
        std::string original;
        std::string name;
        std::string sha256;
        std::string kind;
    };

    struct engine_result
    {
        std::string bytes;                  // le fichier écrit
        std::string name;                   // son nom, protégé : "photo.png.txt" pour une image
        std::string kind;                   // "txt", "csv", "xml", "docx", ..., "image", "zip", "tar", "tar.gz"
        json report = json::object();
        std::vector<std::string> pictures;  // les identifiants des images trouvées, une fois chacun
        std::vector<substitution> substitutions;   // anonymize() : les substituts écrits
        std::vector<entry_written> entries;        // anonymize() d'une archive : ses entrées écrites
    };

    class engine
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'anonymisation et la restauration de fichiers avec le détecteur de
                dlib/privacy, ses règles et ses deux reconnaisseurs appris (l'étiqueteur de
                séquences et le modèle d'empans compilés dans le moteur), et une clé.
                Chaque valeur trouvée est remplacée par son étiquette [[TYPE:body#CCC]],
                chiffrée sous la clé, pour que restore() la rende ; les dates se décalent d'un
                écart secret.

                Une image devient "[Image@<id> : <description>]", la description protégée
                comme tout texte, ou "[Image@<id>]" sans description : à sa place dans un
                document Word ou sur une diapositive, par-dessus son cadre gris dans un PDF,
                sous la forme du fichier texte <name>.txt pour une image seule ou dans une
                archive.  Les archives gardent leurs documents, protégés, et leurs images sous
                forme de fichiers texte ; les entrées d'un autre type sont laissées de côté et
                listées dans report["dropped"], et une restauration les garde telles quelles.
                Un fichier est du type que donne son nom quand son contenu est de ce type, et
                un fichier de tout autre nom est un texte quand son contenu est du texte de
                bout en bout (kind_of()) ; une restauration lit aussi un texte sous n'importe
                quel nom, puisqu'il peut contenir des étiquettes.

                Les noms des fichiers, des entrées des archives et de leurs dossiers, et les
                propriétés des documents (titre, auteurs, sujet, mots-clés, société... de
                Word, Excel et PowerPoint, les informations de document d'un PDF) prennent des
                substituts plutôt que des étiquettes : chaque valeur est écrite sous sa forme
                protégée sans les crochets, le type et le contrôle de son étiquette, qu'un nom
                ne peut pas contenir et qu'une fenêtre de propriétés montrerait tels quels.  Le
                nom d'un document est lu avec ce que son contenu a appris au détecteur.  Un
                substitut ne contient aucun caractère que Windows refuse dans un nom.
                engine_result::substitutions liste chacun avec son étiquette, que l'appelant
                enregistre : restore() les demande à lookup.

                Sans étiquettes (anonymize() et anonymize_text() avec tags à false), chaque
                valeur du texte prend aussi son substitut : le fichier ne contient aucune
                étiquette, et ses valeurs ne reviennent que grâce aux substituts que l'appelant
                garde (engine_result::substitutions, text à true) et rend à restore().

            SÛRETÉ DES THREADS
                Les méthodes peuvent être appelées depuis plusieurs threads à la fois.
        !*/
    public:
        // key : 32 octets.  Lève std::runtime_error quand les modèles ne peuvent pas être lus.
        engine(const std::string& key, const engine_options& options);

        static file_kind kind_of_name(const std::string& name);
        /*
            Le type d'un fichier : le type que donne son extension (common/file_kinds.h) quand le
            contenu est de ce type, un document Office, un PDF ou un texte pour un document, toute
            archive pour une archive.  Un nom dont l'extension ne donne aucun type, ou sans extension,
            est un texte quand son contenu est du texte de bout en bout : dlib n'y trouve aucune
            signature binaire et y trouve les mesures d'un texte (dlib/data_io), chaque caractère se
            décode sans caractère de contrôle hormis la tabulation, les fins de ligne et le saut de
            page, ni U+FFFE ou U+FFFF, et il ne contient aucune partie encodée.  Une partie encodée est
            un bloc de base64, de base64url ou de chiffres hexadécimaux (1024 caractères d'affilée, ou
            16 suites d'affilée de 60, les lignes d'un bloc, y compris dans une chaîne de JSON ou d'un
            programme, ce que ne forme pas une ligne de tirets ou un chemin), des données ou du JSON en
            base64, des octets en hexadécimal, un vidage ou un tableau (64 d'affilée, ou 4 lignes de
            8, avec des lettres ou des signes au milieu), ou un format qui écrit ses valeurs encodées :
            un message avec des parties en base64, quoted-printable ou uuencode, ou avec des mots
            encodés, un document RTF, une vCard ou un iCalendar, du LDIF en base64, un bloc blindé par
            PEM ou OpenPGP, son en-tête et son corps, y compris dans une chaîne, un bloc de uuencode
            ou d'ASCII85, et, d'après sa première ligne, un programme PostScript ou un export du
            registre.  Sans extension, une image, un document Office, une archive ou un PDF est aussi
            reconnu à sa signature.  Les autres fichiers ne sont pas pris en charge : un document
            OpenDocument, ou un e-mail avec des pièces jointes, ressortirait à moitié protégé.
        */
        static file_kind kind_of(const std::string& bytes, const std::string& name);
        static std::string picture_marker(const std::string& id, const std::string& description);

        /*
            Lève std::runtime_error pour un fichier d'aucun type pris en charge, ou illisible.  Les
            substituts que lookup connaît, dans un nom ou une propriété protégés auparavant, ne sont
            pas remplacés une seconde fois.  Sans étiquettes, chaque valeur du texte prend son
            substitut, comme le font les noms et les propriétés, descriptions des images comprises,
            et engine_result::substitutions la liste avec text à true, une fois par emplacement où
            elle est écrite : sa valeur ne revient que grâce aux substituts que l'appelant garde.
            Les substituts de known, ceux d'un fichier protégé auparavant sans étiquettes, restent
            tels quels.
        */
        engine_result anonymize(const std::string& bytes, const std::string& name,
            const picture_handler& pictures, const substitute_lookup& lookup = substitute_lookup(),
            bool tags = true, const std::vector<substitution>& known = std::vector<substitution>()) const;
        /*
            Remet les valeurs des étiquettes, celles des substituts que lookup connaît dans les noms
            et les propriétés, et celles des substituts de text dans le texte, pour un fichier
            protégé sans étiquettes.  Une image ne peut pas revenir dans un document : son marqueur
            s'en va avec les étiquettes de sa description (d'un passage de texte Word, avec la zone
            de texte grise d'une diapositive, de par-dessus le cadre gris d'un PDF, qui reste), de
            même que le fichier texte d'une image d'une archive, report["pictures_removed"] les
            comptant ; le service rend l'image d'un fichier texte seul depuis son coffre.
        */
        engine_result restore(const std::string& bytes, const std::string& name,
            const substitute_lookup& lookup = substitute_lookup(),
            const std::vector<substitution>& text = std::vector<substitution>()) const;
        /*
            Les octets de l'entrée située à path dans l'archive bytes nommée name, lue comme
            anonymize() la lit : path est entry_written::original, les archives contenues jointes
            par '!'.  Lève std::runtime_error quand l'archive ne contient pas une telle entrée.
        */
        std::string extract(const std::string& bytes, const std::string& name, const std::string& path) const;

        // Un nom seul, un dossier par exemple : ses valeurs deviennent des substituts, son extension
        // reste.
        std::string anonymize_name(const std::string& name, const substitute_lookup& lookup,
            std::vector<substitution>& made, json& report) const;
        std::string restore_name(const std::string& name, const substitute_lookup& lookup, json& report) const;

        // Sans étiquettes, les valeurs prennent leurs substituts, que made reçoit.
        std::string anonymize_text(const std::string& text, json& report, bool tags = true,
            std::vector<substitution>* made = nullptr) const;
        // Un texte garde les marqueurs d'image qu'il contient, que son lecteur voit ; les substituts
        // reviennent aussi.
        std::string restore_text(const std::string& text, json& report,
            const std::vector<substitution>& substitutes = std::vector<substitution>()) const;
        /*
            [{"begin", "end", "type"}] des étiquettes d'un texte protégé que cette clé restaure et
            des substituts de la liste, et des valeurs que restore_text() a écrites dans restored à
            leur place dans tagged : des décalages en octets dans le texte, pour la coloration de
            l'application web. Les valeurs sont données quand les étiquettes et les substituts
            suffisent à expliquer la restauration (une étiquette écrite dans un lien, que la
            restauration lit aussi, n'en donne aucune).
        */
        json tag_spans(const std::string& text,
            const std::vector<substitution>& substitutes = std::vector<substitution>()) const;
        json restored_spans(const std::string& tagged, const std::string& restored,
            const std::vector<substitution>& substitutes = std::vector<substitution>()) const;
        // [{"begin", "end", "type", "confidence", "text"}] des valeurs d'un texte.
        json detect(const std::string& text) const;

        /*
            Remplace les termes imposés (common/imposed_terms.h), que tout fichier, texte et nom
            traité ensuite lit avant tout le reste ; un appel en cours garde les termes avec
            lesquels il a commencé pour les détecteurs qu'il a déjà créés.
        */
        void set_terms(const std::vector<imposed_term>& terms);
        std::size_t term_count() const;

        std::string key_fingerprint() const;
        // {"tagger", "span_model", "size"}
        static json models();

    private:
        struct impl;
        std::shared_ptr<impl> p_;
    };
}

#endif // PSEUDALIS_ENGINE_ENGINE_H_
