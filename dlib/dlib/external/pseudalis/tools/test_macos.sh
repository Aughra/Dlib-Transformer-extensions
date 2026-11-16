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
  # Des pseudonymes sans retour (2.3.0) : ni la valeur, ni balise ; le contenu de $1 donne « pseudonymized ».
  pseudonymized() { if grep -q -i -e dupont -e '\[\[' "$1"; then echo "clear: $(head -c 160 "$1")"; else echo pseudonymized; fi; }
  TEXT='Jean Dupont habite 12 rue de la Paix, 75002 Paris, jean.dupont@exemple.fr'
  api -X POST -H "Content-Type: application/json" --data "{\"text\": \"$TEXT\"}" "$URL/v1/text/anonymize" |
    python3 -c 'import json, sys; print(json.load(sys.stdin)["text"])' > "$WORK/text.anon.txt"
  step "text anonymized" '^pseudonymized$' pseudonymized "$WORK/text.anon.txt"
  # Un texte protégé ne revient pas : rien ne garde ses valeurs.
  PROTECTED=$(python3 -c 'import json, sys; print(json.dumps({"text": open(sys.argv[1]).read().strip()}))' "$WORK/text.anon.txt")
  api -X POST -H "Content-Type: application/json" --data "$PROTECTED" "$URL/v1/text/restore" |
    python3 -c 'import json, sys; print(json.load(sys.stdin).get("text", ""))' > "$WORK/text.back.txt"
  step "text not restorable" '^pseudonymized$' pseudonymized "$WORK/text.back.txt"
  # Le relais d'Albireo (3.1.0) : sous la clé de séance, les mêmes pseudonymes d'un texte à l'autre,
  # rendus avec leurs valeurs ; sans session, aucune valeur.
  for n in 1 2; do
    api -X POST -H "Content-Type: application/json" --data "{\"text\": \"$TEXT\", \"session\": true}" \
      "$URL/v1/text/anonymize" > "$WORK/session.$n.json"
  done
  api -X POST -H "Content-Type: application/json" --data "{\"text\": \"$TEXT\"}" "$URL/v1/text/anonymize" \
    > "$WORK/session.none.json"
  step "relay session" '^session ok' python3 -c '
import json, sys
a, b, c = (json.load(open(p)) for p in sys.argv[1:4])
values = {v["substitute"]: v["value"] for v in a.get("values", [])}
back = a["text"]
for s in sorted(values, key=len, reverse=True): back = back.replace(s, values[s])
ok = ("Dupont" not in a["text"] and a["text"] == b["text"] and "Jean Dupont" in values.values()
      and back == sys.argv[4] and "values" not in c and c["text"] != a["text"])
