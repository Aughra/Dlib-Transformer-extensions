#!/bin/bash
# Copyright (C) 2026 Cydral Technology (cydraltechnology@gmail.com)
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Construit et teste Pseudalis sur macOS (Apple Silicon), où le service porte son moteur et où son
# lecteur fonctionne comme un dossier sans lettre, et affiche un résumé, une ligne par étape.
#
#   bash dlib/dlib/external/pseudalis/tools/test_macos.sh [build folder]
#
# Dossier de build par défaut : build/macos-pseudalis à la racine du dépôt.  Les tests du moteur et
# de l'hôte passent d'abord ; le service tourne ensuite dans un dossier de données à lui, sur le port
# 18444 (PSEUDALIS_TEST_PORT), anonymise et restaure par son interface un texte, un fichier CSV, un
# nom et l'archive d'un dossier telle que l'application web l'envoie, puis des fichiers et un dossier
# par son lecteur, dont les originaux reviennent par l'interface, une archive extraite sur le
# lecteur, une archive dont l'original a été oublié qui revient et les copies d'un fichier,
# enregistre les réglages comme le fait l'application web, impose des termes, fait de la place pour
# des originaux plus récents, restaure à partir de leurs étiquettes une archive dont l'image a été
# oubliée et un texte écrit ailleurs à partir de valeurs anonymisées, protège des fichiers, un texte
# et le lecteur sans étiquettes, restaure des fichiers protégés deux fois, et s'arrête.
# Le code de sortie est le nombre d'étapes en échec.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SOURCE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
BUILD=${1:-$ROOT/build/macos-pseudalis}
PORT=${PSEUDALIS_TEST_PORT:-18444}
WORK=$(mktemp -d)
RESULTS=()
FAILED=0

record() { # étape résultat détail
  RESULTS+=("$(printf '%-26s %-5s %s' "$1" "$2" "$3")")
  [ "$2" = FAIL ] && FAILED=$((FAILED + 1))
}

# Lance une commande, puis vérifie que sa sortie contient un motif.
step() { # nom motif commande...
  local name=$1 pattern=$2
  shift 2
  local out rc
  out=$("$@" 2>&1)
  rc=$?
  if [ $rc -eq 0 ] && printf '%s\n' "$out" | grep -E -q "$pattern"; then
    record "$name" PASS "$(printf '%s\n' "$out" | grep -E -o -m1 ".{0,40}$pattern.{0,30}" | head -1)"
  else
    printf '%s\n' "$out" | tail -40 > "$WORK/${name// /_}.log"
    record "$name" FAIL "exit $rc, output in $WORK/${name// /_}.log"
  fi
}

step "configuration" "Generating done|Build files" cmake -S "$SOURCE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
step "build" "." cmake --build "$BUILD" --parallel "$(sysctl -n hw.ncpu)"
step "engine tests" "all tests passed" "$BUILD/bin/test_engine"
step "host tests" "all tests passed" "$BUILD/bin/test_host"

HOME_DIR=$WORK/home
mkdir -p "$HOME_DIR"
cat > "$HOME_DIR/settings.json" <<EOF
{"port": $PORT, "drive_letter": "A", "albireo": "off"}
EOF
PSEUDALIS_HOME=$HOME_DIR "$BUILD/bin/pseudalis-service" --console > "$WORK/service.log" 2>&1 &
SERVICE=$!
CA=$HOME_DIR/certificates/pseudalis-authority.crt
URL=https://localhost:$PORT
api() { curl -sS --cacert "$CA" "$@"; }
ready=0
for _ in $(seq 1 60); do
  if [ -f "$CA" ] && api "$URL/v1/health" 2>/dev/null | grep -q '"ok"'; then ready=1; break; fi
  sleep 1
