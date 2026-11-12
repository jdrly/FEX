// SPDX-License-Identifier: MIT
/*
 * GW2 on Mac (jdrly/gw2-on-mac#109): the part of GW2Contract.h about FEX's stats object, on its
 * own because it needs nothing of FEX but FEXCore/Utils/SHMStats.h. So the native reader of my
 * test stage (tests/gw2/native/fexstats.cpp), built for macOS, where FEX's other headers don't
 * build, takes its layout from here too. GW2Contract.h includes it.
 *
 * Only static_asserts and names: it adds no code.
 */
#pragma once

#include <FEXCore/Utils/SHMStats.h>

#include <cstddef>

namespace GW2Contract {
using FEXCore::SHMStats::ThreadStats;
using FEXCore::SHMStats::ThreadStatsHeader;

// FEX_PROFILESTATS=1: fex-<pid>-stats, a ThreadStatsHeader and then ThreadStats slots.
static_assert(FEXCore::SHMStats::STATS_VERSION == 2, "jdrly/wine heatprof.h: HEAT_FEX_STATS_VERSION has to follow");
static_assert(sizeof(ThreadStatsHeader) == 64, "jdrly/wine heatprof.h: HEAT_FEX_STATS_HEADER has to follow");
static_assert(offsetof(ThreadStatsHeader, Version) == 0, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_VERSION has to follow");
static_assert(sizeof(ThreadStatsHeader::Version) == 1, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_H_VERSION as 1 bytes");
static_assert(offsetof(ThreadStatsHeader, ThreadStatsSize) == 2, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_SLOT has to follow");
static_assert(sizeof(ThreadStatsHeader::ThreadStatsSize) == 2, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_H_SLOT as 2 bytes");
static_assert(offsetof(ThreadStatsHeader, Size) == 56, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_SIZE has to follow");
static_assert(sizeof(ThreadStatsHeader::Size) == 4, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_H_SIZE as 4 bytes");
static_assert(sizeof(ThreadStats) == 112, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SLOT has to follow");
static_assert(offsetof(ThreadStats, TID) == 4, "jdrly/wine heatprof.h: HEAT_FEX_STATS_TID has to follow");
static_assert(sizeof(ThreadStats::TID) == 4, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_TID as 4 bytes");
static_assert(offsetof(ThreadStats, AccumulatedJITTime) == 8, "jdrly/wine heatprof.h: HEAT_FEX_STATS_JIT_TIME has to follow");
static_assert(sizeof(ThreadStats::AccumulatedJITTime) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_JIT_TIME as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedSignalTime) == 16, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SIG_TIME has to follow");
static_assert(sizeof(ThreadStats::AccumulatedSignalTime) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_SIG_TIME as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedSIGBUSCount) == 24, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SIGBUS has to follow");
static_assert(sizeof(ThreadStats::AccumulatedSIGBUSCount) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_SIGBUS as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedSMCCount) == 32, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SMC has to follow");
static_assert(sizeof(ThreadStats::AccumulatedSMCCount) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_SMC as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedFloatFallbackCount) == 40, "jdrly/wine heatprof.h: HEAT_FEX_STATS_FLOAT has to follow");
static_assert(sizeof(ThreadStats::AccumulatedFloatFallbackCount) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_FLOAT as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedCacheMissCount) == 48, "jdrly/wine heatprof.h: HEAT_FEX_STATS_MISS has to follow");
static_assert(sizeof(ThreadStats::AccumulatedCacheMissCount) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_MISS as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedCacheReadLockTime) == 56, "jdrly/wine heatprof.h: HEAT_FEX_STATS_RLOCK has to follow");
static_assert(sizeof(ThreadStats::AccumulatedCacheReadLockTime) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_RLOCK as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedCacheWriteLockTime) == 64, "jdrly/wine heatprof.h: HEAT_FEX_STATS_WLOCK has to follow");
static_assert(sizeof(ThreadStats::AccumulatedCacheWriteLockTime) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_WLOCK as 8 bytes");
static_assert(offsetof(ThreadStats, AccumulatedJITCount) == 72, "jdrly/wine heatprof.h: HEAT_FEX_STATS_JIT_COUNT has to follow");
static_assert(sizeof(ThreadStats::AccumulatedJITCount) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATS_JIT_COUNT as 8 bytes");
} // namespace GW2Contract
