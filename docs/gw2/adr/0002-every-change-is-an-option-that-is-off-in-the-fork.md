# Every change to what FEX does is an option that is off in the fork, and dev builds turn it on

Decided 2026-11-12, narrowed 2026-11-15 and 2026-11-19 (ADR 0008). Status: accepted.

My FEX replaces the engine's for every x86_64 process, Steam and the launcher included. A change to
what FEX does therefore sits behind a `FEX_*` option that is off by default. It is an entry in
`FEXCore/Source/Interface/Config/Config.json.in`, so a rule in my app can set it for one program.
Until an option wins in play, it stays off. Then it becomes the default (ADR 0008). My dev builds turn the options on for
`Gw2-64.exe` from `dev-switches.conf`, because I debug from everything on backwards. A release build
keeps the fork's defaults.

## Consequences

- With the rule on `Gw2-64.exe`, Steam keeps the engine's behaviour for every option that is still
  off. The 128 MB code buffer
  (`FEX_INITIALCODEBUFFERMB`) is committed only where the variable is set, so Steam keeps 16 MB.
- A build's `VERSION` names the options the file has, and the app leaves out a line the installed
  FEX doesn't know.
- A change to code generation raises the disk cache's `FormatVersion` in its own bump commit, which
  stays last in the branch.
- One option stays out of dev builds since 2026-11-15: `FEX_CONDBACKEDGESUSPEND` (jdrly/gw2-on-mac
  #169). It adds two instructions to every taken conditional back-edge, for a hang nobody has seen
  in the game. I asked why we were adding something that costs frames. A fix that adds work on the
  hot path waits for a symptom seen in play. The rule is in the app's ADR on dev builds.
- `FEX_ENTRYHEADERLOOKUP` is never on together with my Wine fork's sampler. The sampler attributes a
  sample by the block header that this option stops storing. Since it is on by default (ADR 0008), a
  sampler session sets it to 0.
- Played results so far: `FEX_DISPATCHERL1LOOKUP` and `FEX_ENTRYHEADERLOOKUP` together are worth
  about 1 fps at equal draws in every busy band and stay on. The code generation cuts, the compile
  lock and hidden AVX together added about 3 to 4 fps on dev.17. `FEX_HOSTFEATURES=enableafp` ran
  about 3 fps worse in the busy bands and is not set.
- `FEX_DISPATCHERL1LOOKUP` and `FEX_ENTRYHEADERLOOKUP` became the defaults on 2026-11-19, under my
  DXMT fork's rule that a tested option either goes or becomes the default (ADR 0008).

## Alternatives I rejected

- **Changes on by default in the fork.** The record has the rule and what it allows, a rule per
  program. It doesn't argue the opposite.
- **Leaving new options off in dev builds until each had a session.** I debug from the end back to
  the start.

## Evidence

- This fork's `AGENTS.md` ("Issues, commits and pull requests").
- jdrly/gw2-on-mac #129 (dev builds), #112, #113, #161, #162, #164, #165, #118, #163, #169, #170, #343.
- jdrly/FEX PR #4, #5, #7, #8, #10, #11, #12, #14.
- GW2 on Mac workspace `plans/night/2026-11-11-plan.md`: 2026-11-12 21:10 (Steam keeps 16 MB) and
  22:10 (everything on), 2026-11-15 00:25 (the L1 pair), 22:10 and 22:35 (dev.17, AFP), 23:50 (no
  frame-costing fix without a symptom); `AGENTS.md` ("dev.16's FEX switches").
- The dispatcher's note `dev-builds-everything-on`.
