#!/bin/bash
# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Remplace les bibliothèques tierces de l'application web par les versions données de leurs
# paquets npm, et écrit les versions installées dans web/vendor/components.json :
#
#   tools/update_web_vendor.sh <marked> <dompurify> <highlight.js> <katex> <pdfjs-dist> <swagger-ui-dist>
#   tools/update_web_vendor.sh 18.1.1 3.4.17 11.12.0 0.19.0 6.4.299 5.33.2
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
# PDF.js vient du build legacy de pdfjs-dist, qui tourne dans Edge et Chrome à partir de la
# version 125.  Son build moderne demande la version 147, et un ordinateur tenu hors du réseau
# peut avoir un navigateur plus ancien.  Les CMaps de pdfjs-dist donnent le texte des polices CJK
# qu'un PDF nomme sans les embarquer.  Les polices standard et les décodeurs d'images WebAssembly
# de pdfjs-dist ne servent qu'à dessiner les pages, ce que l'application ne fait pas, et sont
# laissés de côté.
#
# Après une mise à jour, tester l'application (Markdown, code, formules, documents PDF et la
# documentation de l'interface), puis relire les changements de components.json.

set -euo pipefail
if [ $# -ne 6 ]; then
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
   "katex@$4" "pdfjs-dist@$5" "swagger-ui-dist@$6" > /dev/null
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
copy "$m/katex/dist/katex.min.js" "$web/vendor/katex/katex.min.js"
copy "$m/katex/dist/katex.min.css" "$web/vendor/katex/katex.min.css"
copy "$m/katex/LICENSE" "$web/vendor/katex/LICENSE"
rm -rf "$web/vendor/katex/fonts"
mkdir -p "$web/vendor/katex/fonts"
cp "$m"/katex/dist/fonts/*.woff2 "$web/vendor/katex/fonts/"
# PDF.js vient de son build legacy, qui tourne dans Edge et Chrome à partir de la version 125.  Le
# build moderne demande la version 147 ou plus, et un ordinateur tenu hors du réseau peut avoir un
# navigateur plus ancien.
copy "$m/pdfjs-dist/legacy/build/pdf.min.mjs" "$web/vendor/pdfjs/pdf.min.mjs"
copy "$m/pdfjs-dist/legacy/build/pdf.worker.min.mjs" "$web/vendor/pdfjs/pdf.worker.min.mjs"
copy "$m/pdfjs-dist/LICENSE" "$web/vendor/pdfjs/LICENSE"
# Les CMaps des polices CJK qu'un PDF nomme sans les embarquer, dont son texte a besoin.  Les
# polices standard et les décodeurs d'images WebAssembly de PDF.js ne servent qu'à dessiner les pages.
rm -rf "$web/vendor/pdfjs/cmaps"
mkdir -p "$web/vendor/pdfjs/cmaps"
cp "$m"/pdfjs-dist/cmaps/*.bcmap "$m/pdfjs-dist/cmaps/LICENSE" "$web/vendor/pdfjs/cmaps/"
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

echo "Updated: marked $1, DOMPurify $2, highlight.js $3, KaTeX $4, PDF.js $5, Swagger UI $6"
