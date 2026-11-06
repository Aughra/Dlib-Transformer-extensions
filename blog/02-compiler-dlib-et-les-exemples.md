# Dlib Transformer Extensions sur macOS, épisode 2 : compiler dlib et les exemples

*Publié sur le carnet TortueTech le 5 novembre 2026.*

Suite de [l'épisode 1](01-le-fork-et-l-original.md) : le fork est à jour avec l'original de Cydral. Place à la compilation, sur un Mac à puce Apple.

## Compiler n'est pas entraîner

Puis vint la compilation, longue. L'occasion d'une mise au point qui mérite d'être notée, parce que l'erreur est facile à commettre et qu'on l'a commise. La compilation ne fait que traduire le code C++ en programme : elle n'entraîne aucun réseau de neurones et n'en fait tourner aucun.

Si elle est si lente, c'est que dlib décrit le plan d'un réseau (ses couches, leur taille, leur ordre) dans le code lui-même, avec des templates C++. Le compilateur doit lire et assembler ce plan, et plus le réseau est gros, plus l'assemblage est long. C'est l'architecte qui dessine un immeuble de cinquante étages : le dessin prend du temps, mais personne n'habite encore dedans. Construire et habiter, c'est l'exécution, plus tard.

## La bibliothèque et les exemples compilent

Premier verdict : la bibliothèque dlib et les exemples de modèles de langage compilent sur le Mac, sans une seule erreur. Les 24 programmes `slm_*` sortent du compilateur, et ce sont bien des programmes natifs pour puce Apple (`Mach-O arm64`), vérification faite. Ils n'ont pas encore été lancés : compiler n'est pas fonctionner, la section précédente vient de le rappeler.

Le compilateur a tout de même laissé 28 avertissements, aucun grave :

- 25 fois `ld: warning: -s is obsolete` : l'éditeur de liens d'Apple signale qu'une option héritée de Linux est démodée chez lui. Un GPS moderne à qui l'on demande de tourner à la manivelle : il comprend, il râle, il fait le travail quand même.
- 3 fois `braces around scalar initializer` : des accolades en trop autour d'un simple nombre, `{{5}}` au lieu de `{5}`. Ça marche ; c'est seulement mal écrit.

Si cette partie passe si facilement, c'est que dlib a été écrite dès le départ pour tourner partout, Linux et macOS compris. Le mérite revient à l'amont, pas au portage.

## La suite

La bibliothèque et les exemples passent. Les deux applications du dépôt, Albireo et Pseudalis, sont une autre affaire : [épisode 3](03-albireo-pseudalis-et-cinq-obstacles.md).
