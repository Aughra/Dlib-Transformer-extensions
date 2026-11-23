# Les mises à jour des modèles, chaque semaine

Un agent launchd de l'utilisateur lance chaque **lundi à 1 h** `gulliver/tools/modeles_semaine.py`, qui fait la **veille des modèles** de Gulliver par le service qui tourne sur le Mac, **sans rien télécharger** (décision de l'humain du 23 novembre 2026 : il décide lui-même de ce qu'on intègre) :

1. le catalogue est rafraîchi ;
2. un modèle du catalogue installé dont le catalogue donne une nouvelle version (une autre empreinte) est relevé ;
3. un modèle ajouté depuis Hugging Face dont le fichier a changé là-bas (empreinte lue dans l'en-tête `X-Linked-Etag`, sans télécharger le fichier) est relevé ;
4. les modèles du catalogue que le Mac n'a pas et qui y tiendraient sont relevés : une architecture que le moteur lit, un fichier (avec son projecteur) d'au plus les trois quarts de la mémoire (48 Go sur le Mac mini de 64 Go), un modèle qui répond aux questions.

Les nouveautés pas encore annoncées partent dans **un mail à l'administrateur** (l'adresse de son profil), par Proton Mail Bridge, avec les réglages « Mail des comptes » de l'onglet Service ; `build/bench/hebdo/annonces.json` garde ce qui a été annoncé, pour qu'une même version ne soit annoncée qu'une fois. Une semaine sans nouveauté n'envoie rien. Pour intégrer un modèle : onglet Modèles de Gulliver, ou le demander à Claude.

Jusqu'au 23 novembre 2026, l'agent téléchargeait lui-même ces modèles avant la mesure du lundi à 8 h (`macos/bench-hebdo/`, retirée le même jour).

| Action | Commande |
|---|---|
| Installer | `bash macos/modeles-semaine/install.sh` |
| Retirer | `bash macos/modeles-semaine/install.sh --retirer` |
| Voir le mail qu'il enverrait, sans l'envoyer | `python3 dlib/dlib/external/gulliver/tools/modeles_semaine.py build/gulliver-home --essai` |
| Lancer tout de suite | `launchctl kickstart gui/$(id -u)/fr.fork.gulliver.modeles-semaine` |
| Journal | `build/bench/hebdo/modeles.log` |

Les nouveautés viennent du catalogue de Gulliver (celui de l'Albireo d'origine) et des modèles déjà ajoutés depuis Hugging Face : aucune recherche libre sur Hugging Face, dont les modèles inconnus n'ont ni qualité ni licence vérifiées.
