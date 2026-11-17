// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef GULLIVER_ENGINE_API_H_
#define GULLIVER_ENGINE_API_H_

/*
    Interface C entre l'hôte Albireo et ses moteurs d'inférence.

    Un moteur est une bibliothèque partagée construite à partir des sources de engine/ pour une
    classe de machines : x86-64 simple, x86-64 avec AVX2 et FMA, NPU et GPU Intel via OpenVINO,
    ou GPU NVIDIA via CUDA.  Chaque variante exporte les fonctions ci-dessous avec les mêmes
    signatures, si bien que l'hôte charge celle qui convient à la machine et lui parle par ce
    point d'entrée unique ; remplacer un moteur n'oblige jamais à reconstruire l'hôte.

    Conventions
      - Seuls des types C traversent la frontière.  Les données structurées voyagent en JSON
        UTF-8, soit dans un tampon possédé par l'appelant (fonctions prenant json, capacity et
        needed), soit sous forme d'un pointeur et d'une taille valables seulement pendant un
        callback.
      - Une fonction qui écrit du JSON dans un tampon de l'appelant renvoie
        GULLIVER_E_BUFFER_TOO_SMALL quand le document ne tient pas, et place dans *needed la
        taille requise, zéro terminal compris, pour que l'appelant puisse réessayer avec un
        tampon plus grand.
      - Les messages d'erreur sont écrits terminés par NUL dans le tampon d'erreur de
        l'appelant, tronqués quand il est trop petit.
      - Chaque fonction est thread-safe.  Les callbacks d'événements tournent sur des threads du
        moteur, doivent rendre la main vite et peuvent appeler gulliver_engine_cancel(), et
        aucune autre fonction, sur le moteur qui les a invoqués.

    Durée de vie
      gulliver_engine_create() démarre le moteur avec la configuration de l'hôte ; les modèles
      sont ensuite chargés et déchargés par identifiant ; les requêtes sont soumises à un modèle
      chargé et signalent leur avancement par des événements jusqu'à un unique
      GULLIVER_EVENT_DONE ou GULLIVER_EVENT_ERROR, après quoi le callback n'est plus jamais
      invoqué pour cette requête.  gulliver_engine_destroy() annule les requêtes encore en
      cours, attend leur événement final et libère tout.
*/

#include <stddef.h>
#include <stdint.h>

#  define GULLIVER_ENGINE_EXPORT __attribute__((visibility("default")))
#  define GULLIVER_ENGINE_CALL

#define GULLIVER_ENGINE_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gulliver_engine gulliver_engine;

/* Codes de retour. */
enum
{
    GULLIVER_OK = 0,
    GULLIVER_E_INVALID_ARGUMENT = 1,
    GULLIVER_E_NOT_FOUND = 2,
    GULLIVER_E_BUSY = 3,
    GULLIVER_E_OUT_OF_MEMORY = 4,
    GULLIVER_E_UNSUPPORTED = 5,
    GULLIVER_E_INTERNAL = 6,
    GULLIVER_E_BUFFER_TOO_SMALL = 7,
    GULLIVER_E_CONTEXT_OVERFLOW = 8
};

/* Types des événements remis au callback d'une requête. */
enum
{
    GULLIVER_EVENT_CONTENT = 1,    /* texte UTF-8 ajouté à la réponse */
    GULLIVER_EVENT_REASONING = 2,  /* texte UTF-8 ajouté à la trace de raisonnement */
    GULLIVER_EVENT_TOOL_CALL = 3,  /* JSON {"index","id","name","arguments"} d'un appel complet */
    GULLIVER_EVENT_PROGRESS = 4,   /* JSON {"prompt_done","prompt_total","cached"} pendant la lecture */
    GULLIVER_EVENT_DONE = 5,       /* JSON {"finish_reason","usage","timings"}, événement final ; voir submit */
    GULLIVER_EVENT_ERROR = 6       /* JSON {"code","message"}, événement final */
};

/* Gravités passées au callback de journal. */
enum
{
    GULLIVER_LOG_DEBUG = 0,
    GULLIVER_LOG_INFO = 1,
    GULLIVER_LOG_WARNING = 2,
    GULLIVER_LOG_ERROR = 3
};

