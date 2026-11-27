# Minimum spanning forest: Kruskal variants

Six Kruskal variants live in `src/traversal.c`. They take the edges of an
undirected graph, each once, as one of two types (`include/Graph.h`):

- `EdgeList`: the edges in any order;
- `StarEdgeList`: the same edges **grouped by source**. The edges of a star
  are contiguous and a run of equal `u` is the star of `u`. It is what
  `graph_star_edge_list()` builds, walking the out stars in order and keeping
  `u < v`. A `StarEdgeList` converts to an `EdgeList` with
  `star_as_edge_list()`, not the other way: the signature of an algorithm
  says which one it needs and the compiler rejects the wrong one.

The list is reordered in place, so pass a copy to keep it.

| Function | Input | Sorting |
|---|---|---|
| `graph_mst_kruskal` | `EdgeList` | full quicksort, then one pass |
| `graph_mst_quick_kruskal` | `EdgeList` | lazy quicksort: one edge per step, sorted only as far as the forest reads |
| `graph_mst_quick_star_kruskal` | `StarEdgeList` | one lazy quicksort per star inside its own slice, merged through a d-ary heap; the range about to be partitioned is filtered first |
| `graph_mst_filter_star_kruskal` | `StarEdgeList` | Filter-Kruskal in rounds: threshold, one filtering pass, lazy quicksort of the light edges only |
| `graph_mst_kbest_star_kruskal` | `StarEdgeList` | K lightest edges per star from one read-only scan, 16-byte heap of heads, rescan with filter and compaction when the buffer runs out |
| `graph_mst_filter_kbest_kruskal` | `StarEdgeList` | Filter-Kruskal rounds whose light edges are read by K-best instead of a global lazy quicksort |

Which one is fastest depends on the family of graph: see "MST suite" below.

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

`graph_mst_filter_star_kruskal` is Filter-Kruskal (Osipov, Sanders, Singler)
as an iterative loop over a `StarEdgeList`. Each round makes **one pass**
over the edges still alive (`filter_pass`):

- an edge with `find(u) == find(v)` is dropped: it can never join the forest;
- one with `w <= tau` is copied to a light buffer;
- the others are compacted in place into `edges[]`, keeping their order.

The light buffer is then sorted lazily and read by the usual Kruskal loop.
`tau` (`filter_threshold`) is a quantile of a sample of 1024 weights that
leaves about `target` edges light; `target` starts at `2n` and doubles every
round, so the early rounds settle the forest on few edges and the filter
removes most of what is heavy before it is ever sorted. Every light edge
weighs `<= tau <` every edge kept, so the edges are consumed in Kruskal order.

The star order buys one thing: all the edges of a star share `u`, so
`find(u)` is computed once per star and the pass costs one `find(v)` per edge
instead of two (3 - 15% in the measurements). The compaction keeps the
order, so the grouping survives every round; no per-star state exists.

In `graph_mst_quick_star_kruskal` the same idea runs per star
(`star_filter_top`): before the next step, the range on top of the star's
frame stack, the leftmost one still to be partitioned, drops the edges whose
far end is already in the tree of `u`. Frames below are filtered when their
turn comes, when more trees have merged. It helps (2 - 2.5x on large graphs)
but the heap stays the bottleneck: on 1M vertices `dheap_update` takes about
54% of the cycles, ~1200 cycles a call, since the heap holds 937k elements of
48 bytes. The arity does not help (d = 4 and 8 are the best; 2, 16, 32 are
5 - 25% worse).

## Proposal: K-best scan per star

Implemented as `graph_mst_kbest_star_kruskal`; what it measured is in
"K-best: what the measurements said" below, and the expectations of this
section mostly did not hold. It generalizes idea 2 of the list above, and it
replaces the per-star structure of `graph_mst_quick_star_kruskal`: the lazy
quicksort of today, or a heap built on the whole star (`dheap_heapify`).

### How little of a star Kruskal reads

From the Callgrind profile of `graph_mst_quick_kruskal` on the random graph
of `main.c` (n = 10k, density 0.05, m ~ 2.5M): `uf_union` runs 45,857 times.
That is the number of edges extracted in Kruskal order before the forest is
complete, and it does not depend on the variant. Every edge belongs to one
star only (`u < v`), so:

| | per star, on average |
|---|---|
| edges | ~250 |
| edges ever extracted | ~4.6 |

Kruskal reads less than 2% of a star in order. The rest is heavier than any
edge the forest will need, or joins two vertices already in the same tree.
This matches the theory above (k ~ n ln n extractions whatever m is): as the
graph gets denser the stars grow but the extractions per star do not.

