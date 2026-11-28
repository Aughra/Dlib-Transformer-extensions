# Dlib Transformer Extensions sur macOS, épisode 71 : le conseil passe en C++

> **Repère de vitesse** (Mac mini M5 Pro) : inchangé depuis [l'épisode 68](68-la-tete-trop-large.md) : écriture sur le GPU **163 tokens/s** (2B) et **84 tokens/s** (4B), lecture sur le GPU **2 610 tokens/s** sur 512 tokens et **2 397** sur 4 096 (4B). Gemma 4 26B-A4B : lecture **1 886 tokens/s** sur 4 096 tokens et **1 505** sur 16 384.

Suite de [l'épisode 70](70-le-cachet-de-la-maison.md). L'humain a tranché le 28 novembre : « tout en C++, plus de Python ». Les outils du dépôt passent un à un, chacun vérifié contre l'ancien sur de vraies données. À cette étape, le plus gros d'entre eux : l'assembleur du conseil du lundi.

## Un seul programme, trois sorties

Chaque lundi, le Mac mesure tous ses modèles — vitesse de lecture, d'écriture, perplexité, et une épreuve de questions en français — puis un outil range ces chiffres bruts en trois choses. Une page Markdown, lisible, avec le tableau de tous les modèles et le conseil : lequel prendre pour tout faire, lequel est le plus juste, lequel le plus rapide. La section « mesure de la semaine » du carnet de vitesse, réécrite entre deux marqueurs sans toucher au reste du document. Et un petit fichier `conseil.json`, que l'onglet Modèles lit pour afficher sa carte « Conseil ».

C'était 440 lignes de Python. C'est maintenant un module C++ compilé dans Gulliver, et un programme `conseil` qui prend exactement les mêmes options. Le travail vient de la session Temper, qui l'a écrit sur son propre établi ; cette session l'a relu, rangé dans le dépôt et vérifié. La règle du portage, comme pour les outils précédents : le nouveau doit rendre, au caractère près, ce que rendait l'ancien.

## Le piège des deux arrondis

Un détail a failli faire mentir le test. Python, quand il formate un nombre entier avec `%.0f`, arrondit « au pair » : 7 200,5 devient 7 200, et 2 956,5 devient 2 956 — le voisin pair, pas toujours le plus haut. La fonction d'arrondi la plus courante en C++ arrondit, elle, le demi vers le haut : 7 200,5 donnerait 7 201. Dans l'atelier de Temper, cinq lignes du tableau s'écartaient ainsi d'un jeton. C'est un vieux sujet de ce carnet — [l'épisode 11](11-deux-facons-d-arrondir.md) en parlait déjà. Une petite fonction qui arrondit au pair a remis les deux sorties d'accord. Même affaire pour les nombres à la française : l'espace qui sépare les milliers, la virgule à la place du point.

## L'essai qui décide

Les tests de Temper comparaient le nouveau programme à des sorties de l'ancien figées dans son atelier. Avant de retirer l'outil Python, on a fait un dernier essai, plus rude : les deux programmes lancés l'un après l'autre sur la vraie mesure du 24 novembre, avec toutes les options qu'emploie la mesure du lundi — la machine, la température de la puce, les mesures du service, la recherche dans les documents, les modèles retirés. Résultat : la même page de 91 lignes, la même section du carnet, le même fichier de conseil. Ce n'est qu'après cet essai que l'ancien outil a quitté le dépôt.

La remise a aussi demandé quelques retouches, comme toute copie d'un établi à l'autre : deux tests visaient encore des fichiers par leur chemin sur le Mac, et le programme `conseil` devait se construire même quand on ne construit pas les tests, puisque la mesure du lundi s'en sert.

## La note de cybersécurité s'accroche enfin

Le module qui note la résistance d'un modèle aux détournements — trente-deux sondes, une note sur 10 et sur 100 — était entré dans le dépôt à [l'épisode 69](69-le-sceau-et-la-langue-unique.md), mais il tournait dans son coin, sans être branché au conseil. Il l'est maintenant à moitié : le conseil accepte une option qui lui passe la note de chaque modèle, et quand elle existe, elle vient se ranger à côté de la vitesse et de l'épreuve, dans la ligne du modèle conseillé. Quand elle n'existe pas, rien ne change : le fichier reste identique à celui d'avant. L'autre moitié, faire passer réellement les trente-deux sondes à chaque modèle par le moteur, reste à faire.

## Le test de l'ancien, gardé

`test_conseil.py` vérifiait, sur des chiffres inventés dont on connaît les bonnes réponses, que le conseil choisit bien : le plus juste des modèles assez rapides pour « tout faire », jamais un tout petit modèle ni llama.cpp, le bon réglage de mémoire, le dernier du classement retiré. Ce test est lui aussi passé en C++, ses deux cas intacts, en vingt-deux vérifications. Deux autres l'accompagnent, qui comparent le fichier, la page et le document produits à ceux du Python.

## Pour les curieux : les chiffres et les fichiers

- Version 7.19.0. Le programme : `build/macos-gulliver/bin/conseil`, appelé par `tools/bench_modeles.sh` avec les mêmes arguments que l'ancien `python3 tools/conseil_modeles.py`.
- L'essai final, sur `build/bench/modeles-20261124-1350.raw` :

  ```
  stdout identique (      91 lignes)
  page de la semaine identique
  conseil.json identique (hors date et colonnes cyber)
  ```

- Les tests : `test_bench_conseil_core` (le fichier), `test_bench_conseil_md` (la page et le document, quatre cas), `test_bench_conseil_cli` (les deux cas de l'ancien test, 22 vérifications), tous dans le test complet.
- Fichiers : `bench/conseil_core.{h,cpp}`, `bench/conseil_main.cpp`, `tests/test_bench_conseil_{core,md,cli}.cpp`, `bench/tests/fixtures/` ; `tools/conseil_modeles.py` et `tools/test_conseil.py` supprimés.
- dlib n'est pas touché.

## La suite

Il reste du Python des deux côtés de l'atelier. Chez Temper : l'épreuve des 40 questions, qui branchera enfin les sondes de cybersécurité au moteur, puis le retrait des modèles, la veille du lundi, la mesure du service, le test de la recherche, l'envoi des mails, et le surveillant en dernier, parce qu'un faux pas y laisserait un programme chauffer le Mac. Chez nous : la petite ouverture de session du test de la page.
