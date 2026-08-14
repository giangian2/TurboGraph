#include "../include/DHeap.h"
#include <stdlib.h>

/* Forward declarations: the static helpers are implemented further down
 * the file (after the public entry points), so they need prototypes here
 * to be usable by decreaseKey/extractMin/insert above. */
static int           expandPos(DHeap* h, size_t neededIndex);
static void          expandDHeap(DHeap* h);
static inline size_t getParentIndex(size_t i, size_t D);
static inline size_t getStartSonIndex(size_t i, size_t D);
static inline size_t getEndSonIndex(size_t i, size_t D);
static inline int    getKey(size_t i, DHeap* h);
static inline void   swap(DHeap* h, size_t i, size_t j);
static void          moveUp(DHeap* h, size_t index);
static void          moveDwn(DHeap* h, size_t index);

/* Allocates the heap struct and its backing arrays; frees whichever
 * allocations succeeded if another one fails, so no leak on the
 * partial-failure path. pos[] starts out the same size as data[] and is
 * initialized to -1 ("key not in heap") everywhere. */
DHeap* createDHeap(size_t d, size_t capacity)
{
    DHeap* heap = (DHeap*)malloc(sizeof(DHeap));
    int*   data = (int*)malloc(sizeof(int) * capacity);
    int*   pos  = (int*)malloc(sizeof(int) * capacity);

    if (heap == NULL || data == NULL || pos == NULL)
    {
        if (heap)
        {
            free(heap);
        }

        if (data)
        {
            free(data);
        }

        if (pos)
        {
            free(pos);
        }
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++)
    {
        pos[i] = -1;
    }

    heap->capacity    = capacity;
    heap->size        = 0;
    heap->d           = d;
    heap->data        = data;
    heap->pos         = pos;
    heap->posCapacity = capacity;

    return heap;
}

/* O(log_d n): pos[key] locates the node in O(1) -- see the DHeap struct
 * comment in DHeap.h -- so the only remaining cost is the sift-up. Since
 * delta >= 0, newKey <= key, and key already fits inside pos[] (it's
 * currently in the heap), so pos[] never needs to grow here; only insert()
 * can push posCapacity past the current key values. */
int decreaseKey(DHeap* h, int key, int delta)
{
    if (h == NULL || key < 0 || delta < 0)
    {
        return DHEAP_ERR;
    }

    if ((size_t)key >= h->posCapacity || h->pos[key] == -1)
    {
        return DHEAP_ERR; // key is not currently in the heap
    }

    int newKey = key - delta;
    if (newKey < 0)
    {
        return DHEAP_ERR;
    }

    if (newKey != key && h->pos[newKey] != -1)
    {
        return DHEAP_ERR; // newKey would collide with another key already in the heap
    }

    size_t index = (size_t)h->pos[key];

    // pos[] is keyed by value, not by a separate stable handle, so
    // decreasing a key also changes its identity: retire the old slot and
    // claim the decreased key's slot before sifting up.
    h->pos[key]    = -1;
    h->data[index] = newKey;
    h->pos[newKey] = (int)index;

    moveUp(h, index);
    return DHEAP_OK;
}

/* Removes the root, plugs the gap with the last element (kept compact for
 * the array representation), then sifts it down. pos[] is updated for
 * both the removed key and the relocated one. */
int extractMin(DHeap* h)
{
    if (h == NULL || h->size == 0)
    {
        return DHEAP_ERR;
    }

    int min     = h->data[0];
    h->pos[min] = -1;
    h->size--;

    if (h->size > 0)
    {
        h->data[0]         = h->data[h->size];
        h->pos[h->data[0]] = 0;
        moveDwn(h, 0);
    }

    return min;
}

/* Grows data[] (element storage) and pos[] (key-value index range)
 * independently as needed, appends newKey at the tail, then sifts it up. */
int insert(DHeap* h, int newKey)
{
    if (h == NULL || newKey < 0)
    {
        return DHEAP_ERR;
    }

    if (expandPos(h, (size_t)newKey) != DHEAP_OK)
    {
        return DHEAP_ERR;
    }

    if (h->pos[newKey] != -1)
    {
        return DHEAP_ERR; // duplicate keys are not supported
    }

    if (h->size == h->capacity)
    {
        expandDHeap(h);
        if (h->size == h->capacity)
        {
            return DHEAP_ERR; // expandDHeap left capacity unchanged: realloc failed
        }
    }

    size_t index   = h->size;
    h->data[index] = newKey;
    h->pos[newKey] = (int)index;
    h->size++;

    moveUp(h, index);
    return DHEAP_OK;
}

