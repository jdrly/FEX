/* gw2 (jdrly/gw2-on-mac#343): returns that the call-return stack predicts and returns it doesn't,
 * for FEX_RETURNLOOKUPOUTOFLINE. A Win64 x86_64 program, built with llvm-mingw.
 *
 * With the switch, a predicted return falls through to its ret and the L1 lookup for the others
 * sits after the unit's code. Each part has to compute the same with the switch off and on:
 *
 *   - hold: ret_hold (lea, ret) called 100,000 times from a loop. Its call and its return block are
 *     in one unit, so the prediction holds on every return.
 *   - broken: broken_call calls ret_broken, which swaps its return address for one of four
 *     landings, 25,000 times each. Each such return misses the prediction: the first to a landing
 *     finds nothing in L1 and goes to the dispatcher, the others find it in L1. The landing's own
 *     return pops broken_call's pair, which holds again.
 *   - pushed: push_ret pushes push_landing and returns to it, 1,000 times. It pops its caller's
 *     pair, so its return and the landing's both miss, and the call-return stack runs one pair
 *     short each time.
 *   - step: runs tf_s0 to tf_end 1,000 times without TF (tf_plain), so tf_s3 and tf_land are in L1
 *     as blocks without TF checks. Then three times: sets TF and calls tf_hold, a plain return,
 *     then tf_broken, which returns to tf_land in place of its call's return address. A vectored
 *     handler keeps TF set until tf_end. Under TF each instruction is a block that checks TF, and a
 *     single-instruction block's call pushes no prediction, so every return in it misses and takes
 *     the L1 lookup, after the unit's code with the switch on. tf_run and tf_plain have to return 8.
 *
 * Prints a line for each, then "gw2 test retlookup: passed" when the results hold. When every
 * step round trapped at the ten instructions from tf_s0 to tf_end, in order, and nowhere outside
 * tf_run, it also prints "gw2 test retlookup: every step round trapped as on x64". No case requires
 * it: a return through L1 can land in a block compiled without TF checks and lose the step there
 * (jdrly/gw2-on-mac#168).
 *
 * The calls inside the assembly go to private leaf code that touches no stack slot of its caller
 * and calls nothing, so they skip Win64's home space and stack alignment. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

int ret_hold(int x);
int broken_call(int k);
int push_ret(int k);
int tf_run(void);
int tf_plain(void);
void tf_s0(void), tf_s2(void), tf_s3(void), tf_hold(void), tf_broken(void), tf_b1(void), tf_b2(void), tf_b3(void),
    tf_land(void), tf_end(void);

/* The landings of ret_broken: landing k adds 100 * (k + 1), so broken_call(k) returns k + 100 * (k + 1). */
__asm__(".text\n"
        ".globl ret_hold\n"
        "ret_hold:\n"
        "    leal 1(%rcx), %eax\n"
        "    ret\n"
        ".globl broken_call\n"
        "broken_call:\n"
        "    call ret_broken\n"
        "    ud2\n"
        "ret_broken:\n"
        "    movl %ecx, %ecx\n" /* an int argument leaves rcx's upper half undefined */
        "    leaq landings(%rip), %rax\n"
        "    movq (%rax,%rcx,8), %rax\n"
        "    movq %rax, (%rsp)\n"
        "    movl %ecx, %eax\n"
        "    ret\n"
        "landing0:\n"
        "    addl $100, %eax\n"
        "    ret\n"
        "landing1:\n"
        "    addl $200, %eax\n"
        "    ret\n"
        "landing2:\n"
        "    addl $300, %eax\n"
        "    ret\n"
        "landing3:\n"
        "    addl $400, %eax\n"
        "    ret\n"
        ".globl push_ret\n"
        "push_ret:\n"
        "    leaq push_landing(%rip), %rax\n"
        "    pushq %rax\n"
        "    movl %ecx, %eax\n"
        "    ret\n"
        "push_landing:\n"
        "    addl $1000, %eax\n"
        "    ret\n"
        /* tf_run: TF on, a plain return, a return to tf_land, then TF off at tf_end (the handler).
         * tf_plain: the same from tf_s0, without TF. */
        ".globl tf_plain\n"
        "tf_plain:\n"
        "    jmp tf_s0\n"
        ".globl tf_run\n"
        "tf_run:\n"
        "    pushfq\n"
        "    orq $0x100, (%rsp)\n"
        "    popfq\n"
        "    nop\n"
        ".globl tf_s0\n"
        "tf_s0:\n"
        "    call tf_hold\n"
        ".globl tf_s3\n"
        "tf_s3:\n"
        "    call tf_broken\n"
        "    ud2\n"
        ".globl tf_hold\n"
        "tf_hold:\n"
        "    movl $7, %eax\n"
        ".globl tf_s2\n"
        "tf_s2:\n"
        "    ret\n"
        ".globl tf_broken\n"
        "tf_broken:\n"
        "    leaq tf_land(%rip), %rcx\n"
        ".globl tf_b1\n"
        "tf_b1:\n"
        "    movq %rcx, (%rsp)\n"
        ".globl tf_b2\n"
        "tf_b2:\n"
        "    addl $1, %eax\n"
        ".globl tf_b3\n"
        "tf_b3:\n"
        "    ret\n"
        ".globl tf_land\n"
        "tf_land:\n"
        "    nop\n"
        ".globl tf_end\n"
        "tf_end:\n"
        "    ret\n"
        ".data\n"
        ".p2align 3\n"
        "landings:\n"
        "    .quad landing0, landing1, landing2, landing3\n"
        ".text\n");

