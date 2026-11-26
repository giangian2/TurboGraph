#include "../include/Traversal.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include "../include/Sorting.h"
#include "../include/Stack.h"
#include "../include/UnionFInd.h"
#include "../include/DHeap.h"
#include "internal.h"
#include "star.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>


/* One frame per vertex on the DFS path, iterator included: that cursor
 * is the whole point -- it is what a recursive DFS keeps implicitly in
 * its activation record. */
typedef struct
{
    int       vertex_id;
    GraphIter it;
} dfs_frame;

/*
 * Allocates a Traversal for a graph of n vertices, with all vertices
 * marked "unreached" (parent = -1, dist = -1), as required by the
 * contract in Graph.h. Returns NULL if an allocation fails.
 */
static Traversal* traversal_new(int n)
{
    Traversal* t = malloc(sizeof *t);
    if (!t)
    {
        LOG_ERROR("allocation failed for Traversal header (n=%d)", n);
        return NULL;
    }
    t->order  = malloc((size_t)n * sizeof *t->order);
    t->parent = malloc((size_t)n * sizeof *t->parent);
    t->dist   = malloc((size_t)n * sizeof *t->dist);
    t->count  = 0;
    t->n      = n;
    if (!t->order || !t->parent || !t->dist)
    {
        LOG_ERROR("allocation failed for Traversal arrays (n=%d)", n);
        traversal_cleanup(&t); /* free(NULL) is legal: only frees what actually succeeded */
        return NULL;
    }
    for (int v = 0; v < t->n; v++)
    {
        t->parent[v] = -1;
        t->dist[v]   = -1;
    }
    LOG_DEBUG("Traversal allocated (n=%d)", n);
    return t;
}

/* Marks every vertex "unreached" again, so the same buffers can host a
 * second, independent traversal. */
static void traversal_reset(Traversal* t)
{
    for (int v = 0; v < t->n; v++)
    {
        t->parent[v] = -1;
        t->dist[v]   = -1;
    }
    t->count = 0;
}

/*
 * Both visits below share one contract, which is what lets them build a
 * whole forest out of repeated calls:
 *
 *   - `t` is shared across calls: any vertex with dist != -1 counts as
 *     already visited and is not entered again, so one call per unvisited
 *     root covers the graph in O(n + m) overall;
 *   - `root` must still be unvisited; it becomes the root of a new tree
 *     (parent -1, dist 0), and roots are the only vertices of the forest
 *     left with parent == -1;
 *   - the tree is appended to t->order as one contiguous segment that
 *     begins with its root.
 */

/*
 * Iterative DFS from `root` over the out-arcs of g.
 *
 *   stack   empty, capacity >= g->n; left empty on return
 *   rpost   optional (NULL): reverse finish order. Each vertex, when its
 *           adjacency is exhausted, is written at rpost[--*rpost_next]:
 *           filled from the back, so with *rpost_next starting at n the
 *           array ends up with the LAST vertex to finish first -- the
 *           order Kosaraju's second pass needs (and, on a DAG, a
 *           topological order).
 */
static inline __attribute__((always_inline)) void dfs_visit_with(const Graph* g, int root,
                                                                 Traversal* t, dfs_frame* stack,
                                                                 int* rpost, int* rpost_next,
                                                                 IterOutFn expand, IterNextFn next)
{
    /* The root is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex of its tree in the visit order. */
    t->dist[root]        = 0;
    t->order[t->count++] = root;

    dfs_frame _root_frame = {0};
    _root_frame.vertex_id = root;
    expand(g, root, &_root_frame.it);
    stack_push(stack, _root_frame);

    /*
     * Recursive DFS, unrolled onto an explicit stack. Each frame carries
     * its own neighbor iterator, so a vertex remembers how far along its
     * adjacency it got: the frame below the top is resumed where it left
     * off instead of being re-expanded from the start.
     *
     * One step per iteration: advance the top frame's iterator by exactly
     * one neighbor, or -- when that adjacency is exhausted -- pop and
     * backtrack. Every iteration therefore either consumes an edge or
     * removes a vertex, which is what makes the loop terminate.
     */
    while (!stack_is_empty(stack))
    {
        int v;

        /* Mutated in place through the lvalue: advancing the cursor must
         * not pop and re-push the frame. Valid until this frame is popped,
         * and unused after the push below, which cannot move it anyway
         * (fixed capacity). */
        dfs_frame* _top_frame = &stack_top(stack);

        /* Adjacency exhausted: this vertex is done (this is its finish
         * time), backtrack to its parent. */
        if (!next(&_top_frame->it, &v, NULL))
        {
            if (rpost)
                rpost[--*rpost_next] = _top_frame->vertex_id;
            (void)stack_pop(stack);
            continue;
        }

        /* Already discovered: back, forward or cross edge -- nothing to do.
         * No pop here: the iterator has already advanced, so the loop still
         * makes progress. */
        if (t->dist[v] != -1)
            continue;

        /* First discovery of v: the frame on top is its parent in the DFS
         * tree and its depth is one more than the parent's. Marking here,
         * at discovery time, is what keeps each vertex out of the stack
         * after the first push. */
        t->dist[v]           = t->dist[_top_frame->vertex_id] + 1;
        t->parent[v]         = _top_frame->vertex_id;
        t->order[t->count++] = v;

        dfs_frame child = {0};
        child.vertex_id = v;
        expand(g, v, &child.it);
        stack_push(stack, child);
    }
}

/* Star instance: the iterators are inline constants here, so the arc loop
 * has no call left in it. */
static void dfs_visit_star(const Graph* g, int root, Traversal* t, dfs_frame* stack, int* rpost,
                           int* rpost_next)
{
    dfs_visit_with(g, root, t, stack, rpost, rpost_next, star_iter_out, star_iter_next);
}

