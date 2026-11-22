# Les modèles du mardi

Un agent launchd de l'utilisateur lance chaque mardi à 4 h `gulliver/tools/modeles_mardi.py`, qui tient à jour les modèles de Gulliver avant la mesure du dimanche (`macos/bench-hebdo/`), par le service qui tourne sur le Mac :

1. le catalogue est rafraîchi ;
2. un modèle du catalogue installé dont le catalogue donne une nouvelle version (une autre empreinte) est retéléchargé ;
3. un modèle ajouté depuis Hugging Face dont le fichier a changé là-bas (empreinte lue dans l'en-tête `X-Linked-Etag`, sans télécharger le fichier) est réajouté, ce qui télécharge sa nouvelle version ;
4. les modèles du catalogue qui tiennent dans le Mac sont téléchargés : une architecture que le moteur lit, un fichier (avec son projecteur) d'au plus les trois quarts de la mémoire (48 Go sur le Mac mini de 64 Go), un modèle qui répond aux questions.

Les modèles installés entrent ensuite dans la mesure du dimanche, qui les mesure tous et conseille le meilleur dans l'onglet « Modèles » de Gulliver. Les anciennes versions restent dans la réserve du service (clones APFS, sans place prise tant qu'elles sont identiques).

| Action | Commande |
|---|---|
| Installer | `bash macos/modeles-mardi/install.sh` |
| Retirer | `bash macos/modeles-mardi/install.sh --retirer` |
| Voir ce qu'il ferait, sans rien lancer | `python3 dlib/dlib/external/gulliver/tools/modeles_mardi.py build/gulliver-home --essai` |
| Lancer tout de suite | `launchctl kickstart gui/$(id -u)/fr.fork.gulliver.modeles-mardi` |
| Journal | `build/bench/hebdo/mardi.log` |

Les nouveaux modèles viennent du catalogue de Gulliver (celui de l'Albireo d'origine) et des modèles déjà ajoutés depuis Hugging Face : aucune recherche libre sur Hugging Face, dont les modèles inconnus n'ont ni qualité ni licence vérifiées.
