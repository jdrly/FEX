# A block a call returns to skips its suspend check, behind a switch

Decided 2026-11-28. Status: accepted, off by default until it wins in play (ADR 0002, 0008).

On Windows the JIT polls the suspend doorbell at every entry point of a unit: `ldr w11,
[x28, SuspendDoorbell]`, `cbz`, `brk #0xcafe` (`EmitSuspendInterruptCheck`, `JIT.cpp`). The
frontend makes the address after every call an entry point of the unit the call is in, so a call
and its return poll twice, once at the callee's entry and once on the way back. In #175's sample
(the hot-code study of 2026-11-27, lever 1) 3.15% of the game thread's samples sit on those
instructions, 0.74% at entries that aren't a unit's first.

With `FEX_SKIPRETURNSITESUSPENDCHECK=1` an entry point that a call emitted earlier in the same unit
names as its return block emits no poll. The return there costs two instructions less (`ldr`,
`cbz`), and the block three words less. The rest of the entry stays: the spill slots, the trap
flag check and, with `FEX_ENTRYHEADERLOOKUP=0`, the header store. An entry no call named yet keeps
its poll, so an unusual block order errs towards polling. The gdbserver store in the same function
stays too. Squark's FEX patch `0035` and MetalSharp's `0011` make the same cut. I wrote mine on my
tree from their idea. Both are MIT, as FEX is.

A Linux build has no doorbell, so there the switch changes nothing, and the core job doesn't run
with it.

## Why the thread still polls

Between a call and its return the thread passes at least one poll, whichever way the call leaves
the unit:

| The call | Where it goes | The poll |
|---|---|---|
| Direct, to x86 code | the linked branch or the exit linker, to the callee's entry point | the callee's entry |
| Direct, to ARM64EC code | `ExitFunctionEC` | `ExitFunctionEC` checks the doorbell (`Module.S`). The call names no return block, so the return site keeps its own poll as well |
| Indirect | L1 to the callee's entry point, or the dispatcher to an entry point or to `ExitFunctionEC` | the entry, or `ExitFunctionEC` |

So a thread in a loop that calls something still polls once an iteration, at the callee. A loop
that makes no call and closes with a conditional branch back polls nowhere, with this switch or
without it (ADR 0012).

## What it doesn't cover

A return site's entry is also the entry the lookup cache hands out for that guest address, so
anything that reaches the address other than through its call's return lands there with no poll.
Three cases follow from that, and each needs a loop that polls nowhere else:

- **A function that starts right after a call**, with no padding between them. A call to it from
  elsewhere can land on the return site's entry. A leaf function like that, called in a loop closed
  by a conditional branch, would then poll nowhere.
- **An indirect jump to an address after a call**, a `jmp reg` or a jump table. A loop with no call
  in it that closes with such a jump enters the entry through L1 each time and polls nowhere. Before
  the switch it polled there.
- **A suspend that comes after the callee's entry polled.** If the caller then goes on into a loop
  with no call in it, closed by a conditional branch, the poll at the return was the last one before
  the loop. Such a loop already holds a suspend that arrives while it runs (ADR 0012). The switch
  makes that window longer by what the callee does after its entry.

For the first case I looked in `Gw2-64.exe` (the 2026-11-24 build):

- none of its 35,605 direct call targets is an address a call returns to, from a decode of every
  `.pdata` range;
- 69 `.pdata` ranges start where the range before them ends in a call. All of them are chained
  unwind fragments in the middle of a function, at unaligned addresses, and no 8-byte pointer to
  any of them is in the file.

So no direct call in GW2's own code lands on a return site. The targets of its indirect calls and
jumps aren't known without running it, and the addons and other x86 DLLs I didn't check. Nothing in
GW2 that I know of suspends its game thread in play.

## What it does on GW2's code

The harness of the hot-code study (the 3,109 units of #175's busy span, compiled as the game's FEX
compiles them, with the Windows suspend check forced into a Linux build) with the switch at 0 and 1:

| Suspend checks | Off | On |
|---|---|---|
| At a unit's first entry | 3,109 | 3,109 |
| At another entry point | 24,095 | 0 |
| Before a backward jump | 704 | 704 |

Every removed check sat at an address a call ends at. Each unit is otherwise the same line for
line, apart from branch offsets and alignment words. The units hold 1,034,449 words of code off and
962,135 on: 72,285 for the checks and 29 alignment words.

`tests/gw2/callloop.c`'s loop with a direct call, from the same harness, off and on:

```
  adr x10, #+0xc                  adr x10, #+0xc
  stp x6, x10, [x17, #-16]!       stp x6, x10, [x17, #-16]!
  bl <exit linker thunk>          bl <exit linker thunk>
  ldr w11, [x28, #2752]           ldr w24, [x1]
  cbz w11, #+0x8                  subs w9, w24, #0x0
  brk #0xcafe                     b.eq <the call>
  ldr w24, [x1]
  subs w9, w24, #0x0
  b.eq <the call>
```

The unit goes from 44 words to 40, its indirect twin from 41 to 38.

## Consequences

- Off, the JIT emits what it did: the poll is only skipped when the switch is on.
- The option affects code generation, so the disk cache keeps blocks of each value apart, and
  `FormatVersion` goes up by one in the branch's last commit.
- No CPUState field moves, so `GW2Contract.h` and my Wine sampler stay as they are.
- Tests: `tests/gw2/callloop.c` suspends a thread 1,000 times in a loop with a direct call and 1,000
  times in one with an indirect call, each closed by `je` back. Off, under both FEXes, the suspends
  land at the leaf's entry and after the call, some at each. On, every one lands at the leaf's
  entry, with rax equal to the counter in memory, and the loop counts on after each resume. The dev
  rerun set has the switch on, so `entryfault` (a fault after a call returns) and the others run
  with it.
- Dev builds turn it on for `Gw2-64.exe` once the app's `dev-switches.conf` has the line.

## Alternatives I rejected

- **A checked entry for arrivals from outside.** The predicted return would land past the poll and
  every other arrival on a stub after the unit's code that polls and jumps in. That closes the first
  two cases above, at the cost of a stub and a RIP entry per return site, and not the third. GW2's
  direct calls have no such case, so not now. If the game or an addon ever hangs in `SuspendThread`
  with this switch on, that is where to start.
- **No poll at any entry.** 2.41% of the samples sit at units' first entries. Dropping those needs
  another way to stop a thread at a consistent x86 state, which is a design and not a patch.
- **On by default.** ADR 0002 keeps every change off until it wins in play.

## Evidence

- jdrly/gw2-on-mac #389, #175, and my workspace's `advisor-plans/2026-11-27-fex-hot-code/STUDY.md`
  (lever 1) and `docs/research/fex-forks-survey-2026-11-27.md` (row 4).
- Squark's `runtime/fex/highball/0035-JIT-no-suspend-check-at-call-return-sites.patch` at
  jason5545/squark-app `5f45428107c345b707c3e85047b83e07890d372b`. Squark measured 1.1% fewer host
  instructions and no change in time on an M5 Max with TSO.
- MetalSharp's `patches/fex/0011-FEXCore-Skip-the-suspend-check-at-call-return-target.patch` at
  metalsharp/MetalSharp-Wine `bc1e0eb538ec5d0fcd1c0e477c06908f636e51b3`. MetalSharp measured 13%
  less time a call on an M4 without TSO.