/* DISPATCH: once per visit, never per arc. Any other representation goes
 * through its vtable, one indirect call per arc. */
static void dfs_visit(const Graph* g, int root, Traversal* t, dfs_frame* stack, int* rpost,
                      int* rpost_next)
{
    if (g->repr == GRAPH_STAR)
        dfs_visit_star(g, root, t, stack, rpost, rpost_next);
    else
        dfs_visit_with(g, root, t, stack, rpost, rpost_next, g->ops->iter_out, g->ops->iter_next);
}

/*
 * BFS from `root`.
 *
 *   in      false walks out-arcs (BFS on G), true walks in-arcs (BFS on
 *           the transpose G^T, for free: every representation has iter_in)
 *
 * No separate queue: vertices enter t->order in the same order they must
 * be dequeued, so the slice t->order[head .. count) is the frontier.
 *
 * A frontier entry is a bare vertex id (4 bytes) against the 56 of a DFS
 * frame, so whenever only the set of reached vertices matters -- not the
 * order in which they finish -- this is the cheaper visit.
 */
static inline __attribute__((always_inline)) void
bfs_visit_with(const Graph* g, int root, Traversal* t, IterOutFn expand, IterNextFn next)
{
    /* The root is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex of its tree in the visit order. */
    int head             = t->count;
    t->dist[root]        = 0;
    t->order[t->count++] = root;

    /* While there is a discovered but not-yet-processed vertex... */
    while (head < t->count)
    {
        int u = t->order[head++];

        /* Iterate the neighbors of u (the vertex just dequeued, NOT the
         * root). The iterator lives on the stack: no malloc. */
        GraphIter it;
        expand(g, u, &it);

        int v;
        while (next(&it, &v, NULL))
        { /* weight ignored */
            if (t->dist[v] != -1)
                continue; /* already discovered via an equal-or-shorter path */

            /* First discovery of v: u is its parent in the BFS tree and
             * its distance is one edge more than u's. */
            t->dist[v]           = t->dist[u] + 1;
            t->parent[v]         = u;
            t->order[t->count++] = v;
        }
    }
}

/* Star instances, one per direction: each gets its iterator inlined. */
static void bfs_visit_star_out(const Graph* g, int root, Traversal* t)
{
    bfs_visit_with(g, root, t, star_iter_out, star_iter_next);
}

static void bfs_visit_star_in(const Graph* g, int root, Traversal* t)
{
    bfs_visit_with(g, root, t, star_iter_in, star_iter_next);
}

/* DISPATCH: once per visit, never per arc (see dfs_visit). */
static void bfs_visit(const Graph* g, int root, bool in, Traversal* t)
{
    if (g->repr == GRAPH_STAR)
    {
        if (in)
            bfs_visit_star_in(g, root, t);
        else
            bfs_visit_star_out(g, root, t);
        return;
    }
    bfs_visit_with(g, root, t, in ? g->ops->iter_in : g->ops->iter_out, g->ops->iter_next);
}

/*
 * Kosaraju, pass 1: runs a DFS forest over G and returns its vertices in
 * reverse finish order (a permutation of 0 .. n-1), or NULL if an
 * allocation fails. The loop over every vertex is what the textbook's
 * "dummy vertex connected to everything" stands for. Leaves `t` fully
 * visited: the caller resets it before reusing it.
 */
static int* reverse_finish_order(const Graph* g, Traversal* t)
{
    const int  n     = g->n;
    int*       rpost = malloc((size_t)n * sizeof *rpost);
    dfs_frame* stack = stack_create(dfs_frame, n);
    if (!rpost || !stack)
    {
        LOG_ERROR("allocation failed for Kosaraju pass 1 (n=%d)", n);
        free(rpost);
        stack_free(stack);
        return NULL;
    }

    /* Every vertex finishes exactly once, so this counts down to 0. */
    int rpost_next = n;
    for (int s = 0; s < n; s++)
        if (t->dist[s] == -1)
            dfs_visit(g, s, t, stack, rpost, &rpost_next);

    stack_free(stack);
    return rpost;
}

/*
 * Runs a BFS forest over `t` (which must be fresh or reset), taking the
 * roots in the order roots[0 .. n-1] -- or 0 .. n-1 when roots is NULL --
 * and returns one component per tree.
 *
 * Each tree is a contiguous segment of t->order that begins with its
 * root, and roots are the only vertices with parent == -1 (see the
 * contract above bfs_visit). So once the forest is built, the components
 * are just t->order cut right before every root: that is all the second
 * loop does.
 */
static Components* forest_components(const Graph* g, const int* roots, bool in, Traversal* t)
{
    const int n = g->n;

    /* 1. Visit: one tree per root not already reached by an earlier one. */
    size_t trees = 0;
    for (int k = 0; k < n; k++)
    {
        int r = roots ? roots[k] : k;
        if (t->dist[r] != -1)
            continue;
        bfs_visit(g, r, in, t);
        trees++;
    }

    /* 2. Allocate: header, trees + 1 offsets, n ids -- one block. */
    Components* cc = malloc(sizeof *cc + (trees + 1 + (size_t)n) * sizeof(int));
    if (!cc)
    {
        LOG_ERROR("allocation failed for %zu components (n=%d)", trees, n);
        return NULL;
    }
    cc->count = trees;
    cc->n     = n;
    cc->start = (int*)(cc + 1); /* right after the header, which is 8-aligned */
    cc->ids   = cc->start + trees + 1;

    /* 3. Cut: copy t->order and record where each root sits. */
    size_t c = 0;
    for (int k = 0; k < n; k++)
    {
        cc->ids[k] = t->order[k];
        if (t->parent[cc->ids[k]] == -1)
            cc->start[c++] = k;
    }
    cc->start[trees] = n; /* sentinel: the last component ends at n */

    return cc;
}

