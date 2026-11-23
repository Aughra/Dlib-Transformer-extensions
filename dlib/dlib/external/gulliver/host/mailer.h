// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_MAILER_H_
#define GULLIVER_HOST_MAILER_H_

#include <string>

namespace gulliver
{
    /*
        Le facteur du service (fork pour macOS, 2026-11-23) : il envoie les codes de connexion et
        les liens « mot de passe oublié » des comptes (accounts.h).  Il ne parle qu'à un serveur SMTP
        du Mac lui-même, 127.0.0.1:port : Proton Mail Bridge, qui chiffre le message et l'envoie par
        Proton.  Comme le facteur et le Bridge sont sur la même machine, la conversation passe en
        clair sur la boucle locale (le Bridge accepte AUTH PLAIN sans STARTTLS) et ne quitte jamais
        le Mac : aucune adresse d'une autre machine n'est acceptée, pour que le mot de passe du
        Bridge ne traverse jamais le réseau.

        Le message est en texte brut UTF-8, encodé en base64 (sûr quel que soit le serveur), avec
        un sujet encodé selon la RFC 2047.
    */

    struct mail_server
    {
        std::string from;      // l'adresse d'envoi, qui sert aussi d'utilisateur SMTP
        int port = 1025;       // le port du Bridge sur 127.0.0.1
        std::string secret;    // le mot de passe du Bridge ; vide : pas d'authentification
    };

    // Vrai pour une adresse simple « local@domaine » : sans espace, chevron, guillemet ni caractère de
    // contrôle, de 3 à 254 octets, un seul @, un point dans le domaine.
    bool valid_mail_address(const std::string& address);

    // L'adresse coupée de ses espaces de bord et mise en minuscules (la forme rangée dans la base).
    std::string normalized_mail_address(const std::string& address);

    /*!
        ensures
            - Envoie un message de server.from à to, et renvoie true quand le serveur l'a accepté.
            - Sinon renvoie false et décrit l'échec dans error (en anglais, pour le journal), sans
              jamais y mettre le mot de passe.  Chaque échange attend 20 secondes au plus.
    !*/
    bool send_mail(const mail_server& server, const std::string& to, const std::string& subject,
        const std::string& body, std::string& error);
}

#endif // GULLIVER_HOST_MAILER_H_
