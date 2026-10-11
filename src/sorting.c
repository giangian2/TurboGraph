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

/* Sorts only the first k slots of edges[p..q]: on return edges[p..k-1]
 * hold the k smallest weights in order, the tail is left unsorted. k is an
 * ABSOLUTE index, as p and q are. */
void graph_edges_partial_quick_sort(GraphEdge* edges, int count, int p, int q, int k)
{
    // we keep a stack of pending ranges: the right half is pushed before the left one, so ranges
    // are popped left to right
    quick_sort_frame* stack = stack_create(quick_sort_frame, count);

    if (!stack)
        return;

    quick_sort_frame frame = {.p = p, .q = q};

    stack_push(stack, frame);

    while (!stack_is_empty(stack))
    {
        frame = stack_pop(stack);

        // STOP CONDITION: ranges come out left to right, so every pending one lies past k too
        if (frame.p >= k)
            break;

        // BASE
        if (frame.p >= frame.q)
            continue;

        int pivot_position = graph_edges_partition(edges, count, frame.p, frame.q);

        if (pivot_position < 0)
            break;

        // IF NEEDED FIRST WE PUSH THE RIGHT SIDE, SO THE LEFT REMAINS AT TOP OF STACK
        if (pivot_position + 1 < k)
        {
            quick_sort_frame right = {.p = pivot_position + 1, .q = frame.q};
            stack_push(stack, right);
        }

        quick_sort_frame left = {.p = frame.p, .q = pivot_position};
        stack_push(stack, left);
    }

    stack_free(stack);
}

bool graph_edges_quick_sort_iter_init(quick_sort_iter* it, GraphEdge* edges, int count)
{
    if (it == NULL || edges == NULL || count <= 0)
    {
        LOG_ERROR("invalid iterator arguments (count=%d)", count);
        return false;
    }

    /* Pending ranges are pairwise disjoint and non-empty, so at most count
     * of them can be on the stack at the same time. */
    it->edges = edges;
    it->count = count;
    it->stack = stack_create(quick_sort_frame, count);

    if (!it->stack)
    {
        LOG_ERROR("stack allocation failed (count=%d)", count);
        return false;
    }

    quick_sort_frame frame = {.p = 0, .q = count - 1};
    stack_push(it->stack, frame);

    return true;
}

/* Lazy quicksort: each call does only the partitions needed to settle the
 * next slot and returns it. Ranges are popped left to right, so the first
 * single-element range found is the smallest edge not yet returned, and it
 * is already in its final slot (later partitions only touch ranges to its
 * right): the returned pointer stays valid. NULL once every edge has been
 * returned. */
GraphEdge* graph_edges_quick_sort_step(quick_sort_iter* it)
{
    if (it == NULL || it->stack == NULL)
        return NULL;

    while (!stack_is_empty(it->stack))
    {
        quick_sort_frame frame = stack_pop(it->stack);

        // EMPTY RANGE: never produced by Hoare halves, only guards the initial frame
        if (frame.p > frame.q)
            continue;

        // BASE: a single element is in its final slot
        if (frame.p == frame.q)
            return &it->edges[frame.p];

        int pivot_position = graph_edges_partition(it->edges, it->count, frame.p, frame.q);

        if (pivot_position < 0)
            return NULL;

        /**
         * SINCE WE ARE USING A STACK, WE NEED TO PUSH FIRST THE RIGHT PART
         * AND THEN THE LEFT PART, IN THIS WAY LEFT-PARTITIONS ARE EXECUTED
         * FIRST
         */
        quick_sort_frame right = {.p = pivot_position + 1, .q = frame.q};
        stack_push(it->stack, right);

        quick_sort_frame left = {.p = frame.p, .q = pivot_position};
        stack_push(it->stack, left);
    }

    return NULL;
}

void graph_edges_quick_sort_iter_cleanup(quick_sort_iter* it)
{
    if (it != NULL && it->stack != NULL)
    {
        stack_free(it->stack);
        it->stack = NULL;
        LOG_DEBUG("quick sort iterator freed");
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
