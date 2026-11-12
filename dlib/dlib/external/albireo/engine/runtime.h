// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_RUNTIME_H_
#define ALBIREO_ENGINE_RUNTIME_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "model_config.h"

namespace albireo
{

    struct forward_batch
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les tokens que calcule une passe avant, éventuellement issus de plusieurs
                séquences : le token suivant de chaque séquence en cours de génération, et
                un morceau du prompt d'une séquence en cours de lecture.  Chaque produit de
                la passe s'exécute alors une seule fois sur tous ces tokens, et c'est ce
                qu'apporte le regroupement en lot : les poids sont lus une fois pour tout le
                lot au lieu d'une fois par séquence.

                segments liste, pour chaque séquence présente, la plage de tokens qui lui
                appartient.  Les tokens d'un segment sont des positions consécutives qui
                prolongent la séquence : le premier se place à la longueur actuelle de la
                séquence.

                logit_rows liste les tokens dont la distribution du token suivant est
                nécessaire : le dernier token de chaque segment qui termine un prompt ou
                poursuit une génération.  Avec hidden_only, la passe s'arrête à leurs états
                cachés normalisés, ce que renvoie un modèle d'embedding.

                positions, quand il n'est pas vide, contient les positions rotatives des
                tokens, position_components valeurs par token : temps, ligne et colonne.
                Quand il est vide, chaque composante d'un token est l'emplacement qu'il
                occupe dans sa séquence.  Les modèles à positions rotatives multimodales
                (model_config::rope_sections) donnent aux tokens d'une image les lignes et
                les colonnes de sa grille, de sorte que leurs positions s'écartent de leurs
                emplacements ; les emplacements restent consécutifs, et l'attention laisse
                un token voir les emplacements jusqu'au sien.  Les autres modèles tournent
                selon la composante de temps.

                embedding_rows liste les tokens dont l'entrée vient d'un encodeur au lieu
                de la table d'embeddings : le token à la ligne embedding_rows[i] du lot lit
                config().input_width() flottants dans embeddings, à partir de
                i * config().input_width() : son embedding suivi des tranches qu'un modèle
                avec DeepStack ajoute à son état caché après ses premières couches.
        !*/
        struct segment
        {
            int sequence = -1;
            long start = 0;
            long count = 0;
        };

        static const long position_components = 3;

        std::vector<int> tokens;
        std::vector<segment> segments;
        std::vector<long> logit_rows;
        bool hidden_only = false;
        std::vector<long> positions;
        std::vector<long> embedding_rows;
        std::vector<float> embeddings;

        void clear()
        {
            tokens.clear();
            segments.clear();
            logit_rows.clear();
            hidden_only = false;
            positions.clear();
            embedding_rows.clear();
            embeddings.clear();
        }
    };

    class model_runtime
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La passe avant d'un modèle chargé et le stockage des clés et valeurs de ses
                séquences.  L'ordonnanceur la pilote par cette interface, de sorte que le
                runtime processeur, avec ses noyaux quantifiés, et le runtime tenseur, avec
                les périphériques de dlib, sont interchangeables.

                Les séquences sont numérotées par le runtime.  Une séquence garde ses
                positions d'un appel à l'autre jusqu'à ce qu'elle soit tronquée ou libérée,
                et c'est ainsi que l'ordonnanceur réutilise le début d'une conversation
                d'une requête à la suivante.
        !*/
    public:
        virtual ~model_runtime() {}

        virtual const model_config& config() const = 0;

        // Séquences.
        virtual int create_sequence() = 0;
        virtual void release_sequence(int seq) = 0;
        virtual long sequence_length(int seq) const = 0;
        virtual void truncate_sequence(int seq, long length) = 0;
        // Fait de la place pour la longueur totale donnée ; false quand la mémoire est
        // épuisée.
        virtual bool reserve_sequence(int seq, long length) = 0;
        // Fait contenir à dst les length premières positions de src, quel que soit son
        // contenu antérieur ; false quand la mémoire est épuisée.  C'est ainsi que
        // plusieurs requêtes partent du même préfixe en cache, comme un long prompt
        // système, sans le recalculer.
        virtual bool copy_sequence(int src, int dst, long length) = 0;
        /*
            La plus grande position, au plus length, à laquelle la séquence peut reprendre par
            truncate_sequence() ou copy_sequence() : length pour un modèle à clés et valeurs ; pour un
            modèle à état récurrent (model_config::recurrent()), dont l'état ne revient pas en arrière, 0,
            sa longueur, ou l'un de ses points de reprise.
        */
        virtual long resume_point(int seq, long length) const
        {
            (void)seq;
            return length;
        }

        // Exécute le lot, ajoute ses clés et valeurs aux séquences et écrit, pour chaque
        // entrée de logit_rows, une ligne de config().vocab logits dans logits, sauf si le
        // lot ne demande que les états cachés.
        virtual void forward(const forward_batch& batch, std::vector<float>& logits) = 0;

        // Les états cachés après la normalisation finale des logit_rows du dernier
        // forward, config().d_model flottants chacun.
        virtual const std::vector<float>& hidden() const = 0;

        // Mémoire occupée par les clés et valeurs, en octets, et le maximum qu'elle peut
        // occuper.
        virtual size_t kv_bytes_used() const = 0;
        virtual size_t kv_bytes_budget() const = 0;
        virtual size_t kv_bytes_per_token() const = 0;

        // Rend au système la mémoire des clés et valeurs qu'aucune séquence ne détient.
        virtual void trim_memory() = 0;

        // Indique si le modèle a des requêtes en cours ou en attente (voir matmul_offload).
        virtual void set_busy(bool busy) = 0;

        // Octets de poids que le runtime garde résidents (les fichiers mappés comptent pour
        // leur taille).
        virtual size_t weight_bytes() const = 0;

        virtual std::string description() const = 0;
    };

}

#endif // ALBIREO_ENGINE_RUNTIME_H_
