# An option that won in play becomes the default

Decided 2026-11-19. Status: accepted.

ADR 0002 puts every change to what FEX does behind a `FEX_*` option that is off in the fork, and my
dev builds turn the options on for `Gw2-64.exe`. That is how a change gets tested in play. It says
nothing about what happens after the test. My DXMT fork's ADR 0010 does: a switch that lost goes,
and one that won becomes the default. I apply that rule here too. An option that won in play
becomes the fork's default and keeps its off value, so a bisect can still turn it off.

The first two are `FEX_DISPATCHERL1LOOKUP` (jdrly/gw2-on-mac #161) and `FEX_ENTRYHEADERLOOKUP`
(#162), on by default since #266. They ran in every dev build from 0.2.0-dev.16 on. On dev.16, at
equal draws a frame, the pair was worth about 1 fps in every busy band: 58.0 against 57.0 fps at
3.5k to 4.5k draws, 52.1 against 51.0 at 4.5k to 5.5k, 48.2 against 47.2 above. Since dev.23
(Mach9 0.4.0) every busy band has a median of 60. Neither changes what a translated program
computes. The first has the dispatcher look in the thread's L1 before it leaves for the lookup in
C++. The second stops every block entry from storing the block's header, and the few paths that
need the block find it from the host PC.

## Consequences

- `FEX_DISPATCHERL1LOOKUP=0` gives back the engine's dispatcher, and `FEX_ENTRYHEADERLOOKUP=0` the
  header store at every entry. With both at 0 and nothing else set, the fork behaves as the
  engine's FEX again.
- A default reaches every x86_64 process, not just the game. Steam, its web helper and the
  launcher run both lookups from 0.2.0-rc.1 on. They never ran them in play before, since dev
  builds set them for `Gw2-64.exe` only. If Steam hangs again as in #264, a start with both at 0
  tells whether they matter there.
- My Wine fork's sampler attributes a sample by `CPUState::InlineJITBlockHeader`, which
  `FEX_ENTRYHEADERLOOKUP` stops storing. Leaving the option out no longer turns it off, so a sampler
  session sets `FEX_ENTRYHEADERLOOKUP=0`. The app does that, and its dev table drops the two rows
  (#267).
- The disk cache's `FormatVersion` stays 57. The cache's key holds the resolved value of
  `EntryHeaderLookup`, as it holds every boolean or number option that affects code generation
  (`SerializeForCache` in `Config.cpp`). Blocks compiled with the entry lookup on never mix with
  blocks compiled with it off, and the code for each value is what it was. The dispatcher, which
  the L1 lookup changes, is generated at start and isn't in the cache.
- The tests run the defaults with both on. A case that had an unset run and an `=1` run beside it
  now has an `=0` run there, so the off value keeps its coverage, and the compile lock's control
  runs with both at 0 as before. The compile lock's and the dev builds' combinations still name both
  at 1. Unset has to mean on: CodeSizeValidation's block entry is the same unset as at 1, and
  `l1lookup.exe`'s on case runs with nothing set.
- The other options stay as ADR 0002 has them.

## Alternatives I rejected

- **A rule for `Gw2-64.exe` in release builds too.** It would keep Steam and the launcher on the
  engine's behaviour. Both options are lookups that compute the same results, they ran in the
  game's process in every session since dev.16, and a default needs no rule in the app.
- **Deleting the off path.** Off costs a branch when FEX compiles a block or generates its
  dispatcher, nothing in translated code, and it is the way back for a bisect.

## Evidence

- jdrly/gw2-on-mac #161, #162, #264, #266, #267. jdrly/FEX PR #7 and #8.
- GW2 on Mac workspace `plans/night/2026-11-11-plan.md`: 2026-11-15 00:25 (dev.16 with and without
  the pair), 2026-11-19 01:17 and 01:37 (the dev.23 sessions), 01:46 (my go for the defaults).
- My DXMT fork's ADR 0010 (a tested switch is deleted or becomes the default) and ADR 0017 (its
  first flip to on).
- This fork's ADR 0002.
