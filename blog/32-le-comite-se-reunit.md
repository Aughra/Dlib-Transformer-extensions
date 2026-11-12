# Dlib Transformer Extensions sur macOS, épisode 32 : le comité se réunit

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Qwen 3.6 11B-A3B (mélange d'experts) : écriture **26 tokens/s**, lecture **196 à 198 tokens/s** sur 512 tokens, contre 107 à 112 sur le processeur.

Suite de [l'épisode 31](31-le-grand-rangement.md) : la page était rangée. On est revenu au moteur, pour une pièce qui manquait depuis [l'épisode 28](28-le-comite-d-experts.md) : la lecture des prompts des mélanges d'experts sur le GPU.

## Un comité qui recevait les dossiers un par un

Un mélange d'experts, on l'a vu, ressemble à un comité : pour chaque mot, un aiguilleur choisit 8 experts parmi 256. Sur le GPU, chaque expert donnait son avis mot par mot, comme un comité qui recevrait les dossiers un à un et rouvrirait ses classeurs à chaque fois. Pour écrire une réponse, un mot après l'autre, cela suffit. Pour lire un document de plusieurs milliers de mots, c'était trop lent, et le processeur gardait ce travail.

## Trier le courrier d'abord

La solution est celle d'un secrétariat bien tenu. Avant la séance, on trie le courrier : chaque expert reçoit la pile de tous les mots qui l'ont choisi. Ensuite, chaque expert ouvre ses classeurs une seule fois et traite toute sa pile d'un coup, seize mots à la fois.

Trois petits programmes du GPU font ce travail :

- le premier trie les mots par expert ;
- le deuxième découpe chaque pile en paquets de seize et prépare lui-même la suite des opérations, sans repasser par le processeur ;
- le troisième fait les calculs : il déplie les poids compressés de l'expert dans la mémoire rapide du GPU et les multiplie par les seize mots, en petits tableaux de 8 sur 8, la même « table de multiplication » qu'à [l'épisode 30](30-la-table-de-multiplication.md).

Le reste n'a pas bougé : la façon de peser et d'additionner les avis, et l'expert permanent de Qwen 3.6.

## Vérifié à trois niveaux

- **Pièce par pièce** : un test compare le calcul du GPU à celui du processeur pour les cinq formats de poids que le modèle emploie. L'écart est de deux dix-millionièmes.
- **Au chargement** : le moteur fait lire un court prompt aux deux et compare leurs prédictions : 0,14 % d'écart, et le même mot choisi.
- **Sur une vraie question** : un règlement d'hôtel de 120 articles, environ 4 000 tokens, et la question « que dit la règle 77, et quel registre utilise-t-elle ? ». Le GPU et le processeur répondent le même texte, mot pour mot, et juste : « verify the room numbers without numbers », registre 259.

## Ce que l'on gagne

Sur le M2, la lecture va 1,5 à 1,8 fois plus vite. C'est moins que pour les modèles ordinaires, et c'est attendu : le processeur du M2 s'en tire bien avec un mélange d'experts, et le GPU du MacBook Air est petit. Le vrai destinataire est le Mac mini, dont le GPU est bien plus large, et le Qwen3.6-35B-A3B qu'il pourra charger : lire un long document avec ce modèle était la partie lente.

## Pour les curieux : les chiffres

Qwen 3.6 11B-A3B Q4_K_M, `bench_runtime`, 8 threads, clés et valeurs en 8 bits, mode économie d'énergie coupé, charge 3,9 à 5,2, processeur et GPU en alternance :

| Prompt | Processeur | GPU |
|---|---|---|
| 512 tokens | 111,7 et 107,0 tokens/s | 195,6 et 197,6 tokens/s |
| 2 048 tokens | 122,1 et 120,8 tokens/s | 175,3 et 175,8 tokens/s |
| 4 005 tokens (`engine_chat`, une mesure) | 93,6 tokens/s | 149,9 tokens/s |

Une première mesure du processeur avait donné 59 tokens/s sur 512 tokens : le fichier de 7 Go n'était pas encore dans la mémoire du Mac. Les mesures alternées l'ont corrigée.

Les noyaux s'appellent `moe_group`, `moe_tiles` et `mm_id_q4_K` (et ses variantes Q5_K, Q6_K, Q8_0, Q5_0). Une tuile fait 16 cases sur 32 lignes de poids, lues 64 colonnes à la fois ; les nombres de tuiles sont écrits par le GPU lui-même et lus par un lancement indirect.

## À suivre

Les mesures reprendront sur le Mac mini, avec le grand modèle à 256 experts.
