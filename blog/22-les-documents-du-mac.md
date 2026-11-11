# Dlib Transformer Extensions sur macOS, épisode 22 : les documents du Mac

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 20) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 21](21-le-tableau-de-bord.md) : Albireo se règle depuis la page et s'ouvre au téléphone. Restait à lui donner des documents sans être devant le Mac.

## La porte du jardin, sans digicode

L'épisode précédent avait posé un mot de passe sur la porte réseau. Il n'a pas survécu à la première soirée : « je suis chez moi ». Le mot de passe devient donc facultatif. Vide, tout appareil du Wi-Fi de la maison entre. Le service l'écrit dans son journal à chaque démarrage, et la fiche de l'abeille rappelle ce que cela permet à un invité ou à une télévision connectée. On peut le remettre en une ligne, depuis l'onglet.

## Un tiroir partagé

Jusqu'ici, un document joint dans la page restait dans le navigateur de l'appareil qui l'avait choisi. Un PDF lu sur le téléphone n'existait pas pour le Mac, et inversement. Le service range maintenant un dossier « documents » dans ses données. Le trombone de la page propose deux chemins : « Depuis cet appareil », comme avant, ou « Documents du Mac », une liste à cocher des fichiers de ce dossier. Et tout fichier joint depuis la page y est aussi copié. C'est un tiroir commun dans l'entrée : ce qu'on y dépose depuis le canapé, on le retrouve au bureau.

Le service se méfie des noms qu'on lui donne. Un nom qui tente de sortir du dossier, comme `../settings.json`, est refusé. Un même fichier envoyé deux fois n'est gardé qu'une fois. Un autre fichier du même nom devient « nom (2) », comme le fait le Finder.

## Plus de boutons, et un bouton pour tout remettre

L'onglet Service gagne six réglages. On y trouve les deux seuils qui décident quand le GPU lit un prompt, jusqu'ici écrits en dur dans le code. On y trouve aussi l'attente des threads entre deux calculs, la relecture d'avance de la conversation, le dossier des modèles et la langue du service. Chacun a sa fiche, son schéma et ses trois voix.

Deux outils l'accompagnent. Le premier est un bouton « Revenir aux réglages conseillés ». Les valeurs conseillées viennent des mesures faites tout au long du portage, et chaque carte dit la sienne quand on s'en écarte. Le second, ce sont les sauvegardes : on garde les réglages du moment sous un libellé, « avant essai GPU » par exemple, et on y revient d'un clic. Une règle s'ajoute au carnet du projet : tout réglage nouveau du programme devra rejoindre l'onglet, et les valeurs conseillées suivront les mesures.

## Une erreur de débutant, la nôtre

Les sauvegardes ont d'abord refusé de s'effacer, et en ajouter une effaçait les précédentes. La faute était dans une boucle qui lisait une copie temporaire de la liste, jetée par le programme avant même d'être parcourue : c'est lire une liste de courses déjà partie à la poubelle. L'erreur est facile à commettre, et on l'a commise. Elle a été trouvée par un essai à trois sauvegardes, puis corrigée en gardant la liste dans une variable.

## Pour les curieux : le tiroir à l'essai

Depuis l'adresse du Mac sur le réseau, avec `curl` :

```
envoi:
{"ok":true,"name":"note test.txt","already":false}
même envoi:
{"ok":true,"name":"note test.txt","already":true}
autre contenu même nom:
{"ok":true,"name":"note test (2).txt","already":false}
nom piégé:
{"error":{"message":"a plain file name and a length of 512 MB at most are required","type":"invalid_request_error","param":null,"code":"document_name"}}
lecture piégée:
404
```

Le seuil du GPU déplacé de 2 048 à 4 096 positions depuis l'onglet, tel que le contrôle du chargement le rapporte :

```
prompts of 24 tokens or more, up to 4096 positions
```

| Réglage ajouté | Valeur conseillée | D'où elle vient |
|---|---|---|
| GPU : prompts à partir de | 24 tokens | le GPU perd à 16, gagne dès 24 (épisode 20) |
| GPU : prompts jusqu'à | 2 048 positions | à 4 096, le 2B est plus lent sur le GPU (134 contre 173 tokens/s) |
| Attente active des threads | 2 ms | valeur de l'original |
| Préparer la question suivante | allumé | valeur de l'original |

## À suivre

Le chantier suivant est l'attention en tuiles, qui doit permettre au GPU de lire les longs documents au-delà de 2 048 positions. Si les mesures le confirment, le bouton des réglages conseillés le saura.
