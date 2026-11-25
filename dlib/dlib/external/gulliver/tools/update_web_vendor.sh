#!/bin/bash
# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Remplace les bibliothèques tierces de l'application web par les versions données de leurs
# paquets npm, et écrit les versions installées dans web/vendor/components.json :
#
#   tools/update_web_vendor.sh <marked> <dompurify> <highlight.js> <swagger-ui-dist>
#   tools/update_web_vendor.sh 18.1.1 3.4.17 11.12.0 5.33.2
#
# L'application web tourne sans réseau : le service local sert chaque bibliothèque qu'elle utilise
# depuis web/vendor et web/docs, chaque dossier avec la licence de son composant.  Les paquets
# sont récupérés avec npm dans un dossier temporaire ; les fichiers que sert l'application sont
# copiés avec leurs licences, et les références aux source maps, que l'application ne livre pas,
# sont retirées.
#
# web/vendor/components.json est la liste de ces composants, que montre la page des licences de
# l'application : nom, version, paquet npm, licence, les fichiers de son texte, le dossier des
# fichiers et la source.  Un composant ajouté ici est ajouté à la main à cette liste, avec son
# rôle dans les cinq langues de web/js/i18n.js (component_<id>).
#
# Les PDF ne passent plus par le navigateur (fork pour macOS) : le service en lit le texte et en
# dessine les pages (host/doc_extract_pdf, host/pdf_render.cpp) ; PDF.js est retiré depuis la 7.3.0.
#
# Après une mise à jour, tester l'application (Markdown, code et la documentation de
# l'interface), puis relire les changements de components.json.

set -euo pipefail
if [ $# -ne 4 ]; then
   sed -n '4,30p' "$0" | sed 's/^# \{0,1\}//'
   exit 2
fi

here="$(cd "$(dirname "$0")/.." && pwd)"
web="$here/web"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cd "$work"
npm init -y > /dev/null
npm install --no-audit --no-fund --ignore-scripts "marked@$1" "dompurify@$2" "@highlightjs/cdn-assets@$3" \
   "swagger-ui-dist@$4" > /dev/null
m="$work/node_modules"

copy() { # source destination
   mkdir -p "$(dirname "$2")"
   cp "$1" "$2"
}

copy "$m/marked/lib/marked.esm.js" "$web/vendor/marked/marked.esm.js"
copy "$m/marked/LICENSE" "$web/vendor/marked/LICENSE"
copy "$m/dompurify/dist/purify.es.mjs" "$web/vendor/dompurify/purify.es.mjs"
copy "$m/dompurify/LICENSE" "$web/vendor/dompurify/LICENSE"
copy "$m/@highlightjs/cdn-assets/highlight.min.js" "$web/vendor/highlight/highlight.min.js"
copy "$m/@highlightjs/cdn-assets/styles/github.min.css" "$web/vendor/highlight/github.min.css"
copy "$m/@highlightjs/cdn-assets/styles/github-dark.min.css" "$web/vendor/highlight/github-dark.min.css"
copy "$m/@highlightjs/cdn-assets/LICENSE" "$web/vendor/highlight/LICENSE"
copy "$m/swagger-ui-dist/swagger-ui-bundle.js" "$web/docs/swagger-ui-bundle.js"
copy "$m/swagger-ui-dist/swagger-ui.css" "$web/docs/swagger-ui.css"
copy "$m/swagger-ui-dist/LICENSE" "$web/docs/LICENSE"
copy "$m/swagger-ui-dist/NOTICE" "$web/docs/NOTICE"

# Les source maps ne sont pas livrées : leurs références ne produiraient que des requêtes échouées.
for f in "$web"/vendor/*/*.js "$web"/vendor/*/*.mjs "$web"/vendor/*/*.css "$web"/docs/swagger-ui-bundle.js \
   "$web"/docs/swagger-ui.css; do
   sed -i -E -e '/^\/\/# sourceMappingURL=/d' -e 's|/\*# sourceMappingURL=[^*]*\*/||g' "$f"
done

# Les versions installées, celles des paquets que npm a résolus, dans la liste des composants.
node - "$web/vendor/components.json" "$m" <<'EOF'
const fs = require('fs');
const [file, modules] = process.argv.slice(2);
const list = JSON.parse(fs.readFileSync(file, 'utf8'));
for (const c of list.components)
   c.version = JSON.parse(fs.readFileSync(modules + '/' + c.package + '/package.json', 'utf8')).version;
fs.writeFileSync(file, JSON.stringify(list, null, 2) + '\n');
EOF

echo "Updated: marked $1, DOMPurify $2, highlight.js $3, Swagger UI $4"
