# Dlib Transformer Extensions sur macOS, épisode 45 : l'orchestre et les solistes

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B, Gemma 3 4B, Gemma 4 E2B sur le GPU, les images de Gemma 3 et Gemma 4 26B-A4B : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 44](44-le-passe-partout-et-la-photo-de-groupe.md) : Gemma 3 voyait les images. À la même étape, on s'est attaqué au plus gros modèle de la famille Gemma 4 que le moteur refusait encore, le 26B-A4B.

## Vingt-six milliards, dont quatre au travail

Le nom dit l'essentiel : 26 milliards de nombres en tout, mais environ 4 seulement (« A4B », pour *active 4 billion*) qui travaillent pour chaque mot. C'est un « mélange d'experts » : au lieu d'un seul grand bloc de calcul, chaque couche contient 128 petits spécialistes, et un aiguilleur en choisit 8 pour chaque mot. On a la culture d'un gros modèle pour un coût de calcul de petit modèle. Le fichier pèse 16,9 Go en 4 bits : il tient de justesse dans les 24 Go du MacBook Air, et confortablement dans les 64 Go du Mac mini qui arrive.

Albireo savait déjà faire tourner des mélanges d'experts, ceux de Qwen 3.6 et de LFM2. Celui de Gemma 4 a sa propre recette, lue comme d'habitude dans le code de llama.cpp.

## L'orchestre et les solistes

Chez Gemma 4, les experts ne remplacent pas le bloc ordinaire : ils jouent à côté de lui. Dans chaque couche, un bloc « dense » de taille modeste travaille pour tous les mots, comme l'orchestre qui joue toute la partition, pendant que les 8 experts choisis interviennent comme des solistes. Les deux sorties sont remises à niveau séparément, puis additionnées.

Quelques détails changent tout, et il fallait les avoir tous :

- l'aiguilleur ne lit pas la même chose que les experts : il reçoit une version normalisée sans poids de l'état du mot, divisée par la racine de la largeur, puis multipliée par une échelle apprise ;
- chaque expert a sa propre échelle de sortie, qui s'ajoute au poids que lui donne l'aiguilleur ;
- la porte et la projection d'entrée des experts sont rangées ensemble dans un seul tableau, la porte d'abord.

Une autre surprise attendait dans l'en-tête du fichier : le nombre de « têtes de clés » change d'une couche à l'autre. Les couches qui ne regardent que les 1 024 derniers mots en ont 8, les couches qui regardent tout le texte n'en ont que 2, mais deux fois plus larges. Le moteur supposait le même nombre partout ; il le lit maintenant couche par couche, comme un rangement qui accepterait enfin des tiroirs de deux tailles.

## La case vide du formulaire

La première comparaison avec llama.cpp a montré une différence avant même le premier mot de la réponse : le prompt de llama.cpp comptait 23 tokens, celui d'Albireo 19. En comparant les gabarits de conversation rangés dans les fichiers, on a trouvé la cause : quand on ne demande pas au 26B-A4B de réfléchir, son gabarit écrit quand même une réflexion vide, `<|channel>thought` suivi de `<channel|>`, comme on barre la case « observations » d'un formulaire pour montrer qu'on ne l'a pas oubliée. Le gabarit du petit E2B ne le fait pas, et c'est sur lui que le format d'Albireo avait été réglé. Le format écrit maintenant cette case vide quand le gabarit du modèle le demande.

## La fausse alerte du chronomètre

Deuxième comparaison, un calcul avec réflexion : « 17 fois 23 ». Les deux moteurs trouvent 391, mais la réflexion d'Albireo s'arrêtait net au milieu d'une multiplication posée. On a d'abord cru à un écart de calcul. Une trace du texte brut a montré autre chose : la phrase finale (« I have to give the solution based on the thinking directly now ») n'était pas écrite par le modèle, mais par le moteur lui-même. Depuis une reprise de l'original, Albireo limite la réflexion à la moitié des mots permis pour la réponse, pour qu'il reste toujours de la place pour répondre : avec 400 mots permis, le chronomètre sonnait à 200. Ce n'était pas un défaut, seulement une règle qu'on avait oubliée. Limite levée, les 233 tokens de réflexion sont identiques à ceux de llama.cpp, mot pour mot.

Sur le long journal des épisodes précédents (2 820 tokens), les deux moteurs répondent la même chose : « The secret code word is PELICAN, and Gaëlle travelled in record 3. » C'est la bonne réponse, que le petit E2B manquait (il prenait l'enregistrement 3 pour celui d'Inès). Sur un paragraphe consacré à l'histoire de Paris, les deux textes ne se séparent que sur une hésitation de la référence elle-même : « inhabited » à 49 %, « known » à 34 %.

Ce modèle tourne pour l'instant sur le processeur seulement ; le GPU le refuse proprement. Sa vitesse sur le M2 en mode économie d'énergie ne dit rien de ce qu'il fera sur le Mac mini : c'est là-bas qu'on décidera s'il vaut la peine de le porter au GPU.

## Pour les curieux : les chiffres et les fichiers

- Modèle : `gemma-4-26B-A4B-it-UD-Q4_K_M.gguf` du dépôt unsloth/gemma-4-26B-A4B-it-GGUF, 16 947 541 728 octets, SHA-256 `f2c28b3d…` conforme ; 30 couches de largeur 2 816 ; formats Q8_0, Q5_1, Q4_K et F32. L'en-tête a été lu (25 premiers Mo) avant de télécharger le fichier, pour vérifier que le moteur en connaissait tous les formats.
- Formule : `src/models/gemma4.cpp` et `build_moe_ffn` de `src/llama-graph.cpp`, llama.cpp au commit `cd959bae9`.
- Moteur : `model_config::dense_beside_experts` et `kv_head_counts` (`engine/model_config.*`) ; `dense_ffn`, `dense_and_experts`, l'échelle des experts et la GELU dans `moe_ffn` (`engine/cpu_runtime.*`) ; la réflexion vide dans `gemma4_format` (`engine/chat_format.cpp`) ; le refus levé dans la recherche du service (`host/hub.cpp`).
- Comparaison : `llama-server -ngl 0 --jinja`, `/apply-template` puis `/completion`, choix glouton ; `engine_chat --kv f16 --budget -1`. Prompts de 23, 31 et 2 820 tokens des deux côtés.
- Test complet d'Albireo : deux étapes nouvelles, « gemma 4 moe » (« Paris ») et « gemma 4 moe reasoning » (« 391 »).
- Albireo passe en **4.29.0**. Aucun fichier de dlib n'a été modifié dans cet épisode.

## À suivre

Les deux chantiers demandés sont faits. La suite attend en partie le Mac mini : mesurer, puis choisir ce qui mérite le GPU. Avant cela, il fallait que ces modèles soient utilisables dans Albireo sans Internet ([épisode 46](46-l-etagere-du-mac.md)).
