// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef PSEUDALIS_COMMON_ENGINE_API_H_
#define PSEUDALIS_COMMON_ENGINE_API_H_

/*
    L'interface des bibliothèques du moteur : pseudalis-engine.dll, construite pour tout
    processeur x64, et pseudalis-engine-avx2.dll, construite pour les processeurs dotés d'AVX2 et
    de FMA, que le service charge quand le processeur et Windows les proposent (engine_loader.h).
    Les deux exportent les mêmes fonctions C, pour que le service ne dépende jamais du compilateur
    ni des options du moteur.

    Un moteur détient les modèles du détecteur et la clé de la pseudonymisation ; il traite
    plusieurs fichiers à la fois, chaque appel sur le thread de son appelant.

        psd_process(engine, request, data, size, picture, lookup, context, &result, &output, &error)

    request est un objet JSON :
        "operation"     "anonymize", "restore", "anonymize_text", "restore_text", "detect",
                        "anonymize_name", "restore_name" ou "extract"
        "name"          le nom du fichier, dont l'extension indique le type quand son contenu
                        ne l'indique pas
        "entry"         "extract" : le chemin d'une entrée dans l'archive donnée en données
        "tags"          "anonymize" et "anonymize_text" : false écrit chaque valeur du texte sous
                        la forme de son substitut, sans son étiquette (true par défaut)
        "substitutions" [{"substitute", "tag"}] : les substituts du texte d'un fichier ou d'un
                        texte protégé sans étiquettes, que "restore" et "restore_text" rendent
                        et que "anonymize" laisse tels quels
    result reçoit un objet JSON : "name" (le nom de la sortie, protégée ou restaurée :
    "photo.png.txt" pour une image), "kind" ("txt", "csv", "xml", "docx", "xlsx", "pptx",
    "pdf", "image", "zip", "tar", "tar.gz" ou "text"), "report" ({"values": {type: nombre},
    "removed": [...], "notes": [...], "dropped": [entrées d'une archive laissées de côté],
    "tags_restored", "tags_rejected", "substitutes", "substitutes_restored", "pictures_removed"
    (les marqueurs d'image que "restore" a retirés, qu'aucune restauration ne remet dans un
    document)}), "pictures"
    (les identifiants des images trouvées) et, pour "anonymize" et "anonymize_name",
    "substitutions" ([{"substitute", "tag"}] : les valeurs écrites sans leurs étiquettes dans
    les noms et les propriétés, que l'appelant enregistre pour "restore", et, avec "text": true,
    dans le texte sans étiquettes, une fois par emplacement) ; "substitutions" aussi pour
    "anonymize_text" sans étiquettes ; "spans" pour "detect".  output reçoit les octets du fichier
    écrit.  "anonymize_name" et "restore_name" prennent un nom seul en données, un dossier par
    exemple, et donnent le nouveau nom en sortie et dans "name".
    "anonymize" d'une archive donne aussi "entries", chaque entrée écrite : [{"original" (son
    chemin dans l'archive lue), "name" (son chemin dans l'archive écrite), "sha256" (des octets
    écrits), "kind"}], les entrées d'une archive contenue dans l'archive venant après elle, leurs
    chemins après le sien et '!' ("dossier/notes.tar.gz!a.txt").  Grâce à elles, l'appelant
    reconnaît un fichier extrait d'une archive protégée.  "extract" donne en sortie les octets de
    l'entrée "entry" de l'archive donnée en données, lue comme "anonymize" l'a lue : un
    "original" de "entries", dont l'appelant a gardé l'archive d'origine.

    lookup, quand il n'est pas nul, reçoit chaque substitut candidat que "restore" et
    "restore_name" rencontrent dans les noms et les propriétés, et renvoie 1 avec les étiquettes
    enregistrées pour lui dans *tags, une par ligne, la plus probable d'abord (un tampon que le
    moteur libère avec psd_free(), alloué avec psd_alloc()), ou 0 quand il n'en connaît aucune.
    "anonymize" et "anonymize_name" l'interrogent aussi, pour qu'un substitut écrit auparavant,
    dans un nom ou une propriété modifiés depuis, ne soit pas remplacé une seconde fois.

    picture, quand il n'est pas nul, reçoit chaque image trouvée : dans un document (Word,
    PowerPoint, PDF), dans une archive, ou le fichier lui-même, avec son identifiant (16 chiffres
    hexadécimaux d'une empreinte de l'image calculée avec la clé du moteur), son SHA-256 et son
    type de média, et renvoie 1 avec sa description en clair dans *description (un tampon que le
    moteur libère avec psd_free(), alloué avec psd_alloc()), ou 0 sans description.  Le moteur
    protège la description avec le détecteur, en tenant compte en plus de ce que le document lui
    a appris, et écrit "[Image@<identifier> : <description>]" à la place de l'image, ou
    "[Image@<identifier>]" sans description.

    Chaque fonction renvoie 1 en cas de succès, 0 en cas d'échec avec la raison dans error (UTF-8).
*/

