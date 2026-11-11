# Dlib Transformer Extensions sur macOS, épisode 1 : le fork et l'original

> **Repère de vitesse** : pas encore de mesure, Albireo ne tourne pas encore sur le Mac.

*Publié sur le carnet TortueTech le 5 novembre 2026.*

Dlib Transformer Extensions promet de faire tourner des modèles de langage en C++, sur la base de dlib, sans Python dans la boucle. Le projet vit sous Windows et Linux ; on a voulu le voir tourner sur un Mac. Voici où en est le chantier.

Ce premier épisode raconte la mise en place : le fork, la synchronisation avec l'original et un premier refus de GitHub.

## Un fork, et l'original qui ne s'arrête pas

Première étape, la plus banale : un fork du dépôt de Cydral, sous le compte `PapyTortueTech`, et un premier commit maison, sobrement intitulé `pour mac`. Le dépôt local connaît donc deux adresses : `origin`, notre fork, où l'on envoie nos modifications ; et `upstream`, l'original, d'où l'on récupère les siennes.

Car l'original ne s'est pas arrêté pour nous attendre. Entre le fork et notre premier bilan, Cydral avait publié quatre commits : les versions 1.0.1 et 1.0.2 de Pseudalis, et la détection des données personnelles en italien, en espagnol et en allemand. Un fork, c'est un cahier photocopié chez un camarade qui continue d'écrire dans le sien : si l'on ne va pas régulièrement recopier ses nouvelles pages, les deux cahiers finissent par raconter deux histoires différentes.

## Une fusion sans histoire

Bonne surprise : aucun fichier n'avait été modifié des deux côtés à la fois. La récupération s'est faite en trois gestes, à refaire de temps en temps :

```sh
git fetch upstream          # voir ce qu'il y a de nouveau chez l'original
git merge upstream/main     # l'ajouter à notre version, nos modifications restent
git push origin main        # envoyer le résultat sur le fork
```

GitHub propose le même service par le bouton « Sync fork » de la page du fork, suivi d'un simple `git pull` sur le Mac. La leçon, elle, ne change pas : synchroniser souvent, une fois par semaine par exemple. Plus on attend, plus les deux versions s'éloignent, et plus le premier conflit sera pénible. Quand il viendra (deux modifications sur la même ligne), Git s'arrêtera et montrera les deux versions ; il faudra choisir, ou mélanger.

## L'envoi refusé : bonne clé, mauvais casier

Sur le Mac, la fusion avait marché : les quatre commits de l'original étaient là, `pour mac` aussi. Sur GitHub, en revanche, rien : l'envoi a été refusé avec une erreur 403.

La cause n'avait rien de mystérieux. Le Mac était connecté à GitHub avec un autre compte que celui qui possède le fork. GitHub reconnaissait parfaitement la personne ; simplement, ce casier n'était pas le sien. Deux sorties possibles :

- se connecter aussi avec le compte propriétaire du fork (`gh auth login`, puis `gh auth switch`), et renvoyer ;
- ou inviter l'autre compte comme collaborateur du fork, dans les réglages du dépôt.

Dans les deux cas, la connexion se fait dans le navigateur, avec son propre mot de passe : c'est un geste humain, et c'est très bien ainsi.

## La suite

Le fork à jour, restait à compiler. C'est l'objet de [l'épisode 2 : compiler dlib et les exemples](02-compiler-dlib-et-les-exemples.md).
