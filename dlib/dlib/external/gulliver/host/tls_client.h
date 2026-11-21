// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_TLS_CLIENT_H_
#define GULLIVER_HOST_TLS_CLIENT_H_

#include <memory>
#include <string>

namespace httplib { class Client; enum class Error; }

namespace gulliver
{

    /*!
        WHAT THIS OBJECT REPRESENTS
            Le chemin d'une connexion sortante en HTTPS (fork pour macOS, 2026-11-21 : le TLS
            d'Apple à la place de Mbed TLS).
    !*/
    struct tls_route
    {
        std::string host;           // le nom du serveur : nom TLS (SNI), en-tête Host, vérification
        int port = 443;
        std::string address;        // l'adresse vérifiée où se connecter (épinglage), ou "" pour résoudre host
        std::string ca_path;        // un fichier PEM d'autorités à approuver seules, ou "" pour celles de macOS
        std::string proxy_host;     // un mandataire HTTP (tunnel CONNECT), ou ""
        int proxy_port = 0;
        std::string proxy_user, proxy_password;
        int connect_seconds = 10;   // connexion TCP et poignée de main TLS
    };

    /*!
        ensures
            - Renvoie un client de cpp-httplib qui parle HTTP en clair à un pont privé (une prise
              Unix d'un dossier du seul compte) ; le pont ouvre, pour chaque connexion du client,
              une connexion TLS par Network.framework vers route : à route.address quand elle est
              donnée, avec route.host comme nom TLS et comme nom vérifié dans le certificat, à
              travers le mandataire quand il est donné, les certificats vérifiés contre les
              autorités de macOS ou celles de route.ca_path.  Le pont remet route.host dans la
              ligne Host de chaque requête (le client, joint par une prise Unix, y écrirait
              « localhost ») : une connexion ne doit porter qu'une requête (set_keep_alive(false)).
            - Le pont vit tant que le client.  Renvoie nullptr, avec la raison dans error, quand
              le pont ne peut pas être ouvert.
    !*/
    std::shared_ptr<httplib::Client> https_client(const tls_route& route, std::string& error);

    /*!
        ensures
            - Renvoie e, ou, quand c vient de https_client() et que sa dernière connexion a échoué
              dans le TLS, l'erreur de cpp-httplib qui le dit : SSLServerVerification (certificat
              non approuvé), SSLServerHostnameVerification (il nomme un autre serveur),
              SSLConnection (poignée de main), Connection (le serveur ou le mandataire n'est pas
              joint).  Les messages et les codes d'erreur de Gulliver restent ainsi ceux d'avant.
    !*/
    httplib::Error tls_error(const httplib::Client& c, httplib::Error e);

}

#endif // GULLIVER_HOST_TLS_CLIENT_H_
