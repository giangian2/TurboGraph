/*
 * MST suite: every CPU Kruskal variant on graphs with precise, named
 * characteristics, each result checked against the full-sort Kruskal
 * (forest size and weight).
 *
 *   make suite                       build at -O2 and run everything
 *   bin/mst_suite [filter] [runs]    only the cases whose name contains filter
 *
 * Families:
 *
 *   uniform    G(n, m): uniform endpoints, uniform weights. The baseline,
 *              and the worst case for structure-aware ideas.
 *   sbm        stochastic block model: blocks of equal size, most edges
 *              inside a block. "light-in": in-block edges weigh U(0, .5],
 *              bridges U(.5, 1], so the forest grows inside the communities
 *              first, as on real weighted social graphs.
 *   powerlaw   Chung-Lu: expected degree of vertex i ~ (i + 1)^(-1/(g-1)),
 *              a few huge hubs and a long tail.
 *   geometric  random points in the unit square, an edge between points
 *              closer than r, weight = distance: road-like, planar-ish,
 *              strongly local.
 *   ties       uniform graph with integer weights 1..100: many ties, a test
 *              for the (w, v) threshold of K-best.
 *   edge       tiny correctness cases: disconnected, isolated vertices,
 *              all weights equal, a single vertex.
 *   jsbm       block model with Jaccard weights (bench/jaccard.h): blocks of
 *              100, light edges inside communities, heavy bridges. The
 *              profile of the real social graphs, at any size.
 *   real       DOT and binary files of res/ and res/mst/
 *              (tools/fetch_mst_graphs.sh), skipped when missing.
 *
 * The edge list is restored outside the timer. Multi-edges may appear in the
 * random families: they do not change the forest weight.
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime under -std=c11 */
#include "../include/Graph.h"
#include "../include/Importer.h"
#include "../include/Traversal.h"
#include "jaccard.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- utilities ------------------------------------------------------------ */

static uint64_t rng_state = 88172645463325252ULL;

static uint64_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* Uniform in (0, 1]: never 0, which means "no edge" in this library */
static double rnd_unit(void)
{
    return ((double)(rnd() >> 11) + 1.0) / 9007199254740992.0;
}

