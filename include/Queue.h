#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

/*
 * Fixed-capacity FIFO queue, generic over the element type, backed by a
 * circular buffer. Elements are stored BY VALUE in a single contiguous
 * block (cap * elem_size bytes) for cache locality. Sized once at
 * creation: enough for traversals, where each vertex is enqueued at
 * most once (capacity = g->n).
 */
typedef struct
{
    void*  data;
    int    head;  /* index of the next element to dequeue  */
    int    count; /* elements currently in the queue       */
    int    cap;
    size_t elem_size; /* bytes per element, fixed at creation  */
} Queue;

/* NULL on allocation failure, cap <= 0, or elem_size == 0. */
Queue* queue_create(int cap, size_t elem_size);
void   queue_free(Queue* q);

bool queue_is_empty(const Queue* q);

/* Copies elem_size bytes from *v into the queue. false if full. */
bool queue_enqueue(Queue* q, const void* v);

/* Copies the oldest element into *v (elem_size bytes); false if empty.
 * v may be NULL to discard the element. */
bool queue_dequeue(Queue* q, void* v);

#endif /* QUEUE_H */
