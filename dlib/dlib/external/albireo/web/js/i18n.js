// Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
// License: Boost Software License   See LICENSE.txt for the full license.

// Textes de l'interface, en français seulement (le fork pour macOS) ; une clé manquante s'affiche telle quelle.
// Un texte qui s'accorde avec un nombre qu'il affiche a une variante
// par catégorie de pluriel de la langue, key_one et key_other, parmi lesquelles tn choisit.
const texts = {
  fr: {
    newChat: 'Nouvelle discussion', incognito: 'Navigation privée', incognitoBanner: 'Discussion privée : rien n\'est enregistré dans ce navigateur.',
    incognitoStart: 'Démarrer une discussion privée', search: 'Rechercher dans les discussions', projects: 'Projets',
    newProject: 'Nouveau projet', discussions: 'Discussions', pinned: 'Épinglées', today: 'Aujourd\'hui', yesterday: 'Hier',
    week: '7 derniers jours', month: '30 derniers jours', older: 'Plus anciennes', noDiscussions: 'Aucune discussion',
    noProjects: 'Aucun projet', noResults: 'Aucun résultat', settings: 'Paramètres', api: 'API', about: 'À propos',
    collapse: 'Masquer le panneau latéral',
    role_general: 'Général', role_code: 'Code', role_security: 'Cybersécurité', role_embedding: 'RAG',
    roleHint_general: 'Questions, rédaction, analyse, traduction', roleHint_code: 'Programmation, revue, débogage, tests',
    roleHint_security: 'Analyse de sécurité, audits, durcissement, incidents',
    roleServedBy: 'Modèle : {model}', roleShared: 'Assuré par le modèle général',
    reasoning: 'Raisonnement approfondi', reasoningHint: 'Le modèle réfléchit étape par étape avant de répondre',
    answerRepeated: 'Réponse arrêtée : le modèle se répétait.',
    agent: 'Agent', agentHint: 'Le modèle peut utiliser des outils : calcul, code, documents, fichiers',
    agentNoTools: 'Le modèle de cette fonction ne peut pas appeler d\'outils',
    agentNeedsReasoning: 'Activez d\'abord le raisonnement approfondi : ce modèle raisonne avant d\'appeler ses ' +
      'outils',
    attach: 'Joindre des fichiers', send: 'Envoyer', stop: 'Arrêter', placeholder: 'Posez votre question à Albireo…',
    placeholderProject: 'Une question sur {project}…',
    thinking: 'Réflexion', thoughtFor: 'Réflexion de {s}', thoughtBriefly: 'Réflexion brève',
    toolRunning: 'en cours', toolDone: 'terminé', toolError: 'erreur', toolInput: 'Entrée', toolOutput: 'Résultat',
    toolPending: 'préparation', toolStopped: 'arrêté', preparingContext: 'Préparation du contexte…',
    replayQuestion: 'Reposer cette question à la fin de la discussion', justNow: 'à l\'instant',
    tool_calculator: 'Calculatrice', tool_run_javascript: 'JavaScript', tool_search_documents: 'Recherche documentaire',
    tool_document_outline: 'Plan d\'un document', tool_read_document: 'Lecture d\'un document',
    tool_search_discussions: 'Recherche dans les discussions', tool_current_datetime: 'Date et heure',
    tool_create_file: 'Création de fichier',
    toolHint_calculator: 'Calcule exactement une expression arithmétique, pourcentages compris.',
    toolHint_run_javascript: 'Exécute du code JavaScript dans un interpréteur isolé, sans réseau, pour les calculs ' +
      'et les données.',
    toolHint_search_documents: 'Trouve les passages des documents de la discussion et de son projet les plus proches ' +
      'd\'une question.',
    toolHint_document_outline: 'Liste les pages, diapositives, feuilles ou sections d\'un document.',
    toolHint_read_document: 'Lit une partie d\'un document : une page, une diapositive, une feuille ou une plage de ' +
      'caractères.',
    toolHint_search_discussions: 'Retrouve vos discussions précédentes qui contiennent certains mots.',
    toolHint_current_datetime: 'Donne la date, l\'heure et le fuseau horaire de cet ordinateur.',
    toolHint_create_file: 'Crée un fichier texte, par exemple du code, un CSV ou du Markdown, que vous pouvez ' +
      'télécharger.',
    agentHelpTitle: 'Fonctionnement du mode agent',
    agentHelp: 'Avec l\'agent activé, le modèle peut appeler des outils pendant sa réponse. Il calcule, exécute du ' +
      'code, lit vos documents et crée des fichiers, puis répond à partir de leurs résultats. Chaque appel apparaît ' +
      'dans la réponse avec son entrée, son résultat et sa durée. Avec un modèle qui sait raisonner, l\'agent et ' +
      'l\'accès au Web s\'activent avec le raisonnement approfondi.',
    web: 'Web', webAccess: 'Accès au Web', webHint: 'Le modèle peut chercher sur le Web et lire des pages Web',
    webNoTools: 'Le modèle de cette fonction ne peut pas appeler d\'outils, ce que demande l\'accès au Web',
    webNeedsReasoning: 'Activez d\'abord le raisonnement approfondi : ce modèle raisonne avant de chercher sur ' +
      'le Web',
    webOffline: 'Albireo fonctionne hors ligne : l\'accès au Web est indisponible',
    webOff: 'L\'accès au Web est désactivé dans les paramètres du service Albireo',
    webUnavailable: 'Le service Albireo ne donne pas d\'accès au Web',
    webHelp: 'Avec le Web activé, le modèle peut chercher dans Wikipédia et lire des pages Web et des fichiers PDF ' +
      'par le service Albireo. Il ne lit que les adresses que vous écrivez dans vos questions, celles des ' +
      'résultats de ses recherches et les liens des pages qu\'il a déjà lues. Le raisonnement approfondi, l\'agent ' +
      'et le Web sont désactivés au début de chaque nouvelle discussion, et chaque discussion garde son propre choix.',
    tool_web_search: 'Recherche Web', tool_fetch_page: 'Lecture d\'une page',
    toolHint_web_search: 'Cherche dans les articles de Wikipédia par le service Albireo et liste les résultats ' +
      'avec leurs adresses.',
    toolHint_fetch_page: 'Lit, partie par partie, une page Web ou un fichier PDF dont la discussion contient ' +
      'l\'adresse.',
    webSearchBy: 'Recherche Web : {provider}', provider_wikipedia: 'Wikipédia',
    afterAnswer: 'Disponible une fois la réponse écrite', answering: 'Réponse en cours…',
    unreadAnswer: 'Nouvelle réponse, pas encore lue', scrollStart: 'Aller au début de la discussion',
    scrollEnd: 'Aller à la fin de la discussion',
    busyElsewhere: 'Cette discussion reçoit une réponse dans un autre onglet.',
    copyFailed: 'Le texte n\'a pas pu être copié dans le presse-papiers.',
    errStorage: 'Le navigateur a refusé d\'enregistrer la modification.',
    errStorageFull: 'Le stockage du navigateur est plein, et la modification n\'a pas pu être enregistrée.',
    memoryRewritten: 'Mémoire réécrite dans la langue de l\'interface.',
    storageState: 'Stockage de ce navigateur :', storagePersistent: 'conservé par le navigateur',
    storageNotPersistent: 'peut être effacé par le navigateur quand le disque manque de place',
    storageUnknown: 'état inconnu', storageEstimate: '{used} utilisés sur {quota} disponibles',
    storageHint: 'Le navigateur peut effacer ces données quand le disque manque de place. Exportez-les ' +
      'régulièrement avec le bouton ci-dessous.',
    readingContext: 'Lecture du contexte… {p} %', copy: 'Copier', copied: 'Copié', regenerate: 'Régénérer',
    edit: 'Modifier', delete: 'Supprimer', save: 'Enregistrer', cancel: 'Annuler', rename: 'Renommer', pin: 'Épingler',
    unpin: 'Détacher', moveToProject: 'Déplacer vers un projet', removeFromProject: 'Retirer du projet',
    exportMarkdown: 'Exporter en Markdown', exportJson: 'Exporter en JSON', deleteDiscussion: 'Supprimer cette discussion ?',
    deleteDiscussionText: 'La discussion « {title} » sera supprimée de ce navigateur.',
    exportAnswer: 'Exporter cette réponse', deleteQuestion: 'Supprimer la question et sa réponse',
    compacting: 'Compression de la discussion…',
    aiNotice: 'Albireo est une IA et peut commettre des erreurs.',
    errModelNotFound: 'Aucun modèle ne sert cette fonction : activez-en un dans la fenêtre Modèles d\'Albireo.',
    errContextLength: 'La question ne tient pas dans le contexte du modèle, même raccourcie.',
    errUnloaded: 'Le modèle a quitté le moteur pendant sa réponse : posez la question à nouveau.',
    errService: 'Le service Albireo ne répond pas.',
    errStarting: 'Albireo démarre : posez la question dans un instant.',
    memoryTab: 'Mémoire', memoryEnabled: 'Mémoriser les discussions', memoryText: 'Ce dont Albireo se souvient',
    memoryIntro: 'Albireo garde de courtes notes sur vous à partir de vos discussions, hors discussions privées, et les donne au modèle au début de chaque discussion. Elles restent courtes pour laisser le contexte aux questions.',
    memorySensitive: 'Retenir les informations sensibles',
    memorySensitiveHelp: 'Désactivé par défaut : le modèle écarte la santé, les finances, les opinions, les identifiants et ce qui concerne d\'autres personnes.',
    memoryEmpty: 'Rien pour l\'instant : les notes viennent des discussions.', memorySize_one: '{n} mot sur {max} au plus',
    memorySize_other: '{n} mots sur {max} au plus',
    memoryUpdated: 'mise à jour le {date}', memoryReset: 'Réinitialiser la mémoire',
    memoryLearned_one: 'Mémoire mise à jour : {n} nouvelle information vous concernant.',
    memoryLearned_other: 'Mémoire mise à jour : {n} nouvelles informations vous concernant.',
    memoryResetText: 'Albireo oublie ses notes, et les discussions passées ne les alimentent plus. Les discussions elles-mêmes restent.',
    searchProjects: 'Rechercher dans les projets', searchDiscussions: 'Rechercher dans les discussions',
    searchMoreProjects: 'Rechercher parmi les {n} projets…', searchMoreDiscussions: 'Rechercher parmi les {n} discussions…',
    searchPlaceholder: 'Mots, même incomplets…', resultsCount_one: '{n} résultat', resultsCount_other: '{n} résultats',
    searchHint: 'La casse et les accents sont ignorés, et un mot peut être incomplet. Les titres passent en premier, puis le contenu.',
    found_title: 'Titre', found_question: 'Dans une question', found_answer: 'Dans une réponse',
    found_messages: 'Dans les messages', found_name: 'Nom', found_description: 'Description',
    found_instructions: 'Instructions', found_document: 'Dans le document « {name} »',
    found_discussion: 'Dans la discussion « {name} »',
    contextCompacted: 'Les échanges les plus anciens ont été résumés pour tenir dans le contexte du modèle.',
    deleteQuestionTitle: 'Supprimer cette question ?',
    deleteQuestionText: 'La question et sa réponse quittent la discussion, avec les documents joints à la question.',
    welcome: 'Bonjour{name}', welcomeSub: 'Votre assistant privé. Tout s\'exécute sur cet ordinateur, rien n\'en sort.',
    contextUsage: '{used} / {total} jetons de contexte', contextTrimmed: 'Les messages les plus anciens ont été écartés pour tenir dans le contexte du modèle.',
    noModel: 'Aucun modèle n\'est actif. Ouvrez le menu Albireo de la zone de notification pour en télécharger ou en activer un.',
    serviceDown: 'Le service Albireo ne répond pas.', modelLoading: 'Chargement du modèle…',
    serviceStarting: 'Démarrage d\'Albireo…',
    instructions: 'Instructions', instructionsHelp: 'Données au modèle au début de chaque discussion de ce projet.',
    knowledge: 'Connaissances', knowledgeHelp: 'Documents et images que le modèle lit dans les discussions de ce projet.',
    addFiles: 'Ajouter des fichiers', projectName: 'Nom', description: 'Description', color: 'Couleur',
    deleteProject: 'Supprimer le projet',
    deleteProjectText: 'Le projet « {name} » et ses documents seront supprimés ; ses discussions sont conservées hors projet.',
    projectDiscussions: 'Discussions du projet', startInProject: 'Nouvelle discussion dans ce projet',
    untitledProject: 'Nouveau projet', untitled: 'Nouvelle discussion', open: 'Ouvrir',
    general: 'Général', model: 'Génération', data: 'Données', theme: 'Thème', themeAuto: 'Système', themeLight: 'Clair',
    themeDark: 'Sombre', language: 'Langue', languageAuto: 'Automatique', yourName: 'Votre nom',
    yourNameHelp: 'L\'assistant l\'utilise pour s\'adresser à vous.', defaultRole: 'Fonction par défaut',
    temperature: 'Température',
    temperatureHelp: 'Une valeur basse donne des réponses plus ciblées, une valeur haute des réponses plus ' +
      'variées. Automatique prend la valeur que conseillent les auteurs du modèle, ou une valeur usuelle s\'ils ' +
      'n\'en donnent pas.',
    topP: 'Échantillonnage par noyau (top-p)', maxTokens: 'Longueur maximale d\'une réponse (jetons)', maxTokensAuto: 'automatique',
    topPHelp: 'Chaque mot est choisi parmi les plus probables, qui réunissent ensemble cette part des chances. Une ' +
      'valeur basse donne des réponses plus prévisibles. Automatique prend la valeur que conseillent les auteurs ' +
      'du modèle, ou une valeur usuelle s\'ils n\'en donnent pas.',
    topK: 'Nombre de mots candidats (top-k)',
    topKHelp: 'Chaque mot est choisi parmi ce nombre de mots les plus probables. Un petit nombre donne des réponses ' +
      'plus régulières, et 1 prend toujours le mot le plus probable. 0 ne fixe aucune limite, et automatique prend ' +
      'la valeur que conseillent les auteurs du modèle, ou une valeur usuelle s\'ils n\'en donnent pas.',
    sendOnEnter: 'Envoyer avec Entrée, Maj+Entrée pour aller à la ligne', systemPrompt: 'Instructions personnelles',
    systemPromptHelp: 'Ajoutées à chaque discussion, par exemple votre fonction ou le style que vous préférez.',
    exportAll: 'Tout exporter', importAll: 'Importer', clearAll: 'Supprimer toutes les discussions et tous les projets',
    clearAllText: 'Toutes les discussions et tous les projets enregistrés dans ce navigateur seront supprimés.',
    storageUsage: 'Enregistrés dans ce navigateur : {discussions}, {projects}, {images} ({size}).',
    imported: '{discussions} et {projects} importés.',
    discussionsCount_one: '{n} discussion', discussionsCount_other: '{n} discussions', projectsCount_one: '{n} projet',
    projectsCount_other: '{n} projets', imagesCount_one: '{n} image', imagesCount_other: '{n} images',
    importFailed: 'Ce fichier n\'est pas un export Albireo.',
    exportFailed: 'L\'export n\'a pas pu être écrit : {reason}', exportUser: 'Utilisateur',
    dbBlocked: 'Un onglet d\'une version précédente d\'Albireo garde ouvertes les données de ce navigateur : ' +
      'fermez-le, puis rechargez cette page.',
    tokensCount_one: '{n} jeton', tokensCount_other: '{n} jetons',
    errTooManyImages: 'La requête contient plus d\'images que le service n\'en lit à la fois (16).',
    errImageFormat: 'Une image n\'est pas un fichier PNG ou JPEG, les formats que lit le service.',
    errEngineBusy: 'Le modèle est occupé par d\'autres images : envoyez de nouveau la question dans un instant.',
    errBusy: 'Le service lit trop de grandes requêtes à la fois : envoyez de nouveau la question dans un instant.',
    errZipInvalid: 'le fichier est endommagé, ou n\'est pas le document qu\'indique son nom',
    errZipCompression: 'il utilise une méthode de compression que le navigateur ne lit pas',
    errXmlInvalid: 'son contenu est endommagé (XML non valide)',
    engine: 'Moteur', speed: 'Vitesse', tokensPerSecond: '{n} jetons/s', version: 'Version', service: 'Service',
    memory: 'Mémoire', processor: 'Processeur', models: 'Modèles', usage: 'Utilisation sur 30 jours', requests: 'Requêtes',
    usageDay: '{date} : {generated}, {read}', tokensGenerated_one: '{n} jeton généré',
    tokensGenerated_other: '{n} jetons générés', tokensRead_one: '{n} jeton lu', tokensRead_other: '{n} jetons lus',
    usageGenerated: 'Jetons générés',
    usageRead: 'Jetons lus', usageChart: 'Jetons générés et lus par jour sur les 30 derniers jours',
    tokensIn: 'Jetons lus', tokensOut: 'Jetons écrits',
    gpu: 'GPU', gpuOn: 'sur le GPU, {size} de tampons', gpuChecking: 'GPU en cours de contrôle', gpuProcessor: 'sur le processeur',
    serviceTab: 'Service', modelsTab: 'Modèles', gpuDisabled: 'Coupé (onglet Service)',
    fromDevice: 'Depuis cet appareil', macDocs: 'Documents du Mac', macDocsHelp: 'Les fichiers du dossier des documents du Mac. Tout fichier joint dans la page y est aussi copié.', macDocsEmpty: 'Aucun document pour l\'instant : envoie-en un, ou dépose des fichiers dans {folder}.', macDocsAdd: 'Joindre', macDocsSend: 'Envoyer vers le Mac', macDocsDelete: 'Supprimer', macDocsCopyFailed: '{name} n\'a pas pu être copié sur le Mac : {error}', macDocsSent: '{name} est sur le Mac.', gpuMissing: 'Indisponible',
    tooLarge: '{name} est trop volumineux ({size}) ; la limite est de {limit}.', unsupported: '{name} : ce format ne peut pas être lu.',
    imageTooLarge: 'L\'image {name} est trop volumineuse ({size}) ; la limite d\'une image est de {limit}.',
    tooLargeContent: '{name} ne peut pas être lu : son contenu est trop volumineux une fois décompressé.',
    input_text: 'texte', input_image: 'images', input_audio: 'son', input_video: 'vidéo',
    inputRefused: '{name} : le modèle de cette fonction ({model}) ne lit que : {inputs} ; ce fichier ne peut pas être joint.',
    attachInputs: 'Joindre des fichiers (le modèle lit : {inputs})',
    readFailed: '{name} n\'a pas pu être lu : {error}', generatedFile: 'Fichier créé par l\'agent', download: 'Télécharger',
    indexing: 'Indexation…', indexingProgress: 'Indexation… {p} %', indexed_one: '{n} passage',
    indexed_other: '{n} passages', indexedWords_one: '{n} passage (par mots)', indexedWords_other: '{n} passages (par mots)',
    tokensEstimate_one: '≈ {n} jeton', tokensEstimate_other: '≈ {n} jetons',
    knowledgeWhole: 'Environ {tokens}, {p} % du contexte du modèle : les documents sont lus en entier dans chaque discussion.',
    knowledgeSearch: 'Environ {tokens}, {p} % du contexte du modèle : les passages les plus proches de chaque question l\'accompagnent.',
    knowledgeNoVectors: 'Aucun modèle d\'embeddings n\'est actif : la recherche porte sur les mots des documents.',
    legacyFormat: '{name} : ce format Office est antérieur à 2007 ; enregistrez-le en .docx, .xlsx ou .pptx.',
    pdfImagesTitle: 'PDF sans texte',
    pdfImagesText: '{name} ne contient que des images, comme des pages numérisées. Son texte ne peut pas être lu ' +
      'sans reconnaissance optique de caractères (OCR), qu\'Albireo n\'effectue pas. Le fichier n\'a pas été ajouté.',
    pdfPagesWithoutText_one: '{name} a été ajouté, mais une de ses {total} pages n\'a pas de texte lisible, car elle ' +
      'ne contient que des images.',
    pdfPagesWithoutText_other: '{name} a été ajouté, mais {n} de ses {total} pages n\'ont pas de texte lisible, car ' +
      'elles ne contiennent que des images.',
    pdfPassword: '{name} est protégé par un mot de passe et ne peut pas être ouvert. Retirez le mot de passe, puis ' +
      'joignez de nouveau le fichier.',
    pdfInvalid: '{name} n\'est pas un fichier PDF valide, ou il est endommagé.',
    pdfReader: '{name} ne peut pas être lu, car ce navigateur ne peut pas exécuter le lecteur PDF d\'Albireo. ' +
      'Utilisez une version récente de Microsoft Edge ou de Google Chrome, puis rechargez la page.',
    searchedDocument: 'consulté par recherche', removeAttachment: 'Retirer ce document',
    indexingDocument: 'Indexation de {name} pour la recherche…',
    notAddedImages_one: 'Non ajoutée : {names}.', notAddedImages_other: 'Non ajoutées : {names}.',
    imagesNotAdded: '{notAdded} Le contexte d\'une discussion contient au plus {max} images ; places restantes dans ' +
      'celle-ci : {left}. Les images des échanges les plus anciens quittent le contexte une fois que la discussion ' +
      'les résume, ce qui libère leurs places.',
    projectImagesNotAdded: '{notAdded} Un projet contient au plus {max} images ; places restantes dans celui-ci : ' +
      '{left}.',
    notAddedItems_one: 'Non ajouté : {names}.', notAddedItems_other: '{n} non ajoutés : {names}.',
    notAddedItemsAmong: '{n} non ajoutés, dont {names}.',
    projectItemsNotAdded: '{notAdded} Les connaissances d\'un projet contiennent au plus {max} éléments, ' +
      'documents et images ; places restantes dans celui-ci : {left}.',
    projectItemsCount: 'Éléments des connaissances : {n} sur {max} au plus',
    projectItemsFull: 'Les connaissances de ce projet ont atteint leur limite de {max} éléments : supprimez-en ' +
      'pour en ajouter d\'autres.',
    replayImagesFull: 'Images de cette question : {n} ; places restantes dans le contexte de cette discussion : ' +
      '{left} sur {max}. Les images des échanges les plus anciens quittent le contexte une fois que la discussion ' +
      'les résume.',
    imagePlaces: 'places d\'images restantes dans le contexte : {left} sur {max}',
    imageCountTitle: 'Images dans le contexte de cette discussion, celles en attente d\'envoi comprises : {n} sur ' +
      '{max} au plus',
    imageOpen: 'Ouvrir l\'image {name}', removeImage: 'Retirer cette image', previousImage: 'Image précédente',
    nextImage: 'Image suivante',
    imageOutside: 'Hors du contexte : cet échange a été résumé ou écarté, et le modèle ne reçoit plus cette image.',
    imageMissing: 'Cette image n\'est pas disponible dans ce navigateur.',
    imageUnreadable: 'Ce navigateur ne peut pas lire {name} comme une image. Enregistrez-la en PNG ou en JPEG, ' +
      'puis joignez-la de nouveau.',
    imagesNamed: 'Le modèle de cette fonction ({model}) ne lit que du texte : il reçoit les images de cette ' +
      'discussion sous la forme de leurs noms.',
    pastedImage: 'Image collée {date}',
    projectImages: 'Images', projectImagesCount: 'Images du projet : {n} sur {max} au plus',
    knowledgeImagesHelp: 'Le modèle livré avec Albireo décrit chaque image dans la langue de l\'interface : le type ' +
      'd\'image, son texte visible, ses éléments et ses données. La description rejoint la recherche du projet, et ' +
      'un modèle qui lit les images reçoit aussi l\'image quand une question trouve sa description.',
    imageDescribing: 'Description en cours…', imageDescribed: 'Description prête',
    imageDescriptionFailed: 'Échec de la description',
    imageDescriptionFailedHint: 'Le modèle n\'a pas pu décrire cette image : son nom tient lieu de description ' +
      'jusqu\'à une nouvelle tentative, faite dès que le modèle est disponible ou avec le bouton Réessayer.',
    found_image: 'Dans l\'image « {name} »',
    scanOffer: '{name} ne contient que des images, comme des pages numérisées, et le modèle de cette fonction lit ' +
      'les images : ses pages peuvent être jointes comme images. Pages : {pages} ; places d\'images restantes dans ' +
      'le contexte de cette discussion : {left}.',
    scanFirstPages_one: 'Seule la page 1 est jointe.',
    scanFirstPages_other: 'Les pages jointes vont de la page 1 à la page {n}.',
    scanAttach: 'Joindre les pages comme images',
    scanNoRoom: '{name} ne contient que des images, comme des pages numérisées. Ses pages pourraient être jointes ' +
      'comme images, mais le contexte de cette discussion n\'a plus de place d\'image ({max} au plus). Les images ' +
      'des échanges les plus anciens quittent le contexte une fois que la discussion les résume.',
    scanPageName: '{name}, page {n}', scanRendering: 'Rendu des pages de {name}…',
    stepsCount_one: '{n} étape', stepsCount_other: '{n} étapes', agentLimit: 'L\'agent s\'est arrêté après {n} étapes.',
    aboutText: 'Albireo Community Edition exécute des modèles de langage localement, sur le processeur et le processeur graphique de ce Mac, en reposant sur la bibliothèque open source Dlib-Transformer-extensions.',
    createdBy: 'Créé par {author}', changelog: 'Notes de version', home: 'Accueil : nouvelle discussion, saisie effacée', forkedBy: 'Fork pour macOS (Apple Silicon), modifié par TortueTech', forkShort: 'fork macOS', license: 'Licence',
    thirdParty: 'Composants tiers', apiDocs: 'Documentation de l\'interface', close: 'Fermer', confirm: 'Confirmer',
    licensesTitle: 'Composants et licences',
    licensesIntro: 'Albireo fonctionne sans réseau : les bibliothèques, les programmes et les modèles sur lesquels ' +
      'il repose sont installés avec lui. Les voici, avec leurs versions et leurs licences.',
    licensesWeb: 'Application web',
    licensesWebHelp: 'Les bibliothèques de cette interface, servies par le service local.',
    licensesPrograms: 'Programmes et moteurs',
    licensesProgramsHelp: 'Les composants du service et les environnements d\'exécution des moteurs, tels que ' +
      'l\'installation les fournit.',
    licensesProgramsNone: 'Les textes des licences des programmes et des moteurs sont fournis avec l\'installation. ' +
      'Cette copie d\'Albireo n\'en contient pas, comme une version de développement.',
    licensesModels: 'Modèles',
    licensesModelsHelp: 'Chaque modèle est distribué sous la licence de son éditeur, telle que l\'indique son entrée ' +
      'du catalogue.',
    licensesModelsNone: 'Aucun modèle n\'est installé.', licenseComponent: 'Composant', licenseVersion: 'Version',
    licenseRole: 'Rôle', licenseName: 'Licence', licenseModel: 'Modèle', licensePublisher: 'Éditeur',
    licenseFunctions: 'Fonctions', licenseShow: 'Texte de la licence', licenseLoading: 'Chargement…',
    licenseReadFailed: 'Le texte de la licence n\'a pas pu être lu.', licenseSource: 'Source',
    licenseNotStated: 'Non indiquée : voir la source du fichier', licensePublicDomain: 'Domaine public',
    component_marked: 'Met en forme le Markdown des réponses',
    component_dompurify: 'Retire des réponses le HTML qui pourrait exécuter du code',
    component_highlight: 'Colore le code des réponses', component_katex: 'Affiche les formules mathématiques',
    component_pdfjs: 'Lit le texte des documents PDF',
    component_cmaps: 'Donne le texte des polices chinoises, japonaises et coréennes des documents PDF',
    component_swagger: 'Affiche la documentation interactive de l\'interface',
    program_albireo: 'Le service, l\'icône de la zone de notification et cette interface',
    program_dlib: 'Moteur d\'inférence et bibliothèque des modèles',
    program_mbedtls: 'Chiffrement du serveur HTTPS local, et signatures',
    program_httplib: 'Serveur web local et téléchargements', program_zlib: 'Compression des données',
    program_sqlite: 'Base de données du service',
    program_libpng: 'Lecture des images PNG par les moteurs',
    program_libjpeg: 'Lecture des images JPEG par les moteurs',
    program_pico: 'Lecture des réponses à voix haute, avec ses voix de six langues',
    program_runtime: 'Environnement d\'exécution d\'un moteur',
    errorPrefix: 'Erreur : {message}', retry: 'Réessayer', cancelled: 'Interrompu.', emptyAnswer: '(aucune réponse)',
    filesInProject_one: '{n} document', filesInProject_other: '{n} documents', characters_one: '{n} caractère',
    characters_other: '{n} caractères', newProjectDiscussion: 'Nouvelle discussion dans {name}',
    moveTo: 'Déplacer vers', none: 'Aucun', searchIn: 'Rechercher dans les discussions', share: 'Partager', addToProject: 'Ajouter à un projet',
    suggest_general_1: 'Expliquer une notion|Comment fonctionne la cryptographie à clé publique, avec une analogie simple ?',
    suggest_general_2: 'Rédiger|Rédige un courriel clair et courtois pour reporter une réunion à la semaine prochaine.',
    suggest_general_3: 'Résumer|Résume le document joint en cinq points clés.',
    suggest_general_4: 'Planifier|Prépare un plan de deux semaines pour apprendre les bases des statistiques.',
    suggest_code_1: 'Relire du code|Relis cette fonction et signale les bogues et les constructions risquées.',
    suggest_code_2: 'Écrire une fonction|Écris une fonction C++14 qui analyse une date ISO 8601.',
    suggest_code_3: 'Expliquer une erreur|Explique cette erreur de compilation et comment la corriger.',
    suggest_code_4: 'Écrire des tests|Écris des tests unitaires couvrant les cas limites d\'un découpeur de chaînes.',
    suggest_security_1: 'Analyser un journal|Analyse ces journaux d\'authentification et repère les activités suspectes.',
    suggest_security_2: 'Durcir|Liste les étapes de durcissement d\'un poste Windows 11 de développement.',
    suggest_security_3: 'Modèle de menaces|Construis le modèle de menaces d\'une application web exposant une interface REST.',
    suggest_security_4: 'Incident|Rédige une liste de contrôle de réponse à une infection par rançongiciel.'
  }
};

