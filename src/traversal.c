#include "../include/Traversal.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include "../include/Sorting.h"
#include "../include/Stack.h"
#include "../include/UnionFInd.h"
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
static void dfs_visit(const Graph* g, int root, Traversal* t, dfs_frame* stack, int* rpost,
                      int* rpost_next)
{
    /* The root is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex of its tree in the visit order. */
    t->dist[root]        = 0;
    t->order[t->count++] = root;

    dfs_frame _root_frame = {0};
    _root_frame.vertex_id = root;
    g->ops->iter_out(g, root, &_root_frame.it);
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
        if (!g->ops->iter_next(&_top_frame->it, &v, NULL))
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
        g->ops->iter_out(g, v, &child.it);
        stack_push(stack, child);
    }
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
static void bfs_visit(const Graph* g, int root, bool in, Traversal* t)
{
    void (*expand)(const Graph*, int, GraphIter*) = in ? g->ops->iter_in : g->ops->iter_out;

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
        while (g->ops->iter_next(&it, &v, NULL))
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

GraphEdge* graph_mst_kruskal(const Graph* g, GraphEdge* edges, int* mst_size)
{
    if (g == NULL || edges == NULL || mst_size == NULL || g->directed)
    {
        LOG_ERROR("Kruskal needs an undirected graph and non NULL arguments");
        return NULL;
    }

    *mst_size = 0;

    // The MST has at most n - 1 edges: fewer when g is disconnected (a forest)
    size_t     max_edges = g->n > 1 ? (size_t)(g->n - 1) : 1;
    GraphEdge* MST       = (GraphEdge*)malloc(sizeof(GraphEdge) * max_edges);
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

    QuickSort(edges, (int)g->m, 0, (int)g->m - 1);

    LOG_DEBUG("Ordering edges done with quick sort!");

    for (size_t i = 0; *mst_size < g->n - 1 && i < g->m; i++)
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

