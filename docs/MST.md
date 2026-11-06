# Minimum spanning forest: Kruskal variants

Three Kruskal variants live in `src/traversal.c`. All of them take the same
input: `edges`, the `g->m` edges of an undirected graph, each once (as
`collect_edges()` in `src/main.c` builds them), and sort it in place.

| Function | Sorting |
|---|---|
| `graph_mst_kruskal` | full quicksort, then one pass |
| `graph_mst_quick_kruskal` | lazy quicksort: one edge per step, sorted only as far as the forest reads |
| `graph_mst_quick_star_kruskal` | one lazy quicksort per star, merged through a d-ary heap |

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

## Boruvka on the GPU (branch `cuda-boruvka`)

`cuda/boruvka.cu` is a CUDA Boruvka (per-component lightest edge by two
`atomicMin` passes, hooking, pointer jumping, compaction of the internal
edges). `cuda/bench_mst.c` compares it with the Kruskal variants on the same
edge list and checks that the forest has the same size and weight.

Build and run (Windows, nvcc 11.4 needs MSVC 2019; the 32 bit MinGW gcc
cannot load 64 bit CUDA code, so the GPU side is the executable
`build/boruvka_cli.exe`, fed through a file):

    cuda\build.bat
    gcc -std=gnu11 -O2 -Iinclude cuda/bench_mst.c src/{graph,list,star_impl,matrix_impl,queue,stack,importer,traversal,export}.c -lm -o bin/bench_mst
    bin/bench_mst 1000000 16 3        # n, average degree, runs
    bin/bench_mst res/BayAreaUs_undirected.dot

Measured on a GTX 750 (sm_50) against quick Kruskal, continuous weights,
best of 3. "GPU" = upload + compute + download of the forest.

| graph | quick Kruskal | GPU | speedup |
|---|---|---|---|
| n = 10k, deg 4 | 2.7 ms | 2.3 ms | 1.2x |
| n = 100k, deg 4 | 33 ms | 6.7 ms | 5.0x |
| n = 100k, deg 32 | 123 ms | 38 ms | 3.2x |
| n = 300k, deg 20 | 409 ms | 83 ms | 4.9x |
| n = 1M, deg 4 | 498 ms | 77 ms | 6.5x |
| n = 1M, deg 16 | 1625 ms | 278 ms | 5.8x |
| n = 2k, deg 500 | 6.7 ms | 10.3 ms | 0.65x |
| n = 4k, deg 1000 | 23.6 ms | 39.9 ms | 0.59x |
| BayAreaUs (road, deg 2.5) | 63 ms | 10.5 ms | 6.0x |

The GPU wins by 5-7x as soon as the graph has ~100k vertices, and loses on
small dense graphs: quick Kruskal finishes after ~n ln n extractions
whatever m is, while Boruvka reads all m edges in the first rounds.