#define HOLDS 100000
#define BROKEN 25000
#define PUSHED 1000
#define ROUNDS 3
#define STEPS 10
#define WARM 1000

static uintptr_t seen[ROUNDS][16];
static int seen_count[ROUNDS], round_now, steps, stray_steps;

static LONG WINAPI handler(EXCEPTION_POINTERS *info)
{
    CONTEXT *ctx = info->ContextRecord;
    uintptr_t rip = (uintptr_t)ctx->Rip;

    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    steps++;
    if (rip >= (uintptr_t)tf_s0 && rip <= (uintptr_t)tf_end && seen_count[round_now] < 16)
        seen[round_now][seen_count[round_now]++] = rip;
    if (rip < (uintptr_t)tf_run || rip > (uintptr_t)tf_end)
        stray_steps++;
    /* TF off at tf_end, outside tf_run, or after far more steps than a round has */
    if (rip == (uintptr_t)tf_end || rip < (uintptr_t)tf_run || rip > (uintptr_t)tf_end || steps > 64)
        ctx->EFlags &= ~0x100;
    else
        ctx->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int main(void)
{
    const uintptr_t want[STEPS] = {(uintptr_t)tf_s0, (uintptr_t)tf_hold, (uintptr_t)tf_s2, (uintptr_t)tf_s3, (uintptr_t)tf_broken,
                                   (uintptr_t)tf_b1, (uintptr_t)tf_b2,   (uintptr_t)tf_b3, (uintptr_t)tf_land, (uintptr_t)tf_end};
    int ok = 1, exact = 1, i, r;
    long long sum, expect;

    sum = 0;
    for (i = 0; i < HOLDS; i++)
        sum += ret_hold(i);
    expect = (long long)HOLDS * (HOLDS + 1) / 2;
    printf("gw2 test retlookup: hold: %d returns, sum %lld, wanted %lld\n", HOLDS, sum, expect);
    ok &= sum == expect;

    int wrong = 0;
    for (i = 0; i < 4 * BROKEN; i++)
    {
        int k = i & 3;
        wrong += broken_call(k) != k + 100 * (k + 1);
    }
    printf("gw2 test retlookup: broken: %d returns to 4 landings, %d to the wrong one\n", 4 * BROKEN, wrong);
    ok &= !wrong;

    sum = expect = 0;
    for (i = 0; i < PUSHED; i++)
    {
        sum += push_ret(i);
        expect += i + 1000;
    }
    printf("gw2 test retlookup: pushed: %d returns to a pushed address, sum %lld, wanted %lld\n", PUSHED, sum, expect);
    ok &= sum == expect;

    wrong = 0;
    for (i = 0; i < WARM; i++)
        wrong += tf_plain() != 8;
    printf("gw2 test retlookup: warm: %d runs without TF, %d not returning 8\n", WARM, wrong);
    ok &= !wrong;

    if (!AddVectoredExceptionHandler(1, handler))
    {
        printf("gw2 test retlookup: AddVectoredExceptionHandler failed\n");
        return 1;
    }
    for (r = 0; r < ROUNDS; r++)
    {
        round_now = r;
        steps = 0;
        int result = tf_run();
        int good = seen_count[r] == STEPS;
        for (i = 0; good && i < STEPS; i++)
            good = seen[r][i] == want[i];
        printf("gw2 test retlookup: step round %d: tf_run returned %d, wanted 8; %d steps, %d from tf_s0 to tf_end at", r + 1, result,
               steps, seen_count[r]);
        for (i = 0; i < seen_count[r]; i++)
            printf(" tf_s0+%u", (unsigned)(seen[r][i] - (uintptr_t)tf_s0));
        printf(", %s\n", good ? "each instruction once, in order" : "not the ten instructions in order");
        ok &= result == 8;
        exact &= good;
    }
    printf("gw2 test retlookup: %d steps outside tf_run\n", stray_steps);
    if (exact && !stray_steps)
        printf("gw2 test retlookup: every step round trapped as on x64\n");

    if (!ok)
        return 1;
    printf("gw2 test retlookup: passed\n");
    return 0;
}
