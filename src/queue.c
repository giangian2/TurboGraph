#include <stdlib.h>
#include <string.h>
#include "../include/Queue.h"

Queue *queue_create(int cap, size_t elem_size)
{
    if (cap <= 0 || elem_size == 0)
        return NULL;
    Queue *q = malloc(sizeof(Queue));
    if (!q)
        return NULL;
    q->data = malloc((size_t)cap * elem_size);
    if (!q->data) {
        free(q);
        return NULL;
    }
    q->head = 0;
    q->count = 0;
    q->elem_size = elem_size;
    q->cap = cap;
    return q;
}

void queue_free(Queue *q)
{
    if (!q)
        return;
    free(q->data);
    free(q);
}

bool queue_is_empty(const Queue *q)
{
    return q->count == 0;
}

bool queue_enqueue(Queue *q, const void *v)
{
    if (q->count == q->cap)
        return false;
    void* slot = (char*)q->data + q->elem_size*((q->head + q->count) % q->cap);
    memcpy(slot, v, q->elem_size);
    q->count++;
    return true;
}

bool queue_dequeue(Queue *q, void *v)
{
    if (q->count == 0)
        return false;

    const void *slot = NULL;
    if (v){
        slot = (char*)q->data + q->elem_size*q->head;
        memcpy(v, slot, q->elem_size);
    }
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return true;
}
