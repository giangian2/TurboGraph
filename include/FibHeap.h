#ifndef FIB_HEAP_H
#define FIB_HEAP_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define FIB_HEAP_ID_TYPE int
#define FIB_MAX_DEGREE 92 // a node of degree k has >= F(k+2) nodes, F(94) > SIZE_MAX

/**
 *  Generic Fibonacci heap, same rules of DHeap.h: T must be a struct with a
 *  field `id` of type int in [0, n_ids), the order is given only by cmp
 *  (cmp(a, b) < 0 means a stays closer to the root), cmp, sizeof(T) and the
 *  offset of id are passed by the macros as constants so the compiler can
 *  inline them.
 *
 *  Elements never move: each one lives in a slot of the arena, trees are
 *  made of slot indices (-1 = none). Freed slots are reused through
 *  free_list, so capacity bounds the elements in the heap at the same time.
 *
 *  Memory layout (single malloc, shadow header):
 *
 *    [ fibheap_header_t | T elems[capacity] | int parent[capacity] | child | left | right
 *      | degree | free_list | root_buf | int lookup[n_ids] | bool mark[capacity] ]
 *                         ^-- pointer handed to the user (T*), the top is fibheap_top(h)
 *
 *  lookup[id] = slot of id, -1 if not in the heap.
 *  left/right are circular sibling lists, child is any one of the sons,
 *  mark = lost a son since it became a son, root_buf is the consolidate snapshot.
 *  The arrays are found from capacity and elem_size (fibheap__parent, ...),
 *  no pointers in the header: they would go stale if the block is moved.
 *
 *    typedef struct { double w; int id; } Node;
 *    Node* h = fibheap_create(n, n, node_cmp, Node);
 *    fibheap_insert(h, ((Node){ 3.5, 7 }), node_cmp);
 *    fibheap_decrease(h, ((Node){ 1.0, 7 }), node_cmp);
 *    Node top;
 *    fibheap_pop(h, &top, node_cmp);
 *    fibheap_free(h);
 */

typedef int (*fibheap_cmp)(const void* a, const void* b);

typedef enum
{
    FIBHEAP_OK  = 0,
    FIBHEAP_ERR = -1
} FibHeapStatus;

typedef struct
{
    size_t      capacity;
    size_t      size;
    size_t      used; // slots ever handed out, the others are free_list
    size_t      free_count;
    size_t      elem_size;
    size_t      n_ids; // length of lookup
    int         min;   // slot of the top, -1 if empty
    fibheap_cmp cmp;   // debug check only
} fibheap_header_t;

// Compile error if the two pointers have different types
#define FIBHEAP__CHECK_ID(T) ((void)sizeof(&((T*)0)->id - (FIB_HEAP_ID_TYPE*)0))
#define FIBHEAP__CHECK_PTR(h, p) ((void)sizeof((p) - (h)))
#define FIBHEAP__HDR(h) (((fibheap_header_t*)(void*)(h)) - 1)
// Offset in bytes of id inside T, computed from the pointer (nothing is read)
#define FIBHEAP__ID_OFF(h) ((size_t)((char*)&(h)->id - (char*)(h)))

#define fibheap_create(cap, n_ids, cmp, T)                                                         \
    (FIBHEAP__CHECK_ID(T), (T*)fibheap__create(cap, n_ids, cmp, sizeof(T)))
#define fibheap_free(h) (fibheap__free(FIBHEAP__HDR(h)))
#define fibheap_size(h) (FIBHEAP__HDR(h)->size)
#define fibheap_empty(h) (FIBHEAP__HDR(h)->size == 0)
// Read only, NULL if empty
#define fibheap_top(h) (fibheap_empty(h) ? NULL : (h) + FIBHEAP__HDR(h)->min)
// Fails if full, id out of range or already in the heap
#define fibheap_insert(h, item, cmp)                                                               \
    (fibheap__insert(FIBHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),         \
                     FIBHEAP__ID_OFF(h)))
