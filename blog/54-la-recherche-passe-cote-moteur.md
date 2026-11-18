# Dlib Transformer Extensions sur macOS, épisode 54 : la recherche passe côté moteur

> **Repère de vitesse** (MacBook Air M2) : le moteur de langage est inchangé depuis l'épisode 51 — écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B). La recherche documentaire, elle, change de maison : elle quitte le navigateur pour le service, en C++.

Suite de [l'épisode 53](53-un-nom-pour-chaque-espace.md), qui déménageait les données dans le service. Restait un morceau de cerveau dans le navigateur : la **recherche dans les documents**. Cet épisode l'en sort.

## Chercher dans un document, c'est quoi

Quand on pose une question à propos d'un document trop long pour tenir en entier sous les yeux du modèle, le programme n'envoie pas tout le document : il en choisit les quelques passages les plus utiles. C'est la recherche documentaire. Elle se fait en deux temps, qu'on compare souvent à deux façons de retrouver une page dans un gros classeur.

- **Par les mots** : on cherche les passages qui répètent les mots de la question. Simple, précis pour un terme exact — un nom de fichier, un code d'erreur, une adresse IP.
- **Par le sens** : chaque passage est résumé en une liste de nombres, un « vecteur », qui capture son idée ; on compare au vecteur de la question. Ça retrouve une page qui parle de la même chose même avec d'autres mots.

Jusqu'ici, tout ce calcul se faisait dans le navigateur, en JavaScript. On l'a porté dans le service, en C++, et on en a profité pour offrir le choix.

## Trois façons de chercher, une prise unique

On a posé dans le service une sorte de **prise standard** : une interface que trois moteurs viennent remplir. Mots seuls, sens seul, les deux mélangés. Lequel est branché se règle d'un menu, dans l'onglet Service. Le mélange reste le choix par défaut, le plus complet ; mais « mots seuls » a un mérite à lui : il ne demande aucun modèle d'embedding, et vise juste sur le technique exact. Pour qui range des configurations et des journaux, c'est souvent ce qu'il faut.

## Le piège des accents

Porter un bout de code d'un langage à un autre, c'est surtout attraper les détails qui ne se voient pas. Le plus sournois, ici : la découpe des mots. Le navigateur sait, d'une ligne, mettre un texte en minuscules, retirer les accents, et le couper en mots — « Café » devient « cafe ». En C++, rien de tout cela n'est gratuit : il a fallu reproduire, à la main, la table qui ramène chaque lettre accentuée à sa lettre de base, pour le français et les langues voisines.

On s'est arrêté là volontairement : l'alphabet latin, pas les écritures qui n'en sont pas (le cyrillique, l'arabe, les idéogrammes). Les documents visés sont en français et en anglais ; couvrir le reste aurait demandé une grosse table de données pour un gain nul dans cet usage. Le choix est assumé.

Pour être sûr que la découpe C++ donne exactement la même chose que celle du navigateur, on a calculé les deux sur les mêmes textes et comparé mot pour mot, bornes comprises. Elles coïncident.

## Garder plusieurs cahiers, et changer de l'un à l'autre

Une demande précise appelait une réponse précise : pouvoir garder **plusieurs indexations** et basculer de l'une à l'autre. Pourquoi ? Parce qu'un index dépend du modèle qui a calculé ses vecteurs, et de la façon dont les documents ont été découpés. Changer l'un ou l'autre rend les anciens vecteurs inutilisables — deux modèles différents parlent deux langues de nombres qu'on ne peut pas mélanger. C'est l'erreur classique, celle qui donne des résultats absurdes sans qu'on sache pourquoi.

La réponse : chaque combinaison {modèle, découpage} est un **index nommé**, gardé à part. On en a plusieurs, un seul est actif, et basculer est instantané — rien n'est recalculé, chaque index garde ses passages. On l'a vérifié : on range un passage dans un index, on bascule sur un autre (vide), on rebascule, le passage est toujours là.

## Ce qui reste au navigateur, et pourquoi

Un point ne pouvait pas déménager : la recherche dans les discussions **privées**. Leurs passages ne quittent jamais le navigateur — c'est tout l'intérêt du mode privé. Le service ne peut pas chercher ce qu'il n'a pas. Alors la recherche garde, pour ce seul cas, son ancien calcul en JavaScript. Pour tout le reste, c'est le C++ qui travaille.

## La preuve par la réponse

Comment savoir que le moteur C++ trouve d'aussi bons passages que l'ancien ? On ne compare pas des nombres : on regarde la réponse. Le banc d'essai de la page joint un document, l'indexe, pose une question, et vérifie que la réponse **cite un mot du document** — et, pour un projet, la bonne couleur. Ce test passe avec le nouveau moteur. La recherche trouve les bons extraits ; les réponses sont les mêmes. Le but n'était pas la vitesse — la recherche est rapide des deux côtés — mais la confidentialité et le choix du moteur. Ils sont là.

## La suite

Le chantier commencé par le renommage s'achève : Gulliver garde les données par profil et cherche dans les documents en C++. La suite, ce sera le Mac mini, et les mesures qu'il permettra enfin de faire sérieusement.

## Pour les curieux : les fichiers et la méthode

- **Module** : `host/rag_engine.{h,cpp}`, ajouté aux programmes du service et des tests. `rag_terms` (tokenisation latine), `rag_chunk` (découpage, porté fidèlement de `chunk()` de la page), l'interface `rag_engine` et trois moteurs (`lexical`, `dense`, `hybrid`) avec un post-traitement commun (fusion par rang réciproque, pertinence marginale maximale, prolongement par le passage suivant, fusion des passages consécutifs).
- **Route** : `POST /gulliver/v1/profile/search` ; la page y envoie la question, les propriétaires, et le vecteur de la question en base64 quand un modèle d'embedding est chargé. Le moteur et le nombre d'extraits viennent des réglages (`rag_engine`, `rag_top_k`).
- **Index nommés** : table `profile_indexes`, colonne `index_id` sur les passages ; routes `GET/POST /gulliver/v1/profile/indexes`, `PUT …/indexes/<id>/active`, `DELETE …/indexes/<id>`.
- **Vérifié** : `test_host` couvre la parité de la découpe et des mots (contre des valeurs calculées avec le navigateur), le classement des trois moteurs, et les index nommés (isolation, bascule, cascade). Le banc de la page passe en entier ; son scénario « documents » prouve la recherche de bout en bout sur le moteur C++. La vitesse n'est pas mesurée : le gain visé est la confidentialité et le choix, pas le temps, et le Mac mini permettra des mesures plus justes.
- **Version** : Gulliver **6.2.0**.

## À suivre

Le Mac mini M5 Pro, attendu pour bientôt, et les mesures qu'on remet à son arrivée.
