// MAP_ANONYMOUS / MAP_NORESERVE / madvise are not in strict POSIX.1-2008.
#define _DEFAULT_SOURCE

#include "base/os.h"

#include <sys/mman.h>
#include <time.h>

u64 os_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

void os_sleep_ms(u32 ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0) {}
}

void *os_vm_reserve(u64 size)
{
    void *p = mmap(NULL, (size_t)size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

bool os_vm_commit(void *p, u64 size) { return mprotect(p, (size_t)size, PROT_READ | PROT_WRITE) == 0; }

void os_vm_decommit(void *p, u64 size)
{
    madvise(p, (size_t)size, MADV_DONTNEED);
    mprotect(p, (size_t)size, PROT_NONE);
}

void os_vm_release(void *p, u64 size) { munmap(p, (size_t)size); }
