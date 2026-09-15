#ifndef TRAVERSAL_H
#define TRAVERSAL_H

#include "../include/Graph.h"
#include <stddef.h>

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
    int  count;
    int  n;
} Traversal;

/**
 * Result of a connected components finding algorithm.
 *
 *   node_ids   an array of all the nodes id present in the component
 *   size       size (number of nodes) of the component
 */
typedef struct
{
    int*   node_ids;
    size_t size;
} Component;

Traversal* graph_bfs(const Graph* g, int source);
Traversal* graph_dfs(const Graph* g, int source);
void       traversal_cleanup(Traversal** t);

#define AUTO_FREE_TRAVERSAL __attribute__((cleanup(traversal_cleanup))) Traversal*

#endif /* TRAVERSAL_H */