done
if [ $ready -eq 1 ]; then
  record "service start" PASS "$URL"
  TEXT='Jean Dupont habite 12 rue de la Paix, 75002 Paris, jean.dupont@exemple.fr'
  step "text anonymized" '\[\[PERSON:' api -X POST -H "Content-Type: application/json" \
    --data "{\"text\": \"$TEXT\"}" "$URL/v1/text/anonymize"
  PROTECTED=$(api -X POST -H "Content-Type: application/json" --data "{\"text\": \"$TEXT\"}" "$URL/v1/text/anonymize" |
    python3 -c 'import json, sys; print(json.dumps({"text": json.load(sys.stdin)["text"]}))')
  step "text restored" "jean.dupont@exemple.fr" api -X POST -H "Content-Type: application/json" \
    --data "$PROTECTED" "$URL/v1/text/restore"
  printf 'nom;prenom;email;telephone\nDupont;Jean;jean.dupont@exemple.fr;06 12 34 56 78\n' > "$WORK/sample.csv"
  api -X POST --data-binary @"$WORK/sample.csv" -o "$WORK/sample.anon.csv" "$URL/v1/anonymize?name=sample.csv"
  step "file anonymized" '\[\[EMAIL:' cat "$WORK/sample.anon.csv"
  api -X POST --data-binary @"$WORK/sample.anon.csv" -o "$WORK/sample.back.csv" "$URL/v1/restore?name=sample.csv"
  if cmp -s "$WORK/sample.csv" "$WORK/sample.back.csv"; then record "file restored" PASS "identical to the original"
  else record "file restored" FAIL "the restored file differs, in $WORK"
  fi
  # Le nom d'un fichier prend des substituts, sans étiquettes.
  api -X POST --data-binary @"$WORK/sample.csv" -D "$WORK/name.headers" -o /dev/null \
    "$URL/v1/anonymize?name=jean.dupont%40exemple.fr.csv"
  if grep -i '^x-pseudalis-name:' "$WORK/name.headers" | grep -q -v -i -e dupont -e '\[\['; then
    record "name anonymized" PASS "$(grep -i '^x-pseudalis-name:' "$WORK/name.headers" | tr -d '\r')"
  else record "name anonymized" FAIL "$(grep -i '^x-pseudalis-name:' "$WORK/name.headers" | tr -d '\r')"
  fi
  # Un texte sans type est protégé comme un texte ; un fichier binaire, ou un texte avec des parties
  # encodées, est refusé.
  printf 'Journal de Jean Dupont, jean.dupont@exemple.fr\n' > "$WORK/journal.log2"
  step "text of no kind" '\[\[EMAIL:' api -X POST --data-binary @"$WORK/journal.log2" \
    "$URL/v1/anonymize?name=journal.log2"
  printf 'Jean Dupont\0\001\002' > "$WORK/donnees.bin"
  step "binary of no kind" '"code":"unsupported"' api -X POST --data-binary @"$WORK/donnees.bin" \
    "$URL/v1/anonymize?name=donnees.bin"
  printf 'From: Jean Dupont\r\nContent-Transfer-Encoding: base64\r\n\r\nSmVhbiBEdXBvbnQ=\r\n' > "$WORK/message.eml"
  step "encoded of no kind" '"code":"encoded_content"' api -X POST --data-binary @"$WORK/message.eml" \
    "$URL/v1/anonymize?name=message.eml"
  # Un nom seul, celui d'un dossier, et sa restauration.
  name_call() { # route nom
    api -X POST -H "Content-Type: application/json" --data "$(python3 -c 'import json, sys
print(json.dumps({"name": sys.argv[1]}))' "$2")" "$URL/v1/names/$1" |
      python3 -c 'import json, sys; print(json.load(sys.stdin).get("name", ""))'
  }
  NAMED=$(name_call anonymize "Dossier jean.dupont@exemple.fr")
  if [ -n "$NAMED" ] && ! printf '%s' "$NAMED" | grep -q -i dupont &&
     [ "$(name_call restore "$NAMED")" = "Dossier jean.dupont@exemple.fr" ]; then
    record "folder name" PASS "$NAMED"
  else record "folder name" FAIL "anonymized as '$NAMED'"
  fi

  # Un dossier tel que l'application web l'envoie, une archive ZIP stockée sans compression : ses
  # documents et ses noms protégés, le fichier d'un autre type laissé de côté, l'original rendu.
  python3 - "$WORK/dossier.zip" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_STORED)
z.writestr("Dossier Jean Dupont/", b"")
z.writestr("Dossier Jean Dupont/lettre.txt", "Je suis Jean Dupont, jean.dupont@exemple.fr.\n".encode())
z.writestr("Dossier Jean Dupont/vide/", b"")
z.writestr("Dossier Jean Dupont/outil.bin", b"MZ\x90\x00binary\x00")
z.close()
PY
  api -X POST --data-binary @"$WORK/dossier.zip" -D "$WORK/zip.headers" -o "$WORK/dossier.anon.zip" \
    "$URL/v1/anonymize?name=Dossier%20Jean%20Dupont.zip"
  ZIP_CHECK=$(python3 - "$WORK/dossier.anon.zip" "$WORK/zip.headers" <<'PY'
import json, sys, urllib.parse, zipfile
names = zipfile.ZipFile(sys.argv[1]).namelist()
headers = dict(l.split(":", 1) for l in open(sys.argv[2], encoding="latin-1").read().splitlines() if ":" in l)
headers = {k.strip().lower(): v.strip() for k, v in headers.items()}
report = json.loads(urllib.parse.unquote(headers.get("x-pseudalis-report", "{}")))
name = urllib.parse.unquote(headers.get("x-pseudalis-name", ""))
clean = all("dupont" not in n.lower() for n in names) and "dupont" not in name.lower()
print("ok" if clean and report.get("dropped") == 1 and len(names) == 3 else "bad %s %s %s" % (name, names, report))
PY
)
  if [ "$ZIP_CHECK" = ok ]; then record "folder archive" PASS "names, documents, 1 entry left out"
  else record "folder archive" FAIL "$ZIP_CHECK"
  fi
  api -X POST --data-binary @"$WORK/dossier.anon.zip" -o "$WORK/dossier.back.zip" \
    "$URL/v1/restore?name=dossier.zip"
  if cmp -s "$WORK/dossier.zip" "$WORK/dossier.back.zip"; then record "archive restored" PASS "the original archive"
  else record "archive restored" FAIL "the restored archive differs, in $WORK"
  fi

  # Le lecteur : un document anonymisé sous un nom sans données personnelles, les fichiers d'un
  # autre type sortis à leur place dans l'arborescence, un dossier renommé une fois ses fichiers au
  # repos ; puis les originaux du fichier et du nom du dossier par l'interface.
  DRIVE=$HOME_DIR/drive
  wait_for() { # secondes commande...
    local n=$1
    shift
    for _ in $(seq 1 "$n"); do "$@" && return 0; sleep 1; done
    return 1
  }
  printf 'Bonjour,\nJe suis Jean Dupont, joignable au 06 12 34 56 78.\n' > "$WORK/lettre.txt"
  printf 'Note de Marie Curie, 12 rue des Lilas, 75005 Paris.\n' > "$WORK/note.txt"
  cp "$WORK/lettre.txt" "$DRIVE/Lettre jean.dupont@exemple.fr.txt"
  mkdir -p "$DRIVE/Dossier jean.dupont@exemple.fr"
  cp "$WORK/note.txt" "$DRIVE/Dossier jean.dupont@exemple.fr/note.txt"
  printf 'binary\0' > "$DRIVE/outil.bin"
  printf 'binary\0' > "$DRIVE/Dossier jean.dupont@exemple.fr/outil2.bin"
  cp "$WORK/lettre.txt" "$DRIVE/faux.docx"
  cp "$WORK/lettre.txt" "$DRIVE/journal jean.dupont@exemple.fr.log2"
  drive_done() {
    [ -z "$(find "$DRIVE" -iname '*dupont*')" ] && [ -f "$HOME_DIR/refused/files/outil.bin" ] &&
      [ -f "$HOME_DIR/refused/files/faux.docx" ] && [ -n "$(find "$DRIVE" -mindepth 2 -name note.txt)" ] &&
      [ -n "$(find "$HOME_DIR/refused/files" -mindepth 2 -name outil2.bin)" ] &&
      [ -n "$(find "$DRIVE" -maxdepth 1 -name '*.log2')" ]
  }
  if wait_for 60 drive_done; then record "drive anonymized" PASS "names, content, the files of another kind out"
  else record "drive anonymized" FAIL "$(find "$DRIVE" "$HOME_DIR/refused" | tr '\n' ' ')"
  fi
  # Une archive de plus de 64 Ko : anonymisée, puis renommée avec une extension sans type, elle reste
  # un fichier protégé du lecteur ; sans extension, elle est reconnue à sa signature.
  python3 - "$WORK/grosse.zip" <<'PY'
