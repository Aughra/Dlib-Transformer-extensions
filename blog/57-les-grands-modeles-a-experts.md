# Dlib Transformer Extensions sur macOS, épisode 57 : les grands modèles à experts

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.6.0) : écriture sur le GPU **114 à 155 tokens/s** (2B), **62 à 80 tokens/s** (4B) ; lecture sur le GPU **1 510 à 1 805 tokens/s** sur 512 tokens (2B). Les grands modèles à experts, sur le GPU : Qwen3.6-35B-A3B lit **464 tokens/s** et écrit **50** ; Qwen3-30B-A3B lit **592** et écrit **56**.

Suite de [l'épisode 56](56-la-premiere-etape-sur-le-mac-mini.md), où le Mac mini passait ses premiers tests. Ses 64 Go avaient une raison d'être : faire tourner les grands « modèles à experts », trop gros pour le MacBook Air. À la même étape, on les a fait venir, et deux d'entre eux ont révélé des défauts qu'aucun test n'avait vus, faute d'avoir jamais lancé ces modèles.

## Un comité d'experts, dont on ne réveille que huit

Un modèle à experts ressemble à un grand cabinet de conseil : 128 ou 256 spécialistes, mais pour chaque mot, un aiguilleur n'en consulte que huit. Le modèle pèse lourd en mémoire (tout le cabinet doit être là), mais chaque mot ne coûte que le travail des huit consultés. C'est ce qui permet à un modèle de 35 milliards de paramètres d'écrire presque aussi vite qu'un modèle de 3 milliards, à condition d'avoir la place de le loger.

## Deux petites pièces en trop

Le premier, Qwen3.6-35B-A3B (22 Go), a répondu juste du premier coup, mot pour mot comme llama.cpp, l'outil de référence. Mais il tournait entièrement sur le processeur : le GPU le refusait, sans dire précisément pourquoi.

En ouvrant le fichier, on a trouvé la raison. Ce fichier compressé garde, dans chacune de ses 30 couches linéaires, deux petites matrices (2 048 × 32) en pleine précision, quand tout le reste est compressé. Le GPU savait lire des poids compressés de cinq façons, pas des poids non compressés à cet endroit-là. Comme un déménageur qui refuserait toute la maison parce que deux cartons ne sont pas à la norme.

Il savait pourtant déjà lire une matrice non compressée ailleurs, celle de l'aiguilleur des experts. On a fait passer les deux petites par le même chemin. Le modèle a basculé sur le GPU : il lit ses prompts quatre fois plus vite et écrit 50 % plus vite, et ses réponses restent celles de llama.cpp. Il lit aussi les images, avec son projecteur : la description d'un perroquet est identique, mot pour mot, à celle de llama.cpp.

## Trois modèles admis, jamais reçus

Le moteur déclarait accepter trois autres familles de modèles à experts : Qwen3 MoE, Granite MoE d'IBM et Mixtral de Mistral. Personne ne les avait jamais lancés. C'est le genre de promesse qu'on préfère vérifier avant qu'un utilisateur ne le fasse.

Les trois ont répondu juste, mais pas tous du premier coup :

- **Granite** répondait autrement que llama.cpp dès le premier mot. Pas d'erreur de calcul : les deux moteurs ne lisaient pas la même consigne. Le fichier embarque une version ancienne du modèle de conversation, le moteur suit celle d'IBM, plus récente, avec la date du jour. Avec la même consigne, les réponses sont identiques.
- **Qwen3-30B-A3B** a donné les mêmes réponses que llama.cpp, mot pour mot.
- **Mixtral** a révélé un vrai défaut, hérité de l'original : le moteur lui parlait dans un format générique (« User: … Assistant: ») au lieu du sien (« [INST] … [/INST] »). La détection cherchait un mot de code `[INST]` dans le vocabulaire du modèle ; les premiers modèles de Mistral l'écrivent en lettres ordinaires. Comme si l'on vérifiait qu'un invité parle français en cherchant une carte d'identité qu'il n'a jamais eue. Un format à part lui rend sa vraie consigne. Le défaut est signalé à l'auteur de l'original.

Au passage, ces trois modèles lisaient leurs prompts sur le processeur seul : une prudence posée quand on les avait admis sans pouvoir les essayer. Essayés, ils passent au GPU, et lisent de quatre à six fois plus vite.

## Ce qu'on a laissé de côté, et pourquoi

Deux chantiers prévus ont été examinés, puis repoussés, et il faut dire pourquoi :

- **Gemma 4 26B-A4B sur le GPU** demande trois noyaux neufs pour un format de compression que le GPU ignore, et une architecture plus inhabituelle (un bloc ordinaire à côté des experts, la porte et la montée des experts fondues en un seul tableau). C'est un vrai chantier, alors que le 35B-A3B, de la même famille de taille, tourne déjà sur le GPU.
- **Le noyau de lecture le plus courant du GPU**, qui sur le M2 n'atteignait que 76 % du plafond de la mémoire, en atteint 84 à 86 % sur le Mac mini. La marge restante ne vaut plus une étape de réglages.

Et un dernier pas, plus discret, vers l'indépendance de Gulliver : son moteur ne prend plus à dlib le calcul de l'empreinte des images, confié à la bibliothèque d'Apple.

## Pour les curieux : les chiffres et les fichiers

- **Fichiers** (empreintes SHA-256 vérifiées) : `unsloth/Qwen3.6-35B-A3B-GGUF` (UD-Q4_K_M, 22 134 528 992 octets, et `mmproj-F16.gguf`), `unsloth/Qwen3-30B-A3B-Instruct-2507-GGUF` (Q4_K_M), `bartowski/granite-3.1-1b-a400m-instruct-GGUF` (Q4_K_M), `mradermacher/Mixtral-8x7B-Instruct-v0.1-GGUF` (Q4_K_M). La conversion de TheBloke (2023), un tenseur par expert, est refusée par llama.cpp (« tensor 'blk.0.ffn_down_exps.weight' not found ») comme par le moteur.
- **Le refus du GPU** : `GPU: the passes stay on the processor, the kernels do not cover weights in other formats than Q4_K, Q5_K, Q6_K, Q5_0 and Q8_0` ; en cause, `ssm_alpha` et `ssm_beta` en F32. Après : « logits within 0.0840319% of those of the processor ».
- **Vitesses** (Mac mini, `bench_runtime`, processeur puis GPU en alternance, deux tours) :

| Modèle | Lecture de 512 tokens, processeur → GPU | Écriture, processeur → GPU |
|---|---|---|
| Qwen3.6-35B-A3B UD-Q4_K_M | 103 à 124 → 464 à 465 tokens/s | 29 à 36 → 49 à 52 |
| Qwen3-30B-A3B-Instruct-2507 Q4_K_M | 96 à 115 → 592 | 25 à 27 → 56 |
| Mixtral-8x7B-Instruct-v0.1 Q4_K_M | 29 à 35 → 133 à 143 | 22 à 25 → 27 à 29 |

- **Comparaisons à llama.cpp** : choix glouton, même prompt. Pour Mixtral, la dernière divergence tombe sur une égalité (« rich » ou « romantic ») où llama.cpp change lui-même de mot entre son GPU et son processeur.
- **Le noyau Q4_K** : sonde Metal, 218 à 219 Go/s pour un plafond de 254 à 260 Go/s.
- **Code** : `engine/cpu_runtime_metal.cpp` (matrices en floats, prompts des experts), `engine/chat_format.cpp` (`mistral_v1_format`), `host/hub.cpp` (`qwen35moe` lit les images), `engine/image_input.cpp` (CommonCrypto). dlib n'a pas changé.
- **Versions** : Gulliver **6.4.1**, **6.5.0** et **6.6.0** ; `test_macos.sh` 70 étapes.

## À suivre

Comparer Gulliver à d'autres moteurs sur les longues questions, puis, peut-être, Gemma 4 26B-A4B sur le GPU.
