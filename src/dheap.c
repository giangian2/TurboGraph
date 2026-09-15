#include "../include/DHeap.h"
#include "../include/Debug.h"
#include <stdlib.h>

/* Forward declarations: the static helpers are implemented further down
 * the file (after the public entry points), so they need prototypes here
 * to be usable by decreaseKey/extractMin/insert above. */
static int           expandPos(DHeap* h, size_t neededIndex);
static void          expandDHeap(DHeap* h);
static inline size_t getParentIndex(size_t i, size_t D);
static inline size_t getStartSonIndex(size_t i, size_t D);
static inline size_t getEndSonIndex(size_t i, size_t D);
static inline void   swap(DHeap* h, size_t i, size_t j);
static void          moveUp(DHeap* h, size_t index);
static void          moveDwn(DHeap* h, size_t index);

/* Allocates the heap struct and its backing arrays; frees whichever
 * allocations succeeded if another one fails, so no leak on the
 * partial-failure path. pos[] starts out the same size as id[]/weight[]
 * and is initialized to -1 ("id not in heap") everywhere. */
DHeap* createDHeap(size_t d, size_t capacity)
{
    DHeap*  heap   = (DHeap*)malloc(sizeof(DHeap));
    int*    id     = (int*)malloc(sizeof(int) * capacity);
    double* weight = (double*)malloc(sizeof(double) * capacity);
    int*    pos    = (int*)malloc(sizeof(int) * capacity);

    if (heap == NULL || id == NULL || weight == NULL || pos == NULL)
    {
        LOG_ERROR("allocation failed (d=%zu, capacity=%zu)", d, capacity);
        /* free(NULL) is legal: this frees only whichever allocations
         * actually succeeded, no per-pointer NULL check needed. */
        free(heap);
        free(id);
        free(weight);
        free(pos);
        return NULL;
    }

    for (size_t i = 0; i < capacity; i++)
    {
        pos[i] = -1;
    }

    heap->capacity    = capacity;
    heap->size        = 0;
    heap->d           = d;
    heap->id          = id;
    heap->weight      = weight;
    heap->pos         = pos;
    heap->posCapacity = capacity;

    LOG_DEBUG("DHeap created (d=%zu, capacity=%zu)", d, capacity);
    return heap;
}

/* O(log_d n): pos[id] locates the node in O(1) -- see the DHeap struct
 * comment in DHeap.h -- so the only remaining cost is the sift-up. Unlike
 * a design where the priority doubles as its own identity, newWeight
 * colliding with another node's weight is not a concern here: weights may
 * repeat freely, only ids must stay unique. */
int decreaseKey(DHeap* h, int id, double newWeight)
{
    if (h == NULL || id < 0)
    {
        LOG_ERROR("invalid argument (h=%p, id=%d)", (void*)h, id);
        return DHEAP_ERR;
    }

    if ((size_t)id >= h->posCapacity || h->pos[id] == -1)
    {
        LOG_ERROR("id %d not in heap", id);
        return DHEAP_ERR; // id is not currently in the heap
    }

    size_t index = (size_t)h->pos[id];

    if (newWeight > h->weight[index])
    {
        LOG_ERROR("newWeight %g > current weight %g for id %d", newWeight, h->weight[index], id);
        return DHEAP_ERR; // decreaseKey only ever lowers the weight
    }

    h->weight[index] = newWeight;
    moveUp(h, index);
    LOG_DEBUG("decreased key of id %d to %g", id, newWeight);
    return DHEAP_OK;
}

/* Removes the root, plugs the gap with the last element (kept compact for
 * the array representation), then sifts it down. pos[] is updated for
 * both the removed id and the relocated one. */
int extractMin(DHeap* h, int* outId, double* outWeight)
{
    if (h == NULL || h->size == 0 || outId == NULL)
    {
        LOG_ERROR("invalid argument or empty heap (h=%p, size=%zu, outId=%p)", (void*)h,
                  h ? h->size : (size_t)0, (void*)outId);
        return DHEAP_ERR;
    }

    *outId = h->id[0];
    if (outWeight != NULL)
    {
        *outWeight = h->weight[0];
    }

    h->pos[h->id[0]] = -1;
    h->size--;

    if (h->size > 0)
    {
        h->id[0]         = h->id[h->size];
        h->weight[0]     = h->weight[h->size];
        h->pos[h->id[0]] = 0;
        moveDwn(h, 0);
    }

    LOG_DEBUG("extracted min id=%d, %zu elements left", *outId, h->size);
    return DHEAP_OK;
}

/* Grows id[]/weight[] (element storage) and pos[] (id-value index range)
 * independently as needed, appends id/weight at the tail, then sifts it
 * up. */
