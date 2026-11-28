# Les mises à jour des modèles, chaque semaine

Un agent launchd de l'utilisateur lance chaque **lundi à 1 h** le programme `build/macos-gulliver/bin/modeles_semaine` (en C++ depuis Gulliver 7.23.0, à la place de `tools/modeles_semaine.py`), qui tient à jour les modèles de Gulliver avant la mesure du **lundi à 8 h** (`macos/bench-hebdo/`), par le service qui tourne sur le Mac. Les créneaux évitent les heures où l'humain se sert du Mac (du lundi au vendredi, de 8 h à 20 h, l'humain est au travail ; le samedi et le dimanche, de 10 h à 21 h, l'humain essaie Gulliver) :

1. le catalogue est rafraîchi ;
2. un modèle du catalogue installé dont le catalogue donne une nouvelle version (une autre empreinte) est retéléchargé ;
3. un modèle ajouté depuis Hugging Face dont le fichier a changé là-bas (empreinte lue dans l'en-tête `X-Linked-Etag`, sans télécharger le fichier) est réajouté, ce qui télécharge sa nouvelle version ;
4. les modèles du catalogue qui tiennent dans le Mac sont téléchargés : une architecture que le moteur lit, un fichier (avec son projecteur) d'au plus les trois quarts de la mémoire (48 Go sur le Mac mini de 64 Go), un modèle qui répond aux questions.
5. **5 modèles GGUF dans chacun des 5 tris de Hugging Face** (plus téléchargés, tendance, plus aimés, plus récents, mis à jour récemment ; décisions de l'humain des 23 et 24 novembre 2026 ; un modèle déjà installé compte sans être retéléchargé, un modèle retiré par la mesure — `build/bench/hebdo/retires.json` — est sauté), et 5 dans chacune des 4 recherches de cybersécurité (abliterated GGUF, Qwen3 abliterated, Llama 3 abliterated, dolphin, d'éditeurs connus de ces versions : huihui-ai, mlabonne, mradermacher…), en gardant au moins 200 Go libres sur le disque, d'éditeurs connus (Qwen, Mistral, Google, IBM, Microsoft, Meta, Liquid AI, NVIDIA, et les grands préparateurs de fichiers GGUF : unsloth, bartowski, lmstudio-community, ggml-org…), que le moteur lit, en Q4_K_M de préférence avec leur projecteur d'images, qui tiennent dans le Mac et dont le nom n'est pas déjà installé, sont ajoutés.

Les modèles installés cette nuit-là (empreinte vérifiée par le service) sont écrits dans `build/bench/hebdo/nouveaux.json` : la mesure du lundi les mesure en premier, les signale « (nouveau) », et conseille le meilleur de tous dans l'onglet « Modèles » de Gulliver. Ce qu'il a trouvé (téléchargements, nouvelles versions, refus) part dans un mail à l'administrateur de Gulliver (`tools/mail_admin.h` depuis la 7.23.0, `tools/mail_admin.py` avant, depuis le 23 novembre 2026), qui décide ensuite, d'après la mesure du matin, des modèles à garder. Chaque semaine repart d'une liste vide. Les anciennes versions restent dans la réserve du service (clones APFS, sans place prise tant qu'elles sont identiques).

| Action | Commande |
|---|---|
| Installer | `bash macos/modeles-semaine/install.sh` |
| Retirer | `bash macos/modeles-semaine/install.sh --retirer` |
| Voir ce qu'il ferait, sans rien lancer | `build/macos-gulliver/bin/modeles_semaine build/gulliver-home --essai` |
| Lancer tout de suite | `launchctl kickstart gui/$(id -u)/fr.fork.gulliver.modeles-semaine` |
| Journal | `build/bench/hebdo/modeles.log` |

Les nouveaux modèles viennent du catalogue de Gulliver (celui de l'Albireo d'origine), des modèles déjà ajoutés depuis Hugging Face et des plus téléchargés de Hugging Face, limités aux éditeurs connus (`HUB_EDITORS` dans le script) : un modèle d'un inconnu n'a ni qualité ni licence vérifiées.
