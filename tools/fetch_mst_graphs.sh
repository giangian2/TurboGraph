#!/usr/bin/env bash
# Downloads the real graphs of the MST suite from SNAP and converts them to
# weighted DOT files (see tools/snap_to_dot.py for the weights). Run from the
# project root:
#
#   tools/fetch_mst_graphs.sh
#
# Downloads go to res/mst/raw, the converted graphs to res/mst: both are
# ignored by git, the script rebuilds them. Files already present are not
# fetched or converted again.
#
# Small graphs become DOT files (tools/snap_to_dot.py). Large ones become
# binary edge lists (bin/snap_to_bin, Jaccard weights, see bench/jaccard.h):
# a DOT file of 10M edges would be ~400 MB and slow to parse.
#
#   tools/fetch_mst_graphs.sh          small and large graphs
#   tools/fetch_mst_graphs.sh small    small graphs only (no C build)
#   tools/fetch_mst_graphs.sh big      also the graphs of bench/mst_big.c:
#                                      Orkut (1.4 GB download, ~2 GB .bin) and
#                                      four generated DC-SBM graphs (~5 GB)
set -euo pipefail

RAW=res/mst/raw
OUT=res/mst
SNAP=https://snap.stanford.edu/data
mkdir -p "$RAW"

# name | url | mode | what it is
GRAPHS=(
    "facebook|$SNAP/facebook_combined.txt.gz|jaccard|ego networks of Facebook: 4k vertices, dense communities"
    "email-eu|$SNAP/email-Eu-core.txt.gz|jaccard|e-mail of a research institution: 1k vertices, departments"
    "astroph|$SNAP/ca-AstroPh.txt.gz|jaccard|astrophysics co-authorship: 18k vertices, cliques per paper"
    "amazon|$SNAP/bigdata/communities/com-amazon.ungraph.txt.gz|jaccard|Amazon co-purchase: 335k vertices, sparse with communities"
    "dblp|$SNAP/bigdata/communities/com-dblp.ungraph.txt.gz|jaccard|DBLP co-authorship: 317k vertices, power-law degrees"
    "bitcoin-otc|$SNAP/soc-sign-bitcoinotc.csv.gz|bitcoin|trust ratings between users: real integer weights, many ties"
)

for entry in "${GRAPHS[@]}"; do
    IFS='|' read -r name url mode what <<<"$entry"
    raw="$RAW/$(basename "$url")"
    dot="$OUT/$name.dot"

    if [[ ! -s "$raw" ]]; then
        echo "fetching $name ($what)"
        curl -fsSL --retry 2 -o "$raw" "$url"
    fi
    if [[ ! -s "$dot" ]]; then
        python3 -I tools/snap_to_dot.py "$mode" "$raw" "$dot"
    fi
done

[[ "${1:-}" == small ]] && exit 0

# name | url | file inside the archive (tar.gz) or empty | what it is
LARGE=(
    "twitter-ego|$SNAP/twitter_combined.txt.gz||Twitter ego networks: 81k vertices, communities, deg ~33"
    "artist|$SNAP/gemsec_facebook_dataset.tar.gz|facebook_clean_data/artist_edges.csv|Facebook artist pages: 50k vertices, deg ~33"
    "youtube|$SNAP/bigdata/communities/com-youtube.ungraph.txt.gz||YouTube: 1.1M vertices, sparse, control case"
    "pokec|$SNAP/soc-pokec-relationships.txt.gz||Pokec social network: 1.6M vertices, deg ~28"
    "gplus-ego|$SNAP/gplus_combined.txt.gz||Google+ ego networks: 107k vertices, dense communities, deg ~230"
)

make --no-print-directory bin/snap_to_bin

for entry in "${LARGE[@]}"; do
    IFS='|' read -r name url inner what <<<"$entry"
    raw="$RAW/$(basename "$url")"
    out="$OUT/$name.bin"

    if [[ ! -s "$raw" ]]; then
        echo "fetching $name ($what)"
        curl -fsSL --retry 2 -o "$raw" "$url"
    fi
    if [[ ! -s "$out" ]]; then
        if [[ -n "$inner" ]]; then
            tar -xzOf "$raw" "$inner" | bin/snap_to_bin "$out"
        else
            zcat "$raw" | bin/snap_to_bin "$out"
        fi
    fi
done

[[ "${1:-}" == big ]] || exit 0

# --- graphs of bench/mst_big.c -------------------------------------------------
# Orkut: 3M vertices, 117M edges, deg ~76, ground-truth communities. The
# conversion needs ~6 GB of RAM.
raw="$RAW/com-orkut.ungraph.txt.gz"
if [[ ! -s "$raw" ]]; then
    echo "fetching orkut (Orkut social network: 3M vertices, 117M edges)"
    curl -fsSL --retry 2 -o "$raw" "$SNAP/bigdata/communities/com-orkut.ungraph.txt.gz"
fi
[[ -s "$OUT/orkut.bin" ]] || zcat "$raw" | bin/snap_to_bin "$OUT/orkut.bin"

# Generated: degree-corrected block models, blocks of 1000 vertices, with and
# without hubs. name | n | block | deg_in | deg_out | gamma (0 = no hubs)
make --no-print-directory bin/gen_mst_bin
SYNTH=(
    "big-c1m-d120|1000000|1000|100|20|0"
    "big-c1m-d120-hub|1000000|1000|100|20|2.5"
    "big-c300k-d300|300000|1000|260|40|0"
    "big-c300k-d300-hub|300000|1000|260|40|2.5"
)
for entry in "${SYNTH[@]}"; do
    IFS='|' read -r name n block din dout gamma <<<"$entry"
    [[ -s "$OUT/$name.bin" ]] || bin/gen_mst_bin "$OUT/$name.bin" "$n" "$block" "$din" "$dout" "$gamma"
done
