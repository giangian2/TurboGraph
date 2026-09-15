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

/* Stampa il cammino minimo source -> v risalendo parent[] a ritroso.
 * Ricorsiva: prima stampa il cammino fino al padre, poi v stesso. */
static void print_path(const Traversal* t, int v)
{
    if (t->parent[v] != -1)
    {
        print_path(t, t->parent[v]);
        printf(" -> ");
    }
    printf("%d", v);
}

int main(void)
{

    Graph* roads = graph_import_dot(INPUT_DOT, GRAPH_STAR);
    if (!roads)
    {
        fprintf(stderr, "import di " INPUT_DOT " fallito\n");
        return 1;
    }
    printf("\nimportato " INPUT_DOT ": %d vertici, %zu archi\n", roads->n, roads->m);

    Traversal* rt = graph_dfs(roads, 7);
    if (!rt)
    {
        fprintf(stderr, "BFS su " INPUT_DOT " fallita\n");
        graph_free(roads);
        return 1;
    }
    printf("BFS da 7: raggiunti %d vertici su %d\n", rt->count, rt->n);

    if (rt->count > 1)
    {
        int far = rt->order[rt->count - 1];
        printf("cammino minimo 7 -> %d (%d archi): ", far, rt->dist[far]);
        print_path(rt, far);
        printf("\n");
    }

    int rc = graph_export_dot(roads, rt, OUTPUT_DOT);
    if (rc != GRAPH_OK)
        fprintf(stderr, "export DOT fallito (%d)\n", rc);
    else
        printf("Scritto " OUTPUT_DOT " -- renderizza con:\n"
               "  dot -Tpng " OUTPUT_DOT " -o out/twitch_bfs.png\n");

    traversal_free(rt);
    graph_free(roads);
    return 0;
}
