#!/bin/bash
# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Ouvre l'application de Pseudalis avec son jeton d'administration (fork pour macOS), comme l'icône de l'original le
# fait sous Windows : les originaux conservés, la clé, les réglages et les termes imposés s'affichent alors.
#
#   bash tools/open_pseudalis.sh          # PSEUDALIS_HOME, sinon ~/.local/share/pseudalis
#
# Le jeton ne passe jamais par la ligne de commande d'un programme, que les autres comptes du Mac peuvent lire : il
# est écrit dans un fichier temporaire lisible par ce seul compte, qui redirige le navigateur vers
# https://localhost:<port>/#admin=<jeton> puis est effacé.  Le fragment d'une adresse n'est jamais envoyé au service.
set -euo pipefail
HOME_DIR=${PSEUDALIS_HOME:-$HOME/.local/share/pseudalis}
TOKEN_FILE=$HOME_DIR/admin.token
[ -r "$TOKEN_FILE" ] || { echo "pas de jeton lisible : $TOKEN_FILE (PSEUDALIS_HOME ?)" >&2; exit 1; }
PORT=$(python3 -I -c 'import json, sys
try: print(int(json.load(open(sys.argv[1])).get("port", 8444)))
except Exception: print(8444)' "$HOME_DIR/settings.json")
TOKEN=$(tr -d ' \r\n' < "$TOKEN_FILE")
case "$TOKEN" in *[!0-9A-Za-z_-]*|"") echo "jeton illisible dans $TOKEN_FILE" >&2; exit 1 ;; esac
PAGE=$(mktemp -t pseudalis-open).html
chmod 600 "$PAGE"
printf '<!doctype html><meta charset="utf-8"><title>Pseudalis</title><script>location.replace("https://localhost:%s/#admin=%s")</script>\n' \
  "$PORT" "$TOKEN" > "$PAGE"
open "$PAGE"
# Le navigateur a lu la page : elle part.
(sleep 10; rm -f "$PAGE") > /dev/null 2>&1 &
echo "Pseudalis ouvert sur https://localhost:$PORT/ avec son jeton d'administration."
