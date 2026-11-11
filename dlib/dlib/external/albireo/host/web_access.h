// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_WEB_ACCESS_H_
#define ALBIREO_HOST_WEB_ACCESS_H_

#include <string>

#include "../common/json.h"
#include "settings.h"

/*
    L'accès au web.  Le bouton Web de la zone de saisie de l'application web donne au modèle
    deux outils, web_search et fetch_page, qui atteignent le web par deux routes du
    service :

        POST /albireo/v1/web/search   {"query", "count", "language"}
        POST /albireo/v1/web/fetch    {"url", "language"}

    La recherche passe par la recherche de Wikipédia, son interface REST, dans la langue de la
    requête (le réglage web_search "wikipedia", par défaut) ; web_search "none" la désactive.
    L'adresse de Wikipédia est fixe, et la règle des adresses publiques ne s'y applique pas.

    Une page n'est lue qu'à une adresse publique.  Le nom de son hôte est refusé quand il
    désigne cet ordinateur ou un réseau local (localhost, *.localhost, *.local, *.internal,
    *.home.arpa, un nom d'un seul label) ; sinon il est résolu, et la page est refusée quand
    l'une de ses adresses n'est pas publique (public_address()).  La connexion va à l'adresse
    vérifiée (Client::set_hostname_addr_map), pour que le nom ne soit pas résolu à nouveau
    entre la vérification et la connexion, tandis que l'en-tête Host, le nom de serveur de TLS
    et la vérification du certificat gardent le nom.  Les redirections sont suivies ici, cinq
    au plus, chaque adresse vérifiée comme la première.  Derrière un proxy (http_fetch.h), la
    requête passe par le proxy, qui résout à nouveau le nom de son côté : la vérification
    s'applique aux adresses que cet ordinateur résout, et un nom qu'il ne peut pas résoudre est
    refusé.

    Un texte est lu jusqu'à 2 Mo et coupé au-delà, un fichier PDF jusqu'à 20 Mo et refusé
    au-delà ; la réponse donne les octets en base64, que l'application web décode.  Une requête
    prend au plus 10 s pour se connecter, 15 s pour chaque attente de données et 30 s en tout,
    et ne garde aucun cookie.  Les deux routes sont refusées tant que le service travaille hors
    ligne ou une fois que l'administrateur a désactivé l'accès au web, et leurs requêtes se
    terminent aussitôt quand le service s'arrête (http_cancel_all()).  Le journal nomme l'hôte
    lu et le fournisseur d'une recherche, jamais un chemin, une requête ni le texte recherché.
*/

namespace albireo
{

    // La réponse d'une route de l'accès au web : son corps avec le statut 200, sinon l'erreur
    // que le serveur donne avec send_error().
    struct web_reply
    {
        int status = 200;
        json body;
        std::string message;      // l'erreur, en anglais
        std::string code;         // son code : invalid_url, address_refused, offline,
                                  // web_access_off...
        std::string param;        // le membre de la requête qu'elle concerne, vide s'il n'y en a pas
        int retry_after = 0;      // secondes à attendre avant une autre requête, 0 si inconnu
    };

    /*!
        ensures
            - Renvoie true quand numeric, une adresse IPv4 en notation décimale pointée ou une
              adresse IPv6, est publique.  Ne le sont pas : les plages IPv4 0.0.0.0/8,
              10.0.0.0/8, 100.64.0.0/10, 127.0.0.0/8, 169.254.0.0/16, 172.16.0.0/12,
              192.0.0.0/24, 192.0.2.0/24, 192.88.99.0/24, 192.168.0.0/16, 198.18.0.0/15,
              198.51.100.0/24, 203.0.113.0/24, 224.0.0.0/4 et 240.0.0.0/4 ; les plages IPv6
              ::/96 (::, ::1 et les adresses compatibles IPv4), 64:ff9b:1::/48, 100::/64,
              2001::/32, 2001:db8::/32, fc00::/7, fe80::/10, fec0::/10 et ff00::/8 ; les
              adresses de ::ffff:0:0/96, 64:ff9b::/96 et 2002::/16 dont l'adresse IPv4
              intégrée n'est pas publique.
            - Renvoie false pour un texte qui n'est pas une adresse.
    !*/
    bool public_address(const std::string& numeric);

