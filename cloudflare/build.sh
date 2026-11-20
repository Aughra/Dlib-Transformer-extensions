#!/bin/bash
# Assemble la page de Gulliver pour Cloudflare Pages, dans cloudflare/dist (plan : docs/plan-discussion-en-ligne.md).
#
#   bash cloudflare/build.sh
#
# Réglages du projet Pages : dossier racine « cloudflare », commande « bash build.sh », dossier de sortie « dist ».
# Les fonctions (functions/, la passerelle) sont prises par Pages dans le dossier racine, pas dans dist.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WEB="$HERE/../dlib/dlib/external/gulliver/web"
DIST="$HERE/dist"
rm -rf "${DIST:?}"
mkdir -p "$DIST"
# La page entière (sandbox.html compris : c'est le cadre où l'agent exécute son code). Les en-têtes de
# sécurité sont posés par la fonction (functions/[[path]].js), comme le fait le service sur le Mac.
rsync -a "$WEB/" "$DIST/"
echo "page assemblée dans $DIST ($(find "$DIST" -type f | wc -l | tr -d ' ') fichiers)"
