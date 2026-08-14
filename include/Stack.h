#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stddef.h> /* size_t */

/*
 * Fixed-capacity LIFO stack, generic over the element type.
 * Elements are stored BY VALUE in a single contiguous buffer
 * (cap * elem_size bytes) to preserve cache locality.
 */
typedef struct
{
    void*  data;
    int    count; /* elements currently on the stack */
    int    cap;
    size_t elem_size; /* bytes per element, fixed at creation */
} Stack;

/* NULL on allocation failure, cap <= 0, or elem_size == 0. */
Stack* stack_create(int cap, size_t elem_size);
void   stack_free(Stack* s);

bool stack_is_empty(const Stack* s);

/* Copies elem_size bytes from *v into the stack. false if full. */
bool stack_push(Stack* s, const void* v);

/* Copies the top element into *v (elem_size bytes). false if empty. */
bool stack_pop(Stack* s, void* v);

#endif /* STACK_H */