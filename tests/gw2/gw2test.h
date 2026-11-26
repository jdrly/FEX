/* gw2 (jdrly/gw2-on-mac#109): what a Win64 test program shares with tests/gw2/run.zsh.
 *
 * A program that a native reader has to look at while it runs (a stats case in
 * tests/gw2/cases.zsh) calls gw2_test_wait_for_reader() each time it wants to be read: after its
 * work, or before and after a part of it. Each call prints the line "gw2 test ready", on which
 * run.zsh runs the reader once more (its output goes to <label>.reader.<n>, n counting from 1),
 * waits until run.zsh creates the file GW2_TEST_FLAG names (a Windows path, Z:\ being the Mac's
 * root), for two minutes at most, and deletes it, so the next call waits again. FEX deletes its
 * stats object when the process ends, so every read has to come before that. */
#ifndef GW2TEST_H
#define GW2TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

/* 0 once run.zsh's reader has read, 1 without GW2_TEST_FLAG, when the file never came or when it
 * can't be deleted */
static int gw2_test_wait_for_reader(void)
{
    const char *flag = getenv("GW2_TEST_FLAG");
    if (!flag)
    {
        printf("gw2 test: no GW2_TEST_FLAG to wait for\n");
        return 1;
    }
    printf("gw2 test ready\n");
    fflush(stdout);
    for (int i = 0; i < 1200; i++)
    {
        if (GetFileAttributesA(flag) != INVALID_FILE_ATTRIBUTES)
        {
            if (DeleteFileA(flag))
                return 0;
            printf("gw2 test: can't delete %s (error %lu)\n", flag, GetLastError());
            return 1;
        }
        Sleep(100);
    }
    printf("gw2 test: %s never came\n", flag);
    return 1;
}

#endif
