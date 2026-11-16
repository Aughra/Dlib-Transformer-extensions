# Dlib Transformer Extensions sur macOS, épisode 50 : la liste d'attente

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Gemma 4 E2B : lecture de 512 tokens **671 à 685 tokens/s** sur le GPU ; Qwen 3.5 0.8B en Q5_K_M : écriture **72 tokens/s** sur le processeur, contre 39 à 40 avant cet épisode.

Suite de [l'épisode 49](49-les-etages-qui-ne-servent-a-personne.md) : Gemma 4 lisait trois fois plus vite. Le Mac mini n'est pas encore là, et l'humain a demandé de faire, dans l'ordre, tout ce qui pouvait se faire sur le M2 en l'attendant. Voici le reste de cette liste d'attente.

## L'exercice d'incendie

Qwen 3.5 ne relit pas toute la conversation à chaque mot : il garde un état, une sorte de mémoire résumée, qu'il met à jour en place. Si le GPU tombe en panne au milieu d'une étape, après avoir déjà modifié cet état, le processeur reprend le calcul à partir d'un état faux : le même mot compte deux fois. La panne est rare, et on ne l'a jamais vue en vrai.

Pour corriger une panne qu'on ne voit jamais, il faut la provoquer, comme on déclenche une alarme pour un exercice d'incendie. Un interrupteur provisoire, retiré ensuite, a fait échouer une étape juste après son passage sur le GPU. Sans correction, la réponse dérivait dès sa deuxième phrase.

Le premier remède essayé, une photocopie de l'état avant chaque mot, marchait, mais coûtait 6 à 9 % de vitesse, en permanence, pour une panne qui n'arrive presque jamais. On l'a écarté. Le moteur pose déjà, tous les 256 tokens, des points de reprise de cet état : il tient désormais aussi le carnet des tokens lus depuis le dernier. En cas de panne, il revient au point de reprise et relit le carnet. La réponse est alors identique, au mot près, à celle de la référence, et le carnet ne coûte rien.

## Des boutons qui disent vrai

L'original avait remarqué que certains modèles, comme Qwen2.5 Coder, annoncent dans leur format des outils qu'ils n'appellent jamais vraiment. Les boutons Agent et Web restaient allumés pour rien. Le catalogue de l'original note désormais ce que chaque modèle sait faire, vérifié modèle par modèle, et la page suit ces notes. Une prudence propre au fork : les modèles ajoutés depuis Hugging Face ont des capacités devinées, pas vérifiées, et Qwen 3.5 y est marqué à tort « sans raisonnement ». Ils gardent donc les capacités de leur format.

## Un modèle trop grand pour la maison

Mistral Small 3.2, un modèle de 24 milliards de paramètres, a reçu son format de conversation, repris de l'original : un message système qui contient la date du jour et celle de la veille, les outils écrits juste avant la dernière question, et des étiquettes de 9 caractères pour relier chaque appel d'outil à son résultat. Sur le vrai modèle, la réponse est juste, l'appel d'outil lu, le résultat utilisé.

Mais ses 14,3 Go ne tiennent pas dans les 13,6 Go que macOS accorde au GPU du M2, et à côté des services en marche, le processeur manque de mémoire vive : macOS échange alors avec le disque, et le modèle écrit 0,04 token par seconde. Services arrêtés, il monte à 3,5 tokens par seconde. C'est un modèle pour le Mac mini et ses 64 Go ; il est noté dans le programme du premier jour.

## Un noyau gardé, un noyau jeté

Les modèles se distribuent en plusieurs « formats » de compression des poids. Le moteur avait des noyaux NEON, écrits à la main pour la puce Apple, pour les plus courants (Q4_K et Q6_K). On en a écrit deux de plus, Q8_0 et Q5_K, en vérifiant qu'ils rendent exactement, au bit près, le même résultat que le code ordinaire.

Le premier n'a rien gagné : 0,6 milliseconde avant comme après, parce que le compilateur avait déjà su vectoriser ce code simple tout seul. On l'a jeté, selon la règle du projet : on ne garde que ce qui gagne. Le second divise par près de six le temps d'un produit. Sur un vrai modèle compressé en Q5_K_M, un format courant sur Hugging Face, l'écriture passe de 39-40 à 72 tokens par seconde.

## Deux petites choses

Deux reprises de l'original pour finir. Quand une discussion passe par le relais vers un modèle extérieur qui accepte plusieurs niveaux d'effort, comme Claude, le bouton du raisonnement devient un menu de ces niveaux. Et une discussion se supprime désormais depuis la page de son projet, sans quitter cette page.

## Pour les curieux : les chiffres et les fichiers

- **5.5.1**, la relecture : `token_log`, `log_pass()` et `replay_logs()` dans `engine/cpu_runtime.cpp`. Panne simulée sur Qwen3.5-4B, panne courte comme long journal de 2 823 tokens : réponse identique à la référence. La copie de l'état à chaque étape, écartée, coûtait 17,8-18,5 tokens/s contre 19,4-19,8 (4B), et 24,3-25,1 contre 26,2-26,4 (11B-A3B).
- **5.6.0**, les capacités vérifiées : `roleCapabilities()` dans `web/js/status.js`, repris de l'original 1.0.16.
- **5.7.0**, Mistral Small 3.2 : `template_string()`, `tools_before_last_question()` et `mistral_call_id()` dans `engine/chat_format.cpp`, repris de l'original 1.0.14, avec ses tests.
- **5.8.0**, le noyau Q5_K : `dot_q5_K_neon` dans `engine/qkernels_neon.inc`. Produit de 4 096 lignes de 4 096 valeurs : 2,9 → 0,5 ms (Q8_0 : 0,6 ms dans les deux cas).
- **5.9.0**, le menu de l'effort et la corbeille des projets (`composer.js`, `layout.js`, `navigation.js`, `projects.js`), repris de l'original 1.0.23.
- Tests : test complet d'Albireo, 60 sur 60 après chaque version ; banc de la page, 131 vérifications ; `test_qkernels` et `test_chat_format` passent. Aucun fichier de dlib n'a été modifié dans cet épisode.
- Un imprévu : le Mac a redémarré pendant le travail, pour une mise à jour de macOS. Les services se sont arrêtés proprement, et ont été relancés.

## À suivre

Les nouveautés de Pseudalis dans l'original (des colonnes de fichiers CSV qu'on désigne d'avance, et des détecteurs réentraînés), puis un tout autre chantier, demandé par l'humain : un backend Metal pour les tenseurs de dlib, celui de l'entraînement, en huit phases.
