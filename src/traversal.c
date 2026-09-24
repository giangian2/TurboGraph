#include "../include/Traversal.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include "../include/Queue.h"
#include "../include/Stack.h"
#include <stdio.h>
#include <stdlib.h>

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
 * Iterative DFS from `root`, growing the forest held in `t`.
 *
 * `t` is shared across calls: any vertex with dist != -1 counts as already
 * visited and is not entered again, so calling this once per unvisited
 * root builds a DFS forest in O(n + m) overall. `root` itself must still
 * be unvisited; it becomes the root of a new tree (parent -1, dist 0) and
 * its tree is appended to t->order as one contiguous segment.
 *
 *   in      false walks out-arcs (DFS on G), true walks in-arcs (DFS on
 *           the transpose G^T, for free: every representation has iter_in)
 *   stack   empty, capacity >= g->n; left empty on return
 *   post    optional (NULL): vertices are appended to post[*npost] in
 *           finish order, i.e. when their adjacency is exhausted
 */
static void dfs_visit(const Graph* g, int root, bool in, Traversal* t, dfs_frame* stack, int* post,
                      int* npost)
{
    void (*expand)(const Graph*, int, GraphIter*) = in ? g->ops->iter_in : g->ops->iter_out;

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
        if (!g->ops->iter_next(&_top_frame->it, &v, NULL))
        {
            if (post)
                post[(*npost)++] = _top_frame->vertex_id;
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

/*
 * Runs a DFS forest over `t` (which must be fresh or reset), taking the
 * roots in the order given by roots[0 .. n-1], and returns one Component
 * per tree.
 *
 * No bookkeeping of where each tree starts is needed: dfs_visit() lays
 * every tree out as a contiguous segment of t->order that begins with its
 * root, and roots are exactly the vertices left with parent == -1.
 *
 * The result is a single block -- the Component array followed by the n
 * vertex ids the components point into -- so one free() releases it all.
 */
static Component* forest_components(const Graph* g, const int* roots, bool in, Traversal* t,
                                    dfs_frame* stack, size_t* count)
{
    const int n     = g->n;
    size_t    trees = 0;

    for (int k = 0; k < n; k++)
    {
        if (t->dist[roots[k]] != -1)
            continue;
        dfs_visit(g, roots[k], in, t, stack, NULL, NULL);
        trees++;
    }

    Component* comps = malloc(trees * sizeof *comps + (size_t)n * sizeof(int));
    if (!comps)
    {
        LOG_ERROR("allocation failed for %zu components (n=%d)", trees, n);
        return NULL;
    }

    int*   ids = (int*)(comps + trees); /* Component is 8-aligned: so is ids */
    size_t c   = 0;
    for (int k = 0; k < n; k++)
    {
        ids[k] = t->order[k];
        if (t->parent[ids[k]] == -1) /* a root opens a new component */
        {
            comps[c].node_ids = &ids[k];
            comps[c].size     = 0;
            c++;
        }
        comps[c - 1].size++;
    }

    *count = trees;
    return comps;
}

/*
 * Weakly/strongly connected components, depending on g->directed.
 *
 * Undirected: every DFS tree of a forest over G is one connected
 * component. A single pass, roots taken in vertex order.
 *
 * Directed: Kosaraju. Pass 1 runs a DFS forest over G recording finish
 * order; the outer loop over all vertices plays the role of the textbook
 * "dummy vertex connected to everything", without touching the graph.
 * Pass 2 runs a DFS forest over G^T (iter_in), taking roots by decreasing
 * finish time: each tree of that forest is exactly one strongly connected
 * component, and components come out in topological order of the
 * condensation (a component only has arcs towards later ones).
 */
Component* graph_find_connected_components(const Graph* g, size_t* count)
{
    if (!g || !count || g->n <= 0)
    {
        LOG_ERROR("invalid argument (g=%p, count=%p)", (const void*)g, (void*)count);
        if (count)
            *count = 0;
        return NULL;
    }

    LOG_DEBUG("finding connected components (directed=%d, n=%d)", g->directed, g->n);

    const int  n     = g->n;
    Component* comps = NULL;
    *count           = 0;

    Traversal* t     = traversal_new(n);
    dfs_frame* stack = stack_create(dfs_frame, n);
    int*       roots = malloc((size_t)n * sizeof *roots);
    if (!t || !stack || !roots)
    {
        LOG_ERROR("allocation failed for components state (n=%d)", n);
        goto done;
    }

    if (g->directed)
    {
        /* Pass 1: finish order over G. Every vertex finishes exactly once,
         * so roots ends up holding a permutation of 0 .. n-1. */
        int nfinished = 0;
        for (int s = 0; s < n; s++)
            if (t->dist[s] == -1)
                dfs_visit(g, s, false, t, stack, roots, &nfinished);

        /* Decreasing finish time for pass 2. */
        for (int i = 0, j = n - 1; i < j; i++, j--)
        {
            int tmp  = roots[i];
            roots[i] = roots[j];
            roots[j] = tmp;
        }
        traversal_reset(t);
    }
    else
    {
        for (int v = 0; v < n; v++)
            roots[v] = v;
    }

    /* Pass 2 (directed) walks the transpose; undirected just walks G. */
    comps = forest_components(g, roots, g->directed, t, stack, count);

    LOG_DEBUG("found %zu components (n=%d)", *count, n);

done:
    free(roots);
    stack_free(stack);
    traversal_cleanup(&t);
    return comps;
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

    dfs_visit(g, source, false, t, stack, NULL, NULL);

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

    /* Capacity g->n: given the invariant above, the queue can never fill
     * up, so the enqueues below can never fail. */
    int* q = queue_create(int, g->n);
    if (!q)
    {
        LOG_ERROR("allocation failed for BFS queue (n=%d)", g->n);
        traversal_cleanup(&t);
        return NULL;
    }

    /* The source is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex in the visit order. */
    t->dist[source]      = 0;
    t->order[t->count++] = source;
    queue_enqueue(q, source);

    /* While there is a discovered but not-yet-processed vertex... */
    while (!queue_is_empty(q))
    {
        int u = queue_dequeue(q);

        /* Iterate the outgoing neighbors of u (the vertex just dequeued,
         * NOT the source). The iterator lives on the stack: no malloc. */
        GraphIter it;
        g->ops->iter_out(g, u, &it);

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
            queue_enqueue(q, v);
        }
    }

    /* Empty queue: no reachable vertex is left to explore.
     * Vertices that were never discovered stay at parent == -1, dist == -1. */
    queue_free(q);
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
