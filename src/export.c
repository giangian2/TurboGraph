#include "../include/Export.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include <stdio.h>

/*
 * u->v (or u--v) is a tree edge of the traversal if v was discovered by
 * going through u, i.e. if parent[v] == u. In an undirected graph the
 * edge is emitted only once, so it must be checked in both directions.
 */
static bool is_tree_edge(const Graph* g, const Traversal* t, int u, int v)
{
    if (!t)
        return false;
    return t->parent[v] == u || (!g->directed && t->parent[u] == v);
}

int graph_export_dot(const Graph* g, const Traversal* t, const char* path)
{
    if (!g || !path || (t && t->n != g->n))
    {
        LOG_ERROR("invalid argument (g=%p, path=%p, t=%p)", (const void*)g, (const void*)path,
                  (const void*)t);
        return GRAPH_ERR_ARG;
    }

    FILE* f = fopen(path, "w");
    if (!f)
    {
        LOG_ERROR("fopen failed for %s", path);
        return GRAPH_ERR_ARG;
    }

    LOG_DEBUG("exporting Graph to %s (n=%d, m=%zu, with_traversal=%d)", path, g->n, g->m,
              t != NULL);

    /* DOT distinguishes directed graphs (digraph, "->" edges) from undirected ones (graph, "--") */
    const char* edge_op = g->directed ? "->" : "--";
    fprintf(f, "%s G {\n", g->directed ? "digraph" : "graph");
    fprintf(f, "  rankdir=LR;\n");
    fprintf(f, "  node [shape=circle, style=filled, fillcolor=white];\n\n");

    /* Nodes. With a Traversal: source in orange, reached vertices in light
     * blue with the distance as a label, unreached ones left white and unlabeled. */
    for (int v = 0; v < g->n; v++)
    {
        if (t && t->dist[v] >= 0)
            fprintf(f, "  %d [label=\"%d\\nd=%d\", fillcolor=%s];\n", v, v, t->dist[v],
                    t->dist[v] == 0 ? "orange" : "lightblue");
        else
            fprintf(f, "  %d;\n", v);
    }
    fprintf(f, "\n");

    /* Edges, via the iterator: independent of the representation. In an
     * undirected graph each edge is seen twice (from both endpoints):
     * we emit it only from the side with the smaller index. */
    for (int u = 0; u < g->n; u++)
    {
        GraphIter it;
        g->ops->iter_out(g, u, &it);
        int    v;
        double w;
        while (g->ops->iter_next(&it, &v, &w))
        {
            if (!g->directed && v < u)
                continue;
            if (is_tree_edge(g, t, u, v))
                fprintf(f, "  %d %s %d [label=\"%g\", color=red, penwidth=2.0];\n", u, edge_op, v,
                        w);
            else
                fprintf(f, "  %d %s %d [label=\"%g\", color=gray50];\n", u, edge_op, v, w);
        }
    }

    fprintf(f, "}\n");
    int rc = fclose(f) == 0 ? GRAPH_OK : GRAPH_ERR_ARG;
    if (rc != GRAPH_OK)
        LOG_ERROR("fclose failed for %s", path);
    return rc;
}
