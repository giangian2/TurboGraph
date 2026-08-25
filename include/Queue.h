#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h> /* size_t */

/*
 * Shadow-header generic FIFO queue, stb_ds.h style: a queue IS a plain
 * typed pointer (T*) backed by a circular buffer, not a wrapper struct.
 * Its bookkeeping (head/count/cap) lives in a QueueHdr allocated right
 * before the data and reached via pointer arithmetic (see queue__hdr
 * below) -- so there's no void* / elem_size to thread through the API,
 * and elements are addressed/typed natively.
 *
 * Fixed-capacity, same as the struct-based version this replaces: sized
 * once at creation, enough for traversals where each vertex is enqueued
 * at most once (capacity = g->n).
 *
 * Caveat inherited from the pattern: q (and out, where present) may be
 * evaluated more than once by these macros. Fine for a bare variable,
 * not for an expression with side effects.
 */
typedef struct
{
    size_t head;  /* index of the next element to dequeue */
    size_t count; /* elements currently in the queue       */
    size_t cap;
} QueueHdr;

#define queue__hdr(q) ((QueueHdr*)(void*)(q) - 1)

void*  queue__create(size_t elem_size, size_t cap);
void   queue__free(void* q);
size_t queue__pop_index(QueueHdr* hdr);

/* NULL on allocation failure or cap == 0. */
#define queue_create(T, cap) ((T*)queue__create(sizeof(T), (cap)))
#define queue_free(q) queue__free((void*)(q))

#define queue_len(q) ((q) ? queue__hdr(q)->count : (size_t)0)
#define queue_is_empty(q) (queue_len(q) == 0)

/* Enqueues v. false if full. */
#define queue_enqueue(q, v)                                                                       \
    (queue__hdr(q)->count < queue__hdr(q)->cap                                                     \
         ? ((q)[(queue__hdr(q)->head + queue__hdr(q)->count) % queue__hdr(q)->cap] = (v),          \
            queue__hdr(q)->count++, true)                                                          \
         : false)

/* Dequeues and returns the oldest element. Precondition:
 * !queue_is_empty(q), same as arrpop() in stb_ds.h -- no bounds check
 * here, caller's job. */
#define queue_dequeue(q) ((q)[queue__pop_index(queue__hdr(q))])

#endif /* QUEUE_H */
