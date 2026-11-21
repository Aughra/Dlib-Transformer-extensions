#!/bin/bash
# Construit les outils de mesure de l'Albireo d'origine, à côté de Gulliver, pour les comparer sur
# les mêmes tests (fork pour macOS, 2026-11-21, demande de l'humain).
#
#   bash macos/albireo-original/build.sh
#
# La copie de l'original (avec son propre dlib) est un « git worktree » de upstream, figé sur le
# commit de COMMIT, dans un dossier à part, hors du fork : ALBIREO_ORIGINAL_DIR, par défaut
# ../albireo-original à côté du dépôt.  Rien n'y est modifié : son bench_runtime compile tel quel
# sur macOS, avec ses noyaux « portables » (ceux des processeurs sans AVX2), sur le processeur seul
# (l'original n'a que CUDA et OpenVINO pour accélérateurs).  perplexity_albireo.cpp, du fork, est
# compilé contre son moteur.  Les binaires vont dans <copie>/build-mac/.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
DIR=${ALBIREO_ORIGINAL_DIR:-$(cd "$ROOT/.." && pwd)/albireo-original}
COMMIT=$(cat "$HERE/COMMIT")

if [ ! -d "$DIR" ]; then
  git -C "$ROOT" fetch -q upstream
  git -C "$ROOT" worktree add --detach "$DIR" "$COMMIT"
elif [ "$(git -C "$DIR" rev-parse HEAD)" != "$COMMIT" ]; then
  git -C "$ROOT" fetch -q upstream
  git -C "$DIR" checkout -q --detach "$COMMIT"
fi

A=$DIR/dlib/dlib/external/albireo
OUT=$DIR/build-mac
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O3 -DNDEBUG -arch arm64 -I"$DIR/dlib")
ENGINE=("$A/engine/cpu_runtime.cpp" "$A/engine/model_config.cpp" "$A/engine/model_file.cpp" "$A/engine/qkernels.cpp"
  "$A/common/system_info.cpp")
c++ "${FLAGS[@]}" "$A/tests/bench_runtime.cpp" "${ENGINE[@]}" -o "$OUT/bench_runtime"
c++ "${FLAGS[@]}" "$HERE/perplexity_albireo.cpp" "${ENGINE[@]}" -o "$OUT/perplexity_albireo"
echo "Albireo $(awk '/#define ALBIREO_VERSION_(MAJOR|MINOR|PATCH) /{print $3}' "$A/common/version.h" | paste -sd. -) ($(git -C "$DIR" rev-parse --short HEAD)) : $OUT"
