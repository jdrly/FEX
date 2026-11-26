/* gw2 (jdrly/gw2-on-mac#139): FEX's stats object grows while threads count in their slots.
 *
 * Win64 x86_64, built with llvm-mingw, run with FEX_PROFILESTATS=1. 8 counting threads start first,
 * so their slots are among the first, on the object's first page. Each loops on Sleep(0) (a system
 * call, two hardware TSO switches where FEX switches per thread) and GetTickCount (an exit into
 * native code), and my FEX counts both in the thread's slot. Meanwhile the main thread starts 600
 * more threads that wait until the program ends. The object starts with room for 27 slots and
 * doubles each time it is full, so the 600 make it grow five times under the counting threads.
 * Read 1 comes with all of them alive. It prints
 *   gw2 test slots: counting <thread id>...    the 8
 *   gw2 test slots: waiting <thread id>...     the 600
 * and "gw2 test slots: passed" at the end. */
#include <stdint.h>

#include "gw2test.h"

#define COUNTING 8
#define WAITING 600

static HANDLE done;
static volatile LONG stop, started;
static DWORD counting[COUNTING], waiting[WAITING];

static DWORD WINAPI count(void *arg)
{
    counting[(intptr_t)arg] = GetCurrentThreadId();
    InterlockedIncrement(&started);
    while (!stop)
    {
        Sleep(0);
        GetTickCount();
    }
    return 0;
}

static DWORD WINAPI wait_for_done(void *arg)
{
    waiting[(intptr_t)arg] = GetCurrentThreadId();
    InterlockedIncrement(&started);
    WaitForSingleObject(done, INFINITE);
    return 0;
}

static int start(LPTHREAD_START_ROUTINE routine, int i)
{
    HANDLE thread = CreateThread(NULL, 65536, routine, (void *)(intptr_t)i, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!thread)
    {
        printf("gw2 test slots: thread %d failed, error %lu\n", i, GetLastError());
        return 1;
    }
    CloseHandle(thread);
    return 0;
}

int main(void)
{
    int i, ok;

    if (!(done = CreateEventA(NULL, TRUE, FALSE, NULL)))
    {
        printf("gw2 test slots: CreateEvent failed, error %lu\n", GetLastError());
        return 1;
    }
    for (i = 0; i < COUNTING; i++)
        if (start(count, i))
            return 1;
    while (started < COUNTING)
        Sleep(1);
    for (i = 0; i < WAITING; i++)
        if (start(wait_for_done, i))
            return 1;
    while (started < COUNTING + WAITING)
        Sleep(10);
    printf("gw2 test slots: counting");
    for (i = 0; i < COUNTING; i++)
        printf(" %lu", counting[i]);
    printf("\ngw2 test slots: waiting");
    for (i = 0; i < WAITING; i++)
        printf(" %lu", waiting[i]);
    printf("\n");
    ok = !gw2_test_wait_for_reader();

    stop = 1;
    SetEvent(done);
    if (!ok)
        return 1;
    printf("gw2 test slots: passed\n");
    return 0;
}
