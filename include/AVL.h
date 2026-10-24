#ifndef AVL_H
#define AVL_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 *  Generic AVL tree with fixed capacity (no realloc), same rules of DHeap.h:
 *  T is any struct, the order is given only by cmp (cmp(a, b) < 0 means a
 *  comes before b), duplicates are rejected. cmp and sizeof(T) are passed by
 *  the macros as constants.
 *
 *  Searches take a match function instead of cmp: match(key, elem) tells
 *  where key is with respect to elem (< 0 left, 0 found, > 0 right), key is
 *  any pointer the match understands (a T, a bare field, a range...). The
 *  only rule is that match must agree with the order of cmp. cmp itself is a
 *  valid match when key is a T*.
 *
 *  Elements never move: a node is a slot, the tree is made of slot indices
 *  (-1 = none), so a T* returned by a search stays valid until that element
 *  is removed. A two sons removal relinks the successor, it does not copy it.
 *
 *  No recursion: insert and remove walk down pushing an avl_frame_t per
 *  node (the link holding it) on a fixed stack of AVL_MAX_HEIGHT frames,
 *  then climb back popping them, like the DFS in traversal.c. Every
 *  operation taking cmp or match is always_inline, so the pointer is a
 *  constant at the call site and the comparison is inlined.
 *
 *  Memory layout (single malloc, shadow header):
 *
 *    [ avl_header_t | T elems[capacity] | pad | int left[capacity] | right | height ]
 *                     ^-- pointer handed to the user (T*), not ordered
 *
 *  height[i] == 0 means slot i is free, free slots are chained through
 *  left[] starting from free_head. pad rounds elems up to an int boundary.
 *  The arrays are found from capacity and elems_bytes, no pointers in the
 *  header: they would go stale if the block is moved.
 *
 *    typedef struct { int id; double w; } Node;
 *    Node* t = avl_create(n, node_cmp, Node);        // ordered by id
 *    avl_insert(t, ((Node){ 7, 3.5 }), node_cmp);
 *    int key = 7;
 *    Node* p = avl_find(t, &key, match_id);          // match_id(key, elem) compares an int
 *    for (Node* it = avl_min(t); it != NULL; it = avl_next(t, it, node_cmp)) ...
 *    Node out;
 *    avl_remove(t, &key, match_id, &out);
 *    avl_free(t);
 */

typedef int (*avl_cmp)(const void* a, const void* b);
typedef int (*avl_match)(const void* key, const void* elem);

typedef enum
{
    AVL_OK  = 0,
    AVL_ERR = -1, // full, or nothing matched
    AVL_DUP = 1   // an element equal to the item is already in the tree
} AvlStatus;

typedef struct
{
    size_t  capacity;
    size_t  size;
    size_t  elem_size;
    size_t  elems_bytes; // capacity * elem_size rounded up to sizeof(int)
    int     root;        // -1 if empty
    int     free_head;   // recycled slots, -1 if none
    int     unused;      // first slot never handed out
    avl_cmp cmp;         // debug check only
} avl_header_t;

// A path is at most ~1.44 log2(n) long: 45 for INT_MAX nodes, the slots are int
#define AVL_MAX_HEIGHT 48

/* One frame per node on the descent path: the link (root or a son slot) that
 * holds the node. It is what the recursive version kept in its activation
 * record: on the climb the rebalanced subtree is hung back through it. */
typedef struct
{
    int* link;
} avl_frame_t;

// Fixed capacity, lives on the C stack of the operation: no malloc
typedef struct
{
    avl_frame_t frames[AVL_MAX_HEIGHT];
    int         top;
} avl_frame_stack_t;

// Compile error if the two pointers have different types
#define AVL__CHECK_PTR(t, p) ((void)sizeof((p) - (t)))
#define AVL__HDR(t) (((avl_header_t*)(void*)(t)) - 1)
// The operations taking cmp or match: once inlined the function pointer is a
// constant at the call site and the compiler inlines it too
#define AVL__FORCE_INLINE static inline __attribute__((always_inline))

