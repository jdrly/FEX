/* gw2 (jdrly/gw2-on-mac#395): a compare with zero and the jz or jnz after it, folded into one cbz or
 * cbnz under FEX_FOLDBRANCHPASTMARKERS, and the float compares whose axflag the same switch folds.
 * A Win64 x86_64 program, built with llvm-mingw.
 *
 * Each function compares, branches, and on both exits loads from the pointer it gets, then writes
 * every flag (xor) before it returns, so the flags are dead on both exits and FEX may fold. The
 * fall-through returns 1 and the taken exit 2.
 *
 *   z32  test ecx, ecx; jz     nz64  test rcx, rcx; jnz
 *   e32  cmp ecx, 0; je        ne64  cmp rcx, 0; jne
 *   late test ecx, ecx; mov ecx, r9d; jz: the jump has to see ecx from before the mov. It doesn't
 *        fold today, since the mov's code sits between the compare and the jump, and it guards the
 *        day it does
 *   a, ae, b, be  ucomiss xmm0, xmm1 and ja, jae, jb, jbe, with an unordered pair among the inputs
 *   le   ucomiss xmm0, xmm1; jle, which x86 takes on ZF alone after ucomiss (equal or unordered)
 *        and arm64's LE after fcmp also on less than, so its axflag has to stay
 *
 * Each runs on inputs that take each exit, rcx's upper half set under a 32-bit compare among
 * them, first with a readable pointer, then three times with a pointer into a no-access page. A
 * vectored handler checks that ExceptionAddress and the context's Rip are the load of the exit x86
 * takes, and resumes after it. Each faulting run has to fault exactly once and return what the
 * readable run returned.
 *
 * The handler prints the arithmetic flags it saw next to x86's, and asserts nothing about them.
 * An integer fold drops the compare, so the flags are the ones from before it. A float fold keeps
 * fcmp and drops axflag, so they are fcmp's arm64 flags read as x86's (docs/gw2/adr/0015).
 *
 * Prints a line per function and ends with "gw2 test foldbranch: passed". */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

typedef int (*int_fn)(uint64_t value, const volatile int *p, uint64_t unused, uint64_t late);
typedef int (*float_fn)(float a, float b, const volatile int *p);

/* Every exit's first instruction is "movl (%rdx), %eax" (8b 02), or "movl (%r8), %eax" (41 8b 00)
 * in the float functions, whose pointer is the third argument. */
#define INT_FN(name, compare, jump)        \
    ".globl fb_" name "\n"                 \
    "fb_" name ":\n"                       \
    "  " compare "\n"                      \
    "  " jump " 1f\n"                      \
    ".globl fb_" name "_fall\n"            \
    "fb_" name "_fall:\n"                  \
    "  movl (%rdx), %eax\n"                \
    "  xorl %eax, %eax\n"                  \
    "  incl %eax\n"                        \
    "  ret\n"                              \
    "1:\n"                                 \
    ".globl fb_" name "_taken\n"           \
    "fb_" name "_taken:\n"                 \
    "  movl (%rdx), %eax\n"                \
    "  xorl %eax, %eax\n"                  \
    "  addl $2, %eax\n"                    \
    "  ret\n"

#define FLOAT_FN(name, jump)               \
    ".globl fb_" name "\n"                 \
    "fb_" name ":\n"                       \
    "  ucomiss %xmm1, %xmm0\n"             \
    "  " jump " 1f\n"                      \
    ".globl fb_" name "_fall\n"            \
    "fb_" name "_fall:\n"                  \
    "  movl (%r8), %eax\n"                 \
    "  xorl %eax, %eax\n"                  \
    "  incl %eax\n"                        \
    "  ret\n"                              \
    "1:\n"                                 \
    ".globl fb_" name "_taken\n"           \
    "fb_" name "_taken:\n"                 \
    "  movl (%r8), %eax\n"                 \
    "  xorl %eax, %eax\n"                  \
    "  addl $2, %eax\n"                    \
    "  ret\n"

__asm__(".text\n"
        INT_FN("z32", "testl %ecx, %ecx", "jz")
        INT_FN("nz64", "testq %rcx, %rcx", "jnz")
        INT_FN("e32", "cmpl $0, %ecx", "je")
        INT_FN("ne64", "cmpq $0, %rcx", "jne")
        INT_FN("late", "testl %ecx, %ecx\n  movl %r9d, %ecx", "jz")
        FLOAT_FN("a", "ja")
        FLOAT_FN("ae", "jae")
        FLOAT_FN("b", "jb")
        FLOAT_FN("be", "jbe")
        FLOAT_FN("le", "jle"));

