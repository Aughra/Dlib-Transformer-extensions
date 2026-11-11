// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_COMMON_LOG_H_
#define ALBIREO_COMMON_LOG_H_

#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>

#include "json.h"

namespace albireo
{

    enum class log_level { debug = 0, info = 1, warning = 2, error = 3 };

    /*!
        ensures
            - Renvoie text avec chaque mot qui nomme l'espace de publication d'Albireo remplacé
              par "the publication space of Albireo" : les adresses et les noms des hôtes du
              service qui l'héberge (un mot contenant "pcloud" ou "publink", quelle que soit leur
              casse), et les mots contenant un texte donné à hide_in_messages().  Un mot se
              termine à une espace, un guillemet, un crochet, une virgule ou un point-virgule, et
              la ponctuation qui termine une phrase après lui reste.
            - Le journal (log_file), les erreurs que renvoient les routes du service, les
              fenêtres de la zone de notification et le journal de l'installateur font passer
              leurs textes par cette fonction, dernier garde-fou après les messages, qui nomment
              les fichiers de l'espace de publication et jamais ses adresses.
    !*/
    std::string without_publication_space(const std::string& text);

    // Si without_publication_space() modifie text.
    bool names_publication_space(const std::string& text);

    // Ajoute secret, le code ou le mot de passe de l'espace de publication, à ce que
    // without_publication_space() masque ; un texte de moins de 6 caractères est écarté, pour
    // qu'aucun mot courant ne soit masqué.
    void hide_in_messages(const std::string& secret);

    // Fait passer les lignes du fichier texte situé à path par without_publication_space(), le
    // fichier étant réécrit quand l'une d'elles change.
    void hide_publication_space_in_file(const std::string& path);

    // Si une chaîne de document sous un membre qui signale une erreur ("error", "errors", "args"), à
    // n'importe quelle profondeur, nomme l'espace de publication.
    bool errors_name_publication_space(const json& document);

    // Fait passer les chaînes de document sous les membres qui signalent une erreur par
    // without_publication_space().
    void hide_publication_space_in_errors(json& document);

    class log_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le journal d'un programme : des lignes avec leur heure et leur gravité, écrites
                dans un fichier renommé avec le suffixe ".1" quand il atteint sa taille limite,
                si bien que deux fichiers au plus sont gardés.  Les dernières lignes restent
                aussi en mémoire, pour les fenêtres qui les affichent.  Chaque message passe par
                without_publication_space(), et les fichiers qu'une version antérieure à
                celle-ci a écrits y passent quand ils sont ouverts.
        !*/
    public:
        static log_file& instance();

        void open(const std::string& path, uint64_t max_bytes = 4u << 20);
        void set_console(bool on) { console_ = on; }
        void set_level(log_level l) { level_ = l; }

        void write(log_level level, const std::string& message);
        std::deque<std::string> recent() const;

    private:
        log_file() {}
        mutable std::mutex m_;
        std::string path_;
        uint64_t max_bytes_ = 4u << 20;
        bool console_ = false;
        log_level level_ = log_level::info;
        std::deque<std::string> recent_;
    };

    inline void log_debug(const std::string& m) { log_file::instance().write(log_level::debug, m); }
    inline void log_info(const std::string& m) { log_file::instance().write(log_level::info, m); }
    inline void log_warning(const std::string& m) { log_file::instance().write(log_level::warning, m); }
    inline void log_error(const std::string& m) { log_file::instance().write(log_level::error, m); }

}

#endif // ALBIREO_COMMON_LOG_H_
