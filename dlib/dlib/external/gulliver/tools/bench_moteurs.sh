#!/bin/bash
# Gulliver contre d'autres moteurs locaux, sur les mêmes modèles (fork pour macOS, 2026-11-21).
#
#   bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh [essais]
#
# Pour chaque modèle présent et chaque longueur de question (BENCH_SIZES, « 512 4096 16384 » par
# défaut), la lecture de la question puis l'écriture de 64 tokens après elle, sur le GPU, avec :
#   - Gulliver (bench_runtime), clés et valeurs en 8 bits (le réglage du service) puis en demi-précision ;
#   - llama.cpp (llama-bench de Homebrew), réglages par défaut puis clés et valeurs en 8 bits ;
#   - MLX (mlx_lm.benchmark, environnement build/mlx-venv), pour le modèle qui a sa version MLX.
# Les réglages se suivent en alternance (A, B, C, D, puis de nouveau A…), essais fois (2 par défaut),
# comme le veut la règle de mesure de CLAUDE.md.  Le tableau va dans build/bench/moteurs-<date>.md,
# les sorties brutes à côté.  BENCH_ONLY=<texte> ne garde que les modèles dont le nom le contient.

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
ROUNDS=${1:-2}
SIZES=${BENCH_SIZES:-512 4096 16384}
STEPS=64
BENCH=$ROOT/build/macos-gulliver/tests/bench_runtime
LLAMA=$(command -v llama-bench)
MLX=$ROOT/build/mlx-venv/bin/mlx_lm.benchmark
M=$ROOT/build/bench-moteurs/models
[ -x "$BENCH" ] || { echo "no $BENCH: build Gulliver first"; exit 1; }

STAMP=$(date +%Y%m%d-%H%M)
OUTDIR=$ROOT/build/bench
mkdir -p "$OUTDIR"
OUT=$OUTDIR/moteurs-$STAMP.md
RAW=$OUTDIR/moteurs-$STAMP.raw
LOG=$OUTDIR/moteurs-$STAMP.log
: > "$RAW"; : > "$LOG"

