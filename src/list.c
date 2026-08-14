/*
 * Adjacency list representation.
 *
 * g->data is an array of n chain heads; adj[u] is a linked chain of u's
 * out-edges. Undirected edges are stored twice (once per endpoint),
 * except self-loops which are stored once.
 *
 * The only dynamic representation: add/remove are cheap, but in-neighbor
 * queries on directed graphs must scan every chain, O(n+m).
 */
#include "../include/Graph.h"
#include <stdlib.h>

typedef struct EdgeNode
{
    int              to;
    double           w;
    struct EdgeNode* next;
} EdgeNode;

static EdgeNode** heads(const Graph* g)
{
    return (EdgeNode**)g->data;
}

static EdgeNode* find(EdgeNode* node, int v)
{
    while (node && node->to != v)
        node = node->next;
    return node;
}

/* Push u->v on the front of u's chain. */
static int push(Graph* g, int u, int v, double w)
{
    EdgeNode* nd = malloc(sizeof *nd);
    if (!nd)
        return GRAPH_ERR_ALLOC;
    nd->to      = v;
    nd->w       = w;
    nd->next    = heads(g)[u];
    heads(g)[u] = nd;
    return GRAPH_OK;
}

static int list_add_edge(Graph* g, int u, int v, double w)
{
    EdgeNode* nd = find(heads(g)[u], v);
    if (nd)
    {
        /* edge exists: update the weight (both copies if undirected) */
        nd->w = w;
        if (!g->directed && u != v)
            find(heads(g)[v], u)->w = w;
        return GRAPH_OK;
    }
    int rc = push(g, u, v, w);
    if (rc != GRAPH_OK)
        return rc;
    if (!g->directed && u != v)
    {
        rc = push(g, v, u, w);
        if (rc != GRAPH_OK)
            return rc;
    }
    g->m++;
    return GRAPH_OK;
}

/* Unlink v from u's chain; pointer-to-pointer avoids a "previous node"
 * special case for the head. Returns true if an edge was removed. */
static bool unlink_edge(Graph* g, int u, int v)
{
    for (EdgeNode** pp = &heads(g)[u]; *pp; pp = &(*pp)->next)
    {
        if ((*pp)->to == v)
        {
            EdgeNode* dead = *pp;
            *pp            = dead->next;
            free(dead);
            return true;
        }
    }
    return false;
}

static int list_remove_edge(Graph* g, int u, int v)
{
    if (!unlink_edge(g, u, v))
        return GRAPH_OK; /* absent edge: nothing to do */
    if (!g->directed && u != v)
        unlink_edge(g, v, u);
    g->m--;
    return GRAPH_OK;
}

static double list_get_weight(const Graph* g, int u, int v)
{
    EdgeNode* nd = find(heads(g)[u], v);
    return nd ? nd->w : 0.0;
}

static int list_out_degree(const Graph* g, int u)
{
    int d = 0;
    for (EdgeNode* nd = heads(g)[u]; nd; nd = nd->next)
        d++;
    return d;
}

static int list_in_degree(const Graph* g, int u)
{
    if (!g->directed)
        return list_out_degree(g, u);
    /* directed: the chains only know successors -- full O(n+m) scan */
    int d = 0;
    for (int s = 0; s < g->n; s++)
        for (EdgeNode* nd = heads(g)[s]; nd; nd = nd->next)
            if (nd->to == u)
                d++;
    return d;
}

static void list_iter_out(const Graph* g, int u, GraphIter* it)
{
    it->g    = g;
    it->u    = u;
    it->i    = 0;
    it->node = heads(g)[u];
    it->in   = false;
}

static void list_iter_in(const Graph* g, int u, GraphIter* it)
{
    it->g  = g;
    it->u  = u;
    it->in = true;
    if (!g->directed)
    {
        it->i    = 0;
        it->node = heads(g)[u]; /* symmetric: in == out */
    }
    else
    {
        it->i    = 0; /* scan every chain for edges into u */
        it->node = heads(g)[0];
    }
}

static bool list_iter_next(GraphIter* it, int* v, double* w)
{
    if (!it->in || !it->g->directed)
    {
        EdgeNode* nd = it->node;
        if (!nd)
            return false;
        it->node = nd->next;
        if (v)
            *v = nd->to;
        if (w)
            *w = nd->w;
        return true;
    }
    /* directed in-neighbors: resume the scan across all chains */
    const Graph* g = it->g;
    while (it->i < g->n)
    {
        EdgeNode* nd = it->node;
        while (nd && nd->to != it->u)
            nd = nd->next;
        if (nd)
        {
            it->node = nd->next;
            if (v)
                *v = it->i; /* the source vertex is the neighbor */
            if (w)
                *w = nd->w;
            return true;
        }
        it->i++;
        it->node = it->i < g->n ? heads(g)[it->i] : NULL;
    }
    return false;
}

static void list_destroy(Graph* g)
{
    for (int u = 0; u < g->n; u++)
    {
        EdgeNode* nd = heads(g)[u];
        while (nd)
        {
            EdgeNode* next = nd->next;
            free(nd);
            nd = next;
        }
    }
    free(g->data);
}

static const GraphOps list_ops = {
    .add_edge    = list_add_edge,
    .remove_edge = list_remove_edge,
    .get_weight  = list_get_weight,
    .out_degree  = list_out_degree,
    .in_degree   = list_in_degree,
    .iter_out    = list_iter_out,
    .iter_in     = list_iter_in,
    .iter_next   = list_iter_next,
    .destroy     = list_destroy,
};

int list_init(Graph* g)
{
    g->data = calloc((size_t)g->n, sizeof(EdgeNode*));
    if (!g->data)
        return GRAPH_ERR_ALLOC;
    g->ops = &list_ops;
    return GRAPH_OK;
}