### What the lazy quicksort pays for the first edges

The lazy quicksort does not sort what is never read, but it pays to prepare
the edges it might read. At init every star needs its minimum for the
global heap (`QUICK_SORT_STEP` in the init loop). The quicksort does not
know where the minimum is, so it partitions the whole star, then the left
half, then the left quarter:

```
star (d = 8):   [5 3 8 1 7 2 6 4]

partition 8 elements (pivot 5):  [4 3 2 1 | 7 8 6 5]    ~8 compares + swaps
partition 4 elements (pivot 4):  [1 3 2 | 4]            ~4 compares + swaps
partition 3 elements (pivot 1):  [1 | 3 2]              ~3 compares
-> returns 1
```

That is d + d/2 + d/4 + ... ~ **2d compares plus the swaps** (writes) for
the first edge alone. A linear scan finds the same minimum in **d compares
and no writes**.

The partitions are not wasted in principle. The right halves left on the
frame stack (`[7 8 6 5]`, `[4]`, ...) are already split, and they make the
next extractions cheap. It is an investment in the future of the star, but
with ~4.6 extractions per star that future rarely comes: the right halves
of the first levels are never touched again.

### The proposal

Scan the star **once** and keep its K lightest edges in a small sorted
buffer, with K small (4 - 16: a few registers, one cache line). The next
extractions read the buffer. Only when the buffer is empty is the star
scanned again, for the K best edges **above the threshold** of the last
one extracted:

```
refill(star):
    buf = []                                // at most K edges, sorted
    for e in star:
        if e <= threshold: continue         // already extracted
        if find(e.v) == root(u): drop e     // internal: compact it away
        if len(buf) < K or e < buf.last:
            insert e in buf (insertion sort, drop the (K+1)th)
    threshold = buf.last

next(star):
    if buf is empty: refill(star)
    return buf.pop_front()
```

A scan costs d compares, all sequential reads, plus the insertions in the
buffer. On edges in random order the i-th edge enters the K best with
probability about K/i, so a scan makes about K ln(d/K) insertions: ~27 for
d = 250 and K = 8, negligible against 250. With ~4.6 extractions per star
and K = 8, almost every star is scanned once.

### Why it is more than "2d against d"

The profile of the quicksort says where its time goes: misses to RAM and
mispredicted branches (~26% in the partition loops), not instructions. The
scan is better on all three fronts:

| | lazy quicksort, first minimum | K-best scan |
|---|---|---|
| compares | ~2d | ~d |
| writes to the star | yes (swaps) | **none** |
| passes over memory | log d, ever shorter | **1** |
| branch prediction | `w < pivot`: random, ~25% mispredicted | `w < buf.last`: almost always false, predictable |

The last row is the least obvious. In a partition the answer to "is it
below the pivot?" is yes about half of the time, at random. In the scan the
question is "is it among the K lightest seen so far?", and after the first
few edges the answer is almost always no: the predictor gets it right.

In short: the lazy quicksort is lazy **on the edges you will not read**,
but to read the first ones it still pays to prepare many. The K-best scan
is lazy **on that preparation too**, because it knows that only a few
edges of each star are ever read.

### Details that matter

- **The filter is free.** The scan is the natural place for the filter of
  `star_filter_top`: `find(u)` once per scan, one `find(v)` per edge, and
  the internal edges are compacted away, so every rescan finds a shorter
  star. A star left empty is popped from the global heap at once (idea 5).
- **Ties.** "Above the threshold" on `w` alone loses or duplicates edges of
  equal weight, and the generator's integer weights (1..100) have many.
  Compare the pair (w, v): in a simple graph every v appears once in the
  star of u, so the order is total and the threshold is unique. Not the
  index in the star: the filter compacts the star and indices move, while v
  never does. With multi-edges a global edge id would be needed instead.
  Example: the star of 0 holds (0,1,3) and (0,4,3), the buffer of K = 1
  takes (0,1) and the threshold becomes 3. On weight alone the refill skips
  (0,4) as "already extracted", and if (0,4) belongs to the MST the forest
  comes out heavier than it should.
- **Stale buffer entries.** Two trees may merge between a refill and an
  extraction. Correctness is not at stake, `uf_union` rejects the edge as
  it does now; only a little work is wasted.
- **Stars read a lot.** A star that gives e >> K edges costs d * e / K and
  can lose against the lazy quicksort, whose investment pays off there.
  Make K adaptive (see below).
- **Small stars.** With d <= K one insertion sort of the star, in place,
  settles it for good: it needs no buffer and is read straight from the
  edge list (idea 4 comes for free).
