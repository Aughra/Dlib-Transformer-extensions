#!/bin/bash
# Compare un modèle GGUF importé en réseau dlib (l'atelier, R1) au moteur de Gulliver.
#
#   bash dlib/dlib/external/gulliver/tools/atelier/compare_dlib_import.sh model.gguf [prompt]
#
# 1. slm_gguf_import_ex (build des exemples de dlib, build/macos-arm64-cpu) détecte le modèle et
#    écrit l'en-tête de son réseau dans build/atelier/<nom>/ ;
# 2. l'exemple est recompilé avec cet en-tête (build/macos-atelier-<nom>), puis sonde le prompt
#    et écrit les log-probabilités de chaque position (--dump-logits) ;
# 3. test_runtime de Gulliver (build/macos-gulliver) lit les mêmes tokens et compare, en
#    produits exacts puis en fonctionnement normal.
# Le code de sortie est le nombre de comparaisons échouées.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
MODEL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
PROMPT=${2:-"The capital of France is a city famous for its museums, its river and its bridges. Many visitors come every year to see"}
NAME=$(basename "$MODEL" .gguf)
WORK=$ROOT/build/atelier/$NAME
EXAMPLES=$ROOT/build/macos-arm64-cpu
MODEL_BUILD=$ROOT/build/macos-atelier-$NAME
GULLIVER=$ROOT/build/macos-gulliver
OPTIONS=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DDLIB_NO_GUI_SUPPORT=ON
  -DDLIB_USE_CUDA=OFF -DDLIB_USE_NPU=OFF -DDLIB_USE_BLAS=OFF -DDLIB_USE_LAPACK=OFF -DDLIB_USE_MKL_FFT=OFF
  -DDLIB_USE_FFMPEG=OFF -DDLIB_JPEG_SUPPORT=OFF -DDLIB_PNG_SUPPORT=OFF -DDLIB_GIF_SUPPORT=OFF
  -DDLIB_WEBP_SUPPORT=OFF -DDLIB_JXL_SUPPORT=OFF)

mkdir -p "$WORK"
cd "$WORK" || exit 1

# 1. Détection et en-tête du réseau.
if [ ! -x "$EXAMPLES/slm_gguf_import_ex" ]; then
  cmake -S "$ROOT/dlib/examples" -B "$EXAMPLES" "${OPTIONS[@]}" > "$WORK/config-examples.log" 2>&1 || exit 1
  cmake --build "$EXAMPLES" --target slm_gguf_import_ex > "$WORK/build-examples.log" || exit 1
fi
"$EXAMPLES/slm_gguf_import_ex" --input "$MODEL" --out-prefix slm_imported_model > "$WORK/detect.log" 2>&1
if [ ! -f "$WORK/slm_imported_model.h" ]; then
  echo "detection failed: $WORK/detect.log"; tail -5 "$WORK/detect.log"; exit 1
fi

# 2. Le réseau compilé, puis sondé.
cmake -S "$ROOT/dlib/examples" -B "$MODEL_BUILD" "${OPTIONS[@]}" \
  -DCMAKE_CXX_FLAGS="-I$WORK -DWITH_IMPORTED_MODEL=1" > "$WORK/config-model.log" 2>&1 || exit 1
cmake --build "$MODEL_BUILD" --target slm_gguf_import_ex > "$WORK/build-model.log" 2>&1 \
  || { echo "network build failed: $WORK/build-model.log"; exit 1; }
"$MODEL_BUILD/slm_gguf_import_ex" --input "$MODEL" --probe-logits --prompt "$PROMPT" \
  --dump-logits "$WORK/logprobs.bin" > "$WORK/probe.log" 2>&1
IDS=$(sed -n 's/^Token ids fed: //p' "$WORK/probe.log")
if [ -z "$IDS" ] || [ ! -f "$WORK/logprobs.bin" ]; then
  echo "probe failed: $WORK/probe.log"; tail -5 "$WORK/probe.log"; exit 1
fi
sed -n '/Most probable/,/^$/p' "$WORK/probe.log"

# 3. Le moteur de Gulliver sur les mêmes tokens.
FAILED=0
for mode in "exact logprobs" "logprobs"; do
  # shellcheck disable=SC2086
  "$GULLIVER/tests/test_runtime" "$MODEL" "$WORK/logprobs.bin" $mode $IDS > "$WORK/compare-${mode// /-}.log" 2>&1
  code=$?
  line=$(grep -E "^  reference" "$WORK/compare-${mode// /-}.log")
  if [ $code -eq 0 ]; then echo "PASS  $line"; else echo "FAIL  $line  ($WORK/compare-${mode// /-}.log)"; FAILED=$((FAILED + 1)); fi
done
exit $FAILED
