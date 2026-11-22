#!/bin/bash
# La justesse de Gulliver comparée à llama.cpp, à MLX et à l'Albireo d'origine, sur les mêmes modèles
# et le même texte (fork pour macOS, 2026-11-21) :
#
#   bash dlib/dlib/external/gulliver/tools/justesse_moteurs.sh
#
# La perplexité (plus elle est basse, moins le modèle est surpris par le texte) d'un texte fixe,
# dix épisodes du blog du portage (build/bench-moteurs/texte.txt, refait s'il manque), en
# JUSTESSE_CHUNKS morceaux de 512 tokens (8 par défaut), calculée de la même façon par chaque
# moteur : Gulliver sur le GPU avec les clés en 8 bits (le réglage du service) et sur le
# processeur en demi-précision (perplexity_runtime), llama.cpp sur le GPU (llama-perplexity),
# MLX pour le modèle qui a sa version MLX (perplexity_mlx.py), l'Albireo d'origine sur le
# processeur (perplexity_albireo, construit par macos/albireo-original/build.sh).  Deux moteurs
# justes donnent la même perplexité sur le même fichier ; un autre fichier (MLX, Q8_0) mesure la
# compression, pas le moteur.  Le tableau, une ligne par modèle avec l'écart de chaque moteur à
# Gulliver, va dans build/bench/justesse-<date>.md.  BENCH_ONLY comme bench_moteurs.sh.

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TOOLS=$ROOT/dlib/dlib/external/gulliver/tools
PPL=$ROOT/build/macos-gulliver/tests/perplexity_runtime
LLAMA=$(command -v llama-perplexity)
MLXPY=$ROOT/build/mlx-venv/bin/python
ALBIREO_DIR=${ALBIREO_ORIGINAL_DIR:-$(cd "$ROOT/.." && pwd)/albireo-original}
ALBIREO=$ALBIREO_DIR/build-mac/perplexity_albireo
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
RAW=$OUTDIR/justesse-$STAMP.raw
LOG=$OUTDIR/justesse-$STAMP.log
: > "$LOG"; : > "$RAW"

MODELS=()
add() { [ -f "$2" ] && [[ -z "$BENCH_ONLY" || "$1" == *"$BENCH_ONLY"* ]] && MODELS+=("$1|$2|$3|${4:-1}"); }
add "Qwen3-4B Q4_K_M" "$M/Qwen3-4B-Q4_K_M.gguf" "$M/Qwen3-4B-mlx-4bit"
add "Qwen3-4B Q8_0 (référence)" "$M/Qwen3-4B-Q8_0.gguf" ""
add "Qwen3-VL 4B Thinking Q4_K_M" "$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.gguf" "$M/Qwen3-VL-4B-Thinking-4bit"
add "Qwen3-VL 2B Instruct Q4_K_M" "$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf" "$M/Qwen3-VL-2B-Instruct-4bit"
add "Gemma 3 4B Q4_K_M" "$ROOT/build/models/gemma-3-4b-it-Q4_K_M.gguf" "$M/gemma-3-4b-it-4bit"
add "LFM2.5-1.2B Q8_0" "$ROOT/build/models/LFM2.5-1.2B-Instruct-Q8_0.gguf" "$M/LFM2.5-1.2B-Instruct-8bit"
add "Qwen3-30B-A3B Q4_K_M (MoE)" "$ROOT/build/models/Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" "$M/Qwen3-30B-A3B-Instruct-2507-4bit"
# Puis les modèles installés dans Gulliver, comme bench_moteurs.sh (sans MLX ni Albireo d'origine).
if [ -z "$BENCH_NO_INSTALLED" ]; then
  while IFS='|' read -r iname ifile iid iactive iimage; do
    [ -n "$iid" ] || continue
    known=""
    for entry in "${MODELS[@]}"; do
      IFS='|' read -r _ efile _ <<< "$entry"
      [ "$(realpath "$efile")" = "$(realpath "$ifile")" ] && known=1
    done
    [ -n "$known" ] || add "$iname" "$ifile" "" 0
  done < <(python3 -I "$TOOLS/modeles_installes.py" "${GULLIVER_HOME:-$ROOT/build/gulliver-home}")
fi

# « perplexité|incertitude|top-1 » d'une sortie de perplexity_runtime, perplexity_mlx.py ou
# perplexity_albireo ; « nonlu » pour une architecture que le moteur ne lit pas.
ours() {
  local out; out=$(cat)
  if printf '%s\n' "$out" | grep -q "is not supported by this engine"; then echo "nonlu||"; return; fi
  printf '%s\n' "$out" | sed -n 's/^perplexity: \([0-9.]*\) +\/- \([0-9.]*\)$/\1|\2/p; s/^top-1: \([0-9.]*\) %$/|\1/p' | tr -d '\n'
}