static int rnd_int(int n)
{
    return (int)(rnd() % (uint64_t)n);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

/* Growable edge buffer for the generators */
typedef struct
{
    GraphEdge* e;
    size_t     m, cap;
} Edges;

static void push(Edges* b, int u, int v, double w)
{
    if (b->m == b->cap)
    {
        b->cap = b->cap ? 2 * b->cap : 1024;
        b->e   = realloc(b->e, sizeof(GraphEdge) * b->cap);
        if (!b->e)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
    }
    b->e[b->m++] = (GraphEdge){u, v, w};
}

static Graph* build(int n, Edges* b)
{
    Graph* g = graph_from_edges(n, false, b->e, b->m, GRAPH_STAR);
    free(b->e);
    return g;
}

/* ---- generators --------------------------------------------------------------- */

static Graph* gen_uniform(int n, double deg)
{
    Edges  b = {0};
    size_t m = (size_t)(n * deg / 2);
    for (size_t i = 0; i < m; i++)
    {
        int u = rnd_int(n), v;
        do
            v = rnd_int(n);
        while (v == u);
        push(&b, u, v, rnd_unit());
    }
    return build(n, &b);
}

static Graph* gen_complete(int n)
{
    Edges b = {0};
    for (int u = 0; u < n; u++)
        for (int v = u + 1; v < n; v++)
            push(&b, u, v, rnd_unit());
    return build(n, &b);
}

/* blocks of n / k vertices; deg_in edges per vertex inside its block on
 * average, deg_out towards other blocks */
static Graph* gen_sbm(int n, int k, double deg_in, double deg_out, bool light_in)
{
    Edges  b    = {0};
    int    size = n / k;
    size_t m_in = (size_t)(n * deg_in / 2), m_out = (size_t)(n * deg_out / 2);

    for (size_t i = 0; i < m_in; i++)
    {
        int base = rnd_int(k) * size, u = base + rnd_int(size), v;
        do
            v = base + rnd_int(size);
        while (v == u);
        push(&b, u, v, light_in ? 0.5 * rnd_unit() : rnd_unit());
    }
    for (size_t i = 0; i < m_out; i++)
    {
        int u = rnd_int(n), v;
        do
            v = rnd_int(n);
        while (v / size == u / size);
        push(&b, u, v, light_in ? 0.5 + 0.5 * rnd_unit() : rnd_unit());
    }
    return build(n, &b);
}

/* Block model with blocks of `size` vertices, weighted by the Jaccard
 * distance of the neighborhoods (duplicates merged) */
static Graph* gen_sbm_jaccard(int n, int size, double deg_in, double deg_out)
{
    Edges  b    = {0};
    int    k    = n / size;
    size_t m_in = (size_t)(n * deg_in / 2), m_out = (size_t)(n * deg_out / 2);

    for (size_t i = 0; i < m_in; i++)
    {
        int base = rnd_int(k) * size, u = base + rnd_int(size), v;
        do
            v = base + rnd_int(size);
        while (v == u);
        push(&b, u, v, 1.0);
    }
    for (size_t i = 0; i < m_out; i++)
    {
        int u = rnd_int(n), v;
        do
            v = rnd_int(n);
        while (v / size == u / size);
        push(&b, u, v, 1.0);
    }

    size_t     m;
    GraphEdge* w = jaccard_weights(n, b.e, b.m, &m);
    free(b.e);
    if (!w)
        return NULL;
    Graph* g = graph_from_edges(n, false, w, m, GRAPH_STAR);
    free(w);
    return g;
}

/* Chung-Lu: endpoints drawn proportionally to theta_i = (i + 1)^(-1/(gamma-1)) */
static Graph* gen_powerlaw(int n, double deg, double gamma)
{
    double* cum = malloc(sizeof(double) * (size_t)n);
    double  acc = 0;
    for (int i = 0; i < n; i++)
        cum[i] = acc += pow(i + 1.0, -1.0 / (gamma - 1.0));

    Edges  b = {0};
    size_t m = (size_t)(n * deg / 2);
    for (size_t i = 0; i < m; i++)
    {
        int ends[2];
        for (int j = 0; j < 2; j++)
        {
            double x  = rnd_unit() * acc;
            int    lo = 0, hi = n - 1;
            while (lo < hi)
            {
                int mid = (lo + hi) / 2;
                if (cum[mid] < x)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            ends[j] = lo;
        }
        if (ends[0] != ends[1])
            push(&b, ends[0], ends[1], rnd_unit());
    }
    free(cum);
    return build(n, &b);
}

/* Random geometric graph on a grid of cells of side r, weight = distance */
static Graph* gen_geometric(int n, double deg)
{
    double  r     = sqrt(deg / (n * 3.14159265358979323846));
    int     cells = (int)(1.0 / r);
    double *x = malloc(sizeof(double) * (size_t)n), *y = malloc(sizeof(double) * (size_t)n);
    int *   head = malloc(sizeof(int) * (size_t)cells * cells), *next = malloc(sizeof(int) * (size_t)n);
    for (int c = 0; c < cells * cells; c++)
        head[c] = -1;
    for (int i = 0; i < n; i++)
    {
        x[i]   = rnd_unit();
        y[i]   = rnd_unit();
        int cx = (int)(x[i] * cells) % cells, cy = (int)(y[i] * cells) % cells;
        next[i]               = head[cy * cells + cx];
        head[cy * cells + cx] = i;
    }

    Edges b = {0};
    for (int i = 0; i < n; i++)
    {
        int cx = (int)(x[i] * cells) % cells, cy = (int)(y[i] * cells) % cells;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= cells || ny >= cells)
                    continue;
                for (int j = head[ny * cells + nx]; j >= 0; j = next[j])
                {
                    if (j <= i)
                        continue;
                    double d = hypot(x[i] - x[j], y[i] - y[j]);
                    if (d < r)
                        push(&b, i, j, d > 0 ? d : 1e-12);
                }
            }
    }
    free(x), free(y), free(head), free(next);
    return build(n, &b);
}

