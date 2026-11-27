/* gw2 (jdrly/gw2-on-mac#392): what native code finds in FPCR when x86 code calls it, for
 * FEX_EXITFPCRCHECK. A Win64 x86_64 program, built with llvm-mingw, next to fpcr.dll
 * (tests/gw2/arm64ec/fpcr.c, ARM64EC).
 *
 *   fpcr.exe
 *
 * With AFP in use (FEX_HOSTFEATURES=enableafp on ARM64EC) FEX sets FPCR.AH and FPCR.NEP each time it
 * enters JIT code, and the exit thunk has to clear both before native code runs, under every value
 * of FEX_EXITFPCRCHECK. The program calls gw2_fpcr 1,000 times, each call an exit from JIT code
 * into native code and its return an entry, and fails if any call saw AH (bit 1) or NEP (bit 2).
 * Only a CPU with FEAT_AFP keeps AH, so it first asks fpcr.dll whether this one does and prints
 * the answer: on one that doesn't, the check passes whatever the thunk does. The program does no
 * floating-point math of its own, which enableafp on such a CPU would get wrong. It ends with
 * "gw2 test fpcr: passed". */
#include <stdio.h>
#include <windows.h>

typedef unsigned long long (*fpcr_fn)(void);
typedef int (*keeps_ah_fn)(void);

int main(void)
{
    HMODULE dll = LoadLibraryA("fpcr.dll");
    if (!dll)
    {
        printf("gw2 test fpcr: can't load fpcr.dll (error %lu)\n", GetLastError());
        return 1;
    }
    fpcr_fn volatile fpcr = (fpcr_fn)GetProcAddress(dll, "gw2_fpcr");
    keeps_ah_fn keeps_ah = (keeps_ah_fn)GetProcAddress(dll, "gw2_fpcr_keeps_ah");
    if (!fpcr || !keeps_ah)
    {
        printf("gw2 test fpcr: fpcr.dll has no gw2_fpcr or gw2_fpcr_keeps_ah\n");
        return 1;
    }

    printf("gw2 test fpcr: this CPU keeps FPCR.AH: %s\n", keeps_ah() ? "yes" : "no");

    unsigned long long seen = 0, value = 0;
    unsigned set = 0;
    for (int i = 0; i < 1000; i++)
    {
        value = fpcr();
        if (value & 6)
        {
            set++;
            seen |= value;
        }
    }
    printf("gw2 test fpcr: FPCR in native code 0x%llx, AH or NEP set in %u of 1000 calls (0x%llx)\n", value, set, seen & 6);
    if (set)
        return 1;
    printf("gw2 test fpcr: passed\n");
    return 0;
}
