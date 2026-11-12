#!/bin/bash
# Toutes les mesures de vitesse du moteur d'Albireo en une commande (fork pour macOS).
#
#   bash dlib/dlib/external/albireo/tools/bench_macos.sh [essais] [build folder]
#
# Pour chaque modèle présent, la lecture de prompts de 64, 512 et 1 536 tokens et l'écriture de 64 tokens
# (bench_runtime, clés et valeurs en 8 bits comme le service), sur le processeur puis sur le GPU, en
# alternance (processeur, GPU, processeur, GPU…), essais fois (2 par défaut), comme le veut la règle de
# mesure de CLAUDE.md.  Le résultat, un tableau Markdown avec la machine, va dans build/bench/<machine>-<date>.md,
# les sorties brutes à côté.  Écrit pour comparer le MacBook Air M2 et le Mac mini M5 Pro : les mêmes modèles,
# les mêmes tailles.
#
# Les modèles : ceux du portage quand ils sont là (build/models, build/albireo-home/models), et tout fichier
# .gguf de build/bench-models (par exemple le Qwen3.6-35B-A3B sur le Mac mini).  BENCH_SIZES change les tailles
# de prompt (« 64 512 1536 » : le GPU lit les prompts dont le contexte reste sous 2 048 positions, écriture
# comprise), BENCH_STEPS le nombre de tokens écrits (64).

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
ROUNDS=${1:-2}
BUILD=${2:-$ROOT/build/macos-albireo}
BENCH=$BUILD/tests/bench_runtime
SIZES=${BENCH_SIZES:-64 512 1536}
STEPS=${BENCH_STEPS:-64}
if [ ! -x "$BENCH" ]; then echo "no $BENCH: build Albireo first"; exit 1; fi

HOST=$(sysctl -n hw.model)
STAMP=$(date +%Y%m%d-%H%M)
OUTDIR=$ROOT/build/bench
mkdir -p "$OUTDIR"
OUT=$OUTDIR/$HOST-$STAMP.md
RAW=$OUTDIR/$HOST-$STAMP.raw
: > "$RAW"

# Les modèles, avec leur nom dans le tableau.
NAMES=() FILES=()
add_model() { [ -f "$2" ] && NAMES+=("$1") && FILES+=("$2"); }
add_model "Qwen3-VL 2B Instruct Q4_K_M" "$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf"
add_model "Qwen3-VL 4B Thinking Q4_K_M" "$ROOT/build/albireo-home/models/qwen3-vl-4b-thinking.gguf"
add_model "Qwen3.5-0.8B Q8_0" "$ROOT/build/models/Qwen3.5-0.8B-Q8_0.gguf"
add_model "Qwen3.5-4B Q4_K_M" "$ROOT/build/albireo-home/models/hf-unsloth-qwen3.5-4b-q4_k_m.gguf"
add_model "Qwen 3.6 11B-A3B Q4_K_M (MoE)" "$ROOT/build/models/Qwen3.6-11B-A3B-Niwaki-Q4_K_M.gguf"
for f in "$ROOT"/build/bench-models/*.gguf; do [ -f "$f" ] && add_model "$(basename "$f" .gguf)" "$f"; done
if [ ${#FILES[@]} -eq 0 ]; then echo "no model to measure"; exit 1; fi

THREADS=$(sysctl -n hw.physicalcpu)
LOAD_START=$(sysctl -n vm.loadavg)
{
  echo "# Mesures du moteur d'Albireo — $HOST, $(date '+%Y-%m-%d %H:%M')"
  echo
  echo "| Machine | |"
  echo "|---|---|"
  echo "| Modèle | $(sysctl -n hw.model) |"
  echo "| Puce | $(sysctl -n machdep.cpu.brand_string) |"
  echo "| Cœurs | $(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null) performance, $(sysctl -n hw.perflevel1.physicalcpu 2>/dev/null) efficacité ; $THREADS threads pour les mesures |"
  echo "| Mémoire | $(( $(sysctl -n hw.memsize) / 1073741824 )) Go |"
  echo "| macOS | $(sw_vers -productVersion) ($(sw_vers -buildVersion)) |"
  echo "| Albireo | $(sed -n 's/.*ALBIREO_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/albireo/common/version.h" | head -1) ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null)) |"
  echo "| Mode économie d'énergie | $(pmset -g | awk '/lowpowermode/ {print $2}') (0 attendu) |"
  echo "| Charge au début | $LOAD_START |"
  echo
  echo "Mesures \`bench_runtime\` ($THREADS threads, clés et valeurs en 8 bits), $ROUNDS essais en alternance processeur / GPU ; tokens par seconde."
  echo
} > "$OUT"
if [ "$(pmset -g | awk '/lowpowermode/ {print $2}')" = "1" ]; then
  echo "warning: the low power mode is on, the measures would be about ten times too slow" >&2
fi

# Une mesure : bench_runtime, puis ses deux vitesses (lecture, écriture).
measure() { # fichier taille mode(cpu|gpu)
  local extra=() out
  [ "$3" = gpu ] && extra=(metal)
  out=$("$BENCH" "$1" "$THREADS" "$2" "$STEPS" 1 q8 "${extra[@]}" 2>&1)
  printf '=== %s %s %s\n%s\n' "$(basename "$1")" "$2" "$3" "$out" >> "$RAW"
  local p g
  p=$(printf '%s\n' "$out" | sed -n 's/^prompt: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  g=$(printf '%s\n' "$out" | sed -n 's/^generation: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  # Un modèle que le GPU refuse (format de poids, architecture) ou dont il a échoué le contrôle : le dire.
  local on=""
  if [ "$3" = gpu ] && printf '%s\n' "$out" | grep -qE "stay on the processor|go back to the processor"; then on="*"; fi
  echo "${p:-?}$on ${g:-?}$on"
}

{
  echo "| Modèle | Prompt | Lecture, processeur | Lecture, GPU | Écriture, processeur | Écriture, GPU |"
  echo "|---|---|---|---|---|---|"
} >> "$OUT"
for i in "${!FILES[@]}"; do
  for size in $SIZES; do
    cp_r=() gp_r=() cg_r=() gg_r=()
    for r in $(seq 1 "$ROUNDS"); do
      echo "${NAMES[$i]}, prompt $size, essai $r" >&2
      read -r p g <<< "$(measure "${FILES[$i]}" "$size" cpu)"; cp_r+=("$p"); cg_r+=("$g")
      read -r p g <<< "$(measure "${FILES[$i]}" "$size" gpu)"; gp_r+=("$p"); gg_r+=("$g")
    done
    join() { local out="" s; for s in "$@"; do out+="${out:+ / }$s"; done; echo "$out"; }
    echo "| ${NAMES[$i]} | $size | $(join "${cp_r[@]}") | $(join "${gp_r[@]}") | $(join "${cg_r[@]}") | $(join "${gg_r[@]}") |" >> "$OUT"
  done
done
{
  echo
  echo "Un astérisque : le GPU a refusé le modèle ou échoué son contrôle, la valeur est celle du processeur."
  echo
  echo "Charge à la fin : $(sysctl -n vm.loadavg). Sorties brutes : \`$(basename "$RAW")\`."
} >> "$OUT"
echo "$OUT"
