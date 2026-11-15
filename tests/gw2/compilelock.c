/* gw2 (jdrly/gw2-on-mac#165): threads that miss on the same entry at the same time, the case
 * FEX_ENTRYCOMPILELOCK makes compile once. A Win64 x86_64 program, built with llvm-mingw.
 *
 * 8 worker threads call 64 functions in turn, all 8 the same function at the same time: the main
 * thread lets them go on to the next only once every worker has come back from the last one. Each
 * function is 390 instructions and a ret, one block, long enough to compile that the other workers
 * miss on it while the first compiles it. The first function also compiles the workers' own loop,
 * so read 1 comes after it, and read 2 after the other 63 while the workers wait for the end. The
 * sum of the workers' jit_count between the reads is the compiles of those 63 functions: off,
 * about one per worker that missed together, so far above 63; on, 63. The main thread waits in
 * Sleep(1), so it leaves the cores to the workers.
 *
 * Prints
 *   gw2 test compilelock: workers <thread id>...    the 8
 *   gw2 test compilelock: functions 63
 * and "gw2 test compilelock: passed" once the workers have ended. */
#include <stdatomic.h>
#include <stdint.h>

#include "gw2test.h"

#define WORKERS 8
#define FUNCTIONS 64

/* FUNCTIONS functions of the same size, each 130 times add, xor and imul and a ret. They only
 * touch rax, rcx and rdx, which a call may change. */
extern const uint8_t compilelock_functions[], compilelock_functions_end[];
__asm__(".text\n"
        ".balign 4096\n"
        ".globl compilelock_functions\n"
        "compilelock_functions:\n"
        ".rept 64\n"
        "  .rept 130\n"
        "  addq %rbx, %rax\n"
        "  xorq %rax, %rcx\n"
        "  imulq %rcx, %rdx\n"
        "  .endr\n"
        "  ret\n"
        ".endr\n"
        ".globl compilelock_functions_end\n"
        "compilelock_functions_end:\n");

static atomic_int next = -1, arrived;
static DWORD workers[WORKERS];

/* each worker counts itself in once it is ready for function f, waits for the main thread to let
 * f go, and calls it. FUNCTIONS is the end. The loop runs whole for function 0, so its code is
 * compiled before read 1. */
static DWORD WINAPI worker(void *arg)
{
    size_t size = (size_t)(compilelock_functions_end - compilelock_functions) / FUNCTIONS;
    workers[(intptr_t)arg] = GetCurrentThreadId();
    for (int f = 0;; f++)
    {
        atomic_fetch_add(&arrived, 1);
        while (atomic_load(&next) < f)
            YieldProcessor();
        if (f == FUNCTIONS)
            return 0;
        ((void (*)(void))(compilelock_functions + f * size))();
    }
}

/* waits until every worker is ready for function f */
static void all_ready(int f)
{
    while (atomic_load(&arrived) < WORKERS * (f + 1))
        Sleep(1);
}

int main(void)
{
    HANDLE threads[WORKERS];

    for (int i = 0; i < WORKERS; i++)
    {
        threads[i] = CreateThread(NULL, 0, worker, (void *)(intptr_t)i, 0, NULL);
        if (!threads[i])
        {
            printf("gw2 test compilelock: thread %d failed, error %lu\n", i, GetLastError());
            return 1;
        }
    }
    all_ready(0);
    printf("gw2 test compilelock: workers");
    for (int i = 0; i < WORKERS; i++)
        printf(" %lu", workers[i]);
    printf("\n");

    atomic_store(&next, 0);
    all_ready(1);
    if (gw2_test_wait_for_reader())
        return 1;

    for (int f = 1; f < FUNCTIONS; f++)
    {
        atomic_store(&next, f);
        all_ready(f + 1);
    }
    printf("gw2 test compilelock: functions %d\n", FUNCTIONS - 1);
    if (gw2_test_wait_for_reader())
        return 1;

    atomic_store(&next, FUNCTIONS);
    if (WaitForMultipleObjects(WORKERS, threads, TRUE, 60000) != WAIT_OBJECT_0)
    {
        printf("gw2 test compilelock: the workers didn't end, error %lu\n", GetLastError());
        return 1;
    }
    printf("gw2 test compilelock: passed\n");
    return 0;
}
