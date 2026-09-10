#include "../include/Sorting.h"
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

void QuickSortKruskalMST(GraphEdge* edges, int count, int p, int q)
{
    if (p >= q)
    {
        return;
    }

    int pivot_position = partition(edges, count, p, q);

    if (pivot_position > p)
    {
        QuickSortKruskalMST(edges, count, p, pivot_position);
    }

    // In this point we have piovt_position = p
    // printf("MST QICK FIRST EDGE:  [%i,%i].cost=%f \n",edges[p].u, edges[p].v, edges[p].w);

    if (pivot_position < q)
    {
        // If we reach this point, than  p < pivot_position < q
        QuickSortKruskalMST(edges, count, pivot_position + 1, q);
    }
}