// out must be a T*, fails if empty
#define fibheap_pop(h, out, cmp)                                                                   \
    (FIBHEAP__CHECK_PTR(h, out),                                                                   \
     fibheap__pop(FIBHEAP__HDR(h), (out), (cmp), sizeof(*(h)), FIBHEAP__ID_OFF(h)))
// Only towards the root, fails if item.id is not in the heap or item would go down
#define fibheap_decrease(h, item, cmp)                                                             \
    (fibheap__decrease(FIBHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),       \
                       FIBHEAP__ID_OFF(h)))
#define fibheap_contains(h, id) (fibheap__position(FIBHEAP__HDR(h), (id)) >= 0)
// Read only: changing the key here breaks the heap, use decrease
#define fibheap_get(h, id)                                                                         \
    (fibheap_contains(h, id) ? (h) + fibheap__position(FIBHEAP__HDR(h), (id)) : NULL)

static inline char* fibheap__elem(fibheap_header_t* fh, int slot, size_t es)
{
    return (char*)(fh + 1) + (size_t)slot * es;
}

// Start of the int arrays, they follow elems[capacity] in this order
static inline int* fibheap__ints(fibheap_header_t* fh, size_t k)
{
    return (int*)((char*)(fh + 1) + fh->capacity * fh->elem_size) + k * fh->capacity;
}

static inline int* fibheap__parent(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 0);
}
static inline int* fibheap__child(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 1);
}
static inline int* fibheap__left(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 2);
}
static inline int* fibheap__right(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 3);
}
static inline int* fibheap__degree(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 4);
}
static inline int* fibheap__free_list(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 5);
}
static inline int* fibheap__root_buf(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 6);
}
static inline FIB_HEAP_ID_TYPE* fibheap__lookup(fibheap_header_t* fh)
{
    return (FIB_HEAP_ID_TYPE*)fibheap__ints(fh, 7);
}
static inline bool* fibheap__mark(fibheap_header_t* fh)
{
    return (bool*)(fibheap__lookup(fh) + fh->n_ids);
}

static inline FIB_HEAP_ID_TYPE fibheap__get_id(const void* elem, size_t id_off)
{
    return *(const FIB_HEAP_ID_TYPE*)((const char*)elem + id_off);
}

static inline void* fibheap__create(size_t capacity, size_t n_ids, fibheap_cmp cmp,
                                    size_t elem_size)
{
    assert(cmp != NULL);

    size_t bytes = sizeof(fibheap_header_t) + capacity * elem_size + 7 * capacity * sizeof(int) +
                   n_ids * sizeof(FIB_HEAP_ID_TYPE) + capacity * sizeof(bool);

    fibheap_header_t* fh = (fibheap_header_t*)malloc(bytes);
    if (fh == NULL)
    {
        return NULL;
    }

    fh->capacity   = capacity;
    fh->size       = 0;
    fh->used       = 0;
    fh->free_count = 0;
    fh->elem_size  = elem_size;
    fh->n_ids      = n_ids;
    fh->min        = -1;
    fh->cmp        = cmp;

    // 0xFF bytes = -1: no id in the heap
    memset(fibheap__lookup(fh), 0xFF, n_ids * sizeof(FIB_HEAP_ID_TYPE));

    return fh + 1;
}

static inline void fibheap__free(fibheap_header_t* fh)
{
    free(fh);
}

// Puts the singleton idx at the right of anchor
static inline void fibheap__list_insert(fibheap_header_t* fh, int anchor, int idx)
{
    int anchor_right = fibheap__right(fh)[anchor];

    fibheap__right(fh)[anchor]      = idx;
    fibheap__left(fh)[idx]          = anchor;
    fibheap__right(fh)[idx]         = anchor_right;
    fibheap__left(fh)[anchor_right] = idx;
}

