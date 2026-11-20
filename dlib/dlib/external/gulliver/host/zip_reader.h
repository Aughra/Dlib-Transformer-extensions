// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
// Repris de dlib (dlib/zip/zip_archive.h, le lecteur seulement) le 2026-11-20 (fil rouge,
// morceau 3).  La décompression (raw deflate) et le CRC-32 sont ceux de la zlib de macOS, à la
// place de dlib/zip/deflate.h et dlib/crc32.h ; l'écrivain de ZIP n'est pas repris.
#ifndef GULLIVER_ZIP_READER_H_
#define GULLIVER_ZIP_READER_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace gulliver
{

// ----------------------------------------------------------------------------------------

    class zip_error : public std::runtime_error
    {
    public:
        explicit zip_error(const std::string& message) : std::runtime_error(message), info(message) {}
        std::string info;
    };

// ----------------------------------------------------------------------------------------

    struct zip_limits
    {
        std::uint64_t max_entry_size = 512ull << 20;
        std::uint64_t max_total_size = 2048ull << 20;
        std::size_t max_entries = 20000;
        std::uint64_t max_ratio = 1100;
    };

    struct zip_entry_info
    {
        std::string name;
        std::uint16_t method = 0;
        std::uint16_t flags = 0;
        std::uint32_t crc32 = 0;
        std::uint64_t compressed_size = 0;
        std::uint64_t uncompressed_size = 0;
        std::uint64_t local_header_offset = 0;
    };

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        inline std::uint32_t zip_crc32(const std::string& data)
        {
            uLong crc = ::crc32(0L, Z_NULL, 0);
            const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
            std::size_t left = data.size();
            while (left > 0)
            {
                const uInt n = static_cast<uInt>(left > (1u << 30) ? (1u << 30) : left);
                crc = ::crc32(crc, p, n);
                p += n;
                left -= n;
            }
            return static_cast<std::uint32_t>(crc);
        }

        /* Décompresse un flux deflate brut (sans en-tête zlib) par la zlib de macOS, en une passe,
           dans un tampon d'un octet de plus que la taille annoncée : un flux qui en produirait plus
           est refusé, un flux tronqué ou endommagé aussi, et un flux plus court rend moins
           d'octets, que l'appelant compare à la taille annoncée. */
        inline std::string inflate_raw(const std::string& data, std::size_t expected, const std::string& name)
        {
            z_stream z{};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                throw zip_error("zip archive: cannot inflate " + name + ": the inflater cannot start");
            std::string out(expected + 1, '\0');
            z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
            z.avail_in = static_cast<uInt>(data.size());
            z.next_out = reinterpret_cast<Bytef*>(&out[0]);
            z.avail_out = static_cast<uInt>(out.size());
            const int rc = inflate(&z, Z_FINISH);
            const std::size_t written = static_cast<std::size_t>(z.total_out);
            const std::string message = z.msg ? z.msg : "damaged or truncated data";
            inflateEnd(&z);
            if (written > expected)
                throw zip_error("zip archive: cannot inflate " + name + ": more data than announced");
            if (rc != Z_STREAM_END)
                throw zip_error("zip archive: cannot inflate " + name + ": " + message);
            out.resize(written);
            return out;
        }

        inline std::uint64_t zip_get(const std::string& b, std::size_t pos, int bytes)
        {
            if (pos > b.size() || static_cast<std::size_t>(bytes) > b.size() - pos)
                throw zip_error("zip archive: record extends past the end of the archive");
            std::uint64_t v = 0;
            for (int i = bytes - 1; i >= 0; --i)
                v = (v << 8) | static_cast<unsigned char>(b[pos + static_cast<std::size_t>(i)]);
            return v;
        }

        inline void zip_put(std::string& b, std::uint64_t v, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
        }

        // An offset read from the archive, checked against its size before any use.
        inline std::size_t zip_offset(const std::string& b, std::uint64_t v)
        {
            if (v > b.size())
                throw zip_error("zip archive: offset past the end of the archive");
            return static_cast<std::size_t>(v);
        }
    }

