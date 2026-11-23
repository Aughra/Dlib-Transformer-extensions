#!/bin/bash
# Copyright (C) 2026  papy cyber
# Installe (ou retire, avec --retirer) le surveillant du Mac (fork pour macOS, 2026-11-23) : tools/surveillant.py,
# lancé à l'ouverture de session et relancé s'il s'arrête (KeepAlive).  Agent de l'utilisateur seulement
# (~/Library/LaunchAgents), sans droit d'administrateur : il ne peut arrêter que les programmes de l'utilisateur.
LABEL=fr.fork.gulliver.surveillance
PLIST=$HOME/Library/LaunchAgents/$LABEL.plist
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SCRIPT=$ROOT/dlib/dlib/external/gulliver/tools/surveillant.py

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null
if [ "$1" = "--retirer" ]; then rm -f "$PLIST"; echo "agent retiré"; exit 0; fi

mkdir -p "$HOME/Library/LaunchAgents" "$ROOT/build/surveillance"
cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array><string>/usr/bin/python3</string><string>-I</string><string>$SCRIPT</string><string>$ROOT/build/gulliver-home</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>60</integer>
  <key>StandardOutPath</key><string>$ROOT/build/surveillance/surveillant.log</string>
  <key>StandardErrorPath</key><string>$ROOT/build/surveillance/surveillant.log</string>
  <key>ProcessType</key><string>Background</string>
</dict>
</plist>
PL
plutil -lint "$PLIST" >/dev/null || { echo "plist invalide"; exit 1; }
launchctl bootstrap "gui/$(id -u)" "$PLIST" && echo "agent installé : $LABEL (en continu)"
