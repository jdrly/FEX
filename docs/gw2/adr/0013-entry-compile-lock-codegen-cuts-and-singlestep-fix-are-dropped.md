# `entry-compile-lock`, `codegen-cuts` and `singlestep-fix` are dropped

Decided 2026-11-27. Status: accepted.

On 2026-11-26 I went through every topic of my series against what GW2 does (my workspace's
`advisor-plans/2026-11-26-topic-analysis/ANALYSIS-fex.md`), and on 2026-11-27 I dropped three of
them (`DECISIONS.md` there, jdrly/gw2-on-mac#375). None has a number from play, and for none of
them could the game's code or my logs show an effect.

- **`entry-compile-lock`**, `44967b1eb` (#10) and `2fd6673dd`, jdrly/gw2-on-mac#165.
  `FEX_ENTRYCOMPILELOCK=1` made threads that miss on the same guest entry compile it once, and
  made the pool allocators read the CPU's counter where they made a clock system call. The lock
  can't shorten a wait: a second thread waits for the first one's compile, 16 to 20 µs, about as
  long as compiling its own copy. It only saves the copy, and code space isn't short. The buffer
  held 43.6 to 57.8 MB of 128 in the dev sessions with the lock and 39.8 to 55.8 MB in those
  without. Those sessions differ in more than the lock, so that is a bound, not an A/B. The game
  thread compiles 12.8 blocks a second over a long session, so the clock half saves about 50
  reads a second.
- **`codegen-cuts`**, `10dc2c986` (#11), jdrly/gw2-on-mac#164. `FEX_LEA32BITADDS` and
  `FEX_SIGNEXTENDINGLOADS` saved one instruction at a 32-bit `lea` and at a `movsx` or `movsxd`
  from memory. GW2 has 47,820 such sites, 0.82% of the instructions in its functions. Squark's
  author measured 1.1 and 2% fewer host instructions and no change in time, on an M5 Max with TSO
  on, my configuration. A gain under 1% is below the 1.3 to 3.6 fps that two evenings on one build
  differ by, so no evening of mine could show it.
- **`singlestep-fix`**, `1e6c2d51e` (#13), jdrly/gw2-on-mac#168. `FEX_TRAPFLAGINDIRECTBRANCHES=1`
  sent indirect branches through the dispatcher while TF is set, so a single step isn't lost. It
  was off in every build, dev builds too. I found no `popf` in GW2 that sets TF. The 11 `pushf`
  and `popf` pairs within six instructions of each other all sit in tables of addresses whose
  bytes decode that way. That doesn't rule out a handler setting TF in a `CONTEXT`, or an addon
  that single-steps, and arcdps and ReShade weren't checked.

Each Mach9 move replays every topic (my workspace's ADR 0015), and a base change near their lines
in `Core.cpp`, `OpcodeDispatcher.cpp` or `BranchOps.cpp` conflicts with them. A topic I can't
measure still costs a replay at each move.

## What goes

- `entry-compile-lock`: the lock in `TryCompileBlock` and the clock's setting in `ContextImpl`'s
  constructor (`Core.cpp`), `CounterClock` in `ThreadPoolAllocator.h`, the 1,024 mutexes in
  `Context.h`, `tests/gw2/compilelock.c` and its three cases, and the stress job's fourth FEX.
  With 12 combinations its budget for 5 rounds goes back to 2,010 s, the step to 37 minutes and the
  job to 42.
- `codegen-cuts`: `LoadMemSExt` in `IR.json` and `MemoryOps.cpp`, the paths in `LEAOp`,
  `MOVSXDOp` and `MOVSXOp` (`OpcodeDispatcher.cpp`) with their helper in `OpcodeDispatcher.h`,
  `unittests/ASM/Lea32BitAdds.asm` and `SignExtendingLoads.asm`, and their two listings under
  `unittests/InstructionCountCI/FEXOpt`. `asm-cuts` in the core job stops setting the two
  switches.
- `singlestep-fix`: `ViaDispatcher` in `ExitFunction` (`BranchOps.cpp`) and `BlockChecksTF` in
  `JITClass.h` and `JIT.cpp`, and the cases that set the switch: `singlestep-trapflag`,
  `retlookup-trapflag`, `retlookup-on-trapflag` and `singlestep-trapflag-engine-like`. The `dev`
  rerun set no longer sets it, nor the lock and the cuts.
  `return-lookup` tested `!ViaDispatcher` in its condition, and that test goes with it.
- The four options in `Config.json.in` and `Context.h`, and the sentence about the trap flag in
  `ReturnLookupOutOfLine`'s description. VERSION's `switches:` line comes from `Config.json.in`,
  so it stops naming them with no edit of its own.

The five files only these topics changed (`ThreadPoolAllocator.h`, `MemoryOps.cpp`,
`OpcodeDispatcher.cpp`, `OpcodeDispatcher.h`, `IR.json`) are again the engine's, byte for byte.
In `BranchOps.cpp` my fork's diff to the engine is `return-lookup`'s block and the include it
uses.

## What stays

- `singlestep.exe` and its lines about `step_after` and stray steps. `singlestep` runs under both
  FEXes and wants only its `passed` line, as before. The line that says every round trapped at
  `step_after` is no case's anymore, so a log that has it would show upstream fixed the bug.
  `singlestep-dev` and the new `singlestep-engine-like` want `passed` too.
- `retlookup.exe`'s step part. With no switch left that sends a TF block through the dispatcher,
  its step line is printed and no case requires it, as for the cases without the switch before.
  `retlookup-dev` wants `passed`, as `retlookup-on` does.
- The core job's `asm-cuts` pass, with `FEX_RETURNLOOKUPOUTOFLINE=1` alone. I kept its name: ADR
  0009 and the reference comparison use it.

## Consequences

- With every switch unset the JIT emits what it emitted before the drop. Each removed path was
  behind its switch. `ExitFunction`'s inline path is the engine's code again, which is what it
  emitted with `ViaDispatcher` false.
- `FormatVersion` goes from 59 to 60 in the branch's last commit, and the `BinaryCacheVersion` of
  the 108 listings left under `unittests/InstructionCountCI` follows. Their instructions don't
  change. The analysis wanted these drops to share `suspend-fix`'s step, but that one merged first
  (#28, 59), so this is one step for all three.
- A return in a block that checks TF takes `FEX_RETURNLOOKUPOUTOFLINE`'s shape like any other
  return, as it did whenever the trap flag switch was off.
- My app writes `FEX_ENTRYCOMPILELOCK`, `FEX_LEA32BITADDS` and `FEX_SIGNEXTENDINGLOADS` in dev
  builds. Its `DEV_GONE` rows for them (jdrly/gw2-on-mac#373) merge before this. No list of the
  app named `FEX_TRAPFLAGINDIRECTBRANCHES`. My app's 0.2.1 installs my FEX at `52cb7a2`, which
  still has the code, and a release build writes none of the four.
- The single step bug stays as upstream has it, in both FEXes. If a debugger or an addon loses
  steps in the game, `1e6c2d51e` with its test is where to start again. The same goes for
  `44967b1eb` if the code buffer ever fills, and `10dc2c986` if upstream takes the cuts.
- In `docs/gw2/SERIES.md` the three stay in the table, marked dropped, with this date and the
  commit they were last in.

## Alternatives I rejected

- **Measuring first.** For the cuts nothing in my play can show under 1%. For the lock the most an
  evening could show is a few MB of a buffer that is less than half full. For the trap flag there is
  nothing to measure until something single-steps the game.
- **Keeping them, off.** It costs nothing in play, but each move replays them, and each is an
  option that a rule could set by mistake.
- **Three pull requests.** The listings change once, and `return-lookup`'s line and the
  `asm-cuts` pass are shared between them.

## Evidence

- jdrly/gw2-on-mac#164, #165, #168 and #375, jdrly/FEX PRs #10, #11 and #13.
- My workspace's `advisor-plans/2026-11-26-topic-analysis/ANALYSIS-fex.md` (its sections on the
  three topics and "What the drops take") and `DECISIONS.md`.
- My workspace's ADR 0015, `my-forks-are-curated-patch-series-on-mach9-releases`.
