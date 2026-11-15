/* gw2 (jdrly/gw2-on-mac#118): every way the game can ask whether it has AVX, and a small kernel
 * run on its SSE2 path and on its AVX2 path. A Win64 x86_64 program, built with llvm-mingw.
 *
 * GW2_AVX_EXPECT says what FEX should report: "seen" (the default, AVX on) or "hidden"
 * (FEX_HOSTFEATURES=disableavx). FEX answers CPUID leaves 1, 7 and 0xD, XGETBV,
 * IsProcessorFeaturePresent and SystemCpuInformation's feature bits, and all of them have to
 * agree with GW2_AVX_EXPECT. Wine answers the shared data page's ProcessorFeatures and XState
 * mask and GetEnabledXStateFeatures. Hidden, none of those may report AVX either. Seen, they are
 * printed and not judged: Wine 11.21 on arm64 reports no AVX there, while FEX does.
 *
 * The kernel is a dot product of int16 arrays (pmaddwd) and an elementwise a * b + c of floats,
 * checked against plain C. The SSE2 path always runs. The AVX2 path runs only when CPUID, XGETBV
 * and IsProcessorFeaturePresent all say AVX2, the way a game picks it, and has to run when seen.
 * Prints each answer on a line of its own and "gw2 test avx: passed" when all of it held. */
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define N 4096

/* SYSTEM_CPU_INFORMATION and its feature bits as Wine has them (include/winternl.h) */
typedef struct
{
    USHORT ProcessorArchitecture;
    USHORT ProcessorLevel;
    USHORT ProcessorRevision;
    USHORT MaximumProcessors;
    ULONG ProcessorFeatureBits;
} cpu_information;
#define CPU_FEATURE_AVX 0x40000000
#define CPU_FEATURE_AVX2 0x80000000
typedef LONG(WINAPI *query_fn)(int, void *, ULONG, ULONG *);
typedef DWORD64(WINAPI *xstate_fn)(void);

/* KUSER_SHARED_DATA: ProcessorFeatures at 0x274, XState.EnabledFeatures at 0x3d8 */
#define SHARED_DATA 0x7ffe0000
#define PF_XSAVE 17
#define PF_AVX 39
#define PF_AVX2 40

static short xs[N], ys[N];
static float fa[N], fb[N], fc[N], out_c[N], out_sse[N], out_avx[N];

static int failures;

/* CPUID by hand: cpuid.h's __cpuid macro and mingw's __cpuid function don't mix */
static void cpuid(unsigned leaf, unsigned subleaf, unsigned r[4])
{
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(subleaf));
}

/* one answer: what it says, and whether it has to agree with expect */
static void answer(const char *name, int says, int judged, int expect)
{
    int ok = !judged || says == expect;
    printf("gw2 test avx: %s %d%s\n", name, says, judged ? (ok ? "" : " (wrong)") : " (not judged)");
    if (!ok)
        failures++;
}

static long long dot_c(void)
{
    long long sum = 0;
    for (int i = 0; i < N; i++)
        sum += xs[i] * ys[i];
    return sum;
}

static long long dot_sse2(void)
{
    __m128i acc = _mm_setzero_si128();
    for (int i = 0; i < N; i += 8)
        acc = _mm_add_epi32(acc, _mm_madd_epi16(_mm_loadu_si128((const __m128i *)&xs[i]), _mm_loadu_si128((const __m128i *)&ys[i])));
    int lanes[4];
    _mm_storeu_si128((__m128i *)lanes, acc);
    return (long long)lanes[0] + lanes[1] + lanes[2] + lanes[3];
}

__attribute__((target("avx2"))) static long long dot_avx2(void)
{
    __m256i acc = _mm256_setzero_si256();
    for (int i = 0; i < N; i += 16)
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(_mm256_loadu_si256((const __m256i *)&xs[i]), _mm256_loadu_si256((const __m256i *)&ys[i])));
    int lanes[8];
    _mm256_storeu_si256((__m256i *)lanes, acc);
    long long sum = 0;
    for (int i = 0; i < 8; i++)
        sum += lanes[i];
    return sum;
}

