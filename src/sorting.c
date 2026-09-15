#include "../include/Sorting.h"
#include "../include/Debug.h"
#include "../include/Graph.h"

/* Hoare-scheme partition (NOT Lomuto): the pivot value is edges[p].w and is
 * never swapped out before scanning starts. e_minus/e_plus close in from
 * both ends until they cross; the returned index is a SPLIT POINT, not the
 * pivot's final sorted slot. edges[p..return] are all <= pivot and
 * edges[return+1..q] are all >= pivot, but the element sitting at the
 * returned index has no guarantee of already being in its final position
 * and must still be included in further sorting (see QuickSort below).
 * Safety note: the original pivot value always remains somewhere inside
 * [p,q] and acts as a sentinel that halts both inner while-loops, so
 * e_minus/e_plus can never scan past the p/q bounds. */
int partition(GraphEdge* edges, int count, int p, int q)
{
    // Handle edge cases
    if (p < 0 || q >= count)
    {
        LOG_ERROR("invalid range (p=%d, q=%d, count=%d)", p, q, count);
        return -1;
    }

    int e_minus = p;
    int e_plus  = q;

    while (e_minus < e_plus)
    {
        while (edges[e_plus].w > edges[p].w)
        {
            e_plus--;
        }

        while (edges[e_minus].w < edges[p].w)
        {
            e_minus++;
        }

        if (e_minus < e_plus)
        {
            // SWAP
            GraphEdge tmp  = edges[e_minus];
            edges[e_minus] = edges[e_plus];
            edges[e_plus]  = tmp;

            e_minus++;
            e_plus--;
        }
    }

    return e_plus;
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

/*
 * Same algorithm as QuickSort() above -- same partition(), same split
 * point, same recursion -- kept under its own name because Kruskal's MST
 * callers sort their edge list through it; there is nothing MST-specific
 * about the sort itself, so it is a thin alias rather than a second copy
 * of the recursion to keep in sync.
 */
void QuickSortKruskalMST(GraphEdge* edges, int count, int p, int q)
{
    QuickSort(edges, count, p, q);
}
