#ifndef GRAPH_SORTING_H
#define GRAPH_SORTING_H

#include "../include/Graph.h"

int  partition(GraphEdge* edges, int count, int p, int q);
void QuickSort(GraphEdge* edges, int count, int p, int q);
void QuickSortKruskalMST(GraphEdge* edges, int count, int p, int q);

#endif /* GRAPH_SORTING_H */
