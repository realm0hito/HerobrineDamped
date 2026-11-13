#include "base/arena.h"
#include "base/bits.h"
#include "base/os.h"

#include <string.h>

#define ARENA_COMMIT_GRANULARITY KiB(64)
#define SCRATCH_RESERVE          MiB(256)

void arena_init(Arena *a, u64 reserve, const char *name)
{
    reserve = align_up(reserve, ARENA_COMMIT_GRANULARITY);
    void *base = os_vm_reserve(reserve);
    if (!base) PANIC("arena '%s': failed to reserve %llu bytes", name, (unsigned long long)reserve);
    *a = (Arena){ .base = base, .reserved = reserve, .name = name };
}

void arena_release(Arena *a)
{
    if (a->base) os_vm_release(a->base, a->reserved);
    *a = (Arena){ 0 };
}

void *arena_push(Arena *a, u64 size, u64 align)
{
    ASSERT(is_pow2(align));
    u64 start = align_up(a->pos, align);
    u64 end = start + size;
    if (end > a->reserved || end < start) {
        PANIC("arena '%s' exhausted: need %llu, reserved %llu", a->name, (unsigned long long)end,
              (unsigned long long)a->reserved);
    }
    if (end > a->committed) {
        u64 new_commit = MIN(align_up(end, ARENA_COMMIT_GRANULARITY), a->reserved);
        if (!os_vm_commit(a->base + a->committed, new_commit - a->committed)) {
            PANIC("arena '%s': commit failed at %llu bytes", a->name, (unsigned long long)new_commit);
        }
        a->committed = new_commit;
    }
    a->pos = end;
    return a->base + start;
}

void *arena_push_zero(Arena *a, u64 size, u64 align)
{
    void *p = arena_push(a, size, align);
    memset(p, 0, (size_t)size);
    return p;
}

void arena_pop_to(Arena *a, u64 pos)
{
    ASSERT(pos <= a->pos);
#if defined(HD_DEBUG)
    memset(a->base + pos, 0xCD, (size_t)(a->pos - pos)); // make use-after-pop visible
#endif
    a->pos = pos;
}

static THREAD_LOCAL Arena g_scratch[2];

ArenaTemp scratch_begin(const Arena *conflict)
{
    for (u32 i = 0; i < ARRAY_COUNT(g_scratch); i++) {
        Arena *s = &g_scratch[i];
        if (s == conflict) continue;
        if (!s->base) arena_init(s, SCRATCH_RESERVE, i ? "scratch1" : "scratch0");
        return arena_temp_begin(s);
    }
    UNREACHABLE();
}

void scratch_thread_release(void)
{
    for (u32 i = 0; i < ARRAY_COUNT(g_scratch); i++) arena_release(&g_scratch[i]);
}
