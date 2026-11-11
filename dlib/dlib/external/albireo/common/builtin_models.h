// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_COMMON_BUILTIN_MODELS_H_
#define ALBIREO_COMMON_BUILTIN_MODELS_H_

#include <cstdint>
#include <cstring>
#include <string>

namespace albireo
{

    /*!
        Les modèles fournis avec Albireo : un modèle de langage qui sert les trois fonctions
        de l'application web et le modèle d'embeddings de la recherche dans les documents,
        préparés par le build (tools/albireo_builtin.cpp, à partir de resources/builtin_models.json)
        et nommés avec le suffixe "-albireo".

        Dans une installation, ils se trouvent dans le dossier "models" des programmes, avec leur
        manifeste builtin.json, que le service lit à chaque démarrage : builtin_manifest_name
        dans builtin_models_folder.  Le manifeste est {"format": 1, "version", "models": [...]},
        chaque modèle avec les champs d'une entrée du catalogue et "file", "size" et "sha256" ; un
        modèle qui lit des images a un "projector" avec ses propres "file", "size" et "sha256", le
        fichier de son encodeur d'images.

        L'installateur complet les porte après les sections de son programme, sous forme d'une
        surcouche (overlay) que Windows ne mappe pas :

            les fichiers des modèles, l'un après l'autre, chacun suivi de son projecteur
            le manifeste, chaque modèle et chaque projecteur avec "offset", la position de son
                fichier depuis le début de la surcouche
            des zéros, pour que la surcouche se termine sur un multiple de 8 octets
            builtin_trailer

        La surcouche se termine à la fin du fichier, ou à la table des certificats d'un
        installateur signé, qui suit la surcouche et la couvre ; le lecteur saute jusqu'à 7 octets
        de zéros qu'un outil de signature peut ajouter avant la table.

        Windows n'exécute aucun programme de 4 Gio ou plus (max_installer_size) : quand le programme
        et ses modèles atteindraient cette taille, la surcouche se trouve dans un fichier à part à
        côté de l'installateur, nommé d'après lui avec l'extension builtin_overlay_extension
        (albireo-setup.exe, albireo-setup.models), la surcouche seule dès son premier octet, et
        l'installateur est le programme sans modèles.  Son manifeste nomme la version de
        l'installateur auquel il est associé ; l'installateur le lit quand il ne porte pas de
        surcouche, et les deux fichiers voyagent ensemble.
    !*/

    const char* const builtin_models_folder = "models";
    const char* const builtin_manifest_name = "builtin.json";
    const char* const builtin_overlay_extension = ".models";
    // Le plus gros programme que Windows exécute : 4 Gio moins un octet.
    const uint64_t max_installer_size = 0xFFFFFFFFull;

    struct builtin_trailer
    {
        char magic[16];               // builtin_magic, sans son zéro terminal
        uint64_t manifest_offset = 0; // depuis le début de la surcouche
        uint64_t manifest_size = 0;
        uint64_t overlay_size = 0;    // toute la surcouche, trailer compris
        uint64_t reserved = 0;

        builtin_trailer() { std::memcpy(magic, "ALBIREO-MODELS-1", 16); }
        bool valid() const { return std::memcmp(magic, "ALBIREO-MODELS-1", 16) == 0; }
    };

    static_assert(sizeof(builtin_trailer) == 48, "the trailer of the overlay is 48 bytes");

    /*!
        requires
            - read(position, data, size) lit size octets du fichier à la position position, et
              renvoie true quand il le peut.
        ensures
            - Trouve la surcouche qui se termine à end, ou jusqu'à 7 octets de zéros avant, et
              commence à lower ou après : start reçoit sa position, trailer son trailer et manifest
              le texte de son manifeste.
            - Renvoie false quand aucun trailer valide ne se termine là, ou quand ses tailles ne
              tiennent pas.
    !*/
    template <typename reader>
    bool find_builtin_overlay(const reader& read, uint64_t lower, uint64_t end, uint64_t& start,
        builtin_trailer& trailer, std::string& manifest)
    {
        for (uint64_t pad = 0; pad < 8; ++pad)
        {
            if (end < lower + sizeof(builtin_trailer) + pad) return false;
            const uint64_t tail = end - pad;
            builtin_trailer t;
            if (!read(tail - sizeof(t), &t, sizeof(t)) || !t.valid()) continue;
            if (t.overlay_size > tail - lower || t.overlay_size < sizeof(t) || t.manifest_size > (16u << 20)
                || t.manifest_offset + t.manifest_size > t.overlay_size - sizeof(t))
                return false;
            start = tail - t.overlay_size;
            trailer = t;
            manifest.assign(static_cast<size_t>(t.manifest_size), '\0');
            return manifest.empty() || read(start + t.manifest_offset, &manifest[0], manifest.size());
        }
        return false;
    }

}

#endif // ALBIREO_COMMON_BUILTIN_MODELS_H_