import random, sys, zipfile
random.seed(5)
words = ["".join(random.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(random.randint(3, 9)))
         for _ in range(5000)]
text = "Note de Jean Dupont, " + " ".join(random.choice(words) for _ in range(60000)) + "\n"
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_STORED)
z.writestr("notes.txt", text)
z.close()
PY
  cp "$WORK/grosse.zip" "$DRIVE/grosse.zip"
  cp "$WORK/grosse.zip" "$DRIVE/archive_sans_extension"
  # Le nom sans extension peut aussi prendre des substituts : le fichier est trouvé par sa fin.
  bare() { find "$DRIVE" -maxdepth 1 -type f -name '*sans_extension' | head -1; }
  archive_done() {
    [ -f "$DRIVE/grosse.zip" ] && [ -n "$(bare)" ] && ! cmp -s "$WORK/grosse.zip" "$DRIVE/grosse.zip" &&
      ! cmp -s "$WORK/grosse.zip" "$(bare)"
  }
  wait_for 90 archive_done
  BIG=$(stat -f %z "$DRIVE/grosse.zip" 2>/dev/null || echo 0)
  mv "$DRIVE/grosse.zip" "$DRIVE/grosse.zip.bak"
  sleep 8
  if [ -f "$DRIVE/grosse.zip.bak" ] && [ ! -e "$HOME_DIR/refused/files/grosse.zip.bak" ] &&
     [ "$BIG" -gt 65536 ] && [ -n "$(bare)" ] && ! cmp -s "$WORK/grosse.zip" "$(bare)"; then
    record "drive signatures" PASS "a protected archive renamed stays, an archive without extension is taken"
  else
    record "drive signatures" FAIL "size $BIG, $(find "$DRIVE" "$HOME_DIR/refused" -name '*grosse*' -o -name '*sans*' |
      tr '\n' ' ')"
  fi
  ANON=$(find "$DRIVE" -maxdepth 1 -type f -name 'Lettre*' | head -1)
  step "drive content" '\[\[PERSON:' cat "$ANON"
  api -X POST --data-binary @"$ANON" -D "$WORK/back.headers" -o "$WORK/lettre.back.txt" \
    "$URL/v1/restore?name=$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' \
      "$(basename "$ANON")")"
  BACK_NAME=$(grep -i '^x-pseudalis-name:' "$WORK/back.headers" | cut -d: -f2- | tr -d '\r ' |
    python3 -c 'import sys, urllib.parse; print(urllib.parse.unquote(sys.stdin.read().strip()))')
  FOLDER=$(find "$DRIVE" -mindepth 1 -maxdepth 1 -type d | head -1)
  if cmp -s "$WORK/lettre.txt" "$WORK/lettre.back.txt" && [ "$BACK_NAME" = "Lettre jean.dupont@exemple.fr.txt" ] &&
     [ "$(name_call restore "$(basename "$FOLDER")")" = "Dossier jean.dupont@exemple.fr" ]; then
    record "drive restored" PASS "the original, its name and the name of the folder"
  else record "drive restored" FAIL "name '$BACK_NAME', folder $(basename "$FOLDER")"
  fi

  # Les fiches des originaux, par l'interface des administrateurs.
  TOKEN=$(cat "$HOME_DIR/admin.token" 2>/dev/null)
  records() { api -H "X-Pseudalis-Admin: $TOKEN" "$URL/v1/records?limit=1000"; }
  # La fiche d'un fichier protégé, par le SHA-256 de $1 ou par son identifiant $2 :
  # "<id>|<removed>|<original name>".
  record_of() {
    records | python3 -c 'import json, sys
sha, ident = sys.argv[1], sys.argv[2]
for r in json.load(sys.stdin)["records"]:
    if r["output_sha256"] == sha or str(r["id"]) == ident:
        print("%s|%s|%s" % (r["id"], r["removed"] or "-", r["original_name"]))
        break' "$( [ -f "$1" ] && sha256sum "$1" | cut -c1-64)" "${2:-}"
  }
  total() { records | python3 -c 'import json, sys; print(json.load(sys.stdin)["total"])'; }
  quoted() { python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$1"; }
  restore_to() { # fichier sortie, les en-têtes de la réponse dans output.headers
    api -X POST --data-binary @"$1" -D "$2.headers" -o "$2" "$URL/v1/restore?name=$(quoted "$(basename "$1")")"
  }
  report_of() { # en-têtes : le rapport qu'ils donnent, décodé
    python3 -c 'import sys, urllib.parse
for line in open(sys.argv[1], encoding="latin-1"):
    if line.lower().startswith("x-pseudalis-report:"):
        print(urllib.parse.unquote(line.split(":", 1)[1].strip()))' "$1"
  }
  # Une archive anonymisée sur le lecteur, puis extraite sur le lecteur : ses fichiers restent tels
  # quels, chacun enregistré sous son nom d'origine avec l'original de son entrée, et restauré et
  # oublié seul.
  python3 - "$WORK/envoi.zip" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
z.writestr("Dossier Jean Dupont/lettre.txt", "Je suis Jean Dupont, joignable au 06 12 34 56 78.\n")
z.writestr("note Marie Curie.txt", "Courrier de Marie Curie, 14 avenue des Tilleuls, 69003 Lyon.\n")
z.writestr("reste.txt", "Rendez-vous avec Paul Martin le 3 mars.\n")
z.close()
PY
  cp "$WORK/envoi.zip" "$DRIVE/envoi.zip"
  envoi_done() { [ -f "$DRIVE/envoi.zip" ] && ! cmp -s "$WORK/envoi.zip" "$DRIVE/envoi.zip" &&
    [ -n "$(record_of "$DRIVE/envoi.zip")" ]; }
  wait_for 60 envoi_done
  cp "$DRIVE/envoi.zip" "$WORK/envoi.anon.zip"
  # Deux fichiers extraits sur le lecteur, le troisième ailleurs.
  python3 - "$WORK/envoi.anon.zip" "$WORK/extrait" "$WORK/ailleurs" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.endswith("/"):
        continue
    path = os.path.join(sys.argv[3] if os.path.basename(n) == "reste.txt" else sys.argv[2], n)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(z.read(n))
PY
  OUTSIDE=$WORK/ailleurs/reste.txt
  mkdir -p "$DRIVE/extrait"
  cp -r "$WORK/extrait/." "$DRIVE/extrait/"
  extracted_done() {
    local f n=0
    while IFS= read -r f; do
      [ -n "$(record_of "$f")" ] || return 1
      n=$((n + 1))
    done < <(find "$DRIVE/extrait" -type f)
    [ "$n" -eq 2 ]
  }
  wait_for 60 extracted_done
  SAME=0
  while IFS= read -r f; do
    cmp -s "$f" "$DRIVE/extrait/${f#"$WORK/extrait/"}" && SAME=$((SAME + 1))
  done < <(find "$WORK/extrait" -type f)
  NAMES=$(find "$DRIVE/extrait" -type f | while IFS= read -r f; do record_of "$f" | cut -d'|' -f3; done | sort |
    tr '\n' ',')
  if [ "$SAME" -eq 2 ] && [ "$NAMES" = "lettre.txt,note Marie Curie.txt," ]; then
    record "archive extracted" PASS "2 files kept as they are, recorded under their original names"
  else record "archive extracted" FAIL "unchanged $SAME, names $NAMES"
  fi
  LETTRE=$(find "$DRIVE/extrait" -type f -name 'lettre.txt' | head -1)
  restore_to "$LETTRE" "$WORK/lettre.extrait.txt"
  restore_to "$OUTSIDE" "$WORK/reste.back.txt"
  ZIP_ID=$(record_of "$WORK/envoi.anon.zip" | cut -d'|' -f1)
  api -X DELETE -H "X-Pseudalis-Admin: $TOKEN" -o /dev/null "$URL/v1/records/$ZIP_ID"
  restore_to "$LETTRE" "$WORK/lettre.apres.txt"
  restore_to "$OUTSIDE" "$WORK/reste.apres.txt"
  if grep -q 'Jean Dupont, joignable' "$WORK/lettre.extrait.txt" && grep -q 'Paul Martin' "$WORK/reste.back.txt" &&
     grep -q 'Jean Dupont, joignable' "$WORK/lettre.apres.txt" &&
     grep -q 'Paul Martin le 3 mars' "$WORK/reste.apres.txt" &&
     grep -qi '^x-pseudalis-restored: tags' "$WORK/reste.apres.txt.headers" &&
     report_of "$WORK/reste.apres.txt.headers" | grep -q '"original_forgotten"' 
  then
    record "extracted restored" PASS "from their originals once the archive forgotten, the file outside from its tags"
  else record "extracted restored" FAIL "see lettre.extrait.txt, reste.back.txt, lettre.apres.txt, reste.apres.txt"
  fi
  # L'archive oubliée revient sur le lecteur : elle reste telle quelle, sans nouvel original.
  COUNT=$(total)
  cp "$WORK/envoi.anon.zip" "$DRIVE/revenu.zip"
  sleep 8
  if cmp -s "$WORK/envoi.anon.zip" "$DRIVE/revenu.zip" && [ "$(total)" = "$COUNT" ]; then
    record "forgotten comes back" PASS "kept as it is, no new original"
  else record "forgotten comes back" FAIL "$(ls "$DRIVE" | tr '\n' ' '), records $COUNT then $(total)"
  fi
  # Un fichier protégé copié sur le lecteur : sa fiche reste tant qu'il en reste une copie, et part avec
  # la dernière.
  printf 'Copie pour Lucie Marchal, 3 place du Champ de Foire, 29000 Quimper.\n' > "$DRIVE/copies.txt"
  copies_done() { ! grep -q Marchal "$DRIVE/copies.txt" 2>/dev/null && [ -n "$(record_of "$DRIVE/copies.txt")" ]; }
  wait_for 60 copies_done
  COPIES_ID=$(record_of "$DRIVE/copies.txt" | cut -d'|' -f1)
  COUNT=$(total)
  cp "$DRIVE/copies.txt" "$DRIVE/copies (2).txt"
  # La copie se pose, puis sa fiche est celle du fichier : pas de nouvel original.
  sleep 6
  rm -f "$DRIVE/copies.txt"
  sleep 4
  KEPT=$(record_of "" "$COPIES_ID" | cut -d'|' -f2)
  rm -f "$DRIVE/copies (2).txt"
  copies_gone() { [ "$(record_of "" "$COPIES_ID" | cut -d'|' -f2)" != "-" ]; }
  wait_for 30 copies_gone
  GONE=$(record_of "" "$COPIES_ID" | cut -d'|' -f2)
  if [ -n "$COPIES_ID" ] && [ "$(total)" = "$COUNT" ] && [ "$KEPT" = "-" ] && [ -n "$GONE" ] && [ "$GONE" != "-" ]
  then
    record "copies on the drive" PASS "one record, kept with a copy, removed with the last"
  else record "copies on the drive" FAIL "record '$COPIES_ID', with the copy '$KEPT', after it '$GONE'"
  fi
  # Les réglages tels que l'application web les enregistre, sans flux ni proxy ; une valeur hors de sa
  # plage refusée.
  admin_json() { # méthode route corps
    api -X "$1" -H "X-Pseudalis-Admin: $TOKEN" -H "Content-Type: application/json" --data "$3" "$URL$2"
  }
  SAVED=$(admin_json PUT /v1/settings '{"language": "auto", "drive_letter": "A", "date_order": "dmy",
    "max_file_mb": 150, "removed_days": 0, "history_mb": 10240, "albireo": "off"}')
  REFUSED=$(admin_json PUT /v1/settings '{"removed_days": -1}')
  if printf '%s' "$SAVED" | grep -q '"removed_days":0' && printf '%s' "$SAVED" | grep -q '"restart":false' &&
     printf '%s' "$REFUSED" | grep -q '"code":"settings_value"'; then
    record "settings saved" PASS "the body of the web application, 0 days, a value out of range refused"
  else record "settings saved" FAIL "$SAVED / $REFUSED"
  fi
  # Les termes imposés : gardés, remplacés avant tout le reste, et refusés avec leur raison.
  SET=$(admin_json PUT /v1/terms '{"terms": [{"term": "Projet Alpha", "type": "ORG"},
    {"term": "KX-77", "type": "ID", "case_sensitive": true}]}')
  BAD=$(admin_json PUT /v1/terms '{"terms": [{"term": "Alpha", "type": "DATE"}]}')
  TERMS_TEXT=$(api -X POST -H "Content-Type: application/json" \
    --data '{"text": "Le PROJET-ALPHA suit la reference KX-77, pas kx-77."}' "$URL/v1/text/anonymize")
  LISTED=$(api -H "X-Pseudalis-Admin: $TOKEN" "$URL/v1/terms")
  if printf '%s' "$SET" | grep -q 'Projet Alpha' && printf '%s' "$BAD" | grep -q '"code":"terms_type"' &&
     printf '%s' "$TERMS_TEXT" | grep -q '\[\[ORG:' && printf '%s' "$TERMS_TEXT" | grep -q '\[\[ID:' &&
     ! printf '%s' "$TERMS_TEXT" | grep -q -e ALPHA -e KX-77 && printf '%s' "$TERMS_TEXT" | grep -q 'kx-77' &&
     printf '%s' "$LISTED" | grep -q '"max":100'; then
    record "imposed terms" PASS "replaced first, case kept for KX-77, a type refused"
  else record "imposed terms" FAIL "$SET / $BAD / $TERMS_TEXT"
  fi
  admin_json PUT /v1/terms '{"terms": []}' > /dev/null
  # L'espace des originaux : avec 1 Mo, les originaux les plus anciens font de la place aux plus
  # récents ; un espace plus petit que le plus gros fichier est refusé.
  SMALL=$(admin_json PUT /v1/settings '{"history_mb": 1}')
  admin_json PUT /v1/settings '{"max_file_mb": 1, "history_mb": 1}' > /dev/null
  for k in 1 2 3; do
    python3 -c 'import sys
