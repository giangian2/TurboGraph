![TurboGraph logo](./res/TurboGraph_logo.svg)

# TurboGraph

**An ultra-light, very fast graph engine written in C.**

TurboGraph keeps a graph in a compressed sparse row (CSR) layout, a single
block of flat arrays that the CPU caches can actually work with, and runs
graph algorithms on it with as little overhead between the algorithm and the
hardware as possible. There are no dependencies, no runtime and no hidden
costs. It is plain C11 that you can read down to the instruction.

It is meant to be a foundation for two kinds of systems:

- **Codebase knowledge graphs for AI agents.** A whole repository becomes a
  graph of files, symbols, definitions, calls and dependencies. An agent can
  then ask structural questions, such as what depends on this function, what
  a change would affect, or how two modules are connected, and get answers in
  milliseconds instead of re-reading the code.
- **Graph databases for analytics.** It can serve as the storage and compute
  layer of a database that runs traversals, connectivity, shortest paths and
  spanning trees over large graphs held in memory.

---

## Why

Most graph tooling that LLM applications use today sits on Python and
high-level frameworks. The graph, which is the layer everything else depends
on, is usually the least optimized part of the system:

- objects and dictionaries scatter the graph over millions of small heap
  allocations;
- traversals chase pointers across unrelated memory, and on modern hardware
  each cache miss costs more than the work done per edge;
- you are limited to the algorithms and representations the framework
  exposes.

TurboGraph takes the opposite approach. It treats memory layout and the cost
of every instruction in the inner loops as design decisions, not
afterthoughts.

## Design

### CSR first

The CSR representation (`GRAPH_STAR`) is the "fast" format and the one the
engine is optimized around:

- **One contiguous block.** The header, offsets, targets and weights for both
  out-arcs and in-arcs live in a single allocation. The arcs of `u` are the
  range `to[heads[u] .. heads[u+1])`, so walking a neighborhood is a plain
  array scan.
- **Position independent.** The header stores byte offsets, not pointers, so
  the block stays valid wherever it is copied: another process, a mapped
  file, a socket, or device memory.
- **Built in O(n + m).** The constructor is a counting sort, with no
  temporary allocations.

Two other representations share the same interface, for the cases where CSR
does not fit:

| Representation | Storage | Neighbor sweep | Memory | Use it for |
|---|---|---|---|---|
| `GRAPH_STAR` | forward/backward star (CSR) | `O(deg)`, contiguous | `O(n+m)` | traversals and analytics; immutable |
| `GRAPH_LIST` | adjacency lists | `O(deg)` | `O(n+m)` | building or mutating a graph |
| `GRAPH_MATRIX` | flat `n×n` matrix | `O(n)` | `O(n²)` | small dense graphs |

### One algorithm, specialized per representation

Each algorithm is written once, generic over a neighbor iterator, and
instantiated per representation:

```c
/* written once */
static inline __attribute__((always_inline)) void
bfs_visit_with(const Graph* g, int root, Traversal* t, IterOutFn expand, IterNextFn next);

/* CSR instance: the star iterators are inline constants, so the arc loop has no calls */
static void bfs_visit_star_out(const Graph* g, int root, Traversal* t)
{
    bfs_visit_with(g, root, t, star_iter_out, star_iter_next);
}

/* the representation is checked once per visit, never once per arc */
static void bfs_visit(const Graph* g, int root, bool in, Traversal* t)
{
    if (g->repr == GRAPH_STAR) ...                      /* inlined CSR loop */
    else bfs_visit_with(g, root, t, ..., g->ops->iter_next); /* vtable fallback */
}
```

In the CSR instance the compiler sees the iterator bodies (they live in the
private header `src/star.h`) and knows which ones are called. It inlines
them, keeps the cursor in registers and folds away the unused weight branch.
The inner loop of the BFS goes from more than 20 instructions plus a call
per arc to 8 instructions per arc. The list and matrix representations keep
working through the vtable, with the same source code.

### No hidden costs

- Explicit ownership. Every allocation has one owner, released with
  `*_cleanup()` or the `AUTO_FREE_*` scope guards.
- No global state, so the library is safe to use from several threads on
  different graphs.
- Debug logging (`LOG_DEBUG` / `LOG_ERROR`) compiles to nothing unless it
  is enabled.

## Status

The project is at an early stage and under active development. APIs may
still change.

### Implemented

- **Graphs:** directed and undirected, weighted, in three representations
  behind one API (`Graph.h`), with a uniform neighbor iterator
  (`GraphIter`).
- **Traversals:** BFS and DFS (iterative DFS on an explicit stack), returning
  visit order, tree parents and distances.
- **Connectivity:** connected components for undirected graphs and strongly
  connected components (Kosaraju) for directed ones, returned in a CSR-like
  `Components` block.
- **Minimum spanning forest:** Kruskal with union-find.
- **Edge sorting** (`Sorting.h`), all based on Hoare partition:
  - iterative quicksort on an explicit stack;
  - partial quicksort, which only sorts the first `k` positions;
  - a lazy iterator that returns one edge per step in weight order and only
    does the work needed for the next edge.