static Graph* gen_ties(int n, double deg, int max_w)
{
    Edges  b = {0};
    size_t m = (size_t)(n * deg / 2);
    for (size_t i = 0; i < m; i++)
    {
        int u = rnd_int(n), v;
        do
            v = rnd_int(n);
        while (v == u);
        push(&b, u, v, 1 + rnd_int(max_w));
    }
    return build(n, &b);
}

/* Two cliques of 6 with no edge between them, plus 3 isolated vertices */
static Graph* gen_disconnected(void)
{
    Edges b = {0};
    for (int c = 0; c < 2; c++)
        for (int u = 0; u < 6; u++)
            for (int v = u + 1; v < 6; v++)
                push(&b, 6 * c + u, 6 * c + v, 1 + rnd_int(5));
    return build(15, &b);
}

/* Complete graph on 30 vertices, every weight 1: ties only */
static Graph* gen_all_equal(void)
{
    Edges b = {0};
    for (int u = 0; u < 30; u++)
        for (int v = u + 1; v < 30; v++)
            push(&b, u, v, 1.0);
    return build(30, &b);
}

/* A path where the last vertex has no star (all its neighbors are smaller) */
static Graph* gen_path(void)
{
    Edges b = {0};
    for (int u = 0; u < 99; u++)
        push(&b, u, u + 1, rnd_unit());
    return build(100, &b);
}

/* ---- variants --------------------------------------------------------------- */

typedef GraphEdge* (*mst_fn)(const Graph*, StarEdgeList, int*);

static GraphEdge* run_kruskal(const Graph* g, StarEdgeList l, int* s)
{
    return graph_mst_kruskal(g, star_as_edge_list(l), s);
}
static GraphEdge* run_quick(const Graph* g, StarEdgeList l, int* s)
{
    return graph_mst_quick_kruskal(g, star_as_edge_list(l), s);
}

static const struct
{
    const char* name;
    mst_fn      fn;
} VARIANTS[] = {
    {"kruskal", run_kruskal},
    {"quick", run_quick},
    {"q-star", graph_mst_quick_star_kruskal},
    {"filter", graph_mst_filter_star_kruskal},
    {"kbest", graph_mst_kbest_star_kruskal},
    {"hybrid", graph_mst_filter_kbest_kruskal},
};
enum
{
    N_VARIANTS = sizeof(VARIANTS) / sizeof(VARIANTS[0])
};

/* ---- cases ------------------------------------------------------------------ */

typedef struct
{
    const char* name;
    const char* what;
    Graph* (*make)(const void* arg);
    const void* arg;
} Case;

typedef struct
{
    int    n, k;
    double a, b, c;
    bool   flag;
} Params;

#define P(...) (&(const Params){__VA_ARGS__})

static Graph* mk_uniform(const void* p)
{
    const Params* q = p;
    return gen_uniform(q->n, q->a);
}
static Graph* mk_complete(const void* p)
{
    return gen_complete(((const Params*)p)->n);
}
static Graph* mk_sbm(const void* p)
{
    const Params* q = p;
    return gen_sbm(q->n, q->k, q->a, q->b, q->flag);
}
static Graph* mk_powerlaw(const void* p)
{
    const Params* q = p;
    return gen_powerlaw(q->n, q->a, q->b);
}
static Graph* mk_geometric(const void* p)
{
    const Params* q = p;
    return gen_geometric(q->n, q->a);
}
static Graph* mk_ties(const void* p)
{
    const Params* q = p;
    return gen_ties(q->n, q->a, q->k);
}
static Graph* mk_dot(const void* p)
{
    return graph_import_dot((const char*)p, GRAPH_STAR);
}
static Graph* mk_bin(const void* p)
{
    int        n;
    size_t     m;
    GraphEdge* e = bin_read((const char*)p, &n, &m);
    if (!e)
        return NULL;
    Graph* g = graph_from_edges(n, false, e, m, GRAPH_STAR);
    free(e);
    return g;
}
static Graph* mk_jsbm(const void* p)
{
    const Params* q = p;
    return gen_sbm_jaccard(q->n, q->k, q->a, q->b);
}
static Graph* mk_disconnected(const void* p)
{
    (void)p;
    return gen_disconnected();
}
static Graph* mk_all_equal(const void* p)
{
    (void)p;
    return gen_all_equal();
}
static Graph* mk_path(const void* p)
{
    (void)p;
    return gen_path();
}

