#include <stdbool.h>
#include <stdlib.h>

#ifndef FIB_HEAP_H
#define FIB_HEAP_H

/* Fibonacci min-heap: a forest of heap-ordered trees linked by circular
 * doubly-linked sibling lists, giving O(1) amortized insert/decreaseKey and
 * O(log n) amortized extractMin.
 *
 * Nodes live in a Structure-of-Arrays arena (id/weight/parent/child/left/
 * right/degree/mark), each its own contiguous, realloc-able buffer, instead
 * of individually malloc'd tree nodes linked by raw pointers: every field
 * scan (e.g. the consolidate pass, which only touches weight/degree) stays
 * cache-friendly, and there is exactly one allocation per array instead of
 * one per node. Children/siblings/parent are indices into these arrays, not
 * pointers. Freed slots (from extractMin) are recycled off `freeList` before
 * the arena grows, so the arrays only expand when genuinely more nodes are
 * live at once than have ever been live before.
 *
 * Identity and priority are two separate arrays, not one value doing both
 * jobs: `id[i]` is the node identity at arena slot i (e.g. a graph vertex
 * id), `weight[i]` is its priority -- what the heap is actually ordered
 * by. They always move together (see link()/cut()), so a single arena
 * slot locates both at once. Splitting them like this means weights may
 * repeat freely (two different nodes can share the same priority), unlike
 * a design where the priority doubles as its own identity.
 *
 * `pos[id]` gives the arena index currently holding `id`, or -1 if `id`
 * is not in the heap. That is what makes decreaseKey() an O(1) lookup
 * instead of a search. `pos` grows independently of the node arena, on
 * demand, as ids past its current length are inserted; ids must be
 * non-negative and unique, but weights carry no such restriction.
 *
 * In graph algorithms `id` is a node/vertex id, so `pos` is sized to the
 * largest id seen so far, not to the number of nodes actually in the heap
 * right now -- those two can differ a lot. Inserting a single node with
 * id 10000 still requires `pos` to span index 10000, even if the heap
 * holds just that one element; that is why `pos` grows with the id space,
 * independently of the node arena (`capacity`/`used`/`size`). */
typedef struct
{
    int*    id;       /* id[i]: node identity at arena slot i */
    double* weight;   /* weight[i]: priority of id[i] -- what the heap is ordered by */
    int*    parent;   /* parent[i]: index of i's parent, or -1 if i is a root */
    int*    child;    /* child[i]: index of one arbitrary child of i, or -1 if i is a leaf */
    int*    left;     /* left[i]: previous sibling in i's circular sibling list */
    int*    right;    /* right[i]: next sibling in i's circular sibling list */
    int*    degree;   /* degree[i]: number of children of i */
    bool*   mark;     /* mark[i]: whether i has lost a child since it last became a child */
    size_t  capacity; /* allocated length of id/weight/parent/child/left/right/degree/mark */
    size_t  used;     /* arena slots ever handed out by the bump allocator (<= capacity) */
    size_t  size;     /* number of nodes currently in the heap */
    int     min;      /* index of the minimum root, or -1 if the heap is empty */

    int*   freeList;     /* stack of slot indices reclaimed by extractMin, ready for reuse */
    size_t freeCount;    /* number of indices currently on freeList */
    size_t freeCapacity; /* allocated length of freeList */

    int*   pos;         /* pos[id] = arena index of id, or -1 if absent */
    size_t posCapacity; /* allocated length of pos[] */

    int*   rootBuf;         /* scratch snapshot of the root list, reused across extractMin calls */
    size_t rootBufCapacity; /* allocated length of rootBuf */
} FibHeap;

/* Status codes for operations that don't naturally return a value of their
 * own. FIBHEAP_ERR covers invalid arguments, allocation failure, and "id
 * not currently in the heap" alike -- callers needing to tell those apart
 * should validate their own preconditions before calling. */
typedef enum
{
    FIBHEAP_OK  = 0,
    FIBHEAP_ERR = -1
} FibHeapStatus;

/**
 * @param capacity Initial number of arena slots to allocate
 *
 * @return Pointer to an empty FibHeap allocated on the heap, or NULL on
 * allocation failure
 */
FibHeap* createFibHeap(size_t capacity);

/**
 * @param h FibHeap to insert into
 * @param id Non-negative id to insert; must not already be in the heap
 * @param weight Priority of id; may repeat an existing weight freely
 *
 * @return FIBHEAP_OK on success, FIBHEAP_ERR on invalid/duplicate id or
 * allocation failure
 */
int fibInsert(FibHeap* h, int id, double weight);

/**
 * @param h FibHeap to extract the minimum from
 * @param outId Set to the id of the removed minimum; must not be NULL
 * @param outWeight If non-NULL, set to the removed minimum's weight
 *
 * @return FIBHEAP_OK on success, FIBHEAP_ERR if h is NULL, the heap is
 * empty, or outId is NULL
 */
int fibExtractMin(FibHeap* h, int* outId, double* outWeight);

/**
 * @param h FibHeap containing id
 * @param id Non-negative id currently in the heap
 * @param newWeight New priority for id; must be <= its current weight
 *
 * @return FIBHEAP_OK on success, FIBHEAP_ERR if h is NULL, id is invalid
 * or not currently in the heap, or newWeight is greater than id's current
 * weight
 */
int fibDecreaseKey(FibHeap* h, int id, double newWeight);

/**
 * @param h FibHeap to free, safe to call with NULL
 */
void freeFibHeap(FibHeap* h);

#endif
