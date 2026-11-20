# Dlib Transformer Extensions sur macOS, épisode 59 : quatre familles dans dlib

> **Repère de vitesse** (Mac mini M5 Pro, mesure de Gulliver 6.7.0, inchangée depuis pour ces modèles) : écriture sur le GPU **114 à 155 tokens/s** (2B), **62 à 80 tokens/s** (4B) ; lecture sur le GPU **4 567 à 4 772 tokens/s** sur 512 tokens (2B), **2 041 à 2 100** (4B).

Suite de [l'épisode 58](58-la-demi-precision.md), consacré à la demi-précision. Le noyau des experts y est passé lui aussi (Gulliver 6.8.0, de 3 à 49 % plus rapide selon le modèle), puis les mesures de vitesse se sont arrêtées : l'humain a demandé de reprendre la feuille de route. Sa partie suivante prépare l'entraînement.

## Deux moteurs pour un même modèle

Gulliver lit un modèle avec son propre moteur, écrit pour aller vite : il ne sait que calculer la suite d'un texte. Pour affiner un modèle sur ses propres textes, il faut autre chose : un réseau dont chaque poids peut être corrigé, ce que dlib sait faire. Le premier pas de l'atelier d'entraînement, appelé R1 dans la feuille de route, consiste donc à **convertir un fichier GGUF en réseau dlib**, et à vérifier que ce réseau calcule exactement ce que calcule le moteur.

L'image est celle d'une partition recopiée pour un autre orchestre. Avant de changer une seule note, on fait jouer les deux orchestres côte à côte, et on écoute s'ils jouent la même musique. Ici, l'écoute est un chiffre : pour chaque mot d'un texte, les deux moteurs donnent une probabilité à chacun des mots possibles, et on mesure l'écart entre ces deux listes. Un écart de l'ordre de 1e-7, c'est le bruit des arrondis : la même musique.

Un script fait toute la comparaison d'un coup (`tools/atelier/compare_dlib_import.sh`) : détection du modèle, compilation du réseau, lecture d'un texte, comparaison au moteur.

## L'outil qui ne démarrait pas

Premier essai, premier arrêt : l'outil d'import de dlib refusait tous les modèles, avec un message sans appel (« assigned 170 of 171 parameter tensors; the layer order assumption is wrong »). La cause était une mise à jour de dlib, faite un mois plus tôt chez l'auteur : la table des mots se comptait désormais deux fois. Corrigé, Qwen3-0,6B est passé du premier coup.

Le deuxième défaut était plus sournois. L'outil ne connaissait que quelques familles de modèles, mais laissait passer les autres en les lisant comme des Llama : Gemma 3 était accepté, avec un inventaire qui oubliait 52 de ses 340 tenseurs. Le fork bloque désormais toute famille inconnue. Les deux défauts sont dans les notes pour l'auteur (entrées 27 et 28) : ils touchent aussi Windows.

## Quatre familles, quatre pièces neuves

Restait à apprendre à dlib les familles que Gulliver sert, une par une, chacune vérifiée contre le moteur.

| Famille | Ce qu'il a fallu apprendre à dlib | Écart avec le moteur |
|---|---|---|
| Gemma 3 | une autre GELU, deux normes de plus par bloc, une fenêtre glissante sur cinq couches sur six | 1,2e-7 (1B), 1,1e-7 (4B) ; 3,8e-7 sur 1 303 tokens |
| LFM2 | une convolution courte, et une pile qui mêle deux sortes de couches | 3,2e-7 |
| Qwen 3.5 | une mémoire corrigée à chaque mot (Gated DeltaNet), une attention à porte | 4e-8 ; 1,8e-9 sur 1 560 tokens |
| Mistral 3 | l'étirement YaRN des positions, une « température » de l'attention | 3,8e-8 |

L'ordre a changé en route : LFM2 est passé avant Qwen 3.5. Les deux mêlent des couches d'attention à des couches d'un autre genre, et LFM2 posait la charpente commune avec une pièce simple, une convolution de trois poids. Qwen 3.5 n'avait plus qu'à y loger sa pièce difficile. Un réseau dlib fixe sa forme à la compilation, et le motif de LFM2 est irrégulier (l'attention aux couches 2, 5, 8, 10, 12 et 14) : il s'écrit dans le type même du réseau, comme un masque de bits, une case par couche.

## Le carnet corrigé à chaque mot

La pièce de Qwen 3.5 mérite qu'on s'y arrête. Trois couches sur quatre n'y relisent pas tout le texte à chaque mot, comme le fait l'attention : elles tiennent un carnet, une petite matrice par tête, qu'elles corrigent à chaque mot lu. C'est la règle « delta » : on lit ce que le carnet prédit, on mesure l'erreur, on corrige d'autant.

Pour entraîner une telle couche, il faut remonter le temps : l'erreur sur le dernier mot dépend de toutes les corrections d'avant. Garder chaque page du carnet coûterait un demi-gigaoctet par couche pour un texte de 512 mots. La couche ne garde donc qu'une page tous les 64 mots, et recopie les autres au moment de remonter, comme on reconstitue une journée à partir des photos prises toutes les heures.

Le test habituel de dlib a d'abord refusé la couche : ses dérivées s'écartaient de 5 % de celles mesurées pas à pas. On a cherché l'erreur, et c'était le test qui se trompait. En recommençant avec des pas plus fins, l'écart fondait (12 %, puis 1 %, puis 0,5 %) : la mesure, faite en nombres à 32 bits, était trop grossière pour une couche aussi courbée. Une version de référence écrite à part, en double précision, a tranché : la couche est juste, à 1e-4 près. C'est cette référence qui sert maintenant de test.