- **A lighter global heap.** Without quicksort iterators the state of a
  star is `{start, end, head, threshold, refills, mode}` plus its buffer
  slots, kept in side arrays indexed by id. The global heap holds only
  `{double w; int id;}`, the edge at the head of the star: 16 bytes
  instead of 48, so `dheap_update`, 54% of the cycles on 1M vertices, moves
  a third of the data (idea 3). Build it with an O(n) heapify after the
  first refill of every star.

### The buffer

It is **not** a slice of the `StarEdgeList` and not one `malloc` per star:
one block allocated before the loop (as `frames` is today), where star s
owns slots `s*K .. s*K + K - 1`.

A slot holds the **position of the edge inside the star** (an `int`), not
a copy of the edge: the weight is read from the star, which is hot in cache
during the scan. With n = 1M and K = 8 that is 32 MB, against 96 - 128 MB
for copies of (w, v). Positions stay valid: the star is compacted only by a
refill, and the refill rebuilds the buffer in the same pass, recording the
positions after compaction. Stars with d <= K get no slots at all (see
"Small stars"), which on large sparse graphs is most of them.

The buffer is kept sorted, lightest first, at most K entries. For every
edge the scan reads (after the threshold and the `find(v) == root`
filter):

1. **Fast reject.** Buffer full and the edge >= the last entry (the worst
   of the K): drop it. One compare, and the case of almost every edge, so
   the branch is predicted.
2. **Insertion.** Start from the last occupied slot (slot K - 1 when full:
   the old last entry is overwritten, which is the one that must fall
   out), and move left: while the entry on the left is heavier than the
   new edge, shift it one slot right. Write the new edge in the hole.
   Linear from the right, not a binary search: search and shift are the
   same loop, and with K <= 16 the slots fit in one or two cache lines.
3. **Not yet full.** Same loop without the fast reject.

K = 3, star with weights 7 3 9 5 1 8 at positions 0..5:

| read | buffer (positions) | weights | what happened |
|---|---|---|---|
| 7 (pos 0) | [0] | [7] | not full |
| 3 (pos 1) | [1 0] | [3 7] | 7 shifted right, 3 in front |
| 9 (pos 2) | [1 0 2] | [3 7 9] | appended, now full |
| 5 (pos 3) | [1 3 0] | [3 5 7] | 9 falls out, 7 shifted, 5 in the hole |
| 1 (pos 4) | [4 1 3] | [1 3 5] | 7 falls out, 5 and 3 shifted |
| 8 (pos 5) | [4 1 3] | [1 3 5] | 8 >= 5: fast reject |

Nothing was written to the star. Kruskal reads the buffer left to right
through a `head` index (no shifting on extraction); when it runs out, the
next refill skips everything up to the threshold (5, v of position 3).

Optional: a sentinel of weight -inf in front of the buffer stops the shift
loop by itself, removing the "reached the start?" test.

### Adaptive K: two modes per star

Fixed slots at `s*K` cannot grow per star, and reserving the largest K for
every star is too much memory (n = 1M, K = 64: 256 MB of positions). So a
star starts in **buffer mode** and moves to **in place mode** when it is
read a lot:

| refill of the star | K | mode | where the K best are |
|---|---|---|---|
| 1st | 8 | scan + buffer | the star's slots |
| 2nd | 16 | scan + buffer | the star's slots |
| 3rd | 32 | partial sort in place | the head of the star |
| 4th | 64 | partial sort in place | the head of what is left |
| ... | x2 | in place | ... |

- Slots per star: 16, the largest K of buffer mode. The first refill uses
  only 8 because most stars stop there; with 16 the first scan would cost
  about three times as many shifts (~44 insertions x ~8 against ~27 x ~4).
- Above 16 the buffer degenerates (the shifts grow like K^2 ln(d/K)),
  while a partial sort costs ~d + K log K, so a large K is no longer a
  problem.
- The switch happens inside a refill: one compaction pass keeps only the
  edges above the threshold with `find(v) != root`, then
  `QUICK_SORT_PARTIAL` on the compacted star with k = start + K. From then
  on the star is read from itself through a cursor, like a small star: no
  threshold and no tie-break are needed, the position separates the edges
  already taken from the others. The next refill filters and partially
  sorts only the part after the cursor, with K doubled.

A star with d = 1000 that gives 60 edges: 8 full scans with K fixed at 8,
4 refills with the adaptive K (8 + 16 + 32 + 64 >= 60). The passes drop
from e/K to about log2(e/K), the same logic as the doubling `target` of
Filter-Kruskal, and the worst case stays O(d log e). The average star
(~4.6 extractions) does not notice: one scan with K = 8, as before.

