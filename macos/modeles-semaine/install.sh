#!/bin/bash
# Installe (ou retire, avec --retirer) l'agent launchd qui tient à jour les modèles de Gulliver chaque lundi à 1 h
# (fork pour macOS, 2026-11-22 ; créneau choisi hors des heures où l'humain se sert du Mac) : le programme modeles_semaine (C++ depuis Gulliver 7.23.0, build/macos-gulliver/bin/ ; catalogue rafraîchi, nouvelles versions retéléchargées,
# modèles du catalogue qui tiennent dans ce Mac téléchargés), avant la mesure du lundi à 8 h (macos/bench-hebdo).  Agent de l'utilisateur
# seulement (~/Library/LaunchAgents), sans droit d'administrateur ; si le Mac dort à l'heure dite, launchd le lance au
# réveil.  Le service de Gulliver doit tourner : c'est lui qui télécharge et vérifie.
LABEL=fr.fork.gulliver.modeles-semaine
PLIST=$HOME/Library/LaunchAgents/$LABEL.plist
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PROGRAM=$ROOT/build/macos-gulliver/bin/modeles_semaine
[ -x "$PROGRAM" ] || { echo "manque : $PROGRAM (construire Gulliver d'abord)"; exit 1; }

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
  <array><string>$PROGRAM</string><string>$ROOT/build/gulliver-home</string></array>
  <key>StartCalendarInterval</key>
  <dict><key>Weekday</key><integer>1</integer><key>Hour</key><integer>1</integer><key>Minute</key><integer>0</integer></dict>
  <key>StandardOutPath</key><string>$ROOT/build/bench/hebdo/modeles.log</string>
  <key>StandardErrorPath</key><string>$ROOT/build/bench/hebdo/modeles.log</string>
  <key>ProcessType</key><string>Standard</string>
</dict>
</plist>
PL
plutil -lint "$PLIST" >/dev/null || { echo "plist invalide"; exit 1; }
launchctl bootstrap "gui/$(id -u)" "$PLIST" && echo "agent installé : $LABEL (lundi, 1 h)"
