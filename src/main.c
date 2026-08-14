#include "../include/Graph.h"
#include "../include/Importer.h"
#include <stdio.h>

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
    /*
     * Grafo di esempio (non diretto, 9 vertici):
     * due rombi concatenati 0..5, una coda 5-6-7, e il vertice 8
     * isolato per far vedere come appare un nodo non raggiunto.
     */
    GraphEdge edges[] = {
        {0, 1, 1.0}, {0, 2, 2.0}, {1, 3, 1.5}, {2, 3, 1.0}, {2, 4, 3.0},
        {7, 8, 5.0}, {3, 5, 2.0}, {4, 5, 1.0}, {5, 6, 1.0}, {6, 7, 4.0},
    };

    // QuickSortKruskalMST(edges,10,0,9);

    QuickSortKruskalMST(edges, 10, 0, 9);

    printf("After Sort with Qucik kruskal: \n");

    for (size_t i = 0; i < 10; i++)
    {
        printf("MST QICK FIRST EDGE:  [%i,%i].cost=%f \n", edges[i].u, edges[i].v, edges[i].w);
    }

    /*
     * Stesso identico flusso (crea grafo -> BFS -> export), ma il grafo
     * stavolta arriva da file invece che da un array in codice: graph_import_dot()
     * ritorna un Graph* qualunque come graph_from_edges(), quindi tutto
     * il resto dell'API (BFS, export, ...) non cambia.
     */
    Graph* roads = graph_import_dot("twitch.dot", GRAPH_STAR);
    if (!roads)
    {
        fprintf(stderr, "import di twitch.dot fallito\n");
        return 1;
    }
    printf("\nimportato europe_roads.dot: %d vertici, %zu archi\n", roads->n, roads->m);

    Traversal* rt = graph_bfs(roads, 1);
    if (!rt)
    {
        fprintf(stderr, "BFS su europe_roads fallita\n");
        graph_free(roads);
        return 1;
    }
    printf("BFS da 1: raggiunti %d vertici su %d\n", rt->count, rt->n);

    int rc = graph_export_dot(roads, rt, "twitch_bfs.dot");
    if (rc != GRAPH_OK)
        fprintf(stderr, "export DOT fallito (%d)\n", rc);
    else
        printf("Scritto europe_bfs.dot -- renderizza con:\n"
               "  dot -Tpng twitch_bfs.dot -o twitch_bfs.png\n");

    traversal_free(rt);
    graph_free(roads);
    return 0;
}
