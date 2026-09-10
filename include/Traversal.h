#ifndef TRAVERSAL_H
#define TRAVERSAL_H

#include "../include/Graph.h"

/* ---- traversals -------------------------------------------------------- */

/*
 * Result of a BFS or DFS from a source vertex. Arrays have length n and
 * are indexed by vertex; unreached vertices have parent == -1, dist == -1.
 *
 *   order   the `count` reached vertices, in visit order
 *   parent  traversal-tree parent (-1 for the source and unreached)
 *   dist    BFS: edge distance from the source
 *           DFS: depth in the DFS tree
 */
typedef struct
{
    int* order;
    int* parent;
    int* dist;
    int  count; // numero totale di vertici raggiunti
    int  n;     // numero totale di vertific del grafo
} Traversal;

Traversal* graph_bfs(const Graph* g, int source);
Traversal* graph_dfs(const Graph* g, int source);
void       traversal_free(Traversal* t);

#endif /* TRAVERSAL_H */
