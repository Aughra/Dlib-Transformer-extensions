// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_VAULT_H_
#define PSEUDALIS_HOST_VAULT_H_

#include <string>
#include <vector>

namespace pseudalis
{
    class vault
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les fichiers chiffrés du service : les originaux des fichiers qu'il a
                protégés, leurs images, et le contenu des fichiers du lecteur.  Chaque blob
                se trouve dans <dir>/<two digits>/<identifier>.bin, son identifiant étant 32
                chiffres hexadécimaux aléatoires, chiffré avec AES-256-CTR sous un bloc
                compteur aléatoire et authentifié avec HMAC-SHA-256 sur son identifiant, son
                bloc compteur et son texte chiffré : un blob altéré, ou déplacé sous un autre
                identifiant, ne s'ouvre pas.  Le dossier est privé au compte qui exécute le
                service.

            THREAD SAFETY
                Les méthodes peuvent être appelées depuis plusieurs threads à la fois.
        !*/
    public:
        vault(const std::string& dir, const std::string& encryption_key, const std::string& authentication_key);
        ~vault();

        // Stocke data ; renvoie son identifiant, ou une chaîne vide quand il ne peut pas être écrit.
        std::string put(const std::string& data);
        // Lit un blob ; false quand il manque, est altéré ou illisible.
        bool get(const std::string& id, std::string& data) const;
        bool remove(const std::string& id);
        bool exists(const std::string& id) const;
        static bool valid_id(const std::string& id);
        /*!
            Renvoie les identifiants des blobs que contient le coffre, et supprime les fichiers
            des écritures interrompues.  Pour le démarrage du service, avant toute écriture.
        !*/
        std::vector<std::string> list_and_clean();

    private:
        std::string path_of(const std::string& id) const;
        std::string dir_;
        std::string enc_, mac_;
    };
}

#endif // PSEUDALIS_HOST_VAULT_H_
