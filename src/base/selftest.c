// `engine --selftest`: checks for the base layer. Runs without a GPU or window; also registered with CTest.
#include "base/selftest.h"
#include "base/arena.h"
#include "base/bits.h"
#include "base/ds.h"
#include "base/log.h"
#include "base/mem.h"
#include "base/os.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            LOG_ERROR("selftest: CHECK failed: %s", #c);                                                               \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static u64 xorshift64(u64 *s)
{
    u64 x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static void test_arena(void)
{
    Arena a;
    arena_init(&a, GiB(1), "selftest");
    u8 *b = arena_push(&a, 3, 1);
    u64 *q = ARENA_PUSH(&a, u64, 4);
    CHECK(b && q);
    CHECK(((uintptr_t)q & 7) == 0);
    CHECK((u8 *)q >= b + 3);

    u64 mark = arena_pos(&a);
    u8 *big = arena_push_zero(&a, MiB(10), 4096); // spans many commit chunks
    CHECK(((uintptr_t)big & 4095) == 0);
    bool zero = true;
    for (u64 i = 0; i < MiB(10); i += 4093) zero &= big[i] == 0;
    CHECK(zero);
    memset(big, 0xAB, (size_t)MiB(10));
    arena_pop_to(&a, mark);
    CHECK(arena_pos(&a) == mark);
    u8 *again = arena_push_zero(&a, MiB(10), 4096);
    CHECK(again == big && again[MiB(10) - 1] == 0);

    ArenaTemp t = arena_temp_begin(&a);
    arena_push(&a, 1000, 8);
    arena_temp_end(t);
    CHECK(arena_pos(&a) == t.pos);
    arena_release(&a);
    CHECK(a.base == NULL);
}

static void test_scratch(void)
{
    ArenaTemp s0 = scratch_begin(NULL);
    ArenaTemp s1 = scratch_begin(s0.arena);
    CHECK(s0.arena != s1.arena);
    ArenaTemp s2 = scratch_begin(s1.arena);
    CHECK(s2.arena == s0.arena);
    void *p = arena_push(s1.arena, 64, 16);
    CHECK(p != NULL);
    scratch_end(s2);
    scratch_end(s1);
    scratch_end(s0);
    CHECK(arena_pos(s0.arena) == s0.pos && arena_pos(s1.arena) == s1.pos);
}

static void test_bits(void)
{
    u64 seed = 0x9E3779B97F4A7C15ull;
    int bad = 0;
    for (int n = 0; n < 100000; n++) {
        u64 v = xorshift64(&seed) >> (n % 64); // vary magnitude so clz is exercised
        if (!v) continue;
        u32 lo = (u32)v, pc32 = 0, pc64 = 0, ctz64 = 64, clz64 = 64;
        for (u32 i = 0; i < 64; i++) {
            if (!(v >> i & 1)) continue;
            pc64++;
            if (i < 32) pc32++;
            if (ctz64 == 64) ctz64 = i;
            clz64 = 63 - i;
        }
        bad += bits_popcount64(v) != pc64;
        bad += bits_popcount32(lo) != pc32;
        bad += bits_ctz64(v) != ctz64;
        bad += bits_clz64(v) != clz64;
        if (lo) {
            u32 ctz32 = 0, clz32 = 0;
            while (!(lo >> ctz32 & 1)) ctz32++;
            while (!(lo >> (31 - clz32) & 1)) clz32++;
            bad += bits_ctz32(lo) != ctz32;
            bad += bits_clz32(lo) != clz32;
        }
    }
    CHECK(bad == 0);
    CHECK(is_pow2(1) && is_pow2(4096) && !is_pow2(0) && !is_pow2(12));
    CHECK(align_up(13, 8) == 16 && align_up(16, 8) == 16);
    CHECK(log2_floor32(1) == 0 && log2_floor32(4096) == 12 && log2_floor32(4097) == 12);
}

static void test_time(void)
{
    u64 t0 = os_time_ns();
    os_sleep_ms(5);
    u64 t1 = os_time_ns();
    CHECK(t1 > t0);
    CHECK(t1 - t0 >= 4000000ull); // >= 4 ms; sleep may round down by a tick on some platforms
    u64 prev = os_time_ns();
    bool mono = true;
    for (int i = 0; i < 100000; i++) {
        u64 t = os_time_ns();
        mono &= t >= prev;
        prev = t;
    }
    CHECK(mono);
}

static void test_alloc_counter(void)
{
    u64 before = mem_alloc_count();
    int *arr = NULL;
    for (int i = 0; i < 100; i++) arrput(arr, i);
    CHECK(arrlen(arr) == 100 && arr[99] == 99);
    CHECK(mem_alloc_count() > before);
    arrfree(arr);
}

static void test_file(void)
{
    const char *path = "hd_selftest.tmp";
    const char text[] = "voxel\n\0bytes";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fwrite(text, 1, sizeof(text), f);
    fclose(f);

    ArenaTemp t = scratch_begin(NULL);
    u64 size = 0;
    u8 *data = os_read_file(t.arena, path, &size);
    CHECK(data && size == sizeof(text) && memcmp(data, text, sizeof(text)) == 0 && data[size] == 0);
    u64 mark = arena_pos(t.arena);
    CHECK(os_read_file(t.arena, "hd_selftest_missing.tmp", &size) == NULL && arena_pos(t.arena) == mark);
    scratch_end(t);
    remove(path);
}

int base_selftest(void)
{
    g_failures = 0;
    test_arena();
    test_scratch();
    test_bits();
    test_time();
    test_alloc_counter();
    test_file();
    if (g_failures)
        LOG_ERROR("selftest: %d check(s) failed", g_failures);
    else
        LOG_INFO("selftest: all checks passed");
    return g_failures;
}
