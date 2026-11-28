// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Fixe la version de Pseudalis aux endroits qui la portent : common/version.h (ses trois numéros et son texte),
    project() de CMakeLists.txt et la version de web/openapi.json ; puis vérifie que web/changelog.md a une section
    pour elle.  Le fork numérote Pseudalis à part de l'original à partir de 2.0.0 (2026-11-15).  Remplace
    tools/bump_version.py depuis Pseudalis 3.4.0 (décision de l'humain du 28 novembre 2026 : tout en C++), sur le
    modèle de celui de Gulliver.

        build/macos-pseudalis/tools/bump_version patch    # 2.0.0 -> 2.0.1 : une correction, une retouche de texte
        build/macos-pseudalis/tools/bump_version minor    # 2.0.1 -> 2.1.0 : une nouvelle fonction, un nouveau réglage
        build/macos-pseudalis/tools/bump_version major    # 2.1.0 -> 3.0.0 : un changement qui casse ce qui marchait
        build/macos-pseudalis/tools/bump_version 2.0.0    # une version explicite

    Le dossier de Pseudalis est celui des sources du build (PSEUDALIS_SOURCE_DIR), ou le second argument.
*/

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    bool read(const std::string& path, std::string& text)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream s;
        s << f.rdbuf();
        text = s.str();
        return true;
    }

    bool write(const std::string& path, const std::string& text)
    {
        // Écrit à côté puis renomme : un fichier n'est jamais laissé à moitié écrit.
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!(f << text)) return false;
        }
        return std::rename(tmp.c_str(), path.c_str()) == 0;
    }

    int fail(const std::string& what)
    {
        std::fprintf(stderr, "bump_version: %s\n", what.c_str());
        return 1;
    }
}

int main(int argc, char** argv)
{
#ifdef PSEUDALIS_SOURCE_DIR
    std::string root = PSEUDALIS_SOURCE_DIR;
#else
    std::string root = ".";
#endif
    if (argc == 3) root = argv[2];
    const std::string version_h = root + "/common/version.h", cmake = root + "/CMakeLists.txt",
        openapi = root + "/web/openapi.json", changelog = root + "/web/changelog.md";

    std::string header;
    if (!read(version_h, header)) return fail("cannot read " + version_h);
    int current[3];
    const char* const parts[3] = {"MAJOR", "MINOR", "PATCH"};
    for (int i = 0; i < 3; ++i)
    {
        std::smatch m;
        if (!std::regex_search(header, m, std::regex(std::string("#define PSEUDALIS_VERSION_") + parts[i] + " +([0-9]+)")))
            return fail(std::string("no PSEUDALIS_VERSION_") + parts[i] + " in " + version_h);
        current[i] = std::stoi(m[1]);
    }
    char now[64];
    std::snprintf(now, sizeof(now), "%d.%d.%d", current[0], current[1], current[2]);
    if (argc < 2 || argc > 3)
    {
        std::fprintf(stderr, "usage: bump_version major|minor|patch|X.Y.Z [folder of Pseudalis] (now %s)\n", now);
        return 2;
    }

    const std::string arg = argv[1];
    int next[3] = {current[0], current[1], current[2]};
    std::smatch explicit_version;
    if (arg == "major") { ++next[0]; next[1] = 0; next[2] = 0; }
    else if (arg == "minor") { ++next[1]; next[2] = 0; }
    else if (arg == "patch") ++next[2];
    else if (std::regex_match(arg, explicit_version, std::regex("([0-9]+)\\.([0-9]+)\\.([0-9]+)")))
        for (int i = 0; i < 3; ++i) next[i] = std::stoi(explicit_version[i + 1]);
    else
    {
        std::fprintf(stderr, "usage: bump_version major|minor|patch|X.Y.Z [folder of Pseudalis]\n");
        return 2;
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%d.%d.%d", next[0], next[1], next[2]);

    // common/version.h : les trois numéros et le texte.
    for (int i = 0; i < 3; ++i)
        header = std::regex_replace(header, std::regex(std::string("(#define PSEUDALIS_VERSION_") + parts[i] + " +)[0-9]+"),
            "$01" + std::to_string(next[i]));
    header = std::regex_replace(header, std::regex("(#define PSEUDALIS_VERSION +\")[0-9.]+(\")"),
        std::string("$01") + text + "$2");

    // CMakeLists.txt : la première ligne project(pseudalis VERSION …).
    std::string lists;
    if (!read(cmake, lists)) return fail("cannot read " + cmake);
    lists = std::regex_replace(lists, std::regex("(project\\(pseudalis VERSION )[0-9.]+"), std::string("$01") + text,
        std::regex_constants::format_first_only);

    // web/openapi.json : la version de "info", la première "version" après "info".
    std::string doc;
    if (!read(openapi, doc)) return fail("cannot read " + openapi);
    const size_t info = doc.find("\"info\"");
    const size_t key = info == std::string::npos ? info : doc.find("\"version\"", info);
    const size_t open = key == std::string::npos ? key : doc.find('"', doc.find(':', key));
    const size_t close = open == std::string::npos ? open : doc.find('"', open + 1);
    if (close == std::string::npos || doc.substr(open + 1, close - open - 1) != now)
        return fail("web/openapi.json does not give the version " + std::string(now) + " in \"info\"");
    doc.replace(open + 1, close - open - 1, text);

    if (!write(version_h, header) || !write(cmake, lists) || !write(openapi, doc))
        return fail("cannot write the version");
    std::printf("Pseudalis %s -> %s\n", now, text);

    std::string notes;
    const std::regex section(std::string("(^|\\n)## \\[?") + std::regex_replace(text, std::regex("\\."), "\\.") + "\\]?");
    if (!read(changelog, notes) || !std::regex_search(notes, section))
        std::printf("web/changelog.md has no section for %s yet: add it.\n", text);
    return 0;
}
