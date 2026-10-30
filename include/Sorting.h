#ifndef GRAPH_SORTING_H
#define GRAPH_SORTING_H

#include "../include/Debug.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Single-header generic quicksort (Hoare, CLRS) on any array whose elements
 * carry a double field named w, the sort key: GraphEdge, or any lighter
 * record an algorithm builds for itself. Sorting is in place, so an element
 * carries its own identity (endpoints, index, ...) along with its weight.
 *
 * The macros pass sizeof(T) and the offset of w to the functions as
 * constants, and every function is always_inline: at the call site the
 * compiler folds the strides and turns the memcpys of the swap into plain
 * moves, so there is no cost over a GraphEdge-only version.
 *
 *   QUICK_SORT(edges, m, 0, m - 1);
 *
 *   quick_sort_frame* frames = malloc(sizeof(quick_sort_frame) * m);
 *   quick_sort_iter   it;
 *   QUICK_SORT_ITER_INIT(&it, edges, m, frames);
 *   for (GraphEdge* e; (e = QUICK_SORT_STEP(&it, edges)) != NULL;)
 *       ...                            // e is the next edge by weight
 *   free(frames);
 *
 * Caveat inherited from the pattern: the array argument may be evaluated
 * more than once. Pass a bare variable.
 */

typedef struct
{
    int p;
    int q;
} quick_sort_frame;

/* State of a lazy quicksort over edges[0..count-1]: frames[0..top) holds
 * the ranges still to be partitioned (see QUICK_SORT_STEP). Pending ranges
 * are pairwise disjoint and non-empty, so count frames always suffice. The
 * frames are owned by the caller: many iterators can share one block. */
typedef struct
{
    void*             edges;
    quick_sort_frame* frames;
    int               count;
    int               top;
} quick_sort_iter;

// Compile error if the w field is not a double
#define QUICK__CHECK_W(a) ((void)sizeof(&(a)->w - (double*)0))
// Offset in bytes of w inside the element, computed from the pointer (nothing is read)
#define QUICK__W_OFF(a) ((size_t)((char*)&(a)->w - (char*)(a)))
#define QUICK__FORCE_INLINE static inline __attribute__((always_inline))

/* Split point of edges[p..q] (see quick__partition), -1 on a bad range */
#define QUICK_PARTITION(a, count, p, q)                                                            \
    (QUICK__CHECK_W(a), quick__partition((a), sizeof(*(a)), QUICK__W_OFF(a), (count), (p), (q)))
/* Sorts edges[p..q] by weight */
#define QUICK_SORT(a, count, p, q)                                                                 \
    (QUICK__CHECK_W(a), quick__sort((a), sizeof(*(a)), QUICK__W_OFF(a), (count), (p), (q)))
/* Sorts only edges[p..k-1], the k - p smallest weights of edges[p..q] */
#define QUICK_SORT_PARTIAL(a, count, p, q, k)                                                      \
    (QUICK__CHECK_W(a),                                                                            \
     quick__partial_sort((a), sizeof(*(a)), QUICK__W_OFF(a), (count), (p), (q), (k)))
/* Lazy sort over a[0..count-1], frames must hold at least count slots */
#define QUICK_SORT_ITER_INIT(it, a, count, frames)                                                 \
    (QUICK__CHECK_W(a), quick__iter_init((it), (a), (count), (frames)))
/* Next element by weight, NULL when done. a only gives the element type:
 * any pointer of the type given to QUICK_SORT_ITER_INIT */
#define QUICK_SORT_STEP(it, a)                                                                     \
    (QUICK__CHECK_W(a), (__typeof__(a))quick__step((it), sizeof(*(a)), QUICK__W_OFF(a)))

QUICK__FORCE_INLINE double quick__w(const void* a, size_t es, size_t w_off, int i)
{
    return *(const double*)((const char*)a + (size_t)i * es + w_off);
}

/* No VLA for the temporary: gcc refuses to inline a function that has one.
 * Every record sorted here is a few words, 64 bytes is plenty (checked). */
#define QUICK__MAX_ELEM 64

QUICK__FORCE_INLINE void quick__swap(void* a, size_t es, int i, int j)
{
    char* x = (char*)a + (size_t)i * es;
    char* y = (char*)a + (size_t)j * es;
    char  tmp[QUICK__MAX_ELEM];

    memcpy(tmp, x, es);
    memcpy(x, y, es);
    memcpy(y, tmp, es);
}

/* Hoare partition (CLRS): the pivot value edges[p].w is saved up front,
 * since the swaps can move the element sitting at p. The returned index is
 * a SPLIT POINT, not the pivot's final slot: edges[p..j] are all <= pivot
 * and edges[j+1..q] are all >= pivot, and j < q always holds, so both
 * halves of the recursion are strictly smaller than [p, q]. */
