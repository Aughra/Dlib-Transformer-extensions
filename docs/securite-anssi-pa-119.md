# Gulliver et le guide RAG de l'ANSSI (ANSSI-PA-119)

**Version décrite : Gulliver 7.17.0 (2026-11-28).**

Ce document confronte Gulliver au guide de l'ANSSI *Recommandations de sécurité pour un système d'IA générative à enrichissement contextuel (RAG)*, ANSSI-PA-119, version 1.0 du 13/10/2026. Ce guide est publié sous la Licence Ouverte v2.0 (Etalab), et on le cite ici avec sa source et sa date, comme cette licence le demande. Le guide demande lui-même (R1) une analyse de risque, puis la liste des recommandations retenues, des alternatives et des risques résiduels acceptés : c'est l'objet de ce document. Il suit le code, et se relit à chaque évolution de la recherche dans les documents, du moteur, des accès ou de l'hébergement.

Légende des statuts :

- ✅ appliqué ;
- 🟡 appliqué en partie, ou par une alternative ;
- ⚪ sans objet pour Gulliver ;
- 🔴 non appliqué, risque accepté ou action à faire.

---

## 1. Analyse de risque (R1)

### Le système

Gulliver est un service local qui tourne sur un seul Mac (Apple Silicon) : un seul programme, `gulliver-service`, avec sa page web.

- **Générateur** : le moteur de Gulliver, sur le processeur ou le GPU du Mac. Rien ne sort du Mac, sauf si l'administrateur branche un moteur externe (onglet Service, « Moteur externe »).
- **Récupérateur et base vectorielle** : SQLite dans le dossier de données. Les vecteurs viennent du modèle d'embedding local. La recherche est exhaustive (pas d'index approché), et chaque passage est comparé.
- **Sources** :
  - la bibliothèque d'un profil (`profiles/<id>/documents`) ;
  - le dossier commun ;
  - les documents joints à une discussion ;
  - les pages web que la discussion lit (`fetch_page`, `web_search`).
