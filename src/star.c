/*
 * Forward/backward star (CSR) in a single contiguous block.
 *
 * g->data points to one malloc holding, in this order: the Star header,
 * then the data segments. The out-arcs of u are the range
 * [heads[u], heads[u+1]) of to[]/w[]: contiguous, so out_degree is O(1)
 * and walking the neighbors is a plain array scan. Immutable.
 *
 * The out star is always present. The in star (rheads/from/rw) is only
 * built for directed graphs: in an undirected graph the out star already
 * holds both directions of every edge, so iter_in delegates to iter_out.
 *
 * POSITION INDEPENDENT: the header stores byte offsets relative to its own
 * address, not pointers. The block therefore stays valid wherever it is
 * copied -- device memory, a mapped file, a socket -- because there is
 * nothing to relocate. A host pointer copied to a GPU would be garbage;
 * an offset would not. See star_init_from_edges() below for the layout.
 */
#include <stdlib.h>
#include "../include/Graph.h"


/*
 * Map of the block. Byte offsets from here, not pointers.
 * 0 = segment absent: no segment can start at 0, the header lives there.
 */
typedef struct
{
    size_t bytes;       //total block size: how much to copy
    size_t n_arcs;      //physical arcs = length of to/w (and of from/rw)
    //OUT
    size_t o_heads;
    size_t o_to;
    size_t o_w;
    //IN (0 when the graph is undirected)
    size_t o_rheads;
    size_t o_from;
    size_t o_rw;

} Star;

/* Offset to pointer. One addition, which next to the memory access that
 * follows it is not measurable. */
static inline int    *st_heads (const Star *s) { return (int*)   ((char*)s + s->o_heads); }
static inline int    *st_to    (const Star *s) { return (int*)   ((char*)s + s->o_to);    }
static inline double *st_w     (const Star *s) { return (double*)((char*)s + s->o_w);     }
static inline int    *st_rheads(const Star *s) { return (int*)   ((char*)s + s->o_rheads); }
static inline int    *st_from  (const Star *s) { return (int*)   ((char*)s + s->o_from);  }
static inline double *st_rw    (const Star *s) { return (double*)((char*)s + s->o_rw);    }

/*
 * True when the in star exists and is the one to read. An undirected graph
 * has no in star at all: its offsets are 0, so st_rheads() & co. would
 * resolve to the header itself. Every backward operation must therefore
 * fall back to the out star, which is symmetric anyway.
 */
static inline bool use_in_star(const Graph *g, bool in)
{
    return in && g->directed;
}

static void star_destroy(Graph *g){
    //ONE BLOCK, ONE free
    free(g->data);
}

static void star_iter_out(const Graph *g, int u, GraphIter *it){
    const Star *star  = (const Star*)g->data;

    it->g    = g;
    it->in   = false;
    it->u    = u;
    it->i    = st_heads(star)[u];
    it->node = NULL;   //unused by this representation
}

static void star_iter_in(const Graph *g, int u, GraphIter *it){
    const Star *star  = (const Star*)g->data;

    if (!g->directed) {              /* out star is symmetric: delegate */
        star_iter_out(g, u, it);
        it->in = true;               /* informational: use_in_star stays false */
        return;
    }
    it->g    = g;
    it->in   = true;
    it->u    = u;
    it->i    = st_rheads(star)[u];
    it->node = NULL;
}

static bool star_iter_next(GraphIter *it, int *v, double *w){
    const Star   *star    = (const Star*)it->g->data;
    const bool    back    = use_in_star(it->g, it->in);
    const int    *head    = back ? st_rheads(star) : st_heads(star);
    const int    *nbr     = back ? st_from(star)   : st_to(star);
    const double *weights = back ? st_rw(star)     : st_w(star);

    /*
     * Boundary check. Only the upper bound is tested, and it is exclusive:
     * head[u+1] is the first index belonging to the next vertex. The lower
     * bound needs no test -- iter_out/iter_in set i = head[u] and nothing
     * but the increment below ever moves it. A zero-degree vertex has
     * head[u] == head[u+1] and stops here on the very first call.
     */
    if (it->i >= head[it->u + 1])
        return false;

    /* Both outputs are optional: graph_bfs() passes NULL for the weight. */
    if (v) *v = nbr[it->i];
    if (w) *w = weights[it->i];

    it->i++;
    return true;
}

