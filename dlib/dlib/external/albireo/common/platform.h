// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_COMMON_PLATFORM_H_
#define ALBIREO_COMMON_PLATFORM_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/*
    Fichiers, dossiers et processus, avec des chemins UTF-8 sur toutes les plateformes.  Sous
    Windows, les chemins sont convertis en UTF-16 aux appels système, pour qu'un nom
    d'utilisateur ou un dossier avec des accents, ou dans une autre écriture, marche partout.
*/

namespace albireo
{


    // Le programme en cours, et son dossier.
    std::string executable_path();
    std::string executable_dir();

    /*
        Le dossier des données partagées par les programmes : les réglages, les certificats,
        la base de données, les journaux et, sauf si les réglages disent autre chose, les
        modèles.  $ALBIREO_HOME quand elle est définie, sinon %ProgramData%\Albireo sous Windows
        et ~/.local/share/albireo ailleurs.  Sous Windows, la variable est lue dans
        l'environnement de la machine, celui que reçoivent les services : la variable d'un
        utilisateur ne détourne ni l'installateur ni l'icône du service.  Le dossier que
        l'installateur enregistre (DataDir de HKLM\Software\Albireo) passe en premier.
    */
    std::string data_dir();

    /*
        Fait que data_dir() renvoie p dans ce processus à partir de là.  Les programmes de test
        l'appellent, pour ne jamais écrire dans le dossier de données d'un service installé ;
        les programmes d'Albireo ne le font jamais, et aucune variable de l'environnement d'un
        utilisateur ne peut le faire.
    */
    void set_data_dir(const std::string& p);

    std::string join_path(const std::string& a, const std::string& b);
    std::string parent_path(const std::string& p);
    std::string file_name(const std::string& p);

    bool file_exists(const std::string& p);
    bool dir_exists(const std::string& p);
    bool make_dirs(const std::string& p);
    uint64_t file_size(const std::string& p);
    // Si un autre programme écrit encore un fichier : tenu ouvert en écriture sous Windows,
    // modifié pendant les 30 dernières secondes ailleurs.
    bool file_being_written(const std::string& p);
    bool remove_file(const std::string& p);
    // Déplace un fichier, en remplaçant la destination.
    bool rename_file(const std::string& from, const std::string& to);
    // Copie un fichier, en remplaçant la destination ; une copie que cancel interrompt ne
    // laisse aucune destination derrière elle.
    bool copy_file(const std::string& from, const std::string& to, const std::atomic<bool>* cancel = nullptr);
    // Supprime un dossier et tout ce qu'il contient ; un lien part seul, ce vers quoi il mène reste.
    bool remove_tree(const std::string& p);
    std::vector<std::string> list_dir(const std::string& p);

    // Si p lui-même est un lien : un lien symbolique, une jonction ou un point de montage.
    bool is_link(const std::string& p);

    /*
        Le chemin de p avec ses liens résolus, tel que le système l'ouvre : un chemin sur une
        lettre de lecteur, ou \\server\share\... sur un partage réseau, sous Windows.  Une
        chaîne vide quand p ne peut pas être ouvert.
    */
    std::string final_path(const std::string& p);

    // Si p atteint son fichier ou son dossier sans aucun lien : son chemin final est p lui-même.
    bool direct_path(const std::string& p);

    // Si path, liens résolus, est folder ou se trouve dessous ; path peut ne pas exister encore.
    bool path_inside(const std::string& path, const std::string& folder);

    /*
        Vérifie le propriétaire de p et ses dossiers parents, sans suivre un lien final.
        Accepte le compte effectif ou root ; ni p ni ses parents ne doivent être modifiables par
        les autres comptes, sauf un dossier temporaire de root avec le bit sticky.  Les ACL
        étendues sont lues : une entrée « allow » qui laisse un autre compte écrire est refusée,
        une entrée « deny » est acceptée.
    */
    bool owned_by_system(const std::string& p);

    /*
        Si le lecteur qui contient p, ou le contiendrait quand p n'existe pas encore, garde les
        permissions de ses fichiers, comme NTFS et ReFS et contrairement à exFAT et FAT32.
        file_system reçoit le nom de son système de fichiers ("NTFS", "exFAT"...), vide quand le
        lecteur ne répond pas, ce qui ne garde aucune permission non plus.  Toujours vrai
        ailleurs que sous Windows, file_system étant alors vide.
    */
    bool drive_keeps_permissions(const std::string& p, std::string& file_system);

