# Dlib Transformer Extensions sur macOS, épisode 63 : le facteur et la dernière mesure

> **Repère de vitesse** (Mac mini M5 Pro, Gulliver 6.44.0, mesure du lundi 23 novembre) : écriture sur le GPU **163 tokens/s** (2B, Qwen3-VL 2B Thinking) et **84 tokens/s** (4B, Qwen3-VL 4B Thinking), mémoire de conversation en 8 bits ; lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B).

Suite de [l'épisode 62](62-une-nuit-a-l-atelier.md), qui finissait sur une promesse : le lundi à 8 heures, la mesure de la semaine dirait ce qu'elle pense de la nuit à l'atelier. Elle l'a dit. Entre-temps, Gulliver a appris à envoyer du courrier, et l'humain a décidé que cette mesure du lundi serait la dernière.

## Le gros modèle rattrape son voisin

Avant la mesure, la matinée a fini un travail commencé la nuit : l'écriture du modèle à experts, Qwen3-VL 30B A3B, qui restait derrière llama.cpp. Ce modèle, c'est un grand bureau de 128 spécialistes dont huit seulement travaillent sur chaque mot. Le GPU doit donc faire beaucoup de petits travaux plutôt que quelques gros, et une partie de ses ouvriers attendait les bras croisés.

Trois retouches ont rempli ces temps morts. Les spécialistes aux dossiers courts traitent deux lignes à la fois (6.43.0). L'attention répartit ses têtes sur plus de groupes de calcul quand il y en a peu (6.44.0). Le routeur, celui qui choisit les huit spécialistes, lit quatre valeurs d'un coup (6.44.0). Ensemble, le 30B écrit 90,6 à 91,7 tokens par seconde au lieu de 82,8 à 85,6. llama.cpp en écrit 89,5 à 92 : les deux moteurs sont au coude à coude. La justesse n'a pas bougé : la perplexité passe de 10,9347 à 10,9345, et l'accord avec le modèle d'origine calculé par PyTorch reste le même.

## Dix-huit nouveaux à la pesée

La nuit du dimanche au lundi, l'agent des modèles avait téléchargé tout ce que le catalogue proposait et que le Mac pouvait porter : dix-huit nouveaux, de 2 à 24 milliards de paramètres. À midi, la mesure les a tous pesés, avec les sept modèles déjà là, soit vingt-cinq en tout. Elle a lu une question puis écrit soixante-quatre mots, deux fois en alternance, et vérifié la perplexité de chacun.

| Modèle | Écriture après 512 tokens | Perplexité |
|---|---|---|
| Qwen2.5 Coder 1.5B | 174 tokens/s | 21,92 |
| Qwen3-VL 2B Thinking (nouveau) | 163 | 20,32 |
| Ministral 3 3B (nouveau) | 108 | 12,51 |
| Qwen3-VL 30B A3B Thinking (le choix de la semaine passée) | 94,0 | 10,93 |
| llama.cpp sur ce même 30B | 92,6 | 10,93 |
| Qwen3-VL 4B Thinking | 84,4 | 15,37 |
| Mistral Small 3.2 24B (nouveau) | 19,0 | 8,60 |

Le conseil ne change pas : pour tout faire, images comprises, le 30B à experts reste le meilleur compromis. Il prévoit le mieux le texte français parmi les rapides, et il écrit aussi vite qu'un modèle de 4 milliards de paramètres, puisque huit spécialistes seulement travaillent à chaque mot. Pour aller vite, Ministral 3 3B prend la place. Les deux grands modèles de 24 milliards de paramètres prévoient mieux encore le texte, mais écrivent cinq fois plus lentement : de quoi lire par-dessus leur épaule.

La recherche dans les documents a été mesurée aussi : sur quarante questions figées posées aux épisodes du blog, le bon passage est retrouvé dans les six premiers 92 fois sur 100 avec le meilleur réglage.

## La balance qui pesait deux fois

Un seul essai de la mesure a échoué : le test « comme un utilisateur », qui fait lire au 30B une conversation de 32 000 tokens. Il s'est arrêté au bout de trois secondes sur une erreur 500, sans un mot au journal du service. Le soir, on l'a rejoué jusqu'à lire la réponse elle-même : « the keys and values of this prompt do not fit in memory ». La mémoire de conversation manquait, disait le moteur, dès 8 000 tokens. Pour ce modèle, 8 000 tokens en prennent moins d'un gigaoctet, sur un Mac qui en a 64.

La faute venait d'un garde-fou. Avant chaque nouveau bloc de mémoire de conversation, le moteur vérifie qu'il reste au Mac de quoi respirer : un huitième de sa mémoire, plus le poids du modèle. Sous Windows, d'où vient ce calcul, c'est juste : la mémoire libre que compte Windows contient encore les pages du modèle. Sur Mac, la mémoire libre que lit le moteur ne les contient plus, puisque le modèle s'en sert à chaque mot. C'est une balance qui retirerait deux fois le poids du panier : avec 18 Go de modèle et d'autres programmes ouverts sur le Mac, elle concluait au manque. L'original a d'ailleurs retravaillé ce garde-fou depuis que le fork s'en est détaché, à juste titre chez lui. L'erreur tient au portage, et on l'a commise.

La correction (7.0.2) ne retire plus que la marge. Pour le vérifier honnêtement, il a fallu recréer la gêne : un programme a tenu 22 Go de mémoire pendant l'essai. À la fin, il restait 22,5 Go disponibles, quand l'ancien garde-fou en exigeait 26. Le nouveau a laissé passer les trois longueurs, jusqu'à 32 157 tokens. Désormais, le premier refus écrit aussi ses chiffres au journal : la prochaine fois, on n'aura pas besoin de deviner.

Le test complet a ensuite été rejoué. Il compare des mesures prises à quelques heures d'écart, et non en alternance : c'est une indication, pas une course.

| Comme un utilisateur, 30B A3B | Gulliver (soir) | llama.cpp (midi) |
|---|---|---|
| Chargement jusqu'à une première réponse | 7,2 s | 5,7 s |
| Conversation de 32k : lecture | 863 tokens/s | 560 tokens/s |
| Conversation de 32k : écriture | 37,2 tokens/s | 33,3 tokens/s |
| 4 requêtes à la fois : écriture totale | 160 tokens/s | 175 tokens/s |
| 3 documents de 4 000 tokens : temps total | 13,7 s | 15,3 s |

## Le facteur

L'après-midi a changé la porte d'entrée. Jusqu'ici, on choisissait son profil dans une liste puis on tapait son mot de passe. Depuis la 7.0.0, on donne son adresse mail et son mot de passe, puis un code à 8 chiffres arrive par mail, à taper dans les dix minutes. C'est le principe de la banque en ligne : savoir le mot de passe ne suffit plus, il faut aussi avoir la boîte aux lettres. Un mot de passe oublié, ou le tout premier d'un profil neuf, se choisit par le lien d'un mail.

Le facteur, c'est la boîte Proton de l'humain. Proton Mail Bridge tourne déjà sur le Mac : c'est un petit bureau de poste local, qui reçoit les lettres en clair sur le Mac et les chiffre avant de les envoyer. Gulliver lui parle par un petit client de courrier qui ne s'adresse qu'au Mac lui-même. Les mots de passe restent rangés sous forme d'empreinte, avec 600 000 tours de calcul. Les codes et les liens ne vivent qu'en mémoire, et seule leur empreinte y est gardée.

Le premier essai réel a montré un trou. Le profil de l'humain avait un mot de passe, mais pas encore d'adresse. La page lui demandait donc son adresse et son mot de passe actuel, sans bouton « oublié ». Sans le mot de passe, on était coincé devant la porte. Ajouter ce bouton aurait été pire : n'importe qui sur le Wi-Fi de la maison aurait pu y donner sa propre adresse et prendre la place de l'administrateur. L'issue passe donc par le Mac. Un outil y inscrit l'adresse, puis « Mot de passe oublié » fait le reste, et la page l'explique désormais (7.0.1). Le contrôle complet de la connexion se poursuit demain, avec l'humain.

## La dernière mesure du lundi

Le soir, l'humain a tranché : « on va faire le test de vitesse une seule fois ». La mesure de chaque lundi, qui monopolisait le Mac pendant des heures pour un conseil qui ne changeait guère, est retirée. Celle du 23 novembre est la dernière. Les outils restent et se relancent à la main, quand un travail de vitesse le demande.

L'agent des modèles change de métier. Il ne télécharge plus rien de lui-même : il fait la veille. Chaque lundi à 1 heure, il regarde si un modèle installé a une nouvelle version, au catalogue ou sur Hugging Face, et si un modèle du catalogue tiendrait dans le Mac. Il écrit ce qu'il a trouvé dans un mail à l'administrateur, que le facteur de l'après-midi porte. L'humain décide ensuite de ce qui entre. Chaque nouveauté n'est annoncée qu'une fois, et une semaine sans nouveauté n'envoie rien. Ce premier soir, il n'y avait rien à annoncer : la nuit d'avant avait déjà tout téléchargé.

## Pour les curieux : les commandes et les chiffres

Le corps de la réponse 500, lu en rejouant le test sur un service d'essai isolé :

```
2000 ok 2093
8000 HTTP 500 b'{"error":{"message":"the keys and values of this prompt do not fit in memory","type":"server_error","param":null,"code":"out_of_memory"}}'
32068 HTTP 500 b'{"error":{"message":"the keys and values of this prompt do not fit in memory","type":"server_error","param":null,"code":"out_of_memory"}}'
```

Le garde-fou avant la correction, dans `engine/engine.cpp` : la réserve valait un huitième de la mémoire plus les poids, soit 8 + 18 = 26 Go pour le 30B.

```cpp
const uint64_t reserve = std::max<uint64_t>(uint64_t(1) << 30, mem.total / 8) + weights;
...
return now.commit_available == 0 || now.commit_available >= bytes + reserve;
```

La mémoire disponible que lit `read_memory_info()` additionne les pages libres, inactives et purgeables ; les pages des poids, lues à chaque token, sont actives et n'y figurent pas. Après la correction, le contrôle n'exige plus que `bytes + margin`.

| Version | Changement | Effet |
|---|---|---|
| 6.43.0 | deux lignes par groupe pour les experts aux rangées courtes | 30B A3B, écriture +1,6 % |
| 6.43.1 | l'erreur de mémoire en français dans la page | — |
| 6.44.0 | attention répartie, routeur à quatre sommes | 30B A3B : 82,8-85,6 → 90,6-91,7 tokens/s |
| 7.0.0 | connexion par adresse mail et code à 8 chiffres | — |
| 7.0.1 | l'administrateur au mot de passe oublié guidé | — |
| 7.0.2 | le garde-fou de la mémoire ne compte plus deux fois les poids | 32 157 tokens acceptés là où 8 000 étaient refusés |

La veille des modèles : `python3 dlib/dlib/external/gulliver/tools/modeles_semaine.py build/gulliver-home --essai` montre le mail qu'elle enverrait, sans l'envoyer ; mode d'emploi dans `macos/modeles-semaine/README.md`. La mesure arrêtée se relance à la main avec `tools/bench_hebdo.sh`.

Fichiers touchés, tous dans Gulliver ou dans les outils du fork : `engine/engine.cpp`, `engine/metal_kernels.metal`, `engine/metal_device.mm`, `tests/test_metal.cpp`, `host/mailer.h` et `host/mailer.cpp` (nouveaux), `host/accounts.h`, `host/accounts.cpp`, `host/store.h`, `host/store.cpp`, `host/settings.h`, `host/settings.cpp`, `host/server.cpp`, `web/js/profile.js`, `web/js/accounts.js`, `web/js/service.js`, `tools/modeles_semaine.py`, `tools/bench_service.py`, `tools/faux_smtp.py` (nouveau), `tools/test_comptes.py`, `tools/mot_de_passe.py`, `cloudflare/lib/routes.js`. **dlib n'a pas changé** dans cette étape.

## La suite

La connexion par mail sera essayée demain, de bout en bout, sur les appareils de l'humain. Côté vitesse, il reste les mêmes chantiers : fusionner les petits calculs de chaque couche, donner aux têtes de 512 valeurs de Gemma 4 leur propre attention, et réduire l'écart qui reste avec MLX sur les très longues questions.