// Le fork pour macOS ne parle que français.
export const languages = [{ code: 'fr', name: 'Français' }];

let current = 'fr';

export function detectLanguage() {
  return 'fr';
}

// Le fork pour macOS ne parle que français : l'interface est en français quelle que soit la langue demandée.
export function setLanguage(code) {
  current = 'fr';
  document.documentElement.lang = current;
}

export function language() { return current; }

// Le texte s avec la valeur de chaque variable de vars à la place de son nom entre accolades.
function fillIn(s, vars) {
  if (vars) for (const k of Object.keys(vars)) s = s.split('{' + k + '}').join(String(vars[k]));
  return s;
}

export function t(key, vars) {
  return fillIn(texts.fr[key] || key, vars);
}

// Les règles de pluriel des langues, créées à leur première utilisation.
const pluralRules = {};

/*
    Le texte de key qui s'accorde avec le nombre n, ses variables remplies comme t les remplit, n comme {n} sauf si
    vars donne {n} lui-même, mis en forme par exemple.  Le texte est la variante de key pour la catégorie de pluriel
    de n dans la langue de l'interface (Intl.PluralRules) : key_one, key_other, puis key_other pour une catégorie
    pour laquelle la langue n'a pas de variante (le « many » des millions exacts en français), puis key lui-même.
*/
export function tn(key, n, vars) {
  for (const code of ['fr']) {
    const own = texts[code] || {};
    if (!pluralRules[code]) pluralRules[code] = new Intl.PluralRules(code);
    const category = pluralRules[code].select(Number(n));
    for (const k of [key + '_' + category, key + '_other', key]) {
      if (own[k]) return fillIn(own[k], Object.assign({ n }, vars));
    }
  }
  return key;
}
