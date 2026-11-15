/* gw2 (jdrly/gw2-on-mac#169): a thread spinning in a loop closed by a conditional backward branch
 * can be suspended with FEX_CONDBACKEDGESUSPEND=1, and can't without it. A Win64 x86_64 program,
 * built with llvm-mingw.
 *
 * FEX checks for a suspend at block entries and before unconditional backward jumps. A loop that
 * closes with "je" back skips its head's entry, so it never reaches a check, and Wine's
 * GetThreadContext waits for it for good (run 43452320908, entryfault.c's first loop). With the
 * switch the taken branch checks, and the suspend resumes at the loop's head.
 *
 * Each loop counts in rax and stores the count before it tests the stop flag:
 *
 *   je      cmpl $0, (%rdx); je back: the issue's loop, a b.cond on the flags
 *   fold    cmpl $0, %r8d; je back, with no flag read on either side, so FEX folds the compare into
 *           a cbz. A suspend that resumed at the je would branch on stale flags, which the
 *           exact Rip check below catches.
 *   parity  testl %r8d, %r8d; jp back, a tbz or tbnz on the parity bit
 *
 * A helper thread calls SuspendThread and GetThreadContext, and the main thread waits for it with a
 * time limit, so a suspend that never comes ends the attempt instead of the run.
 *
 * GW2_CONDLOOP_EXPECT=suspend (with the switch): each loop is suspended (je 1,000 times, the others
 * 200), within 10 s each time. Each time the context has to show Rip at the loop's head, rax equal
 * to the counter in memory, and rcx and rdx the loop's pointers. After each resume the counter has
 * to move on. At the end the thread returns its register, which has to equal the counter.
 *
 * GW2_CONDLOOP_EXPECT=hang (without it): the je loop isn't suspended within 2 s. Then the stop flag
 * lets the thread out of the loop, and the pending suspend has to take it at the next check.
 *
 * Prints a line per loop and ends with "gw2 test condloop: passed". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef long long (*loop_fn)(volatile long long *counter, volatile LONG *stop);
long long cl_je(volatile long long *counter, volatile LONG *stop);
long long cl_fold(volatile long long *counter, volatile LONG *stop);
long long cl_parity(volatile long long *counter, volatile LONG *stop);
extern const unsigned char cl_je_loop[], cl_fold_loop[], cl_parity_loop[];

__asm__(".text\n"
        ".globl cl_je\n"
        "cl_je:\n"
        "  xorl %eax, %eax\n"
        ".globl cl_je_loop\n"
        "cl_je_loop:\n"
        "  incq %rax\n"
        "  movq %rax, (%rcx)\n"
        "  cmpl $0, (%rdx)\n"
        "  je cl_je_loop\n"
        "  ret\n"

        ".globl cl_fold\n"
        "cl_fold:\n"
        "  xorl %eax, %eax\n"
        ".globl cl_fold_loop\n"
        "cl_fold_loop:\n"
        "  addq $1, %rax\n"
        "  movq %rax, (%rcx)\n"
        "  movl (%rdx), %r8d\n"
        "  cmpl $0, %r8d\n"
        "  je cl_fold_loop\n"
        "  testq %rax, %rax\n"
        "  ret\n"

        ".globl cl_parity\n"
        "cl_parity:\n"
        "  xorl %eax, %eax\n"
        ".globl cl_parity_loop\n"
        "cl_parity_loop:\n"
        "  addq $1, %rax\n"
        "  movq %rax, (%rcx)\n"
        "  movl (%rdx), %r8d\n"
        "  testl %r8d, %r8d\n"
        "  jp cl_parity_loop\n"
        "  ret\n");

struct loop
{
    const char *name;
    loop_fn fn;
    const unsigned char *head;
    int suspends;
};

static const struct loop loops[] = {
    {"je", cl_je, cl_je_loop, 1000},
    {"fold", cl_fold, cl_fold_loop, 200},
    {"parity", cl_parity, cl_parity_loop, 200},
};

#define SUSPEND_LIMIT_MS 10000
#define HANG_LIMIT_MS 2000
#define WAIT_MS 10000

static volatile long long counter;
/* 1 stops every loop: nonzero for je and fold, odd parity for parity */
static volatile LONG stop;
static loop_fn running;
static long long result;

static DWORD WINAPI spinner(void *arg)
{
    (void)arg;
    result = running(&counter, &stop);
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
        printf("gw2 test condloop: %s FAILED: CreateThread error %lu\n", l->name, GetLastError());
        failed = 1;
        return NULL;
    }
    ULONGLONG deadline = GetTickCount64() + WAIT_MS;
    while (counter < 1000 && GetTickCount64() < deadline)
        Sleep(1);
    if (counter < 1000)
    {
        printf("gw2 test condloop: %s FAILED: the loop counted to %lld in %d ms\n", l->name, counter, WAIT_MS);
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
        printf("gw2 test condloop: %s FAILED: the thread didn't end\n", l->name);
        failed = 1;
        return;
    }
    CloseHandle(t);
    if (result != counter)
    {
        printf("gw2 test condloop: %s FAILED: the thread returned %lld, the counter is %lld\n", l->name, result, counter);
        failed = 1;
    }
}

