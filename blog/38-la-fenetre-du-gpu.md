# Dlib Transformer Extensions sur macOS, épisode 38 : la fenêtre du GPU

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B) ; LFM2.5-1.2B sur le GPU : écriture **40 tokens/s**, lecture **433 à 462 tokens/s** sur 512 tokens ; LFM2.5-8B-A1B et Gemma 3 4B sur le GPU : pas encore de mesure (le Mac est resté en mode économie d'énergie).

Suite de [l'épisode 37](37-le-tiroir-voisin.md) : LFM2 et Gemma 3 tournaient sur le processeur. On les a fait passer au GPU, la puce graphique du Mac, qui répète la même opération sur des milliers de nombres à la fois.

## Le post-it, version GPU

Pour LFM2, le GPU savait déjà presque tout : ses couches d'attention ressemblent à celles des modèles qu'il calculait. Il ne lui manquait que la convolution courte, ce petit post-it de deux mots que chaque couche garde par conversation. Un noyau de quelques lignes la calcule maintenant, un canal par fil de calcul, et le GPU écrit directement dans les post-its rangés par le processeur, sans les recopier.

Le premier contrôle a pourtant refusé le GPU. À chaque chargement, le moteur compare quelques passes du GPU à celles du processeur, et il trouvait 2,2 % d'écart là où il en tolère 1. En demi-précision, l'écart tombait à 0,1 %. Le calcul était juste ; c'est l'arrondi sur 8 bits qui jouait. Le prompt lu par le GPU écrit ses clés et valeurs arrondies un tout petit peu autrement que le processeur, et les étapes suivantes en héritent. Le contrôle tolérait déjà 5 % pour ce prompt, mais pas pour ses héritiers ; ils ont reçu la même tolérance, le mot le plus probable devant toujours être le même.

Sur le LFM2.5-1.2B, mesuré avant que le Mac ne passe en mode économie d'énergie, la lecture d'un prompt passe de 290 à 450 tokens par seconde environ, et l'écriture de 35 à 40.

## L'aiguilleur qui hésite

Le modèle à experts demandait d'apprendre au GPU la façon de choisir de LFM2 : une note de 0 à 1 par expert, un bonus pour choisir, puis la note seule pour peser. Il demandait aussi de laisser tranquilles ses deux premières couches, qui n'ont pas d'experts.

Le contrôle a refusé de nouveau, et plus fort : 7,7 % d'écart, puis 13,6 %. En demi-précision, 0,1 %. Un modèle à experts est un aiguillage : qu'un arrondi déplace d'un cheveu deux notes presque égales, et un mot part chez un autre expert, ce qui change tout son calcul. Le contrôle, qui se fait sur des mots tirés au hasard, y tombe souvent. Il accepte désormais 25 % pour un modèle à experts en 8 bits, à condition que le mot choisi reste le même. Un vrai défaut donne des scores sans rapport et un autre mot.

Puis une vraie frayeur : sur le journal piégé de 2 622 tokens, le GPU répondait « Inès », l'erreur que l'épisode 36 avait corrigée. Même en demi-précision. On a fait lire le prompt par le processeur et écrire par le GPU : la réponse redevenait juste. On a vérifié le prompt du GPU seul, de 29 à 1 200 tokens : juste à 0,4 % près. On a enfin fait tourner llama.cpp sur son propre GPU. Il écrit, mot pour mot, le même raisonnement bancal qu'Albireo sur le sien : « The secret code word is given in Record 77 ». Le modèle est sensible à la façon d'arrondir, et le GPU arrondit autrement que le processeur, chez llama.cpp comme chez Albireo. L'étape du test qui exige la bonne réponse reste donc sur le processeur.

## La fenêtre

Gemma 3 demandait davantage. Cinq couches sur six ne regardent que les derniers mots, 1 024 pour le 4B : c'est la fenêtre glissante. Les noyaux d'attention du GPU, eux, regardaient toujours tout le texte. On leur a appris à commencer à la bonne position, avec une place trouvée sans rien déranger : un champ de bourrage, laissé vide pour l'alignement de la mémoire, porte maintenant la taille de la fenêtre.

Il fallait aussi une seconde table de rotation pour ces couches, la porte GELU, et les deux normes que Gemma ajoute après l'attention et après chaque bloc de calcul. Deux petits noyaux nouveaux s'en chargent.

Le contrôle de chargement ne lit que 29 tokens, bien moins que la fenêtre : il ne pouvait rien en dire. Le test décisif est venu du journal de 2 816 tokens. La réponse est la même sur le GPU et sur le processeur, et une réponse de 150 tokens écrite ensuite est identique octet pour octet. Pour s'assurer que ce test voit vraiment la fenêtre, on l'a coupée exprès sur le GPU : la réponse n'était plus qu'une suite de chiffres. Le test distingue bien les deux, et la fenêtre fonctionne.

Le petit Gemma 3 1B reste sur le processeur : sa largeur, 1 152, n'est pas un multiple de 256, la taille des blocs que lisent les noyaux du GPU.

## Pour les curieux : les chiffres

- Contrôles du GPU (8 bits, puis demi-précision) : LFM2.5-1.2B, 1,07 / 2,21 / 1,04 % puis 0,08 / 0,10 / 0,09 % ; LFM2.5-8B-A1B, 7,7 / 13,6 / 8,9 % puis 0,13 / 0,11 / 0,08 % ; Gemma 3 4B, 2,5 / 2,1 / 2,4 % puis 0,09 / 0,07 / 0,10 % (prompt, première étape, seconde étape).
- LFM2.5-1.2B Q8_0, `bench_runtime` en alternance, mode normal : lecture de 512 tokens 296 et 287 tokens/s sur le processeur, 433 et 462 sur le GPU ; écriture 36,2 et 35,0 contre 40,4 et 40,4.
- Les autres vitesses attendent le Mac mini : on a choisi de laisser le MacBook Air en mode économie d'énergie, qui ralentit le moteur d'environ dix fois.
- Le test complet d'Albireo compte maintenant 51 étapes.

## À suivre

Reste Gemma 4, dont les têtes de 512 valeurs, les clés partagées entre couches et les entrées propres à chaque couche demanderont plus qu'une fenêtre. Et, dans moins de deux semaines, le Mac mini, pour enfin mesurer tout cela.
