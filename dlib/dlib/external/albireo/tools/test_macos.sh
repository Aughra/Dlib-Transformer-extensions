#!/bin/bash
# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Teste un build macOS d'Albireo (Apple Silicon) et affiche un résumé, une ligne par étape.
#
#   bash dlib/dlib/external/albireo/tools/test_macos.sh [build folder] [model.gguf] [embedding model.gguf]
#
# Par défaut : build/macos-albireo à la racine du dépôt, build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf
# et build/albireo-home/models/qwen3-embedding-0.6b.gguf.  Les étapes dont le modèle manque sont
# sautées.  Le service tourne hors ligne dans un dossier de données à lui, sur le port 18443
# (ALBIREO_TEST_PORT, et le port suivant pour le serveur local de test_host), reçoit le modèle comme
# l'application web l'envoie, le refuse par son chemin, répond à une question par l'interface
# OpenAI et s'arrête.  La première étape vérifie les outils de ces tests.  Le code de sortie est
# le nombre d'étapes échouées.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
BUILD=${1:-$ROOT/build/macos-albireo}
MODEL=${2:-$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf}
EMBEDDING=${3:-$ROOT/build/albireo-home/models/qwen3-embedding-0.6b.gguf}
PORT=${ALBIREO_TEST_PORT:-18443}
QUESTION="What is the capital of France? Answer in one sentence."
WORK=$(mktemp -d)
RESULTS=()
FAILED=0

# Nanosecondes : la commande date de macOS n'a pas de %N.
now() { perl -MTime::HiRes=time -e 'printf "%.0f\n", time * 1e9'; }
elapsed() { awk -v a="$1" -v b="$(now)" 'BEGIN { printf "%.1f", (b - a) / 1e9 }'; }

record() { # étape résultat détail secondes
  RESULTS+=("$(printf '%-28s %-5s %6s s  %s' "$1" "$2" "$4" "$3")")
  [ "$2" = FAIL ] && FAILED=$((FAILED + 1))
}

# Lance une commande, puis vérifie que sa sortie contient un motif.
step() { # nom motif commande...
  local name=$1 pattern=$2
  shift 2
  local start out rc seconds line
  start=$(now)
  out=$("$@" 2>&1)
  rc=$?
  seconds=$(elapsed "$start")
  line=$(printf '%s\n' "$out" | grep -E -o -m1 ".{0,40}$pattern.{0,30}" | head -1)
  if [ $rc -eq 0 ] && [ -n "$line" ]; then
    record "$name" PASS "$line" "$seconds"
  else
    printf '%s\n' "$out" | tail -20 > "$WORK/$name.log"
    record "$name" FAIL "exit $rc, output in $WORK/$name.log" "$seconds"
  fi
}

skip() { record "$1" SKIP "$2" 0.0; }

# Les outils de ces tests.
START=$(now)
missing=""
for tool in curl perl awk; do command -v $tool > /dev/null 2>&1 || missing="$missing $tool"; done
if [ -z "$missing" ]; then
  record "prerequisites" PASS "curl, perl and awk are there" "$(elapsed "$START")"
else
  record "prerequisites" FAIL "missing:$missing" "$(elapsed "$START")"
fi

ENGINES=$BUILD/bin/engines
CHAT=$BUILD/tests/engine_chat

# Les noyaux quantifiés (NEON, Accelerate).
if [ -x "$BUILD/tests/test_qkernels" ]; then
  step "quantized kernels" "all checks passed" "$BUILD/tests/test_qkernels"
else skip "quantized kernels" "no $BUILD/tests/test_qkernels (ALBIREO_TESTS=ON)"
fi

# L'attention des étapes répartie entre les threads, comparée à une tâche par tête de clés et valeurs,
# avec les clés et valeurs en demi-précision et sur 8 bits, sur un tout petit modèle écrit dans le
# dossier de travail.
if [ -x "$BUILD/tests/test_runtime" ]; then
  step "attention" "all checks passed" "$BUILD/tests/test_runtime" synthetic "$WORK"
