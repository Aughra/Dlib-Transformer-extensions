// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_KV_CACHE_H_
#define ALBIREO_ENGINE_KV_CACHE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace albireo
{

    /*!
        Le stockage des clés et valeurs : demi-précision, ou entiers 8 bits par groupes de
        kv_q8_group valeurs qui partagent une échelle en demi-précision (q8), 34 octets pour 32
        valeurs contre 64 en demi-précision, pour la mémoire d'un contexte et pour les octets que
        lit l'attention.
    !*/
    enum class kv_format { f16, q8 };

    const long kv_q8_group = 32;

    // Les octets d'une ligne de head_dim clés ou valeurs dans un format, ses échelles comprises.
    inline size_t kv_row_bytes(kv_format format, long head_dim)
    {
        return format == kv_format::q8
            ? static_cast<size_t>(head_dim) + static_cast<size_t>(head_dim / kv_q8_group) * sizeof(uint16_t)
            : static_cast<size_t>(head_dim) * sizeof(uint16_t);
    }

    class kv_pool
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                La mémoire qui contient les clés et valeurs de chaque séquence d'un modèle,
                sous forme de blocs d'un nombre fixe de positions.  Une séquence possède les
                blocs où tombent ses positions et rien de plus, si bien qu'un court échange
                prend quelques blocs et un long en prend beaucoup, et la mémoire que réserve le
                moteur suit le texte réellement contenu plutôt que le plus long contexte qu'il
                pourrait contenir.

                Les clés et valeurs sont stockées dans le format du pool (kv_format).  Dans un
                bloc, les lignes d'une couche et d'une tête de clés et valeurs sont contiguës,
                ce qui est l'ordre dans lequel l'attention les lit ; en q8, les échelles de ces
                lignes les suivent, kv_q8_group valeurs par échelle.

                Les blocs sont alloués par groupes à mesure que les séquences grandissent,
                jusqu'à un budget en octets ; un bloc rendu est réutilisé avant que toute
                nouvelle mémoire soit prise, et trim() rend au système la mémoire des blocs
                qu'aucune séquence ne détient.  Tous les blocs se trouvent dans une seule
                région d'adresses réservée d'un coup pour le budget, alignée sur les pages du
                système, dont le système ne fournit les pages qu'à la première utilisation : un
                processeur graphique qui partage la mémoire du processeur peut prendre toute la
                région comme un seul tampon (region(), block_offset()).  Une garde
                (set_guard) peut refuser un nouveau bloc, d'après la mémoire qui reste au
                système : le pool agit alors comme quand son budget est épuisé.

                Plusieurs séquences peuvent détenir le même bloc : une conversation qui commence
                par le texte d'une conversation en cache détient avec elle les blocs pleins de
                ce texte.  Un bloc compte les séquences qui le détiennent et retourne au pool
                quand la dernière le rend ; une séquence copie un bloc partagé avant d'y écrire
                (kv_sequence::reserve).
        !*/
    public:
        kv_pool(long n_layers, long n_kv_heads, long head_dim, long block_positions, size_t budget_bytes,
            kv_format format = kv_format::f16)
            : layers_(n_layers), heads_(n_kv_heads), head_dim_(head_dim), positions_(block_positions), format_(format)
        {
            if (n_layers <= 0 || n_kv_heads <= 0 || head_dim <= 0 || block_positions <= 0)
                throw std::invalid_argument("kv_pool: invalid geometry");
            if (format == kv_format::q8 && head_dim % kv_q8_group != 0)
                throw std::invalid_argument("kv_pool: the rows of 8-bit keys and values take whole groups");
            // Un bloc de 2^41 octets au plus, dont la taille reste loin du débordement.
            const double bytes = 2.0 * n_layers * n_kv_heads * block_positions * static_cast<double>(head_dim);
            if (bytes > 2199023255552.0) throw std::invalid_argument("kv_pool: blocks too large");
            // Les clés puis les valeurs, chacune en layers x heads dalles de positions lignes (et leurs échelles
            // en q8).
            slab_bytes_ = static_cast<size_t>(positions_) * kv_row_bytes(format_, head_dim_);
            bytes_per_kind_ = static_cast<size_t>(layers_) * heads_ * slab_bytes_;
            block_bytes_ = 2 * bytes_per_kind_;
            max_blocks_ = std::max<size_t>(1, budget_bytes / block_bytes_);
            // La table des blocs ne bouge jamais, pour que les threads qui lisent un bloc pendant
            // une passe avant ne le voient jamais déplacé par une allocation, et les listes ne
            // grandissent jamais, pour que rendre un bloc ne puisse pas échouer.
            reserve_region();
            blocks_.reserve(max_blocks_);
            refs_.reserve(max_blocks_);
            free_.reserve(max_blocks_);
            vacant_.reserve(max_blocks_);
        }

        ~kv_pool() { release_region(); }

        kv_pool(const kv_pool&) = delete;
        kv_pool& operator=(const kv_pool&) = delete;

        // La région de tous les blocs, et sa longueur, un multiple de la page.
        uint8_t* region() const { return region_; }
        size_t region_bytes() const { return region_bytes_; }

        // L'endroit où commence le bloc b dans la région.
        size_t block_offset(int b) const { return static_cast<size_t>(b) * stride_; }

        // L'endroit où commencent les clés (values à false) ou les valeurs (values à true) d'une couche et
        // d'une tête dans un bloc, et où commencent leurs échelles (q8).
        size_t slab_offset(bool values, long layer, long head) const
        {
            return (values ? bytes_per_kind_ : 0) + offset(layer, head);
        }
        size_t scales_offset(bool values, long layer, long head) const
        {
            return slab_offset(values, layer, head) + static_cast<size_t>(positions_) * head_dim_;
        }

        /*
            Le contrôle d'un nouveau bloc de mémoire : guard(bytes, allocated) renvoie false quand
            le système ne peut pas fournir les octets du bloc, allocated étant les octets que
            détiennent déjà les blocs du pool.  Il s'exécute avec le pool verrouillé.
        */
        void set_guard(std::function<bool(size_t, size_t)> guard)
        {
            std::lock_guard<std::mutex> l(m_);
            guard_ = std::move(guard);
        }

        long block_positions() const { return positions_; }
        size_t block_bytes() const { return block_bytes_; }
        size_t max_blocks() const { return max_blocks_; }
        kv_format format() const { return format_; }

        size_t allocated_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return blocks_.size() - vacant_.size();
        }
        size_t used_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return blocks_.size() - free_.size() - vacant_.size();
        }
        size_t free_capacity_blocks() const
        {
            std::lock_guard<std::mutex> l(m_);
            return free_.size() + vacant_.size() + (max_blocks_ - blocks_.size());
        }

        // Renvoie un indice de bloc, détenu une fois, ou -1 quand le budget est épuisé ; lève
        // std::bad_alloc quand le système n'a plus de mémoire.
        int allocate()
        {
            std::lock_guard<std::mutex> l(m_);
            if (!free_.empty())
            {
                const int b = free_.back();
                free_.pop_back();
                refs_[static_cast<size_t>(b)] = 1;
                return b;
            }
            if (guard_ && !guard_(block_bytes_, (blocks_.size() - vacant_.size()) * block_bytes_)) return -1;
            if (!vacant_.empty())
            {
                const int b = vacant_.back();
                commit(b);
                vacant_.pop_back();
                refs_[static_cast<size_t>(b)] = 1;
                return b;
            }
            if (blocks_.size() >= max_blocks_) return -1;
            const int b = static_cast<int>(blocks_.size());
            commit(b);
            blocks_.push_back(region_ + block_offset(b));
            refs_.push_back(1);
            return b;
        }

        // Rend une détention d'un bloc ; le bloc retourne au pool avec la dernière.
        void release(int b)
        {
            if (b < 0) return;
            std::lock_guard<std::mutex> l(m_);
            int& holders = refs_[static_cast<size_t>(b)];
            if (holders > 0 && --holders == 0) free_.push_back(b);
        }

        // Une séquence de plus détient le bloc b.
        void share(int b)
        {
            std::lock_guard<std::mutex> l(m_);
            ++refs_[static_cast<size_t>(b)];
        }

        // Indique si plus d'une séquence détient le bloc b.
        bool shared(int b) const
        {
            std::lock_guard<std::mutex> l(m_);
            return refs_[static_cast<size_t>(b)] > 1;
        }

        // Libère la mémoire des blocs qu'aucune séquence ne détient ; leurs emplacements dans la table
        // restent, pour que les indices des autres blocs ne changent jamais.
        void trim()
        {
            std::lock_guard<std::mutex> l(m_);
            for (int b : free_)
            {
                decommit(b);
                vacant_.push_back(b);
            }
            free_.clear();
        }

        // Copie les clés et valeurs du bloc src dans le bloc dst.
        void copy_block(int src, int dst)
        {
            if (src == dst) return;
            const uint8_t* from = blocks_[static_cast<size_t>(src)];
            std::copy(from, from + block_bytes_, blocks_[static_cast<size_t>(dst)]);
        }

        /*
            Les lignes de positions x head_dim clés ou valeurs d'une couche et d'une tête de clés
            et valeurs : des demi-flottants en f16, des entiers 8 bits en q8, dont les échelles,
            kv_q8_group valeurs par échelle, suivent les lignes (key_scales, value_scales).
        */
        uint8_t* keys(int block, long layer, long head)
        {
            return blocks_[static_cast<size_t>(block)] + offset(layer, head);
        }
        uint8_t* values(int block, long layer, long head)
        {
            return blocks_[static_cast<size_t>(block)] + bytes_per_kind_ + offset(layer, head);
        }
        uint16_t* key_scales(int block, long layer, long head)
        {
            return reinterpret_cast<uint16_t*>(keys(block, layer, head)
                + static_cast<size_t>(positions_) * head_dim_);
        }
        uint16_t* value_scales(int block, long layer, long head)
        {
            return reinterpret_cast<uint16_t*>(values(block, layer, head)
                + static_cast<size_t>(positions_) * head_dim_);
        }

    private:
        size_t offset(long layer, long head) const
        {
            return (static_cast<size_t>(layer) * heads_ + head) * slab_bytes_;
        }

        static size_t page_bytes() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

        // Les adresses de max_blocks_ blocs, réservées sans mémoire derrière elles.
        void reserve_region()
        {
            // Chaque bloc commence sur une page, pour que sa mémoire retourne entière au système.
            const size_t page = page_bytes();
            stride_ = (block_bytes_ + page - 1) / page * page;
            region_bytes_ = max_blocks_ * stride_;
            void* p = mmap(nullptr, region_bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (p == MAP_FAILED) throw std::bad_alloc();
            region_ = static_cast<uint8_t*>(p);
        }

        void release_region()
        {
            if (!region_) return;
            munmap(region_, region_bytes_);
            region_ = nullptr;
        }

        // Reprend les pages du bloc b après decommit() ; le système les fournit à la première utilisation.
        void commit(int b) { madvise(region_ + block_offset(b), stride_, MADV_FREE_REUSE); }

        // Rend au système la mémoire du bloc b, les adresses restant réservées.
        void decommit(int b)
        {
            madvise(region_ + block_offset(b), stride_, MADV_FREE_REUSABLE);
        }

        long layers_, heads_, head_dim_, positions_;
        kv_format format_;
        size_t slab_bytes_ = 0;
        size_t bytes_per_kind_ = 0;
        size_t block_bytes_ = 0;
        size_t max_blocks_ = 0;
        mutable std::mutex m_;
        uint8_t* region_ = nullptr;
        size_t region_bytes_ = 0;
        size_t stride_ = 0;         // la distance entre deux blocs, en pages entières
        std::vector<uint8_t*> blocks_;
        std::vector<int> refs_;     // le nombre de séquences qui détiennent chaque bloc
        std::vector<int> free_;     // les blocs avec de la mémoire, détenus par aucune séquence
        std::vector<int> vacant_;   // les emplacements dont trim() a libéré la mémoire
        std::function<bool(size_t, size_t)> guard_;
    };

    struct kv_sequence
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Les positions d'une séquence détenues dans un kv_pool : les blocs qu'elle
                possède, dans l'ordre des positions, et combien de positions ils contiennent.
        !*/
        std::vector<int> blocks;
        long length = 0;

        /*
            Fait de la place pour les positions [0, n), en prenant des blocs au pool, et copie
            le bloc où va la position suivante quand d'autres séquences le détiennent aussi,
            pour que les positions qu'écrit cette séquence n'appartiennent qu'à elle.  Renvoie
            false, sans modifier la séquence, quand le pool ou le système ne peut pas fournir
            les blocs.
        */
        bool reserve(kv_pool& pool, long n)
        {
            const long per = pool.block_positions();
            const size_t needed = static_cast<size_t>((n + per - 1) / per);
            const size_t next = static_cast<size_t>(length / per);
            const bool copy_next = next < blocks.size() && next < needed && pool.shared(blocks[next]);
            const size_t extra = (needed > blocks.size() ? needed - blocks.size() : 0) + (copy_next ? 1 : 0);
            if (extra == 0) return true;
            std::vector<int> taken;
            try
            {
                blocks.reserve(blocks.size() + extra);
                taken.reserve(extra);
                while (taken.size() < extra)
                {
                    const int b = pool.allocate();
                    if (b < 0) break;
                    taken.push_back(b);
                }
            }
            catch (const std::bad_alloc&)
            {
                // Mémoire épuisée : comme pour un pool plein, l'appelant libère des séquences en cache ou attend.
            }
            if (taken.size() < extra)
            {
                for (int t : taken) pool.release(t);
                return false;
            }
            if (copy_next)
            {
                const int own = taken.back();
                taken.pop_back();
                pool.copy_block(blocks[next], own);
                pool.release(blocks[next]);
                blocks[next] = own;
            }
            blocks.insert(blocks.end(), taken.begin(), taken.end());
            return true;
        }

        /*
            Détient les positions [0, n) d'une autre séquence : les blocs pleins de ces positions
            sont partagés avec elle, et le dernier bloc, en partie rempli, est copié.  Renvoie
            false, en laissant cette séquence vide, quand le pool ou le système ne peut pas
            fournir la copie.
        */
        bool share_prefix(kv_pool& pool, const kv_sequence& from, long n)
        {
            clear(pool);
            n = std::max<long>(0, std::min(n, from.length));
            const long per = pool.block_positions();
            const size_t full = static_cast<size_t>(n / per);
            int copy = -1;
            try
            {
                blocks.reserve(full + 1);
                if (n % per != 0)
                {
                    copy = pool.allocate();
                    if (copy < 0) return false;
                }
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            if (copy >= 0) pool.copy_block(from.blocks[full], copy);
            for (size_t b = 0; b < full; ++b)
            {
                pool.share(from.blocks[b]);
                blocks.push_back(from.blocks[b]);
            }
            if (copy >= 0) blocks.push_back(copy);
            length = n;
            return true;
        }

        // Garde les positions [0, n) et rend les blocs au-delà.
        void truncate(kv_pool& pool, long n)
        {
            if (n < 0) n = 0;
            if (n < length) length = n;
            const long per = pool.block_positions();
            const size_t keep = static_cast<size_t>((n + per - 1) / per);
            while (blocks.size() > keep)
            {
                pool.release(blocks.back());
                blocks.pop_back();
            }
        }

        void clear(kv_pool& pool) { truncate(pool, 0); }
    };

}

#endif // ALBIREO_ENGINE_KV_CACHE_H_
