#include "../include/FibHeap.h"
#include <stdlib.h>

/* Upper bound on a node's degree: a node of degree k roots a subtree of at
 * least F(k+2) nodes (Fibonacci numbers), and F(94) already exceeds SIZE_MAX
 * on a 64-bit machine, so no reachable heap size ever produces degree >= 92.
 * A fixed-size stack array for the consolidate pass is therefore both safe
 * and simpler than a reallocated one. */
#define FIB_MAX_DEGREE 92

/* Forward declarations: the static helpers are implemented further down the
 * file (after the public entry points), so they need prototypes here to be
 * usable by fibInsert/fibExtractMin/fibDecreaseKey above. */
static int  expandPos(FibHeap* h, size_t neededIndex);
static int  expandArena(FibHeap* h);
static int  ensureRootBuf(FibHeap* h, size_t needed);
static int  allocNode(FibHeap* h);
static int  releaseNode(FibHeap* h, int idx);
static void listInsert(FibHeap* h, int anchor, int idx);
static void listRemove(FibHeap* h, int idx);
static void addToRootList(FibHeap* h, int idx);
static void link(FibHeap* h, int y, int x);
static void cut(FibHeap* h, int idx, int parent);
static void cascadingCut(FibHeap* h, int idx);
static void consolidate(FibHeap* h);
static void findMinInRootList(FibHeap* h);

/* Allocates the heap struct and its backing arrays; frees whichever
 * allocations succeeded if another one fails, so no leak on the
 * partial-failure path. pos[] starts out the same size as the node arena
 * and is initialized to -1 ("key not in heap") everywhere. */
