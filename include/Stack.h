#ifndef STACK_H
#define STACK_H

#include <stdbool.h>
#include <stddef.h> /* size_t */

/*
 * Shadow-header generic stack, stb_ds.h style: a stack IS a plain typed
 * pointer (T*), not a wrapper struct. Its bookkeeping (count/cap) lives
 * in a StackHdr allocated right before the data and reached via pointer
 * arithmetic (see stack__hdr below) -- so there's no void* / elem_size to
 * thread through the API, and elements are addressed/typed natively:
 * s[i] just works, and the compiler catches type mismatches at push.
 *
 * Fixed-capacity, same as the struct-based version this replaces: sized
 * once at creation (e.g. cap = g->n for a traversal), no growth on push.
 *
 * Caveat inherited from the pattern: s (and out, where present) may be
 * evaluated more than once by these macros. Fine for a bare variable,
 * not for an expression with side effects.
 */
typedef struct
{
    size_t count; /* elements currently on the stack */
    size_t cap;
} StackHdr;

#define stack__hdr(s) ((StackHdr*)(void*)(s) - 1)

void* stack__create(size_t elem_size, size_t cap);
void  stack__free(void* s);

/* NULL on allocation failure or cap == 0. */
#define stack_create(T, cap) ((T*)stack__create(sizeof(T), (cap)))
#define stack_free(s) stack__free((void*)(s))

#define stack_len(s) ((s) ? stack__hdr(s)->count : (size_t)0)
#define stack_is_empty(s) (stack_len(s) == 0)

/* Pushes v onto the stack. false if full. */
#define stack_push(s, v)                                                                         \
    (stack__hdr(s)->count < stack__hdr(s)->cap ? ((s)[stack__hdr(s)->count++] = (v), true)        \
                                                : false)

/* Pops and returns the top element. Precondition: !stack_is_empty(s),
 * same as arrpop() in stb_ds.h -- no bounds check here, caller's job. */
#define stack_pop(s) ((s)[--stack__hdr(s)->count])

/* The top element as an lvalue: it can be read, assigned, or have its
 * address taken to be mutated in place (&stack_top(s)) -- which is what
 * a frame-based traversal needs, to advance the cursor stored in the
 * frame without popping and re-pushing it. The pointer stays valid until
 * that frame is popped: the capacity is fixed, so a push never moves the
 * data. Precondition: !stack_is_empty(s), as for stack_pop. */
#define stack_top(s) ((s)[stack__hdr(s)->count - 1])

#endif /* STACK_H */
