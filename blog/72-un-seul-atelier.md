# Dlib Transformer Extensions sur macOS, épisode 72 : un seul atelier

> **Repère de vitesse** (Mac mini M5 Pro) : inchangé depuis [l'épisode 68](68-la-tete-trop-large.md) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384.

Suite de [l'épisode 71](71-le-conseil-passe-en-cpp.md). Le conseil du lundi venait de passer en C++. Il avait été écrit dans l'atelier d'une autre session, Temper, puis relu et rangé dans le dépôt. À cette étape, l'humain a changé l'organisation.

## Deux cuisines pour un seul repas

Jusque-là, le travail était coupé en deux. Temper écrivait le code dans son atelier, hors du dépôt. La session Gulliver le recopiait, le testait, corrigeait les chemins et les textes, puis le livrait. Chaque remise demandait donc une relecture et des retouches : des chemins propres à un Mac oubliés dans les tests, un numéro de version déjà pris, une phrase de blog à reprendre.

C'était un peu comme deux cuisines pour un seul repas : l'une prépare, l'autre réchauffe et dresse les assiettes. L'humain a tranché : « je veux pas deux parties ». Temper s'est arrêté. Avant de partir, il a confirmé deux choses. Rien dans son atelier n'était en avance sur le dépôt. Et l'épreuve des 40 questions, qu'il devait porter ensuite, n'était pas commencée. Il a laissé son plan pour elle, qui est noté dans la reprise. Le reste du portage se fait maintenant d'un seul côté.

## Quatre petits outils

On a commencé par les outils les plus petits.

| Ancien outil Python | Nouveau programme | À quoi il sert |
|---|---|---|
| `mail_admin.py` | `mail_admin` | le mail du lundi à l'administrateur |
| `modeles_installes.py` | `modeles_installes` | la liste des modèles à mesurer |
| `retrait_modeles.py` | `retrait_modeles` | le retrait du dernier du classement |
| `page_test/session.py` | `page_session` | la connexion du test de la page |

La règle reste celle des épisodes précédents : le nouveau programme doit rendre ce que rendait l'ancien. La liste des modèles est identique ligne pour ligne, 33 sur 33, sur le service réel. Pour le retrait, il fallait faire l'essai sans rien effacer. Les deux versions ont donc tourné en mode essai, sur une copie des vrais fichiers de la semaine. Elles ont pris la même décision et écrit la même phrase. La seule différence est d'écriture : le nouveau note `100` là où l'ancien notait `100.0`, ce qui ne change pas le nombre lu par la page.

## Le facteur qu'on avait déjà

Pour le mail, il n'y avait presque rien à écrire. Le service sait déjà envoyer des mails : c'est lui qui expédie les codes de connexion. L'outil Python, lui, refaisait le travail de son côté avec sa propre bibliothèque. Le nouveau programme emprunte simplement le facteur du service. Il lit aussi le mot de passe du Bridge exactement comme le service : seulement si ce mot de passe a été donné pour la même adresse d'envoi et le même port.

Même idée pour le faux facteur des tests, ce petit serveur qui garde les mails au lieu de les envoyer. Il était recopié dans le test des comptes. Il vit maintenant dans un seul fichier, partagé par trois programmes.

## La porte qu'on fermait mal

En réécrivant ces outils, on a trouvé un défaut de sécurité, discret. Pour parler au service, les outils Python acceptaient n'importe quel certificat. C'est comme ouvrir la porte à quiconque sonne en disant « c'est moi ». Comme tout se passe sur le même Mac, le risque était faible, mais la porte était mal fermée.

Les nouveaux programmes vérifient le certificat du service avec sa propre autorité, celle qu'il range dans son dossier de données. Personne d'autre ne peut se faire passer pour lui. L'erreur venait du portage, pas de l'original.

## Pour les curieux : les chiffres et les fichiers

- Version 7.20.0. Programmes dans `build/macos-gulliver/bin/` (`mail_admin`, `modeles_installes`, `retrait_modeles`) et `build/macos-gulliver/tools/` (`page_session`).
- En-têtes partagés : `tools/faux_smtp.h` (le faux facteur) et `tools/client_service.h` (HTTPS par le TLS d'Apple, autorité `certificates/gulliver-authority.crt` du service).
- Le retrait est coupé en deux : un cœur sans réseau (`bench/retrait_core`), que l'on peut tester sur des listes inventées, et un programme qui parle au service (`bench/retrait_main.cpp`).
- Tests nouveaux dans le test complet :
  - `test_mail_admin` : 10 contrôles, sur un faux dossier et un faux facteur, jamais le vrai Bridge ;
  - `test_bench_retrait` : 14 vérifications.
- Test des comptes : 100 contrôles, toujours verts avec le faux facteur partagé.
- Test de la page : tous les scénarios ont tourné avec la nouvelle connexion. Un seul a échoué une fois, sur un minutage (« arrêter une longue réponse ») ; relancé seul, il a réussi 31 contrôles sur 31.
- Supprimés : `modeles_installes.py`, `retrait_modeles.py`, `page_test/session.py`. `mail_admin.py` et `faux_smtp.py` restent tant que des outils encore en Python s'en servent.
- dlib n'est pas touché.

## La suite

Le gros morceau vient ensuite : l'épreuve des 40 questions, qui branchera enfin les seize sondes de cybersécurité au moteur. Suivront la mesure du service, le test de la recherche, la veille du lundi, et le surveillant en dernier. Il restera ensuite les petits bouts de Python cachés dans les scripts shell.
