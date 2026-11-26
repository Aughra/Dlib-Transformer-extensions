#!/bin/bash
# Banc d'essai de la page web d'Albireo (fork pour macOS).
#
#   bash dlib/dlib/external/gulliver/tools/page_test/page_test.sh [scénario…]
#
# Démarre un service d'essai isolé (port 18491, hors ligne, données dans build/page-test), qui sert la page des
# sources (web/), y installe des modèles clonés depuis le Mac (copies APFS, sans place prise), puis joue les scénarios
# (scenarios/*.json, tous par défaut) dans Chrome sans fenêtre, et l'arrête.  Le service dont on se sert n'est pas
# touché.  Avant les scénarios, ESLint vérifie les modules de la page (noms non définis, imports réaffectés,
# déclarations en double) quand npm est là : il s'installe une fois dans build/page-test/lint.
#
# Il faut : Albireo construit (build/macos-gulliver), Google Chrome, Node.js (Homebrew), et le modèle
# build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf (PAGE_TEST_MODEL pour un autre).  Le scénario « images » demande en plus
# le Qwen3-VL 4B et son projecteur, tels que le service les installe (build/gulliver-home/models/qwen3-vl-4b-thinking*),
# et il est sauté sans eux.  PAGE_TEST_KEEP=1 laisse le service d'essai tourner à la fin.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../../.." && pwd)
BUILD=${PAGE_TEST_BUILD:-$ROOT/build/macos-gulliver}
WORK=$ROOT/build/page-test
BIN=$WORK/bin HOME_DIR=$WORK/home PORT=18491
URL=https://localhost:$PORT
MODEL=${PAGE_TEST_MODEL:-$ROOT/build/models/Qwen3VL-2B-Instruct-Q4_K_M.gguf}
VL4=$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.gguf
VL4P=$ROOT/build/gulliver-home/models/qwen3-vl-4b-thinking.mmproj.gguf

for need in "$BUILD/bin/gulliver-service" "$MODEL"; do
  [ -e "$need" ] || { echo "missing: $need"; exit 1; }
done
command -v node > /dev/null || { echo "missing: node (brew install node)"; exit 1; }
mkdir -p "$WORK" && chmod 700 "$WORK"

# Le service d'essai, avec la page des sources.
cp -c "$BUILD/bin/gulliver-service" "$BIN/" 2>/dev/null || { mkdir -p "$BIN" && cp -c "$BUILD/bin/gulliver-service" "$BIN/"; }
# L'extraction du texte des PDF passe par PDFKit (cadriciel natif de macOS, 7.3.0) : rien à
# copier à côté du service.
rsync -a --delete "$BUILD/bin/engines/" "$BIN/engines/"
[ -d "$BUILD/bin/licenses" ] && rsync -a --delete "$BUILD/bin/licenses/" "$BIN/licenses/"
rsync -a --delete "$ROOT/dlib/dlib/external/gulliver/web/" "$BIN/web/"

# Vrai quand le service d'essai a installé le modèle id.
installed() {
  curl -sk "$URL/gulliver/v1/models" | python3 -c 'import json, sys
d = json.load(sys.stdin)
sys.exit(0 if any(m.get("id") == sys.argv[1] and m.get("installed") for m in d.get("models", [])) else 1)' "$1"
}

