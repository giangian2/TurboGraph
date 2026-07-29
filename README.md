![Logo del progetto](./res/TurboGraph_logo.svg)
# TurboGraph

A high-performance graph engine written in C, built to be the foundation for
LLM memory systems and, later, large-scale code intelligence — with CUDA
kernels for the algorithms that benefit from massive parallelism.

---

## Motivation

Almost every serious LLM application today — agents, RAG pipelines, "memory"
layers, knowledge bases — ends up representing its state as a **graph**: entities
and relations, documents and citations, code and dependencies, conversation
turns and their references.

The problem is *where* those graphs live. The current ecosystem is built almost
entirely on top of Python and high-level frameworks. That comes with real costs:

- **Memory overhead.** Objects, dictionaries, and boxed values scatter graph
  data all over the heap. A graph that could fit in a few tightly packed arrays
  instead sprawls across millions of small allocations.
- **Poor cache locality.** Traversals chase pointers across unrelated memory
  regions. On modern hardware, where a cache miss can cost hundreds of cycles,
  this alone can dominate runtime.
- **Algorithmic lock-in.** You are limited to whatever algorithms and data
  representations the framework exposes. If the traversal you need isn't there,
  or isn't implemented the way your workload wants, you're stuck.
- **No path to the GPU.** These stacks rarely offer a clean way to push the
  heavy graph computations onto hardware built for exactly that kind of parallel
  work.

The result is that the layer everything else depends on — the graph itself — is
usually the *least* optimized part of the system.

## The idea

**Build the graph core in C, and make it fast enough to be the thing everything
else is built on.**

The goal is a small, dependency-free library that treats memory layout,
cache locality, and predictable performance as first-class concerns — not an
afterthought hidden behind a framework. On top of that solid CPU core, add
**CUDA kernels** for the algorithms where parallel computation pays off
(large-scale traversals, shortest paths, connectivity, PageRank-style
iterations, and so on).

Design principles:

- **Data-oriented layout.** Prefer flat, contiguous arrays (CSR / compressed
  sparse row) over pointer-chasing structures wherever the workload allows, so
  the hardware prefetcher and caches actually work *for* you.
- **You choose the representation.** The same interface sits in front of several
  storage strategies; pick the one that fits your access pattern.
- **Zero magic, zero hidden cost.** Plain C, explicit ownership, no runtime you
  can't see through.
- **GPU-ready.** The layout is chosen so the same data can be handed to CUDA
  kernels without expensive reshuffling.

## Architecture

The core exposes a single `Graph` type behind a small vtable (`GraphOps`), with
three interchangeable representations sharing the same API:

| Representation | Backing store | Edge lookup | Neighbor sweep | Memory | Best for |
|----------------|---------------|-------------|----------------|--------|----------|
| `GRAPH_MATRIX` | flat `n×n` adjacency matrix | `O(1)` | `O(n)` | `O(n²)` | dense graphs |
| `GRAPH_LIST`   | adjacency lists (chains)    | `O(deg)` | `O(deg)` | `O(n+m)` | sparse, mutable graphs |
| `GRAPH_STAR`   | forward/backward star (CSR) | `O(deg)` | `O(deg)`, contiguous | `O(n+m)` | fastest traversals, immutable |

`GRAPH_STAR` is the CSR-style layout — built once from a validated edge list —
that keeps neighbors packed contiguously in memory. It is the representation
meant to feed the GPU path.

A uniform neighbor **iterator** (`GraphIter`) works across all three
representations, so algorithms are written once and run against any backing
store.

### What's implemented today

- Three graph representations behind one interface (matrix / adjacency list / CSR star).
- Directed and undirected, weighted edges.
- Core operations: add/remove edge, weight lookup, in/out degree, neighbor iteration.
- Traversals: **BFS** and **DFS**, returning visit order, traversal-tree parents, and distances.
- **Kruskal MST** support with a custom quicksort over the edge set.
- **Graphviz DOT import** (edge-list subset, e.g. SNAP exports) and **DOT export**,
  including visualization of a traversal (distances + highlighted tree edges).

## Roadmap

1. **CPU core (in progress).** Solidify representations, traversals, and the
   algorithm set; lock down a stable, allocation-conscious API.
2. **CUDA kernels.** Offload the parallel-friendly algorithms (BFS/SSSP,
   connected components, PageRank-style iterations, …) to the GPU, operating
   directly on the CSR layout.
3. **First application — an AST engine.** Use the core to *wire up entire
   codebases* as **giant graphs** (ASTs, symbols, call/def relations,
   dependencies) and run ultra-fast analysis over them with CUDA. This is where
   the whole design pays off: real code produces graphs large enough that layout
   and parallelism stop being nice-to-haves and start being the difference
   between feasible and not.

The end goal is a memory/graph substrate fast enough to sit underneath LLM
applications and code-intelligence tools without becoming the bottleneck.

## Building

```sh
make            # builds bin/libgraph.a and the bin/main demo
make run        # runs the demo
make memcheck   # runs the demo under valgrind
make clean
```

Requirements: a C11 compiler (`gcc`) and `make`. To render exported DOT files
you also need Graphviz:

```sh
dot -Tpng twitch_bfs.dot -o twitch_bfs.png
```

## Project layout

```
include/     public headers (Graph.h, Importer.h, Stack.h, Queue.h)
src/         library sources (graph, list, queue, stack, importer) + main.c demo
bin/         built library (libgraph.a) and demo binary
build/       object files
*.dot        sample graphs (europe_roads, twitch, …) and exports
```

## Status

Early stage and under active development. The CPU core is taking shape; CUDA
kernels and the AST application are next on the roadmap. APIs are expected to
change.

## License

See [LICENSE](LICENSE).
