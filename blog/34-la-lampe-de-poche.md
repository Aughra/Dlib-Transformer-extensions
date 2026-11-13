# Dlib Transformer Extensions sur macOS, épisode 34 : la lampe de poche

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Qwen3.5-4B : écriture **18 à 19 tokens/s** ; Gemma 4 E2B, sur le processeur : écriture **21 à 24 tokens/s**, lecture **143 à 153 tokens/s** sur 512 tokens.

Suite de [l'épisode 33](33-le-carnet-ouvre-les-yeux.md) : Qwen 3.5 lisait les images et se servait d'outils. Dans la liste des modèles que le moteur refusait, dressée à [l'épisode 26](26-le-carnet-de-notes.md), venait ensuite Gemma 4, la famille de Google : 36 dépôts populaires, 19 millions de téléchargements. On l'a fait entrer.

## Un immeuble aux étages différents

Jusqu'ici, les modèles que lit le moteur ressemblaient à des immeubles dont tous les étages ont le même plan. Gemma 4 E2B, le plus petit, a 35 étages, et ils ne se ressemblent pas :

- quatre étages sur cinq regardent le texte avec une **lampe de poche** : ils ne voient que les 512 derniers mots, ce qui suffit pour suivre une phrase et coûte peu ; le cinquième allume le **plafonnier** et voit tout le texte depuis le début ;
- les uns et les autres n'ont pas la même taille de pièces : des têtes d'attention de 256 valeurs pour la lampe de poche, de 512 pour le plafonnier, et deux façons différentes de repérer la place de chaque mot ;
- les 20 derniers étages ne prennent pas de notes : ils **lisent le cahier** du dernier étage du même type qui en tient un, au lieu de remplir le leur ;
- et chaque étage reçoit, en plus de ce qui monte de l'étage précédent, **un petit mot glissé sous la porte** : quelques valeurs propres à chaque mot du texte et à chaque étage, tirées d'une grande table rangée dans le fichier.

Rien de tout cela n'est très compliqué pris à part. Mais le moteur supposait partout que les étages se ressemblent : la taille des notes, la façon de tourner les positions, la largeur des pièces. Il a fallu lui apprendre à demander, à chaque étage, comment celui-ci est fait.

Pour le cahier des notes (les clés et valeurs), on a choisi la solution simple : chaque étage qui écrit reçoit des lignes de 512 valeurs, et un étage à lampe de poche n'en remplit que la moitié. C'est un peu de papier perdu, mais Gemma 4 E2B n'a qu'une tête de clés par étage, et ses notes tiennent en 16 Ko par mot.

## Un dictionnaire de plus

Avant même le modèle, il a fallu le dictionnaire. Gemma 4 découpe le texte à sa manière : il remplace les espaces par un trait souligné (« ▁ »), comme les anciens modèles de Google, mais assemble ensuite les lettres selon une liste de 514 906 fusions classées par priorité, comme les modèles récents. Le découpeur de dlib connaissait l'une et l'autre méthode, pas leur mélange. Une troisième sorte a été ajoutée, d'une quarantaine de lignes.

Sur 22 textes choisis pour piéger (accents, émojis, chinois, code, espaces multiples, sauts de ligne en série), le découpage est identique, token par token, à celui de llama.cpp, la référence. Ces textes sont maintenant une étape des tests d'Albireo.

## Le premier mot de travers

Le premier essai a répondu par une suite d'espaces. Pour savoir où le calcul déraillait, on a fait afficher, étage par étage, les mêmes nombres par Albireo et par llama.cpp sur la phrase « The capital of France is ». Les 14 premiers étages étaient d'accord à quelques centièmes près. Au quinzième, la première ligne s'écartait.

Fausse alerte, en partie : cette première ligne, celle du mot de début de texte, est d'une sensibilité extrême à cet étage. Albireo lui-même ne trouve pas la même valeur selon qu'il calcule en entiers ou en flottants exacts (0,26 ou 0,33, contre 0,12 pour llama.cpp). Et elle ne sert à rien d'autre qu'à elle-même : les étages suivants ne prennent pas de notes, donc personne ne la relit. La ligne qui compte, celle du dernier mot, suivait la référence jusqu'en haut, et le moteur proposait bien « Paris ».

Le vrai coupable était ailleurs : le moteur ne connaissait pas encore la façon de converser de Gemma 4, ses tours ouverts par `<|turn>user` et fermés par `<turn|>`, et lui parlait avec les balises d'un autre modèle. Une fois ce format écrit, à l'octet près comme le gabarit du modèle, la réponse est venue : « La capitale de la France est Paris. »

## La lampe de poche à l'épreuve

Une question courte ne met pas la lampe de poche à l'épreuve : tant que le texte compte moins de 512 mots, elle éclaire tout. On a donc préparé un texte de 2 210 tokens, 72 fiches du genre « Fiche 1000 : Zoé habite à Dijon… », et demandé à la fin le numéro de la fiche de Zoé, écrite tout au début. Seuls les étages à plafonnier peuvent encore la voir.

Les deux moteurs ont répondu la même phrase, mot pour mot : « Le numéro de la fiche de Zoé est la Fiche 1000 et elle habite à Dijon. »

Sur trois questions plus ordinaires, les réponses d'Albireo et de llama.cpp commencent pareil puis bifurquent, chaque fois sur un mot où la référence elle-même hésitait : « dans » à 57 % contre « présentes » à 43 %, « La Pomme » contre « Pomme » à 52 contre 48 %, « Standard » contre « moderate » à 29 contre 25 %. Les deux moteurs ne rangent pas leurs notes avec la même précision (8 bits pour Albireo, 16 pour llama.cpp), et sur une quasi-égalité, cela suffit à faire pencher la balance.

## Ce qui manque encore

- **Le GPU** : Gemma 4 tourne sur le processeur seulement. Ses têtes de 512 valeurs, ses lampes de poche et ses petits mots sous la porte demandent des noyaux nouveaux ; ce sera pour le Mac mini.
- **Les outils et les images** : Gemma 4 a ses propres bons de commande et son propre projecteur, pas encore lus.
- **Les grandes tailles** : seul le E2B a été essayé. La version à mélange d'experts (26B-A4B) est refusée proprement, et la recherche de modèles le signale.

## Pour les curieux : les chiffres

- Modèle : `gemma-4-E2B-it-Q4_K_M.gguf` du dépôt unsloth/gemma-4-E2B-it-GGUF, 3,1 Go, 35 couches de largeur 1 536, 8 têtes de requêtes et 1 de clés, 4,6 milliards de paramètres dont 2,3 dans la table des petits mots (en Q5_K).
- Référence : llama.cpp 0.9.0 (Homebrew), `llama-server -ngl 0`, choix glouton ; tenseurs intermédiaires par `llama-eval-callback`, gabarit par `/apply-template`.
- Prompt rendu : identique au gabarit Jinja sur six conversations (avec et sans message système, plusieurs tours, raisonnement demandé, réponse précédente avec sa pensée).
- Vitesse sur le processeur (8 threads, clés en 8 bits) : écriture 22,5 et 23,3 tokens/s (`bench_runtime`, 64 étapes), 20,9 et 23,8 tokens/s (`engine_chat`, 180 tokens) ; lecture d'un prompt de 512 tokens 143 et 153 tokens/s ; `lowpowermode 0`, charge 7 à 8 ; clés et valeurs : 16 320 octets par token.
- Le test complet d'Albireo compte maintenant 32 étapes, dont « gemma 4 tokenizer », « gemma 4 » et « gemma 4 reasoning » (17 fois 23 : 391, avec une pensée tenue à part de la réponse).

## À suivre

Le Mac mini arrive dans deux semaines. D'ici là, Gemma 4 pourrait apprendre à se servir d'outils, sur le processeur.
