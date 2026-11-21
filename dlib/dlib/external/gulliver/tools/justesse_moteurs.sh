#!/bin/bash
# La justesse de Gulliver comparée à llama.cpp et à MLX, sur les mêmes modèles et le même texte
# (fork pour macOS, 2026-11-21) :
#
#   bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh
#
# La perplexité (plus elle est basse, moins le modèle est surpris par le texte) d'un texte fixe,
# dix épisodes du blog du portage (build/bench-moteurs/texte.txt, refait s'il manque), en
# JUSTESSE_CHUNKS morceaux de 512 tokens (8 par défaut), calculée de la même façon par chaque
# moteur : Gulliver sur le GPU avec les clés en 8 bits (le réglage du service) et sur le
# processeur en demi-précision (perplexity_runtime), llama.cpp sur le GPU (llama-perplexity),
# MLX pour le modèle qui a sa version MLX (perplexity_mlx.py).  Deux moteurs justes donnent la
# même perplexité sur le même fichier ; un autre fichier (MLX, Q8_0) mesure la compression, pas
# le moteur.  Le tableau va dans build/bench/justesse-<date>.md.  BENCH_ONLY comme bench_moteurs.sh.

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TOOLS=$ROOT/dlib/dlib/external/gulliver/tools
PPL=$ROOT/build/macos-gulliver/tests/perplexity_runtime
LLAMA=$(command -v llama-perplexity)
MLXPY=$ROOT/build/mlx-venv/bin/python
M=$ROOT/build/bench-moteurs/models
TEXT=$ROOT/build/bench-moteurs/texte.txt
CHUNKS=${JUSTESSE_CHUNKS:-8}
[ -x "$PPL" ] || { echo "no $PPL: build Gulliver first"; exit 1; }
mkdir -p "$(dirname "$TEXT")"
[ -f "$TEXT" ] || cat "$ROOT"/blog/5[0-9]-*.md > "$TEXT"

STAMP=$(date +%Y%m%d-%H%M)
OUTDIR=$ROOT/build/bench
mkdir -p "$OUTDIR"
OUT=$OUTDIR/justesse-$STAMP.md
LOG=$OUTDIR/justesse-$STAMP.log
: > "$LOG"

MODELS=()
add() { [ -f "$2" ] && [[ -z "$BENCH_ONLY" || "$1" == *"$BENCH_ONLY"* ]] && MODELS+=("$1|$2|$3"); }
add "Qwen3-4B Q4_K_M" "$M/Qwen3-4B-Q4_K_M.gguf" "$M/Qwen3-4B-mlx-4bit"
add "Qwen3-4B Q8_0 (référence)" "$M/Qwen3-4B-Q8_0.gguf" ""
add "Qwen3-VL 4B Thinking Q4_K_M (service)" "$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.gguf" ""
add "Qwen3-VL 2B Instruct Q4_K_M" "$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf" ""
add "Gemma 3 4B Q4_K_M" "$ROOT/build/models/gemma-3-4b-it-Q4_K_M.gguf" ""
add "LFM2.5-1.2B Q8_0" "$ROOT/build/models/LFM2.5-1.2B-Instruct-Q8_0.gguf" ""
add "Qwen3-30B-A3B Q4_K_M (MoE)" "$ROOT/build/models/Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" ""

# « perplexité ± incertitude|top-1 » d'une sortie de perplexity_runtime ou de perplexity_mlx.py.
ours() { sed -n 's/^perplexity: \(.*\)$/\1/p; s/^top-1: \(.*\)$/|\1/p' | tr -d '\n'; }

{
  echo "# Justesse : Gulliver contre d'autres moteurs — $(sysctl -n hw.model), $(date '+%Y-%m-%d %H:%M')"
  echo
  echo "Perplexité d'un même texte (dix épisodes du blog du portage, en français), $CHUNKS morceaux de 512 tokens, seconde moitié de chaque morceau notée, comme \`llama-perplexity\`. Plus elle est basse, mieux le modèle prévoit le texte ; deux moteurs justes donnent la même sur le même fichier. « Top-1 » : part des positions où le mot suivant est le premier choix du modèle."
  echo
  echo "Gulliver $(sed -n 's/.*GULLIVER_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/gulliver/common/version.h" | head -1) ; llama.cpp $(llama-bench --version 2>&1 | sed -n 's/^version: //p' | head -1) ; MLX $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}')."
  echo
  echo "| Modèle | Moteur | Perplexité | Top-1 |"
  echo "|---|---|---|---|"
} > "$OUT"

for entry in "${MODELS[@]}"; do
  IFS='|' read -r name file mlxdir <<< "$entry"
  echo "justesse : $name"
  r=$("$PPL" "$file" "$TEXT" 512 "$CHUNKS" q8 metal 2>&1 | tee -a "$LOG" | ours)
  echo "| $name | Gulliver, GPU, clés en 8 bits | ${r%%|*} | ${r#*|} |" >> "$OUT"
  r=$("$PPL" "$file" "$TEXT" 512 "$CHUNKS" f16 2>&1 | tee -a "$LOG" | ours)
  echo "| $name | Gulliver, processeur, demi-précision | ${r%%|*} | ${r#*|} |" >> "$OUT"
  if [ -n "$LLAMA" ]; then
    p=$("$LLAMA" -m "$file" -f "$TEXT" -c 512 --chunks "$CHUNKS" -ngl 99 2>&1 | tee -a "$LOG" \
      | sed -n 's/.*Final estimate: PPL = \(.*\)$/\1/p')
    echo "| $name | llama.cpp, GPU | ${p:-—} | — |" >> "$OUT"
  fi
  if [ -n "$mlxdir" ] && [ -x "$MLXPY" ]; then
    r=$("$MLXPY" -I "$TOOLS/perplexity_mlx.py" "$mlxdir" "$TEXT" 512 "$CHUNKS" 2>&1 | tee -a "$LOG" | ours)
    echo "| $name | MLX, son propre fichier 4 bits | ${r%%|*} | ${r#*|} |" >> "$OUT"
  fi
done
echo "table: $OUT"