sys.stdout.write(("Note %s de Jean Dupont, jean.dupont@exemple.fr\n" % sys.argv[1]) * 9000)' "$k" > "$WORK/gros$k.txt"
    api -X POST --data-binary @"$WORK/gros$k.txt" -o "$WORK/gros$k.anon.txt" "$URL/v1/anonymize?name=gros$k.txt"
  done
  api -X POST --data-binary @"$WORK/gros1.anon.txt" -D "$WORK/gros1.back.headers" -o "$WORK/gros1.back.txt" \
    "$URL/v1/restore?name=gros1.txt"
  api -X POST --data-binary @"$WORK/gros3.anon.txt" -o "$WORK/gros3.back.txt" "$URL/v1/restore?name=gros3.txt"
  SPACE=$(api "$URL/v1/info" | python3 -c 'import json, sys; h = json.load(sys.stdin)["history"]
print(h["bytes"] <= h["limit"])')
  admin_json PUT /v1/settings '{"history_mb": 10240, "max_file_mb": 150}' > /dev/null
  if cmp -s "$WORK/gros1.txt" "$WORK/gros1.back.txt" &&
     report_of "$WORK/gros1.back.headers" | grep -q '"reason":"space"' &&
     cmp -s "$WORK/gros3.txt" "$WORK/gros3.back.txt" && [ "$SPACE" = True ] &&
     printf '%s' "$SMALL" | grep -q '"field":"history_mb"'; then
    record "room for originals" PASS "the oldest from its tags (space), the newest from its original, 1 MB refused"
  else record "room for originals" FAIL "$(head -c 200 "$WORK/gros1.back.txt"), within the limit: $SPACE / $SMALL"
  fi
  # Une archive avec une image, son original oublié : restaurée à partir de ses étiquettes, sans son
  # image ; le fichier texte d'une image seule, oubliée, est refusé.
  python3 - "$WORK/album.zip" "$WORK/photo.png" <<'PY'
import base64, sys, zipfile
png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAAB"
                       "JRU5ErkJggg==")
