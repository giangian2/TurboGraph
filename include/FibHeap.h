#include <stdbool.h>
#include <stdlib.h>

#ifndef FIB_HEAP_H
#define FIB_HEAP_H

/* Fibonacci min-heap: a forest of heap-ordered trees linked by circular
 * doubly-linked sibling lists, giving O(1) amortized insert/decreaseKey and
 * O(log n) amortized extractMin.
 *
 * Nodes live in a Structure-of-Arrays arena (key/parent/child/left/right/
 * degree/mark), each its own contiguous, realloc-able buffer, instead of
 * individually malloc'd tree nodes linked by raw pointers: every field scan
 * (e.g. the consolidate pass, which only touches key/degree) stays
 * cache-friendly, and there is exactly one allocation per array instead of
 * one per node. Children/siblings/parent are indices into these arrays, not
 * pointers. Freed slots (from extractMin) are recycled off `freeList` before
 * the arena grows, so the arrays only expand when genuinely more nodes are
 * live at once than have ever been live before.
 *
 * Keys double as their own identity, exactly like DHeap: `pos[key]` gives
 * the arena index currently holding `key`, or -1 if `key` is not in the
 * heap. That is what makes decreaseKey() an O(1) lookup instead of a search.
 * A consequence: two nodes can never hold the same key at once, and
 * decreasing a key also changes the identity pos[] tracks for it. `pos`
 * grows independently of the node arena, on demand, as keys past its
 * current length are inserted; keys must be non-negative.
 *
 * In graph algorithms these keys are node/vertex ids, so `pos` is sized to
 * the largest id seen so far, not to the number of nodes actually in the
 * heap right now -- those two can differ a lot. Inserting a single node
 * with id 10000 still requires `pos` to span index 10000, even if the heap
 * holds just that one element; that is why `pos` grows with the id space,
 * independently of the node arena (`capacity`/`used`/`size`). */
typedef struct
{
    int*   key;      /* key[i]: value stored at node i */
    int*   parent;   /* parent[i]: index of i's parent, or -1 if i is a root */
    int*   child;    /* child[i]: index of one arbitrary child of i, or -1 if i is a leaf */
    int*   left;     /* left[i]: previous sibling in i's circular sibling list */
    int*   right;    /* right[i]: next sibling in i's circular sibling list */
    int*   degree;   /* degree[i]: number of children of i */
    bool*  mark;     /* mark[i]: whether i has lost a child since it last became a child */
    size_t capacity; /* allocated length of key/parent/child/left/right/degree/mark */
    size_t used;     /* arena slots ever handed out by the bump allocator (<= capacity) */
    size_t size;     /* number of nodes currently in the heap */
    int    min;      /* index of the minimum root, or -1 if the heap is empty */

    int*   freeList;     /* stack of slot indices reclaimed by extractMin, ready for reuse */
    size_t freeCount;    /* number of indices currently on freeList */
    size_t freeCapacity; /* allocated length of freeList */

    int*   pos;         /* pos[key] = arena index of key, or -1 if absent */
    size_t posCapacity; /* allocated length of pos[] */

    int*   rootBuf;         /* scratch snapshot of the root list, reused across extractMin calls */
    size_t rootBufCapacity; /* allocated length of rootBuf */
} FibHeap;

/* Status codes for operations that don't naturally return a key value.
 * FIBHEAP_ERR covers invalid arguments, allocation failure, and "key not
 * currently in the heap" alike -- callers needing to tell those apart
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
 * @param newKey Non-negative key to insert; must not already be in the heap
 *
 * @return FIBHEAP_OK on success, FIBHEAP_ERR on invalid/duplicate key or
 * allocation failure
 */
int fibInsert(FibHeap* h, int newKey);

/**
 * @param h FibHeap to extract the minimum from
 *
 * @return The minimum key removed from the heap, or FIBHEAP_ERR if h is
 * NULL or the heap is empty
 */
int fibExtractMin(FibHeap* h);

/**
 * @param h FibHeap containing key
 * @param key Non-negative key currently in the heap
 * @param delta Non-negative amount to subtract from key
 *
 * @return FIBHEAP_OK on success, FIBHEAP_ERR if h is NULL, key/delta/the
 * resulting key are invalid, key is not currently in the heap, or the
 * decreased key would collide with another key already present
 */
int fibDecreaseKey(FibHeap* h, int key, int delta);

/**
 * @param h FibHeap to free, safe to call with NULL
 */
void freeFibHeap(FibHeap* h);

#endif
