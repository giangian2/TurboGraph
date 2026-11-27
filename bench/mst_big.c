/*
 * Filter-Kruskal against its K-best variants, and the other CPU variants
 * (lazy star quicksort, lazy quicksort, full sort), on large clustered and
 * hub-dominated graphs.
 *
 *   make big                                   every graph found, best of 3
 *   make big BIG="orkut 5"                     only the names containing orkut, best of 5
 *   make big BIGFLAGS="-DFS_TARGET=8 -DFKB_TARGET=8 -DKB_K=8"
 *   make big BIG="grid 5 filter,hybrid"        the grid, without K-best
 *
 * Besides the named graphs below, every .bin in res/mst/grid is measured
 * (tools/gen_mst_grid.sh). Each graph ends with one RESULT line, easy to grep.
 *
 * The graphs are the binary edge lists (bench/jaccard.h) that
 * tools/fetch_mst_graphs.sh downloads or generates; a missing file is
 * skipped. They are read straight into a StarEdgeList: the format is sorted
 * by (u, v) with u < v, already grouped by source. No adjacency structure
 * is built (the algorithms only read n from the graph), so a graph of 100M
 * edges fits: the list, one working copy and the light buffer of a round,
 * 16 bytes per edge each.
 *
 * BIGFLAGS is compiled into src/traversal.c, so the rounds of both
 * Filter-Kruskal (FS_TARGET) and the hybrid (FKB_TARGET, KB_K) can be set
 * to the same target: the only fair way to tell what K-best adds.
 *
 * The forests are checked against each other (size and weight), not against
 * a full sort: three independent algorithms agreeing is the check.
 */
#define _POSIX_C_SOURCE 200809L /* clock_gettime, strdup and dirent under -std=c11 */
#include "../include/Graph.h"
#include "../include/Traversal.h"
#include "jaccard.h"
#include <dirent.h>
#include <math.h>
#include <time.h>

#ifndef BIGFLAGS
#define BIGFLAGS ""
#endif

static const struct
{
    const char* name;
    const char* path;
    const char* what;
} GRAPHS[] = {
    {"syn/c1m-d120", "res/mst/big-c1m-d120.bin", "DC-SBM n 1M, blocks 1k, in 100 out 20, no hubs"},
    {"syn/c1m-d120-hub", "res/mst/big-c1m-d120-hub.bin", "same, hubs (gamma 2.5)"},
    {"syn/c300k-d300", "res/mst/big-c300k-d300.bin", "DC-SBM n 300k, blocks 1k, in 260 out 40, no hubs"},
    {"syn/c300k-d300-hub", "res/mst/big-c300k-d300-hub.bin", "same, hubs (gamma 2.5)"},
    {"real/gplus-ego", "res/mst/gplus-ego.bin", "Google+ ego networks"},
    {"real/pokec", "res/mst/pokec.bin", "Pokec social network"},
    {"real/orkut", "res/mst/orkut.bin", "Orkut social network"},
    {"hub/wiki-topcats", "res/mst/wiki-topcats.bin", "Wikipedia links, hubs up to deg 238k"},
    {"hub/hollywood", "res/mst/hollywood.bin", "actor collaborations, deg ~99, 20k hubs"},
    {"hub/indochina", "res/mst/indochina.bin", "web crawl, hubs up to deg 256k"},
};

typedef GraphEdge* (*mst_fn)(const Graph*, StarEdgeList, int*);

/* The variants on a plain EdgeList: a StarEdgeList is one */
static GraphEdge* run_quick(const Graph* g, StarEdgeList l, int* s)
{
    return graph_mst_quick_kruskal(g, star_as_edge_list(l), s);
}
static GraphEdge* run_kruskal(const Graph* g, StarEdgeList l, int* s)
{
    return graph_mst_kruskal(g, star_as_edge_list(l), s);
}

static const struct
{
    const char* name;
    mst_fn      fn;
} VARIANTS[] = {
    {"filter", graph_mst_filter_star_kruskal},
    {"hybrid", graph_mst_filter_kbest_kruskal},
    {"kbest", graph_mst_kbest_star_kruskal},
    {"q-star", graph_mst_quick_star_kruskal},
    {"quick", run_quick},
    {"kruskal", run_kruskal},
};
enum
{
    N_VARIANTS = sizeof(VARIANTS) / sizeof(VARIANTS[0])
};

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static int cmp_int_desc(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;
    return (x < y) - (x > y);
}

/* Shape of the stars, the quantities that decide between filter and hybrid:
 * the largest star, the share of edges in the 1% largest stars, their mean
 * size (the one that predicts the winner, see docs/MST.md) and
 * E[s^2] / E[s], the size of the star an edge picked at random sits in */
static void star_stats(const GraphEdge* e, size_t m, int n, double* top_share, double* es2)
{
    int*   size  = calloc((size_t)n, sizeof(int));
    int*   deg   = calloc((size_t)n, sizeof(int));
    int    stars = 0, max_deg = 0;
    double s2    = 0;
    for (size_t i = 0; i < m; i++)
        size[e[i].u]++, deg[e[i].u]++, deg[e[i].v]++;
    for (int u = 0; u < n; u++)
    {
        stars += size[u] > 0;
        s2 += (double)size[u] * size[u];
        max_deg = deg[u] > max_deg ? deg[u] : max_deg;
    }
    qsort(size, (size_t)n, sizeof(int), cmp_int_desc);
    size_t top = 0;
    for (int i = 0; i < stars / 100; i++)
        top += (size_t)size[i];
    printf("  stars %d, largest %d, max degree %d, top 1%% of stars hold %.1f%% of the edges "
           "(mean size %.0f), E[s^2]/E[s] %.0f\n",
           stars, size[0], max_deg, 100.0 * (double)top / (double)m,
           stars >= 100 ? (double)top / (double)(stars / 100) : 0.0, s2 / (double)m);
    *top_share = 100.0 * (double)top / (double)m;
    *es2       = s2 / (double)m;
    free(size), free(deg);
}

