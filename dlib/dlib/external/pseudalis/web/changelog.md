# Notes de version de Pseudalis

Pseudalis pour macOS sur puce Apple, version dérivée de Pseudalis Community (Cydral Technology), maintenue par TortueTech. Le fork numérote Pseudalis à part de l'original à partir de 2.0.0. Les numéros suivent la gestion sémantique des versions : majeur pour une rupture, mineur pour une fonction nouvelle, correctif pour une correction. Les versions sont présentées de la plus récente à la plus ancienne ; avant 2.0.0, la section porte le numéro de la version de l'original sur laquelle reposait le fork.

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
