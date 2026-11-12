// SPDX-License-Identifier: MIT
/*
 * GW2 on Mac (jdrly/gw2-on-mac#109): what my Wine fork's sampler reads of FEX's memory.
 *
 * jdrly/wine's dlls/ntdll/unix/heatprof.h reads the game thread's FEX state at fixed offsets
 * (WINE_HEAT_PROF, jdrly/gw2-on-mac#43). Its constants are copied from this tree, and nothing
 * there can check them. Here each one is checked against FEX's own types. A change that moves
 * one fails the build with the name of the constant in jdrly/wine that has to follow, and the
 * sampler then has to learn the new FEX's build id too (it refuses one it doesn't know).
 *
 * Only static_asserts: it adds no code, so the files built with it are the same.
 * Source/Windows/ARM64EC/Module.cpp includes it, last.
 */
#pragma once

#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Utils/SHMStats.h>
#include "Interface/Core/CPUBackend.h"

#include <cstddef>

namespace GW2Contract {
using FEXCore::Core::CPUState;
using FEXCore::Core::CpuStateFrame;
using JITCodeHeader = FEXCore::CPU::CPUBackend::JITCodeHeader;
using JITCodeTail = FEXCore::CPU::CPUBackend::JITCodeTail;
using FEXCore::SHMStats::ThreadStats;
using FEXCore::SHMStats::ThreadStatsHeader;

// The thread's state: the CPU area's EmulatorData[0] points to a CpuStateFrame, whose first
// member is CPUState.
static_assert(offsetof(CpuStateFrame, State) == 0, "jdrly/wine heatprof.h reads CPUState at the frame's start");
static_assert(offsetof(CPUState, InlineJITBlockHeader) == 0, "jdrly/wine heatprof.h: HEAT_FEX_STATE_BLOCK has to follow");
static_assert(sizeof(CPUState::InlineJITBlockHeader) == 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATE_BLOCK as 8 bytes");
static_assert(offsetof(CPUState, rip) == 24, "jdrly/wine heatprof.h: HEAT_FEX_STATE_RIP has to follow");
static_assert(offsetof(CPUState, gregs) == 32, "jdrly/wine heatprof.h: HEAT_FEX_STATE_GREGS has to follow");
static_assert(sizeof(CPUState::gregs) == 16 * 8, "jdrly/wine heatprof.h reads HEAT_FEX_STATE_GREGS as 16 registers of 8 bytes");
static_assert(offsetof(CPUState, callret_sp) == 176, "jdrly/wine heatprof.h: HEAT_FEX_STATE_CALLRET has to follow");
static_assert(sizeof(CPUState) >= 256, "jdrly/wine heatprof.h: HEAT_FEX_STATE_COPY copies more than CPUState has");

// A translated block: JITCodeHeader at its start, JITCodeTail at OffsetToBlockTail from it.
static_assert(offsetof(JITCodeHeader, OffsetToBlockTail) == 0, "jdrly/wine heatprof.h: HEAT_FEX_HEADER_TAIL has to follow");
static_assert(sizeof(JITCodeHeader::OffsetToBlockTail) == 4, "jdrly/wine heatprof.h reads HEAT_FEX_HEADER_TAIL as 4 bytes");
static_assert(offsetof(JITCodeTail, Size) == 0, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_SIZE has to follow");
static_assert(offsetof(JITCodeTail, RIP) == 8, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_RIP has to follow");
static_assert(offsetof(JITCodeTail, GuestSize) == 16, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_GUEST_SIZE has to follow");
static_assert(offsetof(JITCodeTail, NumberOfRIPEntries) == 24, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_ENTRIES has to follow");
static_assert(offsetof(JITCodeTail, OffsetToRIPEntries) == 28, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_ENTRIES_AT has to follow");
static_assert(offsetof(JITCodeTail, SpinLockFutex) == 32, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_ID has to follow");
static_assert(sizeof(JITCodeTail) == 40, "jdrly/wine heatprof.h: HEAT_FEX_TAIL_COPY has to follow");

// FEX_PROFILESTATS=1: fex-<pid>-stats, a ThreadStatsHeader and then ThreadStats slots.
static_assert(FEXCore::SHMStats::STATS_VERSION == 2, "jdrly/wine heatprof.h: HEAT_FEX_STATS_VERSION has to follow");
static_assert(sizeof(ThreadStatsHeader) == 64, "jdrly/wine heatprof.h: HEAT_FEX_STATS_HEADER has to follow");
static_assert(offsetof(ThreadStatsHeader, Version) == 0, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_VERSION has to follow");
static_assert(offsetof(ThreadStatsHeader, ThreadStatsSize) == 2, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_SLOT has to follow");
static_assert(offsetof(ThreadStatsHeader, Size) == 56, "jdrly/wine heatprof.h: HEAT_FEX_STATS_H_SIZE has to follow");
static_assert(sizeof(ThreadStats) == 112, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SLOT has to follow");
static_assert(offsetof(ThreadStats, TID) == 4, "jdrly/wine heatprof.h: HEAT_FEX_STATS_TID has to follow");
static_assert(offsetof(ThreadStats, AccumulatedJITTime) == 8, "jdrly/wine heatprof.h: HEAT_FEX_STATS_JIT_TIME has to follow");
static_assert(offsetof(ThreadStats, AccumulatedSignalTime) == 16, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SIG_TIME has to follow");
static_assert(offsetof(ThreadStats, AccumulatedSIGBUSCount) == 24, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SIGBUS has to follow");
static_assert(offsetof(ThreadStats, AccumulatedSMCCount) == 32, "jdrly/wine heatprof.h: HEAT_FEX_STATS_SMC has to follow");
static_assert(offsetof(ThreadStats, AccumulatedFloatFallbackCount) == 40, "jdrly/wine heatprof.h: HEAT_FEX_STATS_FLOAT has to follow");
static_assert(offsetof(ThreadStats, AccumulatedCacheMissCount) == 48, "jdrly/wine heatprof.h: HEAT_FEX_STATS_MISS has to follow");
static_assert(offsetof(ThreadStats, AccumulatedCacheReadLockTime) == 56, "jdrly/wine heatprof.h: HEAT_FEX_STATS_RLOCK has to follow");
static_assert(offsetof(ThreadStats, AccumulatedCacheWriteLockTime) == 64, "jdrly/wine heatprof.h: HEAT_FEX_STATS_WLOCK has to follow");
static_assert(offsetof(ThreadStats, AccumulatedJITCount) == 72, "jdrly/wine heatprof.h: HEAT_FEX_STATS_JIT_COUNT has to follow");
} // namespace GW2Contract
