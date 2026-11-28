/* gw2 (jdrly/gw2-on-mac#392): native ARM64EC functions for tests/gw2/fpcr.c, built with llvm-mingw
 * for ARM64EC as fpcr.dll. x86 code that calls one leaves the JIT through FEX's exit thunk
 * (ExitFunctionEC in Source/Windows/ARM64EC/Module.S), so each reads FPCR as native code gets it. */

/* FPCR as this native function finds it */
__declspec(dllexport) unsigned long long gw2_fpcr(void)
{
    unsigned long long fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
}

/* 1 if this CPU keeps FPCR.AH once it is written, which it does only with FEAT_AFP: without it the
 * bit reads as 0 and the exit thunk has nothing to clear. FPCR is restored before it returns. */
__declspec(dllexport) int gw2_fpcr_keeps_ah(void)
{
    unsigned long long old, set;
    __asm__ volatile("mrs %0, fpcr" : "=r"(old));
    __asm__ volatile("msr fpcr, %0" : : "r"(old | 2));
    __asm__ volatile("mrs %0, fpcr" : "=r"(set));
    __asm__ volatile("msr fpcr, %0" : : "r"(old));
    return (set & 2) != 0;
}
