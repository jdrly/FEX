# Arithmetic flags are dead across a call and a return in the main executable, behind a switch

Decided 2026-11-28. Status: accepted, off by default until it wins in play (ADR 0002, 0008).

FEX's dead-flag pass (`DeadFlagCalculationEliminination::ProcessBlock`) starts a block's backward
walk with every flag live unless the block ends in a jump inside the unit. A block that ends in a
call or a return keeps CF, PF, AF, ZF, SF and OF alive for whoever runs next. The Windows x64 ABI
leaves all six undefined across both, so compiled code never reads them there. In #175's sample
(the hot-code study of 2026-11-27, lever 2) 1.43% of the game thread's samples sit on host
instructions that exist only for that: 0.92% at returns and 0.50% at calls, 606 of the 1,619 on
`cfinv`. FEX keeps the carry inverted after `add` or `test` and flips it back at a block's end
for whoever reads CF next. At a `ret` nobody does. Every MSVC epilogue (`add rsp, N; pop; ret`)
pays three instructions for this: the carry's flip, the AF copy and the result copy that
`adds` needs.

With `FEX_ABIFLAGS` at `ret`, `call` or `both` (default `off`), a `ret`, a direct `call` or an
indirect `call` inside the registered range marks all six flags written right before its exit.
The dead-flag pass then drops every flag computation in the unit that only that exit would have
read. DF is left alone: the ABI requires it clear at both, and FEX keeps it apart.

## Where it acts

Only inside one range of guest code, which the frontend registers once before anything compiles
(`Context::SetABIFlagsRange`):