/*
 * Linear scan of u's out block, O(deg): neighbors inside a block are kept
 * in edge-list order, not sorted, so no binary search is possible.
 *
 * Missing arc -> 0.0, the library-wide "no edge" convention (Graph.h):
 * graph_add_edge() rejects a weight of 0.0, so the value is unambiguous,
 * and graph_has_edge() is just a != 0.0 on this result.
 */
static double star_get_weight(const Graph *g, int u, int v){
    const Star   *star = (const Star*)g->data;
    const int    *head = st_heads(star);
    const int    *to   = st_to(star);
    const double *w    = st_w(star);

    for (int i = head[u]; i < head[u + 1]; i++)
        if (to[i] == v)
            return w[i];

    return 0.0;   //no u -> v arc
}

/* O(1): the bounds of the block are the degree. */
static int star_out_degree(const Graph *g, int u){
    const int *head = st_heads((const Star*)g->data);
    return head[u + 1] - head[u];
}

static int star_in_degree(const Graph *g, int u){
    if (!g->directed)
        return star_out_degree(g, u);   //no in star: the out star is symmetric

    const int *rhead = st_rheads((const Star*)g->data);
    return rhead[u + 1] - rhead[u];
}

/*
 * Mutation would mean shifting every arc past u's block and bumping
 * heads[x] for every x > u -- O(n + arcs) per single edge, plus a realloc
 * that would invalidate copies already shipped elsewhere. Hence immutable.
 */
static int star_add_edge(Graph *g, int u, int v, double w)
{
    (void)g; (void)u; (void)v; (void)w;
    return GRAPH_ERR_IMMUTABLE;
}

static int star_remove_edge(Graph *g, int u, int v)
{
    (void)g; (void)u; (void)v;
    return GRAPH_ERR_IMMUTABLE;
}


static size_t arc_count(const Graph *g, const GraphEdge *e, size_t m)
{
    if (g->directed)
        return m;
    size_t a = 0;
    for (size_t k = 0; k < m; k++)
        a += (e[k].u == e[k].v) ? 1 : 2;
    return a;
}

/*
 * Filling: a counting sort in four linear passes, O(n + m), with no
 * temporary allocation. heads[] arrives zeroed from the calloc.
 *
 * Out star: arcs are grouped by source. In an undirected graph every edge
 * must be written into both blocks, except self-loops (the same u != v
 * condition used by arc_count).
 */
static void fill_out(const Graph *g, Star *s, const GraphEdge *edges, size_t m)
{
    const int n    = g->n;
    int    *head   = st_heads(s);
    int    *to     = st_to(s);
    double *w      = st_w(s);

    /* 1. degrees, shifted by one: head[x+1] = degree of x */
    for (size_t k = 0; k < m; k++) {
        head[edges[k].u + 1]++;
        if (!g->directed && edges[k].u != edges[k].v)
            head[edges[k].v + 1]++;
    }

    /* 2. prefix sum: head[x] = start of x's block */
    for (int x = 0; x < n; x++)
        head[x + 1] += head[x];

    /* 3. head[x] doubles as the write cursor: no array of cursors needed */
    for (size_t k = 0; k < m; k++) {
        int u = edges[k].u;
        int v = edges[k].v;
        int p = head[u]++;
        to[p] = v;
        w[p]  = edges[k].w;
        if (!g->directed && u != v) {
            p = head[v]++;
            to[p] = u;
            w[p]  = edges[k].w;
        }
    }

    /* 4. the cursor left head[x] = start of x+1: the right array, shifted
     *    by one. Backwards, so cells still to be read are not overwritten */
    for (int x = n; x >= 1; x--)
        head[x] = head[x - 1];
    head[0] = 0;
}