typedef void (GULLIVER_ENGINE_CALL *gulliver_event_callback)(void* user, int kind, const char* data, size_t size);
typedef void (GULLIVER_ENGINE_CALL *gulliver_log_callback)(void* user, int level, const char* message);

/* Version d'ABI que la bibliothèque implémente, GULLIVER_ENGINE_ABI_VERSION quand elle
   correspond à l'hôte.  L'hôte la vérifie avant d'appeler quoi que ce soit d'autre. */
GULLIVER_ENGINE_EXPORT uint32_t GULLIVER_ENGINE_CALL gulliver_engine_abi_version(void);

/* Description JSON du moteur et des appareils qu'il a trouvés sur cette machine :
   {"variant","version","backend","build","kernels","devices":[...],"usable":true|false,
   "reason","reason_code","reason_args","missing"}.  Un moteur qui ne peut pas tourner ici donne
   la raison en anglais, et un code avec ses valeurs pour les fenêtres, qui l'expliquent dans la
   langue de l'utilisateur : isa_avx2, isa_avx_vnni (le processeur n'a pas les instructions des
   noyaux), no_nvidia_driver, old_nvidia_driver {driver, runtime},
   no_nvidia_card, old_nvidia_card {card, cc}, no_intel_device, intel_device_unusable
   {detail} (le message de chaque plugin d'OpenVINO).  Un moteur qui tourne donne dans missing
   pourquoi les autres appareils qu'il cherche ont été écartés, tel que leur runtime le signale.
   Peut être appelée avant gulliver_engine_create() ; l'hôte la lance dans un processus enfant
   pour classer les moteurs sans risquer son propre processus. */
GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_describe(char* json, size_t capacity, size_t* needed);

/* Démarre un moteur.  config_json contient les réglages de l'hôte (threads, longueur de
   contexte, nombre de séquences, budgets mémoire, appareils) ; les membres absents prennent
   leurs valeurs par défaut.  "warm_history" (true par défaut) lit l'historique de chaque
   réponse d'une requête de chat en arrière-plan une fois la réponse terminée, tel que la
   requête suivante de la conversation le rend, pour que cette requête ne calcule que son
   nouveau message ; la requête "stats" compte ces tokens par modèle dans "warmed_total", et
   les lectures en cours dans "warming".
   Renvoie NULL en cas d'échec, avec la raison dans error. */
GULLIVER_ENGINE_EXPORT gulliver_engine* GULLIVER_ENGINE_CALL gulliver_engine_create(const char* config_json,
    gulliver_log_callback log, void* log_user, char* error, size_t error_capacity);

GULLIVER_ENGINE_EXPORT void GULLIVER_ENGINE_CALL gulliver_engine_destroy(gulliver_engine* engine);

/* Charge le modèle GGUF situé à path (UTF-8) sous l'identifiant model_id.  options_json peut
   remplacer la longueur de contexte et d'autres réglages pour ce modèle.  Le contexte ne
   dépasse jamais les positions que contient la mémoire des clés et valeurs ("kv_mb", ou le
   budget automatique) ; la requête "models" donne le contexte en vigueur.  "projector" nomme le
   fichier GGUF de l'encodeur d'images d'un modèle qui lit des images (un fichier mmproj de
   Qwen3-VL, de LLaVA-NeXT ou de Pixtral, l'encodeur de Ministral 3 et de Devstral Small 2) ; un
   fichier de modèle qui contient ses tenseurs de vision n'en a pas besoin.  La requête
   "models" donne alors "inputs" ["text","image"], "image_tokens" {"min","low","auto","high"}
   (les tokens qu'une image prend au plus avec chaque niveau de détail), "images_per_request"
   et "projector_bytes". */
GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_load_model(gulliver_engine* engine,
    const char* model_id, const char* path, const char* options_json, char* error, size_t error_capacity);

GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_unload_model(gulliver_engine* engine, const char* model_id);

/* Requêtes synchrones, request_json nommant l'opération dans "op" :
     {"op":"stats"}                                   activité, débit et mémoire
     {"op":"models"}                                  modèles chargés et leurs propriétés
     {"op":"tokenize","model":id,"text":s}            identifiants des tokens d'un texte
     {"op":"count","model":id,"messages":[...],...}   tokens d'une conversation rendue */
GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_query(gulliver_engine* engine,
    const char* request_json, char* json, size_t capacity, size_t* needed);

