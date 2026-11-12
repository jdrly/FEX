/* gw2 (jdrly/gw2-on-mac#117): moves each of the counters my FEX adds to its stats slots, in
 * phases a reader of FEX's stats can tell apart (tests/gw2/gw2test.h), and keeps 200 threads alive
 * while it is read.
 *
 * Win64 x86_64, built with llvm-mingw. In order:
 *   - starts 200 threads that wait until the program ends; each one needs a stats slot.
 *     Read 1.
 *   - makes 100,000 calls into native code (GetTickCount, an ARM64EC export of kernel32 that makes
 *     no system call), each an exit from JIT code, and 1,000 Sleep(0), each an exit and a system
 *     call, so two hardware TSO switches where FEX switches them per thread. Read 2.
 *   - writes FUNCTIONS functions, each `mov eax, imm32; ret` on its own 64 bytes, into one
 *     executable allocation, all before the first call, as tests/gw2/codebuf.c does (#112), and
 *     calls them through a pointer: one compile each, more host code than the 16 MB code buffer
 *     FEX starts with, so the thread moves to a new one. A second pass compiles again what the
 *     move took away. Then, all of it compiled, it calls them again for at least 3 seconds. Each
 *     indirect call looks its target up in the thread's L1 cache, and with far more targets than
 *     its 8,192 entries most miss it. The JIT's dispatcher then looks in L2 itself,
 *     and only a miss there reaches the lookup in C++ that counts towards resizing the L1
 *     (FEXCore/Source/Interface/Core/LookupCache.h, FindBlock). 64 bytes apart, the functions
 *     span 3,072 pages of x86 code, more than the 2,047 that L2 holds, so it keeps clearing
 *     itself and those lookups keep coming; over 250 a second for a second
 *     make the dynamic L1 double. Read 3.
 *
 * Only the main thread does any of it. It prints
 *   gw2 test counters: tid <main thread id>
 *   gw2 test counters: workers <thread id>...    the 200 others
 * and "gw2 test counters: passed" at the end. The exit status is 0 with "passed", 1 otherwise. */
#include <stdint.h>
#include <string.h>

#include "gw2test.h"

#define THREADS 200
#define FUNCTIONS 196608u
#define STRIDE 64u

typedef uint32_t (*function)(void);

static HANDLE done;
static volatile LONG started;
static DWORD workers[THREADS];

static DWORD WINAPI wait_for_done(void *arg)
{
    workers[(intptr_t)arg] = GetCurrentThreadId();
    InterlockedIncrement(&started);
    WaitForSingleObject(done, INFINITE);
    return 0;
}

static uint32_t value(uint32_t i)
{
    return i * 2654435761u ^ 0x5a5a5a5au;
}

static uint64_t pass(const uint8_t *code)
{
    /* FNV-1a over the results */
    uint64_t sum = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < FUNCTIONS; i++)
        sum = (sum ^ ((function)(code + (size_t)i * STRIDE))()) * 0x100000001b3ull;
    return sum;
}

int main(void)
{
    uint8_t *code;
    uint64_t first, start;
    int i, passes = 0, ok = 1;

    if (!(done = CreateEventA(NULL, TRUE, FALSE, NULL)))
    {
        printf("gw2 test counters: CreateEvent failed, error %lu\n", GetLastError());
        return 1;
    }
    for (i = 0; i < THREADS; i++)
    {
        HANDLE thread = CreateThread(NULL, 65536, wait_for_done, (void *)(intptr_t)i, STACK_SIZE_PARAM_IS_A_RESERVATION,
                                     NULL);
        if (!thread)
        {
            printf("gw2 test counters: thread %d failed, error %lu\n", i, GetLastError());
            return 1;
        }
        CloseHandle(thread);
    }
    while (started < THREADS)
        Sleep(10);
    printf("gw2 test counters: tid %lu\n", GetCurrentThreadId());
    printf("gw2 test counters: workers");
    for (i = 0; i < THREADS; i++)
        printf(" %lu", workers[i]);
    printf("\n");
    if (gw2_test_wait_for_reader())
        return 1;

    for (i = 0; i < 100000; i++)
        GetTickCount();
    for (i = 0; i < 1000; i++)
        Sleep(0);
    if (gw2_test_wait_for_reader())
        return 1;

    code = VirtualAlloc(NULL, (size_t)FUNCTIONS * STRIDE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        printf("gw2 test counters: VirtualAlloc failed, error %lu\n", GetLastError());
        return 1;
    }
    memset(code, 0xcc, (size_t)FUNCTIONS * STRIDE);
    for (uint32_t f = 0; f < FUNCTIONS; f++)
    {
        uint8_t *p = code + (size_t)f * STRIDE;
        uint32_t v = value(f);
        p[0] = 0xb8; /* mov eax, imm32 */
        memcpy(p + 1, &v, sizeof(v));
        p[5] = 0xc3; /* ret */
    }
    FlushInstructionCache(GetCurrentProcess(), code, (size_t)FUNCTIONS * STRIDE);
    first = pass(code);
    ok &= pass(code) == first;
    start = GetTickCount64();
    do
    {
        ok &= pass(code) == first;
        passes++;
    } while (GetTickCount64() - start < 3000);
    printf("gw2 test counters: %d passes in 3 seconds after the first two, %s\n", passes, ok ? "same sums" : "different sums");
    ok &= !gw2_test_wait_for_reader();

    /* the threads end with the process */
    SetEvent(done);
    if (!ok)
        return 1;
    printf("gw2 test counters: passed\n");
    return 0;
}