# Une entrée de catalogue local : le fichier est déjà dans la réserve, l'adresse ne répond pas.
entry() { # id nom fichier [projecteur]
  local sha size psha psize proj=""
  sha=$(shasum -a 256 "$3" | cut -c1-64); size=$(stat -f %z "$3")
  cp -c "$3" "$HOME_DIR/reserve/$sha.gguf"
  if [ -n "${4:-}" ]; then
    psha=$(shasum -a 256 "$4" | cut -c1-64); psize=$(stat -f %z "$4")
    cp -c "$4" "$HOME_DIR/reserve/$psha.gguf"
    proj=$(printf ',"inputs":["text","image"],"projector":{"size":%s,"sha256":"%s","files":[{"url":"http://127.0.0.1:1/p","size":%s,"sha256":"%s"}]}' "$psize" "$psha" "$psize" "$psha")
  fi
  printf '{"id":"%s","name":"%s","size":%s,"sha256":"%s","files":[{"url":"http://127.0.0.1:1/m","size":%s,"sha256":"%s"}]%s}' \
    "$1" "$2" "$size" "$sha" "$size" "$sha" "$proj"
}
fresh=0
if [ ! -f "$HOME_DIR/settings.json" ]; then
  fresh=1
  mkdir -p "$HOME_DIR/reserve" "$HOME_DIR/import/catalog"
  chmod 700 "$HOME_DIR" "$HOME_DIR/reserve" "$HOME_DIR/import" "$HOME_DIR/import/catalog"
  models=$(entry qwen3-vl-2b-test "Qwen3-VL 2B (essai)" "$MODEL")
  if [ -f "$VL4" ] && [ -f "$VL4P" ]; then
    models="$models,$(entry qwen3-vl-4b-test "Qwen3-VL 4B (essai)" "$VL4" "$VL4P")"
  fi
  chmod 600 "$HOME_DIR"/reserve/*.gguf
  printf '{"updated":"2099-01-01T00:00:00Z","models":[%s]}' "$models" > "$HOME_DIR/import/catalog/catalog.json"
  chmod 600 "$HOME_DIR/import/catalog/catalog.json"
  echo "{\"port\": $PORT, \"offline\": true, \"default_model\": false}" > "$HOME_DIR/settings.json"
fi
# La bibliothèque du Mac (Albireo 4.30.0) : un clone du petit Qwen 3.5, que la page charge dans Albireo.
LIBRARY_MODEL=$ROOT/build/models/Qwen3.5-0.8B-Q8_0.gguf
mkdir -p "$HOME_DIR/library" && chmod 700 "$HOME_DIR/library"
if [ -f "$LIBRARY_MODEL" ] && [ ! -f "$HOME_DIR/library/Qwen3.5-0.8B-Q8_0.gguf" ]; then
  cp -c "$LIBRARY_MODEL" "$HOME_DIR/library/"
fi
# Le conseil de la mesure de la semaine du service d'essai : son propre modèle, avec les réglages qu'il a déjà (le clic
# sur « Choisir » ne redémarre donc pas le service), pour le scénario de la carte du conseil ; un modèle retiré, pour le
# menu « Modèles retirés ».
cat > "$WORK/advice.json" <<'JSON'
{"date": "essai du banc", "previous": null, "new": [], "late": false, "advice": [
 {"role": "general", "id": "qwen3-vl-2b-test", "name": "Qwen3-VL 2B (essai)", "why": "essai",
  "write_512": 160.0, "read_4096": 5000.0, "perplexity": 18.6, "image": true,
  "settings": {"kv_type": "q8", "draft_tokens": 7}}],
 "removed": [{"date": "2026-11-24 12:40", "id": "ancien-essai", "name": "Ancien modèle (essai)", "origin": "huggingface",
  "repo": "essai/ancien-GGUF", "file": "ancien-Q4_K_M.gguf", "projector": "", "quiz": 41, "write_512": 52.5,
  "why": "le dernier du classement"}]}
JSON
GULLIVER_HOME=$HOME_DIR GULLIVER_ADVICE_FILE=$WORK/advice.json "$BIN/gulliver-service" --console > "$WORK/service.log" 2>&1 &
SERVICE=$!
stop() { [ "${PAGE_TEST_KEEP:-0}" = 1 ] || kill -TERM "$SERVICE" 2>/dev/null; }
trap stop EXIT
for _ in $(seq 1 60); do curl -sk --max-time 2 "$URL/gulliver/v1/status" | grep -q '"offline":true' && break; sleep 1; done
if [ "$fresh" = 1 ]; then
  curl -sk -H "Content-Type: application/json" -d "{\"path\":\"$HOME_DIR/import/catalog\"}" "$URL/gulliver/v1/import" > /dev/null
  for id in qwen3-vl-2b-test qwen3-vl-4b-test; do
    curl -sk -X POST "$URL/gulliver/v1/models/$id/download" > /dev/null
  done
  for _ in $(seq 1 60); do
    installed qwen3-vl-2b-test && break; sleep 1
  done
  curl -sk -H "Content-Type: application/json" -d '{"role":"general"}' "$URL/gulliver/v1/models/qwen3-vl-2b-test/activate" > /dev/null
  sleep 5
fi
# Le modèle chargé avant les scénarios : en mode économie d'énergie, son chargement prend une vingtaine de secondes, et
# une question posée avant reçoit « Albireo démarre ».
for _ in $(seq 1 180); do grep -q "models: qwen3-vl-2b-test loaded" "$WORK/service.log" && break; sleep 1; done

# ESLint, installé une fois dans build/page-test/lint.
if command -v npm > /dev/null; then
  LINT=$WORK/lint
  if [ ! -x "$LINT/node_modules/.bin/eslint" ]; then
    mkdir -p "$LINT" && (cd "$LINT" && npm init -y > /dev/null && npm i --silent eslint@9.17.0 globals@15.14.0 > /dev/null)
  fi
  cp "$HERE/eslint.config.mjs" "$LINT/"
  if (cd "$ROOT/dlib/dlib/external/gulliver/web/js" && "$LINT/node_modules/.bin/eslint" --no-config-lookup \
        -c "$LINT/eslint.config.mjs" ./*.js); then
    echo "lint: ok"
  else
    echo "lint: FAILED"; exit 1
  fi
else
  echo "lint: skipped (no npm)"
fi

# Un profil et sa session pour les scénarios : les données vivent côté service, rangées par profil, et chaque
# profil a un mot de passe (comptes, Gulliver 6.31.0).  Le dossier du service d'essai survit d'un passage à
# l'autre : son administrateur « banc » a un mot de passe tiré au hasard au premier passage et gardé dans
# $WORK/banc.pass (dossier d'essai sous build/, jamais commité).  À chaque passage, l'administrateur supprime les
# profils « scenarios-* » d'avant (avec leurs données), en crée un neuf et s'y connecte ; le profil et le jeton
# sont posés dans localStorage (PAGE_TEST_PROFILE, PAGE_TEST_SESSION, que cdp.mjs injecte avant la page).
PASS_FILE=$WORK/banc.pass
# Conforme à la règle des mots de passe (7.3.9) : majuscule, minuscule, chiffre, caractère spécial, 20 caractères au moins.
[ -s "$PASS_FILE" ] || { (umask 077; echo "Banc-1-$(openssl rand -hex 16)" > "$PASS_FILE"); }
read -r PAGE_TEST_PROFILE PAGE_TEST_SESSION < <(python3 "$HERE/session.py" "$URL" "$PASS_FILE")
export PAGE_TEST_PROFILE PAGE_TEST_SESSION
[ -n "$PAGE_TEST_SESSION" ] || { echo "page_test: no session for the scenarios"; }

# Les scénarios.
cd "$WORK" || exit 1
names=("$@")
[ ${#names[@]} -eq 0 ] && names=(regress docs images actions tabs phone)
status=0
for n in "${names[@]}"; do
  if [ "$n" = images ] && ! installed qwen3-vl-4b-test; then
    echo "$n: skipped (no Qwen3-VL 4B with its projector)"; continue
  fi
  rm -rf "$WORK/downloads"
  PAGE_TEST_WORK=$WORK PAGE_URL=$URL/ node "$HERE/cdp.mjs" "$HERE/scenarios/$n.json" > "$WORK/$n.txt" 2>&1 || status=1
  printf '%-8s %3s ok, %s fail; %s\n' "$n:" "$(grep -c '^ok ' "$WORK/$n.txt")" "$(grep -c '^FAIL' "$WORK/$n.txt")" \
    "$(tail -1 "$WORK/$n.txt")"
  grep -h '^FAIL\|^  exception\|^  console' "$WORK/$n.txt" | head -5
done
echo "details: $WORK/<scenario>.txt, screenshots in $WORK/shots"
exit $status