    /*
        Fait de p un dossier que seuls SYSTEM et les administrateurs modifient, et que les
        utilisateurs lisent : les dossiers où le service et l'installateur écrivent avec leurs
        droits.  Sous Windows :
            - un dossier absent est créé ainsi ;
            - les liens sur son chemin doivent appartenir à SYSTEM ou aux administrateurs, et le
              dossier vers lequel ils mènent doit être sur un lecteur local qui garde les
              permissions (drive_keeps_permissions()) ;
            - le dossier doit leur appartenir aussi, ou être vide : il est alors repris ;
            - à moins d'avoir déjà ces permissions, il les reçoit, et ce que d'autres comptes y
              ont placé, liens compris, est renommé avec le suffixe ".untrusted-<random>",
              qu'aucun programme ne lit ; avec check_content, ce qu'il contient est vérifié même
              quand il avait ces permissions.
        Renvoie false, avec la raison dans error, quand le dossier ne peut pas être rendu tel.
        Sur macOS, crée p si nécessaire, le réserve au compte effectif (mode 0700) et, avec
        check_content, refuse les liens et les entrées non régulières, d'un autre propriétaire
        ou modifiables par les autres comptes, par leurs permissions ou par leur ACL.  Il ne les
        déplace pas.  Une ACL de p qui laisse un autre compte écrire est vidée.
    */
    bool secure_folder(const std::string& p, std::string& error, bool check_content = false);

    /*
        Vérifie p et son contenu récursif : dossier sans lien, propriétaires reconnus et aucune
        permission d'écriture pour le groupe ou les autres, ni par les permissions POSIX ni par
        une entrée « allow » d'ACL étendue.
    */
    bool folder_admin_only(const std::string& p);

    class link_free_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un fichier ouvert en lecture sans passer par aucun lien.  Chaque dossier sur le
                chemin d'un dossier de confiance jusqu'au fichier est ouvert dans le dossier qui
                le précède, et le fichier dans le dernier ; un lien symbolique, une jonction ou
                un point de montage parmi eux, fichier compris, fait échouer l'ouverture, pour
                qu'un lien posé ou échangé pendant l'ouverture du fichier ne mène jamais
                ailleurs.  Sous Windows, le fichier n'est partagé qu'en lecture tant qu'il est
                ouvert : ce qui est lu est ce qui a été vérifié.
        !*/
    public:
        link_free_file() {}
        ~link_free_file() { close(); }
        link_free_file(const link_free_file&) = delete;
        link_free_file& operator=(const link_free_file&) = delete;

        /*!
            ensures
                - Ouvre path, qui se trouve sous folder.  folder lui-même est ouvert comme son
                  chemin y mène, liens compris ; un folder vide désigne la racine du lecteur de
                  path.
                - Renvoie false sinon, avec la raison dans why : "link" (un lien en chemin,
                  fichier compris), "missing", ou "invalid" (path hors de folder, un nom "." ou
                  "..", une entrée qui n'est pas un fichier ordinaire, ou une autre erreur).
        !*/
        bool open(const std::string& path, const std::string& folder, std::string& why);
        bool is_open() const;
        uint64_t size() const { return size_; }
        // Le nombre de noms du fichier : plus d'un pour un fichier qui a des liens physiques.
        uint32_t links() const { return links_; }
        // Le volume et l'index du fichier, égaux pour deux noms du même fichier.
        const std::string& identity() const { return identity_; }
        // Lit jusqu'à n octets ; renvoie les octets lus, 0 à la fin du fichier, -1 en cas d'erreur.
        long long read(char* data, size_t n);
        bool seek(uint64_t offset);
        void close();

