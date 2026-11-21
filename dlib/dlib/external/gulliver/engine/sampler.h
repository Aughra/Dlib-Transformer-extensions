// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_ENGINE_SAMPLER_H_
#define GULLIVER_ENGINE_SAMPLER_H_

#include <cstdint>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gulliver
{

    struct sampling_params
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La façon dont le token suivant est tiré des logits du modèle, avec les
                paramètres de l'API de complétion de chat et les extensions habituelles des
                serveurs locaux.  Une température de 0 choisit le token le plus probable.
        !*/
        float temperature = 0.7f;
        float top_p = 0.95f;          // 1 désactive ; 0 garde le candidat le plus probable
        int top_k = 40;               // 0 désactive
        float min_p = 0.05f;          // relatif au token le plus probable ; 0 désactive
        float repeat_penalty = 1.0f;  // 1 désactive
        int repeat_last_n = 64;
        float presence_penalty = 0.0f;
        float frequency_penalty = 0.0f;
        uint64_t seed = 0;            // 0 : tirée du système
        std::unordered_map<int, float> logit_bias;
    };

    class sampler
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                L'état d'échantillonnage d'une requête : ses paramètres, son générateur
                aléatoire et l'historique des tokens qu'elle a générés, que lisent les pénalités.

                Les filtres s'appliquent dans l'ordre des serveurs de référence : biais et
                pénalités sur les logits, puis top-k, température, min-p et top-p sur les
                candidats, puis un tirage.  Seuls les k meilleurs candidats sont triés, si bien
                qu'une étape coûte un seul passage sur le vocabulaire.
        !*/
    public:
        explicit sampler(const sampling_params& p);

        /*!
            ensures
                - Tire le token suivant d'une ligne de logits (modifiée sur place).
                - Les n_pending tokens de pending comptent pour les pénalités comme des tokens de
                  l'historique, après ceux qu'a enregistrés accept() : les tokens tirés avant celui-ci
                  dans la vérification d'un brouillon, que le moteur n'enregistre qu'une fois gardés.
                - Avec draft >= 0, le token qu'un brouillon propose à cette place (repris à la main de
                  l'original, Albireo 1.0.27, 2026-11-21) : à température nulle, le token le plus
                  probable est rendu, comme sans brouillon ; sinon le brouillon est rendu avec la
                  probabilité que lui donnent les filtres, et à défaut un token tiré parmi les autres
                  candidats en proportion de leurs probabilités.  Les tokens suivent alors la loi des
                  tirages sans brouillon (échantillonnage spéculatif) ; l'appelant compare le résultat
                  au brouillon.
        !*/
        int sample(float* logits, long vocab, int draft = -1, const int* pending = nullptr, size_t n_pending = 0);

        // Enregistre un token de la séquence (généré ou du prompt) pour les pénalités.
        void accept(int token);

        // Probabilité du dernier token renvoyé par sample(), pour les log-probabilités.
        float last_probability() const { return last_p_; }

        const sampling_params& params() const { return p_; }

    private:
        sampling_params p_;
        std::mt19937_64 rng_;
        std::vector<int> history_;
        // Le nombre d'occurrences de chaque token de l'historique, tenu à jour au fil de sa croissance
        // quand les pénalités le lisent en entier.
        std::unordered_map<int, int> counts_;
        std::vector<std::pair<float, int>> candidates_;
        float last_p_ = 1.0f;
    };

}

#endif // GULLIVER_ENGINE_SAMPLER_H_
