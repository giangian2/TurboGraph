#include "../include/Sorting.h"
#include "../include/Debug.h"
#include "../include/Graph.h"
#include "../include/Stack.h"
#include <stdbool.h>

/* Hoare partition (CLRS): the pivot value edges[p].w is saved up front,
 * since the swaps can move the element sitting at p. The returned index is
 * a SPLIT POINT, not the pivot's final slot: edges[p..j] are all <= pivot
 * and edges[j+1..q] are all >= pivot, and j < q always holds, so both
 * halves of the recursion are strictly smaller than [p, q]. */
int graph_edges_partition(GraphEdge* edges, int count, int p, int q)
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

void graph_edges_quick_sort(GraphEdge* edges, int count, int p, int q)
{
    quick_sort_frame* stack = stack_create(quick_sort_frame, count);

    if (!stack)
        return;

    quick_sort_frame frame = {.p = p, .q = q};

    stack_push(stack, frame);

    while (!stack_is_empty(stack))
    {
        frame = stack_pop(stack);
        // BASE
        if (frame.p >= frame.q)
            continue;

        int pivot_position = graph_edges_partition(edges, count, frame.p, frame.q);

        if (pivot_position < 0)
            break;

        /**
         * SINCE WE ARE USING A STACK, WE NEED TO PUSH FIRST THE RIGHT PART
         * AND THEN THE LEFT PART, IN THIS WAY LEFT-PARTITIONS ARE EXECUTED
         * FIRST
         */
        quick_sort_frame right = {.p = pivot_position + 1, .q = frame.q};
        stack_push(stack, right);

        quick_sort_frame left = {.p = frame.p, .q = pivot_position};
        stack_push(stack, left);
    }

    stack_free(stack);
}
