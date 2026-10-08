/*
 * Flat C API exported to JavaScript through Emscripten.
 *
 * The public library hands out structs (Traversal, Components, GraphIter)
 * that are awkward to read from JS, so every function here works on plain
 * arrays allocated by the caller (JS mallocs them on the wasm heap and
 * reads them back through HEAP32/HEAPF64). Nothing in this file knows
 * about node positions: layout and rendering live entirely on the JS side.
 *
 * Built natively too (without Emscripten) so the bindings can be tested
 * with gcc: EMSCRIPTEN_KEEPALIVE then expands to nothing.
 */
#include "../include/Graph.h"
#include "../include/Importer.h"
#include "../include/Traversal.h"
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

/* ---- construction / destruction ---------------------------------------- */

EMSCRIPTEN_KEEPALIVE Graph* wg_create(int n, int directed, int repr)
{
    return graph_create(n, directed != 0, (GraphRepr)repr);
}

/* uv holds m pairs (u0, v0, u1, v1, ...), w holds m weights. */
EMSCRIPTEN_KEEPALIVE Graph* wg_from_edges(int n, int directed, const int* uv, const double* w,
                                          int m, int repr)
{
    if (m < 0 || (m > 0 && (!uv || !w)))
        return NULL;

    GraphEdge* edges = malloc((size_t)(m ? m : 1) * sizeof *edges);
    if (!edges)
        return NULL;
    for (int k = 0; k < m; k++)
        edges[k] = (GraphEdge){uv[2 * k], uv[2 * k + 1], w[k]};

    Graph* g = graph_from_edges(n, directed != 0, edges, (size_t)m, (GraphRepr)repr);
    free(edges);
    return g;
}

/* JS writes the DOT text into the Emscripten virtual FS and passes the path. */
EMSCRIPTEN_KEEPALIVE Graph* wg_import_dot(const char* path, int repr)
{
    return graph_import_dot(path, (GraphRepr)repr);
}

EMSCRIPTEN_KEEPALIVE void wg_free(Graph* g)
{
    graph_free(g);
}

/* ---- accessors --------------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE int wg_n(const Graph* g)
{
    return g ? g->n : -1;
}

EMSCRIPTEN_KEEPALIVE int wg_m(const Graph* g)
{
    return g ? (int)g->m : -1;
}

EMSCRIPTEN_KEEPALIVE int wg_directed(const Graph* g)
{
    return g ? g->directed : -1;
}

EMSCRIPTEN_KEEPALIVE int wg_repr(const Graph* g)
{
    return g ? (int)g->repr : -1;
}

/* ---- edges ------------------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE int wg_add_edge(Graph* g, int u, int v, double w)
{
    return g ? graph_add_edge(g, u, v, w) : GRAPH_ERR_ARG;
}

EMSCRIPTEN_KEEPALIVE int wg_remove_edge(Graph* g, int u, int v)
{
    return g ? graph_remove_edge(g, u, v) : GRAPH_ERR_ARG;
}

EMSCRIPTEN_KEEPALIVE int wg_has_edge(const Graph* g, int u, int v)
{
    return g ? graph_has_edge(g, u, v) : 0;
}

EMSCRIPTEN_KEEPALIVE double wg_get_weight(const Graph* g, int u, int v)
{
    return g ? graph_get_weight(g, u, v) : 0.0;
}

/*
 * Writes up to cap edges as pairs into uv (2 * cap ints) and their weights
 * into w (cap doubles). An undirected edge is seen from both endpoints by
 * the iterator: it is emitted once, from the smaller index, as in export.c.
 * Returns the number of edges written, or GRAPH_ERR_ARG.
 */
EMSCRIPTEN_KEEPALIVE int wg_edges(const Graph* g, int* uv, double* w, int cap)
{
    if (!g || !uv || !w || cap < 0)
        return GRAPH_ERR_ARG;

    int k = 0;
    for (int u = 0; u < g->n && k < cap; u++)
    {
        GraphIter it;
        graph_iter_out(g, u, &it);
        int    v;
        double wt;
        while (k < cap && graph_iter_next(&it, &v, &wt))
        {
            if (!g->directed && v < u)
                continue;
            uv[2 * k]     = u;
            uv[2 * k + 1] = v;
            w[k]          = wt;
            k++;
        }
    }
    return k;
}

/*
 * Edges as a renderer wants them: like wg_edges, but in a directed graph
 * the two arcs u->v and v->u collapse into one entry, emitted from the
 * smaller index with bidir = 1 (road networks list every street both
 * ways: this halves what has to be drawn). bidir holds cap bytes.
 * Returns the number of entries written, or GRAPH_ERR_ARG.
 */
