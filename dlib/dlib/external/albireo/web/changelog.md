# Notes de version

Albireo pour macOS sur puce Apple, version dérivée d'Albireo Community Edition (Cydral Technology), maintenue par TortueTech. Les numéros suivent la gestion sémantique des versions : majeur pour une rupture, mineur pour une fonction nouvelle, correctif pour une correction. Les versions sont présentées de la plus récente à la plus ancienne.

## 4.11.10 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : la synchronisation entre onglets (`tabs.js`), le panneau latéral (`sidebar.js`), les actions sur une discussion (`conversations.js`) et le travail d'arrière-plan, titres et compression (`background.js`), quittent `app.js`, qui ne garde que les réglages d'affichage, la navigation, la mise en page et le démarrage.

---

## 4.11.9 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : la zone de saisie (`composer.js`) et l'écriture des réponses, avec la compression des échanges anciens et les étapes de l'agent (`generation.js`), quittent `app.js`.

---

## 4.11.8 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : la zone principale et le fil d'une discussion (barre du haut, accueil, questions, réponses, appels d'outils, défilement) quittent `app.js` pour `thread.js`.

---

## 4.11.7 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : l'état du service et des modèles (`status.js`), les listes et la recherche (`lists.js`) et les brouillons de la zone de saisie (`drafts.js`) quittent `app.js`.

---

## 4.11.6 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : la mémoire des discussions quitte `app.js` pour `memory.js`.

---

## 4.11.5 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : l'indexation des documents et les descriptions des images de projets (`indexing.js`), les fichiers et images joints (`files.js`) et les pages des projets (`projects.js`) quittent `app.js`.

---

## 4.11.4 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : la construction du contexte envoyé au modèle (parts du contexte de travail, estimation des tokens, message système, messages) quitte `app.js` pour `context.js`.

---

## 4.11.3 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : les boîtes de dialogue et les menus (`dialogs.js`), puis les paramètres, les notes de version et la page des licences (`settings.js`) quittent `app.js`.

---

## 4.11.2 — 12 novembre 2026

### Interface

- Réorganisation du code de la page, sans changement visible : l'état partagé (`state.js`) et les outils communs (`dom.js`) quittent le fichier principal `app.js`.

---

## 4.11.1 — 12 novembre 2026

### Sécurité

- Un fichier créé par l'agent dont l'extension est de celles que macOS exécute ou ouvre comme un programme (`.command`, `.app`, `.pkg`, `.dmg`, `.sh`, `.scpt`, `.workflow`, `.webloc`, etc.) reçoit l'extension `.txt` en plus, comme le faisaient déjà les extensions exécutables de Windows, conservées pour les autres appareils du réseau.

### Corrections

- Le message affiché quand aucun modèle n'est actif renvoie à l'onglet Modèles des paramètres, et non plus au menu de la zone de notification de Windows.
- La liste des licences ne mentionne plus cuDNN ni SVOX Pico, qui ne font pas partie de cette version.
- Interface : 31 textes et 5 règles de style qui n'étaient plus utilisés sont retirés.

---

## 4.11.0 — 12 novembre 2026

### Performances

- L'attention des prompts sur le GPU calcule ses scores et sa somme pondérée par des produits de matrices 8 × 8 (instructions `simdgroup_matrix` de Metal), et charge ses clés et valeurs huit nombres à la fois. Ce noyau remplace celui de la 4.10.0. Qwen3-VL 2B sur MacBook Air M2, clés et valeurs en 8 bits, mesures alternées : prompt de 512 tokens 533 → 603 à 608 tokens/s (le processeur : 227 à 243), de 4 096 tokens 198 → 423 à 424 (le processeur : 170 à 177), de 8 192 tokens 106 → 295 (le processeur : 115 à 117), de 16 384 tokens 171 (le processeur : 67).

### Réglages

