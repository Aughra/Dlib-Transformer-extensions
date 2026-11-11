// Fork pour macOS de Dlib-Transformer-extensions (Albireo pour Apple Silicon).
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_HOST_HUB_H_
#define ALBIREO_HOST_HUB_H_

#include <string>

#include "../common/json.h"

namespace albireo
{
    /*
        Les modèles GGUF de Hugging Face, au-delà du catalogue de l'original : la recherche, les fichiers d'un dépôt,
        et l'entrée de catalogue d'un fichier choisi, que le gestionnaire de modèles télécharge comme les autres
        (model_manager::add_custom()).  Chaque modèle et chaque fichier dit s'il tournera sur ce moteur : son
        architecture (model_config.cpp) et son format de poids (qkernels.cpp) ; « gpu » dit si les noyaux Metal le
        prennent (Q4_K et Q6_K d'un modèle dense).  Les requêtes passent par http_get(), donc par le proxy et le
        mode hors ligne du service.
    */

    // Les modèles GGUF de la recherche query, les plus téléchargés d'abord : {"models": [...]}.
    bool hub_search(const std::string& query, json& out, std::string& error);

    // Les fichiers GGUF du dépôt repo, les projecteurs d'images (mmproj) à part : {"files": [...], "projectors": [...]}.
    bool hub_files(const std::string& repo, json& out, std::string& error);

    // L'entrée de catalogue du fichier file du dépôt repo, avec le projecteur projector (vide : aucun).
    bool hub_entry(const std::string& repo, const std::string& file, const std::string& projector, json& entry,
        std::string& error);
}

#endif // ALBIREO_HOST_HUB_H_
