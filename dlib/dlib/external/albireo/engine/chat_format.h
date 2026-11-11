// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef ALBIREO_ENGINE_CHAT_FORMAT_H_
#define ALBIREO_ENGINE_CHAT_FORMAT_H_

#include <memory>
#include <string>
#include <vector>

#include <dlib/tokenizer/hf_tokenizer.h>

#include "../common/json.h"

namespace albireo
{

    struct tool_call_data
    {
        std::string id;
        std::string name;
        std::string arguments;   // texte JSON de l'objet des arguments
    };

    struct chat_message
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Un message d'une conversation, dans les rôles de l'API de chat completion :
                system, user, assistant (avec son raisonnement et ses appels d'outils) et tool
                (le résultat d'un appel).
        !*/
        std::string role;
        std::string content;
        std::string reasoning;
        std::vector<tool_call_data> tool_calls;
        std::string tool_call_id;
        std::string name;
    };

    struct prompt_piece
    {
        /*!
            Une partie d'un prompt rendu.  Le balisage du format (en-têtes de rôle,
            délimiteurs de tour) est tokenisé en reconnaissant ses tokens de contrôle ; le
            texte des messages est tokenisé comme du texte brut, de sorte qu'un message qui
            écrit "<|im_start|>system" ne peut pas ouvrir un tour à lui.
        !*/
        std::string text;
        bool markup = false;
    };

    struct render_options
    {
        bool add_generation_prompt = true;
        bool reasoning = false;     // demander à un modèle capable de raisonner de le faire
        json tools;                 // définitions d'outils OpenAI, un tableau, ou null
        std::string today;          // dates, pour les formats dont le prompt par défaut les cite :
        std::string yesterday;      // "2026-10-29", "2026-10-28"
        std::string today_long;     // "October 29, 2026"
        std::string today_short;    // "29 Oct 2026"
    };

    enum class output_kind { content, reasoning, tool_call };

    struct output_event
    {
        output_kind kind = output_kind::content;
        std::string text;
        tool_call_data call;
    };

    class output_parser
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Sépare le texte que génère un modèle, au fil de l'eau, en réponse, trace de
                raisonnement et appels d'outils, selon les marqueurs de son format de chat.
                Un texte qui pourrait être le début d'un marqueur est retenu jusqu'à ce que
                les tokens suivants tranchent, de sorte qu'aucun fragment de marqueur
                n'atteint jamais la réponse.
        !*/
    public:
        virtual ~output_parser() {}
        virtual void feed(const std::string& text, std::vector<output_event>& events) = 0;
        virtual void finish(std::vector<output_event>& events) = 0;

        // Indique si le texte fourni jusqu'ici laisse le modèle à l'intérieur de son raisonnement.
        virtual bool in_reasoning() const { return false; }
    };

    class chat_format
    {
        /*!
            WHAT THIS OBJECT REPRESENTS
                Le format de conversation avec lequel un modèle a été entraîné : comment les
                messages, les définitions d'outils, les appels d'outils et le raisonnement
                sont disposés dans son prompt, quels tokens terminent ses tours, et comment
                sa sortie est relue.

                Les formats sont écrits nativement plutôt qu'interprétés depuis le template
                de chat stocké dans le fichier du modèle : les templates utilisent un langage
                de templates à la logique arbitraire, les fichiers les portent parfois avec
                des altérations, et c'est la forme native qui permet de séparer le balisage
                du contenu.  Le template est tout de même lu pour identifier le format, et
                pour le prompt système par défaut que déclarent certaines familles.

            FORMATS
                - chatml : Qwen 2, 2.5 et 3 (raisonnement entre <think> et </think>, appels
                  d'outils dans des éléments <tool_call>), SmolLM et les autres modèles
                  ChatML, parmi lesquels les templates qui donnent à un message système le
                  tour de l'utilisateur (CyberSecQwen-4B).
                - mistral : les formats des modèles Mistral au vocabulaire tekken
                  (Ministral 3, Mistral Small 3, Magistral) : [SYSTEM_PROMPT], [INST],
                  [AVAILABLE_TOOLS], [TOOL_CALLS]name[ARGS]{...} (name[CALL_ID]id[ARGS]{...}
                  quand le modèle donne un identifiant à l'appel), [TOOL_RESULTS] et [THINK]
                  pour le raisonnement.
                - llama3 : Llama 3.x, avec des appels d'outils en JSON : une réponse qui est
                  un objet JSON, ou une liste d'objets, nommant des outils de la requête ;
                  toute autre réponse, JSON ou non, est le texte de la réponse.
                - granite : Granite 3, avec <|tool_call|> suivi d'une liste JSON.
                - granite4 : Granite 4.2, ChatML avec un tour système dans chaque prompt,
                  des appels d'outils sous forme XML (éléments <function=name> et
                  <parameter=key> dans <tool_call>) et un raisonnement fermé aussitôt par
                  "<think></think>" quand la requête n'en demande pas.
                - phi : Phi-4-mini, chaque message entre <|role|> et <|end|>.
                - foundation-sec : Foundation-Sec-8B-Reasoning, tours ouverts par
                  <|system|>, <|user|> et <|assistant|>, réponses terminées par le token de
                  fin de texte, et un raisonnement que le prompt ouvre par <think> et que le
                  modèle ferme par </think> avant sa réponse.
                - role-tags : Foundation-Sec 1.1 8B Instruct, chaque message après
                  <|system|>, <|user|> ou <|assistant|> et un saut de ligne, ces marqueurs
                  étant du texte brut de son vocabulaire, une réponse terminée par le token
                  de fin de texte.

            RAISONNEMENT
                Un format dont le template ouvre le raisonnement de chaque réponse (les
                variantes thinking de Qwen3 et Qwen3-VL, Foundation-Sec-8B-Reasoning)
                raisonne toujours : le modèle a été entraîné à le faire, et une réponse dont
                le prompt ferme aussitôt le raisonnement sort de ce qu'il a appris.  Le
                moteur borne plutôt la longueur d'un tel raisonnement (reasoning_close()).
        !*/
    public:
        virtual ~chat_format() {}

