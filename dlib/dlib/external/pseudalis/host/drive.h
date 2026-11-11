// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_DRIVE_H_
#define PSEUDALIS_HOST_DRIVE_H_

#include <memory>
#include <string>

#include "../common/json.h"

namespace pseudalis
{
    class service;

    class drive_host
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le lecteur de Pseudalis : le dossier drive du dossier de données, que le
                service surveille et montre sous la lettre des réglages pour chaque session
                de l'ordinateur, comme le ferait la commande subst.  Il fonctionne comme
                n'importe quel dossier, depuis l'Explorateur et depuis l'invite de commandes,
                à une différence près : un fichier qui y est écrit est anonymisé une fois que
                son auteur l'a fermé.  Son original part dans le coffre chiffré avec un
                enregistrement, le fichier anonymisé prend sa place, sous son nom aux valeurs
                remplacées (images : <name>.txt, qui contient leur marqueur), et le fichier
                écrit est supprimé.  Un dossier qui arrive prend un nom sans donnée
                personnelle une fois que les fichiers qu'il contient sont au repos.  Un
                fichier d'un autre type, ou qui ne peut pas être anonymisé, part dans le
                dossier des fichiers non traités de l'utilisateur qui l'a écrit
                (refused\<user> dans le dossier de données), à la place qu'il avait dans
                l'arborescence du lecteur, avec un événement.

                Pas de pilote : le fichier écrit reste dans son dossier, en clair, de sa
                dernière écriture à son traitement, quelques secondes pour la plupart des
                documents, pendant lesquelles le service le détient seul (violation de
                partage).  Supprimer le dernier fichier du lecteur qui montre un
                enregistrement marque celui-ci (service::mark_removed()), son original restant
                gardé pendant les jours du réglage removed_days ; un fichier retrouvé dans le
                lecteur, renommé, déplacé ou copié là, le conserve.  Un fichier extrait d'une
                archive protégée que la base de données enregistre reste tel quel, enregistré
                avec l'original de son entrée (service::adopt()) ; un fichier protégé dont
                l'original a été oublié reste tel quel, sans enregistrement.  Les fichiers
                déjà traités sont enregistrés dans la base de données, pour qu'un redémarrage
                ne traite que ce qui a changé.

                Hors de Windows, le dossier fonctionne sans lettre.
        !*/
    public:
        explicit drive_host(service& svc);
        ~drive_host();
        drive_host(const drive_host&) = delete;
        drive_host& operator=(const drive_host&) = delete;

        // letter : "A".."Z", ou vide pour aucun lecteur.
        bool start(const std::string& letter, std::string& error);
        void stop();
        /*!
            ensures
                - Renvoie {"letter", "mounted", "folder", "files", "bytes", "error",
                  "refused_folder"}.
        !*/
        json status() const;

    private:
        struct impl;
        std::unique_ptr<impl> p_;
    };
}

#endif // PSEUDALIS_HOST_DRIVE_H_