EMSCRIPTEN_KEEPALIVE int wg_draw_edges(const Graph* g, int* uv, unsigned char* bidir, int cap)
{
    if (!g || !uv || !bidir || cap < 0)
        return GRAPH_ERR_ARG;

    int k = 0;
    for (int u = 0; u < g->n && k < cap; u++)
    {
        GraphIter it;
        graph_iter_out(g, u, &it);
        int v;
        while (k < cap && graph_iter_next(&it, &v, NULL))
        {
            bool back = false;
            if (g->directed && u != v)
                back = graph_has_edge(g, v, u);
            if (v < u && (!g->directed || back))
                continue; /* emitted from v's side */
            uv[2 * k]     = u;
            uv[2 * k + 1] = v;
            bidir[k]      = back;
            k++;
        }
    }
    return k;
}

/* ---- algorithms -------------------------------------------------------- */

/*
 * Runs BFS (bfs != 0) or DFS from source and copies the result into
 * caller-provided arrays of n ints each: order[0..count) is the visit
 * order, parent[] and dist[] are -1 for unreached vertices.
 * Returns count, or GRAPH_ERR_ARG / GRAPH_ERR_ALLOC.
 */
static int traverse(const Graph* g, int source, int bfs, int* order, int* parent, int* dist)
{
    if (!g || !order || !parent || !dist || !vertex_ok(g, source))
        return GRAPH_ERR_ARG;

    AUTO_FREE_TRAVERSAL t = bfs ? graph_bfs(g, source) : graph_dfs(g, source);
    if (!t)
        return GRAPH_ERR_ALLOC;

    for (int i = 0; i < t->count; i++)
        order[i] = t->order[i];
    for (int v = 0; v < t->n; v++)
    {
        parent[v] = t->parent[v];
        dist[v]   = t->dist[v];
    }
    return t->count;
}

EMSCRIPTEN_KEEPALIVE int wg_bfs(const Graph* g, int source, int* order, int* parent, int* dist)
{
    return traverse(g, source, 1, order, parent, dist);
}

EMSCRIPTEN_KEEPALIVE int wg_dfs(const Graph* g, int source, int* order, int* parent, int* dist)
{
    return traverse(g, source, 0, order, parent, dist);
}

/*
 * Connected components (strongly connected if g is directed). comp[v] gets
 * the index of v's component. Returns the number of components, or
 * GRAPH_ERR_ARG / GRAPH_ERR_ALLOC.
 */
EMSCRIPTEN_KEEPALIVE int wg_components(const Graph* g, int* comp)
{
    if (!g || !comp)
        return GRAPH_ERR_ARG;

    AUTO_FREE_COMPONENTS cc = graph_find_connected_components(g);
    if (!cc)
        return GRAPH_ERR_ALLOC;

    for (size_t c = 0; c < cc->count; c++)
        for (int i = cc->start[c]; i < cc->start[c + 1]; i++)
            comp[cc->ids[i]] = (int)c;
    return (int)cc->count;
}

/*
 * Kruskal minimum spanning forest of an undirected graph. Writes the forest
 * edges, in the order Kruskal picks them (by increasing weight), as pairs
 * into uv (2 * cap ints) and their weights into w (cap doubles); n - 1
 * entries are always enough. Returns the number of edges written, or
 * GRAPH_ERR_ARG (directed graph, cap too small) / GRAPH_ERR_ALLOC.
 */
EMSCRIPTEN_KEEPALIVE int wg_mst_kruskal(const Graph* g, int* uv, double* w, int cap)
{
    if (!g || !uv || !w || g->directed || cap < g->n - 1)
        return GRAPH_ERR_ARG;

    GraphEdge* edges = malloc((g->m ? g->m : 1) * sizeof *edges);
    if (!edges)
        return GRAPH_ERR_ALLOC;

    /* Every edge once, from the smaller index, as in wg_edges: self loops
     * included, so exactly g->m entries (uf_union then rejects them) */
    size_t m = 0;
    for (int u = 0; u < g->n; u++)
    {
        GraphIter it;
        graph_iter_out(g, u, &it);
        int    v;
        double wt;
        while (graph_iter_next(&it, &v, &wt))
            if (u <= v)
                edges[m++] = (GraphEdge){u, v, wt};
    }

    int        size = 0;
    GraphEdge* mst  = graph_mst_kruskal(g, edges, &size);
    free(edges);
    if (!mst)
        return GRAPH_ERR_ALLOC;

    for (int k = 0; k < size; k++)
    {
        uv[2 * k]     = mst[k].u;
        uv[2 * k + 1] = mst[k].v;
        w[k]          = mst[k].w;
    }
    free(mst);
    return size;
}