    // Le fournisseur de la recherche de s : "none" quand web_search vaut "none", "wikipedia" sinon.
    std::string web_search_provider(const host_settings& s);

    /*!
        ensures
            - Renvoie ce que /albireo/v1/status donne à l'application web sous "web" :
              {"available": true quand web_access est activé et que le service n'est pas hors
               ligne, "search": web_search_provider(s) tant que c'est disponible, "none"
               sinon}.  La lecture de pages est disponible avec l'accès au web, quel que soit
              le fournisseur de la recherche.
    !*/
    json web_capability(const host_settings& s);

    /*!
        ensures
            - Répond à POST /albireo/v1/web/fetch : lit la page à request["url"], une adresse
              http ou https, avec l'Accept-Language request["language"] quand il est fourni.
            - Le corps de la réponse est {"url", "final_url", "status", "content_type",
              "charset", "size", "truncated", "data"} : l'adresse demandée et celle qui a
              répondu après les redirections, le statut du site (4xx et 5xx compris, avec leur
              corps), le type de média de son Content-Type et son charset ("" sans charset), le
              nombre d'octets lus, si le texte a été coupé à 2 Mo, et les octets en base64.
            - Refuse avec 400 une adresse invalide ; 403 une adresse qui n'est pas publique, un
              service hors ligne et un accès au web désactivé ; 413 un fichier PDF de plus de
              20 Mo ; 415 un type autre que text/<subtype>, application/xhtml+xml,
              application/xml, application/<name>+xml, application/json,
              application/<name>+json et application/pdf ; 502 un site injoignable ou dont la
              connexion TLS échoue ; 503 une quatrième requête simultanée (code busy) et une
              requête que l'arrêt du service a terminée ; 504 un site qui ne répond pas à temps.
    !*/
    web_reply web_fetch(const json& request, const host_settings& s);

    /*!
        ensures
            - Répond à POST /albireo/v1/web/search : recherche request["query"], de 1 à 400
              caractères, par web_search_provider(s), au plus request["count"] résultats (de 1
              à 10, 5 par défaut), dans la langue request["language"] quand elle est fournie
              ([a-z]{2,3} avec une région facultative).
            - Le corps de la réponse est {"provider", "results": [{"title", "url", "snippet"}]},
              seulement les résultats dont l'adresse est http ou https.
            - Refuse avec 400 une requête invalide ; 403 un service hors ligne, un accès au web
              désactivé et une recherche sans fournisseur (code no_search_provider) ; 429 un
              fournisseur qui limite le débit des requêtes, avec retry_after quand il en donne
              un ; 502 un fournisseur qui répond avec un autre statut ou une autre réponse que
              celle de son interface, ou qui est injoignable ; 503 comme web_fetch() ; 504 un
              fournisseur qui ne répond pas à temps.
    !*/
    web_reply web_search(const json& request, const host_settings& s);

    /*
        Réservées aux tests (tests/test_host.cpp), qui servent leurs pages et les réponses des
        fournisseurs depuis un serveur local.  Aucun réglage ni aucune route n'atteint ces
        fonctions.
    */
    // Permet à web_fetch() de lire les adresses de bouclage, 127.0.0.0/8 et ::1.
    void web_allow_loopback_for_tests(bool allow);
    // Envoie les requêtes de Wikipédia à origin, "http://127.0.0.1:<port>" ; une origine vide leur
    // rend leur propre adresse.
    void web_provider_origin_for_tests(const std::string& origin);

}

#endif // ALBIREO_HOST_WEB_ACCESS_H_
