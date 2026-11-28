/* gw2 (jdrly/gw2-on-mac#391): guest addresses above 4 GB, for FEX_GUESTADDRESSLITERALS. A Win64
 * x86_64 program, built with llvm-mingw, whose image sits at 0x140000000 as GW2's does.
 *
 * With the switch, a return address or a [rip + ...] address above 4 GB is one ldr of a literal
 * after the unit's code, and an instruction that reads and writes [rip + ...] builds the address
 * once. Each part has to compute the same with the switch off and on:
 *
 *   - rmw: the same 26 instructions that write memory, once on rip_cell through
 *     [rip + ...] (rip_run) and once on ind_cell through a register (ind_run), which no switch of
 *     mine touches. Among them adc and sbb, which read CF, set from a bit of the round's value so
 *     both carries come in, rcr by cl at 0 and at 3, which splits its block so each part builds
 *     the address again, cmpxchg both ways, xchg (locked by x86), a lock add and an indirect call
 *     through [rip + ...]. After each, the flags go into an
 *     accumulator. 10,000 rounds with a different value each, and the cells and the accumulators
 *     of the two runs have to be equal after every round.
 *   - calls: a call pushes its return address, which comes from a literal with the switch on.
 *     Every call above does, and main checks that 10,000 rounds came back.
 *
 * Prints the image base, a line for each part, then "gw2 test ripaddr: passed" when everything
 * matched and the image sits above 4 GB. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

struct cell
{
    uint64_t q;
    uint32_t d;
    uint16_t w;
    uint8_t b;
    uint8_t pad;
    uint64_t q2;
};

struct cell rip_cell, ind_cell;
int (*rip_fn)(int);
uint64_t rip_called;

/* rcx = the round's value, rdx = &ind_cell for ind_run. Returns the flags accumulator. */
uint64_t rip_run(uint64_t v);
uint64_t ind_run(uint64_t v, struct cell *c);
int plus_one(int x);

/* After an instruction: its flags (CF PF AF ZF SF OF) into r8 */
#define FLAGS                \
    "    pushfq\n"           \
    "    popq %rax\n"        \
    "    andl $0x8d5, %eax\n" \
    "    xorq %rax, %r8\n"   \
    "    rolq $7, %r8\n"

/* The body for one cell, Q, D, W, B and Q2 being its fields; the value in r9 */
#define BODY(Q, D, W, B, Q2)                                    \
    "    movq %r9, %rcx\n"                                      \
    "    addq %rcx, " Q "\n" FLAGS                              \
    "    subq $0x1234, " Q "\n" FLAGS                           \
    "    btq $0, %r9\n" /* carry in: the value's bit 0 */          \
    "    adcq %rcx, " Q "\n" FLAGS                              \
    "    btq $1, %r9\n"                                         \
    "    sbbl %ecx, " D "\n" FLAGS                              \
    "    andq %rcx, " Q2 "\n" FLAGS                             \
    "    orw %cx, " W "\n" FLAGS                                \
    "    xorb %cl, " B "\n" FLAGS                               \
    "    incq " Q "\n" FLAGS                                    \
    "    decl " D "\n" FLAGS                                    \
    "    negq " Q2 "\n" FLAGS                                   \
    "    notl " D "\n"                                          \
    "    shlq $3, " Q "\n" FLAGS                                \
    "    rolq $1, " Q2 "\n" FLAGS                               \
    "    sarl $5, " D "\n" FLAGS                                \
    "    btsq $5, " Q "\n" FLAGS                                \
    "    btcq $63, " Q2 "\n" FLAGS                              \
    "    shldq $4, %rcx, " Q "\n" FLAGS                         \
    "    xorl %ecx, %ecx\n"                                     \
    "    rcrl %cl, " D "\n" FLAGS                               \
    "    movl $3, %ecx\n"                                       \
    "    rcrl %cl, " D "\n" FLAGS                               \
    "    shrq %cl, " Q2 "\n" FLAGS                              \
    "    movq %r9, %rcx\n"                                      \
    "    xaddq %rcx, " Q "\n" FLAGS                             \
    "    addq %rcx, %r8\n"                                      \
    "    movq " Q ", %rax\n"                                    \
    "    movq %r9, %rcx\n"                                      \
    "    cmpxchgq %rcx, " Q "\n" FLAGS                          \
    "    movq %r9, %rax\n"                                      \
    "    cmpxchgq %rcx, " Q2 "\n"                               \
    "    movq %rax, %r11\n" FLAGS                               \
    "    addq %r11, %r8\n"                                      \
    "    movq %r9, %rax\n"                                      \
    "    xchgq %rax, " Q2 "\n"                                  \
    "    addq %rax, %r8\n"                                      \
    "    lock addl %ecx, " D "\n" FLAGS                         \
    "    setc " B "\n"

