# Dlib Transformer Extensions sur macOS, épisode 4 : le fork déménage

> **Repère de vitesse** : pas encore de mesure, Albireo ne tourne pas encore sur le Mac.

Suite de [l'épisode 3](03-albireo-pseudalis-et-cinq-obstacles.md) : cinq obstacles barrent la route d'Albireo et de Pseudalis sur le Mac. Avant de s'y attaquer, un détour s'imposait : le fork ne pouvait toujours rien recevoir.

## Dix commits de plus

Le lendemain, l'original avait encore avancé. Dix commits, cette fois : les versions 1.0.9, 1.0.10 et 1.0.11 d'Albireo, la version 1.0.3 de Pseudalis, deux nouveaux modèles livrés avec Albireo (Qwen3-VL-2B et 4B, en variante « Thinking »), et la lecture des modèles GGUF sur le processeur, un morceau qui intéresse directement un Mac sans carte graphique NVIDIA.

Le camarade du cahier photocopié écrit vite. Raison de plus pour recopier souvent : la fusion est passée sans un seul conflit, 78 fichiers d'un coup, et nos propres pages sont restées intactes.

Un détail compte pour la suite : parmi ces fichiers figure le `CMakeLists.txt` d'Albireo, la recette de sa compilation, augmentée d'une cinquantaine de lignes. Les cinq obstacles de l'épisode 3 n'ont donc peut-être pas tous gardé leur place. Deux sont vérifiés intacts, `st_mtim` et `endian.h`, aux mêmes lignes ; les trois autres seront revus à la prochaine compilation.

## Toujours le mauvais casier

L'envoi vers le fork, lui, se heurtait toujours à la même erreur 403. Le Mac était connecté à GitHub sous le compte `Aughra`, et le fork appartenait à `PapyTortueTech`. Les deux sorties de l'épisode 1 restaient ouvertes : se connecter avec le bon compte, ou inviter l'autre comme collaborateur.

Une troisième s'est présentée, plus simple : abandonner le casier, et en ouvrir un neuf au nom de la clé qu'on a en poche. Un nouveau fork, sous le compte `Aughra`, avec le même nom ; l'ancien, chez `PapyTortueTech`, sera fermé. Il ne contenait que le commit `pour mac`, qui part avec le reste : rien ne se perd dans le déménagement.

## Fork ou dépôt privé ?

Restait à choisir la forme du nouveau dépôt. Deux possibilités, chacune avec son prix :

| | Fork de l'original | Nouveau dépôt privé |
|---|---|---|
| Qui le voit | tout le monde : un fork d'un dépôt public est public | seulement son propriétaire |
| Lien avec Cydral | gardé : le bouton « Sync fork » reste là | perdu sur GitHub (la fusion depuis `upstream` marche toujours sur le Mac) |
| Ce qu'il faut envoyer | seulement nos commits : GitHub a déjà tous les fichiers de l'original | tout le dépôt, 837 Mo |
| Le gros modèle | aucun souci, il est déjà sur GitHub | un fichier de 100 Mo, pile la taille maximale qu'accepte GitHub |

Le fork l'a emporté. Déménager dans l'immeuble d'à côté, quand les meubles y sont déjà, ne demande que d'apporter ses cartons personnels.

## Pour les curieux : le déménagement en trois commandes

```sh
gh repo fork Cydral/Dlib-Transformer-extensions --clone=false --default-branch-only
git remote set-url origin https://github.com/Aughra/Dlib-Transformer-extensions.git
git push -u origin main
```

La première crée le fork sur GitHub sans en télécharger de copie, puisque le Mac en a déjà une. La deuxième change l'adresse d'`origin` : le dépôt local enverra désormais ses modifications au nouveau fork. La troisième envoie tout, les deux fusions avec l'original et nos propres commits compris :

```
To https://github.com/Aughra/Dlib-Transformer-extensions.git
   f0f9f91..50c2c96  main -> main
la branche 'main' est paramétrée pour suivre 'origin/main'.
```

`f0f9f91` est le dernier commit de Cydral, que le nouveau fork connaissait déjà ; `50c2c96` était alors le dernier des nôtres. Après l'envoi, le Mac et le fork en ligne étaient à égalité, pour la première fois depuis le début du chantier.

## La suite

Le fork reçoit enfin ce qu'on lui envoie. Retour aux cinq obstacles : une nouvelle compilation d'Albireo et de Pseudalis dira lesquels la fusion a déplacés, avant de les corriger un à un.
