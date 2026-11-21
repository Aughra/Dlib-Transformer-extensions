# Décisions du 2026-11-20 (en autonomie)

Consigne de l'humain, le matin : « ARRÊT les tests de perfs, et continue la roadmap ». Ce fichier dit ce que j'ai décidé seul, et pourquoi. Le plus récent est en haut.

## 10. Bibliothèques : PDFium gardé, TLS d'Apple choisi par l'humain

- **PDFium gardé, PDFKit écarté** (point 4a ; règle convenue avec l'humain : ne changer que si le texte est aussi bon). Sur 11 PDF (article à deux colonnes, document du fork, remerciements de Xcode, licence de macOS en français, espagnol, grec, hébreu, arabe, japonais, chinois, coréen), PDFKit donne 96,5 à 100 % des mots de PDFium, mais **perd des lettres doublées** sur la licence japonaise (« Hollyhill » → « Holyhil », « Specially » → « Specialy », « iii » → « i »), rend l'arabe en formes de présentation (à normaliser en NFKC) et y coupe des mots. PDFium mélange l'ordre des chiffres et des parenthèses en arabe, défaut moindre.
- **Mbed TLS remplacé par le TLS d'Apple, serveur compris** : question posée à l'humain (garder Mbed TLS, recommandé ; clients Apple et serveur en HTTP local ; tout par Apple) ; il a choisi « tout par Apple ». Architecture retenue : cpp-httplib ne parle plus que HTTP en clair, Network.framework fait tout le TLS autour de lui (un portier qui termine le TLS du serveur et passe le HTTP par une prise Unix interne ; un pont qui ouvre le TLS des clients vers l'adresse épinglée avec le nom du serveur). Pourquoi : le code des routes et des clients (flux, découpage, en-têtes) ne change pas, et l'épinglage d'adresse contre le détournement DNS se garde.

## 9. Mistral 3 : un modèle téléchargé, YaRN de llama.cpp, la fin de R1

- **Ministral 3 3B téléchargé** (Q8_0, 3,7 Go, dépôt officiel `mistralai/Ministral-3-3B-Instruct-2512-GGUF`) : le seul Mistral du Mac, Mistral Small 3.2 24B, est déclaré `llama` (famille déjà connue) et ferait 96 Go en float32, plus que les 64 Go du Mac mini ; Ministral 3 porte l'architecture `mistral3` et ses deux nouveautés.
- **YaRN comme llama.cpp, dans les tables de la rotation**, à côté de la variante propre à dlib (`set_yarn_params`, réglée sur la longueur, que l'import coupe) plutôt qu'à sa place : on ne change pas un comportement que d'autres programmes peuvent utiliser. La passe arrière reste juste sans changement (la rotation est linéaire).
- **La note YaRN de l'original corrigée et mesurée** (notes n° 30) : on a coupé YaRN le temps d'une mesure, puis remis le code (vérifié : plus de trace de la coupure).
- **La température d'attention complète** bien qu'elle n'agisse qu'au-delà de 16 384 positions : sans elle, le réseau serait faux pour les longs textes, et son test le montre sur des tranches de 3 positions.
- **La mesure longue en mode normal seulement** : le mode exact de `test_runtime` passait 69 minutes de calcul sur 1 561 tokens avec un vocabulaire de 131 072, sur des essais du moteur seul qui ne jugent pas l'import ; la comparaison au réseau dlib se fait aussi en mode normal.
- **Les notes de version de 6.10.1 complétées** plutôt qu'une version nouvelle : Qwen 3.5 et Mistral 3 ne changent que dlib, et la règle n'incrémente Gulliver que quand Gulliver change.

## 8. Qwen 3.5 dans dlib : cinq choix

- **La règle delta en une couche à elle** (`gated_delta_net_`), entre deux couches linéaires, comme la convolution de LFM2 : les quatre projections d'entrée (attn_qkv, attn_gate, ssm_alpha, ssm_beta) sont fusionnées en une seule à l'import, ce qui garde un seul `linear_no_bias` que LoRA et l'export savent traiter.
- **La passe arrière recalcule les états par morceaux de 64 tokens** au lieu de les garder tous : chaque état fait head_dim² valeurs par tête (16 384 pour le 0.8B), soit 512 Mo par couche pour 512 tokens ; les points de départ de chaque morceau suffisent, pour un calcul refait une fois.
- **Le test de la dérivée contre une référence en double**, pas `test_layer` : ses différences finies en float donnaient 5 % d'écart sur cette couche, qui fondaient quand le pas diminuait (12 %, 1 %, 0,5 %) ; une référence écrite à part, en double, donne 1e-4 et vérifie en plus la passe avant.
- **La rotation partielle par des fréquences nulles** plutôt qu'une rotation tronquée : les paires au-delà des 64 dimensions gardent un angle nul, ce que les passes avant, incrémentale et arrière de la couche font déjà sans changement ; seules les tables de cosinus et la disposition des lignes à l'import changent.
- **La porte de sortie dans `gqa_attention_`**, rangée après les biais et réglée avant la mise en place comme eux : elle multiplie les têtes avant W_O, à l'intérieur de la couche fusionnée, qu'une couche à part ne pouvait pas atteindre. Gulliver ne change pas : sa version reste 6.10.1.

## 7. LFM2 avant Qwen 3.5, et la forme de la pile mixte

- **LFM2 passe avant Qwen 3.5** (l'ordre de la feuille de route était Gemma 3, Qwen 3.5, LFM2, Mistral 3) : les deux mêlent des blocs d'attention à des couches linéaires, et la pile qui les mêle est la même. LFM2 la pose avec une couche neuve simple (une convolution de 3 poids par canal, dérivée en quelques lignes) ; Qwen 3.5 demande en plus la règle delta entraînable (dérivée à travers toute la séquence) et une attention à porte avec rotation partielle. On sépare ainsi la mise au point de la pile de celle de la couche difficile.
- **Le motif des couches dans le type, en masque de bits** (`ATTENTION_MASK`, 64 couches au plus) : un réseau dlib fixe sa forme à la compilation, et l'import écrit déjà un en-tête propre à chaque modèle ; le motif de LFM2 est irrégulier (2, 5, 8, 10, 12, 14), donc ni « une sur quatre » ni un compte ne suffisent. 64 couches couvrent les modèles visés (LFM2 16, Qwen 3.5 de 24 à 64).
- **La convolution entre deux couches linéaires de dlib** plutôt qu'une couche qui porte les trois matrices : in_proj et out_proj restent des `linear_no_bias`, que le chargeur, LoRA et l'export savent déjà traiter ; la couche neuve ne garde que ses poids de convolution.
- **Un état gardé pour l'écriture token par token**, comme le cache de l'attention : sans lui, une génération dans dlib serait fausse après le premier token. Le décalage du cache des longues générations n'a pas d'équivalent (l'état est une fenêtre glissante), c'est écrit dans la notice.
- **`test_runtime` corrigé plutôt que contourné** : il plantait sur tout modèle récurrent en voulant revenir au milieu d'une séquence, ce que le moteur refuse à juste titre ; la troncature repart de 0 (le seul retour permis sans point de reprise), l'essai du préfixe partagé est sauté avec un message.
- **`transpose_into` contrôlé dans la fonction elle-même** (entrée n° 29 des notes) plutôt que seulement dans le bloc neuf : le défaut est une lecture hors du vecteur, possible pour tout appel ; `copy_into`, qui remplit de zéros, est laissée telle quelle faute de savoir si c'est voulu.

## 6. La fenêtre glissante : un champ toujours écrit

- **Quoi** : la fenêtre de `gqa_attention_` est écrite à la fin de chaque archive de la couche, qu'elle vaille 0 ou non.
- **Pourquoi** : le commentaire de la couche pose la règle (une seule disposition, pas d'étiquette de version ni de champ écrit pour certaines variantes seulement, on réentraîne quand elle change), parce qu'un champ conditionnel a déjà cassé toutes ses archives. Les archives de cette couche écrites avant ne se relisent plus ; le fork n'en a aucune. Pour l'échelle des positions de RoPE (6.8.3), dans une autre classe sans cette règle, j'avais gardé l'ancien format lisible.
- **Comment vérifier** : un test de dlib compare la passe complète et le décodage token par token avec une fenêtre de 3, puis Gemma 3 1B au-delà de sa fenêtre de 512 contre le moteur de Gulliver.

## 5. Gemma 3 dans dlib : six choix

- **GELU tanh, couche nouvelle** plutôt que la GELU exacte de dlib : Gemma a été entraîné avec la version tanh, et Gulliver l'utilise ; la GELU exacte aurait été une erreur systématique dans chaque couche.
- **√d dans la table des embeddings** plutôt qu'une couche d'échelle : la sortie est un tenseur à part dans le réseau de dlib, donc elle n'est pas touchée ; aucune couche de plus à sérialiser. À retenir pour l'export (R6) : diviser la table par √d.
- **Un troisième placement des normes (`gemma`)** plutôt qu'un nouvel empilement : le constructeur de la pile choisit déjà ses blocs par le placement, c'était l'endroit naturel.
- **La règle « une couche globale sur six, base locale 10 000 » écrite dans dlib**, comme llama.cpp, faute de la trouver dans le GGUF (le 1B et le 4B ne la stockent pas) ; elle se lit de la clé `rope.freq_base_swa` si un fichier la porte.
- **L'échelle linéaire du 4B tout de suite**, sur les couches globales seulement : sans elle le 4B restait refusé, et c'est le modèle qu'on affinera plutôt que le 1B.
- **La fenêtre glissante plus tard**, dans une sous-étape à elle : elle touche les passes avant et arrière de la couche d'attention fusionnée (environ 900 lignes) ; sans elle le réseau est juste jusqu'à 512 tokens (1B) ou 1 024 (4B), et l'import le dit en note.

## 4. R1 commence par vérifier l'import de dlib, avec un petit Qwen3

- **Quoi** : avant d'écrire du neuf, faire tourner ce que dlib a déjà (`slm_gguf_import_ex`) sur un petit modèle de la famille la mieux couverte, Qwen3-0.6B (640 Mo, téléchargé chez Qwen), et comparer son réseau au moteur de Gulliver.
- **Pourquoi** : la feuille de route disait « relevé par une recherche dans le code, pas encore vérifié en le lançant ». Bien m'en a pris : l'import s'arrêtait pour tout modèle (défaut de l'original, corrigé et noté pour l'auteur, n° 27), et il refuse les modèles à experts, contrairement à ce que supposait le relevé.
- **Comment comparer** : le réseau de dlib finit par un softmax ; il écrit donc des log-probabilités, que `test_runtime` sait maintenant lire (`logprobs`). C'est la même mesure que les autres tests du moteur (divergence KL, token le plus probable), sans nouvel outil de comparaison.
- **Ordre de la suite** : Gemma 3 (le format que l'humain sert déjà, et une famille que dlib connaît presque, par Gemma 2), puis Qwen 3.5, LFM2, Mistral 3. Les MoE (R11) viendront après : il faut d'abord apprendre à dlib à remplir sa couche d'experts.

## 3. Morceau 2 du fil rouge : copier, pas déplacer

- **Quoi** : cpp-httplib, Mbed TLS, SQLite, libpng et libjpeg sont **copiés** sans changement de `dlib/external/` vers `gulliver/third_party/` (21 Mo). Le CMake de Gulliver a ses propres cibles (`gulliver_httplib`, `gulliver_mbedtls`) et ne fait plus `add_subdirectory` de dlib. Gulliver 6.8.1.
- **Pourquoi copier** : dlib (son enveloppe de sqlite, ses tests) et Pseudalis utilisent encore ces bibliothèques. Les déplacer casserait Pseudalis, alors que la règle est « Pseudalis reste sur dlib pour l'instant ». La copie de Gulliver est désormais la seule qui compte pour lui.
- **Pourquoi `<httplib.h>` plutôt qu'un chemin complet** : les dossiers sont donnés au compilateur par les cibles ; si une bibliothèque change de place un jour, seul le CMake change.
- **Ce qui reste de dlib dans Gulliver** : des en-têtes seulement (lecture des GGUF, tokenizer, ZIP, enveloppe de sqlite). Ce sont les morceaux 3 à 5.

## 2. L'ajout de la règle SSH pour `papy cyber`

- Vous l'avez demandé, mais la modification de `.claude/settings.local.json` a encore été refusée par le contrôle automatique de Claude Code (« Undermining Oversight ») : je n'ai pas le droit d'élargir mes propres permissions, même à votre demande, et je ne contourne pas ce refus.
- **À faire par vous** : `/permissions`, puis ajouter `Bash(ssh m2 *)` (et, si vous voulez la copie de fichiers, `Bash(scp * m2:*)` et `Bash(scp m2:* *)`). Les anciennes règles `aughra@…` peuvent être retirées : ce compte n'existe pas sur le M2.

## 1. Les mesures de vitesse s'arrêtent

- **Quoi** : pas d'Ollama, pas de MLX, pas de nouvelle mesure. Le découpage de l'attention des longues questions et « plusieurs tokens par pas » sont mis de côté.
- **Pourquoi** : c'est votre consigne. Ces trois pistes sont des gains de vitesse, et chacune se juge par des mesures alternées : sans mesure, on ne peut pas les garder.
- **Ce que je fais à la place** : la suite de la feuille de route sans mesure, d'abord le **morceau 2 du fil rouge** (prévu « à la fin du prochain chantier » : la nuit du 19 en était un), puis la **partie 3, l'atelier** (R1 : convertir un GGUF en réseau dlib entraînable). Gemma 4 26B-A4B sur le GPU reste dans la partie 2, pour plus tard.
- Les tests de justesse (`test_macos.sh`, `test_metal`, banc de la page) continuent : ce ne sont pas des tests de vitesse, et chaque sous-étape doit les passer.
