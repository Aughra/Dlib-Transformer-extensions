// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
/*
    albireo-speech : lit un texte à voix haute avec le synthétiseur SVOX Pico, pour le service.

        albireo-speech <voice> <speed>

    Le programme lit le texte en UTF-8 sur son entrée standard (au plus 16 Kio), en garde ce
    que speech_text() en garde, et écrit un fichier RIFF WAVE d'échantillons mono 16 bits à
    16 kHz sur sa sortie standard.  Les deux fichiers de la voix ("fr-FR", "de-DE" ...) sont
    lus dans le dossier courant, où le service démarre le programme : le dossier speech de
    l'installation.  speed va de 0,5 à 2, 1 étant la vitesse de la voix.  Un échec est signalé
    sur la sortie d'erreur, avec le code de sortie 2 (arguments), 4 (synthèse) ou 5 (sortie).

    Le programme lie le synthétiseur, host/speech_text.cpp et le runtime du compilateur, et
    rien du reste d'Albireo : il ne charge aucune bibliothèque des fenêtres de Windows, pour
    démarrer avec le jeton d'intégrité basse et dans le job que le service lui donne, sans
    console et sans accès à un bureau.
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "speech.h"

extern "C"
{
#include "picoapi.h"
}


namespace
{
    using albireo::speech_voice;

    // Le nombre maximal d'échantillons que donne un texte : dix minutes à 16 kHz.
    const size_t most_samples = size_t(16000) * 600;

    // Le nombre maximal d'octets de l'entrée standard que lit le programme.
    const size_t most_input = albireo::speech_max_bytes * 2;

    void put_u32(std::string& s, uint32_t v)
    {
        for (int b = 0; b < 4; ++b) s += static_cast<char>((v >> (8 * b)) & 0xFF);
    }

    void put_u16(std::string& s, uint16_t v)
    {
        s += static_cast<char>(v & 0xFF);
        s += static_cast<char>((v >> 8) & 0xFF);
    }

    // Un fichier RIFF WAVE d'échantillons mono 16 bits à 16 kHz.
    std::string wave_file(const std::vector<int16_t>& samples)
    {
        const uint32_t data = static_cast<uint32_t>(samples.size() * 2);
        std::string w;
        w.reserve(44 + data);
        w += "RIFF";
        put_u32(w, 36 + data);
        w += "WAVEfmt ";
        put_u32(w, 16);
        put_u16(w, 1);          // PCM
        put_u16(w, 1);          // mono
        put_u32(w, 16000);
        put_u32(w, 32000);      // octets par seconde
        put_u16(w, 2);          // octets par trame
        put_u16(w, 16);         // bits par échantillon
        w += "data";
        put_u32(w, data);
        for (int16_t v : samples) put_u16(w, static_cast<uint16_t>(v));
        return w;
    }

    std::string status_text(pico_System sys, pico_Status s)
    {
        pico_Retstring message;
        std::memset(message, 0, sizeof(message));
        if (sys) pico_getSystemStatusMessage(sys, s, message);
        std::ostringstream o;
        o << "speech synthesis error " << s;
        if (message[0]) o << " (" << reinterpret_cast<const char*>(message) << ")";
        return o.str();
    }

    // Le synthétiseur et sa voix, libérés dans l'ordre inverse de leur création.
    struct pico_session
    {
        std::vector<char> memory;
        pico_System sys = nullptr;
        pico_Resource text = nullptr, sound = nullptr;
        pico_Engine engine = nullptr;
        bool voice = false;

        ~pico_session()
        {
            if (engine) pico_disposeEngine(sys, &engine);
            if (voice) pico_releaseVoiceDefinition(sys, reinterpret_cast<const pico_Char*>("albireo"));
            if (sound) pico_unloadResource(sys, &sound);
            if (text) pico_unloadResource(sys, &text);
            if (sys) pico_terminate(&sys);
        }
    };

    /*
        Synthétise text, que donne speech_text(), avec les fichiers de voice dans le dossier
        courant, à la vitesse speed, dans wav.  Renvoie false avec la raison dans error.
    */
    bool synthesize(const speech_voice& voice, const std::string& text, double speed, std::string& wav,
        std::string& error)
    {
        pico_session p;
        p.memory.resize(size_t(4) << 20);
        pico_Status s = pico_initialize(p.memory.data(), static_cast<pico_Uint32>(p.memory.size()), &p.sys);
        if (s != PICO_OK)
        {
            error = status_text(p.sys, s);
            return false;
        }
        s = pico_loadResource(p.sys, reinterpret_cast<const pico_Char*>(voice.text_file.c_str()), &p.text);
        if (s == PICO_OK)
            s = pico_loadResource(p.sys, reinterpret_cast<const pico_Char*>(voice.sound_file.c_str()), &p.sound);
        if (s != PICO_OK)
        {
            error = "the voice " + voice.id + " cannot be read: " + status_text(p.sys, s);
            return false;
        }
        // Les noms des ressources, dans des chaînes de la taille qu'écrit l'interface.
        pico_Retstring text_name, sound_name;
        std::memset(text_name, 0, sizeof(text_name));
        std::memset(sound_name, 0, sizeof(sound_name));
        const pico_Char* name = reinterpret_cast<const pico_Char*>("albireo");
        s = pico_getResourceName(p.sys, p.text, text_name);
        if (s == PICO_OK) s = pico_getResourceName(p.sys, p.sound, sound_name);
        if (s == PICO_OK) s = pico_createVoiceDefinition(p.sys, name);
        if (s == PICO_OK)
        {
            p.voice = true;
            s = pico_addResourceToVoiceDefinition(p.sys, name, reinterpret_cast<const pico_Char*>(text_name));
        }
        if (s == PICO_OK)
            s = pico_addResourceToVoiceDefinition(p.sys, name, reinterpret_cast<const pico_Char*>(sound_name));
        if (s == PICO_OK) s = pico_newEngine(p.sys, name, &p.engine);
        if (s != PICO_OK)
        {
            error = status_text(p.sys, s);
            return false;
        }

        // La vitesse passe par le balisage du synthétiseur, le seul balisage du texte qu'il lit.
        const double factor = std::isfinite(speed) ? std::min(2.0, std::max(0.5, speed)) : 1.0;
        std::string input = "<speed level=\"" + std::to_string(static_cast<int>(std::lround(factor * 100.0)))
            + "\">" + text + "</speed>";
        input.push_back('\0');

        std::vector<int16_t> samples;
        size_t at = 0;
        int stalls = 0;
        while (at < input.size())
        {
            const size_t left = input.size() - at;
            pico_Int16 sent = 0;
            const pico_Int16 chunk = static_cast<pico_Int16>(std::min<size_t>(left, 4096));
            s = pico_putTextUtf8(p.engine, reinterpret_cast<const pico_Char*>(input.data() + at), chunk, &sent);
            if (s != PICO_OK)
            {
                error = status_text(p.sys, s);
                return false;
            }
            at += static_cast<size_t>(std::max<pico_Int16>(sent, 0));
            pico_Status step;
            do
            {
                int16_t buffer[64];
                pico_Int16 received = 0, type = 0;
                step = pico_getData(p.engine, buffer, static_cast<pico_Int16>(sizeof(buffer)), &received, &type);
                if (step != PICO_STEP_BUSY && step != PICO_STEP_IDLE)
                {
                    error = status_text(p.sys, step);
                    return false;
                }
                if (received > 0 && type != PICO_DATA_PCM_16BIT)
                {
                    error = "the voice gives samples of another format";
                    return false;
                }
                if (received > 0)
                    samples.insert(samples.end(), buffer, buffer + std::min<long>(received / 2, 64));
                if (samples.size() > most_samples)
                {
                    error = "the text is too long to be read at once";
                    return false;
                }
            } while (step == PICO_STEP_BUSY);
            // Le synthétiseur ne prend plus rien seulement quand son entrée est pleine et qu'il est inactif :
            // une erreur.
            stalls = sent > 0 ? 0 : stalls + 1;
            if (stalls > 3)
            {
                error = "the speech synthesis does not take the text";
                return false;
            }
        }
        wav = wave_file(samples);
        return true;
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: albireo-speech <voice> <speed>, the text on the standard input\n");
        return 2;
    }
    const speech_voice* voice = albireo::find_voice(argv[1]);
    if (!voice)
    {
        std::fprintf(stderr, "unknown voice %s\n", argv[1]);
        return 2;
    }
    std::string input;
    char buffer[4096];
    for (;;)
    {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), stdin);
        if (n == 0) break;
        input.append(buffer, std::min(n, most_input - std::min(most_input, input.size())));
        if (input.size() >= most_input) break;
    }
    std::string wav, error;
    if (!synthesize(*voice, albireo::speech_text(input, albireo::speech_max_bytes), std::strtod(argv[2], nullptr),
            wav, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 4;
    }
    const bool written = std::fwrite(wav.data(), 1, wav.size(), stdout) == wav.size();
    return std::fflush(stdout) == 0 && written ? 0 : 5;
}
