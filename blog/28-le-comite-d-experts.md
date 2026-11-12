# Dlib Transformer Extensions sur macOS, épisode 28 : le comité d'experts

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B), inchangés depuis l'épisode 20 ; Qwen3.5-4B : **19,5 tokens/s** sur le GPU ; Qwen 3.6 11B-A3B (mélange d'experts) : **26 tokens/s** sur le GPU.

Suite de [l'épisode 27](27-le-carnet-sur-le-gpu.md) : Qwen 3.5 écrivait sur le GPU. Restait sa variante à experts, Qwen 3.6 MoE, qui prend tout son sens avec la machine qui arrive.

## Pourquoi maintenant

Dans une quinzaine de jours, le MacBook Air M2 et ses 24 Go laisseront la place à un Mac mini M5 Pro de 64 Go. Avec lui, un modèle comme Qwen3.6-35B-A3B devient utilisable : 35 milliards de paramètres rangés en mémoire, 22 Go en Q4_K_M, mais 3 milliards seulement au travail pour chaque mot. Le choix a donc été fait de préparer ce modèle dès maintenant, et de garder les réglages fins de vitesse pour la nouvelle machine, où ils seront de toute façon à refaire.

Pour vérifier le travail sur 24 Go, on a pris une version réduite par la communauté : Qwen 3.6 11B-A3B, 7 Go, avec exactement la même structure que le grand modèle.

## Un comité et un permanent

Un mélange d'experts fonctionne comme un comité. Pour chaque mot, un aiguilleur, le « routeur », note les 256 experts et n'en convoque que les 8 mieux notés. Leurs avis sont pesés selon leur note puis additionnés. Le moteur savait déjà faire travailler un tel comité.

Qwen 3.6 y ajoute un **expert permanent**, consulté pour chaque mot, dont l'avis passe par une porte qui décide de son poids. Le moteur refusait jusqu'ici ces experts partagés. Il les accepte maintenant pour cette famille de modèles. Sur le processeur, les réponses ont été comparées à llama.cpp, mot à mot :
- une réponse est identique en entier ;
- les deux autres le sont jusqu'à une vraie hésitation du modèle, 0,061 contre 0,058 dans un cas, 0,399 contre 0,390 dans l'autre.

## Sur le GPU, le comité ralentissait d'abord

Pour mettre le comité sur le GPU, il a fallu quatre pièces : le calcul des notes du routeur, le choix des 8 experts, des produits où chaque case du lot va chercher les poids de « son » expert, et l'addition finale. La réponse était juste dès le premier essai, à 0,00007 % des calculs du processeur. Mais elle sortait à 9 tokens/s, presque trois fois moins vite que sur le processeur.

Deux pièces étaient mal organisées, et l'erreur est facile à commettre :
- **le routeur** faisait lire à chaque petite main du GPU sa propre ligne de poids, loin de celle de sa voisine. C'est comme 32 élèves qui ouvrent chacun un livre différent, au lieu de lire ensemble la même page. Une fois les 32 mains mises sur la même ligne, le modèle est passé à 13 tokens/s ;
- **le choix des 8 experts** était confié à une seule main par mot, qui parcourait seule les 256 notes huit fois de suite, dans chacune des 40 couches. Partagé entre 32 mains, le tri a fait passer le modèle à 26 tokens/s.

## Une fausse alerte instructive

Trois questions posées ensemble ont donné une surprise. Sur le processeur, « 12 fois 12 » donnait 162, et sur le GPU, 144. Le GPU avait raison, mais il fallait comprendre pourquoi le processeur se trompait. La référence a répondu : au chiffre des dizaines, ce modèle réduit hésite entre « 4 », avec une probabilité de 0,37, et « 6 », à 0,33. Un arrondi suffit à le faire basculer. Le modèle élagué calcule mal ; le moteur, lui, calcule ce que le modèle dit.

## Pour les curieux : les mesures

Qwen 3.6 11B-A3B Q4_K_M, 200 tokens, mode économie d'énergie coupé, charge 2,7 à 3,5, mesures alternées :

| Essai | Processeur | GPU |
|---|---|---|
| 1 | 25,0 tokens/s | 26,3 tokens/s |
| 2 | 24,4 tokens/s | 26,1 tokens/s |

Le gain du GPU reste modeste sur le M2, car un mélange d'experts ne lit qu'une petite partie de ses poids à chaque mot, et le processeur du M2 s'en tire bien. Sur le M5 Pro, dont le GPU est beaucoup plus gros par rapport à son processeur, l'écart devrait grandir. C'est à mesurer là-bas.

Les étapes de la mise au point du GPU, sur la même question :

| Version des noyaux | Écriture |
|---|---|
| Premier essai | 9,2 tokens/s |
| Routeur lu par 32 mains par ligne | 13,1 tokens/s |
| Choix des experts partagé entre 32 mains | 26,7 tokens/s |

Le fichier a aussi révélé un format de poids oublié : la moitié de ses experts « down » sont en Q5_0, parce que leurs lignes de 128 valeurs sont trop courtes pour les formats K. Le GPU lit maintenant Q5_0.

## À suivre

Avant l'arrivée du Mac mini : la reprise d'une conversation Qwen 3.5 au milieu d'un texte, la lecture de ses prompts sur le GPU, et un script qui refera toutes les mesures en une seule commande sur la nouvelle machine.
