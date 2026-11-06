# cuda/ — Boruvka on the GPU

An optional add-on: the C11 engine does not depend on it and the top-level
`Makefile` does not build it. It holds a CUDA minimum spanning forest and the
benchmark that compares it with the Kruskal variants of `src/traversal.c`.

| file | what |
|---|---|
| `boruvka.cu` | the algorithm. Exports `boruvka_gpu()` with a plain C ABI |
| `bench_mst.c` | benchmark, plain C: builds the graph, runs the CPU variants, loads the GPU library at run time and checks that every result has the same forest size and weight |
| `build.sh` / `build.bat` | Linux / Windows build of the library and the benchmark |

The GPU side takes the same edge array the CPU variants take (`GraphEdge`:
`int u, v; double w`) and returns the indices of the forest edges. It knows
nothing about `Graph` or the CSR: the library is loaded with `dlopen` /
`LoadLibrary`, so the engine and nvcc never have to share a toolchain.

## Build and run

From the project root.

Linux (CUDA toolkit and gcc):

    cuda/build.sh                 # ARCH=sm_86 cuda/build.sh for a specific GPU
    bin/bench_mst 1000000 16 3    # n, average degree, runs
    bin/bench_mst res/BayAreaUs_undirected.dot

Windows (CUDA toolkit, Visual Studio for nvcc's host compiler, and a **64 bit**
gcc on the PATH, e.g. WinLibs; a 32 bit MinGW cannot load a 64 bit DLL):

    cuda\build.bat                # set ARCH=sm_86 for another GPU (default sm_50)
    bin\bench_mst 1000000 16 3

If the library is missing, the benchmark still prints the CPU numbers.

## How the algorithm works

Each round works on the edges that still join two different components:

1. every component finds its lightest incident edge with two `atomicMin`
   passes (weight key, then lowest position among ties: a strict total
   order, so the picks form no cycle longer than a mutual pair);
2. each component hooks onto the other end of its edge; in a mutual pair the
   smaller id stays a root;
3. pointer jumping flattens the hook forest;
4. the edges are relabeled to component ids and the internal ones are
   compacted away (scan + scatter).

The number of components at least halves per round. Results and the
comparison with the CPU algorithms are in `docs/MST.md`.
