#include "../include/Traversal.h"
#include "../include/Graph.h"
#include "../include/Queue.h"
#include <stdlib.h>

/*
 * Allocates a Traversal for a graph of n vertices, with all vertices
 * marked "unreached" (parent = -1, dist = -1), as required by the
 * contract in Graph.h. Returns NULL if an allocation fails.
 */
static Traversal* traversal_new(int n)
{
    Traversal* t = malloc(sizeof *t);
    if (!t)
        return NULL;
    t->order  = malloc((size_t)n * sizeof *t->order);
    t->parent = malloc((size_t)n * sizeof *t->parent);
    t->dist   = malloc((size_t)n * sizeof *t->dist);
    t->count  = 0;
    t->n      = n;
    if (!t->order || !t->parent || !t->dist)
    {
        traversal_free(t); /* free(NULL) is legal: only frees what actually succeeded */
        return NULL;
    }
    for (int v = 0; v < n; v++)
    {
        t->parent[v] = -1;
        t->dist[v]   = -1;
    }
    return t;
}

Traversal* graph_bfs(const Graph* g, int source)
{
    if (!g || !vertex_ok(g, source))
        return NULL;

    Traversal* t = traversal_new(g->n);
    if (!t)
        return NULL;

    /* Capacity g->n: given the invariant above, the queue can never fill
     * up, so the enqueues below can never fail. */
    int* q = queue_create(int, g->n);
    if (!q)
    {
        traversal_free(t);
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
    return t;
}

void PrimMST(GraphEdge* edges, int count)
{
}

void traversal_free(Traversal* t)
{
    if (!t)
        return;
    free(t->order);
    free(t->parent);
    free(t->dist);
    free(t);
}
