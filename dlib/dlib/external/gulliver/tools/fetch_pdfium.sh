#!/usr/bin/env bash
# Récupère la bibliothèque PDFium (binaire arm64 macOS) pour l'extraction de texte des PDF côté
# service.  La dylib (~7,6 Mo) n'est pas versionnée dans git ; ce script la télécharge dans
# third_party/pdfium/lib/.  Les en-têtes, la licence et ce script, eux, sont versionnés.
#
# Source : bblanchon/pdfium-binaries (licence MIT), version sans V8 (on n'exécute pas le
# JavaScript des PDF, on extrait le texte et on rend des images).
#
# Usage : bash tools/fetch_pdfium.sh   (depuis n'importe où ; idempotent).
set -euo pipefail

# La version attendue (doit concorder avec third_party/pdfium/VERSION).
TAG="chromium/8165"
ASSET="pdfium-mac-arm64.tgz"
URL="https://github.com/bblanchon/pdfium-binaries/releases/download/${TAG}/${ASSET}"

HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$HERE/third_party/pdfium"
LIB="$DEST/lib/libpdfium.dylib"

if [ "$(uname -m)" != "arm64" ]; then
  echo "fetch_pdfium: ce fork ne vise qu'Apple Silicon (arm64) ; architecture vue : $(uname -m)" >&2
  exit 1
fi

if [ -f "$LIB" ]; then
  echo "fetch_pdfium: déjà présent ($LIB)"
  exit 0
fi

echo "fetch_pdfium: téléchargement de $ASSET ($TAG)…"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL --max-time 120 -o "$TMP/pdfium.tgz" "$URL"
tar xzf "$TMP/pdfium.tgz" -C "$TMP"

mkdir -p "$DEST/lib" "$DEST/include"
cp "$TMP/lib/libpdfium.dylib" "$LIB"
# Les en-têtes et la licence suivent le binaire, au cas où le dépôt n'en aurait pas.
cp -R "$TMP/include/." "$DEST/include/" 2>/dev/null || true
cp "$TMP/LICENSE" "$DEST/LICENSE" 2>/dev/null || true
cp "$TMP/VERSION" "$DEST/VERSION" 2>/dev/null || true

# macOS met en quarantaine les fichiers téléchargés ; on la retire pour que la dylib se charge.
xattr -d com.apple.quarantine "$LIB" 2>/dev/null || true

echo "fetch_pdfium: installé dans $LIB"
