# Minimum spanning forest: Kruskal variants

Five Kruskal variants live in `src/traversal.c`. All of them take the same
input: `edges`, the `g->m` edges of an undirected graph, each once (as
`collect_edges()` in `src/main.c` builds them), and sort it in place.

| Function | Sorting |
|---|---|
| `graph_mst_kruskal` | full quicksort, then one pass |
| `graph_mst_quick_kruskal` | lazy quicksort: one edge per step, sorted only as far as the forest reads |
| `graph_mst_quick_star_kruskal` | one lazy quicksort per star, merged through a d-ary heap; each range is filtered before it is partitioned |
| `graph_mst_filter_kruskal` | Filter-Kruskal in rounds: threshold, one filtering pass, lazy quicksort of the light edges only |
| `graph_mst_filter_star_kruskal` | the same, with `find(u)` computed once per run of equal `u` |

The quick star variant wants `edges` **grouped by source**: the edges of a
star are contiguous, which is what walking the out stars in order and
keeping `u < v` produces. Each run of equal `u` is a star and is sorted
inside its own slice. Each undirected edge is copied, sorted and extracted
once.

All of them sort through `Sorting.h`, a single-header Hoare quicksort over
any record with a `double w` field. `sizeof(T)` and the offset of `w` reach
the functions as constants and every function is `always_inline`, so the
strides and the swap fold away at the call site: no `quick__*` symbol is
left in `build/traversal.o`.

## Measured

Random undirected graphs from `generate_random_graph()`, continuous random
weights, `-O2`, best of 5 runs. The edge list is restored outside the timer,
so no variant pays for building its input. Ratio = star time / quick time.

| average degree | n = 1000 | n = 2000 | n = 4000 |
|---|---|---|---|
| 1 - 10 | 1.6 - 2.0x | 2.0 - 2.3x | 2.3x |
| ~100 | 1.9x | 2.2x | |
| ~400 | | 1.3x | 1.7x |
| ~1000 | 1.5x | 1.2x | 1.3x |
| complete | 1.5x | 0.93x | 0.96x |

There is no real break-even point: the star variant reaches parity only on
complete graphs. With the generator's integer weights (1..100) the numbers
swing both ways (0.61x and 3.72x in the same dense regime): ties decide how
much the lazy partitions do, so they are not a reliable comparison.

## Why the theory does not show

| | quick Kruskal | quick star Kruskal |
|---|---|---|
| first minimum | ~2m compares, one lazy quicksort | ~2 deg(u) per star, ~2m in total |
| then, k extractions | ever smaller partitions at the left end | O(log n) heap work each, plus the lazy steps of the stars |
| expected total | O(m + k log(m/k)) | O(m + k log n) |

On a random graph the forest is complete after k ~ n ln n extractions,
whatever m is. Both variants are dominated by the same ~2m term, and the
heap only adds to it: as m grows the ratio tends to 1 from above.

## Ideas, by expected gain

1. **Filter per star.** Before partitioning a range of a star, drop the
   edges (u, v) with `find(v) == find(u)`: they can never enter the forest.
   All edges of a star share u, so `find(u)` is computed once per range and
   the filter costs a single `find(v)` per edge, against two on the global
   array. This is the only structural advantage of the stars (the idea of
   Filter-Kruskal). On dense graphs, after a few hundred unions most of the
   remaining edges are internal to a component, so the filter removes almost
   all the partitioning still to do.
2. **Linear min scan at init.** Settling the first minimum of a star costs
   ~2 deg compares plus the swaps of the lazy step. A scan costs deg compares
   and no swaps. Partitioning would start only at the second extraction from
   that star, and many stars never get there. This halves the dominant term
   and is what could take the dense case below 1x.
3. **Lighter heap.** A heap element is 48 bytes today and `dheap_update`
   moves it whole at every level. Keep `{double w; int id;}` (16 bytes) in
   the heap and the iterators and `cur` in a side array indexed by id. Build
   the heap with an O(n) heapify instead of n inserts.
4. **Small stars out of quicksort.** For deg <= 16, insertion sort once, or
   select the minimum at each extraction. This is where the 2x on sparse
   graphs goes: a Hoare partition of 2-5 elements costs more than its useful
   work.
5. **Earlier exit.** When every remaining v of a star satisfies
   `find(v) == find(u)` (the filter finds this out), pop the star from the
   heap at once instead of draining it extraction by extraction.

