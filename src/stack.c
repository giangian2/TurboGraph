#include "../include/Stack.h"
#include <stdlib.h>

/* Single allocation for header + data, contiguous just like the header
 * comment promises: (StackHdr | data...), with the returned pointer
 * aimed just past the header, at data[0]. */
void* stack__create(size_t elem_size, size_t cap)
{
    if (elem_size == 0 || cap == 0)
        return NULL;

    StackHdr* hdr = malloc(sizeof(StackHdr) + elem_size * cap);
    if (!hdr)
        return NULL;

    hdr->count = 0;
    hdr->cap   = cap;
    return hdr + 1;
}

void stack__free(void* s)
{
    if (!s)
        return;
    free(stack__hdr(s));
}