else skip "attention" "no $BUILD/tests/test_runtime"
fi

# Fichiers de modèle abîmés, le format de chat des agents et l'interface C d'un moteur sous des
# requêtes concurrentes, sur de tout petits modèles que les tests écrivent dans le dossier de travail.
if [ -x "$BUILD/tests/test_model_file" ]; then
  step "damaged model files" "all checks passed" "$BUILD/tests/test_model_file" "$WORK"
else skip "damaged model files" "no $BUILD/tests/test_model_file"
fi
if [ -x "$BUILD/tests/test_chat_format" ]; then step "chat format" "all checks passed" "$BUILD/tests/test_chat_format"
else skip "chat format" "no $BUILD/tests/test_chat_format"
fi
ENGINE=$ENGINES/libalbireo-engine-cpu.dylib
if [ -x "$BUILD/tests/test_engine_api" ] && [ -f "$ENGINE" ]; then
  step "engine interface" "all checks passed" "$BUILD/tests/test_engine_api" "$ENGINE" "$WORK"
else skip "engine interface" "no $BUILD/tests/test_engine_api or no $ENGINE"
fi
# Les fichiers que le service écrit dans son dossier de modèles et lit dans ses imports.
if [ -x "$BUILD/tests/test_host" ]; then
  step "models folder and imports" "all checks passed" "$BUILD/tests/test_host" "$WORK" $((PORT + 1))
else skip "models folder and imports" "no $BUILD/tests/test_host"
fi

if [ ! -f "$ENGINE" ]; then skip "engine cpu" "no $ENGINE"
elif [ ! -f "$MODEL" ]; then skip "engine cpu" "no $MODEL"
else step "engine cpu" "Paris" "$CHAT" "$ENGINE" "$MODEL" --reasoning off --max 40 "$QUESTION"
fi

# Qwen 3.5 (couches linéaires Gated DeltaNet) : une réponse, puis trois tours qui reprennent l'état récurrent de la
# conversation, avec les mêmes réponses que sans reprise.
QWEN35=${QWEN35_MODEL:-$ROOT/build/models/Qwen3.5-0.8B-Q8_0.gguf}
qwen35_turns() {
  local turns=("My name is Léa. Remember it." "What is my name?" "Spell my name backwards.") with without
  with=$("$CHAT" "$ENGINE" "$QWEN35" --reasoning off --temperature 0 --max 40 --pause 2 --turns "${turns[@]}" 2>/dev/null) \
    || return 1
  without=$("$CHAT" "$ENGINE" "$QWEN35" --reasoning off --temperature 0 --max 40 --no-warm --turns "${turns[@]}" \
    2>/dev/null) || return 1
  printf '%s' "$with" | grep -q '"cached_tokens":[1-9]' || { echo "no state was resumed"; return 1; }
  [ "$(printf '%s' "$with" | grep -v '^[-{]')" = "$(printf '%s' "$without" | grep -v '^[-{]')" ] \
    || { echo "the answers differ with and without resumed states"; return 1; }
  echo "same answers with resumed states"
}
# Un point de reprise : deux questions qui partagent un long prompt système ; la seconde reprend l'état au dernier
# multiple de 256 positions.  Sur le processeur (« cpu »), sa réponse est celle de la question posée seule, au
# caractère près.  Avec le GPU, la question reprise ne relit que 15 tokens, sur le processeur (moins de 24), alors
# que la question seule lit son prompt sur le GPU : deux calculs qui ne s'arrondissent pas pareil, et un modèle qui
# hésite peut alors bifurquer ; l'étape vérifie la reprise et le sujet de la réponse.
qwen35_checkpoint() {
  local model=$1 mode=$2 sys both alone extra=()
  [ "$mode" = cpu ] && extra=(--no-metal)
  sys=$(for i in $(seq 1 24); do printf 'Rule %s: always answer politely, mention the opening hours (7am to 7pm) only when asked, and keep answers under three sentences. ' "$i"; done)
  both=$("$CHAT" "$ENGINE" "$model" --reasoning off --temperature 0 --max 30 --no-warm ${extra[@]+"${extra[@]}"} --system "$sys" \
    "What are your opening hours?" "Do you sell croissants?" 2>/dev/null) || return 1
  printf '%s' "$both" | grep -q '"cached_tokens":[1-9][0-9][0-9]' || { echo "no checkpoint was resumed"; return 1; }
  if [ "$mode" != cpu ]; then
    printf '%s\n' "$both" | sed -n '/^=== Do you sell/,/^---/p' | grep -qi croissant \
      || { echo "the resumed answer is not about croissants"; return 1; }
    echo "resumed at a checkpoint, answer on its subject"
    return 0
  fi
  alone=$("$CHAT" "$ENGINE" "$model" --reasoning off --temperature 0 --max 30 --no-warm ${extra[@]+"${extra[@]}"} --system "$sys" \
    "Do you sell croissants?" 2>/dev/null) || return 1
  [ "$(printf '%s\n' "$both" | sed -n '/^=== Do you sell/,/^---/p' | grep -v '^---')" = \
    "$(printf '%s\n' "$alone" | sed -n '/^=== /,/^---/p' | grep -v '^---')" ] \
    || { echo "the answer differs when resumed at a checkpoint"; return 1; }
  echo "same answer resumed at a checkpoint"
}
if [ ! -f "$ENGINE" ] || [ ! -f "$QWEN35" ]; then skip "qwen 3.5" "no $QWEN35"
else
  step "qwen 3.5" "Paris" "$CHAT" "$ENGINE" "$QWEN35" --reasoning off --temperature 0 --max 40 "$QUESTION"
  step "qwen 3.5 resumed state" "same answers with resumed states" qwen35_turns
  step "qwen 3.5 checkpoint" "same answer resumed at a checkpoint" qwen35_checkpoint "$QWEN35" cpu
