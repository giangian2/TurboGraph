/*
 * Generates a large clustered graph for bench/mst_big.c and writes it in the
 * binary format of the MST suite (see bench/jaccard.h):
 *
 *   bin/gen_mst_bin out.bin n block deg_in deg_out gamma [seed]
 *
 * Degree-corrected stochastic block model: n vertices in blocks of `block`
 * consecutive ids, about n * deg_in / 2 edges inside the blocks and
 * n * deg_out / 2 between them. Each endpoint is drawn with probability
 * proportional to theta_i = (i + 1)^(-1 / (gamma - 1)), shuffled over the
 * vertices: a power law of exponent gamma, i.e. hubs. gamma = 0 gives every
 * vertex the same theta (no hubs, a plain block model). Ids are permuted at
 * the end, so blocks are not contiguous in the edge list.
 *
 * Weights are Jaccard distances: light inside the communities, heavy on the
 * bridges, as on the real social graphs. Duplicate edges and self loops are
 * dropped, so m is slightly below the requested one.
 */
#include "../bench/jaccard.h"
#include <math.h>

static uint64_t rng;

static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static int rnd_int(int n)
{
    return (int)(rnd() % (uint64_t)n);
}

static double rnd_unit(void)
{
    return (double)(rnd() >> 11) * (1.0 / 9007199254740992.0);
}

/* index i in [lo, hi) with cum[i - 1] < x <= cum[i], x uniform over the range */
static int pick(const double* cum, int lo, int hi)
{
    double base = lo > 0 ? cum[lo - 1] : 0.0;
    double x    = base + rnd_unit() * (cum[hi - 1] - base);
    while (hi - lo > 1)
    {
        int mid = lo + (hi - lo) / 2;
        if (cum[mid - 1] < x)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

int main(int argc, char** argv)
{
    if (argc < 7)
    {
        fprintf(stderr, "usage: %s out.bin n block deg_in deg_out gamma [seed]\n", argv[0]);
        return 2;
    }
    const char*  out     = argv[1];
    const int    n       = atoi(argv[2]);
    const int    block   = atoi(argv[3]);
    const double deg_in  = atof(argv[4]);
    const double deg_out = atof(argv[5]);
    const double gamma   = atof(argv[6]);
    rng                  = argc > 7 ? strtoull(argv[7], NULL, 10) : 88172645463325252ULL;
    if (n < 2 || block < 2 || block > n || (gamma != 0 && gamma <= 1) || rng == 0)
        return fprintf(stderr, "bad arguments\n"), 2;

    // theta: power law shuffled over the vertices, so the hubs land in random blocks
    double* cum = malloc(sizeof(double) * (size_t)n);
    if (!cum)
        return fprintf(stderr, "out of memory\n"), 1;
    for (int i = 0; i < n; i++)
        cum[i] = gamma > 0 ? pow(i + 1.0, -1.0 / (gamma - 1.0)) : 1.0;
    for (int i = n - 1; i > 0; i--)
    {
        int    j = rnd_int(i + 1);
        double t = cum[i];
        cum[i] = cum[j], cum[j] = t;
    }
    for (int i = 1; i < n; i++)
        cum[i] += cum[i - 1];

    const size_t m_in = (size_t)((double)n * deg_in / 2), m_out = (size_t)((double)n * deg_out / 2);
    GraphEdge*   raw  = malloc(sizeof(GraphEdge) * (m_in + m_out + 1));
    if (!raw)
        return fprintf(stderr, "out of memory\n"), 1;
    size_t m = 0;

    for (size_t i = 0; i < m_in; i++)
    {
        int u = pick(cum, 0, n), base = u / block * block, top = base + block < n ? base + block : n;
        int v = pick(cum, base, top);
        if (v != u)
            raw[m++] = (GraphEdge){u, v, 1.0};
    }
    for (size_t i = 0; i < m_out; i++)
    {
        int u = pick(cum, 0, n), v = pick(cum, 0, n);
        if (u / block != v / block)
            raw[m++] = (GraphEdge){u, v, 1.0};
    }
    free(cum);

    // permute the ids: a block is no longer a run of consecutive vertices
    int* perm = malloc(sizeof(int) * (size_t)n);
    if (!perm)
        return fprintf(stderr, "out of memory\n"), 1;
    for (int i = 0; i < n; i++)
        perm[i] = i;
    for (int i = n - 1; i > 0; i--)
    {
        int j = rnd_int(i + 1), t = perm[i];
        perm[i] = perm[j], perm[j] = t;
    }
    for (size_t i = 0; i < m; i++)
        raw[i].u = perm[raw[i].u], raw[i].v = perm[raw[i].v];
    free(perm);

    size_t     e;
    GraphEdge* w = jaccard_weights(n, raw, m, &e);
    free(raw);
    if (!w || bin_write(out, n, w, e) != 0)
        return fprintf(stderr, "generation failed\n"), 1;
    printf("%s: n = %d, m = %zu, avg degree %.1f\n", out, n, e, 2.0 * (double)e / n);
    free(w);
    return 0;
}
