// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_SERVICE_H_
#define PSEUDALIS_HOST_SERVICE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/engine_loader.h"
#include "../common/imposed_terms.h"
#include "../common/json.h"
#include "albireo_client.h"
#include "settings.h"
#include "store.h"
#include "user_error.h"
#include "vault.h"

namespace httplib { class SSLServer; class Server; }

namespace pseudalis
{
    class drive_host;

    struct protect_result
    {
        std::string bytes;      // le fichier protégé
        std::string name;       // son nom : le nom donné, ou <name>.txt pour une image
        std::string kind;
        json report;
        long long record = 0;   // l'enregistrement de l'original, 0 quand aucun n'a été gardé
        std::string sha256;     // de bytes
    };

    struct restore_result
    {
        std::string bytes;
        std::string name;       // le nom original, quand l'original vient du coffre
        std::string how;        // "original" : les octets gardés ; "picture" : une image d'après son marqueur ; "tags"
        json report;
        long long record = 0;
    };

    class work_slots
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les fichiers et textes de l'interface traités en même temps.  Chacun est en
                mémoire plusieurs fois pendant qu'il est lu, traité et renvoyé, si bien que
                leur nombre est borné par les processeurs et la mémoire de l'ordinateur ; une
                requête attend une place avant que son corps soit lu.  Le lecteur garde sa
                propre borne.
        !*/
    public:
        void init(int slots);
        // Attend une place, jusqu'à wait ; false quand aucune ne s'est libérée à temps.
        bool acquire(std::chrono::milliseconds wait);
        void release();
        int size() const { return size_; }