static const Case CASES[] = {
    {"edge/disconnected", "2 cliques + 3 isolated", mk_disconnected, NULL},
    {"edge/all-equal", "K30, every w = 1", mk_all_equal, NULL},
    {"edge/path", "path, last vertex starless", mk_path, NULL},

    {"uniform/dense", "n 4k, deg 1000", mk_uniform, P(.n = 4000, .a = 1000)},
    {"uniform/complete", "K2000", mk_complete, P(.n = 2000)},
    {"uniform/mid", "n 10k, deg 100", mk_uniform, P(.n = 10000, .a = 100)},
    {"uniform/sparse", "n 1M, deg 16", mk_uniform, P(.n = 1000000, .a = 16)},

    {"sbm/dense", "n 4k, 16 blocks, in 400 out 10", mk_sbm, P(.n = 4000, .k = 16, .a = 400, .b = 10)},
    {"sbm/dense-light-in", "same, light in-block", mk_sbm, P(.n = 4000, .k = 16, .a = 400, .b = 10, .flag = true)},
    {"sbm/sparse", "n 200k, blocks of 100, in 12 out 2", mk_sbm, P(.n = 200000, .k = 2000, .a = 12, .b = 2)},
    {"sbm/sparse-light-in", "same, light in-block", mk_sbm, P(.n = 200000, .k = 2000, .a = 12, .b = 2, .flag = true)},

    {"powerlaw/mid", "n 20k, deg 100, gamma 2.3", mk_powerlaw, P(.n = 20000, .a = 100, .b = 2.3)},
    {"powerlaw/sparse", "n 200k, deg 16, gamma 2.3", mk_powerlaw, P(.n = 200000, .a = 16, .b = 2.3)},

    {"geometric/sparse", "n 200k, deg 12, w = distance", mk_geometric, P(.n = 200000, .a = 12)},
    {"geometric/dense", "n 10k, deg 200, w = distance", mk_geometric, P(.n = 10000, .a = 200)},

    {"ties/dense", "n 2k, deg 500, w in 1..100", mk_ties, P(.n = 2000, .a = 500, .k = 100)},
    {"ties/sparse", "n 100k, deg 20, w in 1..100", mk_ties, P(.n = 100000, .a = 20, .k = 100)},

    {"jsbm/4k", "n 4k, blocks 100, in 30 out 3", mk_jsbm, P(.n = 4000, .k = 100, .a = 30, .b = 3)},
    {"jsbm/40k", "n 40k, same profile", mk_jsbm, P(.n = 40000, .k = 100, .a = 30, .b = 3)},
    {"jsbm/400k", "n 400k, same profile", mk_jsbm, P(.n = 400000, .k = 100, .a = 30, .b = 3)},
    {"jsbm/dense-4k", "n 4k, blocks 250, in 200 out 10", mk_jsbm, P(.n = 4000, .k = 250, .a = 200, .b = 10)},
    {"jsbm/dense-40k", "n 40k, same profile", mk_jsbm, P(.n = 40000, .k = 250, .a = 200, .b = 10)},

    {"real/email-eu", "e-mail, departments", mk_dot, "res/mst/email-eu.dot"},
    {"real/facebook", "ego networks, communities", mk_dot, "res/mst/facebook.dot"},
    {"real/bitcoin-otc", "trust ratings, int weights", mk_dot, "res/mst/bitcoin-otc.dot"},
    {"real/astroph", "co-authorship, cliques", mk_dot, "res/mst/astroph.dot"},
    {"real/amazon", "co-purchase, communities", mk_dot, "res/mst/amazon.dot"},
    {"real/dblp", "co-authorship, power-law", mk_dot, "res/mst/dblp.dot"},
    {"real/bayarea", "roads, w = distance", mk_dot, "res/BayAreaUs_undirected.dot"},
    {"real/artist", "Facebook artist pages", mk_bin, "res/mst/artist.bin"},
    {"real/twitter-ego", "Twitter ego networks", mk_bin, "res/mst/twitter-ego.bin"},
    {"real/gplus-ego", "Google+ ego networks, dense", mk_bin, "res/mst/gplus-ego.bin"},
    {"real/pokec", "Pokec social network", mk_bin, "res/mst/pokec.bin"},
    {"real/youtube", "YouTube, sparse control", mk_bin, "res/mst/youtube.bin"},
};
enum
{
    N_CASES = sizeof(CASES) / sizeof(CASES[0])
};

