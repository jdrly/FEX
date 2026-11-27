# A guest address above 4 GB is one ldr of a literal, behind a switch

Decided 2026-11-28. Status: accepted, off by default until it wins in play (ADR 0002, 0008).

GW2's image starts at `0x140000000`, above 4 GB. Every return address a call pushes and every
`[rip + …]` address is a guest address in it, and `InsertGuestRIPMove` built each with three
moves, `mov; movk; movk`, four where code caching pads them. In #175's session of 2026-11-27,
1.48% of the game thread's samples sat on those lines (my FEX hot-code study, lever 3).

With `FEX_GUESTADDRESSLITERALS=1`:

- A guest address above 4 GB is `ldr xN, <literal>`. The literal is one 8-byte word after the
  unit's code, after the link thunks and the exit linker's address, one per address and unit. It
  carries the `RELOC_GUEST_RIP_LITERAL` relocation that the link thunks' guest addresses already
  use, so cached code relocates the word and not the instruction.
- An instruction that reads and writes the same `[rip + …]`, such as `add [rip + …], reg`, builds
  the address once. The dispatcher keeps the address the instruction built and gives it to the
  store. It forgets it before the next instruction and when the instruction moves to another IR
  block, as `rcr` by `cl` does, so each block builds its own.
- A unit emitted again with far branches (`RequiresFarARM64Jumps`) keeps the moves, since its end
  can be out of `ldr`'s 1 MB reach. A literal out of reach restarts the unit that way.

On the 3,109 units of #175's busy span, compiled with the study's harness:

| | Off | On |
|---|---|---|
| Guest addresses built | 41,262, in three moves or two | 41,170, each one `ldr` |
| A direct call that pushes onto the call-return stack (17,907) | 7 instructions | 5 |
| A `[rip + …]` load that uses the address right away (2,850) | 4 instructions | 2 |
| Words of code, thunks and literals included | 1,034,449 | 1,023,388 |
| Bytes of code and tails | 4,663,564 | 4,618,444 |

The 92 addresses fewer are read-modify-writes that build theirs once. The call and the load are
the two shapes I counted, not every call and load. Off, the harness's output is the same as the
build before the change, unit for unit. The words fall because each site loses two instructions
and the literals, two words apiece, are fewer than the sites: a unit loads the same address from
one literal.

Three moves are the cheapest instructions there are, and an `ldr` from the code's own lines puts
them in the data cache too, so play may give back less than the line share. Whether it helps is
for play to say (jdrly/gw2-on-mac #391).

## Consequences

- Off, the JIT emits what it did: both changes are behind the option. The core job checks it on
  CodeSizeValidation's three probes, compiled at `0x740000000`: unset has to give the same code
  as `=0`, and `=1` a load and a call's push two instructions shorter and `add [rip + …], rax`
  five shorter.
- Below 4 GB the literals change nothing, so a 32-bit program and FEX's own tests, whose code
  sits at `0x10000`, keep the moves. The address reuse applies to any `[rip + …]`, also below
  4 GB, so the core job's `asm-cuts` pass, which has the switch on, runs it.
- A displacement the disk cache marks patchable (`PatchableGuestRIP`) keeps its padded moves and
  isn't reused: its value is rebuilt from the guest's bytes when cached code loads.
- The option affects code generation, so the disk cache keeps blocks of each value apart, and
  `FormatVersion` goes from 61 to 62 in the branch's last commit.
- No CPUState field moves, so `GW2Contract.h` and my Wine sampler stay as they are. The literals lie
  inside the unit's code, before its tail, where a host PC never points, so the RIP entries need
  nothing new.
- The ARM64EC exit to native code (`BranchOps.cpp`, `EC_CALL_CHECKER_PC_REG`) takes the same
  path. Only the runner's test stage compiles it: CodeSizeValidation and the study's harness are
  Linux builds.
- Tests: `tests/gw2/ripaddr.c`, whose image sits at `0x140000000`, runs 26 instructions that write
  memory on `[rip + …]` and through a register, which have to agree, and calls directly and through
  `[rip + …]`, off and on. The dev rerun set has the switch on.
- Dev builds turn it on for `Gw2-64.exe` once the app has its row (`dev-switches.conf`).

## Alternatives I rejected

- **A literal for every guest address.** One that fits in a `movz` would trade an instruction for a
  load. Above 4 GB every address takes at least two moves.
- **Reusing an address across instructions in a block.** It would keep a register alive across the
  block for an address an `ldr` gives in one instruction.
- **`adrp; add` from the code to the guest address.** The code buffer and the guest image aren't
  within 4 GB of each other, and the JIT copies a unit after compiling it, so the distance isn't
  known while it emits.

## Evidence

- jdrly/gw2-on-mac #391, and my FEX hot-code study of 2026-11-27 (`STUDY.md`, lever 3, in the
  workspace's `advisor-plans/2026-11-27-fex-hot-code/`).
- The study's harness, CodeSizeValidation of this fork with a local patch, on the entries of
  `rips-real.txt`, with `FEX_RETURNLOOKUPOUTOFLINE=1` and `FEX_ENTRYHEADERLOOKUP=1`.
