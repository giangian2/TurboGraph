#include <stdlib.h>

#ifndef D_HEAP_H
#define D_HEAP_H

/* Array-backed D-ary min-heap: each node has up to `d` children instead of
 * the usual 2, stored contiguously in `data`. Fewer levels than a binary
 * heap (log_d n) at the cost of scanning up to d children per sift-down.
 *
 * Keys double as their own identity: this heap has no separate (id, value)
 * pair, so `pos[key]` gives the current index of `key` inside `data`, or -1
 * if `key` is not currently in the heap. That lookup is what lets
 * decreaseKey() find a key in O(1) instead of an O(n) linear scan, which is
 * what makes it an O(log_d n) operation overall. A consequence of keys
 * being their own identity: two elements can never hold the same value at
 * once, and decreasing a key also changes the identity pos[] tracks for it.
 * `pos` grows on demand (independently of `data`) as keys past its current
 * length are inserted; keys must be non-negative. */
typedef struct
{
    int*   data;
    size_t capacity;
    size_t size;
    size_t d;           /* branching factor: number of children per node */
    int*   pos;         /* pos[key] = index of key in data[], or -1 if absent */
    size_t posCapacity; /* allocated length of pos[] */
} DHeap;

/* Status codes for operations that don't naturally return a key value.
 * DHEAP_ERR covers invalid arguments, allocation failure, and "key not
 * currently in the heap" alike -- callers needing to tell those apart
 * should validate their own preconditions before calling. */
typedef enum
{
    DHEAP_OK  = 0,
    DHEAP_ERR = -1
} DHeapStatus;

/**
 * @param d Branching factor (number of children per node)
 * @param capacity Initial number of slots to allocate for data[]
 *
 * @return Pointer to an empty DHeap allocated on the heap, or NULL on
 * allocation failure
 */
DHeap* createDHeap(size_t d, size_t capacity);

/**
 * @param h Heap containing key
 * @param key Non-negative key currently in the heap
 * @param delta Non-negative amount to subtract from key
 *
 * @return DHEAP_OK on success, DHEAP_ERR if h is NULL, key/delta/the
 * resulting key are invalid, key is not currently in the heap, or the
 * decreased key would collide with another key already present
 */
int decreaseKey(DHeap* h, int key, int delta);

/**
 * @param h Heap to extract the minimum from
 *
 * @return The minimum key removed from the heap, or DHEAP_ERR if h is
 * NULL or the heap is empty
 */
int extractMin(DHeap* h);

/**
 * @param h Heap to insert into
 * @param newKey Non-negative key to insert; must not already be in the heap
 *
 * @return DHEAP_OK on success, DHEAP_ERR on invalid/duplicate key or
 * allocation failure
 */
int insert(DHeap* h, int newKey);

/**
 * @param h Heap to free, safe to call with NULL
 */
void freeDHeap(DHeap* h);

#endif
