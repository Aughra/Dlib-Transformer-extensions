// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_ALBIREO_CLIENT_H_
#define PSEUDALIS_HOST_ALBIREO_CLIENT_H_

#include <cstdint>
#include <mutex>
#include <string>

#include "../common/json.h"

namespace pseudalis
{
    class albireo_client
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les descriptions des images par Albireo, le service d'inférence local,
                quand il est installé sur le même ordinateur et que l'un de ses modèles lit
                les images.  Albireo se trouve par son interface : son dossier de données
                (DataDir de HKLM\Software\Albireo, que son installateur enregistre), le port
                de son settings.json (8443 par défaut) et son autorité locale
                (certificates\albireo-authority.crt), qui vérifie son certificat ; puis
                GET /v1/models, dont les "inputs" désignent les modèles qui lisent les images.
                Sous Windows, le dossier, son dossier de certificats, l'autorité et les
                réglages doivent appartenir à SYSTEM ou aux administrateurs.  Le résultat est
                gardé une minute, vérifié de nouveau par un seul appelant à la fois, hors du
                verrou de l'état.

                describe() envoie l'image à POST /v1/chat/completions, une requête à la fois,
                et demande une ou deux phrases factuelles qui ne nomment personne et ne
                recopient aucun nom, aucune adresse ni aucun numéro que montre l'image ; le
                moteur protège la réponse comme tout texte avant de l'écrire.  Seules les
                images PNG et JPEG de 15 Mo au plus sont envoyées.
        !*/
    public:
        albireo_client();

        // {"enabled", "found", "port", "model", "reason"}
        json status();
        void set_enabled(bool on);

        // La description, ou une chaîne vide avec la raison dans error.
        std::string describe(const std::string& bytes, const std::string& media_type, const std::string& language,
            std::string& error);

        // Le dossier de données d'Albireo sur cet ordinateur, vide quand il n'en a pas.
        static std::string albireo_data_dir();

    private:
        bool refresh(std::string& reason);
        // Trouve Albireo et le modèle qui lit les images, sans le verrou de l'état.
        static bool probe(int& port, std::string& ca, std::string& model, std::string& reason);
        std::mutex m_;              // l'état
        std::mutex request_m_;      // une description à la fois
        bool enabled_ = true;
        bool checking_ = false;
        int64_t checked_ = 0;
        bool found_ = false;
        int port_ = 8443;
        std::string ca_;
        std::string model_;
        std::string reason_;
    };
}

#endif // PSEUDALIS_HOST_ALBIREO_CLIENT_H_