    private:
        std::mutex m_;
        std::condition_variable cv_;
        int free_ = 0;
        int size_ = 0;
    };

    // Une place de work_slots tenue par sa portée, après une attente d'une minute au plus.
    class work_slot
    {
    public:
        explicit work_slot(work_slots& slots) : slots_(slots), held_(slots.acquire(std::chrono::seconds(60))) {}
        ~work_slot() { if (held_) slots_.release(); }
        work_slot(const work_slot&) = delete;
        work_slot& operator=(const work_slot&) = delete;
        bool held() const { return held_; }

    private:
        work_slots& slots_;
        bool held_;
    };

    class service
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le service Pseudalis : le moteur et sa clé, le coffre et la base de données
                des originaux, le serveur HTTPS local (l'application web, son interface et sa
                documentation), le lecteur et les descriptions des images par Albireo.  run()
                démarre tout et bloque jusqu'à stop() ou une demande de redémarrage ;
                l'appelant démarre alors un nouveau service quand restart_requested() le dit,
                ce qui applique un changement de port ou de lecteur.

            PROTECTION
                protect() exécute le moteur sur un fichier : chaque image est gardée dans le
                coffre, décrite par Albireo quand il répond (la description est gardée avec
                elle, pour les fichiers suivants qui la montrent), et avec record, l'original
                est gardé dans le coffre et enregistré avec le SHA-256 du fichier protégé,
                pour que restore() rende l'original lui-même, à partir de n'importe quelle
                copie du fichier protégé.  restore() d'un fichier que la base de données ne
                connaît pas, écrit ailleurs à partir de valeurs protégées ou protégé ici
                avant que son original soit oublié, remet les valeurs de ses tags, ses
                marqueurs d'images retirés ; un fichier texte contenant le marqueur d'une
                image connue rend cette image.

                Sans tags, les valeurs d'un fichier prennent leurs seuls substituts, et son
                enregistrement les garde avec les tags qu'ils représentent : les copies du
                fichier rendent leur original, et une copie modifiée depuis, reconnue par son
                identifiant NTFS, ses valeurs à partir de ces substituts.  Une fois
                l'enregistrement oublié, ses valeurs ne peuvent plus revenir, et un fichier
                protégé sans enregistrement ne les rend jamais.  Un fichier protégé protégé de
                nouveau, par un moteur plus récent par exemple, garde tels quels les valeurs
                protégées auparavant : sa restauration rend l'original de chaque
                enregistrement de la chaîne qui le garde encore, et les valeurs des tags des
                autres.
        !*/
    public:
        service();
        ~service();

        bool run(std::string& error);
        void stop();
        bool stopping() const { return stopping_; }
        bool restart_requested() const { return restart_; }
        void request_restart();

        std::string program_dir() const { return program_dir_; }
        std::string data_dir() const { return data_dir_; }
        std::string admin_token() const { return admin_token_; }
        int port() const { return port_; }

        settings_store& settings() { return *settings_; }
        store& db() { return *db_; }
        vault& blobs() { return *vault_; }
        albireo_client& albireo() { return albireo_; }
        work_slots& work() { return work_; }

        json status();
        // La langue des descriptions et des messages : le réglage, sinon celle du système.
        std::string language() const;

        /*
            Protège un fichier ; avec record, garde son original (out.record), sauf s'il dépasse
            history_mb ; sans tags, ses valeurs prennent leurs substituts, que l'enregistrement garde.
            out.report["tags"] dit lequel, out.report["restorable"] si le fichier peut être restauré.
            Un fichier protégé sans tags et protégé de nouveau, une copie de son fichier protégé ou,
            avec previous, l'enregistrement de la version qu'il remplace sur le lecteur, garde tels
            quels les substituts de cet enregistrement, et son enregistrement en hérite, ce qui rend
            les valeurs de son original.
        */
        bool protect(const std::string& bytes, const std::string& name, const std::string& source, bool record,
            bool tags, protect_result& out, user_error& error, long long previous = 0);
        /*!
            L'original d'un fichier protégé : l'original de son enregistrement, celui d'une
            entrée d'une archive protégée quand il en a été extrait, une image d'après son
            marqueur, sinon le fichier avec les valeurs de ses tags remises et ses marqueurs
            d'images retirés, le jour et la raison d'un oubli dans
            out.report["original_forgotten"].
        !*/
        bool restore(const std::string& bytes, const std::string& name, const std::string& object_id,
            restore_result& out, user_error& error);
        /*
            L'original de l'enregistrement r tel que restore() le donne : l'original de son propre
            enregistrement quand c'est un fichier protégé qu'un enregistrement garde, sur huit
            enregistrements de profondeur au plus, sinon ses tags et les substituts dont r a hérité,
            restaurés quand il en contient.
        */
        bool recorded_original(const file_record& r, restore_result& out, user_error& error, int depth = 0);
        /*!
            Un fichier du lecteur dont le contenu est une entrée d'une archive protégée que la
            base de données enregistre, extraite de celle-ci, est enregistré avec l'original de
            cette entrée, que le coffre garde à part de l'original de l'archive : chacun est
            restauré et oublié seul.  Renvoie 1 avec son nouvel enregistrement (0 pour une
            entrée qui dépasse history_mb, gardée telle quelle sans enregistrement), 0 quand le
            contenu n'est pas une telle entrée ou que son original ne peut pas être lu, -1
            quand le moteur ou le coffre ne peut pas le faire maintenant.
        !*/
        int adopt(const std::string& sha256, const std::string& name, long long size, long long& record);
        // Un nom seul, un dossier du lecteur par exemple : ses valeurs deviennent des substituts, enregistrés.
        bool protect_name(const std::string& name, std::string& out, user_error& error);
        // Un nom avec les substituts que connaît la base de données rendus, ou le nom tel quel.
        std::string restore_name(const std::string& name);
        /*
            operation : "anonymize_text", "restore_text" ou "detect".  Sans tags, le texte ne garde
            aucun tag et out["substitutions"] liste les substituts écrits, que restore_text rend
            quand substitutions les contient : rien d'autre ne les garde.
        */
        bool text(const std::string& operation, const std::string& text, json& out, user_error& error,
            bool tags = true, const json& substitutions = json());
        /*!
            Supprime un enregistrement, son original et les images qu'aucun autre
            enregistrement ne montre.  Avec une raison, "forgotten" quand un utilisateur l'a
            demandé, "expired" quand son temps est écoulé ou "space" quand les plus anciens
            originaux font de la place (make_room()), le fichier protégé de l'enregistrement
            est mémorisé : sa restauration remet dès lors les valeurs de ses tags, et indique
            le jour et la raison.
        !*/
        bool forget(long long record, const std::string& reason = std::string());
        // Marque un enregistrement dont le fichier a quitté le lecteur, supprimé ou remplacé.  Un
        // déplacement hors du lecteur est une copie suivie d'une suppression : l'original reste
        // restaurable à partir des copies du fichier pendant removed_days jours (réglages, 0 pour
        // toujours), puis purge_removed() l'oublie.
        void mark_removed(long long record);
        // Oublie les enregistrements dont le fichier a quitté le lecteur depuis plus de removed_days jours,
        // aucun pour 0.
        void purge_removed();
        /*!
            Fait de la place pour un original de needed octets : les originaux gardés en premier
            sont oubliés, les plus anciens d'abord, tant que ceux gardés avec lui prendraient
            plus que history_mb (réglages), et, pour un nouvel original, tant que le disque du
            dossier de données garderait moins de 256 Mo libres une fois qu'il est écrit.
            L'oubli ne commence que s'il atteint son but : les originaux restent quand
            d'autres fichiers remplissent le disque, ou quand son espace libre ne peut pas être
            connu.  Renvoie false, sans rien oublier, pour un original qui dépasse history_mb,
            lequel n'est pas gardé.
        !*/
        bool make_room(unsigned long long needed);
        // Exécute purge_removed() et make_room(0) en arrière-plan, aussitôt : les réglages ont changé.
        void request_cleanup();
        // L'espace que prennent les originaux gardés, en octets, et celui qu'ils peuvent prendre.
        void history_space(unsigned long long& used, unsigned long long& limit);

        /*!
            Les termes imposés (common/imposed_terms.h), gardés dans la base de données et
            donnés au moteur : set_terms() prend une liste [{"term", "type", "case_sensitive"}]
            entière, ou la refuse avec sa raison (codes terms_invalid, terms_too_many,
            terms_type, terms_duplicate, avec la position du terme fautif dans "index").
        !*/
        json terms();
        bool set_terms(const json& list, user_error& error);

        bool export_key(const std::string& passphrase, std::string& exported, user_error& error);
        // Permis tant que la base de données n'enregistre aucun fichier : la clé d'une installation antérieure.
        bool import_key(const std::string& exported, const std::string& passphrase, user_error& error);

    private:
        bool start_engine(std::string& error);
        /*
            L'original de l'entrée e, à partir de l'original de son archive, dans bytes : 1, 0 quand
            l'enregistrement de l'archive a disparu, -1 quand le moteur ne peut pas le faire
            maintenant, -2 quand l'original ne peut pas être lu.
        */
        int extract_entry(const entry_record& e, file_record& archive, std::string& bytes, user_error& error);
        // Supprime un enregistrement à moitié écrit, son original étant supprimé par l'appelant.
        void discard(long long record);
        void background_loop();
        // La recherche du moteur : les tags enregistrés pour un substitut.
        std::function<std::vector<std::string>(const std::string&)> substitute_lookup();
        // Enregistre les substituts des noms et des propriétés d'un résultat du moteur.
        bool record_substitutes(const json& result, user_error& error);
        // Les substituts du texte de record, ou seulement ceux dont il a hérité, tels que le moteur les lit.
        json text_substitutes_of(long long record, bool inherited_only = false);
        json text_substitutes_of_inherited(long long record) { return text_substitutes_of(record, true); }

        std::string program_dir_, data_dir_, admin_token_;
        int port_ = 8444;
        std::unique_ptr<settings_store> settings_;
        std::unique_ptr<store> db_;
        std::unique_ptr<vault> vault_;
        std::shared_ptr<engine_library> library_;
        std::unique_ptr<engine_client> engine_;
        std::string key_fingerprint_;
        albireo_client albireo_;
        std::unique_ptr<drive_host> drive_;
        std::unique_ptr<httplib::SSLServer> server_;
        std::mutex server_m_;
        std::thread background_;
        std::mutex wake_m_;
        std::condition_variable wake_;
        std::atomic<bool> cleanup_{false};
        // make_room() s'exécute pour un seul appelant à la fois.
        std::mutex room_m_;
        std::mutex terms_m_;
        std::vector<imposed_term> terms_;
        std::atomic<bool> stopping_{false};
        std::atomic<bool> restart_{false};
        std::mutex key_m_;
        std::string master_;
        int64_t started_ = 0;
        work_slots work_;
        /*
            Les protections tiennent work_m_ en partage ; l'import d'une clé le tient seul de sa
            vérification à l'enregistrement de la clé, après quoi aucune protection ne démarre
            jusqu'au redémarrage (key_replaced_) : une protection faite entre-temps utiliserait la
            clé qui s'en va.
        */
        std::shared_timed_mutex work_m_;
        std::atomic<bool> key_replaced_{false};
        // Les comptes de status(), que chaque icône demande toutes les quelques secondes, gardés deux
        // secondes, ou jusqu'à ce qu'un enregistrement soit ajouté ou oublié (counts_changed()).
        void counts_changed();
        std::mutex counts_m_;
        std::chrono::steady_clock::time_point counts_at_;
        long long records_count_ = 0;
        unsigned long long history_bytes_ = 0, history_limit_ = 0;
        json drive_status_ = json::object();

        friend class drive_host;
    };

    // Les routes du serveur (server.cpp).
    void install_routes(httplib::Server& srv, service& svc);
}

#endif // PSEUDALIS_HOST_SERVICE_H_