/*
 * Weakly/strongly connected components, depending on g->directed.
 *
 * Undirected: every tree of a BFS forest over G is one connected
 * component. A single pass, roots taken in vertex order.
 *
 * Directed: Kosaraju. Pass 1 runs a DFS forest over G to get the reverse
 * finish order; this pass needs a DFS, a BFS has no finish order. Pass 2
 * runs a forest over G^T (in-arcs), taking roots in that order: each tree
 * is exactly one strongly connected component. Pass 2 only needs the set
 * of reached vertices, so it uses the cheaper BFS. Components come out in
 * topological order of the condensation (a component only has arcs
 * towards later ones).
 */
Components* graph_find_connected_components(const Graph* g)
{
    if (!g || g->n <= 0)
    {
        LOG_ERROR("invalid argument (g=%p)", (const void*)g);
        return NULL;
    }

    LOG_DEBUG("finding connected components (directed=%d, n=%d)", g->directed, g->n);

    Traversal* t = traversal_new(g->n);
    if (!t)
        return NULL;

    int*        roots = NULL; /* NULL = vertex order, all an undirected graph needs */
    Components* cc    = NULL;
    if (g->directed)
    {
        roots = reverse_finish_order(g, t);
        if (!roots)
            goto done;
        traversal_reset(t);
    }

    cc = forest_components(g, roots, g->directed, t);
    if (cc)
        LOG_DEBUG("found %zu components (n=%d)", cc->count, cc->n);

done:
    free(roots);
    traversal_cleanup(&t);
    return cc;
}

void components_cleanup(Components** cc)
{
    if (cc != NULL && *cc != NULL)
    {
        free(*cc); /* one block: header, offsets and ids */
        *cc = NULL;
        LOG_DEBUG("Components freed");
    }
}

Traversal* graph_dfs(const Graph* g, int source)
{
    if (!g || !vertex_ok(g, source))
    {
        LOG_ERROR("invalid argument (g=%p, source=%d)", (const void*)g, source);
        return NULL;
    }

    LOG_DEBUG("DFS from source=%d (n=%d)", source, g->n);

    Traversal* t = traversal_new(g->n);
    if (!t)
        return NULL;

    /* Capacity g->n: a vertex is pushed only after its dist is found to be
     * -1 and immediately set, so each vertex enters the stack at most once
     * and the pushes can never fail. */
    dfs_frame* stack = stack_create(dfs_frame, g->n);
    if (!stack)
    {
        LOG_ERROR("allocation failed for DFS stack (n=%d)", g->n);
        traversal_cleanup(&t);
        return NULL;
    }

    dfs_visit(g, source, t, stack, NULL, NULL);

    /* Empty stack: every vertex reachable from the source has been expanded.
     * Vertices that were never discovered stay at parent == -1, dist == -1. */
    stack_free(stack);
    LOG_DEBUG("DFS from source=%d reached %d/%d vertices", source, t->count, t->n);
    return t;
}

Traversal* graph_bfs(const Graph* g, int source)
{
    if (!g || !vertex_ok(g, source))
    {
        LOG_ERROR("invalid argument (g=%p, source=%d)", (const void*)g, source);
        return NULL;
    }

    LOG_DEBUG("BFS from source=%d (n=%d)", source, g->n);

    Traversal* t = traversal_new(g->n);
    if (!t)
        return NULL;

    bfs_visit(g, source, false, t);

    /* Frontier exhausted: no reachable vertex is left to explore.
     * Vertices that were never discovered stay at parent == -1, dist == -1. */
    LOG_DEBUG("BFS from source=%d reached %d/%d vertices", source, t->count, t->n);
    return t;
}

void traversal_cleanup(Traversal** t)
{
    if (t != NULL && *t != NULL)
    {
        free((*t)->order);
        free((*t)->parent);
        free((*t)->dist);
        free(*t);
        *t = NULL;
        LOG_DEBUG("Traversal freed");
    }
}

/* One star of the quick star Kruskal: its edges sorted lazily, cur is the
 * lightest one not yet extracted. Kept small: the heap moves it by value. */
typedef struct
{
    double          min_weight; // cur->w inline, so the comparer never chases cur
    GraphEdge*      cur;
    quick_sort_iter sorting;
    int             id;
} q_step_iter;


static int dheap_q_comparer(const void* a, const void* b)
{
    const q_step_iter* qa = a;
    const q_step_iter* qb = b;

    return (qa->min_weight > qb->min_weight) - (qa->min_weight < qb->min_weight);
}

/* Drops, from the range the next QUICK_SORT_STEP will partition, the edges
 * whose far end already sits in the tree of u: they can never join the
 * forest, so they are not worth partitioning. The range is the top frame of
 * the iterator, the leftmost one still to be treated; all its edges share u,
 * so find(u) is computed once and the filter costs one find(v) per edge. The
 * survivors are compacted at the start of the range and the frame shrinks
 * with it (a frame left empty is popped). Only that frame is touched, so the
 * edges already returned stay valid. Frames below it are filtered when their
 * turn comes: by then more trees have merged and more edges are dropped. */
static void star_filter_top(quick_sort_iter* it, int* union_find)
{
    GraphEdge* star = (GraphEdge*)it->edges;

    while (it->top > 0)
    {
        quick_sort_frame* frame = &it->frames[it->top - 1];
        if (frame->p == frame->q) // one edge: it is the next minimum, the caller's union rejects it if internal
            return;

        int root = uf_find(union_find, star[frame->p].u);
        int kept = frame->p;
        for (int i = frame->p; i <= frame->q; i++)
            if (uf_find(union_find, star[i].v) != root)
                star[kept++] = star[i];

        if (kept > frame->p)
        {
            frame->q = kept - 1;
            return;
        }
        it->top--; // every edge was internal: nothing left in this range
    }
}