// ----------------------------------------------------------------------------------------

    class zip_archive_reader
    {
    public:
        explicit zip_archive_reader(
            const std::string& archive,
            const zip_limits& limits = zip_limits()
        ) : data_(archive), limits_(limits)
        {
            parse();
        }

        const std::vector<zip_entry_info>& entries() const { return entries_; }

        bool contains(const std::string& name) const
        {
            return index_.find(name) != index_.end();
        }

        std::string read(const std::string& name) const
        {
            const auto it = index_.find(name);
            if (it == index_.end())
                throw zip_error("zip archive: no member named " + name);
            return read(it->second);
        }

        std::string read(std::size_t index) const
        {
            using impl::zip_get;
            if (index >= entries_.size())
                throw zip_error("zip archive: member index out of range");
            const zip_entry_info& e = entries_[index];
            const std::size_t lh = impl::zip_offset(data_, e.local_header_offset);
            if (zip_get(data_, lh, 4) != 0x04034b50)
                throw zip_error("zip archive: bad local header signature for " + e.name);
            if (zip_get(data_, lh + 8, 2) != e.method)
                throw zip_error("zip archive: local and central compression methods differ for " + e.name);
            const std::size_t name_len = static_cast<std::size_t>(zip_get(data_, lh + 26, 2));
            const std::size_t extra_len = static_cast<std::size_t>(zip_get(data_, lh + 28, 2));
            if (name_len > data_.size() - lh - 30 || data_.compare(lh + 30, name_len, e.name) != 0)
                throw zip_error("zip archive: local and central names differ for " + e.name);
            const std::size_t start = lh + 30 + name_len + extra_len;
            if (start > data_.size() || e.compressed_size > data_.size() - start)
                throw zip_error("zip archive: member data extends past the end: " + e.name);
            const std::string raw = data_.substr(start, static_cast<std::size_t>(e.compressed_size));

            std::string content;
            if (e.method == 0)
            {
                content = raw;
            }
            else
            {
                content = impl::inflate_raw(raw, static_cast<std::size_t>(e.uncompressed_size), e.name);
            }
            if (content.size() != e.uncompressed_size)
                throw zip_error("zip archive: size mismatch for " + e.name);
            if (impl::zip_crc32(content) != e.crc32)
                throw zip_error("zip archive: CRC-32 mismatch for " + e.name);
            return content;
        }

    private:
        void parse()
        {
            using impl::zip_get;
            if (data_.size() < 22)
                throw zip_error("zip archive: too short to hold an end of central directory record");

            /* The end of central directory record whose comment ends the archive; failing
               that, the last one whose comment fits, since data is sometimes appended. */
            std::size_t eocd = std::string::npos, fallback = std::string::npos;
            const std::size_t lowest = data_.size() > 22 + 65535 ? data_.size() - 22 - 65535 : 0;
            for (std::size_t p = data_.size() - 22 + 1; p-- > lowest;)
            {
                if (zip_get(data_, p, 4) != 0x06054b50)
                    continue;
                const std::size_t end = p + 22 + static_cast<std::size_t>(zip_get(data_, p + 20, 2));
                if (end == data_.size())
                {
                    eocd = p;
                    break;
                }
                if (end < data_.size() && fallback == std::string::npos)
                    fallback = p;
            }
            if (eocd == std::string::npos)
                eocd = fallback;
            if (eocd == std::string::npos)
                throw zip_error("zip archive: end of central directory record not found");

            if (zip_get(data_, eocd + 4, 2) != 0 || zip_get(data_, eocd + 6, 2) != 0)
                throw zip_error("zip archive: multi-disk archives are not supported");
            std::uint64_t count = zip_get(data_, eocd + 10, 2);
            std::uint64_t cd_size = zip_get(data_, eocd + 12, 4);
            std::uint64_t cd_offset = zip_get(data_, eocd + 16, 4);

            if (count == 0xFFFF || cd_size == 0xFFFFFFFFull || cd_offset == 0xFFFFFFFFull)
            {
                if (eocd < 20 || zip_get(data_, eocd - 20, 4) != 0x07064b50)
                    throw zip_error("zip archive: ZIP64 locator missing");
                const std::size_t z64 = impl::zip_offset(data_, zip_get(data_, eocd - 20 + 8, 8));
                if (zip_get(data_, z64, 4) != 0x06064b50)
                    throw zip_error("zip archive: bad ZIP64 end of central directory record");
                count = zip_get(data_, z64 + 32, 8);
                cd_size = zip_get(data_, z64 + 40, 8);
                cd_offset = zip_get(data_, z64 + 48, 8);
            }

            if (count > limits_.max_entries)
                throw zip_error("zip archive: too many members");
            if (cd_offset > data_.size() || cd_size > data_.size() - cd_offset)
                throw zip_error("zip archive: central directory extends past the end");

            std::uint64_t total = 0;
            std::size_t p = static_cast<std::size_t>(cd_offset);
            for (std::uint64_t i = 0; i < count; ++i)
            {
                if (zip_get(data_, p, 4) != 0x02014b50)
                    throw zip_error("zip archive: bad central directory signature");
                zip_entry_info e;
                e.flags = static_cast<std::uint16_t>(zip_get(data_, p + 8, 2));
                e.method = static_cast<std::uint16_t>(zip_get(data_, p + 10, 2));
                e.crc32 = static_cast<std::uint32_t>(zip_get(data_, p + 16, 4));
                e.compressed_size = zip_get(data_, p + 20, 4);
                e.uncompressed_size = zip_get(data_, p + 24, 4);
                const std::size_t name_len = static_cast<std::size_t>(zip_get(data_, p + 28, 2));
                const std::size_t extra_len = static_cast<std::size_t>(zip_get(data_, p + 30, 2));
                const std::size_t comment_len = static_cast<std::size_t>(zip_get(data_, p + 32, 2));
                const std::uint64_t disk = zip_get(data_, p + 34, 2);
                e.local_header_offset = zip_get(data_, p + 42, 4);
                if (p + 46 + name_len + extra_len + comment_len > data_.size())
                    throw zip_error("zip archive: central directory record truncated");
                e.name = data_.substr(p + 46, name_len);

                // ZIP64 extended information: present fields replace the saturated ones.
                std::size_t x = p + 46 + name_len;
                const std::size_t x_end = x + extra_len;
                while (x + 4 <= x_end)
                {
                    const std::uint64_t id = zip_get(data_, x, 2);
                    const std::size_t len = static_cast<std::size_t>(zip_get(data_, x + 2, 2));
                    if (x + 4 + len > x_end) break;
                    if (id == 0x0001)
                    {
                        std::size_t f = x + 4;
                        if (e.uncompressed_size == 0xFFFFFFFFull && f + 8 <= x + 4 + len)
                        {
                            e.uncompressed_size = zip_get(data_, f, 8);
                            f += 8;
                        }
                        if (e.compressed_size == 0xFFFFFFFFull && f + 8 <= x + 4 + len)
                        {
                            e.compressed_size = zip_get(data_, f, 8);
                            f += 8;
                        }
                        if (e.local_header_offset == 0xFFFFFFFFull && f + 8 <= x + 4 + len)
                        {
                            e.local_header_offset = zip_get(data_, f, 8);
                            f += 8;
                        }
                    }
                    x += 4 + len;
                }

                if (disk != 0 && disk != 0xFFFF)
                    throw zip_error("zip archive: multi-disk archives are not supported");
                if (e.flags & 0x0001)
                    throw zip_error("zip archive: encrypted member " + e.name);
                if (e.method != 0 && e.method != 8)
                    throw zip_error("zip archive: unsupported compression method for " + e.name);
                if (e.uncompressed_size > limits_.max_entry_size)
                    throw zip_error("zip archive: member too large: " + e.name);
                if (e.compressed_size > data_.size() || e.local_header_offset > data_.size())
                    throw zip_error("zip archive: member data extends past the end: " + e.name);
                // uncompressed > max_ratio * (compressed + 1), without overflow.
                if (e.method == 8 && e.uncompressed_size > 0 &&
                    (e.uncompressed_size - 1) / (e.compressed_size + 1) >= limits_.max_ratio)
                    throw zip_error("zip archive: implausible compression ratio for " + e.name);
                if (e.method == 0 && e.compressed_size != e.uncompressed_size)
                    throw zip_error("zip archive: stored member with inconsistent sizes: " + e.name);
                total += e.uncompressed_size;
                if (total > limits_.max_total_size)
                    throw zip_error("zip archive: total uncompressed size exceeds the limit");
                if (index_.count(e.name) != 0)
                    throw zip_error("zip archive: duplicate member name " + e.name);

                index_[e.name] = entries_.size();
                entries_.push_back(e);
                p += 46 + name_len + extra_len + comment_len;
            }
        }

        std::string data_;
        zip_limits limits_;
        std::vector<zip_entry_info> entries_;
        std::map<std::string, std::size_t> index_;
    };

// ----------------------------------------------------------------------------------------

}

#endif // GULLIVER_ZIP_READER_H_
