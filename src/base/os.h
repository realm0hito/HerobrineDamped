#pragma once

#include "base/base.h"

typedef struct Arena Arena;

u64 os_time_ns(void); // monotonic, arbitrary epoch
void os_sleep_ms(u32 ms);

void *os_vm_reserve(u64 size); // NULL on failure; size multiple of 64 KiB
bool os_vm_commit(void *p, u64 size);
void os_vm_decommit(void *p, u64 size);
void os_vm_release(void *p, u64 size);

// Whole file into `a`, NUL-terminated (not counted in *out_size). NULL on failure, arena untouched.
u8 *os_read_file(Arena *a, const char *path, u64 *out_size);