#define avl_create(cap, cmp, T) ((T*)avl__create(cap, cmp, sizeof(T)))
#define avl_free(t) (avl__free(AVL__HDR(t)))
#define avl_size(t) (AVL__HDR(t)->size)
#define avl_empty(t) (AVL__HDR(t)->size == 0)
#define avl_full(t) (AVL__HDR(t)->size == AVL__HDR(t)->capacity)
#define avl_clear(t) (avl__clear(AVL__HDR(t)))
// Fails if full (AVL_ERR) or if an equal element is there (AVL_DUP, untouched)
#define avl_insert(t, item, cmp)                                                                   \
    (avl__insert(AVL__HDR(t), &((__typeof__(*(t))[1]){item}), (cmp), sizeof(*(t))))
// Any element with match(key, elem) == 0, NULL if none. Read only: changing
// the fields cmp looks at breaks the tree, remove and insert again
#define avl_find(t, key, match)                                                                    \
    ((__typeof__(t))avl__find(AVL__HDR(t), (key), (match), sizeof(*(t))))
// First element with match(key, elem) <= 0, i.e. not before key, NULL if none
#define avl_lower_bound(t, key, match)                                                             \
    ((__typeof__(t))avl__lower_bound(AVL__HDR(t), (key), (match), sizeof(*(t))))
// out must be a T*, removes the element avl_find would return, fails if none
#define avl_remove(t, key, match, out)                                                             \
    (AVL__CHECK_PTR(t, out), avl__remove(AVL__HDR(t), (key), (match), (out), sizeof(*(t))))
#define avl_min(t) ((__typeof__(t))avl__extreme(AVL__HDR(t), 0, sizeof(*(t))))
#define avl_max(t) ((__typeof__(t))avl__extreme(AVL__HDR(t), 1, sizeof(*(t))))
// In order successor of e (any T, not only one in the tree), NULL if none
#define avl_next(t, e, cmp)                                                                        \
    (AVL__CHECK_PTR(t, e), (__typeof__(t))avl__next(AVL__HDR(t), (e), (cmp), sizeof(*(t))))

static inline char* avl__elem(avl_header_t* th, int i, size_t es)
{
    return (char*)(th + 1) + (size_t)i * es;
}

// Start of the int arrays, they follow elems[capacity] in this order
static inline int* avl__ints(avl_header_t* th, size_t k)
{
    return (int*)((char*)(th + 1) + th->elems_bytes) + k * th->capacity;
}

static inline int* avl__left(avl_header_t* th)
{
    return avl__ints(th, 0);
}
static inline int* avl__right(avl_header_t* th)
{
    return avl__ints(th, 1);
}
static inline int* avl__height(avl_header_t* th)
{
    return avl__ints(th, 2);
}

static inline void avl__clear(avl_header_t* th)
{
    th->size      = 0;
    th->root      = -1;
    th->free_head = -1;
    th->unused    = 0;
}

static inline void* avl__create(size_t capacity, avl_cmp cmp, size_t elem_size)
{
    assert(cmp != NULL);

    size_t elems_bytes = capacity * elem_size;
    elems_bytes        = (elems_bytes + sizeof(int) - 1) / sizeof(int) * sizeof(int);

    avl_header_t* th =
        (avl_header_t*)malloc(sizeof(avl_header_t) + elems_bytes + capacity * 3 * sizeof(int));
    if (th == NULL)
    {
        return NULL;
    }

    th->capacity    = capacity;
    th->elem_size   = elem_size;
    th->elems_bytes = elems_bytes;
    th->cmp         = cmp;
    avl__clear(th);

    return th + 1;
}

static inline void avl__free(avl_header_t* th)
{
    free(th);
}

// Free slot, -1 if full: recycled ones first, then the never used ones
static inline int avl__alloc(avl_header_t* th)
{
    if (th->free_head != -1)
    {
        int x         = th->free_head;
        th->free_head = avl__left(th)[x];
        return x;
    }
    if ((size_t)th->unused < th->capacity)
    {
        return th->unused++;
    }
    return -1;
}