print(("session ok " if ok else "session wrong ") + str(len(values)) + " values, " + a["text"][:80])
' "$WORK/session.1.json" "$WORK/session.2.json" "$WORK/session.none.json" "$TEXT"
  printf 'nom;prenom;email;telephone\nDupont;Jean;jean.dupont@exemple.fr;06 12 34 56 78\n' > "$WORK/sample.csv"
  api -X POST --data-binary @"$WORK/sample.csv" -o "$WORK/sample.anon.csv" "$URL/v1/anonymize?name=sample.csv"
  step "file anonymized" '^pseudonymized$' pseudonymized "$WORK/sample.anon.csv"
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
  api -X POST --data-binary @"$WORK/journal.log2" -o "$WORK/journal.anon" "$URL/v1/anonymize?name=journal.log2"
  step "text of no kind" '^pseudonymized$' pseudonymized "$WORK/journal.anon"
  printf 'Jean Dupont\0\001\002' > "$WORK/donnees.bin"
  step "binary of no kind" '"code":"unsupported"' api -X POST --data-binary @"$WORK/donnees.bin" \
    "$URL/v1/anonymize?name=donnees.bin"
  printf 'From: Jean Dupont\r\nContent-Transfer-Encoding: base64\r\n\r\nSmVhbiBEdXBvbnQ=\r\n' > "$WORK/message.eml"
  step "encoded of no kind" '"code":"encoded_content"' api -X POST --data-binary @"$WORK/message.eml" \
    "$URL/v1/anonymize?name=message.eml"
  # Un nom seul, celui d'un dossier : protégé, et sans retour.
  name_call() { # route nom
    api -X POST -H "Content-Type: application/json" --data "$(python3 -c 'import json, sys
print(json.dumps({"name": sys.argv[1]}))' "$2")" "$URL/v1/names/$1" |
      python3 -c 'import json, sys; print(json.load(sys.stdin).get("name", ""))'
  }
  NAMED=$(name_call anonymize "Dossier jean.dupont@exemple.fr")
  if [ -n "$NAMED" ] && ! printf '%s' "$NAMED" | grep -q -i dupont &&
     [ "$(name_call restore "$NAMED")" = "$NAMED" ]; then
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
  # Attendre que le lecteur ait vu le dernier changement et l'ait traité : deux parcours de plus que maintenant,
  # puis le repos.  Pour les étapes qui vérifient qu'il ne se passe rien, à la place d'une attente fixe.
  drive_state() { api "$URL/v1/drive" | python3 -c 'import json, sys
d = json.load(sys.stdin); print(d.get("scans", 0), 1 if d.get("idle") else 0)'; }
  settled() { set -- "$1" $(drive_state); [ "${2:-0}" -ge $(( $1 + 2 )) ] && [ "$3" = 1 ]; }
  settle() { wait_for 60 settled "$(drive_state | cut -d' ' -f1)"; }
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
  # En mode économie d'énergie, protéger cette archive de 420 Ko prend plus de deux minutes ; renommée avant,
  # l'archive d'origine serait refusée.
  ARCHIVE_DONE=yes
  wait_for 300 archive_done || ARCHIVE_DONE="no, still unprotected after 300 s"
  BIG=$(stat -f %z "$DRIVE/grosse.zip" 2>/dev/null || echo 0)
  mv "$DRIVE/grosse.zip" "$DRIVE/grosse.zip.bak"
  settle
  if [ "$ARCHIVE_DONE" = yes ] && [ -f "$DRIVE/grosse.zip.bak" ] && [ ! -e "$HOME_DIR/refused/files/grosse.zip.bak" ] &&
     [ "$BIG" -gt 65536 ] && [ -n "$(bare)" ] && ! cmp -s "$WORK/grosse.zip" "$(bare)"; then
    record "drive signatures" PASS "a protected archive renamed stays, an archive without extension is taken"
  else
    record "drive signatures" FAIL "archive protected: $ARCHIVE_DONE; size $BIG, $(find "$DRIVE" "$HOME_DIR/refused" -name '*grosse*' -o -name '*sans*' |
      tr '\n' ' ')"
  fi
  ANON=$(find "$DRIVE" -maxdepth 1 -type f -name 'Lettre*' | head -1)
  step "drive content" '^pseudonymized$' pseudonymized "$ANON"
  api -X POST --data-binary @"$ANON" -D "$WORK/back.headers" -o "$WORK/lettre.back.txt" \
    "$URL/v1/restore?name=$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' \
      "$(basename "$ANON")")"
  BACK_NAME=$(grep -i '^x-pseudalis-name:' "$WORK/back.headers" | cut -d: -f2- | tr -d '\r ' |
    python3 -c 'import sys, urllib.parse; print(urllib.parse.unquote(sys.stdin.read().strip()))')
  FOLDER=$(find "$DRIVE" -mindepth 1 -maxdepth 1 -type d | head -1)
  if cmp -s "$WORK/lettre.txt" "$WORK/lettre.back.txt" && [ "$BACK_NAME" = "Lettre jean.dupont@exemple.fr.txt" ] &&
     ! basename "$FOLDER" | grep -q -i dupont; then
    record "drive restored" PASS "the original and its name; the name of the folder stays pseudonymized"
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
     ! grep -q 'Paul Martin' "$WORK/reste.apres.txt" && grep -q '"code"' "$WORK/reste.apres.txt"
  then
    record "extracted restored" PASS "from their originals; the file outside refused once the archive forgotten"
  else record "extracted restored" FAIL "see lettre.extrait.txt, reste.back.txt, lettre.apres.txt, reste.apres.txt"
  fi
  # L'archive oubliée revient sur le lecteur : elle reste telle quelle, sans nouvel original.
  COUNT=$(total)
  cp "$WORK/envoi.anon.zip" "$DRIVE/revenu.zip"
  settle
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
  settle
  rm -f "$DRIVE/copies.txt"
  settle
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
  LANGUAGE_ACCEPTED=1
  for language in en fr; do
    GOOD_LANGUAGE=$(admin_json PUT /v1/settings "{\"language\": \"$language\"}")
    if ! printf '%s' "$GOOD_LANGUAGE" | grep -q "\"language\":\"$language\""; then LANGUAGE_ACCEPTED=0; fi
  done
  RESET_LANGUAGE=$(admin_json PUT /v1/settings '{"language": "auto"}')
  if ! printf '%s' "$RESET_LANGUAGE" | grep -q '"language":"auto"'; then LANGUAGE_ACCEPTED=0; fi
  LANGUAGE_REFUSED=1
  for language in de it es; do
    BAD_LANGUAGE=$(admin_json PUT /v1/settings "{\"language\": \"$language\"}")
    if ! printf '%s' "$BAD_LANGUAGE" | grep -q '"code":"settings_value"'; then LANGUAGE_REFUSED=0; fi
  done
  if printf '%s' "$SAVED" | grep -q '"removed_days":0' && printf '%s' "$SAVED" | grep -q '"restart":false' &&
     printf '%s' "$REFUSED" | grep -q '"code":"settings_value"' && [ "$LANGUAGE_ACCEPTED" = 1 ] &&
     [ "$LANGUAGE_REFUSED" = 1 ]; then
    record "settings saved" PASS "the body of the web application, 0 days, en/fr accepted and de/it/es refused"
  else record "settings saved" FAIL "$SAVED / $REFUSED / $RESET_LANGUAGE / language values accepted: $LANGUAGE_ACCEPTED, refused: $LANGUAGE_REFUSED"
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
  # Les colonnes imposées (3.2.0, repris de l'original 1.0.8 à 1.0.10) : sous leurs noms, un titre au-dessus de
  # l'en-tête, des pseudonymes seuls à la place des valeurs, les autres valeurs détectées ; l'original revient par
  # son enregistrement.  Pas de retour depuis un fichier modifié : les pseudonymes du fork ne reviennent pas.
  COLS=$(admin_json PUT /v1/columns '{"columns": [{"column": "Nom", "type": "PERSON"},
    {"column": "Courriel", "type": "EMAIL"}]}')
  BAD_COLS=$(admin_json PUT /v1/columns '{"columns": [{"column": "Nom", "type": "NONE"}]}')
  LISTED_COLS=$(api "$URL/v1/columns")
  printf 'Export du 12/03/2024\nNOM;Courriel;Ville\nLemoine;paul.lemoine@exemple.fr;Rennes\n' > "$WORK/export.csv"
  api -X POST --data-binary @"$WORK/export.csv" -D "$WORK/export.headers" -o "$WORK/export.anon.csv" \
    "$URL/v1/anonymize?name=export.csv&record=true"
  restore_to "$WORK/export.anon.csv" "$WORK/export.back.csv"
  if printf '%s' "$COLS" | grep -q '"column":"Nom"' && printf '%s' "$BAD_COLS" | grep -q '"code":"columns_type"' &&
     printf '%s' "$LISTED_COLS" | grep -q '"max":100' &&
     sed -n 2p "$WORK/export.anon.csv" | grep -q '^NOM;Courriel;Ville$' &&
     ! grep -q -e Lemoine -e paul.lemoine -e '\[\[' "$WORK/export.anon.csv" &&
     [ "$(sed -n 3p "$WORK/export.anon.csv" | awk -F';' '{print NF}')" = 3 ] &&
     sed -n 3p "$WORK/export.anon.csv" | awk -F';' '{print $2}' | grep -q '@' &&
     cmp -s "$WORK/export.csv" "$WORK/export.back.csv"
  then
    record "imposed columns" PASS "pseudonyms below their names, the header under a title kept, the original back"
  else record "imposed columns" FAIL "$(sed -n 3p "$WORK/export.anon.csv" | head -c 100) / $BAD_COLS / $LISTED_COLS"
  fi
  # Une colonne IP prend les deux formes d'adresse, le mot-clé d'une forme n'étant pas un type de colonne : chaque
  # adresse prend le pseudonyme de sa forme, sa longueur de préfixe et son port gardés ; l'original revient.
  BAD_IP=$(admin_json PUT /v1/columns '{"columns": [{"column": "ip_address", "type": "IPV4"}]}')
  IP_COLS=$(admin_json PUT /v1/columns '{"columns": [{"column": "ip_address", "type": "IP"}]}')
  printf 'host;ip_address\nweb;192.168.1.5\ndb;2001:db8::1\nnet;10.0.0.0/8\napi;[2001:db8::1]:443\n' \
    > "$WORK/hosts.csv"
  api -X POST --data-binary @"$WORK/hosts.csv" -D "$WORK/hosts.headers" -o "$WORK/hosts.anon.csv" \
    "$URL/v1/anonymize?name=hosts.csv&record=true"
  restore_to "$WORK/hosts.anon.csv" "$WORK/hosts.back.csv"
  admin_json PUT /v1/columns '{"columns": []}' > /dev/null
  if printf '%s' "$BAD_IP" | grep -q '"code":"columns_type"' && printf '%s' "$IP_COLS" | grep -q '"type":"IP"' &&
     ! grep -q -F -e 192.168.1.5 -e 2001:db8::1 -e 10.0.0.0 "$WORK/hosts.anon.csv" &&
     grep -q '/8$' "$WORK/hosts.anon.csv" && grep -q ']:443$' "$WORK/hosts.anon.csv" &&
     sed -n 3p "$WORK/hosts.anon.csv" | grep -q ':' && ! grep -q -F '[[' "$WORK/hosts.anon.csv" &&
     cmp -s "$WORK/hosts.csv" "$WORK/hosts.back.csv"
  then
    record "IP column" PASS "IPv4 and IPv6 in one column, prefix and port kept, the original back"
  else record "IP column" FAIL "$(head -c 160 "$WORK/hosts.anon.csv") / $BAD_IP / $IP_COLS"
  fi
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
  if ! grep -q Dupont "$WORK/gros1.back.txt" && grep -q '"code":"tagless_forgotten"' "$WORK/gros1.back.txt" &&
     cmp -s "$WORK/gros3.txt" "$WORK/gros3.back.txt" && [ "$SPACE" = True ] &&
     printf '%s' "$SMALL" | grep -q '"field":"history_mb"'; then
    record "room for originals" PASS "the oldest forgotten (refused), the newest from its original, 1 MB refused"
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
  if ! grep -q Marchand "$WORK/album.back.zip" && grep -q '"code"' "$WORK/album.back.zip" && [ "$PICTURE" = 410 ] &&
     grep -q '"code":"picture_forgotten"' "$WORK/photo.back.txt"; then
    record "pictures forgotten" PASS "the archive and the picture alone refused once forgotten"
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
  if ! grep -q Marchand "$WORK/reponse.back.txt" && ! grep -q Marchand "$WORK/reponse.again.txt" &&
     grep -q '^Réponse : Lettre de ' "$WORK/reponse.again.txt"; then
    record "written elsewhere" PASS "no value back from pseudonyms, recorded or not"
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
  if ! printf '%s' "$BARE_WRITTEN" | grep -q -e '\[\[' -e 'Dupont' && ! printf '%s' "$BARE_BACK" | grep -q Dupont; then
    record "text without tags" PASS "pseudonyms alone, not given back even with their list"
  else record "text without tags" FAIL "$(printf '%s' "$BARE_WRITTEN" | head -c 160) / $BARE_BACK"
  fi
  # Protégé deux fois par l'interface (2.3.0) : la valeur ajoutée est protégée et la restauration rend l'original de
  # la seconde protection.  Sans fiche qui les nomme, ses premiers pseudonymes peuvent changer ; sur le lecteur, la
  # fiche du fichier les garde tels quels (étape suivante).
  printf 'Dossier de Paul Lemaire, paul.lemaire@exemple.fr.\n' > "$WORK/twice.txt"
  api -X POST --data-binary @"$WORK/twice.txt" -o "$WORK/twice1.txt" "$URL/v1/anonymize?name=twice.txt"
  { cat "$WORK/twice1.txt"; printf 'Visa de Hélène Martin.\n'; } > "$WORK/twice1b.txt"
  api -X POST --data-binary @"$WORK/twice1b.txt" -o "$WORK/twice2.txt" "$URL/v1/anonymize?name=twice.txt"
  api -X POST --data-binary @"$WORK/twice2.txt" -o "$WORK/twice.back.txt" "$URL/v1/restore?name=twice.txt"
  if ! grep -q -e '\[\[' -e Lemaire -e Martin "$WORK/twice2.txt" && cmp -s "$WORK/twice1b.txt" "$WORK/twice.back.txt"; then
    record "protected twice" PASS "the new value protected, the original of the second protection back"
  else record "protected twice" FAIL "$(head -c 120 "$WORK/twice2.txt") / $(head -c 80 "$WORK/twice.back.txt")"
  fi
  # Le lecteur : des pseudonymes seuls, l'original de sa fiche ; modifié sur le lecteur, protégé de nouveau sans
  # toucher à ses pseudonymes, et son original est la version modifiée.
  printf 'Courrier de Paul Lemaire, 06 11 22 33 44.\n' > "$WORK/courrier.txt"
  cp "$WORK/courrier.txt" "$DRIVE/courrier.txt"
  courrier_done() {
    ! cmp -s "$WORK/courrier.txt" "$DRIVE/courrier.txt" && [ -n "$(record_of "$DRIVE/courrier.txt")" ]
  }
  wait_for 60 courrier_done
  cp "$DRIVE/courrier.txt" "$WORK/courrier.anon.txt"
  restore_to "$WORK/courrier.anon.txt" "$WORK/courrier.back.txt"
  DRIVE_TAGS=$(api "$URL/v1/info" | python3 -c 'import json, sys; print(json.load(sys.stdin)["tags"])')
  { cat "$WORK/courrier.anon.txt"; printf 'Visa de Hélène Martin.\n'; } > "$WORK/courrier.edit.txt"
  cp "$WORK/courrier.edit.txt" "$DRIVE/courrier.txt"
  edited_done() {
    ! cmp -s "$WORK/courrier.edit.txt" "$DRIVE/courrier.txt" && [ -n "$(record_of "$DRIVE/courrier.txt")" ]
  }
  wait_for 60 edited_done
  cp "$DRIVE/courrier.txt" "$WORK/courrier.anon2.txt"
  restore_to "$WORK/courrier.anon2.txt" "$WORK/courrier.back2.txt"
  if ! grep -q -e '\[\[' -e Lemaire "$WORK/courrier.anon.txt" &&
     cmp -s "$WORK/courrier.txt" "$WORK/courrier.back.txt" && [ "$DRIVE_TAGS" = False ] &&
     ! grep -q -e '\[\[' -e Martin "$WORK/courrier.anon2.txt" &&
     [ "$(head -1 "$WORK/courrier.anon2.txt")" = "$(head -1 "$WORK/courrier.anon.txt")" ] &&
     cmp -s "$WORK/courrier.edit.txt" "$WORK/courrier.back2.txt"
  then
    record "drive pseudonyms" PASS "no tag, the original back, edited: pseudonyms kept, the edited version back"
  else record "drive pseudonyms" FAIL "$(head -c 100 "$WORK/courrier.anon.txt") / $DRIVE_TAGS / \
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
