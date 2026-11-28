/* gw2 (jdrly/gw2-on-mac#389): a thread in a loop that calls a function can still be suspended
 * when the block the call returns to doesn't poll the suspend doorbell
 * (FEX_SKIPRETURNSITESUSPENDCHECK=1). A Win64 x86_64 program, built with llvm-mingw.
 *
 * On Windows FEX polls the doorbell at every entry point of a unit and before an unconditional
 * backward jump. Each loop below calls a leaf function in a unit of its own and closes with "je"
 * back, which polls nothing. So the loop polls at two places an iteration: the leaf's entry, and
 * the block after the call, which is an entry point of the loop's unit. With the switch only the
 * leaf's entry is left:
 *
 *   direct    call cl_leaf
 *   indirect  call *%r8, with r8 holding cl_leaf, which takes the L1 lookup
 *
 * The leaf counts in rax and stores the count, so at either poll rax equals the counter in memory.
 * A helper thread calls SuspendThread and GetThreadContext, and the main thread waits for it with a
 * time limit, so a suspend that never comes ends the attempt instead of the run.
 *
 * Each loop is suspended 1,000 times, within 10 s each time. Each time the context has to show
 * Rip at a poll, rax equal to the counter in memory, and rcx, rdx and r8 the loop's pointers. After
 * each resume the counter has to move on. At the end the thread returns its register, which has to
 * equal the counter.
 *
 * GW2_CALLLOOP_EXPECT=leaf (with the switch): every suspend is at the leaf's entry.
 * GW2_CALLLOOP_EXPECT=both (without it): every suspend is at the leaf's entry or after the call,
 * and each place has some. That shows the test can tell the two apart.
 *
 * Prints a line per loop and ends with "gw2 test callloop: passed". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef long long (*loop_fn)(volatile long long *counter, volatile LONG *stop, void *leaf);
long long cl_direct(volatile long long *counter, volatile LONG *stop, void *leaf);
long long cl_indirect(volatile long long *counter, volatile LONG *stop, void *leaf);
extern const unsigned char cl_leaf[], cl_direct_ret[], cl_indirect_ret[];

/* cl_leaf sits after a ret, where no unit decodes into it, so it is a unit of its own */
__asm__(".text\n"
        ".globl cl_direct\n"
        "cl_direct:\n"
        "  xorl %eax, %eax\n"
        "cl_direct_loop:\n"
        "  call cl_leaf\n"
        ".globl cl_direct_ret\n"
        "cl_direct_ret:\n"
        "  cmpl $0, (%rdx)\n"
        "  je cl_direct_loop\n"
        "  ret\n"

        ".globl cl_indirect\n"
        "cl_indirect:\n"
        "  xorl %eax, %eax\n"
        "cl_indirect_loop:\n"
        "  call *%r8\n"
        ".globl cl_indirect_ret\n"
        "cl_indirect_ret:\n"
        "  cmpl $0, (%rdx)\n"
        "  je cl_indirect_loop\n"
        "  ret\n"

        ".p2align 4\n"
        ".globl cl_leaf\n"
        "cl_leaf:\n"
        "  incq %rax\n"
        "  movq %rax, (%rcx)\n"
        "  ret\n");

struct loop
{
    const char *name;
    loop_fn fn;
    const unsigned char *ret;
};

static const struct loop loops[] = {
    {"direct", cl_direct, cl_direct_ret},
    {"indirect", cl_indirect, cl_indirect_ret},
};

#define SUSPENDS 1000
#define SUSPEND_LIMIT_MS 10000
#define WAIT_MS 10000

static volatile long long counter;
static volatile LONG stop;
static loop_fn running;
static long long result;

static DWORD WINAPI spinner(void *arg)
{
    (void)arg;
    result = running(&counter, &stop, (void *)cl_leaf);
    return 0;
}

/* The helper: on each request, suspends the target and reads its context. */
static HANDLE target, request, done;
static CONTEXT seen;
static BOOL seen_ok;
static DWORD seen_error;

static DWORD WINAPI suspender(void *arg)
{
    (void)arg;
    for (;;)
    {
        WaitForSingleObject(request, INFINITE);
        CONTEXT ctx = {.ContextFlags = CONTEXT_FULL};
        BOOL ok = SuspendThread(target) != (DWORD)-1 && GetThreadContext(target, &ctx);
        seen_error = ok ? 0 : GetLastError();
        seen = ctx;
        seen_ok = ok;
        SetEvent(done);
    }
}

/* 1 when the target was suspended within limit and seen holds its context, 0 when the limit ran
 * out first (the helper still waits), -1 on an error */
static int suspend_once(DWORD limit)
{
    SetEvent(request);
    DWORD w = WaitForSingleObject(done, limit);
    if (w == WAIT_TIMEOUT)
        return 0;
    return w == WAIT_OBJECT_0 && seen_ok ? 1 : -1;
}

static int failed;