# Les modèles : nom|fichier GGUF|dossier MLX (vide s'il n'y en a pas).
MODELS=()
add() { [ -f "$2" ] && [[ -z "$BENCH_ONLY" || "$1" == *"$BENCH_ONLY"* ]] && MODELS+=("$1|$2|$3"); }
add "Qwen3-4B Q4_K_M" "$M/Qwen3-4B-Q4_K_M.gguf" "$M/Qwen3-4B-mlx-4bit"
add "Qwen3-VL 4B Thinking Q4_K_M (service)" "$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.gguf" ""
add "Qwen3-VL 2B Instruct Q4_K_M" "$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf" ""
add "Gemma 3 4B Q4_K_M" "$ROOT/build/models/gemma-3-4b-it-Q4_K_M.gguf" ""
add "LFM2.5-1.2B Q8_0" "$ROOT/build/models/LFM2.5-1.2B-Instruct-Q8_0.gguf" ""
add "Qwen3-30B-A3B Q4_K_M (MoE)" "$ROOT/build/models/Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" ""
[ ${#MODELS[@]} -gt 0 ] || { echo "no model to measure"; exit 1; }

# Une mesure : écrit « modèle|taille|réglage|lecture|écriture » dans RAW.
gulliver() {  # $1 nom, $2 fichier, $3 taille, $4 q8|f16
  local out p g
  out=$(BENCH_GPU_CONTEXT=262144 "$BENCH" "$2" "$(sysctl -n hw.physicalcpu)" "$3" "$STEPS" 0 "$4" metal 2>&1)
  printf '== gulliver %s %s %s\n%s\n' "$1" "$3" "$4" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/^prompt: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  g=$(printf '%s\n' "$out" | sed -n 's/^generation: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  local label="clés en 8 bits"; [ "$4" = f16 ] && label="clés en demi-précision"
  echo "$1|$3|Gulliver, $label|${p:-—}|${g:-—}" >> "$RAW"
}
llama_ts() {  # lit avg_ts dans la sortie JSON de llama-bench
  python3 -I -c 'import json,sys
try:
    print(round(json.load(sys.stdin)[0]["avg_ts"],1))
except Exception:
    print("—")'
}
llama() {  # $1 nom, $2 fichier, $3 taille, $4 étiquette, $5… options
  local name=$1 file=$2 n=$3 label=$4; shift 4
  local p g
  p=$("$LLAMA" -m "$file" -ngl 99 -p "$n" -n 0 -r 2 -o json "$@" 2>>"$LOG" | llama_ts)
  g=$("$LLAMA" -m "$file" -ngl 99 -p 0 -n "$STEPS" -d "$n" -r 2 -o json "$@" 2>>"$LOG" | llama_ts)
  echo "$name|$n|llama.cpp, $label|$p|$g" >> "$RAW"
}
mlx() {  # $1 nom, $2 dossier, $3 taille
  local out p g
  out=$("$MLX" --model "$2" -p "$3" -g "$STEPS" -n 2 2>&1)
  printf '== mlx %s %s\n%s\n' "$1" "$3" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/.*prompt_tps=\([0-9.]*\).*/\1/p' | tail -1)
  g=$(printf '%s\n' "$out" | sed -n 's/.*generation_tps=\([0-9.]*\).*/\1/p' | tail -1)
  echo "$1|$3|MLX $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}'), 4 bits|${p:-—}|${g:-—}" >> "$RAW"
}

LOAD_START=$(sysctl -n vm.loadavg)
for ((r = 1; r <= ROUNDS; ++r)); do
  for entry in "${MODELS[@]}"; do
    IFS='|' read -r name file mlxdir <<< "$entry"
    for n in $SIZES; do
      echo "essai $r : $name, $n tokens"
      gulliver "$name" "$file" "$n" q8
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" "réglages par défaut"
      gulliver "$name" "$file" "$n" f16
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" "clés en 8 bits" -fa 1 -ctk q8_0 -ctv q8_0
      [ -n "$mlxdir" ] && [ -x "$MLX" ] && mlx "$name" "$mlxdir" "$n"
    done
  done
done
LOAD_END=$(sysctl -n vm.loadavg)

# Le tableau : pour chaque modèle et chaque taille, la plage des essais de chaque réglage.
{
  echo "# Gulliver contre d'autres moteurs — $(sysctl -n hw.model), $(date '+%Y-%m-%d %H:%M')"
  echo
  echo "| Machine | |"
  echo "|---|---|"
  echo "| Puce | $(sysctl -n machdep.cpu.brand_string), $(( $(sysctl -n hw.memsize) / 1073741824 )) Go |"
  echo "| macOS | $(sw_vers -productVersion) ($(sw_vers -buildVersion)) |"
  echo "| Gulliver | $(sed -n 's/.*GULLIVER_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/gulliver/common/version.h" | head -1) ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null)) |"
  echo "| llama.cpp | $("$LLAMA" --version 2>&1 | sed -n 's/^version: //p' | head -1) (Homebrew) |"
  echo "| MLX | $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}') (mlx-lm $("$ROOT/build/mlx-venv/bin/pip" show mlx-lm 2>/dev/null | awk '/^Version/{print $2}'), Python 3.9 du système) |"
  echo "| Charge | $LOAD_START au début, $LOAD_END à la fin |"
  echo
  echo "Tokens par seconde, sur le GPU ; lecture de la question, puis écriture de $STEPS tokens après elle ; $ROUNDS essais en alternance (plage des essais)."
  echo
  python3 -I - "$RAW" <<'EOF'
import sys, collections
rows = collections.OrderedDict()
for line in open(sys.argv[1], encoding="utf-8"):
    name, n, cfg, p, g = line.rstrip("\n").split("|")
    rows.setdefault((name, int(n)), collections.OrderedDict()).setdefault(cfg, []).append((p, g))
def span(vals):
    nums = sorted(float(v) for v in vals if v not in ("—", ""))
    if not nums: return "—"
    a, b = nums[0], nums[-1]
    f = lambda x: f"{x:,.0f}".replace(",", " ") if x >= 100 else f"{x:.1f}"
    return f(a) if f(a) == f(b) else f"{f(a)} à {f(b)}"
last = None
for (name, n), cfgs in rows.items():
    if name != last:
        print(f"\n## {name}\n\n| Question | Réglage | Lecture | Écriture |\n|---|---|---|---|")
        last = name
    for cfg, vals in cfgs.items():
        print(f"| {n} tokens | {cfg} | {span(v[0] for v in vals)} | {span(v[1] for v in vals)} |")
EOF
} > "$OUT"
echo "table: $OUT"