static inline void avl__release(avl_header_t* th, int x)
{
    avl__height(th)[x] = 0;
    avl__left(th)[x]   = th->free_head;
    th->free_head      = x;
}

static inline int avl__h(avl_header_t* th, int n)
{
    return n == -1 ? 0 : avl__height(th)[n];
}

static inline void avl__update(avl_header_t* th, int n)
{
    int hl = avl__h(th, avl__left(th)[n]);
    int hr = avl__h(th, avl__right(th)[n]);

    avl__height(th)[n] = 1 + (hl > hr ? hl : hr);
}

// Both return the new root of the subtree
static inline int avl__rotate_right(avl_header_t* th, int n)
{
    int* left  = avl__left(th);
    int* right = avl__right(th);
    int  l     = left[n];

    left[n]  = right[l];
    right[l] = n;
    avl__update(th, n);
    avl__update(th, l);

    return l;
}

static inline int avl__rotate_left(avl_header_t* th, int n)
{
    int* left  = avl__left(th);
    int* right = avl__right(th);
    int  r     = right[n];

    right[n] = left[r];
    left[r]  = n;
    avl__update(th, n);
    avl__update(th, r);

    return r;
}

// Fixes the height of n and restores |balance| <= 1, returns the new root of the subtree
static inline int avl__rebalance(avl_header_t* th, int n)
{
    int* left  = avl__left(th);
    int* right = avl__right(th);

    avl__update(th, n);
    int balance = avl__h(th, left[n]) - avl__h(th, right[n]);

    if (balance > 1)
    {
        // Left-right case: the heavy side of the son is inner, straighten it first
        if (avl__h(th, left[left[n]]) < avl__h(th, right[left[n]]))
        {
            left[n] = avl__rotate_left(th, left[n]);
        }
        return avl__rotate_right(th, n);
    }
    if (balance < -1)
    {
        if (avl__h(th, right[right[n]]) < avl__h(th, left[right[n]]))
        {
            right[n] = avl__rotate_right(th, right[n]);
        }
        return avl__rotate_left(th, n);
    }

    return n;
}

static inline void avl__frame_push(avl_frame_stack_t* path, int* link)
{
    assert(path->top < AVL_MAX_HEIGHT);
    path->frames[path->top++] = (avl_frame_t){link};
}

static inline avl_frame_t avl__frame_pop(avl_frame_stack_t* path)
{
    return path->frames[--path->top];
}

/* Climbs the path bottom up rebalancing every node. Stops as soon as a
 * subtree keeps the height it had: the ancestors only see that height, so
 * nothing above can have changed. */
static inline void avl__retrace(avl_header_t* th, avl_frame_stack_t* path)
{
    int* height = avl__height(th);

    while (path->top > 0)
    {
        int* link  = avl__frame_pop(path).link;
        int  old_h = height[*link];

        *link = avl__rebalance(th, *link);
        if (height[*link] == old_h)
        {
            break;
        }
    }
}

AVL__FORCE_INLINE void* avl__find(avl_header_t* th, const void* key, avl_match match, size_t es)
{
    assert(match != NULL && es == th->elem_size);

    int n = th->root;
    while (n != -1)
    {
        int c = match(key, avl__elem(th, n, es));
        if (c == 0)
        {
            return avl__elem(th, n, es);
        }
        n = c < 0 ? avl__left(th)[n] : avl__right(th)[n];
    }

    return NULL;
}

AVL__FORCE_INLINE void* avl__lower_bound(avl_header_t* th, const void* key, avl_match match,
                                         size_t es)
{
    assert(match != NULL && es == th->elem_size);

    int best = -1;
    int n    = th->root;
    while (n != -1)
    {
        // n is a candidate, a better one can only be on its left
        if (match(key, avl__elem(th, n, es)) <= 0)
        {
            best = n;
            n    = avl__left(th)[n];
        }
        else
        {
            n = avl__right(th)[n];
        }
    }

    return best == -1 ? NULL : avl__elem(th, best, es);
}