- La limite de contexte des prompts lus par le GPU (« GPU : prompts jusqu'à ») passe de 4 096 à 262 144 positions : le GPU est désormais plus rapide à toutes les longueurs mesurées. Prompts d'environ 3 660 tokens, même réponse que sur le processeur : Qwen3-VL 2B 423 contre 168 tokens/s, Qwen3-VL 4B 152 contre 62, Qwen3.5-4B 139 contre 80, Qwen3.5-0.8B 493 contre 419.

---

## 4.10.0 — 12 novembre 2026

### Performances

- L'attention des prompts sur le GPU lit les clés et valeurs par tuiles : un groupe de calcul traite 4 à 16 tokens consécutifs et charge chaque tranche de 32 positions une seule fois pour eux. Qwen3-VL 2B sur MacBook Air M2, mesures alternées : prompt de 512 tokens 512 à 516 → 533 tokens/s, de 4 096 tokens 145 → 198 tokens/s (le processeur : 171 à 181).
- La limite de contexte des prompts lus par le GPU passe de 2 048 à 4 096 positions (réglage « GPU : prompts jusqu'à »), le GPU étant désormais plus rapide jusqu'à cette longueur. Prompts d'environ 3 660 tokens, même réponse que sur le processeur : Qwen3-VL 2B 206 contre 170 tokens/s, Qwen3-VL 4B 74 contre 63, Qwen3.5-4B 107 contre 81. Au-delà, à 8 192 positions, le processeur reste plus rapide (2B : 118 à 123 contre 106).

---

## 4.9.0 — 12 novembre 2026

### Performances

- La lecture des prompts des modèles Qwen 3.5 s'exécute sur le GPU, à partir de 24 tokens. Les poids aux formats Q5_K, Q8_0 et Q5_0 sont développés sur le GPU. Qwen3.5-4B Q4_K_M sur MacBook Air M2, prompt de 783 tokens, mesures alternées : 86 tokens/s sur le processeur, 148 à 149 tokens/s sur le GPU. Les points de reprise sont conservés : un prompt est coupé tous les 256 positions et l'état est copié entre deux morceaux.

### Corrections

- Les produits des prompts sur le GPU portent sur un nombre de lignes multiple de 32. Avec 24 à 31 tokens, ou 40, Metal Performance Shaders rendait des valeurs non numériques pour une matrice de Qwen3.5-0.8B, et la réponse était remplacée par une suite de points d'exclamation. Le contrôle au chargement utilise désormais un prompt de 29 tokens, qui fait apparaître ce cas.

---

## 4.8.0 — 12 novembre 2026

### Performances

- Points de reprise pour les modèles Qwen 3.5 et 3.6 : pendant la lecture d'un prompt, l'état des couches linéaires est copié toutes les 256 positions (les 4 plus récents par conversation, 1 Go au plus en tout). Une conversation qui ne partage qu'un début avec une précédente (même prompt système, message modifié ou relancé) reprend au point le plus proche au lieu de tout relire. Essai avec un prompt système de 794 tokens partagé par deux questions : la seconde reprend 768 tokens, avec la même réponse que posée seule (Qwen3.5-0.8B et Qwen3.5-4B).

---

## 4.7.0 — 12 novembre 2026

### Performances

- Les mélanges d'experts écrivent leurs réponses sur le GPU : produit du routeur, choix des experts (softmax et sélection des plus probables), produits des experts choisis par indirection, expert partagé et somme pondérée. Les prompts de ces modèles restent sur le processeur.
- Le GPU lit aussi le format de poids Q5_0, employé par certains fichiers pour les experts trop étroits pour les formats K.
- Qwen 3.6 réduit à 11B-A3B (Q4_K_M) sur MacBook Air M2, mesures alternées sur 200 tokens : 24,4 à 25,0 tokens/s sur le processeur, 26,1 à 26,3 tokens/s sur le GPU. Le contrôle au chargement trouve des logits à 0,00006 % de ceux du processeur ; réponses identiques pour une question et pour une conversation à trois tours.
- Seule l'architecture Qwen 3.5 / 3.6 MoE a été vérifiée sur le GPU. Les autres mélanges d'experts (Qwen3 MoE, Mixtral, Granite MoE) y sont admis sans avoir été essayés ; le contrôle au chargement les laisse au processeur en cas d'écart.

---

## 4.6.0 — 12 novembre 2026

### Plateforme

- Nouvelle architecture de modèles : Qwen 3.5 et 3.6 à mélange d'experts (`qwen35moe`, par exemple Qwen3.6-35B-A3B) : les couches linéaires de Qwen 3.5, 256 experts dont 8 par token, et un expert partagé dosé par une porte sigmoïde. Ces modèles s'exécutent sur le processeur ; le GPU ne prend pas encore les mélanges d'experts.
- Vérification par rapport à llama.cpp 0.9.0 sur processeur, en choix glouton, avec un Qwen 3.6 réduit à 11B-A3B (Q4_K_M) : une réponse identique en entier, deux autres identiques jusqu'à une hésitation du modèle (0,061 contre 0,058 ; 0,399 contre 0,390). Ce modèle écrit à 24,7 tokens/s sur le processeur d'un MacBook Air M2.

### Accès et documents

- La recherche Hugging Face indique les dépôts Qwen 3.5 et 3.6 à mélange d'experts comme compatibles.

---

## 4.5.0 — 12 novembre 2026

### Performances

- Les étapes d'écriture des modèles Qwen 3.5 s'exécutent sur le GPU par Metal : couches Gated DeltaNet (convolution, normes, règle delta, norme à porte) et attention à porte. Qwen3.5-4B Q4_K_M sur MacBook Air M2, mesures alternées sur 200 tokens : 11,7 à 12,2 tokens/s sur le processeur, 19,5 tokens/s sur le GPU. La lecture des prompts de ces modèles reste sur le processeur.
- Le GPU calcule aussi les produits des poids aux formats Q5_K et Q8_0, présents dans les fichiers Qwen 3.5 en Q4_K_M.
- Le contrôle au chargement compare le GPU au processeur : logits à 0,03 à 0,04 % près pour Qwen3.5-4B, même token le plus probable. Réponses identiques sur le GPU et sur le processeur, pour une question, trois questions traitées ensemble et une conversation à trois tours.

### Plateforme

- L'état des conversations des modèles Qwen 3.5 est rangé dans une zone réservée, que le GPU lit et écrit sans copie. Au plus 16 requêtes simultanées et 32 conversations en cache pour ces modèles.

### Accès et documents

- La recherche Hugging Face annonce « GPU possible » pour les fichiers Qwen 3.5 en Q4_K et Q6_K.

---

## 4.4.0 — 12 novembre 2026

### Plateforme

- Nouvelle architecture de modèles : Qwen 3.5 (`qwen35`, de 0,8B à 27B). Trois couches sur quatre sont des couches « Gated DeltaNet », une attention linéaire qui conserve un état de taille fixe par conversation ; la quatrième est une attention à porte de sortie. Les modèles Qwen 3.5 s'exécutent sur le processeur ; le GPU n'est pas encore utilisé pour eux.
- Vérification par rapport à llama.cpp 0.9.0 sur processeur, en choix glouton : réponses identiques sur 150 tokens (Qwen3.5-4B Q4_K_M ; Qwen3.5-0.8B Q8_0 sur une question sur trois, les deux autres divergeant sur des candidats à égalité au centième près) ; gabarit de conversation identique au gabarit Jinja du modèle, avec et sans raisonnement.

### Accès et documents

- La recherche Hugging Face indique les dépôts Qwen 3.5 comme compatibles. Pour ces modèles, elle n'annonce ni « GPU possible » ni lecture des images, que le moteur ne prend pas encore en charge.

### Performances

- Seules les couches d'attention conservent des clés et valeurs (une sur quatre pour Qwen 3.5), ce qui réduit d'autant la mémoire par token.
- Qwen3.5-4B Q4_K_M sur MacBook Air M2, processeur : écriture 10,2 à 11,5 tokens/s.

### Corrections

- Une conversation n'est reprise qu'à une position que le modèle sait reprendre : pour Qwen 3.5, au début ou à la fin exacte du texte déjà lu. La relecture anticipée de l'historique en tient compte ; sans cette correction, elle était abandonnée dès le troisième tour.

---

## 4.3.0 — 12 novembre 2026

### Accès et documents

- Les recherches Hugging Face et les listes de fichiers des dépôts sont conservées sur le Mac (les 30 plus récentes de chaque sorte). Hors ligne, ou si Hugging Face ne répond pas, la dernière réponse conservée est affichée avec sa date.
- L'onglet Modèles propose deux boutons : « Rafraîchir depuis le Mac » (liste, réserve et dernière recherche, sans réseau) et « Rafraîchir depuis Hugging Face » (catalogue depuis GitHub, dernière recherche depuis Hugging Face). Les recherches conservées sont listées sous le champ de recherche.
- Au démarrage, les modèles installés avant la version 4.2.0 sont ajoutés à la réserve, sous forme de clones.
- API : paramètre `local=1` et champ `cached_at` pour `/albireo/v1/hub/search` et `/albireo/v1/hub/files` ; nouvelle route `/albireo/v1/hub/recent`.

### Corrections

- Une copie entrée dans la réserve porte la date de son entrée, et non celle du fichier d'origine : l'ordre de retrait au-delà du plafond est respecté.

---

## 4.2.0 — 12 novembre 2026

### Accès et documents

- Chaque modèle téléchargé est gardé dans la réserve du Mac dès la fin de son téléchargement, et non plus seulement à sa suppression.
- L'onglet Modèles propose « Restaurer depuis le Mac » pour un modèle dont tous les fichiers sont dans la réserve, en ligne comme hors ligne.
- En ligne, une restauration reprend les fichiers sains de la réserve et ne télécharge que les fichiers absents ou altérés. Une copie altérée est retirée de la réserve.

### Réglages

- Nouveau réglage « Réserve des modèles » (onglet Service, groupe Modèles) : plafond de 0 à 1 000 Go, 200 Go par défaut. Il ne compte que les copies des modèles supprimés ; au-delà, les moins récemment utilisées sont retirées en premier. La carte affiche la place utilisée et propose « Vider la réserve ».
- API : `GET /albireo/v1/reserve` (contenu et place) et `DELETE /albireo/v1/reserve` (vidage) ; champ `in_reserve` dans la liste des modèles.

### Performances

- Les copies de modèles (réserve, restauration, import) sont des clones APFS : instantanées, sans place disque supplémentaire tant que le fichier ne change pas.
- L'empreinte SHA-256 utilise CommonCrypto et les instructions SHA de la puce : 2,1 Go/s contre 0,29 Go/s auparavant (M2). Restauration de Qwen3-VL 4B Thinking et de son projecteur (2,8 Go) : 6,1 à 6,8 s contre 23 s, sans place prise.

---

## 4.1.1 — 12 novembre 2026

### Plateforme

- La version 4.0.0 à 4.1.0, écrite sous Windows, est compilée et testée sur Mac (puce M2) : test complet d'Albireo 18 étapes sur 18, de Pseudalis 34 sur 34.

### Sécurité

- Les contrôles des dossiers (données, modèles, réserve, import) lisent les listes de contrôle d'accès (ACL) de macOS. Une entrée qui autorise un autre compte à écrire, supprimer ou changer les droits fait refuser le dossier ; une entrée qui interdit est acceptée. Un dossier sécurisé par le service perd une telle entrée.

### Accès et documents

- Restauration depuis la réserve vérifiée avec le service sur Mac : Qwen3-VL 4B Thinking et son projecteur (2,8 Go) restaurés hors ligne en 23 secondes, empreintes conformes ; une copie altérée est refusée sans installation ; en ligne, le modèle est alors téléchargé de nouveau.

---

## 4.1.0 — 12 novembre 2026

### Accès et documents

- En mode hors ligne, un modèle du catalogue peut être restauré depuis la réserve lorsque tous ses fichiers sont présents et vérifiés. Cette opération ne recourt jamais au réseau, même si le réglage change pendant son exécution.
- La page des modèles signale les échecs de restauration et de téléchargement.

### Corrections

- Une réserve incomplète ou altérée ne remplace pas la progression d'un téléchargement antérieur. En ligne, le repli vers le catalogue reste disponible.
- La fonction qui retire les paramètres des adresses dans les messages compile de nouveau et préserve les séparateurs de ligne.

---

## 4.0.5 — 12 novembre 2026

### Corrections

- L'outil de préparation des modèles signale les fichiers GGUF invalides ou tronqués sans afficher de trace d'erreur Python.

---

## 4.0.4 — 12 novembre 2026

### Accès et documents

- La suppression d'un modèle conserve dans `<données>/reserve` les fichiers dont l'empreinte correspond à celle enregistrée. Une réinstallation les restaure sans réseau quand tous les fichiers attendus sont présents et conformes ; sinon, le téléchargement reprend depuis le catalogue.

### Sécurité

- La réserve est protégée par des permissions de dossier 0700 et de fichier 0600. Une copie absente, altérée ou non conforme n'est pas installée.

---

## 4.0.3 — 12 novembre 2026

### Réglages

- Le service ne recherche et ne propose plus que le moteur CPU construit pour macOS. Les anciennes valeurs de moteur et d'accélérateur enregistrées sont remplacées par `auto` ; l'API refuse les choix de moteur non pris en charge.
- La liste des options de moteur expose uniquement le choix CPU. Les entrées de licence CUDA et OpenVINO ne sont plus annoncées.

---

## 4.0.2 — 12 novembre 2026

### Corrections

- Les contrôles de dossiers sur macOS vérifient maintenant le compte propriétaire et les permissions POSIX ; les dossiers sécurisés par Albireo sont limités au compte courant. Les ACL étendues ne sont pas évaluées.

---

## 4.0.1 — 12 novembre 2026

### Corrections

- Le message de migration des certificats indique maintenant que la clé du serveur est conservée au format PEM avec des permissions de fichier restreintes.

---

## 4.0.0 — 12 novembre 2026

### Accès et documents

- Plus aucune clé : le catalogue des modèles n'est plus signé et le service ne vérifie plus de signature. Il lit `catalog.json` à son adresse, dans le dépôt de l'original, ou dans un dossier.
- L'espace de publication de l'original (téléchargements par un lien partagé protégé par un mot de passe) est retiré : chaque modèle se télécharge directement à l'adresse que donne le catalogue, en trois tentatives par segment. Le délai perdu à interroger l'espace avant chaque téléchargement disparaît.
- L'import d'un dossier de disque n'exige plus que `catalog.json` : le fichier `catalog.json.sig` n'est plus lu ni attendu. Les modèles listés restent contrôlés par leur nom, leur taille et leur empreinte SHA-256.
- Les outils de signature et de publication de l'original (`albireo-sign`, `fill_public_link.py`, `public_link.py`) sont retirés.

### Sécurité

- L'import d'un dossier accepte désormais un catalogue non signé. Un dossier fourni par un tiers peut donc déclarer ses propres modèles : n'importer que des dossiers de confiance. Les empreintes SHA-256 continuent de vérifier chaque fichier téléchargé ou importé.
- Le masquage des adresses de l'espace de publication dans le journal et dans les erreurs est retiré avec l'espace ; les adresses de requête (clés de recherche) restent retirées des messages.

---

## 3.2.0 — 11 novembre 2026

### Modèles

- Recherche de tous les modèles GGUF publiés sur Hugging Face, depuis l'onglet « Modèles », les plus téléchargés d'abord. Les modèles que le moteur ne sait pas lire (architecture non prise en charge, format de poids non lu, fichier découpé en plusieurs parties) sont affichés en gris, à la fin de la liste, avec la raison.
- Pour un dépôt compatible, liste de ses fichiers avec leur taille, l'unité de calcul attendue (GPU possible ou processeur) et une alerte quand un fichier dépasse la moitié de la mémoire du Mac ; lecture des images proposée quand le dépôt fournit un projecteur.
- Un fichier choisi rejoint une liste personnelle, gardée par le service à côté du catalogue de l'original : il se télécharge, se vérifie par son empreinte SHA-256, s'installe et s'active comme les modèles du catalogue.
- Routes ajoutées : `GET /albireo/v1/hub/search`, `GET /albireo/v1/hub/files`, `POST /albireo/v1/hub/add`.

---

## 3.1.1 — 11 novembre 2026

### Interface

- Page d'accueil : la zone de saisie est placée au milieu de l'écran, sous le message de bienvenue.
- iPad et iPhone : les fenêtres (paramètres, fiches) tiennent dans la hauteur réellement visible, barres de Safari déduites, et défilent jusqu'en bas au toucher ; l'application occupe la même hauteur visible.
- Le bouton « À propos » du panneau latéral a été retiré : l'onglet reste accessible par l'engrenage de la barre supérieure.

---

## 3.1.0 — 11 novembre 2026

### Modèles

- Nouvel onglet « Modèles » dans les paramètres : catalogue des modèles avec leurs spécialités (général, code, cybersécurité, vision, raisonnement, outils), mention « Conseillé », « Possible » ou « Déconseillé » calculée d'après la mémoire du Mac et la vitesse d'écriture estimée, téléchargement et annulation depuis l'interface, suppression d'un modèle installé, rafraîchissement de la liste.
- Chaque modèle dispose d'une fiche explicative (principe, avantages, inconvénients, schéma de la mémoire, explications à trois niveaux), également accessible depuis le bas de la page d'accueil.
- Le modèle choisi est activé pour l'ensemble des fonctions du service ; le modèle de recherche dans les documents reste distinct.

### Interface

- Le sélecteur de fonction (Général, Code, Cybersécurité) de la barre supérieure et le réglage « Fonction par défaut » ont été retirés.
- Les quatre exemples de questions de la page d'accueil ont été retirés.
- Le modèle utilisé est affiché sous la zone de saisie et dans le panneau latéral ; ce dernier indique désormais l'unité de calcul (GPU ou processeur) au lieu du nom technique du moteur.

---

## 3.0.0 — 11 novembre 2026

Version majeure : une route de l'interface a été retirée.

### Retraits

- La lecture des réponses à voix haute a été retirée, avec la route `POST /v1/audio/speech` de l'interface compatible OpenAI, la route `GET /albireo/v1/speech/voices`, le programme `albireo-speech`, la bibliothèque de synthèse SVOX Pico et sa voix française. Les applications qui appelaient ces routes reçoivent désormais une réponse 404.
- Le bouton « Lire à voix haute » des réponses n'apparaît plus.

---

## 2.0.2 — 11 novembre 2026

### Interface

- La fiche « Pas de serrure sur ce Mac » a été retirée de l'onglet « Service ». Les conséquences de ce choix restent décrites dans la section « Sécurité » de la version 2.0.0.

### Code

- Les commentaires du code source d'Albireo et de Pseudalis sont rédigés en français. Le comportement du programme est inchangé : un contrôle automatique a vérifié, fichier par fichier, que seuls les commentaires ont été modifiés.

---

## 2.0.1 — 11 novembre 2026

### Interface

- Le logo d'Albireo, en haut du panneau latéral, ramène à la page d'accueil : il ouvre une nouvelle discussion et efface la saisie en cours ainsi que les pièces jointes en attente.

---

## 2.0.0 — 11 novembre 2026

Première version de la branche macOS. Elle repart d'Albireo Community Edition 1.0.12 (dépôt d'origine, commit `184ea00`) et ne vise plus que les Mac à puce Apple.

### Plateforme

- Albireo fonctionne exclusivement sur macOS, puce Apple (arm64). Le code propre à Windows, à Linux, aux processeurs x86, à CUDA et à OpenVINO a été retiré, ainsi que l'icône de la zone de notification, l'installateur et le mécanisme de mise à jour d'origine.
- L'interface et la synthèse vocale sont proposées en français uniquement ; le choix de la langue, les traductions et les voix étrangères ont été retirés. Les titres de discussion, la mémoire et la lecture à voix haute supposent des échanges en français.

### Performances

- Moteur GPU Metal : les étapes d'écriture et la lecture des prompts de 24 à 2 048 positions s'exécutent sur le GPU de la puce, sans copie des poids grâce à la mémoire unifiée. Sur MacBook Air M2, l'écriture passe de 18,3–19,0 à 21,3–22,1 tokens/s (modèle de 4B) et la lecture de 512 tokens de 91,5–93,5 à 153–156 tokens/s.
- Un contrôle au chargement de chaque modèle compare le GPU au processeur et rend la main au processeur en cas d'écart ou d'incident.
- Processeur : noyaux NEON pour les formats Q4_K et Q6_K et pour l'attention, produits des prompts par le framework Accelerate, écriture répartie sur les cœurs de performance.

### Réglages

- Nouvel onglet « Service » dans les paramètres : dix-neuf réglages du service et du moteur, appliqués immédiatement ou après un redémarrage du service, chacun accompagné d'une fiche explicative (principe, avantages, inconvénients, schéma, explications à trois niveaux).
- Valeurs conseillées pour le Mac, issues des mesures, rétablies par un bouton dédié.
- Sauvegarde et restauration des réglages sous un libellé.
- Nouveaux réglages : seuils des prompts traités par le GPU, attente active des threads, préparation de la question suivante, dossier des modèles.
- Icône d'engrenage dans la barre supérieure, sur toutes les pages : accès direct aux paramètres, à l'onglet « Service », à « À propos » et aux notes de version.
- La ligne « GPU » de l'onglet « À propos » indique l'usage du GPU par chaque modèle chargé et la mémoire qu'il y occupe.

### Accès et documents

- Accès depuis le réseau local, en HTTP sur un port dédié (8444 par défaut), avec un mot de passe facultatif.
- Dossier de documents sur le Mac : les fichiers peuvent être joints depuis ce dossier, et toute pièce jointe ajoutée dans l'interface y est copiée.
- Copie dans le presse-papiers disponible hors connexion chiffrée.
- Lien « Notes de version » dans l'onglet « À propos », qui affiche le présent document.

### Sécurité

- Le jeton d'administration et le contrôle des en-têtes `Host` et `Origin` ont été retirés : sur le Mac, toute requête locale peut modifier les réglages. Ce choix convient à un usage personnel ; il expose le service à une page web malveillante ouverte sur le même Mac.
- Sans mot de passe, l'accès réseau est ouvert à tout appareil du réseau local, et le trafic n'est pas chiffré.
- Le mot de passe réseau, lorsqu'il est défini, est conservé sous forme d'empreinte PBKDF2-HMAC-SHA-256 ; les sessions reposent sur un cookie signé valable trente jours.

### Corrections

- Le redémarrage du service dans un nouveau processus utilise le chemin réel du programme sur macOS.
- Correction d'un débordement d'écriture dans le noyau d'attention Metal (version de développement).

---

## 1.0.12 (base de l'original) — 9 au 11 novembre 2026

Branche macOS en développement, avant sa numérotation propre. Les numéros de cette époque sont ceux de la version de l'original sur laquelle elle reposait.

- Intégration d'Albireo 1.0.12 de l'original : clés et valeurs des modèles de langue rangées en entiers de 8 bits par défaut.
- Attention NEON pour les clés et valeurs en 8 bits : lecture d'un prompt de 4 096 tokens de 36–38 à 158–162 tokens/s, écriture qui suit de 9 à 22,5 tokens/s (modèle de 2B).
- Noyaux Q4_K et Q6_K : sommes conservées en registres vectoriels.
- Écriture des réponses répartie sur les cœurs de performance, lecture des prompts sur tous les cœurs.

---

## 1.0.11 (base de l'original) — 6 au 9 novembre 2026

- Première compilation complète d'Albireo et de Pseudalis sur macOS arm64 : moteurs AVX2 exclus, en-tête d'ordre des octets de macOS pour la synthèse vocale d'alors, sources NEON de libpng, frameworks CoreFoundation et Security liés au serveur HTTP, date de modification des fichiers lue au format de macOS.
- Fonctionnement à l'exécution : chemin du programme lu par `_NSGetExecutablePath`, mémoire et processeur lus par les interfaces de macOS ; premier démarrage du service et première réponse sur Mac.
- Noyaux NEON pour les formats Q4_K et Q6_K, exacts au bit près par rapport au code d'origine ; produit en flottants NEON pour les prompts ; attention NEON. Écriture du modèle de 2B de 7–11 à 33–37 tokens/s.
- Produits des prompts par le framework Accelerate, et attention de l'encodeur d'images par Accelerate : encodage d'une image de 512 × 512 de 8,9–10,3 s à 1,8–2,2 s.
- Pseudalis : test complet à 28 étapes sur 28.

---

## 1.0.8 (base de l'original) — 4 au 6 novembre 2026

- Création de la branche macOS à partir d'Albireo 1.0.8 de l'original.
- Première compilation de dlib et de ses exemples sur Mac Apple Silicon, sans erreur ; relevé des cinq causes d'échec de la compilation d'Albireo et de Pseudalis.
