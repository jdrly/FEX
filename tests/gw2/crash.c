/* gw2 (jdrly/gw2-on-mac#132): a Win64 program that crashes on purpose. It prints one line and
 * writes to address 16, an unhandled page fault. Wine then starts winedbg --auto, which with the
 * prefix's crash dialog off (tests/gw2/run.zsh) prints the exception and a backtrace into the log
 * and lets the program end. The case in tests/gw2/cases.zsh has to fail within seconds, not at its
 * time limit. */
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    volatile int *nowhere = (volatile int *)(uintptr_t)16;

    printf("gw2 test crash: crashing\n");
    fflush(stdout);
    *nowhere = 1;
    printf("gw2 test crash: still here\n");
    return 0;
}
