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

/* Sorts edges[p..q] by weight in place (Hoare quicksort, src/sorting.c) */
int  graph_edges_partition(GraphEdge* edges, int count, int p, int q);
void graph_edges_quick_sort(GraphEdge* edges, int count, int p, int q);

#endif /* GRAPH_SORTING_H */
