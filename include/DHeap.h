#include <stdlib.h>

#ifndef D_HEAP_H
#define D_HEAP_H

/* Array-backed D-ary min-heap: each node has up to `d` children instead of
 * the usual 2, stored contiguously in `id`/`weight`. Fewer levels than a
 * binary heap (log_d n) at the cost of scanning up to d children per
 * sift-down.
 *
 * Identity and priority are two separate arrays, not one value doing both
 * jobs: `id[i]` is the node identity stored at heap position i (e.g. a
 * graph vertex id), `weight[i]` is its priority -- what the heap is
 * actually ordered by. They are always swapped together (see swap()), so
 * a single position always locates both at once. Splitting them like this
 * means weights may repeat freely (two different nodes can share the same
 * priority -- impossible if the priority doubled as its own identity),
 * and comparison-heavy code (moveDwn scanning up to d children before
 * doing at most one swap) only ever touches `weight[]`: more candidates
 * fit in a cache line without dragging along `id` bytes that aren't
 * needed until a swap actually happens.
 *
 * `pos[id]` gives the current heap position of `id`, or -1 if `id` is not
 * currently in the heap. That lookup is what lets decreaseKey() find a
 * node in O(1) instead of an O(n) linear scan, which is what makes it an
 * O(log_d n) operation overall. `pos` grows on demand (independently of
 * `id`/`weight`) as ids past its current length are inserted; ids must be
 * non-negative and unique, but weights carry no such restriction.
 *
 * In graph algorithms `id` is a node/vertex id, so `pos` is sized to the
 * largest id seen so far, not to the number of nodes actually in the heap
 * right now -- those two can differ a lot. Inserting a single node with
 * id 10000 still requires `pos` to span index 10000, even if the heap
 * holds just that one element; that is why `pos` grows with the id space,
 * independently of `id`/`weight`/`capacity`. */
typedef struct
{
    int*    id;     /* id[i]: node identity at heap position i */
    double* weight; /* weight[i]: priority of id[i] -- what the heap is ordered by */
    size_t  capacity;
    size_t  size;
    size_t  d;           /* branching factor: number of children per node */
    int*    pos;         /* pos[id] = heap position of id, or -1 if absent */
    size_t  posCapacity; /* allocated length of pos[] */
} DHeap;

/* Status codes for operations that don't naturally return a value of their
 * own. DHEAP_ERR covers invalid arguments, allocation failure, and "id not
 * currently in the heap" alike -- callers needing to tell those apart
 * should validate their own preconditions before calling. */
typedef enum
{
    DHEAP_OK  = 0,
    DHEAP_ERR = -1
} DHeapStatus;

/**
 * @param d Branching factor (number of children per node)
 * @param capacity Initial number of slots to allocate for id[]/weight[]
 *
 * @return Pointer to an empty DHeap allocated on the heap, or NULL on
 * allocation failure
 */
DHeap* createDHeap(size_t d, size_t capacity);

/**
 * @param h Heap containing id
 * @param id Non-negative id currently in the heap
 * @param newWeight New priority for id; must be <= its current weight
 *
 * @return DHEAP_OK on success, DHEAP_ERR if h is NULL, id is invalid or
 * not currently in the heap, or newWeight is greater than id's current
 * weight
 */
int decreaseKey(DHeap* h, int id, double newWeight);

/**
 * @param h Heap to extract the minimum from
 * @param outId Set to the id of the removed minimum; must not be NULL
 * @param outWeight If non-NULL, set to the removed minimum's weight
 *
 * @return DHEAP_OK on success, DHEAP_ERR if h is NULL, the heap is empty,
 * or outId is NULL
 */
int extractMin(DHeap* h, int* outId, double* outWeight);

/**
 * @param h Heap to insert into
 * @param id Non-negative id to insert; must not already be in the heap
 * @param weight Priority of id; may repeat an existing weight freely
 *
 * @return DHEAP_OK on success, DHEAP_ERR on invalid/duplicate id or
 * allocation failure
 */
int insert(DHeap* h, int id, double weight);

/**
 * @param h Heap to free, safe to call with NULL
 */
void freeDHeap(DHeap* h);

#endif