// Detaches idx from its list and leaves it as a singleton
static inline void fibheap__list_remove(fibheap_header_t* fh, int idx)
{
    int l = fibheap__left(fh)[idx];
    int r = fibheap__right(fh)[idx];

    fibheap__left(fh)[r]    = l;
    fibheap__right(fh)[l]   = r;
    fibheap__left(fh)[idx]  = idx;
    fibheap__right(fh)[idx] = idx;
}

// y becomes a son of x
static inline void fibheap__link(fibheap_header_t* fh, int y, int x)
{
    fibheap__list_remove(fh, y);
    fibheap__parent(fh)[y] = x;

    if (fibheap__child(fh)[x] == -1)
    {
        fibheap__child(fh)[x] = y;
    }
    else
    {
        fibheap__list_insert(fh, fibheap__child(fh)[x], y);
    }

    fibheap__degree(fh)[x]++;
    fibheap__mark(fh)[y] = false;
}

// Moves idx from the sons of parent to the root list
static inline void fibheap__cut(fibheap_header_t* fh, int idx, int parent)
{
    if (fibheap__child(fh)[parent] == idx)
    {
        fibheap__child(fh)[parent] =
            (fibheap__right(fh)[idx] == idx) ? -1 : fibheap__right(fh)[idx];
    }

    fibheap__list_remove(fh, idx);
    fibheap__degree(fh)[parent]--;

    fibheap__parent(fh)[idx] = -1;
    fibheap__mark(fh)[idx]   = false;

    fibheap__list_insert(fh, fh->min, idx);
}

// Climbs while the ancestors were already marked, cutting them
static inline void fibheap__cascading_cut(fibheap_header_t* fh, int idx)
{
    int parent = fibheap__parent(fh)[idx];

    while (parent != -1)
    {
        if (!fibheap__mark(fh)[idx])
        {
            fibheap__mark(fh)[idx] = true;
            break;
        }

        fibheap__cut(fh, idx, parent);
        idx    = parent;
        parent = fibheap__parent(fh)[idx];
    }
}

// Merges the roots with the same degree, then finds the new min
static inline void fibheap__consolidate(fibheap_header_t* fh, fibheap_cmp cmp, size_t es)
{
    int degree_table[FIB_MAX_DEGREE];
    for (int i = 0; i < FIB_MAX_DEGREE; i++)
    {
        degree_table[i] = -1;
    }

    // Snapshot: link changes the root list while we walk it
    size_t root_count = 0;
    int    w          = fh->min;
    do
    {
        fibheap__root_buf(fh)[root_count++] = w;
        w                                   = fibheap__right(fh)[w];
    } while (w != fh->min);

    for (size_t i = 0; i < root_count; i++)
    {
        int x = fibheap__root_buf(fh)[i];
        int d = fibheap__degree(fh)[x];

        while (d < FIB_MAX_DEGREE && degree_table[d] != -1)
        {
            int y = degree_table[d];
            if (cmp(fibheap__elem(fh, y, es), fibheap__elem(fh, x, es)) < 0)
            {
                int tmp = x;
                x       = y;
                y       = tmp;
            }

            fibheap__link(fh, y, x);
            degree_table[d] = -1;
            d               = fibheap__degree(fh)[x];
        }

        if (d < FIB_MAX_DEGREE)
        {
            degree_table[d] = x;
        }
    }

    // The survivors are the roots left in the table
    fh->min = -1;
    for (int d = 0; d < FIB_MAX_DEGREE; d++)
    {
        int x = degree_table[d];
        if (x == -1)
        {
            continue;
        }

        if (fh->min == -1 || cmp(fibheap__elem(fh, x, es), fibheap__elem(fh, fh->min, es)) < 0)
        {
            fh->min = x;
        }
    }
}

static inline int fibheap__position(fibheap_header_t* fh, FIB_HEAP_ID_TYPE id)
{
    if (id < 0 || (size_t)id >= fh->n_ids)
    {
        return -1;
    }

    return fibheap__lookup(fh)[id];
}

