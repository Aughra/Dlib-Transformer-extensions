// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_IMAGE_INPUT_H_
#define ALBIREO_ENGINE_IMAGE_INPUT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace albireo
{

    /*!
        Les images des requêtes, de la data URL d'un message aux pixels que lit un encodeur
        d'images.

        Une requête porte une image sous forme de data URL ("data:image/png;base64,...") : le
        service ne lit rien sur le réseau ni sur le disque pour le compte d'une requête, donc
        toute autre adresse est refusée.  Les octets sont un fichier PNG ou JPEG, reconnu à
        sa signature, et décodé par les copies de libpng et libjpeg fournies avec dlib.  La
        taille d'une image est lue dans son en-tête avant tout décodage, de sorte que les
        limites d'image_limits refusent une image avant que ses pixels n'occupent de mémoire.

        L'image décodée est faite de pixels RGB sur 8 bits, comme la produit le
        prétraitement de référence des modèles de vision : les pixels transparents sont
        composés sur du blanc, un PNG 16 bits garde l'octet de poids fort de ses
        échantillons, et l'orientation qu'un JPEG ou un PNG déclare dans ses données EXIF
        est appliquée, comme les navigateurs affichent l'image.

        Chaque fonction lève std::invalid_argument, avec un message pour l'utilisateur, quand
        l'image est refusée ou ne peut pas être lue.
    !*/

    struct image_limits
    {
        long max_side = 8192;                 // pixels sur le plus grand côté
        long max_pixels = 40000000;           // pixels au total
        size_t max_bytes = 20u << 20;         // octets du fichier PNG ou JPEG
    };

    enum class image_format { png, jpeg };

    struct image_header
    {
        image_format format = image_format::png;
        long width = 0;                       // tel qu'affiché, l'orientation appliquée
        long height = 0;
        int orientation = 1;                  // orientation EXIF, de 1 à 8
    };

    struct rgb_image
    {
        long width = 0;
        long height = 0;
        std::vector<uint8_t> pixels;          // height lignes de width triplets RGB

        void clear()
        {
            width = height = 0;
            std::vector<uint8_t>().swap(pixels);
        }
    };

    /*!
        ensures
            - Renvoie les octets de la data URL base64 url, dont le type de média doit être
              une image.
            - Refuse une adresse http, https ou file, une data URL qui n'est pas en base64, et
              plus de max_bytes octets, avant de les décoder.
    !*/
    std::string decode_image_data_url(const std::string& url, size_t max_bytes);

    /*!
        ensures
            - Renvoie le format et la taille affichée du fichier PNG ou JPEG contenu dans
              bytes, lus dans son en-tête, et refuse un fichier qui dépasse limits.
    !*/
    image_header read_image_header(const std::string& bytes, const image_limits& limits);

    /*!
        ensures
            - Décode en pixels RGB le fichier PNG ou JPEG contenu dans bytes, dans les
              limites de limits.
            - Quand fit_width et fit_height sont positifs, l'image est redimensionnée à ces
              valeurs avec resize_bicubic() avant que son orientation soit appliquée, de sorte
              qu'une grande image n'existe jamais en pleine taille une fois décodée ;
              fit_width et fit_height sont alors la taille de l'image telle qu'affichée.
    !*/
    rgb_image decode_image(const std::string& bytes, const image_limits& limits, long fit_width = 0,
        long fit_height = 0);

    /*!
        ensures
            - Renvoie dans out_width et out_height la taille à laquelle une image de
              width x height est redimensionnée avant qu'un encodeur la lise : deux multiples
              de factor, entre min_pixels et max_pixels pixels quand le rapport d'aspect le
              permet, le rapport d'aspect étant conservé au plus près.  C'est la règle
              smart_resize du prétraitement de référence de Qwen2-VL et Qwen3-VL, avec son
              arrondi des moitiés au pair.
            - Refuse une image dont les côtés diffèrent d'un rapport supérieur à 200.
    !*/
    void fit_image_size(long width, long height, long factor, long min_pixels, long max_pixels, long& out_width,
        long& out_height);

    /*!
        ensures
            - Renvoie src redimensionnée à width x height avec le filtre bicubique de Pillow
              (a = -0.5, son support élargi par le facteur d'échelle quand l'image rétrécit,
              poids en virgule fixe avec 22 bits fractionnaires, la passe horizontale
              d'abord, chaque passe arrondie à 8 bits), qu'utilise le prétraitement de
              référence ; une image de même taille est renvoyée telle quelle.
    !*/
    rgb_image resize_bicubic(const rgb_image& src, long width, long height);

    // La même chose avec le filtre bilinéaire de Pillow (un triangle de support 1, élargi par
    // le facteur d'échelle quand l'image rétrécit), qu'utilise le prétraitement des modèles
    // LLaVA-NeXT.
    rgb_image resize_bilinear(const rgb_image& src, long width, long height);

    // resize_bicubic() ou resize_bilinear().
    rgb_image resize_pillow(const rgb_image& src, long width, long height, bool bilinear);

    // L'empreinte SHA-256 de n octets.
    std::array<uint8_t, 32> sha256(const uint8_t* data, size_t n);

    // L'empreinte des pixels d'une image et de sa taille, la clé de ses encodages.
    std::array<uint8_t, 32> image_digest(const rgb_image& image);

}

#endif // ALBIREO_ENGINE_IMAGE_INPUT_H_
