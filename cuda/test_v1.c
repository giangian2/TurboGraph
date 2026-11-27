#include <stdio.h>
#include "../include/Graph.h"
#include "../include/Generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define GPU_LIB "build/boruvka_cuda_v1.dll"
#else
#include <dlfcn.h>
#include <time.h>
#define GPU_LIB "build/libboruvka_cuda_v1.so"
#endif

typedef GraphEdge* (*boruvka_fn)(const Graph* g, int* mst_size);

static boruvka_fn load_gpu(void)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(GPU_LIB);
    return lib ? (boruvka_fn)GetProcAddress(lib, "boruvka_gpu") : NULL;
#else
    void* lib = dlopen("./" GPU_LIB, RTLD_NOW);
    return lib ? (boruvka_fn)dlsym(lib, "boruvka_gpu") : NULL;
#endif
}

static double total_weight(const GraphEdge* edges, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; i++)
        sum += edges[i].w;
    return sum;
}

int main()
{
    Graph* g = generate_random_graph(0.05, 10000, true, false, 126, GRAPH_STAR);
    if (!g)
        return 1;

    boruvka_fn gpu_mst_compute = load_gpu();

    StarEdgeList edges = graph_star_edge_list(g);
    int          size  = 0;
    GraphEdge*   mst   = edges.edges ?  gpu_mst_compute(g, &size) : NULL;

    double weight = mst ? total_weight(mst, size) : -1.0;
    printf("Kruskal test (CLRS 23.4): %d edges, weight %.0f\n", size, weight);

    free(mst);
    free(edges.edges);
    graph_free(g);
}