/* gw2 (jdrly/gw2-on-mac#390): FEX_ABIFLAGS acts on the main executable's .text and nowhere else. A
 * Win64 x86_64 program, built with llvm-mingw.
 *
 * The Windows x64 ABI leaves the arithmetic flags undefined across a call and a return, and with
 * the switch FEX stops computing the ones only a call or a return would carry. Hand-written code
 * can still pass a flag across either, so the routines below sit in a section of their own,
 * .abiflag, outside .text, where FEX has to keep every flag:
 *
 *   - ret: out_stc, out_clc and out_cmp set CF or ZF and return. Their caller, in .text, reads the
 *     flag right after the call.
 *   - call: out_call_stc and out_call_clc set CF and call out_getc, which returns CF in eax.
 *
 * 1,000 rounds of each, with the flag alternating. Prints the image's .text range, which the case
 * compares with the range FEX logs, then a line for each part and "gw2 test abiflags: passed". */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

void out_stc(void);
void out_clc(void);
void out_cmp(int a, int b);
int out_call_stc(void);
int out_call_clc(void);

/* In .text: call fn, return CF (in_carry) or ZF (in_zero) as it came back */
int in_carry(void (*fn)(void));
int in_zero(int a, int b);

__asm__(".section .abiflag,\"xr\"\n"
        ".globl out_stc\n"
        "out_stc:\n"
        "    stc\n"
        "    ret\n"
        ".globl out_clc\n"
        "out_clc:\n"
        "    clc\n"
        "    ret\n"
        ".globl out_cmp\n"
        "out_cmp:\n"
        "    cmpl %edx, %ecx\n"
        "    ret\n"
        "out_getc:\n"
        "    setc %al\n"
        "    movzbl %al, %eax\n"
        "    ret\n"
        ".globl out_call_stc\n"
        "out_call_stc:\n"
        "    stc\n"
        "    call out_getc\n"
        "    ret\n"
        ".globl out_call_clc\n"
        "out_call_clc:\n"
        "    clc\n"
        "    call out_getc\n"
        "    ret\n"
        ".text\n"
        ".globl in_carry\n"
        "in_carry:\n"
        "    subq $40, %rsp\n"
        "    call *%rcx\n"
        "    setc %al\n"
        "    movzbl %al, %eax\n"
        "    addq $40, %rsp\n"
        "    ret\n"
        ".globl in_zero\n"
        "in_zero:\n"
        "    subq $40, %rsp\n"
        "    call out_cmp\n"
        "    sete %al\n"
        "    movzbl %al, %eax\n"
        "    addq $40, %rsp\n"
        "    ret\n");

/* The section of the image that holds p, by name, with its range */
static const char *section_of(const void *p, uintptr_t *begin, uintptr_t *end)
{
    static char name[9];
    const uint8_t *base = (const uint8_t *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
    {
        uintptr_t b = (uintptr_t)base + s->VirtualAddress, e = b + s->Misc.VirtualSize;
        if ((uintptr_t)p >= b && (uintptr_t)p < e)
        {
            memcpy(name, s->Name, 8);
            *begin = b;
            *end = e;
            return name;
        }
    }
    return "none";
}

int main(void)
{
    int failed = 0;
    uintptr_t tb, te, ob, oe;
    const char *in = section_of((const void *)in_carry, &tb, &te);
    const char *out = section_of((const void *)out_stc, &ob, &oe);
    printf("gw2 test abiflags: .text %llx-%llx\n", (unsigned long long)tb, (unsigned long long)te);
    printf("gw2 test abiflags: the callers in %s, the routines in %s %llx-%llx\n", in, out, (unsigned long long)ob,
           (unsigned long long)oe);
    if (strcmp(in, ".text") != 0 || strcmp(out, ".abiflag") != 0)
    {
        printf("gw2 test abiflags: the sections aren't the ones this test needs\n");
        failed++;
    }

    int ret_bad = 0, call_bad = 0;
    for (int i = 0; i < 1000; i++)
    {
        int set = i & 1;
        if (in_carry(set ? out_stc : out_clc) != set)
            ret_bad++;
        if (in_zero(i, set ? i : i + 1) != set)
            ret_bad++;
        if ((set ? out_call_stc() : out_call_clc()) != set)
            call_bad++;
    }
    printf("gw2 test abiflags: ret: %d of 2000 flags wrong\n", ret_bad);
    printf("gw2 test abiflags: call: %d of 1000 flags wrong\n", call_bad);
    failed += ret_bad + call_bad;

    if (failed)
    {
        printf("gw2 test abiflags: failed\n");
        return 1;
    }
    printf("gw2 test abiflags: passed\n");
    return 0;
}