#define DECLARE(name, type)                                         \
    type##_body fb_##name;                                          \
    extern const unsigned char fb_##name##_fall[], fb_##name##_taken[];
typedef int int_fn_body(uint64_t value, const volatile int *p, uint64_t unused, uint64_t late);
typedef int float_fn_body(float a, float b, const volatile int *p);
DECLARE(z32, int_fn)
DECLARE(nz64, int_fn)
DECLARE(e32, int_fn)
DECLARE(ne64, int_fn)
DECLARE(late, int_fn)
DECLARE(a, float_fn)
DECLARE(ae, float_fn)
DECLARE(b, float_fn)
DECLARE(be, float_fn)
DECLARE(le, float_fn)

/* x86's arithmetic flags: CF, PF, AF, ZF, SF, OF */
#define ARITH_FLAGS 0x8d5

static volatile DWORD64 expected_rip, fault_length;
static volatile LONG faults, wrong_faults;
static volatile DWORD64 wrong_address, wrong_rip, seen_flags;

static LONG CALLBACK on_fault(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !expected_rip)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD64 address = (DWORD64)e->ExceptionRecord->ExceptionAddress, rip = e->ContextRecord->Rip;
    InterlockedIncrement(&faults);
    seen_flags = e->ContextRecord->EFlags & ARITH_FLAGS;
    if (address != expected_rip || rip != expected_rip)
    {
        InterlockedIncrement(&wrong_faults);
        wrong_address = address;
        wrong_rip = rip;
    }
    /* past the load, also after a wrong answer, so the case can report it */
    e->ContextRecord->Rip = expected_rip + fault_length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int failed;
static volatile int readable = 7;
static volatile int *nowhere;
static unsigned flag_lines;

/* One input: the readable run, then three faulting runs. want is 1 (fall through) or 2 (taken),
 * x86_flags x86's arithmetic flags after the compare. */
static void one(const char *name, const char *input, int (*run)(const volatile int *, const void *), const void *arg,
                const unsigned char *fall, const unsigned char *taken, DWORD64 length, int want, DWORD64 x86_flags)
{
    int got = run(&readable, arg);
    if (got != want)
    {
        printf("gw2 test foldbranch: %s %s FAILED: returned %d, wanted %d (1 falls through, 2 is taken)\n", name, input, got,
               want);
        failed = 1;
        return;
    }
    const unsigned char *load = want == 2 ? taken : fall;
    for (int round = 1; round <= 3; round++)
    {
        faults = wrong_faults = 0;
        seen_flags = 0;
        fault_length = length;
        expected_rip = (DWORD64)load;
        got = run(nowhere, arg);
        expected_rip = 0;
        if (faults != 1 || wrong_faults || got != want)
        {
            printf("gw2 test foldbranch: %s %s round %d FAILED: %ld faults, %ld wrong (address %p, rip %p, wanted %p), "
                   "returned %d, wanted %d\n",
                   name, input, round, faults, wrong_faults, (void *)wrong_address, (void *)wrong_rip, (const void *)load, got,
                   want);
            failed = 1;
            return;
        }
    }
    flag_lines++;
    printf("gw2 test foldbranch: %s %s: flags at the fault 0x%03llx, x86 0x%03llx%s (not checked)\n", name, input,
           (unsigned long long)seen_flags, (unsigned long long)x86_flags, seen_flags == x86_flags ? "" : ", differ");
}

struct int_arg
{
    int_fn fn;
    uint64_t value, late;
};

static int run_int(const volatile int *p, const void *arg)
{
    const struct int_arg *a = arg;
    return a->fn(a->value, p, 0, a->late);
}

struct float_arg
{
    float_fn fn;
    float a, b;
};

static int run_float(const volatile int *p, const void *arg)
{
    const struct float_arg *a = arg;
    return a->fn(a->a, a->b, p);
}

/* test or cmp with 0 at a width: ZF, SF and PF of the value, CF and OF clear. AF is undefined
 * after test and clear after cmp with 0; both count as clear here. */
static DWORD64 int_flags(uint64_t value, int bits)
{
    uint64_t v = bits == 32 ? (uint32_t)value : value;
    DWORD64 f = 0;
    if (!v)
        f |= 0x40;
    if (v >> (bits - 1) & 1)
        f |= 0x80;
    if (!(__builtin_popcount((unsigned)(v & 0xff)) & 1))
        f |= 0x4;
    return f;
}

