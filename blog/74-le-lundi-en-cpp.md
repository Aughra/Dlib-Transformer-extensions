# Dlib Transformer Extensions sur macOS, épisode 74 : le lundi en C++

> **Repère de vitesse** (Mac mini M5 Pro) : inchangé depuis [l'épisode 68](68-la-tete-trop-large.md) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384.

Suite de [l'épisode 73](73-les-questions-pieges.md). Les questions-pièges passaient enfin par le moteur. À cette étape, on a porté les derniers outils du lundi. Entre le moment où le Mac se réveille pour chercher de nouveaux modèles et le mail qui donne le conseil de la semaine, il ne reste qu'un seul programme Python : le juge MLX, gardé par décision, parce que MLX ne se pilote qu'en Python.

## La chaîne du lundi

Chaque lundi, le Mac travaille seul, en deux temps. À 1 h, une veille rafraîchit le catalogue, cherche les nouvelles versions et les modèles populaires de Hugging Face, puis télécharge ce qui tient dans le Mac. À 8 h, la mesure fait passer chaque modèle sur le banc : vitesse, justesse, épreuve, sondes, essai « comme un utilisateur », recherche dans les documents. Elle rédige ensuite le conseil, retire le dernier du classement et prévient par mail. Pendant tout ce temps, un surveillant veille à ce que rien ne fasse chauffer le Mac trop longtemps.

| Maillon | Avant | Maintenant |
|---|---|---|
| Veille de 1 h | `modeles_semaine.py` | `modeles_semaine` |
| Épreuve et sondes | `epreuve_modeles.py` | `epreuve` |
| Essai « comme un utilisateur » | `bench_service.py` | `bench_service` |
| Recherche dans les documents | `test_rag.py` | `test_rag` |
| Conseil | `conseil_modeles.py` | `conseil` |
| Retrait du dernier | `retrait_modeles.py` | `retrait_modeles` |
| Mails | `mail_admin.py` | `mail_admin` |
| Petites lectures de JSON dans le script de mesure | `python3 -c …` | `json_get` |
| Surveillant | `surveillant.py` | `surveillant` |

Chaque nouveau programme a d'abord été lancé à côté de l'ancien, sur les mêmes données. On ne retirait l'ancien qu'une fois les deux d'accord. La veille, en mode essai, a écrit les mêmes 17 lignes que l'ancienne, y compris les mêmes 15 dépôts proposés sur Hugging Face. Le test de la recherche a rendu, au chiffre près, le même fichier de résultats.

## Le même hasard

Le test de la recherche dans les documents garde une curiosité. Ses 40 questions ont été écrites une fois par un modèle, à partir de passages du blog tirés au hasard. Si le fichier des questions disparaît, le programme les refait. Pour qu'il tire les mêmes passages que l'outil d'origine, le nouveau programme reproduit exactement le générateur de hasard de Python, avec la même graine.

C'est comme rejouer une partie de cartes en battant le paquet exactement de la même façon. Les 340 paragraphes du blog sortent dans le même ordre des deux côtés.

## Le surveillant en dernier

Le surveillant a été gardé pour la fin, et ce n'était pas par hasard. Il tient un vrai pouvoir. Quand le Mac reste en souci pendant une demi-heure, parce que le processeur est presque plein ou que la puce est trop chaude, il arrête les programmes qui en sont la cause. Une erreur dans le sens doux laisserait un programme chauffer le Mac. Une erreur dans l'autre sens arrêterait un programme qui n'y est pour rien.

Il a donc passé deux épreuves avant de prendre sa place. La première est le test hérité de l'ancien : une demi-heure ramenée à quarante secondes, trois programmes `yes` qui calculent sans arrêt et se relancent toutes les six secondes, et un `sleep` qui ne fait rien. Le surveillant doit arrêter les premiers, nommer celui qui les lance, épargner le second, et prévenir par un faux facteur. Les cinq contrôles passent. La seconde est un relevé à blanc sur le vrai Mac, avec deux `yes` lancés exprès : il les voit, compte deux cœurs, et n'arrête rien, puisque c'est un essai.

Le nouveau surveillant vit dans le dossier des programmes de Gulliver, qu'il protège lui-même. Il ne risque donc pas de se prendre pour un coupable.

## Ce qu'il reste

Il reste encore du Python dans le dépôt, mais plus dans la chaîne du lundi :

- de petites commandes glissées dans des scripts de mesure à la main (llama.cpp, MLX, débordements du GPU), souvent quelques lignes qui lisent un fichier JSON ;
- un outil de vérification des commentaires ;
- les outils de Pseudalis.

Restent aussi, par décision, les juges : PyTorch et MLX ne se pilotent qu'en Python, et ce sont eux qui disent si le moteur calcule juste. Les exemples hérités de l'original restent tels quels.

## Pour les curieux : les chiffres et les fichiers

- Versions 7.22.0 (essai « comme un utilisateur », recherche dans les documents), 7.23.0 (veille), 7.24.0 (surveillant).
- Essai « comme un utilisateur », Qwen3-VL 2B Instruct, l'ancien puis le nouveau : lecture de la conversation longue 2 794 et 2 715 tokens/s, écriture 62,3 et 62,2 tokens/s, même document de 32 165 tokens. Sans le mode rapide, 131 072 tokens tiennent (125 498 lus à 678 tokens/s).
- Recherche dans les documents, 73 épisodes et 40 questions : rappel à 6 passages de 0,85 en hybride et de 0,95 en dense avec le tri fin, identique des deux côtés.
- Surveillant, le test :

  ```
  Arrêtés :
    - yes (3 processus, 1.6 cœurs en moyenne, lancés par test_surveillant (pid 35220), yes)
  ```

- Les deux agents du Mac (`macos/modeles-semaine/`, `macos/surveillance/`) ont été réinstallés pour lancer les programmes directement, sans passer par Python.
- Tous ces programmes parlent au service en vérifiant son certificat. Les outils Python, eux, acceptaient n'importe quel certificat.
- Fichiers : `tools/{bench_service,test_rag,modeles_semaine,surveillant_main}.cpp`, `tools/{mail_admin,surveillant}.h`, `tests/test_surveillant.cpp`. Huit outils Python supprimés.
- dlib n'est pas touché.

## La suite

Le premier lundi tout en C++ arrive le 30 novembre : la veille à 1 h, la mesure à 8 h, et les deux mails. On le lira avec attention. Ensuite viendront les petites commandes Python des scripts, puis Pseudalis.