for entry in "${MODELS[@]}"; do
  IFS='|' read -r name file mlxdir original <<< "$entry"
  echo "justesse : $name"
  echo "$name|gulliver_gpu|$("$PPL" "$file" "$TEXT" 512 "$CHUNKS" q8 metal 2>&1 | tee -a "$LOG" | ours)" >> "$RAW"
  echo "$name|gulliver_cpu|$("$PPL" "$file" "$TEXT" 512 "$CHUNKS" f16 2>&1 | tee -a "$LOG" | ours)" >> "$RAW"
  if [ -n "$LLAMA" ]; then
    p=$("$LLAMA" -m "$file" -f "$TEXT" -c 512 --chunks "$CHUNKS" -ngl 99 2>&1 | tee -a "$LOG" \
      | sed -n 's/.*Final estimate: PPL = \([0-9.]*\) +\/- \([0-9.]*\).*$/\1|\2|/p')
    echo "$name|llama|${p:-||}" >> "$RAW"
  fi
  if [ -d "$mlxdir" ] && [ -x "$MLXPY" ]; then
    echo "$name|mlx|$("$MLXPY" -I "$TOOLS/perplexity_mlx.py" "$mlxdir" "$TEXT" 512 "$CHUNKS" 2>&1 | tee -a "$LOG" | ours)" >> "$RAW"
  fi
  if [ -x "$ALBIREO" ] && [ "$original" = 1 ]; then
    echo "$name|albireo|$("$ALBIREO" "$file" "$TEXT" 512 "$CHUNKS" q8 2>&1 | tee -a "$LOG" | ours)" >> "$RAW"
  fi
done

{
  echo "# Justesse : Gulliver contre les autres moteurs — $(sysctl -n hw.model), $(date '+%Y-%m-%d %H:%M')"
  echo
  echo "Perplexité d'un même texte (dix épisodes du blog du portage, en français), $CHUNKS morceaux de 512 tokens, seconde moitié de chaque morceau notée, comme \`llama-perplexity\`. Plus elle est basse, mieux le modèle prévoit le texte ; deux moteurs justes donnent la même sur le même fichier. Entre parenthèses : l'écart à Gulliver sur le GPU (clés et valeurs en 8 bits, le réglage par défaut). MLX lit son propre fichier 4 bits : son écart mesure la compression, pas le moteur."
  echo
  echo "Gulliver $(sed -n 's/.*GULLIVER_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/gulliver/common/version.h" | head -1) ; llama.cpp $(llama-bench --version 2>&1 | sed -n 's/^version: //p' | head -1) ; MLX $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}')$([ -x "$ALBIREO" ] && echo " ; Albireo d'origine $(awk '/#define ALBIREO_VERSION_(MAJOR|MINOR|PATCH) /{print $3}' "$ALBIREO_DIR/dlib/dlib/external/albireo/common/version.h" | paste -sd. -) ($(git -C "$ALBIREO_DIR" rev-parse --short HEAD)), processeur")."
  python3 -I - "$RAW" <<'EOF'
import sys, collections
data = collections.OrderedDict()
for line in open(sys.argv[1], encoding="utf-8"):
    parts = line.rstrip("\n").split("|")
    name, key = parts[0], parts[1]
    data.setdefault(name, {})[key] = parts[2] if len(parts) > 2 else ""
COLS = [("gulliver_gpu", "Gulliver, GPU"), ("gulliver_cpu", "Gulliver, processeur"), ("llama", "llama.cpp, GPU"),
        ("mlx", "MLX (son propre fichier)"), ("albireo", "Albireo d'origine, processeur")]
cols = [(k, l) for k, l in COLS if any(k in m for m in data.values())]
def val(v):
    try: return float(v)
    except (TypeError, ValueError): return None
def fmt(x): return f"{x:.3f}".replace(".", ",")
print("\n| Modèle | " + " | ".join(l for _, l in cols) + " |")
print("|---|" + "---|" * len(cols))
for name, m in data.items():
    base = val(m.get("gulliver_gpu"))
    row = []
    for k, _ in cols:
        v = m.get(k)
        if v is None: row.append("—"); continue
        if v == "nonlu": row.append("ne lit pas ce modèle"); continue
        x = val(v)
        if x is None: row.append("—"); continue
        if k == "gulliver_gpu" or base is None: row.append(fmt(x)); continue
        d = 100.0 * (x / base - 1.0)
        sign = "±" if abs(d) < 0.005 else "+" if d > 0 else "−"
        row.append(f"{fmt(x)} ({sign}{abs(d):.2f} %)".replace(".", ","))
    print(f"| {name} | " + " | ".join(row) + " |")
EOF
} > "$OUT"
echo "table: $OUT"
