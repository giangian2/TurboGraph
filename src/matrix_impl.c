/*
 * Adjacency matrix representation.
 *
 * g->data is one flat block of doubles, cell (u, v) holds the weight of
 * the edge u->v, MATRIX_NULL_VALUE when absent.
 *
 *   directed    full n*n matrix, row-major: (u, v) -> u*n + v
 *   undirected  the matrix is symmetric, so only the lower triangle
 *               (diagonal included) is stored: n*(n+1)/2 cells, with
 *               (u, v), u >= v -> u*(u+1)/2 + v. (u, v) and (v, u) map
 *               to the same cell, so every edge is written exactly once.
 */
#include "../include/Debug.h"
#include "../include/Graph.h"
#include <stdlib.h>

/* Must stay 0.0: calloc relies on it, this project treats 0.0 as "no edge" for simplicity. */
#define MATRIX_NULL_VALUE 0.0

static inline size_t matrix_cells(const Graph* g)
{
    size_t n = (size_t)g->n;
    return g->directed ? n * n : n * (n + 1) / 2;
}

static inline size_t matrix_index(const Graph* g, int u, int v)
{
    if (g->directed)
        return (size_t)u * (size_t)g->n + (size_t)v;
    if (u < v)
    {
        int t = u;
        u     = v;
        v     = t;
    }
    return (size_t)u * (size_t)(u + 1) / 2 + (size_t)v;
}

static inline double matrix_get_elem_at(const Graph* g, int u, int v)
{
    const double* matrix = (const double*)g->data;
    return matrix[matrix_index(g, u, v)];
}

static inline void matrix_set_elem_at(Graph* g, int u, int v, double w)
{
    double* matrix                = (double*)g->data;
    matrix[matrix_index(g, u, v)] = w;
}

static int adjmatrix_add_edge(Graph* g, int u, int v, double w)
{
    if (matrix_get_elem_at(g, u, v) == MATRIX_NULL_VALUE)
        g->m++;
    matrix_set_elem_at(g, u, v, w); /* existing edge: weight update */
    return GRAPH_OK;
}

static int adjmatrix_remove_edge(Graph* g, int u, int v)
{
    if (matrix_get_elem_at(g, u, v) == MATRIX_NULL_VALUE)
        return GRAPH_OK; /* absent edge: nothing to do */
    matrix_set_elem_at(g, u, v, MATRIX_NULL_VALUE);
    g->m--;
    return GRAPH_OK;
}

static double adjmatrix_get_weight(const Graph* g, int u, int v)
{
    return matrix_get_elem_at(g, u, v);
}

static int adjmatrix_out_degree(const Graph* g, int u)
{
    int d = 0;
    for (int v = 0; v < g->n; v++)
        if (matrix_get_elem_at(g, u, v) != MATRIX_NULL_VALUE)
            d++;
    return d;
}

static int adjmatrix_in_degree(const Graph* g, int u)
{
    if (!g->directed)
        return adjmatrix_out_degree(g, u);
    int d = 0;
    for (int s = 0; s < g->n; s++)
        if (matrix_get_elem_at(g, s, u) != MATRIX_NULL_VALUE)
            d++;
    return d;
}

static void adjmatrix_iter_out(const Graph* g, int u, GraphIter* it)
{
    it->g    = g;
    it->u    = u;
    it->i    = 0;
    it->node = NULL;
    it->in   = false;
}

static void adjmatrix_iter_in(const Graph* g, int u, GraphIter* it)
{
    it->g    = g;
    it->u    = u;
    it->i    = 0;
    it->node = NULL;
    it->in   = true;
}

static bool adjmatrix_iter_next(GraphIter* it, int* v, double* w)
{
    const Graph* g = it->g;
    /* undirected: in == out, matrix_index already folds onto the triangle */
    bool by_column = it->in && g->directed;
    while (it->i < g->n)
    {
        int    j = it->i++;
        double x = by_column ? matrix_get_elem_at(g, j, it->u) : matrix_get_elem_at(g, it->u, j);
        if (x != MATRIX_NULL_VALUE)
        {
            if (v)
                *v = j;
            if (w)
                *w = x;
            return true;
        }
    }
    return false;
}

static void adjmatrix_destroy(Graph* g)
{
    free(g->data);
}

static const GraphOps adjmatrix_ops = {
    .add_edge    = adjmatrix_add_edge,
    .remove_edge = adjmatrix_remove_edge,
    .get_weight  = adjmatrix_get_weight,
    .out_degree  = adjmatrix_out_degree,
    .in_degree   = adjmatrix_in_degree,
    .iter_out    = adjmatrix_iter_out,
    .iter_in     = adjmatrix_iter_in,
    .iter_next   = adjmatrix_iter_next,
    .destroy     = adjmatrix_destroy,
};

int matrix_init(Graph* g)
{
    g->data = calloc(matrix_cells(g), sizeof(double));
    if (!g->data)
    {
        LOG_ERROR("allocation failed for matrix (n=%d, cells=%zu)", g->n, matrix_cells(g));
        return GRAPH_ERR_ALLOC;
    }
    g->ops = &adjmatrix_ops;
    LOG_DEBUG("matrix initialized (n=%d, directed=%d, cells=%zu)", g->n, g->directed,
              matrix_cells(g));
    return GRAPH_OK;
}
