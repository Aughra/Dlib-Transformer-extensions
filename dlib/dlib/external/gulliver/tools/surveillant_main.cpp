// Copyright (C) 2026  papy cyber
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    Le programme du surveillant du Mac (Gulliver 7.24.0, à la place de tools/surveillant.py) ; le fonctionnement est
    décrit dans tools/surveillant.h.

        build/macos-gulliver/bin/surveillant [GULLIVER_HOME]           # la boucle, un relevé toutes les 10 secondes
        build/macos-gulliver/bin/surveillant [GULLIVER_HOME] --essai   # un relevé, ce qu'il en penserait, rien n'est arrêté

    Il vit dans build/macos-gulliver/bin/ : il est donc lui-même parmi les programmes protégés.
*/

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

#include "surveillant.h"

int main(int argc, char** argv)
{
    std::string home;
    bool trial = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--essai") trial = true;
        else if (a.rfind("--", 0) != 0 && home.empty()) home = a;
    }
    gulliver::essai::surveillant_config c;
    // build/macos-gulliver/bin → la racine du dépôt
    c.root = gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(gulliver::parent_path(gulliver::executable_path()))));
    if (home.empty())
    {
        const char* env = std::getenv("GULLIVER_HOME");
        home = env && *env ? env : gulliver::join_path(c.root, "build/gulliver-home");
    }
    gulliver::essai::surveillant w(home, c);
    if (trial)
    {
        w.trial();
        return 0;
    }
    gulliver::make_dirs(gulliver::join_path(c.root, "build/surveillance"));
    gulliver::essai::say("surveillant lancé : processeur " + gulliver::essai::fmt("%g", c.cpu_threshold) + " %, puce " +
                         gulliver::essai::fmt("%.0f", c.temp_threshold) + " °C, " + gulliver::essai::fmt("%.0f", c.window / 60) +
                         " min, " + gulliver::essai::fmt("%.1f", c.cores) + " cœurs");
    for (;;)
    {
        try
        {
            w.tick();
        }
        catch (const std::exception& e)   // un relevé raté ne doit pas arrêter la surveillance
        {
            gulliver::essai::say(std::string("relevé raté : ") + e.what());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(c.step * 1000)));
    }
}
