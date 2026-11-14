# Dlib Transformer Extensions sur macOS, épisode 35 : le formulaire et la mosaïque

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; Qwen3.5-4B : écriture **18 à 19 tokens/s** ; Gemma 4 E2B, sur le processeur : écriture **21 à 24 tokens/s**, lecture **143 à 153 tokens/s** sur 512 tokens, encodage d'une image de 121 tokens en **0,8 s**.

Suite de [l'épisode 34](34-la-lampe-de-poche.md) : Gemma 4 lisait le texte, sur le processeur. Il lui manquait deux choses que Qwen 3.5 avait déjà : se servir d'outils, et voir les images. On lui a donné les deux, dans la même matinée.

## Un formulaire à cases maison

Pour appeler un outil, un modèle remplit une sorte de bon de commande : le nom de l'outil, puis ses paramètres. La plupart des modèles écrivent ce bon en JSON, la notation la plus répandue, ou en XML, comme Qwen 3.5. Gemma 4 a son propre formulaire, avec ses propres cases :

```
<|tool_call>call:get_weather{city:<|"|>Paris<|"|>}<tool_call|>
```

Les clés n'ont pas de guillemets, et les textes sont encadrés par un signe spécial, `<|"|>`, qui est un seul mot du vocabulaire du modèle. C'est un peu comme un formulaire administratif qui aurait inventé ses propres guillemets : rien de difficile, mais il faut le savoir. Les outils disponibles sont décrits de la même manière dans le tour système, avec les types en majuscules (`STRING`, `OBJECT`), et le résultat d'un outil revient sous la forme `response:get_weather{value:<|"|>…<|"|>}`.

Une fois son bon rempli, le modèle écrit un dernier mot, `<|tool_response>`, et s'arrête : il pose le stylo et attend la réponse du guichet. Le moteur doit reconnaître ce mot comme une fin de tour, sinon le modèle inventerait lui-même la réponse de l'outil.

Le moteur sait maintenant écrire ce formulaire, et le relire quand le modèle le remplit. Pour vérifier l'écriture, on a comparé huit conversations, des plus simples aux plus retorses (paramètres imbriqués, listes d'objets, deux appels à la suite, plusieurs tours avec raisonnement), au texte que produit le gabarit du modèle. Les huit sont identiques à l'octet près.

## Deux choix et un défaut

La comparaison s'est faite avec deux références : Jinja2, l'interpréteur de gabarits qu'utilise la bibliothèque de Hugging Face, et llama.cpp. Elles ne sont pas tout à fait d'accord entre elles, et il a fallu choisir.

- **L'ordre des clés.** Le gabarit trie les paramètres par ordre alphabétique. Jinja2 ignore les majuscules (« alpha » avant « Units »), llama.cpp non (« Units » avant « alpha »). On a suivi llama.cpp, la référence de tous les essais du portage ; le cas est rare.
- **Les pensées passées.** Quand le modèle a raisonné avant d'appeler un outil, faut-il garder ce raisonnement dans les tours suivants ? Le gabarit prévoit une option pour cela, éteinte par défaut ; llama.cpp l'allume. Le premier essai l'avait éteinte, et une conversation sur huit différait. On l'a allumée, pour une raison de plus que l'accord avec llama.cpp : le début de la conversation reste alors le même d'une question à l'autre, et le moteur le relit depuis son cache, comme on rouvre un livre au marque-page plutôt qu'à la première page.

Le gabarit lui-même a un défaut, que l'on reproduit tel quel parce que c'est la règle : quand une réponse contient une phrase **et** un appel d'outil, il range la phrase après le résultat de l'outil et ferme le tour sans en rouvrir un. Le modèle se retrouve devant une porte fermée. Dans les essais, Gemma 4 n'a jamais écrit de phrase avant d'appeler un outil ; on surveillera.

Résultat : à « Quel temps fait-il à Paris ? », Gemma 4 E2B appelle l'outil météo, reçoit « Sunny, 21 degrees C » et répond « The weather in Paris right now is sunny with a temperature of 21 degrees C. », exactement la phrase de llama.cpp.

## Des yeux avec des garde-fous

Pour voir, un modèle de langage s'aide d'un **projecteur** : un second petit modèle qui transforme l'image en une suite de « mots » qu'il sait lire. Celui de Gemma 4 E2B pèse un gigaoctet et ne ressemble à aucun des trois que le moteur connaissait.

D'abord, il a des **garde-fous**. Chacun de ses calculs ramène ses entrées et ses sorties entre deux bornes, inscrites dans le fichier : tout nombre au-delà de 3,5, par exemple, est ramené à 3,5, comme une voiture bridée qui ne dépasse jamais une certaine vitesse. Les bornes vont de 2,3 à 22 selon les étages, et elles agissent vraiment : sans elles, le calcul serait faux.

