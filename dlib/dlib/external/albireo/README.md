# Albireo

> **Fork macOS** (TortueTech, version 2.0.0 et suivantes) : Albireo pour les Mac Apple Silicon seulement, son
> interface en français seulement, sans synthèse vocale (retirée en 3.0.0). Depuis 4.0.0, il ne détient aucune clé :
> le catalogue n'est pas signé et vient de son adresse dans le dépôt de l'original ou d'un
> dossier, et l'espace de publication, le flux signé, les mises à jour et les outils de signature sont retirés.
> Voir `web/changelog.md` pour ses changements ; le reste de ce document décrit l'original, pour Windows.

Albireo exécute des modèles de langage sur l'ordinateur où il est installé. C'est un programme
Windows 11 construit sur dlib et Dlib-Transformer-extensions : un moteur d'inférence pour les modèles
GGUF quantifiés, un service Windows qui le sert sur une adresse HTTPS locale avec une interface compatible
avec celle d'OpenAI, une application web servie par ce service, une icône dans la zone de notification
pour le gérer, et un installateur. Les conversations, les documents et les modèles restent sur la
machine, et rien ne demande de compte.

![Une question sur un graphique dans l'application web d'Albireo, à laquelle répond le modèle livré avec lui](docs/images/chat.png)

Albireo a été créé par Aldric Pierrain (cydraltechnology@gmail.com). Son nom est celui
d'une étoile double de la constellation du Cygne, l'une dorée et l'autre bleue, que dessine son icône. Cette
version est la Community Edition : l'installateur, les fenêtres « À propos » de l'icône et de l'application
web, la description de l'interface et la liste des programmes de Windows la nomment
« Albireo Community Edition ».

## Sommaire

