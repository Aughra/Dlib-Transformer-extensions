// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "carnet.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#include "../common/platform.h"

namespace gulliver
{
    namespace
    {
        const char* const sommaire_path = "macos/site-docs/sommaire.json";
        const uint64_t max_file_bytes = 64u << 20;

        bool ends_with(const std::string& s, const std::string& end)
        {
            return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
        }

        json read_sommaire(const std::string& root)
        {
            std::string text;
            json j;
            if (root.empty() || !read_file_link_free(join_path(root, sommaire_path), root, text, 1u << 20)
                || !json::parse(text, j) || !j.is_object())
                return json();
            return j;
        }

        // Un chemin relatif sans composant vide, caché ni « .. », sans barre oblique inverse.
        bool clean_path(const std::string& rel)
        {
            if (rel.empty() || rel.size() > 300 || rel[0] == '/' || rel.find('\\') != std::string::npos) return false;
            size_t from = 0;
            while (from <= rel.size())
            {
                const size_t end = std::min(rel.find('/', from), rel.size());
                const std::string part = rel.substr(from, end - from);
                if (part.empty() || part[0] == '.') return false;
                for (char c : part)
                    if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
                from = end + 1;
            }
            return true;
        }

        // Si le nom name répond au motif d'un nom : « * », « *.extension » ou un nom exact.
        bool name_matches(const std::string& pattern, const std::string& name)
        {
            if (pattern == "*") return true;
            if (pattern.size() > 1 && pattern[0] == '*') return ends_with(name, pattern.substr(1));
            return pattern == name;
        }

        // Si la liste des motifs permet rel : son dossier est exactement celui d'un motif, et son nom y répond.
        bool permitted(const json& sommaire, const std::string& rel)
        {
            if (!clean_path(rel)) return false;
            const size_t slash = rel.rfind('/');
            const std::string dir = slash == std::string::npos ? std::string() : rel.substr(0, slash);
            const std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
            for (const json& m : sommaire["motifs"].elements())
            {
                const std::string motif = m.str();
                const size_t s = motif.rfind('/');
                const std::string mdir = s == std::string::npos ? std::string() : motif.substr(0, s);
                const std::string mname = s == std::string::npos ? motif : motif.substr(s + 1);
                if (mdir == dir && name_matches(mname, name)) return true;
            }
            return false;
        }

        std::string type_of(const std::string& name)
        {
            const size_t dot = name.rfind('.');
            std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            static const std::pair<const char*, const char*> types[] = {
                { "md", "text/plain; charset=utf-8" }, { "png", "image/png" }, { "jpg", "image/jpeg" },
                { "jpeg", "image/jpeg" }, { "gif", "image/gif" }, { "webp", "image/webp" }, { "svg", "image/svg+xml" },
                { "pdf", "application/pdf" } };
            for (const auto& t : types)
                if (ext == t.first) return t.second;
            return "application/octet-stream";
        }

        // Le titre d'un document Markdown : sa première ligne « # ».
        std::string title_of(const std::string& root, const std::string& rel)
        {
            std::string text;
            if (!read_file_link_free(join_path(root, rel), root, text, 1u << 20)) return rel;
            size_t from = 0;
            while (from < text.size())
            {
                size_t end = text.find('\n', from);
                if (end == std::string::npos) end = text.size();
                if (text.compare(from, 2, "# ") == 0)
                {
                    std::string t = text.substr(from + 2, end - from - 2);
                    while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back();
                    return t;
                }
                from = end + 1;
            }
            return rel;
        }
    }

    std::string carnet_root()
    {
        std::string root;
        if (const char* r = std::getenv("GULLIVER_FORK_ROOT")) root = r;
#ifdef GULLIVER_FORK_ROOT_DEFAULT
        if (root.empty()) root = GULLIVER_FORK_ROOT_DEFAULT;
#endif
        return !root.empty() && file_exists(join_path(root, sommaire_path)) ? root : std::string();
    }

    json carnet_index()
    {
        json out = json::object();
        const std::string root = carnet_root();
        const json sommaire = read_sommaire(root);
        out["found"] = sommaire.is_object();
        json sections = json::array();
        for (const json& section : sommaire["sections"].elements())
        {
            json entries = json::array();
            for (const json& e : section["entries"].elements())
            {
                const std::string path = e["path"].str();
                if (!permitted(sommaire, path) || !file_exists(join_path(root, path))) continue;
                json entry = json::object();
                entry["path"] = path;
                entry["title"] = e["title"].str();
                entry["desc"] = e["desc"].str();
                entries.push_back(entry);
            }
            json s = json::object();
            s["title"] = section["title"].str();
            s["entries"] = entries;
            sections.push_back(s);
        }
        out["sections"] = sections;
        // Les épisodes d'un blog : ses fichiers NN-titre.md, dans l'ordre, titrés sans leur préfixe commun.
        json blogs = json::array();
        for (const json& b : sommaire["blogs"].elements())
        {
            const std::string dir = b["dir"].str(), strip = b["strip"].str();
            std::vector<std::string> names;
            for (const std::string& n : list_dir(join_path(root, dir)))
                if (n.size() > 6 && std::isdigit(static_cast<unsigned char>(n[0]))
                    && std::isdigit(static_cast<unsigned char>(n[1])) && n[2] == '-' && ends_with(n, ".md")
                    && permitted(sommaire, dir + "/" + n))
                    names.push_back(n);
            std::sort(names.begin(), names.end());
            json episodes = json::array();
            for (const std::string& n : names)
            {
                const std::string path = dir + "/" + n;
                std::string title = title_of(root, path);
                if (!strip.empty() && title.compare(0, strip.size(), strip) == 0)
                {
                    const size_t colon = title.find(" : ", strip.size());
                    if (colon != std::string::npos) title = title.substr(colon + 3);
                }
                json e = json::object();
                e["number"] = std::atoi(n.substr(0, 2).c_str());
                e["path"] = path;
                e["title"] = title;
                episodes.push_back(e);
            }
            json blog = json::object();
            blog["title"] = b["title"].str();
            blog["episodes"] = episodes;
            blogs.push_back(blog);
        }
        out["blogs"] = blogs;
        return out;
    }

    bool carnet_file(const std::string& rel, std::string& data, std::string& type)
    {
        const std::string root = carnet_root();
        const json sommaire = read_sommaire(root);
        if (!sommaire.is_object() || !permitted(sommaire, rel)) return false;
        if (!read_file_link_free(join_path(root, rel), root, data, max_file_bytes)) return false;
        type = type_of(rel);
        return true;
    }
}
