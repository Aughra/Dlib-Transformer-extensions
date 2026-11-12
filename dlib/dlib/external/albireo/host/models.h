// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_MODELS_H_
#define ALBIREO_HOST_MODELS_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/json.h"
#include "store.h"
#include "user_error.h"

namespace albireo
{
    class link_free_file;


    class model_manager
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les modèles de la machine et du catalogue : le catalogue, un document JSON
                sans signature qui liste les modèles, leurs descriptions dans chaque langue,
                leurs fichiers et leurs empreintes, l'adresse de chacun d'eux, lu depuis son
                adresse dans le dépôt de l'original ; les modèles installés, les
                téléchargements en cours et les imports depuis des supports amovibles.

            MODÈLES QUI LISENT LES IMAGES
                Un tel modèle se présente en deux fichiers GGUF : son décodeur, et un
                projecteur (mmproj-*.gguf, general.architecture "clip") qui contient l'encodeur
                de vision.  Une entrée du catalogue le nomme dans "projector" : {"files",
                "size", "sha256", "quantization"}, et ses "inputs" sont ["text", "image"].
                Une fois installés, les deux fichiers sont <identifier>.gguf et
                <identifier>.mmproj.gguf dans le dossier des modèles, enregistrés ensemble
                (installed_model::projector) ; le moteur reçoit le chemin du projecteur avec le
                modèle (option "projector").  Un décodeur installé sans son projecteur ne lit
                que du texte.

            TÉLÉCHARGEMENTS
                Un modèle se télécharge sous la forme des parties que liste le catalogue (un
                gros fichier est publié en plusieurs parties, compressées ou non), chacune
                découpée en segments récupérés par plusieurs connexions à la fois et écrits à
                leur place dans son fichier partiel : un même travail récupère le décodeur et
                son projecteur, chacun dans son propre fichier partiel, avec une seule
                progression pour les deux.  La progression est enregistrée dans la base, pour
                qu'un téléchargement interrompu par un redémarrage reprenne là où il s'est
                arrêté.  Chaque fichier est vérifié avec sa propre empreinte du catalogue ;
                quand l'une ne correspond pas, les parties dont l'empreinte propre échoue sont
                récupérées de nouveau, une fois.  Le modèle n'est installé qu'une fois les deux
                fichiers en place : une annulation, une empreinte qui échoue encore ou un
                modèle que la base ne peut pas enregistrer ne laisse aucun des deux fichiers
                sous son nom définitif.

                Le téléchargement vérifie d'abord la réserve locale : si le fichier complet du
                décodeur et, le cas échéant, du projecteur y sont présents et conformes, il les
                restaure sans réseau. En mode local seul, une réserve absente ou invalide met
                fin au travail sans accès réseau. Sinon, les segments viennent des adresses
                officielles que donne le catalogue, avec trois tentatives séparées par des pauses qu'une
                annulation interrompt ; une réponse qui annonce une partie à une autre taille
                que le catalogue met fin à sa tentative avant qu'aucun octet ne soit écrit.
                Une partie dont l'empreinte échoue est récupérée de nouveau, une fois. Quand
                l'adresse officielle échoue, le téléchargement se termine en erreur avec le
                code download_failed et le message ("e") nomme l'adresse et l'échec. Les
                empreintes du catalogue vérifient les octets. Une exception d'un transfert fait
                échouer le téléchargement et ne sort jamais de son thread.

            INSTALLATION HORS LIGNE
                import_path() installe les fichiers GGUF du dossier d'import du dossier des
                données : ceux d'un dossier (et de son sous-dossier "models") ou un fichier
                seul. Un fichier
                dont l'empreinte est dans le catalogue prend son entrée ; un autre est décrit
                à partir de ses propres métadonnées, y compris l'identité d'un modèle
                d'Albireo (albireo.id, albireo.name, albireo.roles, albireo.inputs).
                import_bundle() installe les modèles d'un paquet hors ligne, où qu'il se
                trouve : seulement les fichiers que liste son catalogue (non signé), décodeurs
                et projecteurs, trouvés par leurs noms et vérifiés par leurs tailles et leurs
                empreintes avant toute copie.  import_catalog() installe un catalogue apporté
                de la même façon.

