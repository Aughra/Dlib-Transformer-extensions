# Le surveillant du Mac

Un agent launchd de l'utilisateur fait tourner en continu le programme `build/macos-gulliver/bin/surveillant` (en C++ depuis Gulliver 7.24.0, à la place de `tools/surveillant.py` ; demande de l'humain du 23 novembre 2026 : surveiller le processeur et la température, prévenir par mail après 30 minutes de souci, et arrêter ce qui en est la cause).

Toutes les 10 secondes, il relève la température de la puce (la sonde la plus chaude, par l'outil de `macos/temperature/`), la charge du processeur et ce que chaque programme a calculé depuis le relevé d'avant : un programme qui ne vit qu'une demi-minute, puis se relance, reste compté. Le Mac est **en souci** quand le processeur est occupé à 80 % ou plus, ou que la puce dépasse 95 °C. Quand il l'a été pendant au moins 90 % des relevés de la dernière demi-heure :

1. les **coupables** sont les programmes de l'utilisateur qui ont pris en moyenne au moins 2 cœurs sur la demi-heure (regroupés par exécutable ; seuls ses processus qui calculent encore, un quart de cœur au moins, sont visés ; le mail nomme le programme qui les a lancés) ;
2. ils reçoivent l'ordre de s'arrêter (SIGTERM), puis sont arrêtés de force (SIGKILL) 15 secondes plus tard s'ils vivent encore ;
3. un **mail à l'administrateur** de Gulliver (`tools/mail_admin.h`, par Proton Mail Bridge) donne les chiffres, ce qui a été arrêté et les gros consommateurs protégés ;
4. la surveillance repart d'une demi-heure vide.

**Jamais arrêtés** : les programmes du système (ceux d'un autre utilisateur ; l'agent n'a d'ailleurs pas le droit de les arrêter), le service de Gulliver, le tunnel de Cloudflare, Proton Mail Bridge, VS Code, Claude, les programmes de Gulliver (`build/macos-gulliver/bin/`), llama.cpp, MLX, le Finder, le Dock, le Terminal et le surveillant lui-même (la protection se lit sur la ligne de commande entière). Les programmes construits dans le brouillon d'une session de Claude (`/private/tmp/claude-501/…`) ne sont pas protégés. **Pendant la mesure du lundi** (`macos/bench-hebdo/`), qui chauffe le Mac exprès, rien n'est compté ni arrêté. Les seuils sont dans `tools/surveillant.h` (`surveillant_config` : `cpu_threshold`, `temp_threshold`, `cores`, `window`).

| Action | Commande |
|---|---|
| Installer | `bash macos/surveillance/install.sh` |
| Retirer | `bash macos/surveillance/install.sh --retirer` |
| Le test (processus d'essai seulement, faux serveur d'envoi, une minute) | `build/macos-gulliver/tests/test_surveillant` |
| Un relevé, sans rien arrêter | `build/macos-gulliver/bin/surveillant --essai` |
| Voir s'il tourne | `launchctl print gui/$(id -u)/fr.fork.gulliver.surveillance` |
| Journal | `build/surveillance/surveillant.log` |
