# Tests de vitesse chaque semaine

Un agent launchd de l'utilisateur lance chaque dimanche à 4 h `gulliver/tools/bench_hebdo.sh` : il attend une machine au repos (charge sous 3, un essai par heure, six au plus), reconstruit Gulliver, compare sa vitesse (et sa justesse) à llama.cpp et à MLX sur les mêmes modèles (`bench_moteurs.sh`, `justesse_moteurs.sh`), puis ajoute la mesure, sous un titre daté, en tête de `build/bench/hebdo/mesures.md` (les semaines de la plus récente à la plus ancienne ; les semaines seules dans `historique.md`, et une copie datée par semaine). Le carnet de Gulliver le montre dans la rubrique « Tests de vitesse ». Rien n'est commité ni poussé : les résultats restent sur le Mac.

| Action | Commande |
|---|---|
| Installer | `bash macos/bench-hebdo/install.sh` |
| Retirer | `bash macos/bench-hebdo/install.sh --retirer` |
| Lancer tout de suite | `launchctl kickstart gui/$(id -u)/fr.fork.gulliver.bench-hebdo` |
| Voir s'il est installé | `launchctl print gui/$(id -u)/fr.fork.gulliver.bench-hebdo` |
| Journal | `build/bench/hebdo/hebdo.log` |

Pour MLX, il faut l'environnement `build/mlx-venv` (`python3 -m venv build/mlx-venv && build/mlx-venv/bin/pip install mlx-lm`) et le modèle `build/bench-moteurs/models/Qwen3-4B-mlx-4bit` ; sans eux, MLX est simplement sauté. Les versions de llama.cpp (Homebrew) et de MLX ne sont jamais mises à jour par l'agent : chaque tableau note celles qu'il a mesurées.
