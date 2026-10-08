#ifndef D_HEAP_H
#define D_HEAP_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define D_HEAP_ID_TYPE int
#define DHEAP_DEFAULT_D 4 // best or close to it on the road graphs benchmarks

/**
 *  Generic d-ary heap, T must be a struct with a field `id` of type int:
 *  ids are used to index the lookup array, so they must be in [0, n_ids).
 *  The order is given only by cmp: cmp(a, b) < 0 means a stays closer to
 *  the root than b, so the same code works as min-heap or max-heap.
 *
 *  Memory layout (single malloc, shadow header):
 *
 *    [ dheap_header_t | T elems[capacity] | T scratch | int lookup[n_ids] ]
 *                       ^-- pointer handed to the user (T*), h[0] is the top
 *
 *  lookup[id] = heap position of id, -1 if not in the heap.
 *  scratch is the temporary of the swap.
 *
 *  cmp, sizeof(T) and the offset of id are passed by the macros to every
 *  operation as constants: once inlined the compiler inlines cmp and the
 *  memcpys too.
 *  cmp must always be the same given to dheap_create (checked in debug).
 *
 *    typedef struct { double w; int id; } Node;
 *    Node* h = dheap_create(n, DHEAP_DEFAULT_D, n, node_cmp, Node);
 *    dheap_insert(h, ((Node){ 3.5, 7 }), node_cmp);
 *    dheap_update(h, ((Node){ 1.0, 7 }), node_cmp);
 *    Node top;
 *    dheap_pop(h, &top, node_cmp);
 *    dheap_free(h);
 */

typedef int (*dheap_cmp)(const void* a, const void* b);

typedef enum
{
    DHEAP_OK  = 0,
    DHEAP_ERR = -1
} DHeapStatus;

typedef struct
{
    size_t    capacity;
    size_t    size;
    size_t    d; // branching factor
    size_t    elem_size;
    size_t    n_ids; // length of lookup
    dheap_cmp cmp;   // debug check only
} dheap_header_t;

// Compile error if the two pointers have different types
#define DHEAP__CHECK_ID(T) ((void)sizeof(&((T*)0)->id - (D_HEAP_ID_TYPE*)0))
#define DHEAP__CHECK_PTR(h, p) ((void)sizeof((p) - (h)))
#define DHEAP__HDR(h) (((dheap_header_t*)(void*)(h)) - 1)
// Offset in bytes of id inside T, computed from the pointer (nothing is read)
#define DHEAP__ID_OFF(h) ((size_t)((char*)&(h)->id - (char*)(h)))

#define dheap_create(cap, d, n_ids, cmp, T)                                                        \
    (DHEAP__CHECK_ID(T), (T*)dheap__create(cap, d, n_ids, cmp, sizeof(T)))
#define dheap_free(h) (dheap__free(DHEAP__HDR(h)))
#define dheap_size(h) (DHEAP__HDR(h)->size)
#define dheap_empty(h) (DHEAP__HDR(h)->size == 0)
// Fails if full, id out of range or already in the heap
#define dheap_insert(h, item, cmp)                                                                 \
    (dheap__insert(DHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),             \
                   DHEAP__ID_OFF(h)))
// out must be a T*, fails if empty
#define dheap_pop(h, out, cmp)                                                                     \
    (DHEAP__CHECK_PTR(h, out),                                                                     \
     dheap__pop(DHEAP__HDR(h), (out), (cmp), sizeof(*(h)), DHEAP__ID_OFF(h)))
// Decrease and increase key, fails if item.id is not in the heap
#define dheap_update(h, item, cmp)                                                                 \
    (dheap__update(DHEAP__HDR(h), &((__typeof__(*(h))[1]){item}), (cmp), sizeof(*(h)),             \
                   DHEAP__ID_OFF(h)))
#define dheap_contains(h, id) (dheap__position(DHEAP__HDR(h), (id)) >= 0)
// Read only: changing the key here breaks the heap, use update
#define dheap_get(h, id) (dheap_contains(h, id) ? (h) + dheap__position(DHEAP__HDR(h), (id)) : NULL)

static inline void* dheap__values(dheap_header_t* dheap_h)
{
    return (void*)(dheap_h + 1);
}

static inline void* dheap__scratch(dheap_header_t* dheap_h)
{
    return (void*)((char*)(dheap_h + 1) + dheap_h->capacity * dheap_h->elem_size);
}

static inline D_HEAP_ID_TYPE* dheap__lookup(dheap_header_t* dheap_h)
{
    return (D_HEAP_ID_TYPE*)((char*)(dheap_h + 1) + (dheap_h->capacity + 1) * dheap_h->elem_size);
}

static inline D_HEAP_ID_TYPE dheap__get_id(const void* elem, size_t id_off)
{
    return *(const D_HEAP_ID_TYPE*)((const char*)elem + id_off);
}

static inline size_t dheap__parent(const dheap_header_t* dheap_h, size_t i)
{
    assert(i > 0);
    return (i - 1) / dheap_h->d;
}

static inline size_t dheap__start_son(const dheap_header_t* dheap_h, size_t i)
{
    return (dheap_h->d * i) + 1;
}

