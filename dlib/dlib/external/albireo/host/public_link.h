// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_PUBLIC_LINK_H_
#define ALBIREO_HOST_PUBLIC_LINK_H_

#include <cstdint>
#include <string>

/*
    L'espace de publication d'Albireo : un dossier public, en lecture seule, dont le dossier dist
    contient le flux de mises à jour, le catalogue des modèles, leurs signatures et les
    installateurs, et dont le dossier models contient les fichiers des modèles du catalogue,
    chacun dans le dossier de son identifiant sous le nom du fichier de sa source officielle
    (models/<identifier>/<file name>).  C'est la seule source des mises à jour : le flux, sa
    signature et les installateurs ne viennent que de lui, et un fichier qu'il ne contient pas, ou
    un espace qui ne répond pas, fait échouer la mise à jour avec un message qui nomme le fichier
    dans l'espace de publication (updater.h).  C'est la première source du catalogue et des
    fichiers de modèles, et leurs adresses officielles la seconde : quand l'espace n'est pas
    défini, ne répond pas, ne contient pas un fichier, ou en contient un que sa taille, sa
    signature ou son hash refuse, le catalogue vient de son adresse officielle dans le dépôt
    d'Albireo, et chaque partie d'un modèle de l'adresse officielle que le catalogue donne pour
    elle, son hub (model_manager dans models.h).  Les signatures et les hashes vérifient chaque
    fichier, quelle que soit sa source, pour qu'aucune des deux sources n'apporte rien que le
    catalogue et le flux ne nomment pas.

    Le dossier est un lien public de pCloud, une constante du build qu'aucun réglage, aucune
    route ni aucun document ne donne.  Il répond par l'interface de pCloud : showpublink liste le
    dossier avec ses sous-dossiers et l'identifiant et la taille de chaque fichier, et
    getpublinkdownload donne l'adresse d'un fichier sur un serveur de contenu, valable quelques
    heures.  Le mot de passe du lien accompagne chaque requête.  Il est intégré au service à
    partir d'un fichier hors du dépôt, lu à la configuration (ALBIREO_PUBLIC_LINK_PASSWORD_FILE
    dans CMakeLists.txt), et gardé masqué dans le programme.  Un service construit sans ce
    fichier joint le lien sans mot de passe.

    Aucun message ne nomme le lien, son code, son mot de passe, l'adresse de son interface ou
    d'un serveur de contenu, ni un hôte de pCloud : un échec nomme le fichier dans l'espace de
    publication et le genre d'échec ("the publication space of Albireo cannot be reached for
    dist/feed.json: the request is answered with the status 403").  Quand l'adresse officielle
    échoue aussi, le message ajoute cette adresse, qui est publique, et son échec.  Le code et le
    mot de passe vont à hide_in_messages() (common/log.h), que le journal et les erreurs des
    routes appliquent en dernier.  Une réponse autre que le JSON de l'interface (une page web
    qu'envoie un proxy ou un filtre du réseau, un corps tronqué, des valeurs d'autres types)
    échoue de la même façon, tout comme une entrée de la liste sans identifiant ou taille valide.

    Chaque requête passe par http_fetch.h : elle suit le proxy des réglages, est bornée dans le
    temps, et échoue aussitôt quand le service s'arrête ou passe hors ligne.
*/

namespace albireo
{

    // Indique si l'espace est défini : toujours dans le service, dont le lien est une constante du build.
    bool public_link_enabled();
    // Indique si le programme porte un mot de passe pour le lien (intégré depuis le fichier de mot de passe).
    bool public_link_has_password();

    // Pourquoi un fichier de l'espace de publication n'a pas pu être lu.
    enum class link_status
    {
        ok,
        unset,      // aucun espace de publication n'est défini : les tests seulement (public_link_for_tests())
        missing,    // l'espace répond et ne contient pas le fichier : il n'y est pas encore publié
        failed      // l'espace ne répond pas, refuse la requête, répond autrement que par son interface,
                    // ou ne donne aucune adresse pour le fichier
    };

    /*!
        requires
            - path est relatif à l'espace de publication : "dist/feed.json",
              "models/<identifier>/<file name>".
        ensures
            - Donne dans url l'adresse du fichier sur un serveur de contenu, et dans size sa
              taille, d'après une liste de l'espace lue au plus 10 minutes avant et une adresse
              obtenue au plus 30 minutes avant, sauf si fresh redemande les deux.  url ne va
              qu'à http_fetch.h, et jamais dans un message.
            - Renvoie false, avec error et status (quand il est fourni), quand l'espace ne
              répond pas ou ne contient pas ce fichier ; error nomme alors le fichier dans
              l'espace.
    !*/
    bool public_link_file(const std::string& path, std::string& url, uint64_t& size, std::string& error,
        bool fresh = false, link_status* status = nullptr);

    // Lit un fichier de l'espace d'au plus max_bytes, un flux, un catalogue ou leurs signatures.
    bool public_link_get(const std::string& path, std::string& body, std::string& error, size_t max_bytes,
        link_status* status = nullptr);

    // Le message d'un fichier que l'espace ne contient pas :
    // "the publication space of Albireo does not hold <path> yet".
    std::string not_published(const std::string& path);

    // Le chemin dans l'espace d'un fichier du modèle id publié à url : models/<id>/<file name of url>.
    std::string public_link_model_path(const std::string& id, const std::string& url);

    // Le message, sans la partie requête de chaque adresse qu'il cite.
    std::string without_queries(const std::string& message);

    /*!
        Réservé aux tests (tests/test_host.cpp), qui servent l'interface de pCloud depuis un
        serveur local.  Aucun réglage ni aucune route n'y mène.
        ensures
            - Utilise link, "http://127.0.0.1:<port>/publink/show?code=<code>", dont l'interface
              est cette origine, à la place du lien du build, et oublie la liste et les adresses
              lues auparavant ; un lien vide ou invalide ne laisse aucun espace défini
              (public_link_enabled() est false).  Le code va à hide_in_messages().
    !*/
    void public_link_for_tests(const std::string& link);

}

#endif // ALBIREO_HOST_PUBLIC_LINK_H_
