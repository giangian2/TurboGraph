#include "../include/Traversal.h"
#include "../include/Graph.h"
#include "../include/Queue.h"
#include "../include/Stack.h"
#include <stdio.h>
#include <stdlib.h>


static Component* undirected_find_connected_components(const Graph* g){
    for(int c = 0; c < g->n; c++){
        Traversal* trav = graph_dfs(g, c);
    }
    return NULL;
}

static Component* directed_find_connected_components(const Graph* g){
    return NULL;
}

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
        traversal_cleanup(&t); /* free(NULL) is legal: only frees what actually succeeded */
        return NULL;
    }
    for (int v = 0; v < n; v++)
    {
        t->parent[v] = -1;
        t->dist[v]   = -1;
    }
    return t;
}

/**
 * The purpose of this function is finding connected components
 * on both directed and undirected graphs.
 */
Component* graph_find_connected_components(const Graph *g){
    if(g->directed){
        return directed_find_connected_components(g);
    }
    return undirected_find_connected_components(g);
}



Traversal* graph_dfs(const Graph* g, int source)
{
    /* One frame per vertex on the DFS path, iterator included: that cursor
     * is the whole point -- it is what a recursive DFS keeps implicitly in
     * its activation record. */
    typedef struct
    {
        int       vertex_id;
        GraphIter it;
    } dfs_frame;

    if (!g || !vertex_ok(g, source))
        return NULL;

    Traversal* t = traversal_new(g->n);
    if (!t)
        return NULL;

    /* Capacity g->n: a vertex is pushed only after its dist is found to be
     * -1 and immediately set, so each vertex enters the stack at most once
     * and the pushes below can never fail. */
    dfs_frame* stack = stack_create(dfs_frame, g->n);
    if (!stack)
    {
        return NULL;
    }

    /* The source is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex in the visit order. */
    t->dist[source]      = 0;
    t->order[t->count++] = source;

    dfs_frame _source_frame = {0};
    _source_frame.vertex_id = source;
    g->ops->iter_out(g, source, &_source_frame.it);
    stack_push(stack, _source_frame);

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

        /* Adjacency exhausted: this vertex is done, backtrack to its parent. */
        if (!g->ops->iter_next(&_top_frame->it, &v, NULL))
        {
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
        child.vertex_id   = v;
        g->ops->iter_out(g, v, &child.it);
        stack_push(stack, child);
    }

    /* Empty stack: every vertex reachable from the source has been expanded.
     * Vertices that were never discovered stay at parent == -1, dist == -1. */
    stack_free(stack);
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

void traversal_cleanup(Traversal** t) {
    if (t != NULL && *t != NULL) {
        free((*t)->order);
        free((*t)->parent);
        free((*t)->dist);
        free(*t);
        *t = NULL;
        printf("[DEBUG] Traversal freed automatically!\n");
    }
}

