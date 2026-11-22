#!/bin/bash
# Le test complet sur une copie des programmes (fork pour macOS, 2026-11-22) : test_macos.sh lit bin/ et tests/ du
# dossier de build pendant dix à quinze minutes, et une recompilation pendant ce temps faussait son résultat (deux fois
# le 2026-11-22).  La copie se fait par clones APFS (cp -c : instantanée, sans place prise), dans build/test-copie.
#
#   GULLIVER_TEST_PORT=18473 bash dlib/dlib/external/gulliver/tools/test_macos_copie.sh [build] [modèle] [embedding]
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
SRC=${1:-$ROOT/build/macos-gulliver}
[ -x "$SRC/bin/gulliver-service" ] && [ -d "$SRC/tests" ] || { echo "no Gulliver build in $SRC"; exit 1; }
rm -rf "$ROOT/build/test-copie"
mkdir -p "$ROOT/build/test-copie"
cp -cR "$SRC/bin" "$SRC/tests" "$ROOT/build/test-copie/" || { echo "the copy of $SRC failed"; exit 1; }
echo "copie des programmes : $ROOT/build/test-copie ($(sed -n 's/.*GULLIVER_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/gulliver/common/version.h" | head -1))"
[ $# -gt 0 ] && shift
exec bash "$(dirname "$0")/test_macos.sh" "$ROOT/build/test-copie" "$@"
