#include "base/mem.h"
#include "base/atomics.h"

#include <stdlib.h>

static volatile u64 g_alloc_count;

void *mem_alloc(u64 size)
{
    void *p = malloc((size_t)size);
    if (!p && size) PANIC("out of memory (%llu bytes)", (unsigned long long)size);
    atomic_add_u64(&g_alloc_count, 1);
    return p;
}

void *mem_realloc(void *p, u64 size)
{
    if (size == 0) {
        free(p);
        return NULL;
    }
    void *q = realloc(p, (size_t)size);
    if (!q) PANIC("out of memory (%llu bytes)", (unsigned long long)size);
    atomic_add_u64(&g_alloc_count, 1);
    return q;
}

void mem_free(void *p) { free(p); }

u64 mem_alloc_count(void) { return atomic_load_u64(&g_alloc_count); }