- **Utilisateurs** : quelques profils d'une même maison, avec un administrateur principal et un administrateur de secours. On se connecte par une adresse, un mot de passe et un code reçu par mail.
- **Accès** :
  - le Mac lui-même (`127.0.0.1`) ;
  - le réseau de la maison (http, port 8444, à ouvrir dans l'onglet Service) ;
  - la discussion en ligne par la passerelle Cloudflare, qui n'ouvre qu'une liste fermée de routes (`cloudflare/lib/routes.js`).

**Catégorie du tableau 1 du guide : « consultatif, avec des outils ».** Les réponses sont lues par un humain. Le modèle dispose d'outils, mais tous en lecture ou dans un bac à sable : calculatrice, JavaScript isolé sans réseau, recherche et lecture des documents et des discussions, recherche Wikipédia, lecture d'une page web, création d'un fichier à télécharger. Aucun outil n'écrit dans le système, n'envoie de mail ou n'agit pour l'utilisateur.

### Les données et ce qui compte

| Bien | Confidentialité | Intégrité | Disponibilité |
|---|---|---|---|
| Documents et passages de chaque profil | **Forte** : papiers personnels, d'un profil à l'autre | Moyenne | Faible |
| Documents communs | Moyenne | **Forte** : lus par tous les profils | Faible |
| Discussions, mémoire | **Forte** | Moyenne | Faible |
| Comptes (mots de passe, sessions, secret du mail) | **Forte** | **Forte** | Moyenne |
| Le Mac et ses autres données | **Forte** | **Forte** | Moyenne |

### Menaces retenues

| Menace | Vecteur dans Gulliver | Mesures |
|---|---|---|
| Injection indirecte | Un document joint ou déposé, une page web lue, qui contient des consignes (texte invisible, phrase « ignore les consignes ») | R25, R25+, R34, R2, R35 |
| Empoisonnement du dossier commun | Un profil dépose un document commun qui oriente les réponses des autres | R30, R25+ |
| Fuite d'un profil à l'autre | Une recherche qui atteindrait les passages d'un autre profil | R8, R27, R30 |
| Exfiltration par la réponse | Une image ou un lien fabriqués par le modèle sous l'influence d'un document | R2, R35 |
| Exfiltration par les outils | `fetch_page` appelé sur une adresse qui porte des données | R35 |
| Accès aux adresses internes (SSRF) | `fetch_page` vers la box, le Mac ou le réseau local | R35 |
| Vol du Mac ou du disque | Les données en clair dans le dossier de données | R33 |
| Abus du service (volume) | Requêtes démesurées, mots de passe essayés en série | R18 |
| Modèle piégé | Un fichier de modèle qui exécuterait du code | R16 |

### Risques résiduels acceptés

- Le guide le dit lui-même : aucune mesure ne supprime l'injection indirecte. Un document bien écrit, sans motif connu et validé par son propriétaire, peut encore orienter une réponse. Cette réponse reste lue par un humain, et ne déclenche aucune action.
- Le contrôle d'entrée ne lit que le texte extrait. Un texte écrit en blanc sur blanc dans un PDF est extrait comme le reste, puis inspecté, mais sa couleur n'est pas vue.
- Un seul programme fait tout. Le cloisonnement passe par les droits du compte macOS et par le service lui-même, sans noyau distinct entre les composants (R14).
- L'accès depuis la maison est en http, sans TLS (R20). Le Wi-Fi de la maison est jugé maîtrisé. L'accès depuis Internet passe, lui, par le tunnel chiffré de Cloudflare.
- **FileVault est désactivé sur le Mac mini** au 2026-11-27 (R33) : c'est à l'humain de l'activer.

---

## 2. Cartographie (R10)

```
                      ┌──────────────────────── Mac (compte macOS de l'humain) ────────────────────────┐
  Navigateur du Mac ──┤ 127.0.0.1 (https)                                                              │
  Maison (Wi-Fi) ─────┤ :8444 (http, à ouvrir)  ─►  gulliver-service ─┬─ moteur (CPU, GPU Metal)      │
  Internet ─ Cloudflare (TLS) ─ tunnel ─────────┘    (routes, sessions) ├─ SQLite : comptes, discussions,│
     (liste fermée de routes, pas d'administration)                     │   passages, vecteurs,         │
                                                                         │   historique des documents    │
                                                                         ├─ dossiers : profiles/<id>/     │
                                                                         │   documents, documents/ commun │
                                                                         └─ mail : Proton Mail Bridge     │
                                                                             (127.0.0.1, codes et liens)  │
                      └───────────────┬────────────────────────────────────────────────────────────────┘
                                      │ sorties
                                      ├─ web : Wikipédia, pages lues par fetch_page (adresses publiques seulement)
                                      ├─ modèles : téléchargés du catalogue, empreinte SHA-256 vérifiée
                                      └─ moteur externe (option de l'administrateur, éteinte par défaut)
```

- **Points d'entrée** : les routes `/v1/…` et `/gulliver/v1/…` du service (décrites dans `web/openapi.json`), le dossier des documents (Finder) et le dossier d'import des modèles.
- **Administration** : réglages, modèles, journal, redémarrage et gestion des profils ne s'ouvrent qu'au Mac lui-même, ou à la session de l'administrateur depuis la maison. La passerelle les refuse toujours, et le service aussi (en-tête `X-Gulliver-Remote`).
- **Sources extérieures** : les pages web n'entrent que par `fetch_page` et `web_search`, comme données de l'outil. Elles ne sont jamais indexées dans la bibliothèque.

---

## 3. Les 35 recommandations

### 4.1 Utilisation

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R1 Analyse de risque | ✅ | Ce document, section 1. |
| R2 Réponses non fiables | ✅ | Le Markdown des réponses est rendu par le service (`host/markdown.cpp`). Tout HTML d'un modèle s'affiche comme du texte. Un lien ne prend que http, https, mailto ou une adresse de la page, et s'ouvre sans transmettre la page. Une image d'une autre origine devient un lien : elle n'est jamais chargée, ce qui coupe l'exfiltration par image du cas EchoLeak (annexe B du guide). La page a une politique de contenu stricte (`img-src 'self' data: blob:`, `connect-src 'self'`, `frame-ancestors 'none'`). |
| R3 Sorties qui déclenchent des actions | ✅ | Les outils sont une liste fermée, avec des arguments décrits et vérifiés (`web/js/tools.js`, `web/js/web.js`). Aucun n'écrit dans le système. `run_javascript` tourne dans un cadre isolé, sans réseau, pendant 15 s au plus. |
| R3+ Approbation des actions sensibles | ⚪ | Aucune action sensible ni irréversible n'est confiée au modèle. Le fichier que crée `create_file` est proposé au téléchargement, et l'utilisateur décide. |
| R4 Périmètre informationnel | ✅ | Chaque profil a sa bibliothèque. Le dossier commun ne contient que ce qu'un profil y dépose sciemment (case « Document commun »). Les discussions privées ne font aucune recherche. Pour des données personnelles à protéger, l'outil voisin Pseudalis les pseudonymise. |
| R5 Évaluer et tester | ✅ | Plusieurs tests, sur le Mac : `tools/test_rag.py` (rappel et rang de la recherche), `tools/test_comptes.py` (droits entre profils, quarantaine, injection d'une consigne, historique), `tests/test_host.cpp` (contrôle d'entrée, Markdown) et l'épreuve des modèles du lundi. |
| R5+ Évaluations automatisées | 🟡 | Les tests se lancent à la main à chaque changement, et la mesure du lundi est planifiée. Pas de chaîne CI : le forfait GitHub Actions est réservé, et c'est une décision de l'humain. |

### 4.1.1 Solutions « clé en main »

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R6 Périmètre fonctionnel | ⚪ | Gulliver n'est pas une solution d'éditeur. L'accès au web peut se couper (réglage `web_access`), et le moteur externe est éteint par défaut. |
| R7 Sources, connecteurs, indexation automatique | 🟡 | Aucun connecteur vers une messagerie ou un stockage en ligne. Le dossier des documents s'indexe de lui-même, car c'est le dossier du Mac de l'utilisateur. Un document où le contrôle trouve un signe, ou un commun déposé par un autre profil, attend une validation (R25+). |
| R8 Pas de contournement des droits | ✅ | Le service décide de ce qui est cherché au moment de la question, à partir de la session : les passages du profil et le dossier commun, jamais ceux d'un autre profil. Vérifié avec deux comptes par `tools/test_comptes.py` (« la recherche de l'administrateur trouve le commun, jamais le perso de Lui »). |