/* ---- runner ----------------------------------------------------------------- */

static double weight_of(const GraphEdge* e, int k)
{
    double s = 0;
    for (int i = 0; i < k; i++)
        s += e[i].w;
    return s;
}

static bool file_exists(const char* path)
{
    FILE* f = fopen(path, "r");
    if (f)
        fclose(f);
    return f != NULL;
}

/* Runs every variant on one case; returns the number of mismatches */
static int run_case(const Case* c, int runs)
{
    if ((c->make == mk_dot || c->make == mk_bin) && !file_exists((const char*)c->arg))
    {
        printf("%-22s skipped: %s missing (tools/fetch_mst_graphs.sh)\n", c->name, (const char*)c->arg);
        return 0;
    }

    rng_state = 88172645463325252ULL; // every case is reproducible on its own
    Graph* g  = c->make(c->arg);
    if (!g)
    {
        printf("%-22s graph build failed\n", c->name);
        return 1;
    }

    StarEdgeList star  = graph_star_edge_list(g);
    size_t       m     = star.count;
    size_t       bytes = sizeof(GraphEdge) * (m ? m : 1);
    GraphEdge*   work  = malloc(bytes);
    if (!work || (m > 0 && !star.edges))
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    printf("%-22s %-34s n %8d  m %9zu  deg %6.1f |", c->name, c->what, g->n, m,
           g->n ? 2.0 * (double)m / g->n : 0.0);

    int    ref_size = -1, bad = 0;
    double ref_w    = 0, best[N_VARIANTS];

    for (int v = 0; v < N_VARIANTS; v++)
    {
        int        size = 0;
        GraphEdge* mst  = NULL;
        best[v]         = 1e30;
        for (int r = 0; r < runs; r++)
        {
            free(mst);
            if (m)
                memcpy(work, star.edges, bytes);
            double t0 = now_ms();
            mst       = VARIANTS[v].fn(g, (StarEdgeList){m ? work : NULL, m}, &size);
            double dt = now_ms() - t0;
            if (dt < best[v])
                best[v] = dt;
        }

        int    s = mst ? size : 0;
        double w = mst ? weight_of(mst, s) : 0;
        free(mst);
        if (v == 0)
        {
            ref_size = s;
            ref_w    = w;
        }
        bool ok = s == ref_size && fabs(w - ref_w) <= 1e-9 * fabs(ref_w);
        bad += !ok;
        printf(" %9.2f%s", best[v], ok ? " " : "!");
    }

    // the fastest variant, the full-sort reference excluded
    int win = 1;
    for (int v = 2; v < N_VARIANTS; v++)
        if (best[v] < best[win])
            win = v;
    printf(" | %s\n", VARIANTS[win].name);

    free(work);
    free(star.edges);
    graph_free(g);
    return bad;
}

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : "";
    int         runs   = argc > 2 ? atoi(argv[2]) : 3;
    if (runs < 1)
        runs = 1;

    printf("times in ms, best of %d; '!' = forest size or weight differs from the full-sort Kruskal\n\n",
           runs);
    printf("%-22s %-34s %-41s |", "case", "graph", "");
    for (int v = 0; v < N_VARIANTS; v++)
        printf(" %10s", VARIANTS[v].name);
    printf(" | fastest\n");

    int bad = 0;
    for (int i = 0; i < N_CASES; i++)
        if (strstr(CASES[i].name, filter))
            bad += run_case(&CASES[i], runs);

    printf("\n%s\n", bad ? "MISMATCHES FOUND" : "all forests agree with the reference");
    return bad != 0;
}
