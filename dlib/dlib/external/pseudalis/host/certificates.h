// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_CERTIFICATES_H_
#define PSEUDALIS_HOST_CERTIFICATES_H_

#include <string>

namespace pseudalis
{

    struct certificate_files
    {
        std::string ca_cert;       // PEM de l'autorité locale
        std::string server_cert;   // PEM du certificat du serveur local
        std::string server_key;    // clé privée du serveur : chiffrée sous Windows, PEM ailleurs
        std::string plain_key;     // la clé en clair des versions antérieures à 1.0.8, sous Windows
        std::string ca_key;        // la clé de l'autorité des versions antérieures à 1.0.8
    };

    certificate_files certificate_paths(const std::string& dir);

    /*!
        ensures
            - S'assure que dir contient le certificat d'une autorité de certification locale,
              un certificat du serveur local signé par elle, valable pour localhost,
              127.0.0.1 et ::1, et la clé privée de ce serveur.
            - L'autorité signe le certificat du serveur une seule fois, quand les deux sont
              créés, et sa clé privée est alors effacée de la mémoire : elle n'est jamais
              écrite.  Ses contraintes de noms la limitent à localhost et aux adresses de
              bouclage, et son usage étendu de clé à l'authentification des serveurs.  C'est
              le certificat que reçoivent les racines de confiance de la machine
              (trust_authority()), pour que les navigateurs acceptent le serveur local sans
              avertissement.
            - Les deux certificats sont valables 397 jours.  Quand il en reste moins de 30,
              quand la clé du serveur ne se lit pas ou ne correspond pas à son certificat, ou
              quand dir contient la clé d'une autorité d'une version antérieure à 1.0.8, qui a
              pu être copiée, une nouvelle autorité et un nouveau certificat du serveur les
              remplacent, et renewed passe à true.
            - Sous Windows, la clé privée du serveur est chiffrée avec DPAPI pour le compte
              qui exécute cette fonction, LocalSystem pour le service, dans un fichier que
              seuls SYSTEM et les administrateurs peuvent ouvrir : une copie de ce fichier ne
              sert à aucun autre compte ni à aucun autre ordinateur.  Ailleurs, c'est un
              fichier PEM que seul son propriétaire peut lire.  Une clé laissée en clair par
              une version antérieure est chiffrée à la place de son fichier, et la clé d'une
              autorité antérieure est supprimée.
            - Renvoie false, avec la raison dans error, quand les fichiers ne peuvent pas être
              créés.
    !*/
    bool ensure_certificates(const std::string& dir, bool& renewed, std::string& error);

    /*!
        ensures
            - Lit la clé privée du serveur de dir sous forme PEM, déchiffrée en mémoire pour
              le serveur TLS.  Renvoie false quand elle ne peut être ni lue ni déchiffrée, par
              un autre compte ou sur un autre ordinateur par exemple.
    !*/
    bool read_server_key(const std::string& dir, std::string& pem);

    // Efface les caractères d'un secret, puis le vide.
    void wipe_secret(std::string& s);

    // Supprime les certificats et les clés de dir ; false quand l'un d'eux subsiste.
    bool remove_certificate_files(const std::string& dir);

    // SHA-256 de la forme DER d'un certificat PEM, en hexadécimal minuscule.
    std::string certificate_thumbprint(const std::string& pem_path);

    // Octets DER du premier certificat d'un fichier PEM, vide en cas d'échec.
    std::string certificate_der(const std::string& pem_path);


}

#endif // PSEUDALIS_HOST_CERTIFICATES_H_