void freeDHeap(DHeap* h)
{
    if (h == NULL)
    {
        return;
    }

    free(h->data);
    free(h->pos);
    free(h);
}

/* Grows pos[] (doubling) until it can index `neededIndex`, initializing
 * newly added slots to -1 ("key not in heap"). Independent from
 * expandDHeap: data[] grows with the number of elements actually stored,
 * pos[] grows with the largest key value seen so far -- the two capacities
 * can diverge. */
static int expandPos(DHeap* h, size_t neededIndex)
{
    if (neededIndex < h->posCapacity)
    {
        return DHEAP_OK;
    }

    size_t newPosCapacity = h->posCapacity == 0 ? 1 : h->posCapacity;
    while (newPosCapacity <= neededIndex)
    {
        newPosCapacity *= 2;
    }

    int* newPos = (int*)realloc(h->pos, sizeof(int) * newPosCapacity);
    if (newPos == NULL)
    {
        return DHEAP_ERR;
    }

    for (size_t i = h->posCapacity; i < newPosCapacity; i++)
    {
        newPos[i] = -1;
    }

    h->pos         = newPos;
    h->posCapacity = newPosCapacity;
    return DHEAP_OK;
}

/* Doubles the backing array's capacity via realloc. On failure the heap is
 * left untouched (h->data/h->capacity unchanged) so it stays usable, just
 * unable to grow further. */
static void expandDHeap(DHeap* h)
{
    size_t currentCapacity = h->capacity;
    size_t newCapacity     = currentCapacity * 2;

    int* newData = (int*)realloc(h->data, sizeof(int) * newCapacity);
    if (newData == NULL)
    {
        return;
    }
    // realloc already frees/reuses the old block as needed, no manual free here
    // free(h->data);
    h->data     = newData;
    h->capacity = newCapacity;
}

/* Index arithmetic for a D-ary heap laid out level-order in a flat array:
 * node i's children occupy the contiguous range [D*i+1, D*i+D], and its
 * parent is at (i-1)/D (integer division). With D=2 this reduces to the
 * classic binary-heap formulas. */
static inline size_t getParentIndex(size_t i, size_t D)
{
    return (i - 1) / D;
}

static inline size_t getStartSonIndex(size_t i, size_t D)
{
    return (D * i) + 1;
}

static inline size_t getEndSonIndex(size_t i, size_t D)
{
    return (D * i) + D;
}

static inline int getKey(size_t i, DHeap* h)
{
    return h->data[i];
}

/* Swaps two data[] slots and keeps pos[] in sync with the values' new
 * locations -- this is what lets decreaseKey() find any key in O(1). */
static inline void swap(DHeap* h, size_t i, size_t j)
{
    int tmp    = getKey(i, h);
    h->data[i] = h->data[j];
    h->data[j] = tmp;

    h->pos[h->data[i]] = (int)i;
    h->pos[h->data[j]] = (int)j;
}

/* Sift-up: bubbles the node at `index` toward the root by repeatedly
 * swapping it with its parent as long as it's smaller, restoring the
 * min-heap property after a decrease-key or an insertion at the tail. */
static void moveUp(DHeap* h, size_t index)
{
    while (index > 0)
    {
        size_t parent = getParentIndex(index, h->d);

        if (getKey(parent, h) <= getKey(index, h))
        {
            break; // heap property already satisfied
        }

        swap(h, index, parent);
        index = parent;
    }
}

/* Sift-down: pushes the node at `index` toward the leaves by repeatedly
 * swapping it with its smallest child, restoring the min-heap property
 * after removing the root (extractMin moves the last element there). */
static void moveDwn(DHeap* h, size_t index)
{
    while (1)
    {
        size_t start_son = getStartSonIndex(index, h->d);

        // If the first child is out of the heap's bounds, this node is a leaf. Done.
        if (start_son >= h->size)
        {
            break;
        }

        // Compute the theoretical last child and clamp it to the heap size
        size_t end_son = getEndSonIndex(index, h->d);
        if (end_son >= h->size)
        {
            end_son = h->size - 1;
        }

        // Find the child with the minimum value among all D children present
        size_t smallest_son_index = start_son;
        int    min_value          = getKey(start_son, h);

        for (size_t i = start_son + 1; i <= end_son; i++)
        {
            int current_value = getKey(i, h);
            if (current_value < min_value)
            {
                min_value          = current_value;
                smallest_son_index = i;
            }
        }

        // If the current node is already <= the smallest child, the property holds
        if (getKey(index, h) <= min_value)
        {
            break;
        }

        // Otherwise swap the current node with its smallest child and descend
        swap(h, index, smallest_son_index);
        index = smallest_son_index; // update the index for the next iteration
    }
}