static void madd_c(void)
{
    for (int i = 0; i < N; i++)
    {
        volatile float product = fa[i] * fb[i];
        out_c[i] = product + fc[i];
    }
}

static void madd_sse(void)
{
    for (int i = 0; i < N; i += 4)
        _mm_storeu_ps(&out_sse[i], _mm_add_ps(_mm_mul_ps(_mm_loadu_ps(&fa[i]), _mm_loadu_ps(&fb[i])), _mm_loadu_ps(&fc[i])));
}

__attribute__((target("avx"))) static void madd_avx(void)
{
    for (int i = 0; i < N; i += 8)
        _mm256_storeu_ps(&out_avx[i], _mm256_add_ps(_mm256_mul_ps(_mm256_loadu_ps(&fa[i]), _mm256_loadu_ps(&fb[i])), _mm256_loadu_ps(&fc[i])));
}

int main(void)
{
    const char *want = getenv("GW2_AVX_EXPECT");
    int hidden = want && !strcmp(want, "hidden");
    int expect = !hidden;
    unsigned r[4];

    if (want && !hidden && strcmp(want, "seen"))
    {
        printf("gw2 test avx: GW2_AVX_EXPECT is %s, wanted seen or hidden\n", want);
        return 1;
    }
    printf("gw2 test avx: expecting AVX %s\n", hidden ? "hidden" : "seen");

    /* FEX's answers */
    cpuid(1, 0, r);
    unsigned c = r[2];
    answer("cpuid.1.ecx.fma", !!(c & (1u << 12)), 1, expect);
    answer("cpuid.1.ecx.xsave", !!(c & (1u << 26)), 1, expect);
    answer("cpuid.1.ecx.osxsave", !!(c & (1u << 27)), 1, expect);
    answer("cpuid.1.ecx.avx", !!(c & (1u << 28)), 1, expect);
    answer("cpuid.1.ecx.f16c", !!(c & (1u << 29)), 1, expect);
    int cpuid_avx = !!(c & (1u << 28)) && !!(c & (1u << 27));
    cpuid(7, 0, r);
    unsigned b = r[1];
    c = r[2];
    answer("cpuid.7.ebx.bmi1", !!(b & (1u << 3)), 1, expect);
    answer("cpuid.7.ebx.avx2", !!(b & (1u << 5)), 1, expect);
    answer("cpuid.7.ebx.bmi2", !!(b & (1u << 8)), 1, expect);
    /* VAES depends on the host's AES too, so seen it's printed only */
    answer("cpuid.7.ecx.vaes", !!(c & (1u << 9)), hidden, 0);
    answer("cpuid.7.ecx.vpclmulqdq", !!(c & (1u << 10)), hidden, 0);
    int cpuid_avx2 = !!(b & (1u << 5));
    cpuid(0xd, 0, r);
    answer("cpuid.d.eax.ymm", !!(r[0] & 4), 1, expect);
    /* XGETBV runs whatever OSXSAVE says: FEX decodes it either way */
    unsigned xcr0_lo, xcr0_hi;
    __asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    printf("gw2 test avx: xgetbv.0 0x%08x%08x\n", xcr0_hi, xcr0_lo);
    answer("xgetbv.0.sse", !!(xcr0_lo & 2), 1, 1);
    answer("xgetbv.0.ymm", !!(xcr0_lo & 4), 1, expect);
    int xcr0_ymm = (xcr0_lo & 6) == 6;
    answer("IsProcessorFeaturePresent.PF_XSAVE_ENABLED", !!IsProcessorFeaturePresent(PF_XSAVE), 1, expect);
    int pf_avx = !!IsProcessorFeaturePresent(PF_AVX), pf_avx2 = !!IsProcessorFeaturePresent(PF_AVX2);
    answer("IsProcessorFeaturePresent.PF_AVX_INSTRUCTIONS_AVAILABLE", pf_avx, 1, expect);
    answer("IsProcessorFeaturePresent.PF_AVX2_INSTRUCTIONS_AVAILABLE", pf_avx2, 1, expect);
    query_fn query = (query_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    cpu_information info = {0};
    LONG status = query ? query(1 /* SystemCpuInformation */, &info, sizeof(info), NULL) : -1;
    if (status)
    {
        printf("gw2 test avx: NtQuerySystemInformation(SystemCpuInformation) failed, status %#lx\n", (unsigned long)status);
        failures++;
    }
    answer("SystemCpuInformation.avx", !!(info.ProcessorFeatureBits & CPU_FEATURE_AVX), 1, expect);
    answer("SystemCpuInformation.avx2", !!(info.ProcessorFeatureBits & CPU_FEATURE_AVX2), 1, expect);

    /* Wine's answers */
    const volatile BYTE *features = (const volatile BYTE *)(SHARED_DATA + 0x274);
    DWORD64 page_xstate = *(const volatile DWORD64 *)(SHARED_DATA + 0x3d8);
    answer("KUSER_SHARED_DATA.ProcessorFeatures.PF_AVX", !!features[PF_AVX], hidden, 0);
    answer("KUSER_SHARED_DATA.ProcessorFeatures.PF_AVX2", !!features[PF_AVX2], hidden, 0);
    answer("KUSER_SHARED_DATA.XState.EnabledFeatures.ymm", !!(page_xstate & 4), hidden, 0);
    xstate_fn xstate = (xstate_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetEnabledXStateFeatures");
    DWORD64 enabled = xstate ? xstate() : 0;
    printf("gw2 test avx: GetEnabledXStateFeatures %#llx%s\n", (unsigned long long)enabled, xstate ? "" : " (no such export)");
    answer("GetEnabledXStateFeatures.ymm", !!(enabled & 4), hidden, 0);

    /* the kernel */
    unsigned seed = 118;
    for (int i = 0; i < N; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        /* 11 bits each, so no 32-bit lane of the vector paths can overflow */
        xs[i] = (short)((seed >> 16) & 0x7ff) - 1024;
        ys[i] = (short)((seed >> 3) & 0x7ff) - 1024;
        fa[i] = (float)(int)(seed >> 20) / 64.0f;
        fb[i] = (float)(int)((seed >> 8) & 0xfff) / 128.0f;
        fc[i] = (float)(int)(seed & 0xffff) / 256.0f - 100.0f;
    }
    long long dot = dot_c(), dot_sse = dot_sse2();
    madd_c();
    madd_sse();
    int sse_ok = dot_sse == dot && !memcmp(out_sse, out_c, sizeof(out_c));
    printf("gw2 test avx: sse2 path %s (dot %lld, wanted %lld)\n", sse_ok ? "correct" : "wrong", dot_sse, dot);
    if (!sse_ok)
        failures++;

    int use_avx2 = cpuid_avx && cpuid_avx2 && xcr0_ymm && pf_avx2;
    if (use_avx2)
    {
        long long dot_wide = dot_avx2();
        madd_avx();
        int avx_ok = dot_wide == dot && !memcmp(out_avx, out_c, sizeof(out_c));
        printf("gw2 test avx: avx2 path %s (dot %lld, wanted %lld)\n", avx_ok ? "correct" : "wrong", dot_wide, dot);
        if (!avx_ok)
            failures++;
    }
    else
        printf("gw2 test avx: avx2 path not taken\n");
    if (use_avx2 != expect)
    {
        printf("gw2 test avx: the avx2 path was %s, wanted %s\n", use_avx2 ? "taken" : "not taken", expect ? "taken" : "not taken");
        failures++;
    }

    if (failures)
    {
        printf("gw2 test avx: %d failures\n", failures);
        return 1;
    }
    printf("gw2 test avx: passed\n");
    return 0;
}