The starting K can come from the graph: on a random graph Kruskal extracts
~n ln n edges, ~ln(n)/2 per star with u < v (4.6 for n = 10k, the figure of
the profile above; 6.9 for n = 1M), so K = 8 covers both. On non random
graphs (roads, social networks, skewed degrees) the estimate is weaker, and
the per-star doubling is what protects them.

### Why not `QUICK_SORT_PARTIAL` everywhere

It already returns "the K lightest in order", and with the stack moved out
(today it does a `malloc` and a `free` per call, n of them with one call
per star) a single block sized on the largest star would do, since the
call completes before the next one. It also needs no threshold. But its
cost per star is a quickselect: >= ~2d compares, Hoare swaps (writes to the
star), log d passes over memory and the ~25% mispredicted `w < pivot`
branch, exactly the profile the scan avoids. The filter would also need a
pass of its own, while the scan applies it in the same loop. Its other
gains (no iterator per star, a 16 byte heap element, no array of m frames)
come identical with the scan. Hence its role here: the in place mode for a
large K only. Moving the stack out changes `QUICK_SORT` too, which goes
through `quick__partial_sort`: keep a wrapper that allocates it, so
`graph_mst_kruskal` does not change.

### Note for later: align the heap to the cache line

Worth doing together with the switch to `{double w; int id;}`, or half of
its gain is lost.

**Why 16 bytes help.** `dheap__move_down` (`include/DHeap.h`) reads all d
children of a node at every level, and they are contiguous: indices
`d*i + 1 ... d*i + d`. The comparator reads only the weight. With d = 4:

| element | 4 children span | cache lines per level |
|---|---|---|
| `q_step_iter`, 48 bytes | 192 bytes | 3 - 4, for 32 useful bytes |
| `{double w; int id;}`, 16 bytes | 64 bytes | **1** if aligned, 2 otherwise |

The prefetcher does not help here. The descent jumps from `i` to `4i + 1`,
then `16i + 5`, and the next address depends on the compare just made: every
level is a random access, and what it costs is the number of lines it
touches. This is also why d = 4 is the natural arity at 16 bytes: the
children fill exactly one line.

**The problem today.** `dheap_header_t` is 40 bytes (five 8-byte fields)
and the data starts right after it. `malloc` returns 16-byte aligned blocks,
so the data starts at an address = 8 (mod 16). Every 16-byte element then
straddles two 16-byte blocks, and every group of 4 children touches **2
lines instead of 1**.

**The fix.**

- Allocate with `aligned_alloc(64, ...)`. C11 wants the size to be a
  multiple of the alignment: round it up.
- Pad the header so that the **first child of the root** (index 1) starts
  on a 64-byte boundary, that is element 0 at offset 48 (mod 64). The
  children `4i + 1 ... 4i + 4` of every node then fall in exactly one line.
- The header stays right before the data (`DHEAP__HDR(h)` is `h - 1` in
  headers), so the padding goes in front of it, and `dheap__free` must free
  the block start, not the header.

Check it with Callgrind before and after (`-c`, DLmr and D1mr on the
`dheap__*` lines) on a heap large enough to spill out of the LL, e.g.
n = 1M.

### How to measure it

As a new variant next to the others (e.g. `graph_mst_kbest_star_kruskal`),
first with a fixed K = 8, then with an adaptive K. Compare Ir, DLmr and Bcm
against `graph_mst_quick_star_kruskal` on the same input:

```
make profile PROFFLAGS="-f graph_mst_quick_star_kruskal -c -B -s qstar"
make profile PROFFLAGS="-f graph_mst_kbest_star_kruskal -c -B"
```

then the wall clock with `cuda/bench_mst.c` on the graphs of the tables
above. Expected: a large gain on the dense graphs, where stars are long and
read little, and a small one on sparse graphs, where stars are short and
the global heap dominates.

## K-best: what the measurements said

The implementation follows the proposal, with fixed K (`KB_K`, default 3,
`-DKB_K=` to change it):

- one 64-byte block per vertex (`kbest_star`, `aligned_alloc(64)`):
  last edge extracted, start, deg, next, ready and the K slots in one
  cache line;
- a heap of 16-byte heads `{w, u}`, built in place over the heads in O(n);
- a per-star threshold `(w, v)`, the last edge extracted from the star,
  starting at `(-inf, -1)`, so ties are never lost;
- a refill that drops the edges extracted or internal, compacts the star
  and rebuilds the buffer in one pass;
- `kb_skip_internal`: before the head of a star goes back to the heap, the
  buffer moves past the edges that became internal. The largest single gain
  (up to 2.7x on sparse graphs): most buffer entries become internal before
  they are extracted;