                Les projecteurs d'un dossier vont avec ses décodeurs comme le dit
                pair_projectors() : par les empreintes du catalogue, sinon par les largeurs de
                leurs en-têtes et par leurs noms.  Un projecteur sans décodeur dans son
                dossier, ou reçu seul (import_received()), est donné à un décodeur installé
                qui n'en a pas et qui va avec lui ; sinon il attend dans le dossier des modèles
                comme projecteur en attente (.pending-<hash>.mmproj.gguf), que prend le
                prochain décodeur qui va avec lui.  Un décodeur d'une architecture qui lit les
                images, installé sans projecteur, ne lit que du texte jusqu'à ce qu'il en
                arrive un.

                Les fichiers sont lus sans passer par aucun lien (link_free_file), avec un nom
                unique, pour qu'aucun import ne fasse lire au service le fichier d'un autre
                compte.  Un fichier dont le contenu est celui d'un modèle ou d'un projecteur
                installé, y compris un modèle livré avec Albireo, ou d'un projecteur en
                attente, n'est pas recopié : il est listé sous "already".  Un fichier ne
                prend jamais l'identifiant d'un autre modèle installé, sauf s'il est une
                version plus récente, venue du catalogue, d'un modèle du catalogue : il est
                installé sous <identifier>-import, ou <identifier>-import-2 et ainsi de suite
                quand celui-là est pris.

            LE DOSSIER DES MODÈLES
                Chaque fichier que le service y écrit (un téléchargement, ses fichiers
                partiels, un import, un projecteur, un fichier reçu par une route)
                est créé dans le dossier atteint sans passer par aucun lien (direct_path()),
                vérifié de nouveau juste avant que le fichier soit créé, renommé ou supprimé,
                et ouvert sans suivre de lien à son propre nom ; un fichier renommé est
                vérifié comme étant celui qui a été écrit.

            MODÈLES LIVRÉS AVEC ALBIREO
                register_builtin() enregistre les modèles du dossier des programmes, que
                l'installateur complet y place, avec leurs projecteurs.  Ils sont listés comme
                les autres et prennent les fonctions qu'aucun modèle ne tient ; ils ne peuvent
                pas être supprimés, seulement remplacés.

            LES LISTES DES FENÊTRES
                overview() ne liste que les modèles de langage : le modèle d'embedding sert la
                recherche dans les documents, qui en a toujours un, et est donné à part
                ("embedding").  Chaque modèle donne ses "inputs" et son "publisher",
                l'entreprise qui l'a fait.
        !*/
    public:
        model_manager(store& db, const std::string& models_dir, const std::string& reserve_dir = std::string());
        ~model_manager();

        /*!
            ensures
                - Lit le catalogue, sans signature, à son adresse unique : le dépôt de
                  l'original (ou l'adresse que donne official_catalog_for_tests()).
                - Garde le dernier catalogue valide, et celui en usage quand celui qui a été
                  récupéré a été publié avant lui.
                - Renvoie false avec error quand l'adresse ne répond pas ou donne un catalogue
                  refusé : error nomme l'adresse et l'échec.
        !*/
        bool refresh_catalog(std::string& error);
        // La même chose sur un thread à elle, une mise à jour à la fois ; l'appelant n'attend jamais.
        void refresh_catalog_async();
        /*!
            ensures
                - Installe le catalogue de folder (catalog.json), lu sans passer par aucun lien
                  depuis base (une base vide : la racine du disque), sauf si le catalogue en
                  usage a été publié plus tard.
                - Renvoie {"models": nombre de modèles} une fois installé, {"state": "kept"}
                  quand le catalogue en usage est plus récent, {"error"} sinon.
        !*/
        json import_catalog(const std::string& folder, const std::string& base);
        // Le catalogue en usage : "state" (idle, checking, ok, error), "error", "updated",
        // "models" (leur nombre) et "time" (de la dernière installation, en secondes Unix).
        json catalog_status() const;
        // Change à chaque installation d'un catalogue.
        unsigned catalog_generation() const { return generation_; }
        json catalog() const;
        bool catalog_entry(const std::string& id, json& entry) const;

        /*!
            ensures
                - Ajoute entry, une entrée de la forme de celles du catalogue (hub_entry(),
                  host/hub.h), à la liste personnelle du fork, gardée dans la base à côté du
                  catalogue et mêlée à lui : le modèle se télécharge, s'installe et s'active comme
                  ceux du catalogue.  Une entrée du même identifiant est remplacée.
                - Renvoie false avec error quand entry n'a pas d'identifiant valide.
        !*/
        bool add_custom(const json& entry, std::string& error);
        // La liste personnelle (fork pour macOS) : les modèles ajoutés depuis Hugging Face.
        json custom() const;

