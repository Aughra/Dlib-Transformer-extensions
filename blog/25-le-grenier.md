# Dlib Transformer Extensions sur macOS, épisode 25 : le grenier

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 20) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 24](24-sans-cle.md) : Albireo a perdu ses clés, et sa réserve de modèles a été essayée pour de vrai. L'essai avait laissé trois défauts et une lenteur. Cet épisode termine le chantier.

## Deux étiquettes sur le même carton

Garder chaque modèle téléchargé dans une réserve, c'est a priori doubler la place prise : un modèle de 2,8 Go dans l'atelier, et sa copie de 2,8 Go dans le placard. Le disque du Mac, en APFS, connaît pourtant un tour qui change tout : le **clone**. Cloner un fichier ne recopie aucun octet. On colle simplement une deuxième étiquette sur le même carton. Tant que personne n'ouvre le carton pour en changer le contenu, il n'existe qu'une seule fois sur le disque.

Albireo clone donc au lieu de copier, partout où il le peut : à l'entrée dans la réserve, à la restauration, à l'import. Supprimer le 4B, puis le restaurer deux fois de suite, n'a pas pris un seul mégaoctet de place.

Un clone se fait en un instant, mais on ne lui fait pas confiance pour autant : chaque copie est relue et son empreinte recalculée. C'est là qu'était la vraie lenteur.

## Le lecteur d'empreintes

Chaque fichier de modèle a une empreinte, une suite de 64 caractères calculée sur tous ses octets, comme une empreinte digitale. Un seul octet changé, et l'empreinte n'a plus rien à voir. Albireo la recalcule à chaque téléchargement, chaque import et chaque restauration.

L'original la calcule avec une bibliothèque, mbedtls, qui fait tout le travail en logiciel, à la main en quelque sorte. Or la puce du Mac a des instructions faites exprès pour ce calcul, et la bibliothèque d'Apple, CommonCrypto, s'en sert. Mesuré en alternance sur un fichier de 2,5 Go, le premier met 8,7 secondes, le second 1,2 seconde. C'est comme passer d'une loupe à un lecteur d'empreintes : même résultat au caractère près, sept fois plus vite.

Avec les clones et ce lecteur, restaurer le 4B et son projecteur depuis la réserve passe de 23 secondes à un peu plus de 6, sans réseau.

## Les trois défauts de l'essai

L'épisode précédent avait laissé trois défauts. Ils sont corrigés :

- **Tout retélécharger pour un seul fichier abîmé.** La restauration travaille maintenant fichier par fichier. Si le projecteur est abîmé mais le reste sain, en ligne, seul le projecteur se télécharge.
- **La copie abîmée qui restait.** Une copie dont l'empreinte ne correspond plus est retirée de la réserve. Et chaque téléchargement terminé y range une copie saine.
- **La réserve qui ne se remplissait qu'à la suppression.** Chaque modèle y entre maintenant dès la fin de son téléchargement. Les modèles installés avant cette version y sont rangés au démarrage du service : quatre fichiers sur ce Mac, en quelques secondes et sans place prise.

## La taille du grenier

Une réserve sans limite finit toujours par remplir le disque sans qu'on le voie. Elle a donc un plafond, réglable dans l'onglet Service, à 200 Go par défaut. Il ne compte que ce qui prend vraiment de la place, c'est-à-dire les copies des modèles qu'on a supprimés. Les clones des modèles encore installés ne coûtent rien et ne comptent pas. Au-delà du plafond, les copies les moins récemment utilisées partent d'abord. La carte du réglage affiche la place utilisée, avec un bouton « Vider la réserve ».

L'ordre de départ a révélé une erreur facile à commettre, et on l'a commise. Un clone garde la date de son original. Un modèle téléchargé une semaine plus tôt entrait donc dans la réserve avec une date d'une semaine, et il aurait été le premier à partir. Chaque copie porte maintenant la date de son entrée dans la réserve, et un test le vérifie.

## La mémoire de Hugging Face

Dernière pièce : les recherches sur Hugging Face. Chaque réponse est désormais gardée sur le Mac, les trente plus récentes. Sans réseau, ou si Hugging Face ne répond pas, la page montre la dernière réponse connue avec sa date. Les recherches gardées sont listées sous le champ de recherche, et un clic suffit pour les relancer.

L'onglet Modèles gagne deux boutons :
- « Rafraîchir depuis le Mac » relit la liste, la réserve et la dernière recherche, sans réseau ;
- « Rafraîchir depuis Hugging Face » redemande le catalogue à GitHub et relance la recherche en ligne.

## Pour les curieux : les mesures et les essais

| Mesure (M2, `lowpowermode 0`, mesures alternées) | Avant | Après |
|---|---|---|
| Empreinte SHA-256 d'un fichier de 2 497 281 472 octets | 8,66 à 8,81 s (mbedtls) | 1,17 à 1,21 s (CommonCrypto) |
| Restauration hors ligne du 4B et de son projecteur (2,8 Go) | 23 s | 6,8 puis 6,1 s |
| Place prise par une suppression suivie d'une restauration | 2,8 Go | 0 |

Le clone se fait depuis le fichier déjà ouvert, ce qui garde la protection contre un lien échangé pendant l'opération :

```
fclonefileat(fd_, AT_FDCWD, to.c_str(), 0)
```

La réserve, vue par le service juste après sa mise en place :

```
GET /albireo/v1/reserve
{"limit":214748364800,"used":0,"shared":0,"files":[]}
```

Et la recherche gardée, demandée sans réseau :

```
GET /albireo/v1/hub/search?q=granite&local=1   → 40 modèles, cached_at 1794506998
```

Les nouveaux contrôles de `test_host` couvrent la réutilisation du fichier sain, la copie saine remise après un téléchargement et sa date, le plafond à 0, la plus ancienne copie retirée d'abord, et le vidage. Le test complet passe à 19 étapes sur 19 pour Albireo, avec une étape nouvelle : hors ligne, une recherche jamais faite répond « jamais gardée sur ce Mac ».

## À suivre

Le chantier de la réserve est fini. Le suivant est plus lourd : apprendre au moteur à lire des modèles récents de Hugging Face qu'il ne connaît pas encore, Qwen 3.5 et ses cousins.
