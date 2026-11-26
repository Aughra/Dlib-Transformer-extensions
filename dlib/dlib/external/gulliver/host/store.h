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

namespace gulliver { class database; }

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

    // Un index nommé d'un profil : une combinaison {modèle d'embedding, découpage} sous laquelle les
    // passages sont rangés.  Un profil peut en avoir plusieurs et basculer de l'un à l'autre sans
    // réindexer ; un seul est actif à la fois.
    struct index_record
    {
        std::string id;
        std::string name;
        std::string embed_model;
        int64_t passage_chars = 1200;
        int64_t overlap_chars = 240;
        int64_t created = 0;
        bool active = false;
    };

    // Un passage d'un document indexé (RAG) : son texte, sa place, et son vecteur d'embedding
    // quand il est calculé.  Le vecteur est gardé tel quel (octets d'un tableau de float), vide
    // quand le passage n'a pas encore été vectorisé ; `model` dit avec quel modèle il l'a été.
    struct chunk_record
    {
        std::string id;              // "docId:i"
        std::string owner;           // le propriétaire (une discussion ou un projet)
        std::string doc;             // le document d'où vient le passage
        int64_t ordinal = 0;         // sa place dans le document
        std::string name;            // le nom du document
        std::string where;           // la section/page/diapositive
        int64_t start = 0;
        int64_t end = 0;
        std::string text;
        std::string model;           // le modèle d'embedding, vide si pas de vecteur
        std::vector<char> vector;    // les octets du vecteur, vide si pas encore calculé
    };

    /*
        Un document de la bibliothèque (2026-11-26) : un fichier du dossier d'un profil (scope = son
        identifiant) ou du dossier commun (scope vide), indexé par le service lui-même.  size et mtime
        sont ceux du fichier indexé, pour voir qu'il a changé ; state vaut "indexed" ou "error" ; by est
        le profil qui l'a déposé par la page (vide s'il est arrivé par le Finder).
    */
    struct library_doc
    {
        std::string scope;
        std::string name;
        int64_t size = 0;
        int64_t mtime = 0;
        std::string model;
        int64_t passages = 0;
        int64_t vectors = 0;
        std::string state;
        std::string error;
        std::string by;
        int64_t indexed = 0;
    };

    // L'état d'indexation d'un document : combien de passages, combien avec un vecteur du modèle
    // courant, et quel modèle.
    struct chunk_status_row
    {
        std::string doc;
        int64_t passages = 0;
        int64_t vectors = 0;         // passages dont le vecteur est du modèle demandé
        std::string model;
    };

    // Les métadonnées d'une image d'un profil.  Les octets (image pleine et vignette) vivent
    // dans des fichiers à part ; blob_bytes et thumb_bytes en donnent les tailles pour l'usage.
    struct image_record
    {
        std::string id;
        std::string owner;           // la discussion ou le projet
        std::string name;
        std::string type;            // le type MIME (image/jpeg, image/png…)
        int64_t size = 0;            // la taille d'origine annoncée
        int64_t width = 0;
        int64_t height = 0;
        int64_t added = 0;
        int64_t blob_bytes = 0;      // octets du fichier de l'image
        int64_t thumb_bytes = 0;     // octets du fichier de la vignette
    };

    // Un profil : l'espace d'un utilisateur (ses discussions, projets, réglages de page,
    // index de documents et images), gardé par le service pour qu'il se retrouve depuis
    // n'importe quel navigateur.  L'identifiant est opaque et stable ; le nom, lui, peut être changé.
    struct profile
    {
        std::string id;              // identifiant opaque (hex), clé de partition des données
        std::string name;            // le nom choisi, unique, affiché et renommable
        int64_t created = 0;
        int64_t last_used = 0;
        // Les comptes (fork pour macOS, 2026-11-22, host/accounts.h) : l'empreinte du mot de passe
        // (vide : pas encore de mot de passe) et le rôle d'administrateur (un seul, le premier profil).
        std::string password;
        bool admin = false;
        // L'adresse mail de connexion (fork pour macOS, 2026-11-23), en minuscules, unique ; vide pour
        // un profil d'avant les adresses, qui ne peut pas se connecter tant qu'il n'en a pas.
        std::string email;
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
        bool find_profile_by_email(const std::string& email, profile& p);
        void put_profile(const profile& p);
        void remove_profile(const std::string& id);
        void touch_profile(const std::string& id, int64_t when);

        // Les sessions des profils (fork pour macOS) : la clé est l'empreinte du jeton (session_key()),
        // jamais le jeton.  session_profile() rend l'identifiant du profil d'une session encore valable
        // (servie il y a moins de max_idle secondes) et note son usage, ou une chaîne vide (une session
        // trop ancienne est effacée).  remove_sessions_of() efface celles d'un profil, sauf keep.  Les
        // sessions d'un profil supprimé partent avec lui (ON DELETE CASCADE).
        void add_session(const std::string& key, const std::string& profile, int64_t now);
        std::string session_profile(const std::string& key, int64_t now, int64_t max_idle);
        void remove_session(const std::string& key);
        void remove_sessions_of(const std::string& profile, const std::string& keep = std::string());
        // L'administrateur principal : le plus ancien administrateur, puis le premier enregistré (vide s'il n'y en a pas).
        std::string principal_admin();

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

        // Les passages indexés (RAG) d'un profil.  replace_chunks() remplace en une transaction tous
        // les passages d'un document par ceux donnés (supprime puis insère).  chunks_of() rend les
        // passages d'un propriétaire (texte et vecteur), chunks_of_document() ceux d'un document.
        // chunk_status() compte, par document d'un propriétaire, les passages et ceux dont le vecteur
        // est du modèle donné.  chunk_documents() liste les couples (owner, doc) distincts, pour
        // l'élagage.  delete_chunks() efface par propriétaire ou par document.
        // Les passages sont rangés par index (index_id ; vide = l'index par défaut).  Les méthodes
        // travaillent dans l'index donné.
        void replace_chunks(const std::string& profile, const std::string& doc,
                            const std::vector<chunk_record>& records, const std::string& index_id = std::string());
        std::vector<chunk_record> chunks_of(const std::string& profile, const std::string& owner,
                                            const std::string& index_id = std::string());
        std::vector<chunk_record> chunks_of_document(const std::string& profile, const std::string& doc,
                                                     const std::string& index_id = std::string());
        std::vector<chunk_status_row> chunk_status(const std::string& profile, const std::string& owner,
                                                   const std::string& model, const std::string& index_id = std::string());
        std::vector<std::pair<std::string, std::string>> chunk_documents(const std::string& profile,
                                                                         const std::string& index_id = std::string());
        void delete_chunks_of_owner(const std::string& profile, const std::string& owner,
                                    const std::string& index_id = std::string());
        void delete_chunks_of_document(const std::string& profile, const std::string& doc,
                                       const std::string& index_id = std::string());

        /*
            La bibliothèque : ses documents et leurs passages, à part des passages que la page range
            pour ses discussions et ses projets (que son ménage efface quand leur propriétaire n'est
            plus là).  put_library_doc() range un document et remplace ses passages en une
            transaction ; set_library_doc() ne touche qu'à sa fiche.
        */
        void put_library_doc(const library_doc& d, const std::vector<chunk_record>& records);
        void set_library_doc(const library_doc& d);
        std::vector<library_doc> library_docs(const std::string& scope);
        bool find_library_doc(const std::string& scope, const std::string& name, library_doc& out);
        std::vector<chunk_record> library_chunks(const std::string& scope);
        void remove_library_doc(const std::string& scope, const std::string& name);
        void remove_library_scope(const std::string& scope);

        // Les index nommés d'un profil.  indexes() les liste ; find_index() en trouve un ; active_index()
        // rend l'id de l'index actif (vide s'il n'y en a pas) ; put_index() crée ou met à jour ;
        // set_active_index() rend un index actif (et les autres inactifs) ; remove_index() l'efface avec
        // ses passages.
        std::vector<index_record> indexes(const std::string& profile);
        bool find_index(const std::string& profile, const std::string& id, index_record& out);
        std::string active_index(const std::string& profile);
        void put_index(const std::string& profile, const index_record& r);
        void set_active_index(const std::string& profile, const std::string& id);
        void remove_index(const std::string& profile, const std::string& id);

        // Les métadonnées des images d'un profil (les octets sont dans des fichiers à part).
        // images_of() rend celles d'un propriétaire ; find_image() une seule (false si absente) ;
        // put_image() écrit ou remplace ; delete_image() en efface une, delete_images_of_owner()
        // toutes celles d'un propriétaire.  image_usage() rend le nombre et le total d'octets
        // (image pleine + vignette) d'un profil.
        std::vector<image_record> images_of(const std::string& profile, const std::string& owner);
        bool find_image(const std::string& profile, const std::string& id, image_record& out);
        void put_image(const std::string& profile, const image_record& r);
        void delete_image(const std::string& profile, const std::string& id);
        std::vector<std::string> delete_images_of_owner(const std::string& profile, const std::string& owner);
        void image_usage(const std::string& profile, int64_t& count, int64_t& bytes);

    private:
        // Ouvre la connexion et règle ses options ; m_ est tenu, ou le store est en construction.
        void connect();
        // Exécute une écriture ; une connexion que SQLite a ouverte en lecture seule est rouverte une
        // fois.  m_ est tenu.
        void writing(const std::function<void()>& work);

        std::mutex m_;
        std::string path_;
        std::unique_ptr<database> db_;
    };

}

#endif // GULLIVER_HOST_STORE_H_
