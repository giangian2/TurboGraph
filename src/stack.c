#include <stdlib.h>
#include <string.h>
#include "../include/Stack.h"

Stack *stack_create(int cap, size_t elem_size)
{
    if (cap <= 0 || elem_size == 0)
        return NULL;
    Stack *s = malloc(sizeof(Stack));
    if (!s)
        return NULL;
    s->data = malloc((size_t)cap * elem_size);
    if (!s->data) {
        free(s);
        return NULL;
    }
    s->count = 0;
    s->cap = cap;
    s->elem_size = elem_size;
    return s;
}

void stack_free(Stack *s)
{
    if (!s)
        return;
    free(s->data);
    free(s);
}

bool stack_is_empty(const Stack *s)
{
    return s->count == 0;
}

bool stack_push(Stack *s, const void *v)
{
    if (s->count == s->cap)
        return false;
    void *slot = (char *)s->data + s->elem_size * (size_t)s->count;
    memcpy(slot, v, s->elem_size);
    s->count++;
    return true;
}

bool stack_pop(Stack *s, void *v)
{
    if (s->count == 0)
        return false;
    s->count--;
    if (v) {
        const void *slot = (char *)s->data + s->elem_size * (size_t)s->count;
        memcpy(v, slot, s->elem_size);
    }
    return true;
}
