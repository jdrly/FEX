// SPDX-License-Identifier: MIT
/*
 * gw2 (jdrly/gw2-on-mac#109): what FEX's stats object says about a Wine process. Built natively
 * with Apple's clang against this tree's FEX headers, so the layout is FEX's own
 * (FEXCore/include/FEXCore/Utils/SHMStats.h, held to my Wine fork's sampler by
 * Source/Windows/Common/GW2Contract.h). It never runs Wine itself.
 *
 *   fexstats <pid>...
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
 * A change to ThreadStats adds its field to FEX_STATS_FIELDS and raises STATS_VERSION in FEX;
 * GW2Contract.h and my Wine fork's heatprof.h follow.
 */
#include "Windows/Common/GW2Contract.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// <name> <ThreadStats member>, in the order the line prints them
#define FEX_STATS_FIELDS(X)                                  \
  X(jit_time, AccumulatedJITTime)                            \
  X(signal_time, AccumulatedSignalTime)                      \
  X(sigbus, AccumulatedSIGBUSCount)                          \
  X(smc, AccumulatedSMCCount)                                \
  X(float_fallback, AccumulatedFloatFallbackCount)           \
  X(cache_miss, AccumulatedCacheMissCount)                   \
  X(cache_read_lock_time, AccumulatedCacheReadLockTime)      \
  X(cache_write_lock_time, AccumulatedCacheWriteLockTime)    \
  X(jit_count, AccumulatedJITCount)                          \
  X(disk_cache_hit, AccumulatedDiskCacheHitCount)            \
  X(disk_cache_miss, AccumulatedDiskCacheMissCount)          \
  X(disk_cache_lookup_time, AccumulatedDiskCacheLookupTime)

using Header = GW2Contract::ThreadStatsHeader;
using Slot = GW2Contract::ThreadStats;

static int Print(const char* Name, const uint8_t* Base, size_t Size) {
  const auto* Head = reinterpret_cast<const Header*>(Base);
  if (!Head->fex_version[0]) {
    return 1;
  }
  printf("%s: version %u, app type %u, slot %u bytes, FEX %.*s\n", Name, Head->Version, static_cast<unsigned>(Head->app_type),
         Head->ThreadStatsSize, static_cast<int>(strnlen(Head->fex_version, sizeof(Head->fex_version))), Head->fex_version);
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
  for (int i = 1; i < argc; i++) {
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
    const int Result = Print(Name, static_cast<const uint8_t*>(Base), St.st_size);
    munmap(Base, St.st_size);
    if (Result != 1) {
      return Result;
    }
  }
  return 1;
}
