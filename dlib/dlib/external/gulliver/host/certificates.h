// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_CERTIFICATES_H_
#define ALBIREO_HOST_CERTIFICATES_H_

#include <string>

namespace albireo
{

    struct certificate_files
    {
        std::string ca_cert;       // PEM de l'autorité locale
        std::string server_cert;   // PEM du certificat du serveur local
        std::string server_key;    // clé privée du serveur au format PEM
        std::string plain_key;     // ancien chemin de la clé, consulté pendant la migration
        std::string ca_key;        // la clé de l'autorité des versions antérieures à 1.0.8
    };

    certificate_files certificate_paths(const std::string& dir);

    /*!
        ensures
            - S'assure que dir contient le certificat d'une autorité de certification locale,
              un certificat du serveur local signé par elle, valable pour localhost, 127.0.0.1
              et ::1, et la clé privée de ce serveur.
            - L'autorité signe le certificat du serveur une seule fois, quand les deux sont
              créés, et sa clé privée est alors effacée de la mémoire : elle n'est jamais
              écrite.  Ses contraintes de nom la limitent à localhost et aux adresses de
              bouclage, et son usage étendu de clé à l'authentification des serveurs.  C'est
              le certificat que reçoivent les racines de confiance de la machine
              (trust_authority()), pour que les navigateurs acceptent le serveur local sans
              avertissement.
            - Les deux certificats sont valables 397 jours.  Quand il reste moins de 30 jours,
              quand la clé du serveur ne se lit pas ou ne correspond pas à son certificat, ou
              quand dir contient la clé d'une autorité d'une version antérieure à 1.0.8, qui a
              pu être copiée, une nouvelle autorité et un nouveau certificat du serveur les
              remplacent, et renewed passe à true.
            - La clé privée du serveur reste au format PEM dans un fichier créé avec des
              permissions restreintes.  Une ancienne clé d'autorité trouvée sur disque est
              supprimée et entraîne le renouvellement des certificats.
            - Renvoie false, avec la raison dans error, quand les fichiers ne peuvent pas être
              créés.
    !*/
    bool ensure_certificates(const std::string& dir, bool& renewed, std::string& error);

    /*!
        ensures
            - Lit la clé privée du serveur de dir au format PEM pour le serveur TLS.
              Renvoie false quand le fichier ne peut pas être lu.
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

#endif // ALBIREO_HOST_CERTIFICATES_H_