with zipfile.ZipFile(sys.argv[1], "w") as z:
    z.writestr("lettre.txt", "Album de Jeanne Marchand, 12 rue des Lilas, 29000 Quimper.\n")
    z.writestr("photo.png", png)
open(sys.argv[2], "wb").write(png)
PY
  for f in album.zip photo.png; do
    api -X POST --data-binary @"$WORK/$f" -D "$WORK/$f.anon.headers" -o "$WORK/$f.anon" \
      "$URL/v1/anonymize?name=$f&record=true"
    ID=$(grep -i '^x-pseudalis-record:' "$WORK/$f.anon.headers" | tr -dc '0-9')
    api -X DELETE -H "X-Pseudalis-Admin: $TOKEN" -o /dev/null "$URL/v1/records/$ID"
  done
  api -X POST --data-binary @"$WORK/album.zip.anon" -D "$WORK/album.back.headers" -o "$WORK/album.back.zip" \
    "$URL/v1/restore?name=album.zip"
  PICTURE=$(api -X POST --data-binary @"$WORK/photo.png.anon" -o /dev/null -w '%{http_code}' \
    "$URL/v1/restore?name=photo.png.txt")
  api -X POST --data-binary @"$WORK/photo.png.anon" -o "$WORK/photo.back.txt" "$URL/v1/restore?name=photo.png.txt"
  ALBUM=$(python3 - "$WORK/album.back.zip" <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
print("%s|%s" % (",".join(z.namelist()), z.read("lettre.txt").decode() if "lettre.txt" in z.namelist() else ""))
PY
)
  if [ "$ALBUM" = "lettre.txt|Album de Jeanne Marchand, 12 rue des Lilas, 29000 Quimper." ] &&
     report_of "$WORK/album.back.headers" | grep -q '"pictures_removed":1' &&
     report_of "$WORK/album.back.headers" | grep -q '"original_forgotten"' && [ "$PICTURE" = 410 ] &&
     grep -q '"code":"picture_forgotten"' "$WORK/photo.back.txt"; then
    record "pictures forgotten" PASS "the archive from its tags without its picture, the picture alone refused (410)"
  else record "pictures forgotten" FAIL "$ALBUM / picture $PICTURE / $(head -c 200 "$WORK/photo.back.txt")"
  fi
  # Un texte écrit ailleurs à partir de valeurs anonymisées, qui cite une image : restauré à partir de ses
  # étiquettes sans le marqueur ; anonymisé et enregistré comme original, il revient avec ses valeurs et
  # sans le marqueur, lui aussi.
  printf 'Lettre de Jeanne Marchand.\n' > "$WORK/source.txt"
  api -X POST --data-binary @"$WORK/source.txt" -o "$WORK/source.anon" \
    "$URL/v1/anonymize?name=source.txt&record=false"
  printf 'Réponse : %s [Image@0123456789abcdef : Photo]\n' "$(cat "$WORK/source.anon")" > "$WORK/reponse.txt"
  api -X POST --data-binary @"$WORK/reponse.txt" -D "$WORK/reponse.back.headers" -o "$WORK/reponse.back.txt" \
    "$URL/v1/restore?name=reponse.txt"
  api -X POST --data-binary @"$WORK/reponse.txt" -o "$WORK/reponse.anon" \
    "$URL/v1/anonymize?name=reponse.txt&record=true"
  api -X POST --data-binary @"$WORK/reponse.anon" -o "$WORK/reponse.again.txt" "$URL/v1/restore?name=reponse.txt"
  if [ "$(cat "$WORK/reponse.back.txt")" = "Réponse : Lettre de Jeanne Marchand." ] &&
     report_of "$WORK/reponse.back.headers" | grep -q '"pictures_removed":1' &&
     [ "$(cat "$WORK/reponse.again.txt")" = "Réponse : Lettre de Jeanne Marchand." ]; then
    record "written elsewhere" PASS "from its tags without the marker, recorded: from its original, values back"
  else record "written elsewhere" FAIL \
    "$(head -c 120 "$WORK/reponse.back.txt") / $(head -c 120 "$WORK/reponse.again.txt")"
  fi
  # Sans étiquettes : des substituts seuls, l'original grâce à sa fiche ; une fois oublié, rien ne revient
  # (410).
  printf 'nom;prenom;email\nMarchand;Jeanne;jeanne.marchand@exemple.fr\n' > "$WORK/bare.csv"
  api -X POST --data-binary @"$WORK/bare.csv" -D "$WORK/bare.headers" -o "$WORK/bare.anon.csv" \
    "$URL/v1/anonymize?name=bare.csv&tags=false&record=true"
  BARE_ID=$(grep -i '^x-pseudalis-record:' "$WORK/bare.headers" | tr -dc '0-9')
  api -X POST --data-binary @"$WORK/bare.anon.csv" -o "$WORK/bare.back.csv" "$URL/v1/restore?name=bare.csv"
  BARE_TAGS=$(records | python3 -c 'import json, sys
print(next((r["tags"] for r in json.load(sys.stdin)["records"] if str(r["id"]) == sys.argv[1]), "none"))' "$BARE_ID")
  api -X DELETE -H "X-Pseudalis-Admin: $TOKEN" -o /dev/null "$URL/v1/records/$BARE_ID"
  BARE_GONE=$(api -X POST --data-binary @"$WORK/bare.anon.csv" -o "$WORK/bare.gone.json" -w '%{http_code}' \
    "$URL/v1/restore?name=bare.csv")
  if ! grep -q -e '\[\[' -e Marchand "$WORK/bare.anon.csv" && cmp -s "$WORK/bare.csv" "$WORK/bare.back.csv" &&
     [ "$BARE_TAGS" = False ] && [ "$BARE_GONE" = 410 ] && grep -q '"code":"tagless_forgotten"' "$WORK/bare.gone.json"
  then
    record "files without tags" PASS "from the record, the record says so, forgotten: refused (410)"
  else record "files without tags" FAIL "$(head -c 100 "$WORK/bare.anon.csv") / tags $BARE_TAGS / $BARE_GONE"
  fi
  # Sans étiquettes et sans fiche : rien ne peut revenir, ce que dit le rapport.
  printf 'nom;prenom;email\nLemoine;Paul;paul.lemoine@exemple.fr\n' > "$WORK/bare2.csv"
  api -X POST --data-binary @"$WORK/bare2.csv" -D "$WORK/bare2.headers" -o "$WORK/bare2.anon.csv" \
    "$URL/v1/anonymize?name=bare2.csv&tags=false&record=false"
  BARE2=$(api -X POST --data-binary @"$WORK/bare2.anon.csv" -o /dev/null -w '%{http_code}' \
    "$URL/v1/restore?name=bare2.csv")
  if [ "$BARE2" = 404 ] && report_of "$WORK/bare2.headers" | grep -q '"restorable":false'; then
    record "without tags nor record" PASS "nothing kept, not restorable (404)"
  else record "without tags nor record" FAIL "$BARE2 / $(report_of "$WORK/bare2.headers" | head -c 160)"
  fi
  # Un texte sans étiquettes : ses substituts listés, qui le rendent quand ils l'accompagnent.
  BARE_TEXT=$(api -X POST -H "Content-Type: application/json" --data "{\"text\": \"$TEXT\", \"tags\": false}" \
    "$URL/v1/text/anonymize")
  BARE_BACK=$(printf '%s' "$BARE_TEXT" | python3 -c 'import json, sys
j = json.load(sys.stdin)
print(json.dumps({"text": j["text"], "substitutions": j.get("substitutions", [])}))' |
    api -X POST -H "Content-Type: application/json" --data @- "$URL/v1/text/restore" |
    python3 -c 'import json, sys; print(json.load(sys.stdin)["text"])')
  BARE_WRITTEN=$(printf '%s' "$BARE_TEXT" | python3 -c 'import json, sys; print(json.load(sys.stdin)["text"])')
  if ! printf '%s' "$BARE_WRITTEN" | grep -q -e '\[\[' -e 'Dupont' && [ "$BARE_BACK" = "$TEXT" ]; then
    record "text without tags" PASS "substitutes alone, given back with their list"
  else record "text without tags" FAIL "$(printf '%s' "$BARE_WRITTEN" | head -c 160) / $BARE_BACK"
  fi
  # Protégé deux fois, par un moteur plus récent par exemple : avec étiquettes et sa fiche oubliée, puis avec
  # ou sans étiquettes ; sans étiquettes avec sa fiche gardée, puis avec ou sans ; les valeurs des deux
  # reviennent sans avertissement.
  printf 'Dossier de Paul Lemaire, paul.lemaire@exemple.fr.\n' > "$WORK/twice.txt"
  api -X POST --data-binary @"$WORK/twice.txt" -D "$WORK/twice1.headers" -o "$WORK/twice1.txt" \
    "$URL/v1/anonymize?name=twice.txt&tags=true"
  T1=$(grep -i '^x-pseudalis-record:' "$WORK/twice1.headers" | tr -dc '0-9')
  api -X DELETE -H "X-Pseudalis-Admin: $TOKEN" -o /dev/null "$URL/v1/records/$T1"
  printf 'Visa de Hélène Martin.\n' >> "$WORK/twice1.txt"
  api -X POST --data-binary @"$WORK/twice1.txt" -o "$WORK/twice2.txt" "$URL/v1/anonymize?name=twice.txt&tags=true"
  api -X POST --data-binary @"$WORK/twice2.txt" -D "$WORK/twice.back.headers" -o "$WORK/twice.back.txt" \
    "$URL/v1/restore?name=twice.txt"
  api -X POST --data-binary @"$WORK/twice1.txt" -o "$WORK/bare4.txt" "$URL/v1/anonymize?name=twice.txt&tags=false"
  api -X POST --data-binary @"$WORK/bare4.txt" -o "$WORK/bare4.back.txt" "$URL/v1/restore?name=twice.txt"
  api -X POST --data-binary @"$WORK/twice.txt" -o "$WORK/bare1.txt" "$URL/v1/anonymize?name=twice.txt&tags=false"
  api -X POST --data-binary @"$WORK/bare1.txt" -o "$WORK/bare2.txt" "$URL/v1/anonymize?name=twice.txt&tags=true"
  api -X POST --data-binary @"$WORK/bare1.txt" -o "$WORK/bare3.txt" "$URL/v1/anonymize?name=twice.txt&tags=false"
  api -X POST --data-binary @"$WORK/bare2.txt" -o "$WORK/bare2.back.txt" "$URL/v1/restore?name=twice.txt"
  api -X POST --data-binary @"$WORK/bare3.txt" -o "$WORK/bare3.back.txt" "$URL/v1/restore?name=twice.txt"
  TWICE=$(printf 'Dossier de Paul Lemaire, paul.lemaire@exemple.fr.\nVisa de Hélène Martin.')
  if [ "$(cat "$WORK/twice.back.txt")" = "$TWICE" ] &&
     cmp -s "$WORK/twice.txt" "$WORK/bare2.back.txt" && cmp -s "$WORK/twice.txt" "$WORK/bare3.back.txt" &&
     ! grep -q -e '\[\[' -e Lemaire -e Martin "$WORK/bare4.txt" && [ "$(cat "$WORK/bare4.back.txt")" = "$TWICE" ] &&
     ! grep -q '"code"' "$WORK/twice.back.txt"; then
    record "protected twice" PASS "tags then with or without, without tags then with or without: the values of both"
  else record "protected twice" FAIL "$(head -c 120 "$WORK/twice.back.txt") / $(head -c 80 "$WORK/bare2.back.txt")"
  fi
  # Le lecteur sans étiquettes, comme le disent les réglages : son fichier ne contient aucune étiquette,
  # son original revient.
  admin_json PUT /v1/settings '{"tags": false}' > /dev/null
  printf 'Courrier de Paul Lemaire, 06 11 22 33 44.\n' > "$WORK/courrier.txt"
  cp "$WORK/courrier.txt" "$DRIVE/courrier.txt"
  courrier_done() {
    ! cmp -s "$WORK/courrier.txt" "$DRIVE/courrier.txt" && [ -n "$(record_of "$DRIVE/courrier.txt")" ]
  }
  wait_for 60 courrier_done
  cp "$DRIVE/courrier.txt" "$WORK/courrier.anon.txt"
  restore_to "$WORK/courrier.anon.txt" "$WORK/courrier.back.txt"
  DRIVE_TAGS=$(api "$URL/v1/info" | python3 -c 'import json, sys; print(json.load(sys.stdin)["tags"])')
  # Modifié sur le lecteur, comme un éditeur l'enregistre : protégé à nouveau, ses valeurs protégées
  # auparavant reviennent aussi.
  { cat "$WORK/courrier.anon.txt"; printf 'Visa de Hélène Martin.\n'; } > "$WORK/courrier.edit.txt"
  cp "$WORK/courrier.edit.txt" "$DRIVE/courrier.txt"
  edited_done() {
    ! cmp -s "$WORK/courrier.edit.txt" "$DRIVE/courrier.txt" && [ -n "$(record_of "$DRIVE/courrier.txt")" ]
  }
  wait_for 60 edited_done
  cp "$DRIVE/courrier.txt" "$WORK/courrier.anon2.txt"
  restore_to "$WORK/courrier.anon2.txt" "$WORK/courrier.back2.txt"
  admin_json PUT /v1/settings '{"tags": true}' > /dev/null
  if ! grep -q -e '\[\[' -e Lemaire "$WORK/courrier.anon.txt" &&
     cmp -s "$WORK/courrier.txt" "$WORK/courrier.back.txt" && [ "$DRIVE_TAGS" = False ] &&
     ! grep -q -e '\[\[' -e Martin "$WORK/courrier.anon2.txt" &&
     [ "$(cat "$WORK/courrier.back2.txt")" = "$(printf 'Courrier de Paul Lemaire, 06 11 22 33 44.\nVisa de %s.' \
       'Hélène Martin')" ]
  then
    record "drive without tags" PASS "the setting followed, the original back, edited: the values of both"
  else record "drive without tags" FAIL "$(head -c 100 "$WORK/courrier.anon.txt") / $DRIVE_TAGS / \
    $(head -c 100 "$WORK/courrier.back2.txt")"
  fi
else
  tail -20 "$WORK/service.log" > "$WORK/service_start.log"
  record "service start" FAIL "no answer on $URL, log in $WORK/service_start.log"
fi
kill "$SERVICE" 2>/dev/null
wait "$SERVICE" 2>/dev/null

printf '%s\n' "${RESULTS[@]}"
echo "$FAILED failed, work folder $WORK"
exit $FAILED