/****************************************************************************************** */

#ifndef KB_K
#define KB_K 3 /* slots per star: the block stays 64 bytes up to KB_K = 10 */
#endif

typedef struct
{
    double  thr_w;     /* threshold (w, v): last edge extracted from the star */
    int     thr_v;     /* (-INFINITY, -1) before the first extraction         */
    int     start;     /* first edge of the star in list.edges                */
    int     deg;       /* current length, shrinks at every refill             */
    uint8_t head, len; /* next slot for Kruskal, valid slots                  */
    uint8_t mode;      /* 0 buffer, 1 in place (buf[0..2] = cur, end, k)      */
    int     buf[KB_K]; /* positions inside the star, lightest first           */
} __attribute__((aligned(64))) kb_star_t;

_Static_assert(sizeof(kb_star_t) == 64, "one cache line per star");
_Static_assert(KB_K >= 3, "in place mode keeps cur, end and k in buf[0..2]");

#ifndef KB_INPLACE
#define KB_INPLACE 0 /* 1: from the first refill on, partial quicksort in place (not faster) */
#endif
#ifndef KB_K_INPLACE
#define KB_K_INPLACE 32 /* edges sorted by the first in place refill, then doubled */
#endif

/* In place mode: the star is read from itself, cur is the next edge, [cur,
 * end) the part already sorted, k the size of the last partial sort */
#define KB_CUR(st) ((st)->buf[0])
#define KB_END(st) ((st)->buf[1])
#define KB_KIN(st) ((st)->buf[2])

/* Position inside the star of the edge at the head, in either mode */
static inline __attribute__((always_inline)) int kb_head_pos(const kb_star_t* st)
{
    return st->mode ? KB_CUR(st) : st->buf[st->head];
}

static inline __attribute__((always_inline)) bool kb_has_head(const kb_star_t* st)
{
    return st->mode ? KB_CUR(st) < KB_END(st) : st->head < st->len;
}

static inline __attribute__((always_inline)) void kb_advance(kb_star_t* st)
{
    if (st->mode)
        KB_CUR(st)++;
    else
        st->head++;
}

typedef struct
{
    double w;  /* weight of the head of the star */
    int    id; /* the vertex u of the star */
} kb_top_t;

/*
 * Heap of the star heads, 4-ary, lighter head first. K-best only ever touches
 * the root (the new head of the extracted star is heavier, so it can only
 * sink; an exhausted star leaves from the root), so DHeap's position map and
 * its sift-up attempt are pure overhead: this heap has neither. x sinks as a
 * hole from i: each level moves one child up, x is written once at the end.
 */
#define KB_HEAP_D 4

static inline __attribute__((always_inline)) void kb_heap_sink(kb_top_t* h, size_t n, size_t i,
                                                               kb_top_t x)
{
    for (;;)
    {
        size_t first = KB_HEAP_D * i + 1;
        if (first >= n)
            break;
        size_t last = first + KB_HEAP_D < n ? first + KB_HEAP_D : n;
        size_t best = first;
        for (size_t c = first + 1; c < last; c++)
            if (h[c].w < h[best].w)
                best = c;
        if (h[best].w >= x.w)
            break;
        h[i] = h[best];
        i    = best;
    }
    h[i] = x;
}

/* Heap order on h[0 .. n) in place, O(n) */
static void kb_heapify(kb_top_t* h, size_t n)
{
    if (n > 1)
        for (size_t i = (n - 2) / KB_HEAP_D + 1; i-- > 0;)
            kb_heap_sink(h, n, i, h[i]);
}

static inline __attribute__((always_inline)) int kb_less(double aw, int av, double bw, int bv)
{
    return aw < bw || (aw == bw && av < bv);
}

/*
 * Refill of a star: drop the edges extracted or internal, compact the
 * survivors, keep the KB_K best above the threshold. first = true on the
 * init scan: nothing extracted and every tree a singleton, so no find and
 * no writes. Returns the new len (0 = star exhausted).
 */

static inline __attribute__((always_inline)) int kb_refill(kb_star_t* st, GraphEdge* edges, int* uf,
                                                           bool first)
{
    GraphEdge* e    = edges + st->start;
    int*       b    = st->buf;
    int        root = first ? -1 : uf_find(uf, e[0].u); /* deg >= 1 here, see below */
    int        wr = 0, len = 0;

    /* keys of the buffer slots, kept in sync with b: comparing through e[b[j]]
     * costs two dependent loads, and e[wr] = x forces a reload every time */
    double bw[KB_K];
    int    bv[KB_K];
    bw[KB_K - 1] = INFINITY; /* worst slot of an empty buffer: everything enters */
    bv[KB_K - 1] = INT_MAX;

    for (int i = 0; i < st->deg; i++)
    {
        const GraphEdge x = e[i];

        if (!first)
        {
            if (!kb_less(st->thr_w, st->thr_v, x.w, x.v)) /* (w, v) <= threshold: extracted */
                continue;
            if (uf_find(uf, x.v) == root) /* internal to the tree of u */
                continue;
            e[wr] = x; /* compact: wr <= i, nothing unread is overwritten */
        }
        else
            wr = i; /* first scan: no writes, positions stay */

        if (len < KB_K || kb_less(x.w, x.v, bw[KB_K - 1], bv[KB_K - 1]))
        {
            int j = len < KB_K ? len++ : KB_K - 1; /* full: overwrite the worst */
            while (j > 0 && kb_less(x.w, x.v, bw[j - 1], bv[j - 1]))
            {
                b[j]  = b[j - 1];
                bw[j] = bw[j - 1];
                bv[j] = bv[j - 1];
                j--;
            }
            b[j]  = wr;
            bw[j] = x.w;
            bv[j] = x.v;
        }
        wr++;
    }

    st->deg  = wr;
    st->head = 0;
    st->len  = (uint8_t)len;
    return len;
}