        /*!
            ensures
                - Renvoie le catalogue fusionné avec les modèles installés et les
                  téléchargements, pour les fenêtres : "models" (les modèles de langage, chacun
                  avec "inputs", "publisher", vide quand il est inconnu, "total_size", les
                  octets du modèle et de son projecteur, et "projector" pour un modèle qui en a
                  un), "embedding_models", "pending_projectors" ({"name", "size", "sha256",
                  "projector_type", "projection_width"} des projecteurs qui attendent leurs
                  décodeurs), "downloads" et "catalog".
                - Une entrée du catalogue dont un modèle livré avec Albireo porte le fichier
                  (le source_sha256 de son manifeste) n'est pas listée, sauf si elle est
                  installée sous son propre identifiant : le modèle livré avec Albireo la
                  remplace.
        !*/
        json overview(const std::string& language) const;

        /*!
            ensures
                - Lance le téléchargement de l'entrée id du catalogue, son projecteur compris.
                - Si local_only est vrai, restaure seulement les fichiers vérifiés de la
                  réserve, sans jamais accéder au réseau. Une réserve inutilisable termine le
                  travail avec le code reserve_unavailable.
                - Refuse, avec error rempli, une entrée qui n'est pas dans le catalogue, un
                  modèle livré avec Albireo, et une entrée dont un tel modèle porte le fichier
                  (builtin_installed, qui nomme ce modèle).
                - Refuse avec download_busy une demande locale alors qu'un téléchargement
                  réseau du même modèle est encore en cours.
        !*/
        bool start_download(const std::string& id, user_error& error, bool local_only = false);
        // Affiche un téléchargement de id qui n'a pas pu démarrer, avec son erreur, jusqu'à ce qu'un
        // téléchargement démarre.
        void note_failure(const std::string& id, const user_error& error);
        void cancel_download(const std::string& id);
        void resume_pending();
        json downloads() const;

        // Garde les fichiers vérifiés dans la réserve locale, puis retire le modèle et son enregistrement.
        bool remove(const std::string& id, std::string& error);
        /*!
            ensures
                - Recalcule les empreintes d'un modèle installé et de son projecteur, et les
                  compare à celles enregistrées ("local_match", true quand les deux
                  correspondent) et au catalogue ("catalog_match", "projector_match",
                  "catalog_projector_match").
        !*/
        json verify(const std::string& id);
        /*!
            requires
                - path est base, le dossier d'import, ou se trouve sous lui.
            ensures
                - Installe les fichiers GGUF de path : un fichier, ou un dossier avec son
                  sous-dossier "models", chacun lu sans passer par aucun lien depuis base, les
                  projecteurs avec les décodeurs qui vont avec eux (voir MODÈLES QUI LISENT
                  LES IMAGES).  Avec move, les fichiers installés, ceux déjà installés et les
                  projecteurs gardés en attente quittent path : déplacés dans le dossier des
                  modèles, ou copiés puis supprimés quand il est sur un autre volume.  Un
                  fichier qui ne peut pas être installé reste où il est.
                - Renvoie {"imported": identifiants des modèles installés, et des modèles
                  installés qui ont reçu leur projecteur, "already": identifiants des modèles
                  installés dont les fichiers répètent le contenu, "pending": noms des
                  projecteurs gardés jusqu'à l'arrivée de leur décodeur, "errors": messages,
                  "files": [{"file", "result": imported, already, pending ou error, "id"}]
                  pour chaque fichier lu}.
        !*/
        json import_path(const std::string& path, const std::string& base, bool move = false);
        /*!
            ensures
                - Installe les modèles du paquet hors ligne de folder que liste catalog, son
                  catalogue : models/<name> ou <name>, le nom de son fichier unique ou
                  <identifier>.gguf, lu sans passer par aucun lien depuis la racine de son
                  disque, avec la taille et l'empreinte du catalogue, et de même pour le
                  projecteur d'une entrée qui en a un (<name> de son fichier unique ou
                  <identifier>.mmproj.gguf).  Les fichiers restent dans le paquet.
                - Renvoie {"imported", "already", "pending", "errors"} comme import_path().
        !*/
        json import_bundle(const std::string& folder, const json& catalog);
        /*!
            ensures
                - Installe un fichier reçu par le service (un transfert depuis la barre des
                  tâches), déplacé dans le dossier des modèles ; sha est l'empreinte calculée
                  pendant sa réception.  Un décodeur prend un projecteur en attente qui va avec
                  lui ; un projecteur va à un décodeur installé qui n'en a pas et qui va avec
                  lui, ou attend comme projecteur en attente.
                - Renvoie {"imported", "already", "pending", "errors"} comme import_path().
        !*/
        json import_received(const std::string& file, const std::string& original_name, const std::string& sha);