## L'étirement qu'on croyait inoffensif

Dernière famille, dernière surprise. Ministral 3 déclare un étirement des positions, YaRN, pour lire des textes seize fois plus longs que ceux de son entraînement. L'outil d'origine l'ignorait, avec une note rassurante : seules les très longues séquences en souffriraient. C'est faux. YaRN ralentit les rotations lentes dès le premier mot ; un modèle importé sans lui est un autre modèle.

On l'a mesuré en coupant YaRN pour l'occasion. Sur un texte court, l'écart avec le moteur est cinquante fois plus grand qu'avec YaRN ; sur 1 561 mots, il devient visible à l'œil, avec un mot le plus probable qui change. YaRN est désormais calculé dans dlib exactement comme dans llama.cpp, et l'erreur de la note est la trentième entrée des notes pour l'auteur.

## Ailleurs dans Gulliver

Le chantier de l'atelier n'a pas occupé toute la journée.

- **Gulliver ne construit plus dlib** (6.8.1) : les bibliothèques d'autres projets qu'il utilisait à travers lui sont copiées dans son propre dossier. C'est un morceau de plus de son indépendance.
- **La discussion depuis Internet** est préparée (6.9.0) : la page servie par Cloudflare Pages derrière une connexion GitHub, le calcul restant sur le Mac, joint par un tunnel. Une passerelle ne laisse passer que la discussion ; l'administration reste au Mac. La mise en ligne attend les réglages du compte Cloudflare.
- **Les documents du Mac sont lus par le Mac** (6.10.0) : un document joint depuis le dossier du Mac ne fait plus l'aller-retour par le téléphone, la page n'envoie que son nom.

## Pour les curieux : les chiffres et les fichiers

Comparaison au moteur de Gulliver (`compare_dlib_import.sh`), en produits exacts, sur 25 ou 26 positions sauf mention :

| Modèle | KL moyenne | Plus grand écart des logits | Même mot le plus probable |
|---|---|---|---|
| Qwen3-0.6B Q8_0 | 4,2e-7 | 0,010 | 100 % |
| Gemma 3 1B Q4_K_M | 1,2e-7 | 0,0081 | 100 % |
| Gemma 3 1B, 1 303 tokens | 3,8e-7 | — | 100 % |
| Gemma 3 4B Q4_K_M | 1,1e-7 | — | 100 % |
| LFM2.5-1.2B Q8_0 | 3,2e-7 | 0,010 | 100 % |
| Qwen3.5-0.8B Q8_0 | 4,0e-8 | 0,0072 | 100 % |
| Qwen3.5-0.8B, 1 560 tokens | 1,8e-9 | 0,0072 | 100 % |
| Ministral 3 3B Q8_0 | 3,8e-8 | 0,0035 | 100 % |

YaRN coupé, comme dans l'outil d'origine, sur Ministral 3 3B : KL 2,1e-6 et plus grand écart 0,042 sur 26 tokens ; KL 2,2e-3, plus grand écart 6,2 et un mot le plus probable différent sur 1 561 tokens (avec YaRN : KL 2,5e-8, plus grand écart 0,078, même mot partout).

Le test de la couche DeltaNet, première version : « Gradient error in parameter #2. Relative error: -0.0568839 ». La référence en double précision : passe avant à 6e-7, dérivées à 1e-4 près, sur 6 à 140 tokens.

**Ce que le fork change dans dlib pour R1** (chaque ligne est au registre des notes pour l'auteur) :

- `dlib/data_io/gguf_weight_loader.h`, `gguf_model_spec.h` et son abstract : la table des mots comptée une fois, les familles inconnues bloquées, puis `gemma3`, `lfm2`, `qwen35`, `mistral3` ; `transpose_into` refuse un tenseur trop petit au lieu de lire au-delà (entrée 29) ;
- `dlib/dnn/layers.h` et son abstract : `gelu_tanh_`, `geglu`, `short_conv_`, `gated_delta_net_` ; la rotation partielle, l'échelle des positions et YaRN dans `rotary_positional_embedding_` ;
- `dlib/dnn/transformer.h` et son abstract : blocs de Gemma, fenêtre glissante, porte de sortie et température de l'attention, `short_conv_block`, `delta_net_block`, `hybrid_stack` ;
- `dlib/dnn/decoder_transformer_config.h` et son abstract : le placement des normes, `short_conv_transformer_config`, `delta_net_transformer_config` ;
- `dlib/examples/slm_gguf_import_ex.cpp` : `--dump-logits`, l'inventaire des nouvelles familles ;
- `dlib/test/dnn.cpp` : un test par ajout ; `dtest --test_dnn` passe, 794 553 vérifications.

Côté Gulliver, `tests/test_runtime.cpp` a appris à lire ces comparaisons (`logprobs`), et à ne plus s'arrêter sur un modèle à mémoire récurrente : il voulait revenir au milieu d'une séquence, ce que ces modèles ne permettent pas. Versions : Gulliver **6.8.1 à 6.10.1** ; Qwen 3.5 et Mistral 3 ne changent que dlib.

## À suivre

Le réseau est juste ; il reste à l'entraîner. La suite de la feuille de route affine un modèle sur ses propres textes, avec un adaptateur léger (R2), en commençant par vérifier qu'une passe d'entraînement fait baisser l'erreur sur un petit modèle.