static inline int fibheap__insert(fibheap_header_t* fh, const void* elem, fibheap_cmp cmp,
                                  size_t es, size_t id_off)
{
    assert(cmp == fh->cmp && es == fh->elem_size);

    FIB_HEAP_ID_TYPE id = fibheap__get_id(elem, id_off);

    if (fh->size >= fh->capacity || id < 0 || (size_t)id >= fh->n_ids ||
        fibheap__lookup(fh)[id] != -1)
    {
        return FIBHEAP_ERR;
    }

    int slot = fh->free_count > 0 ? fibheap__free_list(fh)[--fh->free_count] : (int)fh->used++;

    memcpy(fibheap__elem(fh, slot, es), elem, es);
    fibheap__parent(fh)[slot] = -1;
    fibheap__child(fh)[slot]  = -1;
    fibheap__degree(fh)[slot] = 0;
    fibheap__mark(fh)[slot]   = false;
    fibheap__left(fh)[slot]   = slot;
    fibheap__right(fh)[slot]  = slot;

    if (fh->min == -1)
    {
        fh->min = slot;
    }
    else
    {
        fibheap__list_insert(fh, fh->min, slot);
        if (cmp(fibheap__elem(fh, slot, es), fibheap__elem(fh, fh->min, es)) < 0)
        {
            fh->min = slot;
        }
    }

    fibheap__lookup(fh)[id] = slot;
    fh->size++;

    return FIBHEAP_OK;
}

static inline int fibheap__pop(fibheap_header_t* fh, void* out, fibheap_cmp cmp, size_t es,
                               size_t id_off)
{
    assert(cmp == fh->cmp && es == fh->elem_size);

    if (fh->size == 0)
    {
        return FIBHEAP_ERR;
    }

    int z = fh->min;
    memcpy(out, fibheap__elem(fh, z, es), es);

    // Sons of z become roots
    int sons = fibheap__degree(fh)[z];
    int c    = fibheap__child(fh)[z];
    for (int i = 0; i < sons; i++)
    {
        int next = fibheap__right(fh)[c];

        fibheap__list_remove(fh, c);
        fibheap__parent(fh)[c] = -1;
        fibheap__mark(fh)[c]   = false;
        fibheap__list_insert(fh, z, c);

        c = next;
    }

    // Any root left, z itself if it was alone
    int survivor = fibheap__right(fh)[z];
    fibheap__list_remove(fh, z);

    fibheap__lookup(fh)[fibheap__get_id(fibheap__elem(fh, z, es), id_off)] = -1;
    fibheap__free_list(fh)[fh->free_count++]                               = z;
    fh->size--;

    if (fh->size == 0)
    {
        fh->min = -1;
    }
    else
    {
        fh->min = survivor;
        fibheap__consolidate(fh, cmp, es);
    }

    return FIBHEAP_OK;
}

static inline int fibheap__decrease(fibheap_header_t* fh, const void* elem, fibheap_cmp cmp,
                                    size_t es, size_t id_off)
{
    assert(cmp == fh->cmp && es == fh->elem_size);

    int slot = fibheap__position(fh, fibheap__get_id(elem, id_off));
    if (slot < 0 || cmp(elem, fibheap__elem(fh, slot, es)) > 0)
    {
        return FIBHEAP_ERR;
    }

    memcpy(fibheap__elem(fh, slot, es), elem, es);

    int parent = fibheap__parent(fh)[slot];
    if (parent != -1 && cmp(fibheap__elem(fh, slot, es), fibheap__elem(fh, parent, es)) < 0)
    {
        fibheap__cut(fh, slot, parent);
        fibheap__cascading_cut(fh, parent);
    }

    if (cmp(fibheap__elem(fh, slot, es), fibheap__elem(fh, fh->min, es)) < 0)
    {
        fh->min = slot;
    }

    return FIBHEAP_OK;
}

#endif