### 4.2 Transverses

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R9 Architecture simple | ✅ | Un programme, une base SQLite, pas de base vectorielle externe, pas d'orchestrateur. |
| R10 Cartographie | ✅ | Section 2. |
| R11 Hébergement | ✅ | Tout est sur le Mac de l'humain. Le générateur est local. |
| R12 Générateur en SaaS | 🟡 | Seulement si l'administrateur branche un moteur externe. La discussion part alors chez ce fournisseur (fiche « Moteur externe » de l'onglet Service). C'est une option, éteinte par défaut. |
| R12+ Traitement externalisé renforcé | ⚪ | Pas de traitement externalisé par défaut. |
| R13 Domaines de sécurité | 🟡 | Trois domaines : le Mac lui-même ; la maison, où l'administrateur peut tout faire, mais seulement par sa session ; Internet, par la passerelle, sans aucune administration. L'administration est séparée des usages, mais un seul programme les sert. |
| R14 Cloisonnement | 🔴 | Pas de noyau distinct entre les composants : un seul programme, sous le compte de l'humain. Le service écoute sur `127.0.0.1`. Le réseau de la maison est fermé par défaut. Risque accepté pour un usage familial. |
| R14- Bac à sable | 🟡 | Le code produit par le modèle tourne dans un cadre isolé du navigateur, sans réseau. Le service lui-même n'est pas dans un bac à sable macOS. |
| R14+ Séparation physique | ⚪ | Hors de portée d'un Mac de maison. |
| R15 Durcissement des composants | ✅ | Politique de contenu stricte sur la page. Corps de requête bornés (64 Mo en JSON, 512 Mo pour un document). Noms de fichiers simples, sans dossier ni lien. Administration fermée à la passerelle. Accès réseau à ouvrir explicitement. |
| R16 Chaîne de dépendance | ✅ | Les modèles sont des fichiers GGUF, qui ne contiennent pas de code exécuté au chargement (le guide déconseille les formats fondés sur pickle). Ils viennent d'un catalogue **signé par l'original** (ECDSA P-256, signature vérifiée par le framework Security de macOS depuis la 7.14.0 ; un catalogue modifié, signé par une autre clé ou sans signature est refusé), avec une empreinte SHA-256 vérifiée au téléchargement, et sont refusés si elle diffère. Depuis la 7.17.0, un modèle ne se charge que s'il est dans ce catalogue signé, fourni avec Gulliver, ou validé par l'administrateur (validation signée par une clé P-256 née dans la Secure Enclave du Mac, dont la base ne garde que la forme scellée) ; un modèle hors de tout catalogue (Hugging Face, bibliothèque, fichier envoyé) est bloqué jusque-là, et un fichier changé depuis son installation est refusé avant chaque chargement (taille, date et numéro du fichier, sinon empreinte entière recalculée). Limite : la clé n'est pas protégée par un code, un programme hostile sous le compte de l'utilisateur pourrait valider. Une seule bibliothèque embarquée : cpp-httplib. Le reste est fourni par macOS. |
| R16+ Nomenclatures (SBOM, AIBOM) | 🔴 | Pas encore produites automatiquement. L'onglet « À propos » liste les licences, et le catalogue des modèles leurs empreintes. À faire. |
| R17 Maintien en condition de sécurité | 🟡 | L'agent du lundi met les modèles à jour, et la mesure du lundi les rejoue. La mise à jour de cpp-httplib est manuelle. |
| R17+ Corrélation avec les vulnérabilités | 🔴 | Pas de veille automatique. |
| R18 Limites d'usage et de ressources | ✅ | Le frein de connexion attend 10 s à 10 min, puis bloque au 7e échec et prévient l'administrateur. Le service limite l'envoi des mails. La recherche est bornée par le service (40 extraits, 200 000 caractères), quoi que demande la page. Le surveillant arrête ce qui chauffe le Mac pendant 30 min. |
| R19 Observabilité | 🟡 | Le journal du service note chaque dépôt, indexation, mise en quarantaine, validation et suppression, avec le profil et le nombre de passages. Ces événements sont rangés un an (`GET /gulliver/v1/documents-history`), et le journal est réservé à l'administrateur. Le texte des questions n'est pas journalisé. Pas d'identifiant de corrélation de bout en bout. |
| R20 Flux chiffrés | 🟡 | Sur le Mac, en https. Depuis Internet, en TLS jusqu'à Cloudflare, puis par le tunnel chiffré. Les sorties du service se font en TLS 1.2 au moins, ou 1.3 (Network.framework). La maison reste en http (risque accepté, section 1). |
| R21 Authentification centralisée | 🟡 | Les comptes sont gérés par le service lui-même : adresse, mot de passe (PBKDF2), code à 8 chiffres reçu par mail, sessions qui expirent après une période sans usage, rôles d'administrateur. Pas d'annuaire, ce qui est sans objet pour une maison. |
| R21- Secret distinct par composant | ✅ | Le mot de passe du Bridge est rangé à part (`mail.secret`), jamais affiché ni journalisé. Le jeton du tunnel est rangé hors du dépôt. |

