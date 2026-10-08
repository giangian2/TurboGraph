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
 *  field `id` of type int in [0, capacity), the order is given only by cmp
 *  (cmp(a, b) < 0 means a stays closer to the root), cmp, sizeof(T) and the
 *  offset of id are passed by the macros as constants so the compiler can
 *  inline them.
 *
 *  Elements never move, so the slot of an element is its id: h[id] is the
 *  element with that id, trees are made of ids (-1 = none).
 *
 *  Memory layout (single malloc, shadow header):
 *
 *    [ fibheap_header_t | T elems[capacity] | int parent[capacity] | child | left | right
 *      | degree | root_buf | bool mark[capacity] ]
 *                         ^-- pointer handed to the user (T*), the top is fibheap_top(h)
 *
 *  left/right are circular sibling lists, left[id] == -1 means id is not in
 *  the heap. child is any one of the sons, mark = lost a son since it became
 *  a son, root_buf is the consolidate snapshot.
 *  The arrays are found from capacity and elem_size, no pointers in the
 *  header: they would go stale if the block is moved.
 *
 *    typedef struct { double w; int id; } Node;
 *    Node* h = fibheap_create(n, node_cmp, Node);
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
    size_t      elem_size;
    int         min; // id of the top, -1 if empty
    fibheap_cmp cmp; // debug check only
} fibheap_header_t;

// Compile error if the two pointers have different types
#define FIBHEAP__CHECK_ID(T) ((void)sizeof(&((T*)0)->id - (FIB_HEAP_ID_TYPE*)0))
#define FIBHEAP__CHECK_PTR(h, p) ((void)sizeof((p) - (h)))
#define FIBHEAP__HDR(h) (((fibheap_header_t*)(void*)(h)) - 1)
// Offset in bytes of id inside T, computed from the pointer (nothing is read)
#define FIBHEAP__ID_OFF(h) ((size_t)((char*)&(h)->id - (char*)(h)))

#define fibheap_create(cap, cmp, T) (FIBHEAP__CHECK_ID(T), (T*)fibheap__create(cap, cmp, sizeof(T)))
#define fibheap_free(h) (fibheap__free(FIBHEAP__HDR(h)))
#define fibheap_size(h) (FIBHEAP__HDR(h)->size)
#define fibheap_empty(h) (FIBHEAP__HDR(h)->size == 0)
// Read only, NULL if empty
#define fibheap_top(h) (fibheap_empty(h) ? NULL : (h) + FIBHEAP__HDR(h)->min)
// Fails if id out of range or already in the heap
#define fibheap_insert(h, item, cmp)                                                               \
    (fibheap__insert(FIBHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),         \
                     FIBHEAP__ID_OFF(h)))
// out must be a T*, fails if empty
#define fibheap_pop(h, out, cmp)                                                                   \
    (FIBHEAP__CHECK_PTR(h, out), fibheap__pop(FIBHEAP__HDR(h), (out), (cmp), sizeof(*(h))))
// Only towards the root, fails if item.id is not in the heap or item would go down
#define fibheap_decrease(h, item, cmp)                                                             \
    (fibheap__decrease(FIBHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),       \
                       FIBHEAP__ID_OFF(h)))
#define fibheap_contains(h, id) (fibheap__contains(FIBHEAP__HDR(h), (id)))
// Read only: changing the key here breaks the heap, use decrease
#define fibheap_get(h, id) (fibheap_contains(h, id) ? (h) + (id) : NULL)

