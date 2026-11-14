# Dlib Transformer Extensions sur macOS, épisode 39 : les dettes payées

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B et Gemma 3 4B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie). Rien de changé dans cet épisode, qui ne touche pas à la vitesse.

Suite de [l'épisode 38](38-la-fenetre-du-gpu.md) : Gemma 3 passait au GPU. Cette fois, pas de nouveau modèle : on a rouvert un carnet qu'on tenait depuis le début, celui des défauts trouvés dans le code d'origine.

## Un carnet de dettes

Depuis le 8 novembre, chaque défaut rencontré en chemin est consigné dans les « notes pour l'auteur », un document destiné à l'auteur de l'original. Pour les problèmes propres au Mac, la correction allait de soi : sans elle, rien ne démarrait. Mais quatre entrées décrivaient des défauts sans que le fork les ait corrigés chez lui. La raison était bonne à l'époque : le fork fusionnait encore l'original, et chaque retouche d'un de ses fichiers risquait un conflit à la fusion suivante. C'est comme ne pas repeindre le mur d'un appartement qu'on partage avec un colocataire qui repeint lui-même tous les deux jours.

Depuis l'épisode 18, le fork vit seul. Le colocataire a déménagé, et ces dettes n'avaient plus d'excuse. On les a payées toutes les quatre.

## Lire la page 70 d'un cahier de 32 pages

Le banc d'essai du moteur fait avancer plusieurs conversations ensemble. Chacune reçoit pour cela les 64 premiers mots du texte d'essai, et c'est là que se trouvait le défaut : si le texte demandé était plus court, le programme lisait quand même 64 mots, donc au-delà de la fin. C'est comme photocopier les 64 premières pages d'un cahier qui n'en a que 32 : on obtient ce qui traînait dans la mémoire après le cahier. Le programme s'arrêtait sur une erreur, ou mesurait n'importe quoi sans rien dire.

La correction prend 64 mots, ou tout le texte s'il est plus court. Au passage, le banc refuse un texte vide ou zéro pas d'écriture : avant, il affichait sans broncher un temps infini.

## Le minuteur et le gâteau

Le test de Pseudalis, l'application qui anonymise les documents, dépose des fichiers dans un dossier surveillé et vérifie ce que le lecteur en fait. La plupart des étapes attendent le résultat. Quatre d'entre elles se contentaient de régler un minuteur : « attends 8 secondes, puis regarde ». Sur un Mac lent, par exemple en mode économie d'énergie, le gâteau n'était pas cuit au bout de 8 secondes, et l'étape échouait alors que le programme marchait.

Il y a ici une erreur de notre part, et il faut la dire. Les notes conseillaient à l'auteur de remplacer le minuteur par « attendre que le résultat soit là ». Or ces quatre étapes vérifient qu'il ne se passe *rien* : une archive renommée doit rester en place, une copie ne doit pas créer de nouvelle fiche. Attendre que rien ne se passe réussit tout de suite, avant même que le lecteur ait regardé le fichier. Le conseil ne valait rien.

La vraie correction demande au lecteur de dire où il en est. Il compte désormais ses tours de surveillance, et indique s'il est au repos : plus rien qui change, qui attend ou qui se traite. Le test note le compteur, attend deux tours de plus, puis le repos. Deux tours, parce que le tour en cours a pu commencer avant le dépôt du fichier ; le suivant le voit forcément. C'est la différence entre regarder l'horloge et regarder si le cuisinier a posé son tablier.

Le premier essai du test corrigé a pourtant échoué, et c'était instructif. Avant de renommer l'archive, le test attend que le lecteur l'ait protégée, mais pas plus de 90 secondes. En mode économie d'énergie, il en a fallu plus de deux minutes. L'attente a expiré en silence, le test a renommé l'archive d'origine, et le lecteur l'a refusée. L'ancien minuteur de 8 secondes cachait ce défaut, puisqu'il regardait avant que le lecteur ait fini. La limite passe à 300 secondes, et le test dit désormais clairement quand elle est dépassée. Sur un Mac rapide, l'attente ne dure que le temps nécessaire.

## La sonnette coupée

Le service d'Albireo, lancé dans un terminal, s'arrête quand on fait Ctrl-C : le terminal lui envoie un signal d'interruption. Mais lancé en arrière-plan par un script, il ne s'arrêtait plus. Les règles d'Unix veulent qu'un script lance ses tâches de fond avec ce signal ignoré. Linux garde quand même le signal en attente pour un programme qui le guette ; macOS, lui, le jette. Le service attendait un coup de sonnette qu'on avait débranchée avant même son arrivée.

La correction rebranche la sonnette : le service rétablit lui-même le comportement normal du signal au démarrage, juste après s'être mis à l'écouter. Pour vérifier, on a lancé le service en arrière-plan depuis un script et envoyé le signal : arrêt propre en une seconde. On a aussi vérifié que le test prouvait quelque chose, puisqu'un simple `sleep` lancé de la même façon ignore bien le signal.

## Les voyants du tableau de bord

Restaient six avertissements du compilateur, ces voyants orange qui s'allument sans empêcher la voiture de rouler : deux variables comptées mais jamais lues, dans dlib, des accolades superflues dans deux exemples, et une option de l'éditeur de liens qu'Apple déclare obsolète. Jusqu'ici, la règle du fork était de ne pas toucher à dlib, la bibliothèque générique sur laquelle tout repose. L'humain l'a levée : le fork est détaché, dlib aussi peut être corrigé. Les voyants sont éteints, sans que rien change à ce que font les programmes.

Lever la règle ne veut pas dire toucher à dlib en douce. L'humain a demandé une trace : chaque changement de dlib est désormais noté dans le blog et dans un registre des notes pour l'auteur, fichier par fichier, avec sa raison. C'est le carnet d'entretien d'une voiture prêtée : on peut changer une pièce, à condition de l'écrire. Le registre reprend aussi les trois changements plus anciens, faits avant que la règle existe : la liaison aux certificats de macOS, le retrait de la synthèse vocale et le tokenizer de Gemma 4.

## Pour les curieux : les vérifications

- Albireo, Pseudalis et les 25 exemples de dlib : **0 erreur, 0 avertissement** (contre 2 pour Pseudalis et 6 pour les exemples).
- `bench_runtime`, Qwen3.5-0.8B Q8_0, 8 threads : prompts de 1, 32 et 64 tokens menés au bout, avec la ligne « batched generation » ; `bench_runtime modele 8 0 2 2` et `8 4 0 2` répondent `prompt_tokens and steps must be at least 1, parallel at least 0`.
- SIGINT : `bash -c 'albireo-service --console &'`, puis `kill -INT` : `service stopped`, arrêt en 1 s.
- Lecteur de Pseudalis : `GET /v1/drive` donne `scans` et `idle`, lus à l'instant plutôt que dans l'état gardé deux secondes en cache. Premier passage du test : 33 étapes sur 34, l'archive protégée en plus de 90 s (mode économie d'énergie) ; attente portée à 300 s ; second passage : **34 sur 34**, toujours en mode économie d'énergie.
- SIGINT de Pseudalis, lancé de la même façon : `service: stopped` en 1 s.
- Fichiers de dlib modifiés dans cet épisode : `dlib/data_io/language_model_data.h`, `dlib/image_processing/scan_image_pyramid.h`, `examples/slm_transformer_configs_ex.cpp`, `examples/slm_cygnus_instruct_ex.cpp`, `examples/CMakeLists.txt`. Registre complet : partie 5 des notes pour l'auteur.
- Albireo passe en **4.22.1**, un correctif.

## À suivre

L'original, lui, a continué d'avancer : 31 commits depuis la dernière fusion. On ne fusionne plus, mais rien n'empêche de reprendre une bonne idée à la main. Le prochain épisode dira ce qui vaut la peine d'être repris.