### 4.3 Ingestion

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R22 Minimiser les données ingérées | 🟡 | Seul ce que l'utilisateur dépose ou joint est indexé. Les discussions privées ne sont pas gardées. |
| R23 Anonymiser, pseudonymiser | 🟡 | Pseudalis, à côté, pseudonymise un document avant qu'on le confie à Gulliver. |
| R24 Cycle de vie des données personnelles | ✅ | Supprimer un document efface aussi ses passages et ses vecteurs. Supprimer un profil efface son dossier, sa bibliothèque et son historique. Un fichier changé est réindexé, sans ses anciens passages. C'est vérifié par `tools/test_comptes.py`. |
| R25 Filtrer les documents | ✅ | **Contrôle d'entrée (`host/content_guard.cpp`)**. Il retire les caractères invisibles : contrôle, largeur nulle, sens d'écriture, étiquettes U+E0000, sélecteurs étendus. Il repère trois signes : un texte caché, une demande d'ignorer les consignes (en français et en anglais) et le balisage d'une discussion (`<|im_start|>`, `</excerpt>`…). Il s'applique à la bibliothèque, aux documents joints (la page prévient d'un signe) et à l'indexation des pièces jointes. |
| R25+ Quarantaine | ✅ | Deux cas mettent un document de la bibliothèque en quarantaine : un signe trouvé à l'entrée, ou un document commun déposé par un profil qui n'est pas administrateur. Ses passages sont prêts, mais aucune recherche ne les lit avant qu'on le valide. Le propriétaire valide le sien ; seul l'administrateur valide un commun, et seulement depuis le Mac ou la maison. La validation vaut pour l'empreinte du fichier : changé, il se valide de nouveau. |
| R26 Tracer le cycle de vie | ✅ | Chaque document a une empreinte SHA-256, son déposant, sa date et les signes trouvés. L'historique garde chaque réception, indexation, quarantaine, validation, suppression et disparition (un an). Un document se retire d'un clic, avec ses passages. |