QUICK__FORCE_INLINE int quick__partition(void* a, size_t es, size_t w_off, int count, int p, int q)
{
    // Handle edge cases
    if (p < 0 || q >= count || es > QUICK__MAX_ELEM)
    {
        LOG_ERROR("invalid range (p=%d, q=%d, count=%d, es=%zu)", p, q, count, es);
        return -1;
    }

    double pivot   = quick__w(a, es, w_off, p);
    int    e_minus = p - 1;
    int    e_plus  = q + 1;

    while (true)
    {
        do
        {
            e_plus--;
        } while (quick__w(a, es, w_off, e_plus) > pivot);

        do
        {
            e_minus++;
        } while (quick__w(a, es, w_off, e_minus) < pivot);

        if (e_minus >= e_plus)
        {
            return e_plus;
        }

        quick__swap(a, es, e_minus, e_plus);
    }
}

/* Sorts only the first k slots of edges[p..q]: on return edges[p..k-1]
 * hold the k smallest weights in order, the tail is left unsorted. k is an
 * ABSOLUTE index, as p and q are. */
QUICK__FORCE_INLINE void quick__partial_sort(void* a, size_t es, size_t w_off, int count, int p,
                                             int q, int k)
{
    if (count <= 0)
        return;

    // we keep a stack of pending ranges: the right half is pushed before the left one, so ranges
    // are popped left to right
    quick_sort_frame* stack = malloc(sizeof(quick_sort_frame) * (size_t)count);
    int               top   = 0;

    if (!stack)
        return;

    stack[top++] = (quick_sort_frame){.p = p, .q = q};

    while (top > 0)
    {
        quick_sort_frame frame = stack[--top];

        // STOP CONDITION: ranges come out left to right, so every pending one lies past k too
        if (frame.p >= k)
            break;

        // BASE
        if (frame.p >= frame.q)
            continue;

        int pivot_position = quick__partition(a, es, w_off, count, frame.p, frame.q);

        if (pivot_position < 0)
            break;

        // IF NEEDED FIRST WE PUSH THE RIGHT SIDE, SO THE LEFT REMAINS AT TOP OF STACK
        if (pivot_position + 1 < k)
            stack[top++] = (quick_sort_frame){.p = pivot_position + 1, .q = frame.q};

        stack[top++] = (quick_sort_frame){.p = frame.p, .q = pivot_position};
    }

    free(stack);
}

QUICK__FORCE_INLINE void quick__sort(void* a, size_t es, size_t w_off, int count, int p, int q)
{
    // A full sort is a partial one that settles every slot up to q
    quick__partial_sort(a, es, w_off, count, p, q, q + 1);
}

QUICK__FORCE_INLINE bool quick__iter_init(quick_sort_iter* it, void* a, int count,
                                          quick_sort_frame* frames)
{
    if (it == NULL || a == NULL || frames == NULL || count <= 0)
    {
        LOG_ERROR("invalid iterator arguments (count=%d)", count);
        return false;
    }

    it->edges     = a;
    it->frames    = frames;
    it->count     = count;
    it->top       = 1;
    it->frames[0] = (quick_sort_frame){.p = 0, .q = count - 1};

    return true;
}

/* Lazy quicksort: each call does only the partitions needed to settle the
 * next slot and returns it. Ranges are popped left to right, so the first
 * single-element range found is the smallest edge not yet returned, and it
 * is already in its final slot (later partitions only touch ranges to its
 * right): the returned pointer stays valid. NULL once every edge has been
 * returned. */
QUICK__FORCE_INLINE void* quick__step(quick_sort_iter* it, size_t es, size_t w_off)
{
    while (it->top > 0)
    {
        quick_sort_frame frame = it->frames[--it->top];

        // BASE: a single element is in its final slot
        if (frame.p == frame.q)
            return (char*)it->edges + (size_t)frame.p * es;

        int pivot_position = quick__partition(it->edges, es, w_off, it->count, frame.p, frame.q);

        if (pivot_position < 0)
            return NULL;

        /**
         * SINCE WE ARE USING A STACK, WE NEED TO PUSH FIRST THE RIGHT PART
         * AND THEN THE LEFT PART, IN THIS WAY LEFT-PARTITIONS ARE EXECUTED
         * FIRST
         */
        it->frames[it->top++] = (quick_sort_frame){.p = pivot_position + 1, .q = frame.q};
        it->frames[it->top++] = (quick_sort_frame){.p = frame.p, .q = pivot_position};
    }

    return NULL;
}

#endif /* GRAPH_SORTING_H */
