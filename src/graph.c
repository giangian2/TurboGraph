/*
 * Public API: argument validation + dispatch through g->ops.
 * Nothing here knows how any representation works.
 */
#include "../include/Graph.h"
#include "../include/Debug.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @param n
 * @param deirected
 * @param repr
 *
 * @return Pointer to Graph structure allocated in Heap,
 * for the representation specified in input and dimension n
 */
static Graph* shell(int n, bool directed, GraphRepr repr)
{
    Graph* g = malloc(sizeof(Graph));
    if (!g)
    {
        LOG_ERROR("allocation failed for Graph shell (n=%d)", n);
        return NULL;
    }
    g->n        = n;
    g->m        = 0;
    g->directed = directed;
    g->repr     = repr;
    g->ops      = NULL;
    g->data     = NULL;
    return g;
}

Graph* graph_create(int n, bool directed, GraphRepr repr)
{
    if (n <= 0)
    {
        LOG_ERROR("invalid vertex count (n=%d)", n);
        return NULL;
    }
    Graph* g = shell(n, directed, repr);
    if (!g)
        return NULL;
    int rc;
    switch (repr)
    {
    case GRAPH_MATRIX:
        rc = GRAPH_ERR_ALLOC;
        break;
    case GRAPH_LIST:
        rc = list_init(g);
        break;
    case GRAPH_STAR: /* immutable: an empty star would be useless */
    default:
        rc = GRAPH_ERR_ARG;
        break;
    }
    if (rc != GRAPH_OK)
    {
        LOG_ERROR("representation init failed (repr=%d, rc=%d)", repr, rc);
        free(g);
        return NULL;
    }
    LOG_DEBUG("Graph created (n=%d, directed=%d, repr=%d)", n, directed, repr);
    return g;
}

Graph* graph_from_edges(int n, bool directed, const GraphEdge* edges, size_t m, GraphRepr repr)
{
    if (n <= 0 || (m > 0 && !edges))
    {
        LOG_ERROR("invalid argument (n=%d, edges=%p, m=%zu)", n, (const void*)edges, m);
        return NULL;
    }
    for (size_t k = 0; k < m; k++)
    {
        const GraphEdge* e = &edges[k];
        if (e->u < 0 || e->u >= n || e->v < 0 || e->v >= n || e->w == 0.0)
        {
            LOG_ERROR("invalid edge at index %zu (u=%d, v=%d, w=%g)", k, e->u, e->v, e->w);
            return NULL;
        }
    }

    LOG_DEBUG("building Graph from %zu edges (n=%d, directed=%d, repr=%d)", m, n, directed, repr);

    if (repr == GRAPH_STAR)
    {
        Graph* g = shell(n, directed, repr);
        if (!g)
            return NULL;
        if (star_init_from_edges(g, edges, m) != GRAPH_OK)
        {
            LOG_ERROR("star_init_from_edges failed (n=%d, m=%zu)", n, m);
            free(g);
            return NULL;
        }
        return g;
    }

    Graph* g = graph_create(n, directed, repr);
    if (!g)
        return NULL;
    for (size_t k = 0; k < m; k++)
    {
        if (g->ops->add_edge(g, edges[k].u, edges[k].v, edges[k].w) != GRAPH_OK)
        {
            LOG_ERROR("add_edge failed at index %zu (u=%d, v=%d)", k, edges[k].u, edges[k].v);
            graph_free(g);
            return NULL;
        }
    }
    return g;
}

void graph_free(Graph* g)
{
    if (!g)
        return;
    LOG_DEBUG("freeing Graph (n=%d, m=%zu, repr=%d)", g->n, g->m, g->repr);
    if (g->ops)
        g->ops->destroy(g);
    free(g);
}

int graph_add_edge(Graph* g, int u, int v, double w)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v) || w == 0.0)
    {
        LOG_ERROR("invalid argument (u=%d, v=%d, w=%g)", u, v, w);
        return GRAPH_ERR_ARG;
    }
    return g->ops->add_edge(g, u, v, w);
}

int graph_remove_edge(Graph* g, int u, int v)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v))
    {
        LOG_ERROR("invalid argument (u=%d, v=%d)", u, v);
        return GRAPH_ERR_ARG;
    }
    return g->ops->remove_edge(g, u, v);
}

bool graph_has_edge(const Graph* g, int u, int v)
{
    return graph_get_weight(g, u, v) != 0.0;
}

double graph_get_weight(const Graph* g, int u, int v)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v))
        return 0.0;
    return g->ops->get_weight(g, u, v);
}

int graph_out_degree(const Graph* g, int u)
{
    if (!g || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    return g->ops->out_degree(g, u);
}

int graph_in_degree(const Graph* g, int u)
{
    if (!g || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    return g->ops->in_degree(g, u);
}

int graph_iter_out(const Graph* g, int u, GraphIter* it)
{
    if (!g || !it || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    g->ops->iter_out(g, u, it);
    return GRAPH_OK;
}

int graph_iter_in(const Graph* g, int u, GraphIter* it)
{
    if (!g || !it || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    g->ops->iter_in(g, u, it);
    return GRAPH_OK;
}

bool graph_iter_next(GraphIter* it, int* v, double* w)
{
    if (!it || !it->g)
        return false;
    return it->g->ops->iter_next(it, v, w);
}
