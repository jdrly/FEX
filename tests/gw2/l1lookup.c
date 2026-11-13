/* gw2 (jdrly/gw2-on-mac#161): native code calling an x86 callback, the entry into JIT code that
 * FEX_DISPATCHERL1LOOKUP serves from the dispatcher instead of from C++. A Win64 x86_64 program,
 * built with llvm-mingw.
 *
 * qsort from ucrtbase.dll, an ARM64EC module, sorts arrays of 1,000 ints with a comparator in this
 * program until the comparator has run at least 100,000 times. Each comparison is an entry from
 * native code into the comparator and a return into native code. The comparator was compiled in
 * a first sort before the measured ones, so each entry finds it in the thread's L1. With the
 * switch off the dispatcher doesn't look in L1, and each entry is a lookup in C++ that L1 answers
 * (l1_hits in fexstats). With it on, the dispatcher finds the comparator itself.
 *
 * Prints
 *   gw2 test l1lookup: tid <main thread id>
 *   gw2 test l1lookup: calls <comparator calls between the two reads>
 * and "gw2 test l1lookup: passed" when every sort came out sorted. Read 1 comes after the first
 * sort, read 2 after the measured ones. Only the main thread sorts. */
#include "gw2test.h"

#define COUNT 1000
#define CALLS 100000

typedef void(__cdecl *qsort_fn)(void *, size_t, size_t, int(__cdecl *)(const void *, const void *));

static long calls;
static int values[COUNT];

static int __cdecl compare(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    calls++;
    return (x > y) - (x < y);
}

/* fills values from an LCG and sorts them; 1 when they came out sorted */
static int sort(qsort_fn sorter, unsigned *seed)
{
    for (int i = 0; i < COUNT; i++)
    {
        *seed = *seed * 1664525u + 1013904223u;
        values[i] = (int)(*seed >> 8);
    }
    sorter(values, COUNT, sizeof(values[0]), compare);
    for (int i = 1; i < COUNT; i++)
        if (values[i - 1] > values[i])
            return 0;
    return 1;
}

int main(void)
{
    HMODULE ucrt = LoadLibraryA("ucrtbase.dll");
    qsort_fn sorter = ucrt ? (qsort_fn)GetProcAddress(ucrt, "qsort") : NULL;
    unsigned seed = 12345;
    int ok;

    if (!sorter)
    {
        printf("gw2 test l1lookup: no qsort in ucrtbase.dll, error %lu\n", GetLastError());
        return 1;
    }
    printf("gw2 test l1lookup: tid %lu\n", GetCurrentThreadId());
    ok = sort(sorter, &seed);
    if (gw2_test_wait_for_reader())
        return 1;

    calls = 0;
    while (calls < CALLS)
        ok &= sort(sorter, &seed);
    printf("gw2 test l1lookup: calls %ld\n", calls);
    if (gw2_test_wait_for_reader())
        return 1;

    if (!ok)
    {
        printf("gw2 test l1lookup: a sort came out unsorted\n");
        return 1;
    }
    printf("gw2 test l1lookup: passed\n");
    return 0;
}
