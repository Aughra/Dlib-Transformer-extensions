# Dlib Transformer Extensions sur macOS, épisode 37 : le tiroir voisin

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; sur le processeur, LFM2.5-8B-A1B : écriture **34 tokens/s** ; Gemma 3 1B : écriture **25 à 28 tokens/s**, lecture **396 à 436 tokens/s** sur 512 tokens ; Gemma 3 4B : écriture **16 à 17 tokens/s**, lecture **88 à 92 tokens/s**.

Suite de [l'épisode 36](36-le-post-it-et-la-photocopie.md) : LFM2 lisait le texte, et le moteur avait appris à tourner ses valeurs avant de les arrondir. Il restait à LFM2 ses outils, et une famille voisine attendait : Gemma 3.

## Des appels écrits comme en Python

Gemma 4 avait son formulaire à lui, avec ses propres guillemets. LFM2 a choisi une autre convention, celle des programmeurs Python : pour demander la météo, il écrit `[get_weather(city='Paris')]`, comme on appellerait une fonction dans un programme. Les crochets permettent d'en appeler plusieurs d'un coup, et les valeurs suivent les habitudes de Python : `True` plutôt que `true`, `None` pour « rien », des apostrophes autour des textes.

Le moteur sait maintenant lire ces appels, et les réécrire dans la conversation quand elle revient. La difficulté était là : les trois modèles essayés n'ont pas le même gabarit. Celui du 8B réécrit les appels passés, celui du 1.2B ne garde que le texte des réponses, et celui de l'Audio entoure les résultats de marqueurs à lui. C'est comme trois administrations qui demandent le même renseignement sur trois formulaires différents. On a comparé chaque cas au texte que produit llama.cpp : les douze rendus sont identiques, y compris un appel qui contient un nom à apostrophe (« O'Hara »), une liste et un objet.

Le petit modèle appelle l'outil, reçoit « Sunny, 21 degrees C » et répond « The current weather in Paris is sunny with a temperature of 21 degrees Celsius. », la phrase de llama.cpp, avec le même nombre de tokens aux deux tours.

## Le tiroir voisin

Gemma 3 est le grand frère de Gemma 4. Il en a presque tout : quatre normes par couche, la même activation, les mêmes fenêtres glissantes, où cinq couches sur six ne regardent que les derniers mots. Le moteur savait déjà faire tout cela pour Gemma 4 ; pour Gemma 3, il suffisait d'ouvrir le tiroir voisin et d'y changer quelques réglages :

- **pas de norme des valeurs**, que Gemma 4 ajoute et que Gemma 3 n'a pas ;
- **une échelle d'attention** propre, fixée par la largeur des têtes (et autrement pour le 27B, comme le fait le code de référence de Google) ;
- **une rotation à deux vitesses** : les couches à fenêtre tournent sur une base de 10 000, les couches globales sur une base de 1 000 000, ralentie huit fois sur le 4B pour lire de longs textes.

Le format de conversation, `<start_of_turn>user` puis `<start_of_turn>model`, est l'un des plus simples du moteur. Le découpage du texte en tokens n'a demandé aucun changement : sur les 22 textes de test de Gemma 4, Gemma 3 donne les mêmes tokens que llama.cpp.

## La tête unique

Les premières comparaisons ont surpris : le 1B bifurquait parfois sur des mots où llama.cpp ne doutait guère, comme une virgule qu'il choisissait à 81 %. On venait de corriger l'arrondi des valeurs, et le soupçon est revenu aussitôt.

Cette fois, ce n'était pas une erreur. En demi-précision, Albireo retrouvait la phrase de llama.cpp. Et llama.cpp, avec son propre cache sur 8 bits, rotation comprise, bifurquait lui aussi, et même plus tôt. Gemma 3 1B n'a qu'une seule tête de clés par couche, de 256 valeurs, que se partagent ses quatre têtes de requêtes : chaque arrondi y pèse davantage, comme une seule paire de lunettes pour toute une table. Les comparaisons de cet épisode se font donc en demi-précision, comme llama.cpp par défaut, et les réponses suivent la référence jusqu'à des hésitations réelles (52 % contre 48 % pour le 4B).

Sur le journal piégé de l'épisode précédent, 2 816 tokens, bien au-delà de la fenêtre de 512 positions, les deux modèles donnent la même réponse que llama.cpp. Pour le 1B, elle est d'ailleurs fausse : il retrouve le mot de code, puis se trompe de voyageur, exactement comme la référence. Un petit modèle reste un petit modèle.

## L'octet orphelin

Un poème du 1B a fait planter le script de comparaison : la réponse contenait un octet seul, la première moitié d'une espace insécable, celle que le français place devant un point d'exclamation. Le moteur l'avait-il coupée en deux ? Non : llama.cpp écrit le même octet orphelin. C'est le modèle qui commence le caractère et ne le finit pas, comme une lettre accentuée dont on aurait oublié l'accent. Le service d'Albireo remplace cet octet par « � » dans ses réponses ; seul l'outil d'essai, qui écrit tout tel quel dans le terminal, le laissait passer.

## Pour les curieux : les chiffres

- Modèles : `gemma-3-1b-it-Q4_K_M.gguf` (26 couches, largeur 1 152, 4 têtes de 256 et 1 tête de clés, fenêtre de 512 positions) et `gemma-3-4b-it-Q4_K_M.gguf` (34 couches, 8 têtes et 4 de clés, fenêtre de 1 024, mise à l'échelle linéaire ×8 des couches globales), du dépôt unsloth.
- Bifurcations du 1B en demi-précision, probabilités de llama.cpp : 0,563 / 0,436 ; 0,168 / 0,125 ; 0,46 / 0,184 ; 0,602 / 0,398. En 8 bits : « , » à 0,808, que le choix d'Albireo n'approchait pas.
- Outils de LFM2 : quatre conversations rendues comme llama.cpp pour trois gabarits (1.2B, 8B, Audio), une cinquième pour le 8B ; avec le 8B, qui raisonne avant d'appeler, même appel et même réponse finale, les deux pensées bifurquant vers leur fin.
- Vitesse sur le processeur, en alternance (charge 3,3 à 7,8) : 1B, écriture 28,2 et 25,0 tokens/s, lecture de 512 tokens 436 et 396 ; 4B, 17,2 et 16,3, lecture 92 et 88.
- Le test complet d'Albireo compte maintenant 46 étapes.

## À suivre

Gemma 3 sait aussi regarder des images, avec un projecteur de la famille SigLIP que le moteur ne lit pas encore. Ce sera probablement la prochaine étape, avant l'arrivée du Mac mini.
