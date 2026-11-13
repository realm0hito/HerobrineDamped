#include "base/os.h"

#include <windows.h>

u64 os_time_ns(void)
{
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    u64 f = (u64)freq.QuadPart, t = (u64)c.QuadPart;
    return (t / f) * 1000000000ull + (t % f) * 1000000000ull / f; // split to avoid overflow
}

void os_sleep_ms(u32 ms) { Sleep(ms); }

void *os_vm_reserve(u64 size) { return VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE, PAGE_NOACCESS); }

bool os_vm_commit(void *p, u64 size) { return VirtualAlloc(p, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE) != NULL; }

void os_vm_decommit(void *p, u64 size) { VirtualFree(p, (SIZE_T)size, MEM_DECOMMIT); }

void os_vm_release(void *p, u64 size)
{
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
}
