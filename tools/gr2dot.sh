#!/bin/sh
# Converts a DIMACS shortest-path graph (.gr or .gr.gz, the format of the
# 9th DIMACS Implementation Challenge road networks) into the DOT edge list
# read by graph_import_dot, in the same shape as res/BayAreaUs.dot:
#
#   digraph G {
#     node [shape=circle];
#     1 -> 2 [label="1988"];
#   }
#
# "a u v w" lines become arcs with the weight as label; comments ("c") and
# the problem line ("p sp n m") are dropped.
#
#   tools/gr2dot.sh USA-road-d.W.gr.gz > res/WesternUSA.dot
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 file.gr[.gz] > out.dot" >&2
    exit 1
fi

case "$1" in
    *.gz) reader="gzip -dc" ;;
    *)    reader="cat" ;;
esac

$reader "$1" | LC_ALL=C awk '
    BEGIN { print "digraph G {"; print "  node [shape=circle];" }
    $1 == "a" { printf "  %s -> %s [label=\"%s\"];\n", $2, $3, $4 }
    END { print "}" }
'
