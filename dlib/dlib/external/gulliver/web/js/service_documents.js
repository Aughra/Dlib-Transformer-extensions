// Fork Mac de Dlib-Transformer-extensions (Gulliver pour macOS sur puce Apple).
// License: Boost Software License   See LICENSE.txt for the full license.

/*
    Groupe « La recherche dans les documents » de l'onglet Service : le moteur de recherche, le nombre d'extraits et le second tri.

    Exports :
      - items : les fiches de ces réglages (titre, contrôle, effet, principe, pour/contre, hibou,
        marmotte, abeille) ;
      - schemas : les schémas SVG de ces fiches, par la même clé.
*/

export const items = {
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

export const schemas = {
};
