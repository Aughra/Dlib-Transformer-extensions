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

if [ -f "$EMBEDDING" ] && [ -f "$ENGINE" ]; then
  step "embedding model" "cosine" "$CHAT" "$ENGINE" "$EMBEDDING" --parallel \
    "The service runs on this computer." "Where does the service run?" "The quarterly revenue grew."
else
  skip "embedding model" "no $EMBEDDING"
fi

# Le programme de la synthèse vocale seul : une phrase lue à voix haute dans le dossier des voix.
SPEAKER=$BUILD/bin/albireo-speech
if [ -x "$SPEAKER" ] && [ -d "$BUILD/bin/speech" ]; then
  step "speech program" "RIFF" sh -c "cd '$BUILD/bin/speech' && printf '%s' 'Bonjour, Albireo lit à voix haute.' \
    | '$SPEAKER' fr-FR 1 > '$WORK/program.wav' && head -c 4 '$WORK/program.wav' && echo && wc -c < '$WORK/program.wav'"
else
  skip "speech program" "no $SPEAKER or no $BUILD/bin/speech"
fi

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
    # Les voix de la synthèse vocale, puis une phrase lue à voix haute par le programme que lance le
    # service.
    step "service speech voices" '"id":"fr-FR"' curl -sk --max-time 5 \
      https://localhost:$PORT/albireo/v1/speech/voices
    SPEECH="{\"input\": \"Bonjour, Albireo lit ses réponses à voix haute.\", \"voice\": \"fr-FR\"}"
    step "service speech" "RIFF" sh -c "curl -sk --max-time 60 -o '$WORK/speech.wav' \
      -H 'Content-Type: application/json' -d '$SPEECH' https://localhost:$PORT/v1/audio/speech \
      && head -c 4 '$WORK/speech.wav' && echo && wc -c < '$WORK/speech.wav'"
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
