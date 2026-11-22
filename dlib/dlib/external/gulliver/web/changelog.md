# Notes de version

Gulliver, le service de chat local pour macOS sur puce Apple, dérivé d'Albireo Community Edition (Cydral Technology) et maintenu par papy cyber. Les numéros suivent la gestion sémantique des versions : majeur pour une rupture, mineur pour une fonction nouvelle, correctif pour une correction. Les versions sont présentées de la plus récente à la plus ancienne. (Les sections antérieures à la 6.0.0 nomment le service « Albireo », son nom avant le renommage.)

## 6.28.1 — 22 novembre 2026

### Outils de mesure

- La mesure de la semaine relève toutes les 20 secondes la température de la puce, l'utilisation du processeur et celle du GPU, et en donne le minimum, la moyenne et le maximum, dans la page « Mesures de la semaine » comme dans le document de vitesse. L'outil `macos/temperature/` lit aussi la charge du processeur (sur une seconde) et du GPU, sans droits d'administrateur.

---

## 6.28.0 — 22 novembre 2026

### Performances

- Les modèles dont les têtes de requête se regroupent autrement que par 1, 2, 4 ou 8 autour de chaque tête de clés (Qwen2.5 et Qwen2.5-Coder : 6 ou 7) passent sur le GPU, qui les laissait au processeur. Qwen2.5-Coder 1,5B : lecture d'une question de 512 tokens de 510 à 5 598 tokens/s, écriture de 112 à 179 ; justesse identique au processeur (perplexité 12,5959 contre 12,5970).

---

## 6.27.0 — 22 novembre 2026

### Performances

- Les modèles Mistral 3 (Ministral 3 8B, Ministral 3 3B, Devstral) passent sur le GPU : la « température » de leur attention (les requêtes multipliées par un facteur qui croît avec la position) est appliquée par le GPU, qui les laissait jusqu'ici au processeur. Ministral 3 8B : lecture d'une question de 512 tokens de 86 à 1 444 tokens/s, écriture de 38 à 51 (llama.cpp : 1 280 et 49) ; justesse identique au processeur (perplexité 5,8183 contre 5,8187 sur 16 384 tokens, là où la température agit), et la puce ne chauffe plus à plein.

---

## 6.26.0 — 22 novembre 2026

### Outils de mesure

- La mesure de la semaine (`tools/bench_modeles.sh`, nouvel outil) mesure Gulliver sur tous ses modèles installés, et llama.cpp et MLX sur le modèle de référence comme deux lignes de plus du même tableau : lecture et écriture à 512 et 4 096 tokens, mémoire de conversation en 8 bits et en demi-précision, perplexité ; chaque modèle comparé au choix de la semaine précédente. Le choix précédent passe en premier, puis les nouveaux modèles de la nuit. La température de la puce est relevée toutes les 20 secondes (moyenne et maximum).
- Créneaux, hors des heures où l'utilisateur se sert du Mac : mise à jour des modèles le lundi à 1 h (`macos/modeles-semaine/`, `tools/modeles_semaine.py`, qui remplace le passage du mardi), mesure le lundi à 8 h avec arrêt à 19 h au plus tard ; jamais le samedi ni le dimanche entre 9 h et 21 h.
- Chaque mesure réécrit la section « La mesure de la semaine » du document de vitesse du carnet (Gulliver seul, sur les modèles mesurés, et la température moyenne), et la page « Mesures du dimanche » devient « Mesures de la semaine ».
- L'Albireo d'origine n'est plus mesuré (`macos/albireo-original/` retiré) ; les comparaisons sur six modèles fixes avec llama.cpp et MLX restent disponibles à la main (`tools/bench_moteurs.sh`, `tools/justesse_moteurs.sh`).

### Modèles

- La carte du conseil s'appelle « Conseil de la mesure de la semaine ».

---

## 6.25.0 — 22 novembre 2026

### Modèles

- L'onglet « Modèles » affiche le conseil de la dernière mesure du dimanche : le modèle pour tout faire (images comprises), le plus juste et le plus rapide, parmi les modèles installés, avec leurs mesures. Le bouton « Choisir » montre les changements, active le modèle pour la discussion, le code et la sécurité, met les réglages qui lui vont le mieux (mémoire de conversation en 8 bits ou en demi-précision selon les mesures, brouillons) et redémarre le service s'il le faut (`GET /gulliver/v1/models/advice`) ; une fenêtre récapitule ensuite ce qui a changé, avant et après (modèle, mémoire de conversation, brouillons).

### Outils de mesure

- La mesure du dimanche (`tools/bench_moteurs.sh`, `tools/justesse_moteurs.sh`) mesure aussi tous les modèles installés dans Gulliver (`tools/modeles_installes.py`, sans MLX ni l'Albireo d'origine), et écrit son conseil dans `build/bench/hebdo/conseil.json` (`tools/conseil_modeles.py`). Le modèle d'essai Qwen3-VL 4B Thinking n'y porte plus la mention « service ».
- Chaque mardi à 4 h, un agent du Mac (`macos/modeles-mardi/`, `tools/modeles_mardi.py`) rafraîchit le catalogue, retélécharge les modèles dont une nouvelle version est parue (dans le catalogue ou sur Hugging Face, empreinte comparée sans télécharger le fichier) et télécharge les modèles du catalogue qui tiennent dans le Mac (architecture lue par le moteur, au plus les trois quarts de la mémoire), pour que la mesure du dimanche les compare tous.

---

## 6.24.0 — 22 novembre 2026

### Accès et documents

- La ligne du débit, sous la zone de saisie, montre aussi la température de la puce du Mac (la sonde la plus chaude, la moyenne au survol), relue toutes les deux secondes ; la case passe en orange quand macOS juge la machine assez chaude, en rouge quand il commence à freiner. `GET /gulliver/v1/speed` rend `thermal_state`, `chip_temp_max` et `chip_temp_mean`.
- Un clic sur cette case ouvre une nouvelle page, « Température de la puce » (`/temperature/`) : la courbe de la sonde la plus chaude et de la moyenne des sondes sur 1 heure, 6 heures ou 24 heures, mise à jour chaque minute. Le service relève la température toutes les cinq minutes depuis son démarrage et garde les dernières 24 heures (`GET /gulliver/v1/thermal/history`, permise aussi par la discussion en ligne). Dans une colonne étroite, les cases du débit et de la température se resserrent (unités et mot « Puce » effacés).

---

## 6.23.3 — 22 novembre 2026

### Outils de mesure

- Les campagnes de mesure (`tools/bench_moteurs.sh`, et donc la mesure du dimanche) notent la température de la puce au début et à la fin, lue par le nouvel outil `macos/temperature/` (état thermique de macOS et sondes de la puce, sans droits d'administrateur).

---

## 6.23.2 — 22 novembre 2026

### Performances

- La réunion des parties de l'attention de l'écriture calcule le poids de chaque partie une seule fois par tête au lieu d'une fois par valeur : écriture après 16 384 tokens de Gemma 3 4B +4 à +5 %, du modèle du service +2 % ; justesse inchangée (perplexité 8,6964 sur deux morceaux de 4 096 tokens, contre 8,6962).

---

## 6.23.1 — 22 novembre 2026

### Performances

- Les produits par expert de l'écriture (modèles à mélange d'experts, formats Q4_K et Q6_K) prennent le même calcul que la 6.23.0 : Qwen3-30B-A3B de 76,2 à 83,8 tokens/s (+10 %), LFM2.5-8B-A1B de 180,8 à 190,4 (+5 %) ; justesse inchangée (perplexité 10,9446 contre 10,9541).

### Corrections

- `test_metal` contrôle aussi les produits par expert d'une étape d'écriture, dans les cinq formats ; ils ne l'étaient nulle part.

---

## 6.23.0 — 22 novembre 2026

### Performances

- Les produits par les poids de l'écriture d'une réponse (formats Q4_K et Q6_K) reprennent l'organisation des noyaux de llama.cpp (licence MIT) : activations chargées une fois par bloc et quarts d'octet lus par masques, sans décalage. Écriture sur le GPU, Mac mini M5 Pro, après 512 tokens : modèle du service de 82,9 à 89,2-89,3 tokens/s (+7,7 %), Qwen3-VL 2B de 167 à 175 (+4,8 %), Gemma 3 4B de 80,2 à 86,3 (+7,6 %), Qwen3-30B-A3B +1,3 % ; après 4 096 tokens, modèle du service de 69,4 à 73,3. Justesse inchangée (perplexité 15,3689 contre 15,3676 sur le modèle du service).

### Accès et documents

- Chaque mesure du dimanche se termine par un conseil sur le modèle à employer (`tools/conseil_modeles.py`, tiré des tableaux de la semaine : le modèle du service, le plus juste de la famille Qwen3, le plus rapide), et la rubrique « Tests de vitesse » du carnet gagne une conclusion « Quel modèle employer avec Gulliver ».

---

## 6.22.2 — 22 novembre 2026

### Performances

- La norme par tête des requêtes et des clés et leurs positions rotatives (Qwen3, Gemma 3) se font en un seul noyau au lieu de quatre par couche : écriture de 1 à 2 % plus rapide (modèle du service 81,7-82,0 à 82,7-82,8 tokens/s, Qwen3-VL 2B 164,5-165,4 à 166,9-167,6, Gemma 3 4B 79,3-79,4 à 80,2-80,3) ; résultats identiques au bit près.

### Corrections

- `test_metal` échoue (code 1) quand les noyaux Metal ou ceux des accélérateurs du M5 ne compilent pas ; il répondait « skipped » avec le code 0.

---

## 6.22.1 — 22 novembre 2026

### Corrections

- Les modèles à mélange d'experts restent sur le GPU avec la mémoire de conversation en demi-précision : le contrôle au chargement leur appliquait la limite des modèles ordinaires, et une simple quasi-égalité entre deux experts renvoyait tout le calcul au processeur. Qwen3-30B-A3B, clés et valeurs en demi-précision : lecture de 512 tokens de 119 à 1 208 tokens/s, écriture de 45 à 75 ; justesse vérifiée (perplexité 10,9015, contre 10,902 sur le processeur). Le réglage du service (8 bits) n'était pas concerné.

---

## 6.22.0 — 21 novembre 2026

### Performances

- Sur les Mac à puce M5, les passes de plusieurs tokens (l'écriture de plusieurs réponses à la fois, la vérification des brouillons) passent par les accélérateurs neuronaux : chaque poids est développé une seule fois pour tous les tokens. Mac mini M5 Pro, modèle du service : écriture de 4 réponses à la fois de 123 à 239 tokens/s au total, de 8 réponses de 130 à 465 ; l'écriture d'une seule réponse est inchangée ; justesse inchangée.
- Brouillons de l'écriture (décodage spéculatif, repris de l'original) : le moteur propose la suite des derniers mots écrits quand elle figure déjà dans la conversation, et le modèle la vérifie en une passe. Modèle du service : recopie d'un texte de 78,5 à 245 tokens/s, réponse à une question sur un document +26 %, code +9 %, résumé +8 %, réponse libre +1 %. Le texte reste celui que le modèle choisit, aux arrondis de calcul près.

### Réglages

- Nouveau réglage « Brouillons de l'écriture » (`draft_tokens`, de 0 à 7 mots, 7 par défaut) dans l'onglet « Service ».

### Outils de mesure

- `perplexity_runtime` accepte le mode `stepsN`, qui donne la seconde moitié de chaque morceau N tokens à la fois.

---

## 6.21.1 — 21 novembre 2026