static inline char* fibheap__elem(fibheap_header_t* fh, int id, size_t es)
{
    return (char*)(fh + 1) + (size_t)id * es;
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
static inline int* fibheap__root_buf(fibheap_header_t* fh)
{
    return fibheap__ints(fh, 5);
}
static inline bool* fibheap__mark(fibheap_header_t* fh)
{
    return (bool*)fibheap__ints(fh, 6);
}

static inline FIB_HEAP_ID_TYPE fibheap__get_id(const void* elem, size_t id_off)
{
    return *(const FIB_HEAP_ID_TYPE*)((const char*)elem + id_off);
}

static inline bool fibheap__contains(fibheap_header_t* fh, FIB_HEAP_ID_TYPE id)
{
    return id >= 0 && (size_t)id < fh->capacity && fibheap__left(fh)[id] != -1;
}

static inline void* fibheap__create(size_t capacity, fibheap_cmp cmp, size_t elem_size)
{
    assert(cmp != NULL);

    fibheap_header_t* fh = (fibheap_header_t*)malloc(
        sizeof(fibheap_header_t) + capacity * (elem_size + 6 * sizeof(int) + sizeof(bool)));
    if (fh == NULL)
    {
        return NULL;
    }

    fh->capacity  = capacity;
    fh->size      = 0;
    fh->elem_size = elem_size;
    fh->min       = -1;
    fh->cmp       = cmp;

    // 0xFF bytes = -1: no id in the heap
    memset(fibheap__left(fh), 0xFF, capacity * sizeof(int));

    return fh + 1;
}

static inline void fibheap__free(fibheap_header_t* fh)
{
    free(fh);
}

// Puts the singleton x at the right of anchor
static inline void fibheap__list_insert(fibheap_header_t* fh, int anchor, int x)
{
    int* left  = fibheap__left(fh);
    int* right = fibheap__right(fh);

    left[x]             = anchor;
    right[x]            = right[anchor];
    left[right[anchor]] = x;
    right[anchor]       = x;
}

// Detaches x from its list and leaves it as a singleton
static inline void fibheap__list_remove(fibheap_header_t* fh, int x)
{
    int* left  = fibheap__left(fh);
    int* right = fibheap__right(fh);

    left[right[x]] = left[x];
    right[left[x]] = right[x];
    left[x]        = x;
    right[x]       = x;
}

// y becomes a son of x
static inline void fibheap__link(fibheap_header_t* fh, int y, int x)
{
    int* child = fibheap__child(fh);

    fibheap__list_remove(fh, y);
    fibheap__parent(fh)[y] = x;

    if (child[x] == -1)
    {
        child[x] = y;
    }
    else
    {
        fibheap__list_insert(fh, child[x], y);
    }

    fibheap__degree(fh)[x]++;
    fibheap__mark(fh)[y] = false;
}

// Moves x from the sons of p to the root list
static inline void fibheap__cut(fibheap_header_t* fh, int x, int p)
{
    int* child = fibheap__child(fh);
    int* right = fibheap__right(fh);

    if (child[p] == x)
    {
        child[p] = (right[x] == x) ? -1 : right[x];
    }

    fibheap__list_remove(fh, x);
    fibheap__degree(fh)[p]--;

    fibheap__parent(fh)[x] = -1;
    fibheap__mark(fh)[x]   = false;

    fibheap__list_insert(fh, fh->min, x);
}

// Climbs while the ancestors were already marked, cutting them
static inline void fibheap__cascading_cut(fibheap_header_t* fh, int x)
{
    int*  parent = fibheap__parent(fh);
    bool* mark   = fibheap__mark(fh);

    while (parent[x] != -1)
    {
        if (!mark[x])
        {
            mark[x] = true;
            break;
        }

        int p = parent[x];
        fibheap__cut(fh, x, p);
        x = p;
    }
}

// Merges the roots with the same degree, then finds the new min
static inline void fibheap__consolidate(fibheap_header_t* fh, fibheap_cmp cmp, size_t es)
{
    int* right    = fibheap__right(fh);
    int* degree   = fibheap__degree(fh);
    int* root_buf = fibheap__root_buf(fh);

    int degree_table[FIB_MAX_DEGREE];
    memset(degree_table, 0xFF, sizeof(degree_table));

    // Snapshot: link changes the root list while we walk it
    size_t root_count = 0;
    int    w          = fh->min;
    do
    {
        root_buf[root_count++] = w;
        w                      = right[w];
    } while (w != fh->min);

    for (size_t i = 0; i < root_count; i++)
    {
        int x = root_buf[i];
        int d = degree[x];

        while (degree_table[d] != -1)
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
            d               = degree[x];
        }

        assert(d < FIB_MAX_DEGREE);
        degree_table[d] = x;
    }

    // The survivors are the roots left in the table
    fh->min = -1;
    for (int d = 0; d < FIB_MAX_DEGREE; d++)
    {
        int x = degree_table[d];
        if (x != -1 &&
            (fh->min == -1 || cmp(fibheap__elem(fh, x, es), fibheap__elem(fh, fh->min, es)) < 0))
        {
            fh->min = x;
        }
    }
}

