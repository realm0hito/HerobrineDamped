#pragma once

// Minimal atomics without <stdatomic.h> (MSVC needs /experimental:c11atomics for it). Grows as M3 needs.
#include "base/base.h"

#if defined(__GNUC__) || defined(__clang__)
static inline u64 atomic_add_u64(volatile u64 *p, u64 v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
static inline u64 atomic_load_u64(volatile u64 *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
#elif defined(_MSC_VER)
#include <intrin.h>
static inline u64 atomic_add_u64(volatile u64 *p, u64 v)
{
    return (u64)_InterlockedExchangeAdd64((volatile long long *)p, (long long)v);
}
static inline u64 atomic_load_u64(volatile u64 *p) { return (u64)_InterlockedOr64((volatile long long *)p, 0); }
#else
#error "unsupported compiler"
#endif
