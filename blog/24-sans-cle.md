# Dlib Transformer Extensions sur macOS, épisode 24 : sans clé

> **Repère de vitesse** (MacBook Air M2, inchangé depuis l'épisode 20) : écriture **45 à 51 tokens/s** (2B), **21 à 22 tokens/s** (4B) ; lecture **418 à 422 tokens/s** sur 512 tokens (2B), **153 à 156 tokens/s** (4B).

Suite de [l'épisode 23](23-deux-point-zero.md) : Albireo a pris son propre numéro. Depuis, il est passé à la 3.2.0, qui sait chercher et installer n'importe quel modèle GGUF de Hugging Face. Cette fois, il perd ses clés, et le travail se fait pour la première fois à deux bureaux.

## Le trousseau de l'original

L'original d'Albireo vit dans un monde où l'auteur publie et où des milliers de machines reçoivent. Pour qu'une machine ne se laisse pas tromper, chaque liste de modèles (le « catalogue ») arrive avec une signature. C'est le cachet de cire sur l'enveloppe : seul l'auteur possède le sceau, et le programme refuse une lettre dont le cachet ne correspond pas. Les modèles passaient d'abord par un espace de stockage de l'auteur, protégé par un mot de passe glissé dans le programme au moment de le construire.

Sur un fork qui ne publie rien et ne sert qu'un Mac, ce trousseau ne servait plus. Le fork n'a pas le sceau de l'auteur, et l'espace de stockage répondait « protégé par mot de passe » : chaque téléchargement y perdait seize secondes avant de se rabattre sur Hugging Face. La demande de l'humain tenait en une phrase : « je veux tout pouvoir faire sans utiliser de clé ».

La version **4.0.0** retire donc tout : la vérification de la signature du catalogue, l'espace de stockage, les deux outils qui servaient à signer, la clé publique de Pseudalis. Les modèles se téléchargent directement à l'adresse du catalogue. Le premier chiffre change, parce qu'un dossier de modèles signé ne s'importe plus de la même façon. Le premier commit enlève 2 874 lignes et en ajoute 299.

## Un collègue dans l'autre bureau

La nouveauté de la journée tient moins au code qu'à la façon de travailler. L'humain était loin du Mac, devant un PC sous Windows. Il a confié le chantier à une autre session de Claude, avec une consigne claire : ne toucher qu'à ce qui ne demande pas le Mac.

Un PC sous Windows ne peut ni compiler ni lancer un programme fait pour macOS. C'est un cuisinier qui prépare un plat dans une cuisine sans four : il peut éplucher, découper, assaisonner, mais il ne peut pas goûter le résultat cuit. Le collègue de l'autre bureau a donc travaillé sur une branche à part, sans toucher à la version principale. Il a laissé, en partant, une liste précise de ce qu'il fallait vérifier sur le Mac avant de mélanger son travail au reste.

Il a fait plus que préparer. En sept commits, de 10 h 12 à 17 h, la branche est allée jusqu'à la version 4.1.0. Elle ajoute des contrôles des dossiers et une première réserve de modèles, dont on reparle plus bas. Pour s'approcher du four, il a compilé une partie du code sous Linux, avec une pièce provisoire à la place de la fonction propre au Mac. C'est là qu'il a trouvé sa propre faute : en déplaçant une petite fonction qui masque les adresses dans le journal, il avait abîmé des caractères, et le fichier ne compilait plus. La faute était facile à commettre ; il l'a commise, puis corrigée.

Le soir, sur le Mac, la branche a été fusionnée dans la version principale et compilée sans erreur. Le test complet d'Albireo est passé à 18 étapes sur 18, et celui de Pseudalis à 34 sur 34. Le plat sortait du four comme prévu, et le mérite en revient au collègue qui l'avait préparé à l'aveugle.

## Le gardien qui disait toujours oui

Albireo et Pseudalis demandent souvent, avant de toucher à un dossier, s'il est sûr. Sur Windows, l'original pose vraiment la question. Sur Mac, la réponse était écrite d'avance : « oui ». C'était un gardien d'immeuble qui laissait passer tout le monde sans regarder les badges.

La branche du PC lui a appris à regarder. Le dossier doit appartenir au compte du service ou à l'administrateur du Mac. Personne d'autre ne doit pouvoir y écrire, ni dans ses dossiers parents, jusqu'à la racine du disque. Les dossiers sensibles, comme ceux des modèles, de la réserve ou des clés de Pseudalis, sont fermés à tout autre compte.

Il restait un angle mort, que la branche signalait elle-même : macOS a, en plus des droits ordinaires, des listes de contrôle d'accès, les « ACL ». C'est une liste d'invités affichée à côté de la serrure : la serrure peut être fermée et la liste dire quand même « le voisin peut entrer ». La version **4.1.1** lit aussi cette liste. Une ligne qui *autorise* un autre compte à écrire fait refuser le dossier. Une ligne qui *interdit* est acceptée : le dossier personnel du Mac en porte justement une, « personne ne peut supprimer ce dossier », qui est une protection et non un risque. Sept vérifications nouvelles s'ajoutent au test d'Albireo, et les mêmes à celui de Pseudalis.

## La réserve, essayée pour de vrai

La réserve est un placard à côté de l'atelier. Quand on supprime un modèle, ses fichiers y sont rangés au lieu d'être jetés. Le réinstaller devient alors une copie locale, sans réseau, utile le jour où Hugging Face serait en panne. La branche du PC l'avait écrite et testée avec de faux fichiers. Il fallait encore l'essayer avec un vrai modèle de près de 3 Go, sans risquer les vrais.

L'essai s'est fait sur une copie des données d'Albireo, faite par un clone du système de fichiers du Mac : instantanée et sans place prise. Un service d'essai tournait sur cette copie, sur un autre port. Au premier démarrage, la liste des modèles a montré un détail inquiétant : la base de la copie gardait les chemins complets des *vrais* fichiers. Supprimer un modèle depuis la copie aurait effacé l'original. Rien n'avait encore été supprimé. Le service a été arrêté, les chemins corrigés dans la copie, et le vrai dossier des modèles mis en lecture seule le temps de l'essai, par ceinture et bretelles.

Ensuite, tout s'est passé comme prévu, à une maladresse près :

- supprimé, le modèle Qwen3-VL 4B et son projecteur d'images sont allés dans la réserve, sous leur empreinte, lisibles par le seul compte de l'utilisateur ;
- en mode hors ligne, la restauration les a remis en place en 23 secondes, empreintes conformes ;
- un octet changé dans la copie du projecteur a fait refuser la restauration, sans rien installer, avec un message clair ;
- de retour en ligne, le service a téléchargé le modèle et l'a rechargé en 56 secondes, puis il a répondu « Paris est la capitale de la France. ».

La maladresse a eu lieu au moment d'abîmer le projecteur. La suppression attendait que le modèle soit déchargé de la mémoire. Elle s'est donc terminée après l'octet changé, et elle a rangé une copie saine par-dessus. Le service a alors restauré un modèle parfaitement sain, et l'essai a d'abord semblé montrer un défaut. Il a fallu attendre la fin de la suppression avant d'abîmer le fichier pour que le test dise vrai.

Deux points restent à améliorer. En ligne, le service retélécharge tout le modèle (2,8 Go) alors qu'un seul fichier était abîmé. Et la copie abîmée reste dans la réserve jusqu'à la prochaine suppression du modèle.

## Pour les curieux : la journée en chiffres

| Étape | Résultat |
|---|---|
| Branche écrite sous Windows | 7 commits, de 10 h 12 à 17 h 00 ; 77 fichiers, 1 861 lignes ajoutées, 3 550 retirées |
| Fusion dans la version principale | sans conflit |
| Compilation sur le Mac | 0 erreur ; les 2 avertissements connus, venus de dlib |
| Test complet d'Albireo (`tools/test_macos.sh`) | 18 étapes sur 18, dont « offline reserve » |
| Test complet de Pseudalis | 34 étapes sur 34 |
| `test_host` d'Albireo avec les ACL | 194 vérifications |
| Restauration hors ligne du 4B et de son projecteur | 23 s, empreintes conformes |
| Téléchargement en ligne du même modèle | 56 s, 2 951 255 648 octets |

Le refus d'une copie abîmée, tel que le service l'écrit dans son journal :

```
[warning] models: the reserved copy of qwen3-vl-4b-thinking cannot be used: the copy of …/reserve/6b71c77c5…
[warning] models: the local reserve cannot restore qwen3-vl-4b-thinking
```

Une ACL se pose et se retire avec la commande `chmod` du Mac ; c'est ainsi que les tests la fabriquent :

```
chmod +a 'everyone allow write' fichier     # refusé par les contrôles
chmod +a 'everyone deny delete' dossier     # accepté
chmod -N fichier                            # retire l'ACL
```

## À suivre

La réserve doit encore garder chaque modèle dès son téléchargement, et pas seulement à sa suppression. Elle doit aussi recevoir ses deux boutons dans l'onglet Modèles, « depuis le Mac » et « depuis Hugging Face ».