        /*!
            ensures
                - Enregistre les modèles livrés avec Albireo, listés par le manifeste
                  builtin.json de dir, le dossier "models" des programmes
                  (common/builtin_models.h) : source "builtin", leurs fichiers et leurs
                  projecteurs là où ils se trouvent, leurs empreintes telles que
                  l'installateur les a vérifiées.  Les modèles d'un manifeste antérieur que
                  celui-ci ne liste plus, ou dont le fichier a disparu, quittent la base et
                  sont ajoutés à removed.
                - Renvoie les modèles que cet appel enregistre pour la première fois.
        !*/
        std::vector<installed_model> register_builtin(const std::string& dir, std::vector<installed_model>& removed);

        // Appelé quand un téléchargement se termine, qu'un import installe un modèle ou donne
        // un projecteur à un modèle installé.
        void on_installed(std::function<void(const installed_model&)> f) { installed_cb_ = std::move(f); }

        /*!
            ensures
                - f(id, place) est appelé quand de nouveaux fichiers prennent les noms de ceux
                  du modèle installé id, une version plus récente téléchargée ou importée : f
                  exécute place, qui les renomme après que ses requêtes sont servies et que le
                  modèle est déchargé, afin que le moteur reprenne ses nouveaux fichiers, puis
                  renvoie son résultat. Sans f, place s'exécute directement.
        !*/
        typedef std::function<bool(const std::string&, const std::function<bool()>&)> replace_function;
        void on_replace(replace_function f) { replace_cb_ = std::move(f); }

        // Arrête les téléchargements et la mise à jour en cours et refuse les suivants : le
        // service s'arrête.  La progression des téléchargements est gardée pour le prochain
        // démarrage.
        void stop();

        const std::string& models_dir() const { return dir_; }

