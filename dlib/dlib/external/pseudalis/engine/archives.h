// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_ENGINE_ARCHIVES_H_
#define PSEUDALIS_ENGINE_ARCHIVES_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

/*
    Les archives que le moteur ouvre : ZIP, TAR et TAR compressé avec gzip.  Elles ne sont lues
    et écrites qu'en mémoire, pour que le contenu d'une archive n'atteigne jamais un disque en
    clair.
*/

namespace pseudalis
{
    enum class archive_kind { none, zip, tar, tar_gz };

    struct archive_error : public std::runtime_error
    {
        explicit archive_error(const std::string& what) : std::runtime_error(what) {}
    };

    struct archive_entry
    {
        std::string name;               // séparé par des '/', tel que l'archive le nomme
        std::string data;
        bool directory = false;
        bool regular = true;            // false pour les liens et les périphériques d'un TAR
        uint32_t mode = 0644;           // TAR
        int64_t mtime = 0;              // secondes depuis 1970, TAR
    };

    struct archive_limits
    {
        uint64_t max_entry_size = 512ull << 20;
        uint64_t max_total_size = 2048ull << 20;
        size_t max_entries = 20000;
    };

    // Le type d'une archive d'après son contenu : les signatures de ZIP, gzip et TAR, sinon
    // none.
    archive_kind archive_kind_of(const std::string& bytes);
    // Le type que donne son nom : .zip, .tar, .tar.gz ou .tgz, sinon none.
    archive_kind archive_kind_of_name(const std::string& name);
    const char* archive_kind_name(archive_kind k);

    /*!
        ensures
            - Renvoie les entrées de l'archive, dans leur ordre.  Une entrée TAR nommée par un
              nom long GNU ou un en-tête PAX prend ce nom ; ses en-têtes PAX et GNU ne sont pas
              renvoyés.  Les entrées d'un ZIP dont le nom finit par '/' sont des répertoires.
        throws
            - archive_error quand l'archive est endommagée, chiffrée, ou dépasse les limites.
    !*/
    std::vector<archive_entry> read_archive(archive_kind k, const std::string& bytes,
        const archive_limits& limits = archive_limits());

    /*!
        ensures
            - Renvoie une archive du type k contenant les entrées dont regular vaut true,
              dans leur ordre : les noms plus longs que ce que permet l'en-tête TAR reçoivent
              un en-tête PAX.
    !*/
    std::string write_archive(archive_kind k, const std::vector<archive_entry>& entries);

    // Membres gzip, concaténés ou non ; la décompression lève archive_error au-delà de
    // max_size.
    std::string gzip_compress(const std::string& data);
    std::string gzip_decompress(const std::string& data, uint64_t max_size);
}

#endif // PSEUDALIS_ENGINE_ARCHIVES_H_
