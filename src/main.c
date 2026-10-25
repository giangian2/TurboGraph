#include "../include/Export.h"
#include "../include/Graph.h"
#include "../include/Importer.h"
#include "../include/Sorting.h"
#include "../include/Traversal.h"
#include "../include/Generator.h"
#include <stdio.h>
#include <stdlib.h>

/* I grafi di input stanno in res/, tutto cio' che viene generato in out/.
 * I percorsi sono relativi alla radice del progetto: esegui con `make run`. */
#define INPUT_DOT "res/BayAreaUs.dot"
#define INPUT_DOT_UNDIRECTED "res/BayAreaUs_undirected.dot"
#define OUTPUT_DOT "out/bayarea.dot"
#define EXAMPLE_NODE 7

/* Edge list of the graph, each edge once: every out-arc when directed,
 * only u < v when undirected (both adjacency lists hold the edge), as
 * graph_mst_kruskal wants it. NULL if the allocation fails. */
static GraphEdge* collect_edges(const Graph* g, size_t* count)
{
    GraphEdge* edges = malloc(sizeof(GraphEdge) * (g->m > 0 ? g->m : 1));
    if (!edges)
        return NULL;

    *count = 0;
    for (int u = 0; u < g->n; u++)
    {
        GraphIter it;
        int       v;
        double    w;
        graph_iter_out(g, u, &it);
        while (graph_iter_next(&it, &v, &w))
            if (g->directed || u < v)
                edges[(*count)++] = (GraphEdge){u, v, w};
    }
    return edges;
}

static double total_weight(const GraphEdge* edges, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; i++)
        sum += edges[i].w;
    return sum;
}

static int test_kruskal_random(void)
{

    Graph* g = generate_random_graph(0.05, 10000, true, false, 126, GRAPH_STAR);
    if (!g)
        return 1;

    size_t     m;
    GraphEdge* edges = collect_edges(g, &m);
    int        size  = 0;
    GraphEdge* mst   = edges ? graph_mst_quick_kruskal(g, edges, &size) : NULL;

    double weight = mst ? total_weight(mst, size) : -1.0;
    printf("Kruskal test (CLRS 23.4): %d edges, weight %.0f\n", size, weight);

    free(mst);
    free(edges);
    graph_free(g);
    return 0;
}

int main(void)
{
    if (test_kruskal_random() != 0)
        return 1;


    Graph* roads = graph_import_dot(INPUT_DOT, GRAPH_STAR);
    if (!roads)
    {
        fprintf(stderr, "import di " INPUT_DOT " fallito\n");
        return 1;
    }
    printf("\nImported " INPUT_DOT ": %d vertex, %zu arcs\n", roads->n, roads->m);

    AUTO_FREE_TRAVERSAL rt = graph_dfs(roads, EXAMPLE_NODE);
    if (!rt)
    {
        fprintf(stderr, "Node Search on " INPUT_DOT " failed\n");
        graph_free(roads);
        return 1;
    }
    printf("Node Serch from %d: reached %d vertex of %d\n", EXAMPLE_NODE, rt->count, rt->n);

    AUTO_FREE_COMPONENTS cc = graph_find_connected_components(roads);
    if (!cc)
    {
        fprintf(stderr, "Connected components on " INPUT_DOT " failed\n");
        graph_free(roads);
        return 1;
    }
    int largest = 0;
    for (size_t c = 0; c < cc->count; c++)
        if (components_size(cc, c) > largest)
            largest = components_size(cc, c);
    printf("%s components: %zu (largest: %d vertices)\n",
           roads->directed ? "Strongly connected" : "Connected", cc->count, largest);

    /* Kruskal sulla versione non orientata, pesata con le label:
     * la foresta ricoprente ha n - #componenti archi */
    Graph* roads_und = graph_import_dot(INPUT_DOT_UNDIRECTED, GRAPH_STAR);
    AUTO_FREE_COMPONENTS cc_und = roads_und ? graph_find_connected_components(roads_und) : NULL;
    if (!cc_und)
        fprintf(stderr, "import di " INPUT_DOT_UNDIRECTED " fallito\n");
    else
    {
        size_t     m;
        GraphEdge* edges = collect_edges(roads_und, &m);
        int        size  = 0;
        GraphEdge* mst   = edges ? graph_mst_kruskal(roads_und, edges, &size) : NULL;
        if (!mst)
            fprintf(stderr, "Kruskal on " INPUT_DOT_UNDIRECTED " failed\n");
        else
            printf("Kruskal MST on " INPUT_DOT_UNDIRECTED ": %d edges (expected %d), "
                   "total weight %.0f\n",
                   size, roads_und->n - (int)cc_und->count, total_weight(mst, size));
        free(mst);
        free(edges);
    }
    graph_free(roads_und);

    int rc = graph_export_dot(roads, rt, OUTPUT_DOT);
    if (rc != GRAPH_OK)
        fprintf(stderr, "export DOT failed (%d)\n", rc);
    else
        printf("Wrote " OUTPUT_DOT " -- render with:\n"
               "  dot -Tsvg " OUTPUT_DOT " -o your_output_name.svg\n");

    graph_free(roads);
    return 0;
}
