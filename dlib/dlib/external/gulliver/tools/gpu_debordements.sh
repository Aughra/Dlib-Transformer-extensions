#!/bin/bash
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Les débordements de registres des noyaux Metal (fork pour macOS, 2026-11-22, chantier Vitesse).
#
#   bash tools/gpu_debordements.sh modele.gguf [tokens]
#
# Un noyau qui manque de registres range une partie de ses valeurs en mémoire (« spill ») : chaque accès coûte alors
# un aller-retour à la mémoire, au cœur de ses boucles.  Le compilateur de Metal le signale pendant l'exécution, et la
# trace « Metal System Trace » d'Instruments le relève par encodeur.  Ce script lance une lecture de [tokens] tokens
# (2 048 par défaut) et quatre pas d'écriture sous cette trace, avec GULLIVER_GPU_PROFILE (un encodeur par noyau,
# nommé d'après lui), puis donne, pour chaque noyau qui déborde, le nombre d'encodeurs touchés et les octets débordés.
#
# Exemple (22 novembre 2026, Qwen3-VL 4B) : attention_nax_4_128, 272 octets ; l'attention de MLX, 128.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
MODEL=${1:?usage: gpu_debordements.sh modele.gguf [tokens]}
TOKENS=${2:-2048}
WORK=$ROOT/build/gpu-debordements
mkdir -p "$WORK"
TRACE=$WORK/trace.trace
[ -d "$TRACE" ] && rm -r "$TRACE"
xcrun xctrace record --template 'Metal System Trace' --output "$TRACE" --env GULLIVER_GPU_PROFILE=1 \
    --launch -- "$ROOT/build/macos-gulliver/tests/bench_runtime" "$MODEL" 18 "$TOKENS" 4 1 q8 metal > "$WORK/record.log" 2>&1
xcrun xctrace export --input "$TRACE" \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="graphics-compiler-spill-events"]' > "$WORK/spills.xml"
xcrun xctrace export --input "$TRACE" \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="metal-application-encoders-list"]' > "$WORK/encoders.xml"
"$ROOT/build/macos-gulliver/bin/debordements" "$WORK/encoders.xml" "$WORK/spills.xml"