fi
# Qwen 3.6 MoE (qwen35moe : couches linéaires, 256 experts et un expert partagé), quand le petit modèle de test est là.
QWEN36=${QWEN36_MODEL:-$ROOT/build/models/Qwen3.6-11B-A3B-Niwaki-Q4_K_M.gguf}
if [ ! -f "$ENGINE" ] || [ ! -f "$QWEN36" ]; then skip "qwen 3.6 moe" "no $QWEN36"
else
  step "qwen 3.6 moe" "Paris" "$CHAT" "$ENGINE" "$QWEN36" --reasoning off --temperature 0 --max 40 --no-metal "$QUESTION"
  # Depuis Albireo 4.12.0, ses prompts aussi (produits groupés des experts), coupés aux points de reprise.
  step "qwen 3.6 moe on the GPU" "the generation steps and the prompts" "$CHAT" "$ENGINE" "$QWEN36" --reasoning off \
    --temperature 0 --max 20 "$QUESTION"
  step "qwen 3.6 moe checkpoint" "resumed at a checkpoint" qwen35_checkpoint "$QWEN36" gpu
fi

# Qwen 3.5 sur le GPU : ses étapes de génération, quand un modèle en Q4_K / Q5_K / Q6_K est là (le 4B qu'installe
# le service, par défaut).
QWEN35_GPU=${QWEN35_GPU_MODEL:-$ROOT/build/albireo-home/models/hf-unsloth-qwen3.5-4b-q4_k_m.gguf}
if [ ! -f "$ENGINE" ] || [ ! -f "$QWEN35_GPU" ]; then skip "qwen 3.5 on the GPU" "no $QWEN35_GPU"
else
  step "qwen 3.5 on the GPU" "the generation steps and the prompts" "$CHAT" "$ENGINE" "$QWEN35_GPU" --reasoning off \
    --temperature 0 --max 20 "$QUESTION"
  step "qwen 3.5 checkpoint on the GPU" "resumed at a checkpoint, answer on its subject" qwen35_checkpoint "$QWEN35_GPU" gpu
  # Ses images (Albireo 4.13.0), quand son projecteur est là : le perroquet des images de compte de macOS.
  QWEN35_PROJECTOR=${QWEN35_PROJECTOR:-$ROOT/build/models/Qwen3.5-4B-mmproj-F16.gguf}
  PARROT="/Library/User Pictures/Animals/Parrot.heic"
  if [ -f "$QWEN35_PROJECTOR" ] && [ -f "$PARROT" ] && sips -s format jpeg -Z 512 "$PARROT" --out "$WORK/parrot.jpg" > /dev/null 2>&1
  then
    step "qwen 3.5 image" "macaw" "$CHAT" "$ENGINE" "$QWEN35_GPU" --projector "$QWEN35_PROJECTOR" --image "$WORK/parrot.jpg" \
      --reasoning off --temperature 0 --max 60 "Describe this image in two sentences."
  else skip "qwen 3.5 image" "no $QWEN35_PROJECTOR"
  fi
  # Ses appels d'outils, sous forme XML (<function=…>, <parameter=…>), lus par le moteur (Albireo 4.12.1).
  step "qwen 3.5 tool call" '"name":"get_weather","arguments":".*Paris' "$CHAT" "$ENGINE" "$QWEN35_GPU" \
    --tools --reasoning off --temperature 0 --max 120 "What is the weather in Paris right now?"
