#!/bin/bash
# Gulliver contre d'autres moteurs locaux, sur les mêmes modèles (fork pour macOS, 2026-11-21).
#
#   bash dlib/dlib/external/gulliver/tools/bench_moteurs.sh [essais]
#
# Pour chaque modèle présent et chaque longueur de question (BENCH_SIZES, « 512 4096 16384 » par
# défaut), la lecture de la question puis l'écriture de 64 tokens après elle, sur le GPU, avec :
#   - Gulliver (bench_runtime), clés et valeurs en 8 bits (le réglage du service) puis en demi-précision ;
#   - llama.cpp (llama-bench de Homebrew), réglages par défaut puis clés et valeurs en 8 bits ;
#   - MLX (mlx_lm.benchmark, environnement build/mlx-venv), pour le modèle qui a sa version MLX ;
# Les réglages se suivent en alternance (A, B, C, D, puis de nouveau A…), essais fois (2 par défaut),
# comme le veut la règle de mesure de CLAUDE.md.  Les tableaux mettent les moteurs côte à côte, chacun
# avec son écart à Gulliver en pourcentage (+ : plus rapide que Gulliver, − : plus lent).  Ils vont
# dans build/bench/moteurs-<date>.md,
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

# Les modèles : nom|fichier GGUF|dossier MLX (vide s'il n'y en a pas).  Les modèles installés dans Gulliver sont
# mesurés chaque semaine par bench_modeles.sh, Gulliver seul.
MODELS=()
add() { [ -f "$2" ] && [[ -z "$BENCH_ONLY" || "$1" == *"$BENCH_ONLY"* ]] && MODELS+=("$1|$2|$3"); }
add "Qwen3-4B Q4_K_M" "$M/Qwen3-4B-Q4_K_M.gguf" "$M/Qwen3-4B-mlx-4bit"
add "Qwen3-VL 4B Thinking Q4_K_M" "$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.gguf" "$M/Qwen3-VL-4B-Thinking-4bit"
add "Qwen3-VL 2B Instruct Q4_K_M" "$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf" "$M/Qwen3-VL-2B-Instruct-4bit"
add "Gemma 3 4B Q4_K_M" "$ROOT/build/models/gemma-3-4b-it-Q4_K_M.gguf" "$M/gemma-3-4b-it-4bit"
add "LFM2.5-1.2B Q8_0" "$ROOT/build/models/LFM2.5-1.2B-Instruct-Q8_0.gguf" "$M/LFM2.5-1.2B-Instruct-8bit"
add "Qwen3-30B-A3B Q4_K_M (MoE)" "$ROOT/build/models/Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" "$M/Qwen3-30B-A3B-Instruct-2507-4bit"
# Les modèles conseillés de la semaine (ajoutés le 2026-11-27, demande de l'humain : MLX vaut-il le changement ?).
add "Granite 4.2 8B Q4_K_M" "$ROOT/build/gulliver-home/models/granite-4.2-8b.gguf" "$M/granite-4.2-8b-MLX-4bit"
add "Gemma 4 26B-A4B Q4_K_M (MoE)" "$ROOT/build/gulliver-home/models/hf-unsloth-gemma-4-26b-a4b-it-q4_k_m.gguf" "$M/gemma-4-26b-a4b-it-4bit"
[ ${#MODELS[@]} -gt 0 ] || { echo "no model to measure"; exit 1; }

# Une mesure : écrit « modèle|taille|réglage|lecture|écriture » dans RAW.
gulliver() {  # $1 nom, $2 fichier, $3 taille, $4 q8|f16
  local out p g
  out=$(BENCH_GPU_CONTEXT=262144 "$BENCH" "$2" "$(sysctl -n hw.physicalcpu)" "$3" "$STEPS" 0 "$4" metal 2>&1)
  printf '== gulliver %s %s %s\n%s\n' "$1" "$3" "$4" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/^prompt: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  g=$(printf '%s\n' "$out" | sed -n 's/^generation: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  echo "$1|$3|gulliver_$4|${p:-—}|${g:-—}" >> "$RAW"
}
llama_ts() {  # lit avg_ts dans la sortie JSON de llama-bench
  "$ROOT/build/macos-gulliver/bin/json_get" - 0.avg_ts --round 1 --default —
}
llama() {  # $1 nom, $2 fichier, $3 taille, $4 clé du réglage, $5… options
  local name=$1 file=$2 n=$3 label=$4; shift 4
  local p g
  p=$("$LLAMA" -m "$file" -ngl 99 -p "$n" -n 0 -r 2 -o json "$@" 2>>"$LOG" | llama_ts)
  g=$("$LLAMA" -m "$file" -ngl 99 -p 0 -n "$STEPS" -d "$n" -r 2 -o json "$@" 2>>"$LOG" | llama_ts)
  echo "$name|$n|$label|$p|$g" >> "$RAW"
}
mlx() {  # $1 nom, $2 dossier, $3 taille
  local out p g
  out=$("$MLX" --model "$2" -p "$3" -g "$STEPS" -n 2 2>&1)
  printf '== mlx %s %s\n%s\n' "$1" "$3" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/.*prompt_tps=\([0-9.]*\).*/\1/p' | tail -1)
  g=$(printf '%s\n' "$out" | sed -n 's/.*generation_tps=\([0-9.]*\).*/\1/p' | tail -1)
  echo "$1|$3|mlx|${p:-—}|${g:-—}" >> "$RAW"
}

LOAD_START=$(sysctl -n vm.loadavg)
# La température de la puce (macos/temperature, construit s'il manque), au début et à la fin.
TEMP_TOOL=$ROOT/build/temperature
[ -x "$TEMP_TOOL" ] || clang -fobjc-arc -framework Foundation -framework IOKit -o "$TEMP_TOOL" "$ROOT/macos/temperature/temperature.m" 2>/dev/null
chip_temp() { [ -x "$TEMP_TOOL" ] && "$TEMP_TOOL" | sed -n 's/^puce : \([0-9.]*\) °C au plus.*/\1 °C/p' | sed 's/\./,/'; }
TEMP_START=$(chip_temp)
for ((r = 1; r <= ROUNDS; ++r)); do
  for entry in "${MODELS[@]}"; do
    IFS='|' read -r name file mlxdir <<< "$entry"
    for n in $SIZES; do
      echo "essai $r : $name, $n tokens"
      gulliver "$name" "$file" "$n" q8
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" llama
      gulliver "$name" "$file" "$n" f16
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" llama_q8 -fa 1 -ctk q8_0 -ctv q8_0
      [ -d "$mlxdir" ] && [ -x "$MLX" ] && mlx "$name" "$mlxdir" "$n"
    done
  done
done
LOAD_END=$(sysctl -n vm.loadavg)
TEMP_END=$(chip_temp)

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
  echo "| MLX | $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}') (mlx-lm $("$ROOT/build/mlx-venv/bin/pip" show mlx-lm 2>/dev/null | awk '/^Version/{print $2}'), $("$ROOT/build/mlx-venv/bin/python" --version 2>&1)) |"
  echo "| Charge | $LOAD_START au début, $LOAD_END à la fin |"
  [ -n "$TEMP_START$TEMP_END" ] && echo "| Puce, sonde la plus chaude | ${TEMP_START:-?} au début, ${TEMP_END:-?} à la fin |"
  echo
  echo "Jetons par seconde ; lecture de la question, puis écriture de $STEPS jetons après elle ; $ROUNDS essais en alternance (moyenne des essais). Gulliver, llama.cpp et MLX sur le GPU. Entre parenthèses : l'écart à Gulliver (+ : plus rapide que Gulliver, − : plus lent)."
  echo
  "$ROOT/build/macos-gulliver/bin/tableaux" vitesse "$RAW"
} > "$OUT"
echo "table: $OUT"
