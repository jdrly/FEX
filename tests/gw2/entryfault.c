/* gw2 (jdrly/gw2-on-mac#162): faults and suspends inside translated code report the exact x86
 * address. A Win64 x86_64 program, built with llvm-mingw.
 *
 * With FEX_ENTRYHEADERLOOKUP=1 no block entry stores its header into
 * CPUState::InlineJITBlockHeader, and FEX finds the block a fault or suspend happened in from the
 * host PC. Without it, every entry stores the header. Both have to give the same answers:
 *
 *   first   a load from a no-access page as a function's first instruction, called through a
 *           pointer
 *   return  the same load as the first instruction after a call returns
 *   deep    the same load after some arithmetic, three calls deep
 *   loop    a load in a loop that has read 1,000,000 ints before the one in the no-access page
 *
 * Each runs three times. A vectored handler checks that ExceptionAddress and the context's Rip
 * are the faulting instruction's address, then resumes after that instruction. Each run has to fault
 * exactly once, and the function has to return what it computes around the fault.
 *
 *   smc     a function in an executable, writable page stores 2 into the immediate of its next
 *           instruction, "movl $1, %eax", and returns eax: 2, when FEX takes the store as a write
 *           into the block it runs in (inline self-modifying code). The page is reset to 1 before
 *           each of three calls.
 *
 *   suspend a thread spins in a loop that never leaves translated code. FEX checks for a suspend
 *           at block entries and unconditional backward jumps, not at a conditional backward
 *           branch, so the loop leaves through a forward branch and closes with a jmp. Under
 *           both FEXes a thread in a loop closed by "je" back can't be suspended at all: Wine's
 *           GetThreadContext waits for it for good (run 43452320908). The main thread suspends
 *           it 1,000 times, and each time its context has to show Rip inside the loop and its
 *           counter register equal to the counter in memory or one ahead (between the add and
 *           the store). After each resume the counter has to move on. At the end the thread
 *           returns its register, which has to equal the counter in memory.
 *
 * Prints a line per case and ends with "gw2 test entryfault: passed". */
#include <stdio.h>
#include <string.h>
#include <windows.h>

int ef_first(const volatile int *nowhere);
int ef_return(const volatile int *nowhere);
int ef_deep1(const volatile int *nowhere);
int ef_loop(const volatile int *ints, unsigned long long count);
long long ef_spin(volatile long long *counter, volatile LONG *stop);
extern const unsigned char ef_first_fault[], ef_return_fault[], ef_deep_fault[], ef_loop_fault[];
extern const unsigned char ef_spin_loop[], ef_spin_end[];

/* Every fault is "movl (%rcx), %eax" (8b 01) but the loop's "movl (%rcx,%r8,4), %r9d" (46 8b 0c 81). */
__asm__(".text\n"
        ".globl ef_first\n"
        "ef_first:\n"
        ".globl ef_first_fault\n"
        "ef_first_fault:\n"
        "  movl (%rcx), %eax\n"
        "  movl $11, %eax\n"
        "  ret\n"

        "ef_leaf:\n"
        "  movl $5, %edx\n"
        "  ret\n"

        ".globl ef_return\n"
        "ef_return:\n"
        "  subq $8, %rsp\n"
        "  call ef_leaf\n"
        ".globl ef_return_fault\n"
        "ef_return_fault:\n"
        "  movl (%rcx), %eax\n"
        "  leal 17(%rdx), %eax\n"
        "  addq $8, %rsp\n"
        "  ret\n"

        ".globl ef_deep1\n"
        "ef_deep1:\n"
        "  subq $8, %rsp\n"
        "  call ef_deep2\n"
        "  addl $100, %eax\n"
        "  addq $8, %rsp\n"
        "  ret\n"
        "ef_deep2:\n"
        "  subq $8, %rsp\n"
        "  call ef_deep3\n"
        "  addl $10, %eax\n"
        "  addq $8, %rsp\n"
        "  ret\n"
        "ef_deep3:\n"
        "  movl $3, %edx\n"
        "  imull $7, %edx, %edx\n"
        "  addl $2, %edx\n"
        ".globl ef_deep_fault\n"
        "ef_deep_fault:\n"
        "  movl (%rcx), %eax\n"
        "  movl %edx, %eax\n"
        "  ret\n"

        ".globl ef_loop\n"
        "ef_loop:\n"
        "  xorl %eax, %eax\n"
        "  xorl %r8d, %r8d\n"
        "1:\n"
        "  movl $1, %r9d\n"
        ".globl ef_loop_fault\n"
        "ef_loop_fault:\n"
        "  movl (%rcx,%r8,4), %r9d\n"
        "  addl %r9d, %eax\n"
        "  incq %r8\n"
        "  cmpq %rdx, %r8\n"
        "  jb 1b\n"
        "  ret\n"

        ".globl ef_spin\n"
        "ef_spin:\n"
        "  xorl %eax, %eax\n"
        ".globl ef_spin_loop\n"
        "ef_spin_loop:\n"
        "  incq %rax\n"
        "  movq %rax, (%rcx)\n"
        "  cmpl $0, (%rdx)\n"
        "  jne ef_spin_end\n"
        "  jmp ef_spin_loop\n"
        ".globl ef_spin_end\n"
        "ef_spin_end:\n"
        "  ret\n");

