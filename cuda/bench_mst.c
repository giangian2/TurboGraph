/*
 * Boruvka on the GPU against the Kruskal variants on the CPU.
 *
 *   bench_mst [n] [avg_degree] [runs]       random graph, continuous weights
 *   bench_mst file.dot [runs]               imported (undirected) graph
 *
 * Run it from the project root. The CPU variants sort their edge list in
 * place, so it is restored outside the timer. The GPU library is loaded at
 * run time (build/boruvka_cuda.dll or build/libboruvka_cuda.so, see
 * cuda/README.md): without it the CPU numbers are still printed. The GPU time
 * is split into transfer and compute: the edge list is uploaded as it is, 16
 * bytes per edge, and split into arrays on the GPU.
 */
#include "../include/Generator.h"
#include "../include/Graph.h"
#include "../include/Importer.h"
#include "../include/Traversal.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define GPU_LIB "build/boruvka_cuda.dll"
#else
#include <dlfcn.h>
#include <time.h>
#define GPU_LIB "build/libboruvka_cuda.so"
#endif

/* must match BoruvkaTimes in cuda/boruvka.cu */
typedef struct
{
    float h2d_ms, compute_ms, d2h_ms;
    int   rounds;
} BoruvkaTimes;
typedef int (*boruvka_fn)(int, size_t, const GraphEdge*, int*, BoruvkaTimes*);

static double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e3 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
#endif
}

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

/* Every undirected edge once (u < v), grouped by source: the order the
 * Kruskal variants expect */
static GraphEdge* collect_edges(const Graph* g, size_t* count)
{
    GraphEdge* edges = malloc(sizeof(GraphEdge) * (g->m > 0 ? g->m : 1));
    if (!edges)
        return NULL;
    *count = 0;
    for (int u = 0; u < g->n; u++)
    {
        GraphIter it;
        int       v;
        double    w;
        graph_iter_out(g, u, &it);
        while (graph_iter_next(&it, &v, &w))
            if (g->directed || u < v)
                edges[(*count)++] = (GraphEdge){u, v, w};
    }
    return edges;
}

static double weight_of(const GraphEdge* e, int k)
{
    double s = 0;
    for (int i = 0; i < k; i++)
        s += e[i].w;
    return s;
}

static const char* verdict(int size, double weight, int ref_size, double ref_weight)
{
    return size == ref_size && fabs(weight - ref_weight) <= 1e-9 * fabs(ref_weight) ? "OK" : "MISMATCH";
}

static uint64_t rng_state = 88172645463325252ULL;
static uint64_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static Graph* random_graph(int n, double avg_deg)
{
    size_t     m     = (size_t)((double)n * avg_deg / 2.0);
    GraphEdge* edges = malloc(sizeof(GraphEdge) * m);
    if (!edges)
        return NULL;
    for (size_t i = 0; i < m; i++)
    {
        int u, v;
        do
        {
            u = (int)(rnd() % (uint64_t)n);
            v = (int)(rnd() % (uint64_t)n);
        } while (u == v);
        edges[i] = (GraphEdge){u, v, ((double)(rnd() >> 11) + 1.0) / 9007199254740992.0};
    }
    Graph* g = graph_from_edges(n, false, edges, m, GRAPH_STAR);
    free(edges);
    return g;
}

/* Best of `runs` of `body`, restoring the edge list outside the timer. The
 * last run's MST is left in `mst`, the earlier ones are freed. */
#define BEST_OF(runs, body, out)            \
    do                                      \
    {                                       \
        double best_ = 1e30;                \
        for (int r_ = 0; r_ < (runs); r_++) \
        {                                   \
            memcpy(work, edges, bytes);     \
            double t0_ = now_ms();          \
            body;                           \
            double t_ = now_ms() - t0_;     \
            if (t_ < best_)                 \
                best_ = t_;                 \
            if (r_ < (runs) - 1)            \
                free(mst);                  \
        }                                   \
        (out) = best_;                      \
    } while (0)

