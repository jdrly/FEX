/* gw2 (jdrly/gw2-on-mac#113): what a guest syscall costs with FEX_HARDWARETSOSYSCALLS=switch and
 * with keep. A Win64 x86_64 program, built with llvm-mingw.
 *
 *   tsocalls.exe [<iterations>]
 *
 * Three loops, each on a thread of its own, one after the other, <iterations> each (ten million
 * by default):
 *
 *   native  RtlGetCurrentPeb, an ntdll export that makes no syscall: an exit into native code
 *           and back, and no trap in either mode
 *   close   NtClose on a null handle, a syscall with a wineserver round trip
 *   qpc     NtQueryPerformanceCounter, a short syscall
 *
 * Each loop is timed with rdtsc, so the clock the third loop measures isn't the measurement's
 * own clock. rdtsc is converted to nanoseconds once, against QueryPerformanceCounter over 200 ms,
 * before the loops. Prints one line per loop:
 *
 *   gw2 test tsocalls: <loop> thread <tid> <ns> ns per iteration
 *
 * Under tests/gw2/run.zsh (GW2_TEST_FLAG set), the three threads stay alive after their loops
 * until its reader of FEX's stats object (FEX_PROFILESTATS=1) has read once, so each thread's trap
 * count is in its slot, found by <tid>. Without GW2_TEST_FLAG, as in a run by hand, it doesn't
 * wait. It ends with "gw2 test tsocalls: passed". */
#include "gw2test.h"

#include <winternl.h>
#include <x86intrin.h>

typedef void *(WINAPI *peb_fn)(void);
typedef NTSTATUS(WINAPI *close_fn)(HANDLE);
typedef NTSTATUS(WINAPI *qpc_fn)(LARGE_INTEGER *, LARGE_INTEGER *);

static peb_fn volatile rtl_get_current_peb;
static close_fn volatile nt_close;
static qpc_fn volatile nt_query_performance_counter;

static unsigned long long iterations = 10000000;
static double ns_per_tick;
static HANDLE release;

struct loop
{
    const char *name;
    int which;
    DWORD tid;
    double ns;
    HANDLE finished;
};

static DWORD WINAPI run_loop(void *arg)
{
    struct loop *l = arg;
    LARGE_INTEGER counter;
    unsigned long long start, end;

    l->tid = GetCurrentThreadId();
    switch (l->which)
    {
    case 0:
        start = __rdtsc();
        for (unsigned long long i = 0; i < iterations; i++)
            rtl_get_current_peb();
        end = __rdtsc();
        break;
    case 1:
        start = __rdtsc();
        for (unsigned long long i = 0; i < iterations; i++)
            nt_close(NULL);
        end = __rdtsc();
        break;
    default:
        start = __rdtsc();
        for (unsigned long long i = 0; i < iterations; i++)
            nt_query_performance_counter(&counter, NULL);
        end = __rdtsc();
        break;
    }
    l->ns = (double)(end - start) * ns_per_tick / (double)iterations;
    SetEvent(l->finished);
    /* the thread keeps its stats slot while it waits */
    if (release)
        WaitForSingleObject(release, INFINITE);
    return 0;
}

int main(int argc, char **argv)
{
    struct loop loops[3] = {{"native", 0}, {"close", 1}, {"qpc", 2}};
    HANDLE threads[3];
    LARGE_INTEGER freq, q0, q1;
    unsigned long long t0, t1;
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    int reader = getenv("GW2_TEST_FLAG") != NULL;

    if (argc > 2 || (argc == 2 && !(iterations = strtoull(argv[1], NULL, 10))))
    {
        printf("gw2 test tsocalls: usage: tsocalls.exe [<iterations>]\n");
        return 2;
    }
    rtl_get_current_peb = (peb_fn)GetProcAddress(ntdll, "RtlGetCurrentPeb");
    nt_close = (close_fn)GetProcAddress(ntdll, "NtClose");
    nt_query_performance_counter = (qpc_fn)GetProcAddress(ntdll, "NtQueryPerformanceCounter");
    if (!rtl_get_current_peb || !nt_close || !nt_query_performance_counter)
    {
        printf("gw2 test tsocalls: an ntdll export is missing\n");
        return 1;
    }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&q0);
    t0 = __rdtsc();
    Sleep(200);
    QueryPerformanceCounter(&q1);
    t1 = __rdtsc();
    if (t1 <= t0 || q1.QuadPart <= q0.QuadPart)
    {
        printf("gw2 test tsocalls: rdtsc or the performance counter doesn't advance\n");
        return 1;
    }
    ns_per_tick = (double)(q1.QuadPart - q0.QuadPart) * 1e9 / (double)freq.QuadPart / (double)(t1 - t0);
    printf("gw2 test tsocalls: %llu iterations a loop, rdtsc at %.1f MHz\n", iterations, 1e3 / ns_per_tick);

    if (reader && !(release = CreateEventA(NULL, TRUE, FALSE, NULL)))
    {
        printf("gw2 test tsocalls: CreateEvent failed (%lu)\n", GetLastError());
        return 1;
    }
    for (int i = 0; i < 3; i++)
    {
        /* one after the other, so the loops don't share the CPU */
        if (!(loops[i].finished = CreateEventA(NULL, TRUE, FALSE, NULL)) ||
            !(threads[i] = CreateThread(NULL, 0, run_loop, &loops[i], 0, NULL)))
        {
            printf("gw2 test tsocalls: CreateEvent or CreateThread failed (%lu)\n", GetLastError());
            return 1;
        }
        WaitForSingleObject(loops[i].finished, INFINITE);
        printf("gw2 test tsocalls: %s thread %lu %.1f ns per iteration\n", loops[i].name, loops[i].tid, loops[i].ns);
        fflush(stdout);
    }

    if (reader)
    {
        int rc = gw2_test_wait_for_reader();
        SetEvent(release);
        if (rc)
            return 1;
    }
    WaitForMultipleObjects(3, threads, TRUE, INFINITE);
    printf("gw2 test tsocalls: passed\n");
    return 0;
}