static volatile DWORD64 expected_rip;
static volatile DWORD64 fault_length;
static volatile LONG faults, wrong_faults;
static volatile DWORD64 wrong_address, wrong_rip;

static LONG CALLBACK on_fault(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !expected_rip)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD64 address = (DWORD64)e->ExceptionRecord->ExceptionAddress, rip = e->ContextRecord->Rip;
    InterlockedIncrement(&faults);
    if (address != expected_rip || rip != expected_rip)
    {
        InterlockedIncrement(&wrong_faults);
        wrong_address = address;
        wrong_rip = rip;
    }
    /* past the faulting instruction, also after a wrong answer, so the case can report it */
    e->ContextRecord->Rip = expected_rip + fault_length;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int failed;

/* Runs the case three times. Each run has to fault once at fault, with the handler's answers
 * right, and return want. */
static void fault_case(const char *name, const unsigned char *fault, DWORD64 length, int (*run)(void *), void *arg, int want)
{
    for (int round = 1; round <= 3; round++)
    {
        faults = wrong_faults = 0;
        fault_length = length;
        expected_rip = (DWORD64)fault;
        int got = run(arg);
        expected_rip = 0;
        if (faults != 1 || wrong_faults || got != want)
        {
            printf("gw2 test entryfault: %s round %d FAILED: %ld faults, %ld wrong (address %p, rip %p, wanted %p), returned %d, wanted %d\n",
                   name, round, faults, wrong_faults, (void *)wrong_address, (void *)wrong_rip, (const void *)fault, got, want);
            failed = 1;
            return;
        }
    }
    printf("gw2 test entryfault: %s faulted at %p three times, as it should\n", name, (const void *)fault);
}

static int (*volatile first_fn)(const volatile int *) = ef_first;
static int run_first(void *p) { return first_fn(p); }
static int run_return(void *p) { return ef_return(p); }
static int run_deep(void *p) { return ef_deep1(p); }

#define LOOP_INTS 1000000ULL
static volatile int *loop_ints;
static int run_loop(void *p)
{
    (void)p;
    return ef_loop(loop_ints, LOOP_INTS + 1);
}

/* movl $2, 1(%rip); movl $1, %eax; ret. The store's target is the next instruction's immediate. */
static const unsigned char smc_code[] = {0xc7, 0x05, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                                         0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};

static void smc_case(void)
{
    unsigned char *code = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        printf("gw2 test entryfault: smc FAILED: no executable page (error %lu)\n", GetLastError());
        failed = 1;
        return;
    }
    memcpy(code, smc_code, sizeof(smc_code));
    int (*volatile fn)(void) = (int (*)(void))code;
    for (int round = 1; round <= 3; round++)
    {
        code[11] = 1;
        FlushInstructionCache(GetCurrentProcess(), code, sizeof(smc_code));
        int got = fn();
        if (got != 2 || code[11] != 2)
        {
            printf("gw2 test entryfault: smc round %d FAILED: returned %d, immediate %d, wanted 2 and 2\n", round, got, code[11]);
            failed = 1;
            return;
        }
    }
    printf("gw2 test entryfault: smc saw its own store three times\n");
}

static volatile long long spin_counter;
static volatile LONG spin_stop;
static long long spin_result;

