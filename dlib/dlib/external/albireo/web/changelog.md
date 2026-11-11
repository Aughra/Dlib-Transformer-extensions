# Notes de version

Albireo pour macOS sur puce Apple, version dérivée d'Albireo Community Edition (Cydral Technology), maintenue par TortueTech. Les numéros suivent la gestion sémantique des versions : majeur pour une rupture, mineur pour une fonction nouvelle, correctif pour une correction. Les versions sont présentées de la plus récente à la plus ancienne.

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
