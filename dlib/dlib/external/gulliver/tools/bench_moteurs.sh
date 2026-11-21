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
#   - l'Albireo d'origine (son bench_runtime, construit par macos/albireo-original/build.sh, tel quel :
#     processeur seul, noyaux portables), jusqu'à BENCH_ALBIREO_MAX tokens (4096 par défaut : au-delà,
#     une seule mesure lui prendrait plus d'un quart d'heure).
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
ALBIREO_DIR=${ALBIREO_ORIGINAL_DIR:-$(cd "$ROOT/.." && pwd)/albireo-original}
ALBIREO=$ALBIREO_DIR/build-mac/bench_runtime
ALBIREO_MAX=${BENCH_ALBIREO_MAX:-4096}
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
  echo "$1|$3|gulliver_$4|${p:-—}|${g:-—}" >> "$RAW"
}
llama_ts() {  # lit avg_ts dans la sortie JSON de llama-bench
  python3 -I -c 'import json,sys
try:
    print(round(json.load(sys.stdin)[0]["avg_ts"],1))
except Exception:
    print("—")'
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
albireo() {  # $1 nom, $2 fichier, $3 taille : l'original, processeur seul, clés en 8 bits
  local out p g
  out=$("$ALBIREO" "$2" "$(sysctl -n hw.physicalcpu)" "$3" "$STEPS" 1 q8 2>&1)
  printf '== albireo %s %s\n%s\n' "$1" "$3" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/^prompt: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  g=$(printf '%s\n' "$out" | sed -n 's/^generation: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  # Une architecture que l'original ne lit pas (LFM2, Gemma 3…) : la case le dira.
  printf '%s\n' "$out" | grep -q "is not supported by this engine" && p=nonlu && g=nonlu
  echo "$1|$3|albireo|${p:-—}|${g:-—}" >> "$RAW"
}

LOAD_START=$(sysctl -n vm.loadavg)
for ((r = 1; r <= ROUNDS; ++r)); do
  for entry in "${MODELS[@]}"; do
    IFS='|' read -r name file mlxdir <<< "$entry"
    for n in $SIZES; do
      echo "essai $r : $name, $n tokens"
      gulliver "$name" "$file" "$n" q8
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" llama
      gulliver "$name" "$file" "$n" f16
      [ -n "$LLAMA" ] && llama "$name" "$file" "$n" llama_q8 -fa 1 -ctk q8_0 -ctv q8_0
      [ -n "$mlxdir" ] && [ -x "$MLX" ] && mlx "$name" "$mlxdir" "$n"
      [ -x "$ALBIREO" ] && [ "$n" -le "$ALBIREO_MAX" ] && albireo "$name" "$file" "$n"
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
  echo "| MLX | $("$ROOT/build/mlx-venv/bin/pip" show mlx 2>/dev/null | awk '/^Version/{print $2}') (mlx-lm $("$ROOT/build/mlx-venv/bin/pip" show mlx-lm 2>/dev/null | awk '/^Version/{print $2}'), $("$ROOT/build/mlx-venv/bin/python" --version 2>&1)) |"
  [ -x "$ALBIREO" ] && echo "| Albireo d'origine | $(awk '/#define ALBIREO_VERSION_(MAJOR|MINOR|PATCH) /{print $3}' "$ALBIREO_DIR/dlib/dlib/external/albireo/common/version.h" | paste -sd. -) ($(git -C "$ALBIREO_DIR" rev-parse --short HEAD)), tel quel : processeur seul, noyaux portables, jusqu'à $ALBIREO_MAX tokens |"
  echo "| Charge | $LOAD_START au début, $LOAD_END à la fin |"
  echo
  echo "Jetons par seconde ; lecture de la question, puis écriture de $STEPS jetons après elle ; $ROUNDS essais en alternance (moyenne des essais). Gulliver, llama.cpp et MLX sur le GPU, l'Albireo d'origine sur le processeur. Entre parenthèses : l'écart à Gulliver (+ : plus rapide que Gulliver, − : plus lent)."
  echo
  python3 -I - "$RAW" <<'EOF'
import sys, collections
data = collections.OrderedDict()
for line in open(sys.argv[1], encoding="utf-8"):
    name, n, key, p, g = line.rstrip("\n").split("|")
    data.setdefault(name, collections.OrderedDict()).setdefault(int(n), {}).setdefault(key, []).append((p, g))
def mean(vals):
    vals = list(vals)
    if vals and all(v == "nonlu" for v in vals): return "nonlu"
    nums = [float(v) for v in vals if v not in ("—", "", "nonlu")]
    return sum(nums) / len(nums) if nums else None
def num(x):
    return "—" if x is None or x == "nonlu" else (f"{x:,.0f}".replace(",", " ") if x >= 100 else f"{x:.1f}".replace(".", ","))
def cell(x, base):
    if x == "nonlu": return "ne lit pas ce modèle"
    if x is None: return "—"
    if base is None or base == "nonlu" or base == 0: return num(x)
    d = round(100.0 * (x / base - 1.0))
    return f"{num(x)} ({'±' if d == 0 else '+' if d > 0 else '−'}{abs(d)} %)"
MAIN = [("gulliver_q8", "Gulliver"), ("llama", "llama.cpp"), ("mlx", "MLX"), ("albireo", "Albireo d'origine")]
OTHER = [("gulliver_f16", "Gulliver, clés en demi-précision"), ("llama_q8", "llama.cpp, clés en 8 bits")]
for what, idx in (("Lecture de la question", 0), ("Écriture de la réponse", 1)):
    cols = [(k, l) for k, l in MAIN if any(k in s for m in data.values() for s in m.values())]
    print(f"\n## {what}\n")
    print("| Modèle | Question | " + " | ".join(l for _, l in cols) + " |")
    print("|---|---|" + "---|" * len(cols))
    for name, sizes in data.items():
        for n, cfg in sizes.items():
            base = mean(v[idx] for v in cfg.get("gulliver_q8", []))
            row = [num(base)] + [cell(mean(v[idx] for v in cfg[k]), base) if k in cfg
                                 else ("non mesuré" if k == "albireo" else "—") for k, _ in cols[1:]]
            print(f"| {name} | {n} | " + " | ".join(row) + " |")
print("\n## Les autres réglages\n")
print("Écart au réglage du service de Gulliver (clés et valeurs en 8 bits), lecture puis écriture.\n")
print("| Modèle | Question | " + " | ".join(l for _, l in OTHER) + " |")
print("|---|---|" + "---|" * len(OTHER))
for name, sizes in data.items():
    for n, cfg in sizes.items():
        out = []
        for k, _ in OTHER:
            if k not in cfg: out.append("—"); continue
            parts = []
            for idx in (0, 1):
                parts.append(cell(mean(v[idx] for v in cfg[k]), mean(v[idx] for v in cfg.get("gulliver_q8", []))))
            out.append(" / ".join(parts))
        print(f"| {name} | {n} | " + " | ".join(out) + " |")
EOF
} > "$OUT"
echo "table: $OUT"
