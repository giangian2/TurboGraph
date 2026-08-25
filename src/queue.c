#include "../include/Queue.h"
#include <stdlib.h>

/* Single allocation for header + data, contiguous just like the header
 * comment promises: (QueueHdr | data...), with the returned pointer
 * aimed just past the header, at data[0]. */
void* queue__create(size_t elem_size, size_t cap)
{
    if (elem_size == 0 || cap == 0)
        return NULL;

    QueueHdr* hdr = malloc(sizeof(QueueHdr) + elem_size * cap);
    if (!hdr)
        return NULL;

    hdr->head  = 0;
    hdr->count = 0;
    hdr->cap   = cap;
    return hdr + 1;
}

void queue__free(void* q)
{
    if (!q)
        return;
    free(queue__hdr(q));
}

/* Advances the circular buffer past one element and returns the index
 * it was read from. Caller (queue_dequeue) must ensure count > 0. */
size_t queue__pop_index(QueueHdr* hdr)
{
    size_t idx = hdr->head;
    hdr->head  = (hdr->head + 1) % hdr->cap;
    hdr->count--;
    return idx;
}