### Performances

- Le produit groupé des experts sur les accélérateurs avance de 64 colonnes à la fois au lieu de 32 (deux fois moins d'attentes entre les fils) : Qwen3-30B-A3B lit 2 à 3 % plus vite (1 193 tokens/s à 512 tokens, 1 386-1 432 à 4 096).

---

## 6.21.0 — 21 novembre 2026

### Performances

- Sur les Mac à puce M5, le produit groupé des experts de la lecture des questions (modèles à mélange d'experts) passe par les accélérateurs neuronaux, par tuiles de 64 tokens au lieu de 16. Qwen3-30B-A3B, Mac mini M5 Pro : lecture de 512 tokens de 835-838 à 1 135-1 149 tokens/s, de 4 096 tokens de 851-853 à 1 342-1 371 ; justesse inchangée.

---

## 6.20.0 — 21 novembre 2026

### Performances

- L'attention de la lecture sur les accélérateurs du M5 prend aussi les têtes de 256 valeurs (Gemma 3, couches à fenêtre de Gemma 4). Gemma 3 4B, Mac mini M5 Pro : lecture d'une question de 4 096 tokens de 1 670 à 2 775 tokens/s (+66 %, au niveau de llama.cpp), de 16 384 tokens de 1 102-1 116 à 2 240-2 372 ; justesse inchangée.

### Outils de mesure

- `GULLIVER_GPU_PROFILE=N` donne le tableau des noyaux toutes les N passes (une seule passe pour une lecture).

---

## 6.19.3 — 21 novembre 2026

### Performances

- La nouvelle attention de l'écriture prend aussi les têtes de 256 valeurs (Gemma 3) : Gemma 3 4B écrit 10 % plus vite après 512 tokens (70,6 à 77,5), 5 à 10 % après 4 096 et 16 384 ; justesse inchangée.

---

## 6.19.2 — 21 novembre 2026

### Performances

- Les produits par les poids de l'écriture d'une réponse calculent une ligne par groupe de 32 fils au lieu de quatre, ce qui répartit mieux le travail sur le GPU : écriture +5 % sur le modèle du service (77,9 à 81-82 tokens/s après 512 tokens), +5,5 % pour Qwen3-VL 2B, +6 % pour Qwen3-30B-A3B. L'écriture de plusieurs réponses à la fois garde l'ancien découpage, plus rapide pour elle.

---

## 6.19.1 — 21 novembre 2026

### Corrections

- Le journal du moteur dit si les accélérateurs neuronaux du M5 sont employés (« neural accelerators (applegpu_g17s) »), ou pourquoi ils ne le sont pas.

---

## 6.19.0 — 21 novembre 2026

### Performances

- L'attention de l'écriture des réponses sur le GPU traite 32 positions à la fois par groupe de calcul au lieu d'une, et groupe ses lectures de la mémoire de conversation. Modèle du service, Mac mini M5 Pro : écriture après 512 tokens de 70,7 à 78,4 tokens/s, après 4 096 de 58,3 à 66, après 16 384 de 28-31 à 41-42 ; justesse inchangée. Valable sur toutes les puces Apple.

### Outils de mesure

- `GULLIVER_GPU_TIMING` (temps du GPU et de l'écriture des commandes à chaque pas) et `GULLIVER_GPU_PROFILE` (temps par noyau) ; `perplexity_runtime … steps` calcule la perplexité token par token, par le chemin de l'écriture.

---

## 6.18.1 — 21 novembre 2026

### Performances

- L'attention de la lecture sur les accélérateurs du M5 traite 128 lignes de requête par groupe au lieu de 64 : chaque pas de la mémoire de conversation sert à deux fois plus de requêtes. Modèle du service : 16 384 tokens à 1 093-1 255 tokens/s (984-1 021 en 6.18.0), 4 096 tokens à 2 186-2 280.

---

## 6.18.0 — 21 novembre 2026

### Performances

- Sur les Mac à puce M5, l'attention de la lecture des questions passe par les accélérateurs neuronaux de la puce (Metal 4), pour les modèles à têtes de 64 et 128 valeurs. Modèle du service, Mac mini M5 Pro : lecture d'une question de 4 096 tokens de 1 467 à 2 100-2 128 tokens/s, de 16 384 tokens de 524 à 984-1 021 tokens/s ; justesse inchangée. Les autres Mac gardent l'attention précédente.

---

## 6.17.0 — 21 novembre 2026

### Performances

- Les poids développés en demi-précision pour les produits des prompts sur le GPU sont gardés d'une passe à l'autre, dans la limite d'un quart de la mémoire du Mac, au lieu d'être redéveloppés à chaque passe. Lecture d'une question de 512 tokens avec le modèle du service (Mac mini M5 Pro) : de 2 060-2 159 à 2 593-2 708 tokens/s, devant llama.cpp ; perplexité inchangée.

---

## 6.16.0 — 21 novembre 2026

### Performances

- Les tests de vitesse et de justesse comparent aussi l'Albireo d'origine (version 1.0.27 du 21 novembre 2026), construit à part et sans modification (`macos/albireo-original/`) : sur ce Mac, il tourne sur le processeur seul, avec ses noyaux portables.
- Nouvelle présentation des tableaux : une ligne par modèle et par longueur de question, Gulliver, llama.cpp, MLX et l'Albireo d'origine côte à côte, chacun avec son écart à Gulliver en pourcentage (positif s'il est plus rapide, négatif s'il est plus lent) ; la justesse de la même façon, avec l'écart de perplexité.

---

## 6.15.1 — 21 novembre 2026

### Accès et documents

- Les mesures du dimanche (`tools/bench_hebdo.sh`) ne remplacent plus la précédente : chacune s'ajoute, sous un titre daté, en tête de la page « Mesures du dimanche » du carnet, de la plus récente à la plus ancienne.

---

## 6.15.0 — 21 novembre 2026

### Accès et documents

- Sous chaque réponse, la ligne d'information donne désormais deux vitesses : la lecture de la question et l'écriture de la réponse, en jetons par seconde (auparavant, l'écriture seule).
- Le carnet du portage gagne une rubrique « Tests de vitesse » : Gulliver comparé à llama.cpp et à MLX sur les mêmes modèles, en vitesse et en justesse, avec l'explication des écarts, et la dernière mesure hebdomadaire.

### Atelier d'entraînement

- Premier affinage d'un modèle de 4 milliards de paramètres sur le Mac : Gemma 3 4B, affiné par adaptateurs LoRA sur les 59 épisodes du blog du portage (perte sur un texte mis de côté de 4,61 à 2,28 en une lecture, 30 minutes). Les changements portent sur dlib : un tampon commun pour les dérivées des couches gelées, une part de validation pour l'étape « corpus », et des fils d'exécution de 16 Mo de pile (512 Ko par défaut sur macOS, insuffisants pour un réseau de 34 blocs).

### Performances

- Nouveaux outils de mesure : `tools/bench_moteurs.sh` (vitesse de Gulliver, llama.cpp et MLX, en alternance), `tools/justesse_moteurs.sh` et `tests/perplexity_runtime` (perplexité d'un même texte, découpée comme llama-perplexity), `tools/bench_hebdo.sh` (les deux, lancés chaque dimanche par un agent launchd de l'utilisateur, `macos/bench-hebdo/`).
- Constat du 21 novembre (Mac mini M5 Pro) : sur le modèle du service, llama.cpp lit 1,2 fois (512 tokens) à 2,2 fois (16 384 tokens) plus vite et écrit 1,3 à 1,6 fois plus vite ; la perplexité est identique entre les deux moteurs (écarts de 0,02 à 0,35 %).

---

## 6.14.2 — 21 novembre 2026

### Performances

- **Atelier d'entraînement** : le script de l'atelier (`tools/atelier/compare_dlib_import.sh`) construit désormais dlib avec Accelerate. Un affinage LoRA de Qwen3-0.6B (deux pas de quatre fenêtres de 256 tokens) passe de 780 s à 7 s sur le Mac mini M5 Pro, pour un résultat identique à l'octet près.

### Corrections

- L'affinage (`slm_lora_finetune_ex`) affiche la perte moyenne de toute l'époque. L'entraîneur de dlib remettait sa moyenne à zéro à chaque ligne affichée, si bien que le chiffre dépendait de la vitesse de la machine.

---

## 6.14.1 — 21 novembre 2026

### Plateforme

- **Mbed TLS est retiré** de Gulliver : tout le chiffrement (le serveur local, les connexions sortantes, les certificats) passe par Network.framework et Security, de macOS. cpp-httplib ne sert plus qu'en HTTP, derrière eux. Gulliver n'embarque plus que cpp-httplib et PDFium, d'autres projets ; SQLite, zlib, le décodage des images et le TLS sont ceux de macOS.
- La page « Composants et licences » ne liste plus Mbed TLS ; la fiche du réglage du port décrit le portier TLS de macOS.

---

## 6.14.0 — 21 novembre 2026

### Sécurité

- Les connexions sortantes en HTTPS (téléchargements depuis Hugging Face, catalogue des modèles, recherche Wikipedia, lecture de pages web, relais vers un modèle en ligne, Pseudalis) passent par le **TLS de Network.framework**, de macOS, au lieu de Mbed TLS : certificats vérifiés par les autorités de macOS (ou, pour Pseudalis, par sa seule autorité), nom du serveur contrôlé, TLS 1.2 au moins. L'épinglage de l'adresse vérifiée, qui protège la lecture de pages web contre le détournement par le DNS, est gardé, comme le passage par un mandataire HTTP. Les messages d'erreur restent ceux d'avant (certificat non valide, certificat qui nomme un autre serveur, connexion impossible).
- Un serveur ancien qui coupe les poignées de main TLS 1.3 de macOS est injoignable, comme depuis Safari.

---

## 6.13.0 — 21 novembre 2026

### Sécurité

- Le chiffrement du service local (`https://localhost:18443`) est fait par **Network.framework**, de macOS, au lieu de Mbed TLS : TLS 1.3 et 1.2, TLS 1.1 refusé, certificat du serveur suivi de l'autorité locale. Le serveur HTTP de Gulliver ne reçoit plus que des requêtes déchiffrées, par une prise Unix d'un dossier réservé au compte, joignable par aucune autre machine ni aucun autre compte. L'adresse ne change pas.
- Mêmes limites qu'avant : 128 connexions au plus, une connexion inactive fermée après 5 secondes ; aucun autre programme ne peut écouter sur le port du service.
- Différence : une requête en HTTP non chiffré envoyée au port HTTPS est fermée après 10 secondes, au lieu de l'être aussitôt.

---

## 6.12.1 — 20 novembre 2026

### Plateforme

- Les certificats du serveur local (l'autorité locale et le certificat de localhost) sont fabriqués par **Security et CommonCrypto**, de macOS, au lieu de Mbed TLS : clés EC P-256 gardées en mémoire, jamais dans le trousseau ; mêmes fichiers, même format et mêmes extensions qu'avant (contraintes de nom de l'autorité, usages, noms localhost, 127.0.0.1 et ::1). Les certificats déjà en place restent valables et ne sont pas refaits. L'encodage base64 ne passe plus par Mbed TLS. Première étape du passage au TLS d'Apple.

---

## 6.12.0 — 20 novembre 2026

### Plateforme

- **Gulliver ne lit plus rien de dlib.** La lecture des fichiers GGUF, le tokenizer, la lecture des archives ZIP (documents Word, Excel, PowerPoint, OpenDocument) et l'accès à la base sont repris dans Gulliver, sous la licence de dlib (Boost) ; sa construction n'a plus de chemin vers dlib.
- **SQLite et zlib sont ceux de macOS** : la copie de SQLite est retirée. La base s'ouvre en mode sûr entre fils (« serialized »), avec l'effacement sécurisé et les tables temporaires en mémoire, comme avec la copie. La décompression des archives ZIP et leur contrôle CRC passent par la zlib de macOS.
- Aucun changement de comportement : le texte extrait de documents Word, Excel et OpenDocument est identique à celui de la version précédente, archives compressées ou non ; une archive tronquée ou abîmée est refusée de la même façon.

---

## 6.11.0 — 20 novembre 2026

### Plateforme

- Les images envoyées au modèle sont décodées par **ImageIO**, de macOS, au lieu des copies de libpng et de libjpeg, retirées du service. Les pixels sont lus tels que le fichier les stocke, sans gestion des couleurs, comme le fait le prétraitement de référence ; la transparence posée sur du blanc, l'orientation EXIF et les limites de taille restent celles de Gulliver. Comparés à l'ancien décodage sur 338 images : les 58 PNG (dont 16 bits, niveaux de gris, palettes transparentes) sont identiques au pixel près ; sur les 280 JPEG, 94 % des valeurs sont identiques ou à un niveau près, l'écart moyen par image étant de 0,41 niveau sur 255 en médiane (1,67 au plus), chaque décodeur JPEG ayant ses arrondis.
- La page « Composants et licences » ne liste plus libpng ni libjpeg.

---

## 6.10.2 — 20 novembre 2026

### Sécurité

- **cpp-httplib 0.67.0** (au lieu de 0.63.0) et **Mbed TLS 3.6.8** (au lieu de 3.6.7, branche maintenue à long terme), téléchargés aux sources officielles ; l'archive de Mbed TLS est contrôlée par sa somme SHA-256 publiée. La 3.6.8 corrige notamment des empreintes SHA fausses au-delà de 4 Gio de données. La retouche qui refuse une ligne d'en-tête terminée par un LF seul (contrebande de requêtes) est reportée sur la nouvelle version ; le refus des codages de transfert inconnus (501) est désormais fait par cpp-httplib lui-même.
- SQLite (3.54.1), libpng (1.6.59) et libjpeg (IJG 10) sont déjà les dernières versions stables.

### Plateforme

- La configuration de Mbed TLS ne garde que macOS (fils POSIX) ; ses fichiers propres à Windows sont retirés de la copie de Gulliver.

---

## 6.10.1 — 20 novembre 2026

### Atelier

- **LFM2** s'importe en réseau dlib : LFM2.5-1.2B donne les mêmes probabilités que le moteur de Gulliver (divergence moyenne 3,2e-7 sur 26 positions, même token le plus probable partout). dlib reçoit une couche de convolution courte entraînable et une pile qui mêle, couche par couche, blocs d'attention et blocs à convolution.

- **Qwen 3.5** et **Mistral 3** s'importent aussi en réseau dlib, sans changement de Gulliver : Qwen3.5-0.8B (divergence moyenne 4e-8 sur 26 positions, 1,8e-9 sur 1 560) et Ministral 3 3B (3,8e-8 sur 26, 2,5e-8 sur 1 561) donnent les mêmes probabilités que le moteur. dlib reçoit pour eux une couche Gated DeltaNet entraînable, une porte de sortie et une rotation partielle dans l'attention, YaRN calculé comme llama.cpp et la température d'attention. Les quatre familles prévues pour l'import (Gemma 3, LFM2, Qwen 3.5, Mistral 3) sont en place.

### Corrections

- `test_runtime` ne s'arrête plus sur un modèle à état récurrent (LFM2, Qwen 3.5) : la troncature y repart de la position 0, et l'essai du préfixe partagé, qui coupe une séquence au milieu d'un bloc, est sauté avec un message.
- L'import de dlib refuse un tenseur plus petit que prévu au lieu de le lire au-delà de sa fin.

---

## 6.10.0 — 20 novembre 2026

### Accès et documents

- Un document joint depuis « Documents du Mac » est lu par le Mac lui-même : la page n'envoie plus que son nom (champ `document` de la route `/gulliver/v1/extract`), au lieu de télécharger le fichier pour le renvoyer au service. Depuis un téléphone, et à travers la passerelle Cloudflare, un document de ce dossier ne traverse plus le réseau ; la limite de lecture passe d'environ 48 Mo (corps de requête de 64 Mo en base64) à 100 Mo, celle de la lecture des documents. Les images et les sons de ce dossier sont toujours téléchargés par la page, qui les prépare ; un PDF scanné aussi, quand ses pages doivent être dessinées en images.

---

## 6.9.0 — 20 novembre 2026

### Accès et documents

- La page de discussion peut être servie par **Cloudflare Pages**, derrière une connexion GitHub (Cloudflare Access), le calcul restant sur le Mac, joint par un tunnel Cloudflare (connexion sortante, aucun port ouvert sur la box). Une passerelle ne transmet au Mac que la discussion, le calcul et les données des profils ; l'administration du service (modèles, réglages, redémarrage, journaux) en est refusée. Le dossier `cloudflare/` du dépôt contient la passerelle, l'assemblage de la page, le lancement du tunnel et le mode d'emploi.
- Servie par cette passerelle, la page cache les onglets « Modèles » et « Service » et leurs raccourcis du menu.

---

## 6.8.4 — 20 novembre 2026

### Atelier

- La **fenêtre glissante de Gemma 3** est appliquée au réseau dlib importé : sur un texte de 1 303 tokens, bien au-delà de la fenêtre de 512 positions du 1B, le réseau donne les mêmes probabilités que le moteur de Gulliver (divergence moyenne 3,8e-7, même token le plus probable à chaque position).

### Corrections

- `test_runtime`, en mode `logprobs`, applique au moteur le même plancher (probabilité de 10⁻³⁰) que le réseau dlib : sur un long texte, des mots de probabilité infime faisaient croire à un écart.

---

## 6.8.3 — 20 novembre 2026

### Atelier

- **Gemma 3** s'importe en réseau dlib : Gemma 3 1B et 4B donnent les mêmes probabilités que le moteur de Gulliver (divergence moyenne 1,2e-7 et 1,1e-7 sur 26 positions, même token le plus probable partout). La fenêtre glissante de Gemma 3 n'est pas encore appliquée : le réseau ne correspond au modèle que jusqu'à 512 tokens (1B) ou 1 024 (4B).
- `tools/atelier/compare_dlib_import.sh` ne juge plus que la comparaison au réseau dlib ; les vérifications du moteur seul sont signalées à part.

---

## 6.8.2 — 20 novembre 2026

### Atelier

- Premier pas de l'atelier d'entraînement : un modèle GGUF importé en réseau dlib (le réseau que l'on pourra entraîner) est comparé au moteur de Gulliver par `tools/atelier/compare_dlib_import.sh`. Pour Qwen3-0.6B, les deux donnent les mêmes probabilités (divergence moyenne 4,2e-7 sur 25 positions, même token le plus probable partout).
- `test_runtime` accepte des log-probabilités comme référence (`logprobs`).

### Corrections

- L'import d'un GGUF en réseau dlib s'arrêtait pour tout modèle (« assigned 170 of 171 parameter tensors ») : la table des embeddings était comptée deux fois. Corrigé dans dlib.
- `test_runtime` exigeait, hors du mode exact, des résultats identiques au bit près entre une séquence seule et la même séquence dans un lot, alors que les deux ne passent pas forcément par les mêmes calculs (8 bits sous 16 tokens, Accelerate au-dessus) : il compare désormais leurs distributions. Il refuse aussi une suite de moins de 4 tokens au lieu de s'arrêter sur une erreur.

---

## 6.8.1 — 20 novembre 2026

### Plateforme

- Les bibliothèques d'autres projets qu'utilise Gulliver (cpp-httplib, Mbed TLS, SQLite, libpng, libjpeg) sont désormais livrées avec lui, dans `third_party/`, copiées sans modification de celles de dlib. La construction de Gulliver ne construit plus dlib, dont il n'utilise plus que quelques en-têtes (lecture des GGUF, tokenizer, ZIP, accès à SQLite). Aucun changement de comportement.

---

## 6.8.0 — 20 novembre 2026

### Performances

- Les **modèles à experts** lisent leurs questions **3 à 49 % plus vite** sur le GPU : le calcul groupé de leurs experts prépare désormais les poids en demi-précision, les sommes restant en pleine précision. Sur le Mac mini, Qwen3-30B-A3B passe de 577 à 674 à 760 à 787 tokens/s sur une question de 512 tokens, Qwen3.6-35B-A3B de 557 à 566 à 617 à 620, Granite 3.1 MoE de 3 377 à 3 906 à 4 768 à 5 028.

---

## 6.7.0 — 20 novembre 2026

### Performances

- **La lecture des questions sur le GPU va 1,2 à 2,7 fois plus vite**, pour tous les modèles. Le GPU multiplie désormais les poids et les entrées des prompts en demi-précision, sur ses unités de calcul matriciel, comme le fait llama.cpp, au lieu de tout calculer en pleine précision ; les sommes restent en pleine précision. Sur le Mac mini, le modèle du service lit 2 041 à 2 100 tokens/s sur une question de 512 tokens (750 à 761 avant), et une question de 16 384 tokens en 31 s au lieu de 44. Le gain est le plus fort sur les questions courtes et moyennes ; l'écriture des réponses ne change pas.
- Le contrôle fait au chargement de chaque modèle compare toujours le GPU au processeur. Un modèle qui ne le passe pas en demi-précision le refait en pleine précision avant d'être renvoyé au processeur : LFM2.5-8B-A1B garde ainsi le GPU, avec ses questions lues en pleine précision, à la vitesse d'avant.

---

## 6.6.0 — 20 novembre 2026

### Performances

- Les **prompts des mélanges d'experts** passent désormais tous sur le GPU, et plus seulement ceux de Qwen 3.5/3.6 et de LFM2 : Qwen3-30B-A3B lit ses prompts 4 à 6 fois plus vite (96 à 132 → 521 à 593 tokens/s sur le Mac mini), Mixtral 8x7B environ 4 fois (29 à 35 → 133 à 143), Granite 3.1 MoE 3 à 5 fois.

### Modèles

- **Qwen3-30B-A3B, Granite 3.1 MoE et Mixtral 8x7B** ont été essayés pour la première fois, sur le processeur et sur le GPU : leurs réponses sont celles de llama.cpp.
- **Mixtral et les premiers Mistral 7B** (v0.1, v0.2) reçoivent enfin leur vrai format de conversation, « [INST] … [/INST] ». Leur vocabulaire n'ayant pas de token [INST], le service leur appliquait jusqu'ici un format générique (« User: … Assistant: »), qui dégradait leurs réponses.
- Une conversion de Mixtral qui range ses experts un par un (fichiers de 2023) est refusée avec un message clair, comme le fait llama.cpp ; il faut une conversion récente.

---

## 6.5.0 — 19 novembre 2026

### Modèles

- **Qwen3.6-35B-A3B** (fichier UD-Q4_K_M d'unsloth, 22,1 Go) tourne désormais **sur le GPU**. Ce fichier garde deux petites matrices par couche en pleine précision, que le GPU refusait ; il les lit maintenant telles quelles. Sur le Mac mini, la lecture d'un prompt passe de 103 à 176 tokens/s à 419 à 465, et l'écriture de 29 à 36 à 49 à 52. Le texte produit est identique à celui de llama.cpp.
- Ses **images** sont lues avec son projecteur (`mmproj-F16`) : la description d'une image est identique, mot pour mot, à celle de llama.cpp. L'onglet Modèles propose désormais la lecture des images pour cette famille de modèles.

---

## 6.4.1 — 19 novembre 2026

### Plateforme

- L'empreinte des images que lit le moteur (la clé qui évite de réencoder une image déjà vue) passe par **CommonCrypto**, la bibliothèque d'Apple, comme celle des modèles depuis la 4.2.0. Gulliver n'utilise plus le SHA-256 de dlib : c'est le premier pas pour rendre Gulliver indépendant de dlib. Un nouveau test vérifie l'empreinte sur les valeurs de référence de la norme.

---

## 6.4.0 — 19 novembre 2026

### Plateforme

- Premier passage sur le **Mac mini M5 Pro** (18 cœurs, 64 Go) : tous les tests passent (service, Pseudalis, noyaux Metal, page). Par rapport au MacBook Air M2, le GPU lit les prompts 2,6 à 4,6 fois plus vite et écrit 2,5 à 3,6 fois plus vite (même version, mêmes modèles).

### Performances

- **Écriture sur le processeur plus rapide sur les puces sans cœurs d'efficacité.** Le moteur réservait l'écriture des tokens aux seuls cœurs de la première famille, ce qui convient au M2 (cœurs « Performance » et « Efficiency », lents). Le M5 Pro a 6 cœurs « Super » et 12 cœurs « Performance », tous rapides : le moteur les emploie désormais tous. Sur le Mac mini, l'écriture sur le processeur gagne de 15 à 60 % (Qwen3-VL 4B Thinking : 38 à 41 → 64 tokens/s après 512 tokens) ; la lecture ne change pas, et rien ne change sur le M2.

### Corrections

- **Les réponses ne dépendent plus du nombre de threads.** Pendant l'écriture, le moteur découpe la relecture du contexte en plages traitées en parallèle ; leur nombre suivait le nombre de threads, et l'ordre des additions avec lui. Une réponse sur le fil pouvait alors changer d'une machine à l'autre (LFM2.5-8B-A1B retrouvait un détail d'un long journal avec 6 ou 8 threads, pas avec 18). Le découpage est désormais fixe, sans perte de vitesse mesurable.

### Réglages

- La fiche du réglage **« Threads du moteur »** décrit maintenant aussi le Mac mini ; la valeur conseillée reste « Auto » (tous les cœurs), le meilleur choix mesuré sur les deux machines.

---

## 6.3.0 — 18 novembre 2026

### Documents

- La **lecture du texte des documents** se fait désormais dans le service, et non plus dans le navigateur : Word, PowerPoint, Excel, OpenDocument, RTF et HTML par le service lui-même, et les **PDF par PDFium** (une bibliothèque de lecture de PDF). Le navigateur n'extrait plus aucun texte ; il envoie le fichier, le service rend le texte. Un PDF numérisé (fait d'images) est toujours lu page par page par le modèle de vision, comme avant.
- L'**indexation d'un document** — le découpage en passages et le calcul de leurs vecteurs — se fait entièrement dans le service, en un seul appel, avec une barre de progression. Plus aucun calcul de recherche ne reste dans le navigateur.
- Nouveau réglage **« Affiner le tri des extraits »** (onglet Service, éteint par défaut) : après la recherche, un second tri met devant l'extrait qui couvre le mieux l'ensemble des mots de la question, sans modèle supplémentaire.

### Modèles

- L'onglet Hugging Face propose des **suggestions de modèles « abliterated »** (sans réflexe de refus), utiles pour la cybersécurité : un clic lance la recherche. À n'utiliser que pour un usage autorisé.

---

## 6.2.0 — 18 novembre 2026

### Documents

- La recherche dans les documents (le choix des extraits qui répondent à une question) se fait désormais dans le service, en C++, et non plus dans le navigateur. On choisit sa façon de chercher dans l'onglet Service : **mots et sens** (par défaut), **sens seul** (par les vecteurs d'embedding) ou **mots seuls** (sans modèle d'embedding, pour les termes techniques exacts). Le nombre d'extraits remontés se règle aussi.
- Un profil peut garder **plusieurs index** de documents côte à côte — par exemple un par modèle d'embedding ou par taille de découpage — et basculer de l'un à l'autre instantanément, sans tout réindexer.
- Les discussions privées gardent leur recherche dans le navigateur : leurs passages n'en sortent jamais.

---

## 6.1.0 — 17 novembre 2026

### Profils

- Les données de l'application — discussions, projets, réglages de la page, index des documents (passages et vecteurs) et images — ne vivent plus dans le navigateur mais dans le service, rangées par **profil**. Un profil est un simple nom, sans mot de passe. Au premier démarrage, la page demande de choisir un profil ou d'en créer un ; le menu « Le Mac » permet d'en changer. Un même profil retrouve ses données depuis n'importe quel navigateur ou appareil, sans rien exporter.
- Les discussions privées restent en mémoire, comme avant, et ne sont jamais envoyées au service. Quand le service est injoignable ou qu'aucun profil n'est choisi, la page fonctionne en mémoire : rien n'est gardé, jusqu'au prochain démarrage avec un profil.

### Accès et documents

- Les données d'un profil sont accessibles depuis le réseau de la maison, comme le chat : on retrouve ses discussions depuis le téléphone, sans mot de passe (réseau personnel maîtrisé).

---

## 6.0.0 — 17 novembre 2026

### Plateforme

- Le service de chat change de nom : **Albireo devient Gulliver**. C'est une rupture de nom et d'interface. Les adresses de l'interface passent de `/albireo/v1/…` à `/gulliver/v1/…` ; la variable d'environnement du dossier de données devient `GULLIVER_HOME` (sinon `~/.local/share/gulliver`) ; la base de données du service s'appelle `gulliver.db` ; les identifiants de modèles de l'interface deviennent `gulliver-general`, `gulliver-code`, `gulliver-security`, `gulliver-relay`, `gulliver-background` et `gulliver-embedding`. Les routes compatibles OpenAI (`/v1/chat/completions`, `/v1/models`, `/v1/embeddings`, `/v1/completions`) sont inchangées. Pseudalis garde son nom et trouve désormais Gulliver par `GULLIVER_HOME`.

### Sécurité

- Les réglages du relais (vers un moteur extérieur) se changent désormais depuis **n'importe quel appareil du réseau de la maison**, le téléphone compris. La garde qui les réservait au Mac lui-même (code `relay_mac_only`) est retirée. Cela permet de tout piloter depuis le téléphone ; en contrepartie, un appareil du Wi-Fi peut rediriger le relais : à n'utiliser que sur un réseau personnel maîtrisé.

---

## 5.9.0 — 16 novembre 2026

### Moteurs extérieurs

- Le niveau d'effort d'un modèle extérieur du relais se choisit dans la discussion, repris de l'original (Albireo 1.0.23) : quand ce modèle prend des niveaux d'effort (Claude, gpt-oss chez Ollama, un moteur OpenAI qui raisonne), le bouton du raisonnement devient « Effort · … » et ouvre leur menu, celui par défaut d'abord ; le niveau choisi part avec chaque question de la discussion (`reasoning_effort`).

### Accès et documents

- Une discussion se supprime depuis la page de son projet, par la corbeille de sa ligne, une fois confirmé, repris de l'original (Albireo 1.0.23). La page du projet reste affichée ; une discussion supprimée alors qu'elle était ouverte cède la place à une nouvelle discussion.

---

## 5.8.0 — 16 novembre 2026

### Performances

- Les modèles en Q5_K (les fichiers « Q5_K_M » de Hugging Face, et les couches en Q5_K des autres) écrivent avec un noyau NEON sur le processeur, au lieu du code portable, avec le même résultat au bit près. Qwen 3.5 0.8B en Q5_K_M sur le MacBook Air M2 : de 38,7-40,1 à 71,6-72,0 tokens/s, réponse identique. Sur une ligne de 4 096 valeurs, le produit passe de 2,9 à 0,5 ms.
- Un noyau NEON pour Q8_0, essayé en même temps, ne gagnait rien (le compilateur vectorise déjà le code portable de ce format) et n'a pas été gardé. Gemma 4 26B-A4B, dont seules quelques couches sont en Q5_K, écrit à la même vitesse qu'avant.

---

## 5.7.0 — 16 novembre 2026

### Modèles

- Mistral Small 3.2 24B (déjà au catalogue de l'original) a son format de conversation, repris de l'original (Albireo 1.0.14) : le prompt système par défaut de son gabarit, avec la date du jour et celle de la veille ; les outils écrits juste avant la dernière question, comme le fait l'encodage de référence de Mistral ; l'identifiant de chaque appel d'outil après `[CALL_ID]` et celui de l'appel auquel répond un résultat avant `[TOOL_CONTENT]`, en 9 lettres ou chiffres. Ministral 3 et Devstral Small 2 gardent leur format.
- Vérifié sur le MacBook Air M2 avec le fichier Q4_K_M d'Unsloth (14,3 Go) : réponse juste, appel d'outil lu, résultat d'outil utilisé. Sur ce Mac, le modèle ne tient pas dans la mémoire que macOS accorde au GPU (13,6 Go) et reste sur le processeur, à environ 3,5 tokens/s d'écriture, services arrêtés ; il est fait pour une machine de 32 Go ou plus.

---

## 5.6.0 — 16 novembre 2026

### Réglages

- Les boutons Raisonnement approfondi, Agent et Web suivent les capacités vérifiées de chaque modèle, repris de l'original (Albireo 1.0.16) : le format d'un modèle chargé donne son raisonnement et ses outils, et son entrée dans le catalogue de l'original ou dans les modèles livrés les restreint. Qwen2.5 Coder 1.5B et 7B, dont le gabarit décrit des outils qu'ils n'appellent pas, ont désormais Agent et Web éteints. Un modèle importé, ou ajouté depuis Hugging Face, dont les capacités ne sont que devinées, garde celles de son format.
- La liste des modèles se relit quand le service adopte un catalogue plus récent, ou après une lecture manquée, et en 8 secondes au plus ; les boutons de la zone de saisie suivent, avant même la première discussion.

---

## 5.5.1 — 16 novembre 2026

### Corrections

- Qwen 3.5 et 3.6 (modèles à état récurrent) : une étape qui échoue sur le GPU après avoir écrit l'état de ses séquences ne laisse plus cet état faussé. Le moteur tient, pour chaque séquence, le carnet des tokens lus depuis son dernier point de reprise ; en cas d'échec, chaque séquence repart de ce point et relit son carnet sur le processeur, puis la réponse continue. Auparavant, le même token pouvait compter deux fois dans l'état, et la suite de la réponse dériver.
- Le carnet ne coûte rien à l'écriture (19,7 à 19,8 tokens/s pour Qwen 3.5 4B sur le GPU, avant comme après). Une copie de l'état avant chaque étape, essayée d'abord, coûtait 6 à 9 % et n'a pas été gardée.
- Quand le carnet ne permet pas de relire (une image lue depuis le point de reprise, ou des positions partagées avec une autre séquence), le moteur continue comme avant et l'écrit dans son journal.

---

## 5.5.0 — 16 novembre 2026

### Performances

- Gemma 4 lit les prompts environ trois fois plus vite. Ses dernières couches (20 des 35 du E2B) relisent les clés et valeurs de couches précédentes sans en écrire : elles ne calculent plus que les lignes dont la réponse a besoin, en général le dernier token, et un morceau de prompt qui n'en demande aucune s'arrête avant elles. Sur le processeur et sur le GPU.
- Mesures sur le MacBook Air M2 (gemma-4-E2B-it Q4_K_M, 8 threads, avant et après en alternance) : 512 tokens, de 154-156 à 463-471 tokens/s sur le processeur et de 233-237 à 671-685 sur le GPU ; 4 096 tokens, de 129-134 à 371-383 et de 93-95 à 241-249. L'écriture des réponses ne change pas.
- Les réponses restent identiques au token près, sur le processeur comme sur le GPU.

---

## 5.4.1 — 16 novembre 2026

### Sécurité

- Les réglages du relais se changent depuis le Mac seul (décision de l'utilisateur) : `PUT /albireo/v1/settings` avec la clé `relay`, et `POST /albireo/v1/relay/test`, ne sont acceptés que d'une requête arrivée par l'adresse locale du service (`https://localhost`, qui n'écoute que sur 127.0.0.1), dont l'en-tête Host nomme ce serveur et dont l'origine, s'il y en a une, est la page du service. L'accès depuis le réseau de la maison et les pages des autres sites, même ouvertes sur le Mac, reçoivent 403 (code `relay_mac_only`) : ils ne peuvent plus diriger le relais vers un autre moteur ni le rallumer.
- Depuis un autre appareil, la carte du relais montre son état et le dit, sans champs ni boutons. Le cadenas des discussions reste utilisable : tout appareil du Wi-Fi peut encore envoyer une discussion par un relais déjà réglé, donc utiliser le crédit du compte extérieur.

---

## 5.4.0 — 16 novembre 2026

### Moteurs extérieurs

- Les discussions passent par le relais depuis la page : un cadenas apparaît à côté du bouton d'envoi tant que le relais est prêt. Fermé, la discussion reçoit ses réponses sur le Mac ; ouvert, il devient rouge et affiche le nom du modèle extérieur, et les questions de cette discussion seule partent vers lui (modèle `albireo-relay`). Chaque nouvelle discussion commence cadenas fermé. Son titre dit si les valeurs personnelles sont pseudonymisées, si les réponses gardent les pseudonymes (contrôle) et si les images partent en description.
- Une discussion relayée prend la fenêtre de contexte, les images et le raisonnement du modèle extérieur. L'agent reste réservé aux modèles du Mac ; le bouton Web demande à Claude de chercher lui-même sur le web (`web_search`), et reste éteint pour les autres fournisseurs. Le choix du Web fait pour le relais ne passe pas aux modèles du Mac, et inversement.
- Une réponse du relais garde le nom du modèle extérieur qui l'a écrite. La ligne d'état indique « hors du Mac » au lieu du processeur ou du GPU, et le pied de page nomme le modèle extérieur.
- Un réglage du relais changé depuis (fournisseur, modèle, adresse, pseudonymisation) referme le cadenas des discussions qui l'avaient ouvert, jusqu'à un nouveau clic.
- La relecture d'avance de l'historique, faite par le moteur du Mac, ne s'applique pas aux discussions relayées.

### Corrections

- Les échecs du relais s'affichent en français, selon leur code : clé ou crédit refusés, moteur injoignable, réponse coupée, mode hors ligne, Pseudalis absent (la question ne part pas), relais pas prêt.
- La page se redessine quand l'état du relais change (prêt, modèle, pseudonymisation), sans tenir compte des compteurs de la mémoire de pseudonymisation.

---

## 5.3.0 — 16 novembre 2026

### Moteurs extérieurs

- Relais vers un moteur extérieur, repris de l'original (Albireo 1.0.22 et 1.0.23) : le modèle `albireo-relay` de l'interface envoie une discussion à un moteur de l'interface d'OpenAI (adresse, clé, utilisateur et mot de passe, ou rien), au nuage d'Ollama ou à l'interface de Claude, et rend sa réponse comme le moteur du Mac rend la sienne : texte, raisonnement, usage, en flux ou en entier. Les autres modèles restent sur le Mac. Les clients ne voient ni l'adresse ni la clé du moteur extérieur.
- Modèles connus : sept modèles de Claude (fenêtre d'un million de tokens, images, raisonnement adaptatif, cinq niveaux d'effort) et cinq d'Ollama ; pour un moteur d'OpenAI, la fenêtre, la longueur des réponses, les images et le raisonnement se règlent. Une adresse sans `/v1`, ou avec le chemin d'une opération, est complétée.
- Recherche sur le web de Claude (`web_search: true`), cinq recherches au plus par réponse, facturées par Anthropic ; ses requêtes viennent avec le raisonnement et ses sources en liens après le texte. L'accès au web des réglages doit le permettre.
- Pseudonymisation par Pseudalis (3.1.0 ou plus, sa clé de séance) : les textes partent avec des pseudonymes, et la réponse revient avec les vraies valeurs, remises au fil du flux, y compris un nom de famille cité seul. Les images ne partent pas : un modèle du Mac qui lit les images les décrit, et la description part protégée. Un Pseudalis absent ou trop ancien arrête la requête : le texte ne part jamais en clair. Le réglage `keep_substitutes` laisse les pseudonymes dans la réponse, pour contrôler ce qui a quitté le Mac.
- Mode hors ligne : seul un moteur du Mac ou du réseau de la maison reste joignable (adresses de bouclage, privées et lien-local ; un nom dont une adresse est publique est refusé) ; Ollama et Claude sont refusés avant toute connexion.
- Routes `GET /albireo/v1/relay` (fournisseurs, modèles, état du relais et de Pseudalis) et `POST /albireo/v1/relay/test` (liste les modèles du fournisseur, pour vérifier l'adresse et la clé) ; réglage `relay` de `PUT /albireo/v1/settings` ; `albireo-relay` dans `/v1/models` une fois le relais prêt, et dans `/albireo/v1/count` (estimation).

### Sécurité

- La clé ou le mot de passe du relais est gardé dans `relay.secret`, lisible par le compte du service seul (droits 0600), jamais dans `settings.json` ni dans les réponses (`********`). Le masque ne vaut que pour le fournisseur, l'adresse et l'utilisateur pour lesquels la clé a été donnée : changer l'un d'eux demande de la retaper. La clé ne passe que dans les en-têtes des requêtes, jamais dans le journal ni dans un message d'erreur, même quand le moteur extérieur la recopie.
- Avec l'accès depuis le réseau de la maison ouvert (sans mot de passe), tout appareil du Wi-Fi peut utiliser le relais, donc la clé et le crédit du compte extérieur, et changer ses réglages ; il ne peut pas lire la clé.

---

## 5.2.0 — 15 novembre 2026

### Accès et documents

- Les documents du portage se lisent dans Albireo : menu « En savoir plus », « Documents du portage », ou la page `/carnet/`, depuis le Mac, le téléphone ou l'iPad. Sommaire en cartes, deux blogs, journaux, guides, notes de version et README, mis en page en clair et en sombre, avec les liens entre documents. Routes `GET /albireo/v1/carnet` et `GET /albireo/v1/carnet/file`, limitées à une liste fermée de fichiers du dépôt (`macos/site-docs/sommaire.json`) : aucun autre fichier, ni chemin caché, ni lien n'est servi. Le petit site à part (port 8650) est retiré.
- Le sélecteur « Albireo | Pseudalis » apparaît aussi depuis un autre appareil, quand l'accès réseau de Pseudalis est ouvert (Pseudalis 2.1.0) ; `GET /albireo/v1/pseudalis` donne `lan` et `lan_port`.

### Corrections

- Le README ne décrit plus l'hébergeur de l'espace de publication de l'original, ni les tests de cet espace, retirés du fork ; le test des anciens réglages n'en cite plus l'adresse.

---

## 5.1.0 — 15 novembre 2026

### Accès et documents

- L'engrenage de la barre du haut devient un bouton « Menu » à trois traits, qui ouvre un grand menu en colonnes : « Les réglages » (Général, Modèles, Génération, Mémoire), « Le Mac » (Service, Données), « Cette discussion » (Contexte de la prochaine question), « En savoir plus » (À propos, Notes de version, Interface de programmation), et une ligne de raccourcis vers le contexte et l'API. Sur un téléphone, les colonnes passent sur deux rangées.
- Les boutons du contexte et de l'API quittent la barre du haut, qui ne garde que le panneau latéral, le titre, les actions de la discussion et le menu.

---

## 5.0.0 — 15 novembre 2026

### Sécurité

- L'accès depuis le réseau de la maison n'a plus de mot de passe : il est ouvert à tous les appareils du réseau dès qu'il est activé dans l'onglet Service. Le réglage `lan_password`, la page de connexion, les routes `POST /albireo/v1/lan/login` et `POST /albireo/v1/lan/logout` et le cookie de session sont retirés. Un mot de passe enregistré par une version antérieure n'est plus lu, et disparaît de `settings.json` au prochain enregistrement des réglages. Changement incompatible pour un client qui utilisait ces routes, d'où le passage à la version 5.
- La fiche « Accès depuis le réseau » le dit : sur un Wi-Fi partagé ou public, ne pas l'activer.

---

## 4.32.1 — 15 novembre 2026

### Accès et documents

- Le bouton de la page de l'interface de programmation (API) passe du bas du panneau latéral à la barre du haut, à côté de l'engrenage, dans toutes les vues. Il quitte le menu de l'engrenage, qui garde les onglets des paramètres et les notes de version.

---

## 4.32.0 — 15 novembre 2026

### Accès et documents

- Un bouton dans la barre du haut d'une discussion ouvre « Contexte de la prochaine question » : ce que le modèle lirait si l'on envoyait maintenant ce qui est tapé, assemblé comme pour un envoi réel (consigne système, mémoire, historique, brouillon, outils de l'agent et de la recherche web quand ils sont actifs). Le service en compte le total exact (`POST /albireo/v1/count`), rapporté à la taille du contexte du modèle ; chaque message montre une estimation de ses jetons et son texte. La fenêtre rappelle qu'à l'envoi des extraits de documents peuvent s'ajouter et que les échanges les plus anciens peuvent être résumés.
- Le menu de l'engrenage, en haut à droite, donne accès à tous les onglets des paramètres (Général, Modèles, Modèle, Mémoire, Données, Service, À propos), aux notes de version et à la page de l'interface. Le bouton « Paramètres » en bas à gauche, qui faisait double emploi, est retiré.
- Les explications « ? » du contexte et du débit décrivent la mesure exacte : fenêtre de cinq secondes, passes du moteur, préchauffage compté, tokens repris du cache non comptés.
- La page de l'interface de programmation est entièrement en français : descriptions des routes, et libellés de Swagger UI (« Essayer », « Paramètres », « Réponses », filtre des sections).

### Corrections

- Les boutons « Agent » et « Web » ne sont plus grisés sans explication quand le raisonnement approfondi est coupé : un appui les active et active le raisonnement quand le modèle en a besoin pour agir, avec un message qui le dit ; sans outils dans le modèle, un message explique pourquoi l'agent est indisponible. Ils restent pâles tant qu'ils ne peuvent pas servir.

---

## 4.31.1 — 15 novembre 2026

### Accès et documents

- La ligne « Contexte » et le débit du moteur, en bas à gauche, ont chacun un bouton « ? » qui ouvre une explication : ce que mesure la ligne, ses chiffres du moment, et, pour le contexte, les couleurs de la barre et le résumé des échanges les plus anciens au-delà de la moitié du contexte.
- Le README d'Albireo est traduit en français.
- Pseudalis a désormais sa propre numérotation (2.0.0) et ses propres notes de version, dans sa propre documentation ; les changements de Pseudalis n'apparaissent plus ici.

---

## 4.31.0 — 15 novembre 2026

### Accès et documents

- Passage entre Albireo et Pseudalis : un sélecteur « Albireo | Pseudalis » sous le logo de chacune des deux applications, montré sur le Mac lui-même quand l'autre application y tourne. Albireo trouve Pseudalis par son dossier de données (`PSEUDALIS_HOME`, sinon `~/.local/share/pseudalis`) et le port de ses réglages (route `GET /albireo/v1/pseudalis`) ; Pseudalis trouvait déjà Albireo. Aucun jeton d'administration ne passe d'une application à l'autre.
- Débit du moteur en direct, en bas à gauche : les tokens lus (prompts, documents) et écrits (réponses) par seconde, sur les cinq dernières secondes, relus chaque seconde ; au repos, les dernières valeurs restent affichées en plus pâle (route légère `GET /albireo/v1/speed`).
- La page de l'interface de programmation (bouton « API ») est refaite : en-tête en français avec le retour à l'application, le passage à l'interface de Pseudalis, le fichier `openapi.json` et un bouton de thème (système, clair, sombre) ; une carte « Démarrage rapide » avec des exemples curl, Python et JavaScript prêts à copier, remplis avec l'adresse et le certificat de ce service ; Swagger UI aux couleurs de l'application, en clair et en sombre, avec un filtre des sections.

### Pseudalis

- L'application de Pseudalis est réorganisée : un menu à gauche (protéger des fichiers, restaurer, protéger un texte, et l'administration quand le jeton est donné), une section à la fois, l'état du service et le débit en direct de son moteur (octets lus et écrits par seconde, route `GET /v1/speed`) dans le menu, un thème sombre, le sélecteur vers Albireo. Sa page d'interface prend le même habillage que celle d'Albireo, en anglais et en français.
- `tools/open_pseudalis.sh` ouvre l'application avec son jeton d'administration, comme l'icône le fait sous Windows, sans que le jeton paraisse dans la liste des processus. La description de l'interface donne le chemin du jeton sur macOS.

---

## 4.30.2 — 15 novembre 2026

### Réglages

- Les fiches de l'onglet Service décrivent l'état actuel du moteur : la fiche « GPU du Mac » liste ce qui reste sur le processeur (Gemma 3 1B, les experts de Gemma 4 26B-A4B, le modèle de recherche, les passes qui lisent une image, les prompts des mélanges d'experts autres que Qwen 3.6) au lieu d'annoncer que Gemma 4 lit ses prompts sur le processeur, et ses seuils de contrôle (1 %, 5 % en clés 8 bits, 25 % pour un mélange d'experts en 8 bits) ; la fiche « GPU : prompts jusqu'à » décrit les têtes de 512 de Gemma 4 et la fenêtre glissante ; la fiche « Hors ligne » cite la bibliothèque du Mac.
- L'onglet Modèles ne dit plus qu'un mélange d'experts tourne sur le processeur seulement : les experts de Qwen 3.6 et de LFM2 passent par le GPU depuis les versions 4.7.0 et 4.21.0 ; seul Gemma 4 26B-A4B reste sur le processeur. Les formats acceptés par le GPU (Q5_K, Q8_0 et Q5_0 en plus pour Qwen 3.5, LFM2 et les mélanges d'experts) entrent dans l'avis donné sur chaque modèle.

---

## 4.30.1 — 15 novembre 2026

### Corrections

- Un projecteur d'images d'un type connu du moteur (Gemma 3, Gemma 4, Qwen3-VL et Qwen 3.5) ne s'associe plus qu'à un modèle de sa famille. À largeur égale, le projecteur de Qwen3.5-4B était associé à Gemma 3 4B dans la bibliothèque du Mac, et le vrai projecteur de Gemma 3 restait seul ; la même règle vaut pour les imports.

---

## 4.30.0 — 15 novembre 2026

### Accès et documents

- La bibliothèque du Mac : un dossier de fichiers GGUF (réglage « Bibliothèque du Mac » de l'onglet Service, par défaut le dossier « library » des données du service) que le service lit sans réseau et sans jamais y écrire. Le bouton « Rafraîchir depuis le Mac » de l'onglet Modèles la liste dans une section « Sur ce Mac » : chaque modèle avec son architecture, sa quantification, sa taille, le projecteur d'images qui va avec lui et la mention « déjà dans Albireo » ; ceux que le moteur ne lit pas sont en gris. « Charger dans Albireo » installe le modèle et son projecteur par une copie, un clone APFS qui ne prend pas de place sur le même disque, après vérification de leur empreinte ; le fichier reste dans la bibliothèque. Routes `GET /albireo/v1/library` et `POST /albireo/v1/library/load`. Un dossier que d'autres comptes du Mac peuvent modifier est refusé.
- Les modèles Gemma 3 et Gemma 4 dont le vocabulaire contient les tokens d'image sont reconnus comme lisant les images, ce qui leur associe leur projecteur lors d'un import.

### Corrections

- Le mémo du portage annonçait 113 vérifications du banc de la page ; il y en avait 112. Le banc en compte 114 avec les deux vérifications de la bibliothèque.

---

## 4.29.0 — 15 novembre 2026

### Modèles

- Gemma 4 26B-A4B, le mélange d'experts de la famille, est pris en charge sur le processeur. Dans chaque couche, un bloc feed-forward dense et 128 experts, dont 8 sont choisis pour chaque token, travaillent côte à côte, et leurs sorties normalisées sont additionnées ; le routeur lit l'état normalisé sans poids, avec son échelle propre, et la sortie de chaque expert reçoit la sienne. Les couches globales ont 2 têtes de clés et valeurs, les couches à fenêtre glissante 8 : le moteur lit désormais ce nombre couche par couche. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton, avec gemma-4-26B-A4B-it UD-Q4_K_M : un calcul avec raisonnement de 233 tokens identique mot pour mot ; une réponse sur un journal de 2 820 tokens identique ; un paragraphe identique jusqu'à une quasi-égalité de la référence (0,494 contre 0,339). Le GPU reste inutilisé pour ce modèle. Le service ne refuse plus ces dépôts dans sa recherche.

### Corrections

- Pour les modèles Gemma 4 dont le gabarit l'écrit (le 26B-A4B, pas le E2B), un raisonnement vide est ajouté au prompt quand aucun raisonnement n'est demandé, comme le fait le gabarit ; sans lui, le prompt comptait quatre tokens de moins que celui de la référence.

---

## 4.28.0 — 15 novembre 2026

### Modèles

- Les modèles Gemma 3 qui lisent des images (à partir du 4B) les lisent avec leur projecteur (type `gemma3`) : l'image est redimensionnée dans un carré de 896 pixels, son rapport d'aspect gardé et le reste rempli de noir, lue par un encodeur SigLIP de 27 blocs, puis ses patchs sont regroupés par carrés de 4 × 4, ce qui donne toujours 256 tokens par image. Comme dans llama.cpp, les 256 tokens d'une image se voient les uns les autres dans le modèle de langage, au lieu de ne voir que ceux qui les précèdent ; une image est donc toujours lue en une seule passe. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton, avec gemma-3-4b-it Q4_K_M et son projecteur F16 : même nombre de tokens de prompt (274) ; avec les clés et valeurs en demi-précision, une description identique mot pour mot et, pour une image en PNG, des écarts seulement sur des quasi-égalités de la référence (0,444 contre 0,415 ; 0,466 contre 0,37). Une passe qui contient une image reste sur le processeur. Encodage d'une image sur MacBook Air M2 en mode économie d'énergie : 26 à 30 s, à mesurer hors de ce mode.

---

## 4.27.0 — 15 novembre 2026

### Performances

- Les modèles Gemma 4 dense lisent aussi leurs prompts sur le GPU : l'attention des prompts en tuiles prend les têtes de 512 valeurs en deux tranches de 256, pour tenir dans la mémoire rapide du GPU. Au chargement, le contrôle trouve les logits du prompt à 0,48 % de ceux du processeur, avec le même token le plus probable. Sur un journal de 2 622 tokens, la réponse est la même que sur le processeur. Vitesse à mesurer sur le Mac mini.

---

## 4.26.2 — 15 novembre 2026

### Plateforme

- Albireo et Pseudalis se lient à la zlib de macOS au lieu d'en compiler une copie ; la licence de zlib n'est plus livrée avec les programmes, ni listée dans la page « Composants et licences ».
- La bibliothèque dlib, sur laquelle reposent Albireo et Pseudalis, ne vise plus que macOS : son interface Python et sa copie de CBLAS sont retirées, et son code ne contient plus de conditions Apple ni Windows (versions Windows des services de base, contournements de Visual Studio, branches des fichiers de construction, exemples et tests, soit 7 233 lignes). Les bibliothèques tierces qu'elle embarque sont inchangées. Aucun changement de comportement ; les builds de dlib, d'Albireo et de Pseudalis ne produisent plus aucun avertissement.

---

## 4.26.1 — 14 novembre 2026

### Plateforme

- Le moteur processeur ne contient plus de conditions de plateforme : les noyaux NEON et les produits par Accelerate s'appliquent toujours, et la compilation s'arrête avec un message clair sans les instructions de produit scalaire des puces Apple. Les versions portables de l'attention et de l'encodeur d'images, devenues inutiles, sont retirées (244 lignes retirées, 29 ajoutées), et les commentaires qui décrivaient encore les noyaux x86 sont corrigés. Aucun changement de comportement.

---

## 4.26.0 — 14 novembre 2026

### Performances

- Les modèles Gemma 4 dense écrivent leurs réponses sur le GPU : têtes d'attention de 256 et de 512 valeurs, couches qui partagent les clés et valeurs d'une couche antérieure, norme des valeurs, entrée propre à chaque couche, échelle de sortie et plafond des logits. Les prompts restent lus par le processeur. Sur Gemma 4 E2B, les 150 tokens écrits par le GPU après un journal de 2 622 tokens sont identiques à ceux du processeur calculant en nombres à virgule flottante. Vitesse à mesurer sur le Mac mini.

### Corrections

- Les noyaux GPU lisent les échelles des clés et valeurs sur 8 bits à leur place réelle quand une couche a des têtes plus étroites que celles que prévoit le cache (couches de 256 valeurs de Gemma 4 dans un cache prévu pour 512).

---

## 4.25.1 — 14 novembre 2026

### Moteur

- Les noyaux GPU des étapes d'écriture acceptent des têtes d'attention de 512 valeurs (écriture des clés et valeurs, attention, remise en place des sorties), première étape du passage de Gemma 4 au GPU. Gemma 4 reste sur le processeur tant que les autres étapes ne sont pas faites.

---

## 4.25.0 — 14 novembre 2026

Reprise à la main de trois nouveautés d'Albireo Community Edition 1.0.14, 1.0.15 et 1.0.17 (Cydral Technology). Avec les versions 4.23.0 et 4.24.0, elle termine la reprise des sept nouveautés retenues.

### Interface

- Une barre sous la ligne d'état indique la part du contexte du modèle qu'occupe la discussion affichée (tokens comptés par le service, sur le contexte du modèle), en orange à partir des trois quarts et en rouge à partir de 90 %. Elle est mesurée de nouveau quand la discussion, son modèle ou ses options changent.
- La fenêtre d'une discussion est le contexte du modèle, et non plus le contexte de travail calculé d'après la vitesse de lecture : les échanges les plus anciens sont résumés au-delà de la moitié de ce contexte. Le contexte de travail continue de régler la place des documents lus en entier, des extraits et du résumé.
- La zone de saisie, la modification d'une question, les instructions globales, les instructions d'un projet et la mémoire dessinent le markdown au fil de la frappe : gras, italique, barré, surligné (`==texte==`), code, titres, listes, citations et liens, leurs marques en gris clair. Le texte envoyé au modèle reste le markdown tapé. Entrée continue une liste, Tab et Maj+Tab décalent ses éléments, Cmd+B et Cmd+I mettent en gras ou en italique, Cmd+Z et Cmd+Maj+Z annulent et rétablissent.
- Les questions du fil sont dessinées de la même façon, et le surlignage `==texte==` est aussi rendu dans les réponses.

---

## 4.24.0 — 14 novembre 2026

Reprise à la main de trois nouveautés d'Albireo Community Edition 1.0.14 et 1.0.15 (Cydral Technology).

### Moteur

- Sans budget de raisonnement ni effort élevé, le raisonnement prend au plus la moitié des tokens de la réponse, 32 768 tokens au plus, pour que la réponse garde sa place après un raisonnement qui s'éternise. Un budget à -1 ou à `null` supprime cette limite, et l'effort `high` aussi.
- Les appels d'outils dont un modèle laisse les guillemets du code sans échappement, des caractères de contrôle bruts ou des barres obliques inverses isolées sont réparés : le moteur cherche la seule lecture qui donne des appels valides, d'après les paramètres que déclarent les outils de la requête, et laisse le texte tel quel quand aucune lecture, ou plus d'une, ne convient.

### Modèles

- Le manifeste des modèles livrés désigne Qwen3-VL 4B Instruct, qui répond sans phase de raisonnement, à la place de Qwen3-VL 4B Thinking, comme le catalogue de l'original, que lit le service, le fait déjà pour une installation neuve. Fichiers vérifiés auprès de Hugging Face (taille et SHA-256).

---

## 4.23.0 — 14 novembre 2026

Reprise à la main de deux nouveautés d'Albireo Community Edition 1.0.15 (Cydral Technology), publiées après la dernière fusion du fork.

### Agent

- L'agent n'exécute plus un appel d'outil qui répète un appel antérieur de la même réponse dont le résultat est encore lu, ni une recherche dont les mots reprennent ceux de deux recherches antérieures, ni plus de 8 recherches par outil. Après un second refus ou au-delà de ce budget, les outils s'arrêtent : l'étape suivante n'exécute plus d'appel, puis une dernière étape sans outils demande au modèle de répondre avec les résultats obtenus. Les appels non exécutés sont affichés comme tels, et une note sous la réponse indique pourquoi l'agent s'est arrêté.
- Les consignes des outils web précisent que Wikipédia contient peu d'actualité et qu'une même recherche reformulée retrouve les mêmes articles.

### Confidentialité

- « Supprimer toutes les discussions et tous les projets » supprime aussi la mémoire, le nom de l'utilisateur et les instructions globales, dans la même transaction, dans l'onglet qui supprime comme dans les autres. Une mise à jour de la mémoire en cours s'arrête, et une remise à zéro est datée pour qu'aucune mise à jour commencée avant ne réécrive l'ancienne mémoire.

### Corrections

- Le banc d'essai de la page attend que le modèle soit chargé avant de poser ses questions.

---

## 4.22.1 — 14 novembre 2026

### Corrections

- Le service lancé en arrière-plan par un script s'arrête désormais par SIGINT (Ctrl-C ou `kill -INT`) : un shell non interactif lance ses tâches avec ce signal ignoré, et macOS ne le transmettait pas. Même correction dans Pseudalis.
- `bench_runtime` ne lit plus au-delà de son prompt quand celui-ci compte moins de 64 tokens, et refuse un prompt vide ou zéro étape au lieu d'afficher un temps infini.
- Pseudalis : le lecteur indique le nombre de ses parcours et s'il est au repos (`scans`, `idle` de `GET /v1/drive`, lus à l'instant) ; son test attend ce repos au lieu de délais fixes, qui échouaient sur un Mac lent.
- Compilation sans avertissement d'Albireo, de Pseudalis et des exemples de dlib (deux variables jamais lues dans dlib, accolades superflues dans deux exemples, option `-s` de l'éditeur de liens, obsolète sur macOS, remplacée par `-x`).

---

## 4.22.0 — 14 novembre 2026

### Performances

- Les modèles Gemma 3 dont la largeur est un multiple de 256 (4B, 12B, 27B) passent au GPU, pour l'écriture comme pour la lecture des prompts : les noyaux d'attention du GPU tiennent compte de la fenêtre glissante des couches qui en ont une, chaque type de couche a sa table de positions rotatives, et deux noyaux nouveaux calculent la porte GELU et ajoutent les sorties normalisées. Le GPU donne les logits du processeur à 0,07 à 0,10 % près en demi-précision ; sur un prompt de 2 816 tokens, qui dépasse la fenêtre de 1 024 positions, la réponse est celle du processeur et de llama.cpp, et une réponse de 150 tokens est identique à celle du processeur. Le 1B (largeur 1 152) reste sur le processeur. Vitesse à mesurer sur le Mac mini.
- Gemma 4 reste sur le processeur.

---

## 4.21.0 — 14 novembre 2026

### Performances

- Les modèles LFM2-MoE passent au GPU : le routage du GPU sait choisir les experts par sigmoïde avec un biais de choix et laisser denses les premières couches. Le GPU donne les logits du processeur à 0,06 à 0,37 % près en demi-précision, sur des prompts de 29 à 1 200 tokens. Vitesse à mesurer sur le Mac mini (les premières mesures, en mode économie d'énergie, ne sont pas retenues).
- Sur un long prompt, LFM2.5-8B-A1B raisonne autrement sur le GPU que sur le processeur, comme llama.cpp sur son propre GPU, dont la pensée est la même : ce modèle est sensible aux différences d'arrondi entre les deux.

### Corrections

- Le contrôle du GPU au chargement admet 25 % d'écart pour un mélange d'experts avec les clés et valeurs sur 8 bits, le token le plus probable devant rester le même : une différence d'arrondi suffit à changer l'expert d'un token. LFM2.5-8B-A1B : 7,7 % et 13,6 % en 8 bits, 0,13 % et 0,11 % en demi-précision.

---

## 4.20.0 — 14 novembre 2026

### Performances

- Les modèles LFM2 sans mélange d'experts passent au GPU, pour l'écriture comme pour la lecture des prompts : un noyau Metal calcule la convolution courte et tient l'état de chaque conversation, que le GPU prend sans copie, comme pour Qwen 3.5 ; les longs prompts sont lus par morceaux aux points de reprise. LFM2.5-1.2B-Instruct Q8_0 sur MacBook Air M2, mesures alternées : lecture d'un prompt de 512 tokens de 287-296 à 433-462 tokens/s, écriture de 35-36 à 40 tokens/s ; sur un prompt de 2 623 tokens, réponse identique à celle du processeur et lecture à 420 tokens/s au lieu de 260.
- LFM2-MoE, Gemma 3 et Gemma 4 restent sur le processeur.

### Corrections

- Le contrôle du GPU au chargement admettait 5 % d'écart sur un prompt avec les clés et valeurs sur 8 bits, mais 1 % sur les étapes qui le suivent, alors qu'elles en héritent les différences d'arrondi ; les étapes reçoivent désormais la même limite dans ce cas (le token le plus probable doit toujours être le même). Avec LFM2.5-1.2B, l'écart était de 2,2 % en 8 bits et de 0,10 % en demi-précision.

---

## 4.19.0 — 14 novembre 2026

### Plateforme

- Le moteur lit les modèles Gemma 3 de Google (architecture `gemma3`), sur le processeur : une couche globale toutes les six, les autres à fenêtre glissante (512 positions pour le 1B, 1 024 pour le 4B), une base de positions rotatives propre à chaque type de couche et la mise à l'échelle linéaire sur les seules couches globales. Format de conversation de Gemma 3 : prompt identique à son gabarit sur cinq conversations. Le tokenizer donne les mêmes tokens que llama.cpp sur 22 textes de test. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton : avec gemma-3-1b-it et gemma-3-4b-it Q4_K_M, divergences seulement sur des quasi-égalités de la référence (par exemple 0,516 contre 0,484), et réponse identique sur un prompt de 2 816 tokens qui dépasse la fenêtre glissante. Vitesse sur MacBook Air M2, processeur : 1B, écriture de 25 à 28 tokens/s, lecture d'un prompt de 512 tokens à 396 à 436 tokens/s ; 4B, écriture de 16 à 17 tokens/s, lecture à 88 à 92 tokens/s.
- Les images de Gemma 3 (projecteur) et les versions de recherche (EmbeddingGemma) ne sont pas encore prises en charge. Le gabarit de Gemma 3 n'a pas d'outils.

### Accès et documents

- La recherche Hugging Face marque les dépôts Gemma 3 comme compatibles, sans GPU.

---

## 4.18.0 — 14 novembre 2026

### Plateforme

- Les modèles LFM2 appellent des outils (mode agent), dans leur notation propre : outils déclarés en JSON dans le message système (« List of tools: [...] »), appels écrits à la manière de Python entre `<|tool_call_start|>` et `<|tool_call_end|>` (`[get_weather(city='Paris', days=3)]`, valeurs `True`, `False`, `None`, listes et dictionnaires), résultats dans un tour `tool`. Le prompt suit le gabarit de chaque modèle à l'octet près : quatre conversations avec outils comparées à llama.cpp 0.9.0 pour LFM2.5-1.2B-Instruct, LFM2.5-8B-A1B et LFM2.5-Audio-1.5B, dont une qui contient des arguments de tous les types, et une cinquième (raisonnement puis appel) pour LFM2.5-8B-A1B. LFM2.5-1.2B-Instruct appelle l'outil météo, reçoit son résultat et répond comme llama.cpp, avec le même nombre de tokens de prompt aux deux tours ; LFM2.5-8B-A1B raisonne, appelle l'outil, puis donne la même réponse finale.
- La recherche Hugging Face et le catalogue annoncent de nouveau les outils pour LFM2.

---

## 4.17.1 — 14 novembre 2026

### Corrections

- Avec les clés et valeurs sur 8 bits (format « auto » des modèles de langue), les valeurs sont désormais tournées avant l'arrondi, comme les clés l'étaient déjà (signes pseudo-aléatoires puis transformée de Walsh-Hadamard), et la sortie de l'attention est remise en place ensuite, sur le processeur comme sur le GPU. llama.cpp procède de même. Sans cette rotation, LFM2.5-8B-A1B perdait un détail d'un prompt de 2 622 tokens ; il le retrouve désormais en 8 bits.
- LFM2 revient au format 8 bits par défaut, comme les autres modèles de langue (en 4.17.0, il restait en demi-précision faute de cette rotation).

### Performances

- Coût de la rotation non mesurable sur MacBook Air M2 : Qwen3-VL 2B Q4_K_M, quatre essais en alternance avec la version précédente, écriture sur le processeur de 29,1 à 32,9 tokens/s contre 27,5 à 34,7, lecture de 4 096 tokens sur le GPU de 288 à 297 tokens/s contre 281 à 300 (un essai perturbé de la version précédente écarté).

---

## 4.17.0 — 14 novembre 2026

### Plateforme

- Le moteur lit les modèles LFM2 et LFM2-MoE de Liquid AI (architectures `lfm2` et `lfm2moe`), sur le processeur : couches à convolution courte, dont l'état par conversation tient en deux positions, et couches d'attention ; pour LFM2-MoE, 32 experts dont 4 par token, routés par sigmoïde avec un biais de choix, après deux couches denses. La reprise d'une conversation depuis l'état gardé et les points de reprise toutes les 256 positions fonctionnent comme pour Qwen 3.5. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton : avec LFM2.5-1.2B-Instruct Q8_0, deux réponses sur quatre identiques (87 et 150 tokens), les deux autres jusqu'à une quasi-égalité de la référence (par exemple 0,26 contre 0,213), réponse identique sur un prompt de 2 623 tokens, et réponses identiques aux deuxième et troisième tours d'une conversation reprise depuis l'état gardé ; avec LFM2.5-8B-A1B Q4_K_M, raisonnement et réponse jusqu'à une quasi-égalité sur quatre questions. Vitesse sur MacBook Air M2, processeur : LFM2.5-1.2B Q8_0, écriture de 35 à 36 tokens/s, lecture d'un prompt de 512 tokens à 253 à 281 tokens/s ; LFM2.5-8B-A1B Q4_K_M, écriture de 34 tokens/s, lecture à 141 à 149 tokens/s.
- Format de conversation de LFM2 : prompt identique à son gabarit sur huit conversations, token de début de texte compris ; le raisonnement que le modèle ouvre lui-même (`<think>`) est séparé de la réponse. Les appels d'outils de LFM2 ne sont pas encore pris en charge. Seuls LFM2.5-350M, 1.2B-Instruct, Audio-1.5B (en texte) et 8B-A1B ont été essayés ; LFM2.5-24B-A2B ne l'a pas été.

### Réglages

- Avec le format de clés et valeurs « auto », les modèles LFM2 gardent leurs clés et valeurs en demi-précision : en 8 bits, LFM2.5-8B-A1B perdait un détail d'un prompt de 2 622 tokens. Le surcoût reste faible, ces modèles n'ayant que six couches d'attention (12 Ko par token).

### Accès et documents

- La recherche Hugging Face marque les dépôts LFM2 comme compatibles, sans GPU, et ne propose plus les fichiers annexes du modèle LFM2.5-Audio (tokenizer audio, vocoder). Le catalogue n'annonce pas d'outils pour LFM2. Vérifié avec un service d'essai : LFM2.5-1.2B-Instruct et LFM2.5-350M ajoutés depuis Hugging Face, installés et interrogés en français.

### Corrections

- L'ajout d'un modèle depuis Hugging Face sans le champ `projector` (facultatif) était refusé (« invalid repository or file name ») : la lecture de ce champ absent invalidait celle des deux autres.

---

## 4.16.0 — 14 novembre 2026

### Plateforme

- Les modèles Gemma 4 lisent les images, avec leur projecteur (type `gemma4v`) : encodeur de 16 couches dont chaque produit écrête son entrée et sa sortie aux bornes du fichier, positions des patchs par colonne et par ligne, regroupement des patchs par carrés de 3 × 3. Une image prend de 70 à 140, 280 ou 1 120 tokens selon le détail demandé (« low », « auto » ou « high »), son rapport d'aspect gardé. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton, avec Gemma 4 E2B Q4_K_M et son projecteur F16, sur quatre images (carrée, large, haute, grande) : même nombre de tokens de prompt dans les quatre cas ; une description identique mot pour mot, les trois autres jusqu'à une quasi-égalité de la référence (par exemple 0,477 contre 0,468). Encodage sur MacBook Air M2 : 0,8 s pour une image de 121 tokens, 2,5 à 2,8 s pour 273 tokens.
- La partie son du projecteur de Gemma 4 n'est pas lue.

### Accès et documents

- La recherche Hugging Face propose les projecteurs des dépôts Gemma 4 (« Avec la lecture des images »). Vérifié avec un service d'essai : Gemma 4 E2B et son projecteur ajoutés depuis Hugging Face, installés, puis une image décrite en français.

---

## 4.15.0 — 14 novembre 2026

### Plateforme

- Les modèles Gemma 4 appellent des outils (mode agent), dans leur notation propre : outils déclarés dans le tour système (`<|tool>declaration:…<tool|>`), appels `<|tool_call>call:nom{…}<tool_call|>`, résultats `<|tool_response>…<tool_response|>`, chaînes entre `<|"|>`. Le prompt suit le gabarit du modèle à l'octet près : huit conversations comparées à Jinja2 3.1.6, dont six aussi à llama.cpp 0.9.0 (une conversation que llama.cpp refuse, une autre dont il retire la description de la réponse d'un outil). Gemma 4 E2B appelle l'outil, reçoit son résultat et s'en sert dans sa réponse, identique à celle de llama.cpp en choix glouton.
- Quand le raisonnement est demandé, la pensée qui précède un appel d'outil est gardée dans les tours suivants, comme le fait llama.cpp ; le début du prompt reste ainsi le même d'une requête à l'autre et reste lu depuis le cache. Après les résultats des outils, le prompt rouvre le raisonnement, comme le gabarit, et la réponse en est séparée.

---

## 4.14.0 — 13 novembre 2026

### Plateforme

- Le moteur lit les modèles Gemma 4 de Google sans mélange d'experts (architecture `gemma4`), sur le processeur : couches à fenêtre glissante de 512 positions et couches globales, avec des têtes d'attention de 256 et de 512 valeurs et deux bases de positions rotatives ; clés et valeurs des 20 dernières couches reprises de couches précédentes ; entrées propres à chaque couche ; logits plafonnés. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton, avec Gemma 4 E2B Q4_K_M : même premier token ; sur trois questions de 150 tokens, divergences seulement sur des quasi-égalités de la référence (par exemple 0,571 contre 0,427) ; sur un prompt de 2 210 tokens, qui dépasse la fenêtre glissante, réponse identique. Gemma 4 E2B Q4_K_M sur MacBook Air M2, processeur : écriture de 21 à 24 tokens/s, lecture d'un prompt de 512 tokens à 143 à 153 tokens/s.
- Le tokenizer de Gemma 4 (fusions BPE classées sur un texte où les espaces deviennent « ▁ ») donne les mêmes tokens que llama.cpp sur 22 textes de test.
- Format de conversation de Gemma 4 : prompt identique à son gabarit sur six conversations ; le raisonnement, quand il est demandé, est séparé de la réponse. Les appels d'outils et les images de Gemma 4 ne sont pas encore pris en charge.
- Seul Gemma 4 E2B a été essayé. Les versions à mélange d'experts (26B-A4B) sont refusées au chargement.

### Accès et documents

- La recherche Hugging Face marque les dépôts Gemma 4 comme compatibles, sans GPU, et les versions à mélange d'experts comme non prises en charge ; les petits modèles de brouillon (`mtp-…`) des dépôts ne sont plus proposés comme modèles.

---

## 4.13.2 — 13 novembre 2026

### Corrections

- La fiche du réglage « GPU : prompts jusqu'à » cite les mesures alternées de la version 4.11.0 (16 384 tokens : 171 tokens/s sur le GPU contre 67 sur le processeur) au lieu de mesures isolées antérieures.

---

## 4.13.1 — 13 novembre 2026

### Corrections

- Modèles Qwen 3.5 et 3.6 : quand le GPU échoue au milieu d'un long prompt, lu en plusieurs morceaux, l'état des couches linéaires et la longueur de la conversation sont rendus tels qu'avant le prompt, que le processeur relit alors en entier. Auparavant, la requête échouait (« sequence storage was not reserved »). Vérifié par une panne simulée au deuxième morceau : même réponse que sur le processeur seul.

---

## 4.13.0 — 13 novembre 2026

### Plateforme

- Les modèles Qwen 3.5 lisent les images, avec leur projecteur (du même type que celui de Qwen3-VL) et les mêmes positions rotatives entrelacées. Vérification par rapport à llama.cpp 0.9.0, sur le processeur et en choix glouton, avec Qwen3.5-4B Q4_K_M et son projecteur F16 : la description d'un perroquet est identique mot pour mot, celle d'un zèbre jusqu'à une quasi-égalité (« grass » contre « grasses ») ; même nombre de tokens de prompt (277, dont 256 pour l'image). Encodage d'une image de 512 × 512 points : 1,7 s sur un MacBook Air M2.
- La recherche Hugging Face propose le projecteur des dépôts Qwen 3.5 (Q8_0, sinon F16). Un modèle installé avant cette version n'a pas de projecteur. Les projecteurs de Qwen 3.6 à mélange d'experts, pas encore essayés, ne sont pas proposés.

### Corrections

- Les positions rotatives que le moteur donne pour chaque token (modèles qui lisent les images) sont transmises à chaque morceau d'un prompt coupé aux points de reprise, pour les modèles Qwen 3.5 et 3.6.

---

## 4.12.1 — 13 novembre 2026

### Corrections

- Les modèles Qwen 3.5 et 3.6 appellent de nouveau leurs outils (mode agent) : leurs appels s'écrivent sous forme XML (`<tool_call>`, `<function=nom>`, `<parameter=clé>`), que le moteur ne lisait pas pour ce format de conversation. Le prompt avec outils suit désormais leur gabarit à l'octet près (vérifié sur trois conversations, dont deux appels dans une réponse) ; Qwen3.5-4B appelle l'outil, reçoit son résultat et s'en sert dans sa réponse.

---

## 4.12.0 — 13 novembre 2026

### Performances

- Les prompts des modèles Qwen 3.5 et 3.6 à mélange d'experts (`qwen35moe`) sont lus sur le GPU. Au lieu d'un produit par token et par expert, chaque expert multiplie d'un coup tous les tokens qui l'ont choisi : produits groupés, poids développés dans la mémoire du GPU par tuiles de 32 lignes sur 64 colonnes, produits de matrices 8 × 8. Qwen 3.6 11B-A3B Q4_K_M sur MacBook Air M2, mesures alternées : prompt de 512 tokens 107 à 112 → 196 à 198 tokens/s, de 2 048 tokens 121 à 122 → 175 à 176 tokens/s. Prompt de 4 005 tokens, même réponse que sur le processeur : 150 contre 94 tokens/s. Contrôle au chargement : logits à 0,14 % de ceux du processeur.
- Les autres mélanges d'experts (Qwen3 MoE, Mixtral, Granite MoE), jamais essayés sur le GPU, gardent leurs prompts sur le processeur.

---

## 4.11.12 — 13 novembre 2026

### Corrections

- La page « Composants et licences » liste de nouveau les programmes du service (Albireo, dlib, Mbed TLS, cpp-httplib, zlib, SQLite, libpng, libjpeg) avec le texte de leur licence : le build copie ces textes dans le dossier `licenses` à côté du service. Le texte de la licence d'Albireo, retiré par erreur avec les fichiers propres à Windows, est rétabli, sans la liste des composants Windows (OpenVINO, CUDA, Visual C++).
- Sur un écran étroit (860 points ou moins, un téléphone par exemple), le panneau latéral est fermé à l'ouverture de la page, au lieu de la recouvrir, et se referme une fois une discussion ou une page choisie.

---

## 4.11.11 — 12 novembre 2026

### Interface

- Fin de la réorganisation du code de la page, sans changement visible : les réglages d'affichage (`preferences.js`), la navigation (`navigation.js`) et la mise en page (`layout.js`) quittent `app.js`, qui ne garde que le démarrage (156 lignes au lieu de plus de 7 000) et la carte des 22 modules de la page.

---

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