    private:
        struct job;
        // Un fichier GGUF d'un import, ouvert sans passer par aucun lien, haché et décrit.
        struct import_item;
        bool save_in_reserve(const std::string& path, const std::string& sha, std::string& error);
        bool restore_from_reserve(const std::shared_ptr<job>& j);
        // Comment put_file() a placé un fichier : déplacé, copié, ou déjà trouvé à son nom.
        struct placement
        {
            bool moved = false;
            bool copied = false;
            bool in_place = false;
        };
        void run_job(std::shared_ptr<job> j);
        bool install_catalog(const std::string& body, bool keep_newer, json& result, std::string& error);
        // Enregistre le modèle id de file, avec son projecteur quand projector n'est pas vide.
        bool install_file(const std::string& id, const std::string& file, const json& descriptor,
            const std::string& sha, const std::string& source, const std::string& projector,
            const std::string& projector_sha, std::string& error);
        /*!
            ensures
                - Ouvre item.path sans passer par aucun lien depuis item.base, avec un nom unique,
                  le hache (known_sha s'il est fourni) et le décrit ; false avec item.error
                  sinon.
        !*/
        bool inspect(import_item& item, const std::string& known_sha);
        /*!
            ensures
                - Installe les fichiers de items, inspectés, comme le décrit import_path() : les
                  décodeurs avec les projecteurs qui vont avec eux, les autres projecteurs donnés
                  à des décodeurs installés ou gardés en attente.  Fixe le résultat de chaque
                  item.
        !*/
        void import_items(std::vector<import_item>& items, bool move);
        // La réponse d'un import : imported, already, pending, errors et files, d'après les résultats de
        // items.
        static json import_result(const std::vector<import_item>& items);
        /*!
            ensures
                - Installe le décodeur d avec le projecteur p, ou avec le projecteur en attente
                  pending, ou seul.  Renvoie true avec d.id rempli.
        !*/
        bool import_decoder(import_item& d, import_item* p, const pending_projector* pending, bool move,
            std::string& error);
        // Donne le projecteur p au modèle installé m, qui se trouve dans le dossier des modèles et n'en a
        // pas.
        bool attach_projector(import_item& p, const installed_model& m, bool move, std::string& error);
        // Garde le projecteur p dans le dossier des modèles jusqu'à l'arrivée d'un décodeur qui va avec lui.
        bool keep_pending(import_item& p, bool move, std::string& error);
        // Le projecteur en attente avec lequel va le décodeur d (pair_projectors()), ou false.
        bool pending_for(import_item& d, pending_projector& found);
        // Le décodeur installé du dossier des modèles, sans projecteur, avec lequel va p, ou false.
        bool installed_for(const import_item& p, installed_model& found);
        // Renomme le projecteur en attente en target, son enregistrement retiré ; false quand il reste en
        // attente.
        bool take_pending(const pending_projector& pending, const std::string& target);
        // Rend aux projecteurs en attente celui que take_pending() a renommé en target.
        void return_pending(const pending_projector& pending, const std::string& target);
        // Supprime le projecteur en attente de contenu sha, une fois un modèle installé avec le même
        // projecteur.
        void drop_pending_copy(const std::string& sha);
        /*!
            ensures
                - Ouvre out à models/<name> ou <name> de folder, name étant le nom de la partie
                  unique de parts ou fallback, sans passer par aucun lien depuis la racine de son
                  disque, et vérifie sa taille et son empreinte.  False quand aucun fichier de ce
                  nom n'est là, ou avec out.error quand il est refusé.
        !*/
        bool bundle_file(const std::string& folder, const json& parts, const std::string& fallback,
            const std::string& sha, uint64_t size, import_item& out);
        /*!
            ensures
                - Place le fichier source, ouvert sans passer par aucun lien à path sous base, à
                  target dans le dossier des modèles : déplacé quand move est vrai et qu'il se
                  trouve sur le volume du dossier, sinon copié depuis le fichier ouvert et
                  vérifié avec sha pendant l'écriture.  Ferme source ; renvoie false avec error
                  quand il ne peut pas être placé.
                - target est un fichier du modèle installé id, quand id n'est pas vide : le
                  renommage passe alors par replacing().
        !*/
        bool put_file(link_free_file& source, const std::string& path, const std::string& base,
            const std::string& sha, bool move, const std::string& target, placement& how, std::string& error,
            const std::string& id);
        /*
            Exécute work, qui renomme de nouveaux fichiers par-dessus ceux du modèle id, par la
            fonction de on_replace() quand id nomme un modèle installé.
        */
        bool replacing(const std::string& id, const std::function<bool()>& work);
        // Défait ce qu'a fait put_file() : le fichier déplacé retourne à path, la copie est supprimée.
        void undo_put(const std::string& path, const std::string& target, const placement& how);
        // Le modèle installé dont le fichier a le contenu d'empreinte sha, celui livré avec Albireo
        // d'abord, ou false.
        bool installed_with_hash(const std::string& sha, installed_model& m);
        /*!
            ensures
                - Renvoie true quand un projecteur de contenu sha est installé, avec holder le
                  modèle qui l'a, ou attend comme projecteur en attente, avec holder vide et
                  pending rempli ; path reçoit son fichier.
        !*/
        bool projector_with_hash(const std::string& sha, std::string& holder, std::string& path);
        // Le SHA-256 du projecteur que nomme l'entrée du catalogue du décodeur d'empreinte sha, ou une
        // chaîne vide.
        std::string catalog_projector(const std::string& decoder_sha) const;
        /*
            L'identifiant que prend un nouveau fichier : id quand aucun modèle ne le tient, ou
            quand update est vrai et que le modèle du catalogue qui le tient reçoit le fichier
            comme sa version plus récente ; sinon id-import, id-import-2, id-import-3..., le
            premier libre, pour qu'un fichier ne prenne jamais la place d'un autre modèle.
        */
        std::string free_identifier(const std::string& id, bool update);
        // Déplace le fichier from vers target, un nom du dossier des modèles : false quand le dossier n'est
        // plus atteint directement, quand le déplacement échoue, ou quand target nomme alors un autre
        // fichier que celui déplacé.
        bool place(const std::string& from, const std::string& target);