- **I/O:** Graphviz DOT import (edge lists with numeric weights) and DOT
  export, including a traversal overlay.
- **Tooling:** instruction-exact profiling with callgrind, and a per-function
  diff between two runs (`tools/`).

### Measured performance

The test graph is the Bay Area road network (`res/BayAreaUs.dot`, 9th DIMACS
Challenge), with 321,270 vertices and 800,172 arcs, stored as `GRAPH_STAR`.
Measured on an Intel i5-7200U under WSL2 with `-O2`; times are medians.

| Operation | Before the inline CSR instances | Now |
|---|---|---|
| BFS, full graph | ~16 ms | **~10 ms** |
| Connected components (undirected) | 17.3 ms | **10.9 ms** |
| Strongly connected components (directed) | 37.8 ms | **29.6 ms** |
| DOT import of the whole graph | | ~0.2 s |

The inline CSR refactor was checked against the previous version: the
order, parents, distances and components it produces are identical (hashed
over the four sample graphs, in both STAR and LIST).

### Known limitations

- `graph_create()` does not build `GRAPH_MATRIX` yet: it always returns an
  error.
- In a directed `GRAPH_LIST`, in-arcs are found by scanning every chain,
  which costs `O(n + m)` per vertex. Kosaraju on a large directed list is
  therefore impractical; convert to `GRAPH_STAR` first.
- DFS gains little from inlining: its iterators must live in the stack
  frames, in memory. A smaller CSR frame of vertex, cursor and end is
  planned.
- `graph_mst_prim()` and the quick Kruskal variants are declared but not yet
  implemented.
- `graph_convert()` between representations is not implemented yet.

## Roadmap

1. **CSR core.** Inline instances for every algorithm, `graph_convert()`,
   Dijkstra / A* and bidirectional search, a compact DFS frame, and vertex
   renumbering for graphs whose ids have poor locality.
2. **Query engine.** A server that loads a graph once and answers queries
   concurrently. Each query runs as a stackful coroutine from
   [TurboRoutine](https://github.com/giangian2/TurboRoutine) on a worker
   pool, sliced by cooperative checkpoints, so long traversals never starve
   short queries. See [ARCHITECTURE.md](ARCHITECTURE.md). A working
   prototype exists outside this repository.
3. **Codebase knowledge graphs.** Turn whole repositories into graphs (ASTs,
   symbols, call and definition relations, dependencies) and expose fast
   structural queries to AI agents.
4. **Analytics.** Shortest paths at scale (contraction hierarchies),
   PageRank-style iterations, and GPU kernels on the same position
   independent CSR block.

## Building

```sh
make            # bin/libgraph.a and the bin/main demo (-O0, debuggable)
make OPT=-O2    # optimized build; switching OPT rebuilds from scratch
make DEBUG=1    # LOG_DEBUG / LOG_ERROR on stderr
make run        # runs the demo
make memcheck   # runs the demo under valgrind
make profile    # profiles the demo with callgrind (see below)
make clean
```

You need a C11 compiler (`gcc`) and `make`. To render exported DOT files you
also need Graphviz:

```sh
dot -Tsvg out/bayarea.dot -o bayarea.svg
```

The demo (`src/main.c`) checks Kruskal against the CLRS 23.4 example, then
imports the Bay Area graph and runs DFS, strongly connected components and
a minimum spanning forest on it.

## Profiling

`make profile` rebuilds at `-O2` and runs the demo under callgrind, which
counts the instructions actually executed and attributes them to source
lines. It does not sample, so two runs of the same binary give exactly the
same numbers. That makes it reliable for asking "did my change reduce the
work?", even on a noisy laptop.

```sh
make profile                                     # whole program
make profile PROFFLAGS="-f graph_dfs"            # only inside graph_dfs
make profile PROFFLAGS="-f graph_dfs -c -B"      # also simulate cache and branch predictor
make profile PROFFLAGS="-f graph_dfs -l"         # annotate the sources line by line
make profile PROFFLAGS="-f graph_dfs -s before"  # store a baseline
make profile PROFFLAGS="-f graph_dfs -d before"  # per-function delta against it
tools/profile.sh --help                          # every option
```

Always profile at `-O2`. At `-O0` the profile is dominated by accessors that
the real build inlines away. Remember that `Ir` counts instructions, not
time: `-c` and `-B` add cache misses and mispredicted branches, which on
large graphs often matter more.

## Project layout

```
include/     public API: Graph.h, Traversal.h, Sorting.h, Importer.h, Export.h,
             Stack.h, Queue.h, DHeap.h, FibHeap.h, UnionFInd.h, hashmap.h
src/         library sources, private headers and the demo:
               internal.h   shared by every representation (iterator signatures)
               star.h       CSR internals: block accessors, inline iterators
               *_impl.c     the three representations
               traversal.c  BFS, DFS, components, Kruskal
               main.c       demo, not part of the library
tools/       profile.sh, cgdiff.c
res/         sample graphs (Bay Area roads, Europe roads, Twitch)
out/         generated output: DOT exports, callgrind profiles (git-ignored)
```

## Contributors

- **Gianluca Bianchi** ([@giangian2](https://github.com/giangian2)), author
  and maintainer.

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).
