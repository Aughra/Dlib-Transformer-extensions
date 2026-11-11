// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_MASKED_SECRET_H_
#define ALBIREO_HOST_MASKED_SECRET_H_

#include <cstddef>
#include <string>

namespace albireo
{

    /*!
        ensures
            - Renvoie le secret que la configuration a masqué en l'intégrant au programme
              (CMakeLists.txt), le mot de passe du lien public : chacun de ses size octets combiné par
              xor avec les bits 16 à 23 de la valeur suivante du générateur
              x = (x * 1103515245 + 12345) mod 2^31 démarré à seed, que la configuration fait tourner
              de la même façon.
    !*/
    inline std::string unmask_secret(const unsigned char* bytes, size_t size, unsigned long long seed)
    {
        std::string s(size, '\0');
        for (size_t i = 0; i < size; ++i)
        {
            seed = (seed * 1103515245ull + 12345ull) % 2147483648ull;
            s[i] = static_cast<char>(bytes[i] ^ static_cast<unsigned char>((seed >> 16) & 255));
        }
        return s;
    }

}

#endif // ALBIREO_HOST_MASKED_SECRET_H_
