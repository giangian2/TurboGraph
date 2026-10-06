#include "../include/Traversal.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include <stdbool.h>

/* Hoare partition (CLRS): the pivot value edges[p].w is saved up front,
 * since the swaps can move the element sitting at p. The returned index is
 * a SPLIT POINT, not the pivot's final slot: edges[p..j] are all <= pivot
 * and edges[j+1..q] are all >= pivot, and j < q always holds, so both
 * halves of the recursion are strictly smaller than [p, q]. */
int partition(GraphEdge* edges, int count, int p, int q)
{
    // Handle edge cases
    if (p < 0 || q >= count)
    {
        LOG_ERROR("invalid range (p=%d, q=%d, count=%d)", p, q, count);
        return -1;
    }

    double pivot   = edges[p].w;
    int    e_minus = p - 1;
    int    e_plus  = q + 1;

    while (true)
    {
        do
        {
            e_plus--;
        } while (edges[e_plus].w > pivot);

        do
        {
            e_minus++;
        } while (edges[e_minus].w < pivot);

        if (e_minus >= e_plus)
        {
            return e_plus;
        }

        // SWAP
        GraphEdge tmp  = edges[e_minus];
        edges[e_minus] = edges[e_plus];
        edges[e_plus]  = tmp;
    }
}

void QuickSort(GraphEdge* edges, int count, int p, int q)
{
    // BASE
    if (p >= q)
    {
        return;
    }

    LOG_DEBUG("sorting range [%d, %d]", p, q);

    int pivot_position = partition(edges, count, p, q);

    /* Because partition() uses the Hoare scheme, pivot_position is only a
     * split point, not the pivot's final index: the element at
     * pivot_position is NOT guaranteed to be already sorted, so it must be
     * included in the left recursive call (range [p, pivot_position]).
     * Using [p, pivot_position-1] here would silently skip that element
     * from both halves and leave the array unsorted. */
    QuickSort(edges, count, p, pivot_position);

    QuickSort(edges, count, pivot_position + 1, q);
}
