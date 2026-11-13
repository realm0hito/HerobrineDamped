#pragma once

// Linear arena on reserved virtual memory: reserve once, commit on demand, never moves.
#include "base/base.h"

typedef struct Arena {
    u8 *base;
    u64 pos;
    u64 committed;
    u64 reserved;
    const char *name;
} Arena;

typedef struct ArenaTemp {
    Arena *arena;
    u64 pos;
} ArenaTemp;

void arena_init(Arena *a, u64 reserve, const char *name);
void arena_release(Arena *a);
void *arena_push(Arena *a, u64 size, u64 align); // uninitialised
void *arena_push_zero(Arena *a, u64 size, u64 align);
void arena_pop_to(Arena *a, u64 pos);
static inline u64 arena_pos(const Arena *a) { return a->pos; }
static inline void arena_clear(Arena *a) { arena_pop_to(a, 0); }

static inline ArenaTemp arena_temp_begin(Arena *a) { return (ArenaTemp){ a, a->pos }; }
static inline void arena_temp_end(ArenaTemp t) { arena_pop_to(t.arena, t.pos); }

#define ARENA_PUSH(a, T, n)    ((T *)arena_push_zero((a), sizeof(T) * (u64)(n), _Alignof(T)))
#define ARENA_PUSH_NZ(a, T, n) ((T *)arena_push((a), sizeof(T) * (u64)(n), _Alignof(T)))

// Per-thread scratch: returns a scratch arena different from `conflict` (pass the arena the caller is
// allocating its results into, or NULL). Always pair with scratch_end.
ArenaTemp scratch_begin(const Arena *conflict);
#define scratch_end(t) arena_temp_end(t)
void scratch_thread_release(void);
