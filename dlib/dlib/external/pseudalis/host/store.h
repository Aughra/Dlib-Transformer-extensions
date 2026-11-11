// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_STORE_H_
#define PSEUDALIS_HOST_STORE_H_

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../common/json.h"

struct sqlite3;

namespace pseudalis
{
    struct file_record
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un fichier que le service a protégé et dont il garde l'original : le fichier
                protégé se retrouve par le SHA-256 de ses octets, que partage chaque copie, ou
                par l'identifiant d'objet NTFS qu'a reçu son fichier (un fichier remplacé sur
                place, renommé ou déplacé sur son volume depuis), quel que soit son nom.
        !*/
        long long id = 0;
        int64_t created = 0;
        std::string source;             // "drive", "shell", "web" ou "api"
        std::string original_name;
        std::string output_name;
        std::string original_sha256;
        std::string output_sha256;
        std::string original_blob;      // l'original dans le coffre
        long long original_size = 0;
        long long output_size = 0;
        std::string kind;
        std::string object_id;          // 32 chiffres hexadécimaux, ou vide
        std::string path;               // où le fichier protégé a été écrit, quand on le sait
        int64_t removed = 0;            // quand son fichier a quitté le lecteur (temps Unix), sinon 0
        bool tags = true;               // si le fichier protégé contient les tags de ses valeurs
    };

    struct entry_record
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Une entrée d'une archive protégée que la base de données enregistre : un
                fichier extrait de cette archive est reconnu par le SHA-256 de ses octets, et
                son original est l'entrée originale de l'archive gardée dans le coffre.  Les
                chemins des entrées d'une archive contenue dans l'archive suivent le sien et
                '!' (engine_api.h).
        !*/
        long long record = 0;           // l'enregistrement de l'archive
        std::string name;               // son chemin dans l'archive protégée
        std::string original;           // son chemin dans l'archive originale
        std::string sha256;             // de l'entrée écrite
        std::string kind;
    };

    struct picture_record
    {
        std::string id;                 // 16 chiffres hexadécimaux d'un condensat à clé (picture_found)
        std::string sha256;
        std::string media_type;
        std::string blob;               // l'image dans le coffre
        std::string description;        // la description d'Albireo chiffrée dans le coffre, ou vide
        int64_t created = 0;
    };

    struct folder_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un fichier du dossier du lecteur (drive.h) tel que le service l'a laissé :
                traité ou laissé tel quel, avec la taille et l'heure de dernière écriture qui
                disent s'il a changé depuis.  Un dossier du lecteur est enregistré aussi, dans
                l'état "folder", une fois son nom protégé.
        !*/
        std::string folder;             // "drive"
        std::string path;               // dans le dossier, '/' entre ses parties
        long long size = 0;
        int64_t written = 0;            // l'heure de sa dernière écriture, telle que la donne le système de fichiers
        std::string sha256;
        long long record = 0;           // l'enregistrement de son original
        std::string state;              // "protected", "kept" ou "folder"
    };

    class store
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La base de données du service, pseudalis.db dans un dossier de SYSTEM et des
                administrateurs : les fichiers protégés et leurs originaux, les entrées des
                archives protégées, les images et leurs descriptions, les fichiers protégés
                dont l'original a été oublié, les fichiers du lecteur et les événements
                récents.  Chaque méthode prend le verrou de la connexion, pour que les threads
                du serveur et du lecteur la partagent.
            THROWS
                std::runtime_error quand la base de données ne peut être ni ouverte ni écrite.
        !*/
    public:
        explicit store(const std::string& path);
        ~store();
        store(const store&) = delete;
        store& operator=(const store&) = delete;

        long long add_file(const file_record& r);
        bool get_file(long long id, file_record& r);
        // L'enregistrement d'un fichier protégé ; parmi plusieurs de même contenu, celui dont le nom protégé est name.
        bool find_by_output(const std::string& sha256, file_record& r, const std::string& name = std::string());
        bool find_by_object(const std::string& object_id, file_record& r);
        // Si un fichier protégé de cette taille est enregistré, ce qu'un fichier doit être pour en être un.
        bool output_size_known(long long size);
        std::vector<file_record> list_files(long long limit, long long offset,
            const std::string& source = std::string());
        // Les enregistrements, ceux d'une seule source quand source n'est pas vide ("web", "shell", "api", "drive").
        long long count_files(const std::string& source = std::string());
        // Les octets des originaux que gardent les enregistrements.
        long long originals_bytes();
        // Les enregistrements créés en premier, les plus anciens d'abord : leurs identifiants et la taille de leurs
        // originaux.
        std::vector<std::pair<long long, long long>> oldest_files(long long limit);
        // Les blobs que nomment les enregistrements et les images.
        std::set<std::string> referenced_blobs();
        void set_file_location(long long id, const std::string& object_id, const std::string& path);
        // Enregistre l'heure à laquelle le fichier d'un enregistrement a quitté le lecteur, supprimé ou remplacé.
        void set_file_removed(long long id, int64_t when);
        // Les enregistrements dont le fichier a quitté le lecteur à when ou avant.
        std::vector<long long> files_removed_before(int64_t when);
        /*!
            ensures
                - Supprime l'enregistrement, ses entrées et ses liens vers ses images ; blobs
                  reçoit les blobs auxquels plus rien ne renvoie : son original, et les images
                  qu'aucun autre fichier ne montre.
                - Avec une raison ("forgotten", "expired"), le fichier protégé de
                  l'enregistrement et les entrées d'une archive protégée sont mémorisés comme
                  oubliés (find_forgotten()), dans la même transaction : une copie extraite de
                  l'archive est restaurée à partir de ses tags aussi, sauf si un enregistrement
                  à elle la contient.
                - Les substituts du texte de l'enregistrement partent avec lui
                  (add_text_substitutes()).
        !*/
        bool remove_file(long long id, std::vector<std::string>& blobs, const std::string& reason = std::string());
        // Le dernier original oublié d'un fichier protégé, par son SHA-256 ou son identifiant d'objet.
        bool find_forgotten(const std::string& sha256, const std::string& object_id, int64_t& time,
            std::string& reason, bool* tags = nullptr);

        /*!
            ensures
                - Enregistre les substituts que le moteur a écrits dans le texte du fichier
                  protégé de record, un fichier protégé sans tags, chaque (substitut, tag) une
                  seule fois avec le nombre d'endroits qui le contiennent : ils partent avec
                  l'enregistrement (remove_file()), et avec eux le moyen de rendre ses valeurs.
        !*/
        void add_text_substitutes(long long record, const std::vector<std::pair<std::string, std::string>>& made);
        // Les substituts du texte de record, ou seulement ceux dont il a hérité (copy_text_substitutes()).
        std::vector<std::pair<std::string, std::string>> text_substitutes(long long record,
            bool inherited_only = false);
        /*!
            ensures
                - Donne à record to les substituts du texte de record from : ceux de l'archive
                  d'une entrée qui en est extraite, ou, hérités, ceux que contient l'original de
                  record to, un fichier protégé sans tags et protégé de nouveau, que sa
                  restauration rend.
        !*/
        void copy_text_substitutes(long long from, long long to, bool inherited = false);

        // Les entrées de l'archive protégée de record (entry_record).
        void add_entries(long long record, const std::vector<entry_record>& entries);
        // L'entrée écrite avec ce SHA-256, du plus récent enregistrement qui en contient une.
        bool find_entry(const std::string& sha256, entry_record& e);
        // Les entrées contenues dans l'entrée e de son enregistrement, une archive, leurs chemins pris à l'intérieur.
        std::vector<entry_record> entries_inside(const entry_record& e);

        bool get_picture(const std::string& id, picture_record& r);
        void add_picture(const picture_record& r);
        /*
            Donne à l'image id le blob de description description, avant de recevoir le blob qu'il
            nommait ; false quand aucune image n'a cet identifiant.
        */
        bool set_picture_description(const std::string& id, const std::string& description, std::string& before);
        // Supprime les images qu'aucun fichier ne nomme, dont le coffre abandonne alors les blobs ; leur nombre.
        size_t remove_unlinked_pictures();
        void link_picture(long long file, const std::string& picture);
        std::vector<std::string> pictures_of(long long file);

        // Les fichiers du dossier du lecteur tels que le service les a laissés ; leurs chemins se comparent sans casse.
        std::vector<folder_file> folder_files(const std::string& folder);
        void set_folder_file(const folder_file& f);
        void remove_folder_file(const std::string& folder, const std::string& path);
        // Les fichiers et les octets du lecteur, ses dossiers exclus.
        void drive_usage(long long& files, long long& bytes);

        void add_event(const std::string& kind, const std::string& path, const std::string& message);
        json events(long long since, int limit = 100);

        std::string value(const std::string& key);
        void set_value(const std::string& key, const std::string& value);

        /*!
            ensures
                - Enregistre les substituts que le moteur a écrits dans les noms et les
                  propriétés des fichiers, chaque (substitut, tag), un tag et jamais une valeur
                  en clair, pour que leurs valeurs reviennent à la restauration de n'importe
                  quelle copie du fichier.
        !*/
        void add_substitutes(const std::vector<std::pair<std::string, std::string>>& made);
        // Les tags enregistrés pour un substitut, les plus récents d'abord, huit au plus.
        std::vector<std::string> tags_for(const std::string& substitute);

    private:
        void exec(const char* sql);
        std::mutex m_;
        sqlite3* db_ = nullptr;
    };
}

#endif // PSEUDALIS_HOST_STORE_H_
