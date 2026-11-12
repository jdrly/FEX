/* gw2 (jdrly/gw2-on-mac#132): a Win64 program that hangs on purpose. It prints one line and sleeps
 * for good, so run.zsh's watchdog has to end it and leave a sample of where it stood. The case in
 * tests/gw2/cases.zsh gives it a short limit. */
#include <stdio.h>
#include <windows.h>

int main(void)
{
    printf("gw2 test hang: sleeping\n");
    fflush(stdout);
    Sleep(INFINITE);
    return 0;
}
