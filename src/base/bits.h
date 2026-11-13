#pragma once

#include "base/base.h"

#if defined(__GNUC__) || defined(__clang__)
static inline u32 bits_popcount32(u32 x) { return (u32)__builtin_popcount(x); }
static inline u32 bits_popcount64(u64 x) { return (u32)__builtin_popcountll(x); }
static inline u32 bits_ctz32(u32 x) { return (u32)__builtin_ctz(x); } // x != 0
static inline u32 bits_ctz64(u64 x) { return (u32)__builtin_ctzll(x); }
static inline u32 bits_clz32(u32 x) { return (u32)__builtin_clz(x); }
static inline u32 bits_clz64(u64 x) { return (u32)__builtin_clzll(x); }
#elif defined(_MSC_VER)
#include <intrin.h>
static inline u32 bits_popcount32(u32 x) { return __popcnt(x); }
static inline u32 bits_popcount64(u64 x) { return (u32)__popcnt64(x); }
static inline u32 bits_ctz32(u32 x)
{
    unsigned long i;
    _BitScanForward(&i, x);
    return (u32)i;
}
static inline u32 bits_ctz64(u64 x)
{
    unsigned long i;
    _BitScanForward64(&i, x);
    return (u32)i;
}
static inline u32 bits_clz32(u32 x)
{
    unsigned long i;
    _BitScanReverse(&i, x);
    return 31u - (u32)i;
}
static inline u32 bits_clz64(u64 x)
{
    unsigned long i;
    _BitScanReverse64(&i, x);
    return 63u - (u32)i;
}
#else
#error "unsupported compiler"
#endif

static inline bool is_pow2(u64 x) { return x && !(x & (x - 1)); }
static inline u64 align_up(u64 x, u64 a) { return (x + a - 1) & ~(a - 1); } // a = power of two
static inline u32 log2_floor32(u32 x) { return 31u - bits_clz32(x); }       // x != 0
