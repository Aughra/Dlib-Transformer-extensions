// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_STORE_H_
#define GULLIVER_HOST_STORE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../common/json.h"

namespace dlib { class database; }

namespace gulliver
{

    struct installed_model
    {
        std::string id;              // identifiant du catalogue, ou de l'import
        std::string name;
        std::string file;            // chemin absolu du fichier GGUF
        uint64_t size = 0;
        std::string sha256;          // du fichier tel qu'installé
        std::string source;          // "catalog", "import" ou "builtin"
        int64_t installed_at = 0;
        int64_t verified_at = 0;     // dernière vérification réussie des empreintes
        json descriptor;             // l'entrée du catalogue, ou ce que l'import a appris
        // Le projecteur d'un modèle qui lit les images (un fichier mmproj, general.architecture
        // "clip", qui contient l'encodeur de vision) : son chemin absolu, sa taille et son
        // SHA-256.  Vide pour un modèle qui ne lit que du texte.
        std::string projector;
        uint64_t projector_size = 0;
        std::string projector_sha256;
    };

    // Un projecteur arrivé sans son décodeur, gardé dans le dossier des modèles jusqu'à
    // l'installation d'un décodeur qui va avec lui.
    struct pending_projector
    {
        std::string sha256;
        std::string file;            // chemin absolu, dans le dossier des modèles
        uint64_t size = 0;
        std::string name;            // le nom du fichier d'où il vient
        json descriptor;             // describe_gguf()
        int64_t received_at = 0;
    };

    // Un profil : l'espace d'un utilisateur (ses discussions, projets, réglages de page,
    // index de documents et images), gardé par le service pour qu'il se retrouve depuis
    // n'importe quel navigateur.  Pas de mot de passe (usage personnel sur un seul Mac).
    // L'identifiant est opaque et stable ; le nom, lui, peut être changé.
    struct profile
    {
        std::string id;              // identifiant opaque (hex), clé de partition des données
        std::string name;            // le nom choisi, unique, affiché et renommable
        int64_t created = 0;
        int64_t last_used = 0;
    };

    class store
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La base de données du service (SQLite, dans le dossier des données) : les
                modèles installés avec leurs projecteurs, les projecteurs qui attendent leurs
                décodeurs, l'état des téléchargements, pour qu'un téléchargement interrompu
                reprenne là où il s'est arrêté, les chiffres d'utilisation quotidiens et
                quelques valeurs nommées.

                Une base d'une version antérieure reçoit les colonnes des projecteurs à son
                ouverture.  Le constructeur vérifie chaque page de la base (PRAGMA
                quick_check) et lève une erreur qui la dit "malformed" quand l'une échoue.

                Chaque fonction verrouille la base pendant sa durée, pour que les threads du
                serveur, des téléchargements et du moteur puissent la partager.

                Un échec lève l'erreur de SQLite, que les appelants signalent.  Une base tenue
                par un autre programme est attendue cinq secondes.  SQLite ouvre en lecture
                seule un fichier qu'il ne peut pas écrire, et sa connexion le reste : une
                écriture qui y échoue rouvre la connexion, pour que les écritures remarchent dès
                que le fichier peut être écrit.
        !*/
    public:
        explicit store(const std::string& path);
        ~store();

        std::vector<installed_model> models();
        bool find_model(const std::string& id, installed_model& m);
        void put_model(const installed_model& m);
        void remove_model(const std::string& id);
        void set_verified(const std::string& id, int64_t when);

        // Les projecteurs qui attendent leurs décodeurs, les plus anciens d'abord.
        std::vector<pending_projector> projectors();
        void put_projector(const pending_projector& p);
        void remove_projector(const std::string& sha256);

        // Téléchargements : les parties reçues jusqu'ici sont enregistrées par indice.
        json download_state(const std::string& id);
        void set_download_state(const std::string& id, const json& state);
        void remove_download_state(const std::string& id);
        std::vector<std::string> pending_downloads();

        void add_usage(int64_t prompt_tokens, int64_t generated_tokens, int64_t requests);
        json usage(int days);

        std::string value(const std::string& key, const std::string& def = std::string());
        void set_value(const std::string& key, const std::string& v);

        // Les profils, le plus récemment utilisé d'abord.  find_profile() et find_profile_by_name()
        // renvoient false quand le profil n'existe pas.  put_profile() crée le profil ou met son nom
        // à jour (le nom reste unique : une collision lève une erreur de SQLite, que l'appelant
        // signale).  remove_profile() efface le profil ; les données qui s'y rattachent partent avec
        // lui par les clés étrangères des tables de profil (ON DELETE CASCADE).  touch_profile() note
        // qu'il vient de servir (last_used), sans rien lire.
        std::vector<profile> profiles();
        bool find_profile(const std::string& id, profile& p);
        bool find_profile_by_name(const std::string& name, profile& p);
        void put_profile(const profile& p);
        void remove_profile(const std::string& id);
        void touch_profile(const std::string& id, int64_t when);

        // Les discussions et les projets d'un profil.  `doc` est le JSON entier que la page gère.
        // *_docs() rend tous les documents d'un profil (discussions triées du plus récent au plus
        // ancien) ; get_* un seul, false s'il manque.  put_* écrit ou remplace, avec une révision
        // optimiste : si expected_updated >= 0, l'écriture n'a lieu que si l'`updated` en base vaut
        // exactement cette valeur (ou que la ligne n'existe pas et expected_updated == 0), sinon elle
        // renvoie false sans rien changer — l'appelant relit et réessaie.  expected_updated < 0 force
        // l'écriture.  delete_* efface, et un projet supprimé détache ses discussions (project = '').
        std::vector<std::string> conversation_docs(const std::string& profile);
        bool get_conversation(const std::string& profile, const std::string& id, std::string& doc);
        bool put_conversation(const std::string& profile, const std::string& id, const std::string& project,
                              int64_t updated, const std::string& doc, int64_t expected_updated);
        void delete_conversation(const std::string& profile, const std::string& id);

        std::vector<std::string> project_docs(const std::string& profile);
        bool get_project(const std::string& profile, const std::string& id, std::string& doc);
        void put_project(const std::string& profile, const std::string& id, const std::string& name,
                         const std::string& doc);
        void delete_project(const std::string& profile, const std::string& id);

        // Les réglages de page d'un profil, en clé/valeur.  page_settings() rend l'objet entier
        // {clé: valeur} ; set_page_setting() écrit une clé.
        json page_settings(const std::string& profile);
        void set_page_setting(const std::string& profile, const std::string& key, const std::string& value);

    private:
        // Ouvre la connexion et règle ses options ; m_ est tenu, ou le store est en construction.
        void connect();
        // Exécute une écriture ; une connexion que SQLite a ouverte en lecture seule est rouverte une
        // fois.  m_ est tenu.
        void writing(const std::function<void()>& work);

        std::mutex m_;
        std::string path_;
        std::unique_ptr<dlib::database> db_;
    };

}

#endif // GULLIVER_HOST_STORE_H_