// right_side = 0 leftmost (min), 1 rightmost (max)
static inline void* avl__extreme(avl_header_t* th, int right_side, size_t es)
{
    assert(es == th->elem_size);

    if (th->root == -1)
    {
        return NULL;
    }

    int* side = right_side ? avl__right(th) : avl__left(th);
    int  n    = th->root;
    while (side[n] != -1)
    {
        n = side[n];
    }

    return avl__elem(th, n, es);
}

AVL__FORCE_INLINE void* avl__next(avl_header_t* th, const void* elem, avl_cmp cmp, size_t es)
{
    assert(cmp == th->cmp && es == th->elem_size);

    int best = -1;
    int n    = th->root;
    while (n != -1)
    {
        if (cmp(elem, avl__elem(th, n, es)) < 0)
        {
            best = n;
            n    = avl__left(th)[n];
        }
        else
        {
            n = avl__right(th)[n];
        }
    }

    return best == -1 ? NULL : avl__elem(th, best, es);
}

AVL__FORCE_INLINE int avl__insert(avl_header_t* th, const void* elem, avl_cmp cmp, size_t es)
{
    assert(cmp == th->cmp && es == th->elem_size);

    int* left  = avl__left(th);
    int* right = avl__right(th);

    avl_frame_stack_t path;
    path.top = 0;

    // Descent: a frame per node, the duplicate check comes before any slot is taken
    int* link = &th->root;
    while (*link != -1)
    {
        int c = cmp(elem, avl__elem(th, *link, es));
        if (c == 0)
        {
            return AVL_DUP;
        }

        avl__frame_push(&path, link);
        link = c < 0 ? &left[*link] : &right[*link];
    }

    int x = avl__alloc(th);
    if (x == -1)
    {
        return AVL_ERR;
    }

    memcpy(avl__elem(th, x, es), elem, es);
    left[x]            = -1;
    right[x]           = -1;
    avl__height(th)[x] = 1;

    *link = x;
    th->size++;

    avl__retrace(th, &path);

    return AVL_OK;
}

AVL__FORCE_INLINE int avl__remove(avl_header_t* th, const void* key, avl_match match, void* out,
                                  size_t es)
{
    assert(match != NULL && es == th->elem_size);

    int* left   = avl__left(th);
    int* right  = avl__right(th);
    int* height = avl__height(th);

    avl_frame_stack_t path;
    path.top = 0;

    int* link = &th->root;
    while (*link != -1)
    {
        int c = match(key, avl__elem(th, *link, es));
        if (c == 0)
        {
            break;
        }

        avl__frame_push(&path, link);
        link = c < 0 ? &left[*link] : &right[*link];
    }

    if (*link == -1)
    {
        return AVL_ERR;
    }

    int x = *link;

    if (left[x] == -1 || right[x] == -1)
    {
        // At most one son: it is already balanced, it takes the place of x
        *link = left[x] != -1 ? left[x] : right[x];
    }
    else
    {
        // Two sons: the successor s takes the place of x, relinked so no element moves
        avl__frame_push(&path, link);
        int base = path.top;

        int* s_link = &right[x];
        while (left[*s_link] != -1)
        {
            avl__frame_push(&path, s_link);
            s_link = &left[*s_link];
        }

        int s   = *s_link;
        *s_link = right[s]; // detach s, when s == right[x] this already is right[x]

        left[s]   = left[x];
        right[s]  = right[x];
        height[s] = height[x]; // the old height of that position, for retrace
        *link     = s;

        // The first frame below s went through right[x], x is gone: now it is right[s]
        if (path.top > base)
        {
            path.frames[base].link = &right[s];
        }
    }

    memcpy(out, avl__elem(th, x, es), es);
    avl__release(th, x);
    th->size--;

    avl__retrace(th, &path);

    return AVL_OK;
}

#endif
