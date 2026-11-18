// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_HOST_RAG_ENGINE_H_
#define GULLIVER_HOST_RAG_ENGINE_H_

/*
    La recherche documentaire (RAG) côté service (fork pour macOS).  Portée depuis la page
    (web/js/rag.js) pour que le calcul quitte le navigateur : découpage d'un document en
    passages, et recherche des passages proches d'une question — par les mots (BM25), par le
    sens (cosinus des vecteurs d'embedding), ou les deux fusionnés.

    Un moteur (rag_engine) choisit la façon de chercher ; trois implémentations :
      - lexical : les mots seuls (BM25), sans modèle d'embedding ;
      - dense   : le sens seul (cosinus des vecteurs) ;
      - hybrid  : les deux fusionnés, comme la page (par défaut).
    engine_for(name) rend le moteur d'un nom.

    Le découpage (chunk) et la tokenisation (terms) sont portés fidèlement depuis rag.js.  La
    tokenisation couvre l'alphabet latin (minuscules, accents retirés : é→e, ç→c…) et les
    chiffres, ce qui suffit aux documents en français et en anglais ; les écritures non
    latines ne sont pas normalisées (choix de l'humain, 2026-11-18).
*/

#include <memory>
#include <string>
#include <vector>

namespace gulliver
{
    // Un passage découpé d'un document : sa place dans le texte et le texte lui-même.  Le nom et
    // le propriétaire du document sont ajoutés par l'appelant.
    struct rag_piece
    {
        long long start = 0;
        long long end = 0;
        std::string where;   // la page, la diapositive, la feuille ou la section
        std::string text;
    };

    // Un passage indexé, tel que la recherche le reçoit : son texte, son identité, et son vecteur
    // d'embedding (vide s'il n'a pas été calculé ; un float par dimension, longueur unité attendue).
    struct rag_passage
    {
        std::string id;
        std::string doc;
        std::string name;
        std::string where;
        long long ordinal = 0;
        std::string text;
        std::string model;              // le modèle du vecteur, vide s'il n'y en a pas
        std::vector<float> vector;
    };

    // Un extrait rendu pour une question : un ou plusieurs passages consécutifs d'un document, fondus.
    struct rag_excerpt
    {
        std::string doc;
        std::string name;
        std::string where;
        std::string text;
        double score = 0;
        std::vector<std::string> passages;   // les ids des passages fondus dans l'extrait
    };

    // Les options d'une recherche (mêmes valeurs par défaut que la page).
    struct rag_search_options
    {
        int limit = 12;
        long long budget = 12000;
        std::vector<float> question_vector;   // le vecteur de la question (vide : pas de recherche par le sens)
        std::string model;                    // le modèle du vecteur de la question
        bool rerank = false;                  // reclasse les extraits par un tri fin (couverture et densité des mots)
    };

    // Les mots d'un texte pour la recherche par les mots : minuscules, accents latins retirés, coupés
    // sur ce qui n'est ni lettre ni chiffre, les mots d'une lettre et les mots vides écartés.
    std::vector<std::string> rag_terms(const std::string& text);

    // Découpe un document en passages (fidèle à chunk() de rag.js).
    std::vector<rag_piece> rag_chunk(const std::string& text);

    // Un moteur de recherche documentaire.
    class rag_engine
    {
    public:
        virtual ~rag_engine() = default;
        // Cherche dans passages les extraits proches de question ; au plus options.limit extraits,
        // dans le budget de caractères.  Le vecteur de la question sert aux moteurs dense et hybrid.
        virtual std::vector<rag_excerpt> search(const std::vector<rag_passage>& passages,
                                                const std::string& question,
                                                const rag_search_options& options) const = 0;
        // Le nom du moteur (lexical, dense, hybrid).
        virtual std::string name() const = 0;
    };

    // Le moteur d'un nom (lexical, dense, hybrid) ; hybrid par défaut pour un nom inconnu.
    std::unique_ptr<rag_engine> engine_for(const std::string& name);
}

#endif // GULLIVER_HOST_RAG_ENGINE_H_
