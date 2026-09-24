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

/*
 * Result of a connected components search: a partition of 0 .. n-1,
 * laid out like a CSR. Component c is the slice
 *
 *   ids[start[c] .. start[c+1])
 *
 * so it has start[c+1] - start[c] vertices, and start[count] == n.
 *
 *   count   number of components
 *   n       number of vertices = length of ids
 *   start   count + 1 offsets into ids
 *   ids     every vertex exactly once, grouped by component
 *
 * One block holds the header and both arrays: release it with
 * components_cleanup() (or AUTO_FREE_COMPONENTS).
 */
typedef struct
{
    size_t count;
    int    n;
    int*   start;
    int*   ids;
} Components;

/* Number of vertices in component c. */
static inline int components_size(const Components* cc, size_t c)
{
    return cc->start[c + 1] - cc->start[c];
}

Traversal* graph_bfs(const Graph* g, int source);
Traversal* graph_dfs(const Graph* g, int source);

/*
 * Connected components: strongly connected (Kosaraju) when g is directed,
 * plain connected components when it is undirected. O(n + m), on every
 * representation, without modifying g. For a directed graph the
 * components come out in topological order of the condensation.
 * NULL on error.
 */
Components* graph_find_connected_components(const Graph* g);
void        components_cleanup(Components** cc);
void        traversal_cleanup(Traversal** t);

#define AUTO_FREE_TRAVERSAL __attribute__((cleanup(traversal_cleanup))) Traversal*
#define AUTO_FREE_COMPONENTS __attribute__((cleanup(components_cleanup))) Components*

#endif /* TRAVERSAL_H */