static DWORD WINAPI spin_thread(void *arg)
{
    (void)arg;
    spin_result = ef_spin(&spin_counter, &spin_stop);
    return 0;
}

static void suspend_case(void)
{
    HANDLE thread = CreateThread(NULL, 0, spin_thread, NULL, 0, NULL);
    if (!thread)
    {
        printf("gw2 test entryfault: suspend FAILED: CreateThread error %lu\n", GetLastError());
        failed = 1;
        return;
    }
    while (spin_counter < 1000)
        Sleep(1);

    for (int i = 1; i <= 1000; i++)
    {
        CONTEXT ctx = {.ContextFlags = CONTEXT_FULL};
        if (SuspendThread(thread) == (DWORD)-1 || !GetThreadContext(thread, &ctx))
        {
            printf("gw2 test entryfault: suspend FAILED at %d: error %lu\n", i, GetLastError());
            failed = 1;
            break;
        }
        long long memory = spin_counter;
        long long reg = (long long)ctx.Rax;
        if (ctx.Rip < (DWORD64)ef_spin_loop || ctx.Rip >= (DWORD64)ef_spin_end || ctx.Rcx != (DWORD64)&spin_counter ||
            ctx.Rdx != (DWORD64)&spin_stop || (reg != memory && reg != memory + 1))
        {
            printf("gw2 test entryfault: suspend FAILED at %d: rip %p (loop %p to %p), rax %lld, counter %lld, rcx %p, rdx %p\n", i,
                   (void *)ctx.Rip, (const void *)ef_spin_loop, (const void *)ef_spin_end, reg, memory, (void *)ctx.Rcx, (void *)ctx.Rdx);
            failed = 1;
            ResumeThread(thread);
            break;
        }
        if (ResumeThread(thread) == (DWORD)-1)
        {
            printf("gw2 test entryfault: suspend FAILED at %d: ResumeThread error %lu\n", i, GetLastError());
            failed = 1;
            break;
        }
        for (long long seen = spin_counter; spin_counter == seen;)
            SwitchToThread();
    }

    spin_stop = 1;
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0)
    {
        printf("gw2 test entryfault: suspend FAILED: the spinning thread didn't end\n");
        failed = 1;
        return;
    }
    CloseHandle(thread);
    if (spin_result != spin_counter)
    {
        printf("gw2 test entryfault: suspend FAILED: the thread returned %lld, the counter is %lld\n", spin_result, spin_counter);
        failed = 1;
        return;
    }
    if (!failed)
        printf("gw2 test entryfault: suspended 1,000 times in the loop, counter %lld\n", spin_counter);
}

int main(void)
{
    /* each line reaches the log at once, also from a run that hangs */
    setvbuf(stdout, NULL, _IONBF, 0);

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const SIZE_T page = si.dwPageSize;
    const SIZE_T bytes = (LOOP_INTS * sizeof(int) + page - 1) / page * page;

    /* the ints, then one no-access page; the loop's ints end where that page starts */
    unsigned char *memory = VirtualAlloc(NULL, bytes + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (!memory || !VirtualProtect(memory + bytes, page, PAGE_NOACCESS, &old))
    {
        printf("gw2 test entryfault: can't set up memory (error %lu)\n", GetLastError());
        return 1;
    }
    volatile int *nowhere = (volatile int *)(memory + bytes);
    loop_ints = (volatile int *)(memory + bytes - LOOP_INTS * sizeof(int));
    for (unsigned long long i = 0; i < LOOP_INTS; i++)
        loop_ints[i] = 1;

    if (!AddVectoredExceptionHandler(1, on_fault))
    {
        printf("gw2 test entryfault: no vectored handler\n");
        return 1;
    }

    fault_case("first", ef_first_fault, 2, run_first, (void *)nowhere, 11);
    fault_case("return", ef_return_fault, 2, run_return, (void *)nowhere, 22);
    fault_case("deep", ef_deep_fault, 2, run_deep, (void *)nowhere, 133);
    /* the faulting iteration adds the 1 its register held before the load */
    fault_case("loop", ef_loop_fault, 4, run_loop, NULL, (int)LOOP_INTS + 1);
    smc_case();
    suspend_case();

    if (failed)
        return 1;
    printf("gw2 test entryfault: passed\n");
    return 0;
}
