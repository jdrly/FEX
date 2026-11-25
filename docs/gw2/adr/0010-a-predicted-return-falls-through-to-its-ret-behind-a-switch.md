# A predicted return falls through to its ret, behind a switch

Decided 2026-11-26. Status: accepted, off by default until it wins in play (ADR 0002, 0008).

A guest return pops the pair its call pushed on the call-return stack, the guest return address
and the host PC, and compares the address with the one it returns to. On a match it ends in
`ret`, which the host's return predictor follows. The L1 lookup for a mismatch, eight
instructions, sat between the compare and that `ret`, so every predicted return branched over 32
bytes it never ran:

    ldp x?, x?, [callret_sp], #16; sub; cbz 1f; <L1 lookup, 8>; 1: ret

With `FEX_RETURNLOOKUPOUTOFLINE=1` the return is `ldp`, `sub`, `cbnz <lookup>`, `ret`, and the
lookup goes after the unit's code, next to the link thunks, with the same instructions in the same
order and the same `ret` at its end. A predicted return runs one taken branch less and its site is
eight instructions shorter. A missed one takes one branch more. In a unit emitted again with far
branches (`RequiresFarARM64Jumps`) the site's `cbnz` becomes `cbz` over a `b`, so the site is seven
instructions shorter and a predicted return still takes one branch, over the `b`.

The idea and the code are Squark's FEX patch `0045` (jason5545/squark-app at `2bf62f7`, MIT like
the files it changes), ported by hand because my #168 change sits in the same lines. Its author measured 3.19 to 2.58 ns a call with
4,096 small functions and 13.31 to 12.14 with 65,536, and a small loss at 16,384. Whether it helps
GW2's game thread is for play to say (jdrly/gw2-on-mac #343).

## Consequences

- Off, the JIT emits what it did: the old path is untouched and the new one is behind the option.
  The core job checks the default from CodeSizeValidation's `ret` block on the same build: unset has
  to give the same return site as `=0`, and `=1` a site eight instructions shorter. Its "Return
  site" and "Return rest" lines are the dump of both shapes.
- A block that checks TF with `FEX_TRAPFLAGINDIRECTBRANCHES=1` keeps its path through the
  dispatcher (#168): the switch doesn't touch it. A block that checks TF without that switch takes
  the new shape like any other block, as it took the inline lookup before.
- The option affects code generation, so the disk cache keeps blocks of each value apart, and
  `FormatVersion` goes from 57 to 58 in the branch's last commit, as for #164, #168 and #169.
- No CPUState field moves, so `GW2Contract.h` and my Wine sampler stay as they are. The moved
  lookup lies inside its block's code, before the block's tail, so a host PC there still finds its
  block. Each lookup gets a RIP entry with its return's guest offset, so a host PC in it maps to that
  return, as it did inline, and the code after the lookups maps to the unit's last instruction, as
  before. It reads only FEX's own state and L1, so it can't fault.
- Tests: `tests/gw2/retlookup.c` returns with the prediction holding, to swapped and pushed
  addresses (through the dispatcher and through L1), and under single steps, with the switch off
  and on and with #168's switch. The dev rerun set has the switch on. The core job's `asm-cuts`
  pass, which runs FEX's assembly tests with the dev build's code generation switches, has it too.
- Dev builds turn it on for `Gw2-64.exe` (`dev-switches.conf` in the app).

## Alternatives I rejected

- **Squark's default, on.** ADR 0002 keeps every change off until it wins in play.
- **Squark's call-return epoch compare.** It belongs to Highball's `HB_FEX_CALLRET_EPOCH`, which
  my fork doesn't have.
- **The lookup right after the block that returns, not after the unit.** It would still sit in the
  middle of hot code, where the point is to keep that code dense.

## Evidence

- jdrly/gw2-on-mac #343, and my upstream triage of 2026-11-25 (FEX, "Patches in other forks").
- Squark's `runtime/fex/highball/0045-JIT-predicted-returns-fall-through-to-ret.patch` at
  `2bf62f76d1b8425820dc51ac4eadfe2789ecb0de`.