/*
 * Moves the head of the buffer past the edges internal to the tree of u:
 * they can never join the forest, so they never reach the heap. A skipped
 * edge counts as extracted, so the threshold follows it. Returns false when
 * the buffer runs out.
 */
static inline __attribute__((always_inline)) bool kb_skip_internal(kb_star_t* st, const GraphEdge* e,
                                                                   int* uf)
{
    int root = -1;
    for (; kb_has_head(st); kb_advance(st))
    {
        const GraphEdge* x = &e[kb_head_pos(st)];
        if (root < 0)
            root = uf_find(uf, x->u);
        if (uf_find(uf, x->v) != root)
            return true;
        st->thr_w = x->w;
        st->thr_v = x->v;
    }
    return false;
}

/*
 * Refill in place. The star is compacted from the cursor on, dropping the
 * edges internal to the tree of u (and, on the switch from buffer mode, the
 * ones at or below the threshold, already extracted), then the next k edges
 * are partially sorted in place: KB_K_INPLACE on the switch, doubled at every
 * later refill. From then on the position separates the edges taken from the
 * others: no threshold. Returns the edges ready (0 = star exhausted).
 */
static inline __attribute__((always_inline)) int kb_refill_inplace(kb_star_t* st, GraphEdge* edges,
                                                                   int* uf)
{
    GraphEdge* e         = edges + st->start;
    const bool switching = !st->mode;
    const int  from      = switching ? 0 : KB_CUR(st);
    const int  root      = uf_find(uf, e[0].u); /* deg >= 1: the head just read is there */
    int        wr        = from;

    for (int i = from; i < st->deg; i++)
    {
        const GraphEdge x = e[i];
        if (switching && !kb_less(st->thr_w, st->thr_v, x.w, x.v)) /* already extracted */
            continue;
        if (uf_find(uf, x.v) == root) /* internal to the tree of u */
            continue;
        e[wr++] = x;
    }

    const int k     = switching ? KB_K_INPLACE : 2 * KB_KIN(st);
    const int avail = wr - from;
    const int end   = from + (k < avail ? k : avail);

    st->deg     = wr;
    st->mode    = 1;
    KB_CUR(st)  = from;
    KB_KIN(st)  = k;
    KB_END(st)  = end;
    if (avail > 0)
        QUICK_SORT_PARTIAL(e, wr, from, wr - 1, end);
    return avail;
}

/* One 64-byte block per vertex, aligned to the cache line; NULL on error */
static kb_star_t* kb_alloc(int n)
{
    const size_t bytes = ((size_t)(n > 0 ? n : 1) * sizeof(kb_star_t) + 63) & ~(size_t)63;
    return (kb_star_t*)aligned_alloc(64, bytes);
}

/*
 * Loads the stars of list into their blocks, kb[u] = star of u: threshold at
 * "nothing extracted" and first K-best scan. The head of each star goes to
 * init[]; returns how many. Only the blocks of the stars in list are written:
 * the heap built on init[] never names any other.
 */
static int kb_load(kb_star_t* kb, StarEdgeList list, int* uf, kb_top_t* init)
{
    GraphEdge* const e    = list.edges;
    const size_t     m    = list.count;
    int              tops = 0;

    /* each run of equal u is the star of u */
    for (size_t start = 0, end; start < m; start = end)
    {
        const int u = e[start].u;
        for (end = start + 1; end < m && e[end].u == u; end++)
            ;

        kb_star_t* st = &kb[u];
        st->start     = (int)start;
        st->deg       = (int)(end - start);
        st->thr_w     = -INFINITY; /* nothing extracted yet: below every (w, v) */
        st->thr_v     = -1;
        st->mode      = 0;

        kb_refill(st, e, uf, true); /* len >= 1: a run is never empty */
        init[tops++] = (kb_top_t){.w = e[st->start + st->buf[0]].w, .id = u};
    }
    return tops;
}

/*
 * The Kruskal loop of K-best: extracts the heap top, the lightest edge not
 * yet extracted, until the heap is empty or the forest has n - 1 edges. The
 * accepted edges are appended to MST[*mst_size ...].
 */
static void kb_drain(kb_star_t* kb, kb_top_t* heap, size_t size, GraphEdge* edges, int* union_find,
                     int n, GraphEdge* MST, int* mst_size)
{
    while (*mst_size < n - 1 && size > 0)
    {
        const int       u  = heap[0].id; // the star holding the lightest edge not yet extracted
        kb_star_t*      st = &kb[u];
        const GraphEdge x  = edges[st->start + kb_head_pos(st)];
        kb_advance(st);

        // A merge happens only if u and v were in different trees: no cycle
        if (uf_union(union_find, x.u, x.v))
            MST[(*mst_size)++] = x;

        // THE THRESHOLD IS ALWAYS THE LAST EDGE EXTRACTED FROM THE STAR
        st->thr_w = x.w;
        st->thr_v = x.v;

        // Buffer drained, internal edges included: scan the star again, nothing left
        // above the threshold = exhausted
        if (!kb_skip_internal(st, edges + st->start, union_find) &&
            (KB_INPLACE ? kb_refill_inplace(st, edges, union_find)
                        : kb_refill(st, edges, union_find, false)) == 0)
        {
            // the star leaves: the last leaf takes the root and sinks
            size--;
            if (size > 0)
                kb_heap_sink(heap, size, 0, heap[size]);
            continue;
        }

        // The new head is heavier than the old one: it can only sink
        kb_heap_sink(heap, size, 0, (kb_top_t){.w = edges[st->start + kb_head_pos(st)].w, .id = u});
    }
}

