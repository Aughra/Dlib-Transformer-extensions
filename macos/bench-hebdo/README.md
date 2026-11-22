# La mesure des modèles, chaque semaine

Un agent launchd de l'utilisateur lance chaque **lundi à 8 h** (arrêt à 19 h au plus tard ; jamais le samedi ni le dimanche entre 9 h et 21 h, quand l'humain se sert du Mac) `gulliver/tools/bench_hebdo.sh` : il attend une machine au repos (charge sous 3, un essai par heure, six au plus), reconstruit Gulliver, puis lance `bench_modeles.sh`, qui mesure **Gulliver sur tous ses modèles installés**, avec **llama.cpp et MLX** sur le modèle de référence, comme deux lignes de plus du tableau (décisions de l'humain du 22 novembre 2026 : « avoir le plus rapide toutes les semaines ») : lecture et écriture à 512 et 4 096 tokens, mémoire de conversation en 8 bits et en demi-précision, deux essais en alternance, puis la perplexité sur le GPU. Passent d'abord le modèle choisi la semaine précédente (`conseil.json`), puis les nouveaux de la nuit (`nouveaux.json`, de `macos/modeles-semaine/`, lundi 1 h), puis les autres ; à 19 h, ce qui reste n'est pas mesuré. La température de la puce est relevée toutes les 20 secondes (moyenne et maximum). La semaine, avec le tableau comparé au choix précédent et le conseil, s'ajoute sous un titre daté en tête de `build/bench/hebdo/mesures.md` (les semaines de la plus récente à la plus ancienne ; les semaines seules dans `historique.md`, et une copie datée par semaine), le conseil va dans `build/bench/hebdo/conseil.json`, que l'onglet Modèles de Gulliver affiche avec son bouton « Choisir », et la section de la semaine de `docs/mesures/tests-de-vitesse.md` (entre ses deux marqueurs) est réécrite : Gulliver seul, sur les modèles mesurés, et la température moyenne. Rien n'est commité ni poussé : les résultats restent sur le Mac.

Jusqu'au 22 novembre 2026, la mesure (le dimanche à 4 h) comparait Gulliver à llama.cpp, à MLX et à l'Albireo d'origine sur six modèles fixes ; l'Albireo d'origine n'est plus mesuré, et les comparaisons sur ces six modèles se refont à la main avec `tools/bench_moteurs.sh` et `tools/justesse_moteurs.sh`.

| Action | Commande |
|---|---|
| Installer | `bash macos/bench-hebdo/install.sh` |
| Retirer | `bash macos/bench-hebdo/install.sh --retirer` |
| Lancer tout de suite | `launchctl kickstart gui/$(id -u)/fr.fork.gulliver.bench-hebdo` |
| Voir s'il est installé | `launchctl print gui/$(id -u)/fr.fork.gulliver.bench-hebdo` |
| Journal | `build/bench/hebdo/hebdo.log` |

L'agent tourne en `ProcessType = Interactive`, la priorité d'une application ordinaire. Avec `Background` (réglage de la première installation, jusqu'au 22 novembre 2026), macOS le cantonnait aux cœurs lents avec une priorité basse : la mesure de la nuit du 21 au 22 novembre en a été faussée (l'Albireo d'origine dix fois plus lent, la lecture courte de Gulliver 40 % plus lente) et a été refaite. Après un changement de ce réglage, relancer `install.sh`, puis vérifier que `launchctl print` donne `spawn type = interactive`.

Pour les comparaisons à la main avec MLX, il faut l'environnement `build/mlx-venv` et les versions MLX des modèles dans `build/bench-moteurs/models/` ; sans eux, MLX est sauté.