fi

if [ -f "$EMBEDDING" ] && [ -f "$ENGINE" ]; then
  step "embedding model" "cosine" "$CHAT" "$ENGINE" "$EMBEDDING" --parallel \
    "The service runs on this computer." "Where does the service run?" "The quarterly revenue grew."
else
  skip "embedding model" "no $EMBEDDING"
fi

# Une entrée de catalogue et une petite copie de réserve suffisent à tester la route sans réseau.
offline_reserve_test() {
  local folder="$HOME_DIR/import/offline-reserve-test" sample="$WORK/reserve-fixture.gguf"
  local sha size payload reply code restored=0 removed=0 missing=0 path
  mkdir -p "$folder" "$HOME_DIR/reserve" || return 1
  chmod 700 "$folder" "$HOME_DIR/reserve" || return 1
  printf 'albireo offline reserve test\n' > "$sample" || return 1
  sha=$(perl -MDigest::SHA=sha256_hex -e 'local $/; print sha256_hex(<STDIN>)' < "$sample") || return 1
  size=$(wc -c < "$sample" | tr -d '[:space:]') || return 1
  cp "$sample" "$HOME_DIR/reserve/$sha.gguf" || return 1
  chmod 600 "$HOME_DIR/reserve/$sha.gguf" || return 1
  printf '{"updated":"2099-01-01T00:00:00Z","models":[{"id":"offline-reserve-test","name":"Réserve test","size":%s,"sha256":"%s","files":[{"url":"http://127.0.0.1:1/unreachable","size":%s,"sha256":"%s"}]}]}' \
    "$size" "$sha" "$size" "$sha" > "$folder/catalog.json" || return 1
  chmod 600 "$folder/catalog.json" || return 1
  payload=$(perl -MJSON::PP -e 'print encode_json({path => shift})' "$folder") || return 1
  reply=$(curl -sk --max-time 10 -H "Content-Type: application/json" -d "$payload" \
    "https://localhost:$PORT/albireo/v1/import") || return 1
  if ! printf '%s' "$reply" | perl -MJSON::PP -e '
    my $j = decode_json(do { local $/; <STDIN> });
    exit !(ref($j->{catalog}) eq "HASH" && ($j->{catalog}{models} || 0) == 1);
  '; then echo "catalog import failed: $reply"; return 1; fi

  code=$(curl -sk --max-time 10 -X POST -o "$WORK/reserve-start.json" -w '%{http_code}' \
    "https://localhost:$PORT/albireo/v1/models/offline-reserve-test/download") || return 1
  if [ "$code" != 202 ]; then echo "restoration refused ($code): $(cat "$WORK/reserve-start.json")"; return 1; fi
  for i in $(seq 1 200); do
    reply=$(curl -sk --max-time 5 "https://localhost:$PORT/albireo/v1/models") || return 1
    if printf '%s' "$reply" | perl -MJSON::PP -e '
      my $j = decode_json(do { local $/; <STDIN> });
      my ($m) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{models} || []};
      my ($d) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{downloads} || []};
      exit !($m && $m->{installed} && $d && ($d->{state} || "") eq "done");
    '; then restored=1; break; fi
    sleep 0.1
  done
  if [ "$restored" != 1 ]; then echo "restoration did not finish: $reply"; return 1; fi
  path=$(printf '%s' "$reply" | perl -MJSON::PP -e '
    my $j = decode_json(do { local $/; <STDIN> });
    my ($m) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{models}};
    print $m->{file};
  ') || return 1
  cmp -s "$sample" "$path" || { echo "restored bytes differ"; return 1; }

  code=$(curl -sk --max-time 10 -X DELETE -o "$WORK/reserve-delete.json" -w '%{http_code}' \
    "https://localhost:$PORT/albireo/v1/models/offline-reserve-test") || return 1
  if [ "$code" != 200 ]; then echo "model removal failed ($code): $(cat "$WORK/reserve-delete.json")"; return 1; fi
  for i in $(seq 1 200); do
    reply=$(curl -sk --max-time 5 "https://localhost:$PORT/albireo/v1/models") || return 1
    if printf '%s' "$reply" | perl -MJSON::PP -e '
      my $j = decode_json(do { local $/; <STDIN> });
      my ($m) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{models} || []};
      exit !($m && !$m->{installed});
    '; then removed=1; break; fi
    sleep 0.1
  done
  if [ "$removed" != 1 ]; then echo "model removal did not finish: $reply"; return 1; fi
  rm -f "$HOME_DIR/reserve/$sha.gguf" || return 1
  code=$(curl -sk --max-time 10 -X POST -o "$WORK/reserve-missing.json" -w '%{http_code}' \
    "https://localhost:$PORT/albireo/v1/models/offline-reserve-test/download") || return 1
  if [ "$code" != 202 ]; then echo "missing reserve refused synchronously ($code)"; return 1; fi
  for i in $(seq 1 200); do
    reply=$(curl -sk --max-time 5 "https://localhost:$PORT/albireo/v1/models") || return 1
    if printf '%s' "$reply" | perl -MJSON::PP -e '
      my $j = decode_json(do { local $/; <STDIN> });
      my ($m) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{models} || []};
      my ($d) = grep { ($_->{id} || "") eq "offline-reserve-test" } @{$j->{downloads} || []};
      exit !($m && !$m->{installed} && $d && ($d->{state} || "") eq "error"
        && ($d->{code} || "") eq "reserve_unavailable");
    '; then missing=1; break; fi
    sleep 0.1
  done
  if [ "$missing" != 1 ]; then echo "missing reserve was not reported: $reply"; return 1; fi
  echo "local restoration and missing reserve reported"
}

