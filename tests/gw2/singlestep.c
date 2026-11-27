/* gw2 (jdrly/gw2-on-mac#161): single steps and int3 breakpoints into code that is already in
 * the thread's L1 cache. A Win64 x86_64 program, built with llvm-mingw.
 *
 * Each exception resumes through the dispatcher. With FEX_DISPATCHERL1LOOKUP on, the dispatcher
 * looks in L1 there, and that lookup has to stay behind its trap flag check: a resume with TF set
 * into a block found in L1 would run the whole block without a trap.
 *
 *   - step: calls step_target, four instructions, 1,000 times, so its block is in L1. Then three
 *     times: sets TF and calls it once, with a vectored handler that keeps TF set until the
 *     return. Each instruction of step_target has to trap once, so the handler sees the four
 *     instruction addresses in it, in order, and nothing else in it.
 *   - int3: calls int3_target (int3; ret) 1,000 times. The handler finds Rip on the int3, as on
 *     x64 Windows, and moves it past. It has to see 1,000 breakpoints, each at int3_target.
 *
 * Prints one line for each and "gw2 test singlestep: passed" when both hold.
 *
 * jdrly/gw2-on-mac#168: a step outside step_run is a stray one. Each round's line says whether the
 * round trapped at step_after, where the handler clears TF, and one line a round lists the stray
 * steps' Rips, main's as main+<offset>. When every round trapped at step_after and nothing else
 * stepped, the program also prints "gw2 test singlestep: every round trapped at step_after, no
 * stray steps". In run 43444435324 neither FEX printed it: step_target's return found step_after
 * in L1, in a block compiled without TF checks. My switch for that is gone since 2026-11-27
 * (docs/gw2/adr/0013), so no case requires the line, and a log that has it shows the bug fixed. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

void step_target(void);
void step_run(void);
void step_after(void);
void int3_target(void);
int main(void);

/* step_target's instructions: mov 5 bytes, add 3, add 3, ret */
__asm__(".text\n"
        ".globl step_target\n"
        "step_target:\n"
        "    movl $1, %eax\n"
        "    addl $2, %eax\n"
        "    addl $3, %eax\n"
        "    ret\n"
        ".globl step_run\n"
        "step_run:\n"
        "    pushfq\n"
        "    orq $0x100, (%rsp)\n"
        "    popfq\n"
        "    nop\n"
        "    call step_target\n"
        ".globl step_after\n"
        "step_after:\n"
        "    nop\n"
        "    ret\n"
        ".globl int3_target\n"
        "int3_target:\n"
        "    int3\n"
        "    ret\n");

#define ROUNDS 3
#define BREAKS 1000
#define STRAYS 256

static const uintptr_t step_offsets[] = {0, 5, 8, 11};
static uintptr_t seen[ROUNDS][8];
static int seen_count[ROUNDS], round_now, steps, stray_steps;
static int at_after[ROUNDS], stray_round[STRAYS];
static uintptr_t stray_rip[STRAYS];
static int breaks, stray_breaks;

static LONG WINAPI handler(EXCEPTION_POINTERS *info)
{
    CONTEXT *ctx = info->ContextRecord;
    uintptr_t rip = (uintptr_t)ctx->Rip, target = (uintptr_t)step_target;

    switch (info->ExceptionRecord->ExceptionCode)
    {
    case EXCEPTION_SINGLE_STEP:
        steps++;
        if (rip >= target && rip < (uintptr_t)step_run && seen_count[round_now] < 8)
            seen[round_now][seen_count[round_now]++] = rip - target;
        if (rip == (uintptr_t)step_after)
            at_after[round_now] = 1;
        /* stop at the return, or after far more steps than the run has */
        if (rip == (uintptr_t)step_after || steps > 64)
            ctx->EFlags &= ~0x100;
        else
            ctx->EFlags |= 0x100;
        if (rip < (uintptr_t)step_target || rip > (uintptr_t)step_after)
        {
            if (stray_steps < STRAYS)
            {
                stray_rip[stray_steps] = rip;
                stray_round[stray_steps] = round_now;
            }
            stray_steps++;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    case EXCEPTION_BREAKPOINT:
        if (rip == (uintptr_t)int3_target)
            breaks++;
        else
            stray_breaks++;
        ctx->Rip = (uintptr_t)int3_target + 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* rip as main+<offset> when it is in main's first 64 KB, else as <module>+<offset> */
static void print_rip(uintptr_t rip)
{
    char path[MAX_PATH] = "", *name = path;
    HMODULE module;

    if (rip >= (uintptr_t)main && rip - (uintptr_t)main < 0x10000)
    {
        printf(" main+0x%llx", (unsigned long long)(rip - (uintptr_t)main));
        return;
    }
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (const char *)rip, &module) ||
        !GetModuleFileNameA(module, path, sizeof(path)))
    {
        printf(" 0x%llx", (unsigned long long)rip);
        return;
    }
    for (char *p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    printf(" %s+0x%llx", name, (unsigned long long)(rip - (uintptr_t)module));
}

int main(void)
{
    int ok = 1, r, i;

    if (!AddVectoredExceptionHandler(1, handler))
    {
        printf("gw2 test singlestep: AddVectoredExceptionHandler failed\n");
        return 1;
    }

    for (i = 0; i < 1000; i++)
        step_target();
    for (r = 0; r < ROUNDS; r++)
    {
        round_now = r;
        steps = 0;
        step_run();
        int good = seen_count[r] == 4;
        for (i = 0; good && i < 4; i++)
            good = seen[r][i] == step_offsets[i];
        printf("gw2 test singlestep: step round %d: %d steps, %d in step_target at", r + 1, steps, seen_count[r]);
        for (i = 0; i < seen_count[r]; i++)
            printf(" +%u", (unsigned)seen[r][i]);
        printf(", %s, %s\n", good ? "each instruction once" : "wanted +0 +5 +8 +11",
               at_after[r] ? "trapped at step_after" : "no trap at step_after");
        ok &= good;
    }

    for (i = 0; i < BREAKS; i++)
        int3_target();
    printf("gw2 test singlestep: int3: %d breakpoints at int3_target, %d elsewhere, wanted %d\n", breaks, stray_breaks, BREAKS);
    ok &= breaks == BREAKS && !stray_breaks;

    /* Not a failure without the switch: the engine's FEX steps 120 times in main after rounds
     * whose return skipped the trap at step_after (run 43444435324). Printed after the int3 part,
     * by when the handler has cleared TF again. */
    printf("gw2 test singlestep: %d steps outside step_run, main at %p, step_run at %p\n", stray_steps, (void *)main, (void *)step_run);
    for (r = 0; r < ROUNDS; r++)
    {
        int n = 0;
        for (i = 0; i < stray_steps && i < STRAYS; i++)
            n += stray_round[i] == r;
        if (!n)
            continue;
        printf("gw2 test singlestep: round %d: %d stray steps at", r + 1, n);
        for (i = 0; i < stray_steps && i < STRAYS; i++)
            if (stray_round[i] == r)
                print_rip(stray_rip[i]);
        printf("\n");
    }
    int clean = !stray_steps;
    for (r = 0; r < ROUNDS; r++)
        clean &= at_after[r];
    if (clean)
        printf("gw2 test singlestep: every round trapped at step_after, no stray steps\n");

    if (!ok)
        return 1;
    printf("gw2 test singlestep: passed\n");
    return 0;
}