/* Starts the loop on a new thread and waits until it has counted to 1,000. */
static HANDLE start_loop(const struct loop *l)
{
    counter = 0;
    stop = 0;
    running = l->fn;
    HANDLE t = CreateThread(NULL, 0, spinner, NULL, 0, NULL);
    if (!t)
    {
        printf("gw2 test callloop: %s FAILED: CreateThread error %lu\n", l->name, GetLastError());
        failed = 1;
        return NULL;
    }
    ULONGLONG deadline = GetTickCount64() + WAIT_MS;
    while (counter < 1000 && GetTickCount64() < deadline)
        Sleep(1);
    if (counter < 1000)
    {
        printf("gw2 test callloop: %s FAILED: the loop counted to %lld in %d ms\n", l->name, counter, WAIT_MS);
        failed = 1;
        return NULL;
    }
    target = t;
    return t;
}

/* Stops the loop. The thread has to end and return the counter. */
static void end_loop(const struct loop *l, HANDLE t)
{
    stop = 1;
    if (WaitForSingleObject(t, WAIT_MS) != WAIT_OBJECT_0)
    {
        printf("gw2 test callloop: %s FAILED: the thread didn't end\n", l->name);
        failed = 1;
        return;
    }
    CloseHandle(t);
    if (result != counter)
    {
        printf("gw2 test callloop: %s FAILED: the thread returned %lld, the counter is %lld\n", l->name, result, counter);
        failed = 1;
    }
}

static void suspend_loop(const struct loop *l, int leaf_only)
{
    HANDLE t = start_loop(l);
    if (!t)
        return;
    ULONGLONG longest = 0;
    int at_leaf = 0, at_ret = 0;
    for (int i = 1; i <= SUSPENDS; i++)
    {
        ULONGLONG start = GetTickCount64();
        int r = suspend_once(SUSPEND_LIMIT_MS);
        ULONGLONG took = GetTickCount64() - start;
        if (took > longest)
            longest = took;
        if (r <= 0)
        {
            if (r == 0)
                printf("gw2 test callloop: %s FAILED at %d: not suspended within %d ms\n", l->name, i, SUSPEND_LIMIT_MS);
            else
                printf("gw2 test callloop: %s FAILED at %d: error %lu\n", l->name, i, seen_error);
            failed = 1;
            return;
        }
        long long memory = counter;
        long long reg = (long long)seen.Rax;
        int leaf = seen.Rip == (DWORD64)cl_leaf, ret = seen.Rip == (DWORD64)l->ret;
        at_leaf += leaf;
        at_ret += ret;
        if (!(leaf || (ret && !leaf_only)) || reg != memory || seen.Rcx != (DWORD64)&counter || seen.Rdx != (DWORD64)&stop ||
            seen.R8 != (DWORD64)cl_leaf)
        {
            printf("gw2 test callloop: %s FAILED at %d: rip %p (leaf %p, after the call %p), rax %lld, counter %lld, rcx %p, "
                   "rdx %p, r8 %p\n",
                   l->name, i, (void *)seen.Rip, (const void *)cl_leaf, (const void *)l->ret, reg, memory, (void *)seen.Rcx,
                   (void *)seen.Rdx, (void *)seen.R8);
            failed = 1;
            ResumeThread(t);
            return;
        }
        if (ResumeThread(t) == (DWORD)-1)
        {
            printf("gw2 test callloop: %s FAILED at %d: ResumeThread error %lu\n", l->name, i, GetLastError());
            failed = 1;
            return;
        }
        ULONGLONG deadline = GetTickCount64() + WAIT_MS;
        while (counter == memory && GetTickCount64() < deadline)
            SwitchToThread();
        if (counter == memory)
        {
            DWORD ended = WaitForSingleObject(t, 0) == WAIT_OBJECT_0;
            printf("gw2 test callloop: %s FAILED at %d: the counter stayed at %lld after the resume%s\n", l->name, i, memory,
                   ended ? ", the thread left the loop" : "");
            failed = 1;
            return;
        }
    }
    end_loop(l, t);
    if (failed)
        return;
    printf("gw2 test callloop: %s suspended %d times, %d at the leaf's entry and %d after the call, counter %lld, longest "
           "suspend %llu ms\n",
           l->name, SUSPENDS, at_leaf, at_ret, counter, longest);
    if (!leaf_only && (at_leaf == 0 || at_ret == 0))
    {
        printf("gw2 test callloop: %s FAILED: without the switch both places have to poll\n", l->name);
        failed = 1;
    }
}

int main(void)
{
    const char *expect = getenv("GW2_CALLLOOP_EXPECT");
    if (!expect || (strcmp(expect, "leaf") && strcmp(expect, "both")))
    {
        printf("gw2 test callloop: FAILED: GW2_CALLLOOP_EXPECT has to be leaf or both\n");
        return 1;
    }
    int leaf_only = !strcmp(expect, "leaf");
    request = CreateEventW(NULL, FALSE, FALSE, NULL);
    done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!request || !done || !CreateThread(NULL, 0, suspender, NULL, 0, NULL))
    {
        printf("gw2 test callloop: FAILED: setup error %lu\n", GetLastError());
        return 1;
    }
    for (size_t i = 0; i < sizeof(loops) / sizeof(loops[0]) && !failed; i++)
        suspend_loop(&loops[i], leaf_only);
    if (failed)
        return 1;
    printf("gw2 test callloop: passed\n");
    return 0;
}