# Le service, hors ligne, dans un dossier de données à lui.
SERVICE=$BUILD/bin/albireo-service
if [ -x "$SERVICE" ] && [ -f "$MODEL" ]; then
  HOME_DIR=$WORK/home
  mkdir -p "$HOME_DIR"
  echo "{\"port\": $PORT, \"offline\": true, \"default_model\": false}" \
    > "$HOME_DIR/settings.json"
  ALBIREO_HOME=$HOME_DIR "$SERVICE" --console > "$WORK/service.log" 2>&1 &
  PID=$!
  START=$(now)
  for i in $(seq 1 300); do
    curl -sk --max-time 1 https://localhost:$PORT/albireo/v1/status -o /dev/null && break
    sleep 0.1
  done
  if curl -sk --max-time 2 https://localhost:$PORT/albireo/v1/status | grep -q '"offline":true'; then
    record "service start" PASS "offline, https://localhost:$PORT" "$(elapsed "$START")"
    # Les textes de licence de l'installation, aucun dans un dossier de build, et un nom hors de leur
    # dossier refusé.
    step "service licenses" '^\[(\]|\{"file")' curl -sk --max-time 5 https://localhost:$PORT/albireo/v1/licenses
    step "service license refusal" '"code":"invalid_license"' curl -sk --max-time 5 --path-as-is \
      "https://localhost:$PORT/albireo/v1/licenses/../settings.json"
    # Un modèle de l'utilisateur n'est pas lu par son chemin : l'application web l'envoie.
    step "service import refusal" '"code":"import_use_upload"' curl -sk --max-time 30 -X POST \
      -H "Content-Type: application/json" -d "{\"path\": \"$MODEL\"}" \
      https://localhost:$PORT/albireo/v1/import
    step "service import" '"imported":\["' curl -sk --max-time 600 -X POST \
      -H "Content-Type: application/octet-stream" -H "Expect:" -T "$MODEL" \
      "https://localhost:$PORT/albireo/v1/models/upload?name=$(basename "$MODEL")"
    for i in $(seq 1 300); do
      curl -sk --max-time 1 https://localhost:$PORT/v1/models | grep -q albireo-general && break
      sleep 0.2
    done
    BODY="{\"model\": \"albireo-general\", \"reasoning\": false, \"max_tokens\": 40,"
    BODY="$BODY \"messages\": [{\"role\": \"user\", \"content\": \"$QUESTION\"}]}"
    step "service chat" "Paris" curl -sk --max-time 300 https://localhost:$PORT/v1/chat/completions \
      -H "Content-Type: application/json" -d "$BODY"
    step "offline reserve" "local restoration and missing reserve reported" offline_reserve_test
    # Hors ligne, une recherche Hugging Face jamais gardée sur le Mac ne sort pas : 404 hub_not_cached.
    step "hub search kept on the Mac" '"code":"hub_not_cached"' curl -sk --max-time 5 \
      "https://localhost:$PORT/albireo/v1/hub/search?q=never-searched-$$"
    # Le fork n'a plus de lecture à voix haute : la route speech de l'interface OpenAI n'existe plus.
    step "service without speech" "404" curl -sk --max-time 5 -o /dev/null -w "%{http_code}" \
      -H "Content-Type: application/json" -d '{"input": "Bonjour"}' https://localhost:$PORT/v1/audio/speech
  else
    record "service start" FAIL "no answer, log in $WORK/service.log" 0.0
  fi
  # Le service a 30 secondes pour s'arrêter, puis il est tué par son numéro de processus.  SIGTERM :
  # macOS laisse tomber le SIGINT d'un programme qu'un script lance en arrière-plan, qui l'ignore.
  START=$(now)
  kill -TERM $PID 2> /dev/null
  for i in $(seq 1 300); do
    kill -0 $PID 2> /dev/null || break
    sleep 0.1
  done
  if kill -0 $PID 2> /dev/null; then
    kill -KILL $PID 2> /dev/null
    wait $PID 2> /dev/null
    record "service stop" FAIL "killed after 30 s, log in $WORK/service.log" "$(elapsed "$START")"
  else
    wait $PID
    RC=$?
    if [ $RC -eq 0 ]; then record "service stop" PASS "exit 0" "$(elapsed "$START")"
    else record "service stop" FAIL "exit $RC, log in $WORK/service.log" "$(elapsed "$START")"
    fi
  fi
else
  skip "service" "no $SERVICE or no $MODEL"
fi

echo
printf '%s\n' "${RESULTS[@]}"
echo
echo "failed steps: $FAILED"
[ $FAILED -eq 0 ] && rm -rf "$WORK"
exit $FAILED
