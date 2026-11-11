// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_MODEL_FILE_H_
#define ALBIREO_ENGINE_MODEL_FILE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <dlib/data_io/gguf_reader.h>

#include "qkernels.h"

namespace albireo
{

    class mapped_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un fichier mappé en lecture seule dans l'espace d'adressage du processus.
                Les poids d'un modèle sont lus sur place dans le mappage : le système
                d'exploitation charge les pages à la première utilisation, les partage
                entre processus et les abandonne sous pression mémoire sans rien réécrire,
                ce que ne permettrait pas une copie dans le tas.

                Le chemin est en UTF-8 sur toutes les plateformes.
        !*/
    public:
        explicit mapped_file(const std::string& utf8_path);
        ~mapped_file();
        mapped_file(const mapped_file&) = delete;
        mapped_file& operator=(const mapped_file&) = delete;

        const uint8_t* data() const { return data_; }
        uint64_t size() const { return size_; }

        // Demande au système de lire une plage avant son utilisation.
        void prefetch(uint64_t offset, uint64_t length) const;

    private:
        const uint8_t* data_ = nullptr;
        uint64_t size_ = 0;
    };

    struct tensor_view
    {
        std::string name;
        dlib::ggml_type type = dlib::ggml_type::F32;
        std::vector<uint64_t> dims;   // ordre ggml, dims[0] contigu
        const uint8_t* data = nullptr;
        uint64_t nbytes = 0;

        uint64_t elements() const
        {
            uint64_t n = 1;
            for (uint64_t d : dims) n *= d;
            return n;
        }
    };

    class model_file
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un modèle GGUF ouvert pour l'inférence : ses métadonnées, analysées par
                dlib::gguf_reader depuis le mappage, et une vue de chaque tenseur pointant
                dans le mappage.
        !*/
    public:
        explicit model_file(const std::string& utf8_path);

        const dlib::gguf_reader& reader() const { return *reader_; }
        const std::string& path() const { return path_; }
        uint64_t size() const { return file_->size(); }
        // Le premier octet du mappage du fichier, sur une page : le GPU le prend en entier
        // (cpu_runtime_metal.cpp).
        const uint8_t* data() const { return file_->data(); }

        const tensor_view* find(const std::string& name) const;
        bool has(const std::string& name) const { return find(name) != nullptr; }

        // Le tenseur du nom donné sous forme de matrice rows x cols, rows étant sa dimension
        // externe.  Lève une exception quand le tenseur manque ou a d'autres dimensions.
        weight_matrix matrix(const std::string& name, long rows, long cols) const;

        // Idem pour un tenseur facultatif, qui donne une matrice invalide quand il est absent.
        weight_matrix optional_matrix(const std::string& name, long rows, long cols) const;

        // Un petit tenseur converti en flottants (normes, biais, facteurs de fréquence).  Lève
        // une exception quand il manque ou ne contient pas exactement count valeurs.
        std::vector<float> floats(const std::string& name, size_t count) const;
        bool optional_floats(const std::string& name, size_t count, std::vector<float>& out) const;

        // Tranche de l'expert e d'un tenseur 3D contenant n_experts matrices rows x cols.
        weight_matrix expert_matrix(const std::string& name, long rows, long cols, long expert, long n_experts) const;

        // Lit toute la section de données une fois pour que le premier token n'attende pas le
        // disque, sur un thread d'arrière-plan quand asynchronous vaut true.
        void warm_up(bool asynchronous) const;

    private:
        std::string path_;
        std::unique_ptr<mapped_file> file_;
        std::unique_ptr<dlib::gguf_reader> reader_;
        std::vector<tensor_view> tensors_;
        std::unordered_map<std::string, size_t> index_;
    };

}

#endif // ALBIREO_ENGINE_MODEL_FILE_H_