Ensuite, il repère chaque morceau de l'image comme on repère une maison dans une ville quadrillée : un numéro de rue et un numéro d'avenue. Deux tables de 10 240 lignes, l'une pour les colonnes et l'autre pour les lignes, donnent à chaque carreau de 16 pixels son adresse.

Enfin, il regarde l'image comme une **mosaïque** : à la fin, chaque carré de 3 × 3 carreaux est fondu en un seul mot. Une photo de 512 pixels de côté devient ainsi une grille de 11 × 11 mots, 121 en tout, que le modèle lit avant la question.

Un détail rappelle l'épisode précédent : à chaque étage, Gemma 4 reçoit un petit mot glissé sous la porte, tiré du mot du texte. Mais un morceau d'image n'est pas un mot du dictionnaire. llama.cpp lui donne le petit mot du mot numéro 0, celui qui sert de remplissage, une feuille blanche en somme, et note lui-même dans son code que ce choix reste « à vérifier ». On a fait comme lui.

## L'étiquette sur un autre tiroir

Le premier essai s'est arrêté net : « le fichier ne contient pas d'encodeur d'images ». Il en contenait bien un, mais aussi un encodeur de son, et il range donc le type de chacun dans son propre tiroir, `clip.vision.projector_type` pour les images, au lieu du tiroir commun que lisait le moteur. Une ligne a suffi pour regarder aussi dans le bon tiroir ; la même étourderie se trouvait dans la fiche des modèles du service, corrigée de même.

Au second essai, le perroquet des images de compte de macOS est devenu « a vibrant red macaw, showcasing its bright plumage and distinctive facial features ».

## Quatre images à l'épreuve

On a comparé Albireo et llama.cpp sur quatre images : le perroquet carré, un zèbre recadré en largeur, le même en hauteur, et le perroquet agrandi à 1 600 × 1 000 pixels, que le moteur doit réduire pour rester sous 280 mots.

Les deux moteurs comptent exactement le même nombre de mots dans les quatre prompts, ce qui veut dire que les images sont découpées à la même taille. La grande image donne la même description, mot pour mot. Les trois autres bifurquent, chaque fois sur un mot où la référence hésitait : « its » à 47,7 % contre « a » à 46,8 %, « image » à 57 % contre « is » à 43 %. Pour le zèbre en largeur, llama.cpp a d'ailleurs changé d'avis entre deux requêtes identiques, « clearly showing » puis « showcasing », le mot qu'avait choisi Albireo.

Côté vitesse, l'encodage prend 0,8 s pour le perroquet de 121 mots et 2,5 à 2,8 s pour la grande image de 273 mots. Une première mesure isolée avait donné 8,6 s, sur une machine chargée ; refaite deux fois, en alternance avec la petite image, elle a donné 2,5 et 2,8 s. C'est la raison d'être de la règle des mesures alternées.

Dernière épreuve, celle de l'usage réel : un service d'essai a ajouté Gemma 4 E2B et son projecteur depuis Hugging Face, comme le fait la page avec la case « Avec la lecture des images », puis décrit le perroquet en français : « Cette image est un gros plan d'un perroquet aux couleurs vives, caractérisé par un plumage rouge éclatant et un bec jaune et noir. »

## Ce qui manque encore

- **Le GPU** : Gemma 4 et son projecteur tournent sur le processeur seulement ; ce sera pour le Mac mini.
- **Le son** : le projecteur contient un encodeur de son (12 étages), que le moteur ne lit pas.
- **Les grandes tailles** : seul le E2B a été essayé ; la version à mélange d'experts (26B-A4B) reste refusée.

## Pour les curieux : les chiffres

- Projecteur : `mmproj-F16.gguf` du dépôt unsloth/gemma-4-E2B-it-GGUF, 985 654 080 octets ; encodeur d'images de 16 couches de largeur 768, 12 têtes de 64, patchs de 16 pixels, regroupement 3 × 3, sortie de 1 536 valeurs par mot.
- Référence : llama.cpp 0.9.0 (Homebrew), `llama-server -ngl 0 --no-mmproj-offload --jinja --mmproj … --image-max-tokens 280`, choix glouton ; gabarit comparé par `/apply-template` et par Jinja2 3.1.6, installé dans un environnement Python jetable, hors du dépôt.
- Tokens de prompt (Albireo et llama.cpp) : 139 (perroquet 512 × 512), 122 (zèbre 640 × 360), 96 (zèbre 300 × 640), 291 (perroquet 1 600 × 1 000, ramené à 21 × 13 mots).
- Détail des images dans Albireo : 140 mots au plus en « low », 280 en « auto », 1 120 en « high », 70 au moins.
- Le test complet d'Albireo compte maintenant 36 étapes, dont « gemma 4 tool call », « gemma 4 tool result », « gemma 4 image » et « gemma 4 wide image ».

## À suivre

Le Mac mini arrive dans deux semaines. D'ici là, il reste à choisir entre les noyaux GPU de Gemma 4, qui se régleront mieux sur la nouvelle machine, et d'autres familles de modèles encore refusées.