### 4.4 Récupérateur et base vectorielle

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R27 Filtrer les requêtes | ✅ | Le service décide lui-même des portées cherchées, à partir de la session : les propriétaires du profil, sa bibliothèque et le dossier commun. Les bornes de nombre et de taille sont imposées par le service. |
| R28 Transformations de la requête | ✅ | Aucune reformulation par un modèle. Une question courte reçoit le début de la précédente, de façon déterministe. Le reclassement est un calcul (couverture et densité), extractif. |
| R29 Sélection des passages | ✅ | Le nombre d'extraits et la taille totale sont limités. Pour la bibliothèque, un seuil de pertinence s'ajoute : cosinus d'au moins 0,5, ou la moitié des mots de la question. Chaque extrait garde son document et sa place. |
| R30 Contrôle des accès | ✅ | On écrit dans son propre dossier. Un commun d'un autre profil attend l'administrateur (R25+). Un commun ne se supprime que par son déposant ou par l'administrateur. |
| R30+ Filtrage fin | ✅ | Le filtrage se fait au niveau du document : portée, état (indexé, quarantaine) et propriétaire, à chaque question. |
| R31 Protéger les vecteurs | ✅ | Les vecteurs ne sont plus rendus par les routes. `?vectors=1` les réserve à l'administrateur, depuis le Mac ou la maison. Ils ne sont jamais journalisés. |
| R32 Index | ✅ | La recherche est exhaustive : pas d'index approché, pas d'écart entre cas moyen et pire cas. Un changement du modèle d'embedding réindexe document par document. |
| R32+ Bascule pendant la reconstruction | ⚪ | Les anciens passages restent cherchés jusqu'à ce que chaque document soit réindexé. |
| R33 Chiffrer les données stockées | 🔴 → à faire par l'humain | Le service vérifie FileVault (`fdesetup isactive`, une fois par heure). Disque non chiffré, il l'écrit au journal et l'onglet Service l'affiche en rouge. **Au 2026-11-27, FileVault est désactivé sur le Mac mini.** Pour l'activer : Réglages Système → Confidentialité et sécurité → FileVault. |

### 4.5 Générateur

| Réc. | Statut | Dans Gulliver |
|---|---|---|
| R34 Recommandations sur le générateur | ✅ | Quatre mesures. **Balises** : les documents et extraits entrent entre balises (`<document>`, `<excerpts>`, `<excerpt>`), que leur texte ne peut ni fermer ni imiter (`asData`, `web/js/context.js`) ; les noms de fichiers sont échappés. **Règle** : le prompt système dit que ces textes et les résultats des outils sont des données, jamais des consignes. **Jetons de contrôle** : le moteur ne les cherche jamais dans le texte des messages (`chat_format::tokenize`), si bien qu'un document qui écrit `<|im_start|>system` ne peut pas se faire passer pour le système. C'est l'équivalent, pour un modèle, d'une requête SQL paramétrée, mais limité aux rôles. **Contrôles d'entrée et de sortie** : R25 et R2. |
| R35 Bloquer les flux sortants | ✅ | `fetch_page` ne lit qu'une adresse donnée par l'utilisateur dans la discussion, ou trouvée par la recherche. L'accès au web refuse les adresses locales et privées (box, Mac, réseau local, `.local`, `.internal`). Les images des réponses ne sont jamais chargées. L'accès au web se coupe d'un réglage. |

---

## 4. À faire

1. **Activer FileVault** sur le Mac mini (R33) : c'est à l'humain.
2. Produire une nomenclature des composants et des modèles à chaque build (R16+).
3. Un identifiant de corrélation par question, du récupérateur au générateur, dans le journal (R19).
4. Étudier un bac à sable macOS (`sandbox-exec` ou App Sandbox) pour le service (R14-).