/*
 * Kruskal on K-best scans of the stars, merged through a heap of 16-byte
 * heads. Every star keeps in buf its KB_K lightest edges above its threshold
 * (the last edge extracted from it); the heap top is the lightest edge not
 * yet extracted. A star is scanned again, filtered and compacted, only when
 * its buffer runs out. The list is compacted in place: pass a copy to keep
 * it. NULL on error or directed g.
 */
GraphEdge* graph_mst_kbest_star_kruskal(const Graph* g, StarEdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    size_t     max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge* MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    kb_top_t*  heap       = (kb_top_t*)malloc(sizeof(kb_top_t) * (size_t)(g->n > 0 ? g->n : 1));
    int*       union_find = uf_create(g->n);
    kb_star_t* kb         = kb_alloc(g->n);

    if (MST == NULL || heap == NULL || union_find == NULL || kb == NULL)
    {
        LOG_ERROR("K-best star Kruskal allocation failed (n=%d, m=%zu)", g->n, list.count);
        free(MST);
        MST = NULL;
        goto cleanup;
    }

    // the heads are built in place into a heap: no copy, no position map
    size_t tops = (size_t)kb_load(kb, list, union_find, heap);
    kb_heapify(heap, tops);
    kb_drain(kb, heap, tops, edges, union_find, g->n, MST, mst_size);

    LOG_DEBUG("K-best star Kruskal: %d edges in the spanning forest", *mst_size);

cleanup:
    if (union_find != NULL)
        uf_free(union_find);
    free(kb);
    free(heap);
    return MST;
}

/*
 * Kruskal fed by lazily sorted stars merged through a heap: the heap top
 * is the lightest edge not yet extracted, and every star is sorted only as
 * far as the MST actually reads it.
 *
 * The edges come as a StarEdgeList: grouped by source, so the edges of a
 * star are contiguous. Each run of equal u is a star, sorted in place
 * inside its own slice. Star s owns the slice of frames at the same offset
 * as its edges: its edge count is its bound. NULL on error or directed g.
 */
GraphEdge* graph_mst_quick_star_kruskal(const Graph* g, StarEdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;
    const size_t     m     = list.count;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    /**
     * Don't waste computazion on the heap move downs: only compute for each star the k-lowest and use them.
     */
    size_t            max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge*        MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    quick_sort_frame* frames     = (quick_sort_frame*)malloc(sizeof(quick_sort_frame) * (m > 0 ? m : 1));
    int*              union_find = uf_create(g->n);
    q_step_iter*      heap = dheap_create(g->n, DHEAP_DEFAULT_D, dheap_q_comparer, q_step_iter);

    if (MST == NULL || frames == NULL || union_find == NULL || heap == NULL)
    {
        LOG_ERROR("quick star Kruskal allocation failed (n=%d, m=%zu)", g->n, m);
        free(MST);
        MST = NULL;
        goto cleanup;
    }

    // INIT: ONE ITERATOR PER RUN OF EQUAL u, ITS LIGHTEST EDGE SETTLED. THE LIST
    // IS GROUPED BY SOURCE (StarEdgeList), SO THERE ARE AT MOST n RUNS: THE HEAP FITS
    int stars = 0;
    for (size_t start = 0, end; start < m; start = end)
    {
        for (end = start + 1; end < m && edges[end].u == edges[start].u; end++)
            ;

        GraphEdge*  star = edges + start;
        q_step_iter q_it = {.id = stars++};

        QUICK_SORT_ITER_INIT(&q_it.sorting, star, (int)(end - start), frames + start);
        q_it.cur        = QUICK_SORT_STEP(&q_it.sorting, star);
        q_it.min_weight = q_it.cur->w;
        dheap_insert(heap, q_it, dheap_q_comparer);
    }

    // EVERY EDGE IS IN ONE STAR ONLY: AT MOST m EXTRACTIONS
    while (*mst_size < g->n - 1 && !dheap_empty(heap))
    {
        q_step_iter* best = &heap[0]; // the star holding the lightest edge not yet extracted

        // A merge happens only if u and v were in different trees: no cycle
        if (uf_union(union_find, best->cur->u, best->cur->v))
            MST[(*mst_size)++] = *best->cur;

        star_filter_top(&best->sorting, union_find);
        GraphEdge* next_edge = QUICK_SORT_STEP(&best->sorting, (GraphEdge*)best->sorting.edges);

        if (next_edge == NULL)
        {
            q_step_iter exhausted;
            dheap_pop(heap, &exhausted, dheap_q_comparer);
            continue;
        }

        best->cur        = next_edge;
        best->min_weight = next_edge->w;
        dheap_update(heap, *best, dheap_q_comparer);
    }

    LOG_DEBUG("quick star Kruskal: %d edges in the spanning forest", *mst_size);

cleanup:
    if (heap != NULL)
        dheap_free(heap);
    if (union_find != NULL)
        uf_free(union_find);
    free(frames);
    return MST;
}

// WEIGHTS SAMPLED TO PICK THE THRESHOLD OF A ROUND
#define FILTER_SAMPLE 1024

/*
 * Quickselect (Hoare): the k-th smallest of a[0..n-1], expected O(n), no
 * sorting. Reorders a[]. The pivot is the middle element, so sorted or
 * reversed input does not degrade it.
 */