int main(int argc, char** argv)
{
    Graph* g;
    int    runs = 5;
    if (argc > 1 && strstr(argv[1], ".dot"))
    {
        g    = graph_import_dot(argv[1], GRAPH_STAR);
        runs = argc > 2 ? atoi(argv[2]) : 5;
    }
    else
    {
        int    n   = argc > 1 ? atoi(argv[1]) : 100000;
        double deg = argc > 2 ? atof(argv[2]) : 8;
        runs       = argc > 3 ? atoi(argv[3]) : 5;
        g          = random_graph(n, deg);
    }
    if (!g)
        return fprintf(stderr, "graph build failed\n"), 1;
    if (runs < 1)
        runs = 1;

    size_t     m;
    GraphEdge* edges = collect_edges(g, &m);
    size_t     bytes = sizeof(GraphEdge) * (m ? m : 1);
    GraphEdge* work  = malloc(bytes);
    int*       idx   = malloc(sizeof(int) * (size_t)g->n);
    if (!edges || !work || !idx)
        return fprintf(stderr, "out of memory\n"), 1;
    printf("n = %d, m = %zu (avg degree %.1f)\n", g->n, m, 2.0 * (double)m / g->n);

    int        size = 0;
    double     t_kruskal, t_quick, t_star, t_filter, t_filter_star;
    GraphEdge* mst;

    BEST_OF(runs, mst = graph_mst_kruskal(g, work, &size), t_kruskal);
    int    ref_size   = size;
    double ref_weight = weight_of(mst, size);
    free(mst);

    BEST_OF(runs, mst = graph_mst_quick_kruskal(g, work, &size), t_quick);
    printf("quick kruskal      : %9.2f ms  %s\n", t_quick,
           verdict(size, weight_of(mst, size), ref_size, ref_weight));
    free(mst);

    BEST_OF(runs, mst = graph_mst_quick_star_kruskal(g, work, &size), t_star);
    printf("quick star kruskal : %9.2f ms  %s\n", t_star,
           verdict(size, weight_of(mst, size), ref_size, ref_weight));
    free(mst);

    BEST_OF(runs, mst = graph_mst_filter_kruskal(g, work, &size), t_filter);
    printf("filter kruskal     : %9.2f ms  %s\n", t_filter,
           verdict(size, weight_of(mst, size), ref_size, ref_weight));
    free(mst);

    BEST_OF(runs, mst = graph_mst_filter_star_kruskal(g, work, &size), t_filter_star);
    printf("filter star kruskal: %9.2f ms  %s\n", t_filter_star,
           verdict(size, weight_of(mst, size), ref_size, ref_weight));
    free(mst);

    printf("kruskal (full sort): %9.2f ms\n", t_kruskal);

    boruvka_fn gpu = load_gpu();
    if (!gpu)
    {
        printf("boruvka GPU        : not available (cannot load " GPU_LIB ", see cuda/README.md)\n");
        return 0;
    }

    BoruvkaTimes t = {0}, best = {1e30f, 1e30f, 1e30f, 0};
    int          gsize = gpu(g->n, m, edges, idx, &t); /* warm-up: context creation, module load */
    double       wall  = 1e30;
    for (int r = 0; r < runs && gsize >= 0; r++)
    {
        double t0 = now_ms();
        gsize     = gpu(g->n, m, edges, idx, &t);
        double dt = now_ms() - t0;
        if (dt < wall)
            wall = dt;
        if (t.h2d_ms + t.compute_ms + t.d2h_ms < best.h2d_ms + best.compute_ms + best.d2h_ms)
            best = t;
    }
    if (gsize < 0)
        return fprintf(stderr, "boruvka_gpu failed\n"), 1;

    double gpu_weight = 0;
    for (int i = 0; i < gsize; i++)
        gpu_weight += edges[idx[i]].w;
    double total = (double)best.h2d_ms + best.compute_ms + best.d2h_ms;

    printf("boruvka GPU        : %9.2f ms  %s\n", total, verdict(gsize, gpu_weight, ref_size, ref_weight));
    printf("  h2d %.2f + compute %.2f + d2h %.2f ms, %d rounds; wall clock of the call "
           "(includes device allocation): %.2f ms\n",
           best.h2d_ms, best.compute_ms, best.d2h_ms, best.rounds, wall);
    printf("  speedup vs quick kruskal: %.2fx compute only, %.2fx with transfers\n",
           t_quick / best.compute_ms, t_quick / total);
    return 0;
}
