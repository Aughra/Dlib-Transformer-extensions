# Notes de version de Pseudalis

Pseudalis pour macOS sur puce Apple, version dérivée de Pseudalis Community (Cydral Technology), maintenue par TortueTech. Le fork numérote Pseudalis à part de l'original à partir de 2.0.0. Les numéros suivent la gestion sémantique des versions : majeur pour une rupture, mineur pour une fonction nouvelle, correctif pour une correction. Les versions sont présentées de la plus récente à la plus ancienne ; avant 2.0.0, la section porte le numéro de la version de l'original sur laquelle reposait le fork.

## 3.3.0 — 16 novembre 2026

### Protection

- Détecteurs réentraînés, repris de l'original (Pseudalis 1.0.11) : l'étiqueteur de séquences à neuf et le modèle d'empans à partir de celui de 1.0.5, sur un complément d'exports sans en-tête, de pages de titre, de courriers de santé, de rapports économiques et de fichiers source dans les cinq langues. Mieux reconnus : les noms et les villes seuls dans les cellules d'un fichier sans en-tête, une ville après une date, les caisses de santé et les cabinets, les nationalités après leurs indices italiens, espagnols et allemands, les numéros de médecins après leurs libellés (LANR, BSNR, GMC) et leurs ordres, le numéro de sécurité sociale espagnol, le numéro NHS, et la fin de validité d'une carte après ses mots (« expiry date 07/29 »), décalée comme un mois et son année. Laissés lisibles : les identifiants, mots-clés et littéraux du code (True, False, None, null), les médicaments, examens et termes de statistique, les étapes numérotées des diapositives.
- Débit inchangé : un texte de 30 000 octets protégé en 1,46 à 1,48 s (1,44 à 1,46 s avec les détecteurs de 3.2.0, mesures non alternées).

### Corrections

- Une liste de mots de dlib, reprise de l'original, faisait refuser l'exemple de l'anonymiseur par clang (concaténation de chaînes prise pour une virgule oubliée) : l'entrée est mise entre parenthèses.

---

## 3.2.0 — 16 novembre 2026

### Protection

- Colonnes imposées, reprises de l'original (Pseudalis 1.0.8 à 1.0.10) : dans un fichier CSV à en-tête, chaque valeur d'une colonne nommée par l'administrateur prend le pseudonyme du type de la colonne, sans détection. L'en-tête est cherché par ces noms parmi les 16 premières lignes, sous un titre ou la ligne `sep=` d'Excel, sans tenir compte de la casse ni des accents ; un texte d'un autre nom est lu comme un CSV quand son en-tête et les deux lignes qui le suivent contiennent autant de fois le séparateur. Les valeurs de ces colonnes, apprises comme valeurs de leur type, sont remplacées aussi ailleurs dans le fichier.
- Une colonne de type IP prend les adresses IPv4 et IPv6 mélangées, chacune avec le pseudonyme de sa forme, sa longueur de préfixe, ses crochets et son port gardés ; IPV4 et IPV6 ne sont pas des types de colonne.
- Un texte protégé dont les octets sont devenus de l'ASCII pur garde la trace de l'encodage de son original (Windows-1252 par exemple), pour qu'une restauration le rende dans cet encodage.

### Accès et documents

- Section « Colonnes imposées » dans le menu de la page : nom de la colonne, type, ajout, retrait, enregistrement, erreurs en français. Routes `GET` et `PUT /v1/columns` (100 colonnes au plus, noms de 200 caractères au plus).

---

## 3.1.1 — 16 novembre 2026

### Corrections

- Code que plus rien n'appelait retiré, comme dans l'original (Pseudalis 1.0.6) : dix-sept fonctions et une structure (copie et identité de fichiers, lecture sans lien, lancement de processus, comparaison de versions, empreinte de certificat, résolution de noms, liste des images d'un enregistrement, nombre de termes imposés), soit 451 lignes, sans changement visible. Le reste de 1.0.6 (icône de Windows, langue du navigateur, import d'une mise à jour depuis un dossier) ne concerne pas le fork, en français seul et sans programme de mise à jour.

---

## 3.1.0 — 16 novembre 2026

### Protection

