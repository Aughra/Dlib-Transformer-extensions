# Manuel d'utilisation — Gulliver

*Un assistant de discussion qui tourne sur le Mac lui-même : les modèles, les documents et les conversations restent à la maison.*

Version décrite : **Gulliver 7.25.0**. Ce manuel dit comment se servir de Gulliver ; la façon dont il est construit est dans le [mémo du portage](memo-portage-macos-arm64.md), et son histoire dans le [blog](../blog/README.md).

---

## Table des matières

1. [Vue d'ensemble](#1-vue-densemble)
2. [Lancer et arrêter le service](#2-lancer-et-arrêter-le-service)
3. [Se connecter](#3-se-connecter)
4. [Discuter](#4-discuter)
5. [Joindre des fichiers](#5-joindre-des-fichiers)
6. [La bibliothèque](#6-la-bibliothèque)
7. [Les modèles](#7-les-modèles)
8. [Les paramètres](#8-les-paramètres)
9. [Les profils et l'administration](#9-les-profils-et-ladministration)
10. [Depuis le téléphone, la maison ou en ligne](#10-depuis-le-téléphone-la-maison-ou-en-ligne)
11. [Ce que le Mac fait tout seul](#11-ce-que-le-mac-fait-tout-seul)
12. [Dépannage](#12-dépannage)

---

## 1. Vue d'ensemble

Gulliver est un service qui tourne sur le Mac (Apple Silicon) et une page web pour lui parler, depuis le Mac, un téléphone ou une tablette. Les modèles de langage calculent sur la puce du Mac, de préférence sur son GPU : rien ne part sur Internet, sauf ce que l'on demande exprès (recherche sur le Web, relais vers un modèle extérieur).

| Ce que fait Gulliver | Où le trouver |
|---|---|
| Discuter avec un modèle, garder ses discussions et ses projets | la page, colonne de gauche |
| Lire des documents joints (PDF, Office, texte) et des images | le trombone de la zone de saisie |
| Chercher dans ses documents à chaque question | la bibliothèque (interrupteur « Bibliothèque ») |
| Retenir des faits sur vous d'une discussion à l'autre | Paramètres, onglet « Mémoire » |
| Plusieurs personnes, chacune ses données | les profils (Paramètres, onglet « Profils ») |
| Choisir et télécharger des modèles | Paramètres, onglet « Modèles » |
| Régler le moteur et le service | Paramètres, onglet « Service » |

Chaque profil a ses discussions, ses projets, sa mémoire, ses documents et ses modèles préférés ; un profil ne voit jamais ceux d'un autre.

---

## 2. Lancer et arrêter le service

### Lancer

Depuis la racine du dépôt, sur le Mac :

```bash
GULLIVER_HOME=$PWD/build/gulliver-home PSEUDALIS_HOME=$PWD/build/pseudalis-home \
  build/macos-gulliver/bin/gulliver-service --console
```

`GULLIVER_HOME` est le dossier des données (base, modèles, documents, journal) ; `PSEUDALIS_HOME` sert au relais pseudonymisé (voir la partie 4). Au démarrage, le service charge les modèles choisis, puis la page s'ouvre à l'adresse qu'il donne (sur le Mac de la maison : `https://localhost:18443`).

### Arrêter et redémarrer

- **Arrêter** : `Ctrl-C` dans la console, ou un signal d'arrêt (`kill -TERM`) au processus ; le service finit ses tâches avant de s'arrêter.
- **Le bouton « Appliquer et redémarrer »** (onglet Service) relance le service pour les réglages qui le demandent : il recharge les modèles, **mais pas le programme**.
- **Passer à une nouvelle version** : arrêter le processus, attendre qu'il soit sorti, puis le relancer avec la commande ci-dessus.

Avant d'arrêter, vérifier qu'aucune réponse n'est en train de s'écrire : un arrêt la coupe.

---

## 3. Se connecter

### La connexion

1. Taper son **adresse mail** et son **mot de passe**.
2. Un **code à 8 chiffres** part par mail ; le taper dans la boîte suivante. Il vaut 10 minutes et 5 essais.
3. La session dure 30 jours sur cet appareil ; « Se déconnecter » (onglet Profils) la ferme.

Un bouton en forme d'œil, au bout de chaque champ de mot de passe, montre ou cache ce qu'on tape.

### Première connexion, ou mot de passe oublié

Le bouton « Mot de passe oublié ou première connexion » envoie un lien par mail, valable 30 minutes et une seule fois, pour choisir son mot de passe. Il ne part que vers une adresse de profil connue (sans rien dire de plus à une adresse inconnue).

Un mot de passe doit avoir **au moins 20 caractères** (2 048 au plus), avec au moins une minuscule, une majuscule, un chiffre et un caractère spécial. La règle s'affiche et se coche au fil de la frappe.

### Le frein des mauvais mots de passe

Après un mauvais mot de passe, **la page n'affiche rien** et garde ce qui est tapé. Le prochain essai n'est permis qu'après 10 secondes, puis 30 s, 1 min, 3 min, 9 min et 10 min après les échecs suivants. Au **septième échec**, la connexion du profil est bloquée et l'administrateur reçoit un mail. Pour la débloquer : demander le lien « Mot de passe oublié », ou qu'un administrateur choisisse un nouveau mot de passe.

---

## 4. Discuter

### La colonne de gauche

- **Nouvelle discussion**, et **Démarrer une discussion privée** (l'icône de masque) : une discussion privée n'est gardée nulle part, et ses pièces jointes ne sont pas copiées sur le Mac.
- **Projets** : un projet regroupe des discussions et ses propres documents et images.
- **Rechercher dans les discussions**, **Épingler**, **Renommer**, **Supprimer** (menu « … » d'une discussion).
- Sous le nom Gulliver, en haut à gauche : la **version** du service.

### La zone de saisie

| Bouton | Ce qu'il fait |
|---|---|
| Trombone | Joindre des fichiers, depuis l'appareil ou depuis les documents du Mac (partie 5) |
| Raisonnement approfondi | Laisse le modèle réfléchir avant de répondre (pour les modèles qui savent le faire) |
| Agent | Laisse le modèle se servir de ses outils (lire un document partie par partie, calculer…) |
| Web | Laisse le modèle chercher sur le Web et lire des pages (si le service y a accès) |
| Bibliothèque | Cherche aussi dans vos documents du Mac et dans les documents communs (partie 6) ; allumé par défaut |
| Cadenas | Le relais : la question part vers un modèle extérieur réglé par l'administrateur, les valeurs personnelles pseudonymisées par Pseudalis ; éteint, tout reste sur ce Mac |

Chaque choix vaut pour sa discussion. Le relais éteint d'office la bibliothèque : aucun document ne quitte le Mac sans avoir été joint exprès.

### Les réponses

Les réponses sont mises en forme (titres, listes, tableaux, code coloré) par le service. Sous une réponse : copier, **Régénérer**, télécharger, et le modèle employé avec ses vitesses de lecture et d'écriture. Une question peut être modifiée (« Modifier ») et renvoyée.

---

## 5. Joindre des fichiers

- **Depuis cet appareil** : le fichier est lu, et une copie va dans **vos** documents du Mac (sauf en discussion privée), où la bibliothèque le retrouvera ensuite.
- **Documents du Mac** : la liste de vos documents et des documents communs (marqués « commun »), avec leur état d'indexation. On y coche ce qu'on joint, on y envoie des fichiers (case « Document commun » pour les partager avec tous les profils), on y supprime un document.

Formats lus : PDF (par PDFKit, le cadriciel de macOS), Word, Excel, PowerPoint et OpenDocument, texte, Markdown, CSV, JSON, HTML ; images pour les modèles qui les voient.

- Un **PDF protégé** (par un mot de passe à l'ouverture, ou contre la copie et l'impression) est refusé : retirer la protection depuis Aperçu (Fichier → Exporter au format PDF, sans chiffrement), puis le joindre de nouveau.
- Un **PDF scanné** (des images de pages, sans texte) est proposé comme images de ses pages à un modèle qui voit les images.
- Un grand document n'est pas lu en entier : ses passages les plus proches de la question sont cherchés et joints.
- Les caractères invisibles d'un document sont retirés à sa lecture. Si le document semble contenir des consignes cachées pour le modèle (texte invisible, phrase « ignore les consignes précédentes », balises de discussion), un message le dit : il reste joint, lu comme une donnée, mais vérifiez d'où il vient.

---

## 6. La bibliothèque

Chaque profil a son dossier de documents sur le Mac, à côté d'un **dossier commun** à tous. Le service indexe lui-même chaque document posé (texte découpé en passages, vecteurs du modèle de recherche) ; il repasse chaque minute, pour les fichiers déposés directement par le Finder ou changés depuis.

À chaque question, si l'interrupteur « Bibliothèque » est allumé, Gulliver cherche dans vos documents et dans les documents communs. Un passage n'est joint que s'il est vraiment proche de la question, par le sens ou par les mots : une question sans rapport ne reçoit rien.

| Action | Qui peut |
|---|---|
| Voir et chercher dans ses documents | le profil seul |
| Voir et chercher dans les documents communs | tous les profils |
| Supprimer un document commun | l'administrateur, ou le profil qui l'a déposé |
| Valider un de ses documents en quarantaine | le profil |
| Valider un document commun en quarantaine | l'administrateur, depuis le Mac ou la maison |

**La quarantaine.** Deux sortes de documents ne sont pas cherchées tant qu'elles n'ont pas été validées. Leur état dit « en quarantaine », avec la raison :

- un document où Gulliver a trouvé un signe de consignes cachées (texte invisible, phrase qui demande d'ignorer les consignes, balises de discussion) ;
- un document commun déposé par un profil qui n'est pas administrateur, puisque tous les profils le liraient.

Le bouton ✓ « Valider le document » le libère, après confirmation : ne le valider que si l'on connaît sa provenance. Une validation vaut pour ce fichier-là : s'il change, il repasse en quarantaine. Chaque document garde son empreinte et son historique (reçu, indexé, en quarantaine, validé, supprimé), pendant un an.

Un PDF scanné n'est pas indexé (il se lit en le joignant). La suppression d'un profil efface aussi ses documents.

---

## 7. Les modèles

### Pour l'administrateur : l'onglet « Modèles »

Les modèles installés, leurs fonctions (discussion, code, cybersécurité, et le modèle de recherche dans les documents), le catalogue à télécharger, et le **conseil de la semaine** : chaque lundi, le Mac mesure tous ses modèles et conseille le meilleur, qu'on applique d'un clic (bouton « Choisir »). Les modèles retirés du classement se remettent par le menu « Modèles retirés ».

**Le catalogue est signé.** Le catalogue vient de l'original, qui le signe ; Gulliver vérifie cette signature à chaque mise à jour et refuse un catalogue modifié, signé par quelqu'un d'autre ou sans signature : il garde alors le précédent. La ligne « Catalogue mis à jour » de l'onglet dit « (signé par l'original) » ; un catalogue installé avant la 7.14.0 affiche « signature non vérifiée » jusqu'à la prochaine mise à jour (bouton « Rafraîchir »). Un paquet hors ligne (clé USB, disque) doit contenir `catalog.json` **et** `catalog.json.sig`, sans quoi rien n'est installé.

**Chaque modèle est signé, ou validé par vous** (depuis la 7.17.0). La carte de chaque modèle installé dit d'où vient la confiance (🔏) :

| Mention | Ce que cela veut dire |
|---|---|
| Signé : dans le catalogue signé de l'original | Son empreinte figure dans le catalogue signé |
| Fourni avec Gulliver | Livré avec le programme |
| Validé par l'administrateur | Vous l'avez validé ; la validation est signée par la puce de sécurité du Mac |
| Non signé (encadré rouge) | Ni dans le catalogue signé, ni validé : **il ne se charge pas** |

Un modèle venu de Hugging Face, de la bibliothèque du Mac ou d'un fichier envoyé arrive **non signé**. Pour vous en servir : **« Valider ce modèle »**, puis confirmer. Ne validez qu'un modèle dont vous connaissez la provenance. Gulliver relit ses fichiers en entier (quelques secondes pour un gros modèle) et signe leur empreinte avec une clé gardée dans la Secure Enclave du Mac ; s'il était choisi pour une fonction, il se charge aussitôt. Le bouton « Choisir » reste grisé tant qu'un modèle n'est pas validé. « Retirer la validation » le rend de nouveau non signé.

Si un fichier de modèle change après son installation (fichier remplacé, abîmé), Gulliver le voit avant de le charger et refuse de s'en servir : le journal du service le dit. Les modèles téléchargés par la veille du lundi depuis Hugging Face arrivent non signés, comme les autres : à valider avant de les choisir.

Le modèle de recherche dans les documents n'a pas de carte dans l'onglet : s'il n'est pas dans le catalogue signé, il se valide depuis le Mac par `curl -sk -X POST https://localhost:18443/gulliver/v1/models/<identifiant>/approve`.

Limite à connaître : la clé de validation n'est pas protégée par un code (le service tourne sans écran). Elle empêche qu'une copie de la base serve sur une autre machine et qu'un fichier modifié se charge, mais pas qu'un programme hostile déjà installé sous votre compte valide un modèle.

### Pour chacun : « Mes modèles »

Dans l'onglet Profils, chaque profil peut choisir ses propres modèles de discussion (discussion, code, cybersécurité) parmi ceux installés, à la place du modèle commun. Le service charge le modèle choisi sans changer celui des autres ; un modèle choisi prend de la mémoire du Mac tant qu'un profil s'en sert. Le modèle de recherche dans les documents reste commun à tous.

---

## 8. Les paramètres

Le bouton ⚙ ouvre les paramètres, en onglets :

| Onglet | Contenu |
|---|---|
| Général | Thème, votre nom, envoi par la touche Entrée, consigne donnée au modèle |
| Modèles | (administrateur) modèles, catalogue, conseil de la semaine |
| Génération | Longueur des réponses, température, top-k, top-p |
| Mémoire | Ce que Gulliver retient de vous, à lire et à corriger |
| Données | Tout exporter, importer |
| Profils | Votre compte, votre mot de passe, vos modèles ; pour l'administrateur, la gestion des profils |
| Service | (administrateur) les réglages du service et du moteur |
| À propos | Version, composants, licences, **notes de version** |

### L'onglet « Service »

Si FileVault est désactivé sur le Mac, l'onglet le dit en rouge en tête : les documents, les discussions et leurs passages sont alors rangés en clair sur le disque. Il s'active dans Réglages Système → Confidentialité et sécurité → FileVault.

Chaque réglage (moteur, GPU, contexte, mémoire de conversation, réseau, Internet, mail des comptes, relais…) a son effet en une ligne et, derrière son point d'interrogation, une fiche : principe, avantages, inconvénients, schéma, puis trois explications, celle du **hibou** 🦉 (très technique), de la **marmotte** 🦫 (simple) et de l'**abeille** 🐝 (ce qui arrive si on pousse trop ou qu'on s'en passe). Certains réglages s'appliquent tout de suite ; les autres attendent « Appliquer et redémarrer ». Le bouton « Revenir aux réglages conseillés » remet les valeurs mesurées comme les meilleures.

---

## 9. Les profils et l'administration

- Le **premier profil** est l'**administrateur principal**. Il crée les profils (nom et adresse ; la personne choisit son mot de passe par le lien du mail), les renomme, change leur adresse ou leur mot de passe, et les supprime avec toutes leurs données.
- Il peut nommer un **administrateur de secours** (un profil qui a une adresse) : celui-ci gère les profils, les modèles et les réglages, mais ne peut ni changer le nom, l'adresse ou le mot de passe de l'administrateur principal, ni lui retirer son rôle.
- Depuis le réseau de la maison, seuls les administrateurs changent les modèles, les réglages et le journal du service ; depuis le Mac lui-même, ces actions restent libres.

Pour les essais, une rangée « Passer à … » de l'onglet Profils ouvre directement un autre profil, sans mot de passe : c'est une fonction **temporaire**, réservée aux administrateurs, qui sera retirée.

---

## 10. Depuis le téléphone, la maison ou en ligne

| D'où | Comment | Ce qui est permis |
|---|---|---|
| Le Mac | l'adresse donnée au démarrage | tout |
| Le réseau de la maison | `http://<adresse du Mac>:8444/`, une fois l'accès réseau ouvert dans l'onglet Service | tout, avec un compte ; les réglages, les modèles et le journal aux administrateurs |
| En ligne | la discussion en ligne, par la passerelle Cloudflare, derrière Cloudflare Access | discuter et ses données ; ni réglages, ni modèles, ni gestion des profils, ni choix de modèle |

La page s'adapte au téléphone. Les **documents du portage** se lisent dans le carnet (`/carnet/`, menu ☰ « Documents du portage »).

---

## 11. Ce que le Mac fait tout seul

| Quand | Quoi |
|---|---|
| Le lundi à 1 h | L'agent des modèles tient le catalogue à jour, télécharge les nouveaux modèles qui tiennent dans le Mac, et écrit à l'administrateur ce qu'il a trouvé |
| Le lundi à 8 h | La mesure de la semaine : chaque modèle mesuré (vitesse, justesse, une épreuve de 40 questions en français, 32 sondes de cybersécurité), le conseil écrit, le dernier du classement retiré, un mail à l'administrateur |
| En continu | Le surveillant : un programme qui garde le Mac en souci (processeur à 80 % ou puce à 95 °C pendant 30 minutes) est arrêté, avec un mail |
| Chaque minute | La bibliothèque repasse sur les dossiers de documents |

Les mails partent par Proton Mail Bridge, sur le Mac, avec les réglages de la rubrique « Mail des comptes » de l'onglet Service.

---

## 12. Dépannage

| Ce qu'on voit | Ce qu'il faut faire |
|---|---|
| « Le service Gulliver ne répond pas » | Le service est arrêté ou redémarre : attendre, ou le relancer (partie 2). En attendant, la page passe en mode mémoire : rien n'est gardé |
| Le code de connexion n'arrive pas | Proton Mail Bridge doit tourner sur le Mac ; vérifier la rubrique « Mail des comptes » de l'onglet Service (depuis le Mac, sans session) |
| Rien ne se passe après le mot de passe | Le mot de passe est faux, ou le frein attend (partie 3) : patienter, ou demander le lien « Mot de passe oublié » |
| « Ce profil n'a pas encore de mot de passe » | Demander le lien « Mot de passe oublié ou première connexion » |
| Un modèle répond lentement | Il tourne peut-être sur le processeur : la ligne d'état, en bas de la colonne, dit « GPU » ou « processeur » ; le journal du service dit pourquoi |
| Un document n'est pas trouvé par la bibliothèque | Ouvrir « Documents du Mac » : son état dit « indexé », « en quarantaine » (à valider, partie 6), « indexation en cours », « sans texte à chercher » ou la raison d'un échec |
| « … est protégé (mot de passe…) » sur un PDF | Retirer la protection depuis Aperçu (partie 5) |
| Une nouvelle version n'apparaît pas | « Appliquer et redémarrer » ne relit pas le programme : arrêter et relancer le service (partie 2) |

Le journal du service est dans `<GULLIVER_HOME>/logs/service.log` ; l'administrateur le lit aussi depuis la page.
