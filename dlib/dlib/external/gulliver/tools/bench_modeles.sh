#!/bin/bash
# Les modèles de Gulliver, mesurés chaque semaine (fork pour macOS, 2026-11-22, demande de l'humain : « le but est
# d'avoir le plus rapide toutes les semaines », comparé au choix de la semaine dernière).
#
#   bash dlib/dlib/external/gulliver/tools/bench_modeles.sh [essais]
#
# Pour chaque modèle installé dans Gulliver (tools/modeles_installes.py) : la lecture d'une question de 512 et de 4 096
# tokens (BENCH_SIZES) puis l'écriture de 64 tokens après elle (bench_runtime, GPU), mémoire de conversation en 8 bits
# puis en demi-précision, essais fois en alternance (2 par défaut) ; puis la perplexité sur le GPU (perplexity_runtime,
# 8 morceaux de 512 du texte de justesse_moteurs.sh).  llama.cpp (llama-bench, llama-perplexity, réglages par défaut)
# et MLX (s'il a la version MLX du modèle) sont mesurés de la même façon sur le modèle de référence, comme deux lignes de
# plus du tableau (décision de l'humain : « un modèle à comparer comme un autre »).  Passent d'abord le choix de la
# semaine dernière (conseil.json, sinon le modèle actif), puis les nouveaux de la nuit (nouveaux.json, tools/modeles_semaine.py), puis les autres ;
# la mesure va toujours jusqu'au bout, sans heure limite (décision de l'humain, 2026-11-24).  La température de la puce,
# l'utilisation du processeur et celle du GPU sont relevées toutes les 20 secondes pendant la mesure (macos/temperature :
# minimum, moyenne et maximum de chacune).  Écrit les chiffres bruts dans
# build/bench/modeles-<date>.raw, puis la page de la semaine et le conseil (tools/conseil_modeles.py), et met à jour la
# section de la semaine de docs/mesures/tests-de-vitesse.md : « table: <fichier .md> » en dernière ligne.  Enfin, sur le
# modèle de référence, les mesures « comme un utilisateur » par le vrai service (tools/bench_service.py), et la justesse
# de la recherche dans les documents (tools/test_rag.py).  Après la perplexité, l'épreuve de 40 questions en français
# (tools/epreuve_modeles.py, une note sur 100, lignes « quiz|id|note ») sur chaque modèle ; le conseil s'appuie sur elle et
# désigne le dernier du classement, que bench_hebdo.sh retire ensuite (tools/retrait_modeles.py).
#
# Lignes brutes : id|nom|images|taille|mémoire|lecture|écriture|calcul (gpu, processeur, nonlu) ; ppl|id|perplexité.
# L'identifiant de llama.cpp et de MLX est « llama.cpp:<id> » et « mlx:<id> » (pas un modèle de Gulliver).

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TOOLS=$ROOT/dlib/dlib/external/gulliver/tools
ROUNDS=${1:-2}
SIZES=${BENCH_SIZES:-512 4096}
STEPS=64
BENCH=$ROOT/build/macos-gulliver/tests/bench_runtime
PPL=$ROOT/build/macos-gulliver/tests/perplexity_runtime
HEBDO=$ROOT/build/bench/hebdo
LLAMA=$(command -v llama-bench)
LLAMA_PPL=$(command -v llama-perplexity)
MLX=$ROOT/build/mlx-venv/bin/mlx_lm.benchmark
MLXPY=$ROOT/build/mlx-venv/bin/python
MLXDIR=$ROOT/build/bench-moteurs/models
TEXT=$ROOT/build/bench-moteurs/texte.txt
[ -x "$BENCH" ] && [ -x "$PPL" ] || { echo "no $BENCH or $PPL: build Gulliver first"; exit 1; }
mkdir -p "$(dirname "$TEXT")" "$HEBDO"
[ -f "$TEXT" ] || cat "$ROOT"/blog/5[0-9]-*.md > "$TEXT"

STAMP=$(date +%Y%m%d-%H%M)
OUTDIR=$ROOT/build/bench
RAW=$OUTDIR/modeles-$STAMP.raw
LOG=$OUTDIR/modeles-$STAMP.log
OUT=$OUTDIR/modeles-$STAMP.md
: > "$RAW"; : > "$LOG"

