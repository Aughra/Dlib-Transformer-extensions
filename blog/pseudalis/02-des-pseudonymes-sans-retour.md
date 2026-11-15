# Pseudalis sur macOS, épisode 2 : des pseudonymes sans retour

> **Repère de débit** (MacBook Air M2, mode économie d'énergie coupé, charge 2,6 à 2,8) : protection d'un texte de 30 000 octets en **1,59 à 1,61 s**, soit **18,6 à 18,9 Ko/s**.

Suite de [l'épisode 1](01-le-coffre-sans-cle-de-service.md), où Pseudalis avait perdu son jeton d'administration et gagné son propre carnet. Cette fois, il perd quelque chose de plus profond : sa clé.

## Le cadenas et le masque

Jusqu'ici, Pseudalis remplaçait chaque nom par une balise qui contenait le vrai nom, chiffré. « Jean Dupont » devenait `[[PERSON:Xoydoeh Nuggwdfsorn#EKR]]`. C'est un cadenas : la valeur est toujours là, enfermée, et la clé de l'ordinateur sait l'ouvrir. D'où une clé à garder, à exporter avec une phrase secrète, à ne pas perdre.

L'utilisateur du fork a posé une autre règle : la pseudonymisation n'a pas besoin d'être inversée. Si l'on veut le document d'origine, on reprend l'original, que Pseudalis garde sur le Mac. Il n'y a donc plus rien à ouvrir, et plus de clé à garder. Le cadenas devient un masque : il cache, et c'est tout.

## Un dé par document

La solution retenue tient en une image : pour chaque document, Pseudalis lance un dé, une clé tirée au hasard, s'en sert, puis la jette. Dans ce document, « Jean Dupont » reçoit toujours le même faux nom, ce qui garde le texte cohérent ; dans le document suivant, un autre. Et comme le dé a disparu, personne, pas même Pseudalis, ne peut remonter du faux nom au vrai.

Sur le service, deux envois de la même phrase le montrent :

| Envoi | Résultat |
|---|---|
| 1 | Qdyiuzb Bewrgupbzbx écrit à Vnbolwgdi Ryruuvxxy. Qdyiuzb Bewrgupbzbx signe. |
| 2 | Doysppx Kwsmqhsrfnp écrit à Tpxnbufxl Abwkkqqyb. Doysppx Kwsmqhsrfnp signe. |

Les crochets disparaissent aussi : ils ne servaient qu'à retrouver la valeur. La copie se lit comme un document ordinaire, peuplé d'inconnus aux noms improbables.

## L'obstacle : se reconnaître soi-même

Le premier essai a fait apparaître un défaut que l'on n'attendait pas. Le lecteur de Pseudalis, ce dossier où un fichier déposé est protégé sur place, renomme aussi les dossiers dont le nom contient une personne. Il savait jusqu'ici reconnaître un nom qu'il avait déjà protégé : il ouvrait la balise avec sa clé. Avec un dé jeté, plus rien ne s'ouvre, et le faux nom passait pour un vrai ; le dossier aurait été renommé à chaque passage, comme un chien qui court après sa queue.

La correction se fait dans dlib, la bibliothèque sur laquelle repose Pseudalis : une option, coupée par défaut, permet au pseudonymiseur de reconnaître un faux nom qu'il a déjà écrit, d'après la liste que garde Pseudalis, sans avoir à l'ouvrir. Il le laisse alors tel quel. Rien ne change pour qui n'active pas l'option.

## Ce que l'on perd, dit franchement

- Un fichier dont l'original a été oublié ne revient plus : ses faux noms restent faux.
- Un texte protégé dans la page ne se restaure plus.
- Les numéros changent sans garder leurs chiffres de contrôle valides au sens du document d'origine, et les copies d'un même dossier n'emploient pas les mêmes faux noms d'un fichier à l'autre.
- Les copies protégées par une version antérieure, avec leurs balises, se restaurent encore : l'ancienne clé reste sur le Mac pour elles seules.

En échange, il n'y a plus de clé à exporter ni de phrase secrète à retenir, et les originaux, gardés en clair depuis la version précédente, sont la seule voie de retour.

## Pour les curieux : les fichiers et les chiffres

- dlib : `dlib/privacy/pseudonymizer.h` et `pseudonymizer_abstract.h`, option `pseudonymizer_options::opaque_substitutes` et fonction `opaque_type()` ; noté au registre des [notes pour l'auteur](../../docs/notes-pour-l-auteur.md). `dtest --test_privacy` : 1 254 vérifications passent.
- Moteur : `engine::one_time_key` (`engine/engine.h`, `engine.cpp`), un pseudonymiseur sous une clé de 32 octets tirée au hasard et effacée, actif le temps d'une opération ; la requête le demande par `"one_time_key": true` (`engine_api.cpp`).
- Service : chaque protection (fichier, nom, texte) le demande et n'écrit plus de balises ; le réglage `tags` vaut toujours `false` ; les routes `/v1/key/export` et `/v1/key/import` et la section « Clé de cet ordinateur » sont retirées.
- Tests : `test_host` et `test_engine` passent ; `test_macos.sh` passe ses 34 étapes, dont treize réécrites pour la nouvelle règle (aucune valeur ni balise dans une copie, l'original seul en retour, un pseudonyme déjà écrit gardé sur le lecteur).
- Versions : Pseudalis 2.2.0 (coffre et export de la clé en clair), puis 3.0.0 (pseudonymes sans retour).

## À suivre

Mesurer Pseudalis sur de vrais documents, un Word, un PDF de plusieurs pages, une archive, et voir si le lecteur gagnerait à apparaître dans le Finder comme un disque, à la manière du lecteur A: de Windows.
