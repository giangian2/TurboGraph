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

/*
 * Connected components: strongly connected (Kosaraju) when g is directed,
 * plain connected components when it is undirected. O(n + m), on every
 * representation, without modifying g.
 *
 * Returns an array of *count components that partition 0 .. n-1; for a
 * directed graph they come out in topological order of the condensation.
 * The array and every node_ids it points to live in ONE block: release
 * it with a single free(). NULL (and *count = 0) on error.
 */
Component* graph_find_connected_components(const Graph* g, size_t* count);
void       traversal_cleanup(Traversal** t);

#define AUTO_FREE_TRAVERSAL __attribute__((cleanup(traversal_cleanup))) Traversal*

#endif /* TRAVERSAL_H */
