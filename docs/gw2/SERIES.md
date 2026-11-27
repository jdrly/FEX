# My FEX fork as a patch series

- Fork: jdrly/FEX, branch `gw2-on-mac`.
- Base: athei/FEX `597e7ccbedd`, the commit Mach9 0.4.0's engine was built from, with the
  lightweight tag `engine-0.4.0`.
- Head the topic table was written against: `3e0bd23b8c0` (#26), 2026-11-26.
- Series branch: `series/2026-11-26`.

My app's 0.2.1 ships my FEX next to the engine's and starts the game on the engine's. None of the
topics below reaches a player by default. `gw2om.sh set fex mine` brings the fork's defaults
(`block-lookups`) and the diagnostics, and dev builds turn on most of the switches that are off,
until I decide that `fex` defaults to mine in a release.

The workspace's ADR 0015 makes each of my forks a curated patch series. This file is the series of
this one: every commit in `engine-0.4.0..gw2-on-mac` with its topic, and the rules that cut those
commits into one or a few commits per topic on a `series/…` branch. That branch's tree equals
`gw2-on-mac`'s. It is the same code cut differently, and nothing on it is dropped.

63 commits are in `engine-0.4.0..3e0bd23b8c0`. 30 are what I carry on the engine's commit
(`git rev-list --no-merges engine-0.4.0..gw2-on-mac ^5ee7f94bb`). The other 33 are the 0.3.1 line,
which the move to 0.4.0 kept reachable under a merge, and the two merges of that move.

## Topics

How to read the columns:

- **Fork default** is what the code does with the variable unset, by `Config.json.in`.
- **0.2.1 with `fex=mine`** is what my app's 0.2.1 does in a release build after
  `gw2om.sh set fex mine`. *Set* means the app writes the variable into the game's compatdb rule.
  *Shipped, unset* means the code is in the files 0.2.1 installs and the fork default applies.
  *Not in use* means nothing a player runs reads it. 0.2.1 installs my FEX at `52cb7a2`, so #25 and
  #26 aren't in it.
- **App** is where my app's `arm64` branch names the variable, at `c328492`. *Release* is a line a
  release build writes. *Dev list* is `DEV_SWITCHES`, which only a development build reads. *Gone
  list* is `DEV_GONE`. The app's `FEX_SWITCHES` table, the release rows for my FEX's switches, is
  empty. Dropping a switch the dev list writes needs a `DEV_GONE` row in the app first.
- **Verdict.** A drop is a proposal, and I decide each one. Everything that never became a default
  and has no note against it is "decide", with the open case named.

| Topic | Commits | What it does | Switches (fork default) | 0.2.1 with `fex=mine` | App | Evidence | Verdict |
|---|---|---|---|---|---|---|---|
| `ci` | 9 | The `GW2` workflow: builds Mach9's four FEX files, gates them against the engine's, runs my tests and FEX's own, and decides which tests a change runs | none | not in use | no | n/a | **keep.** CI |
| `tests` | 2 | Where a hung program stands, and the stress job's rounds | none | not in use | no | n/a | **keep.** Tests |
| `docs` | 1 | `AGENTS.md`, this file and the ADRs. The backfill brought 0001 to 0005. 0006 to 0011 came with their pull requests, and the rules put them here | none | not in use | no | n/a | **keep.** Docs |
| `engine-pins` | 3 | The check against Mach9 0.4.0's engine, its five x87 listings in the core reference, and my disk cache bump. By the rules it also holds every `FormatVersion` change and every regenerated listing under `unittests/InstructionCountCI`, #25's included. `FormatVersion` is 58 at head, the base has 53 | none | shipped | no | The fork's `AGENTS.md`: a change to code generation raises the version, and the bump stays last | **keep.** Each move writes it again |
| `counters` | 3 | A stats slot for every thread, up to about 29,000 in the 4 MB object, and four new counts (#3, #6). #26 adds no code: `VERSION` names the slot fix on its `fixes:` line, `profilestats-slots` (ADR 0011) | none of its own. They ride on FEX's `FEX_PROFILESTATS` | `FEX_PROFILESTATS=1` set with Debug logging and my `ntdll.so` | Release: `FEX_PROFILESTATS`, only while my FEX is in place and its `VERSION` names `profilestats-slots` (the app's ADR 0024) | The triage's 1.60 million native exits a second and `code_buffer_moves 0` come from them. The engine's own FEX faults at its 37th thread with `FEX_PROFILESTATS=1` on macOS, and mine doesn't (ADR 0011, jdrly/gw2-on-mac#344) | **keep.** Diagnostics my Wine fork reads |
| `block-lookups` | 3 | The dispatcher looks in L1 before it leaves for C++ (#7), a block entry stores no header (#8), and both are the fork's defaults (#22) | `FEX_DISPATCHERL1LOOKUP` (on), `FEX_ENTRYHEADERLOOKUP` (on) | shipped, on. `FEX_ENTRYHEADERLOOKUP=0` set while the sampler can run | Release: `FEX_ENTRYHEADERLOOKUP=0` for the sampler. Gone list: both, with `0` as the off value | About 1 fps at equal draws in every busy band on dev.16 (ADR 0008) | **keep.** The fork's defaults, and the only FEX change with a number from play |
| `code-buffer` | 1 | The first code buffer can start at 128 MB (#4) | `FEX_INITIALCODEBUFFERMB` (16, upstream's) | shipped, unset | Dev list: `=128` | With 128, `code_buffer_moves 0` on every thread of the long dev.23 session (the triage). No session without it since | **decide.** Never a default. The open case is one line: is the count 0 at 16 too |
| `tso-syscalls` | 1 | On ARM64EC with hardware TSO switched per thread, a thread stays on hardware TSO across a guest system call. Unix calls still switch it off (#5) | `FEX_HARDWARETSOSYSCALLS` (`switch`, upstream's) | shipped, unset | Dev list: `=keep` | No measurement | **decide.** Never a default |
| `entry-compile-lock` | 2 | One compile per guest entry when threads miss on it together (#10), and where its lock sits after athei's split of `CompileBlock`. On Windows ARM64 FEX's pool allocators also read the CPU's counter for their clock in place of `QueryPerformanceCounter` | `FEX_ENTRYCOMPILELOCK` (off), gone since the drop | shipped, unset | Dev list: `=1`, gone list since jdrly/gw2-on-mac#373 | No measurement. The game thread compiles 12.8 blocks a second in the long dev.23 session (the triage), which is little | **dropped 2026-11-27** (ADR 0013). Last in `gw2-on-mac` at `3863eae77` |
| `codegen-cuts` | 1 | Shorter code for a 32-bit `lea` and for sign-extending loads that need no TSO emulation (#11). Its title says three cuts, and `Config.json.in` had these two options for it | `FEX_LEA32BITADDS` (off), `FEX_SIGNEXTENDINGLOADS` (off), both gone since the drop | shipped, unset | Dev list: both `=1`, gone list since jdrly/gw2-on-mac#373 | No measurement. It added two instruction-count listings, gone with it, and its `FormatVersion` step touched the other 108 | **dropped 2026-11-27** (ADR 0013). Last in `gw2-on-mac` at `3863eae77` |
| `avx-hide` | 1 | `FEX_HOSTFEATURES=disableavx` hides AVX from the game (#12) | FEX's own variable, with a value only my FEX honours (unset) | shipped, unset | Dev list: `=disableavx`, with the app's code to join several values | No frame number. Upstream's `834c976` renames the field its lines test (the triage) | **decide.** Never a default, and it is the FEX topic the next base is known to break |
| `singlestep-fix` | 1 | While TF is set, indirect branches go through the dispatcher, so a single step isn't lost (#13) | `FEX_TRAPFLAGINDIRECTBRANCHES` (off), gone since the drop | shipped, unset | no | Found by a test. Nobody has seen it in play, and it acts only while TF is set, by a debugger's single step or by guest code | **dropped 2026-11-27** (ADR 0013). Last in `gw2-on-mac` at `3863eae77` |
| `suspend-fix` | 1 | A thread spinning in a loop closed by a conditional backward branch can be suspended (#14). The JIT emits the check on Windows only | `FEX_CONDBACKEDGESUSPEND` (off), gone since the drop | shipped, unset | An ADR only | Two instructions more per taken conditional back-edge, for a problem nobody has seen in play. I asked why we were adding something that costs frames, and kept it out of dev builds (the app's ADR 0009, 2026-11-15) | **dropped 2026-11-26** (ADR 0012). Last in `gw2-on-mac` at `537a2281c` |
| `return-lookup` | 1 | A predicted guest return falls through to its `ret`, and the L1 lookup for a missed one moves after the unit's code (#25, from Squark's patch `0045`) | `FEX_RETURNLOOKUPOUTOFLINE` (off) | not in 0.2.1 | None yet. jdrly/gw2-on-mac#350 adds its dev list row | 48 bytes a return site off, 16 on plus 36 after the unit (ADR 0010). Squark's author measured 3.19 to 2.58 ns a call with 4,096 small functions. No number from GW2: jdrly/gw2-on-mac#343 waits for my on and off sessions | **decide.** Never a default. The open case is #343's sessions |
| `upstream-shufps` | 1 | FEX-Emu's `604f018` and its test `b5a4954`, picked as they are with their authors (jdrly/gw2-on-mac#381): a `SHUFPS` with the same register as both sources takes `PSHUFD`'s one-source path. It came after the head this table was written against | none. It is upstream's code in upstream's file, without the switch ADR 0002 asks for, as #381 decided: every shuffle gives the same result on my Mac, and a switch of mine would conflict when the base brings the commit. On a host with SVE256, which my Mac doesn't have, `vshufps xmm, xmm, xmm, 0xe4` now keeps the source's upper lane, where the old code cleared it (review of #381) | not in 0.2.1 | no | `Gw2-64.exe` has 9,789 `shufps` with one register twice. 7,255 have an immediate other than 0 and take the new path. 4,977 of those are broadcasts, three instructions before and one after, and 0xc9's 1,214 go from a two-table lookup to a one-table one (the triage of 2026-11-27). No number from play | **keep until the base has it.** A replay onto a base that has both `604f018` and `b5a4954` reports it empty, and the topic goes |
| `kept-0.3.1-history` | 31 | athei's 10 commits of the 0.3.1 base and my 21 on them, reachable under the merges | n/a | n/a | n/a | n/a | **keep as history.** Not replayed. Its content is in the 30 |
| `engine-move-merges` | 2 | `4af914c59`, with `597e7ccbedd`'s tree, and `35f3f2d0c` (#21), with `8a969bc74`'s | n/a | n/a | n/a | n/a | **keep as history.** No tree effect |

Sum: 9 + 2 + 1 + 3 + 3 + 3 + 1 + 1 + 2 + 1 + 1 + 1 + 1 + 1 + 31 + 2 = 63.

Numbers like #25 are this fork's pull requests. The triage is my workspace's
`upstream-watch-triage-2026-11-25.md`, in its `docs/research/`.

### For me to decide

- **Does `fex` default to mine in a release?** Until it does, the whole series is carried for dev
  builds and for `set fex mine`. `block-lookups` is the one topic with a result from play.
- **Open:** `code-buffer`, `tso-syscalls`, `avx-hide` and `return-lookup`. None has a number from
  play.

### Dropped

| Topic | Dropped | Last in `gw2-on-mac` at | Record |
|---|---|---|---|
| `suspend-fix` | 2026-11-26 | `537a2281c` (#27) | ADR 0012, jdrly/gw2-on-mac#361. No list of the app wrote `FEX_CONDBACKEDGESUSPEND`, and the app's ADR 0009 records why I kept it out of dev builds |
| `entry-compile-lock` | 2026-11-27 | `3863eae77` (#28) | ADR 0013, jdrly/gw2-on-mac#375. The lock can't shorten a wait, and the code buffer is less than half full with or without it. The app's `DEV_GONE` row is #373's |
| `codegen-cuts` | 2026-11-27 | `3863eae77` (#28) | ADR 0013, jdrly/gw2-on-mac#375. It touches 0.82% of GW2's instructions, and Squark's author measured no change in time. The app's `DEV_GONE` rows are #373's |
| `singlestep-fix` | 2026-11-27 | `3863eae77` (#28) | ADR 0013, jdrly/gw2-on-mac#375. Off in every build, and I found no `popf` in GW2 that sets TF. A handler that sets it in a `CONTEXT` isn't ruled out |

A drop is a commit on `gw2-on-mac` that takes the topic's code out. The topic keeps its lines in
the machine blocks, and before the series is next built the drop commit gets one too, under the
topic. The topic's series commit then holds what the drop left: for `suspend-fix`, the include
of `Interface/Core/JIT/DebugData.h` in `BranchOps.cpp`, which `return-lookup` uses.

The drop of 2026-11-27 is one commit for three topics, and `series-topics` gives a commit one. Its
paths outside the rules are `entry-compile-lock`'s (`Core.cpp`, `ThreadPoolAllocator.h`),
`codegen-cuts`' (`MemoryOps.cpp`, `OpcodeDispatcher.cpp`, `OpcodeDispatcher.h`, `IR.json`,
`unittests/ASM`) and `singlestep-fix`'s (`BranchOps.cpp`, `JIT.cpp`, `JITClass.h`). Its
`BranchOps.cpp` hunk also changes the line of `return-lookup` that tested `ViaDispatcher`, so its
slice applies only after `return-lookup`'s commit. Whichever of the three its line names, the
other two keep their code in their own series commits and lose it in that one. The three leave
nothing behind once dropped. On 2026-11-27 I built the series with a stand-in for the squash
commit, once under `singlestep-fix` and once under `entry-compile-lock`, with `3863eae77` under
`suspend-fix`: both gave 16 commits for 15 topics and the head's tree. Either line works.

### What a drop costs

Each line is the series replayed onto `597e7ccbedd` with one topic's commits left out, on
2026-11-26:

| Topic left out | Result |
|---|---|
| `suspend-fix`, `singlestep-fix`, `codegen-cuts`, `avx-hide`, `entry-compile-lock`, `tso-syscalls`, `code-buffer` | clean, each alone |
| `return-lookup` | clean. The core job's return check stays in `ci` and fails: it reads the `Return site` line that `CodeSizeValidation` prints, and that line comes with the topic |
| `block-lookups` | one conflict hunk, in `Source/Tools/CodeSizeValidation/Main.cpp`, in `return-lookup`. The core job's `Block entry` check stays in `ci` |
| `counters` | one conflict hunk, in `FEXCore/include/FEXCore/Utils/SHMStats.h`, in `block-lookups`. The `fixes: profilestats-slots` line stays in `ci`, so the app would turn on `FEX_PROFILESTATS` for a FEX without the fix: that line goes with the topic. The `slots` cases stay in `tests` |

A clean drop of a topic with an option of its own still leaves the declaration in `options`,
which reads a variable nothing acts on. `avx-hide` declares none, since `FEX_HOSTFEATURES` is FEX's,
but `ci` still names `disableavx` among my switches and `tests` still expects AVX hidden. A topic that changes code generation leaves stale listings in `engine-pins`. Both go
in a small commit after the drop, and the listings are regenerated as at every bump. Whether the
rest builds without the topic is the first run's answer after the drop.

## The series branch

The commits that aren't merges and aren't in a skipped topic are cut by the rules below: a commit's
files under a rule's paths go to that rule's topic, whatever the commit's own topic, so the CI,
test and doc lines of a feature commit land in `ci`, `tests` and `docs`. Then the slices are
picked onto the base topic by topic, in the order below (greedy): per round and topic, a topic
takes its slices until one doesn't apply yet, and the rest wait for the next round. Here one round
places them all, so each topic is one commit.

| # | Series commit | Commits it takes slices from |
|---|---|---|
| 1 | `ci` | 20 |
| 2 | `options` | 10 |
| 3 | `counters` | 2 |
| 4 | `docs` | 12 |
| 5 | `tests` | 21 |
| 6 | `code-buffer` | 1 |
| 7 | `tso-syscalls` | 1 |
| 8 | `block-lookups` | 3 |
| 9 | `entry-compile-lock` | 2 |
| 10 | `codegen-cuts` | 1 |
| 11 | `avx-hide` | 1 |
| 12 | `singlestep-fix` | 1 |
| 13 | `suspend-fix` | 1 |
| 14 | `return-lookup` | 1 |
| 15 | `engine-pins` | 5 |

That is 15 topics: 14 of the table's, and `options`, which holds `Config.json.in` and `Context.h`.
Every switch is declared there one under the other, so without it leaving one switch topic out
conflicts in every later one. It also holds two lines of `Context.h` that aren't declarations
(`SyncCurrentBlock` and the entry locks' array). `engine-pins` is last, as `AGENTS.md` asks of the
disk cache bump, with every `FormatVersion` change and listing of the series in it. `counters`
takes slices from 2 commits: #26's lines are all under `.github` and `docs`. Each series commit's
message names its topic in `Series-Topic:` and lists the `gw2-on-mac` commits it is made of.

Onto athei's `macos-patched` at `cfcbb86469d`, the tip of 2026-11-25, all 15 apply clean.

## Machine blocks

My workspace's replay script reads these blocks by their fence label. Paths are globs where `*`
matches `/` too, and the first rule that matches wins.

```series-topics
2f1eb20b65e0b663ce9c833d1fca2b493c329509 kept-0.3.1-history
c6f7845f344fbf48757682ac2b5714b85910a439 kept-0.3.1-history
55d309b97d91ab2a9890f791cfc772a5799d47f4 kept-0.3.1-history
3cf2f6951c59e11bfed93dd550c2ed5b47076048 kept-0.3.1-history
a1bfd4fad41b94562640b22c97fb21a6f9ab6c80 kept-0.3.1-history
c3bbf11bc816b445a2543ffb9f51b76a90dbe2c6 kept-0.3.1-history
e41915d5a085611941e40083b8d7e1e1c564adf5 kept-0.3.1-history
00794d3e07e3e41ea0d1b5abaa98c55cdc344558 kept-0.3.1-history
f05f8d5b133f1b0d58848fa82ba6f338274bb951 kept-0.3.1-history
9d4fa4b46b1d8d834462a540539f37528883eec4 kept-0.3.1-history
bff3f7c3686b376a03d6c5700ecaf947008ca2e8 kept-0.3.1-history
3fcc436a6df3272aca7f1cdf43653d6aa9edbb44 kept-0.3.1-history
0bea04486a30797de23f95e6e9002b135a771206 kept-0.3.1-history
d06061da09f1136d3119613b0acd7157067c46ff kept-0.3.1-history
35a3c305f01ef0f622ef22a5c387e2a0ad0608ac kept-0.3.1-history
b9e412fdafc6d87a9221588d28dcb05fc361e60b kept-0.3.1-history
c2e25accebcbdc41b9be06f8b8f87df00b3b737f kept-0.3.1-history
9fde333b9012df6b37f99378fed09f992eb9ea5e kept-0.3.1-history
fc6952e3c5d07212667524751a5356879c81f7c8 kept-0.3.1-history
f1fa75f8fa1f8c61cf0ca9b84bf9629758355ba4 kept-0.3.1-history
b183e58ab648b39d01d8748d5ea9f5084c0727f1 kept-0.3.1-history
b14e778e1a9d8efc83d3322c7ae4e1bcadfee4d3 kept-0.3.1-history
f6b619e72b9f6fc6560307d130888336a962fc85 kept-0.3.1-history
02b23da05229ac68595d84b3c0bbd4918999fe14 kept-0.3.1-history
da1f65621a17b42ebcd72053a5df3a3616f11770 kept-0.3.1-history
8986239c391bf91da36743faade6a3cf03a874c5 kept-0.3.1-history
b077bcdbd341774d2513683a8af7c532ae960a0d kept-0.3.1-history
f81e02ef06e2864319cfc40a021fadd154b5d607 kept-0.3.1-history
d7020b77e16cc88da509562b571d54d637297578 kept-0.3.1-history
a6e9a772128c7eea3f8c5ac70f56c3554dffe8b4 kept-0.3.1-history
5ee7f94bb3a4f483dca5df47ffa1b7389a9c758d kept-0.3.1-history
4af914c59f0964c04375c26d84f9c2249ce3045e engine-move-merges
e6798172a2317cf9a2ca6f50d438218bc2b27553 ci
c7daf3b91bddfbcf99e25fc0adc2fdfb06470382 ci
b9472890fab9d7e99ce90c1367c3ebc950c347ef counters
ead36f2f3e375acb97dad59f26a072b6d3fe8324 code-buffer
0f5ed73f212835bb7acf42968749bb7aa5d6d393 tso-syscalls
a62f9541e509ebd33e0d2e4c97250c00c375bd17 counters
4bbce871af5f6b7eb1e44260ebe663fe3591fc25 tests
06a94ddc22ee5fcd18c4d40205b321e30d4f519f block-lookups
6cf9467fffe8379ce94bdf01cafb1839e5e3d8eb block-lookups
d6d07750270d23605ddbdd19e97a27a2abae9b6e tests
44967b1eb59de10c15d532f43f719d8d4e7ec0c9 entry-compile-lock
10dc2c98600f18149092641858451c1f41a347c8 codegen-cuts
07403843f028029279fbeedcd64e41f9fbedca16 avx-hide
1e6c2d51e55011c027c5699a0b156faa1e9b2231 singlestep-fix
591aa244bbffb9b10b5831fdf28617558cc3bc13 suspend-fix
411cf2c59bd55e6969d89619114971e02e32a86b ci
97d0dab86a876eb2fd0f6aa9978b25bf918791cb ci
ff25667bcf89166bfdcd4d0bc5bc6305bd9e6bc4 ci
9886b58f5407362f54c49cbc677275832d106816 ci
5426a51cde20ccea3bd7bcaf868adca0cc5e5cc5 ci
40b4e34eac7f0658e38543d66455c5cb4d1c8fed docs
2fd6673ddefe6be798e2d58770c8514c65b37ccf entry-compile-lock
2dcc85eab384ef897797d15e55d4e37222495924 engine-pins
d119be7878c0b186e5ffc18169e9b32fdb159945 engine-pins
8a969bc741516acd88e7a14b29abb93cc61a8fd1 engine-pins
35f3f2d0cb093bcbc54674d4d49fe5bcfa90a485 engine-move-merges
52cb7a24b38e001f1b76faf44c7495f2cb28d7aa block-lookups
9b0d320f50ea20bb43c30cd2daed76b7a89f2b41 ci
dffbd2aa56cb48637b003935761382db8eb17821 ci
a4aa2cd5a595033b47796dd65351fc1b3ff899da return-lookup
3e0bd23b8c01ee2783c282931363b2f2d12cc093 counters
3863eae771282ae33add637166d4ee9a81a1ed5f suspend-fix
9085ab554ece21175466940e7e9c199469ab9b7d singlestep-fix
```

```series-rules
.github/* ci
tests/gw2/* tests
docs/gw2/* docs
AGENTS.md docs
CLAUDE.md docs
FEXCore/Source/Interface/Core/DiskCache.h engine-pins
unittests/InstructionCountCI/* engine-pins
FEXCore/Source/Interface/Config/Config.json.in options
FEXCore/Source/Interface/Context/Context.h options
```

```series-order
mode greedy
ci
options
counters
docs
tests
upstream-shufps
code-buffer
tso-syscalls
block-lookups
entry-compile-lock
codegen-cuts
avx-hide
singlestep-fix
suspend-fix
return-lookup
engine-pins
```

```series-skip
kept-0.3.1-history
engine-move-merges
```

```series-titles
ci CI: The GW2 workflow builds Mach9's four FEX files and gates them against the engine's
options Options: Every FEX_* switch of mine, declared in Config.json.in and Context.h
counters FEX's counters: A stats slot for every thread, and four new counts
docs Docs: AGENTS.md, SERIES.md and the ADRs
tests Tests: The programs and the cases under tests/gw2
code-buffer Code buffer: The first code buffer can start at 128 MB, behind a switch
tso-syscalls TSO: A thread stays on hardware TSO across guest system calls, behind a switch
block-lookups Block lookups: The dispatcher looks in L1 first and a block entry stores no header, both on by default
entry-compile-lock Compiles: One compile per guest entry when threads miss on it together, behind a switch
codegen-cuts Code generation: Shorter code for a 32-bit lea and for sign-extending loads, behind switches
avx-hide AVX: A rule can hide it from the game with FEX_HOSTFEATURES=disableavx
singlestep-fix Single-step: Indirect branches go through the dispatcher while TF is set, behind a switch
suspend-fix Suspend: A thread spinning in a loop closed by a conditional backward branch can be suspended, behind a switch
return-lookup Returns: A predicted return falls through to its ret, and the L1 lookup moves after the unit, behind a switch
upstream-shufps SHUFPS: Upstream's one-source SHUFPS takes PSHUFD's path (FEX-Emu 604f018 and b5a4954)
engine-pins Engine pins: The check against Mach9 0.4.0's engine, my disk cache version and the regenerated listings
```

## Rules

- A commit needs no line in `series-topics` when the rules place every path it changes. That
  covers the commit that adds this file, and later commits that only touch CI, tests or docs.
- Any other commit gets its line when the series is next rebuilt, and the build refuses one
  without it.
- The series branch is rebuilt under a new date, never rewritten.
- `gw2-on-mac` is never rewritten.
