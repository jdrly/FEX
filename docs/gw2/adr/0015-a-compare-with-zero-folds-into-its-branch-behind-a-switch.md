# A compare with zero folds into its branch, behind a switch

Decided 2026-11-28. Status: accepted, off by default until it wins in play (ADR 0002, 0008).

`FoldBranch` in FEX's dead-flag pass turns a compare with zero and a `jz` or `jnz` into one `cbz`
or `cbnz`, and a float compare's `axflag` and the branch after it into one branch on the float
condition, when the flags are dead on both exits. It looks at the IR operation right before the
jump and skips only register and PF/AF stores. `Core.cpp` emits a `GuestOpcode` marker before
every `Jcc`, because a `Jcc` sets RIP, and `test` invalidates AF. Either one stops the scan, so on
GW2's code the fold never happened: #175's units hold no folded compare.

With `FEX_FOLDBRANCHPASTMARKERS=1` the scan also skips `GuestOpcode` and `InvalidateFlags`.
Neither emits a flag-setting instruction or changes a register. The fold itself is upstream's,
with one mapping out: upstream's table sent x86's `jle` after a float compare to arm64's LE. After
`ucomiss` SF and OF are clear, so `jle` is taken on equal or unordered, while LE after `fcmp` is
also taken on less than. That `jle` keeps its `axflag` now. The marker had kept the table
unreachable, which is how the wrong entry went unnoticed (review of #395). The idea is aurora-silicon/fex's open pull request 40 (commit `edd71e9`, MIT, by
AxxzyWasTaken), ported by hand. Its folds of single-bit and sign tests to `tbz` and `tbnz` stay
out: they hold 0.04% of #175's sample and most of its lines.

On the 3,109 units of #175's busy span, compiled with the hot-code study's harness and the
session's settings (`FEX_ENTRYHEADERLOOKUP=0`, `FEX_RETURNLOOKUPOUTOFLINE=1`):

| | Off | On |
|---|---|---|
| Words of code, thunks and literals included | 1,088,837 | 1,080,840 |
| `cmp` | 15,822 | 9,895 |
| `cbz` and `cbnz` | 40,803 | 46,730 |
| `axflag` | 4,700 | 2,647 |

5,927 compares and 2,053 `axflag`s go, and 17 words in the units' tails. Off, the harness's
output is the session's, unit for unit. Folding removes an instruction and not the load the
compare waits for, so play may give back less than the instruction count suggests. aurora's
author measured minus 6.0% time on his own branch-heavy loop on an M4 Max. Whether it helps GW2
is for play to say (jdrly/gw2-on-mac #395).

## Consequences

- Off, the scan stops where it did, and the instruction count listings keep their code. A new
  check runs in both: a compare whose first source is an inlined zero, as after
  `xor eax, eax; cmp eax, 0`, doesn't fold, since `CondJump` needs a register there and would trip
  its assertion. aurora's patch checks the same.
- The flags at a fault after a folded branch aren't x86's. The fold needs NZCV dead on both exits,
  and FEX's dead-flag pass doesn't count a load that can fault as a reader of the flags. ARM64EC
  builds the context's EFlags from the host's NZCV, so after an integer fold they are the ones from
  before the compare, and after a float fold they are `fcmp`'s arm64 flags without the `axflag`
  that turned them into x86's. PF and AF are already stale there with the switch off. A
  handler that reads EFlags, or moves RIP to code that reads them, sees those flags, as after
  any compare FEX's pass removes. #395 asked for the flags x86 has there, which no fold can give,
  so its test checks RIPs and branch directions and prints the flags.
- RIP stays exact. A folded compare emits no code, so with `FEX_EXTENDEDDEBUGINFO=1` its RIP
  entry and the jump's sit at one host address. `RestoreRIPFromHostPC` takes the later one there,
  the jump's, and the `cbz` is the jump. My Wine sampler's `heat_prof_resolve` walks the entries
  the same way, and the zero host delta has the two-byte encoding both decode. With the default
  settings only side-effecting compares get an entry. On the session's units with extended debug
  info, 9,452 pairs of entries already shared a host address off, and 13,800 do on.
- The option affects code generation, so the disk cache keeps blocks of each value apart, and
  `FormatVersion` goes up by one in the branch's last commit.
- No CPUState field moves, so `GW2Contract.h` stays as it is.
- Tests: `tests/gw2/foldbranch.c` folds `test` and `cmp` with zero at 32 and 64 bits, and
  `ucomiss` with `ja`, `jae`, `jb` and `jbe` on ordered and unordered pairs, and runs `jle`, which
  mustn't fold. It takes each exit, with rcx's upper half set under a 32-bit compare, and faults on
  the first instruction of each: the exception has to name that instruction. Off, on, on with
  `FEX_EXTENDEDDEBUGINFO=1`, where the tied RIP entries exist, and in both rerun sets. The
  listings `FEXOpt/FoldBranch.json` and `FoldBranch_Off.json` show the same five jumps with the
  switch on and unset. The core job's `asm-cuts` pass runs
  FEX's assembly tests with it.
- Dev builds turn it on for `Gw2-64.exe` once the app has its row (`dev-switches.conf`).

## Alternatives I rejected

- **aurora's whole patch.** The `tbz` folds add most of the lines for 0.04% of the sample, and
  would hide the effect of the skip itself.
- **Dropping the marker before a `Jcc`.** The marker is the jump's RIP entry, which a fault in a
  block's last instructions and my sampler need.
- **Exact flags at a fault.** It would mean counting every load as a reader of the flags, which
  undoes FEX's dead-flag pass everywhere.

## Evidence

- jdrly/gw2-on-mac #395, and my FEX forks survey of 2026-11-27 (row 1, "Row 1: why a compare with
  zero doesn't fold today"), in the workspace's `docs/research/`.
- aurora-silicon/fex pull request 40, commit `edd71e9ec793ffcb0542dffd40981b424a79e6b8`.
- The study's harness, CodeSizeValidation of this fork with a local patch, on the entries of
  `rips-real.txt` (`advisor-plans/2026-11-27-fex-hot-code/` in the workspace).
