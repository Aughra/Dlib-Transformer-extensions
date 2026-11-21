// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_TLS_GATE_H_
#define GULLIVER_HOST_TLS_GATE_H_

#include <memory>
#include <string>

#include <Security/Security.h>

namespace gulliver
{

    /*!
        WHAT THIS OBJECT REPRESENTS
            Le portier TLS du serveur local (fork pour macOS, 2026-11-20 : le TLS d'Apple à la
            place de Mbed TLS).  Il écoute sur une adresse et un port TCP, termine le TLS par
            Network.framework avec l'identité du serveur, et passe les octets en clair, dans les
            deux sens, au serveur HTTP de cpp-httplib qui écoute sur une prise Unix.  Comme un
            portier qui ouvre les enveloppes scellées à l'entrée et passe les lettres à
            l'intérieur : les routes du serveur ne voient que du HTTP, et la prise Unix, dans un
            dossier du seul compte, n'est joignable par aucune autre machine ni aucun autre
            compte.

            Au plus 128 connexions sont tenues à la fois ; les suivantes sont fermées aussitôt.
            Une poignée de main TLS qui n'aboutit pas en 10 secondes ferme sa connexion, y compris
            une connexion qui parle HTTP en clair, que Network.framework ne refuse pas de lui-même.  TLS 1.2
            au moins, ALPN « http/1.1 ».
    !*/
    class tls_gate
    {
    public:
        // identity : le certificat du serveur et sa clé (retenue ici) ; chain : les certificats
        // envoyés après lui (l'autorité locale), ou nullptr.
        tls_gate(SecIdentityRef identity, CFArrayRef chain);
        ~tls_gate();
        tls_gate(const tls_gate&) = delete;
        tls_gate& operator=(const tls_gate&) = delete;

        /*!
            ensures
                - Écoute sur host:port et passe chaque connexion à la prise Unix backend_path.
                - Rend la main quand l'écoute est établie (true) ou a échoué (false, la raison dans
                  error), par exemple parce que le port est pris : comme bind_to_port().
        !*/
        bool listen(const std::string& host, int port, const std::string& backend_path, std::string& error);

        // Ferme l'écoute et les connexions ouvertes ; sans effet si rien n'écoute.
        void stop();

    private:
        struct impl;
        std::unique_ptr<impl> p_;
    };

    /*!
        ensures
            - Crée un dossier privé (0700) dans le dossier temporaire du compte et renvoie le
              chemin d'une prise Unix à y ouvrir, ou "" en cas d'échec.  Le dossier est retiré par
              remove_private_socket().
    !*/
    std::string private_socket_path(const std::string& name);
    void remove_private_socket(const std::string& path);

}

#endif // GULLIVER_HOST_TLS_GATE_H_