- Clé de séance pour le relais d'Albireo : `POST /v1/text/anonymize` accepte le champ `session`. Le texte est alors protégé sous une clé tirée au hasard au démarrage du moteur, gardée en mémoire seulement et jamais écrite. Une même valeur garde le même pseudonyme d'un texte à l'autre tant que le service tourne, et la réponse donne la valeur de chaque pseudonyme écrit (`values`). Albireo s'en sert pour envoyer une discussion à un moteur extérieur sans ses données personnelles, puis remettre les vraies valeurs dans la réponse.
- Les documents, les noms et les textes protégés sans ce champ gardent leur clé jetable : leurs pseudonymes ne reviennent pas, comme en 3.0.0. Un pseudonyme fait sous une clé jetable, présent dans un texte du relais, est reconnu et laissé tel quel.
- Un redémarrage du service change la clé de séance : une même valeur reçoit alors un autre pseudonyme.

---

## 3.0.0 — 15 novembre 2026

### Protection

- Pseudonymes sans retour : chaque fichier, texte ou nom est protégé sous une clé tirée au hasard pour lui seul, puis effacée. Une même valeur reçoit le même pseudonyme dans tout le document (archive comprise), un autre dans un autre document, et aucun pseudonyme ne peut plus revenir à sa valeur. Les copies protégées ne contiennent plus de balises `[[TYPE:…]]`, seulement des pseudonymes lisibles.
- Restaurer un fichier, c'est rendre son original conservé : un fichier dont l'original a été oublié, ou écrit ailleurs à partir d'un fichier protégé, ne peut plus être restauré. Un texte protégé ne se restaure pas. Les noms des dossiers du lecteur restent pseudonymisés.
- Un pseudonyme déjà écrit (un dossier renommé par le lecteur, un fichier extrait d'une archive protégée, un fichier modifié sur le lecteur) est reconnu et laissé tel quel.
- Les copies protégées par une version antérieure, avec leurs balises, se restaurent encore.

### Accès et documents

- La section « Clé de cet ordinateur », les routes `POST /v1/key/export` et `POST /v1/key/import`, les cases « Écrire les balises » et le bouton « Restaurer » d'un texte sont retirés ; le réglage `tags` vaut toujours `false`.

### Rupture

- Version majeure : deux routes et un comportement (la restauration à partir des balises, pour les nouvelles copies) disparaissent.

---

## 2.2.0 — 15 novembre 2026

### Sécurité

- Le coffre des originaux n'est plus chiffré : les originaux, leurs images et les descriptions d'Albireo sont gardés tels quels dans `vault/`. Les blobs chiffrés d'une version antérieure se lisent encore, et sont réécrits en clair au démarrage du service.
- L'export de la clé est la clé en clair (format version 2), sans phrase secrète : quiconque lit le fichier peut restaurer les copies protégées. Les champs de phrase secrète quittent la section « Clé de cet ordinateur ». Un export protégé d'une version antérieure (version 1) s'importe encore par l'interface, avec sa phrase.
- Ces deux changements suivent la décision de l'utilisateur de ne plus rien chiffrer en sortie de Pseudalis ; les balises des copies protégées, qui portent encore les valeurs chiffrées, suivront.

---

## 2.1.0 — 15 novembre 2026

### Accès et documents

- Accès depuis le réseau de la maison (iPad, téléphone) : réglage « Ouvrir Pseudalis aux appareils de la maison » et son port (8445 par défaut), dans la section Réglages. Un second serveur, en HTTP simple et sans mot de passe, répond sur chaque adresse du Mac avec les mêmes routes ; le service redémarre quand le réglage change. Réglages `lan` et `lan_port` dans l'interface de programmation.
- Le sélecteur « Albireo | Pseudalis » apparaît aussi depuis un autre appareil : il mène à l'accès réseau d'Albireo quand celui-ci est ouvert.

### Sécurité

- L'accès réseau n'a pas de mot de passe : tout appareil du réseau peut protéger et restaurer des fichiers, lire les originaux conservés, exporter la clé et changer les réglages. Il ne doit pas être activé sur un Wi-Fi partagé ou public.
- Le serveur du réseau n'accepte que les requêtes qui nomment le Mac par une adresse IPv4 ou un nom en `.local` (421 sinon), et refuse une requête venue d'une page d'une autre origine (403) : un site web ne peut pas piloter Pseudalis par le navigateur.

### Corrections

- Trois textes restés de la version Windows renvoyaient à « l'application ouverte depuis l'icône » ou au jeton d'administration ; ils renvoient à la section Réglages.

---

## 2.0.1 — 15 novembre 2026

### Accès et documents

- L'application est en français seulement : le choix de la langue et les textes anglais sont retirés, le service répond toujours en français (descriptions d'images comprises), et le réglage `language` n'a plus d'effet.
- La page de l'interface de programmation est en français : descriptions des routes et libellés de Swagger UI.
- Le débit du moteur a un bouton « ? » qui explique comment il est mesuré.

