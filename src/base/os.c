// Portable parts of the OS layer (stdio only).
#include "base/arena.h"
#include "base/os.h"

#include <stdio.h>

u8 *os_read_file(Arena *a, const char *path, u64 *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 *data = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        u64 mark = arena_pos(a);
        data = arena_push(a, (u64)size + 1, 16);
        if (fread(data, 1, (size_t)size, f) == (size_t)size) {
            data[size] = 0;
            if (out_size) *out_size = (u64)size;
        } else {
            arena_pop_to(a, mark);
            data = NULL;
        }
    }
    fclose(f);
    return data;
}
