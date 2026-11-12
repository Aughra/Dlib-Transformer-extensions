// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_HTTP_FETCH_H_
#define ALBIREO_HOST_HTTP_FETCH_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/*
    Les téléchargements du service : le flux de mises à jour, le catalogue des modèles, les
    installateurs et les fichiers de modèles.  Chaque requête tourne sur un thread de son
    propre appelant, une tâche de fond du service, et est bornée dans le temps à chaque
    étape : la résolution du nom du serveur (sous Windows), la connexion, puis chaque
    attente de données.  Un service qui s'arrête, ou que les réglages mettent hors ligne,
    appelle http_cancel_all(), qui termine les requêtes en cours à leur étape suivante et
    fait échouer aussitôt les suivantes, pour qu'un réseau injoignable, comme sur une
    machine sans accès à Internet, ne retarde jamais le service.

    L'accès au web du mode agent (web_access.h) fait ses propres requêtes avec le même
    proxy (http_proxy_for()), la même résolution bornée (http_resolve()) et la même
    annulation (http_registration).
*/

namespace httplib { class Client; }

namespace albireo
{

    struct url_parts
    {
        std::string origin;   // "https://host:port"
        std::string target;   // "/path?query"
    };

    bool parse_url(const std::string& url, url_parts& out);

    /*!
        ensures
            - Fixe le proxy des téléchargements qui suivent, d'après le réglage "proxy" :
                - "" utilise le proxy du système : les variables HTTPS_PROXY et HTTP_PROXY
                  (avec NO_PROXY), puis, sous Windows, le proxy de WinHTTP que
                  "netsh winhttp set proxy" configure pour les services ;
                - "none" se connecte directement ;
                - "http://[user:password@]host[:port]" désigne un proxy, joint avec CONNECT.
            - Les adresses locales sont toujours jointes directement.
    !*/
    void set_http_proxy(const std::string& setting);

    // Indique si une valeur du réglage "proxy" est valide.
    bool valid_proxy_setting(const std::string& setting);

    /*!
        ensures
            - Termine les téléchargements en cours : un transfert s'arrête aussitôt, et sous
              Windows un nom en cours de résolution ou un serveur en cours de connexion
              aussi.  Une connexion qu'httplib est en train d'ouvrir, sa négociation TLS ou la
              réponse d'un proxy comprises, se termine dans ses limites (connect_seconds, puis
              le délai de la requête pour le proxy).  Une requête sur le point de se connecter
              n'ouvre rien : son client vérifie l'annulation avant que sa socket se connecte.
            - Fait échouer aussitôt les téléchargements qui suivent, jusqu'à http_restore().
            - reason devient l'erreur des téléchargements qu'elle termine ou refuse.
    !*/
    void http_cancel_all(const std::string& reason);
    void http_restore();
    bool http_cancelled();

    // Secondes accordées pour résoudre le nom d'un serveur, et pour s'y connecter.
    const int connect_seconds = 10;

    /*!
        ensures
            - Télécharge url dans body, en suivant jusqu'à cinq redirections, et en vérifiant
              les certificats des serveurs https avec ceux du système.  Échoue au-delà de
              max_bytes, quand aucune donnée n'arrive pendant timeout_seconds, quand la
              requête entière prend plus de total_seconds, et quand une page web (text/html)
              répond à la place du fichier, comme en envoie un proxy ou un filtre du réseau.
            - Les erreurs disent le genre d'échec ("the request is answered with the status
              403") et ne nomment jamais l'adresse ni son hôte, que les appelants nomment
              selon leurs besoins.
            - Ne lève jamais d'exception : une exception du transfert devient son erreur.
    !*/
    bool http_get(const std::string& url, std::string& body, std::string& error, size_t max_bytes = 16u << 20,
        int timeout_seconds = 20, int total_seconds = 120);

    /*!
        ensures
            - Télécharge les octets [from, to] de url (to < 0 : jusqu'à la fin), en les
              passant à receiver à mesure qu'ils arrivent ; receiver renvoie false pour
              arrêter.  Les redirections sont suivies avec la même plage.
            - total, quand il est fourni, reçoit la taille de la ressource entière quand le
              serveur la donne (Content-Range ou Content-Length), 0 sinon.
            - Échoue quand le serveur ignore la plage d'une requête qui ne commence pas à 0,
              répond avec une plage qui commence ailleurs, ou répond avec une page web
              (text/html), puisque écrire sa réponse corromprait le fichier ; échoue quand
              aucune donnée n'arrive pendant 30 s.
            - Avec to >= 0, receiver reçoit au plus les octets [from, to] : la lecture
              s'arrête une fois qu'ils sont arrivés, réponse d'un serveur qui ignore la plage
              comprise, et une réponse qui se termine avant eux échoue.
            - Les erreurs sont celles de http_get(), et la fonction ne lève jamais d'exception
              non plus : une exception du transfert ou de receiver devient son erreur.
    !*/
    bool http_get_range(const std::string& url, uint64_t from, int64_t to,
        const std::function<bool(const char*, size_t)>& receiver, std::string& error, uint64_t* total = nullptr);

    /*!
        Réservé aux tests (tests/test_host.cpp).  Aucun réglage ni aucune route n'y mène.
        ensures
            - Borne chaque attente de données de http_get() et de http_get_range() par
              read_seconds, et chaque requête entière de http_get() par total_seconds, quand
              ils sont supérieurs à 0 et inférieurs aux limites des appelants ; 0 rend aux
              appelants leurs limites.
    !*/
    void http_bounds_for_tests(int read_seconds, int total_seconds);

    /*!
        ensures
            - Donne dans host et port le proxy par lequel les téléchargements joignent origin
              ("https://host:port"), tel que set_http_proxy() le choisit, avec son utilisateur
              et son mot de passe quand il en a.
            - Renvoie false quand origin est joint directement : le réglage "none", aucun proxy
              du système, une adresse locale ou un hôte de NO_PROXY.
    !*/
    bool http_proxy_for(const std::string& origin, std::string& host, int& port, std::string& user,
        std::string& password);

    /*!
        ensures
            - Donne dans addresses les adresses IPv4 et IPv6 du nom host, sous forme de texte
              ("93.184.215.14", "2606:2800:21f:cb07:6820:80da:af6b:8b2c"), dans l'ordre du
              résolveur du système.  Sous Windows, la résolution prend au plus connect_seconds
              et se termine aussitôt quand les téléchargements sont annulés.
            - Renvoie false avec error quand le nom n'est pas résolu ou quand les
              téléchargements sont annulés.
    !*/
    bool http_resolve(const std::string& host, std::vector<std::string>& addresses, std::string& error);

    class http_registration
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'enregistrement du client d'une requête pendant que la requête tourne, pour
                que http_cancel_all() l'arrête : les téléchargements de ce module enregistrent
                leurs clients, et les requêtes de l'accès au web (web_access.h) aussi.
        !*/
    public:
        explicit http_registration(const std::shared_ptr<httplib::Client>& c);
        ~http_registration();
        http_registration(const http_registration&) = delete;
        http_registration& operator=(const http_registration&) = delete;

    private:
        std::shared_ptr<httplib::Client> c_;
    };

}

#endif // ALBIREO_HOST_HTTP_FETCH_H_
