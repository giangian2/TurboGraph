#include "../include/Export.h"
#include "../include/Graph.h"
#include "../include/Importer.h"
#include "../include/Sorting.h"
#include "../include/Traversal.h"
#include <stdio.h>

/* I grafi di input stanno in res/, tutto cio' che viene generato in out/.
 * I percorsi sono relativi alla radice del progetto: esegui con `make run`. */
#define INPUT_DOT "res/BayAreaUs.dot"
#define OUTPUT_DOT "out/bayarea.dot"
#define EXAMPLE_NODE 7

int main(void)
{

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

    int rc = graph_export_dot(roads, rt, OUTPUT_DOT);
    if (rc != GRAPH_OK)
        fprintf(stderr, "export DOT failed (%d)\n", rc);
    else
        printf("Wrote " OUTPUT_DOT " -- render with:\n"
               "  dot -Tsvg " OUTPUT_DOT " -o your_output_name.svg\n");

    graph_free(roads);
    return 0;
}
