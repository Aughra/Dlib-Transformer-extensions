#!/bin/bash
# Les tests de vitesse et de justesse de Gulliver contre les autres moteurs, une fois par semaine
# (fork pour macOS, 2026-11-21, demande de l'humain).  Lancé par l'agent launchd de
# macos/bench-hebdo/ (dimanche, 4 h), ou à la main :
#
#   bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh
#
# Il attend une machine au repos (charge sur une minute sous BENCH_MAX_LOAD, 3 par défaut ; un essai
# par heure, 6 au plus), reconstruit Gulliver (build/macos-gulliver), lance bench_moteurs.sh et, s'il
# existe, justesse_moteurs.sh, puis copie les tableaux dans build/bench/hebdo/ : « dernier.md » (lu
# par le carnet, rubrique « Tests de vitesse ») et un historique daté.  Rien n'est commité : le
# carnet lit ces fichiers sur le disque.

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TOOLS=$ROOT/dlib/dlib/external/gulliver/tools
HEBDO=$ROOT/build/bench/hebdo
MAX_LOAD=${BENCH_MAX_LOAD:-3}
mkdir -p "$HEBDO"
LOCK=$HEBDO/.en-cours
exec >> "$HEBDO/hebdo.log" 2>&1
echo "=== $(date '+%Y-%m-%d %H:%M') début"

# Une seule campagne à la fois.
if ! mkdir "$LOCK" 2>/dev/null; then echo "une campagne tourne déjà, abandon"; exit 0; fi
trap 'rmdir "$LOCK"' EXIT

# Le PATH de launchd est court : llama-bench, cmake et ninja viennent de Homebrew.
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin

load1() { sysctl -n vm.loadavg | awk '{print $2}'; }
for try in 1 2 3 4 5 6; do
  l=$(load1)
  if awk -v l="$l" -v m="$MAX_LOAD" 'BEGIN { exit !(l < m) }'; then break; fi
  echo "charge $l, au-dessus de $MAX_LOAD : nouvel essai dans une heure ($try/6)"
  [ $try -eq 6 ] && { echo "machine jamais au repos, campagne reportée à la semaine suivante"; exit 0; }
  /bin/sleep 3600
done

echo "charge $(load1) : reconstruction de Gulliver"
cmake --build "$ROOT/build/macos-gulliver" --target bench_runtime > "$HEBDO/build.log" 2>&1 \
  || { echo "build en échec, voir $HEBDO/build.log"; exit 1; }

STAMP=$(date +%Y-%m-%d)
: > "$HEBDO/dernier.tmp"
for script in bench_moteurs.sh justesse_moteurs.sh; do
  [ -f "$TOOLS/$script" ] || continue
  echo "lancement de $script"
  table=$(bash "$TOOLS/$script" 2 | sed -n 's/^table: //p' | tail -1)
  if [ -n "$table" ] && [ -f "$table" ]; then
    cat "$table" >> "$HEBDO/dernier.tmp"; echo >> "$HEBDO/dernier.tmp"
  else
    echo "$script n'a pas produit de tableau"
  fi
done
if [ -s "$HEBDO/dernier.tmp" ]; then
  mv "$HEBDO/dernier.tmp" "$HEBDO/dernier.md"
  cp "$HEBDO/dernier.md" "$HEBDO/$STAMP.md"
  echo "=== $(date '+%Y-%m-%d %H:%M') fini : $HEBDO/dernier.md"
else
  rm -f "$HEBDO/dernier.tmp"; echo "=== aucune mesure"
fi