Planned order: 2 and 3 (mechanical, low risk), then 1 (changes the nature of
the algorithm, measure it carefully), then 4. Rerun the same benchmark after
each step to see what each one is worth.

## Filter-Kruskal in rounds

`graph_mst_filter_kruskal` is Filter-Kruskal (Osipov, Sanders, Singler) as
an iterative loop. Each round makes **one pass** over the edges still alive:

- an edge with `find(u) == find(v)` is dropped: it can never join the forest;
- one with `w <= tau` is copied to a light buffer;
- the others are compacted in place into `edges[]`, keeping their order.

The light buffer is then sorted lazily and read by the usual Kruskal loop.
`tau` is a quantile of a sample of 1024 weights that leaves about `target`
edges light; `target` starts at `2n` and doubles every round, so the early
rounds settle the forest on few edges and the filter removes most of what is
heavy before it is ever sorted. Every light edge weighs `<= tau <` every
heavy one, so the edges are consumed in Kruskal order.

`graph_mst_filter_star_kruskal` is the same function with `by_star`: when
the edges are grouped by source, `find(u)` is computed once per run of equal
`u`, so the pass costs one `find` per edge instead of two. Nothing else
depends on the grouping and the result is the same on any order. The
compaction keeps the order, so the grouping survives every round. This is
the only thing the star adds here: no per-star state exists.

In `graph_mst_quick_star_kruskal` the same filter runs per star: before a
range is partitioned, the edges whose far end is already in the tree of `u`
are dropped. It helps (2 - 2.5x on large graphs) but the heap stays the
bottleneck: on 1M vertices `dheap_update` takes about 54% of the cycles,
~1200 cycles a call, since the heap holds 937k elements of 48 bytes. The
arity does not help (d = 4 and 8 are the best; 2, 16, 32 are 5 - 25% worse).

## Boruvka on the GPU

`cuda/` (see `cuda/README.md`) holds a CUDA Boruvka and the benchmark
`cuda/bench_mst.c`, which runs every variant on the same edge list and
checks that forest size and weight agree. Per-round steps: two `atomicMin`
passes for the lightest edge of every component, hooking, pointer jumping,
relabeling and compaction of the edges.

Measured on a GTX 750 (sm_50), gcc 64 bit `-O2`, continuous weights, best of
5. GPU = upload + compute + download of the forest. Times in ms.

| graph | quick Kruskal | filter star | GPU | GPU vs quick | GPU vs filter star |
|---|---|---|---|---|---|
| n = 2k, deg 500 | 4.9 | 3.6 | 9.5 | 0.5x | 0.4x |
| n = 4k, deg 1000 | 17.2 | 13.1 | 37.2 | 0.5x | 0.4x |
| n = 4k, complete | 77 | 46 | 141 | 0.55x | 0.3x |
| n = 10k, deg 100 | 7.3 | 5.7 | 9.8 | 0.7x | 0.6x |
| n = 100k, deg 4 | 24.7 | 26.2 | 6.4 | 3.8x | 4.1x |
| n = 100k, deg 32 | 88 | 40.7 | 37.7 | 2.3x | 1.1x |
| n = 300k, deg 20 | 291 | 117 | 82 | 3.5x | 1.4x |
| n = 1M, deg 4 | 333 | 351 | 78 | 4.3x | 4.5x |
| n = 1M, deg 16 | 1152 | 451 | 276 | 4.2x | 1.6x |
| BayAreaUs (road, deg 2.5) | 42.5 | 47.0 | 10.5 | 4.1x | 4.5x |
| twitch (n = 1.9k) | 0.74 | 0.92 | 1.13 | 0.7x | 0.8x |

- On sparse graphs from ~100k vertices the GPU wins 4 - 4.5x against every
  CPU variant: quick Kruskal is the best of them there and Filter-Kruskal
  has little to filter.
- On dense or mid-degree graphs the filter closes most of the gap: the GPU
  is 1.1 - 1.6x faster than filter star, and on small dense graphs the CPU
  wins (the GPU reads all `m` edges in the first rounds).
- The wall clock of a GPU call is 5 - 45 ms above the figure in the table:
  device allocation is not inside the CUDA events.
- Numbers taken with a 32 bit gcc overstate the GPU: the CPU code was 30 -
  40% slower there.
