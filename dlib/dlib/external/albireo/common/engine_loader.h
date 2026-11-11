// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_COMMON_ENGINE_LOADER_H_
#define ALBIREO_COMMON_ENGINE_LOADER_H_

#include <string>
#include <vector>

#include "engine_api.h"
#include "json.h"

namespace albireo
{

    class engine_library
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Une bibliothèque de moteur d'inférence chargée à l'exécution, avec les
                fonctions de engine_api.h résolues.  L'hôte en garde une pour le moteur qu'il
                a choisi ; la bibliothèque est libérée quand l'objet est détruit, après le
                moteur créé à partir d'elle.
        !*/
    public:
        engine_library() {}
        ~engine_library();
        engine_library(const engine_library&) = delete;
        engine_library& operator=(const engine_library&) = delete;

        /*
            Charge la bibliothèque située à path (UTF-8) ; false avec la raison dans error
            sinon.  Sous Windows, ses dépendances sont d'abord cherchées dans son dossier.
            Quand elle importe le shim de cuDNN (cudnn64_*.dll), les parties de cuDNN placées à
            côté (cudnn_*64_*.dll), que le shim charge par leur nom, sont chargées aussitôt par
            leur chemin complet : ces noms mènent alors à ces fichiers, quelles que soient les
            copies que contiennent le système ou le PATH.
        */
        bool load(const std::string& path, std::string& error);
        void unload();

        /*
            Garde la bibliothèque dans le processus : unload() et le destructeur l'oublient
            alors sans la décharger.  Les moteurs des appareils restent chargés ainsi, car les
            runtimes d'OpenVINO et de CUDA gardent des threads que leur déchargement, lancé par
            Windows sous son verrou du chargeur, attendrait indéfiniment.
        */
        void keep_loaded() { keep_ = true; }
        bool loaded() const { return handle_ != nullptr; }
        const std::string& path() const { return path_; }

        // Les fonctions de la bibliothèque.
        uint32_t (ALBIREO_ENGINE_CALL* abi_version)(void) = nullptr;
        int (ALBIREO_ENGINE_CALL* describe)(char*, size_t, size_t*) = nullptr;
        albireo_engine* (ALBIREO_ENGINE_CALL* create)(
            const char*, albireo_log_callback, void*, char*, size_t) = nullptr;
        void (ALBIREO_ENGINE_CALL* destroy)(albireo_engine*) = nullptr;
        int (ALBIREO_ENGINE_CALL* load_model)(
            albireo_engine*, const char*, const char*, const char*, char*, size_t) = nullptr;
        int (ALBIREO_ENGINE_CALL* unload_model)(albireo_engine*, const char*) = nullptr;
        int (ALBIREO_ENGINE_CALL* query)(albireo_engine*, const char*, char*, size_t, size_t*) = nullptr;
        int (ALBIREO_ENGINE_CALL* submit)(
            albireo_engine*, const char*, albireo_event_callback, void*, uint64_t*, char*, size_t) = nullptr;
        int (ALBIREO_ENGINE_CALL* cancel)(albireo_engine*, uint64_t) = nullptr;

        // describe() sous forme de document, ou null.
        json description() const;

        // query() avec un tampon agrandi selon les besoins.
        int run_query(albireo_engine* e, const json& request, json& answer) const;

    private:
        void* handle_ = nullptr;
        // Les parties de cuDNN chargées avec la bibliothèque, libérées avec elle.
        std::vector<void*> preloaded_;
        std::string path_;
        bool keep_ = false;
    };

    // Nom de fichier d'une bibliothèque de moteur de la variante donnée sur cette plateforme :
    // "albireo-engine-cpu-avx2.dll", "libalbireo-engine-cpu-avx2.so".
    std::string engine_file_name(const std::string& variant);

}

#endif // ALBIREO_COMMON_ENGINE_LOADER_H_
