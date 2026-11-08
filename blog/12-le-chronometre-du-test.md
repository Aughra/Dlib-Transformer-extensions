# Dlib Transformer Extensions sur macOS, épisode 12 : le chronomètre du test

Suite de [l'épisode 11](11-deux-facons-d-arrondir.md) : Albireo est rapide et complet sur le Mac. Place à Pseudalis, l'autre application du projet, celle qui anonymise les documents. Elle attendait deux corrections. Elle n'en avait besoin que d'une, et pas pour la raison qu'on croyait.

## La même boîte aux lettres

À l'épisode 10, le service d'Albireo ne savait pas où il était installé : il cherchait son adresse dans le dossier `/proc` de Linux, que macOS n'a pas. Pseudalis, écrit par le même auteur, porte une copie du même code. On lui a donc appliqué la même correction, à l'identique : demander l'adresse à macOS.

Chez Pseudalis, le défaut se voyait moins. Hors de Windows, son moteur est intégré au programme, il n'a pas besoin de le chercher. Mais sa page web, elle, est rangée à côté du programme. Sans adresse, le service la cherchait dans le dossier d'où on l'avait lancé. Lancé depuis le bon dossier, tout allait bien. Lancé depuis n'importe quel autre, la page répondait « introuvable ». On l'a vérifié dans les deux sens : sans la correction, erreur 404 ; avec, la page s'affiche.

## Les deux étapes qui échouaient

Restait le vrai chantier. Depuis l'épisode 5, le test complet de Pseudalis, un script de l'auteur qui fait faire au service tout son travail, passait 26 étapes sur 28. Les deux qui échouaient concernaient le « lecteur », un dossier où l'on dépose des fichiers pour qu'ils soient anonymisés : une archive protégée qu'on renomme, et un fichier protégé qu'on copie.

On avait noté une piste : le système de fichiers du Mac, APFS, traite les copies et les renommages à sa façon. Une copie y est un « clone », qui partage ses données avec l'original, comme deux étiquettes collées sur le même carton. On s'attendait à une enquête de ce côté.

Elle n'a pas eu lieu. Relancé tel quel, le test a passé ses 28 étapes.

## Le chronomètre

Pour savoir ce qui avait changé, on a retiré la correction du jour et relancé : 28 sur 28 encore. Ce n'était donc pas elle. Le code de Pseudalis, lui, n'avait pas bougé depuis le premier essai.

La réponse était dans le script de test. Pour la plupart des étapes, il attend que le travail soit fait, en vérifiant chaque seconde. Pour ces deux-là, il attend un temps fixe, 8 secondes, puis 6 et 4, avant de regarder le résultat. C'est un examen chronométré : si l'élève n'a pas fini quand la cloche sonne, la copie est ramassée quand même.

Or le premier passage datait du 6 novembre, avant l'épisode 7, celui où l'on a découvert que le Mac tournait en mode économie d'énergie, environ dix fois moins vite. On ne l'a pas vérifié au moment même du test, mais le mode était actif lors des mesures qui l'ont suivi de près. Le lecteur de Pseudalis n'avait sans doute simplement pas eu le temps de finir avant la cloche.

La piste APFS était donc fausse, et elle aurait pu coûter cher. C'est l'erreur qu'on commet en cherchant une cause compliquée à un symptôme quand la cause simple, déjà connue, n'a pas encore été écartée. La leçon de l'épisode 7 valait aussi pour les tests : avant de croire un résultat, vérifier que la machine tourne à pleine vitesse.

## Pour les curieux : les deux contre-épreuves

Le résumé du test avec la correction (extrait des dernières lignes) :

```
drive signatures           PASS  a protected archive renamed stays, an archive without extension is taken
drive content              PASS
drive restored             PASS  the original, its name and the name of the folder
archive extracted          PASS  2 files kept as they are, recorded under their original names
extracted restored         PASS  from their own originals once the archive forgotten, the file outside refused
forgotten comes back       PASS  kept as it is, no new original
copies on the drive        PASS  one record, kept with a copy, removed with the last
settings saved             PASS  the body of the web application, 0 days, a value out of range refused
imposed terms              PASS  replaced first, case kept for KX-77, a type refused
room for originals         PASS  the oldest forgotten (original_space), the newest restored, 1 MB refused
0 failed
```

Les attentes à temps fixe, dans `pseudalis/tools/test_linux.sh` (numéros de ligne du script) :

```
199:  sleep 8
303:  sleep 8
316:  sleep 6
318:  sleep 4
```

Les deux contre-épreuves :

| Essai | Sans la correction | Avec |
|---|---|---|
| `test_linux.sh` | 28 sur 28 | 28 sur 28 |
| Page web, service lancé depuis la racine du dépôt | 404, « no web folder at web » | 200 |

Le test demande toujours deux outils de Linux absents du Mac, `nproc` et la version GNU de `stat`. On les lui fournit par un petit dossier placé en tête du `PATH`, sans toucher au script de l'auteur.

## À suivre

Les deux applications passent maintenant tous leurs tests sur le Mac. Restent la page de Pseudalis à essayer dans un navigateur, puis les tests de dlib lui-même, la bibliothèque sur laquelle tout repose, qu'on n'a encore jamais lancés.