- the code is split in `kb_alloc`, `kb_load` and `kb_drain`, reused by the
  hybrid below.

Why it is not the fast path the proposal expected, from counters on a
complete graph of 4000 vertices (K = 8): the first scan reads 8M edges and
the rescans another **6.5M**. The K lightest edges of u lead to the vertices
that join the tree of u first, so for every edge used 2-3 buffer entries
are thrown away as internal and the buffer runs dry; and with `u < v` the
long stars (small u) are the ones read most, so the rescans hit the
longest stars. The heap is not the cost on dense graphs (7k updates); the
second pass over the edges is.

Tried and dropped: an in place mode (from the first refill, partial
quicksort with k doubling, once `-DKB_INPLACE=1`, removed since). Almost
every star refills once or twice, and a quickselect costs ~2d with writes
against the d reads of a rescan: never faster.

### Profile and the two fixes that paid

Callgrind (`tools/profile.sh -f graph_mst_kbest_star_kruskal -c -B`) on
`jsbm/dense-40k` (2.95M edges, deg 148) split K-best into the refills
(~54% of the instructions, `uf_find` included), the heap (~26%) and the
first scan (~20%). Filter-Kruskal executes the same number of instructions
(403M against 405M) and is still ~1.7x faster: its work is sequential
passes, K-best jumps between stars scattered in memory.

- **A heap that only touches its root.** K-best reads `heap[0]`, then
  either replaces it with a heavier head (it can only sink) or removes it.
  `DHeap` pays for what this case never uses: the position map `pos[]`
  (two random writes per swap), full swaps through a scratch slot and a
  sift-up attempt on every update. `kb_heap_sink` sinks a hole instead:
  one 16-byte copy per level, the new head written once, no `pos[]`.
  The comparator was not the issue: `DHeap` already gets it inlined.
- **Buffer keys in registers.** The refill compared each edge with the
  worst slot through `e[b[K-1]].w`: two dependent loads, reloaded every
  time because the compaction `e[wr] = x` may alias them. The scan keeps
  the keys of the slots next to their positions (`kbest_buffer`), local
  to the scan: only the positions are stored in the star.

Together: 405M -> 336M instructions (-17%), 5-18% faster on every graph
tried.

### Tried and dropped: a K per star

The rescans read the list 2.5 times, so the idea was to pick for every
star the K it will need, from its degree, before the first scan. An oracle
recorded how many edges Kruskal takes from each star: 3-7 on average, and
with that K a single scan would do. Measured, it did not pay:

- with K = max(6, 2 sqrt(deg)) the reads drop from 2.5 to 1.5-1.8 m, but
  the time does not move (+-5%): a larger buffer holds edges that become
  internal while they wait (skips 150k -> 1.24M on dense-40k);
- with the **perfect** K (an in place run with K = deg records where each
  star stops, a second run sorts exactly that much) K-best is 1.4-1.6x
  *slower* than with K = 3 on dense-40k and gplus. The perfect K adds up
  to **93-100% of m**: to finish a star almost all of its edges must be
  discarded as internal anyway.

With a small K the internal edges never enter the buffer: the refill drops
them in bulk, in one sequential pass, without sorting them. A large K sorts
them first and skips them one by one. What would help is knowing *which*
edges will still be alive when extracted, and that depends on merges not
yet made. K = 3 is the right choice, not a compromise; insertion into a
buffer inside the 64-byte block beats the same buffer in a separate pool by
~20% (same work, other memory).

## MST suite

`bench/mst_suite.c`, run with `make suite` (always `-O2`; `make suite
SUITE="real 5"` runs only the cases whose name contains `real`, best of 5).
Every variant is checked against the full-sort Kruskal (forest size and
weight) on:

| family | what it tests |
|---|---|
| `uniform` | G(n, m), uniform weights: the old benchmark, worst case for structure |
| `sbm` | stochastic block model, uniform weights or light in-block edges |
| `jsbm` | block model with Jaccard weights: the profile of real social graphs, at 4k - 400k vertices |
| `powerlaw` | Chung-Lu, hubs and a long tail |
| `geometric` | random geometric graph, weight = distance (road-like) |
| `ties` | integer weights 1..100: the `(w, v)` threshold |
| `edge` | disconnected, isolated vertices, all weights equal, starless vertices |
| `real` | SNAP graphs and BayAreaUs |

The real graphs come from `tools/fetch_mst_graphs.sh` (~5 minutes, into
`res/mst/`, ignored by git). They have no weights, so they get the
**Jaccard distance** of the two neighborhoods (`bench/jaccard.h`): light
inside a community, 1 on a bridge with nothing shared. Small ones become DOT
files (`tools/snap_to_dot.py`), large ones a binary edge list
(`tools/snap_to_bin.c`). bitcoin-otc keeps its real trust ratings.