- [Ce qu'il fait](#ce-quil-fait)
- [Captures d'écran](#captures-décran)
- [Comment il est construit](#comment-il-est-construit)
- [Arborescence des sources](#arborescence-des-sources)
- [Construire sous Windows](#construire-sous-windows)
- [Construire sous Linux](#construire-sous-linux)
- [Tests](#tests)
- [Installation](#installation)
- [Réglages](#réglages)
- [Interfaces](#interfaces)
- [Accès au web](#accès-au-web)
- [Modèles](#modèles)
- [Modèles livrés avec Albireo](#modèles-livrés-avec-albireo)
- [Ordinateurs sans réseau](#ordinateurs-sans-réseau)
- [Publier une version](#publier-une-version)
- [Sécurité](#sécurité)
- [Dossiers de données](#dossiers-de-données)

## Ce qu'il fait

- **Discussion** dans l'application web, avec les discussions conservées dans le navigateur (IndexedDB), un
  mode navigation privée qui ne les garde qu'en mémoire, une recherche dans les discussions, et le raisonnement
  des modèles qui raisonnent affiché pendant qu'ils réfléchissent, puis replié dès que la réponse commence. Chaque
  question et chaque réponse garde son heure, affichée à côté de ses boutons (« il y a 3 heures » pendant une journée,
  puis la date courte avec ses heures et ses minutes) ; une question passée peut être reposée à la fin
  de la discussion, et une question modifiée fait repartir la discussion d'elle, ce qui la suivait étant
  supprimé. Les boutons d'une réponse agissent une fois la réponse écrite, réponse régénérée
  comprise, et le bouton Arrêter pulse pendant qu'elle est préparée et écrite. Une réponse continue pendant
  qu'une autre discussion est affichée ou qu'une nouvelle commence ; le panneau latéral marque sa discussion d'un
  point clignotant, qui devient bleu une fois la réponse écrite et reste jusqu'à ce que la discussion soit
  ouverte, et une discussion en navigation privée arrête sa réponse quand on la quitte. Quand l'utilisateur fait défiler une
  discussion plus longue que la fenêtre, deux boutons au-dessus de son coin inférieur droit mènent d'un coup à son
  début et à sa fin. Le modèle livré avec Albireo nomme chaque discussion dans
  la langue de sa première question, les premiers mots tenant lieu de titre jusqu'à son arrivée,
  et garde une mémoire de l'utilisateur, écrite dans la langue de l'interface quelle que soit la langue
  des discussions. L'application demande au navigateur de conserver sa base de données, et ses réglages
  indiquent si le navigateur le fait et la place occupée, avec le conseil d'un export régulier sinon.
- **Documents** joints à un message ou à un projet : Word, Excel, PowerPoint, OpenDocument,
  PDF, RTF, HTML, texte, CSV, Markdown et code source. Le navigateur extrait le texte de chaque page
  d'un document, de chaque feuille d'un classeur dans l'ordre de ses onglets, et de chaque diapositive d'une
  présentation dans l'ordre du diaporama. Chaque fichier apparaît sous forme de vignette avec son format, qui
  peut être retirée avant l'envoi, et la modification d'une question garde, retire ou ajoute ses fichiers.
- **Images** données aux modèles qui les lisent : une question porte des images PNG ou JPEG (le
  navigateur convertit les autres formats qu'il affiche), jointes, collées ou déposées, affichées en vignettes
  qui s'ouvrent en entier dans une visionneuse, dix au plus dans le contexte d'une discussion, les images des
  échanges qu'une compression a résumés quittant le contexte avec eux. La base de connaissances d'un projet
  contient 100 éléments au plus, documents et images ensemble, dont dix images, que le modèle
  livré avec Albireo décrit en arrière-plan ; leurs descriptions sont cherchées avec ses
  documents, et une question qui en trouve une reçoit l'image, quatre au plus.
  Un modèle qui ne lit que du texte ne reçoit que les noms des images.
- **Lecture à voix haute** : chaque réponse a un bouton qui la lit à voix haute, en anglais, français, allemand,
  italien ou espagnol selon ce que montrent ses mots, avec la synthèse vocale livrée avec Albireo, qui
  n'a besoin ni de réseau ni d'une voix de Windows (voir [Service](#service)).
- **Projets** : des discussions regroupées avec des instructions et des documents qui forment leur
  contexte commun, lus en entier quand ils tiennent, cherchés sinon par le sens et par les mots, la recherche et
  son index tournant dans le navigateur (voir [Documents et contexte](#documents-et-contexte)).
- **Trois fonctions**, générale, code et sécurité, chacune servie par un modèle de langage du
  catalogue, choisi dans la fenêtre Modèles ou par le service quand un modèle arrive, et un modèle
  d'embedding pour la recherche dans les documents, que le service installe dans tous les cas.
- **Modèles livrés avec Albireo** : l'installateur complet porte Qwen3-VL-4B-Thinking-albireo,
  qui lit les images et le texte, raisonne avant sa réponse quand le raisonnement approfondi est activé et répond
  directement sinon, et sert les trois fonctions dès qu'Albireo est installé, et Qwen3-Embedding-0.6B-albireo pour la
  recherche, tous deux récupérés et vérifiés par le build (voir
  [Modèles livrés avec Albireo](#modèles-livrés-avec-albireo)).
- **Mode agent** : le modèle appelle des outils qui tournent dans la page (calculatrice, JavaScript dans un
  cadre isolé, plan, lecture et recherche des documents, recherche dans les autres
  discussions, date, fichiers à télécharger), jusqu'à 100 étapes par réponse, le contexte ajusté avant
  chaque étape. L'aide à côté de son bouton décrit les outils ; chaque étape d'une réponse montre
  son outil, ses arguments, son résultat, son état et sa durée. La calculatrice lit les
  pourcentages (« 17,5 % de 2 340 »), les virgules décimales et les milliers écrits avec des espaces.
- **Accès au web** : le bouton Web de la zone de saisie, désactivé dans chaque nouvelle discussion comme le
  raisonnement approfondi et l'agent, donne au modèle une recherche dans Wikipédia et la lecture des pages et des fichiers PDF d'adresses publiques, que le
  service récupère ; le modèle ne lit que les adresses que contient la discussion (voir
  [Accès au web](#accès-au-web)).
- **Plusieurs conversations à la fois** : les requêtes de tous les clients sont calculées ensemble, une
  seule passe avant servant le token suivant de chacune, et les clés et valeurs d'une requête terminée restent
  en cache pour que le tour suivant d'une conversation commence là où le précédent s'est arrêté ; une fois une
  réponse terminée, le moteur lit son historique en arrière-plan tel que la question suivante le présentera,
  sans le raisonnement des tours précédents, pour que la question suivante ne calcule que ses propres
  tokens.
- **Une interface pour les autres programmes** : `/v1/chat/completions`, `/v1/completions`,
  `/v1/embeddings`, `/v1/audio/speech` et `/v1/models` suivent l'interface d'OpenAI, streaming
  et images compris, avec sa documentation sous forme de page Swagger à `/docs/`.
- **Des moteurs pour chaque type de processeur** : portable, AVX2, AVX-VNNI, NPU Intel et GPU
  intégré via OpenVINO, GPU NVIDIA via CUDA, derrière une seule interface C. Le choix automatique
  fait tourner l'accélérateur de l'ordinateur, le processeur calculant ce que son périphérique ne prend pas, et
  le menu Accélérateur de l'icône le choisit, ou le processeur seul, ou un périphérique par son nom, et
  mesure les moteurs sur la machine.
- **Des modèles issus d'un catalogue** (non signé dans le fork macOS), téléchargés en parties parallèles depuis leurs
  sources officielles, vérifiés par rapport à leurs empreintes, ou importés depuis un disque ou une carte mémoire.
- **Des mises à jour** annoncées par un flux signé de l'espace de publication, leurs installateurs publiés
  là, téléchargées en arrière-plan et installées au redémarrage du service (retirées dans le
  fork macOS).
- **Un mode hors ligne** pour les ordinateurs sans réseau : le service n'ouvre aucune connexion, et
  le catalogue, les modèles et les mises à jour viennent d'un paquet hors ligne apporté sur un disque.
- **Cinq langues** dans chaque fenêtre : anglais, français, allemand, italien et espagnol, la langue
  de Windows par défaut.

## Captures d'écran

L'application web servie par le service, les fenêtres de l'icône et l'installateur, avec le
modèle livré avec Albireo (Qwen3-VL-4B-Thinking-albireo) sur un processeur à deux cœurs.

| | |
|---|---|
| ![La page d'un projet](docs/images/project.png) | ![Une discussion du projet](docs/images/project-chat.png) |
| Un projet : ses instructions, ses documents et ses images avec leurs descriptions | Une discussion du projet, qui cite la politique et le document numérisé donné sous forme d'image |
| ![Du code dans le thème sombre](docs/images/code-dark.png) | ![La mémoire des discussions](docs/images/memory.png) |
| Du code, dans le thème sombre | La mémoire des discussions, écrite en arrière-plan |
| ![La fenêtre Modèles](docs/images/models.png) | ![L'onglet À propos](docs/images/about.png) |
| La fenêtre Modèles de l'icône | Version, moteur, processeur et utilisation du service |
| ![L'installateur](docs/images/setup.png) | ![La documentation de l'interface](docs/images/api.png) |
| L'installateur, qui porte les modèles | La documentation de l'interface, à `/docs/` |

## Comment il est construit

### Moteurs

Un moteur est une bibliothèque (`engines\albireo-engine-<variant>.dll`) qui exporte les fonctions de
[`common/engine_api.h`](common/engine_api.h) : décrire, créer, charger et décharger un modèle, soumettre une
requête dont les tokens reviennent sous forme d'événements, l'annuler, et répondre à des interrogations (statistiques, tokens,
modèles). Chaque variante est construite à partir des mêmes sources :

| Variante      | Produits avec les poids                                               | Construite avec              |
|---------------|-----------------------------------------------------------------------|------------------------------|
| `cpu`         | noyaux quantifiés, tout processeur x86-64                             | l'arbre de build principal   |
| `cpu-avx2`    | noyaux quantifiés, AVX2, FMA et F16C (Intel Haswell, AMD Zen et suivants) | l'arbre de build principal |
| `cpu-avxvnni` | `cpu-avx2` avec les produits scalaires d'AVX-VNNI (Intel Core à partir d'Alder Lake, Intel Xeon à partir de Sapphire Rapids, AMD Zen 5) | l'arbre de build principal |
| `openvino`    | NPU et GPU intégré des processeurs Intel, noyaux de `cpu-avx2`        | `ALBIREO_ENGINE_OPENVINO=ON` |
| `openvino-avxvnni` | `openvino` avec les noyaux de `cpu-avxvnni`                      | `ALBIREO_ENGINE_OPENVINO=ON` |
| `cuda`        | processeurs graphiques NVIDIA, noyaux AVX2                            | `ALBIREO_ENGINE_CUDA=ON`     |

Un processeur doté d'AVX-512 et sans AVX-VNNI (Intel Core Ice Lake, Tiger Lake et Rocket Lake,
Intel Xeon Cascade Lake et Ice Lake, AMD Zen 4) exécute `cpu-avx2` ; une mise à jour retire de
l'installation les bibliothèques des moteurs AVX-512 qu'installaient les versions précédentes.

Les conversions en demi-précision de F16C appartiennent à `cpu-avx2` : tout processeur doté d'AVX2 les possède,
et les échelles des clés et des valeurs de l'attention, ainsi que ces clés et valeurs elles-mêmes quand
les réglages les gardent en demi-précision, passent par elles. Les variantes
ne diffèrent que par leurs noyaux quantifiés ([`engine/qkernels.cpp`](engine/qkernels.cpp)), compilés pour
les instructions de chaque variante (`/arch:AVX2` et la macro `ALBIREO_AVX_VNNI` avec Visual Studio ;
`-mavxvnni` avec GCC et Clang) tandis que le reste de la bibliothèque reste en AVX2 : la description d'une
bibliothèque s'exécute sur tout processeur doté d'AVX2 et signale les instructions qui manquent au processeur, pour que le
service passe à la variante suivante. VNNI additionne quatre produits d'octets, ou deux produits d'entiers
de 16 bits, en une seule instruction (`vpdpbusd`, `vpdpwssd`) : les noyaux de Q4_0 et Q8_0 tirent les décalages
de leurs poids des sommes des activations, et ceux de Q4_K, Q5_K et Q6_K appliquent leurs
échelles avec `vpdpwssd`. Les intrinsèques AVX-VNNI demandent Visual Studio 2022 (GCC 11, Clang 12) ; un
compilateur plus ancien construit les autres variantes.

Les produits que le NPU ou le processeur graphique ne prend pas, et tout produit que le routage de
dlib laisse au processeur, s'exécutent sur les noyaux quantifiés du moteur. Le moteur OpenVINO est
donc construit une fois par famille de noyaux, pour qu'un processeur doté d'AVX-VNNI garde ses noyaux
quand le NPU ou le GPU sert les modèles. Le service décrit le moteur OpenVINO de la
famille la plus riche qu'exécute le processeur et propose ses périphériques (`openvino:npu`, `openvino:gpu`,
`openvino:both`) ; le réglage `"engine": "openvino"` désigne ce moteur. Une bibliothèque dont les noyaux
demandent des instructions qui manquent au processeur est signalée comme telle sans être chargée, car l'environnement d'exécution
d'un périphérique met des secondes à démarrer.

La passe avant ([`engine/cpu_runtime.cpp`](engine/cpu_runtime.cpp)) garde les poids dans la
projection en mémoire du fichier GGUF, sous leur forme quantifiée, et les multiplie avec des noyaux qui
quantifient les activations sur 8 bits par bloc, pour qu'un token généré lise chaque poids une seule fois.
Quand plusieurs réponses sont écrites ensemble, les noyaux Q4_K et Q6_K d'AVX2 et d'AVX-VNNI calculent
deux à quatre tokens pour chaque poids qu'ils décompressent, chaque token avec l'arithmétique qu'il aurait
seul : avec quatre réponses à la fois, une étape prend 32 à 40 % de temps en moins (Qwen3 0.6B et 1.7B, deux
cœurs). Une passe de huit tokens ou plus calcule en simple précision, avec un produit matriciel compacté
à partir de 256 tokens ; les poids de Q6_K sont dépliés par un
noyau vectoriel qui leur est propre, bit pour bit identique à dlib ; les tampons de travail sont alignés sur des lignes de 64 octets.
Ces chemins gardent chaque résultat dans les tolérances des tests, et les nombres d'une passe
dépendent de sa taille : à température 0, la première réponse à un prompt, dont le dernier token est lu
au sein du prompt entier, peut différer des suivantes, qui prennent le prompt dans le cache.
Les clés et les valeurs vivent dans des pages de 256 positions, une séquence par requête, sur 8 bits par défaut
(`kv_type`) : des groupes de 32 valeurs qui partagent une échelle en demi-précision, 34 octets contre 64 en demi-précision
([`engine/kv_cache.h`](engine/kv_cache.h)). Les clés sont tournées avant leur arrondi,
par les signes d'un motif fixe puis la transformée de Walsh-Hadamard normalisée, et les requêtes de même,
ce qui conserve leurs produits : quelques canaux des clés de Qwen3 portent des valeurs dix fois plus grandes que
les autres, que les groupes arrondiraient avec un pas grossier, et la rotation les répartit sur la
ligne. Par rapport à la demi-précision, les clés et valeurs sur 8 bits ont donné le même token suivant sur les 210
positions d'un texte avec Qwen3-VL 4B, avec une divergence moyenne de 6.5e-5 (KL), vingt-cinq fois
inférieure aux différences des produits entiers de la génération ; sans la rotation, les clés
seules donnaient 1.1e-2 avec Qwen3 0.6B, et 7e-4 avec elle. Les conversations qui commencent par le même
texte partagent les pages pleines de ce texte, et une séquence copie une page partagée avant d'y écrire.
L'attention s'exécute sur le processeur dans toutes les variantes, par tuiles : jusqu'à 64 lignes de requêtes, les
têtes qui partagent une même tête clé/valeur pour des tokens consécutifs d'un prompt, lisent les pages l'une après
l'autre avec un maximum et une somme courants, comme le fait FlashAttention, pour que chaque clé et valeur
tirée de la mémoire serve la tuile entière au lieu d'une seule ligne. Sur deux cœurs, les 2 900 tokens
d'une question qu'un projet ajoute à une discussion de 3 200 tokens ont pris 80 s au lieu de 228 s avec
Qwen3 0.6B. Pendant qu'une seule réponse est générée, les positions de chaque tuile sont découpées en plages
de 256 positions au moins, chacune lue par une tâche pour toutes les têtes de son groupe, et les sommes
partielles des plages sont fusionnées (le découpage de FlashDecoding) : chaque thread y prend part, et les clés
et valeurs d'un long contexte sont lues une fois par token au lieu d'une fois par tête. Sur deux cœurs, une
étape de Qwen3 0.6B après 8 192 positions a pris 102 ms avec les clés et valeurs sur 8 bits, contre
116 ms en demi-précision. Les sommes des plages arrivent dans un autre ordre que dans une seule tâche, si
bien que les logits d'une étape diffèrent de l'arrondi des flottants d'un nombre de threads à
l'autre.

Le contexte d'un modèle est celui pour lequel il a été entraîné, que le moteur ne réduit que lorsque les
clés et valeurs d'un contexte plein dépassent la mémoire que le système peut engager, sa mémoire physique et
son fichier d'échange ou son swap ensemble, moins une réserve laissée au système (un huitième de la mémoire
physique, 1 Go au moins) et les poids du modèle, qu'une génération lit à chaque token ; le
réglage `context` en choisit un autre. Les pages sont allouées à mesure que les séquences grandissent, dans un
budget de deux contextes pleins dans cette mémoire, et au-delà des pages des 4 096 premières positions une
garde refuse une nouvelle page dès que la mémoire que le système peut encore engager descend à cette réserve,
la requête attendant alors ou se terminant comme quand le budget s'épuise. La mémoire des clés et valeurs suit le texte réellement
contenu : les 262 144 positions de Qwen3-VL 4B prennent 20,5 Go sur 8 bits (38,6 Go en demi-précision), ce que
seule une conversation de cette longueur atteint, le fichier d'échange ou le swap contenant ce que la mémoire
physique ne contient pas. Les clés et valeurs restent dans la mémoire de l'ordinateur avec tous les moteurs : les
moteurs graphiques gardent l'attention sur le processeur, et V²RAM étend la mémoire de la carte pour
les poids. L'application web dimensionne ses propres choix dans ce contexte (voir le prompt d'une
question ci-dessous). L'ordonnanceur ([`engine/engine.cpp`](engine/engine.cpp)) construit chaque passe à partir
du token suivant de chaque requête en cours de génération et d'une tranche des prompts en cours de lecture, et tire
les tokens suivants de la passe en parallèle. Quand les pages s'épuisent, un prompt en cours de lecture rend ses
pages aux réponses en cours d'écriture et est relu dès qu'il y a de la place ; le contexte d'un
modèle ne dépasse jamais les positions que ses pages peuvent contenir, si bien qu'un prompt accepté par le moteur peut toujours
être lu jusqu'au bout.

Les moteurs processeur ne prennent aucune BLAS : leurs produits partent des poids quantifiés du fichier
projeté, et le noyau compacté des passes de prompt atteint 66 à 77 % de la crête des unités AVX2
de deux cœurs. Mesuré là, oneMKL 2026.2 limité à AVX2 et appelé par tuile de lignes dépliées
lisait un prompt de 2 182 tokens 1 à 6 % plus vite avec Qwen3 0.6B et 7 à 11 % plus vite avec
Qwen3-VL 4B, pour 283 Mo de bibliothèques dans l'installation. Les moteurs CUDA multiplient sur la carte
avec cuBLAS, dont les installateurs portent les bibliothèques.

Les moteurs des périphériques ajoutent un `matmul_offload` ([`engine/offload.h`](engine/offload.h)) qui
reçoit chaque produit avec la façon de le calculer sur le processeur
([`engine/tensor_backend.cpp`](engine/tensor_backend.cpp)). Le produit part vers le périphérique par
les tenseurs de dlib, avec une copie de la matrice des poids conservée tant que les copies de tous les modèles tiennent
dans un budget :

- avec OpenVINO, les copies contiennent des flottants en simple précision, et le routage du module NPU de
  dlib chronomètre les noyaux du processeur contre le NPU et le GPU, forme par forme, et garde le
  plus rapide ;
- avec CUDA, une copie contient les blocs au format GGUF de sa matrice tels que le fichier les contient, pour
  F32, F16, BF16, Q4_0, Q4_1, Q5_0, Q5_1, Q8_0, IQ4_NL, Q4_K, Q5_K et Q6_K, et des flottants en simple
  précision pour les autres formats. Les noyaux de dlib
  ([`dlib/cuda/cuda_gguf.cu`](../../cuda/cuda_gguf.cu)) lisent les blocs sur place pour un produit de
  moins de 64 tokens, un warp par ligne décodant chaque groupe de 32 poids une seule fois pour jusqu'à huit
  tokens, et les déplient à partir de 64 tokens par tuiles de 64 Mo que cuBLAS multiplie. Avant qu'une nouvelle
  copie serve, ses 64 premières lignes multiplient des activations de sonde et sont dépliées sur la carte, comparées
  au même travail du processeur : un format dont les résultats diffèrent garde ses matrices en simple
  précision. Le moteur mesure les
  produits de chaque forme sur deux matrices et sur le processeur, et vérifie son choix à des
  intervalles croissants. Les blocs traversent le bus depuis la projection du fichier, sans image dans la mémoire
  de l'hôte, et les copies restent dans la mémoire de la carte que gère la mémoire de périphérique étendue
  de dlib (V²RAM), moins 1 Gio (un quart de celle-ci sur une carte plus petite) pour les états cachés des
  passes, les tuiles et cuBLAS. Une matrice laissée hors des copies est calculée par le processeur
  à partir de ses poids quantifiés pour un produit de moins de 128 tokens, dont un token généré,
  et traverse le bus dans son format pour ce seul produit à partir de 128 tokens. Une taille donnée au
  fichier de V²RAM (`v2ram_max_mb`) permet aux copies de dépasser la carte pour s'y étendre, les blocs qui ne
  tiennent pas passant dans la mémoire de l'hôte ou dans ce fichier et revenant avant leur utilisation ; dès que
  les copies dépassent la mémoire de la carte, les produits de moins de huit tokens restent sur le
  processeur. Seules les lignes d'un produit traversent le bus.

Avec CUDA, les couches d'un modèle dense s'exécutent sur la carte d'un seul tenant (`layer_pass`) : les états cachés
d'une passe restent sur la carte de la première couche à la dernière, la carte calcule les normalisations,
les sept produits, la porte SwiGLU et les sommes résiduelles de chaque couche, et le processeur garde
l'attention sur les clés et valeurs paginées, les biais, les normalisations des requêtes et des clés
et les positions rotatives. Une couche traverse le bus une fois dans chaque sens, les requêtes, clés et valeurs de
ses tokens vers le processeur et les sorties de leur attention au retour, par de la mémoire épinglée et
des copies asynchrones, là où les produits seuls le traversaient deux fois par produit. Avant sa première utilisation,
une passe de la carte sur 8 tokens de sonde, dont les produits lisent les blocs sur place, et une passe sur
64, dont les produits passent par les tuiles, doivent donner les logits du processeur seul à 1 % près,
le token le plus probable différant pour un des 8 tokens de sonde et quatre des 64 au plus, sinon les
couches restent avec les produits ; une vérification que les clés et valeurs ou la carte ont reportée est refaite
64 passes plus tard. Une défaillance de la carte ramène la passe au processeur, qui
la recalcule. Un modèle à mélange d'experts, et les passes qui ajoutent les tranches DeepStack
d'une image, gardent les produits.

Avec OpenVINO, les produits d'un seul token restent souvent sur le processeur, qui les sert à partir
des poids compacts ; les produits des prompts vont au périphérique. L'encodeur d'images d'un modèle envoie lui aussi ses
produits au périphérique du moteur, ses copies quittant le périphérique pour le décodeur entre
deux images.

Les modèles chargés ensemble partagent le budget du périphérique (`device_mb`, automatique par défaut :
80 % de la mémoire libre de la carte sans V²RAM, la mémoire de la carte que gère V²RAM moins sa
réserve avec lui, et aussi son fichier quand `v2ram_max_mb` lui donne une taille, un quart de la mémoire de
l'hôte avec OpenVINO). Un modèle qui a des requêtes en cours ou en attente garde ses
copies. Quand il lui faut de la place, les modèles sans requêtes donnent la leur, le modèle utilisé le moins récemment
en premier, chacun donnant la place demandée et gardant ses autres copies ; ces copies reviennent quand leur
modèle répond de nouveau. Le modèle d'embedding y prend part comme les modèles de langage : tant qu'aucun document n'est
en cours d'indexation ou de recherche, sa place va aux modèles qui répondent. Quand tous les modèles ont des requêtes
et que le budget est plein, une matrice sans place est calculée par le processeur, ou traverse le bus pour
un produit de 128 tokens ou plus (CUDA), pour que les requêtes de tous les modèles avancent ensemble. Les statistiques du périphérique
comptent les copies qui sont parties (`evictions`, `evicted_bytes`).

Un modèle remplacé ou désactivé dans la fenêtre Modèles quitte le moteur une fois ses requêtes servies
(`engine_host::unload_model`) : à partir de ce moment, le service ne lui envoie plus aucune nouvelle requête, les
requêtes en cours et en attente se terminent normalement, éventuellement calculées avec celles des autres
modèles, et le modèle est déchargé après la dernière (les requêtes encore en cours après 30 minutes
se terminent avec le déchargement). La fenêtre l'affiche comme en cours de déchargement entre-temps ; réactivé avant de
partir, le modèle reste chargé. Un modèle supprimé pendant qu'il répond est supprimé de la même façon, une fois ses
requêtes servies, et est affiché comme en cours de suppression jusque-là.

### Mesures des moteurs

« Mesurer les moteurs », dans le menu Accélérateur (`POST /albireo/v1/engines/benchmark`), mesure chaque
moteur que l'ordinateur exécute, chaque périphérique d'un moteur qui en a plusieurs, dans un processus à part
(`--bench-engine`) avec le modèle général, ou le plus petit modèle installé : une première requête de 16
tokens lit le modèle sur le disque, laisse un périphérique compiler ses graphes et faire ses copies, et
laisse le routage des produits se stabiliser ; deux requêtes de 64 tokens suivent, et la plus rapide des
deux donne la mesure, en tokens générés par seconde. Le moteur du service ne calcule rien
pendant ce temps : chaque mesure attend qu'il ne serve plus aucune requête, deux minutes au plus, et est refaite
une fois quand le service a reçu une requête pendant celle-ci, pour qu'une discussion, une indexation de documents
ou le travail en arrière-plan ne partage pas le processeur avec le moteur mesuré. Le choix automatique
donne ensuite au moteur processeur la place d'un accélérateur mesuré plus lent que lui, et
le service redémarre quand son choix change. Le menu Accélérateur et la fenêtre de l'ordinateur
donnent les mesures dans la langue de l'interface (« 23.5 tokens/s », « 23,5 jetons/s »).

Le moteur CUDA garde les poids sur la carte dans leurs formats GGUF et y calcule les couches
d'un modèle dense, l'attention restant sur le processeur : un token généré traverse le bus une fois
dans chaque sens par couche et lit 1,1 Go de poids sur la carte pour Qwen3-VL 2B en Q4_K_M, là où il les lisait
en simple précision, six fois plus d'octets, et traversait le bus deux fois pour chacun des 197
produits de sa passe. Les produits pour lesquels la carte manque de place s'exécutent sur le processeur à partir de leurs
poids quantifiés.

Les formats de conversation sont écrits nativement ([`engine/chat_format.cpp`](engine/chat_format.cpp)),
le modèle de discussion (chat template) d'un modèle désignant son format : ChatML (Qwen2.5, Qwen3, Qwen3-VL, EuroLLM, avec le
raisonnement entre `<think>` et `</think>`), Mistral (Ministral 3, Devstral Small 2), Llama 3,
Granite 3, Granite 4.2 (ChatML avec les appels d'outils sous la forme XML `<function=...>`), Phi-4-mini,
le format de Foundation-Sec-8B-Reasoning et celui de Foundation-Sec 1.1 8B Instruct, dont les marqueurs de rôle
`<|system|>`, `<|user|>` et `<|assistant|>` sont du texte ordinaire de son vocabulaire : ses fichiers GGUF
ne portent aucun template, le moteur prend celui du dépôt du modèle, et ces marqueurs terminent
sa réponse comme le fait son token de fin de texte. Les instructions des outils de Qwen2.5 gardent les accolades
doublées de l'exemple de son template, et CyberSecQwen 4B reçoit un message système dans le tour de
l'utilisateur, comme l'écrit le template de son entraînement. Les tests comparent les prompts avec ceux des templates
de référence rendus par Jinja, octet pour octet, comme les rendent les bibliothèques qui appliquent les chat templates,
le saut de ligne après chaque balise et les espaces qui le précèdent retirés : le template de
Foundation-Sec-8B-Reasoning, écrit sur plusieurs lignes, donne ses tours l'un après l'autre, la
réponse juste après `<|assistant|>`. Le tokenizer de dlib lit les vocabulaires BPE au niveau de l'octet,
avec entre autres les pré-tokenizers de GPT-2, Llama 3, Qwen2, tekken et GPT-4o (Phi-4-mini), et
ceux de SentencePiece (EuroLLM), dont il place l'espace initial après chaque token de contrôle comme le font
les tokenizers de référence ; les identifiants de tokens des tests sont ceux de llama.cpp.

Un modèle dont le template ouvre le raisonnement de chaque réponse, les variantes Thinking de Qwen3 et de
Qwen3-VL et Foundation-Sec-8B-Reasoning, a été entraîné à raisonner avant chaque réponse, et son prompt
ouvre ce raisonnement quoi que demande la requête. Quand la requête ne demande aucun raisonnement, le moteur
le termine aussitôt en écrivant à la place du modèle la phrase que ses auteurs donnent pour le budget d'un
raisonnement (« Considering the limited time by the user, I have to give the solution based on the
thinking directly now. ») et le marqueur `</think>` ; le modèle répond alors directement, le prompt
restant dans ce qu'il a appris, là où un raisonnement fermé vide avant de commencer amenait sa réponse à
reprendre les corrections et les retours d'un raisonnement, ce qui pouvait tourner en boucle. Avec l'effort `low`, le
moteur termine le raisonnement après 512 tokens, 256 pour le travail en arrière-plan ; sans budget
ni effort élevé, le raisonnement prend au plus la moitié de `max_tokens`, 32 768 tokens au plus, pour que
la réponse garde de la place après lui ; et `reasoning_budget` fixe cette limite pour toute requête, -1 ou null
la levant. La fin qu'écrit le moteur compte
parmi les tokens de la réponse et reste hors du `reasoning_content` que reçoit le client ; la
réponse d'une requête qui ne demandait aucun raisonnement reçoit son `max_tokens` une fois le raisonnement terminé,
et ses séquences d'arrêt ne s'appliquent qu'à cette réponse. Le prompt de
Foundation-Sec-8B-Reasoning ouvre son raisonnement par `<think>`, comme le fait le template actuel du modèle ;
le template de son fichier GGUF, plus ancien, laisse ce marqueur au modèle.

Le moteur surveille séparément les répétitions du raisonnement et de la réponse : une partie tourne en boucle quand neuf
dixièmes de ses 384 derniers tokens terminent un passage de 16 tokens qu'elle a déjà écrit deux fois. Un raisonnement
qui tourne en boucle se termine comme un raisonnement qui a atteint son budget, et le modèle répond ; une réponse qui tourne en boucle
se termine avec la raison `length` et le champ `repetition`, et l'application web indique que le
modèle s'est répété. Un passage réécrit une fois, un code montré avant et après sa correction,
ne compte pas ; ni les passages du prompt, pour qu'une réponse qui reprend un document ou
du code qu'on lui a donné continue, ni ceux du raisonnement pour la réponse, qui peut reprendre son brouillon.

### Images

Un modèle lit les images quand son décodeur prend les tokens d'un encodeur d'images, le projecteur : un
second fichier GGUF (`mmproj`, architecture `clip`) que le catalogue télécharge avec le décodeur et
que le moteur charge avec lui (option `projector` du modèle). Le moteur lit trois familles
([`engine/vision_encoder.cpp`](engine/vision_encoder.cpp)) :

- Qwen3-VL (projecteur `qwen3vl_merger`) : l'image, redimensionnée à des multiples de 32 pixels, est découpée en
  patchs de 16 pixels, fusionnés par carrés de quatre en tokens du décodeur ; les positions
  apprises sont interpolées sur la grille des patchs, et chaque tête de l'attention fait tourner
  ses paires selon la ligne et la colonne de son patch. Le décodeur place ces tokens par des
  positions rotatives multimodales (temps, ligne, colonne) et ajoute les sorties de trois fusionneurs DeepStack
  à ses états cachés après ses premières couches. Le niveau de détail d'une image donne le nombre de tokens qu'elle prend
  au plus : 256 (`low`), 576 (`auto`) ou 1024 (`high`).
- LLaVA-NeXT (projecteur `mlp` avec pinpoints, EuroVLM) : un encodeur SigLIP lit l'image entière
  redimensionnée en carré de 384 pixels, puis les tuiles de 384 pixels de l'image redimensionnée dans la
  résolution des pinpoints qui en garde le plus, son rapport d'aspect conservé ; les tokens sont
  ceux de l'image entière, puis la grille des tuiles sans ses bandes noires, chaque ligne terminée par
  un embedding de saut de ligne. Le niveau de détail d'une image donne le nombre de tuiles qu'elle prend au plus, et ses tokens : 1
  tuile et 1 485 tokens (`low`), 2 et 2 241 (`auto`) ou 4 et 3 753 (`high`).
- Pixtral (projecteur `pixtral`, Ministral 3 et Devstral Small 2) : l'image, son plus long côté
  ramené à 1 540 pixels au plus et ses côtés arrondis au multiple de 28 pixels supérieur, est découpée en
  patchs de 14 pixels ; chaque tête de l'attention fait tourner ses paires de dimensions consécutives selon
  la ligne et la colonne de son patch, les blocs normalisent par leur moyenne quadratique et utilisent une
  porte SiLU, et le projecteur fusionne les carrés de quatre patchs en tokens du décodeur,
  chaque ligne de tokens suivie de l'embedding `[IMG_BREAK]` et l'image du token
  `[IMG_END]`. Le niveau de détail d'une image donne le nombre de tokens fusionnés qu'elle prend au plus : 256 (`low`), 576
  (`auto`) ou 1024 (`high`). Un message d'un texte et d'une image met l'image en premier, comme le font les
  chat templates de ces modèles. Les projecteurs stockés en bfloat16 sont convertis en demi-précision
  à leur chargement, ce qui conserve leurs valeurs.

Le service ne lit rien sur le réseau ni sur le disque pour une requête : une image arrive sous forme de data URL
d'un fichier PNG ou JPEG (20 Mo, 8 192 pixels de côté et 40 mégapixels au plus), décodée par les
copies de libpng et de libjpeg de dlib, son orientation EXIF appliquée et sa transparence composée
sur du blanc, puis redimensionnée comme le fait le prétraitement de référence (le filtre bicubique de Pillow pour
Qwen3-VL et Pixtral, son filtre bilinéaire pour LLaVA-NeXT, en virgule fixe). Une requête porte 16
images au plus. L'encodage d'une image s'exécute entre les passes des autres requêtes, un bloc
de l'encodeur à la fois, pour que les réponses en cours d'écriture avancent après chaque bloc (quelques secondes
au plus pour une grande image sur deux cœurs), et est conservé selon l'empreinte de ses pixels
(256 Mo d'encodages par modèle) : les tours suivants d'une discussion lisent l'image sans l'encoder
de nouveau, et les tokens de l'image entrent dans le cache des prompts sous des clés faites de cette empreinte,
pour qu'une conversation qui répète ses images prenne leurs clés et valeurs dans le cache. Sur deux
cœurs, Qwen3-VL-2B encode une image de 576 tokens en 9,4 s (llama.cpp : 9,1 s) et la décrit comme
le fait llama.cpp à température 0 ; Ministral 3 3B encode une image de 414 tokens fusionnés en 16,7 s
(llama.cpp : 14,4 s) et lit ses formes, leurs couleurs et son mot comme le fait llama.cpp.

### Service

`albireo-service.exe` s'exécute en tant que service Windows `Albireo` (LocalSystem, démarrage automatique différé)
et, pour le développement, dans une console avec `--console`. Il :

- lit les réponses à voix haute avec `albireo-speech.exe`, un programme qui contient le synthétiseur SVOX Pico
  et rien d'autre, et ses voix de six langues (le dossier `speech` de
  l'installation, 6,5 Mo). Le service le lance pour chaque lecture dans le dossier `speech`, sans
  console, avec un jeton de faible intégrité et sans privilèges, dans un job limité à 256 Mo qui
  lui refuse tout processus enfant, pour qu'une faute du synthétiseur n'atteigne jamais le service ;
  le programme ne charge aucune bibliothèque des fenêtres de Windows, qu'un processus de faible intégrité
  lancé par un service ne pourrait pas initialiser. Le texte qu'il lit ne garde que les lettres, les chiffres
  et la ponctuation usuelle, pour qu'aucun balisage du synthétiseur ne l'atteigne, et deux de ces
  programmes tournent à la fois ;
- décrit chaque bibliothèque de moteur dans un processus enfant (`--probe-engine`), pour qu'une bibliothèque qui
  ne peut pas se charger laisse le service tourner, et les mesure de la même façon (`--bench-engine`, voir
  [Mesures des moteurs](#mesures-des-moteurs)) ; une
  description prend 60 secondes au plus, et sa ligne de journal donne les périphériques que chaque moteur a trouvés et,
  pour ceux que son environnement d'exécution a écartés, la raison qu'il donne (le message de chaque plugin
  d'OpenVINO, par exemple) ;
- démarre le moteur choisi et les modèles actifs, et sert `https://localhost:<port>/` avec
  cpp-httplib et Mbed TLS, avec un certificat émis par sa propre autorité, qu'il ajoute aux
  racines de confiance de la machine quand l'installation l'a choisi (voir [Sécurité](#sécurité)) ;
- garde ses réglages dans `settings.json`, les modèles, les téléchargements et l'utilisation dans une base
  SQLite, et son journal dans `logs\service.log` ;
- redémarre de lui-même quand un réglage l'exige, dans un nouveau processus dès que le moteur d'un périphérique a
  tourné, puisque cette bibliothèque reste chargée avec les threads de son environnement d'exécution. Un processus qui a chargé le
  moteur d'un périphérique, processus enfants des descriptions et des mesures compris, se termine
  sans le décharger : Windows décharge une bibliothèque sous son verrou de chargeur, où les plugins
  d'OpenVINO attendraient indéfiniment des threads de leur environnement d'exécution.

Le serveur répond dès que son port est ouvert : les moteurs sont décrits et celui qui est choisi
démarre en arrière-plan, puis les modèles actifs se chargent. Jusque-là, `/albireo/v1/status` donne l'état
`starting` avec l'étape en cours (`engines`, puis `models`), les requêtes destinées à un modèle répondent
503 avec le code `starting` ou `model_loading` et un en-tête `Retry-After`, et l'application web
indique que le service démarre. Une requête dont le moteur ne peut pas encore contenir les images, celles déjà
en file pour son modèle occupant la mémoire qui leur est réservée (1 Gio de pixels décodés), répond 503 avec
le code `engine_busy` et un `Retry-After` de 5 secondes. Un ordinateur lent, après une installation ou au début de la
session, montre ainsi un démarrage en cours au lieu d'un échec.

### Icône, installateur et application web

`albireo-tray.exe` place l'icône dans la zone de notification et ouvre les fenêtres des modèles,
de l'ordinateur (processeur, moteur, mémoire étendue avec son utilisation, dossier des modèles, port, langue,
proxy) et du produit. Tant que le service ne répond pas, l'icône lit son état dans le
gestionnaire de contrôle des services (Service Control Manager) : un service qui démarre, ou un démarrage automatique différé qui n'est pas encore venu
à l'ouverture de la session, affiche l'icône d'un démarrage en cours, et l'icône avertit que le
service ne répond pas, une fois par épisode, seulement après un délai de grâce adapté à cet état : 10
minutes pour un service en cours de démarrage, 5 minutes pour un service en marche qui n'a pas encore répondu, 15
minutes pour un démarrage automatique différé pendant les 15 premières minutes après l'amorçage, 2 minutes pour un
service qui s'arrête, 30 secondes pour un service qui a déjà répondu, une minute pour un service
arrêté. L'application ouverte
depuis l'icône pendant un démarrage s'ouvre dès que le service répond. Le service donne ses erreurs avec un code et ses valeurs
([`host/user_error.h`](host/user_error.h)), et les moteurs les raisons pour lesquelles ils ne peuvent pas s'exécuter
(`reason_code`), que les fenêtres expliquent dans la langue de l'interface ; un modèle par défaut
qui n'a pas pu être téléchargé montre son téléchargement en erreur, avec la raison, dans la fenêtre Modèles.
Les réglages de la mémoire étendue s'appliquent au moteur CUDA et sont grisés sans lui. Le
menu Accélérateur propose toujours le choix automatique et le processeur seul, puis une entrée par
périphérique trouvé par les moteurs, nommée d'après lui et son environnement d'exécution (« GPU NVIDIA GeForce RTX 5070 Ti Laptop
GPU (CUDA) », « GPU Intel(R) Graphics (OpenVINO) », « NPU Intel(R) AI Boost (OpenVINO) », « NPU + GPU
Intel (OpenVINO) »), qui fixe le moteur et l'accélérateur des réglages ; un moteur de périphérique
qui ne peut pas s'exécuter ici, ou un périphérique que son environnement d'exécution a écarté, apparaît grisé avec sa raison, comme dans la
fenêtre de l'ordinateur. La fenêtre Modèles donne pour chaque modèle son nom, l'entreprise qui
l'a fait, ses fonctions, ce qu'il lit, la taille de ses fichiers (projecteur compris) et son
état ; un clic sur le titre d'une colonne trie la liste selon elle, une flèche montrant l'ordre, qu'un
second clic inverse et que le démarrage suivant conserve (`HKCU\Software\Albireo`), les modèles
classés à égalité restant dans l'ordre du catalogue. La fenêtre de l'ordinateur, la fenêtre Modèles
et le menu mettent à jour leurs listes sur place, sans scintillement, et l'icône garde sa connexion au
service entre deux interrogations. `albireo-setup.exe` porte les programmes sous forme de charge utile ZIP, les installe
avec un assistant ou en silence, enregistre le service et l'entrée d'Applications et fonctionnalités, et les désinstalle.
L'application web ([`web/`](web)) est faite de simples modules ES avec ses bibliothèques dans
[`web/vendor`](web/vendor), servie par le service avec une politique de sécurité du contenu qui n'autorise aucune
autre origine. Le navigateur vérifie chaque fichier de l'application à chaque chargement (`Cache-Control:
no-cache`, auquel répond 304 tant que le fichier est inchangé), pour que les pages et les scripts d'une
mise à jour apparaissent aussitôt. L'application fonctionne sans aucun réseau : elle ne charge rien d'une autre
origine, ses polices, styles, scripts, les tables de caractères de pdf.js et la page de l'interface
(`/docs/`) compris, et une image que le markdown d'une réponse prendrait sur un autre site est
affichée sous forme de lien. La réponse en cours d'écriture est dessinée dix fois par seconde au plus, bloc par bloc : les
blocs et les étapes de l'agent déjà écrits restent tels quels, un bloc de code est coloré
dès que sa clôture se ferme, le raisonnement reçoit son nouveau texte sur place, et les animations de la page
ne changent que l'opacité et la position de leurs éléments, que le compositeur du navigateur
dessine sans repeindre la page. Le fil garde ses dernières lignes au-dessus de la zone de saisie quelle que soit
la hauteur de celle-ci. Chaque discussion garde son propre brouillon dans la zone de saisie, son texte et les
documents et images en attente d'envoi, pendant qu'une autre discussion est affichée, et « Nouvelle discussion » garde
le sien jusqu'à ce que sa première question crée la discussion : les textes restent après un rechargement de la page
dans le stockage du navigateur (`localStorage`, une clé par discussion, retirée dès que sa question est
envoyée ou la discussion supprimée), les fichiers pour l'onglet, et une discussion en navigation privée n'en garde aucun.

### Documents et contexte

Le texte d'un document est extrait dans le navigateur ([`web/js/documents.js`](web/js/documents.js)) :
les formats Office et OpenDocument sont des archives ZIP de XML, lues avec la décompression du
navigateur dans la limite de 256 Mio de XML extrait par document (une entrée qui grossit de plus de 200 fois
est refusée), et le PDF passe par pdf.js. Le texte marque chaque page, diapositive et feuille d'une ligne qui
commence par `## `, qui nomme les passages de la recherche et permet aux outils de l'agent de lire un
document partie par partie. Rien ne quitte le navigateur, sauf le texte placé dans les messages.

Les fichiers PDF sont lus par la version héritée (legacy) de pdf.js, qui fonctionne dans Edge et dans Chrome 125 et suivants,
avec les tables de caractères d'Adobe pour les polices chinoises, japonaises et coréennes que les fichiers
n'embarquent pas ([`web/vendor/pdfjs`](web/vendor/pdfjs)). Un fichier chiffré avec un mot de passe vide se lit
comme n'importe quel autre ; un fichier protégé par un mot de passe, ou endommagé, est refusé avec un message. Un PDF fait
uniquement d'images, comme un document numérisé, n'a pas de texte à extraire : pour un modèle qui lit les images, l'application
propose de joindre ses pages sous forme d'images, dans la limite des places que la discussion a laissées ; pour
un autre modèle, une boîte de dialogue indique que son texte ne peut pas être lu. Un PDF dont certaines pages ne contiennent que des images
est ajouté avec un avertissement donnant le nombre de ces pages, dont le texte manque. Les mêmes règles s'appliquent
aux fichiers joints à une question et à la base de connaissances d'un projet. Un document de plus de
100 Mo et une image de plus de 15 Mo, joints à une question ou ajoutés à un projet, sont refusés
avant d'être lus, avec un message qui nomme le fichier et la limite. La base de connaissances d'un projet
prend 100 éléments au plus, documents et images ensemble : sa carte en affiche le nombre, son bouton
se désactive une fois ce nombre atteint, et les fichiers d'une sélection ou d'un dépôt au-delà sont écartés avec
un message qui les compte et nomme les premiers. Un projet d'une version antérieure qui en contient
davantage les garde, et n'en prend aucun autre tant qu'il n'en contient pas moins de 100. Les ajouts d'une page s'appliquent
l'un après l'autre, chacun au projet tel que l'a laissé le précédent.

Les images d'une question ([`web/js/images.js`](web/js/images.js)) sont réduites dans le navigateur à
1 536 pixels au plus sur leur plus long côté et conservées dans sa base de données ; le message les donne avant
son texte. Une discussion garde au plus dix images dans son contexte : la zone de saisie montre les places restantes,
et les images des échanges qu'une compression a résumés quittent le contexte avec eux et
libèrent leurs places. Les images d'un projet, dix au plus, sont décrites par le modèle d'arrière-plan à
leur arrivée, leur nature et leur sujet en une phrase, puis le texte qu'elles montrent ; ces descriptions
sont cherchées avec les documents, et une question dont les extraits viennent d'une image reçoit
l'image elle-même, quatre au plus, quand son modèle lit les images. Un modèle qui ne lit que du texte reçoit
les noms des images.

La recherche ([`web/js/rag.js`](web/js/rag.js)) découpe les documents en passages de 1 200
caractères qui se chevauchent de 240, nommés d'après leur document et leur partie. Le modèle d'embedding du
service calcule le vecteur de chaque passage une seule fois, par `/v1/embeddings`, et les passages
sont conservés avec leurs vecteurs dans la base de données du navigateur (IndexedDB), ou en mémoire pour une
discussion en navigation privée. Un titre commence un passage à part, sans le chevauchement du
précédent, et nomme la section des passages qui suivent ; une ligne courte qui ne tient pas, comme une
ligne d'une feuille de calcul, commence entière le passage suivant. Une question est comparée aux passages par le sens
(cosinus des vecteurs) et par les mots (BM25), chaque classement portant sur les passages proches de son meilleur : un
cosinus de 0,35 au moins et à moins de 0,2 du meilleur, mesuré avec Qwen3-Embedding (0,2 à 0,3
pour les passages qui ne répondent pas, 0,6 et plus pour celui qui répond), et un score en mots d'au moins 30 %
du meilleur, pour que « Merci, c'est parfait ! » ne reçoive aucun extrait. Les deux classements sont
fusionnés (fusion des rangs réciproques), les passages choisis de façon à ne pas se répéter
(pertinence marginale maximale), le passage qui suit chacun des trois meilleurs ajouté dans
la limite du budget, les passages consécutifs d'un document fusionnés en un seul extrait qui donne une seule fois les noms
des colonnes d'une feuille, et les documents donnés dans l'ordre de leur meilleur extrait. Les
passages que les questions du contexte ont déjà apportés ne sont pas redonnés, et une question de
moins de six mots est cherchée avec le début de la précédente (« et en 2025 ? »). Un
document joint à une question est indexé avant sa réponse pendant 15 secondes au plus : les passages
dont les vecteurs ne sont pas encore calculés sont cherchés par leurs mots pendant que la file les calcule, et
une indexation interrompue (une réponse arrêtée, un service occupé, un nouveau modèle d'embedding) reprend plus tard
à partir des vecteurs déjà calculés. Le moteur calcule les vecteurs de tous les clients dans les mêmes
passes et garde ces passes courtes pendant qu'une conversation reçoit sa réponse, pour que l'indexation de documents
ne retarde jamais une réponse. La page d'un projet liste chaque document avec son format, l'estimation de
ses tokens et le nombre de ses passages (« MD · ≈ 265 tokens · 3 passages », « (by words) » tant
qu'ils n'ont pas de vecteurs du modèle d'embedding), et sa carte de la base de connaissances prend les fichiers déposés
sur elle comme le fait son bouton ; un fichier déposé là où rien ne le prend est refusé au lieu d'être ouvert
par le navigateur. Les nombres de l'interface prennent leur singulier et leur pluriel dans chaque langue
(`tn()` de [`web/js/i18n.js`](web/js/i18n.js), via `Intl.PluralRules`), et ses décimales et ses
tailles les formes de la langue (« 23,5 jetons/s », « 2,4 Mo »).

Le prompt d'une question est construit pour le cache du moteur, qui garde les clés et valeurs de
chaque conversation et ne recalcule que les tokens qui diffèrent de ce qu'il a déjà lu
([`web/js/app.js`](web/js/app.js)) :

L'application dimensionne ses choix à partir du contexte de travail du modèle : son contexte, tel que le
moteur le donne, dans la limite de ce que l'ordinateur lit en quatre minutes à la vitesse que le moteur a mesurée
sur les réponses du modèle (`prompt_tokens_per_second` des modèles chargés, arrondi à la
puissance de deux inférieure, 128 tokens par seconde pour les moteurs processeur tant que le modèle n'a pas répondu), 16 384
tokens au moins, arrondi à une puissance de deux, pour qu'une nouvelle mesure ne change les choix que lorsque
la vitesse double ou diminue de moitié : 32 768 tokens à 128 tokens par seconde, 65 536 à 256, et les
262 144 de Qwen3-VL 4B à partir de 1 024. Le moteur garde les clés et valeurs des discussions lues,
si bien qu'une discussion ne relit tout son contexte que lorsqu'elles ont quitté la mémoire ; les documents
lus en entier et les extraits ont leurs propres temps de lecture, puisqu'une question les attend.
Un document peut encore être plus long que ces parts : le contexte du modèle reste la limite de
ce que contient une question.

| Partie du prompt | Contenu | Part du contexte de travail |
|---|---|---|
| Message système | documents du projet lus en entier, le jour où la discussion a commencé, instructions, mémoire | 30 % et 60 s de lecture au plus, sinon le projet est cherché |
| Mémoire | courtes phrases sur l'utilisateur, telles qu'elles étaient au début de la discussion | 4 % au plus, 400 mots |
| Résumé | les échanges qui ont quitté le contexte, compressés par le modèle d'arrière-plan | 15 % et 60 s d'écriture au plus |
| Document joint | lu en entier | 25 % et 40 s de lecture au plus, sinon indexé et cherché |
| Extraits d'une question | les passages trouvés pour elle, gardés avec le message une fois envoyé | 20 % et 20 s de lecture, 2 000 à 12 000 tokens, 4 à 24 passages |
| Échanges | la conversation, une question posée un jour ultérieur avec sa date | le reste ; les plus anciens partent d'un coup, jusqu'à 30 % de la place laissée par le message système et les outils |
| Réponse | la place qui lui est réservée | la réponse la plus longue des réglages, sinon 8 192 tokens avec le raisonnement approfondi et 2 048 sans, un quart du contexte au plus |

Les tokens d'un document sont estimés d'après ses caractères : 3,6 caractères par token dans les
alphabets latins, trois tokens pour quatre caractères des idéogrammes, des kana et du hangul,
et 2,2 caractères par token pour les autres écritures. Une question qui ne tient pas à côté
des documents de son projet lus en entier les fait chercher, dans sa discussion à partir de ce moment.

Le message système commence par le projet et ses documents, puis garde le jour où la discussion
a commencé, en anglais, et une question posée un jour ultérieur porte sa propre date, pour qu'une
discussion poursuivie le lendemain garde le début de son prompt. Le début du prompt
reste donc le même d'une question à la suivante : chaque question ne coûte que ses propres tokens
(le moteur lit la réponse précédente, telle que le template la rend dans l'historique, pendant que l'utilisateur
la lit), les discussions d'un projet partagent la lecture de ses documents quels que soient leur jour et
leur mémoire (le moteur partage les pages du préfixe en cache entre les séquences), les images
d'un projet trouvées pour une question restent avec elle aux questions suivantes, et
la compression des échanges les plus anciens n'a lieu qu'une fois toutes les quelques questions. Lors des tests, la
deuxième question d'une discussion a trouvé 954 tokens dans le cache et lu son contexte en 0,5 s,
contre 6,4 s pour la première question. Le service sert plusieurs sessions à la fois,
chacune avec son propre contexte : les requêtes de toutes les sessions sont calculées ensemble, une seule passe avant
servant le token suivant de chacune.

Une discussion n'a pas de limite de longueur. Dès que ses échanges prennent la moitié de la place que le message
système et les outils laissent dans le contexte de travail, tels que le service les compte après chaque réponse
et à l'ouverture de la discussion, les échanges les plus anciens sont compressés en arrière-plan, jusqu'à
30 % de cette place, la dernière question restant avec sa réponse : le modèle d'arrière-plan réécrit le
résumé de la discussion avec eux, en parties qui tiennent dans son contexte de travail, les plus anciennes d'abord, en gardant
les faits, les chiffres, les noms, les décisions, les demandes et les questions ouvertes, et la discussion commence alors
par le résumé, donné avec sa première question restante. Les documents lus en entier des
questions qui ont quitté le contexte sont indexés, pour que les extraits des questions suivantes puissent en
venir, et une réponse écrite dans un autre onglet garde le début de son prompt. Une question qui
arrive avant le résumé ne l'attend que lorsqu'elle ne tient pas sans lui, et un résumé écrit
pendant qu'une réponse s'écrit dans la discussion s'applique une fois cette réponse terminée ; un résumé qui
ne peut pas être écrit laisse les échanges partir sans lui. Un seul onglet compresse une discussion à la fois.
Quand la dernière question seule ne tient pas, les textes les plus anciens perdent leur début, à la fin d'une
phrase : le résumé d'abord, puis les documents et les textes des questions précédentes avec les
résultats des outils de leurs réponses, puis le raisonnement et les résultats des outils des étapes
de la réponse en cours d'écriture, puis ces étapes entières, les plus anciennes d'abord et la dernière restant,
et les documents et le texte de la question en dernier. Une réponse de l'agent ajuste son contexte
avant chacune de ses étapes, et une compression des échanges les plus anciens s'y produit quand elle est
nécessaire ; les coupes restent d'une étape à la suivante, et un quart de la place est libéré au-delà de ce qui est
nécessaire dès que ses étapes sont coupées, pour que les étapes suivantes gardent le début de leur prompt. Une
question supprimée d'une discussion part avec sa réponse et les documents qui y étaient joints ; quand
elle était déjà compressée, le résumé est réécrit sans elle.

Un résumé change le début du prompt, que le moteur n'a pas encore lu. Dès qu'il s'applique
et qu'aucune réponse ne s'écrit dans la discussion, l'onglet qui l'a écrit demande au modèle d'arrière-plan
un token après ce début (message système, résumé et échanges gardés, construits comme les construit la question
suivante), pour que le moteur le lise en arrière-plan et que la question suivante
ne calcule que ses propres tokens ; lors des tests, la première question après un résumé a lu 852 de ses 869
tokens dans le cache, en 0,8 s au lieu de 13,8 s. La requête n'est envoyée que lorsque le modèle d'arrière-plan
est le modèle de la fonction de la discussion ; une question, une suppression ou une nouvelle compression
de la discussion l'arrête, et son échec est silencieux.

Le travail que l'application fait en arrière-plan, les titres, les résumés et la mémoire, va
au modèle d'arrière-plan : le modèle livré avec Albireo (`albireo-background`), que le
service garde chargé quelles que soient les fonctions, sans raisonnement, le raisonnement d'un modèle qui
raisonne toujours étant terminé aussitôt ; sans lui, le modèle général s'en charge. Il échantillonne comme le conseillent
ses auteurs, sans la pénalité de présence que certains d'entre eux donnent, pour que les titres, les résumés et les
citations de la mémoire reprennent les mots de la discussion, et un résumé qui répète ses lignes ne garde
chacune d'elles qu'une fois.

La mémoire des discussions contient de courtes phrases sur l'utilisateur, l'une après l'autre en un seul
paragraphe, les faits durables que l'utilisateur énonce sur lui-même et qui aident dans d'autres discussions :
rôle, profession et domaine, organisation, projets et leurs objectifs, langues, outils, frameworks,
systèmes et ordinateurs, niveau d'expertise, habitudes de travail, la façon de répondre (langue, longueur,
format, style) et les instructions permanentes (« toujours… », « jamais… »), sans les demandes ni les
sujets des discussions ; les notes qu'une version antérieure écrivait sous forme de liste deviennent des phrases à
l'ouverture suivante. Elle lit les questions qu'elle n'a pas encore lues, chaque discussion à partir de sa marque, les
discussions modifiées le moins récemment d'abord, en une seule passe dès qu'il y en a assez (trois questions ou
1 200 caractères) et que l'utilisateur est resté inactif pendant 20 secondes, à l'ouverture de l'application
comprise, ce que le modèle d'arrière-plan lit en 30 secondes au plus, les questions suivantes attendant
la passe suivante : le modèle d'arrière-plan n'écrit que les faits nouveaux, dans la langue de l'interface
quelle que soit la langue des discussions, qui rejoignent la mémoire à moins qu'elle ne les contienne déjà en
d'autres mots. La mémoire arrive au modèle numérotée, et un fait nouveau qui en change un (un nouveau
rôle, une autre préférence) donne son numéro et prend sa place ; un fait redit passe à la
fin, pour que les faits que les questions n'apportent plus partent en premier quand la mémoire dépasse sa taille
(400 mots), après une réécriture d'ensemble qui échoue. Les phrases que l'utilisateur ajoute ou modifie dans les
réglages restent, à moins que les mots de l'utilisateur dans une discussion n'en changent une, et les faits que le
modèle apprend gardent au moins la moitié de la mémoire à côté d'elles. Les faits écrits dans
une autre langue sont d'abord réécrits dans la langue de l'interface, phrase par phrase et
numérotés, une phrase que le modèle a omise restant telle quelle, et un changement de cette langue programme une
mise à jour. Un fait nouveau n'entre que lorsqu'il vient avec les mots de l'utilisateur sur lesquels il repose, que
l'application retrouve dans les questions lues, accents mis à part et les morceaux d'une citation raccourcis par des
points de suspension dans leur ordre, dans une phrase qui ne demande rien, ou dans une proposition d'une question qui commence
par dire ce que l'utilisateur est ou fait (« As a C++ developer, how do I…? », « Je suis enseignant,
comment…? »), et dans une proposition qui ne
commence pas par une demande (« Write… », « Crée… », « Explique-moi… ») à moins qu'elle ne contienne une instruction permanente
(« Please always answer in French »), trois mots ou 12 caractères au moins, chaque citation servant un seul
fait : sur les questions des tests, les faits inventés par le modèle livré avec Albireo sont passés
de 7 sur 16 à aucun sur 9.
Une notification, qui disparaît d'elle-même, indique combien de faits ont été ajoutés, et une nouvelle question arrête
une mise à jour en cours, qui revient dès que l'utilisateur est de nouveau inactif. Un seul onglet met à jour la mémoire
à la fois, par un bail de trois minutes dans la base de données du navigateur : un onglet fermé au milieu
d'une mise à jour ne retient rien, le service arrête la requête en moins de deux secondes, et la mise à jour suivante
relit les mêmes questions, puisque les marques ne bougent qu'une fois la mémoire enregistrée ; le bail est
renouvelé avant chaque requête du modèle d'arrière-plan. Les onglets se signalent
les uns aux autres les changements des réglages ; une mise à jour relit la mémoire avant de l'enregistrer et
abandonne son résultat quand un autre onglet a modifié, réinitialisé ou désactivé la mémoire entre-temps. Les notes
écartent les informations sensibles (santé, finances, opinions, identifiants, autres personnes) à moins que
les réglages ne les gardent, et les phrases qui contiennent un mot de passe, une clé ou un numéro de paiement n'entrent jamais
dans la mémoire, à moins que les réglages ne les gardent. Une discussion reçoit la mémoire telle qu'elle était à son début, pour que son
prompt garde le même début ; les discussions privées ne la lisent ni ne l'alimentent. Les réglages
montrent la mémoire, permettent à l'utilisateur de la modifier, de la désactiver et de la réinitialiser. Le modèle d'arrière-plan nomme aussi
chaque discussion d'après sa première question, dans la langue de cette question, à moins que l'utilisateur
ne la renomme : les premiers mots de la question tiennent lieu de titre jusqu'à son arrivée, un titre qui
répète la question est redemandé en d'autres mots, et un titre qui n'a pas pu être écrit (un
modèle qui se charge, un service absent, une page rechargée) est redemandé pendant deux minutes, puis à la
prochaine ouverture de l'application. Un seul onglet nomme une discussion à la fois.

Les listes du panneau latéral montrent les 20 projets et discussions les plus récents ; la fenêtre de recherche
de chacune trouve les autres, les mots quels que soient la casse et les accents, complets ou non, dans les titres d'abord,
puis dans le contenu (questions, réponses, descriptions, instructions et documents des projets),
et montre où elle les a trouvés. La fenêtre À propos montre les tokens lus et générés chaque jour sur
30 jours, chaque barre donnant sa date et ses chiffres dans la langue de l'interface, et, tant que
l'accès au web propose la recherche, une ligne qui nomme Wikipédia (« Web search » et « Wikipedia », en
français « Recherche Web » et « Wikipédia ») ; l'aide à côté des boutons de la zone de saisie la nomme aussi
(« Web search: Wikipedia »), et rien ne la nomme tant que l'accès au web est indisponible. Chaque réponse
s'exporte en Markdown ou en JSON avec sa question, comme la
discussion entière. L'interface prend la langue d'Albireo, réglée dans la fenêtre de
l'ordinateur, à moins que ses propres réglages n'en choisissent une autre. L'onglet Génération des réglages donne la
température, `top_p`, `top_k` et la longueur des réponses, chacun avec une phrase qui dit ce
qu'il change ; une valeur laissée sur automatique garde le choix du service, et le travail en
arrière-plan garde ses propres valeurs.

La page des composants tiers, ouverte depuis À propos, liste les composants de l'application
web ([`web/vendor/components.json`](web/vendor/components.json), que
[`tools/update_web_vendor.sh`](tools/update_web_vendor.sh) tient à jour avec leurs versions),
ceux des programmes et des moteurs, depuis le dossier `licenses` de l'installation, et les
modèles installés, chacun avec sa version, son rôle et sa licence, dont le texte s'ouvre dans la page.

## Arborescence des sources

| Dossier                  | Contenu                                                                      |
|--------------------------|------------------------------------------------------------------------------|
| [`engine/`](engine)      | le moteur d'inférence : environnement d'exécution, noyaux, ordonnanceur, formats de conversation, périphériques |
| [`host/`](host)          | le service : serveur, réglages, stockage, modèles, mises à jour, certificats, accès au web |
| [`common/`](common)      | l'interface C des moteurs, JSON, journaux, détails de la plateforme et du processeur |
| [`windows/`](windows)    | l'icône, l'installateur, leurs fenêtres et leurs textes en cinq langues       |
| [`web/`](web)            | l'application web, la page Swagger et la description OpenAPI                  |
| [`resources/`](resources)| icônes, images, licence, ressources de version, modèles livrés avec Albireo   |
| [`cmake/`](cmake)        | chaîne de compilation MinGW-w64, charge utile de l'installateur, bibliothèques d'exécution des périphériques, téléchargements des modèles |
| [`tests/`](tests)        | tests des noyaux, de l'environnement d'exécution, du tokenizer et des périphériques, pilote du moteur, banc de mesure |
| [`tools/`](tools)        | scripts de build et de test, préparation des modèles livrés avec Albireo, catalogue, flux, publication, paquet hors ligne, images |

## Construire sous Windows

Prérequis : Windows 10 ou 11 sur x64, un accès à Internet, direct ou par un proxy, et
l'espace libre indiqué ci-dessous. Le script [`tools/build_windows.ps1`](tools/build_windows.ps1) commence
par une étape, « vérification des prérequis », qui vérifie ce dont le build a besoin, installe ce qui
manque et affiche un tableau de ce qu'il a trouvé et installé :

- Visual Studio 2022 ou ses Build Tools avec la charge de travail C++ (les outils x64, un SDK Windows, les
  fichiers redistribuables de l'environnement d'exécution C++), trouvés par vswhere. Sans installation, le
  script télécharge les Build Tools depuis `https://aka.ms/vs/17/release/vs_BuildTools.exe`, vérifie
  la signature de Microsoft et les installe ; une installation à laquelle il manque un composant le reçoit
  de son installateur (`setup.exe modify`). Les deux demandent les droits d'administrateur, que Windows
  demande une fois que le script l'a annoncé, et après une nouvelle installation le script s'arrête, pour être relancé
  depuis un nouveau Developer PowerShell for VS 2022.
- CMake 3.21 ou suivant : celui du PATH, sinon les outils CMake C++ de Visual Studio, installés
  avec lui.
- Python 3.10 ou suivant, un interpréteur qui fonctionne, l'alias du Microsoft Store étant ignoré :
  `winget install --id Python.Python.3.12 --scope user`, sinon l'installateur de python.org 3.12.10,
  dont le SHA-256 est vérifié, tous deux pour l'utilisateur seul.
- L'environnement d'exécution d'OpenVINO, sauf avec `-NoOpenVINO` : le paquet de cet interpréteur, installé avec
  `pip install --user "openvino>=2025.0,<2027"`, ou l'archive d'OpenVINO avec
  `INTEL_OPENVINO_DIR` défini.
- CUDA et cuDNN, sauf avec `-NoCuda` : une boîte à outils CUDA 12.8 ou suivante installée avec son intégration
  à Visual Studio et cuDNN 9, sinon les archives redistribuables de NVIDIA, disposées dans un cache
  (ci-dessous). Sans Visual Studio, l'installateur de la boîte à outils CUDA ajoute son intégration aux
  Build Tools.
- L'espace libre de chaque volume qui reçoit les arbres de build et les caches, estimé à 7 Go
  pour l'arbre des programmes et des installateurs, 1 Go pour celui d'OpenVINO, 2 Go pour
  celui de CUDA, 6,8 Go pour les modèles livrés avec Albireo dans leur cache et 4,2 Go pour les
  archives de NVIDIA (1,5 Go pour cuDNN seul), moins ce que ces dossiers contiennent déjà ; le script
  s'arrête quand un volume manque de place.

Une installation qui échoue arrête le build en indiquant l'emplacement de son journal. `-NoInstall` n'installe
rien ; le tableau donne alors la commande qui installe chaque élément manquant, pour un ordinateur où un
administrateur les installe, et le build s'arrête quand l'un d'eux manque. Seul Visual Studio demande les
droits d'administrateur ; Python, OpenVINO et les archives de NVIDIA s'installent pour l'utilisateur. Les
téléchargements prennent le proxy de `HTTPS_PROXY`, sinon celui du système, que le script transmet à
CMake et à pip par `HTTPS_PROXY`, et Windows vérifie les serveurs avec son magasin de certificats, si
bien qu'un proxy d'entreprise qui inspecte le TLS est accepté.

Le moteur CUDA n'a besoin ni d'une carte NVIDIA, ni d'une boîte à outils CUDA, ni des droits d'administrateur. Quand
la boîte à outils, son intégration à Visual Studio ou cuDNN manque, le script prend les
archives redistribuables que NVIDIA publie pour Windows : CUDA 12.9.1 (le compilateur, l'environnement
d'exécution, les en-têtes de CCCL, cuBLAS, cuRAND, cuSOLVER et l'intégration à Visual Studio) et
cuDNN 9.11.0, les versions qui gardent les séries GeForce GTX 900 et 10.
[`cmake/cuda_redist.cmake`](cmake/cuda_redist.cmake) les télécharge une seule fois, 1,6 Go, dans
`%LOCALAPPDATA%\Albireo\build-cache\cuda` (`-CudaCache <folder>` place ce cache ailleurs),
vérifie chacune par rapport au SHA-256 des manifestes de NVIDIA, retélécharge une archive qui ne
correspond pas, et les y dispose sous forme d'une boîte à outils et d'un dossier cuDNN, 4,2 Go avec les archives ; une
marque dans chaque dossier fait que les builds suivants ne téléchargent et n'extraient rien. L'arbre CUDA est alors
configuré avec cette boîte à outils comme boîte autonome, par le jeu d'outils `cuda=<folder>` de Visual
Studio. Une boîte à outils installée avec son
intégration et cuDNN est prise en premier ; elle s'installe aussi sans carte, en installation personnalisée
sans le pilote. nvcc compile le moteur pour chaque architecture du tableau ci-dessous, et seuls
ses tests sur une carte sont sautés. Une machine de build produit ainsi un installateur qui porte tous les
moteurs, quel que soit son propre matériel.

dlib est configuré pour un périphérique par arbre de build, si bien que les moteurs des périphériques sont construits dans leurs
propres arbres et rejoignent l'installateur par `ALBIREO_EXTRA_ENGINE_DIRS`. Le script
[`tools/build_windows.ps1`](tools/build_windows.ps1) exécute toute la séquence depuis la racine du
dépôt :

```
powershell -ExecutionPolicy Bypass -File dlib\dlib\external\albireo\tools\build_windows.ps1
```

Les moteurs OpenVINO sont construits par défaut, pour que chaque installation atteigne le NPU et le
processeur graphique d'une machine Intel ; la vérification des prérequis installe l'environnement d'exécution
d'OpenVINO, et `-NoOpenVINO` construit sans ces moteurs (`-OpenVINO`, qu'exigeaient les versions
antérieures, est toujours accepté). Le moteur CUDA est construit avec la boîte à outils installée, sinon
avec les archives de NVIDIA ; quand un téléchargement, une empreinte ou une extraction échoue, le script dit pourquoi
et continue sans le moteur. `-Cuda` en fait une erreur et `-NoCuda` laisse
le moteur de côté. Sur une machine dotée d'une carte NVIDIA prise en charge, le service préfère le moteur CUDA dans
son choix automatique, puisque la carte prend tous les produits. Les moteurs CUDA, avec cuBLAS et
cuDNN, ajoutent plusieurs centaines de mégaoctets aux installateurs. Un arbre de build
configuré par une installation de Visual Studio supprimée depuis, comme Visual Studio Community
remplacé par les Build Tools, est supprimé et reconfiguré, de même que l'arbre CUDA configuré pour
une autre intégration CUDA (une boîte à outils installée, puis les archives). `-NoBuiltinModels` construit sans
les modèles livrés avec Albireo et `-ModelCache <folder>` place leur cache sur un autre disque.
`-Tests` ajoute les programmes de test, `-Jobs <n>` limite les compilations lancées à la fois sur une machine avec
peu de mémoire, `-CudaVersion 12.9` choisit une boîte à outils CUDA parmi plusieurs installées (les archives servent
sans cette option, ou avec 12.9), `-Cudnn <folder>` une installation de cuDNN pour une boîte à outils installée
(`C:\Program Files\NVIDIA\CUDNN\v9.11` par exemple), et
`-CudaArchitectures` remplace la liste des architectures du moteur CUDA. Parmi plusieurs versions de cuDNN
dans `C:\Program Files\NVIDIA\CUDNN`, le script prend la plus récente antérieure à 9.12 qui sert
la boîte à outils, sinon la plus récente ; quand le cuDNN trouvé est 9.12 ou suivant, une boîte à outils CUDA 12 reçoit
à la place cuDNN 9.11.0 des archives de NVIDIA, 0,6 Go téléchargés une fois et 1,4 Go dans le cache,
sauf si `-Cudnn` ou `-CudaArchitectures` est donné. Chaque configuration fixe les architectures, la
liste donnée ou celle par défaut, jamais une liste qu'une configuration antérieure aurait gardée, et la configuration
et la fin du script avertissent quand le moteur CUDA laisse de côté les séries GTX 900 et 10. Après qu'un
arbre de périphérique a été reconstruit à la main, supprimer `build-albireo/payload` pour que l'installateur réemballe ses
moteurs. Le script revient à :

```
cmake -S dlib/dlib/external/albireo -B build-albireo-ov -G "Visual Studio 17 2022" -A x64 -DALBIREO_PROGRAMS=OFF -DALBIREO_ENGINE_OPENVINO=ON
cmake --build build-albireo-ov --config Release
cmake -S dlib/dlib/external/albireo -B build-albireo-cuda -G "Visual Studio 17 2022" -A x64 -DALBIREO_PROGRAMS=OFF -DALBIREO_ENGINE_CUDA=ON
cmake --build build-albireo-cuda --config Release
cmake -S dlib/dlib/external/albireo -B build-albireo -G "Visual Studio 17 2022" -A x64 "-DALBIREO_EXTRA_ENGINE_DIRS=build-albireo-ov/bin/engines;build-albireo-cuda/bin/engines"
cmake --build build-albireo --config Release
```

Avec les archives de NVIDIA, `cmake -DCACHE=<cache> -P dlib/dlib/external/albireo/cmake/cuda_redist.cmake`
les dispose d'abord, et l'arbre CUDA est configuré avec `-T cuda=<cache>\cuda-12.9.1`,
`-DCUDAToolkit_ROOT=<cache>/cuda-12.9.1`, `-DCUDNN_INCLUDE_PATH=<cache>/cudnn-9.11.0-cuda12/include`
et `-DCUDNN_LIBRARY_PATH=<cache>/cudnn-9.11.0-cuda12/lib/x64/cudnn.lib`.

Les installateurs sont `build-albireo\bin\albireo-setup.exe`, complet avec les modèles livrés avec
Albireo, et `build-albireo\bin\albireo-setup-online.exe`, celui des mises à jour ; le script
affiche leur taille et leur SHA-256. Les arbres des périphériques copient à côté de leur moteur
les bibliothèques qu'il charge (OpenVINO avec les plugins du NPU et du GPU, le compilateur du
plugin NPU et l'environnement d'exécution de ses graphes, la table des noyaux réglés du plugin GPU et la
bibliothèque avec laquelle TBB lit les cœurs d'un processeur hybride, ou l'environnement d'exécution de CUDA, cuBLAS et
les parties de cuDNN qu'utilise le moteur, avec l'environnement d'exécution Visual C++) et leurs licences,
que l'installateur place dans son dossier `licenses` ; les mêmes bibliothèques vont à côté de
`tests\test_offload.exe`, avec le plugin CPU d'OpenVINO pour une machine sans NPU ni carte graphique
Intel, et le test s'exécute alors depuis son dossier. Les nombres aléatoires et les solveurs de
CUDA, que dlib lie et que le moteur n'appelle jamais, ne sont chargés qu'à leur première utilisation et sont laissés hors de
l'installation. La configuration s'arrête quand le dossier de l'environnement d'exécution d'OpenVINO ou l'environnement d'exécution Visual C++
de Visual Studio manque, et le build d'un moteur de périphérique s'arrête quand une bibliothèque que porte
l'installateur (OpenVINO, TBB, cuDNN, cuBLAS, l'environnement d'exécution CUDA, l'environnement d'exécution Visual C++) n'est trouvée
dans aucun de ses dossiers ; avec l'archive d'OpenVINO, TBB vient de `runtime\3rdparty\tbb\bin`.

Le moteur CUDA porte le code machine des cartes des ordinateurs de bureau et des portables à partir de la série GeForce GTX
900, et le code intermédiaire de l'architecture la plus récente, que le pilote compile pour
les cartes qui suivront :

| Boîte à outils  | Architectures (`CMAKE_CUDA_ARCHITECTURES`)                             | Cartes                        |
|-----------------|------------------------------------------------------------------------|-------------------------------|
| CUDA 12.8, 12.9 | `50-real 61-real 70-real 75-real 86-real 89-real 120-real 120-virtual` | GTX 900, 10, 16, RTX 20 à 50  |
| CUDA 13         | `75-real 86-real 89-real 120-real 120-virtual`                         | GTX 16, RTX 20 à 50           |

cuDNN 9.12 et suivants ne fonctionnent que sur les cartes Turing et suivantes (séries GTX 16 et RTX). Un build pour les
séries GTX 900 et 10 prend donc CUDA 12.8 ou 12.9 avec cuDNN 9.11 ou antérieur, la version
copiée à côté du moteur, que fournissent les archives de NVIDIA (CUDA 12.9.1, cuDNN 9.11.0). Les
en-têtes et les bibliothèques du cuDNN donné à la configuration passent avant ceux de la
boîte à outils, dont le dossier peut porter une autre version de cuDNN. La configuration affiche la boîte à outils,
la version de cuDNN et la plus ancienne
capacité de calcul (compute capability) du résultat, que le moteur vérifie aussi à son démarrage : une carte en dessous est
signalée comme inutilisable, avec sa capacité de calcul, et les autres moteurs servent les modèles.

| Option CMake              | Défaut  | Effet                                                             |
|---------------------------|---------|-------------------------------------------------------------------|
| `ALBIREO_PROGRAMS`        | ON      | service, icône, installateur, moteurs processeur                  |
| `ALBIREO_ENGINE_OPENVINO` | OFF     | moteurs OpenVINO (dlib avec `DLIB_USE_NPU`), un par famille de noyaux |
| `ALBIREO_ENGINE_CUDA`     | OFF     | moteur CUDA (dlib avec `DLIB_USE_CUDA`)                           |
| `ALBIREO_ENGINE_DLIB`     | OFF     | moteur dont les produits passent par le code hôte de dlib (tests) |
| `ALBIREO_TESTS`           | ON      | programmes de test, dans `<build>/tests`                          |
| `ALBIREO_EXTRA_ENGINE_DIRS` |       | dossiers de moteurs construits dans d'autres arbres, ajoutés à l'installateur |
| `ALBIREO_COPY_RUNTIME`    | ON sous Windows | copie des bibliothèques d'exécution à côté des moteurs des périphériques |
| `ALBIREO_BUILTIN_MODELS`  | ON sous Windows | modèles livrés avec Albireo, dans `bin/models` et l'installateur complet |
| `ALBIREO_MODEL_CACHE`     | cache de l'utilisateur | modèles livrés avec Albireo téléchargés et convertis |
| `ALBIREO_BUILTIN_MANIFEST`| `resources/builtin_models.json` | les modèles livrés avec Albireo     |
| `ALBIREO_BUILTIN_TOOL`    |         | `albireo-builtin` de la machine de build, pour un build croisé    |
| `ALBIREO_RUNTIME_LICENSES`|         | fichiers de licence supplémentaires pour le dossier `licenses`    |

Les programmes lient leur environnement d'exécution C et C++ de façon statique et ne dépendent que de bibliothèques du système ; le
moteur OpenVINO utilise l'environnement d'exécution partagé des propres bibliothèques d'OpenVINO, installé à côté de lui.

## Construire sous Linux

Le service, les moteurs processeur et les tests se construisent sous Linux, qui sert au développement
(`--console` lance le service dans le terminal, `ALBIREO_HOME` désigne son dossier de données).
[`tools/prerequisites_linux.sh`](tools/prerequisites_linux.sh) vérifie d'abord les outils du build,
CMake 3.21 ou suivant, un compilateur C++14, make ou ninja, Python 3.8 ou suivant, git et curl, et
installe ceux qui manquent avec apt-get (Debian, Ubuntu) ou dnf (Fedora), via sudo, après
avoir nommé les paquets ; `--dry-run` affiche la commande sans la lancer, et
`tools/test_linux.sh` lance le script comme première étape. La même vérification sert avant le build de
la publication sous WSL :

```
bash dlib/dlib/external/albireo/tools/prerequisites_linux.sh
cmake -S dlib/dlib/external/albireo -B build-albireo
cmake --build build-albireo -j
ALBIREO_HOME=$HOME/albireo-data build-albireo/bin/albireo-service --console
```

Les programmes Windows peuvent aussi être compilés en croisé depuis Linux avec MinGW-w64 (threads POSIX), pour
les moteurs processeur :

```
cmake -S dlib/dlib/external/albireo -B build-albireo-win -DCMAKE_TOOLCHAIN_FILE=dlib/dlib/external/albireo/cmake/mingw-w64.cmake
cmake --build build-albireo-win -j
```

## Tests

| Programme        | Vérifie                                                                              |
|------------------|--------------------------------------------------------------------------------------|
| `test_qkernels`  | chaque noyau quantifié par rapport à sa version portable et à une référence en flottants, l'attention par tuiles par rapport à une référence en double précision, et le dépliage de Q6_K bit pour bit identique à dlib ; `test_qkernels_avxvnni` vérifie les noyaux d'AVX-VNNI, et saute les processeurs dépourvus de ses instructions, et `test_qkernels_vnni256` (GCC, Clang) exécute les noyaux VNNI de 256 bits par la forme AVX-512 de leurs instructions |
| `test_runtime`   | les logits d'un modèle par rapport à `tests/reference_forward.py`, et les mêmes résultats quel que soit le découpage des lots |
| `test_tokenizer` | le tokenizer d'un fichier GGUF par rapport à des séquences de tokens de référence  |
| `test_offload`   | les produits et les couches d'un périphérique par rapport aux noyaux du processeur, le chemin à petit budget et le routage mesuré |
| `test_device_ops` | les opérations de dlib sur les poids dans leurs formats GGUF (`tt::gguf_matvec`, `tt::gguf_dequantize`, `tt::rms_norm_rows`, `tt::silu_multiply`) par rapport aux noyaux du processeur, pour chaque format, avec des activations alignées sur 16 octets et non alignées, puis le temps des produits d'un token généré sur les matrices d'un modèle 2B |
| `test_model_file` | un petit modèle Qwen3 construit en mémoire et 144 formes endommagées de celui-ci (dont des noms donnés deux fois et des tenseurs hors de l'alignement), que le chargeur doit toutes refuser sans allouer plus de 256 Mo, puis un fichier creux de plus de 2 Gio dont le dernier tenseur se trouve au-delà de 2 Gio, lu projeté en mémoire et en flux, chaque valeur de ce tenseur vérifiée et les logits comparés à ceux du même modèle dans un petit fichier |
| `test_engine_api` | les refus de l'interface C, trois threads de requêtes avec des annulations sous la pression des clés et valeurs, le déchargement et la destruction pendant des requêtes, un seul événement final par requête, et l'historique d'une réponse lu en avance sur la question suivante |
| `test_chat_format` | les prompts du mode agent et de chaque format de conversation par rapport à ceux de son template rendu par Jinja, les tokens marqueurs et l'analyse des appels d'outils, entiers ou découpés en morceaux, les appels de Mistral avec et sans leur identifiant, les réponses de Llama 3 qui ne sont pas des appels et une réponse antérieure contenant `</think>` |
| `test_host`      | dans un dossier de données à part : les fichiers des téléchargements et des imports par aucun lien, les refus de la route d'import, les copies de modèles déjà installés, les identifiants des imports, le remplacement d'un fichier de modèle, les plages des téléchargements, une base de données endommagée, les réglages d'une version antérieure réécrits sans le lien de publication et les fournisseurs de recherche, et l'accès au web par un serveur local (les adresses publiques de part et d'autre de chaque plage, les adresses refusées avant toute connexion, une page, un texte coupé à 2 Mo, un type refusé, les fichiers PDF au-delà de 20 Mo, une redirection vers une adresse privée, la recherche dans Wikipédia, les refus hors ligne, avec l'accès au web désactivé et sans fournisseur) |
| `engine_chat`    | pilote une bibliothèque de moteur par son interface C : discussion, images avec leur projecteur (`--image`, `--detail`), outils, les résultats des appels d'outils rendus au modèle comme les rend le mode agent (`--tools --tool-result`), raisonnement et son budget (`--budget`), l'échantillonnage que conseillent les auteurs du modèle (`--temperature auto`) et une pénalité de présence (`--presence`), requêtes parallèles, tours avec pauses (`--turns --pause`), embeddings comparés à une référence ; il se termine sans décharger le moteur d'un périphérique, comme le fait le service |
| `bench_runtime`  | tokens par seconde du prompt et de la génération, aussi sous la forme `bench_runtime_avxvnni` et `bench_runtime_vnni256` |

`test_offload model.gguf CPU` valide le moteur OpenVINO sur une machine sans NPU, avec le plugin
CPU d'OpenVINO ; `ALBIREO_ENGINE_DLIB=ON` construit les mêmes tests sur le code hôte de dlib, qui
exécute les couches d'un modèle dense comme le fait la carte. Un quatrième argument donne les mégaoctets des
copies de sa première passe, pour que les couches d'un modèle plus grand s'y exécutent sur un ordinateur dont
le budget automatique ne les contient pas : `test_offload qwen3-vl-4b-thinking-albireo.gguf "" 2 2400`.

Deux scripts lancent ces programmes et le service, puis affichent une ligne par étape avec son résultat :

```
bash dlib/dlib/external/albireo/tools/test_linux.sh
powershell -ExecutionPolicy Bypass -File dlib\dlib\external\albireo\tools\test_windows.ps1
```

Ils prennent `~/models/Qwen3-0.6B-Q4_K_M.gguf` et `~/models/Qwen3-Embedding-0.6B-Q8_0.gguf` par
défaut et sautent les étapes dont le modèle ou l'arbre de build manque. Le script Linux lance un service
hors ligne dans un dossier de données à part, sur le port `ALBIREO_TEST_PORT` (18443 par défaut), lui envoie
le modèle par la route d'envoi, vérifie que la route des chemins refuse un fichier ordinaire, lance
les tests du service (`test_host`), pose une question par l'interface OpenAI et fait lire
une phrase à voix haute ; le script Windows demande au service installé une question, les vecteurs de deux
textes et une phrase lue à voix haute, et teste les moteurs OpenVINO et CUDA quand leurs arbres existent
(`-Device GPU` ou `-Device NPU` pour `test_offload`, CPU par défaut). Les deux font lire une phrase par
`albireo-speech` de l'arbre de build. Les étapes de moteur du script Windows s'exécutent avec le `PATH` du
service, System32 et le dossier de Windows seulement ; le script Linux laisse 30 secondes au service
pour s'arrêter, puis le tue. Leur code de sortie est le nombre d'étapes échouées.

## Installation

Un build donne deux installateurs. `albireo-setup.exe` est complet : il porte les modèles livrés
avec Albireo et installe un ordinateur qui répond aussitôt, sans réseau. `albireo-setup-online.exe`
est le même programme sans eux : les mises à jour le prennent, et sur un nouvel ordinateur le service
télécharge les modèles par défaut du catalogue. Windows n'exécute aucun programme de 4 Gio ou plus : quand le
programme et ses modèles atteindraient cette taille, comme le fait l'installateur complet avec les bibliothèques de
CUDA et d'OpenVINO, ses modèles se trouvent à côté de lui dans `albireo-setup.models`, que l'installateur lit
quand il n'en porte aucun ; les deux fichiers voyagent ensemble et sont renommés ensemble
(`albireo-setup-complete-<version>.exe` et `albireo-setup-complete-<version>.models`).

L'assistant demande la langue, la licence, les dossiers des programmes, des données et des
modèles, les options (démarrage avec Windows, raccourci sur le bureau, confiance dans le certificat local,
téléchargement du modèle par défaut, dont l'installateur complet n'a pas besoin, port), puis installe,
enregistre le service et lance l'icône. La ligne de commande couvre les autres cas ; les options
qu'elle ne donne pas gardent les valeurs de l'installation en place :

```
albireo-setup /silent [/dir <folder>] [/data-dir <folder>] [/models <folder>] [/port <port>] [/offline]
albireo-setup /uninstall [/silent] [/keep-data]
albireo-setup /models-dir <folder>
albireo-setup /service start|stop|restart
albireo-setup /lang en|fr|de|it|es
```

Le dossier des données, `C:\ProgramData\Albireo` par défaut, peut être placé ailleurs à
l'installation. Son emplacement est enregistré pour la machine (`DataDir` sous `HKLM\Software\Albireo`,
que les programmes lisent avant la variable système `ALBIREO_HOME`), avec l'emplacement des modèles
(`ModelsDir`) et le fait que l'installateur a créé ce dossier ou non (`ModelsDirOwned`), pour que le
service, l'icône, les mises à jour et la désinstallation les trouvent ; une mise à jour les garde. Une installation silencieuse qui
refuse un dossier dit pourquoi dans `%TEMP%\albireo-setup.log` et ne laisse rien derrière elle ; une nouvelle installation
qui échoue, démarrage de son service compris, retire les dossiers qu'elle a créés et vide ceux qu'elle
a trouvés vides, son journal allant alors dans `%TEMP%\albireo-setup.log`. La fin de
chaque installation, mise à jour ou désinstallation est écrite dans `<data>\logs\setup-result.json`, que
l'icône relancée par l'installateur signale (`--setup-result`).

L'installateur lancé depuis un paquet hors ligne (voir [Ordinateurs sans réseau](#ordinateurs-sans-réseau))
copie dans le dossier d'import, que le service lit à son premier démarrage, le catalogue du
paquet (non signé dans le fork macOS) et les modèles que liste ce catalogue, chacun vérifié
par rapport à son empreinte. L'option « Cet ordinateur n'a pas d'accès à Internet » de l'assistant, ou `/offline`,
met le service hors ligne dès le départ.

Le dossier des données doit se trouver sur un disque local fixe, ce que sont la plupart des disques durs et SSD externes
et pas la plupart des clés USB (`folder_not_fixed`), ceux des programmes et des modèles sur un
disque local, chacun sur un disque qui répond (`folder_drive_unavailable` pour un disque verrouillé par
BitLocker, vide ou pas prêt) et dont le système de fichiers conserve les permissions, NTFS ou ReFS ; l'installateur
refuse un dossier d'un disque exFAT ou FAT32 (`folder_no_permissions`, qui nomme le système de fichiers),
une racine de disque, les dossiers de Windows et des utilisateurs, un dossier qui les contient, un lien créé par
un autre compte, un dossier de programmes ou de données sur le chemin duquel un autre compte pourrait renommer un dossier
(`folder_parent_unsafe`, qui nomme ce dossier), ou dont la racine du disque appartient à un autre compte
ou permet à d'autres comptes de supprimer ou de renommer ce qu'elle contient, comme le fait une racine ouverte à tous
(`folder_root_unsafe`), un dossier de programmes ou de données qui n'est ni vide ni une installation
d'Albireo, ou qui se trouve dans Windows ou dans les profils des utilisateurs, et un dossier de modèles à l'intérieur du
dossier des programmes, ou qui le contient (`folder_overlap`), dans l'assistant comme avec
`/silent /models` et `/models-dir`. Sur un disque NTFS externe dont la racine garde les permissions que Windows
donne à un disque qu'il formate, les dossiers des programmes, des données et des modèles peuvent se trouver sous
un nouveau dossier à la racine, `E:\Albireo` par exemple, que l'installateur crée avec eux. Il
donne aux dossiers qu'il crée ou dont il prend possession, et aux dossiers parents qu'il crée pour les programmes,
les données et les modèles, les droits du dossier des données (écriture pour les administrateurs et le
service seulement), revérifie le chemin des programmes et des données une fois leurs dossiers créés, pour
qu'un dossier qu'un autre compte y aurait créé entre-temps arrête l'installation, et enregistre les fichiers
qu'il installe dans `albireo-files.json`. Le service refuse les mêmes disques sans permissions pour
son dossier de données et son dossier de modèles, un dossier de modèles placé là laissant les modèles au dossier
des données, et ne charge un modèle que depuis un dossier que seuls SYSTEM et les administrateurs peuvent modifier,
projecteur compris : un modèle laissé dans un dossier où un autre compte peut écrire, ou dans un
dossier mis à la place du dossier des modèles, reste non chargé avec cette raison.

La désinstallation arrête l'icône de chaque session, le service et les programmes du
dossier d'installation, retire la confiance dans l'autorité locale et les certificats, puis
supprime tout le dossier d'installation, fichiers ajoutés depuis compris, et elle-même, par
une copie qui termine le travail une fois qu'elle s'est fermée. Elle supprime ensuite tout le dossier des données, sauf si la
case de la page de désinstallation, ou `/keep-data`, garde les modèles, les réglages et les journaux pour
une installation ultérieure, qui les retrouve (`/purge`, dont avaient besoin les versions antérieures, est
accepté et correspond au comportement par défaut). L'entrée des programmes installés est retirée en dernier, pour qu'une
désinstallation interrompue puisse être relancée. Un dossier de
modèles placé ailleurs part en entier quand l'installateur l'a créé ou l'a trouvé vide, sinon seuls les
modèles qu'Albireo y a mis partent. La désinstallation ne retire que les dossiers enregistrés à
l'installation, jamais une racine de disque ni un dossier de Windows ou des utilisateurs, et enlève les liens
et jonctions qu'elle trouve sans les suivre. La copie de l'installateur gardée dans le dossier
des programmes ne fait que désinstaller, et l'installateur refuse de désinstaller quand aucune installation n'est
enregistrée.

### Mises à jour

L'installateur trouve l'installation en place par l'entrée des programmes installés et son
service, et la met à jour au lieu d'installer une seconde copie : l'assistant s'ouvre sur « Albireo
<version> est installé dans <folder> », passe de la licence à la mise à jour, et garde le dossier des
programmes, les réglages, les modèles, les certificats et leur confiance, le jeton de l'icône,
les raccourcis et le démarrage de l'icône ; l'entrée des programmes installés et le service sont
réécrits sur place. Une version plus récente en place est laissée telle quelle. `/silent` met à jour de la même façon,
les options données rejoignant les réglages, et `/update /silent` est la mise à jour que lance le service.
L'installateur vérifie l'espace libre avant d'arrêter le service, écrit chaque nouveau fichier à côté
de l'ancien, puis les échange tous, en mettant de côté les fichiers des moteurs, de l'application web,
des licences et des voix que la nouvelle version ne porte plus ; le nouveau service doit
alors tourner et écouter sur son port dans les deux minutes. Jusque-là, les fichiers précédents restent de côté, et
un échec, démarrage du nouveau service compris, les remet en place avec l'entrée des programmes
installés et relance le service qui tournait, dont les actions de récupération sont suspendues pendant
la mise à jour ; dès que le nouveau service répond, les fichiers mis de côté sont supprimés, avec les dossiers qu'ils
laissent vides. Un dossier de modèles qu'une version antérieure acceptait à l'intérieur de ces dossiers garde ses fichiers.
Une mise à jour garde le type de démarrage qu'a choisi un administrateur, et un service désactivé reste arrêté.
`/update /silent` ne met à jour qu'une installation en place, jamais avec une version plus ancienne, et un
service en attente de suppression arrête l'installateur avant que quoi que ce soit ne change.
Les modèles livrés avec Albireo ne sont copiés que lorsqu'ils diffèrent de ceux en place, leur
empreinte vérifiée ; une mise à jour par l'installateur sans modèles les garde.

Deux façons de mettre à jour un ordinateur installé :

- à la main : lancer le nouveau `albireo-setup.exe`, ou `albireo-setup-online.exe`, et suivre
  l'assistant de mise à jour ;
- automatiquement (retiré dans le fork macOS) : une fois une version publiée (voir [Publier une version](#publier-une-version)),
  le service lit le flux signé à son démarrage et toutes les six heures dans le lien public,
  télécharge l'installateur sans modèles en arrière-plan depuis le lien également, vérifie
  sa taille, son SHA-256 et la signature du flux,
  et le menu de l'icône propose « Installer la version <version> et redémarrer » ; sinon la mise à jour est
  installée au prochain démarrage du service. « Rechercher des mises à jour » interroge aussitôt le flux.

## Réglages

`settings.json`, dans le dossier des données, contient les réglages, à part le mot de passe du proxy ; l'icône
les modifie par l'interface d'administration, un nouveau port étant ouvert une fois avant d'être enregistré,
et le service redémarre quand un changement l'exige. Un `settings.json` d'une version antérieure est lu
sans erreur : les réglages qui manquent à cette version (`public_link`, `web_search_url`, `web_search_key`)
et un autre fournisseur de recherche, lu comme `wikipedia`, en disparaissent au démarrage suivant, et
`web_search.secret` est supprimé.

| Clé             | Défaut  | Signification                                                             |
|-----------------|---------|---------------------------------------------------------------------------|
| `port`          | 8443    | port de `https://localhost` ; un port que le service ne peut pas ouvrir à son démarrage cède la place au dernier sur lequel il a servi |
| `models_dir`    | `""`    | dossier des modèles, `<data>\models` quand il est vide                    |
| `language`      | `auto`  | `auto`, `en`, `fr`, `de`, `it`, `es`                                      |
| `engine`        | `auto`  | `auto` (l'accélérateur de l'ordinateur : cuda, sinon `openvino:both`, `openvino:gpu` ou `openvino:npu`, le processeur calculant ce que son périphérique ne prend pas ; le moteur processeur, le plus rapide mesuré ou celui des noyaux les plus riches, quand aucun accélérateur n'est utilisable ou quand une mesure a trouvé l'accélérateur plus lent), `best` (le plus rapide mesuré), ou une variante |
| `accelerator`   | `auto`  | `none` (le moteur processeur), `npu`, `gpu`, `both` : le périphérique des moteurs qui en ont plusieurs |
| `v2ram`         | true    | mémoire de périphérique étendue du moteur CUDA                            |
| `v2ram_max_mb`  | 0       | taille de son fichier en mégaoctets, 0 pour automatique ; une taille permet aux copies des poids de dépasser la carte pour s'y étendre ; sur un volume sans fichiers creux (exFAT, FAT32), le budget et la moitié de l'espace libre au plus |
| `device_mb`     | 0       | mémoire du périphérique pour les copies des poids, 0 pour automatique     |
| `threads`       | 0       | threads du moteur, 0 pour les cœurs physiques                             |
| `context`       | 0       | contexte des modèles, 0 pour celui de l'entraînement, réduit quand la mémoire que le système peut engager ne peut pas contenir ses clés et valeurs |
| `max_sequences` | 4       | requêtes calculées ensemble par modèle                                    |
| `kv_mb`         | 0       | mémoire des clés et valeurs, 0 pour deux contextes dans la mémoire que le système peut engager, une réserve laissée |
| `kv_type`       | `auto`  | clés et valeurs : `q8` (8 bits, les clés tournées), `f16` (demi-précision), `auto` (`q8` pour les modèles de langage dont les têtes contiennent des groupes entiers de 32 valeurs) |
| `check_updates` | true    | vérifie le flux à chaque démarrage et toutes les six heures               |
| `default_model` | true    | télécharge le modèle de langage par défaut du catalogue au premier démarrage |
| `offline`       | false   | aucune connexion : catalogue, modèles et mises à jour viennent d'imports  |
| `proxy`         | `""`    | `""` pour le proxy du système (`HTTPS_PROXY`, puis WinHTTP), `none`, ou `http://[user:password@]host:port` ; les réglages donnent le mot de passe sous la forme `********`, conservé dans `proxy.secret` |
| `web_access`    | true    | l'accès au web du bouton Web, `false` le désactive (voir [Accès au web](#accès-au-web)) |
| `web_search`    | `wikipedia` | fournisseur de la recherche : `wikipedia`, ou `none` pour désactiver la recherche |
| `active`        |         | le modèle de chaque fonction : `general`, `code`, `security`, `embedding` |

## Interfaces

L'interface compatible avec OpenAI ne prend aucune clé : le service écoute sur l'interface de bouclage
(et, dans le fork macOS, sur le port réseau une fois l'accès réseau activé, sans mot de passe).
Le champ `model` prend
l'identifiant d'un modèle chargé, une fonction (`general`, `code`, `security`, ou `albireo-code` et
ses semblables), `albireo-background` pour le modèle livré avec Albireo, ou `auto` ; le `model`
de `/v1/embeddings` prend `albireo-embedding`.

| Route                                    | Usage                                                     |
|------------------------------------------|-----------------------------------------------------------|
| `GET /v1/models`                         | modèles chargés et alias des fonctions, `albireo-background` compris |
| `POST /v1/chat/completions`              | discussion, streaming, outils, `reasoning_content`, images |
| `POST /v1/completions`                   | complétion d'un prompt                                    |
| `POST /v1/embeddings`                    | vecteurs de textes, en flottants ou en base64, `dimensions` pour les raccourcir |
| `POST /v1/audio/speech`                  | un texte lu à voix haute, WAVE ou PCM, 16 bits mono à 16 kHz |
| `GET /albireo/v1/speech/voices`          | voix de la synthèse vocale                                |
| `GET /albireo/v1/status`                 | hôte, moteur, statistiques, modèles, téléchargements, mises à jour |
| `GET /albireo/v1/models?lang=fr`         | catalogue et modèles installés                            |
| `GET /albireo/v1/engines`                | bibliothèques de moteurs, options et mesures              |
| `POST /albireo/v1/count`                 | tokens d'une conversation pour un modèle                  |
| `POST /albireo/v1/web/search`            | une recherche dans Wikipédia (voir [Accès au web](#accès-au-web)) |
| `POST /albireo/v1/web/fetch`             | une page du web à une adresse publique, ses octets en base64 |
| `GET /albireo/v1/licenses`               | textes de licence du dossier `licenses` de l'installation, avec les versions que connaît le service |
| `GET /albireo/v1/licenses/<file>`        | l'un de ces textes, 1 Mo au plus, un fichier de ce dossier seulement |
| `/docs/`                                 | page Swagger de l'interface                               |

L'échantillonnage de chaque modèle passe par l'échantillonneur d'Albireo, quels que soient l'architecture du
modèle et le moteur : `temperature`, `top_p`, `top_k`, `min_p` et les pénalités s'appliquent de la
même façon à tous. Les réglages qu'une requête omet prennent les valeurs que les auteurs de la
famille du modèle conseillent sur sa fiche, pour une requête de texte seul ou avec images comme les distinguent les fiches de
Qwen3-VL, ou la température d'usage et des évaluations que décrivent leurs fiches
pour CyberSecQwen 4B et les modèles Foundation-Sec, `min_p` valant 0 pour tous. Un modèle Thinking
de Qwen3-VL dont le moteur termine aussitôt le raisonnement répond comme un modèle sans raisonnement, avec les
valeurs que conseillent les auteurs de Qwen3 pour ce mode. L'onglet « Génération » de l'application web les
omet tant que ses réglages restent sur automatique :

| Modèles                                  | `temperature` | `top_p` | `top_k` | Pénalité                                  |
|------------------------------------------|---------------|---------|---------|-------------------------------------------|
| Qwen3-VL Thinking, texte seul            | 1             | 0.95    | 20      | `presence_penalty` 1.5 sur la réponse     |
| Qwen3-VL Thinking, avec images           | 1             | 0.95    | 20      | aucune                                    |
| Qwen3-VL Thinking, raisonnement terminé aussitôt | 0.7   | 0.8     | 20      | `presence_penalty` 1.5 sur la réponse     |
| Qwen3-VL Instruct, texte seul            | 1             | 1       | 40      | `presence_penalty` 2 sur la réponse       |
| Qwen3-VL Instruct, avec images           | 0.7           | 0.8     | 20      | `presence_penalty` 1.5 sur la réponse     |
| CyberSecQwen 4B                          | 0.3           | 0.8     | 20      | aucune                                    |
| Qwen3 sans raisonnement                  | 0.7           | 0.8     | 20      | aucune                                    |
| Qwen2.5                                  | 0.7           | 0.8     | 20      | `repeat_penalty` 1.05 sur 64 tokens       |
| Foundation-Sec 1.1 8B et 8B Reasoning    | 0.3           | 0.95    | 20      | aucune                                    |
| Granite 4.2, avec et sans raisonnement   | 1             | 0.95    | aucun   | aucune                                    |
| Ministral 3                              | 0.1           | 1       | aucun   | aucune                                    |
| Devstral, autres Mistral sans raisonnement | 0.15        | 1       | aucun   | aucune                                    |
| Autres modèles, pendant qu'ils raisonnent | 0.6          | 0.95    | 20      | aucune                                    |
| Autres modèles                           | 0.7           | 0.9     | 40      | aucune                                    |

`top_k` garde les tokens les plus probables, 1 024 au plus même avec 0 ; une valeur hors de 0 à 1024, ou
qui n'est pas un nombre entier, est refusée avec 400. À température 0, le token le plus probable est
pris, ce que donne aussi `top_k` 1. `repeat_last_n` donne les tokens que lisent les pénalités : les 64 derniers
par défaut, fin du prompt comprise comme la lisent les serveurs de référence, et -1 pour la réponse
entière seule, sans le prompt ni la fin d'un raisonnement qu'écrit le moteur. Le moteur refuse les autres valeurs hors de leurs
bornes : `temperature` de 0 à 10 000, `top_p` et `min_p` de 0 à 1, `repeat_penalty` au-dessus de 0 et jusqu'à
100, `presence_penalty`, `frequency_penalty` et les valeurs de `logit_bias` de -100 à 100, chaque séquence
d'arrêt de 1 024 octets au plus. Une passe que la mémoire du système ne peut pas contenir termine ses requêtes
avec `out_of_memory` et rend les pages du cache.

Avec `tools`, les appels qu'un modèle écrit dans la forme de son template reviennent sous forme de `tool_calls`,
entiers, avec les arguments tels que le modèle les a écrits, un saut de ligne laissé brut dans une chaîne étant réparé ;
l'analyseur de Qwen3 accepte aussi un appel entre des clôtures de code, plusieurs objets dans un seul appel, un caractère
fermant en trop et un appel laissé ouvert à la fin de la réponse. Les appels de Granite 4.2, sous
la forme XML, reviennent sous forme d'arguments JSON, chaque valeur typée par la définition de l'outil. Les
appels des modèles Mistral gardent l'identifiant que le modèle leur donne (`[CALL_ID]`), et des arguments
qui ne sont pas un objet JSON laissent le texte tel que le modèle l'a écrit. Une réponse de Llama 3 n'est un appel que
lorsqu'elle est un objet JSON, ou une liste d'objets, nommant des outils de la requête ; toute autre réponse, JSON ou
non, revient sous forme de son texte, tel que le modèle l'a écrit.

Un message d'un modèle qui lit les images (`inputs` de `/v1/models`) prend des parties `image_url`, dont
l'`url` est une data URL d'un fichier PNG ou JPEG et dont le `detail` est `low`, `auto` ou `high`. Le
service ne récupère aucune adresse (`invalid_image_url`), ne lit aucun autre format
(`invalid_image_format`), refuse plus de 16 images dans une requête (`too_many_images`) et les
images envoyées à un modèle qui ne lit que du texte (`image_input_not_supported`).

Les routes d'administration (`/albireo/v1/settings`, `/albireo/v1/models/<id>/download`,
`/activate`, `/deactivate`, `DELETE /albireo/v1/models/<id>`, `/verify`, `/import`,
`/models/upload`, `/updates/check`, `/updates/apply`, `/restart`, `/engines/benchmark`, `/logs`)
ne prennent aucun jeton dans le fork macOS, une version personnelle : toutes les routes sont ouvertes, sans jeton ni mot de
passe, sur le Mac comme sur le port réseau.
Une mesure des
moteurs s'exécute seule : `/engines/benchmark` répond 409 `benchmark_running` pendant ce temps. Un modèle
désactivé, remplacé ou supprimé pendant qu'il répond quitte le moteur une fois ses requêtes servies :
`/albireo/v1/models` le liste sous `leaving` (`unloading` ou `removing`) pendant ce temps, et la
suppression répond `"pending": true`. Les erreurs que renvoient ces routes portent un `code` avec ses `args`
et ses `sizes` (`disk_space`, `not_installed`, `being_deleted`, `embedding_needed`, `load_failed`...),
que les fenêtres expliquent dans la langue de l'utilisateur.

## Accès au web

Le bouton Web de la zone de saisie donne au modèle deux outils qui atteignent le web par le service
([`host/web_access.h`](host/web_access.h)) : `web_search`, qui cherche dans Wikipédia, et
`fetch_page`, qui lit une page à une adresse, l'un des résultats ou une adresse que l'utilisateur a donnée. Le
service fait les requêtes, pour que l'application ne charge toujours rien d'une autre origine, et
rend les octets qu'il a lus, que l'application décode ([`web/js/web.js`](web/js/web.js)) : un
texte avec `TextDecoder` et son jeu de caractères, le HTML avec `DOMParser`, le PDF avec son lecteur.

| Route | Corps | Réponse |
|-------|-------|---------|
| `POST /albireo/v1/web/search` | `{"query", "count", "language"}` : 1 à 400 caractères, 1 à 10 résultats (5 par défaut), une langue comme `fr` ou `pt-BR` | `{"provider", "results": [{"title", "url", "snippet"}]}`, les résultats d'adresses http et https seulement |
| `POST /albireo/v1/web/fetch` | `{"url", "language"}` : une adresse http ou https, la langue envoyée comme `Accept-Language` | `{"url", "final_url", "status", "content_type", "charset", "size", "truncated", "data"}`, les octets en base64 |

Ces routes ne prennent aucun jeton d'administration : elles appartiennent à l'interface de l'application web,
de la même origine que ses autres routes, que le serveur refuse à tout autre site (voir
[Sécurité](#sécurité)). `/albireo/v1/status`, que l'application lit à son démarrage, donne
`"web": {"available": true, "search": "wikipedia"}` : `available` tant que l'administrateur laisse
l'accès au web activé et que le service n'est pas hors ligne, et le fournisseur de la recherche, `none` quand la
recherche est désactivée, la lecture des pages restant disponible. Les deux routes répondent 403 tant que le
service fonctionne hors ligne (`offline`) ou une fois que l'administrateur a désactivé l'accès au web
(`web_access_off`).

Le réglage `web_search` désigne le fournisseur de la recherche :

- `wikipedia`, par défaut : la recherche de l'interface REST de Wikipédia dans la langue de la
  requête, l'anglais à défaut (`GET https://<language>.wikipedia.org/w/rest.php/v1/search/page`),
  chaque résultat étant un article de ce Wikipédia avec son extrait, son balisage retiré, ou sinon sa
  description. Les requêtes portent le User-Agent que demande la politique de Wikimedia, avec un
  contact : `Albireo/<version> (https://github.com/Cydral/Dlib-Transformer-extensions;
  cydraltechnology@gmail.com)`. Wikimedia limite le débit des requêtes anonymes par minute
  et par client, 200 requêtes par minute pour un client qui se nomme ainsi et 10 pour un client
  connu par sa seule adresse, des limites introduites en 2026 qui peuvent changer, et les ordinateurs derrière
  une même adresse publique les partagent. La route signale la limite par 429 (`rate_limited`) et le
  délai que donne Wikimedia (`Retry-After`) ;
- `none` : pas de recherche, `fetch_page` restant disponible.

Le service ne lit aucune page de résultats destinée aux personnes et n'envoie aucun User-Agent de navigateur ; une
limite de débit ou une vérification anti-robots de Wikipédia revient sous forme d'erreur de la route. L'adresse
de Wikipédia est fixe, et la règle des adresses publiques qui suit ne s'applique qu'aux pages.

Une page n'est lue qu'à une adresse publique. Le service refuse avec 403 (`address_refused`) les
noms de cet ordinateur et des réseaux locaux, `localhost`, `*.localhost`, `*.local`,
`*.internal`, `*.home.arpa` et les noms d'un seul libellé, puis résout le nom et refuse
la page quand l'une de ses adresses n'est pas publique : 0.0.0.0/8, 10/8, 100.64/10, 127/8, 169.254/16,
172.16/12, 192.0.0/24, 192.0.2/24, 192.88.99/24, 192.168/16, 198.18/15, 198.51.100/24,
203.0.113/24, 224/4 et 240/4 en IPv4 ; ::/96 (`::`, `::1` et les adresses compatibles IPv4),
64:ff9b:1::/48, 100::/64, 2001::/32, 2001:db8::/32, fc00::/7, fe80::/10, fec0::/10 et ff00::/8
en IPv6, avec les adresses IPv4 que portent ::ffff:0:0/96, 64:ff9b::/96 et 2002::/16, vérifiées
comme IPv4. Les formes décimale, hexadécimale et abrégée d'IPv4
(`http://2130706433/`, `http://0x7f.1/`) sont lues comme les lisent les résolveurs et vérifiées de la même
façon. La connexion va à l'adresse vérifiée, pour que le nom ne soit pas résolu de nouveau entre
la vérification et la connexion, tandis que l'en-tête `Host`, le nom de serveur de TLS et la vérification du
certificat gardent le nom. Le service suit lui-même les redirections, cinq au plus, chaque adresse
vérifiée comme la première, et donne l'adresse qui a répondu (`final_url`). Une adresse d'un autre
schéma que http et https, avec un nom d'utilisateur ou un mot de passe, ou sans hôte est refusée avec 400
(`invalid_url`).

Un texte (`text/...`, XML et les types `+xml`, JSON et les types `+json`) est lu jusqu'à 2 Mo et
coupé au-delà (`truncated`), un fichier PDF jusqu'à 20 Mo et refusé au-delà (413), et tout autre type est
refusé avec son nom (415). La réponse donne le statut du site, 404 ou 500 compris, avec
son corps. Une requête prend 10 s au plus pour se connecter, 15 s pour chaque attente de données et 30 s en tout
(504), envoie `Albireo/<version> (+https://github.com/Cydral/Dlib-Transformer-extensions)` comme
User-Agent et `Accept-Language` quand la requête donne une langue, et ne garde aucun cookie. Trois
requêtes de l'accès au web s'exécutent à la fois, comme Wikimedia le demande à ses clients (503 `busy` au-delà, avec
`Retry-After: 2`), et l'arrêt du service ou son mode hors ligne termine celles en cours. Le journal
nomme l'hôte lu, le statut et la taille (`web: en.wikipedia.org read (200, 81234 bytes)`),
et le fournisseur d'une recherche avec le nombre de ses résultats, jamais un chemin, une requête ou le texte
cherché.

Derrière un proxy, celui du réglage `proxy` ou du système, les requêtes passent par le
proxy, qui résout de nouveau le nom de son côté : la vérification s'applique aux adresses que cet ordinateur
résout, et le proxy peut atteindre d'autres adresses pour le même nom. Un nom que cet ordinateur ne peut pas
résoudre est refusé, ce qui exclut la lecture des pages sur les réseaux où seul le proxy
résout les noms d'Internet ; la recherche, dont l'adresse n'est pas vérifiée, passe par le
proxy sur ces réseaux.

L'administrateur règle l'accès au web par la route des réglages de l'interface d'administration
(`PUT /albireo/v1/settings` avec le jeton), par exemple avec l'un de ces corps :

```
{"web_access": false}
{"web_search": "none"}
{"web_search": "wikipedia"}
```

Dans l'application web, le bouton Web se trouve à côté d'Agent. Les boutons du raisonnement approfondi, de
l'agent et du web sont désactivés dans chaque nouvelle discussion, dans un projet ou non, quoi qu'aient choisi les
précédentes : leur état appartient à leur discussion, qui le garde avec ses messages, après une
réponse, une régénération, une modification et un rechargement et dans les autres onglets, si bien qu'une autre discussion peut
montrer un autre état. Avec un modèle capable de raisonner, Agent et Web vont avec le raisonnement approfondi, pour
que le modèle raisonne avant chaque appel d'un outil : ils sont désactivés tant qu'il est éteint, et l'éteindre
les éteint ; un modèle qui appelle des outils sans raisonner (Qwen3-VL Instruct, Ministral 3,
Devstral Small 2, Qwen2.5 Coder) les prend seuls. Le bouton Web est désactivé, avec la raison comme
titre, tant que le modèle de la fonction n'appelle aucun outil, tant qu'il lui manque le raisonnement approfondi dont il
a besoin, tant que le service fonctionne hors ligne et une fois que l'administrateur a désactivé l'accès au web. Avec Web activé, chaque question porte `web_search`, quand le service propose la recherche dans
Wikipédia, et `fetch_page`, après les outils de l'agent tant qu'Agent est activé, et la réponse exécute les
appels des outils même avec Agent désactivé. Les outils et le message système restent les mêmes d'une
question à la suivante tant que les boutons et le fournisseur ne changent pas, pour que le début du
prompt reste dans le cache du moteur. L'aide à côté des boutons décrit les outils web
avec ceux de l'agent.

`fetch_page` ne lit qu'une adresse de la discussion : une adresse que contient une question de l'utilisateur
(`http://`, `https://`, ou `www.` pour les deux schémas), une adresse d'un résultat de `web_search`, ou un
lien d'une page déjà lue, comparées sans leur fragment. Toute autre adresse, que le modèle
composerait, est refusée avec cette raison comme résultat de l'appel, pour qu'une page lue ne puisse pas
faire envoyer par le modèle les données de la discussion à une adresse de son choix. Les résultats des
recherches et les liens des pages restent avec leurs appels dans la discussion, pour que la règle
tienne après un rechargement de la page et dans un autre onglet.

Une page HTML est lue dans un document inerte de `DOMParser`, qui n'exécute aucun script et ne charge rien,
sans ses scripts, styles, cadres, dessins, formulaires, navigation, en-têtes, pieds de page et encadrés, dans
son élément `main`, sinon son unique `article`, sinon son corps : son titre, ses titres de section sous forme de lignes `#`,
ses éléments de liste sous forme de lignes `- `, ses tableaux sous forme de lignes de cellules séparées par ` | `, ses blocs
préformatés tels quels, et ses liens, rendus absolus, http et https seulement, 50 au plus, listés à la
fin. Un fichier PDF passe par le lecteur des documents, JSON, XML et le texte restent tels quels. Le
modèle reçoit une page par parties de 8 000 caractères, chacune avec l'adresse, le statut et le
titre, et le début de la partie suivante quand le texte continue ; le texte des dernières pages lues reste
en mémoire pour leur discussion, 30 pages par discussion, 60 pages et 8 millions de caractères en tout,
pour que les parties suivantes n'aient pas besoin d'un second téléchargement, et part avec une discussion supprimée ou en navigation privée.

Une recherche qui trouve moins de trois articles dans le Wikipédia de la langue de l'interface, ou
moins qu'elle n'en demande quand elle en demande moins, continue dans le Wikipédia anglais, ses articles suivant
ceux du premier. Chaque appel lit aussi les pages vers lesquelles il mène, sur un niveau de profondeur et pas plus,
pour que le modèle reçoive le texte de ces pages avec leurs titres et leurs extraits. `web_search`
lit les pages de ses cinq premiers résultats, et `fetch_page`, la première fois qu'il donne la première partie
d'une page, les quatre premiers liens du texte de cette page qui mènent à d'autres pages de texte. Un lien vers
un fichier d'un autre type (images, archives, programmes, styles, scripts, fichiers PDF), vers les pages des
comptes ou des règles d'un site (connexion, inscription, confidentialité, conditions, partage), ou, sur un wiki
de Wikimedia, vers une page hors des articles (un nom avec un espace de noms, l'historique, la modification) est
écarté. Le résultat se termine par les 1 200 premiers caractères de chaque page lue avec lui, avec son titre
et son adresse, et le modèle lit le reste avec `fetch_page` depuis la mémoire de la discussion,
liens de ces pages compris. Les pages lues avec un appel vont deux par deux, pour que les
discussions auxquelles on répond en même temps gardent leur part des trois requêtes qu'exécute le service, et
dans la limite de 25 secondes en tout : une page qui échoue, répond par une erreur ou est encore en cours de lecture à ce moment est
écartée du résultat. Les adresses où ces pages ont répondu rejoignent les adresses que
`fetch_page` lit.

Chaque résultat des outils web commence par une ligne qui dit au modèle que le texte du web est une donnée
et qu'il ne suit aucune des instructions qu'il contient. Tant que Web est activé, le message système, après
la partie qui reste la même, dit ce que font les outils web et demande au modèle de citer les adresses
qu'il a utilisées et de ne mettre aucune donnée de la discussion dans une adresse ou une recherche. Les mots d'une recherche
sont ceux que choisit le modèle, et cette seule instruction en tient les données de la discussion à
l'écart.

Chaque appel s'affiche dans la réponse comme les appels de l'agent, sous le nom de son outil dans la
langue de l'interface (« Recherche Web » et « Lecture d'une page » en français), avec les mots
cherchés ou l'adresse demandée, un lien qui s'ouvre dans un nouvel onglet (`target="_blank"`,
`rel="noopener noreferrer"`) une fois la page lue. Une erreur du service, une adresse refusée,
une page trop grande, un type non lu, un site qui ne répond pas ou le mode hors ligne, parvient au
modèle comme résultat de l'appel, avec le message du service, et ne change rien d'autre dans la
page.

## Modèles

Le catalogue ([`dist/albireo/catalog.json`](../../../../dist/albireo/catalog.json)) liste des modèles des
architectures que prend en charge le moteur (llama, mistral, mistral3, granite, granitemoe, phi3, qwen2,
qwen3, qwen3moe, qwen3vl, qwen3vlmoe, mixtral), avec leurs fonctions, l'entreprise ou le
consortium qui les a faits, ce qu'ils lisent, leurs descriptions dans les cinq langues, leur
licence, et l'adresse, la taille et le SHA-256 de chaque fichier, projecteur d'un modèle qui lit les
images compris. [`tools/make_catalog.py`](tools/make_catalog.py) l'écrit à partir du hub Hugging Face,
les adresses épinglées à un commit pour qu'un fichier ne change jamais sous son empreinte. Dans le fork
macOS, le service télécharge chaque fichier directement depuis l'adresse du catalogue, la source officielle
du modèle, avec trois tentatives par segment ; un téléchargement qui échoue se termine avec le code
`download_failed`, dont le message nomme l'adresse et son échec. Le catalogue, non signé, vient
de son adresse dans le dépôt de l'original, `dist/albireo/catalog.json`, ou d'un dossier.
Les empreintes vérifient les octets. Les tailles ci-dessous comptent le décodeur et son projecteur.

| Modèle                         | Éditeur            | Fonctions               | Entrées       | Taille   | Licence |
|--------------------------------|--------------------|-------------------------|---------------|----------|---------|
| Qwen3-VL 4B Thinking (par défaut) | Alibaba         | général, code, sécurité | texte, image  | 2,95 Go  | Apache-2.0 |
| Qwen3 Embedding 0.6B (par défaut) | Alibaba         | embedding               | texte         | 0,64 Go  | Apache-2.0 |
| Qwen3-VL 2B Thinking           | Alibaba            | général, code, sécurité | texte, image  | 1,55 Go  | Apache-2.0 |
| Qwen3-VL 2B Instruct           | Alibaba            | général                 | texte, image  | 1,55 Go  | Apache-2.0 |
| Qwen3-VL 4B Instruct           | Alibaba            | général, code           | texte, image  | 2,95 Go  | Apache-2.0 |
| Qwen3-VL 8B Instruct           | Alibaba            | général, code, sécurité | texte, image  | 5,78 Go  | Apache-2.0 |
| Qwen3-VL 8B Thinking           | Alibaba            | général, code, sécurité | texte, image  | 5,78 Go  | Apache-2.0 |
| EuroVLM 9B Preview             | consortium EuroLLM | général                 | texte, image  | 6,42 Go  | Apache-2.0 |
| Phi-4-mini                     | Microsoft          | général                 | texte         | 2,49 Go  | MIT |
| Ministral 3 3B                 | Mistral AI         | général                 | texte, image  | 2,99 Go  | Apache-2.0 |
| Ministral 3 8B                 | Mistral AI         | général                 | texte, image  | 6,06 Go  | Apache-2.0 |
| EuroLLM 9B Instruct            | consortium EuroLLM | général                 | texte         | 5,58 Go  | Apache-2.0 |
| Granite 4.2 3B                 | IBM                | général, code           | texte         | 2,24 Go  | Apache-2.0 |
| Granite 4.2 8B                 | IBM                | général, code           | texte         | 5,35 Go  | Apache-2.0 |
| Qwen3-VL 30B A3B Thinking      | Alibaba            | général, code, sécurité | texte, image  | 19,27 Go | Apache-2.0 |
| Qwen2.5 Coder 1.5B             | Alibaba            | code                    | texte         | 1,12 Go  | Apache-2.0 |
| Qwen2.5 Coder 7B               | Alibaba            | code                    | texte         | 4,68 Go  | Apache-2.0 |
| Devstral Small 2               | Mistral AI         | code                    | texte, image  | 15,21 Go | Apache-2.0 |
| CyberSecQwen 4B                | athena129 (lablab.ai) | sécurité             | texte         | 2,50 Go  | Apache-2.0 |
| Foundation-Sec 1.1 8B          | Cisco              | sécurité                | texte         | 4,92 Go  | Llama 3.1 Community License |
| Foundation-Sec 8B Reasoning    | Cisco              | sécurité                | texte         | 4,92 Go  | Llama 3.1 Community License |

La liste donne à chaque fonction des modèles qui lisent les images et des modèles qui lisent le texte, de 2 à 6 Go
pour les ordinateurs de 8 et 16 Go de mémoire, un mélange d'experts pour ceux de 24 Go et un
agent de programmation pour ceux de 32 Go :

| Fonction | Texte et images | Texte |
|----------|-----------------|-------|
| Générale | Qwen3-VL 2B, 4B et 8B, Qwen3-VL 30B A3B Thinking, Ministral 3 3B et 8B, EuroVLM 9B | Phi-4-mini, EuroLLM 9B, Granite 4.2 3B et 8B |
| Code     | Qwen3-VL 2B Thinking, Qwen3-VL 4B (Thinking et Instruct), Qwen3-VL 8B (Instruct et Thinking), Qwen3-VL 30B A3B Thinking, Devstral Small 2 | Granite 4.2 3B et 8B, Qwen2.5 Coder 1.5B et 7B |
| Sécurité | Qwen3-VL 2B et 4B Thinking, Qwen3-VL 8B (Instruct et Thinking), Qwen3-VL 30B A3B Thinking | CyberSecQwen 4B, Foundation-Sec 1.1 8B et 8B Reasoning |

Les choix suivent les mesures publiées : Qwen3-VL donne les meilleurs scores des modèles qui lisent les
images et que le moteur exécute (MMMU 67,4 pour le 4B Instruct, 74,1 pour le 8B Thinking, OCRBench
881 et 896) ; Granite 4.2 donne les meilleurs scores de raisonnement et de code sous 6 Go (LiveCodeBench v6
69,7 pour le 3B et 73,2 pour le 8B) ; Foundation-Sec 8B Reasoning relie le mieux les vulnérabilités à leurs
faiblesses parmi les modèles de 8B (CTIBench-RCM 0,753), et CyberSecQwen 4B, affiné à partir de
Qwen3-4B-Instruct-2507, garde 97 % du score de Foundation-Sec 8B Instruct sur cette tâche pour la moitié de
sa taille (CTI-RCM 0,666) et le dépasse sur les questions de renseignement sur les menaces (CTI-MCQ 0,587,
mesuré par ses auteurs selon le protocole de Cisco). Qwen3-VL 30B A3B Thinking, un mélange de 128
experts dont 8 calculent chaque token, lit les images et raisonne comme les autres modèles Qwen3-VL Thinking,
au rythme d'un modèle de 3B. Ministral 3,
que Mistral AI a entraîné en partenariat avec NVIDIA, lit les images par son encodeur Pixtral, comme
Devstral Small 2, l'agent de programmation de 24B de Mistral AI, qui lit les captures d'écran et les
diagrammes ; Phi-4-mini est le dernier petit modèle de Microsoft avec un décodeur standard. EuroLLM 9B
Instruct traduit et écrit les 24 langues officielles de l'Union européenne le mieux de la liste
(FLORES 88,9), et EuroVLM 9B Preview, construit sur lui, lit les images dans ces langues ; ses
éditeurs le donnent comme version préliminaire.

Sans les modèles livrés avec l'installateur complet, le service télécharge le modèle de langage
par défaut à son premier démarrage, sauf si l'installation en a décidé autrement, et le modèle
d'embedding par défaut dans tous les cas, puisque la recherche dans les documents repose sur lui. Un modèle de langage qui
arrive prend les fonctions que nomme son entrée et qu'aucun modèle ne tient ; un modèle d'embedding ne sert que
la fonction `embedding`. Une fonction sans modèle choisi pour elle est servie par le modèle
général, sinon par le modèle livré avec Albireo : dès que tous les choix sont annulés dans la fenêtre Modèles,
ce modèle sert de nouveau les trois fonctions, et l'état donne le modèle qu'utilise chaque fonction
(`active`) à part de ceux qui ont été choisis (`selected`). Le moteur calcule un modèle d'embedding comme
il calcule un modèle de langage, puis prend l'état caché du dernier token de chaque texte,
normalisé, comme vecteur (`pooling_type` 3 du fichier GGUF) ; ses clés et valeurs sont rendues
dès que chaque texte est lu.

Un modèle peut aussi être importé depuis n'importe quel fichier GGUF : la fenêtre Modèles l'envoie au service
(`/albireo/v1/models/upload`), lu avec les droits de l'utilisateur qui l'a choisi, ou le service
importe les fichiers placés dans `<data>\import`. Un fichier préparé par le build d'Albireo garde son
identité (`albireo.id`, `albireo.name`, `albireo.roles`, `albireo.inputs` dans ses métadonnées). Un fichier
dont le contenu est celui d'un modèle déjà installé ou d'un modèle livré avec Albireo n'est pas
recopié : la réponse le liste sous `already` et il quitte le dossier d'import. Un fichier ne
prend jamais l'identifiant d'un autre modèle installé (il devient `<id>-import`, `<id>-import-2`...),
sauf la version plus récente d'un modèle du catalogue, qui le remplace une fois que le moteur a libéré le
modèle chargé ; le téléchargement d'un modèle livré avec Albireo est refusé (`builtin_installed`).

La route `/albireo/v1/import` ne lit un dossier donné par son chemin que lorsqu'il s'agit du dossier d'import,
ou d'un paquet hors ligne (un dossier avec un `catalog.json`, non signé dans le fork macOS), dont les fichiers sont lus par nom, taille et empreinte tels que les liste son catalogue
avant que quoi que ce soit ne soit copié. Tout autre chemin est refusé avec un code (`import_use_upload` pour un
fichier ordinaire, `import_path_relative`, `import_path_network`, `import_path_device`,
`import_path_invalid`), et un dossier qui n'est ni l'un ni l'autre donne la même réponse quel que soit son contenu
(`import_not_bundle`), pour que le service, qui lit tous les fichiers de la machine, ne révèle
rien des dossiers d'un autre compte. Les fichiers sont ouverts dossier par dossier à partir d'un dossier
de confiance, sans passer par aucun lien, jonction ou point de montage, et un lien physique est refusé.

La fenêtre Modèles liste les modèles de langage, avec leurs fonctions, leurs entrées et leur état ;
le modèle d'embedding sert la recherche dans les documents et est donné à part (`embedding_models` de
`/albireo/v1/models`). Les entrées d'un modèle (`inputs` : `text`, et `image` ou `audio` pour les
modèles qui les lisent, le texte quand le catalogue ne dit rien) décident des pièces jointes de l'application
web : les documents, dont le navigateur extrait le texte, vont à tous les modèles, et une image ou
un son seulement à un modèle qui le lit ; le bouton de pièce jointe de chaque conversation nomme les entrées
du modèle de sa fonction, et le sélecteur de fichiers propose ce qu'il lit. Un modèle qui lit les images
se présente en deux fichiers, le décodeur et son projecteur, que la fenêtre Modèles compte ensemble. Un
projecteur importé seul attend son décodeur (`pending_projectors`), puis va avec le décodeur
dont l'entrée du catalogue nomme son SHA-256, sinon avec le décodeur d'une architecture qui lit les images
dont la largeur est la largeur de ses tokens, les noms les plus proches départageant plusieurs candidats.

## Modèles livrés avec Albireo

[`resources/builtin_models.json`](resources/builtin_models.json) nomme les modèles que porte l'installateur
complet, le fichier dont chacun provient, épinglé à un commit avec sa taille et son SHA-256,
et la quantification que livre Albireo :

| Modèle                       | Origine                                                  | Livré           | Fonctions               |
|------------------------------|----------------------------------------------------------|-----------------|-------------------------|
| Qwen3-VL-4B-Thinking-albireo | Qwen3VL-4B-Thinking-Q4_K_M.gguf de Qwen, 2,50 Go, et son projecteur mmproj-Qwen3VL-4B-Thinking-Q8_0.gguf, 0,45 Go | Q4_K_M et Q8_0, 2,95 Go | général, code, sécurité |
| Qwen3-Embedding-0.6B-albireo | Qwen3-Embedding-0.6B-Q8_0.gguf, 0,64 Go                  | Q8_0, 0,64 Go   | recherche dans les documents |

Le build les récupère et les convertit de la même façon que l'exemple d'import de dlib lit un fichier GGUF
([`examples/slm_gguf_import_ex.cpp`](../../../../examples/slm_gguf_import_ex.cpp)), avec le code
du moteur :

1. [`cmake/builtin_models.cmake`](cmake/builtin_models.cmake) télécharge chaque fichier une seule fois dans le
   cache, `%LOCALAPPDATA%\Albireo\build-cache` (`~/.cache/albireo` sous Linux, `ALBIREO_MODEL_CACHE`
   ou `-ModelCache` sinon), nommé d'après son SHA-256, qu'il vérifie ; un fichier déjà présent n'est gardé
   que si sa taille et son SHA-256 correspondent, et un téléchargement s'arrête après deux minutes sans données ;
   derrière un proxy, le téléchargement prend celui de `HTTPS_PROXY`. `build_windows.ps1` donne d'abord au cache les fichiers déjà présents sur
   l'ordinateur, dans le dossier des modèles d'une installation d'Albireo ou dans `$HOME\models`, dont le
   SHA-256 correspond ;
2. [`tools/albireo_builtin.cpp`](tools/albireo_builtin.cpp) le lit avec le lecteur GGUF de dlib,
   résout sa géométrie comme le fait le moteur, ce qui rejette une architecture ou une configuration que le
   moteur n'exécute pas, vérifie que son tokenizer restitue un texte qu'il a encodé et que le moteur
   reconnaît son chat template ;
3. il convertit le modèle : un modèle publié en Q8_0 est requantifié selon le mélange Q4_K_M du
   format GGUF (Q4_K, avec Q6_K pour les embeddings partagés, les valeurs de l'attention et les
   projections descendantes du premier et du dernier huitième des couches et d'une couche sur trois entre
   les deux), avec la recherche et le mélange du quantificateur de référence du format, dont il reproduit la sortie
   octet pour octet, et les premières et dernières lignes de chaque matrice sont vérifiées par rapport à leur
   source ; un modèle publié en Q4_K_M, comme l'est Qwen3-VL-4B-Thinking, garde ses poids. Les
   métadonnées reçoivent l'identité du modèle dans Albireo. Le projecteur d'un modèle qui lit les
   images est vérifié par rapport à son SHA-256, lu par l'encodeur d'images du moteur et installé
   à côté de son modèle (`<id>.mmproj.gguf`) ;
4. le fichier converti est relu et revérifié, puis gardé dans le cache sous le nom de sa
   conversion, pour que les builds suivants le réutilisent ; il atteint `bin/models` de l'arbre de build par un
   lien physique, avec son manifeste `builtin.json`, et l'installateur complet comme la surcouche qui
   suit son programme ([`common/builtin_models.h`](common/builtin_models.h)), ou dans
   `albireo-setup.models` à côté de lui quand les deux atteindraient les 4 Gio qu'exécute Windows ; la surcouche est
   relue avant de prendre son nom.

La conversion prend quelques minutes et le cache contient environ 6,8 Go, les fichiers téléchargés et les
fichiers convertis ; les modèles de l'installateur complet pèsent environ 3,6 Go. `-DALBIREO_BUILTIN_MODELS=OFF`, ou
`-NoBuiltinModels` de `build_windows.ps1`, construit sans eux ; un build croisé donne
`ALBIREO_BUILTIN_TOOL`, l'`albireo-builtin` de la machine de build.

L'installateur copie les modèles dans le dossier `models` des programmes, vérifie leurs empreintes, et
écrit leur manifeste en dernier ; le service les enregistre à chaque démarrage (source `builtin`, le
suffixe `-albireo` dans leurs noms). Un modèle livré avec Albireo prend les fonctions qu'aucun modèle
ne tient, ce qui sur un nouvel ordinateur veut dire toutes les fonctions ; il peut les laisser à d'autres modèles, et il
ne peut pas être supprimé (code `builtin_model`). L'entrée du catalogue du fichier dont il a été fait
(`source_sha256` du manifeste) quitte la fenêtre Modèles et son téléchargement est refusé
(`builtin_installed`) : le modèle livré avec Albireo le remplace. Le modèle de langage parmi
eux sert le travail en arrière-plan (`albireo-background`) et reste chargé quelles que soient les
fonctions. Albireo exécute ces modèles avec son propre moteur, les
noyaux quantifiés sur le fichier projeté, comme tout autre modèle : les réseaux compilés de dlib
(`slm_gguf_import_ex --convert`) calculent en simple précision et restent l'outil de l'entraînement et de
l'affinage.

## Ordinateurs sans réseau

Le réglage `offline`, fixé par l'installateur (option « Cet ordinateur n'a pas d'accès à Internet »,
`/offline`) ou par la fenêtre de l'ordinateur, fait que le service n'ouvre aucune connexion : le catalogue,
les mises à jour, les téléchargements et l'accès au web sont refusés avec la raison, et le menu de
l'icône grise « Rechercher des mises à jour ». Ce dont le service a besoin vient alors d'un paquet hors ligne,
préparé sur un ordinateur doté d'un réseau :

> Dans le fork macOS, `make_offline_bundle.py`, les installateurs et le flux sont retirés : un paquet est
> un dossier contenant un `catalog.json`, non signé, et les modèles qu'il liste, dans le dossier ou dans son
> sous-dossier `models` ; chacun est vérifié par son nom, sa taille et son SHA-256. Le reste de cette section
> décrit l'original.

```
python3 dlib/dlib/external/albireo/tools/make_offline_bundle.py <folder> [--models qwen3-4b,qwen3-embedding-0.6b] [--from ~/models]
```

Le paquet contient le catalogue signé et le flux de mises à jour signé de `dist/albireo`, l'installateur qu'annonce le
flux, pris dans `--installer` ou sinon dans le seul dossier `dist` du lien public, un
dossier `models` avec les modèles choisis, chaque modèle qui lit les images avec son
projecteur (les deux modèles par défaut quand `--models` est absent) et un `README.txt`. `--complete build-albireo/bin/albireo-setup.exe` ajoute l'installateur
complet, qui porte les modèles livrés avec Albireo : le paquet n'a alors besoin d'aucun modèle. Chaque
fichier est vérifié par rapport aux empreintes des fichiers signés quand il entre dans le paquet, et de nouveau par le
service quand il l'installe ; les signatures sont vérifiées avec `albireo-sign` (`--sign`, sinon
`build-albireo/bin`, sinon le `PATH`), et `--no-verify` laisse cette vérification au service. Le paquet s'utilise de l'une de
ces trois façons :

- `albireo-setup-<version>.exe` lancé depuis le paquet installe Albireo avec ses modèles ;
- le bouton Importer de la fenêtre Modèles, à qui l'on donne le dossier du paquet, installe le catalogue,
  les modèles et la mise à jour d'un ordinateur installé ;
- le contenu du paquet copié dans `C:\ProgramData\Albireo\import` est installé par le service
  en moins d'une minute ; un fichier qui ne peut pas être installé est renommé avec le suffixe `.rejected`.

Un catalogue plus ancien que celui en place n'est jamais installé, et une mise à jour est installée au prochain
démarrage du service.

Sur un ordinateur doté d'un réseau, chaque requête du service s'exécute sur un thread à part et est
limitée dans le temps : 10 secondes pour résoudre le nom du serveur et s'y connecter, poignée de main
TLS comprise, puis 20 secondes sans données pour le catalogue et le flux. Les requêtes des utilisateurs
n'attendent jamais le réseau. Quand le service s'arrête ou passe hors ligne, les transferts en cours s'arrêtent
aussitôt et aucune nouvelle connexion ne s'ouvre ; une connexion qu'httplib est en train d'ouvrir se termine dans ses limites,
10 secondes pour la connexion et sa poignée de main TLS et, derrière un proxy, la réponse du proxy,
pendant que le service indique au gestionnaire des services de Windows que son arrêt se poursuit.

## Publier une version

> Dans le fork macOS (4.0.0 et suivantes), rien de cette section ne s'applique : il n'y a ni clé, ni flux
> ou catalogue signé, ni espace de publication, ni mise à jour. `albireo-sign`, `publish_release.py`,
> `fill_public_link.py`, `public_link.py`, `host/public_link.h` et l'option CMake de son
> mot de passe sont retirés ; le catalogue est lu non signé depuis son adresse dans le dépôt de
> l'original. La section est conservée comme description de l'original.

Le flux et le catalogue vivent dans [`dist/albireo`](../../../../dist/albireo) du dépôt, chacun
avec une signature détachée (ECDSA P-256 sur ses octets exacts) que les programmes vérifient avec la clé
publique intégrée à [`host/crypto.cpp`](host/crypto.cpp). La clé privée reste chez l'éditeur ;
`albireo-sign keygen` refuse un chemin qui existe, pour que la clé du flux ne soit jamais remplacée, et
ne donne la clé privée qu'au compte qui l'exécute. Les programmes voyagent dans l'installateur, qui
porte le service, l'icône, chaque moteur avec les bibliothèques d'exécution de son périphérique, et l'application
web.

Les fichiers publiés se trouvent dans l'espace de publication d'Albireo, un dossier public en lecture seule chez un
hébergeur de fichiers, d'où le service lit le flux, l'installateur, le catalogue et les fichiers des modèles, ces deux
derniers venant de leurs adresses officielles quand l'espace ne les sert pas (voir [Modèles](#modèles)). Les
signatures et les empreintes vérifient chaque fichier. (Le fork macOS ne garde de cet espace ni le lien, ni son
mot de passe, ni le nom de l'hébergeur.)

[`tools/publish_release.py`](tools/publish_release.py) écrit le flux d'une version à partir de son
installateur sans modèles : il vérifie que l'installateur porte la version de
[`common/version.h`](common/version.h), écrit le flux avec le nom de l'installateur dans le lien,
sa taille et son SHA-256, le signe, vérifie la signature, met en place le flux et sa signature
et affiche les commandes qui les envoient. [`tools/fill_public_link.py`](tools/fill_public_link.py)
remplit ensuite la copie locale du lien, le dossier que synchronise le service de stockage : `dist` copie le
flux et le catalogue signés avec leurs signatures, vérifiés avec `albireo-sign`, et les deux installateurs
sous leurs noms, vérifiés par rapport au flux, avec le fichier des modèles de l'installateur complet
quand il en a un, le flux en dernier ; `models` place les fichiers du
catalogue, gardés quand ils sont en place, copiés depuis les dossiers de `--from` quand un fichier a leur taille
et leur SHA-256, sinon téléchargés depuis leurs adresses officielles, repris après une coupure et vérifiés ; `check`
lit le listing du lien et signale les fichiers qui lui manquent ou qu'il contient avec une autre taille :

```
python3 dlib/dlib/external/albireo/tools/publish_release.py build-albireo/bin/albireo-setup-online.exe --complete build-albireo/bin/albireo-setup.exe --key ~/keys/albireo-feed-signing.key --notes notes.json
python dlib\dlib\external\albireo\tools\fill_public_link.py dist <copy of the link>\dist --online build-albireo\bin\albireo-setup-online.exe --complete build-albireo\bin\albireo-setup.exe
python dlib\dlib\external\albireo\tools\fill_public_link.py models <copy of the link>\models --from C:\ProgramData\Albireo\models
python dlib\dlib\external\albireo\tools\fill_public_link.py check
```

Le flux nomme l'installateur par son seul fichier dans le lien, sans adresse d'un autre site. Un
modèle publié hors du hub, un modèle converti pour le moteur par exemple, est découpé en parties
de moins de 2 Gio et joint à la release `albireo-models` de GitHub par
[`tools/publish_model.py`](tools/publish_model.py), dont la sortie est l'entrée du catalogue, avec
l'architecture, les paramètres et le contexte lus dans le fichier ; ces fichiers de release sont les
adresses officielles de ses parties, d'où `fill_public_link.py models` les copie vers le lien,
et le jeton de GitHub vient de `GITHUB_TOKEN`, `GH_TOKEN` ou `gh auth token` :

```
python3 dlib/dlib/external/albireo/tools/publish_model.py model.gguf my-model parts --release albireo-models --gzip
```

[`dist/albireo/README.md`](../../../../dist/albireo/README.md) donne chaque étape, catalogue
compris. Un flux qui annonce une version supérieure fait télécharger l'installateur aux services, qui vérifient sa
taille et son SHA-256, et l'exécutent en mode mise à jour à leur démarrage suivant.

## Sécurité

- Le service n'écoute que sur `127.0.0.1` (le fork macOS ajoute le port réseau, en HTTP simple,
  sans mot de passe, et ne vérifie plus ni `Host` ni `Origin`), ne prend que des corps JSON, de 64 Mo et
  524 288 valeurs au plus, et aucun corps sur `GET`, `HEAD`, `DELETE` et `OPTIONS`, et répond avec
  `nosniff`, `no-referrer`, une politique de sécurité du contenu et des protections contre l'intégration en cadre. Il lit au plus quatre
  corps de plus de 8 Mo à la fois, chacun compté jusqu'à ce que sa réponse soit écrite, et tient environ 2 millions
  de valeurs analysées à la fois sur l'ensemble des requêtes (503 `busy` avec `Retry-After` au-delà), attend 30
  secondes au plus un client qui lit ou écrit, ferme une connexion inactive après 5 secondes,
  sert 64 connexions à la fois avec 128 de plus en attente, et termine un flux dont le client laisse plus
  de 16 384 événements en attente.
- Les routes d'administration comparent le jeton en temps constant ; le jeton et le mot de passe du
  proxy sont des fichiers aux droits restreints, et le dossier des données n'accorde l'écriture qu'aux
  administrateurs et au service : sous Windows, le service le sécurise de nouveau à chaque démarrage,
  et le dossier d'import aussi quand un autre compte pourrait y écrire.
- Une base de données que le service ne peut pas ouvrir en SQLite, ou dont les pages échouent à la vérification de son démarrage, est mise
  de côté sous le nom `albireo.db.corrupt-<time>`, et une nouvelle base enregistre de nouveau les modèles du dossier des
  modèles ; une base ouverte en lecture seule est rouverte à la prochaine écriture qui échoue, et une base
  verrouillée par un autre programme est attendue 5 secondes : le service se rétablit sans redémarrage dès que
  la cause a disparu. Les fichiers qu'écrit le service atteignent le disque avant de remplacer les
  précédents, et `settings.json` a une copie, `settings.json.bak`, que le service lit quand le fichier
  est endommagé.
- Le certificat du serveur local vient d'une autorité propre à cet ordinateur, créée par
  le service sous LocalSystem avec une clé ECDSA P-256, dont les contraintes de nom la limitent à
  `localhost`, `127.0.0.0/8` et `::1` : Windows et les navigateurs refusent tout certificat qu'elle
  signerait pour un autre site. Sa clé privée signe le certificat du serveur une seule fois, en mémoire,
  et n'est jamais écrite. Les deux certificats durent 397 jours ; 30 jours avant leur fin, ou quand la
  clé du serveur ne s'ouvre pas, le service crée une nouvelle autorité et un nouveau certificat et
  remplace l'autorité dans les racines de confiance de la machine, après avoir vérifié ses contraintes,
  quand l'installation a choisi de lui faire confiance. L'autorité est limitée à l'authentification des
  serveurs, et une autorité dont une version antérieure à 1.0.8 a laissé la clé sur le disque est remplacée au
  démarrage du service. La clé privée du serveur est chiffrée avec
  DPAPI pour le compte LocalSystem, dans un fichier que seuls SYSTEM et les administrateurs peuvent ouvrir :
  une copie de ce fichier ne s'ouvre sous aucun autre compte ni sur aucun autre ordinateur. Le service
  la déchiffre en mémoire pour le serveur TLS, sans fichier en clair. Une mise à jour garde les
  certificats et leur confiance, supprime la clé de l'autorité d'une version antérieure à 1.0.8 et
  chiffre sa clé du serveur ; une désinstallation retire l'autorité des racines de confiance
  et supprime les certificats et les clés.
- Une mise à jour préparée garde à côté de son installateur le flux signé qui l'a annoncée ; le service revérifie
  par rapport à lui la version, le chemin, la taille et le SHA-256 de l'installateur, le fichier maintenu
  ouvert, juste avant de l'exécuter.
- Le flux, le catalogue et chaque fichier téléchargé sont vérifiés par rapport à des signatures et à des empreintes (dans
  le fork macOS, par rapport aux empreintes seulement : le catalogue n'est pas signé, si bien qu'un dossier à importer doit venir
  d'une source de confiance). Les
  fichiers du dossier des modèles, du dossier d'import, des téléchargements et des mises à jour sont
  ouverts et créés sans passer par aucun lien, jonction ou point de montage, leur chemin revérifié avant
  chaque ouverture, chaque renommage et chaque suppression ; la route des chemins ne lit que le dossier d'import et
  les paquets hors ligne signés (voir [Modèles](#modèles)). Chaque erreur des mises à jour porte un code
  avec ses valeurs (`update_signature`, `update_installer_mismatch`, `update_package_manager`...),
  que l'icône explique dans la langue de l'utilisateur.
- L'espace de publication (retiré dans le fork macOS), seule source des mises à jour et première source du catalogue et des
  modèles, n'apporte rien que le flux et le catalogue signés ne nomment pas, à la taille et avec le SHA-256
  qu'ils donnent : un fichier de l'espace qui diffère est refusé, et seule l'adresse officielle du
  catalogue ou du modèle, vérifiée de la même façon, prend sa place. Une page web qu'un proxy ou un
  filtre du réseau envoie à la place d'une réponse, un corps tronqué, un JSON d'autres types, un
  serveur bloqué et une connexion réinitialisée terminent la tentative avec son message, dans un temps limité, et aucune
  exception ne sort des threads des téléchargements et des vérifications.
- Le JavaScript qu'exécute le mode agent tourne dans un cadre isolé d'origine opaque, dans un worker
  arrêté après 15 s ; la page construit sa propre copie de chaque réponse du cadre, avec des champs vérifiés et
  raccourcis, et abandonne un cadre resté silencieux pendant 20 s.
- L'accès au web ne lit que les pages d'adresses publiques, les noms et les adresses de cet
  ordinateur et des réseaux locaux étant refusés, sa connexion allant à l'adresse vérifiée ; il
  suit lui-même les redirections, chaque adresse revérifiée, et lit 2 Mo de texte et 20 Mo de
  PDF au plus. L'application ne lit que les adresses de la discussion, lit les pages
  dans un document inerte, et donne leur texte au modèle comme donnée non fiable (voir
  [Accès au web](#accès-au-web)).
- Le texte lu à voix haute n'atteint le synthétiseur que par `albireo-speech.exe`, lancé pour chaque
  lecture avec un jeton de faible intégrité et sans privilèges, qui ne peut écrire ni dans les dossiers
  du service, ni dans ceux des programmes ou des utilisateurs, dans un job de 256 Mo qui le termine avec le service
  et lui refuse tout processus enfant ; le texte est d'abord réduit aux lettres des écritures latines,
  aux chiffres et à la ponctuation usuelle, pour qu'aucun balisage du synthétiseur, dont une partie nomme des
  fichiers, ne l'atteigne.
- Les bibliothèques des moteurs sont chargées depuis le dossier `engines` de l'installation, leurs
  dépendances étant cherchées d'abord dans ce dossier. Le service cherche les bibliothèques qu'il charge par
  nom dans son propre dossier, dans `engines` et dans System32 seulement, jamais dans le `PATH` ni dans le dossier
  courant, et charge les parties de cuDNN depuis `engines` par leurs chemins complets, pour qu'une autre copie de
  CUDA, de cuDNN ou d'OpenVINO présente sur la machine ne soit jamais prise.
- Un fichier de modèle est lu dans des limites, vérifiées avant toute allocation : 2^20 entrées et
  des tableaux de 2^24 valeurs au plus dans le fichier GGUF, un alignement qui est une puissance de deux jusqu'à 65 536,
  1 024 couches au plus et pas plus que ses tenseurs, 1 024 têtes, une largeur de 2^17, une largeur
  feed-forward de 2^20, des têtes de 4 096 valeurs, un contexte de 2^26 positions, un vocabulaire de 2^24 tokens,
  4 096 experts, les tokens de début et de fin dans le vocabulaire, chaque entier vérifié
  avant d'être réduit et chaque réel fini. Les options d'un chargement et les réglages du moteur
  sont aussi des nombres bornés (un contexte de 2^26 positions, 256 séquences, 65 536 tokens par
  passe, 2^24 Mo de clés et valeurs au plus). Un fichier hors de ces limites est refusé aussitôt,
  quoi qu'il déclare.

## Dossiers de données

| Chemin (Windows)                       | Contenu                                                      |
|----------------------------------------|--------------------------------------------------------------|
| `C:\Program Files\Albireo`             | programmes, moteurs, application web, licences, `albireo-files.json` |
| `C:\Program Files\Albireo\models`      | les modèles livrés avec Albireo et leur `builtin.json`       |
| `C:\Program Files\Albireo\speech`      | les voix de la synthèse vocale                               |
| `C:\ProgramData\Albireo\settings.json` | réglages, et leur dernière copie `settings.json.bak`         |
| `C:\ProgramData\Albireo\proxy.secret`  | mot de passe du proxy, lisible par les administrateurs et le service |
| `C:\ProgramData\Albireo\albireo.db`    | modèles installés, téléchargements, utilisation, catalogue   |
| `C:\ProgramData\Albireo\models`        | modèles (ou le dossier choisi à l'installation)              |
| `C:\ProgramData\Albireo\import`        | paquets hors ligne et modèles, installés en moins d'une minute |
| `C:\ProgramData\Albireo\certificates`  | certificats de l'autorité locale et du serveur, clé du serveur chiffrée pour LocalSystem |
| `C:\ProgramData\Albireo\engine-cache`  | graphes compilés d'OpenVINO, fichier de la mémoire étendue   |
| `C:\ProgramData\Albireo\logs`          | journaux du service et de l'installateur                     |
| `C:\ProgramData\Albireo\updates`       | installateur de la version suivante, avec le flux signé qui l'a annoncée |

Le dossier des données est celui qu'a enregistré l'installateur (`DataDir` sous `HKLM\Software\Albireo`),
`C:\ProgramData\Albireo` par défaut, qui peut être choisi à l'installation ; `ALBIREO_HOME`
le remplace quand aucun dossier n'est enregistré, et pour le développement sous Linux. Sous Windows, la
variable est lue dans l'environnement de la machine (les variables système), que reçoit le
service ; la variable de l'environnement propre d'un utilisateur est ignorée.
