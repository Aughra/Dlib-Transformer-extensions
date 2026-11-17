// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_CARNET_H_
#define GULLIVER_HOST_CARNET_H_

#include <string>

#include "../common/json.h"

/*
    Le carnet du portage (fork pour macOS, demande de l'humain du 2026-11-15 : « Albireo affiche lui-même les
    documents ») : les documents du dépôt du fork (docs/, blog/, logs/, quelques README et les notes de version),
    lus en lecture seule et mis en page par web/carnet/.  Il remplace le petit site à part du port 8650.

        GET /albireo/v1/carnet              le sommaire : ses sections, et les épisodes de chaque blog
        GET /albireo/v1/carnet/file?path=   un fichier, par son chemin dans le dépôt

    La liste des fichiers lisibles est fermée : les motifs de macos/site-docs/sommaire.json (un dossier exact et
    un nom, « * » ou « *.extension »).  Un chemin avec un composant vide, caché ou « .. », ou qui passe par un
    lien, est refusé ; rien d'autre du dépôt n'est atteint.  Le dépôt est celui où le service a été construit
    (GULLIVER_FORK_ROOT_DEFAULT, donné par CMakeLists.txt), ou celui que nomme la variable GULLIVER_FORK_ROOT.
*/

namespace albireo
{
    // Le dossier du dépôt, ou une chaîne vide quand il n'a pas de sommaire.
    std::string carnet_root();

    /*!
        ensures
            - Renvoie {sections: [{title, entries: [{path, title, desc}]}], blogs: [{title, episodes: [{number,
              path, title}]}]}, les entrées dont le fichier manque étant omises ; found false sans dépôt.
    !*/
    json carnet_index();

    /*!
        ensures
            - Lit le fichier de chemin rel dans le dépôt quand la liste le permet : data reçoit ses octets et type
              son type pour le navigateur.  False sinon.
    !*/
    bool carnet_file(const std::string& rel, std::string& data, std::string& type);
}

#endif // GULLIVER_HOST_CARNET_H_