static inline int fibheap__insert(fibheap_header_t* fh, const void* elem, fibheap_cmp cmp,
                                  size_t es, size_t id_off)
{
    assert(cmp == fh->cmp && es == fh->elem_size);

    FIB_HEAP_ID_TYPE x = fibheap__get_id(elem, id_off);
    if (x < 0 || (size_t)x >= fh->capacity || fibheap__left(fh)[x] != -1)
    {
        return FIBHEAP_ERR;
    }

    memcpy(fibheap__elem(fh, x, es), elem, es);
    fibheap__parent(fh)[x] = -1;
    fibheap__child(fh)[x]  = -1;
    fibheap__degree(fh)[x] = 0;
    fibheap__mark(fh)[x]   = false;
    fibheap__left(fh)[x]   = x;
    fibheap__right(fh)[x]  = x;

    if (fh->min == -1)
    {
        fh->min = x;
    }
    else
    {
        fibheap__list_insert(fh, fh->min, x);
        if (cmp(fibheap__elem(fh, x, es), fibheap__elem(fh, fh->min, es)) < 0)
        {
            fh->min = x;
        }
    }

    fh->size++;

    return FIBHEAP_OK;
}

static inline int fibheap__pop(fibheap_header_t* fh, void* out, fibheap_cmp cmp, size_t es)
{
    assert(cmp == fh->cmp && es == fh->elem_size);

    if (fh->size == 0)
    {
        return FIBHEAP_ERR;
    }

    int* parent = fibheap__parent(fh);
    int* right  = fibheap__right(fh);
    int  z      = fh->min;

    memcpy(out, fibheap__elem(fh, z, es), es);

    // Sons of z become roots
    int sons = fibheap__degree(fh)[z];
    int c    = fibheap__child(fh)[z];
    for (int i = 0; i < sons; i++)
    {
        int next = right[c];

        fibheap__list_remove(fh, c);
        parent[c]            = -1;
        fibheap__mark(fh)[c] = false;
        fibheap__list_insert(fh, z, c);

        c = next;
    }

    // Any root left, z itself if it was alone
    int survivor = right[z];
    fibheap__list_remove(fh, z);
    fibheap__left(fh)[z] = -1;
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

    FIB_HEAP_ID_TYPE x = fibheap__get_id(elem, id_off);
    if (!fibheap__contains(fh, x) || cmp(elem, fibheap__elem(fh, x, es)) > 0)
    {
        return FIBHEAP_ERR;
    }

    memcpy(fibheap__elem(fh, x, es), elem, es);

    int p = fibheap__parent(fh)[x];
    if (p != -1 && cmp(fibheap__elem(fh, x, es), fibheap__elem(fh, p, es)) < 0)
    {
        fibheap__cut(fh, x, p);
        fibheap__cascading_cut(fh, p);
    }

    if (cmp(fibheap__elem(fh, x, es), fibheap__elem(fh, fh->min, es)) < 0)
    {
        fh->min = x;
    }

    return FIBHEAP_OK;
}

#endif
