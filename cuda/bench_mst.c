/*
 * Boruvka on the GPU against the Kruskal variants on the CPU.
 *
 *   bench_mst [n] [avg_degree] [runs]       random graph, continuous weights
 *   bench_mst file.dot [runs]               imported (undirected) graph
 *
 * The CPU variants sort their edge list in place, so it is restored outside
 * the timer. The GPU time is split into transfer and compute: the edge list
 * is uploaded as it is, 16 bytes per edge, and split into arrays on the GPU.
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
#include <time.h>
#include <windows.h>

#define popen _popen
#define pclose _pclose


static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e3 / (double)f.QuadPart;
}

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

#define BEST_OF(runs, body, out)                  \
    do                                            \
    {                                             \
        double best_ = 1e30;                      \
        for (int r_ = 0; r_ < (runs); r_++)       \
        {                                         \
            memcpy(work, edges, bytes);           \
            double t0_ = now_ms();                \
            body;                                 \
            double t_ = now_ms() - t0_;           \
            if (t_ < best_)                       \
                best_ = t_;                       \
        }                                         \
        (out) = best_;                            \
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

    size_t     m;
    GraphEdge* edges = collect_edges(g, &m);
    size_t     bytes = sizeof(GraphEdge) * (m ? m : 1);
    GraphEdge* work  = malloc(bytes);
    printf("n = %d, m = %zu (avg degree %.1f)\n", g->n, m, 2.0 * (double)m / g->n);

    /* the GPU side is a 64 bit executable: hand it the edges through a file */
    FILE* f = fopen("build/edges.bin", "wb");
    if (!f || fwrite(edges, sizeof(GraphEdge), m, f) != m)
        return fprintf(stderr, "cannot write build/edges.bin\n"), 1;
    fclose(f);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "build\\boruvka_cli.exe build/edges.bin %d %zu %d", g->n, m, runs);
    FILE* p = popen(cmd, "r");
    int   gsize = -1, rounds = 0;
    double gw = 0, bh = 0, bc = 0, bd = 0;
    if (!p || fscanf(p, "%d %lf %d %lf %lf %lf", &gsize, &gw, &rounds, &bh, &bc, &bd) != 6)
        return fprintf(stderr, "boruvka_cli failed\n"), 1;
    pclose(p);
    double best_total = bh + bc + bd;

    int    size;
    double t_k, t_q, t_s, ref_w;
    GraphEdge* mst;

    BEST_OF(runs, { mst = graph_mst_kruskal(g, work, &size); if (r_ < runs - 1) free(mst); }, t_k);
    ref_w = weight_of(mst, size);
    int ref_size = size;
    free(mst);
    BEST_OF(runs, { mst = graph_mst_quick_kruskal(g, work, &size); if (r_ < runs - 1) free(mst); }, t_q);
    free(mst);
    BEST_OF(runs, { mst = graph_mst_quick_star_kruskal(g, work, &size); if (r_ < runs - 1) free(mst); }, t_s);
    free(mst);

    printf("kruskal (full sort): %9.2f ms\n", t_k);
    printf("quick kruskal      : %9.2f ms\n", t_q);
    printf("quick star kruskal : %9.2f ms\n", t_s);
    printf("boruvka GPU        : %9.2f ms  (h2d %.2f + compute %.2f + d2h %.2f, %d rounds)\n",
           best_total, bh, bc, bd, rounds);
    printf("  compute only     : %9.2f ms   speedup vs quick: %.2fx (compute), %.2fx (with transfers)\n", bc,
           t_q / bc, t_q / best_total);
    printf("check: GPU %d edges, weight %.9f | CPU %d edges, weight %.9f -> %s\n", gsize, gw, ref_size,
           ref_w, (gsize == ref_size && fabs(gw - ref_w) <= 1e-9 * fabs(ref_w)) ? "OK" : "MISMATCH");
    return 0;
}
