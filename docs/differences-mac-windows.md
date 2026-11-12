# Ce que le fork Mac a changé par rapport à l'original Windows, et pourquoi

Un tableau par thème. Chaque ligne dit ce que fait l'original (Albireo et Pseudalis pour Windows, de Cydral), ce que fait le fork sur Mac, pourquoi, et le gain mesuré quand il y en a un. Les mesures viennent du MacBook Air M2 (8 cœurs, 24 Go), en mode économie d'énergie coupé et en mesures alternées (règle de CLAUDE.md).

Le détail de chaque changement se trouve ailleurs :
- les fiches des changements de vitesse, avec le code avant et après, dans [notes-pour-l-auteur.md](notes-pour-l-auteur.md), partie 4 (« ch. N » ci-dessous) ;
- la liste par version dans les [notes de version d'Albireo](../dlib/dlib/external/albireo/web/changelog.md) (« v. x.y.z ») ;
- l'historique dans le [journal](../logs/journal-modifications.md).

Base de comparaison : l'original au commit `184ea00` (Albireo 1.0.12, Pseudalis 1.0.5), dernière fusion. Dernière mise à jour de ce tableau : Albireo 4.12.0, 13 novembre 2026.

## 1. Vitesse du moteur

Modèles mesurés : Qwen3-VL 2B Instruct (« 2B ») et Qwen3-VL 4B Thinking (« 4B », le modèle du service).

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré sur le M2 | Où |
|---|---|---|---|---|---|
| Produits des poids Q4_K et Q6_K pendant l'écriture | Noyaux AVX2 (x86) ; sur Mac, code portable seulement | Noyaux NEON de la puce Apple, exacts au bit près | Les noyaux rapides de l'original n'existent que pour les processeurs Intel et AMD | 2B, écriture : 11 → 33 à 37 tokens/s | ch. 1 |
| Produit en flottants des prompts | Code portable | NEON, exact au bit près | Même raison | 2B, prompt : 5 à 13 → 35 à 50 tokens/s | ch. 2 |
| Attention (relire le contexte) | Code portable | NEON, exacte au bit près | Sur un long texte, l'attention prenait les deux tiers du temps | 2B, 4 096 tokens : prompt 25 → 51, écriture 8 → 21 tokens/s | ch. 3 |
| Produits des prompts | Noyaux entiers du processeur | Framework Accelerate d'Apple (coprocesseur matriciel), à partir de 16 tokens | Le Mac a un coprocesseur fait pour ces produits | 2B, 512 tokens : 64 à 72 → 217 tokens/s ; 4 096 tokens : 52 à 58 → 156 à 163 | ch. 4 |
| Attention de l'encodeur d'images | Code portable | Accelerate | Lire une image prenait 7 à 8 s | Image 512 × 512 : 6,7 à 8,5 s → 1,8 à 2,2 s | ch. 5 |
| Sommes dans les noyaux Q4_K et Q6_K | — | Gardées en registres vectoriels | Moins d'allers-retours en mémoire | 4B, écriture : 16,5 à 17,0 → 18,0 à 18,1 tokens/s | ch. 6 |
| Attention avec clés et valeurs en 8 bits (Albireo 1.0.12) | Code portable | NEON, exacte au bit près | Sans elle, la fusion de la 1.0.12 rendait les longs prompts 4 fois plus lents sur Mac | 2B, 4 096 tokens : 37 → 158 à 162 tokens/s ; écriture ensuite : 9 → 22,5 | ch. 7 |
| Threads de l'écriture | Tous les cœurs | Cœurs de performance seuls pour l'écriture, tous les cœurs pour les prompts | Les cœurs lents faisaient attendre les rapides | 4B : 17,6 à 18,1 → 18,2 à 18,6 tokens/s ; 2B : 37,4 à 37,9 → 39,7 à 40,8 | ch. 8 |
| Écriture des réponses | CUDA (cartes NVIDIA) ou processeur | GPU de la puce par Metal, sans copie des poids (mémoire unifiée) | Le Mac n'a ni CUDA ni NVIDIA ; son GPU partage la mémoire | 4B : 18,3 à 19,0 → 21,3 à 21,7 tokens/s ; 2B : 35,7 à 40,8 → 49,4 à 49,6 | ch. 9, v. 2.0.0 |
| Lecture des prompts | CUDA ou processeur | GPU par Metal, de 24 à 2 048 positions | Même raison | 4B, 512 tokens : 92 → 154 tokens/s ; 2B : 222 → 420 | ch. 10, v. 2.0.0 |
| Produits des poids Q5_K et Q8_0 sur le GPU | — (pas de GPU Apple) | Noyaux Metal `mv_q5_K`, `mv_q8_0` | Les fichiers Qwen 3.5 en Q4_K_M en contiennent | permettent l'écriture de Qwen 3.5 sur le GPU | ch. 12, v. 4.5.0 |
| Mélange d'experts | Sur le processeur (et CUDA) ; pas d'expert partagé | Sur le GPU aussi (routeur, choix des experts, produits par indirection) ; expert partagé de Qwen 3.6 ; format Q5_0 sur le GPU ; prompts de Qwen 3.5/3.6 MoE par produits groupés, chaque expert multipliant d'un coup les tokens qui l'ont choisi | Les modèles à experts (Qwen3.6-35B-A3B) visent le Mac mini de 64 Go | Qwen 3.6 11B-A3B : écriture 24,4-25,0 → 26,1-26,3 tokens/s ; lecture de 512 tokens 107-112 → 196-198 (M2) | ch. 12, 15, v. 4.6.0, 4.7.0, 4.12.0 |
| Reprise d'une conversation Qwen 3.5 au milieu | — (architecture non lue) | Points de reprise de l'état toutes les 256 positions | L'état d'une couche linéaire ne revient pas en arrière ; sans eux, tout serait relu | prompt système de 794 tokens partagé : 768 tokens repris | v. 4.8.0 |
| Attention des prompts sur le GPU | CUDA (cartes NVIDIA) | Noyau Metal en tuiles, scores et somme par produits de matrices 8 × 8 (`simdgroup_matrix`), clés chargées huit valeurs à la fois ; prompts lus par le GPU jusqu'à 262 144 positions | L'attention précédente relisait toutes les clés pour chaque token et perdait au-delà de 2 048 positions ; la première version en tuiles, au-delà de 4 096 | 2B, 4 096 tokens : 145 → 198 (v. 4.10.0) → 423 tokens/s ; 8 192 tokens : 295 contre 115 à 117 sur le processeur | ch. 13, 14, v. 4.10.0, 4.11.0 |
| Contrôle au chargement | — | Le GPU est comparé au processeur à chaque chargement ; retour au processeur s'il diffère | Le GPU n'est pas exact au bit près ; on vérifie qu'il donne le même mot | logits à 0,1 à 0,2 % du processeur, même token | ch. 9, 10 |

## 2. Modèles : téléchargement, vérification, réserve

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Gain mesuré | Où |
|---|---|---|---|---|---|
| Empreinte SHA-256 des modèles | mbedtls, en logiciel | CommonCrypto d'Apple, avec les instructions SHA de la puce | Chaque téléchargement, import et restauration relit tout le fichier | 285 → 2 100 Mo/s (8,7 → 1,2 s pour 2,5 Go) | ch. 11, v. 4.2.0 |
| Copie d'un modèle (réserve, restauration, import) | Copie octet par octet | Clone APFS (`fclonefileat`), puis relu et haché | Un clone est instantané et ne prend pas de place | Restaurer le 4B : 23 → 6,1 à 6,8 s ; place prise : 2,8 Go → 0 | ch. 11, v. 4.2.0 |
| Signature du catalogue | Catalogue signé, vérifié par une clé publique | Plus aucune clé ni signature | Demande de l'humain (« tout faire sans clé ») ; le fork ne publie rien | — | v. 4.0.0 |
| Espace de téléchargement | Lien de l'auteur protégé par mot de passe, puis Hugging Face | Directement à l'adresse du catalogue | L'espace répondait « protégé par mot de passe » | 16 s gagnées avant chaque téléchargement | v. 4.0.0 |
| Réserve de modèles | — | Chaque modèle gardé (clone) ; restauration sans réseau, fichier par fichier ; plafond de 200 Go | Ne pas dépendre de Hugging Face ; réinstaller sans retélécharger | Restauration du 4B en 6 s sans réseau | v. 4.0.4 à 4.3.0 |
| Choix des modèles | Catalogue de l'auteur | Catalogue, plus tous les GGUF de Hugging Face (recherche, liste personnelle), gardés pour le hors-ligne | Utiliser d'autres modèles que ceux du catalogue | — | v. 3.2.0, 4.3.0 |
| Architectures lues par le moteur | llama, mistral, mistral3, granite, qwen2, qwen3, qwen3vl, mixtral, granitemoe, qwen3moe, qwen3vlmoe, phi3 | Les mêmes, plus **Qwen 3.5** (`qwen35`, couches Gated DeltaNet à état récurrent ; écriture et lecture des prompts sur le GPU) et **Qwen 3.5 / 3.6 MoE** (`qwen35moe`, avec expert partagé ; écriture sur le GPU) | Qwen 3.5 puis Qwen 3.6 MoE sont les architectures GGUF les plus téléchargées que le moteur refusait (58,8 et 18,8 millions de téléchargements) ; avec 64 Go, le 35B-A3B devient utilisable | Qwen3.5-4B Q4_K_M : écriture 19,5 tokens/s sur le GPU (11,7 à 12,2 sur le processeur), lecture 148 à 149 tokens/s (86) ; mêmes 150 tokens que llama.cpp | v. 4.4.0 à 4.6.0, ch. 12 |

## 3. Système et sécurité

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Où |
|---|---|---|---|---|
| Plateformes | Windows (et une variante Linux en console) ; x86, CUDA, OpenVINO | macOS sur puce Apple seulement | But du fork (décision du 2026-11-11, « du pur Apple ») | v. 2.0.0 |
| Chemin du programme | `/proc/self/exe` (Linux) ou API Windows | `_NSGetExecutablePath` | macOS n'a pas de `/proc` : sans ça, ni moteur ni page n'étaient trouvés | v. 1.0.11, notes partie 2 |
| Mémoire et nom du processeur | API Windows ou `/proc` | `sysctl` | Même raison | v. 1.0.11 |
| Contrôle des dossiers (propriétaire, droits) | Vérifié sous Windows ; **toujours « oui »** sous Linux et macOS | Propriétaire, droits POSIX et ACL de macOS vérifiés | Sans ça, un dossier ouvert à tous aurait été accepté | v. 4.0.2, 4.1.1, notes remarque 21 |
| Jeton d'administration, contrôle `Host` et `Origin` | Présents | Retirés | Usage personnel sur un seul Mac, choix de l'humain ; risque écrit dans les notes de version (une page piégée pourrait piloter le service) | v. 2.0.0 |
| Accès depuis le réseau de la maison | — | Port 8444 en http, mot de passe facultatif | Ouvrir Albireo depuis le téléphone ou l'iPad | v. 2.0.0 |
| Installateur, icône de la barre des tâches, service Windows, mises à jour signées | Présents | Retirés | Propres à Windows | v. 2.0.0, 4.0.0 |
| Fichiers créés par l'agent | Une extension exécutable par Windows (`.exe`, `.bat`…) reçoit `.txt` en plus | Aussi celles que macOS exécute ou ouvre comme un programme (`.command`, `.app`, `.pkg`, `.sh`, `.scpt`, `.webloc`…) | La page s'ouvre sur le Mac et depuis les autres appareils de la maison | v. 4.11.1 |

## 4. Interface et usage

| Ce qui change | Original (Windows) | Fork (Mac) | Pourquoi | Où |
|---|---|---|---|---|
| Langues | Anglais, français, allemand, italien, espagnol | Français seulement (Pseudalis : anglais et français) | Décision de l'humain | v. 2.0.0 |
| Lecture des réponses à voix haute | Présente (SVOX Pico) | Retirée | « Ça alourdit le programme et ne sert pas » (l'humain) | v. 3.0.0 |
| Réglages du service | Fichier `settings.json` et icône Windows | Onglet « Service » : chaque réglage avec sa fiche (normale, hibou, marmotte, abeille) et ses valeurs conseillées mesurées | Tout régler depuis la page, y compris depuis le téléphone | v. 2.0.0 |
| Choix du modèle | Sélecteur de fonction (Général, Code, Cybersécurité) | Onglet « Modèles » : un modèle pour tout, avec un avis calculé pour ce Mac | Plus simple ; l'avis tient compte de la mémoire du Mac | v. 3.1.0 |
| Documents | Pièces jointes | Plus un dossier de documents sur le Mac, où chaque pièce jointe est copiée | Retrouver ses documents d'un appareil à l'autre | v. 2.0.0 |
| Code de la page | Un fichier `app.js` de plus de 7 000 lignes | 22 modules (`app.js` : 156 lignes), sans changement visible ; banc d'essai `tools/page_test` (ESLint et 106 vérifications dans Chrome sans fenêtre) | Lire, corriger et vérifier la page sans tout parcourir | v. 4.11.2 à 4.11.11 |
| Numéro de version | Celui de l'original (1.0.x) | Numérotation propre au fork, à partir de 2.0.0 | Le programme n'a plus grand-chose de la version Windows | v. 2.0.0 |

## Ce qui reste comme dans l'original

- La bibliothèque dlib, générique : elle n'est pas nettoyée.
- La génération pour les formats de poids autres que Q4_K et Q6_K (Q8_0, Q5_K…), en code portable sur le processeur (le GPU les lit pour Qwen 3.5 et les mélanges d'experts).
- Les autres architectures de modèles (Gemma 4… : chantier B du fork).

---

*Tenu à jour à chaque changement qui distingue le fork de l'original (règle de CLAUDE.md). Aucun chemin privé, aucune clé, aucune adresse e-mail.*
