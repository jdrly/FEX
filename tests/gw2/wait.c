/* gw2 (jdrly/gw2-on-mac#109): a Win64 program that does nothing under FEX but stay alive until the
 * reader has read FEX's stats object (tests/gw2/gw2test.h). */
#include "gw2test.h"

int main(void)
{
    if (gw2_test_wait_for_reader())
        return 1;
    printf("gw2 test wait: passed\n");
    return 0;
}
