# Dlib Transformer Extensions sur macOS, épisode 36 : le post-it et la photocopie

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; sur le processeur, Gemma 4 E2B : écriture **21 à 24 tokens/s** ; LFM2.5-1.2B : écriture **35 à 36 tokens/s**, lecture **253 à 281 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B : écriture **34 tokens/s**, lecture **141 à 149 tokens/s**.

Suite de [l'épisode 35](35-le-formulaire-et-la-mosaique.md) : Gemma 4 savait se servir d'outils et lire des images. Restait à choisir la famille de modèles suivante parmi celles que le moteur refusait. On a pris LFM2, de Liquid AI, et c'est elle qui a fini par montrer un défaut ancien du moteur.

## Trop grands pour la maison

Le service d'Albireo sait déjà interroger Hugging Face. On lui a demandé quelles familles il refusait, avec la taille de leurs modèles. Les trois plus téléchargées, Qwen 4 expérimental, DeepSeek 4.1 et GLM 5.3, font de 165 à 755 milliards de paramètres. Même le Mac mini et ses 64 Go ne les logeraient pas : c'est un piano à queue devant la porte d'un studio.

Venait ensuite LFM2, une famille de petits modèles, de 230 millions à 2,6 milliards de paramètres, et deux modèles à experts, le 8B-A1B et le 24B-A2B, ce dernier téléchargé plus d'un million de fois. Le 8B-A1B n'utilise qu'un milliard de paramètres par mot : il tient dans 5 Go et écrit presque aussi vite qu'un petit modèle.

## Un post-it au lieu d'un cahier

La plupart des modèles relisent tout le texte à chaque mot, grâce à l'attention. LFM2 n'en garde que six couches sur seize. Les dix autres font une **convolution courte** : chaque mot est mélangé avec les deux qui le précèdent, et c'est tout. Pour suivre la conversation, ces couches n'ont besoin que d'un post-it, deux mots par couche, là où l'attention tient un cahier qui grossit à chaque page.

Le moteur savait déjà garder un état de ce genre, pour Qwen 3.5 : il le range à part pour chaque conversation, le reprend au bon endroit et le photographie toutes les 256 positions. On y a glissé le post-it de LFM2. Le calcul lui-même tient en une quarantaine de lignes.

Le modèle à experts ajoute une nuance. Son aiguilleur ne fait pas voter les experts entre eux, comme chez Qwen : il note chacun séparément, de 0 à 1, ajoute un petit bonus propre à chaque expert pour choisir les quatre meilleurs, puis les pèse sans ce bonus. Et ses deux premières couches se passent d'experts.

## Le mot de passe oublié

Premier essai : « The capital of France is Paris. » Mais comparé à llama.cpp, la référence de tous nos essais, le modèle répondait souvent autrement dès le premier mot, et pas sur des hésitations : « Certainly! », qu'Albireo choisissait, ne figurait même pas parmi les deux premiers choix de llama.cpp.

Les deux moteurs ne lisaient pas le même prompt : 20 tokens d'un côté, 21 de l'autre. Le gabarit de LFM2 commence par un mot spécial, `<|startoftext|>`, le signal « début de texte ». Albireo prenait LFM2 pour un modèle de la famille de Qwen, qui n'en a pas, et l'oubliait. C'est comme entrer dans une conversation sans dire bonjour : on vous répond, mais pas tout à fait de la même façon.

LFM2 a reçu son propre format, qui dit bonjour, et suit le gabarit du modèle pour les pensées des réponses passées. Huit conversations ont été comparées au texte que produit llama.cpp : les huit sont identiques, pour le petit modèle comme pour le grand. Les réponses, elles, suivent llama.cpp mot pour mot jusqu'à un endroit où la référence elle-même hésite, comme « -la » à 26 % contre « -m » à 21 %.

## La photocopie trop contrastée

Restait un test plus dur : un journal de 120 enregistrements, 2 600 tokens, avec un mot de code glissé au milieu, puis deux questions, le mot de code et le voyageur de l'enregistrement 3. Le petit modèle répondait juste, comme llama.cpp. Le modèle à experts retrouvait le mot de code, mais nommait « Inès » au lieu de « Gaëlle ».

Pour savoir qui se trompait, on a donné à llama.cpp le début exact du raisonnement d'Albireo, jusqu'au nom. Il choisissait « Gaëlle » à 74 % et « Inès » à 9 %. Ce n'était donc pas une hésitation du modèle, mais une perte d'information dans le moteur.

Le coupable était un réglage ancien : pour gagner de la mémoire, Albireo arrondit sur 8 bits les clés et les valeurs que l'attention relit. Avant l'arrondi, il fait tourner les clés, une opération qui étale les nombres extrêmes, comme on mélange les cartes avant de les ranger en piles égales. Il ne le fait pas pour les valeurs. llama.cpp fait tourner les deux. La preuve est venue de llama.cpp lui-même : avec sa rotation coupée, il répondait lui aussi « Inès ». Un essai rapide, qui fait aussi tourner les valeurs dans Albireo, a retrouvé « Gaëlle ».

C'est une photocopie trop contrastée : le texte principal reste lisible, mais un petit nom en marge disparaît. Et LFM2 y est plus sensible que d'autres, parce que ses six couches d'attention portent seules toute la mémoire du texte. L'erreur ne vient pas du portage de LFM2 : elle est dans le code d'origine, et ce modèle n'a fait que la montrer.

Pour l'instant, LFM2 garde ses clés et valeurs en demi-précision. Avec si peu de couches d'attention, cela ne coûte que 12 Ko par mot. La vraie correction, faire tourner les valeurs partout, sur le processeur comme sur le GPU, attend son tour ; elle est décrite à l'auteur de l'original.

## Deux petites portes mal fermées

L'essai de bout en bout, un service jetable qui ajoute LFM2 depuis Hugging Face comme le fait la page, a trouvé deux défauts de plus, tous deux du fork.

- **L'étagère déménagée.** Un ajout envoyé sans le champ du projecteur, qui est facultatif, était refusé avec un message absurde. Lire ce champ absent le créait, et cette création déplaçait en mémoire les deux autres champs, déjà lus : c'est noter l'adresse d'un livre, puis voir l'étagère déménager avant d'aller le chercher. La page envoyait toujours ce champ, vide, et n'en souffrait pas.
- **Les pièces détachées.** Le dépôt de LFM2.5-Audio range, à côté du modèle, son oreille, sa voix et son propre découpeur de sons, dans des fichiers du même format. La recherche écartait déjà l'oreille, mais proposait la voix et le découpeur comme des modèles. Ils sont maintenant écartés eux aussi ; le modèle Audio lui-même converse en texte.

## Pour les curieux : les chiffres

- Modèles : `LFM2.5-1.2B-Instruct-Q8_0.gguf` (1 246 253 888 octets ; 16 couches, dont 10 convolutions et 6 attentions, largeur 2 048, 32 têtes de 64, 8 têtes de clés) et `LFM2.5-8B-A1B-Q4_K_M.gguf` (5 155 564 768 octets ; 24 couches, dont 18 convolutions ; 32 experts, 4 par token, après 2 couches denses), du compte LiquidAI.
- Référence : llama.cpp 0.9.0, `llama-server -ngl 0 --jinja`, choix glouton, probabilités par `top_logprobs` ; rotation coupée par `LLAMA_ATTN_ROT_DISABLE=1` avec un cache `-ctk q8_0 -ctv q8_0`.
- Conversation reprise depuis l'état : 49 puis 97 tokens repris aux tours 2 et 3, réponses identiques à celles de llama.cpp, qui relit tout.
- Vitesse, processeur, 8 threads, en alternance, machine reposée (charge 3,0 à 5,7) : 1.2B Q8_0, prompt de 512 tokens 281 et 253 tokens/s, écriture 36,1 et 35,3 ; 8B-A1B Q4_K_M, 149 et 141 tokens/s, écriture 34,3 et 34,2. Une première série, prise juste après 40 minutes de tests, avait donné de 19 à 35 tokens/s : un MacBook Air sans ventilateur ralentit quand il chauffe, et on l'a écartée.
- Le test complet d'Albireo compte maintenant 41 étapes, dont « lfm2 moe long prompt », qui rejoue le journal piégé : à une ligne vide près en fin de texte, les deux formats répondaient juste, et l'étape ne protégeait plus de rien.

## À suivre

Il reste à LFM2 ses outils, écrits comme des appels de fonctions Python, et le GPU. Le moteur, lui, doit encore faire tourner ses valeurs, ce qui profitera à tous les modèles. Le Mac mini arrive dans moins de deux semaines.
