#!/usr/bin/env bash
# Generates the grid of bench/mst_big.c: DC-SBM graphs that vary one factor
# at a time, average degree and hub strength, everything else fixed (blocks
# of 1000 vertices, 85% of the degree inside the block). Run from the
# project root:
#
#   tools/gen_mst_grid.sh          the grid (n = 300k) and the large checks (n = 1M)
#
# Output in res/mst/grid (ignored by git); files already present are kept.
# The hubs eat part of the requested degree (repeated edges to a hub are
# merged), so the real degree is lower with hubs: the bench prints it.
set -euo pipefail

OUT=res/mst/grid
mkdir -p "$OUT"
make --no-print-directory bin/gen_mst_bin

gen() { # name n deg gamma
    local name=$1 n=$2 deg=$3 gamma=$4
    local din dout
    din=$(awk -v d="$deg" 'BEGIN { printf "%.1f", d * 0.85 }')
    dout=$(awk -v d="$deg" 'BEGIN { printf "%.1f", d * 0.15 }')
    [[ -s "$OUT/$name.bin" ]] || bin/gen_mst_bin "$OUT/$name.bin" "$n" 1000 "$din" "$dout" "$gamma"
}

# gamma 0 = no hubs; lower gamma = stronger hubs
for deg in 100 180 260 340; do
    for gamma in 0 3.0 2.5 2.2; do
        gen "n300k-d${deg}-g${gamma}" 300000 "$deg" "$gamma"
    done
done

# the same question on a graph three times larger
gen "n1m-d200-g0" 1000000 200 0
gen "n1m-d200-g2.5" 1000000 200 2.5