/* Soumet une requête et rend aussitôt la main avec son identifiant.  Les événements
   parviennent à callback, invoqué avec user, jusqu'au dernier.

   Un modèle de langage prend les membres d'une requête de chat completion d'OpenAI :
     "model", "messages" (ou "prompt", un texte lu tel quel), "tools", "tool_choice",
     "reasoning" (true ou false) ou "reasoning_effort" ou
     "chat_template_kwargs":{"enable_thinking"}, "reasoning_budget", "max_tokens" ou
     "max_completion_tokens", "temperature", "top_p", "top_k", "min_p", "presence_penalty",
     "frequency_penalty", "repeat_penalty", "repeat_last_n", "seed", "stop", "logit_bias".
   "top_k" est limité à 1024, et les clés de "logit_bias" doivent être des identifiants de
   tokens du vocabulaire.  Les réglages d'échantillonnage qu'une requête omet prennent les
   valeurs que conseillent les auteurs de la famille du modèle.  Le moteur termine un
   raisonnement après "reasoning_budget" tokens (-1 ou null sans limite) par une phrase qui
   demande la réponse ; sans ce membre, le raisonnement prend au plus la moitié de "max_tokens"
   (la moitié de la place que le contexte laisse à la réponse sans lui), 32768 tokens au plus,
   l'effort "high" ne mettant aucune limite et l'effort "low" lui donnant 512 tokens, 256 avec
   "priority":"background".  Un modèle dont le template ouvre le raisonnement de chaque réponse
   ("reasoning_always" dans la requête "models") raisonne quoi que demande la requête, avec ce
   raisonnement court quand la requête n'en demande pas ; sa réponse reçoit alors le
   "max_tokens" de la requête une fois le raisonnement terminé, et les séquences d'arrêt ne
   s'appliquent qu'à cette réponse.  Quand neuf dixièmes des 384 derniers tokens d'un
   raisonnement répètent des passages de 16 tokens qu'il a déjà écrits deux fois, le moteur
   termine le raisonnement de la même manière ; une réponse qui se répète ainsi se termine avec
   la raison de fin "length" et "repetition":true dans le JSON de DONE.  Un modèle chargé avec
   son projecteur lit les parties "image_url" des messages,
   {"url": une URL data d'un fichier PNG ou JPEG, "detail": "low", "auto" ou "high"}, 16 au
   plus ; un autre modèle les refuse.  Ses événements sont CONTENT, REASONING, TOOL_CALL et
   PROGRESS, puis DONE, ou ERROR avec le code "out_of_memory" quand les réponses en cours
   d'écriture remplissent la mémoire des clés et valeurs ou quand la mémoire manque pour
   l'encodage d'une de ses images (les autres requêtes continuent).

   Un modèle d'embeddings prend "model", "input" (un texte, une liste de textes, une liste
   d'identifiants de tokens ou une liste de telles listes, 2048 entrées au plus) et
   "dimensions" (0 ou absent pour la largeur du modèle ; sinon les premières dimensions,
   normalisées à nouveau).  Son seul événement est DONE, dont le JSON contient
   "data":[{"index","embedding"}] avec chaque vecteur sous la forme base64 de ses valeurs
   float32 petit-boutistes de longueur unité, "dimensions", "usage" et "timings" ; ou ERROR
   quand une entrée échoue.  Une requête annulée avec gulliver_engine_cancel() se termine,
   comme toute autre, par DONE et la raison de fin "cancelled".

   Renvoie GULLIVER_E_BUSY pour une requête que le moteur refuse pour un temps : ses images
   porteraient les pixels décodés que détiennent les requêtes en attente du modèle au-delà de
   1 Gio.  La même requête réussit une fois ces requêtes encodées. */
GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_submit(gulliver_engine* engine,
    const char* request_json, gulliver_event_callback callback, void* user, uint64_t* request_id,
    char* error, size_t error_capacity);

/* Demande à une requête de s'arrêter.  Son événement final, GULLIVER_EVENT_DONE avec la raison
   de fin "cancelled", suit de peu.  Les identifiants inconnus ou terminés sont ignorés. */
GULLIVER_ENGINE_EXPORT int GULLIVER_ENGINE_CALL gulliver_engine_cancel(gulliver_engine* engine, uint64_t request_id);

#ifdef __cplusplus
}
#endif

#endif /* GULLIVER_ENGINE_API_H_ */