static double select_kth(double* a, size_t n, size_t k)
{
    size_t lo = 0, hi = n - 1;

    while (lo < hi)
    {
        double pivot = a[lo + (hi - lo) / 2];
        size_t i = lo, j = hi;

        while (i <= j)
        {
            while (a[i] < pivot) i++;
            while (a[j] > pivot) j--;
            if (i <= j)
            {
                double t = a[i]; a[i] = a[j]; a[j] = t;
                i++;
                if (j == 0) break;
                j--;
            }
        }
        // a[lo..j] <= pivot <= a[i..hi]; KEEP ONLY THE SIDE HOLDING k
        if (k <= j)      hi = j;
        else if (k >= i) lo = i;
        else             return a[k];
    }
    return a[k];
}

/*
 * Threshold tau of a round: the weight under which about `target` of the
 * `alive` edges lie, estimated on a sample of FILTER_SAMPLE random edges.
 * A wrong estimate costs time, never correctness. HUGE_VAL when the target
 * covers every alive edge: all of them are light and no sample is needed.
 */
static double filter_threshold(const GraphEdge* edges, size_t alive, size_t target, uint64_t* rng)
{
    if (target >= alive)
        return HUGE_VAL;

    double sample[FILTER_SAMPLE];
    size_t samples = alive < FILTER_SAMPLE ? alive : FILTER_SAMPLE;

    for (size_t i = 0; i < samples; i++)
    {
        // XORSHIFT, ALWAYS FROM THE SAME SEED: SAME GRAPH, SAME ROUNDS
        *rng ^= *rng << 13;
        *rng ^= *rng >> 7;
        *rng ^= *rng << 17;
        sample[i] = edges[*rng % alive].w;
    }

    // THE FRACTION target / alive OF THE SAMPLE LIES BELOW THE THRESHOLD:
    // ONLY THAT RANK IS NEEDED, SO SELECT IT INSTEAD OF SORTING THE SAMPLE
    size_t k = (size_t)((double)target / (double)alive * (double)samples);
    if (k >= samples)
        k = samples - 1;
    return select_kth(sample, samples, k);
}

/*
 * One pass over the first *alive edges of the star list, in order:
 *   - an edge whose ends already share a tree is dropped: it can never join
 *     the forest, so it is never sorted;
 *   - one with w <= tau is copied to light[], to be sorted and read by Kruskal;
 *   - the others are compacted at the start of edges[], keeping their order,
 *     so the list stays grouped by source for the next round.
 * All the edges of a star share u: its root is found once per star, and the
 * pass costs one find(v) per edge. *alive becomes the number of edges kept;
 * returns the number of light edges.
 */
static size_t filter_pass(GraphEdge* edges, size_t* alive, GraphEdge* light, double tau, int* union_find)
{
    size_t kept        = 0;
    size_t light_count = 0;
    int    last_u      = -1;
    int    root_u      = -1;

    for (size_t i = 0; i < *alive; i++)
    {
        GraphEdge edge = edges[i];

        if (edge.u != last_u)
        {
            last_u = edge.u;
            root_u = uf_find(union_find, edge.u);
        }

        // INTERNAL EDGE: u AND v ARE IN THE SAME TREE ALREADY
        if (uf_find(union_find, edge.v) == root_u)
        {
            continue;
        }

        if (edge.w <= tau)
        {
            light[light_count++] = edge;
        }
        else
        {
            edges[kept++] = edge;
        }
    }

    *alive = kept;
    return light_count;
}

/*
 * Starting size of the light part of a Filter-Kruskal round, in multiples
 * of n: doubled at every round. Overridable to compare it with the hybrid at
 * the same target (bench/mst_big.c)
 */
#ifndef FS_TARGET
#define FS_TARGET 2
#endif

/*
 * Filter-Kruskal in rounds on a list grouped by source. Each round picks a
 * threshold tau that leaves about `target` of the alive edges light, filters
 * the list (see filter_pass), then runs the usual Kruskal loop on the light
 * edges only, sorted lazily. The heavy edges are not touched again until the
 * next round, when the trees built meanwhile let the filter drop most of
 * them. `target` starts at FS_TARGET * n and doubles every round, so the early rounds
 * settle the forest on few edges. Every light edge weighs <= tau < every
 * edge kept, so the edges are consumed in Kruskal order.
 *
 * The list is reordered in place. NULL on error or directed g.
 */
GraphEdge* graph_mst_filter_star_kruskal(const Graph* g, StarEdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;
    const size_t     m     = list.count;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    size_t            max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    size_t            capacity   = m > 0 ? m : 1;
    GraphEdge*        MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    GraphEdge*        light      = (GraphEdge*)malloc(sizeof(GraphEdge) * capacity);
    quick_sort_frame* frames     = (quick_sort_frame*)malloc(sizeof(quick_sort_frame) * capacity);
    int*              union_find = uf_create(g->n);

    if (MST == NULL || light == NULL || frames == NULL || union_find == NULL)
    {
        LOG_ERROR("filter star Kruskal allocation failed (n=%d, m=%zu)", g->n, m);
        free(MST);
        MST = NULL;
        goto cleanup;
    }

    uint64_t rng    = 88172645463325252ULL;
    size_t   alive  = m;
    size_t   target = (size_t)FS_TARGET * (size_t)g->n;

    while (*mst_size < g->n - 1 && alive > 0)
    {
        double tau         = filter_threshold(edges, alive, target, &rng);
        size_t light_count = filter_pass(edges, &alive, light, tau, union_find);

        // THE LIGHT EDGES COME OUT BY WEIGHT, SORTED ONLY AS FAR AS THE FOREST NEEDS
        quick_sort_iter sorting;
        if (light_count > 0 && QUICK_SORT_ITER_INIT(&sorting, light, (int)light_count, frames))
        {
            GraphEdge* min_edge;

            while (*mst_size < g->n - 1 && (min_edge = QUICK_SORT_STEP(&sorting, light)) != NULL)
            {
                // A merge happens only if u and v were in different trees: no cycle
                if (uf_union(union_find, min_edge->u, min_edge->v))
                {
                    MST[(*mst_size)++] = *min_edge;
                }
            }
        }

        target *= 2;
    }

    LOG_DEBUG("filter star Kruskal: %d edges in the spanning forest", *mst_size);

cleanup:
    if (union_find != NULL)
        uf_free(union_find);
    free(frames);
    free(light);
    return MST;
}

