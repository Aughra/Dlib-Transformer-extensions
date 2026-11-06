# 00 — Deux cahiers

Tout commence par une phrase de l'humain, tapée vite, avec des majuscules qui sautent au hasard : « J'ai fait un fork pour le projet, et je vais le modifier, mais l'original continue des nouveaux commits. »

Le projet, c'est Dlib-Transformer-extensions : une version de la bibliothèque dlib, en C++, enrichie de tout ce qu'il faut pour construire des modèles de langage, les entraîner, les spécialiser, les faire tourner. Son auteur l'écrit pour Windows 11. L'humain, lui, n'a que des Mac. Il veut que tout ça tourne sur une puce Apple, et rien d'autre.

Un fork, c'est une photocopie du cahier d'un camarade, sur laquelle on écrit ses propres notes. Le camarade, lui, continue de remplir son cahier. La question de l'humain est simple : comment recopier ses nouvelles pages sans effacer les miennes ?

Je regarde d'abord comment le dépôt est branché. Il ne connaît qu'une seule adresse, `origin`, qui pointe vers le fork. L'original n'y apparaît nulle part. J'ajoute donc une deuxième adresse, `upstream`, vers le dépôt de l'auteur, et je regarde ce qu'il y a de nouveau là-bas.

```
--- nouveaux commits de l'original :
dda6f44 Publish version 1.0.2 of Pseudalis
be7fb7e Pseudalis Community 1.0.2: ...
86788a5 Publish version 1.0.1 of Pseudalis
44b1031 Detect personal data in Italian, Spanish and German ...
--- tes commits à toi :
07c8c80 pour mac
```

Quatre pages neuves dans le cahier de l'auteur. Une seule dans celui de l'humain : `pour mac`, un mémo qui décrit comment compiler le projet sur un Mac. Avant de proposer quoi que ce soit, je vérifie un dernier point : y a-t-il un fichier que les deux côtés ont modifié ? Aucun. La fusion ne devrait rencontrer aucun conflit.

J'explique la recette à l'humain : `git fetch upstream` pour aller voir, `git merge upstream/main` pour recopier, `git push origin main` pour envoyer le résultat sur le fork. Il répond : « Oui ».

La fusion passe sans un accroc : 54 fichiers changés, et le mémo de l'humain toujours en place. L'envoi, lui, se heurte à un mur :

```
The requested URL returned error: 403
```

Le compte connecté sur ce Mac n'est pas celui à qui appartient le fork. C'est une clé qui ouvre bien une porte, mais pas celle-là. Je ne peux pas régler ça moi-même : la connexion à un autre compte passe par le navigateur et par son mot de passe. Je laisse à l'humain les deux chemins possibles, et il répond : « OK, tout à l'heure. »

Les deux cahiers sont désormais à jour sur le Mac. Il reste à voir si le projet, lui, accepte d'y vivre.

> **Carnet de bord**
>
> - **Résultats vérifiés** : remote `upstream` ajouté ; 4 commits de l'original fusionnés dans `main`, sans conflit (commit de fusion `0abb591`, 54 fichiers changés). Push refusé, erreur 403.
> - **Choix** : une fusion (`merge`) plutôt qu'un `rebase`, parce que le commit `pour mac` était déjà publié sur le fork et qu'un rebase en aurait réécrit l'histoire. Avant de proposer la fusion, j'ai vérifié qu'aucun fichier n'avait été modifié des deux côtés.
> - **Questions et réponses** : « Tu veux que je lance la mise à jour maintenant ? » — « Oui ». Pour le push refusé : « Ok, tout à l'heure. »
> - **Difficultés** : le compte connecté sur le Mac n'a pas le droit d'écrire sur le fork. Le problème reste ouvert, et `main` a 5 commits d'avance sur le fork en ligne.
> - **Conseil** : mettre à jour son fork souvent. Plus les deux cahiers s'éloignent, plus les conflits seront nombreux le jour où l'on recopie.
