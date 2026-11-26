// codebuf.exe (jdrly/gw2-on-mac#112): makes FEX translate more code than its first code buffer
// holds, then runs the same code again, so a reader of FEX's stats can tell whether the second
// pass compiled it again.
//
// Win64 x86_64, built with llvm-mingw. It writes N functions, each `mov eax, imm32; ret` on its
// own 16 bytes, into one executable allocation, all before the first call: FEX write-protects a
// page once it has compiled code from it. Each function is a block of its own, so one pass makes
// N compiles. At a few hundred bytes of host code a block, one pass is 30 to 90 MB, over the
// 16 MB FEX starts with and well under the 128 MB a buffer can be.
//
// It prints, with a read of FEX's stats at each "gw2 test ready" (tests/gw2/gw2test.h):
//   codebuf: tid <thread id> functions <N>   the stats slot to read is this thread's
//   codebuf: pass 1 checksum <hex>
//   gw2 test ready                           read 1
//   codebuf: pass 2 checksum <hex>
//   gw2 test ready                           read 2
//   codebuf: passed                          when both passes gave the same checksum
// tests/gw2/codebuf_check.py judges the reads. The exit status is 0 with "passed", 1 otherwise.
#include <stdint.h>
#include <string.h>

#include "gw2test.h"

#define FUNCTIONS 196608u
#define STRIDE 16u

typedef uint32_t (*function)(void);

static uint32_t value(uint32_t i)
{
    return i * 2654435761u ^ 0x5a5a5a5au;
}

static uint64_t pass(const uint8_t *code)
{
    // FNV-1a over the results
    uint64_t sum = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < FUNCTIONS; i++)
        sum = (sum ^ ((function)(code + (size_t)i * STRIDE))()) * 0x100000001b3ull;
    return sum;
}

int main(void)
{
    uint8_t *code = VirtualAlloc(NULL, (size_t)FUNCTIONS * STRIDE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        printf("codebuf: VirtualAlloc failed, error %lu\n", GetLastError());
        return 1;
    }
    memset(code, 0xcc, (size_t)FUNCTIONS * STRIDE);
    for (uint32_t i = 0; i < FUNCTIONS; i++)
    {
        uint8_t *f = code + (size_t)i * STRIDE;
        uint32_t v = value(i);
        f[0] = 0xb8; // mov eax, imm32
        memcpy(f + 1, &v, sizeof(v));
        f[5] = 0xc3; // ret
    }
    FlushInstructionCache(GetCurrentProcess(), code, (size_t)FUNCTIONS * STRIDE);

    printf("codebuf: tid %lu functions %u\n", GetCurrentThreadId(), FUNCTIONS);
    uint64_t first = pass(code);
    printf("codebuf: pass 1 checksum %016llx\n", (unsigned long long)first);
    if (gw2_test_wait_for_reader())
        return 1;
    uint64_t second = pass(code);
    printf("codebuf: pass 2 checksum %016llx\n", (unsigned long long)second);
    if (gw2_test_wait_for_reader())
        return 1;
    if (first != second)
        return 1;
    printf("codebuf: passed\n");
    return 0;
}
