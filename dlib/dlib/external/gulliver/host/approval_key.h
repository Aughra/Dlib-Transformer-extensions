// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_APPROVAL_KEY_H_
#define GULLIVER_HOST_APPROVAL_KEY_H_

#include <mutex>
#include <string>

namespace gulliver
{
    class store;

    class approval_key
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La clé avec laquelle l'administrateur valide un modèle hors de tout catalogue signé
                (Gulliver 7.17.0) : une clé P-256 née dans la Secure Enclave du Mac, dont la partie
                privée ne sort jamais de la puce.  La base ne garde que sa forme scellée (l'attribut
                « toid », que seule la Secure Enclave de ce Mac sait rouvrir) et sa clé publique.

                Limite, assumée et écrite dans la documentation : la clé n'est pas liée à un code ni à
                une empreinte digitale (le service tourne sans écran) ; un programme qui tourne sous le
                compte de l'utilisateur et lit la base peut donc la rouvrir et signer.  Elle protège
                contre une copie de la base sur une autre machine et contre un fichier de modèle changé
                après sa validation, pas contre un programme hostile déjà installé sous ce compte.
        !*/
    public:
        explicit approval_key(store& db) : db_(db) {}

        /*!
            ensures
                - Signe message (ECDSA P-256 sur SHA-256, signature DER en base64 dans signature),
                  en créant la clé à la première signature.
                - Renvoie false avec error quand la Secure Enclave refuse (Mac sans puce, session
                  verrouillée) ou que la clé gardée ne se rouvre pas.
        !*/
        bool sign(const std::string& message, std::string& signature, std::string& error);

        // Vrai quand signature signe message avec la clé publique gardée ; faux sans clé.
        bool verify(const std::string& message, const std::string& signature) const;

    private:
        store& db_;
        std::mutex m_;
    };
}

#endif // GULLIVER_HOST_APPROVAL_KEY_H_