static void int_case(const char *name, int_fn fn, const unsigned char *fall, const unsigned char *taken, int bits,
                     int taken_if_zero)
{
    static const uint64_t values[] = {0, 5, 0x100000000ULL, 0xffffffffffffffffULL, 0x8000000000000000ULL};
    static const char *const names[] = {"0", "5", "2^32", "-1", "2^63"};
    unsigned before = flag_lines;
    for (int i = 0; i < 5; i++)
    {
        uint64_t v = bits == 32 ? (uint32_t)values[i] : values[i];
        struct int_arg arg = {fn, values[i], 0};
        int want = (v == 0) == taken_if_zero ? 2 : 1;
        one(name, names[i], run_int, &arg, fall, taken, 2, want, int_flags(values[i], bits));
    }
    if (flag_lines - before == 5)
        printf("gw2 test foldbranch: %s took the exits x86 takes, and faulted at their loads\n", name);
}

/* test ecx, ecx; mov ecx, r9d; jz: the jump goes by the old ecx */
static void late_case(void)
{
    static const struct
    {
        uint64_t value, late;
        const char *name;
    } inputs[] = {{0, 1, "0 then 1"}, {1, 0, "1 then 0"}, {0x100000000ULL, 3, "2^32 then 3"}};
    unsigned before = flag_lines;
    for (int i = 0; i < 3; i++)
    {
        struct int_arg arg = {fb_late, inputs[i].value, inputs[i].late};
        int want = (uint32_t)inputs[i].value == 0 ? 2 : 1;
        one("late", inputs[i].name, run_int, &arg, fb_late_fall, fb_late_taken, 2, want, int_flags(inputs[i].value, 32));
    }
    if (flag_lines - before == 3)
        printf("gw2 test foldbranch: late went by the register before the mov\n");
}

/* ucomiss a, b: unordered sets ZF, PF and CF, a < b CF, a == b ZF; OF, SF and AF are cleared */
static DWORD64 float_flags(float a, float b)
{
    if (isnan(a) || isnan(b))
        return 0x45;
    return a < b ? 0x1 : a == b ? 0x40 : 0;
}

static void float_case(const char *name, float_fn fn, const unsigned char *fall, const unsigned char *taken)
{
    const float nan = NAN;
    const struct
    {
        float a, b;
        const char *name;
    } inputs[] = {{1, 2, "1 < 2"}, {2, 1, "2 > 1"}, {1, 1, "1 == 1"}, {nan, 1, "NaN, 1"}, {1, nan, "1, NaN"}};
    unsigned before = flag_lines;
    for (int i = 0; i < 5; i++)
    {
        DWORD64 f = float_flags(inputs[i].a, inputs[i].b);
        int cf = f & 1, zf = !!(f & 0x40), cond;
        if (fn == fb_a)
            cond = !cf && !zf;
        else if (fn == fb_ae)
            cond = !cf;
        else if (fn == fb_b)
            cond = cf;
        else if (fn == fb_be)
            cond = cf || zf;
        else /* jle: ZF, or SF != OF, both clear after ucomiss */
            cond = zf;
        struct float_arg arg = {fn, inputs[i].a, inputs[i].b};
        one(name, inputs[i].name, run_float, &arg, fall, taken, 3, cond ? 2 : 1, f);
    }
    if (flag_lines - before == 5)
        printf("gw2 test foldbranch: %s took the exits x86 takes, and faulted at their loads\n", name);
}

int main(void)
{
    /* each line reaches the log at once, also from a run that hangs */
    setvbuf(stdout, NULL, _IONBF, 0);

    nowhere = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!nowhere)
    {
        printf("gw2 test foldbranch: no no-access page (error %lu)\n", GetLastError());
        return 1;
    }
    if (!AddVectoredExceptionHandler(1, on_fault))
    {
        printf("gw2 test foldbranch: no vectored handler\n");
        return 1;
    }

    int_case("z32", fb_z32, fb_z32_fall, fb_z32_taken, 32, 1);
    int_case("nz64", fb_nz64, fb_nz64_fall, fb_nz64_taken, 64, 0);
    int_case("e32", fb_e32, fb_e32_fall, fb_e32_taken, 32, 1);
    int_case("ne64", fb_ne64, fb_ne64_fall, fb_ne64_taken, 64, 0);
    late_case();
    float_case("a", fb_a, fb_a_fall, fb_a_taken);
    float_case("ae", fb_ae, fb_ae_fall, fb_ae_taken);
    float_case("b", fb_b, fb_b_fall, fb_b_taken);
    float_case("be", fb_be, fb_be_fall, fb_be_taken);
    float_case("le", fb_le, fb_le_fall, fb_le_taken);

    if (failed)
        return 1;
    printf("gw2 test foldbranch: passed\n");
    return 0;
}
