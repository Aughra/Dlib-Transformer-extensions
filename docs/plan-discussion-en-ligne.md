# Plan : la discussion de Gulliver en ligne, le calcul sur le Mac

Demande de l'humain (2026-11-20) : la page de discussion publiée comme `dev.tortuetech.com` (des fichiers servis par Cloudflare Pages, derrière le login GitHub de Cloudflare Access), **le Mac ne faisant que le calcul**, sans Tailscale. **Étapes 1, 2 et 4 préparées le 2026-11-20 (Gulliver 6.9.0)**, choix de l'humain : dossier `cloudflare/` (passerelle, assemblage, tunnel, [mode d'emploi](../cloudflare/README.md)), essayées sur le Mac avec `wrangler pages dev` ; reste ce qui se fait dans le compte Cloudflare (tunnel, jeton de service, deux applications Access, projet Pages). L'étape 3 (les données chez Cloudflare) est à rediscuter.

## Ce qu'on a relevé dans le code

- La page appelle le service en chemins relatifs (`/gulliver/v1/...`, `/v1/...`) ; le service n'envoie aucun en-tête CORS et n'a ni mot de passe ni contrôle d'origine (en tête de `host/server.cpp`) : qui l'atteint peut tout faire.
- Le service écoute sur `127.0.0.1:18443` en HTTPS (certificat maison) ; l'accès réseau de la maison (http, 8444) est à part.
- Les réponses en flux envoient un signal d'attente (`: keep-alive`, `host/server.cpp:916`), utile pour Cloudflare, qui coupe une connexion muette au bout de 100 s.
- Les 60 routes se rangent en trois familles :

| Famille | Routes | Où elles tournent dans le plan |
|---|---|---|
| **Calcul** | `/v1/chat/completions`, `/v1/completions`, `/v1/embeddings`, `/v1/models`, `count`, `extract` (lecture des PDF et documents), `profile/search` et `profile/documents/{doc}/index` (recherche documentaire), `web/fetch`, `web/search`, `status`, `speed`, `usage`, `pseudalis` | le Mac |
| **Données** | `profiles`, `profile/conversations`, `profile/projects`, `profile/page-settings`, `profile/images`, `profile/chunks`, `documents`, `settings/profiles` | le Mac (étape 1) ; Cloudflare si l'étape 3 est choisie |
| **Administration** | `models/*` (télécharger, envoyer, importer, supprimer, activer), `hub/*`, `library/*`, `import`, `reserve`, `settings` (écriture), `restart`, `logs`, `engines/benchmark`, `relay`, `relay/test` | **jamais par Internet** : depuis le Mac (ou le réseau de la maison) seulement |

## Le schéma

```
navigateur (téléphone, ailleurs)
   │  https://gulliver.tortuetech.com        ← Cloudflare Access : login GitHub (ton équipe tortueteam)
   ▼
Cloudflare Pages
   ├─ les fichiers de la page (web/ de Gulliver), servis par Pages
   └─ une fonction Pages sur /gulliver/v1/* et /v1/* : la « passerelle »
         • laisse passer le calcul et les données, refuse l'administration (403)
         • ajoute le jeton de service Access, garde le flux des réponses
   │  https://mac-gulliver.tortuetech.com    ← Cloudflare Access : jeton de service seulement
   ▼
Cloudflare Tunnel (cloudflared sur le Mac, connexion sortante : aucun port ouvert sur la box)
   ▼
Gulliver sur le Mac, https://localhost:18443 : le calcul (et, à l'étape 1, les données)
```

Image : la page est la vitrine en ville (Pages), la passerelle est le guichet qui ne transmet que les commandes de cuisine, le tunnel est le monte-plats vers la cuisine du Mac, qui ne fait que cuisiner.

Pourquoi une passerelle sur la même adresse que la page : la page garde ses chemins relatifs (aucun changement de ses appels), il n'y a pas de CORS, une seule connexion GitHub suffit, et le service n'est jamais joignable directement (son adresse de tunnel n'accepte que le jeton de service).

## Les étapes

| Étape | Quoi | Où | Vérification |
|---|---|---|---|
| 1. Le tunnel | `cloudflared` installé sur le Mac (Homebrew), lancé au démarrage (LaunchAgent) ; une entrée `mac-gulliver.tortuetech.com → https://localhost:18443` (`noTLSVerify`, le seul saut non vérifié étant local) ; application Access sur cette adresse, règle « Service Auth » avec un jeton de service | Mac, tableau de bord Cloudflare (par toi : je n'ai pas accès au compte) | `curl` sans jeton : refusé ; avec le jeton : `/gulliver/v1/status` répond |
| 2. La passerelle et la page | Un dossier `cloudflare/` dans le dépôt : un script qui assemble la page (`web/`) et la fonction `functions/[[path]].js` (liste des routes permises, transfert en flux, jeton lu dans les secrets de Pages) ; projet Pages relié au dépôt GitHub, comme ton site ; application Access GitHub sur `gulliver.tortuetech.com` | dépôt, Pages | une discussion complète depuis le téléphone hors de la maison ; une route d'administration refusée (403) ; la réponse arrive en flux |
| 3. (au choix) Les données chez Cloudflare | Conversations, projets, réglages de page dans D1 ; images et documents dans R2 ; la passerelle répond elle-même à `profile/*` ; le profil pourrait devenir ton compte GitHub (Access le donne) | Pages Functions, D1, R2 | les données migrées et relues ; le Mac ne garde que le calcul et l'index de recherche |
| 4. La page sait où elle est | Servie par Pages, elle cache les onglets d'administration (Modèles, Service…) ; la passerelle le lui dit par un en-tête | `web/js`, passerelle | banc de la page (`page_test.sh`) au vert, onglets cachés hors du Mac |

## Les choix à faire, avec leurs pour et contre

**Les données : étape 1 seule, ou étape 3 aussi ?**

| | Données sur le Mac (étapes 1, 2, 4) | Données chez Cloudflare (étape 3 en plus) |
|---|---|---|
| Ce que fait le Mac | le calcul **et** le rangement des discussions (sa base `gulliver.db`) | le calcul seulement, comme demandé à la lettre |
| Travail | petit : aucune route à récrire | gros : une vingtaine de routes récrites en JavaScript, migration de la base, deux moteurs de stockage |
| Vie privée | les conversations traversent Cloudflare (en clair dans la passerelle), mais n'y restent pas | elles **restent** chez Cloudflare |
| Mac éteint | plus rien, même pas l'historique | l'historique se lit, on ne peut pas discuter |

**Recommandation : étapes 1, 2 et 4 d'abord, l'étape 3 plus tard si le besoin se confirme.** Le rangement des discussions ne coûte presque rien au Mac (de petites écritures SQLite) ; le déplacer chez Cloudflare demande beaucoup de travail et y met tes conversations à demeure.

## Ce qui reste vrai quel que soit le choix

- **Cloudflare voit le contenu** : Access et la passerelle déchiffrent le trafic pour le filtrer. C'est le prix d'une adresse web sans Tailscale ; Gulliver reste local pour le calcul, pas pour le transit.
- **Le Mac doit rester allumé** (le Mac mini n'a pas de mise en veille gênante, à vérifier dans Réglages, Énergie).
- **Limites de Cloudflare** : 100 Mo par requête (un document très gros ou un modèle ne passent pas : l'envoi de modèles reste de l'administration, donc au Mac) ; une réponse sans aucun octet pendant 100 s est coupée (le signal d'attente du flux la couvre ; à vérifier sur la lecture d'un très long document, où le premier mot peut tarder).
- **La sécurité tient à deux règles Access et à la liste de la passerelle** : une erreur dans l'une ouvre ton Mac. La passerelle refuse par défaut ce qu'elle ne connaît pas.
- Ce qui se fait de ton côté, dans le compte Cloudflare (je n'y ai pas accès) : créer le tunnel et récupérer son jeton, le jeton de service, les deux applications Access, le projet Pages. Je prépare tout le reste et les commandes exactes.
