#!/bin/bash
# Lance le tunnel Cloudflare de Gulliver au démarrage de la session (LaunchAgent), ou l'arrête.
#
#   bash cloudflare/tunnel/install_tunnel.sh            installer et démarrer
#   bash cloudflare/tunnel/install_tunnel.sh --remove   arrêter et retirer
#
# Le jeton du tunnel (donné par le tableau de bord Cloudflare, Zero Trust → Networks → Tunnels) est lu
# dans ~/keys/cloudflare-tunnel-gulliver.token, un fichier que seul ce compte peut lire (chmod 600).
# Ce script ne l'affiche jamais. Le tunnel ne fait que des connexions sortantes : aucun port ouvert
# sur la box. Les adresses qu'il sert (mac-gulliver… → https://localhost:18443) se règlent dans le
# tableau de bord, pas ici.
set -eu
LABEL=com.tortuetech.gulliver-tunnel
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
TOKEN="$HOME/keys/cloudflare-tunnel-gulliver.token"
LOG="$HOME/Library/Logs/gulliver-tunnel.log"
BIN=$(command -v cloudflared || true)

if [ "${1:-}" = "--remove" ]; then
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  rm -f "$PLIST"
  echo "tunnel arrêté et retiré"
  exit 0
fi

[ -n "$BIN" ] || { echo "cloudflared manque : brew install cloudflared"; exit 1; }
[ -f "$TOKEN" ] || { echo "le jeton manque : $TOKEN (voir cloudflare/README.md)"; exit 1; }
if [ "$(stat -f %Lp "$TOKEN")" != "600" ]; then
  echo "le jeton doit n'être lisible que par ce compte : chmod 600 $TOKEN"; exit 1
fi

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$BIN</string>
    <string>tunnel</string>
    <string>--no-autoupdate</string>
    <string>run</string>
    <string>--token-file</string>
    <string>$TOKEN</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PLIST
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "tunnel démarré ; journal : $LOG"