static double weight_of(const GraphEdge* e, int k)
{
    double s = 0;
    for (int i = 0; i < k; i++)
        s += e[i].w;
    return s;
}

/* Measures one graph: the variants in `mask` alternate inside each
 * repetition, so a slow phase of the machine hits all of them, not one.
 * Returns the number of forests that differ from the first variant's. */
static int run_graph(const char* name, const char* path, const char* what, int runs, unsigned mask)
{
    int        n;
    size_t     m;
    double     t0 = now_ms();
    GraphEdge* e  = bin_read(path, &n, &m);
    if (!e)
    {
        printf("%-24s skipped: %s missing (tools/fetch_mst_graphs.sh big)\n", name, path);
        return 0;
    }
    for (size_t k = 1; k < m; k++)
        if (e[k].u < e[k - 1].u)
        {
            fprintf(stderr, "%s: not grouped by source\n", path);
            exit(1);
        }

    Graph*     g    = graph_create(n, false, GRAPH_LIST); // empty: the algorithms read only n
    GraphEdge* work = malloc(sizeof(GraphEdge) * (m ? m : 1));
    if (!g || !work)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    printf("\n%-24s %s\n  n %d, m %zu, avg degree %.1f (loaded in %.0f ms)\n", name, what, n, m,
           2.0 * (double)m / n, now_ms() - t0);
    double top, es2;
    star_stats(e, m, n, &top, &es2);

    double best[N_VARIANTS], w[N_VARIANTS];
    int    size[N_VARIANTS];
    for (int v = 0; v < N_VARIANTS; v++)
        best[v] = 1e30, w[v] = 0, size[v] = 0;
    for (int r = 0; r < runs; r++)
        for (int v = 0; v < N_VARIANTS; v++)
        {
            if (!(mask >> v & 1))
                continue;
            memcpy(work, e, sizeof(GraphEdge) * m);
            double     t   = now_ms();
            GraphEdge* mst = VARIANTS[v].fn(g, (StarEdgeList){work, m}, &size[v]);
            t              = now_ms() - t;
            if (t < best[v])
                best[v] = t;
            w[v] = mst ? weight_of(mst, size[v]) : -1;
            free(mst);
        }

    int bad = 0, ref = -1;
    printf("  ");
    for (int v = 0; v < N_VARIANTS; v++)
    {
        if (!(mask >> v & 1))
            continue;
        if (ref < 0)
            ref = v;
        bool ok = size[v] == size[ref] && fabs(w[v] - w[ref]) <= 1e-9 * fabs(w[ref]);
        bad += !ok;
        printf("%s %.1f%s   ", VARIANTS[v].name, best[v], ok ? "" : " (!)");
    }
    if ((mask & 3) == 3)
        printf("| hybrid vs filter: %.2fx", best[0] / best[1]);
    printf("\n");
    // name, avg degree, top 1% share, E[s^2]/E[s], then the time of each variant (-1 = not run)
    printf("RESULT %s %.1f %.1f %.0f", name, 2.0 * (double)m / n, top, es2);
    for (int v = 0; v < N_VARIANTS; v++)
        printf(" %.1f", mask >> v & 1 ? best[v] : -1.0);
    printf("\n");
    fflush(stdout);

    free(work), free(e);
    graph_free(g);
    return bad;
}

static int cmp_str(const void* a, const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : "";
    int         runs   = argc > 2 ? atoi(argv[2]) : 3;
    unsigned    mask   = 0;
    if (runs < 1)
        runs = 1;
    for (int v = 0; v < N_VARIANTS; v++) // comma separated names, all by default
        if (argc <= 3 || strstr(argv[3], VARIANTS[v].name))
            mask |= 1u << v;
    printf("best of %d, times in ms, build flags: \"%s\"\n", runs, BIGFLAGS);

    int bad = 0;
    for (size_t i = 0; i < sizeof GRAPHS / sizeof GRAPHS[0]; i++)
        if (strstr(GRAPHS[i].name, filter))
            bad += run_graph(GRAPHS[i].name, GRAPHS[i].path, GRAPHS[i].what, runs, mask);

    // the grid, in name order
    DIR* dir = opendir("res/mst/grid");
    if (dir)
    {
        char*          names[1024];
        int            count = 0;
        struct dirent* d;
        while ((d = readdir(dir)) && count < 1024)
        {
            size_t len = strlen(d->d_name);
            if (len > 4 && strcmp(d->d_name + len - 4, ".bin") == 0)
                names[count++] = strdup(d->d_name);
        }
        closedir(dir);
        qsort(names, (size_t)count, sizeof(char*), cmp_str);
        for (int k = 0; k < count; k++)
        {
            char name[300], path[300];
            snprintf(name, sizeof name, "grid/%.*s", (int)(strlen(names[k]) - 4), names[k]);
            snprintf(path, sizeof path, "res/mst/grid/%s", names[k]);
            if (strstr(name, filter))
                bad += run_graph(name, path, "DC-SBM grid (tools/gen_mst_grid.sh)", runs, mask);
            free(names[k]);
        }
    }

    printf("\n%s\n", bad ? "FORESTS DIFFER (!)" : "all forests agree");
    return bad != 0;
}