int insert(DHeap* h, int id, double weight)
{
    if (h == NULL || id < 0)
    {
        LOG_ERROR("invalid argument (h=%p, id=%d)", (void*)h, id);
        return DHEAP_ERR;
    }

    if (expandPos(h, (size_t)id) != DHEAP_OK)
    {
        LOG_ERROR("expandPos failed for id %d", id);
        return DHEAP_ERR;
    }

    if (h->pos[id] != -1)
    {
        LOG_ERROR("duplicate id %d", id);
        return DHEAP_ERR; // duplicate ids are not supported
    }

    if (h->size == h->capacity)
    {
        expandDHeap(h);
        if (h->size == h->capacity)
        {
            LOG_ERROR("expandDHeap failed to grow past capacity %zu", h->capacity);
            return DHEAP_ERR; // expandDHeap left capacity unchanged: realloc failed
        }
    }

    size_t index     = h->size;
    h->id[index]     = id;
    h->weight[index] = weight;
    h->pos[id]       = (int)index;
    h->size++;

    moveUp(h, index);
    LOG_DEBUG("inserted id=%d weight=%g, size=%zu", id, weight, h->size);
    return DHEAP_OK;
}

void freeDHeap(DHeap* h)
{
    if (h == NULL)
    {
        return;
    }

    LOG_DEBUG("freeing DHeap (size=%zu, capacity=%zu)", h->size, h->capacity);
    free(h->id);
    free(h->weight);
    free(h->pos);
    free(h);
}

/* Grows pos[] (doubling) until it can index `neededIndex`, initializing
 * newly added slots to -1 ("id not in heap"). Independent from
 * expandDHeap: id[]/weight[] grow with the number of elements actually
 * stored, pos[] grows with the largest id value seen so far -- the two
 * capacities can diverge. Concretely, ids here are node ids, so pos[]
 * tracks the id space (it must be able to index pos[maxIdInserted]), not
 * how many nodes happen to be in the heap at once -- a heap holding one
 * node with id 10000 still needs pos[] to reach index 10000. */
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
        LOG_ERROR("realloc failed growing pos[] to %zu entries", newPosCapacity);
        return DHEAP_ERR;
    }

    for (size_t i = h->posCapacity; i < newPosCapacity; i++)
    {
        newPos[i] = -1;
    }

    h->pos         = newPos;
    h->posCapacity = newPosCapacity;
    LOG_DEBUG("pos[] grown to %zu entries", newPosCapacity);
    return DHEAP_OK;
}

/* Doubles id[]/weight[] via realloc, one call per array. h->capacity only
 * advances once both arrays have grown successfully: if one fails, the
 * array that did grow simply carries unused headroom past h->capacity
 * (harmless -- nothing is ever indexed past h->capacity), and the next
 * expansion attempt retries it at little extra cost, since realloc to a
 * size it already holds is cheap. This keeps both arrays' *valid* length
 * in lockstep without needing a rollback path. */
static void expandDHeap(DHeap* h)
{
    size_t currentCapacity = h->capacity;
    size_t newCapacity     = currentCapacity * 2;

    int* newId = (int*)realloc(h->id, sizeof(int) * newCapacity);
    if (newId)
    {
        h->id = newId;
    }

    double* newWeight = (double*)realloc(h->weight, sizeof(double) * newCapacity);
    if (newWeight)
    {
        h->weight = newWeight;
    }

    if (!newId || !newWeight)
    {
        LOG_ERROR("realloc failed growing DHeap to capacity %zu", newCapacity);
        return;
    }

    h->capacity = newCapacity;
    LOG_DEBUG("DHeap grown to capacity %zu", newCapacity);
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

/* Swaps two (id, weight) pairs and keeps pos[] in sync with their new
 * locations -- this is what lets decreaseKey() find any id in O(1). id
 * and weight always move together: a single pos[] entry per id is enough
 * because the pair never splits across positions. */
static inline void swap(DHeap* h, size_t i, size_t j)
{
    int    tmpId     = h->id[i];
    double tmpWeight = h->weight[i];

    h->id[i]     = h->id[j];
    h->weight[i] = h->weight[j];
    h->id[j]     = tmpId;
    h->weight[j] = tmpWeight;

    h->pos[h->id[i]] = (int)i;
    h->pos[h->id[j]] = (int)j;
}

/* Sift-up: bubbles the node at `index` toward the root by repeatedly
 * swapping it with its parent as long as it's smaller, restoring the
 * min-heap property after a decrease-key or an insertion at the tail. */
static void moveUp(DHeap* h, size_t index)
{
    while (index > 0)
    {
        size_t parent = getParentIndex(index, h->d);

        if (h->weight[parent] <= h->weight[index])
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

        // Find the child with the minimum weight among all D children present
        size_t smallest_son_index = start_son;
        double min_value          = h->weight[start_son];

        for (size_t i = start_son + 1; i <= end_son; i++)
        {
            double current_value = h->weight[i];
            if (current_value < min_value)
            {
                min_value          = current_value;
                smallest_son_index = i;
            }
        }

        // If the current node is already <= the smallest child, the property holds
        if (h->weight[index] <= min_value)
        {
            break;
        }

        // Otherwise swap the current node with its smallest child and descend
        swap(h, index, smallest_son_index);
        index = smallest_son_index; // update the index for the next iteration
    }
}