static inline void dheap__swap(dheap_header_t* dheap_h, size_t a_indx, size_t b_indx, size_t es,
                               size_t id_off)
{
    assert(a_indx < dheap_h->size && b_indx < dheap_h->size);

    char*           data   = (char*)dheap__values(dheap_h);
    D_HEAP_ID_TYPE* lookup = dheap__lookup(dheap_h);
    void*           tmp    = dheap__scratch(dheap_h);
    char*           a      = data + a_indx * es;
    char*           b      = data + b_indx * es;

    memcpy(tmp, a, es);
    memcpy(a, b, es);
    memcpy(b, tmp, es);

    lookup[dheap__get_id(a, id_off)] = (D_HEAP_ID_TYPE)a_indx;
    lookup[dheap__get_id(b, id_off)] = (D_HEAP_ID_TYPE)b_indx;
}

static inline void* dheap__create(size_t capacity, size_t branching_factor, size_t n_ids,
                                  dheap_cmp cmp, size_t elem_size)
{
    assert(branching_factor >= 1 && cmp != NULL);

    dheap_header_t* dheap = (dheap_header_t*)malloc(
        sizeof(dheap_header_t) + (capacity + 1) * elem_size + n_ids * sizeof(D_HEAP_ID_TYPE));
    if (dheap == NULL)
    {
        return NULL;
    }

    dheap->capacity  = capacity;
    dheap->size      = 0;
    dheap->d         = branching_factor;
    dheap->elem_size = elem_size;
    dheap->n_ids     = n_ids;
    dheap->cmp       = cmp;

    // 0xFF bytes = -1: no id in the heap
    memset(dheap__lookup(dheap), 0xFF, n_ids * sizeof(D_HEAP_ID_TYPE));

    return dheap + 1;
}

static inline void dheap__free(dheap_header_t* h)
{
    free(h);
}

static inline size_t dheap__move_down(dheap_header_t* h, size_t i, dheap_cmp cmp, size_t es,
                                      size_t id_off)
{
    assert(i < h->size);
    assert(cmp == h->cmp && es == h->elem_size);

    char* data = (char*)dheap__values(h);

    while (true)
    {
        size_t first = dheap__start_son(h, i);
        if (first >= h->size)
        {
            break;
        }

        size_t last = first + h->d;
        if (last > h->size)
        {
            last = h->size;
        }

        // Swap with the best son, not with the first that beats i
        size_t best = first;
        for (size_t c = first + 1; c < last; c++)
        {
            if (cmp(data + c * es, data + best * es) < 0)
            {
                best = c;
            }
        }

        if (cmp(data + best * es, data + i * es) >= 0)
        {
            break;
        }

        dheap__swap(h, i, best, es, id_off);
        i = best;
    }

    return i;
}

static inline size_t dheap__move_up(dheap_header_t* h, size_t i, dheap_cmp cmp, size_t es,
                                    size_t id_off)
{
    assert(i < h->size);
    assert(cmp == h->cmp && es == h->elem_size);

    char* data = (char*)dheap__values(h);

    while (i > 0)
    {
        size_t p = dheap__parent(h, i);
        if (cmp(data + p * es, data + i * es) <= 0)
        {
            break;
        }

        dheap__swap(h, i, p, es, id_off);
        i = p;
    }

    return i;
}

static inline D_HEAP_ID_TYPE dheap__position(dheap_header_t* h, D_HEAP_ID_TYPE id)
{
    if (id < 0 || (size_t)id >= h->n_ids)
    {
        return -1;
    }

    return dheap__lookup(h)[id];
}

static inline int dheap__insert(dheap_header_t* h, const void* elem, dheap_cmp cmp, size_t es,
                                size_t id_off)
{
    D_HEAP_ID_TYPE  id     = dheap__get_id(elem, id_off);
    D_HEAP_ID_TYPE* lookup = dheap__lookup(h);

    if (h->size >= h->capacity || id < 0 || (size_t)id >= h->n_ids || lookup[id] != -1)
    {
        return DHEAP_ERR;
    }

    size_t i = h->size++;
    memcpy((char*)dheap__values(h) + i * es, elem, es);
    lookup[id] = (D_HEAP_ID_TYPE)i;

    dheap__move_up(h, i, cmp, es, id_off);

    return DHEAP_OK;
}

static inline int dheap__pop(dheap_header_t* h, void* out, dheap_cmp cmp, size_t es, size_t id_off)
{
    if (h->size == 0)
    {
        return DHEAP_ERR;
    }

    char*           data   = (char*)dheap__values(h);
    D_HEAP_ID_TYPE* lookup = dheap__lookup(h);

    memcpy(out, data, es);
    lookup[dheap__get_id(data, id_off)] = -1;
    h->size--;

    // Last leaf goes to the root and sinks
    if (h->size > 0)
    {
        memcpy(data, data + h->size * es, es);
        lookup[dheap__get_id(data, id_off)] = 0;

        dheap__move_down(h, 0, cmp, es, id_off);
    }

    return DHEAP_OK;
}

static inline int dheap__update(dheap_header_t* h, const void* elem, dheap_cmp cmp, size_t es,
                                size_t id_off)
{
    D_HEAP_ID_TYPE pos = dheap__position(h, dheap__get_id(elem, id_off));
    if (pos < 0)
    {
        return DHEAP_ERR;
    }

    memcpy((char*)dheap__values(h) + (size_t)pos * es, elem, es);

    // The new key can move it both ways
    if (dheap__move_up(h, (size_t)pos, cmp, es, id_off) == (size_t)pos)
    {
        dheap__move_down(h, (size_t)pos, cmp, es, id_off);
    }

    return DHEAP_OK;
}

#endif
