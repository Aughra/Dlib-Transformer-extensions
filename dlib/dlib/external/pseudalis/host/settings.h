// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_SETTINGS_H_
#define PSEUDALIS_HOST_SETTINGS_H_

#include <functional>
#include <mutex>
#include <string>

#include "../common/json.h"

namespace pseudalis
{
    struct host_settings
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les réglages du service, gardés dans settings.json dans le dossier de
                données et modifiés dans l'application web.  Le service redémarre de lui-même quand
                le port, le lecteur, l'ordre des dates ou le proxy changent.
        !*/
        int port = 8444;
        std::string language = "auto";          // from_json garde auto, en et fr ; auto suit la langue du service
        std::string drive_letter = "A";         // la lettre du lecteur, vide sans lecteur
        std::string date_order = "dmy";         // dmy ou mdy : les dates numériques que lit le détecteur
        std::string albireo = "auto";           // auto : les images sont décrites quand Albireo répond ; off
        int max_file_mb = 150;                  // le plus gros fichier traité, en Mo
        /*
            Les jours pendant lesquels un original reste gardé une fois que son fichier a quitté le
            lecteur, 0 pour toujours ; quels que soient les jours, les originaux gardés ne prennent
            jamais plus que history_mb, ni ne laissent au disque du dossier de données moins de
            256 Mo libres : les plus anciens partent d'abord (service::make_room()).
        */
        int removed_days = 45;
        static const int max_removed_days = 3650;
        int history_mb = 10240;                 // l'espace permis aux originaux gardés, en Mo, max_file_mb au moins
        static const int max_history_mb = 1048576;
        /*
            Si les valeurs des fichiers prennent leurs tags, qui les rendent à partir de n'importe quelle
            copie avec la clé ; false écrit leurs seuls substituts, que seul l'enregistrement du fichier
            rend tant qu'il dure : le lecteur, l'application web et l'interface le prennent sauf si une
            requête le précise.
        */
        bool tags = true;
        bool trust_authority = true;            // l'autorité locale dans les racines de confiance (l'installateur)
        std::string proxy;                      // "", "none" ou http://host:port (http_fetch.h)

        json to_json() const;
        static host_settings from_json(const json& j);
    };

    // Si une lettre de lecteur est de celles que le lecteur peut prendre : D à Z, ou A et B, sans deux-points.
    bool valid_drive_letter(const std::string& letter);

    class settings_store
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                settings.json, lu au démarrage et écrit de façon atomique à chaque
                changement, hors du verrou que prend get().  Un fichier qui n'est plus du JSON
                valide est remplacé par les valeurs par défaut, et mis de côté sous le nom
                settings.json.invalid ; un fichier qui ne peut pas être lu reste tel quel.
                Dans les deux cas, les originaux des fichiers retirés du lecteur sont gardés
                sans limite de temps (removed_days 0) jusqu'à ce que les réglages soient de
                nouveau définis, dans l'espace de history_mb.
        !*/
    public:
        explicit settings_store(const std::string& path);

        host_settings get() const;
        bool set(const host_settings& s);
        // Applique change aux réglages actuels et les écrit en une seule étape : result les
        // reçoit ; false quand ils n'ont pas pu être écrits.
        bool update(const std::function<void(host_settings&)>& change, host_settings& result);
        const std::string& path() const { return path_; }

    private:
        bool write(const host_settings& s);
        std::string path_;
        mutable std::mutex m_;
        std::mutex write_m_;
        host_settings s_;
    };
}

#endif // PSEUDALIS_HOST_SETTINGS_H_
