// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

#include "sampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>


namespace gulliver
{

    namespace
    {
        const long scan_block = 16;

        // Indique si l'une des scan_block valeurs de x dépasse t ; NaN ne dépasse rien.
        inline bool block_above(const float* x, float t)
        {
            bool above = false;
            for (long i = 0; i < scan_block; ++i) above |= x[i] > t;
            return above;
        }
    }

    sampler::sampler(const sampling_params& p) : p_(p)
    {
        uint64_t seed = p_.seed;
        if (seed == 0)
        {
            std::random_device rd;
            seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        }
        rng_.seed(seed);
    }

    void sampler::accept(int token)
    {
        history_.push_back(token);
        if (p_.repeat_last_n < 0) ++counts_[token];
    }

    int sampler::sample(float* logits, long vocab, int draft, const int* pending, size_t n_pending)
    {
        for (const auto& b : p_.logit_bias)
            if (b.first >= 0 && b.first < vocab) logits[b.first] += b.second;

        /* Pénalités sur les tokens récents, ou sur tout l'historique avec les comptes tenus au fil de sa croissance ;
           les tokens en attente (vérification d'un brouillon) suivent l'historique. */
        if (p_.repeat_penalty != 1.0f || p_.presence_penalty != 0.0f || p_.frequency_penalty != 0.0f)
        {
            std::unordered_map<int, int> recent;
            if (p_.repeat_last_n >= 0)
            {
                const size_t last = static_cast<size_t>(p_.repeat_last_n);
                const size_t from_pending = std::min(n_pending, last);
                const size_t n = std::min(history_.size(), last - from_pending);
                for (size_t i = history_.size() - n; i < history_.size(); ++i) ++recent[history_[i]];
                for (size_t i = n_pending - from_pending; i < n_pending; ++i) ++recent[pending[i]];
            }
            else if (n_pending > 0)
            {
                recent = counts_;
                for (size_t i = 0; i < n_pending; ++i) ++recent[pending[i]];
            }
            const std::unordered_map<int, int>& counts = p_.repeat_last_n < 0 && n_pending == 0 ? counts_ : recent;
            for (const auto& c : counts)
            {
                if (c.first < 0 || c.first >= vocab) continue;
                float& l = logits[c.first];
                if (p_.repeat_penalty != 1.0f) l = l > 0 ? l / p_.repeat_penalty : l * p_.repeat_penalty;
                l -= p_.frequency_penalty * static_cast<float>(c.second) + p_.presence_penalty;
            }
        }

        if (p_.temperature <= 0.0f)
        {
            // Le premier des plus grands logits ; un NaN n'est jamais choisi.
            long best = 0;
            float best_value = -std::numeric_limits<float>::infinity();
            for (long v = 0; v < vocab; ++v)
            {
                if (logits[v] > best_value)
                {
                    best_value = logits[v];
                    best = v;
                }
            }
            last_p_ = 1.0f;
            return static_cast<int>(best);
        }

        // Les k meilleurs candidats ; sans top-k, assez d'entre eux pour le noyau de probabilité (nucleus).
        const long k = p_.top_k > 0 ? std::min<long>(p_.top_k, vocab) : std::min<long>(1024, vocab);
        candidates_.clear();
        candidates_.reserve(static_cast<size_t>(k) + 1);
        {
            /*
                Une sélection bornée garde les k plus grands dans un tas min, dont la plus petite valeur est
                le seuil qu'un logit doit dépasser pour y entrer.  Les blocs de logits qui n'en contiennent aucun
                au-dessus sont sautés d'un coup, ce qui laisse le tas tel que le ferait le parcours complet.  Les
                valeurs NaN n'y entrent jamais.
            */
            auto cmp = [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; };
            float threshold = -std::numeric_limits<float>::infinity();
            long v = 0;
            for (; v < vocab && static_cast<long>(candidates_.size()) < k; ++v)
            {
                const float l = logits[v];
                if (l != l) continue;
                candidates_.emplace_back(l, static_cast<int>(v));
                std::push_heap(candidates_.begin(), candidates_.end(), cmp);
            }
            if (static_cast<long>(candidates_.size()) == k) threshold = candidates_.front().first;
            auto offer = [&](long u) {
                const float l = logits[u];
                if (!(l > threshold)) return;
                std::pop_heap(candidates_.begin(), candidates_.end(), cmp);
                candidates_.back() = std::make_pair(l, static_cast<int>(u));
                std::push_heap(candidates_.begin(), candidates_.end(), cmp);
                threshold = candidates_.front().first;
            };
            for (; v < vocab && v % scan_block != 0; ++v) offer(v);
            for (; v + scan_block <= vocab; v += scan_block)
                if (block_above(logits + v, threshold))
                    for (long u = v; u < v + scan_block; ++u) offer(u);
            for (; v < vocab; ++v) offer(v);
            if (candidates_.empty())
            {
                // Tous les logits sont NaN.
                last_p_ = 1.0f;
                return 0;
            }
            std::sort(candidates_.begin(), candidates_.end(),
                [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
        }

        // Température et softmax sur les candidats.
        const float inv_t = 1.0f / p_.temperature;
        const float top = candidates_.front().first;
        double sum = 0;
        for (auto& c : candidates_)
        {
            // Un meilleur logit infini prend toute la masse, partagée avec ses égaux.
            c.first = std::isinf(top) ? (c.first == top ? 1.0f : 0.0f) : std::exp((c.first - top) * inv_t);
            sum += c.first;
        }
        for (auto& c : candidates_) c.first = static_cast<float>(c.first / sum);

        // min-p : les candidats bien moins probables que le meilleur.
        size_t keep = candidates_.size();
        if (p_.min_p > 0.0f)
        {
            const float floor = candidates_.front().first * p_.min_p;
            size_t m = 1;
            while (m < keep && candidates_[m].first >= floor) ++m;
            keep = m;
        }
        // top-p : le plus petit préfixe qui atteint la masse de probabilité, le candidat le plus probable seul pour 0.
        if (p_.top_p < 1.0f)
        {
            double mass = 0;
            size_t m = 0;
            while (m < keep)
            {
                mass += candidates_[m].first;
                ++m;
                if (mass >= p_.top_p) break;
            }
            keep = m;
        }
        double total = 0;
        for (size_t i = 0; i < keep; ++i) total += candidates_[i].first;
        // Le candidat qu'un tirage laisse de côté : le brouillon refusé, aucun sinon.
        size_t excluded = keep;
        if (draft >= 0)
        {
            double p = 0;
            for (size_t i = 0; i < keep; ++i)
                if (candidates_[i].second == draft)
                {
                    p = candidates_[i].first;
                    excluded = i;
                    break;
                }
            std::uniform_real_distribution<double> accept(0.0, total);
            if (accept(rng_) < p)
            {
                last_p_ = static_cast<float>(p / total);
                return draft;
            }
            // Les autres candidats se partagent la masse ; un brouillon qui la tenait toute a été gardé ci-dessus.
            if (excluded < keep) total -= p;
            if (!(total > 0)) return draft;
        }
        std::uniform_real_distribution<double> uni(0.0, total);
        const double r = uni(rng_);
        double acc = 0;
        size_t last = keep;
        for (size_t i = 0; i < keep; ++i)
        {
            if (i == excluded) continue;
            last = i;
            acc += candidates_[i].first;
            if (r < acc)
            {
                last_p_ = static_cast<float>(candidates_[i].first / total);
                return candidates_[i].second;
            }
        }
        if (last == keep) return draft;
        last_p_ = static_cast<float>(candidates_[last].first / total);
        return candidates_[last].second;
    }

}