---

## 2.0.0 — 15 novembre 2026

### Accès et documents

- L'administration n'a plus de jeton : les originaux conservés, la clé de l'ordinateur, les réglages et les termes imposés s'affichent toujours dans l'application, et leurs routes répondent sur le Mac sans l'en-tête `X-Pseudalis-Admin`, comme celles d'Albireo. Le service ne crée plus le fichier `admin.token`.
- L'application est en français par défaut, quelle que soit la langue du navigateur.
- L'application est réorganisée : un menu à gauche (protéger des fichiers, restaurer des fichiers, protéger un texte, originaux conservés, clé, réglages, termes imposés), une section à la fois, l'état du service dans le menu, un thème sombre (celui du système, ou celui du bouton du menu).
- Le débit du moteur s'affiche en direct dans le menu : les documents lus et les copies protégées écrites, en kilo-octets par seconde, sur les cinq dernières secondes (route `GET /v1/speed`).
- Un sélecteur « Albireo | Pseudalis » mène à Albireo quand il tourne sur le même Mac.
- La page de l'interface de programmation est refaite : en-tête en français, démarrage rapide (curl et Python, prêts à copier, avec l'adresse et le certificat de ce service), thème clair ou sombre, filtre des sections.

### Sécurité

- Sans jeton, tout programme du Mac qui joint `https://localhost` au port du service peut lire les originaux conservés et changer les réglages, comme pour Albireo. Restent les gardes de toutes les routes : l'en-tête Host doit nommer le serveur local, et une requête venue d'une page doit venir de ce serveur, pour qu'aucun site web ne pilote le service par le navigateur.

### Documents

- Le README est traduit en français. Pseudalis a sa propre documentation dans le fork : guide, notes pour l'auteur, tableau des différences avec Windows, blog et ces notes de version.

---

## 1.0.5 (fork) — du 6 au 15 novembre 2026

### Plateforme

- Pseudalis compile et tourne sur macOS à puce Apple (6 novembre) : chemin du programme par `_NSGetExecutablePath`, dates des fichiers par `st_mtimespec`.
- Le code propre à Windows, à Linux et aux processeurs x86 est retiré (11 novembre) : installateur, icône de la zone de notification, commandes de l'Explorateur, mises à jour par installateur, moteurs AVX2 ; le moteur est compilé dans le service.
- La page ne garde que le français et l'anglais (11 novembre) ; les modèles de reconnaissance de dlib lisent toujours les autres langues.
- Les contrôles de dossiers vérifient le propriétaire, les droits POSIX et les ACL étendues de macOS (12 et 13 novembre).
- Pseudalis se lie à la zlib de macOS (15 novembre), et dlib, sur laquelle il repose, ne vise plus que macOS.

### Corrections

- Le lecteur compte ses parcours et dit s'il est au repos (`scans` et `idle` de `GET /v1/drive`), ce qui permet au test complet d'attendre son travail au lieu d'un temps fixe ; 34 étapes sur 34 (14 novembre).
- En `--console`, un service lancé en arrière-plan s'arrête par SIGINT (14 novembre).
- Deux avertissements du compilateur dans le code de dlib qu'il emploie sont corrigés (14 novembre).

### Origine

- Fusions de l'original jusqu'à Pseudalis 1.0.5 (8 et 9 novembre) ; la dernière, le 9 novembre, prend l'original au commit `184ea00`. Le fork ne fusionne plus l'original depuis le 11 novembre.
