#!/bin/bash
# Les modèles de Gulliver mesurés une fois par semaine (fork pour macOS, 2026-11-21, demande de l'humain ; depuis le
# 2026-11-22, Gulliver sur tous ses modèles installés, avec llama.cpp et MLX sur le modèle de référence, comparés au
# choix de la semaine dernière).  Lancé par l'agent launchd de macos/bench-hebdo/ (lundi, 8 h, arrêt à 19 h :
# BENCH_DEADLINE), ou à la main :
#
#   bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh
#
# Il attend une machine au repos (charge sur une minute sous BENCH_MAX_LOAD, 3 par défaut ; un essai
# par heure, 6 au plus), reconstruit Gulliver (build/macos-gulliver), lance bench_modeles.sh (qui écrit
# aussi build/bench/hebdo/conseil.json, lu par l'onglet Modèles), puis met la mesure de la semaine, sous un titre daté, en tête de
# build/bench/hebdo/mesures.md (lu par le carnet, rubrique « Tests de vitesse » : les semaines de
# la plus récente à la plus ancienne), avec une copie datée.  Rien n'est commité : le carnet lit
# ces fichiers sur le disque.

ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd)
TOOLS=$ROOT/dlib/dlib/external/gulliver/tools
HEBDO=$ROOT/build/bench/hebdo
MAX_LOAD=${BENCH_MAX_LOAD:-3}
mkdir -p "$HEBDO"
LOCK=$HEBDO/.en-cours
exec >> "$HEBDO/hebdo.log" 2>&1
echo "=== $(date '+%Y-%m-%d %H:%M') début"

# Jamais le samedi ni le dimanche de 9 h à 21 h : l'humain se sert alors du Mac (launchd lance au réveil une tâche
# manquée pendant le sommeil du Mac ; elle attendra la semaine suivante).
if [ "$(date +%u)" -ge 6 ] && [ "$(date +%H)" -ge 9 ] && [ "$(date +%H)" -lt 21 ]; then
  echo "samedi ou dimanche en journée : pas de mesure"; exit 0
fi

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
cmake --build "$ROOT/build/macos-gulliver" --target bench_runtime perplexity_runtime > "$HEBDO/build.log" 2>&1 \
  || { echo "build en échec, voir $HEBDO/build.log"; exit 1; }

STAMP=$(date +%Y-%m-%d)
WHEN=$(LC_ALL=fr_FR.UTF-8 date '+%A %e %B %Y à %H h %M' | sed 's/  / /')
WEEK=$HEBDO/semaine.tmp
echo "lancement de bench_modeles.sh"
table=$(bash "$TOOLS/bench_modeles.sh" 2 | sed -n 's/^table: //p' | tail -1)
if [ -z "$table" ] || [ ! -s "$table" ]; then echo "=== aucune mesure"; exit 0; fi
{ echo "## Mesure du $WHEN"; echo; cat "$table"; } > "$WEEK"

# La page du carnet : les semaines de la plus récente à la plus ancienne.  historique.md garde
# les semaines seules ; mesures.md y ajoute l'en-tête.
cp "$WEEK" "$HEBDO/$STAMP.md"
{ cat "$WEEK"; [ -f "$HEBDO/historique.md" ] && { echo; echo "---"; echo; cat "$HEBDO/historique.md"; }; } > "$HEBDO/historique.tmp"
mv "$HEBDO/historique.tmp" "$HEBDO/historique.md"
rm -f "$WEEK"
{
  echo "# Mesures de la semaine : les modèles de Gulliver"
  echo
  echo "Refaites par le Mac chaque lundi à 8 h (\`macos/bench-hebdo/\`), après la mise à jour des modèles de la nuit (\`macos/modeles-semaine/\`), la plus récente en tête : tous les modèles installés dans Gulliver, sa vitesse et sa justesse sur chacun, comparés au choix de la semaine précédente, puis le conseil que l'onglet Modèles applique d'un clic. Les semaines jusqu'au 22 novembre 2026 comparaient Gulliver à d'autres moteurs sur six modèles."
  echo
  cat "$HEBDO/historique.md"
} > "$HEBDO/mesures.md"
echo "=== $(date '+%Y-%m-%d %H:%M') fini : $HEBDO/mesures.md"
