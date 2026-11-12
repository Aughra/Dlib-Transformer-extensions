# Dlib Transformer Extensions sur macOS, épisode 26 : le carnet de notes

> **Repère de vitesse** (MacBook Air M2) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B), inchangés depuis l'épisode 20 ; nouveau, Qwen3.5-4B sur le processeur : écriture **10 à 11,5 tokens/s**.

Suite de [l'épisode 25](25-le-grenier.md) : la réserve de modèles est finie. Le chantier suivant touche le cœur du moteur : lui apprendre à lire Qwen 3.5, la famille de modèles la plus téléchargée qu'il refusait.

## Ce que le moteur ne savait pas lire

Un inventaire de 527 dépôts populaires de modèles GGUF sur Hugging Face a donné un classement net. En tête des architectures refusées par le moteur, on trouve `qwen35` : Qwen 3.5 à 3.8, 69 dépôts, près de 59 millions de téléchargements. Viennent ensuite Gemma 4 (19 millions) et la variante à experts de Qwen 3.6 (19 millions). C'est par Qwen 3.5 qu'on a commencé.

## Le livre et le carnet

Un modèle de langage classique relit, à chaque mot qu'il écrit, tout ce qui précède. Pour ne pas tout recalculer, il garde pour chaque mot déjà lu une fiche, ses « clés et valeurs ». C'est un livre qu'on rouvre sans cesse, et qui s'épaissit à chaque page.

Qwen 3.5 fait autrement trois fois sur quatre. Trois de ses couches sur quatre, appelées « Gated DeltaNet », ne gardent pas de fiche par mot. Elles tiennent un **carnet de notes** de taille fixe, qu'elles mettent à jour à chaque mot : elles effacent un peu les anciennes notes, puis corrigent le carnet avec ce que le nouveau mot apporte. Le carnet ne grossit jamais, quelle que soit la longueur du texte. Seule la quatrième couche relit le livre à l'ancienne, avec une petite porte en plus qui dose ce qu'elle laisse passer.

Pour le moteur, cela veut dire trois choses nouvelles :
- une couche d'un type inconnu, avec sa règle de mise à jour du carnet ;
- un carnet par conversation, environ 20 Mo pour le petit modèle ;
- moins de fiches à garder, puisque seule une couche sur quatre en a besoin.

## Un carnet ne se rembobine pas

La difficulté n'était pas dans le calcul, mais dans une habitude du moteur. Pour gagner du temps, il reprend une conversation déjà lue à l'endroit où le nouveau texte cesse de ressembler à l'ancien, souvent au milieu. Avec des fiches, c'est facile : on jette celles qui suivent. Avec un carnet, c'est impossible, car on ne peut pas retrouver ce qu'il contenait trois phrases plus tôt.

La règle est donc devenue simple : une conversation Qwen 3.5 ne se reprend qu'au tout début ou à la fin exacte de ce qui a été lu, sinon tout est relu. Un premier essai à trois tours a montré que ce n'était pas suffisant. Le moteur relit en avance l'historique pendant que l'utilisateur lit la réponse, et il estimait le travail en supposant une reprise au milieu. Au troisième tour, il découvrait 84 tokens à lire au lieu des 40 prévus, et il abandonnait par prudence. L'estimation tient maintenant compte du carnet, et chaque tour reprend ce qui a été lu en avance : 34 tokens au deuxième tour, 84 au troisième.

## Une référence pour ne pas se raconter d'histoires

Écrire une couche nouvelle est une chose. Savoir qu'elle calcule juste en est une autre. La formule exacte a été prise dans le code de llama.cpp, un autre moteur qui lit déjà Qwen 3.5, et llama.cpp a servi de référence pour comparer, mot à mot, en choisissant toujours le mot le plus probable.

Le premier essai a semé le doute : deux questions sur trois divergeaient tôt, l'une dès le premier mot. Il a fallu regarder les probabilités. La référence avait été lancée par un autre programme de llama.cpp, sur le GPU. Relancée sur le processeur, elle choisissait le même premier mot que le moteur. Sur les trois questions, voici ce qui reste :

- une réponse identique sur les 150 tokens demandés ;
- deux réponses identiques sur une soixantaine de mots, puis séparées sur une vraie hésitation du modèle : 0,440 contre 0,428 dans un cas, 0,288 contre 0,253 dans l'autre. Le moteur a pris le second candidat, à un cheveu près. C'est la trace d'un arrondi différent, pas d'une erreur.

Le modèle de 4 milliards de paramètres, lui, donne exactement le même texte que la référence sur les 150 tokens. Pour la conversation à plusieurs tours, les réponses sont identiques avec et sans reprise du carnet : celui qu'on reprend est bien le bon. Enfin, le gabarit de conversation produit par le moteur est identique, au caractère près, à celui du modèle, avec et sans raisonnement.

Une petite surprise au passage : à la question « What is my name? », posée après « My name is Léa. Remember it. », le petit modèle répond qu'il s'appelle Qwen3.5. La référence répond mot pour mot la même chose : le défaut est du modèle de 0,8 milliard, pas du moteur.

## Par la porte d'entrée

Dernière épreuve : passer par le service, comme le ferait la page. La recherche Hugging Face marque désormais Qwen 3.5 comme compatible. Elle n'annonce plus, pour lui, ni GPU ni lecture des images, deux choses que le moteur ne sait pas encore faire avec ces couches. Le fichier Qwen3.5-4B Q4_K_M de 2,6 Go a été trouvé, téléchargé, vérifié et rangé dans la réserve en 32 secondes. À « Quelle est la capitale de la France ? », il répond « La capitale de la France est Paris. ».

Il écrit à 11 tokens par seconde environ, sur le processeur seul. C'est deux fois moins vite que le Qwen3-VL de 4B que le service utilise, qui tourne sur le GPU. Le carnet de notes n'a pas encore de version Metal.

## Pour les curieux : la règle du carnet

Pour chaque tête, l'état est une matrice S de 128 × 128. À chaque token, avec la requête q, la clé k, la valeur v, l'oubli g et la force β calculés par la couche :

```
S ← S · exp(g)                       l'oubli
δ ← β · (v − S k)                    ce que le nouveau token corrige
S ← S + δ kᵀ                         la correction
o ← S q / √128                       ce que la couche lit
```

Avant cela, une convolution relie chaque canal à ses trois tokens précédents, et q et k sont ramenés à une longueur de 1. Après, chaque tête est normalisée puis multipliée par une porte.

Les comparaisons avec llama.cpp 0.9.0 sur processeur, en choix glouton, 150 tokens :

| Modèle et question | Texte identique | Au point de divergence (probabilités de la référence) |
|---|---|---|
| 0,8B, « why the sky is blue » | 299 caractères | « are » 0,440, « longer » 0,428 (le moteur) |
| 0,8B, histoire d'un chat qui nage | les 595 caractères | — |
| 0,8B, dix premiers nombres premiers | 299 caractères | « concept » 0,288, « problem » 0,253 (le moteur) |
| 4B, « why the sky is blue » | les 634 caractères | — |

Le test complet du service a deux étapes nouvelles : « qwen 3.5 », qui demande la capitale de la France, et « qwen 3.5 resumed state », qui vérifie qu'une conversation à trois tours donne les mêmes réponses avec et sans reprise du carnet.

## À suivre

Le carnet de notes sur le GPU, pour que Qwen 3.5 écrive aussi vite que le modèle actuel ; puis la variante à experts, Qwen 3.6, qui réutilisera tout ce travail.