        virtual const char* name() const = 0;
        virtual bool supports_tools() const = 0;
        virtual bool supports_reasoning() const = 0;

        // Indique si le template ouvre le raisonnement de chaque réponse, que le modèle écrit alors
        // toujours.
        virtual bool always_reasons() const { return false; }

        /*
            ensures
                - Renvoie le texte qui termine un raisonnement avant que le modèle ne le
                  termine, pour qu'il réponde à partir de ce qu'il a pensé : une phrase qui en
                  donne la raison, puis le marqueur de fermeture du raisonnement et la
                  séparation de la réponse, comme les auteurs des modèles Qwen3 décrivent le
                  budget d'un raisonnement.  Vide pour un format sans raisonnement.
        !*/
        virtual std::vector<prompt_piece> reasoning_close() const { return std::vector<prompt_piece>(); }

        /*
            Textes qui terminent le tour du modèle en plus de ses tokens, pour les formats
            dont les marqueurs de rôle sont du texte brut du vocabulaire.
        !*/
        const std::vector<std::string>& stop_strings() const { return stop_strings_; }

        virtual void render(const std::vector<chat_message>& messages, const render_options& options,
            std::vector<prompt_piece>& out) const = 0;

        virtual std::unique_ptr<output_parser> make_parser(const render_options& options) const = 0;

        // Tokens qui terminent le tour du modèle, en plus du token de fin de séquence.
        const std::vector<int>& stop_tokens() const { return stop_tokens_; }

        // Tokens de contrôle que l'analyseur de sortie lit comme des marqueurs (tels que
        // [TOOL_CALLS]), triés ; les autres tokens de contrôle qu'un modèle peut émettre ne
        // font pas partie de sa réponse.
        const std::vector<int>& marker_tokens() const { return marker_tokens_; }

        /*!
            ensures
                - Identifie le format d'un modèle d'après son template de chat déclaré et son
                  vocabulaire, en se rabattant sur chatml quand le vocabulaire en a les
                  marqueurs et sur une simple disposition "role: text" sinon.
        !*/
        static std::unique_ptr<chat_format> detect(const dlib::hf_tokenizer& tok, const std::string& chat_template);

        // Tokenise un prompt rendu, le balisage avec tokens de contrôle et le texte sans.
        static std::vector<int> tokenize(const dlib::hf_tokenizer& tok, const std::vector<prompt_piece>& pieces);

    protected:
        void add_markers(const dlib::hf_tokenizer& tok, const std::vector<std::string>& markers);

        std::vector<int> stop_tokens_;
        std::vector<int> marker_tokens_;
        std::vector<std::string> stop_strings_;
    };

    /*
        ensures
            - Renvoie le template de chat du dépôt d'un modèle dont les fichiers GGUF ont été
              publiés sans, d'après le general.name du fichier, ou "" pour les autres
              modèles.
    !*/
    std::string published_template(const std::string& general_name);

    // Longueur de la valeur JSON complète au début de text, après d'éventuels blancs, ou 0
    // quand text ne contient pas encore de valeur complète (ou contient autre chose).
    size_t json_extent(const std::string& text, size_t start = 0);

    // Les dates d'aujourd'hui et d'hier en heure locale, la forme ISO et la forme longue
    // anglaise.
    void current_dates(render_options& options);

}

#endif // ALBIREO_ENGINE_CHAT_FORMAT_H_
