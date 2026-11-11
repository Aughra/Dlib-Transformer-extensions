// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_SPEECH_H_
#define ALBIREO_HOST_SPEECH_H_

#include <string>
#include <vector>

namespace albireo
{

    /*!
        La synthèse vocale fournie avec Albireo, qui lit les réponses à voix haute sans réseau
        et sans les voix du système : le synthétiseur SVOX Pico et ses voix de six langues
        (anglais américain et britannique, français, allemand, italien et espagnol), chacune
        faite d'un fichier d'analyse du texte et d'un fichier de génération du signal dans le
        dossier speech de l'installation.  La sortie est un fichier RIFF WAVE d'échantillons
        mono 16 bits à 16 kHz.

        Le service n'exécute jamais lui-même le synthétiseur.  Le programme albireo-speech de
        l'installation (host/speech_worker.cpp) lit le texte sur son entrée standard et écrit
        le fichier WAVE sur sa sortie standard.  Le service le démarre dans le dossier speech,
        sans console, avec un jeton d'intégrité basse et sans privilèges, dans un job borné en
        mémoire (run_process avec process_options::restricted), pour qu'une faute du
        synthétiseur sur un texte n'atteigne jamais le service.  Ce programme contient le
        synthétiseur, les fonctions de host/speech_text.cpp et le runtime du compilateur, rien
        d'autre : il ne charge aucune des bibliothèques des fenêtres de Windows, qu'un
        processus d'intégrité basse démarré par un service ne pourrait pas initialiser.

        speech_voices(), find_voice() et speech_text() servent à la fois le service et ce
        programme (host/speech_text.cpp) ; installed_voices() et speak() servent le service
        (host/speech.cpp).
    !*/

    struct speech_voice
    {
        std::string id;         // "fr-FR"
        std::string language;   // "fr"
        std::string text_file;  // analyse du texte, "fr-FR_ta.bin"
        std::string sound_file; // génération du signal, "fr-FR_nk0_sg.bin"
    };

    // Les voix qu'Albireo connaît, présentes ou non, la première de chaque langue étant celle par défaut.
    const std::vector<speech_voice>& speech_voices();

    /*
        La voix désignée par un identifiant ou une langue, quelle que soit la casse ("fr-FR",
        "fr", "en-gb"), sinon nullptr.
    */
    const speech_voice* find_voice(const std::string& name);

    // Le plus long texte qu'une requête lit, en octets UTF-8.
    const size_t speech_max_bytes = 8192;

    /*
        Le texte que lit le synthétiseur : les lettres des écritures latines, les chiffres, la
        ponctuation usuelle et les sauts de ligne, les guillemets et tirets typographiques sous
        leur forme simple, tout autre caractère devenant une espace (le synthétiseur lit <...>
        comme des commandes, dont certaines désignent des fichiers) ; suites d'espaces
        réduites, mots coupés tous les 64 caractères, au plus max_bytes octets coupés à la
        limite d'un caractère.
    */
    std::string speech_text(const std::string& text, size_t max_bytes);

    // Les voix dont les deux fichiers se trouvent dans dir.
    std::vector<speech_voice> installed_voices(const std::string& dir);

    /*
        Lit text à voix haute à la vitesse speed (0,5 à 2, 1 étant la vitesse de la voix) avec
        program, le programme albireo-speech, démarré dans dir, le dossier speech : renvoie
        true avec le fichier WAVE dans wav, ou false avec la raison dans error.  Au plus deux
        de ces programmes tournent à la fois ; une requête attend son tour jusqu'à 30 secondes.
    */
    bool speak(const std::string& program, const std::string& dir, const speech_voice& voice, double speed,
        const std::string& text, std::string& wav, std::string& error);

}

#endif // ALBIREO_HOST_SPEECH_H_
