#pragma once

// Heap wrappers. All engine heap traffic (including stb_ds) goes through these so the per-frame
// allocation counter can prove hot paths are allocation-free.
#include "base/base.h"

void *mem_alloc(u64 size);
void *mem_realloc(void *p, u64 size);
void mem_free(void *p);
u64 mem_alloc_count(void); // number of mem_alloc + growing mem_realloc calls since start
