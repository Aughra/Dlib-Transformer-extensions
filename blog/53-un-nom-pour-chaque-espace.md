# Dlib Transformer Extensions sur macOS, épisode 53 : un nom pour chaque espace

> **Repère de vitesse** (MacBook Air M2) : le moteur est inchangé depuis l'épisode 51 — écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B), lecture sur le GPU **603 à 608 tokens/s** sur 512 tokens (2B). Ce qui change ici n'est pas la vitesse, mais l'endroit où vivent les données.

Suite de [l'épisode 52](52-un-nouveau-nom.md), qui renommait le service en Gulliver. Cette fois, une vraie nouvelle fonction : un **profil**, pour que les discussions ne soient plus prisonnières d'un navigateur.

## Le cahier rangé dans le mauvais tiroir

Jusqu'ici, tout ce que l'application gardait — les discussions, les projets, les documents indexés, les images — vivait dans une base cachée **du navigateur** : celle de Safari sur le Mac, celle de Chrome ailleurs, chacune de son côté. Ouvrir la page depuis un autre navigateur, c'était ouvrir un cahier vierge. Changer de machine, pareil. Un peu comme si vos notes étaient écrites à l'encre invisible qui n'apparaît que sous une seule lampe.

Le remède : déménager tout ça dans le **service**, le programme qui tourne sur le Mac. Le service, lui, est unique ; il sert tous les navigateurs. S'il garde les données, alors Safari, Chrome, le téléphone voient la même chose.

Restait une question toute bête : s'il garde les données de **qui** ? Il n'y a pas de compte, pas de mot de passe. La réponse tient en un mot : un **profil**, qui n'est qu'un nom. On en choisit un au démarrage — « barbidou », par exemple — et le service range ses affaires sous ce nom. On peut en avoir plusieurs, comme plusieurs cahiers sur une étagère, chacun étiqueté. Aucun mot de passe : c'est un usage personnel, sur un Mac à soi, derrière un Wi-Fi à soi.

## Ce que le service a appris à garder

Le service avait déjà une petite base de données, pour ses modèles et ses téléchargements. On lui a ajouté, par profil, de quoi tenir :

| Ce qui est gardé | Comment |
|---|---|
| Les discussions et les projets | Chacun, son texte entier, dans une ligne de la base |
| Les réglages de la page (thème, nom, mémoire) | En paires clé-valeur, par profil |
| Les passages des documents et leurs **vecteurs** | Les passages en texte, les vecteurs en blocs d'octets |
| Les images | Les images elles-mêmes dans des fichiers, à côté de la base |

Deux choix méritent un mot. Les **vecteurs** — ces listes de nombres qui disent le « sens » d'un passage pour la recherche — auraient pu être recalculés à chaque fois. On a préféré les garder : les recalculer, c'est faire repasser chaque passage par le moteur, et une question aurait attendu. Mieux vaut les ranger une fois.

Les **images**, elles, ne vont pas dans la base mais dans des fichiers ordinaires, un par image, dans un dossier au nom du profil. Une base de données se relit entièrement à chaque démarrage pour vérifier qu'elle est saine ; y entasser des mégaoctets d'images l'alourdirait pour rien. Les documents du Mac étaient déjà rangés ainsi ; les images suivent la même règle.

## Chaque étagère est fermée à clé de l'intérieur

Un point comptait : qu'un profil ne voie jamais les affaires d'un autre. Chaque donnée porte le nom de son profil, et chaque requête de la page dit, dans un en-tête discret, à quel profil elle s'adresse. Le service ne répond qu'avec les données de ce profil-là. Supprimer un profil emporte d'un coup tout ce qui s'y rattache, par une règle de la base qui fait le ménage en cascade.

On l'a vérifié bêtement mais sûrement : deux profils, Alice et Bob. Alice écrit une discussion ; Bob demande ses discussions et n'en voit aucune. C'est le genre de test qui ne paie pas de mine mais qui rassure.

## Deux onglets qui écrivent en même temps

Avant, quand deux onglets modifiaient la même discussion, la base du navigateur les mettait d'accord dans une seule opération. Côté service, il a fallu une autre méthode : à chaque écriture, la page dit « je modifie la version que j'ai lue il y a un instant ». Si, entre-temps, un autre onglet a écrit, le service refuse poliment et la page relit puis réessaie. C'est la règle du vestiaire : on ne rend pas le manteau si le ticket ne correspond plus.

## La page ne sait presque pas qu'elle a déménagé

Le plus satisfaisant : l'application web, qui compte une vingtaine de modules, n'a eu qu'**un seul** fichier profondément changé — celui qui parlait à la base du navigateur. On l'a réécrit pour qu'il parle au service, en gardant mot pour mot les mêmes fonctions, les mêmes noms. Les quinze autres modules qui s'en servent n'ont rien vu. C'est l'avantage d'avoir, de longue date, fait passer tous les accès aux données par une seule porte : on ne déplace que la porte, pas les murs.

Il a fallu convertir au passage : les vecteurs voyagent sur le réseau sous forme de texte (du base64), que la page retransforme en liste de nombres ; les images aussi. Mais ce sont des traductions mécaniques, vérifiées au nombre près.

## Un détail promis tenu

Au passage, une petite chose demandée depuis longtemps : les réglages du moteur extérieur (le « relais ») ne sont plus réservés au Mac. On les change désormais depuis le téléphone, comme le reste. La maison est à soi, le Wi-Fi est à soi ; la serrure qui n'y servait qu'à se gêner a sauté.

## La suite

La page garde encore, pour l'instant, le calcul de la recherche documentaire — comparer une question à tous les passages. Le prochain chantier le confiera au service, en C++, avec le choix entre plusieurs façons de chercher. C'est l'épisode suivant.

## Pour les curieux : les routes et la méthode

- **Les routes** : la gestion des profils (`/gulliver/v1/profiles`, pour lister, créer, renommer, supprimer) et les données d'un profil, toutes sous `/gulliver/v1/profile/…` (`conversations`, `projects`, `page-settings`, `chunks`, `images`), avec l'en-tête `X-Gulliver-Profile` qui dit de quel profil il s'agit (sinon 400 ; profil inconnu, 404).
- **La base** : SQLite, qui tenait déjà les modèles ; tables `profile_conversations`, `profile_projects`, `profile_page_settings`, `profile_chunks` (vecteur en BLOB), `profile_images` (métadonnées ; octets en fichiers sous `<données>/profiles/<id>/images/`). Chaque table a une clé étrangère `ON DELETE CASCADE` vers le profil.
- **La révision optimiste** : un champ `expected_updated` ; l'écriture n'a lieu que si la valeur en base n'a pas changé, sinon 409 et la page relit.
- **Vérifié** : un test C++ (`test_host`) couvre le stockage (création, isolation entre profils, révision, round-trip d'un vecteur par comparaison d'octets, cascade à la suppression) ; le banc de la page passe en entier (110+ scénarios), y compris la recherche documentaire de bout en bout — joindre un document, l'indexer, retrouver un mot de son contenu dans une réponse. Aucune fonction de la page n'a été perdue.
- **Version** : Gulliver **6.1.0**.

## À suivre

[Épisode 54](54-la-recherche-passe-cote-moteur.md) : la recherche documentaire portée en C++, avec le choix du moteur.