/*
 * Starting size of the light part of a hybrid round, in multiples of n
 * (Filter-Kruskal uses 2): doubled at every round
 */
#ifndef FKB_TARGET
#define FKB_TARGET 2
#endif

/*
 * Hybrid of Filter-Kruskal and K-best. The rounds are those of
 * graph_mst_filter_star_kruskal: a global threshold tau from a sample, one
 * filter_pass that drops the internal edges, moves the light ones (w <= tau)
 * to a buffer and compacts the heavy ones in place. The light buffer comes
 * out of the pass still grouped by source, so it is a StarEdgeList itself:
 * K-best reads it (one scan per star, a heap of heads) instead of a global
 * lazy quicksort. Every light edge weighs <= tau < every heavy one, so the
 * rounds consume the edges in Kruskal order. `target` starts at FKB_TARGET * n
 * and doubles every round.
 *
 * The list is reordered in place. NULL on error or directed g.
 */
GraphEdge* graph_mst_filter_kbest_kruskal(const Graph* g, StarEdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;
    const size_t     m     = list.count;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    size_t     max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge* MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    GraphEdge* light      = (GraphEdge*)malloc(sizeof(GraphEdge) * (m > 0 ? m : 1));
    kb_top_t*  init       = (kb_top_t*)malloc(sizeof(kb_top_t) * (size_t)(g->n > 0 ? g->n : 1));
    int*       union_find = uf_create(g->n);
    kb_star_t* kb         = kb_alloc(g->n);

    if (MST == NULL || light == NULL || init == NULL || union_find == NULL || kb == NULL)
    {
        LOG_ERROR("filter K-best Kruskal allocation failed (n=%d, m=%zu)", g->n, m);
        free(MST);
        MST = NULL;
        goto cleanup;
    }

    uint64_t rng    = 88172645463325252ULL;
    size_t   alive  = m;
    size_t   target = (size_t)FKB_TARGET * (size_t)g->n;

    while (*mst_size < g->n - 1 && alive > 0)
    {
        double tau         = filter_threshold(edges, alive, target, &rng);
        size_t light_count = filter_pass(edges, &alive, light, tau, union_find);

        // THE LIGHT EDGES ARE STILL GROUPED BY SOURCE: K-BEST READS THEM AS STARS
        if (light_count > 0)
        {
            size_t tops = (size_t)kb_load(kb, (StarEdgeList){light, light_count}, union_find, init);
            kb_heapify(init, tops);
            kb_drain(kb, init, tops, light, union_find, g->n, MST, mst_size);
        }

        target *= 2;
    }

    LOG_DEBUG("filter K-best Kruskal: %d edges in the spanning forest", *mst_size);

cleanup:
    if (union_find != NULL)
        uf_free(union_find);
    free(kb);
    free(init);
    free(light);
    return MST;
}

GraphEdge* graph_mst_quick_kruskal(const Graph* g, EdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;
    const size_t     m     = list.count;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    size_t            max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge*        MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    quick_sort_frame* frames     = (quick_sort_frame*)malloc(sizeof(quick_sort_frame) * (m > 0 ? m : 1));
    int*              union_find = uf_create(g->n);
    if (MST == NULL || frames == NULL || union_find == NULL)
    {
        free(MST);
        free(frames);
        if (union_find != NULL)
        {
            uf_free(union_find);
        }
        return NULL;
    }

    quick_sort_iter sorting;

    // No edges: nothing to sort, the forest is empty
    if (m > 0 && QUICK_SORT_ITER_INIT(&sorting, edges, (int)m, frames))
    {
        GraphEdge* min_edge_i;

        // The edges come out by weight, sorted only as far as the forest needs
        while (*mst_size < g->n - 1 && (min_edge_i = QUICK_SORT_STEP(&sorting, edges)) != NULL)
        {
            // A merge happens only if u and v were in different trees: no cycle
            if (uf_union(union_find, min_edge_i->u, min_edge_i->v))
            {
                MST[(*mst_size)++] = *min_edge_i;
            }
        }
    }

    free(frames);
    uf_free(union_find);
    return MST;
}

GraphEdge* graph_mst_kruskal(const Graph* g, EdgeList list, int* mst_size)
{
    GraphEdge* const edges = list.edges;
    const size_t     m     = list.count;

    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    // The MST has at most n - 1 edges: fewer when g is disconnected (a forest)
    size_t     max_edges  = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge* MST        = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
    int*       union_find = uf_create(g->n);
    if (MST == NULL || union_find == NULL)
    {
        free(MST);
        if (union_find != NULL)
        {
            uf_free(union_find);
        }
        return NULL;
    }

    QUICK_SORT(edges, (int)m, 0, (int)m - 1);

    LOG_DEBUG("Ordering edges done with quick sort!");

    for (size_t i = 0; *mst_size < g->n - 1 && i < m; i++)
    {
        // A merge happens only if u and v were in different trees: no cycle
        if (uf_union(union_find, edges[i].u, edges[i].v))
        {
            MST[(*mst_size)++] = edges[i];
        }
    }

    LOG_DEBUG("Kruskal: %d edges in the spanning forest", *mst_size);

    uf_free(union_find);
    return MST;
}