# Le choix de la semaine dernière et les nouveaux de la nuit, pour l'ordre de passage.
PREVIOUS=$(python3 -I -c 'import json,sys
try: print(json.load(open(sys.argv[1]))["advice"][0]["id"] or "")
except Exception: print("")' "$HEBDO/conseil.json")
NEW=$(python3 -I -c 'import json,sys
try: print(" ".join(json.load(open(sys.argv[1])).get("ids", [])))
except Exception: print("")' "$HEBDO/nouveaux.json")

LIST=$(python3 -I "$TOOLS/modeles_installes.py" "${GULLIVER_HOME:-$ROOT/build/gulliver-home}")
[ -n "$LIST" ] || { echo "the Gulliver service gives no model: is it running?"; exit 1; }
rank() {  # 0 : le choix de la semaine dernière ; 1 : un nouveau ; 2 : les autres
  local id=$1
  [ "$id" = "$PREVIOUS" ] && { echo 0; return; }
  for n in $NEW; do [ "$n" = "$id" ] && { echo 1; return; }; done
  echo 2
}
MODELS=()
REF_ID="" REF_NAME="" REF_FILE="" ACTIVE_ID="" ACTIVE_ALL=""
while IFS='|' read -r name file id active image; do
  [ -n "$id" ] || continue
  MODELS+=("$(rank "$id")|$name|$file|$id|$image")
  [ "$active" = 1 ] && [ -z "$ACTIVE_ID" ] && ACTIVE_ID=$id
  [ "$active" = 1 ] && ACTIVE_ALL="$ACTIVE_ALL $id"
done <<< "$LIST"
# Le modèle de référence : le choix de la semaine dernière, sinon le modèle actif ; llama.cpp et MLX le mesurent.
REF_ID=${PREVIOUS:-$ACTIVE_ID}
while IFS='|' read -r name file id _; do
  [ "$id" = "$REF_ID" ] && REF_NAME=$name && REF_FILE=$file
done <<< "$LIST"
# La version MLX du modèle de référence, quand build/bench-moteurs/models l'a.
case "$REF_ID" in
  qwen3-vl-4b-thinking) REF_MLX=$MLXDIR/Qwen3-VL-4B-Thinking-4bit ;;
  qwen3-vl-2b-instruct) REF_MLX=$MLXDIR/Qwen3-VL-2B-Instruct-4bit ;;
  *) REF_MLX="" ;;
esac
IFS=$'\n' MODELS=($(printf '%s\n' "${MODELS[@]}" | sort -t'|' -k1,1n)); unset IFS

