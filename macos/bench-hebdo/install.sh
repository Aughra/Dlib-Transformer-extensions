#!/bin/bash
# Installe (ou retire, avec --retirer) l'agent launchd qui lance les tests de vitesse et de
# justesse de Gulliver chaque dimanche à 4 h (fork pour macOS, 2026-11-21).  Agent de l'utilisateur
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
  <dict><key>Weekday</key><integer>0</integer><key>Hour</key><integer>4</integer><key>Minute</key><integer>0</integer></dict>
  <key>StandardOutPath</key><string>$ROOT/build/bench/hebdo/launchd.log</string>
  <key>StandardErrorPath</key><string>$ROOT/build/bench/hebdo/launchd.log</string>
  <key>ProcessType</key><string>Background</string>
</dict>
</plist>
PL
plutil -lint "$PLIST" >/dev/null || { echo "plist invalide"; exit 1; }
launchctl bootstrap "gui/$(id -u)" "$PLIST" && echo "agent installé : $LABEL (dimanche, 4 h)"
