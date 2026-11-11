// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_HOST_KEYS_H_
#define PSEUDALIS_HOST_KEYS_H_

#include <string>

/*
    Les clés de Pseudalis.  Une clé maîtresse de 32 octets aléatoires, créée au premier
    démarrage du service, donne avec HKDF-SHA-256 la clé de la pseudonymisation (les tags des
    valeurs) et les clés du coffre (les originaux et les images, AES-256-CTR et HMAC-SHA-256).

    Sous Windows, la clé maîtresse est gardée dans keys\master.key du dossier de données,
    chiffrée avec DPAPI pour le compte du service (LocalSystem), dans un dossier que seuls
    SYSTEM et les administrateurs ouvrent : une copie du fichier ne sert à aucun autre compte
    ni à aucun autre ordinateur.  Ailleurs, elle est en clair dans un dossier que seul son
    propriétaire ouvre.  Son export, protégé par une phrase secrète (PBKDF2-SHA-256,
    600 000 itérations, AES-256-CTR et HMAC-SHA-256), est le moyen d'en garder une copie ou de
    déplacer l'installation vers un autre ordinateur.
*/

namespace pseudalis
{
    struct key_set
    {
        std::string pseudonymization;       // 32 octets
        std::string vault_encryption;       // 32 octets
        std::string vault_authentication;   // 32 octets

        key_set() = default;
        key_set(const key_set&) = default;
        key_set& operator=(const key_set&) = default;
        // Les clés ne laissent aucune copie en mémoire une fois qu'elles ne servent plus.
        ~key_set();
    };

    key_set derive_keys(const std::string& master);

    /*!
        ensures
            - Lit la clé maîtresse de keys_dir, ou la crée quand le dossier n'en contient
              aucune (created vaut alors true).  Renvoie false avec error quand la clé ne
              peut être ni lue (autre compte, autre ordinateur, fichier endommagé) ni écrite.
    !*/
    bool load_master_key(const std::string& keys_dir, std::string& master, bool& created, std::string& error);
    bool save_master_key(const std::string& keys_dir, const std::string& master, std::string& error);

    // L'export de la clé maîtresse : un document JSON protégé par phrase secrète (12 caractères au moins).
    std::string export_master_key(const std::string& master, const std::string& passphrase);
    bool import_master_key(const std::string& exported, const std::string& passphrase, std::string& master,
        std::string& error);

    /*!
        ensures
            - Fait de path un dossier que seuls SYSTEM et les administrateurs ouvrent, sous
              Windows, et seul son propriétaire ailleurs : créé ainsi s'il manque, ses
              permissions remplacées sinon.
    !*/
    bool private_folder(const std::string& path, std::string& error);

    // Efface un secret, puis le vide.
    void wipe(std::string& s);

    // AES-256-CTR avec un bloc compteur iv de 16 octets, pour le chiffrement comme pour le déchiffrement.
    std::string aes256_ctr(const std::string& key, const std::string& iv, const std::string& data);
    // AES-256-CTR de in vers out, size octets, iv de 16 octets ; in et out peuvent être identiques.
    void aes256_ctr_into(const std::string& key, const char* iv, const char* in, char* out, size_t size);
    std::string hmac_sha256(const std::string& key, const std::string& data);
}

#endif // PSEUDALIS_HOST_KEYS_H_