FibHeap* createFibHeap(size_t capacity)
{
    FibHeap* h      = (FibHeap*)malloc(sizeof(FibHeap));
    int*     key    = (int*)malloc(sizeof(int) * capacity);
    int*     parent = (int*)malloc(sizeof(int) * capacity);
    int*     child  = (int*)malloc(sizeof(int) * capacity);
    int*     left   = (int*)malloc(sizeof(int) * capacity);
    int*     right  = (int*)malloc(sizeof(int) * capacity);
    int*     degree = (int*)malloc(sizeof(int) * capacity);
    bool*    mark   = (bool*)malloc(sizeof(bool) * capacity);
    int*     pos    = (int*)malloc(sizeof(int) * capacity);

    if (h == NULL || key == NULL || parent == NULL || child == NULL || left == NULL ||
        right == NULL || degree == NULL || mark == NULL || pos == NULL)
    {
        if (h)
        {
            free(h);
        }

        if (key)
        {
            free(key);
        }

        if (parent)
        {
            free(parent);
        }

        if (child)
        {
            free(child);
        }

        if (left)
        {
            free(left);
        }

        if (right)
        {
            free(right);
        }

        if (degree)
        {
            free(degree);
        }

        if (mark)
        {
            free(mark);
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

    h->key    = key;
    h->parent = parent;
    h->child  = child;
    h->left   = left;
    h->right  = right;
    h->degree = degree;
    h->mark   = mark;

    h->capacity = capacity;
    h->used     = 0;
    h->size     = 0;
    h->min      = -1;

    h->freeList     = NULL;
    h->freeCount    = 0;
    h->freeCapacity = 0;

    h->pos         = pos;
    h->posCapacity = capacity;

    h->rootBuf         = NULL;
    h->rootBufCapacity = 0;

    return h;
}

/* O(1) amortized: allocate a node, splice it into the root list as a
 * singleton tree, and update the min pointer if needed. */
int fibInsert(FibHeap* h, int newKey)
{
    if (h == NULL || newKey < 0)
    {
        return FIBHEAP_ERR;
    }

    if (expandPos(h, (size_t)newKey) != FIBHEAP_OK)
    {
        return FIBHEAP_ERR;
    }

    if (h->pos[newKey] != -1)
    {
        return FIBHEAP_ERR; // duplicate keys are not supported
    }

    int idx = allocNode(h);
    if (idx == -1)
    {
        return FIBHEAP_ERR; // arena expansion failed
    }

    h->key[idx]    = newKey;
    h->parent[idx] = -1;
    h->child[idx]  = -1;
    h->degree[idx] = 0;
    h->mark[idx]   = false;
    h->left[idx]   = idx; // singleton circular list
    h->right[idx]  = idx;

    addToRootList(h, idx);

    if (h->min == -1 || h->key[idx] < h->key[h->min])
    {
        h->min = idx;
    }

    h->pos[newKey] = idx;
    h->size++;
    return FIBHEAP_OK;
}

/* O(log n) amortized: detach the min root, promote its children to roots,
 * then consolidate the root list so at most one tree of each degree
 * survives -- this is what keeps the amortized cost logarithmic despite
 * insert/decreaseKey doing O(1) worth of work each. */
int fibExtractMin(FibHeap* h)
{
    if (h == NULL || h->size == 0)
    {
        return FIBHEAP_ERR;
    }

    int z      = h->min;
    int minKey = h->key[z];

    int numChildren = h->degree[z];
    int c           = h->child[z];
    for (int i = 0; i < numChildren; i++)
    {
        int next = h->right[c]; // save before mutating c's links below
        listRemove(h, c);
        h->parent[c] = -1;
        h->mark[c]   = false; // roots are never marked
        listInsert(h, z, c);
        c = next;
    }
    h->child[z]  = -1;
    h->degree[z] = 0;

    int survivor = h->right[z]; // any node still in the root list, or z itself if it was alone
    listRemove(h, z);

    h->pos[minKey] = -1;
    releaseNode(h, z); // best-effort: on failure the slot just isn't recycled
    h->size--;

    if (h->size == 0)
    {
        h->min = -1;
    }
    else
    {
        h->min = survivor;
        consolidate(h);
    }

    return minKey;
}

/* O(1) amortized: decreasing a key never violates heap order at the root,
 * only possibly against its parent, so at most one cut plus a cascade of
 * marked-ancestor cuts is needed -- no re-sift through the whole tree. */
int fibDecreaseKey(FibHeap* h, int key, int delta)
{
    if (h == NULL || key < 0 || delta < 0)
    {
        return FIBHEAP_ERR;
    }

    if ((size_t)key >= h->posCapacity || h->pos[key] == -1)
    {
        return FIBHEAP_ERR; // key is not currently in the heap
    }

    int newKey = key - delta;
    if (newKey < 0)
    {
        return FIBHEAP_ERR;
    }

    if (newKey != key && h->pos[newKey] != -1)
    {
        return FIBHEAP_ERR; // newKey would collide with another key already in the heap
    }

    int idx = h->pos[key];

    // Same identity relabeling as DHeap::decreaseKey: retire the old slot
    // and claim the decreased key's slot before touching tree structure.
    h->pos[key]    = -1;
    h->key[idx]    = newKey;
    h->pos[newKey] = idx;

    int parent = h->parent[idx];
    if (parent != -1 && h->key[idx] < h->key[parent])
    {
        cut(h, idx, parent);
        cascadingCut(h, parent);
    }

    if (h->key[idx] < h->key[h->min])
    {
        h->min = idx;
    }

    return FIBHEAP_OK;
}

void freeFibHeap(FibHeap* h)
{
    if (h == NULL)
    {
        return;
    }

    free(h->key);
    free(h->parent);
    free(h->child);
    free(h->left);
    free(h->right);
    free(h->degree);
    free(h->mark);
    free(h->freeList);
    free(h->pos);
    free(h->rootBuf);
    free(h);
}

/* Grows pos[] (doubling) until it can index `neededIndex`, initializing
 * newly added slots to -1 ("key not in heap"). Independent from
 * expandArena: the node arena grows with the number of nodes actually
 * live, pos[] grows with the largest key value seen so far -- the two
 * capacities can diverge. Concretely, keys here are node ids, so pos[]
 * tracks the id space (it must be able to index pos[maxIdInserted]), not
 * how many nodes happen to be live at once -- a heap holding one node
 * with id 10000 still needs pos[] to reach index 10000. */
static int expandPos(FibHeap* h, size_t neededIndex)
{
    if (neededIndex < h->posCapacity)
    {
        return FIBHEAP_OK;
    }

    size_t newPosCapacity = h->posCapacity == 0 ? 1 : h->posCapacity;
    while (newPosCapacity <= neededIndex)
    {
        newPosCapacity *= 2;
    }

    int* newPos = (int*)realloc(h->pos, sizeof(int) * newPosCapacity);
    if (newPos == NULL)
    {
        return FIBHEAP_ERR;
    }

    for (size_t i = h->posCapacity; i < newPosCapacity; i++)
    {
        newPos[i] = -1;
    }

    h->pos         = newPos;
    h->posCapacity = newPosCapacity;
    return FIBHEAP_OK;
}

/* Doubles the node arena via realloc, one call per SoA array. h->capacity
 * is only advanced once every array has grown successfully: if one array
 * fails to grow, the arrays that did succeed simply carry unused headroom
 * past h->capacity (harmless -- nothing is ever indexed past h->capacity),
 * and the next expansion attempt retries them at little extra cost, since
 * realloc to a size they already hold is cheap. This keeps every array's
 * *valid* length in lockstep without needing a rollback path. */
static int expandArena(FibHeap* h)
{
    size_t newCapacity = h->capacity == 0 ? 1 : h->capacity * 2;

    int* newKey = (int*)realloc(h->key, sizeof(int) * newCapacity);
    if (newKey)
    {
        h->key = newKey;
    }

    int* newParent = (int*)realloc(h->parent, sizeof(int) * newCapacity);
    if (newParent)
    {
        h->parent = newParent;
    }

    int* newChild = (int*)realloc(h->child, sizeof(int) * newCapacity);
    if (newChild)
    {
        h->child = newChild;
    }

    int* newLeft = (int*)realloc(h->left, sizeof(int) * newCapacity);
    if (newLeft)
    {
        h->left = newLeft;
    }

    int* newRight = (int*)realloc(h->right, sizeof(int) * newCapacity);
    if (newRight)
    {
        h->right = newRight;
    }

    int* newDegree = (int*)realloc(h->degree, sizeof(int) * newCapacity);
    if (newDegree)
    {
        h->degree = newDegree;
    }

    bool* newMark = (bool*)realloc(h->mark, sizeof(bool) * newCapacity);
    if (newMark)
    {
        h->mark = newMark;
    }

    if (!newKey || !newParent || !newChild || !newLeft || !newRight || !newDegree || !newMark)
    {
        return FIBHEAP_ERR;
    }

    h->capacity = newCapacity;
    return FIBHEAP_OK;
}

/* Grows the consolidate scratch buffer (doubling) so it can hold `needed`
 * root indices. Allocated lazily and kept around across calls instead of
 * malloc/free'd per extractMin. */
static int ensureRootBuf(FibHeap* h, size_t needed)
{
    if (needed <= h->rootBufCapacity)
    {
        return FIBHEAP_OK;
    }

    size_t newCapacity = h->rootBufCapacity == 0 ? 8 : h->rootBufCapacity;
    while (newCapacity < needed)
    {
        newCapacity *= 2;
    }

    int* newBuf = (int*)realloc(h->rootBuf, sizeof(int) * newCapacity);
    if (newBuf == NULL)
    {
        return FIBHEAP_ERR;
    }

    h->rootBuf         = newBuf;
    h->rootBufCapacity = newCapacity;
    return FIBHEAP_OK;
}

/* Hands out an arena slot: reused from freeList when one is available
 * (keeps the arena as compact as the live working set, not the historical
 * high-water mark), otherwise bump-allocated, growing the arena first if
 * it's full. Returns -1 on allocation failure. */
static int allocNode(FibHeap* h)
{
    if (h->freeCount > 0)
    {
        return h->freeList[--h->freeCount];
    }

    if (h->used == h->capacity)
    {
        if (expandArena(h) != FIBHEAP_OK)
        {
            return -1;
        }
    }

    return (int)(h->used++);
}

/* Pushes idx onto freeList for reuse by a later allocNode(), growing
 * freeList (doubling) if needed. */
static int releaseNode(FibHeap* h, int idx)
{
    if (h->freeCount == h->freeCapacity)
    {
        size_t newCapacity = h->freeCapacity == 0 ? 1 : h->freeCapacity * 2;
        int*   newFree     = (int*)realloc(h->freeList, sizeof(int) * newCapacity);
        if (newFree == NULL)
        {
            return FIBHEAP_ERR; // idx just won't be recycled; the heap stays consistent
        }
        h->freeList     = newFree;
        h->freeCapacity = newCapacity;
    }

    h->freeList[h->freeCount++] = idx;
    return FIBHEAP_OK;
}

/* Inserts singleton node idx immediately to the right of anchor in
 * anchor's circular sibling list. idx must not currently be part of any
 * multi-element list (left[idx] == right[idx] == idx). */
static void listInsert(FibHeap* h, int anchor, int idx)
{
    int anchorRight = h->right[anchor];

    h->right[anchor]     = idx;
    h->left[idx]         = anchor;
    h->right[idx]        = anchorRight;
    h->left[anchorRight] = idx;
}

/* Detaches idx from whichever circular sibling list it's currently in,
 * patching its neighbors' links, and leaves idx as a singleton list of
 * one. Safe to call even if idx was the list's only member. */
static void listRemove(FibHeap* h, int idx)
{
    int l = h->left[idx];
    int r = h->right[idx];

    h->left[r]  = l;
    h->right[l] = r;

    h->left[idx]  = idx;
    h->right[idx] = idx;
}

/* Splices singleton root idx into the root list, or makes it the whole
 * root list if the heap was empty. Does not touch h->min. */
static void addToRootList(FibHeap* h, int idx)
{
    if (h->min == -1)
    {
        return; // idx is already a singleton; it becomes the root list on its own
    }

    listInsert(h, h->min, idx);
}

/* Makes y a child of x (caller guarantees x holds the smaller key):
 * detaches y from the root list and splices it into x's child list. */
static void link(FibHeap* h, int y, int x)
{
    listRemove(h, y);
    h->parent[y] = x;

    if (h->child[x] == -1)
    {
        h->child[x] = y; // y is already a singleton after listRemove
    }
    else
    {
        listInsert(h, h->child[x], y);
    }

    h->degree[x]++;
    h->mark[y] = false;
}

/* Detaches idx from parent's child list and reinserts it as a new,
 * unmarked root -- the O(1) step behind decreaseKey's heap-order fixup. */
static void cut(FibHeap* h, int idx, int parent)
{
    if (h->child[parent] == idx)
    {
        h->child[parent] = (h->right[idx] == idx) ? -1 : h->right[idx];
    }

    listRemove(h, idx);
    h->degree[parent]--;

    h->parent[idx] = -1;
    h->mark[idx]   = false;

    addToRootList(h, idx);
}

/* Walks up from idx marking ancestors that have now lost a second child;
 * the first one found already marked gets cut too, and the cascade
 * continues from its parent. This is what bounds decreaseKey's structural
 * damage to O(1) amortized instead of unbounded chains of thin trees. */
static void cascadingCut(FibHeap* h, int idx)
{
    int parent = h->parent[idx];
    if (parent == -1)
    {
        return; // idx is a root, nothing left to cascade
    }

    if (!h->mark[idx])
    {
        h->mark[idx] = true;
    }
    else
    {
        cut(h, idx, parent);
        cascadingCut(h, parent);
    }
}

/* Pairwise-merges root-list trees of equal degree until at most one tree
 * of each degree remains, then recomputes h->min from the survivors. This
 * is what amortizes extractMin to O(log n) despite the O(1) lazy work done
 * by insert/decreaseKey. */
static void consolidate(FibHeap* h)
{
    int degreeTable[FIB_MAX_DEGREE];
    for (int i = 0; i < FIB_MAX_DEGREE; i++)
    {
        degreeTable[i] = -1;
    }

    if (ensureRootBuf(h, h->size) != FIBHEAP_OK)
    {
        // Can't snapshot the root list to consolidate safely; degrade
        // gracefully by just refreshing h->min over the untouched list.
        findMinInRootList(h);
        return;
    }

    // Snapshot the root list before mutating it: link() reparents nodes,
    // which would otherwise corrupt an in-progress traversal of the same
    // list it's changing.
    size_t rootCount = 0;
    int    w         = h->min;
    do
    {
        h->rootBuf[rootCount++] = w;
        w                       = h->right[w];
    } while (w != h->min);

    for (size_t i = 0; i < rootCount; i++)
    {
        int x = h->rootBuf[i];
        int d = h->degree[x];

        while (d < FIB_MAX_DEGREE && degreeTable[d] != -1)
        {
            int y = degreeTable[d];
            if (h->key[y] < h->key[x])
            {
                int tmp = x;
                x       = y;
                y       = tmp;
            }
            link(h, y, x); // x holds the smaller key, y becomes its child
            degreeTable[d] = -1;
            d              = h->degree[x];
        }

        if (d < FIB_MAX_DEGREE)
        {
            degreeTable[d] = x;
        }
    }

    // The surviving roots are exactly the non-empty degreeTable slots --
    // reading the min off there avoids re-walking a root list that link()
    // may have punched holes in (a node recorded here can later be
    // absorbed by a different degree collision further down the loop).
    h->min = -1;
    for (int d = 0; d < FIB_MAX_DEGREE; d++)
    {
        int x = degreeTable[d];
        if (x == -1)
        {
            continue;
        }

        if (h->min == -1 || h->key[x] < h->key[h->min])
        {
            h->min = x;
        }
    }
}

/* Refreshes h->min by scanning the (untouched) root list starting from the
 * current h->min. Only valid when the root list hasn't been mutated by
 * link() since h->min was last known-good. */
static void findMinInRootList(FibHeap* h)
{
    int best = h->min;
    int w    = h->right[h->min];

    while (w != h->min)
    {
        if (h->key[w] < h->key[best])
        {
            best = w;
        }
        w = h->right[w];
    }

    h->min = best;
}