### What the structure decides

Two measurements explain most of the table:

- **How much of the sorted list Kruskal must read.** On uniform random
  graphs the last MST edge is light (~n ln n of m edges are read), so the
  lazy methods (quick, filter) win. On every real graph it is **100%**:
  communities are joined by heavy bridges, or the graph is disconnected,
  and laziness saves nothing.
- **How many of the edges read are useful.** On facebook and email-eu only
  5-6% of them join the forest: the rest are inside a community, and a
  per-star filter drops them without sorting them. On sparse graphs
  (amazon, dblp) 30-36% are useful and stars have 2-3 edges.

Hence, best of 3, ms:

| graph | quick | filter | kbest | hybrid |
|---|---|---|---|---|
| gplus-ego (107k, deg 227) | 1667 | 567 | 615 | **527** (437 with K 8, target 8n) |
| jsbm dense 4k (deg 148) | 23.0 | 12.7 | 11.1 | **8.0** |
| facebook (4k, deg 44) | 8.2 | 8.0 | 6.0 | **6.0** |
| artist (50k, deg 32) | 74 | **46** | 76 | 51 |
| twitter-ego (81k, deg 33) | 144 | **114** | 160 | 126 |
| jsbm deg 29, 400k | 480 | **326** | 854 | 626 |
| pokec (1.6M, deg 27) | 2620 | **2387** | 4296 | 3476 |
| amazon (335k, deg 5.5) | 97 (full sort 82) | 152 | 324 | 291 |
| uniform 1M, deg 16 | 1397 | **862** | 3193 | 2149 |

- **Dense graphs with communities** (degree >= ~150): the per-star variants
  are ~3x faster than quick. Whether the hybrid beats filter there depends
  on hubs, see "Large graphs" below.
- **Degree ~30**: filter wins, and its lead over K-best grows with n (1.7x
  at 4k vertices, 2.5x at 400k): a heap operation per extraction costs more
  than a lazy quicksort on a contiguous array. The K-best wins on facebook
  and email-eu are a small-graph effect (everything in cache).
- **Large sparse graphs**: the plain full-sort Kruskal is often the
  fastest. Kruskal reads everything anyway, and every lazy structure is
  overhead.

## Hybrid: Filter-Kruskal rounds read by K-best

`graph_mst_filter_kbest_kruskal` keeps the rounds of Filter-Kruskal: a
global threshold tau from a sample, one `filter_pass` that drops the
internal edges, copies the light ones to a buffer and compacts the heavy
ones in place. The light buffer comes out of the pass still grouped by
source, so it is a `StarEdgeList` itself: K-best reads it (`kb_load`,
heapify, `kb_drain`) instead of a global lazy quicksort. Every light edge
weighs <= tau < every heavy one, so the rounds consume the edges in Kruskal
order. `target` starts at `FKB_TARGET * n` (default 2) and doubles.

The target is a trade-off: 8n helps gplus and hurts the degree ~30 graphs
(jsbm 40k: 55 ms against 39), so the default stays at 2n. Where it wins is
below.

## Large graphs: where the hybrid beats Filter-Kruskal

`bench/mst_big.c` (`make big`) runs every CPU variant (filter, hybrid,
K-best, quick star, lazy quicksort, full sort) on graphs of 10M - 151M
edges, read straight into a `StarEdgeList` from the binary format (no
adjacency structure, so 151M edges fit in 15 GB). `BIGFLAGS` is
compiled into `src/traversal.c`, so both can run at the same target
(`FS_TARGET` for filter, `FKB_TARGET` and `KB_K` for the hybrid):

```sh
tools/fetch_mst_graphs.sh big      # Orkut, Hollywood, Indochina + 4 large DC-SBM graphs
tools/gen_mst_grid.sh              # the grid below, res/mst/grid
make big BIG="grid 5 filter,hybrid"
make big BIG="grid 5 filter,hybrid" BIGFLAGS="-DFS_TARGET=8 -DFKB_TARGET=8 -DKB_K=8"
```

### Hubs

A **hub** is a vertex whose degree is far above the average, like a
celebrity in a social network: most users have a few hundred contacts, a
few have tens of thousands. The technical name of the property is a
heavy-tailed (scale-free when it is a power law, P(k) ~ k^-gamma) degree
distribution; the lower gamma, the stronger the hubs. It is independent of
**community structure** (groups densely connected inside, sparsely between
them): community structure is about *who* the neighbors are, hubs about
*how many* each vertex has. A block model has communities and no hubs,
Barabasi-Albert has hubs and no communities, real social graphs have both.

