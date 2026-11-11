// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_FILE_KINDS_H_
#define PSEUDALIS_COMMON_FILE_KINDS_H_

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

/*
    Les types de fichiers que Pseudalis protège, d'après leurs noms : le moteur vérifie que le
    contenu correspond au type du nom, un document Office, un PDF, un texte, une image ou une
    archive.  Un nom dont l'extension ne donne aucun type est pris pour un texte quand son contenu
    est du texte de bout en bout, ce que le moteur décide avec dlib ; le lecteur et le shell le
    prévoient dès le début du fichier (text_head()), et écartent aussitôt les autres fichiers.  Un
    nom sans extension va au moteur, qui prend son contenu pour ce qu'il est.
*/

namespace pseudalis
{
    enum class name_kind { unsupported, document, picture, archive };

    /*
        L'extension d'un nom en minuscules : ".docx", ".tar.gz", ou une chaîne vide.  Ce qui suit le
        dernier point est une extension quand cela contient de 1 à 16 lettres, chiffres, '-' ou '_' :
        "Lettre à M. Dupont" n'en a pas.
    */
    inline std::string name_extension(const std::string& name)
    {
        const size_t slash = name.find_last_of("/\\");
        std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
        for (char& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (base.size() > 7 && base.compare(base.size() - 7, 7, ".tar.gz") == 0) return ".tar.gz";
        const size_t dot = base.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == base.size() || base.size() - dot - 1 > 16)
            return std::string();
        for (size_t i = dot + 1; i < base.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(base[i]);
            if (c >= 0x80 || (!std::isalnum(c) && c != '-' && c != '_')) return std::string();
        }
        return base.substr(dot);
    }

    // Les familles de documents : un texte (brut, délimité ou balisé), un document Office, un PDF.
    enum class document_family { none, text, office, pdf };

    namespace file_kinds_impl
    {
        const char* const texts[] = {
            ".txt", ".text", ".log", ".md", ".markdown", ".rst", ".csv", ".tsv", ".tab", ".json", ".jsonl", ".ndjson",
            ".xml", ".html", ".htm", ".xhtml", ".svg", ".yaml", ".yml", ".toml", ".ini", ".cfg", ".conf",
            ".properties", ".sql", ".tex", ".srt", ".vtt" };
        const char* const offices[] = {
            ".docx", ".docm", ".dotx", ".dotm", ".xlsx", ".xlsm", ".xltx", ".xltm", ".pptx", ".pptm", ".potx", ".potm",
            ".ppsx", ".ppsm" };
        const char* const pictures[] = {
            ".png", ".jpg", ".jpeg", ".jpe", ".jfif", ".gif", ".bmp", ".tif", ".tiff", ".webp" };
        const char* const archives[] = { ".zip", ".tar", ".tgz", ".tar.gz" };
    }

    inline document_family document_family_of_name(const std::string& name)
    {
        const std::string ext = name_extension(name);
        if (ext == ".pdf") return document_family::pdf;
        for (const char* e : file_kinds_impl::texts)
            if (ext == e) return document_family::text;
        for (const char* e : file_kinds_impl::offices)
            if (ext == e) return document_family::office;
        return document_family::none;
    }

    inline name_kind kind_of_name(const std::string& name)
    {
        if (document_family_of_name(name) != document_family::none) return name_kind::document;
        const std::string ext = name_extension(name);
        for (const char* e : file_kinds_impl::pictures)
            if (ext == e) return name_kind::picture;
        for (const char* e : file_kinds_impl::archives)
            if (ext == e) return name_kind::archive;
        return name_kind::unsupported;
    }

    // Les extensions des noms que Pseudalis protège, ".tar.gz" compris.
    inline std::vector<std::string> supported_extensions()
    {
        std::vector<std::string> all(1, ".pdf");
        for (const char* e : file_kinds_impl::texts) all.push_back(e);
        for (const char* e : file_kinds_impl::offices) all.push_back(e);
        for (const char* e : file_kinds_impl::pictures) all.push_back(e);
        for (const char* e : file_kinds_impl::archives) all.push_back(e);
        return all;
    }

    inline bool supported_name(const std::string& name)
    {
        return kind_of_name(name) != name_kind::unsupported;
    }

    /*
        Indique si le début d'un fichier peut être le début d'un texte : de l'UTF-16, avec sa marque
        d'ordre des octets ou ses octets nuls, dont les unités de code ne contiennent aucun caractère
        de contrôle hormis la tabulation, les fins de ligne et le saut de page, ni U+FFFE ou U+FFFF,
        ni demi-code (surrogate) isolé, ou bien des octets sans un tel caractère de contrôle, ce que
        le moteur exige aussi sur tout le contenu.  Le lecteur et le shell écartent aussitôt un
        fichier d'extension sans type dont le début n'est pas un texte, sans en lire la suite.
    */
    inline bool text_head(const char* data, size_t size)
    {
        const unsigned char* d = reinterpret_cast<const unsigned char*>(data);
        int order = 0;      // 1 : UTF-16 petit-boutiste, 2 : gros-boutiste
        const bool bom = size >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF));
        const size_t at = bom ? 2 : 0;
        if (bom)
        {
            order = d[0] == 0xFF ? 1 : 2;
        }
        else
        {
            const size_t pairs = std::min<size_t>(size, 4096) / 2;
            size_t zero_even = 0, zero_odd = 0;
            for (size_t i = 0; i < pairs; ++i)
            {
                if (d[2 * i] == 0) ++zero_even;
                if (d[2 * i + 1] == 0) ++zero_odd;
            }
            if (pairs >= 4 && 10 * zero_odd >= 4 * pairs && 20 * zero_even < pairs) order = 1;
            else if (pairs >= 4 && 10 * zero_even >= 4 * pairs && 20 * zero_odd < pairs) order = 2;
        }
        if (order != 0)
        {
            bool high = false;
            for (size_t i = at; i + 1 < size; i += 2)
            {
                const unsigned u = order == 1 ? d[i] | (static_cast<unsigned>(d[i + 1]) << 8)
                                              : (static_cast<unsigned>(d[i]) << 8) | d[i + 1];
                const bool low = u >= 0xDC00 && u <= 0xDFFF;
                if (high != low) return false;
                high = u >= 0xD800 && u <= 0xDBFF;
                if (low || high) continue;
                if (u < 0x20 ? u != '\t' && u != '\n' && u != '\r' && u != '\f'
                             : u == 0x7F || (u >= 0x80 && u < 0xA0) || u == 0xFFFE || u == 0xFFFF)
                    return false;
            }
            return true;
        }
        for (size_t i = 0; i < size; ++i)
        {
            const unsigned char c = d[i];
            if (c < 0x20 ? c != '\t' && c != '\n' && c != '\r' && c != '\f' : c == 0x7F) return false;
        }
        return true;
    }
}

#endif // PSEUDALIS_COMMON_FILE_KINDS_H_
