# Dlib Transformer Extensions sur macOS, épisode 23 : deux point zéro

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 20) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 22](22-les-documents-du-mac.md) : Albireo prend ses documents dans un dossier du Mac et se règle depuis le canapé. Il était temps de lui donner un nom à lui.

## Un numéro qui ne ment plus

Jusqu'ici, la page affichait « 1.0.12 », le numéro de la dernière version de l'original fusionnée dans le fork. Or le programme qui tourne sur le Mac n'a plus grand-chose de cette version : un moteur GPU, une autre façon de se régler, un accès depuis le réseau, plus une ligne de Windows. Garder ce numéro, c'était laisser sur la boîte l'étiquette d'un autre produit.

Le fork passe donc en **2.0.0**. Le premier chiffre change parce que la rupture est réelle : ce qui marchait sous Windows ne marche plus, et ce n'est plus le but. Désormais, chaque modification d'Albireo monte le numéro selon la règle habituelle. Le dernier chiffre bouge pour une correction, celui du milieu pour une fonction nouvelle, le premier pour une nouvelle rupture. Un petit script met le numéro à jour aux trois endroits où le programme le garde. Le build vérifie qu'ils s'accordent, comme un notaire qui compare trois copies du même acte.

## Des notes au ton de notaire

Le blog raconte, avec ses marmottes et ses cuisines. Les utilisateurs d'un logiciel attendent autre chose : une liste sèche de ce qui a changé. La page gagne donc des « Notes de version », ouvertes depuis l'onglet « À propos » ou depuis un engrenage, désormais présent en haut de chaque page. Elles sont écrites sur un ton professionnel, sans image ni humour : plateforme, performances, réglages, accès, sécurité, corrections. Elles seront mises à jour à chaque nouvel épisode du blog, qui reste la version racontée des mêmes faits.

La section « Sécurité » de la 2.0.0 dit noir sur blanc ce que les épisodes précédents racontaient : plus de serrure sur le Mac, une porte réseau ouverte sans mot de passe, un trafic en clair sur le Wi-Fi. Un choix assumé reste un choix qu'on écrit.

## Une seule langue

Albireo parlait cinq langues : anglais, français, allemand, italien et espagnol, pour l'interface comme pour la lecture à voix haute. Ce fork ne fait que du français. Il a donc perdu le choix de la langue, quatre dictionnaires sur cinq et cinq voix sur six. Avec eux sont partis les listes de mots qui servaient à deviner la langue d'un texte et les exemples en quatre langues envoyés au modèle pour titrer les discussions. Le dictionnaire français était complet : ses 427 entrées couvraient les 426 de l'anglais, de sorte que rien ne s'affiche en anglais par défaut.

Deux choses restent en anglais, volontairement. D'une part, les commentaires du code, écrits par l'auteur de l'original dans cette langue. D'autre part, les consignes que la page donne au modèle en coulisse, que l'utilisateur ne voit jamais et que les modèles comprennent le mieux en anglais.

## Pour les curieux : la 2.0.0 en chiffres

| Élément | Avant | Après |
|---|---|---|
| Version affichée | 1.0.12 | 2.0.0 · fork macOS |
| Langues de l'interface | 5 | 1 |
| Voix de lecture | 6 (dont deux anglaises) | 1 (fr-FR) |
| Lignes du dictionnaire de la page | environ 1 500 | 355 |
| Réglages dans l'onglet Service | 0 | 19 |

Le script de version :

```
python3 tools/bump_version.py 2.0.0
Albireo 1.0.12 -> 2.0.0
```

## À suivre

L'attention en tuiles reste le prochain chantier du GPU, et elle portera sans doute le numéro 2.1.0.