In a `StarEdgeList` a hub owns a star of thousands of contiguous edges.
The bench measures the concentration as the share of the edges held by the
1% largest stars: ~2% without hubs, 7-20% with them, 22% on gplus.

The generator (`tools/gen_mst_bin.c`) is a degree-corrected stochastic
block model (Karrer, Newman 2011): blocks of 1000 vertices, 85% of the
degree inside the block, a power-law weight per vertex for the hubs
(gamma = 0: none), Jaccard weights. Hubs merge repeated edges, so the real
degree is below the requested one.

### The grid

n = 300k, best configuration of each (filter at target 2n or 8n; hybrid
with K 3 / 2n or K 8 / 8n), best of 5, ratios against the other one, real
average degree in brackets. Identical runs vary by 5-9%, so 1.01-1.04 is a
tie.

| requested degree | no hubs (2%) | gamma 3.0 (~7%) | gamma 2.5 (~11%) | gamma 2.2 (~18%) |
|---|---|---|---|---|
| 100 | filter 1.48x (96) | filter 1.06x (89) | filter 1.07x (75) | filter 1.16x (54) |
| 180 | filter 1.14x (169) | tie 1.04x (150) | tie 1.01x (125) | **hybrid 1.13x** (89) |
| 260 | filter 1.08x (237) | **hybrid 1.13x** (207) | **hybrid 1.10x** (170) | tie 1.02x (120) |
| 340 | filter 1.10x (302) | **hybrid 1.13x** (260) | **hybrid 1.12x** (212) | **hybrid 1.09x** (149) |

Larger and real graphs, same comparison:

| graph | m | degree | top 1% stars | result |
|---|---|---|---|---|
| gplus-ego | 12.2M | 227 | 22% | **hybrid 1.42x** |
| DC-SBM 300k, hubs | 28.3M | 189 | 10% | **hybrid 1.13x** |
| DC-SBM 300k, no hubs | 40.3M | 269 | 2% | tie |
| DC-SBM 1M, no hubs | 57.5M - 93M | 115 - 186 | 2% | filter 1.09 - 1.34x |
| DC-SBM 1M, hubs | 44.4M - 68M | 89 - 136 | 12% | tie - filter 1.11x |
| Orkut | 117M | 76 | 17% | filter 1.13x |
| Pokec | 22.3M | 27 | 12% | filter 1.20x |

On gplus the gain is K-best's, not the target's: filter at 8n runs as at
2n (543 ms against 536), the hybrid drops to 378.

### Real graphs with large hubs

Three graphs picked for their hubs (`make big BIG=hub/`): Wikipedia links
(SNAP `wiki-topcats`), actor collaborations and a web crawl (LAW
`hollywood-2009` and `indochina-2004`, from SuiteSparse). Every CPU
variant at its defaults (target 2n, K 3), best of 3, ms:

| graph | m | degree | max degree | top 1% stars | filter | hybrid | K-best | quick star | lazy quick | full sort |
|---|---|---|---|---|---|---|---|---|---|---|
| wiki-topcats | 25.4M | 28 | 238k | 29.7% | **1779** | 2626 | 3777 | 5585 | 3287 | 2935 |
| hollywood | 56.4M | 99 | 11k | 20.7% | 5627 | **4373** | 4876 | 7206 | 7386 | 6784 |
| indochina | 151.0M | 41 | 256k | 45.5% | 18023 | **9821** | 10161 | 15727 | 14631 | 13160 |

Best configuration of each, as in the grid (filter at 8n: 3819, 7112,
19429 ms; hybrid at K 8 / 8n: 3231, 4655, 9576 ms):

- **indochina: hybrid 1.88x** (9576 against 18023). Filter-Kruskal is the
  slowest of all six there, slower than a full sort;
- **hollywood: hybrid 1.29x**, and K-best alone is second;
- **wiki-topcats: filter 1.48x**. At the same target 8n the hybrid wins
  1.18x, but only because filter slows down there.

Indochina (degree 41) wins by the largest margin measured so far, far
below the degree ~150 the grid suggested; wiki-topcats loses with 30% of
its edges in the top 1% stars. Neither the degree nor the share decides.

### The rule

The variable that decides is the **mean size of the 1% largest stars**
(printed by `make big` as "mean size"): how many edges a typical hub hands
to the refills. Every graph measured, sorted by it (best configuration of
each; identical runs vary by 5-9%, so 1.01-1.04 is a tie):

