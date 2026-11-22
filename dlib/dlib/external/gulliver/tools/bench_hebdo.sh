#!/bin/bash
# Les tests de vitesse et de justesse de Gulliver contre les autres moteurs, une fois par semaine
# (fork pour macOS, 2026-11-21, demande de l'humain).  Lancé par l'agent launchd de
# macos/bench-hebdo/ (dimanche, 4 h), ou à la main :
#
#   bash dlib/dlib/external/gulliver/tools/bench_hebdo.sh
#
# Il attend une machine au repos (charge sur une minute sous BENCH_MAX_LOAD, 3 par défaut ; un essai
# par heure, 6 au plus), reconstruit Gulliver (build/macos-gulliver) et l'Albireo d'origine
# (macos/albireo-original/build.sh), lance bench_moteurs.sh et, s'il existe, justesse_moteurs.sh, puis met la mesure de la semaine, sous un titre daté, en tête de
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
# L'Albireo d'origine, figé sur macos/albireo-original/COMMIT, pour sa colonne des tableaux ; sans
# lui, les mesures se font quand même.
bash "$ROOT/macos/albireo-original/build.sh" >> "$HEBDO/build.log" 2>&1 \
  || echo "Albireo d'origine non construit, voir $HEBDO/build.log"

STAMP=$(date +%Y-%m-%d)
WHEN=$(LC_ALL=fr_FR.UTF-8 date '+%A %e %B %Y à %H h %M' | sed 's/  / /')
WEEK=$HEBDO/semaine.tmp
echo "## Mesure du $WHEN" > "$WEEK"
found=0
for script in bench_moteurs.sh justesse_moteurs.sh; do
  [ -f "$TOOLS/$script" ] || continue
  echo "lancement de $script"
  table=$(bash "$TOOLS/$script" 2 | sed -n 's/^table: //p' | tail -1)
  [ "$script" = bench_moteurs.sh ] && speed_table=$table
  if [ -n "$table" ] && [ -f "$table" ]; then
    # Les titres du tableau descendent de deux crans, sous celui de la semaine.
    { echo; sed 's/^#/###/' "$table"; } >> "$WEEK"; found=1
  else
    echo "$script n'a pas produit de tableau"
  fi
done
if [ $found -eq 0 ]; then rm -f "$WEEK"; echo "=== aucune mesure"; exit 0; fi
# Le conseil de la semaine : quel modèle employer, tiré des tableaux ci-dessus.
# Avec les modèles installés dans Gulliver (le .ids de bench_moteurs.sh) : conseil.json, que lit l'onglet Modèles.
ids_args=()
[ -n "$speed_table" ] && [ -f "${speed_table%.md}.ids" ] && ids_args=(--ids "${speed_table%.md}.ids")
conseil=$(python3 "$TOOLS/conseil_modeles.py" "$WEEK" "${ids_args[@]}" --json "$HEBDO/conseil.json" 2>> "$HEBDO/build.log")
[ -n "$conseil" ] && { echo; echo "$conseil"; } >> "$WEEK"

# La page du carnet : les semaines de la plus récente à la plus ancienne.  historique.md garde
# les semaines seules ; mesures.md y ajoute l'en-tête.
cp "$WEEK" "$HEBDO/$STAMP.md"
{ cat "$WEEK"; [ -f "$HEBDO/historique.md" ] && { echo; echo "---"; echo; cat "$HEBDO/historique.md"; }; } > "$HEBDO/historique.tmp"
mv "$HEBDO/historique.tmp" "$HEBDO/historique.md"
rm -f "$WEEK"
{
  echo "# Mesures du dimanche : Gulliver contre les autres moteurs"
  echo
  echo "Refaites par le Mac chaque dimanche à 4 h (\`macos/bench-hebdo/\`), la plus récente en tête. Vitesse en jetons par seconde sur le GPU, puis justesse (perplexité d'un même texte). L'explication des écarts est dans « Gulliver contre les autres moteurs »."
  echo
  cat "$HEBDO/historique.md"
} > "$HEBDO/mesures.md"
echo "=== $(date '+%Y-%m-%d %H:%M') fini : $HEBDO/mesures.md"