/*
 * In star: identical, but groups by destination and stores the source.
 * Only directed graphs need it, so there is never a double write.
 */
static void fill_in(const Graph *g, Star *s, const GraphEdge *edges, size_t m)
{
    const int n    = g->n;
    int    *head   = st_rheads(s);
    int    *from   = st_from(s);
    double *rw     = st_rw(s);

    for (size_t k = 0; k < m; k++)
        head[edges[k].v + 1]++;

    for (int x = 0; x < n; x++)
        head[x + 1] += head[x];

    for (size_t k = 0; k < m; k++) {
        int p = head[edges[k].v]++;
        from[p] = edges[k].u;
        rw[p]   = edges[k].w;
    }

    for (int x = n; x >= 1; x--)
        head[x] = head[x - 1];
    head[0] = 0;
}

static const GraphOps star_ops = {
    .add_edge    = star_add_edge,
    .remove_edge = star_remove_edge,
    .get_weight  = star_get_weight,
    .out_degree  = star_out_degree,
    .in_degree   = star_in_degree,
    .iter_out    = star_iter_out,
    .iter_in     = star_iter_in,
    .iter_next   = star_iter_next,
    .destroy     = star_destroy,
};

int star_init_from_edges(Graph *g, const GraphEdge *edges, size_t m){
    const int    tot_nodes = g->n;
    const bool   back      = g->directed;   //only directed graphs need the in star
    const size_t tot_arcs  = arc_count(g,edges,m); //physical arcs: 2m - self-loops when undirected

    /*
     * Offsets of the segments inside the single block, computed with an
     * accumulator: each segment records the current offset, then advances
     * by n_elements * sizeof(type). Optional segments advance inside the
     * if, so when they are absent the next segment takes their place and
     * the total falls out on its own: there is no single line restating
     * the total that could forget a piece.
     *
     * Ordered by decreasing alignment (doubles first, then ints): this way
     * every segment lands already aligned and no internal padding is needed.
     */
    size_t off       = (sizeof(Star) + 7u) & ~(size_t)7u;
    size_t o_w       = off;  off += tot_arcs * sizeof(double);
    size_t o_rw      = 0;
    size_t o_rheads  = 0;
    size_t o_from    = 0;

    if (back) {
        o_rw = off;  off += tot_arcs * sizeof(double);
    }

    size_t o_heads   = off;  off += (size_t)(tot_nodes + 1) * sizeof(int);
    size_t o_to      = off;  off += tot_arcs * sizeof(int);

    if (back) {
        o_rheads = off;      off += (size_t)(tot_nodes + 1) * sizeof(int);
        o_from   = off;      off += tot_arcs * sizeof(int);
    }

    //SINGLE CONTIGUOUS ALLOCATION. calloc, not malloc: the degree counting
    //pass assumes heads[] (and rheads[]) are already zeroed.
    char *blk = calloc(1, off);
    if (!blk)
        return GRAPH_ERR_ALLOC;

    //Header: offsets, not pointers. No absolute address lives in the block,
    //so copying it elsewhere (device, file, network) needs no relocation.
    //The three in-star offsets stay at 0 = absent when undirected.
    Star *s     = (Star*)blk;
    s->bytes    = off;
    s->n_arcs   = tot_arcs;
    s->o_w      = o_w;
    s->o_heads  = o_heads;
    s->o_to     = o_to;
    s->o_rw     = o_rw;
    s->o_rheads = o_rheads;
    s->o_from   = o_from;

    fill_out(g, s, edges, m);
    if (back)
        fill_in(g, s, edges, m);

    g->data = blk;
    g->m    = m;            //LOGICAL edge count, not tot_arcs
    g->ops  = &star_ops;
    return GRAPH_OK;
}
