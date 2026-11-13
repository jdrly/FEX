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
 * Prints one line for each and "gw2 test singlestep: passed" when both hold. */
#include <stdint.h>

#include "gw2test.h"

void step_target(void);
void step_run(void);
void step_after(void);
void int3_target(void);

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

static const uintptr_t step_offsets[] = {0, 5, 8, 11};
static uintptr_t seen[ROUNDS][8];
static int seen_count[ROUNDS], round_now, steps, stray_steps;
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
        /* stop at the return, or after far more steps than the run has */
        if (rip == (uintptr_t)step_after || steps > 64)
            ctx->EFlags &= ~0x100;
        else
            ctx->EFlags |= 0x100;
        if (rip < (uintptr_t)step_target || rip > (uintptr_t)step_after)
            stray_steps++;
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
        printf(", %s\n", good ? "each instruction once" : "wanted +0 +5 +8 +11");
        ok &= good;
    }
    if (stray_steps)
    {
        printf("gw2 test singlestep: %d steps outside step_run\n", stray_steps);
        ok = 0;
    }

    for (i = 0; i < BREAKS; i++)
        int3_target();
    printf("gw2 test singlestep: int3: %d breakpoints at int3_target, %d elsewhere, wanted %d\n", breaks, stray_breaks, BREAKS);
    ok &= breaks == BREAKS && !stray_breaks;

    if (!ok)
        return 1;
    printf("gw2 test singlestep: passed\n");
    return 0;
}
