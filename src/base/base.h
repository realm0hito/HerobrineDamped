#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

#define ARRAY_COUNT(a)      (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi)    MIN(MAX(x, lo), hi)
#define KiB(n)              ((u64)(n) << 10)
#define MiB(n)              ((u64)(n) << 20)
#define GiB(n)              ((u64)(n) << 30)
#define STATIC_ASSERT(c, m) _Static_assert(c, m)

#if defined(_MSC_VER)
#define DEBUG_BREAK() __debugbreak()
#define THREAD_LOCAL  __declspec(thread)
#define NORETURN      __declspec(noreturn)
#define PRINTF_FMT(f, a)
#else
#define DEBUG_BREAK()    __builtin_trap()
#define THREAD_LOCAL     _Thread_local
#define NORETURN         _Noreturn
#define PRINTF_FMT(f, a) __attribute__((format(printf, f, a)))
#endif

NORETURN void panic(const char *file, int line, const char *fmt, ...) PRINTF_FMT(3, 4);
NORETURN void vk_check_failed(i32 result, const char *expr, const char *file, int line);
const char *vk_result_name(i32 result);

#define PANIC(...) panic(__FILE__, __LINE__, __VA_ARGS__)

#if defined(HD_ASSERTS)
#define ASSERT(c)                                                                                                      \
    do {                                                                                                               \
        if (!(c)) panic(__FILE__, __LINE__, "assertion failed: %s", #c);                                               \
    } while (0)
#else
#define ASSERT(c) ((void)sizeof(!(c)))
#endif

#define UNREACHABLE() panic(__FILE__, __LINE__, "unreachable")

// Any result other than VK_SUCCESS aborts. Non-error codes that need handling (SUBOPTIMAL, OUT_OF_DATE,
// NOT_READY, ...) must be checked explicitly at the call site instead.
#define VK_CHECK(x)                                                                                                    \
    do {                                                                                                               \
        i32 vk_r_ = (i32)(x);                                                                                          \
        if (vk_r_ != 0) vk_check_failed(vk_r_, #x, __FILE__, __LINE__);                                                \
    } while (0)
