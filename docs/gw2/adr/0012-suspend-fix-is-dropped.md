# `suspend-fix` is dropped

Decided 2026-11-26. Status: accepted.

`suspend-fix` was one commit, `591aa244b` (#14, jdrly/gw2-on-mac#169). With
`FEX_CONDBACKEDGESUSPEND=1`, the JIT on Windows checked the suspend doorbell on the taken side of a
conditional branch back to an earlier guest instruction. Without it, a thread spinning in a loop
closed by such a branch can't be suspended, and Wine's `GetThreadContext` waits for good. A test
found that, in run 43452320908, under the engine's FEX and under mine. Nobody has seen it in the
game.

The check costs two instructions on every taken conditional back-edge. A short branch becomes four
instructions of code where there was one, a far branch six where there were two. On 2026-11-15 I asked why we were adding something that costs frames, and kept
it out of dev builds (the app's ADR 0009). The app never set the variable: no release row, no line
in `DEV_SWITCHES` or `DEV_GONE`. Only my test stage turned it on, in condloop's cases and in the
`dev` set that reruns the other programs. My
workspace's ADR 0015 makes each fork a patch series that every Mach9 move replays, so a topic
nothing runs still costs a replay at each move. Its extension of 2026-11-26 drops this one.

## What goes

- The check in `CondJump` (`FEXCore/Source/Interface/Core/JIT/BranchOps.cpp`). The function is
  again what it was before `591aa244b`, line for line. With the switch unset, the old code took the
  same branch through a helper, so the JIT emits the same instructions as before the drop. The
  helper sat outside the Windows-only block, so on Linux every conditional jump went through it.
  The core job's instruction count listings, conditional jumps back in loops and string operations
  among them, have to come out unchanged with the restored function. The Windows path with the
  switch unset was that same helper call.
- The option: its entry in `Config.json.in` and its line in `Context.h`. VERSION's `switches:` line
  comes from `Config.json.in`, so it stops naming the switch with no edit of its own.
- `tests/gw2/condloop.c` and its four cases in `tests/gw2/cases.zsh`: off under both FEXes and on
  under mine (#169), and one in each rerun set. The `dev` set no longer sets the variable.
- The workflow's comment on why the core job never ran with it.

What stays: the include of `Interface/Core/JIT/DebugData.h` that `591aa244b` added to
`BranchOps.cpp`. #25's return lookup uses `DebugData` there now.

## Consequences

- `FormatVersion` goes from 58 to 59 in the branch's last commit, and the `BinaryCacheVersion` of
  the 110 listings under `unittests/InstructionCountCI` follows. Their instructions don't change.
  The cache alone wouldn't need it: it hashes every option marked `AffectsCodeGen` into its bucket
  (`DiskCache.cpp`, `SerializeForCache`), so with one option fewer no cache written before the drop
  matches. But a build with the switch set emits other code now, and the fork's rule raises the
  version for a change to code generation.
- My app's 0.2.1 installs my FEX at `52cb7a2`, which still has the code. It doesn't write the
  variable, so nothing changes for a player.
- The suspend hang stays as upstream has it, in both FEXes. If a thread of the game hangs in a
  suspend, this is the first place to look, and `591aa244b` with its test is where to start again.
- In `docs/gw2/SERIES.md` the topic stays in the table, marked dropped, with this date and its
  commit. No rule places `BranchOps.cpp`, so before the next series build the drop's squash commit
  gets a line `<sha> suspend-fix`. The build refuses without it. With it, the topic's series commit
  keeps only the include.

## Alternatives I rejected

- **Keeping it, off.** It costs nothing in play, but every move replays it and every base change near
  `CondJump` can conflict with it. It is also an option that a rule could set by mistake.
- **Moving it to `series-skip`.** The replay script skips a skipped topic's commits whole, with their
  `options`, `tests` and `engine-pins` slices, and the series' tree would then stop being the head's.
  The code has to go from `gw2-on-mac` itself.

## Evidence

- jdrly/gw2-on-mac#169 and #361, jdrly/FEX PR #14.
- The app's ADR 0009, `a-dev-build-starts-with-every-switch-on`, narrowed 2026-11-15.
- My workspace's ADR 0015, `my-forks-are-curated-patch-series-on-mach9-releases`, extended
  2026-11-26.
