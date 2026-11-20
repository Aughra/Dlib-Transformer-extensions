# Décisions du 2026-11-20 (en autonomie)

Consigne de l'humain, le matin : « ARRÊT les tests de perfs, et continue la roadmap ». Ce fichier dit ce que j'ai décidé seul, et pourquoi. Le plus récent est en haut.

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

## 2. L'ajout de la règle SSH pour `barbidou`

- Vous l'avez demandé, mais la modification de `.claude/settings.local.json` a encore été refusée par le contrôle automatique de Claude Code (« Undermining Oversight ») : je n'ai pas le droit d'élargir mes propres permissions, même à votre demande, et je ne contourne pas ce refus.
- **À faire par vous** : `/permissions`, puis ajouter `Bash(ssh barbidou@192.168.1.12 *)` (et, si vous voulez la copie de fichiers, `Bash(scp * barbidou@192.168.1.12:*)` et `Bash(scp barbidou@192.168.1.12:* *)`). Les anciennes règles `aughra@…` peuvent être retirées : ce compte n'existe pas sur le M2.

## 1. Les mesures de vitesse s'arrêtent

- **Quoi** : pas d'Ollama, pas de MLX, pas de nouvelle mesure. Le découpage de l'attention des longues questions et « plusieurs tokens par pas » sont mis de côté.
- **Pourquoi** : c'est votre consigne. Ces trois pistes sont des gains de vitesse, et chacune se juge par des mesures alternées : sans mesure, on ne peut pas les garder.
- **Ce que je fais à la place** : la suite de la feuille de route sans mesure, d'abord le **morceau 2 du fil rouge** (prévu « à la fin du prochain chantier » : la nuit du 19 en était un), puis la **partie 3, l'atelier** (R1 : convertir un GGUF en réseau dlib entraînable). Gemma 4 26B-A4B sur le GPU reste dans la partie 2, pour plus tard.
- Les tests de justesse (`test_macos.sh`, `test_metal`, banc de la page) continuent : ce ne sont pas des tests de vitesse, et chaque sous-étape doit les passer.