measure() {  # $1 id, $2 nom, $3 fichier, $4 images, $5 taille, $6 q8|f16
  local out p g where=gpu
  out=$(BENCH_GPU_CONTEXT=262144 "$BENCH" "$3" "$(sysctl -n hw.physicalcpu)" "$5" "$STEPS" 0 "$6" metal 2>&1)
  printf '== %s %s %s\n%s\n' "$1" "$5" "$6" "$out" >> "$LOG"
  p=$(printf '%s\n' "$out" | sed -n 's/^prompt: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  g=$(printf '%s\n' "$out" | sed -n 's/^generation: .* \([0-9.]*\) tokens\/s$/\1/p' | head -1)
  printf '%s\n' "$out" | grep -qE "stay on the processor|go back to the processor|no GPU" && where=processeur
  printf '%s\n' "$out" | grep -qE "is not supported by this engine|unsupported ggml type" && where=nonlu
  echo "$1|$2|$4|$5|$6|${p:-—}|${g:-—}|$where" >> "$RAW"
}

LOAD_START=$(sysctl -n vm.loadavg)
TEMP_TOOL=$ROOT/build/temperature
[ -x "$TEMP_TOOL" ] || clang -fobjc-arc -framework Foundation -framework IOKit -o "$TEMP_TOOL" "$ROOT/macos/temperature/temperature.m" 2>/dev/null
chip_temp() { [ -x "$TEMP_TOOL" ] && "$TEMP_TOOL" | sed -n 's/^puce : \([0-9.]*\) °C au plus.*/\1 °C/p' | sed 's/\./,/'; }
TEMP_START=$(chip_temp)
# La température, le processeur et le GPU toutes les 20 secondes pendant la mesure : « température processeur GPU ».
TEMPS=$OUTDIR/modeles-$STAMP.temp
: > "$TEMPS"
( while :; do
    [ -x "$TEMP_TOOL" ] && "$TEMP_TOOL" | awk '/^puce :/ {t = $3} /^charge :/ {c = $4; g = $7} END {if (t != "") print t, c, g}' >> "$TEMPS"
    sleep 20
  done ) &
SAMPLER=$!
engines() {  # llama.cpp et MLX sur le modèle de référence, pour une taille
  local n=$1 p g out
  if [ -n "$LLAMA" ] && [ -f "$REF_FILE" ]; then
    p=$("$LLAMA" -m "$REF_FILE" -ngl 99 -p "$n" -n 0 -r 2 -o json 2>>"$LOG" | python3 -I -c 'import json,sys
try: print(round(json.load(sys.stdin)[0]["avg_ts"],1))
except Exception: print("—")')
    g=$("$LLAMA" -m "$REF_FILE" -ngl 99 -p 0 -n "$STEPS" -d "$n" -r 2 -o json 2>>"$LOG" | python3 -I -c 'import json,sys
try: print(round(json.load(sys.stdin)[0]["avg_ts"],1))
except Exception: print("—")')
    echo "llama.cpp:$REF_ID|llama.cpp · $REF_NAME|0|$n|q8|$p|$g|gpu" >> "$RAW"
  fi
  if [ -n "$REF_MLX" ] && [ -d "$REF_MLX" ] && [ -x "$MLX" ]; then
    out=$("$MLX" --model "$REF_MLX" -p "$n" -g "$STEPS" -n 2 2>&1)
    printf '== mlx %s\n%s\n' "$n" "$out" >> "$LOG"
    p=$(printf '%s\n' "$out" | sed -n 's/.*prompt_tps=\([0-9.]*\).*/\1/p' | tail -1)
    g=$(printf '%s\n' "$out" | sed -n 's/.*generation_tps=\([0-9.]*\).*/\1/p' | tail -1)
    echo "mlx:$REF_ID|MLX · $REF_NAME (son fichier 4 bits)|0|$n|q8|${p:-—}|${g:-—}|gpu" >> "$RAW"
  fi
}
for ((r = 1; r <= ROUNDS; ++r)); do
  for entry in "${MODELS[@]}"; do
    IFS='|' read -r _ name file id image <<< "$entry"
    for n in $SIZES; do
      echo "essai $r : $name, $n tokens"
      measure "$id" "$name" "$file" "$image" "$n" q8
      measure "$id" "$name" "$file" "$image" "$n" f16
    done
  done
  if [ -n "$REF_ID" ]; then
    for n in $SIZES; do echo "essai $r : llama.cpp et MLX, $n tokens"; engines "$n"; done
  fi
done
for entry in "${MODELS[@]}"; do
  IFS='|' read -r _ name file id image <<< "$entry"
  echo "justesse : $name"
  out=$("$PPL" "$file" "$TEXT" 512 8 q8 metal 2>&1)
  printf '== perplexité %s\n%s\n' "$id" "$out" >> "$LOG"
  echo "ppl|$id|$(printf '%s\n' "$out" | sed -n 's/^perplexity: \([0-9.]*\) .*/\1/p' | head -1)" >> "$RAW"
done
# L'épreuve : 40 questions en français, une note sur 100 par modèle (de quelques secondes à quelques minutes chacun).
for entry in "${MODELS[@]}"; do
  IFS='|' read -r _ name file id image <<< "$entry"
  grep -q "^$id|.*|nonlu$" "$RAW" && continue
  echo "épreuve : $name"
  out=$(python3 -I "$TOOLS/epreuve_modeles.py" "$file" --json "$OUTDIR/epreuve-$STAMP-$id.json" 2>&1)
  printf '== épreuve %s\n%s\n' "$id" "$out" >> "$LOG"
  note=$(printf '%s\n' "$out" | sed -n 's/^epreuve: \([0-9]*\) .*/\1/p' | tail -1)
  [ -n "$note" ] && echo "quiz|$id|$note" >> "$RAW"
done
if [ -n "$REF_ID" ]; then
  if [ -n "$LLAMA_PPL" ] && [ -f "$REF_FILE" ]; then
    p=$("$LLAMA_PPL" -m "$REF_FILE" -f "$TEXT" -c 512 --chunks 8 -ngl 99 2>&1 | tee -a "$LOG" \
      | sed -n 's/.*Final estimate: PPL = \([0-9.]*\) .*/\1/p')
    echo "ppl|llama.cpp:$REF_ID|$p" >> "$RAW"
  fi
  if [ -n "$REF_MLX" ] && [ -d "$REF_MLX" ] && [ -x "$MLXPY" ]; then
    p=$("$MLXPY" -I "$TOOLS/perplexity_mlx.py" "$REF_MLX" "$TEXT" 512 8 2>&1 | tee -a "$LOG" \
      | sed -n 's/^perplexity: \([0-9.]*\) .*/\1/p')
    echo "ppl|mlx:$REF_ID|$p" >> "$RAW"
  fi
fi
# Les mesures « comme un utilisateur » (tools/bench_service.py), par le service, sur le modèle de référence : un service
# d'essai isolé et llama-server, jamais le service de l'utilisateur.  La température et la charge restent relevées.
SERVICE_JSON=""
if [ -f "$REF_FILE" ]; then
  echo "comme un utilisateur : $REF_NAME"
  if python3 -I "$TOOLS/bench_service.py" "$REF_FILE" --name "$REF_NAME" --llama --json "$OUTDIR/service-$STAMP.json" >> "$LOG" 2>&1; then
    SERVICE_JSON=$OUTDIR/service-$STAMP.json
  fi
fi
# La justesse de la recherche dans les documents (tools/test_rag.py) : 40 questions figées sur les épisodes du blog, par un
# service d'essai isolé (quelques minutes).
RAG_JSON=""
echo "recherche dans les documents"
if python3 -I "$TOOLS/test_rag.py" --json "$OUTDIR/rag-$STAMP.json" >> "$LOG" 2>&1; then
  RAG_JSON=$OUTDIR/rag-$STAMP.json
fi
kill "$SAMPLER" 2>/dev/null
LOAD_END=$(sysctl -n vm.loadavg)
TEMP_END=$(chip_temp)
# Minimum, moyenne et maximum de chaque grandeur : « t_min|t_moy|t_max|c_min|c_moy|c_max|g_min|g_moy|g_max|relevés ».
TEMP_MEAN=$(awk 'NF >= 3 {for (i = 1; i <= 3; ++i) {v = $i + 0; s[i] += v; if (n == 0 || v < lo[i]) lo[i] = v; if (n == 0 || v > hi[i]) hi[i] = v}; n++}
  END {if (n) printf "%.1f|%.1f|%.1f|%.0f|%.0f|%.0f|%.0f|%.0f|%.0f|%d", lo[1], s[1] / n, hi[1], lo[2], s[2] / n, hi[2], lo[3], s[3] / n, hi[3], n}' "$TEMPS")

python3 -I "$TOOLS/conseil_modeles.py" "$RAW" --previous "$PREVIOUS" --new "$NEW" --rounds "$ROUNDS" \
  --machine "$(sysctl -n hw.model)|$(sysctl -n machdep.cpu.brand_string), $(( $(sysctl -n hw.memsize) / 1073741824 )) Go|$(sw_vers -productVersion) ($(sw_vers -buildVersion))|$(sed -n 's/.*GULLIVER_VERSION "\(.*\)".*/\1/p' "$ROOT/dlib/dlib/external/gulliver/common/version.h" | head -1) ($(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null))|$LOAD_START au début, $LOAD_END à la fin|${TEMP_START:-?} au début, ${TEMP_END:-?} à la fin" \
  --temperature "$TEMP_MEAN" --doc "$ROOT/docs/mesures/tests-de-vitesse.md" ${SERVICE_JSON:+--service "$SERVICE_JSON"} ${RAG_JSON:+--rag "$RAG_JSON"} \
  --keep "$ACTIVE_ALL" --removed "$HEBDO/retires.json" --json "$HEBDO/conseil.json" > "$OUT"
echo "table: $OUT"
