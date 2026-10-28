#ifndef GRAPH_SORTING_H
#define GRAPH_SORTING_H

#include "../include/Graph.h"
#include <stdbool.h>

/* ---- ordering utils --------------------------------------------- */

typedef struct
{
    int p;
    int q;
} quick_sort_frame;

/* State of a lazy quicksort over edges[0..count-1]: the stack holds the
 * ranges still to be partitioned (see graph_edges_quick_sort_step). */
typedef struct
{
    GraphEdge*        edges;
    int               count;
    quick_sort_frame* stack;
} quick_sort_iter;

typedef enum
{
    PROCEED = 0,
    STOP    = -1
} quick_sort_step;

/* Sorts edges[p..q] by weight in place (Hoare quicksort, src/sorting.c) */
int  graph_edges_partition(GraphEdge* edges, int count, int p, int q);
void graph_edges_quick_sort(GraphEdge* edges, int count, int p, int q);
void graph_edges_partial_quick_sort(GraphEdge* edges, int count, int p, int q, int k);


/* Lazy sort: init, then each step returns the next edge by weight (NULL when
 * done), cleanup frees the stack. */
bool       graph_edges_quick_sort_iter_init(quick_sort_iter* it, GraphEdge* edges, int count);
GraphEdge* graph_edges_quick_sort_step(quick_sort_iter* it);

void       graph_edges_quick_sort_iter_cleanup(quick_sort_iter* it);

#endif /* GRAPH_SORTING_H */
