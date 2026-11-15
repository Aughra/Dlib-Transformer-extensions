// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Les textes de l'application web, en français seulement (fork pour macOS, demande de l'humain du 2026-11-15).
window.PSEUDALIS_TEXTS = {
  fr: {
    tagline: 'Protection des données personnelles de vos documents',
    theme_auto: 'Thème : système', theme_light: 'Thème : clair', theme_dark: 'Thème : sombre',
    speed_help: 'Comment le débit est mesuré',
    speed_help_text: [
      'Les deux cases montrent combien d’octets le moteur lit et écrit par seconde, en ce moment. Ce n’est pas une ' +
        'moyenne de la session : seules comptent les cinq dernières secondes.',
      'Comment c’est mesuré. Chaque opération du moteur (un fichier, un texte, un nom à protéger ou à restaurer) est ' +
        'chronométrée de son début à sa fin ; quand elle se termine, le service note les octets reçus et les octets ' +
        'écrits. Le débit est la somme des octets des opérations terminées dans les cinq dernières secondes, divisée ' +
        'par le temps écoulé depuis le début de la plus ancienne d’entre elles. La page le relit chaque seconde ' +
        '(GET /v1/speed).',
      'Lecture : les documents et les textes donnés au moteur. Écriture : les copies protégées ou restaurées qu’il ' +
        'produit ; une copie protégée est souvent plus grosse que l’original, ses balises portant les valeurs chiffrées.',
      'Un fichier compte quand son traitement se termine : pendant un gros fichier, les cases restent immobiles, puis ' +
        'montrent son débit d’un coup. Au repos, elles retombent à zéro au bout de cinq secondes, et la dernière ' +
        'valeur reste affichée, en plus pâle.'
    ],
    close: 'Fermer',
    speed_read: 'Lecture', speed_write: 'Écriture', speed_title: 'Débit du moteur en direct : les documents lus et les copies protégées écrites',
    api: "Documentation de l'interface",
    protect_title: 'Protéger des fichiers',
    protect_hint: 'Word, Excel, PowerPoint, PDF, textes de toute extension, CSV, images, leurs archives ZIP, TAR ou ' +
      'TAR.GZ, et des dossiers entiers, rendus en archive ZIP. Chaque donnée personnelle est remplacée par une ' +
      'balise que seul cet ordinateur peut restaurer.',
    restore_title: 'Restaurer des fichiers',
    restore_hint: 'Un fichier protégé, ou toute copie de celui-ci, rend son original ; un fichier protégé modifié ' +
      'depuis, un fichier dont l’original a été oublié et un fichier écrit ailleurs à partir de valeurs protégées ' +
      'retrouvent leurs valeurs par leurs balises, sans leurs images. Un fichier protégé sans balises revient tant ' +
      'que son original est conservé.',
    drop: 'Déposez des fichiers ou des dossiers ici, ou cliquez pour choisir des fichiers',
    choose_folder: 'Choisir un dossier',
    folder_too_big: 'Le dossier contient trop de fichiers, ou trop d’octets, pour une archive.',
    read_failed: 'Le fichier ne peut pas être lu.',
    keep: 'Conserver les originaux sur cet ordinateur, pour les restaurer à l’identique',
    text_title: 'Protéger un texte',
    text_protect: 'Protéger',
    text_restore: 'Restaurer',
    text_detect: 'Afficher les valeurs trouvées',
    legend_person: 'Personne',
    legend_org: 'Organisation',
    legend_place: 'Lieu',
    legend_contact: 'Coordonnées',
    legend_id: 'Identifiant',
    legend_date: 'Date',
    legend_protected: 'Surlignées : les données protégées.',
    legend_restored: 'Soulignées : les valeurs restaurées.',
    download: 'Télécharger',
    working: 'Traitement...',
    values: '{n} valeurs remplacées',
    values_one: '{n} valeur remplacée',
    dropped: '{n} éléments écartés',
    dropped_one: '{n} élément écarté',
    restored_original: 'original restauré',
    restored_picture: 'image restaurée',
    restored_tags: '{n} valeurs restaurées',
    restored_tags_one: '{n} valeur restaurée',
    restored_forgotten: 'son original oublié le {date}',
    pictures_removed: '{n} images retirées',
    pictures_removed_one: '{n} image retirée',
    none_found: 'Aucune donnée personnelle trouvée.',
    written_by: 'Écrit par',
    s_version: 'Version',
    s_engine: 'Moteur',
    s_drive: 'Lecteur',
    s_drive_off: 'pas de lecteur',
    s_albireo: 'Description des images',
    s_albireo_off: 'Albireo absent',
    s_records: 'Originaux conservés',
    s_offline: 'Le service ne répond pas.',
    records_title: 'Originaux conservés',
    records_hint: 'Les originaux des fichiers protégés sur cet ordinateur, chiffrés dans son coffre. Un original ' +
      'oublié est supprimé avec ses images : son fichier protégé est alors restauré par ses balises, qui rendent ' +
      'ses valeurs mais pas ses images ; protégé sans balises, il ne peut plus être restauré.',
    records_none: 'Aucun original conservé.',
    records_removed: 'retiré du lecteur, conservé jusqu’au {d}',
    records_more: 'Afficher plus',
    col_name: 'Nom',
    col_date: 'Date',
    col_source: 'Origine',
    col_size: 'Taille',
    source_drive: 'lecteur',
    source_shell: 'Explorateur',
    source_web: 'web',
    source_api: 'interface',
    btn_original: 'Original',
    btn_forget: 'Oublier',
    confirm_forget: 'Oublier l’original de {f} ? Il est supprimé du coffre avec ses images, et son fichier protégé ' +
      'ne sera plus restauré que par ses balises, sans ses images.',
    confirm_forget_untagged: 'Oublier l’original de {f} ? Il est supprimé du coffre avec ses images ; protégé sans ' +
      'balises, son fichier ne pourra plus être restauré.',
    forget_title: 'Oublier un original',
    btn_cancel: 'Annuler',
    btn_close: 'Fermer',
    dialog_error: 'Opération impossible',
    records_untagged: 'protégé sans balises',
    tags_option: 'Écrire les balises dans ces fichiers',
    tags_option_hint: 'Avec elles, toute copie se restaure avec la clé de cet ordinateur. Cette case ne vaut ' +
      'que pour les fichiers déposés ici ; le réglage général, que suit le lecteur, est dans les Réglages ' +
      'de l’application ouverte depuis l’icône.',
    no_restore_hint: 'Sans balises ni original conservé, les fichiers protégés ne pourront pas être restaurés.',
    protected_untagged: 'sans balises',
    not_restorable: 'il ne pourra pas être restauré',
    text_tags: 'Écrire les balises dans ce texte',
    text_tags_hint: 'Cette case ne vaut que pour ce texte ; le réglage général, que suit le lecteur, est ' +
      'dans les Réglages de l’application ouverte depuis l’icône.',
    text_untagged_hint: 'Sans balises, seule cette page restaure le texte, tant qu’elle reste ouverte.',
    set_tags: 'Écrire les balises par défaut',
    set_tags_hint: 'Le lecteur suit ce réglage, l’interface l’applique aux requêtes sans paramètre tags, et ' +
      'les cases des encadrés Protéger des fichiers et Protéger un texte en partent. Sans balises, un fichier ne ' +
      'revient que tant que son original est conservé.',
    err_tagless_forgotten: 'Ce fichier a été protégé sans balises et son original a été oublié le {date} : ses ' +
      'valeurs ne peuvent plus revenir.',
    key_title: 'Clé de cet ordinateur',
    key_hint: 'Cette clé restaure tous les fichiers protégés ici. Exportez-la, protégée par une phrase secrète de ' +
      "12 caractères ou plus, et conservez l'export hors de cet ordinateur : sans lui, une nouvelle installation " +
      'ne restaure rien.',
    key_fingerprint: 'Empreinte',
    passphrase: 'Phrase secrète',
    passphrase_again: 'Phrase secrète, à nouveau',
    btn_export: 'Exporter la clé',
    btn_import: 'Importer une clé',
    import_hint: 'Un import remplace la clé de cette installation, qui ne doit encore conserver aucun original.',
    passphrase_mismatch: 'Les deux phrases secrètes diffèrent.',
    key_exported: 'La clé est exportée : conservez le fichier hors de cet ordinateur.',
    key_imported: 'La clé est importée : le service redémarre avec elle.',
    settings_title: 'Réglages',
    set_language: 'Langue des descriptions d’images',
    set_language_auto: 'Français si la langue du service est le français, anglais sinon',
    set_drive: 'Lettre du lecteur (vide : pas de lecteur)',
    set_albireo: "Décrire les images avec Albireo lorsqu'il est installé",
    set_max: 'Fichier le plus grand (Mo)',
    set_removed: 'Conservation après retrait du lecteur (jours)',
    btn_save: 'Enregistrer',
    saved: 'Enregistré.',
    saved_restart: 'Enregistré : le service redémarre.',
    err_http: "Le service a répondu par l'erreur {status}.",
    err_busy: "Le service traite d'autres fichiers : réessayez dans un instant.",
    err_settings_publication: "Le proxy n'a pas une forme que le service lit.",
    err_engine: "Le moteur de Pseudalis n'est pas prêt.",
    err_file_too_large: 'Le fichier dépasse {mb} Mo, la limite des réglages.',
    err_protect_failed: 'Le fichier ne peut pas être anonymisé : {error}',
    err_restore_failed: 'Le fichier ne peut pas être restauré : {error}',
    err_unsupported: 'Pseudalis ne traite pas les fichiers de ce type.',
    err_kind_mismatch: 'Le contenu de ce fichier ne correspond pas à son nom.',
    err_encoded_content: 'Ce fichier contient des parties encodées, pièces jointes, base64 ou RTF par exemple, que ' +
      'Pseudalis ne sait pas lire : il n’est pas traité.',
    err_not_protected: 'Ce fichier ne contient aucune valeur protégée par Pseudalis avec la clé de cet ordinateur.',
    err_original_missing: "L'original de ce fichier ne peut plus être lu dans le coffre.",
    err_picture_forgotten: 'L’image de ce fichier, oubliée le {date}, ne peut plus être rendue.',
    err_picture_missing: 'Le coffre de cet ordinateur ne contient pas l’image de ce fichier.',
    err_vault: "L'original ne peut pas être conservé dans le coffre.",
    err_database: 'La base de données de Pseudalis ne peut pas être écrite : {e}',
    err_authentication: "Le jeton d'administration manque ou est incorrect : rouvrez Pseudalis depuis son icône.",
    err_bad_request: "La requête n'est pas valide.",
    err_not_found: "Cet original n'est plus conservé.",
    err_server_error: "Le service n'a pas pu traiter la requête.",
    err_text_failed: 'Le texte ne peut pas être traité : {e}',
    err_stopping: "Le service s'arrête.",
    err_passphrase_short: 'La phrase de passe doit compter au moins 12 caractères.',
    err_import_failed: 'La clé ne peut pas être importée : {e}',
    err_import_not_empty: "Une clé ne s'importe que tant que cette installation ne conserve aucun original " +
      'ni aucun fichier sur son lecteur.',
    err_settings: 'Les réglages ne peuvent pas être écrits.',
    err_settings_drive: 'La lettre du lecteur doit être A, B ou de D à Z.',
    err_settings_value: 'Le réglage « {field} » a une valeur que le service n’accepte pas',
    range: ' (de {min} à {max}).',
    s_history: 'Espace des originaux',
    records_removed_always: 'retiré du lecteur, conservé sans limite de durée',
    set_dates: 'Ordre des dates numériques',
    set_dates_dmy: 'jour/mois/année (31/12/2026)',
    set_dates_mdy: 'mois/jour/année (12/31/2026)',
    set_history: 'Espace des originaux conservés (Mo)',
    set_removed_hint: 'Avec 0 jour, les originaux des fichiers retirés du lecteur sont conservés sans limite de ' +
      'durée. Quelle que soit la durée, les originaux conservés restent dans leur espace : quand il est plein, ou ' +
      'que le disque manque d’espace libre, les plus anciens sont oubliés en premier.',
    terms_title: 'Termes imposés',
    terms_hint: 'Chaque terme est remplacé par son étiquette avant toute autre détection, dans les fichiers, les ' +
      'textes et les noms : le nom d’un client, d’un projet, d’un contrat ou d’une organisation que Pseudalis ne ' +
      'connaît pas. Sans respect de la casse, la casse et les accents sont ignorés. 100 termes au plus.',
    col_term: 'Terme',
    col_case: 'Respect de la casse',
    col_tag: 'Étiquette',
    terms_none: 'Aucun terme imposé.',
    terms_add: 'Ajouter un terme',
    terms_save: 'Enregistrer les termes',
    terms_count: '{n} terme(s) sur {max}',
    terms_saved: 'Enregistré : {n} terme(s) imposé(s).',
    btn_remove: 'Retirer',
    tag_ORG: 'Organisation (ORG)',
    tag_PERSON: 'Personne (PERSON)',
    tag_LOCATION: 'Lieu (LOCATION)',
    tag_ADDRESS: 'Adresse (ADDRESS)',
    tag_ID: 'Identifiant (ID)',
    tag_EMAIL: 'Adresse e-mail (EMAIL)',
    tag_PHONE: 'Numéro de téléphone (PHONE)',
    tag_URL: 'Adresse web (URL)',
    err_terms_invalid: 'Terme {n} : un terme est un texte d’une ligne, de 1 à {length} caractères, avec une lettre ' +
      'ou un chiffre.',
    err_terms_too_many: '{max} termes au plus peuvent être imposés.',
    err_terms_type: 'Terme {n} : son étiquette n’est pas une de celles que le service accepte.',
    err_terms_duplicate: 'Le terme {n} figure deux fois.'
  }
};