static void suspend_loop(const struct loop *l)
{
    HANDLE t = start_loop(l);
    if (!t)
        return;
    ULONGLONG longest = 0;
    for (int i = 1; i <= l->suspends; i++)
    {
        ULONGLONG start = GetTickCount64();
        int r = suspend_once(SUSPEND_LIMIT_MS);
        ULONGLONG took = GetTickCount64() - start;
        if (took > longest)
            longest = took;
        if (r <= 0)
        {
            if (r == 0)
                printf("gw2 test condloop: %s FAILED at %d: not suspended within %d ms\n", l->name, i, SUSPEND_LIMIT_MS);
            else
                printf("gw2 test condloop: %s FAILED at %d: error %lu\n", l->name, i, seen_error);
            failed = 1;
            return;
        }
        long long memory = counter;
        long long reg = (long long)seen.Rax;
        if (seen.Rip != (DWORD64)l->head || reg != memory || seen.Rcx != (DWORD64)&counter || seen.Rdx != (DWORD64)&stop)
        {
            printf("gw2 test condloop: %s FAILED at %d: rip %p (head %p), rax %lld, counter %lld, rcx %p, rdx %p\n", l->name, i,
                   (void *)seen.Rip, (const void *)l->head, reg, memory, (void *)seen.Rcx, (void *)seen.Rdx);
            failed = 1;
            ResumeThread(t);
            return;
        }
        if (ResumeThread(t) == (DWORD)-1)
        {
            printf("gw2 test condloop: %s FAILED at %d: ResumeThread error %lu\n", l->name, i, GetLastError());
            failed = 1;
            return;
        }
        ULONGLONG deadline = GetTickCount64() + WAIT_MS;
        while (counter == memory && GetTickCount64() < deadline)
            SwitchToThread();
        if (counter == memory)
        {
            DWORD ended = WaitForSingleObject(t, 0) == WAIT_OBJECT_0;
            printf("gw2 test condloop: %s FAILED at %d: the counter stayed at %lld after the resume%s\n", l->name, i, memory,
                   ended ? ", the thread left the loop" : "");
            failed = 1;
            return;
        }
    }
    end_loop(l, t);
    if (!failed)
        printf("gw2 test condloop: %s suspended %d times at its head, counter %lld, longest suspend %llu ms\n", l->name, l->suspends,
               counter, longest);
}

static void hang_loop(const struct loop *l)
{
    HANDLE t = start_loop(l);
    if (!t)
        return;
    int r = suspend_once(HANG_LIMIT_MS);
    if (r != 0)
    {
        if (r == 1)
            printf("gw2 test condloop: %s FAILED: suspended at %p, wanted no suspend without the switch\n", l->name, (void *)seen.Rip);
        else
            printf("gw2 test condloop: %s FAILED: error %lu\n", l->name, seen_error);
        failed = 1;
        if (r == 1)
        {
            ResumeThread(t);
            end_loop(l, t);
        }
        return;
    }
    printf("gw2 test condloop: %s not suspended within %d ms, as without the switch\n", l->name, HANG_LIMIT_MS);

    /* out of the loop, the thread reaches a check and the pending suspend takes it */
    stop = 1;
    if (WaitForSingleObject(done, WAIT_MS) != WAIT_OBJECT_0 || !seen_ok)
    {
        printf("gw2 test condloop: %s FAILED: not suspended within %d ms after it left the loop (error %lu)\n", l->name, WAIT_MS,
               seen_error);
        failed = 1;
        return;
    }
    if (ResumeThread(t) == (DWORD)-1)
    {
        printf("gw2 test condloop: %s FAILED: ResumeThread error %lu\n", l->name, GetLastError());
        failed = 1;
        return;
    }
    end_loop(l, t);
    if (!failed)
        printf("gw2 test condloop: %s suspended once it left the loop, counter %lld\n", l->name, counter);
}

int main(void)
{
    /* each line reaches the log at once, also from a run that hangs */
    setvbuf(stdout, NULL, _IONBF, 0);

    const char *want = getenv("GW2_CONDLOOP_EXPECT");
    if (!want || (strcmp(want, "suspend") && strcmp(want, "hang")))
    {
        printf("gw2 test condloop: GW2_CONDLOOP_EXPECT is %s, wanted suspend or hang\n", want ? want : "unset");
        return 1;
    }

    request = CreateEventW(NULL, FALSE, FALSE, NULL);
    done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!request || !done || !CreateThread(NULL, 0, suspender, NULL, 0, NULL))
    {
        printf("gw2 test condloop: can't start the helper (error %lu)\n", GetLastError());
        return 1;
    }

    if (!strcmp(want, "hang"))
        hang_loop(&loops[0]);
    else
        for (size_t i = 0; i < sizeof(loops) / sizeof(loops[0]) && !failed; i++)
            suspend_loop(&loops[i]);

    if (failed)
        return 1;
    printf("gw2 test condloop: passed\n");
    return 0;
}
