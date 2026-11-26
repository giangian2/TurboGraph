#!/usr/bin/env python3
"""
Converts a SNAP edge list into an undirected weighted DOT file that
graph_import_dot() reads (vertex ids 0..n-1, weight in label=).

    snap_to_dot.py jaccard  in.txt.gz  out.dot
    snap_to_dot.py bitcoin  in.csv.gz  out.dot

Vertex ids are compacted to 0..n-1 in increasing order of the original id.
Self loops are dropped and every undirected edge is written once (u < v).

Weights, never 0 (0 means "no edge" in this library):

  jaccard  the graph has no weights: w = Jaccard distance of the two
           neighborhoods, 1 - |N(u) & N(v)| / |N(u) | N(v)|, rounded to 6
           decimals and at least 1e-6. An edge inside a community (many
           common neighbors) is light, a bridge between communities is
           heavy: the correlation between weights and structure real
           weighted graphs have. Edges with no common neighbor all weigh 1,
           so there are many ties.
  bitcoin  soc-sign-bitcoin-otc, trust ratings -10..10 between users. An
           edge rated both ways keeps the best rating; w = 11 - rating, an
           integer in 1..21: the most trusted edges are the lightest. Heavy
           ties, a test for the (w, v) threshold.
"""
import gzip
import sys


def open_text(path):
    return gzip.open(path, "rt") if path.endswith(".gz") else open(path, "r")


def read_pairs(path):
    with open_text(path) as f:
        for line in f:
            if not line.strip() or line[0] in "#%":
                continue
            yield line


def compact(ids):
    return {old: new for new, old in enumerate(sorted(ids))}


def jaccard(src):
    edges = set()
    for line in read_pairs(src):
        a, b = line.split()[:2]
        u, v = int(a), int(b)
        if u != v:
            edges.add((min(u, v), max(u, v)))

    idx = compact({x for e in edges for x in e})
    nbr = [set() for _ in idx]
    out = []
    for u, v in edges:
        u, v = idx[u], idx[v]
        nbr[u].add(v)
        nbr[v].add(u)
        out.append((min(u, v), max(u, v)))

    weighted = []
    for u, v in sorted(out):
        common = len(nbr[u] & nbr[v])
        union = len(nbr[u]) + len(nbr[v]) - common
        weighted.append((u, v, max(round(1.0 - common / union, 6), 1e-6)))
    return len(idx), weighted


def bitcoin(src):
    best = {}
    for line in read_pairs(src):
        a, b, r = line.split(",")[:3]
        u, v, r = int(a), int(b), int(r)
        if u == v:
            continue
        key = (min(u, v), max(u, v))
        best[key] = max(best.get(key, -11), r)

    idx = compact({x for e in best for x in e})
    weighted = sorted((idx[u], idx[v], 11 - r) for (u, v), r in best.items())
    return len(idx), weighted


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("jaccard", "bitcoin"):
        sys.exit(__doc__)
    mode, src, dst = sys.argv[1:]
    n, edges = (jaccard if mode == "jaccard" else bitcoin)(src)

    with open(dst, "w") as f:
        f.write("graph G {\n")
        for u, v, w in edges:
            f.write(f"  {u} -- {v} [label={w:g}];\n")
        f.write("}\n")
    print(f"{dst}: n = {n}, m = {len(edges)}, avg degree {2 * len(edges) / n:.1f}")


if __name__ == "__main__":
    main()