    private:
        int fd_ = -1;
        uint64_t size_ = 0;
        uint32_t links_ = 0;
        std::string identity_;
    };

    // Lit un fichier d'au plus max_bytes sans passer par aucun lien, comme link_free_file l'ouvre.
    bool read_file_link_free(const std::string& path, const std::string& folder, std::string& data,
        uint64_t max_bytes);

    // Si le dossier path, sous folder, est atteint sans aucun lien, comme link_free_file ouvre un
    // fichier.
    bool link_free_folder(const std::string& path, const std::string& folder);

    // L'identité du fichier p lui-même, sans suivre de lien, telle que link_free_file::identity() la
    // donne, ou une chaîne vide pour un lien, un dossier ou une entrée absente.
    std::string file_identity(const std::string& p);

    // Si un fichier ouvert, un handle sous Windows et un descripteur ailleurs, se trouve à path atteint
    // sans aucun lien : un dossier de path remplacé par un lien après une vérification se voit.
    bool handle_at(int fd, const std::string& path);

    class new_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un fichier créé en écriture qui ne doit pas encore exister : un nom pris entre-
                temps, par un lien par exemple, fait échouer la création, pour que les écritures
                restent dans le fichier créé.  Un fichier que close() ne termine pas est laissé
                à l'appelant, qui le supprime.
        !*/
    public:
        new_file() {}
        ~new_file() { close(); }
        new_file(const new_file&) = delete;
        new_file& operator=(const new_file&) = delete;

        bool create(const std::string& path);
        bool write(const char* data, size_t n);
        // Écrit les données sur le disque et ferme le fichier ; false quand une écriture ou le vidage a
        // échoué.
        bool close();

    private:
        int fd_ = -1;
        bool failed_ = false;
    };

    bool read_file(const std::string& p, std::string& data, uint64_t max_bytes = 256u << 20);
    /*
        Écrit par un fichier temporaire dont les données atteignent le disque avant qu'il soit
        renommé par-dessus la cible, pour qu'un plantage ou une coupure de courant laisse
        l'ancien fichier ou le nouveau, jamais un fichier vide ou à moitié écrit.
    */
    bool write_file_atomic(const std::string& p, const std::string& data);

    /*
        Écrit un fichier que seuls le compte du programme et les administrateurs peuvent lire
        (mode 0600 ailleurs que sous Windows), par un fichier temporaire renommé par-dessus la
        cible.  Avec interactive_readers, les utilisateurs de la console peuvent aussi le lire
        sous Windows : le jeton de la zone de notification est écrit ainsi.
    */
    bool write_file_restricted(const std::string& p, const std::string& data, bool interactive_readers = false);

    // Octets disponibles pour l'utilisateur sur le volume d'un chemin.
    uint64_t free_disk_space(const std::string& p);

    // Secondes depuis 1970, et la forme ISO 8601 d'une heure en UTC.
    int64_t now_unix();
    std::string iso_time(int64_t t);

    // Une chaîne hexadécimale aléatoire de n caractères, tirée du générateur du système.
    std::string random_hex(size_t n);

    // Compare des versions "1.2.3" : négatif, zéro ou positif selon que a est plus ancienne, égale
    // ou plus récente.
    int compare_versions(const std::string& a, const std::string& b);

    struct process_options
    {
        // Écrit sur l'entrée standard du programme, qui se ferme une fois l'écriture faite.
        const std::string* input = nullptr;
        /*
            Sous Windows, le programme tourne sans console, avec une copie du jeton de ce
            processus sans ses privilèges et de faible intégrité, seul dans un job qui l'arrête
            au-delà de memory_limit octets et quand le job se ferme, et qui lui refuse tout
            processus enfant ; il ne tourne pas quand Windows refuse l'une de ces étapes.
            Ailleurs, son espace d'adressage est limité par memory_limit, il n'écrit aucun
            fichier et ne laisse aucun fichier core, et sous Linux il n'acquiert aucun privilège
            en lançant un autre programme.
        */
        bool restricted = false;
        size_t memory_limit = 0;
        // Le dossier où démarre le programme, celui de ce processus quand il est vide.
        std::string directory;
    };

    /*
        Lance un programme avec des arguments et l'attend ; renvoie son code de sortie, ou -1.
        Le programme est arrêté quand il tourne au-delà de timeout_ms, ou dès que stop devient
        vrai.  output reçoit ce que le programme écrit sur sa sortie standard et sa sortie
        d'erreur.
    */
    int run_process(const std::string& program, const std::vector<std::string>& args, std::string* output = nullptr,
        unsigned timeout_ms = 60000, const std::atomic<bool>* stop = nullptr, const process_options* options = nullptr);

    /*
        Termine le processus avec code une fois ses sorties standard vidées, sans décharger ses
        bibliothèques et sans les destructeurs de ses objets statiques.  Un processus qui a
        chargé le moteur d'un appareil se termine ainsi : le runtime d'OpenVINO ou de CUDA
        serait sinon démonté pendant le déchargement de sa bibliothèque, ce que Windows fait
        sous son verrou du chargeur, où le runtime attend des threads qui ne peuvent pas se
        terminer.
    */
    [[noreturn]] void end_process(int code);

}

#endif // ALBIREO_COMMON_PLATFORM_H_