__asm__(".text\n"
        ".globl rip_run\n"
        "rip_run:\n"
        "    pushq %r9\n"
        "    movq %rcx, %r9\n"
        "    xorl %r8d, %r8d\n" BODY("rip_cell(%rip)", "rip_cell+8(%rip)", "rip_cell+12(%rip)", "rip_cell+14(%rip)",
                                     "rip_cell+16(%rip)") /* an indirect call through [rip + ...], which pushes its return address */
        "    movl %r9d, %ecx\n"
        "    call *rip_fn(%rip)\n"
        "    addq %rax, %r8\n"
        "    incq rip_called(%rip)\n"
        "    movq %r8, %rax\n"
        "    popq %r9\n"
        "    ret\n"
        ".globl ind_run\n"
        "ind_run:\n"
        "    pushq %r9\n"
        "    pushq %r10\n"
        "    movq %rcx, %r9\n"
        "    movq %rdx, %r10\n"
        "    xorl %r8d, %r8d\n" BODY("(%r10)", "8(%r10)", "12(%r10)", "14(%r10)", "16(%r10)")
        "    movl %r9d, %ecx\n"
        "    call plus_one\n"
        "    addq %rax, %r8\n"
        "    movq %r8, %rax\n"
        "    popq %r10\n"
        "    popq %r9\n"
        "    ret\n"
        /* leaf code that touches no stack slot of its caller, so its callers skip Win64's home space */
        ".globl plus_one\n"
        "plus_one:\n"
        "    leal 1(%rcx), %eax\n"
        "    ret\n");

#define ROUNDS 10000

int main(void)
{
    const uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    int ok = 1, i, bad = 0, first_bad = -1;

    printf("gw2 test ripaddr: image at 0x%llx, %s 4 GB\n", (unsigned long long)base, base >> 32 ? "above" : "below");
    ok &= (base >> 32) != 0;

    rip_fn = plus_one;
    memset(&rip_cell, 0, sizeof(rip_cell));
    memset(&ind_cell, 0, sizeof(ind_cell));
    for (i = 0; i < ROUNDS; i++)
    {
        const uint64_t v = 0x9e3779b97f4a7c15ULL * (uint64_t)(i + 1);
        const uint64_t a = rip_run(v);
        const uint64_t b = ind_run(v, &ind_cell);
        if (a != b || memcmp(&rip_cell, &ind_cell, sizeof(rip_cell)) != 0)
        {
            if (first_bad < 0)
            {
                first_bad = i;
                printf("gw2 test ripaddr: round %d: flags 0x%llx and 0x%llx, q 0x%llx and 0x%llx, d 0x%x and 0x%x, q2 0x%llx and 0x%llx\n",
                       i, (unsigned long long)a, (unsigned long long)b, (unsigned long long)rip_cell.q, (unsigned long long)ind_cell.q,
                       rip_cell.d, ind_cell.d, (unsigned long long)rip_cell.q2, (unsigned long long)ind_cell.q2);
            }
            bad++;
            /* go on from equal cells */
            ind_cell = rip_cell;
        }
    }
    printf("gw2 test ripaddr: rmw: %d rounds of 26 instructions on [rip + ...] and through a register, %d different\n", ROUNDS, bad);
    ok &= !bad;

    printf("gw2 test ripaddr: calls: %llu of %d rounds came back through the indirect call\n", (unsigned long long)rip_called, ROUNDS);
    ok &= rip_called == ROUNDS;

    if (!ok)
        return 1;
    printf("gw2 test ripaddr: passed\n");
    return 0;
}
