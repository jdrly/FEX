// SPDX-License-Identifier: MIT
/*
 * gw2 (jdrly/gw2-on-mac#109): what FEX's stats object says about a Wine process. Built natively
 * with Apple's clang (-DARCHITECTURE_arm64) against this tree's FEX headers through
 * Source/Windows/Common/GW2ContractStats.h, so the layout is FEX's own
 * (FEXCore/include/FEXCore/Utils/SHMStats.h) and the one my Wine fork's sampler reads. That part of
 * the contract is on its own because FEX's other headers don't build for macOS. It never runs
 * Wine itself.
 *
 *   fexstats [--header] <pid>...
 *
 * With FEX_PROFILESTATS=1, FEX's unix half creates the shared memory object fex-<pid>-stats for
 * its process (Source/Windows/UnixLib/FEXUnixLib.cpp, InitializeSHM): a ThreadStatsHeader, then
 * one ThreadStats slot per thread, a free one with TID 0. This tries each <pid> and prints the
 * first object it can open:
 *
 *   fex-<pid>-stats: version <n>, app type <n>, slot <n> bytes, FEX <git describe>
 *   thread <tid>: jit_time=<n> signal_time=<n> … jit_count=<n> …
 *
 * one "thread" line per slot in use, each with every field of FEX_STATS_FIELDS below as
 * name=value, in that order. Times are in the counter's ticks (CNTVCT_EL0, what
 * mach_absolute_time() reads), counts are counts.
 *
 * Exit 0: it read one. 1: none of the pids has one yet, or its header isn't written yet. 3: the
 * object's version or slot size isn't this tree's, so it was made by another FEX than these
 * headers describe; the header line is printed, the slots aren't.
 *
 * With --header it prints only the header line and exits 0 whatever the layout: FEX only appends
 * to the header and the slots, so the version and fex_version stay where they are. That is how
 * the test stage asks which FEX ran, the engine's included, after my stats layout has moved on.
 *
 * A change to ThreadStats adds its field to FEX_STATS_FIELDS and raises STATS_VERSION in FEX;
 * GW2ContractStats.h and my Wine fork's heatfex.h follow.
 */
#include "Windows/Common/GW2ContractStats.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// <name> <ThreadStats member>, in the order the line prints them
#define FEX_STATS_FIELDS(X)                                 \
  X(jit_time, AccumulatedJITTime)                           \
  X(signal_time, AccumulatedSignalTime)                     \
  X(sigbus, AccumulatedSIGBUSCount)                         \
  X(smc, AccumulatedSMCCount)                               \
  X(float_fallback, AccumulatedFloatFallbackCount)          \
  X(cache_miss, AccumulatedCacheMissCount)                  \
  X(cache_read_lock_time, AccumulatedCacheReadLockTime)     \
  X(cache_write_lock_time, AccumulatedCacheWriteLockTime)   \
  X(jit_count, AccumulatedJITCount)                         \
  X(disk_cache_hit, AccumulatedDiskCacheHitCount)           \
  X(disk_cache_miss, AccumulatedDiskCacheMissCount)         \
  X(disk_cache_lookup_time, AccumulatedDiskCacheLookupTime) \
  X(tso_switches, AccumulatedHardwareTSOSwitchCount)        \
  X(native_exits, AccumulatedNativeExitCount)               \
  X(code_buffer_moves, AccumulatedCodeBufferMoveCount)      \
  X(l1_resizes, AccumulatedL1ResizeCount)

using Header = GW2Contract::ThreadStatsHeader;
using Slot = GW2Contract::ThreadStats;

static int Print(const char* Name, const uint8_t* Base, size_t Size, bool HeaderOnly) {
  const auto* Head = reinterpret_cast<const Header*>(Base);
  if (!Head->fex_version[0]) {
    return 1;
  }
  printf("%s: version %u, app type %u, slot %u bytes, FEX %.*s\n", Name, Head->Version, static_cast<unsigned>(Head->app_type),
         Head->ThreadStatsSize, static_cast<int>(strnlen(Head->fex_version, sizeof(Head->fex_version))), Head->fex_version);
  if (HeaderOnly) {
    return 0;
  }
  if (Head->Version != FEXCore::SHMStats::STATS_VERSION || Head->ThreadStatsSize != sizeof(Slot)) {
    printf("%s: not this tree's layout (version %u, slot %zu bytes), so no slots\n", Name, FEXCore::SHMStats::STATS_VERSION, sizeof(Slot));
    return 3;
  }
  // the bytes in use, as FEX keeps them, within what the object has
  size_t InUse = Head->Size.load(std::memory_order_relaxed);
  if (InUse > Size) {
    InUse = Size;
  }
  for (size_t Offset = sizeof(Header); Offset + sizeof(Slot) <= InUse; Offset += sizeof(Slot)) {
    const auto* S = reinterpret_cast<const Slot*>(Base + Offset);
    const uint32_t TID = S->TID.load(std::memory_order_relaxed);
    if (!TID) {
      continue;
    }
    printf("thread %u:", TID);
#define PRINT_FIELD(Label, Member) printf(" " #Label "=%llu", static_cast<unsigned long long>(S->Member));
    FEX_STATS_FIELDS(PRINT_FIELD)
#undef PRINT_FIELD
    printf("\n");
  }
  return 0;
}

int main(int argc, char** argv) {
  int First = 1;
  const bool HeaderOnly = argc > 1 && !strcmp(argv[1], "--header");
  if (HeaderOnly) {
    First = 2;
  }
  for (int i = First; i < argc; i++) {
    char Name[64];
    struct stat St;
    snprintf(Name, sizeof(Name), "fex-%s-stats", argv[i]);
    const int fd = shm_open(Name, O_RDONLY, 0);
    if (fd < 0) {
      continue;
    }
    if (fstat(fd, &St) || St.st_size < static_cast<off_t>(sizeof(Header))) {
      close(fd);
      continue;
    }
    void* Base = mmap(nullptr, St.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (Base == MAP_FAILED) {
      continue;
    }
    const int Result = Print(Name, static_cast<const uint8_t*>(Base), St.st_size, HeaderOnly);
    munmap(Base, St.st_size);
    if (Result != 1) {
      return Result;
    }
  }
  return 1;
}