#include <stddef.h>

#define PSD_API __attribute__((visibility("default")))
#define PSD_CALL

#define PSEUDALIS_ENGINE_API_VERSION 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psd_buffer
{
    char* data;
    size_t size;
} psd_buffer;

typedef struct psd_picture
{
    const char* bytes;
    size_t size;
    const char* id;
    const char* sha256;
    const char* media_type;
    const char* location;
} psd_picture;

typedef int (PSD_CALL* psd_picture_fn)(void* context, const psd_picture* picture, psd_buffer* description);
typedef int (PSD_CALL* psd_lookup_fn)(void* context, const char* candidate, size_t size, psd_buffer* tags);

typedef struct psd_engine psd_engine;

PSD_API int PSD_CALL psd_api_version(void);
// "avx2" ou "baseline".
PSD_API const char* PSD_CALL psd_variant(void);
/*
    La version et le build du moteur, "1.0.0+20261103170000-0123abcd" : le service ne charge que
    le moteur de son propre build, que l'installateur a placé à côté de lui.
*/
PSD_API const char* PSD_CALL psd_build(void);
// Les modèles du moteur : {"tagger": source, "span_model": source, "size": "tiny"}.
PSD_API int PSD_CALL psd_models(psd_buffer* description);

/*
    Crée un moteur avec une clé de 32 octets, et des options sous forme d'objet JSON :
    "date_order" ("dmy" ou "mdy"), "context" (le contexte de la pseudonymisation, "default").
*/
PSD_API psd_engine* PSD_CALL psd_create(const char* key, size_t key_size, const char* options, psd_buffer* error);
PSD_API void PSD_CALL psd_destroy(psd_engine* engine);
PSD_API int PSD_CALL psd_process(psd_engine* engine, const char* request, const char* data, size_t size,
    psd_picture_fn picture, psd_lookup_fn lookup, void* context, psd_buffer* result, psd_buffer* output,
    psd_buffer* error);
// L'empreinte de la clé, telle que les rapports la donnent.
PSD_API int PSD_CALL psd_key_fingerprint(psd_engine* engine, psd_buffer* fingerprint);
/*
    Remplace les termes imposés du moteur (common/imposed_terms.h) : terms est une liste JSON
    [{"term", "type", "case_sensitive"}] de size octets, que les appels qui démarrent ensuite
    lisent avant tout le reste de leurs textes.  0, avec la raison dans error, pour une liste que
    le service n'accepte pas, les termes du moteur restant alors inchangés.
*/
PSD_API int PSD_CALL psd_set_terms(psd_engine* engine, const char* terms, size_t size, psd_buffer* error);

PSD_API char* PSD_CALL psd_alloc(size_t size);
PSD_API void PSD_CALL psd_free(psd_buffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // PSEUDALIS_COMMON_ENGINE_API_H_