        store& db_;
        std::string dir_;
        std::string reserve_dir_;
        std::mutex reserve_m_;
        mutable std::mutex m_;
        json catalog_;
        // La liste personnelle, mêlée à catalog_ à chaque chargement (merge_custom()).
        json custom_;
        void merge_custom();
        std::string catalog_state_ = "idle";
        std::string catalog_error_;
        std::atomic<unsigned> generation_{0};
        std::thread refresher_;
        std::mutex refresher_m_;            // le lancement d'une mise à jour, par une route ou le service
        std::atomic<bool> refreshing_{false};
        std::atomic<bool> stopped_{false};
        std::map<std::string, std::shared_ptr<job>> jobs_;
        std::function<void(const installed_model&)> installed_cb_;
        replace_function replace_cb_;
    };

    /*!
        ensures
            - Renvoie une courte description d'un fichier GGUF lue dans ses métadonnées :
              "name", "arch", "parameters", "quantization", "gguf_name" (general.name), et
              "publisher" quand l'en-tête nomme l'entreprise qui a fait le modèle
              (general.organization, sinon general.author).
            - Pour un décodeur : "context", "experts", "embedding", "roles", "reasoning",
              "tools", "inputs", l'identité d'un fichier d'Albireo ("albireo_id") et
              "reads_images", true pour une architecture qui lit les images (reads_images())
              et pour un vocabulaire qui contient le token <image> (LLaVA) ; pour ceux-ci,
              "projection_width", la largeur des tokens de vision qu'il prend :
              embedding_length * (1 + n_deepstack_layers).
            - Pour un projecteur (general.architecture "clip") : "projector" à true,
              "projector_type" (clip.projector_type), "projection_width", la largeur des
              tokens de vision qu'il donne : clip.vision.projection_dim * (1 + les couches de
              clip.vision.is_deepstack_layers), ou la largeur de sortie de sa dernière couche
              quand l'en-tête n'en nomme aucune (LLaVA), "image_size" et "vision" (s'il
              contient un encodeur de vision).
            - "error" quand le fichier ne peut pas être lu.
    !*/
    json describe_gguf(const std::string& path);

    // Indique si les décodeurs d'une architecture prennent les tokens de vision d'un projecteur : qwen3vl,
    // qwen3vlmoe.
    bool reads_images(const std::string& arch);

    // Un fichier GGUF tel que le voit l'appariement des imports.
    struct pairing_file
    {
        std::string name;                 // le nom du fichier
        std::string sha256;
        json description;                 // describe_gguf()
        std::string expected_projector;   // un décodeur : le SHA-256 du projecteur que nomme son entrée du catalogue
    };

    /*!
        ensures
            - Renvoie, pour chaque projecteur, l'indice dans decoders du décodeur qui va avec
              lui, ou -1 :
                - un décodeur prend le projecteur dont son entrée du catalogue nomme le SHA-256
                  (expected_projector) ;
                - les autres projecteurs vont avec un décodeur d'une architecture qui lit les
                  images dont la largeur de projection est la leur ("projection_width" de
                  describe_gguf()) ; parmi plusieurs, celui dont le nom est le plus proche de
                  celui du projecteur : le même general.name dans les deux en-têtes d'abord,
                  puis le même nom de fichier une fois mis de côté le préfixe "mmproj", la
                  quantification et l'extension, puis le plus long début commun de ces noms ;
                  aucun quand deux sont aussi proches l'un que l'autre.
            - Chaque décodeur prend un projecteur au plus.
    !*/
    std::vector<int> pair_projectors(const std::vector<pairing_file>& decoders,
        const std::vector<pairing_file>& projectors);

    // Les entrées d'un modèle installé : celles que nomme son entrée du catalogue, "text" au moins, avec
    // "image" quand il a un projecteur et sans sinon.
    json model_inputs(const installed_model& m);

    // Les entrées d'une entrée du catalogue : celles qu'elle nomme, avec "image" seulement quand elle
    // nomme un projecteur.
    json entry_inputs(const json& entry);

    /*!
        Réservé aux tests (tests/test_host.cpp), qui servent le catalogue depuis un serveur
        local.  Aucun réglage ni aucune route n'y mène.
        ensures
            - model_manager::refresh_catalog() lit le catalogue à url à la place de son
              adresse ; une url vide rend l'adresse.
    !*/
    void official_catalog_for_tests(const std::string& url);

}

#endif // ALBIREO_HOST_MODELS_H_
