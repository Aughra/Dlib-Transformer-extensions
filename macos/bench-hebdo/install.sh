#!/bin/bash
# Installe (ou retire, avec --retirer) l'agent launchd qui mesure les modèles de Gulliver chaque
# lundi à 8 h, sans heure limite (décision de l'humain, 2026-11-24) (fork pour macOS, 2026-11-21 ; créneau du 2026-11-22, hors des
# heures où l'humain se sert du Mac : la nuit de lundi télécharge les modèles, macos/modeles-semaine).  Agent de l'utilisateur
# seulement (~/Library/LaunchAgents), sans droit d'administrateur ; si le Mac dort à l'heure dite,
# launchd le lance au réveil.
LABEL=fr.fork.gulliver.bench-hebdo
PLIST=$HOME/Library/LaunchAgents/$LABEL.plist
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SCRIPT=$ROOT/dlib/dlib/external/gulliver/tools/bench_hebdo.sh

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null
if [ "$1" = "--retirer" ]; then rm -f "$PLIST"; echo "agent retiré"; exit 0; fi

mkdir -p "$HOME/Library/LaunchAgents" "$ROOT/build/bench/hebdo"
cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array><string>/bin/bash</string><string>$SCRIPT</string></array>
  <key>StartCalendarInterval</key>
  <dict><key>Weekday</key><integer>1</integer><key>Hour</key><integer>8</integer><key>Minute</key><integer>0</integer></dict>
  <key>StandardOutPath</key><string>$ROOT/build/bench/hebdo/launchd.log</string>
  <key>StandardErrorPath</key><string>$ROOT/build/bench/hebdo/launchd.log</string>
  <key>ProcessType</key><string>Interactive</string>
</dict>
</plist>
PL
plutil -lint "$PLIST" >/dev/null || { echo "plist invalide"; exit 1; }
launchctl bootstrap "gui/$(id -u)" "$PLIST" && echo "agent installé : $LABEL (lundi, 8 h)"