| Frontend | Range |
|---|---|
| ARM64EC (`Source/Windows/ARM64EC/Module.cpp`) | the main executable's `.text`, by section name. FEX logs it: `FEX: ABIFlags 3 in the main executable's .text 140001000-...` |
| WOW64, FEXLoader | none, so the switch does nothing there |
| `TestHarnessRunner` | the test's code, so FEX's assembly tests run with it |
| `CodeSizeValidation` | its code region below 4 GB, so a listing can show it |

DLLs, Wine's x86 code, addons and any code the game generates at run time keep every flag. A call
from a DLL into the executable keeps its flags at the call. The return back to the DLL is the
executable's `ret` and loses them, which the ABI allows.

## The functions that pass a flag (the list the issue asked for first)

For `Gw2-64.exe` of 2026-11-24 (PE time stamp 1795558714) the list is empty. The script is
`work/fex-hot/390/abiflags.py` in my workspace, which git ignores. It holds no game code. Run with
`--gaps --all`, it:

- decodes `.text` by recursive descent from every `.pdata` entry, every direct call or jump
  target, the image's entry point, every 64-bit relocation and every `lea [rip + …]` that points
  into `.text`, and through 3,672 switch tables: MSVC's tables of RVAs, and the tables of offsets
  that hand-written routines like the CRT's `memcpy` jump through. Then it decodes every byte left
  that isn't int3 or nop padding or a table it resolved, as code, until nothing is left. That is
  6,318,797 instructions, 553,100 call sites and 87,527 function entries, and it leaves 9,880 bytes
  of the 23,419,304 in `.pdata` ranges and 1,169 of the 2,170,177 outside them undecoded;
- from the instruction after every call follows every path until each of the six flags is
  written: a read on the way means the caller takes a flag from the callee, or through it from
  before the call;
- from every function entry does the same: a read means the callee takes a flag from its caller.

Its flag model is capstone 5.0.7's, with two fixes. capstone gives `test` with a memory operand and
a register, and four AVX compares, no flag information. Those count as writing every flag, so 700
`test`s and 31 compares don't hide a read. A shift or rotate by CL counts as no write, since a count
of 0 writes nothing. Every instruction that reads a flag whatever its operands (`jcc`, `setcc`,
`cmovcc`, `adc`, `sbb`, `pushf`, `lahf`, `rcl`, `rcr`, `cmc` and the others) counts as a reader
even where capstone gives no read. None had to be. A copy of the executable with two planted
cases, a caller whose `test eax, eax` after a call became a `nop` and an entry whose first
instruction became `sete al`, gives exactly those two hits more.

On the real file it finds 7 call sites and 6 entries, all in bytes that aren't code: 3 calls in
tables of RVAs after a `ret`, decoded as instructions, 4 calls to addresses outside the image, 5
entries at the first word of a table of offsets, and 1 in the middle of a `jmp`. The first run without the `test` fix found 81 call sites and 16 entries, every one of
them a `test [mem], reg` before the read.

The same script on the 19 programs of my test stage (`tests/gw2/*.c` and Mach9's smoke test, built
with llvm-mingw as the runner builds them) finds what they do on purpose and nothing else:
`abiflags.exe`'s two callers that read CF and ZF from routines outside `.text`, and the `pushfq`
with which `retlookup.exe` and `singlestep.exe` set TF at an entry, which puts the arithmetic flags
back unread.

So no function in GW2's own code passes a flag across a call or a return, and the exclusion the
issue asked for has no entry. I didn't build a mechanism for an empty list. An RVA list would also
be wrong after the next game build.

What the search doesn't see:

- a `ret` used as a jump (`push target; ret`) whose target reads a flag;
- an indirect jump whose targets aren't in a table it reads, when the sweep decodes their code
  out of step;
- a routine in a DLL or an addon that the executable calls and that reads the flags it arrives
  with: the executable's call drops them;
- a later game build. The script runs again in about four minutes, and has to before the switch
  becomes a default.

## What it does on GW2's code

The harness of the hot-code study (the 3,109 units of #175's busy span, compiled as the game's FEX
compiles them, with the session's settings: `FEX_ENTRYHEADERLOOKUP=0`, `FEX_EXTENDEDDEBUGINFO=1`):

| `FEX_ABIFLAGS` | Host instructions |
|---|---|
| unset, `off` | 1,088,837, the code of the commit before, unit for unit |
| `ret` | 1,079,931 |
| `call` | 1,070,579 |
| `both` | 1,061,487 (27,350 fewer, 2.5%) |

Each of the three is the scratch patch's code, instruction for instruction, from the study, which
set the pass's starting set to empty at those exits.

The units hold 1,481 epilogues `add rsp, N; pop; ret`, and each loses three instructions up to the
return the call-return stack predicts: 1,470 go from 8 to 5 and 11 from 9 to 6, as at `0x14023fbe0`:

```
unset                              both
mov x24, x23                       ldr x26, [x23], #8
mov x23, x9                        ldr x6, [x23], #8
ldr x26, [x23], #8                 ldp x10, x11, [x17], #16
cfinv                              sub x10, x10, x6
ldr x6, [x23], #8                  cbnz x10, #+0x8
ldp x10, x11, [x17], #16           ret x11
sub x10, x10, x6
cbnz x10, #+0x8
ret x11
```

`FEXOpt/ABIFlags.json` and `ABIFlags_Off.json` list the same five sequences with `both` and unset.
The epilogue, as the listing prints it (up to the return's lookup):

```
unset                              both
adds x26, x8, #0x28 (40)           add x8, x8, #0x28 (40)
mov x27, x8                        ldp x6, x20, [x8], #16
mov x8, x26
ldp x6, x20, [x8], #16
cfinv
```

A compare right before a `ret` goes entirely, and `sub` before a call becomes a plain `sub`.

## Where it must run after the flush

`ExitFunction` in the opcode dispatcher writes back the carry's polarity and the PF and AF
registers (`FlushRegisterCache`) before it emits the exit. The marker, an `InvalidateFlags` of the
six flags, goes after that flush, so the pass drops those writes too. A marker before it would
leave the `cfinv` and both copies in place. `InvalidateFlags` emits no host code.

## Consequences

- Off, the JIT emits what it did, and the harness's units are the same code unit for unit.
- A fault or a suspend inside a callee, or right after a return, in the executable's code reports
  arithmetic flags in its `CONTEXT` that x86 wouldn't have. The ABI makes them undefined there, and
  nothing in GW2 that I know of reads them from a context.
- The option affects code generation, so the disk cache keeps blocks of each value apart, and
  `FormatVersion` goes up by one in the branch's last commit.
- No CPUState field moves, so `GW2Contract.h` and my Wine sampler stay as they are.
- Tests: `tests/gw2/abiflags.c` passes CF and ZF across 2,000 returns and 1,000 calls from routines
  in a section of their own, `.abiflag`, which have to keep them. It runs under both FEXes off,
  under mine at `both`, where its case also checks that FEX's logged range is the program's
  `.text`, and in both rerun sets. The dev set has the switch at `both`. The core job's `asm-cuts`
  pass runs FEX's assembly tests with it. Of the 2,564, 31 have a call or a return, and the two
  `InlineSMC_DF` tests are the only ones of those that read a flag: they read DF with `pushf`,
  which this doesn't touch.
- Dev builds turn it on for `Gw2-64.exe` once the app's `dev-switches.conf` has
  `FEX_ABIFLAGS=both`.

## Alternatives I rejected

- **An exclusion list of RVAs**, set per game build. It is empty for this build and would go stale
  with every update.
- **Only functions with `.pdata`.** MSVC's leaf functions have none, so that would keep their
  returns' flags, and the search found nothing such a rule would protect.
- **The dead-flag pass reading the exit's hint**, as the study's scratch patch did. The pass has
  no context, no guest RIP for the exit and no range, and the marker gives the same code.
- **The whole process.** DLLs and Wine's x86 code weren't checked.
- **Squark's `0037`**, which does this for listed modules. I didn't read it and took nothing from
  it. My survey of 2026-11-27 left it out for the risk in hand-written assembly. The search above
  is my answer to that risk for GW2's executable.
- **On by default.** ADR 0002 keeps every change off until it wins in play.

## Evidence

- jdrly/gw2-on-mac #390, #175, and my workspace's `advisor-plans/2026-11-27-fex-hot-code/STUDY.md`
  (lever 2, the scratch patch's numbers) and `docs/research/fex-forks-survey-2026-11-27.md`.
- `work/fex-hot/390/` in my workspace: the script, its output for the executable
  (`abiflags-final.json`, `final.log`) and the harness's output for each value.