| graph | degree | top 1% stars | **mean size of the top 1%** | result |
|---|---|---|---|---|
| grid d100, no hubs | 96 | 2.2% | 109 | filter 1.48x |
| DC-SBM 1M, no hubs (two) | 115 - 186 | 2.1 - 2.2% | 128 - 201 | filter 1.09 - 1.34x |
| grid d180, no hubs | 169 | 2.2% | 184 | filter 1.14x |
| Pokec | 27 | 11.5% | 223 | filter 1.20x |
| grid d260, no hubs | 237 | 2.1% | 253 | filter 1.08x |
| DC-SBM 300k, no hubs | 269 | 2.1% | 284 | tie |
| grid d340, no hubs | 302 | 2.1% | 317 | filter 1.10x |
| grid d100, gamma 3.0 | 89 | 8.3% | 372 | filter 1.06x |
| **wiki-topcats** | 28 | 29.7% | 468 | **filter 1.48x** |
| grid d100, gamma 2.5 | 75 | 13.0% | 498 | filter 1.07x |
| grid d180, gamma 3.0 | 150 | 7.4% | 562 | tie 1.04x |
| DC-SBM 1M, hubs (two) | 89 - 136 | 11.6 - 12.8% | 578 - 799 | tie - filter 1.11x |
| grid d100, gamma 2.2 | 54 | 20.7% | 585 | filter 1.16x |
| grid d260, gamma 3.0 | 207 | 6.8% | 712 | hybrid 1.13x |
| Orkut | 76 | 16.7% | 719 | filter 1.13x |
| grid d180, gamma 2.5 | 125 | 11.6% | 733 | tie 1.01x |
| grid d340, gamma 3.0 | 260 | 6.5% | 841 | hybrid 1.13x |
| grid d180, gamma 2.2 | 89 | 18.8% | 851 | hybrid 1.13x |
| grid d260, gamma 2.5 | 170 | 10.9% | 932 | hybrid 1.10x |
| DC-SBM 300k, hubs | 189 | 10.3% | 981 | hybrid 1.13x |
| grid d260, gamma 2.2 | 120 | 18.0% | 1097 | tie 1.02x |
| **hollywood** | 99 | 20.7% | 1102 | **hybrid 1.29x** |
| grid d340, gamma 2.5 | 212 | 10.4% | 1106 | hybrid 1.12x |
| **indochina** | 41 | 45.5% | 1157 | **hybrid 1.88x** |
| grid d340, gamma 2.2 | 149 | 17.4% | 1312 | hybrid 1.09x |
| gplus-ego | 227 | 22.1% | 2699 | hybrid 1.42x |

**Below ~600 edges Filter-Kruskal wins or ties, above ~800 the hybrid
wins or ties, on every graph**; 700 - 800 is a grey zone (712 hybrid, 719
filter). The earlier rule, "hubs (>= ~7% in the top 1% stars) and a real
degree >= ~150", was a proxy for it: the mean size of the top stars grows
with both the share and the degree (it is about share x degree x 50), so
the rule held on the grid, where both move together, and breaks on the
real graphs, where they do not. Indochina has degree 41 but its top stars
hold 1157 edges each; wiki-topcats concentrates its hubs in a few huge
stars (the largest has 202k edges) while the rest of its top 1% is short,
468 on average. E[s^2]/E[s] does not decide either: 2936 on wiki-topcats,
the highest of all, and it loses.

The size of the top stars is an O(m) count on a `StarEdgeList`, so the
choice can be made before running either.

Why both are needed. The two algorithms run the same rounds and differ only
on the light edges of a round: filter sorts them all in one contiguous
array (each light edge moved ~log L times), the hybrid reads each star once
and drops the edges that became internal in bulk at the next refill,
without sorting them.

- **High degree** means that most light edges are useless: inside a
  community every vertex has tens or hundreds of light edges and the tree
  needs about one (useful edges: 49% at degree 4, 4% at 64, 1% at 256).
  Filter spends its sort on edges headed for the bin. At low degree almost
  every edge read is needed, and a quicksort step on a contiguous array is
  cheaper than a heap operation per extraction.
- **Hubs** put those useless edges in a few long stars. How long is what
  counts (the rule above): the fixed costs of a star are paid once per
  refill, the edges it drops in bulk grow with its size. A hub joins the
  tree early, then nearly its whole star is internal and one sequential
  refill drops it. Without hubs every star holds a handful of light edges
  per round, and its fixed costs (64-byte block, heap entry, a jump in
  memory) are not paid back.

Use the hybrid when the 1% largest stars hold ~800 edges or more on
average; Filter-Kruskal otherwise. Its best configuration is K = 8 /
target 8n on most of the grid, K = 3 / 2n on hollywood (4373 against
4655).

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
