// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « Le moteur » de l'onglet Service : les onze réglages du moteur de calcul, et les
    schémas de leurs fiches.  Rassemble ce qui concerne le GPU, les cœurs du processeur, le
    contexte, les clés et valeurs et les brouillons d'écriture.

    Exports :
      - items : les onze fiches (titre, contrôle, effet, principe, pour/contre, hibou, marmotte,
        abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

import { box, arrow, text, schema, contexts, kvSizes, formatTokens, formatMb } from './service_commun.js';

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
  }
};

export const schemas = {
  accelerator: schema('La puce du Mac : un seul plan de travail pour deux cuisiniers',
    `<rect x="40" y="44" width="520" height="160" rx="16" class="s-zone"/>` + text(300, 64, 'Puce Apple M2', 's-f') +
    box(70, 78, 170, 56, 'Processeur', 's-gold', 'contrôle, découpe, images') +
    box(360, 78, 170, 56, 'GPU', 's-blue', 'écrit et lit en grand') +
    box(70, 152, 460, 40, 'Mémoire unifiée : le modèle, les clés et valeurs, sans copie', 's-soft') +
    arrow(155, 134, 155, 152) + arrow(445, 134, 445, 152) +
    arrow(240, 98, 360, 98, 'prompts ≥ 24 tokens') + arrow(240, 120, 360, 120, 'chaque token écrit'), 215),
  threads: schema('Sur un M2 : deux familles de cœurs, deux équipes',
    [0, 1, 2, 3].map((i) => box(40 + i * 70, 60, 60, 46, 'P' + (i + 1), 's-gold')).join('') +
    [0, 1, 2, 3].map((i) => box(330 + i * 60, 66, 50, 34, 'E' + (i + 1), 's-soft')).join('') +
    text(155, 128, 'cœurs de performance', 's-f') + text(435, 128, "cœurs d'efficacité", 's-f') +
    `<rect x="32" y="146" width="280" height="30" rx="8" class="s-band s-blue"/>` +
    text(172, 166, 'écrire un token : 4 threads rapides', 's-t') +
    `<rect x="32" y="182" width="556" height="30" rx="8" class="s-band s-gold"/>` +
    text(310, 202, 'lire un long prompt : tous les threads', 's-t'), 222),
  max_sequences: schema('Plusieurs questions dans le même passage',
    box(20, 60, 110, 34, 'question A') + box(20, 104, 110, 34, 'question B') + box(20, 148, 110, 34, 'question C', 's-soft', 'attend') +
    arrow(130, 77, 230, 110) + arrow(130, 121, 230, 121) + text(180, 180, 'file', 's-f') +
    box(230, 84, 150, 74, 'Modèle', 's-blue', '1 passage = A + B') +
    arrow(380, 104, 470, 80) + arrow(380, 138, 470, 160) +
    box(470, 60, 110, 40, 'mémoire A', 's-gold') + box(470, 140, 110, 40, 'mémoire B', 's-gold') +
    text(300, 205, 'réglage = 2 : deux à la fois, la troisième attend', 's-f'), 215),
  context: schema('La fenêtre de contexte : ce que le modèle voit à la fois',
    Array.from({ length: 22 }, (_, i) => `<rect x="${24 + i * 25}" y="86" width="21" height="34" rx="4" class="s-box ${i < 7 ? 's-soft' : 's-gold'}"/>`).join('') +
    `<rect x="195" y="76" width="380" height="54" rx="10" class="s-window"/>` +
    text(385, 154, 'contexte : le modèle relit tout ce qui est dans la fenêtre', 's-f') +
    text(110, 154, 'trop ancien : hors de vue', 's-f') +
    arrow(560, 66, 590, 66) + text(530, 70, 'nouveau texte', 's-f', 'end'), 175),
  kv_mb: schema('La mémoire du Mac et la part des clés et valeurs',
    `<rect x="24" y="70" width="552" height="46" rx="10" class="s-box"/>` +
    `<rect x="24" y="70" width="150" height="46" rx="10" class="s-soft"/>` + text(99, 98, 'macOS, apps') +
    `<rect x="174" y="70" width="120" height="46" class="s-blue"/>` + text(234, 98, 'modèles') +
    `<rect x="294" y="70" width="190" height="46" class="s-gold"/>` + text(389, 98, 'clés et valeurs') +
    text(530, 98, 'réserve') +
    arrow(389, 146, 389, 120) + text(389, 164, 'kv_mb : la taille de cette part', 's-f') +
    text(300, 196, 'auto : deux contextes complets, en gardant une réserve au système', 's-f'), 210),
  kv_type: schema('Une clé de 32 nombres, en deux formats',
    text(24, 76, 'f16', 's-t', 'start') +
    Array.from({ length: 16 }, (_, i) => `<rect x="${70 + i * 31}" y="58" width="28" height="28" rx="4" class="s-box s-blue"/>`).join('') +
    text(70 + 16 * 31 + 4, 77, '64 o', 's-f', 'start') +
    text(24, 136, 'q8', 's-t', 'start') +
    Array.from({ length: 8 }, (_, i) => `<rect x="${70 + i * 31}" y="118" width="28" height="28" rx="4" class="s-box s-gold"/>`).join('') +
    `<rect x="${70 + 8 * 31}" y="118" width="20" height="28" rx="4" class="s-box s-soft"/>` +
    text(70 + 8 * 31 + 30, 137, '34 o (+ une échelle)', 's-f', 'start') +
    text(300, 186, "q8 : presque moitié moins de mémoire, un arrondi minuscule", 's-f'), 200),
  warm_history: schema('Pendant que tu lis, le moteur prépare la suite',
    box(20, 70, 130, 56, 'Réponse finie', 's-blue') + arrow(150, 98, 250, 98) +
    box(250, 70, 150, 56, 'Relecture', 's-gold', 'en arrière-plan') + arrow(400, 98, 480, 98) +
    box(480, 70, 100, 56, 'Prête', 's-soft') +
    text(300, 160, 'question suivante : seule la question est encore à lire', 's-f'), 185),
  spin_us: schema('Entre deux passes, les threads attendent',
    `<rect x="40" y="80" width="200" height="40" rx="8" class="s-box s-gold"/>` + text(140, 105, 'attente active') +
    `<rect x="240" y="80" width="320" height="40" rx="8" class="s-box s-soft"/>` + text(400, 105, 'sommeil') +
    arrow(240, 150, 240, 124) + text(240, 168, 'spin_us : la limite', 's-f') +
    text(300, 196, 'actif : repart tout de suite, mais le cœur reste occupé', 's-f'), 210),
  gpu_prompt_tokens: schema('À partir de combien de tokens le GPU lit',
    `<line x1="40" y1="110" x2="560" y2="110" class="s-line"/>` +
    [8, 16, 24, 32, 64, 128].map((n, i) => `<line x1="${60 + i * 95}" y1="102" x2="${60 + i * 95}" y2="118" class="s-line"/>` +
      text(60 + i * 95, 140, String(n), 's-f')).join('') +
    `<rect x="60" y="66" width="190" height="28" rx="6" class="s-gold"/>` + text(155, 85, 'processeur') +
    `<rect x="250" y="66" width="310" height="28" rx="6" class="s-blue"/>` + text(405, 85, 'GPU') +
    text(300, 176, 'mesuré sur le M2 : le GPU perd à 16 tokens, gagne dès 24', 's-f'), 195),
  draft_tokens: schema('Un brouillon, vérifié en une passe',
    box(20, 70, 150, 56, 'Conversation', 's-soft', '« … le moteur Metal »') + arrow(170, 98, 230, 98) +
    box(230, 70, 150, 56, 'Brouillon', 's-gold', 'jusqu’à 7 mots') + arrow(380, 98, 440, 98) +
    box(440, 70, 140, 56, 'Une passe', 's-blue', 'garde les bons') +
    text(300, 160, 'les mots justes sont gardés ; au premier faux, le modèle reprend la main', 's-f'), 185),
  gpu_prompt_context: schema('Jusqu’à quelle longueur le GPU lit',
    `<rect x="40" y="70" width="320" height="40" rx="8" class="s-blue"/>` + text(200, 95, 'GPU') +
    `<rect x="360" y="70" width="200" height="40" rx="8" class="s-gold"/>` + text(460, 95, 'processeur') +
    arrow(360, 140, 360, 114) + text(360, 158, 'limite (par défaut : la longueur du modèle)', 's-f') +
    text(300, 190, 'le GPU lit plus vite à toutes les longueurs mesurées', 's-f'), 205)
};
