// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    L'onglet « Service » des paramètres : chaque réglage du service que la page peut changer, avec son
    effet en une ligne et, derrière son point d'interrogation, une fiche à la manière du glossaire de
    TortueTech : le principe, les avantages et les inconvénients, un schéma, puis trois explications,
    normale, très technique (le hibou) et pour un ado (la marmotte), et ce qui arrive si on le pousse
    trop loin ou qu'on s'en passe (l'abeille).

    Les réglages « en direct » s'appliquent dès qu'on les change ; ceux qui demandent un redémarrage du
    service attendent le bouton « Appliquer et redémarrer », qui les envoie ensemble.  Les textes sont
    en français seulement : cette version est personnelle.
*/

/*
    Les réglages du service, un par un : titre, contrôle, effet, principe, avantages, inconvénients,
    et fiches hibou/marmotte/abeille.  Le dictionnaire `items` est consommé par web/js/service.js
    (ordre des groupes, panneau de l'onglet « Service »).  Les entiers manipulés ici sont des
    jetons et des méga-octets ; `formatTokens` et `formatMb` les affichent.
*/

const contexts = [0, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144];
const kvSizes = [0, 512, 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384];

function formatTokens(n) {
  return n >= 1024 ? (n / 1024) + ' K tokens' : n + ' tokens';
}

function formatMb(n) {
  return n >= 1024 ? (n / 1024).toLocaleString('fr-FR') + ' Go' : n + ' Mo';
}

export const items = {
  accelerator: {
    title: 'GPU du Mac',
    control: { kind: 'switch', on: 'auto', off: 'none' },
    restart: true,
    effect: 'Allumé : écriture environ 15 % plus rapide et lecture des documents plus de deux fois plus rapide.',
    principle: "Le moteur confie les gros calculs au GPU de la puce Apple (Metal), et garde le processeur pour le reste.",
    pros: ['Écrit plus vite : 21 à 22 tokens/s au lieu de 18 à 19 pour le modèle de 4B, sur un M2.',
      'Lit les prompts de 24 tokens ou plus deux à trois fois plus vite, à toutes les longueurs.',
      'Contrôlé à chaque chargement : au moindre écart, le moteur repasse seul au processeur.'],
    cons: ['Pas identique au dernier chiffre près au processeur (écart de 0,04 % à quelques % sur les scores, même mot choisi).',
      'Environ 200 Mo de mémoire en plus pour ses tampons.',
      'Seuls les poids en Q4_K et Q6_K y passent (aussi Q5_K, Q8_0 et Q5_0 pour Qwen 3.5, LFM2 et les mélanges d’experts, et les petites matrices restées en floats de Qwen 3.6 35B-A3B).',
      'Restent sur le processeur : Gemma 3 1B (largeur de 1 152), Gemma 4 26B-A4B (ses experts), le modèle de recherche, ' +
        'et les passes qui lisent une image. Depuis la 6.6.0, les prompts de tous les mélanges d’experts que le GPU accepte (Qwen 3, Qwen 3.6, Granite, Mixtral, LFM2) y passent aussi.'],
    normal: "Le Mac a deux moteurs de calcul sur la même puce : le processeur, polyvalent, et le GPU, fait pour répéter " +
      "la même opération sur des milliers de nombres à la fois. Écrire une réponse, c’est relire tout le modèle pour " +
      "chaque mot : le GPU lit cette mémoire plus vite. Lire un long document, c’est surtout multiplier : le GPU calcule " +
      "plus. Le moteur l’emploie pour ces deux travaux et vérifie au chargement qu’il donne les mêmes réponses.",
    owl: "Le moteur ouvre un MTLDevice et compile ses noyaux Metal au démarrage (newLibraryWithSource). Les poids du " +
      "fichier GGUF sont pris sans copie (newBufferWithBytesNoCopy sur la projection mmap alignée sur les pages), de même " +
      "que la zone des clés et valeurs. Un pas d’écriture (1 à 8 tokens) est encodé en une seule commande : matvec Q4_K/Q6_K " +
      "en simdgroups, RMSNorm, RoPE, écriture des clés et valeurs q8 tournées (Walsh-Hadamard), attention en ligne par morceaux de 256 " +
      "positions puis fusion. Un prompt de 24 tokens ou plus décompresse chaque matrice en float16, convertit ses entrées en float16, puis passe par " +
      "MPSMatrixMultiplication (résultat en float32, sur les unités matricielles du GPU ; avant la 6.7.0, tout en float32, 2 à 3 fois plus lent) (les experts des mélanges d’experts par produits groupés, matmul_id) ; son attention multiplie des " +
      "matrices 8 × 8 (simdgroup_matrix), les têtes de 512 de Gemma 4 en deux tranches de 256 pour tenir dans les 32 Ko " +
      "de mémoire de threadgroup. Au chargement, une sonde de 29 tokens et deux pas sont comparés au processeur en " +
      "produits flottants : logits à 1 % près (5 % en clés 8 bits, 25 % pour un mélange d’experts en 8 bits), même " +
      "argmax, sinon repli.",
    marmot: "Imagine une cuisine avec un chef et une brigade de commis. Le chef (le processeur) sait tout faire, mais " +
      "un à la fois. Les commis (le GPU) épluchent mille carottes en même temps. Comme ils partagent le même plan de " +
      "travail, personne ne transporte rien : on leur donne les carottes, et le repas est prêt plus vite.",
    bee: "Coupé, tout repasse sur le processeur : les réponses restent justes, mais arrivent un peu moins vite, et un long " +
      "document met plus de deux fois plus de temps à être lu. Utile pour comparer, ou si le contrôle du chargement " +
      "signale un souci dans le journal du service."
  },
  threads: {
    title: 'Threads du moteur',
    control: { kind: 'range', values: (env) => [0].concat(Array.from({ length: env.logical }, (_, i) => i + 1)),
      label: (v, env) => v === 0 ? 'Auto (' + env.cores + ')' : String(v) },
    restart: true,
    effect: 'Combien de cœurs du processeur le moteur occupe. Auto : tous les cœurs physiques.',
    principle: 'Le moteur partage les calculs du processeur entre plusieurs threads, un par cœur. Plus de threads, plus de calcul en parallèle, jusqu’au nombre de cœurs.',
    pros: ['Auto donne le meilleur résultat sur un Mac qui ne fait que ça.',
      'Moins de threads laisse des cœurs libres pour le reste (visio, montage, compilation).'],
    cons: ['Moins de threads : lecture des prompts sur le processeur plus lente, presque en proportion.',
      'Au-delà du nombre de cœurs, aucun gain : les threads se gênent.'],
    normal: "Le moteur lance un thread par cœur. Le processeur du M2 a huit cœurs : quatre rapides (performance) et " +
      "quatre économes (efficacité). Pour écrire un token, il n’y emploie que l’équipe rapide, car attendre les cœurs lents " +
      "ferait perdre du temps ; pour lire un long prompt sur le processeur, il les prend tous. Le Mac mini M5 Pro a dix-huit " +
      "cœurs tous rapides (six « Super », douze « Performance ») : il n’y a pas d’équipe rapide, et Auto (18) est le meilleur " +
      "choix mesuré, en lecture comme en écriture (devant 14, 12, 10 et 6). Avec le GPU allumé, ce réglage compte moins : " +
      "le GPU fait la plus grosse part.",
    owl: "La thread pool d’Gulliver crée N threads (0 = cœurs physiques, sysctl hw.physicalcpu) qui attendent en spin " +
      "2 ms au plus entre deux passes. Sur Apple Silicon, les passes de moins de 24 tokens tournent sur une « fast team » " +
      "limitée à hw.perflevel0.physicalcpu threads (cœurs P), pour éviter que la barrière de fin de passe n’attende un " +
      "cœur E — seulement si hw.perflevel1.name vaut « Efficiency » (Gulliver 6.4.0 : sur un M5 Pro, Super + Performance, " +
      "l’équipe de 6 écrivait à 47 tokens/s contre 65 sur les 18 cœurs, 4B Thinking). Les produits des prompts sur le processeur passent par Accelerate (cblas_sgemm sur le coprocesseur " +
      "matriciel), alimenté par les threads qui décompressent les poids par tranches de 256 lignes.",
    marmot: "C’est comme le nombre de caisses ouvertes au supermarché. Plus il y en a, plus la file avance, mais il " +
      "n’y a que huit caissiers dans le magasin du M2 (dix-huit dans celui du Mac mini) : en ouvrir plus ne sert à rien. Et si tu en fermes quelques-unes, les " +
      "caissiers libres peuvent ranger les rayons pendant ce temps.",
    bee: "Trop bas, le moteur se traîne, surtout pour lire de longs documents sans GPU. Trop haut, les threads se " +
      "disputent les cœurs et le Mac devient moins réactif sans aller plus vite."
  },
  max_sequences: {
    title: 'Questions traitées ensemble',
    control: { kind: 'range', values: () => Array.from({ length: 16 }, (_, i) => i + 1), label: (v) => String(v) },
    restart: true,
    effect: 'Combien de conversations un modèle avance en même temps. 4 par défaut.',
    principle: 'Le moteur regroupe plusieurs questions dans le même passage du modèle : relire les poids une fois sert à toutes.',
    pros: ['Plusieurs onglets ou appareils obtiennent leur réponse en même temps, sans attendre leur tour.',
      'Le débit total augmente : un passage pour plusieurs questions coûte à peine plus qu’un seul.'],
    cons: ['Chaque question occupe sa propre mémoire de contexte : plus de questions, plus de mémoire réservée.',
      'Chacune avance un peu moins vite quand elles sont nombreuses.'],
    normal: "Quand deux questions arrivent en même temps, le moteur peut les faire avancer ensemble : à chaque passage, " +
      "il écrit un mot pour chacune. Le plus cher, relire tout le modèle, n’est payé qu’une fois. Au-delà du nombre " +
      "réglé, les questions suivantes attendent leur tour. Seul sur ton Mac, 2 à 4 suffisent largement.",
    owl: "max_sequences borne le nombre de séquences actives par modèle dans l’ordonnanceur (1 à 64). Les pas de " +
      "génération de plusieurs séquences forment un forward_batch : chaque matvec traite T tokens d’un coup (jusqu’à " +
      "8 sur le GPU, metal_device::max_tokens), donc le coût mémoire-bande des poids est amorti sur T. Chaque séquence " +
      "possède ses blocs de clés et valeurs dans le kv_pool ; le budget kv_mb est partagé entre elles.",
    marmot: "Pense à un bus plutôt qu’à un taxi. Le trajet coûte presque pareil avec un ou quatre passagers, donc on " +
      "en prend plusieurs. Mais chaque passager a besoin de son siège : un bus de seize places prend plus de place " +
      "au garage.",
    bee: "À 1, une seconde question (un autre onglet, le téléphone) attend que la première soit finie. Trop haut, la " +
      "mémoire réservée aux contextes est partagée en petits morceaux, et un long document peut ne plus tenir."
  },
  context: {
    title: 'Contexte des modèles',
    control: { kind: 'range', values: () => contexts, label: (v) => v === 0 ? 'Auto (celui du modèle)' : formatTokens(v) },
    restart: true,
    effect: 'La longueur de texte qu’un modèle garde en tête. Auto : celle de son entraînement, réduite si la mémoire manque.',
    principle: 'Le contexte est la fenêtre de tokens que le modèle relit pour écrire le mot suivant : la conversation, les documents, sa réponse.',
    pros: ['Plus grand : de longs documents et de longues conversations sans oubli.',
      'Plus petit : moins de mémoire, et un démarrage plus léger.'],
    cons: ['Plus grand : plus de mémoire pour les clés et valeurs, et une écriture qui ralentit quand le texte s’allonge.',
      'Plus petit : le modèle oublie le début des longues conversations, ou refuse un document trop long.'],
    normal: "Un modèle ne lit pas un livre : il voit une fenêtre de texte, mesurée en tokens (des morceaux de mots). " +
      "Tout ce qui est dans la fenêtre compte pour la réponse ; ce qui en sort est oublié. En auto, le moteur prend la " +
      "fenêtre prévue par le modèle et la réduit d’elle-même si la mémoire du Mac ne peut pas la contenir.",
    owl: "context = 0 prend n_ctx_train du GGUF, puis le moteur le ramène à kv_bytes_budget / kv_bytes_per_token " +
      "(journal : « context reduced from … to … »). Les clés et valeurs coûtent 2 × couches × têtes KV × head_dim × " +
      "octets par token : pour le 4B du service (36 couches, 8 têtes KV de 128) en q8, environ 76 Ko par token, " +
      "0,3 Go pour 4 096 tokens ; sur un Mac de 24 Go, l’auto lui donne 250 112 tokens sur 262 144. L’attention de chaque pas lit toutes " +
      "les clés : coût linéaire en longueur pour l’écriture, quadratique pour la lecture d’un prompt. Sur le GPU, les " +
      "prompts passent jusqu’à la longueur fixée par le réglage « GPU : prompts jusqu’à » (par défaut, celle du modèle).",
    marmot: "C’est la taille de ton bureau. Sur un petit bureau, tu ne poses que deux pages : pour lire la troisième, " +
      "tu en ranges une et tu l’oublies. Un grand bureau garde tout le dossier ouvert, mais il prend de la place dans " +
      "la pièce et tu mets plus longtemps à tout parcourir des yeux.",
    bee: "Trop petit, le modèle oublie le début de la conversation ou refuse les documents longs. Trop grand pour la " +
      "mémoire, le moteur le réduit de lui-même au chargement : le réglage n’est alors pas tenu, et le journal le dit."
  },
  kv_mb: {
    title: 'Mémoire des clés et valeurs',
    control: { kind: 'range', values: () => kvSizes, label: (v) => v === 0 ? 'Auto' : formatMb(v) },
    restart: true,
    effect: 'La mémoire réservée aux contextes des conversations. Auto : deux contextes complets, avec une réserve pour macOS.',
    principle: 'Pour ne pas tout recalculer à chaque mot, le modèle garde des « clés et valeurs » pour chaque token déjà lu. Ce réglage fixe leur budget total.',
    pros: ['Plus : des contextes plus longs, plus de conversations gardées en mémoire pour reprendre vite.',
      'Moins : plus de mémoire libre pour les autres applications.'],
    cons: ['Plus : le Mac peut manquer de mémoire et ralentir (swap).',
      'Moins : contextes réduits au chargement, conversations relues depuis le début plus souvent.'],
    normal: "Chaque token lu laisse une petite fiche dans la mémoire du moteur : ce sont les clés et valeurs. Elles " +
      "permettent d’écrire le mot suivant sans relire tout le texte depuis le début. Ce réglage dit combien de mémoire " +
      "leur donner. En auto, le moteur prend de quoi tenir deux contextes complets tout en laissant de la marge au système.",
    owl: "kv_mb = 0 : budget = 2 contextes dans la mémoire engageable (sur macOS, la mémoire physique), moins une " +
      "réserve ; sinon kv_mb mégaoctets. Le kv_pool réserve une seule région mmap pour tous ses blocs (pages alignées), " +
      "engagée et rendue par madvise (MADV_FREE_REUSABLE / REUSE) : seuls les blocs utilisés coûtent de la mémoire " +
      "réelle. Le GPU lit cette même région sans copie. Le contexte de chaque modèle est plafonné à ce que le budget peut " +
      "tenir (18,7 Go de clés et valeurs pour le 4B, sur un Mac de 24 Go).",
    marmot: "C’est la taille de ton classeur de notes. Un gros classeur garde les notes de toutes tes matières, tu " +
      "retrouves vite où tu en étais. Mais il prend de la place dans ton sac, et il en reste moins pour le reste.",
    bee: "Trop gros, le Mac commence à échanger avec le disque et tout ralentit, y compris les autres applications. " +
      "Trop petit, les longs documents ne tiennent plus et le moteur réduit le contexte au chargement."
  },
  kv_type: {
    title: 'Format des clés et valeurs',
    control: { kind: 'choice', options: [['auto', 'Auto'], ['q8', '8 bits'], ['f16', '16 bits']] },
    restart: true,
    effect: 'Auto : 8 bits pour les modèles de langue, presque deux fois moins de mémoire pour le même contexte.',
    principle: 'Les clés et valeurs peuvent être rangées en demi-précision (16 bits) ou en entiers de 8 bits avec une échelle par groupe de 32.',
    pros: ['8 bits : presque moitié moins de mémoire, donc des contextes deux fois plus longs.',
      '16 bits : aucun arrondi supplémentaire.'],
    cons: ['8 bits : un arrondi minuscule, qui peut changer un mot dans une très longue réponse.',
      '16 bits : deux fois plus de mémoire, contexte plus court à mémoire égale.'],
    normal: "Chaque fiche des clés et valeurs est une liste de nombres. En 16 bits, on les garde presque tels quels. " +
      "En 8 bits, on les arrondit, mais intelligemment : par petits groupes, avec une échelle pour chacun. La différence " +
      "de qualité est à peine mesurable, et la mémoire gagnée permet de lire des documents deux fois plus longs.",
    owl: "q8 : groupes de 32 valeurs en int8 plus une échelle en half (34 octets contre 64 en f16). Les clés et les " +
      "valeurs sont d’abord tournées (signes pseudo-aléatoires puis transformée de Walsh-Hadamard, qk_rotate_row) pour " +
      "étaler les valeurs extrêmes avant l’arrondi ; les requêtes subissent la même rotation, ce qui laisse les produits " +
      "scalaires inchangés, et la sortie de l’attention, moyenne des valeurs tournées, est remise en place " +
      "(qk_unrotate_row), comme le fait llama.cpp. Auto choisit q8 pour les modèles de langue dont la tête contient des groupes entiers de 32, f16 pour " +
      "les modèles de recherche. Noyaux NEON et Metal pour les deux formats ; le contrôle du GPU tolère 5 % sur un prompt en q8.",
    marmot: "C’est comme une photo en haute qualité ou bien compressée. Sur ton téléphone, tu ne vois presque pas la " +
      "différence, mais la photo compressée prend deux fois moins de place, et tu peux en garder deux fois plus.",
    bee: "En 16 bits, un long document peut ne plus tenir dans la mémoire réservée et le contexte est réduit. En 8 bits, " +
      "une réponse très longue peut, rarement, prendre un autre tournant qu’en 16 bits, sans être moins juste."
  },
  lan: {
    title: 'Accès depuis le réseau',
    control: { kind: 'switch', on: true, off: false },
    restart: true,
    effect: 'Ouvre la page aux appareils de la maison (http, port réseau ci-dessous), sans mot de passe.',
    principle: 'Le service ouvre une seconde porte, sur l’adresse du Mac dans ton réseau, pour le téléphone ou la tablette.',
    pros: ['Gulliver depuis le canapé, sur le téléphone ou la tablette, sans rien installer.',
      'Le Mac fait tout le calcul : le téléphone n’affiche que la page.',
      'Chaque profil retrouve ses discussions sur tous les appareils.'],
    cons: ['En http : le trafic n’est pas chiffré sur le Wi-Fi (le mot de passe et le code y passent en clair).',
      'Pas de dictée au micro sur le téléphone (elle exige https).'],
    normal: "Normalement, la page n’est visible que sur le Mac lui-même. Allumé, ce réglage ouvre une seconde entrée " +
      "pour les autres appareils de ton réseau. Chacun s’y connecte avec son adresse mail, son mot de passe et le code " +
      "reçu par mail, et retrouve ses discussions, rangées sur le Mac. Seul l’administrateur y change les modèles et " +
      "les réglages. Le calcul, lui, se fait toujours sur le Mac.",
    owl: "Un second serveur httplib (HTTP, sans TLS) écoute sur 0.0.0.0:lan_port avec les mêmes routes. Les données " +
      "exigent une session (X-Gulliver-Session) ; les actions qui changent le service (require_manager), celle de " +
      "l’administrateur. Le serveur ne répond qu’aux appareils qui peuvent joindre le Mac : ceux du réseau local, " +
      "sauf si la box redirige ce port depuis Internet (elle ne le fait pas d’elle-même).",
    marmot: "C’est une deuxième porte d’entrée, côté jardin. Chacun a sa clé (son mot de passe) et reçoit un code " +
      "par mail pour entrer, mais la porte est en verre : un voisin bricoleur sur le même Wi-Fi peut voir passer ce " +
      "qu’on tape.",
    bee: "Sur un Wi-Fi partagé ou public, ne l’allume pas : le trafic n’y est pas chiffré, et un autre appareil du " +
      "réseau pourrait lire une discussion ou un mot de passe au passage."
  },
  lan_port: {
    title: 'Port réseau',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'Le numéro de la porte réseau. L’adresse à taper sur le téléphone s’affiche plus bas.',
    principle: 'Une adresse réseau a des « ports », comme un immeuble a des portes numérotées. Celui-ci sert aux autres appareils.',
    pros: ['Séparé du port du Mac : on peut couper l’un sans toucher à l’autre.'],
    cons: ['Changer le port change l’adresse à taper sur chaque appareil.'],
    normal: "Le Mac a une adresse sur ton réseau (par exemple 192.168.1.12), et chaque service y a son numéro de porte. " +
      "Gulliver utilise 8444 par défaut pour les autres appareils. On tape donc « http://adresse-du-mac:8444/ » sur le téléphone.",
    owl: "Entier de 1024 à 65535, différent du port HTTPS du Mac. Le service vérifie qu’il peut lier 0.0.0.0 sur ce port " +
      "avant d’enregistrer (409 port_unavailable sinon). macOS peut demander, au premier lancement, d’autoriser les " +
      "connexions entrantes pour gulliver-service (pare-feu applicatif).",
    marmot: "Ton immeuble a une adresse, et chaque appartement un numéro. Les copains doivent connaître les deux pour " +
      "sonner chez toi. Changer de numéro, c’est leur redonner la nouvelle adresse.",
    bee: "Un port déjà pris par un autre programme est refusé. Si tu le changes, les favoris enregistrés sur les " +
      "appareils ne mènent plus nulle part."
  },
  port: {
    title: 'Port du Mac',
    control: { kind: 'number', min: 1024, max: 65535 },
    restart: true,
    effect: 'L’adresse de la page sur ce Mac (https://localhost:port). La changer change l’adresse de la page.',
    principle: 'Le service écoute sur ce port, seulement pour le Mac lui-même (127.0.0.1), en https.',
    pros: ['Utile seulement si un autre programme veut ce numéro.'],
    cons: ['Les conversations sont rangées par adresse dans le navigateur : à la nouvelle adresse, la page est vide.',
      'Il faut mettre à jour les favoris et les outils qui appellent l’API.'],
    normal: "C’est la porte d’Gulliver sur le Mac lui-même. La page que tu regardes y est servie. Le navigateur range " +
      "les conversations selon l’adresse exacte : en changeant de port, tu arrives sur une page neuve, et les anciennes " +
      "conversations restent à l’ancienne adresse (exporte-les d’abord depuis l’onglet Données).",
    owl: "Portier TLS de Network.framework lié à 127.0.0.1:port (TLS 1.2 au moins), qui passe le HTTP au serveur httplib " +
      "par une prise Unix privée ; certificat émis par l’autorité locale du dossier de données. " +
      "Le stockage de la page (IndexedDB, localStorage) dépend de l’origine https://localhost:port. Le service vérifie " +
      "que le port est libre avant d’enregistrer ; au démarrage, un port pris le fait revenir au dernier port servi.",
    marmot: "C’est le numéro de ta chambre dans un hôtel. Si tu changes de chambre, tes affaires restent dans " +
      "l’ancienne : il faut les déménager avant.",
    bee: "Sans export préalable, tes conversations semblent avoir disparu (elles sont toujours à l’ancienne adresse). " +
      "Un programme ou un script qui parlait à l’ancien port ne trouve plus le service."
  },
  offline: {
    title: 'Hors ligne',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Plus aucune connexion vers Internet : ni mise à jour du catalogue, ni téléchargement réseau, ni recherche web.',
    principle: 'Le service coupe ses connexions sortantes ; les modèles viennent de la bibliothèque du Mac, des imports ou de la réserve locale.',
    pros: ['Rien ne sort du Mac : confidentialité totale.', 'Aucun téléchargement surprise.'],
    cons: ['Pas de modèles qui ne sont pas déjà sur le Mac, pas de bouton Web.',
      'Un téléchargement en cours s’arrête (sa progression est gardée).'],
    normal: "Gulliver calcule déjà tout sur le Mac. Il ne va sur Internet que pour trois choses : le catalogue des " +
      "modèles, leur téléchargement, et la recherche du bouton Web. Hors ligne, il n’y va plus du tout. On peut " +
      "toujours charger un modèle de la bibliothèque du Mac (« Rafraîchir depuis le Mac », section « Sur ce Mac »), " +
      "importer un modèle comme fichier, ou restaurer celui dont tous les fichiers sont vérifiés dans la réserve.",
    owl: "offline = true annule à l’instant les transferts en cours (http_cancel_all, raison « offline mode »), refuse " +
      "les routes qui ont besoin du réseau (refuse_offline : 409, code offline) et ferme " +
      "l’accès web (403, code offline). Les restaurations de la réserve vérifient taille et SHA-256, sans aucun repli " +
      "réseau, même si le réglage change pendant le travail. Le dossier d’import et la bibliothèque du Mac " +
      "(GET /gulliver/v1/library) restent lus. Remis à faux, http_restore() " +
      "rouvre les transferts.",
    marmot: "C’est le mode avion de ton téléphone, mais pour Gulliver. Tout ce qui est déjà sur le Mac marche toujours ; " +
      "seulement, plus rien ne part vers Internet, et rien n’en vient.",
    bee: "Oublié allumé, le catalogue ne se met plus à jour et le bouton Web répond « hors ligne ». Si la réserve " +
      "ne contient pas tous les fichiers vérifiés, il faut les importer ou revenir en ligne."
  },
  web_access: {
    title: 'Bouton Web',
    control: { kind: 'switch', on: true, off: false },
    restart: false,
    effect: 'Permet au modèle de chercher sur le web et de lire des pages pour répondre.',
    principle: 'Le service fait les recherches et lit les pages pour la page, qui les donne ensuite au modèle.',
    pros: ['Des réponses à jour, avec des sources.', 'Le modèle peut lire une adresse que tu lui donnes.'],
    cons: ['Les mots de ta recherche partent vers le site cherché.', 'Plus lent : il faut télécharger puis lire les pages.'],
    normal: "Un modèle ne connaît que ce qu’il a appris jusqu’à une certaine date. Avec le bouton Web, il peut chercher " +
      "et lire des pages, puis répondre en citant ce qu’il a lu. Coupé, il répond seulement avec ce qu’il sait déjà.",
    owl: "Routes POST /gulliver/v1/web/search (1 à 10 résultats, http et https seulement) et /gulliver/v1/web/fetch " +
      "(octets de la page en base64, adresses publiques seulement : les adresses locales et privées sont refusées pour " +
      "éviter de servir de relais vers le réseau de la maison). web_access = false les ferme (403).",
    marmot: "C’est comme autoriser ton assistant à aller à la bibliothèque. Il revient avec des livres récents et te dit " +
      "où il a trouvé l’info. Sans ça, il te répond de mémoire.",
    bee: "Coupé, les questions sur l’actualité reçoivent des réponses datées. Allumé, ce que tu cherches sort du Mac " +
      "vers Wikipédia et les sites lus."
  },
  web_search: {
    title: 'Recherche Wikipédia',
    control: { kind: 'switch', on: 'wikipedia', off: 'none' },
    restart: false,
    effect: 'Le moteur de recherche du bouton Web : Wikipédia, ou aucun.',
    principle: 'Le service interroge Wikipédia dans la langue de la question et rend les articles trouvés.',
    pros: ['Sans clé, sans compte, et des sources de bonne qualité.'],
    cons: ['Seulement ce que Wikipédia couvre : ni actualité fine, ni sites commerciaux.'],
    normal: "Quand le modèle a besoin de chercher, le service pose la question à Wikipédia et rapporte les articles. " +
      "Coupée, la recherche ne marche plus, mais le bouton Web sait toujours lire une adresse précise que tu donnes.",
    owl: "web_search = « wikipedia » ou « none ». Les anciennes valeurs (« auto », « brave », « searxng ») sont ramenées " +
      "à Wikipédia à la lecture des réglages. La recherche passe par l’API publique de Wikipédia, dans la langue de la " +
      "requête (paramètre language), via le proxy éventuel.",
    marmot: "C’est le moteur de recherche que ton assistant utilise : une grande encyclopédie. Pratique et fiable, mais " +
      "il n’y trouvera pas le prix du dernier jeu vidéo.",
    bee: "Coupée, le modèle ne trouve plus rien seul : il faut lui donner les adresses des pages à lire."
  },
  gpu_prompt_tokens: {
    title: 'GPU : prompts à partir de',
    control: { kind: 'range', values: () => [16, 24, 32, 48, 64, 128, 256, 512], label: (v) => v + ' tokens' },
    restart: true,
    effect: 'La longueur à partir de laquelle un prompt est lu par le GPU plutôt que par le processeur.',
    principle: 'Le GPU doit d’abord déplier les poids de chaque couche : cela ne vaut la peine que pour des passes assez longues.',
    pros: ['24 : le seuil mesuré où le GPU devient plus rapide que le processeur sur le M2.',
      'Plus haut : les petites questions restent sur le processeur, qui n’a rien à déplier.'],
    cons: ['Plus bas que 24 : le GPU perd du temps à déplier les poids pour peu de tokens.',
      'Plus haut : les prompts moyens perdent l’accélération du GPU.'],
    normal: "Lire un prompt, c’est multiplier chaque token par tous les poids du modèle. Le GPU le fait très vite, mais il " +
      "doit d’abord décompresser les poids, ce qui prend un temps fixe. Pour une petite question, ce temps fixe coûte " +
      "plus que ce qu’il fait gagner. Ce réglage dit à partir de combien de tokens le GPU prend la main.",
    owl: "Sur le chemin des prompts, chaque matrice Q4_K/Q6_K est décompressée en float32 (dequant_q4_K/q6_K) puis " +
      "multipliée par MPSMatrixMultiplication : environ 200 ms de décompression par passe pour le 2B. Mesures (2B, une " +
      "passe, clés q8) : 12 tokens 85,7 contre 59,5 tokens/s (processeur gagnant), 16 tokens 84,4 contre 77,7, 24 tokens " +
      "63,2 contre 114,3 (GPU gagnant). Le seuil vaut 9 au moins : les passes de 1 à 8 tokens sont des pas d’écriture, " +
      "toujours sur le GPU. 24 coïncide avec qk_float_product_tokens, où le processeur passe lui-même à Accelerate.",
    marmot: "Sortir le grand robot de cuisine pour couper une seule carotte, c’est plus long que de la couper au " +
      "couteau. Pour un sac entier, le robot gagne. Ce réglage dit à partir de combien de carottes on le sort.",
    bee: "Trop bas, les petites questions démarrent plus lentement. Trop haut, les documents moyens sont lus deux fois " +
      "moins vite qu’ils pourraient l’être."
  },
  draft_tokens: {
    title: 'Brouillons de l’écriture',
    control: { kind: 'range', values: () => [0, 1, 2, 3, 4, 5, 6, 7], label: (v) => v === 0 ? 'aucun' : v + (v > 1 ? ' mots' : ' mot') },
    restart: true,
    effect: 'Combien de mots le moteur devine à l’avance, en les reprenant de la conversation, puis vérifie en une seule passe.',
    principle: 'Quand la réponse recopie des mots déjà présents (un document, du code, une citation), le moteur propose la suite ' +
      'et le modèle la vérifie d’un coup : chaque mot juste est un pas d’écriture gagné.',
    pros: ['Recopie, citations, questions sur un document : jusqu’à trois fois plus vite (mesuré sur le Mac mini).',
      'Rien ne change au choix des mots : un mot n’est gardé que si le modèle l’aurait écrit.'],
    cons: ['Une réponse libre, qui ne recopie rien, gagne peu (1 % mesuré).',
      'Sans GPU, la vérification coûte presque autant que les mots gagnés : de −4 % à +4 % sur le processeur.'],
    normal: "Pour écrire, le modèle fait une passe par mot. Or une réponse reprend souvent des morceaux de la conversation : " +
      "le nom d’un fichier, une phrase du document, une ligne de code. Le moteur cherche dans ce qui précède la suite des " +
      "derniers mots écrits et la propose comme brouillon ; une seule passe vérifie tous les mots du brouillon à la fois et " +
      "garde ceux que le modèle aurait choisis. Ce réglage dit la longueur maximale d’un brouillon.",
    owl: "Décodage spéculatif par recherche dans le prompt (prompt lookup), repris de l’Albireo d’origine : les 2 à 4 " +
      "derniers tokens sont cherchés dans les 65 536 précédents, la suite trouvée sert de brouillon, et la passe suivante " +
      "calcule les logits de tous ses tokens ; le tirage accepte chaque token avec sa probabilité (en glouton : s’il est le " +
      "plus probable), puis tire le suivant normalement. Après des échecs répétés, le moteur fait une pause. Une passe de 8 " +
      "tokens passe par mv_nax (accélérateurs du M5, Gulliver 6.22.0) et ne coûte que 1,37 fois une passe d’un token. Mesures " +
      "(4B, glouton, 400 tokens) : 0 / 4 / 7 mots, recopie 78,5 / 192,6 / 245,3 tokens/s ; question sur un texte 76,6 / " +
      "94,6 / 96,3 ; code 79,2 / 86,6 / 85,7 ; résumé 76,3 / 82,4 / 82,5 ; réponse libre 79,3 / 80,1 / 80,0. Le texte est " +
      "identique sauf aux quasi-égalités : les additions d’une passe de plusieurs tokens se font en demi-précision et dans " +
      "un autre ordre (réponse libre : premier écart au caractère 2 335 sur 3 586). Sans effet sur les modèles récurrents.",
    marmot: "C’est comme dicter une lettre à quelqu’un qui connaît déjà le texte : au lieu d’attendre chaque mot, il écrit " +
      "toute la phrase qu’il devine, et tu n’as qu’à dire « oui » d’un coup d’œil. S’il se trompe, tu reprends au mot faux.",
    bee: "À 0, chaque mot coûte une passe, même pour recopier un document mot pour mot : trois fois plus lent. Au maximum " +
      "(7), rien ne se perd sur le GPU : un brouillon faux ne coûte qu’un peu plus qu’une passe ordinaire, et le moteur " +
      "arrête d’en proposer quand ils échouent."
  },
  gpu_prompt_context: {
    title: 'GPU : prompts jusqu’à',
    control: { kind: 'range', values: () => [0, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144],
      label: (v) => v === 0 ? 'jamais (processeur)' : formatTokens(v) },
    restart: true,
    effect: 'La longueur de contexte jusqu’à laquelle le GPU lit les prompts. Au-delà, le processeur reprend.',
    principle: 'Le GPU lit les prompts dont le contexte reste sous cette longueur ; au-delà, le processeur.',
    pros: ['Par défaut, la longueur du modèle : le GPU lit tout, et il gagne à toutes les longueurs mesurées.',
      'Plus bas : seulement pour comparer, ou si une machine donnait l’avantage au processeur sur les longs textes.'],
    cons: ['Plus bas : les longs documents sont lus par le processeur, deux à trois fois plus lentement.',
      '0 : plus aucun prompt sur le GPU, la lecture redevient deux fois plus lente.'],
    normal: "Pour chaque token d’un prompt, le modèle relit tous ceux qui le précèdent. Sur un long document, ce travail " +
      "grossit vite. L’attention du GPU partage chaque tranche de clés entre plusieurs tokens et calcule par petites " +
      "matrices : elle reste plus rapide que le processeur même sur de très longs textes. Ce réglage fixe une frontière, " +
      "par défaut au bout du modèle.",
    owl: "Le chemin des prompts sur le GPU (takes_gpu) exige que la séquence finisse dans gpu_prompt_context positions. " +
      "L’attention de prompt (attention_mtile) prend 32 lignes de requête (16 pour des têtes de 256 valeurs, 8 pour " +
      "celles de 512 de Gemma 4, coupées en deux tranches de 256 dont chacune calcule tous les scores) par tête " +
      "de clés, charge chaque tranche de 32 positions une fois en mémoire de threadgroup (8 valeurs par thread, une " +
      "échelle par groupe) et calcule scores et sorties par simdgroup_matrix 8 x 8 ; la fenêtre glissante de Gemma 3 " +
      "et 4 y est appliquée. Mesures alternées (bench_runtime, " +
      "clés q8, 2B, Gulliver 4.11.0) : 4 096 tokens 423 à 424 tokens/s contre 170 à 177 sur le processeur ; 8 192 tokens " +
      "295 contre 115 à 117 ; 16 384 tokens 171 contre 67.",
    marmot: "C’est comme un livreur qui a appris à charger tout un carton au lieu d’un colis à la fois : même pour les " +
      "longues tournées, il reste le plus rapide. Ce réglage dit jusqu’où on l’envoie ; par défaut, partout.",
    bee: "Trop bas, les longs documents sont lus par le processeur, deux à trois fois plus lentement. À 0, le GPU ne lit " +
      "plus aucun prompt : l’écriture reste rapide, mais la lecture redevient deux fois plus lente."
  },
  spin_us: {
    title: 'Attente active des threads',
    control: { kind: 'range', values: () => [0, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000],
      label: (v) => v === 0 ? 'aucune' : v >= 1000 ? (v / 1000) + ' ms' : v + ' µs' },
    restart: true,
    effect: 'Combien de temps les threads restent éveillés entre deux passes avant de dormir. 2 ms par défaut.',
    principle: 'Un thread qui dort met du temps à se réveiller ; un thread qui attend éveillé repart tout de suite, mais occupe son cœur.',
    pros: ['Plus long : les passes s’enchaînent sans délai de réveil.',
      'Plus court : moins de chaleur et de batterie quand le moteur attend.'],
    cons: ['Plus long : les cœurs tournent à vide entre deux questions, le Mac chauffe un peu.',
      'Aucune : chaque passe paie le réveil des threads, l’écriture ralentit sur le processeur.'],
    normal: "Entre deux étapes de calcul, les threads du moteur peuvent soit attendre éveillés, soit s’endormir. " +
      "Éveillés, ils repartent instantanément ; endormis, ils économisent l’énergie mais mettent un peu de temps à " +
      "redémarrer. Avec le GPU, qui écrit les réponses, ce réglage compte surtout pour la lecture sur le processeur.",
    owl: "thread_pool::set_spin_time : après une section parallèle, chaque worker boucle (ALBIREO_CPU_RELAX, soit " +
      "std::this_thread::yield sur ARM64) jusqu’à spin_us microsecondes, puis attend sur une variable de condition. " +
      "Défaut 2 000 µs, plafond 100 000 µs (au-delà, le processeur resterait occupé entre deux passes). Sur macOS, " +
      "yield fait tomber la priorité du thread : l’attente active coûte peu aux autres programmes.",
    marmot: "C’est la différence entre attendre le bus debout à l’arrêt, prêt à monter, et rentrer s’asseoir au café " +
      "d’à côté. Au café, tu te reposes, mais tu rates parfois le départ.",
    bee: "Trop long, le Mac sans ventilateur chauffe et la batterie descend même quand personne ne pose de question. " +
      "À zéro, les réponses écrites par le processeur ralentissent."
  },
  warm_history: {
    title: 'Préparer la question suivante',
    control: { kind: 'switch', on: true, off: false },
    restart: true,
    effect: 'Après chaque réponse, relit la conversation en arrière-plan pour que la question suivante parte plus vite.',
    principle: 'Pendant que tu lis la réponse, le moteur relit l’historique tel que la prochaine question le présentera.',
    pros: ['La question suivante ne paie que sa propre lecture : réponse plus rapide.'],
    cons: ['Du calcul après chaque réponse, même si tu ne poses plus de question.',
      'Un peu de mémoire pour garder cette relecture.'],
    normal: "À chaque tour, le modèle doit relire toute la conversation. Mais la réponse qu’il a écrite n’est pas " +
      "rangée exactement comme il la relira (les modèles qui raisonnent effacent leur raisonnement des tours passés). " +
      "Le moteur profite donc du temps où tu lis pour préparer cette relecture. Ta question suivante démarre alors " +
      "presque tout de suite.",
    owl: "Quand une réponse de chat finit, son historique est rendu comme la requête suivante le rendra (gabarit de chat, " +
      "raisonnement des tours passés retiré, réponse retokenisée), puis lu en arrière-plan (warm-up, priorité basse). " +
      "Les clés et valeurs restent en cache ; une requête qui commence par les mêmes tokens les reprend. Une requête " +
      "d’un client qui attend interrompt les warm-ups (yield_warm_ups). warm_history = false les supprime.",
    marmot: "C’est comme relire tes notes du cours pendant la pause, pour être prêt quand le prof repose une question. " +
      "Si tu ne l’as pas fait, il faut tout relire au moment de répondre.",
    bee: "Coupé, chaque nouvelle question d’une longue conversation commence par relire tout l’historique : plusieurs " +
      "secondes d’attente avant le premier mot."
  },
  models_dir: {
    title: 'Dossier des modèles',
    control: { kind: 'text', placeholder: 'vide : dans les données du service' },
    restart: true,
    effect: 'Où le service range et cherche ses modèles. Vide : le dossier « models » de ses données.',
    principle: 'Les modèles sont de gros fichiers (2 à 4 Go) : on peut les ranger ailleurs, sur un autre disque.',
    pros: ['Libère le disque du Mac en mettant les modèles sur un disque externe.'],
    cons: ['Les modèles de l’ancien dossier ne sont pas déplacés : il faut le faire dans le Finder.',
      'Disque débranché : plus de modèle au démarrage.'],
    normal: "Le service garde ses modèles dans un dossier. En changer, c’est lui dire de regarder ailleurs : il ne " +
      "déplace rien lui-même. Déplace d’abord les fichiers .gguf dans le nouveau dossier, puis indique-le ici. Le " +
      "dossier doit exister.",
    owl: "models_dir vide : <données>/models (effective_models_dir). Le service vérifie que le dossier existe avant " +
      "d’enregistrer. Les modèles installés sont reconnus par leurs fichiers dans ce dossier ; les poids sont ouverts " +
      "par mmap et pris par le GPU sans copie, donc un disque lent ralentit surtout le chargement.",
    marmot: "C’est le placard où tu ranges tes jeux. Si tu changes de placard, il faut porter les jeux dedans ; sinon " +
      "tu ouvres un placard vide.",
    bee: "Un dossier vide ou un disque débranché, et le service démarre sans modèle : plus de réponses jusqu’à ce que " +
      "les fichiers reviennent."
  },
  library_dir: {
    title: 'Bibliothèque du Mac',
    control: { kind: 'text', placeholder: 'vide : le dossier « library » des données du service' },
    restart: false,
    effect: 'Le dossier de fichiers .gguf que « Rafraîchir depuis le Mac » liste dans l’onglet Modèles, pour les charger sans réseau.',
    principle: 'Des modèles déjà présents sur le Mac, venus d’ailleurs qu’Gulliver, deviennent utilisables même sans Hugging Face.',
    pros: ['Un modèle se charge sans Internet, en quelques secondes : le temps de vérifier son empreinte.',
      'La copie ne prend pas de place sur le même disque (clone APFS), et le fichier reste dans la bibliothèque.',
      'Chaque modèle vient avec son projecteur d’images, quand le dossier le contient.'],
    cons: ['Sur un autre disque que celui des modèles, la copie prend vraiment sa taille.',
      'Le service refuse un dossier que d’autres comptes du Mac peuvent modifier.'],
    normal: "Dépose des fichiers .gguf dans ce dossier avec le Finder, puis clique « Rafraîchir depuis le Mac » dans " +
      "l’onglet Modèles : la section « Sur ce Mac » les liste, avec leur architecture, leur taille et le projecteur " +
      "qui va avec chacun. « Charger dans Gulliver » en fait une copie dans le dossier des modèles. Le service ne " +
      "déplace, ne renomme et n’efface jamais rien dans la bibliothèque. Vide : <données>/library, créé à la première " +
      "lecture. Un autre dossier doit exister et être nommé par son chemin complet.",
    owl: "library_dir vide : <données>/library (effective_library_dir), créé par make_dirs. GET /gulliver/v1/library : " +
      "le dossier doit passer folder_admin_only (propriétaire, permissions POSIX, ACL) ; chaque .gguf du dossier, sans " +
      "sous-dossier, est ouvert par link_free_file (aucun lien, un seul nom) et décrit par describe_gguf ; les " +
      "projecteurs sont appariés par pair_projectors, comme un import. POST /gulliver/v1/library/load {file} : " +
      "model_manager::import_files(fichier et projecteur, move = false), SHA-256 par CommonCrypto, copie par " +
      "fclonefileat. « Déjà dans Gulliver » : un modèle installé de même taille et de même architecture ; l’import " +
      "tranche par l’empreinte.",
    marmot: "C’est l’étagère de ta chambre où tu ranges des jeux que tu as eus ailleurs. Le service regarde l’étagère " +
      "et te montre ce qu’il y a ; si tu choisis un jeu, il en fait une photocopie magique qui ne prend pas de place, " +
      "et laisse l’original sur l’étagère.",
    bee: "Un dossier que n’importe quel compte du Mac peut modifier, et le service ne le lit plus : quelqu’un pourrait " +
      "y glisser un faux modèle. Un dossier sur une clé USB débranchée, et la liste est vide."
  },
  reserve_gb: {
    title: 'Réserve des modèles',
    control: { kind: 'range', values: () => [0, 50, 100, 200, 300, 500, 1000],
      label: (v) => v === 0 ? 'Rien de plus' : v.toLocaleString('fr-FR') + ' Go' },
    restart: false,
    effect: 'La place que peuvent prendre, sur le Mac, les modèles supprimés gardés pour revenir sans réseau.',
    principle: 'Chaque modèle téléchargé est gardé dans la réserve ; le réinstaller devient une copie locale, sans Internet.',
    pros: ['Réinstaller un modèle prend quelques secondes, sans réseau, même si Hugging Face est en panne.',
      'Tant qu’un modèle reste installé, sa copie de réserve ne prend aucune place (un clone APFS).'],
    cons: ['Les modèles supprimés continuent d’occuper le disque, jusqu’au plafond.',
      'Plafond baissé : les copies les plus anciennes partent aussitôt.'],
    normal: "Quand un modèle finit de se télécharger, le service en garde une copie dans le dossier « reserve » de ses " +
      "données. Quand tu supprimes le modèle, la copie reste : le réinstaller la reprend sur le Mac, sans Internet. Le " +
      "plafond ne compte que les copies des modèles supprimés ; au-delà, les moins récemment utilisées partent d’abord. " +
      "« Vider la réserve » retire toutes les copies.",
    owl: "<données>/reserve/<sha256>.gguf, dossier 0700, fichiers 0600. La copie est un clone APFS (fclonefileat) : " +
      "les blocs sont partagés avec le fichier du modèle tant qu’aucun des deux ne change. Chaque copie est relue et " +
      "hachée (SHA-256 par CommonCrypto). Restauration fichier par fichier : en ligne, seuls les fichiers absents ou " +
      "abîmés se téléchargent ; une copie abîmée est retirée. Plafond (reserve_gb) : somme des copies dont l’empreinte " +
      "n’est plus celle d’un modèle installé ; tri par mtime, rafraîchie à chaque restauration (trim_reserve).",
    marmot: "C’est comme ranger un jeu que tu n’utilises plus dans un carton au grenier au lieu de le jeter : si tu " +
      "le veux de nouveau, tu remontes le chercher au lieu de le racheter. Le plafond, c’est la taille du grenier.",
    bee: "À 0, rien n’est gardé : un modèle supprimé se retélécharge en entier (2 à 3 Go). Trop haut, le disque se " +
      "remplit de modèles oubliés sans que tu le voies : regarde la place utilisée sous ce réglage."
  },
  proxy: {
    title: 'Proxy',
    control: { kind: 'text', placeholder: 'vide, none, ou http://hôte:port' },
    restart: false,
    effect: 'Un relais pour sortir sur Internet, si ton réseau l’exige. Vide le plus souvent.',
    principle: 'Toutes les connexions du service vers Internet passent par ce relais.',
    pros: ['Indispensable sur certains réseaux d’entreprise ou d’école.'],
    cons: ['Mal réglé, plus rien ne passe : catalogue, téléchargements et recherche échouent.'],
    normal: "Sur certains réseaux, on ne sort pas directement sur Internet : on passe par un relais, le proxy. Vide, " +
      "le service suit les variables du système (HTTPS_PROXY), et sinon sort en direct. « none » force le direct.",
    owl: "Vide : HTTPS_PROXY / HTTP_PROXY / ALL_PROXY de l’environnement du service (pas les réglages de proxy des " +
      "Réglages Système de macOS), NO_PROXY respecté, localhost toujours en direct. « none » : jamais de proxy. " +
      "Sinon http://[utilisateur:mot-de-passe@]hôte:port ; le mot de passe est rendu masqué (********) et gardé à part " +
      "dans proxy.secret. Appliqué à chaud (set_http_proxy).",
    marmot: "C’est comme passer par l’accueil d’un bâtiment pour sortir : certains endroits l’exigent. Chez toi, la " +
      "porte donne directement sur la rue, et tu n’en as pas besoin.",
    bee: "Une adresse fausse coupe Gulliver d’Internet sans prévenir : téléchargements et recherches échouent avec " +
      "« the proxy refuses the connection ». Vide-le pour revenir au réglage normal."
  },
  mail_from: {
    title: 'Adresse d’envoi',
    control: { kind: 'text', placeholder: 'vide : pas de mail' },
    restart: false,
    effect: 'L’adresse d’où partent les codes de connexion et les liens. Vide : personne ne peut se connecter.',
    principle: 'Gulliver confie ses mails à Proton Mail Bridge, qui les envoie depuis cette adresse.',
    pros: ['Les codes et les liens partent d’une adresse que tu connais : pas de service en plus.',
      'Proton chiffre le message avant qu’il quitte le Mac.'],
    cons: ['Il faut que Proton Mail Bridge tourne sur le Mac, avec cette adresse dans son compte.',
      'Vide ou fausse, plus personne ne reçoit de code : la connexion échoue (« le mail n’a pas pu partir »).'],
    normal: "Pour se connecter, on tape son adresse et son mot de passe, puis un code à 8 chiffres reçu par mail. " +
      "Ce réglage dit de quelle adresse part ce mail. Elle doit appartenir au compte Proton ouvert dans Proton Mail " +
      "Bridge sur le Mac. Elle sert aussi de nom d’utilisateur auprès du Bridge.",
    owl: "mail_from, normalisée en minuscules, validée par valid_mail_address (host/mailer.cpp). send_mail ouvre une " +
      "prise TCP vers 127.0.0.1:mail_port, EHLO, AUTH PLAIN (mail_from, mail_secret) si un mot de passe est rangé, " +
      "MAIL FROM, RCPT TO, DATA : texte UTF-8 en base64, sujet RFC 2047. Vide : /login, /setup et /password/forgot " +
      "répondent 503 mail_unconfigured.",
    marmot: "C’est l’adresse de l’expéditeur sur l’enveloppe. Si elle n’est pas la tienne chez Proton, le facteur " +
      "refuse de porter la lettre.",
    bee: "Si tu la vides, plus personne ne peut se connecter depuis un appareil neuf : les sessions déjà ouvertes " +
      "restent, mais aucun code ne part. Pour revenir, remets l’adresse depuis le Mac."
  },
  mail_port: {
    title: 'Port de Proton Mail Bridge',
    control: { kind: 'number', min: 1, max: 65535 },
    restart: false,
    effect: 'Le port SMTP du Bridge sur le Mac (1025 par défaut). Le facteur ne parle qu’au Mac lui-même.',
    principle: 'Le Bridge écoute sur une porte du Mac (127.0.0.1) ; Gulliver y dépose ses mails.',
    pros: ['La conversation avec le Bridge ne quitte jamais le Mac : le mot de passe ne passe pas sur le Wi-Fi.'],
    cons: ['Il doit être le même que celui des réglages du Bridge (Paramètres → SMTP).'],
    normal: "Proton Mail Bridge ouvre un petit serveur d’envoi sur le Mac, au port 1025 le plus souvent. Gulliver " +
      "s’y connecte pour lui confier les mails. Si tu changes le port dans le Bridge, change-le ici aussi.",
    owl: "Entier de 1 à 65535. Connexion TCP vers 127.0.0.1 seulement (INADDR_LOOPBACK, aucune autre machine permise), " +
      "en clair sur la boucle locale : le Bridge annonce AUTH PLAIN avant STARTTLS et chiffre lui-même vers Proton. " +
      "Délai de 20 s par échange (SO_RCVTIMEO, SO_SNDTIMEO).",
    marmot: "C’est le numéro de la boîte aux lettres du Bridge, dans le couloir du Mac. Gulliver n’a pas besoin de " +
      "sortir de la maison pour poster.",
    bee: "Un mauvais port : « the mail server 127.0.0.1:… does not answer » dans le journal, et la connexion dit que " +
      "le mail n’a pas pu partir."
  },
  mail_secret: {
    title: 'Mot de passe du Bridge',
    control: { kind: 'text', secret: true, placeholder: 'le mot de passe SMTP du Bridge' },
    restart: false,
    effect: 'Le mot de passe que Proton Mail Bridge donne pour l’envoi (pas celui de ton compte Proton).',
    principle: 'Le Bridge invente un mot de passe à part pour les programmes du Mac ; Gulliver le lui redonne à chaque mail.',
    pros: ['Rangé à part, lisible par le seul compte du service ; la page n’en voit que le masque.'],
    cons: ['Un nouveau mot de passe dans le Bridge (après une réinstallation) doit être recopié ici.'],
    normal: "Dans Proton Mail Bridge, ouvre ton compte, puis « Configurer le client de messagerie » : le Bridge montre " +
      "le nom d’utilisateur (ton adresse) et un mot de passe propre au Bridge. Copie ce mot de passe ici. Ce n’est " +
      "pas le mot de passe de ton compte Proton.",
    owl: "mail_secret, rendu masqué (********) par GET /settings ; gardé dans mail.secret (0600, write_file_restricted) " +
      "avec l’adresse et le port pour lesquels il a été donné ; le masque renvoyé tel quel garde le mot de passe " +
      "enregistré. Envoyé en AUTH PLAIN sur 127.0.0.1 seulement, effacé de la mémoire après usage (wipe_secret).",
    marmot: "C’est la clé de la boîte aux lettres du Bridge. Gulliver la garde dans un tiroir fermé, et la page ne " +
      "voit que des étoiles.",
    bee: "Sans lui, le Bridge refuse les mails (« the mail server refused the login ») ; un faux, de même. Vide-le " +
      "pour l’effacer du Mac."
  },
  mail_link: {
    title: 'Adresse des liens',
    control: { kind: 'text', placeholder: 'vide : le réseau de la maison' },
    restart: false,
    effect: 'L’adresse de la page mise dans les liens « mot de passe oublié ». Vide : celle du réseau de la maison.',
    principle: 'Le lien du mail rouvre Gulliver à cette adresse, avec un jeton qui permet de choisir le mot de passe.',
    pros: ['Vide, le lien marche sur tous les appareils de la maison.',
      'Une adresse en ligne (la discussion par Cloudflare) fait marcher le lien hors de la maison.'],
    cons: ['Vide et réseau de la maison fermé, le lien ne s’ouvre que sur le Mac lui-même.'],
    normal: "Quand quelqu’un a oublié son mot de passe, ou se connecte pour la première fois, il reçoit un lien. Ce " +
      "lien doit mener à Gulliver depuis l’appareil où il lit ses mails. Vide, c’est l’adresse du Mac sur le Wi-Fi " +
      "(http://adresse-du-mac:8444/) ; tu peux y mettre l’adresse en ligne de la discussion.",
    owl: "Une adresse http(s) sans « ? » ni « # » ; le service y ajoute « #reset=<jeton> » (fragment : jamais envoyé " +
      "au serveur ni gardé dans ses journaux ; profile.js le lit puis l’efface de l’adresse). Vide : " +
      "http://<première adresse IPv4>:lan_port/ si lan, sinon https://localhost:port/. Jamais tirée de l’en-tête Host " +
      "de la requête, qu’un autre pourrait choisir pour recevoir le jeton.",
    marmot: "C’est l’adresse écrite sur le carton d’invitation : si elle est fausse, l’invité sonne à la mauvaise porte.",
    bee: "Une adresse fausse : le lien mène nulle part, et il faut en redemander un une fois corrigée. Le jeton vaut " +
      "30 minutes et une seule fois."
  },
  relay: {
    title: 'Relais vers un moteur extérieur',
    control: { kind: 'relay' },
    restart: false,
    effect: 'Envoie les discussions qui le demandent à Claude, à Ollama ou à un autre moteur, et rend leur réponse ici.',
    principle: 'Le service sert d’intermédiaire : il envoie la discussion au moteur extérieur avec ta clé, et rend sa ' +
      'réponse comme celle d’un modèle du Mac.',
    pros: ['Des modèles bien plus grands que ceux qui tiennent dans le Mac (Claude : un million de tokens de contexte).',
      'Avec Pseudalis, les noms, adresses et numéros partent remplacés par des pseudonymes, et la réponse revient ' +
        'avec les vrais.',
      'La clé reste dans le service : la page et les autres programmes ne la voient jamais.',
      'Hors ligne, un moteur du réseau de la maison (un autre ordinateur) reste joignable.'],
    cons: ['La discussion quitte le Mac : seule la pseudonymisation en retire les données personnelles, et elle ne ' +
        'reconnaît pas tout (un détail qui identifie quelqu’un sans être un nom, par exemple).',
      'Payant chez Claude et Ollama : chaque question consomme le crédit du compte.',
      'Ni outils, ni sons ; la recherche sur le web n’existe qu’avec Claude.',
      'Avec l’accès depuis le réseau ouvert, tout appareil du Wi-Fi peut y envoyer des discussions, donc utiliser ' +
        'ton crédit ; ses réglages, eux, ne se changent que sur le Mac.'],
    normal: "Un modèle du Mac répond sans que rien ne sorte. Pour une question difficile, on peut préférer un modèle " +
      "beaucoup plus grand, qui tourne chez Anthropic (Claude), chez Ollama, ou sur un autre ordinateur. Le relais " +
      "envoie alors la discussion à ce moteur, avec ta clé, puis rend sa réponse dans Gulliver. Avec la " +
      "pseudonymisation, Pseudalis remplace d’abord chaque donnée personnelle par un pseudonyme ; le moteur ne voit " +
      "que les pseudonymes, et Gulliver remet les vrais noms dans la réponse pendant qu’elle s’écrit. Les images ne " +
      "partent pas : un modèle du Mac qui lit les images les décrit, et la description part protégée elle aussi.",
    owl: "Le modèle albireo-relay de l’interface (POST /v1/chat/completions) passe par relay_service (host/relay.cpp, " +
      "repris de l’original 1.0.23) : huit réponses à la fois au plus, chacune sur son thread. OpenAI : " +
      "/chat/completions à l’adresse donnée (/v1 ajouté si seuls ses modèles y répondent) ; Ollama : " +
      "https://ollama.com/v1 ; Claude : /v1/messages, système à part, raisonnement adaptatif, effort, 64 000 tokens " +
      "de réponse sans borne de la requête, web_search à cinq recherches au plus. La clé ne va que dans les en-têtes " +
      "(x-api-key ou Authorization), et vit dans relay.secret (0600) ; settings.json n’a que ********, valable pour le " +
      "même fournisseur, la même adresse et le même utilisateur. Réglages et essai depuis n’importe quel appareil du " +
      "Wi-Fi, le téléphone compris (6.0.0 ; Wi-Fi personnel maîtrisé). Pseudonymisation : POST /v1/text/anonymize de " +
      "Pseudalis avec session: true (sa clé de séance, 3.1.0), réponse values ; une mémoire de 2 h (4 096 textes, " +
      "16 384 valeurs) évite de reprotéger les messages renvoyés à chaque tour ; stream_restorer remet les valeurs au " +
      "fil des fragments, mots seuls de quatre octets ou plus compris. Les séquences d’arrêt ne partent pas avec la " +
      "pseudonymisation. Hors ligne : seules les adresses de bouclage, privées et lien-local, vérifiées avant la " +
      "connexion.",
    marmot: "Imagine que tu demandes de l’aide pour un devoir à un prof d’une autre école. Le relais, c’est le " +
      "messager qui porte ta question et te rapporte la réponse. La pseudonymisation, c’est comme si le messager " +
      "remplaçait ton nom et celui de tes potes par des surnoms avant de partir, puis remettait les vrais noms en " +
      "revenant : le prof aide sans savoir de qui on parle.",
    bee: "Coupé, tout reste sur le Mac. Allumé sans pseudonymisation, la discussion part telle quelle chez le " +
      "fournisseur. Pseudalis arrêté ou trop ancien alors que la pseudonymisation est demandée : la question ne part " +
      "pas du tout, plutôt que de partir en clair. Une clé fausse ou un compte sans crédit donnent une erreur, sans " +
      "jamais montrer la clé."
  },
  rag_engine: {
    title: 'Comment chercher dans les documents',
    control: { kind: 'choice', options: [['hybrid', 'Mots et sens'], ['dense', 'Sens seul'], ['lexical', 'Mots seuls']] },
    restart: false,
    effect: 'La façon dont le service choisit les extraits d’un document pour répondre : par les mots, par le sens, ou les deux.',
    principle: 'Chaque document est coupé en passages. « Mots » compare les mots de la question à ceux des passages (BM25) ; « sens » compare des vecteurs qui résument l’idée (il faut un modèle d’embedding) ; « mots et sens » fond les deux.',
    pros: ['« Mots et sens » trouve large : les bons mots comme les bonnes idées.',
      '« Mots seuls » n’a besoin d’aucun modèle d’embedding et vise juste sur du technique exact (noms de fichiers, codes d’erreur, IP).',
      '« Sens seul » retrouve une idée même quand les mots diffèrent.'],
    cons: ['« Sens » et « mots et sens » exigent un modèle d’embedding chargé et des passages vectorisés.',
      '« Mots seuls » manque une reformulation qui ne partage aucun mot avec le document.'],
    normal: "Quand tu poses une question sur tes documents, le service en remonte les passages les plus utiles. Il peut " +
      "les choisir par les mots communs, par le sens (plus malin, mais il faut un modèle qui transforme le texte en " +
      "vecteurs), ou mélanger les deux. Le mélange est le plus sûr ; « mots seuls » marche partout, même sans modèle " +
      "d’embedding, et excelle sur les termes techniques exacts.",
    owl: "La recherche est portée en C++ dans host/rag_engine.{h,cpp} : rag_chunk découpe (fidèle à chunk() de la page), " +
      "rag_terms tokenise (latin : accents retirés, mots vides écartés), BM25 pour les mots, cosinus des vecteurs pour " +
      "le sens, fusion par rang réciproque, pertinence marginale maximale, prolongement par le passage suivant. " +
      "engine_for(rag_engine) rend lexical, dense ou hybrid. Route POST /gulliver/v1/profile/search.",
    marmot: "C’est comme chercher dans un gros classeur. « Mots » cherche les pages qui répètent tes mots. « Sens » " +
      "cherche les pages qui parlent de la même chose, même avec d’autres mots. « Les deux » fait les deux à la fois.",
    bee: "Sur « sens » ou « mots et sens » sans modèle d’embedding chargé, la recherche retombe sur les mots. Sur " +
      "« mots seuls », une question qui reformule sans reprendre aucun mot du document peut ne rien trouver."
  },
  rag_top_k: {
    title: 'Combien d’extraits de documents',
    control: { kind: 'number', min: 1, max: 50 },
    restart: false,
    effect: 'Le nombre d’extraits de documents qu’une recherche remonte au plus pour une question.',
    principle: 'Plus d’extraits donnent plus de contexte au modèle, mais remplissent sa fenêtre et laissent moins de place à la conversation.',
    pros: ['Plus d’extraits : moins de chance de manquer le passage utile.'],
    cons: ['Trop d’extraits noient la question, coûtent des tokens et peuvent dépasser le budget.'],
    normal: "Pour répondre à partir de tes documents, le service joint quelques extraits à ta question. Ce réglage dit " +
      "combien au maximum. Douze est un bon équilibre ; monte-le pour des documents longs où la réponse est éparpillée, " +
      "baisse-le pour laisser plus de place à la conversation.",
    owl: "rag_top_k borne le nombre d’extraits après la pertinence marginale maximale (options.limit de rag_search_options) ; " +
      "un budget de caractères les coupe aussi. Les extraits consécutifs d’un document fondent en un seul.",
    marmot: "C’est le nombre de bouts de tes documents que l’assistant lit avant de répondre. Peu : il va vite mais peut " +
      "rater un détail. Beaucoup : il lit plus, mais il a moins de place pour le reste.",
    bee: "Trop haut, les extraits mangent toute la fenêtre du modèle et il reste peu de place à ta question et à la " +
      "discussion ; certains extraits sont alors coupés par le budget."
  },
  rag_rerank: {
    title: 'Affiner le tri des extraits',
    control: { kind: 'switch' },
    restart: false,
    effect: 'Reclasse les extraits trouvés par un second tri, plus fin, avant de les joindre à la question.',
    principle: 'La recherche remonte les extraits par passages. Le reranker les reclasse sur l’extrait entier : il récompense celui qui couvre le plus de mots de la question, le plus densément et le plus serré.',
    pros: ['Le meilleur extrait remonte souvent en tête.', 'Sans modèle en plus : aucun coût de chargement, aucune attente.'],
    cons: ['Gain modeste : c’est un tri par les mots, pas un modèle qui « comprend » la question.', 'Un tout petit temps de calcul en plus par recherche.'],
    normal: "Après avoir trouvé les extraits, le service peut les reclasser une seconde fois pour mettre devant celui qui " +
      "répond le mieux à l’ensemble des mots de ta question. C’est léger et sans modèle supplémentaire ; laisse-le activé si " +
      "les meilleurs extraits ne sont pas toujours en tête, désactivé sinon.",
    owl: "rerank_excerpts (host/rag_engine.cpp) reclasse les extraits assemblés : score = 2·couverture + 1·densité + " +
      "0,5·proximité, sur rag_terms(question). Tri stable (garde l’ordre de fusion à égalité). Réglage rag_rerank " +
      "(options.rerank de rag_search_options), appliqué par les trois moteurs après assemble(). Ce n’est pas un cross-encoder " +
      "neuronal (remis au Mac mini).",
    marmot: "Quand l’assistant a trouvé des bouts de tes documents, ce réglage les range une deuxième fois pour mettre en " +
      "premier celui qui colle le mieux à TOUTE ta question, pas juste à un mot.",
    bee: "Sans lui, le meilleur extrait peut arriver deuxième ou troisième. Avec lui, il remonte ; mais ne lui demande pas de " +
      "deviner le sens — il compte les mots, finement, rien de plus."
  }
};

